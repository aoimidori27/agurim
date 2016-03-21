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

#ifndef AGURIM_ODFLOW_H
#define AGURIM_ODFLOW_H

#include <sys/queue.h>
#include <sys/types.h>

#include <stdint.h>

#define MAXLEN		16
#define MAX_NUM_PROTO   10

struct odflow_spec {
	uint8_t src[MAXLEN];	/* source ip/proto */
	uint8_t dst[MAXLEN];	/* destination ip/proto */
	uint8_t srclen;		/* prefix length of source ip/proto */
	uint8_t dstlen;		/* prefix length of destination ip/proto */
};

struct odf_tailq {
	TAILQ_HEAD(odfq, odflow) odfq_head;
	int nrecord;	/* number of record */
};

struct odflow {
	struct odflow_spec spec;
	int af;

	uint64_t packet;
	uint64_t byte;

	uint64_t list_index;

	struct odflow_list *subflow;
	struct odflow_list *cache;

	TAILQ_ENTRY(odflow) odf_chain; /* for hash table */
};

struct odflow_hash {
	struct odf_tailq *tbl;
	int nrecord;	/* number of records */
	uint64_t byte;
	uint64_t packet;
};

struct odflow_list {
 	struct odflow **list;
	uint64_t max_size;
 	uint64_t size;
};

void odflow_init(void);
struct odflow *odflow_alloc(void);
struct odflow *odflow_addcount(struct odflow *pflow);
void odflow_reset(void);
void odflow_free(struct odflow* pflow);
void odflow_copy(struct odflow *dst, struct odflow *src);
void odflow_print(struct odflow *pflow);
void odproto_print(struct odflow *pproto);

void
subodflow_addcount(struct odflow *pflow, struct odflow *pproto);

struct odflow_spec
create_spec(struct odflow_spec *pspec, const int *label, int bytesize);

void prefix_set(uint8_t *r0, uint8_t len, uint8_t *r1, int bytesize);
int prefix_comp(uint8_t *r, uint8_t *r2, uint8_t len);
int is_overlapped(struct odflow *p0, struct odflow *p1);

#endif /* AGURIM_ODFLOW_H */
