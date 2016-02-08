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

#ifndef AGURIM_PARAM_H
#define AGURIM_PARAM_H

#include "agurim_odflow.h"

typedef enum _aggr_basis {
	PACKET = 1,
	BYTE,
	COMBINATION
} AGGR_BASIS;

typedef enum _agurim_mode {
	HHH_MAIN_MODE,
	HHH_SEC_MODE,
	AGURIM_PLOT_MODE
} AGURIM_MODE;

typedef enum _agurim_format {
	REAGGREGATION,
	DEBUG,
	JSON
} AGURIM_FORMAT;

typedef enum {
	ADDR_VIEW,
	PROTO_VIEW
} AGURIM_VIEW;

struct agurim_query {
	AGGR_BASIS    basis;
	AGURIM_FORMAT outfmt;

	int aggr_interval;
	int threshold;
	int nflow;
	int total_duration;
	time_t start_time;
	time_t end_time;

	/* options */
	AGURIM_VIEW   view;
	struct odflow inflow; /* filtering odflow */
};

struct plot_list {
 	time_t 	 *time_list;
 	time_t 	 *total_list;
 	uint64_t **cnt_list;
 	uint64_t size;
};

struct agurim_param {
	/* agurim paramters */
	AGURIM_MODE mode;

	time_t start_time;
	time_t cur_time;
	time_t end_time;

	/* plot internal paramters */
	int plot_interval; /* time resolution in plots */
	struct plot_list plots;	/* this structure is used for data in time resultion */
	uint64_t plot_index;		/* indicate the index of the list in plot_list */

	/* HHH internal paramters */
	struct odflow_list *agrflow_list;
	struct odflow_list *subflow_list;
	uint64_t total_byte, total_packet;
	uint64_t total2_byte, total2_packet;
	uint64_t thresh_byte, thresh_packet; 
	uint64_t thresh2_byte,  thresh2_packet; 
};

#define max(a, b)	(((a)>(b))?(a):(b))

extern struct agurim_query query;
extern struct agurim_param inparam;
extern struct odflow_hash *ip_hash;
extern struct odflow_hash *ip6_hash;
extern struct odflow_hash *proto_hash;

void param_init(void);
void param_finish(void);

void param_set_nextmode(void);
void param_switch_hhhmode(void);
void param_reset_hhhmode(void);
void param_update_total(uint64_t byte, uint64_t packet);
void param_update_total2(uint64_t byte, uint64_t packet);
void param_set_thresh(void);
void param_set_thresh2(void);
void param_update_cntlist_index(void);
void param_update_plot_count(uint32_t agrflowlist_index, struct odflow *pflow);
void param_set_starttime(time_t t, int *exit_flg, int *agr_flg);
void param_set_endtime(time_t t);
void param_add_agrflow(struct odflow *pflow);
void param_add_subflow(struct odflow *pflow);

#endif /* AGURIM_PARAM_H */
