/*
 * Copyright (C) 2012-2015 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hhh_task.h"
#include "odflow_list.h"
#include "odflow_hash.h"
#include "../agurim_odflow.h"
#include "../agurim_param.h"

/*
 * the HHH overlapping algorithm for odflow fields.
 */
/*
 * prefix combination table, label[prefixlen1][prefixlen2],
 * for walking through a HHH diamond mesh from bottom to top (level), and
 * from left to right (position).
 * note: must be inversely ordered by the sum of the prefix lengths
 */
static int ipv4_labels[25][2] = {
  {32,32},{32,24},{24,32},{32,16},{16,32},{24,24}, 
  {32,8},{8,32},{24,16},{16,24},{32,0},{0,32},{24,8},{8,24},{16,16},
  {24,0},{0,24},{16,8},{8,16},{16,0},{0,16},{8,8},{8,0},{0,8},{0,0}
};

/* IPv6 heuristics: use only 39/81 combinations */
static int ipv6_labels[39][2] = {
  {128,128},{128,112},{112,128},/*{128,96},{96,128},*/{112,112},
  /*{128,80},{80,128},{112,96},{96,112},*/
  {128,64},{64,128},/*{112,80},{80,112},{96,96},*/
  {128,48},{48,128},{112,64},{64,112},/*{96,80},{80,96},*/
  {128,32},{32,128},/*{112,48},{48,112},{96,64},{64,96},{80,80},*/
  {128,16},{16,128},/*{112,32},{32,112},{96,48},{48,96},{80,64},{64,80},*/
  {128,0},{0,128},/*{112,16},{16,112},{96,32},{32,96},{80,48},{48,80},*/{64,64},
  /*{112,0},{0,112},{96,16},{16,96},{80,32},{32,80},{64,48},{48,64},*/
  /*{96,0},{0,96},{80,16},{16,80},*/{64,32},{32,64},{48,48},
  /*{80,0},{0,80},*/{64,16},{16,64},{48,32},{32,48},
  {64,0},{0,64},{48,16},{16,48},{32,32},
  {48,0},{0,48},{32,16},{16,32},{32,0},{0,32},{16,16},{16,0},{0,16},{0,0}
};

static int proto_labels[5][2] = {
  {24,24},{24,8},{8,24},{8,8},{0,0}
};
static void
order_list(struct odflow_hash *phash, struct odflow_list *plist);
static int hhh_comp(const void *p0, const void *p1);

static int is_label(struct odflow_spec *pspec, int *label);
static int
get_child_bitsize(struct hhh_task *ptask);
static void
set_child_label(struct hhh_task *ptask, struct hhh_task *pctask, struct odflow_spec *pspec);

static int
binsearch(struct odflow **plist, int *label, int start, int end);

/* 
 * NOTE: this API never release hash space 
 *       in order to make the data processing time short
 */
struct hhh_task *
task_alloc(uint8_t alloc_flg)
{
	struct hhh_task *ptask;

	ptask = malloc(sizeof(struct hhh_task));
	if (ptask == NULL)
		goto end;

	memset(ptask, 0,  sizeof(struct hhh_task));

	if (((alloc_flg & TASK_FLG_LABEL)) != 0) {
		ptask->label = malloc(sizeof(int) * 2);
		if (ptask->label == NULL)
			goto err;
	}
	if (((alloc_flg & TASK_FLG_LIST)) != 0) {
		/* TODO */
	}
end:
	return ptask;
err:
	task_free(ptask);
	goto end;
}

/* 
 * NOTE: this API never release hash space 
 *       in order to make the data processing time short
 */
void
task_free(struct hhh_task *ptask)
{
	if (ptask->orig_flow != NULL) {
		free(ptask->label);
		//list_free(ptask->list);
		//odflow_free(ptask->orig_flow);
	}
#if 0
	if (((free_flg & TASK_FLG_LABEL)) != 0) {
		if (ptask->label != NULL)
			free(ptask->label);
	}

	if (((free_flg & TASK_FLG_LIST)) != 0) {
		if (ptask->list != NULL)
			list_free(ptask->list);
	}

	if (ptask->orig_flow != NULL) {
		odflow_free(ptask->orig_flow);
	}
#endif
	free(ptask);
}

