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
#include <time.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include "../agurim_param.h"
#include "../agurim_odflow.h"

static void print_version(void);
static void print_localtime(time_t time, char *key);
static void print_traffic_rate(void);
static void print_agurim_basis(void);
static void print_agurim_threshold(void);
static void 
subflow_print(struct odflow *pflow);
static void print_agrflow_list(void);

void
print_aguri(void)
{
	printf("\n");
	print_version();
	print_localtime(inparam.start_time, "StartTime");
	print_localtime(inparam.end_time, "EndTime");
	print_traffic_rate();
	print_agurim_basis();

	/* STEP6: display flow aggregation threshold */
	print_agurim_threshold();
	printf("\n");

	print_agrflow_list();
}

/* display Aguri version */
static void
print_version(void)
{
	printf("%%!AGURI-2.0\n");
}

/* display start and end timestamp */
static void
print_localtime(time_t time, char *key)
{
	char buf[128];

	strftime(buf, sizeof(buf), "%a %b %d %T %Y",
	    localtime(&time));
	printf("%%%%%s: %s ", key, buf);
	strftime(buf, sizeof(buf), "%Y/%m/%d %T",
	    localtime(&time));
	printf("(%s)\n", buf);
}

/* display average traffic rate in bps and pps */
static void
print_traffic_rate(void)
{
	double avg_byte, avg_pkt;
	double sec;
	sec = (double)(inparam.end_time - inparam.start_time);
	if (sec == 0.0) {
		return;
	}
	avg_pkt = (double)inparam.total_packet / sec;
	avg_byte = (double)inparam.total_byte * 8 / sec;

	if (avg_byte > 1000000000.0)
		printf("%%AvgRate: %.2fGbps %.2fpps\n",
		    avg_byte/1000000000.0, avg_pkt);
	else if (avg_byte > 1000000.0)
		printf("%%AvgRate: %.2fMbps %.2fpps\n",
		    avg_byte/1000000.0, avg_pkt);
	else if (avg_byte > 1000.0)
		printf("%%AvgRate: %.2fKbps %.2fpps\n",
		    avg_byte/1000.0, avg_pkt);
	else
		printf("%%AvgRate: %.2fbps %.2fpps\n",
		    avg_byte, avg_pkt);
}

/* display agggregation basis in Agurim */
static void
print_agurim_basis(void)
{
	if (query.basis == BYTE)
		printf("%% criteria: byte counter ");
	else if (query.basis == PACKET)
		printf("%% criteria: pkt counter ");
	else if (query.basis == COMBINATION)
		printf("%% criteria: combination ");
}

static void
print_agurim_threshold(void)
{
	printf("(%.f %% for addresses, %.f %% for protocol data)\n",
	    (double)query.threshold, (double)query.threshold);
}

static void
print_agrflow_list(void)
{
	struct odflow *pflow;
	struct odflow *pproto;
	int i;

	for (i = 0; i < inparam.agrflow_list->size; i++) {
		pflow = inparam.agrflow_list->list[i];
		/* STEP1: display the list index and primary odflow spec */
		printf("[%2d] ", i);
		odflow_print(pflow);

		/* STEP2: display byte and packet count */
		printf(": %" PRIu64 " (%.2f%%)\t%" PRIu64 " (%.2f%%)\n",
		    pflow->byte, (double)pflow->byte / inparam.total_byte * 100,
		    pflow->packet, (double)pflow->packet / inparam.total_packet * 100);
		/* STEP3: display byte and packet count */
		subflow_print(pflow); // TODO 
	}
}

static void 
subflow_print(struct odflow *pflow)
{
	struct odflow_list *plist = pflow->subflow;
	struct odflow *psubflow;
	uint64_t i;

	if ((plist == NULL) || (plist->size == 0)){
		//printf("[*:*:*] 100.00%% 100.00%%");
		goto end;
	}
	for (i = 0; i < plist->size; i++){
		psubflow = plist->list[i];
		printf("[");
		odflow_print(psubflow);
		printf("]");
		printf(" %.2f%% %.2f%% ",
		    (double)psubflow->byte / pflow->byte * 100,
		    (double)psubflow->packet / pflow->packet * 100);
		odflow_free(psubflow); // FIXME 
	}
	list_free(plist);
end:
	printf("\n"); // FIXME
}

static void
print_agrflow_list_data(void)
{
}
