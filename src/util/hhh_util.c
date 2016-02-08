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

#include "../agurim_odflow.h"
#include "../agurim_param.h"
#include "hhh_util.h"
#include "hhh_task.h"
#include "odflow_hash.h"
#include "odflow_list.h"

#define CL_INITSIZE	64	/* initial array size */

static int
check_thresh(struct odflow* pflow);

static void recount_hh(struct odflow *pagrflow);
static void
add_agrflow(struct hhh_task *ptask, struct odflow *pflow);

static int
extract_now(struct hhh_task *ptask, struct odflow *pflow);
static int
check_hhstatus(struct hhh_task *ptask, struct odflow *pflow);
static int
has_more_child_task(struct hhh_task *ptask);
static void
cache_update(struct odflow *pagrflow, struct odflow_spec *pspec);
static void
cache_flush(struct odflow *pagrflow);

void
refresh_hh(struct hhh_task *ptask)
{
	if (ptask->orig_flow == NULL) {
		/* subflow aggregation has completed in child tasks. */
		/* This parent task must be free without packet/byte counting */
		return;
	}
	recount_hh(ptask->orig_flow);
	if (check_thresh(ptask->orig_flow)){
		hhh_submain(ptask->orig_flow);
		cache_flush(ptask->orig_flow);
		param_add_agrflow(ptask->orig_flow);
	} else {
		if (ptask->bitsize == 0)
			odflow_free(ptask->orig_flow);
	}
	ptask->orig_flow = NULL;
	
}

void
create_hh(struct hhh_task *ptask)
{
	struct odflow *pflow;
	uint64_t i;

	if (ptask->list == NULL || ptask->end < 0){
		printf("%s\n", __func__);
		while(1);
	}
	for (i = 0; i < ptask->end; i++) {
		pflow = ptask->list->list[i];
		if (pflow == NULL)
			continue;
		if ((pflow->byte == 0) && (pflow->packet == 0))
			continue;
		if ((pflow->spec.srclen >= ptask->label[0]) && (pflow->spec.dstlen >= ptask->label[1])){
			add_agrflow(ptask, pflow);
		}
	}
}

int
find_hh(struct hhh_task *ptask)
{
	struct odflow *pflow;
	struct odf_tailq *ptailq;
	int more_task = 0;
	uint64_t i;

	if (ptask->hash->nrecord == 0)
		goto end;
	for (i = 0; i < NBUCKETS; i++) {
		if (ptask->hash->nrecord <= 0)
			break;
		ptailq = &ptask->hash->tbl[i];
		ptask->hash->nrecord -= ptailq->nrecord;
                while ((pflow = TAILQ_FIRST(&ptailq->odfq_head)) != NULL) {
			TAILQ_REMOVE(&ptailq->odfq_head, pflow, odf_chain);
			ptailq->nrecord--;
			if (!check_thresh(pflow)) {
				odflow_free(pflow);
				continue;
			}
			more_task += extract_now(ptask, pflow);
		}
	}

end:
	if ((more_task == 0) && has_more_child_task(ptask)){
		add_child_task(ptask, ptask->orig_flow);
	}
	ptask->done = 1;
	return ((more_task != 0) ? 0 : 1);
}

static int
check_thresh(struct odflow* pflow)
{
	int ret = 0;
	if ((query.basis & BYTE) != 0){
		if (inparam.mode == HHH_MAIN_MODE){
			if (pflow->byte >= inparam.thresh_byte)
				ret = 1;
		} else {
			if (pflow->byte >= inparam.thresh2_byte)
				ret = 1;
		}
	}
	if ((query.basis & PACKET) != 0){
		if (inparam.mode == HHH_MAIN_MODE){
			if (pflow->packet >= inparam.thresh_packet)
				ret = 1;
		} else {
			if (pflow->packet >= inparam.thresh2_packet)
				ret = 1;
		}
	}
	return ret;
}