struct odflow_list *
taskq_create(struct task_tailq *ptaskq, int af)
{
	struct odflow_list *plist;
	struct odflow_hash *phash;
	struct hhh_task *ptask;
	int (*plabels)[2];
	uint32_t i, len;
	uint32_t bytesize;
	uint64_t nflow;

	if (af == AF_INET) {
		phash = ip_hash;
		nflow = ip_hash->nrecord;
		plabels = ipv4_labels;
		len = sizeof(ipv4_labels)/sizeof(int)/2;
		bytesize = 8;
	} else if (af == AF_INET6) {
		phash = ip6_hash;
		nflow = ip6_hash->nrecord;
		plabels = ipv6_labels;
		len = sizeof(ipv6_labels)/sizeof(int)/2;
		bytesize = 16;
	} else {
		phash = proto_hash;
		nflow = proto_hash->nrecord;
		plabels = proto_labels;
		len = sizeof(proto_labels)/sizeof(int)/2;
		bytesize = 3;
	}

	/* No task needs to append, thus, return immediately */
	if (nflow == 0)
		return NULL;

	plist = list_alloc(nflow);
	order_list(phash, plist);
	for (i = 0; i < len; i++) {
		ptask = task_alloc(TASK_FLG_NONE);

		ptask->label    = &plabels[i][0];
		ptask->bytesize = bytesize;
		ptask->bitsize  = 0;

		ptask->hash = phash;

		ptask->list = plist;
		ptask->end  = (uint64_t)binsearch(ptask->list->list, plabels[i], 0, nflow);

		ptask->orig_flow = NULL;

		ptask->done = 0;
		ptask->taskq_head = ptaskq;
		
		TAILQ_INSERT_TAIL(&ptaskq->task_head, ptask, task_chain);
		ptaskq->ntask++;
	}
	return plist;
}

void
add_child_task(struct hhh_task *ptask, struct odflow *pflow)
{
	struct hhh_task *pctask;
	uint32_t pctask_bitsize;

	if (ptask->bitsize == 0){
		/* clone the aggregated flow */
		pctask = task_alloc(TASK_FLG_LABEL);
		pctask->bitsize = 0;
		pctask->bytesize = ptask->bytesize;
		pctask->label[0] = ptask->label[0];
		pctask->label[1] = ptask->label[1];
		pctask->taskq_head = ptask->taskq_head;

		/* FIXME the way to set parameter configuration in clone task is tricky */
		pctask->done = 1;
		pctask->orig_flow = pflow;

		TAILQ_INSERT_HEAD(&ptask->taskq_head->task_head, pctask, task_chain);
		ptask->taskq_head->ntask++; 
	}

	pctask_bitsize = get_child_bitsize(ptask);
	if ((pflow->cache->size > 1) && (pctask_bitsize > 0)) {
		pctask = task_alloc(TASK_FLG_LABEL);
		pctask->orig_flow = pflow;

		pctask->hash = ptask->hash;
		pctask->list = pflow->cache;
		pctask->end = pflow->cache->size;

		pctask->taskq_head = ptask->taskq_head;

		pctask->bitsize = pctask_bitsize;
		set_child_label(ptask, pctask, &pflow->spec);

		pctask->bytesize = ptask->bytesize;

		TAILQ_INSERT_HEAD(&ptask->taskq_head->task_head, pctask, task_chain);
		ptask->taskq_head->ntask++; 
	}
}

static void
order_list(struct odflow_hash *phash, struct odflow_list *plist)
{
	struct odf_tailq *ptailq;
	struct odflow *pflow;
	uint64_t i;

	for (i = 0; i < NBUCKETS; i++) {
		if (phash->nrecord == 0)
			break;
		ptailq = &phash->tbl[i];
		phash->nrecord -= ptailq->nrecord;
		while ((pflow = TAILQ_FIRST(&ptailq->odfq_head)) != NULL) {
			TAILQ_REMOVE(&ptailq->odfq_head, pflow, odf_chain);
			ptailq->nrecord--;
			plist->list[plist->size] = pflow;
			pflow->list_index = plist->size++;
		}
	}
        qsort(plist->list, plist->size, sizeof(struct odflow *), hhh_comp);
#if 0
	for (i = 0; i < plist->size; i++) {
		pflow = plist->list[i];
		printf("[%ld] ", i);
		odflow_print(pflow);
		printf("\n");
		//printf(" %d %d ", pflow->spec.srclen, pflow->spec.dstlen);
	}
#endif
}

/* helper for qsort: compare the sum of prefix length */
static int
hhh_comp(const void *p0, const void *p1)
{
	struct odflow *e0, *e1;
	uint16_t len0, len1;

	e0 = *(struct odflow **)p0;
	e1 = *(struct odflow **)p1;

	len0 = e0->spec.srclen + e0->spec.dstlen;
	len1 = e1->spec.srclen + e1->spec.dstlen;

	if (len0 < len1)
		return (1);
	else if (len0 > len1)
		return (-1);
	else {
		if (e0->spec.srclen < e1->spec.srclen)
			return (1);
		else if (e0->spec.srclen > e1->spec.srclen)
			return (-1);
		else if (e0->spec.dstlen < e1->spec.dstlen)
			return (1);
		else if (e0->spec.dstlen > e1->spec.dstlen)
			return (-1);
	}
	return (0);
}

