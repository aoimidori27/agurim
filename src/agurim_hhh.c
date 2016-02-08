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

#include "agurim_hhh.h"
#include "agurim_param.h"
#include "util/odflow_list.h"
#include "util/odflow_hash.h"
#include "util/hhh_task.h"
#include "util/hhh_util.h"

static void hhh_main(struct task_tailq *ptaskq);
static void
hhh_finish(struct odflow_list **list, uint32_t nlist);
static void
create_subhash(struct odflow_hash *phash, struct odflow *pagrflow);

void 
hhh_run(void)
{
	struct task_tailq taskq;
	struct odflow_list *list[2];
	uint32_t nlist;

	/* step1: set threshold */
	param_set_thresh();
	
	/* step2: set HHH internal parameters */
	TAILQ_INIT(&taskq.task_head);
	taskq.ntask = 0;
	if (query.view != PROTO_VIEW) {
		nlist = 2;
		list[0] = taskq_create(&taskq, AF_INET);
		list[1] = taskq_create(&taskq, AF_INET6);
	} else {
		nlist = 1;
		list[0]= taskq_create(&taskq, AF_LOCAL);
	}

	/* step3: HHH (overlap algorithm) */
	hhh_main(&taskq);

	/* step4: free internal parameters */
	hhh_finish(list, nlist); // TODO
}

/* NOTE: this function for subflow aggregation */
void
hhh_submain(struct odflow *pagrflow)
{
	struct task_tailq taskq;
	struct odflow_list *list[2];
	uint32_t nlist;

	/* step1: set agurim internal parameter */
	param_switch_hhhmode();

	/* step2: set HHH internal parameters */
	TAILQ_INIT(&taskq.task_head);
	taskq.ntask = 0;
	if (query.view != PROTO_VIEW) {
		create_subhash(proto_hash, pagrflow);
		nlist = 1;
		list[0]= taskq_create(&taskq, AF_LOCAL);
	} else {
		create_subhash(ip_hash, pagrflow);
		create_subhash(ip6_hash, pagrflow);
		nlist = 2;
		list[0] = taskq_create(&taskq, AF_INET);
		list[1] = taskq_create(&taskq, AF_INET6);
	}
	param_set_thresh2();

	/* step3: HHH (overlap algorithm) */
	hhh_main(&taskq);

	/* step4: free agurim internal parameter */
	hhh_finish(list, nlist);

	param_switch_hhhmode();
}

static void
hhh_main(struct task_tailq *ptaskq)
{
	struct hhh_task *ptask;
	int done;

	while (ptaskq->ntask > 0){
                ptask = TAILQ_FIRST(&ptaskq->task_head);
#if 0
		printf("XXX %s ntask(%ld) label(%ld, %ld) end=%d ", __func__, ptaskq->ntask, ptask->label[0], ptask->label[1], ptask->end);
		if (ptask->orig_flow){
			odflow_print(ptask->orig_flow);
		}
		printf("\n");
#endif
		if (ptask->done) {
			refresh_hh(ptask);
			done = 1;
		} else {
			create_hh(ptask);
			done = find_hh(ptask);
		}
		if (done){
			TAILQ_REMOVE(&ptaskq->task_head, ptask, task_chain);
			task_free(ptask);
			ptaskq->ntask--; 
		}
	}
}

static void
hhh_finish(struct odflow_list **list, uint32_t nlist)
{
	struct odflow *pflow;
	uint32_t i;
	uint64_t j;

	for (i = 0; i < nlist; i++) {
		if (list[i] == NULL)
			continue;
		for (j = 0; j < list[i]->size; j++) {
        	        pflow = list[i]->list[j]; 
			if (pflow == NULL) {
				printf("%s\n", __func__);
				while(1);
			}
			if (pflow->cache != NULL)
				list_free(pflow->cache);
			if (pflow->subflow != NULL)
				list_free(pflow->subflow);
			odflow_free(pflow);
		}
	}
}

static void
create_subhash(struct odflow_hash *phash, struct odflow *pagrflow)
{
	struct odflow *pflow, *psubflow;
	uint64_t i, j;
	uint32_t is_exist;

	for (i = 0; i < pagrflow->cache->size; i++){
		pflow = pagrflow->cache->list[i];
		param_update_total2(pflow->byte, pflow->packet);
		if (pflow->subflow == NULL){
			printf("%s subflow is null\n", __func__);
			while (1);
		}
		for (j = 0; j < pflow->subflow->size; j++){
			psubflow = pflow->subflow->list[j];
			is_exist = hash_add(phash, psubflow);
			if (is_exist != 0){
				odflow_free(psubflow);
			}
		}
		list_free(pflow->subflow);
		pflow->subflow = NULL; 
	}
}
