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

#include <sys/stat.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <math.h>

#include "agurim_param.h"
#include "util/odflow_hash.h"
#include "util/odflow_list.h"

#define INIT_LIST_SIZE 16

struct agurim_query query;
struct agurim_param inparam;

static void query_init(void);
static void inparam_init(void);
static int calc_interval(void);
static void alloc_cntlist(uint64_t nslot);

void
param_init(void)
{
	query_init();

	inparam_init();

	ip_hash = hash_alloc();
	ip6_hash = hash_alloc();
	proto_hash = hash_alloc();
}

void
param_finish(void)
{
	if ((inparam.agrflow_list != NULL) && (inparam.agrflow_list->size > 0))
		list_free(inparam.agrflow_list);

	if ((inparam.subflow_list != NULL) && (inparam.subflow_list->size > 0))
		list_free(inparam.subflow_list);
}

void
param_set_nextmode(void)
{
	uint64_t ntimeslot;
	if (inparam.mode != HHH_MAIN_MODE)
		while(1); // FIXME

	inparam.mode = AGURIM_PLOT_MODE;

	/* calculate time resolution */
	inparam.plot_interval  = calc_interval();
 	ntimeslot = ceil((inparam.end_time - inparam.start_time)/inparam.plot_interval) + 1; 
	alloc_cntlist(ntimeslot);
	inparam.plot_index = 0;
	inparam.start_time = 0;
	inparam.cur_time = 0;
	inparam.end_time = 0;
}

void
param_switch_hhhmode(void)
{
	if (inparam.mode == HHH_MAIN_MODE){
		inparam.mode = HHH_SEC_MODE;
		inparam.total2_byte   = 0;
		inparam.total2_packet = 0;
	} else {
		inparam.mode = HHH_MAIN_MODE;
		inparam.total2_byte   = 0;
		inparam.total2_packet = 0;
	}
}

void 
param_reset_hhhmode(void)
{
	inparam.total_byte   = 0;
	inparam.total_packet = 0;
	inparam.total2_byte   = 0;
	inparam.total2_packet = 0;
	inparam.start_time = inparam.end_time;
	inparam.cur_time   = inparam.start_time;
	if (inparam.agrflow_list->size > 0){
		list_free(inparam.agrflow_list);
		inparam.agrflow_list = list_alloc(INIT_LIST_SIZE);
	}
}

void 
param_update_total(uint64_t byte, uint64_t packet)
{
	inparam.total_byte   += byte;
	inparam.total_packet += packet;
}

void
param_update_total2(uint64_t byte, uint64_t packet)
{
	inparam.total2_byte   += byte;
	inparam.total2_packet += packet;
}		

void 
param_update_cntlist_index(void)
{
	uint64_t i, n, total_count = 0;
	/* set total count in this turn */
	n = inparam.agrflow_list->size;
	for (i = 0; i < n; i++){
		total_count += inparam.plots.cnt_list[i][inparam.plot_index];
	}
	inparam.plots.total_list[inparam.plot_index] = total_count;

	/* set this timestamp */
	inparam.plots.time_list[inparam.plot_index] = inparam.start_time;

	/* reset next timestamp */
	inparam.plot_index++;
	inparam.plots.time_list[inparam.plot_index] = 0;
}

void
param_update_plot_count(uint32_t agrflowlist_index, struct odflow *pflow)
{
	uint64_t cnt;

	if (query.basis == PACKET)
		cnt = pflow->packet;
	else
		cnt = pflow->byte;
	//printf("%s: agrflowlist_index=%d, plot_index=%d\n", __func__, agrflowlist_index, inparam.plot_index);
	inparam.plots.cnt_list[agrflowlist_index][inparam.plot_index] += cnt;
}
		
void
param_set_thresh(void)
{
	inparam.thresh_byte   = inparam.total_byte * query.threshold / 100;
	inparam.thresh_packet = inparam.total_packet * query.threshold / 100;
}

void
param_set_thresh2(void)
{
	inparam.thresh2_byte   = inparam.total_byte * query.threshold / 100;
	inparam.thresh2_packet = inparam.total_packet * query.threshold / 100;
}

void
param_set_starttime(time_t t, int *exit_flg, int *agr_flg)
{
	/* This timestamp has not reached the target timestamp. */
	if (query.start_time > t)
		return;

	if (inparam.start_time == 0) {
		inparam.start_time = t;
	}
	inparam.cur_time = t;

	if (inparam.mode != AGURIM_PLOT_MODE) {
		if (t - inparam.start_time >= query.aggr_interval){
			*agr_flg = 1;
		}
	} else {
		if (t - inparam.start_time >= inparam.plot_interval){
			*agr_flg = 1;
		}
	}

	if (query.outfmt == REAGGREGATION)
		return;

	/* int duration = t - plot_timestamps[time_slot]; */
	if ((inparam.cur_time - inparam.start_time) >= query.total_duration) {
		*exit_flg = 1;
	}
	
}

void
param_set_endtime(time_t t)
{
	if (!inparam.start_time)
		return;

	if ((query.end_time != 0) && (query.end_time < t))
		return;

	inparam.end_time = t;
}

