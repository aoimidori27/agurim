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
#include <unistd.h>

#include "agurim_param.h"
#include "agurim_file.h"
#include "agurim_plot.h"
#include "agurim_odflow.h"
#include "agurim_hhh.h"
#include "util/file_string.h"

static void agurim_init(void);
static void agurim_finish(void);
static void option_parse(int argc, void *argv);

static void
usage()
{
	fprintf(stderr, "usage:\n");
	fprintf(stderr, "  agurim [-dhpP]\n");
	fprintf(stderr, "          [-f '<src> <dst>' or '<proto>:<sport>:<dport>'\n");
	fprintf(stderr, "          [-m criteria (byte/packet)]\n"); 
	fprintf(stderr, "          [-n nflow] [-s duration] \n");
	fprintf(stderr, "          [-t thresh_percentage]\n");
	fprintf(stderr, "          [-S start_time] [-E end_time]\n");
	fprintf(stderr, "          files or directories\n");
	exit(1);
}

int main(int argc, char **argv)
{
	int n;
	char **in;
	char *path;

	option_parse(argc, argv);
	agurim_init();

	argc -= optind;
	argv += optind;

	if (argc == 0){
		/* stdin supports re-aggregation format only. */
		if (query.outfmt != REAGGREGATION) 
			usage();
		else
			read_stdin();
	}

again:
	n = argc;
	in = argv;

	while (n > 0) {
		path = *in;
		if (is_dir(path))
			read_dir(path);
		else
			read_file(path);
		++in;
		--n;
	}
	if (inparam.mode == HHH_MAIN_MODE){
		hhh_run();
		if (query.outfmt != REAGGREGATION) {
			/* reset internal parameters for text processing */
			param_set_nextmode();
			/* goto the second pass */
			goto again;
		}
	} else {
		plot_run();
	}
	agurim_finish();

	return (0);
}

static void 
agurim_init(void)
{
	param_init();
}

static void
agurim_finish(void)
{
	plot_show();
	param_finish();
}

static void
option_parse(int argc, void *argv)
{
	int ch;

	while ((ch = getopt(argc, argv, "df:hi:m:n:ps:t:E:PS:")) != -1) {
		switch (ch) {
		case 'd':	/* Set the output format = txt */
			query.outfmt = DEBUG;
			query.basis = BYTE;
			break;
		case 'f':	/* Filter */
			if (!is_ip(optarg, &query.inflow))
				usage();
			break;
		case 'h':
			usage();
			break;
		case 'i':
			query.aggr_interval = strtol(optarg, NULL, 10);
			break;
		case 'm':
			if (!strncmp(optarg, "byte", 4))
				query.basis = BYTE;
			else if (!strncmp(optarg, "packet", 6))
				query.basis = PACKET;
			else
				usage();
			break;
		case 'n':
			if (optarg[0] == '-')
				usage();
			query.nflow = strtol(optarg, NULL, 10);
			break;
		case 'p':	/* Set the output format = json */
			/* If -d and -p are input at the same time, use -d */
			if (query.outfmt != DEBUG) {
				query.outfmt = JSON;
				query.basis  = BYTE;
			}
			break;
		case 's':
			if (optarg[0] == '-')
				usage();
			query.total_duration = strtol(optarg, NULL, 10);
			break;
		case 't':
			if (optarg[0] == '-')
				usage();
			query.threshold = strtod(optarg, NULL);
			break;
		case 'E':
			if (optarg[0] == '-')
				usage();
			query.end_time = strtol(optarg, NULL, 10);
			break;
		case 'P':
			query.view = PROTO_VIEW;
			break;
		case 'S':
			if (optarg[0] == '-')
				usage();
			query.start_time = strtol(optarg, NULL, 10);
			break;
		default:
			usage();
			break;
		}
	}
}