static int
is_label(struct odflow_spec *pspec, int *label)
{
	int sum = pspec->srclen + pspec->dstlen;
	int label_sum = label[0] + label[1];
	return (sum - label_sum);
}

static int
get_child_bitsize(struct hhh_task *ptask)
{
	uint32_t bitsize;

	/* update */
	if (ptask->bitsize == 0)
		bitsize = 4;
	else if (ptask->bitsize == 4)
		bitsize = 2;
	else if (ptask->bitsize == 2)
		bitsize = 1;
	else
		bitsize = 0;
	return bitsize;
}

static void
set_child_label(struct hhh_task *ptask, struct hhh_task *pctask, struct odflow_spec *pspec)
{
	uint32_t max_bitsize = ptask->bytesize * 8;	/* border of plots */
#if 0
	uint32_t sum = pspec->srclen + pspec->dstlen;
#else
	uint32_t sum = ptask->label[0] + ptask->label[1];
#endif
	int diff;

	diff = (int)pctask->bitsize - (int)ptask->bitsize;
	printf("XXX dif=%d caled(%d)\n", diff, pspec->srclen + diff);
	if (max_bitsize > sum) {
#if 0
		if (pspec->srclen < pspec->dstlen){
			pctask->label[0] = pspec->srclen + diff;
			pctask->label[1] = pspec->dstlen;
		} else if (pspec->srclen > pspec->dstlen){
			pctask->label[0] = pspec->srclen;
			pctask->label[1] = pspec->dstlen + diff;
		} else {
			pctask->label[0] = pspec->srclen + diff;
			pctask->label[1] = pspec->dstlen + diff;
		}
#else
		if (ptask->label[0] < ptask->label[1]){
			pctask->label[0] = ptask->label[0] + diff;
			pctask->label[1] = ptask->label[1];
		} else if (ptask->label[0] > ptask->label[1]){
			pctask->label[0] = ptask->label[0];
			pctask->label[1] = ptask->label[1] + diff;
		} else {
			pctask->label[0] = ptask->label[0] + diff;
			pctask->label[1] = ptask->label[1] + diff;
		}
#endif
	} else {
		if (ptask->label[0] > ptask->label[1]){
			pctask->label[0] = ptask->label[0] + diff;
			pctask->label[1] = ptask->label[1];
		} else if (ptask->label[0] < ptask->label[1]){
			pctask->label[0] = ptask->label[0];
			pctask->label[1] = ptask->label[1] + diff;
		} else {
			pctask->label[0] = ptask->label[0] + diff;
			pctask->label[1] = ptask->label[1] + diff;
		}
	}
}


static int
binsearch(struct odflow **plist, int *label, int start, int end)
{
	struct odflow *pflow = NULL;
	int saved_end = end;
	int mid;
	uint16_t len;
	int flg;
	uint16_t sum = label[0] + label[1];

	if (end == 0)	return end;

	/* find a corresponding position of the label */
	mid = (start + end) / 2;
	while (end - start > 1){
		pflow = plist[mid];
		len = pflow->spec.srclen + pflow->spec.dstlen;
		if (len < sum)
			end = mid;
		else if (len > sum)
			start = mid; 
		else if (pflow->spec.srclen < label[0])
			end = mid;
		else if (pflow->spec.srclen > label[0])
			start = mid; 
		else if (pflow->spec.dstlen < label[1])
			end = mid;
		else if (pflow->spec.dstlen > label[1])
			start = mid; 
		else
			break;
		mid = (start + end) / 2;
	}
	if (pflow == NULL)
		pflow = plist[mid];

	/* find start position of the label if label exists */
	flg = is_label(&pflow->spec, label);
	if (flg > 0){
		while ((mid >= 0) && (mid < saved_end)) {
			pflow = plist[mid];
			if (is_label(&pflow->spec, label) <= 0)
				break; 
			mid++;
		}
	} else if (flg == 0) {
		while ((mid >= 0) && (mid < saved_end)) {
			pflow = plist[mid];
			if (is_label(&pflow->spec, label) < 0)
				break; 
			mid++;
		}
	} else {
		while ((mid > 0) && (mid < saved_end)) {
			pflow = plist[mid];
			if (is_label(&pflow->spec, label) >= 0)
				break; 
			mid--;
		}
	}
	return mid;
}