void
param_add_agrflow(struct odflow *pflow)
{
#if 0
	printf("\n");
	printf("%s: [%d]" , __func__, inparam.agrflow_list->size);
	odflow_print(pflow);
	//printf(" %2f%% %2f%%", pflow->byte*100/inparam.total_byte, pflow->packet*100/inparam.total_packet );
	printf("\n%llu %llu %llu %llu", inparam.total_byte, inparam.total_packet, pflow->byte, pflow->packet);
	printf("\n");
#endif
	if (pflow->cache != NULL)
		list_free(pflow->cache);

	if (inparam.subflow_list != NULL){
		/* move all entries as subflows.
		   Then, allocate subflist space again */
		pflow->subflow = inparam.subflow_list;
		inparam.subflow_list = NULL;
	}
#if 0
	{
		if (pflow->subflow != NULL ){
		struct odflow_list *plist = pflow->subflow;
		struct odflow *psubflow;
		uint64_t i;
		for (i = 0; i < plist->size; i++){
			psubflow = plist->list[i];
			printf("\t");
			odflow_print(psubflow);
			printf("\n");
		}
		}
	}
#endif
	list_add(inparam.agrflow_list, pflow);
}

void
param_add_subflow(struct odflow *pflow)
{
	if (inparam.subflow_list == NULL)
		inparam.subflow_list = list_alloc(1);
	//if (pflow->cache != NULL)	// FIXME
	//	list_free(pflow->cache);
	list_add(inparam.subflow_list, pflow);
}

static void
query_init(void)
{
	if (!query.basis)
		query.basis = COMBINATION;
	if (!query.aggr_interval) 
		query.aggr_interval = 60;
	if (!query.outfmt) 
		query.outfmt = REAGGREGATION;
	if (!query.threshold) {
		if (query.outfmt == REAGGREGATION)
			query.threshold = 1;
		else
			query.threshold = 3; // or query.threshold = 10;
	}
	if (!query.nflow && query.outfmt != REAGGREGATION)
		query.nflow = 7;
	if (query.outfmt == REAGGREGATION)
		return;
	if (!query.start_time && !query.end_time && !query.total_duration)
		query.total_duration = 60*60*24;
	else {
		if ((!query.start_time || !query.end_time) && !query.total_duration)
			query.total_duration = 60*60*24;
		if (query.total_duration && query.end_time)
			 query.start_time = query.end_time - query.total_duration;
		if (query.total_duration && query.start_time)
			 query.end_time = query.start_time + query.total_duration;
		if (query.start_time && query.end_time)
			query.total_duration = query.end_time - query.start_time;
	}
}

static void
inparam_init(void)
{
	memset(&inparam, 0, sizeof(struct agurim_param));

	/* create the primary flow list in advance */
	inparam.agrflow_list = list_alloc(INIT_LIST_SIZE);	// FIXME parameter optimization
}

/* compute the appropriate interval from the duration */
/* note: this api assumes inparam.start_time and inparam.end_time r filled in */
static int
calc_interval(void)
{
	double duration;
	int interval;
	int d;

	duration = inparam.end_time - inparam.start_time;

	/*
	 * Guideline for a plotting interval
	 * +---------------------------------------+
	 * | duration | interval   (sec) | # of pt |
	 * +---------------------------------------+
	 * |  1year   |   1day   (86400) |  365pt  |
	 * |  1month  |   4hour  (14400) |  180pt  |
	 * |  1week   |   60min   (3600) |  168pt  |
	 * |  1day    |   10min    (600) |  144pt  |
	 * |  1hour   |   30sec     (30) |  120pt  |  	
	 * +---------------------------------------+
	 */
	d = (int)ceil(duration/3600);
	if (d <= 24) {
		/* shorter than 24hours: hours * 30 */
		interval = d * 30;
		return (interval < 600 ? interval : 600);
	}

	d = (int)ceil(duration/3600/24);
	if (d <= 7) {
		/* shorter than 7days: days * 600 */
		interval = d * 600;
		return (interval < 3600 ? interval : 3600);
	}
	if (d <= 31) {
		/* shorter than 31days: 14400 */
		return (14400);
	}

	d = (int)ceil(duration/3600/24/31);
	if (d <= 12) {
		/* shorter than 12months: months * 10800 */
		interval = d * 14400;
		return (interval < 86400 ? interval : 86400);
	}

	/* longer than 12months: years * 86400 */
	d = (int)ceil(duration/3600/24/366);
	interval = (int)duration * 86400;
	return (interval); 
}

static void
alloc_cntlist(uint64_t nslot)
{
	uint64_t i, n;

	inparam.plots.size = nslot;

	n = inparam.agrflow_list->size;
	inparam.plots.cnt_list   = malloc(sizeof(uint64_t) * n);
	inparam.plots.time_list  = malloc(sizeof(time_t) * nslot);
	inparam.plots.total_list = malloc(sizeof(uint64_t) * nslot);
	memset(inparam.plots.time_list, 0, sizeof(time_t) * nslot);
	memset(inparam.plots.total_list, 0, sizeof(uint64_t) * nslot);
	assert(inparam.plots.time_list == NULL);
	assert(inparam.plots.total_list == NULL);
 
	for (i = 0; i < n; i++){
		inparam.plots.cnt_list[i]  = malloc(sizeof(uint64_t) * nslot);
		memset(inparam.plots.cnt_list[i],   0, sizeof(uint64_t) * nslot);
		assert(inparam.plots.cntlist[i] == NULL);
	}
}

