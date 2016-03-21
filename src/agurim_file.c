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
#include <sys/types.h>

#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <assert.h>

#include "agurim_file.h"
#include "agurim_param.h"
#include "agurim_odflow.h"
#include "agurim_plot.h"
#include "agurim_hhh.h"
#include "util/file_string.h"

#define AGURIM_BUFSIZ	(BUFSIZ << 1)

static void read_in(FILE *fp);
static int
is_filter(struct odflow *pip, struct odflow *pproto, uint64_t nproto);

int
is_dir(char *path)
{
	struct stat st;

        return ((stat(path, &st) == 0) && ((st.st_mode & S_IFMT) == S_IFDIR));
}

void
read_dir(char *path)
{
        struct dirent **flist;
	char file[254];
        int i, m;

        m = scandir(path, &flist, NULL, alphasort);
	if (m < 0)
		fprintf(stderr, "scandir(%s) failed\n", path);

        for (i = 0; i < m; i++) {
                if (!strncmp(flist[i]->d_name, ".", 1)) 
                        continue;
                if (!strncmp(flist[i]->d_name, "..", 2)) 
                        continue;

                sprintf(file, "%s/%s", path, flist[i]->d_name);

		read_file(file);
        }
}

void
read_file(char *file)
{
	FILE *fp;

	if ((fp = fopen(file, "r")) != NULL) {
		read_in(fp);
		(void)fclose(fp);
	}
}

void
read_stdin(void)
{
	if (isatty(fileno(stdin)))
		fprintf(stderr, "reading from stdin...\n");

	 /* read from stdin */
	read_in(stdin);
}

static void
read_in(FILE *fp)
{
	struct odflow odflow, odproto[MAX_NUM_PROTO];
	struct odflow *pflow;
	int exit_flg, agr_flg;
	uint64_t nproto, i;
	char buf[AGURIM_BUFSIZ];
	int filter_idx;

	agr_flg = 0;
	exit_flg = 0;

	while (fgets(buf, AGURIM_BUFSIZ, fp)) {
		if (is_preamble(buf, &exit_flg, &agr_flg)) {
			if (exit_flg){
				break;
			}
			if (agr_flg) {
				agr_flg = 0;
				if (inparam.mode == AGURIM_PLOT_MODE){
					plot_run();
				} else {
					if (query.outfmt != REAGGREGATION)
						break;
					hhh_run();
					plot_show();
					param_reset_hhhmode();
				}
			}
			continue;
		}

		/* Does this string include srcip and dstip? */
		if (!is_ip(buf, &odflow))
			continue;

		if (fgets(buf, AGURIM_BUFSIZ, fp) == NULL)
			continue;

		/* Does this string include proto, sport and dport? */
		nproto = is_proto(buf, odflow.byte, odflow.packet, odproto);
		if (nproto == 0)
			continue;

		/* is target odflow? */
		if ((filter_idx = is_filter(&odflow, odproto, nproto)) < 0)
			continue;

		if (inparam.mode != AGURIM_PLOT_MODE){
			/* add flow entries as odflows based on primary flow criteria */
			if (query.view == PROTO_VIEW) {
				for (i = 0; i < nproto; i++){
					pflow = odflow_addcount(&odproto[i]);
					assert(pflow != NULL);
					subodflow_addcount(pflow, &odflow);
					param_update_total(odproto[i].byte, odproto[i].packet);
				}
			} else {
				pflow = odflow_addcount(&odflow);
				param_update_total(odflow.byte, odflow.packet);
				assert(pflow != NULL);
				for (i = 0; i < nproto; i++){
					subodflow_addcount(pflow, &odproto[i]);
				}
			}
		} else {
			if (query.view == PROTO_VIEW) {
				for (i = 0; i < nproto; i++){
					plot_addcount(&odproto[i]);
					param_update_total(odproto[i].byte, odproto[i].packet);
				}
			} else {
				plot_addcount(&odflow);
				param_update_total(odflow.byte, odflow.packet);
			}
		}
	}
}

static int
is_filter(struct odflow *pip, struct odflow *pproto, uint64_t nproto)
{
	int ret = -1;
	int i;

	if (query.inflow.af == AF_UNSPEC)
		ret = 0;
	else if (query.inflow.af == AF_INET){
		if (is_overlapped(pip, &query.inflow)){
			ret = 0;
		}
	}
	else if (query.inflow.af == AF_INET6){
		for (i = 0; i < nproto; i++){
			if (is_overlapped(pproto, &query.inflow)){
				ret = i;
				break;
			}
		}
	}
	return ret;
}
