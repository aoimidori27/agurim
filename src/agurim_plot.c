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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "agurim_plot.h"
#include "agurim_param.h"
#include "util/plot_aguri.h"
#include "util/plot_csv.h"
#include "util/plot_json.h"
#include "util/odflow_hash.h"

static int
plot_comp(const void *p0, const void *p1);
static int
comp_frac(struct odflow *pflow0, struct odflow *pflow1);
static void
add_timeslot(struct odflow_hash *phash);
static int
find_overlapped_agrflow(struct odflow *pflow);

void
plot_addcount(struct odflow* pflow)
{
	(void)odflow_addcount(pflow);
}

void
plot_run(void)
{
	struct odflow_hash *phash;

	if (query.view == PROTO_VIEW){
		phash = proto_hash;
		add_timeslot(phash);
	} else {
		phash = ip_hash;
		add_timeslot(phash);
		phash = ip6_hash;
		add_timeslot(phash);
	}
	param_update_cntlist_index();
}

void
plot_show(void)
{
	uint64_t i;
	struct odflow_list *psubflow_list;
	struct odflow *pflow;

	// set current index before shaffling the flow list 
        for (i = 0; i < inparam.agrflow_list->size; i++) {
		pflow = inparam.agrflow_list->list[i];
		pflow->list_index = i;
	}

        /* sort flow entries based on the byte/packet count in order */
        qsort(inparam.agrflow_list->list, inparam.agrflow_list->size, sizeof(struct odflow *), plot_comp);

        /* sort subflow entries based on the byte/packet count in order */
        for (i = 0; i < inparam.agrflow_list->size; i++) {
		psubflow_list = inparam.agrflow_list->list[i]->subflow;
		if ((psubflow_list != NULL) && (psubflow_list->size > 0))
        		qsort(psubflow_list->list, psubflow_list->size, sizeof(struct odflow *), plot_comp);
	}

	switch (query.outfmt) {
	case REAGGREGATION:
		print_aguri();
		break;
	case JSON:
		print_json();
		break;
	case DEBUG:
		print_csv();
		break;
	}
}

/* helper for qsort: compare the counters by the given criteria */
static int
plot_comp(const void *p0, const void *p1)
{
	struct odflow *pflow0, *pflow1;
	int ret = 0;

	pflow0 = *(struct odflow **)p0;
	pflow1 = *(struct odflow **)p1;

	switch (query.basis) {
	case BYTE:
		if (pflow0->byte > pflow1->byte)
			ret = -1;
		else if (pflow0->byte < pflow1->byte)
			ret = 1;
		break;
	case PACKET:
		if (pflow0->packet > pflow1->packet)
			ret = -1;
		else if (pflow0->packet < pflow1->packet)
			ret = 1;
		break;
	case COMBINATION:
		ret = comp_frac(pflow0, pflow1);
		break;
	}
	return (ret);
}

static int
comp_frac(struct odflow *pflow0, struct odflow *pflow1)
{
	double fbyte0, fbyte1;
	double fpacket0, fpacket1;
	double fv0, fv1;

	fbyte0 = pflow0->byte * 100 / inparam.total_byte;
	fbyte1 = pflow1->packet * 100 / inparam.total_packet;
	fpacket0 = pflow0->byte * 100 / inparam.total_byte;
	fpacket1 = pflow1->packet * 100 / inparam.total_packet;

	fv0 = max(fbyte0, fpacket0);
	fv1 = max(fbyte1, fpacket1);
	if (fv0 > fv1)
		return -1;
	else if (fv0 < fv1)
		return 1;
	return 0;
}

static void
add_timeslot(struct odflow_hash *phash)
{
	struct odf_tailq *ptailq;
	struct odflow *pflow;
	int i, agrflow_index;

	/* no traffic means no necessary to aggregate flows */
	if (phash->nrecord == 0)
		return;

        for (i = 0; i < NBUCKETS; i++) {
		if (phash->nrecord <= 0)
			break;
		ptailq = &phash->tbl[i];
		phash->nrecord -= ptailq->nrecord;
                while ((pflow = TAILQ_FIRST(&ptailq->odfq_head)) != NULL) {
                        TAILQ_REMOVE(&ptailq->odfq_head, pflow, odf_chain);
			ptailq->nrecord--;
			agrflow_index = find_overlapped_agrflow(pflow);
#if 0
			printf("idx[%d] ", agrflow_index);
			odflow_print(pflow);
			printf("\n");
#endif
			param_update_plot_count(agrflow_index, pflow);
		}
	}
}

static int
find_overlapped_agrflow(struct odflow *pflow)
{
	struct odflow *pagrflow;
	uint64_t i;

	// TODO do binary search instead of linear search
	for (i = 0; i < inparam.agrflow_list->size; i++){
		pagrflow = inparam.agrflow_list->list[i];
		if (is_overlapped(pagrflow, pflow)){
			break;
		}
	}
	if (i == inparam.agrflow_list->size){
		while(1);
	}
	return i;
}