static void
recount_hh(struct odflow *pagrflow)
{
	struct odflow *pflow;
	int i;

	/* reset byte and packet counter */
	pagrflow->byte = 0;
	pagrflow->packet = 0;
	if (pagrflow->cache == NULL)
		return;

	/* count byte and packet again */
	for (i = 0; i < pagrflow->cache->size; i++) {
		pflow = pagrflow->cache->list[i];
		if (pflow != NULL){
			pagrflow->byte   += pflow->byte;
			pagrflow->packet += pflow->packet;
		}
	}
}

static void
add_agrflow(struct hhh_task *ptask, struct odflow *pflow)
{
	struct odflow *pagrflow;
	struct odflow_spec spec;

	spec = create_spec(&pflow->spec, ptask->label, ptask->bytesize);

	pagrflow = hash_find(ptask->hash, &spec);
	if (pflow != NULL) {
		/* update values */
		pagrflow->byte   += pflow->byte;
		pagrflow->packet += pflow->packet;
		pagrflow->af      = pflow->af;

		/* FIXME allocate cache here because the count 
		   of agrflows is included multiple odflows */
		if (pagrflow->cache == NULL)
			pagrflow->cache = list_alloc(CL_INITSIZE);
		list_add(pagrflow->cache, pflow);
	}
}

static int
extract_now(struct hhh_task *ptask, struct odflow *pflow)
{
	int subtask_flg;

	subtask_flg = check_hhstatus(ptask, pflow);
	if (subtask_flg){
		add_child_task(ptask, pflow);
		goto end;
	}

	if (inparam.mode == HHH_MAIN_MODE) {
		hhh_submain(pflow);
		cache_flush(pflow);
		param_add_agrflow(pflow);
	}
	else {
		cache_flush(pflow);
		param_add_subflow(pflow);
	}
end:
	return subtask_flg;
}

/* this function checks the necessary of additional tasks 
   before appending the internal flow list */
static int
check_hhstatus(struct hhh_task *ptask, struct odflow *pflow)
{
	if (ptask == NULL || pflow == NULL)
		return 0;
	/* srclen = dstlen = 0 */
	if ((ptask->label[0] == 0) || (ptask->label[1] == 0))
		return 0;
	/* bitlen is even number. This is in the middle of flow aggregation */
	if (ptask->bitsize%2 != 0)
		return 0;

	if (pflow->af == AF_INET) {
		if (ptask->label[0] == 32 && ptask->label[1] == 32)
			return 0;
		if ((ptask->label[0] <= 16) || (ptask->label[1] <= 16))
			return 0;
	}
	else if (pflow->af == AF_INET6) {
		if (ptask->label[0] == 128 && ptask->label[1] == 128)
			return 0;
		if ((ptask->label[0] <= 64) || (ptask->label[1] <= 64))
			return 0;
	}
	else if (pflow->af == AF_LOCAL) {
		return 0;
	}
	return 1;
}

static int
has_more_child_task(struct hhh_task *ptask)
{
	return check_hhstatus(ptask, ptask->orig_flow);
}

static void
cache_update(struct odflow *pagrflow, struct odflow_spec *pspec)
{
	struct odflow *pflow;
	uint64_t i;

	if ((pagrflow == NULL) || (pagrflow->cache == NULL))
		return;

	for (i = 0; i < pagrflow->cache->size; i++){
		pflow = pagrflow->cache->list[i]; 
		printf("memcmp=%d\n", memcmp(pspec, &pflow->spec, sizeof(struct odflow_spec)));
		/* is pspec included in pspec2? */
		if (memcmp(pspec, &pflow->spec, sizeof(struct odflow_spec)) == 0) {
			pflow->byte = 0; 
			pflow->packet = 0; 
			pagrflow->byte -= pflow->byte;
			pagrflow->packet -= pflow->packet;
			pagrflow->cache->list[i] = NULL;
			break;
		}
	}
}

static void
cache_flush(struct odflow *pagrflow)
{
	struct odflow *pflow;
	uint64_t i, n;

	n = pagrflow->cache->size;
	for (i = 0; i < n; i++){
		pflow = pagrflow->cache->list[i]; 
#if 0
		printf("\t");
		odflow_print(pflow);
		printf("\n");
#endif
		pflow->byte = 0; 
		pflow->packet = 0; 
		//pagrflow->cache->list[i] = NULL;
		//pagrflow->cache->size--;
	}
}
