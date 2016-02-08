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

#include <sys/socket.h>

#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "odflow_hash.h"
#include "../agurim_param.h"

/*
 * The following hash function is adapted from "Hash Functions" by Bob Jenkins
 * ("Algorithm Alley", Dr. Dobbs Journal, September 1997).
 *
 * http://www.burtleburtle.net/bob/hash/spooky.html
 */
#define mix(a, b, c)                                                    \
do {                                                                    \
	a -= b; a -= c; a ^= (c >> 13);                                 \
	b -= c; b -= a; b ^= (a << 8);                                  \
	c -= a; c -= b; c ^= (b >> 13);                                 \
	a -= b; a -= c; a ^= (c >> 12);                                 \
	b -= c; b -= a; b ^= (a << 16);                                 \
	c -= a; c -= b; c ^= (b >> 5);                                  \
	a -= b; a -= c; a ^= (c >> 3);                                  \
	b -= c; b -= a; b ^= (a << 10);                                 \
	c -= a; c -= b; c ^= (b >> 15);                                 \
} while (/*CONSTCOND*/0)

struct odflow_hash *ip_hash;
struct odflow_hash *ip6_hash;
struct odflow_hash *proto_hash;

static uint32_t calc_slot(uint8_t *v1, uint8_t *v2);

struct odflow_hash *
hash_alloc(void)
{
	struct odflow_hash *phash;
	int i;

	phash = calloc(1, sizeof(struct odflow_hash));
	if (phash == NULL)
		goto end;

	if ((phash->tbl = calloc(NBUCKETS, sizeof(struct odf_tailq))) == NULL)
		goto err;

	for (i = 0; i < NBUCKETS; i++) {
		TAILQ_INIT(&phash->tbl[i].odfq_head);
		phash->tbl[i].nrecord = 0;
	}
	phash->nrecord = 0;
end:
	return (phash);
err:
	free(phash);
	goto end;
}

/* NOTE: This API finds always set a flow pointer as return value. */
struct odflow*
hash_find(struct odflow_hash *phash, struct odflow_spec *pspec)
{
        struct odflow *pflow;
	uint32_t slot;
	uint32_t is_found = 0;

	slot = calc_slot(pspec->src, pspec->dst);

	/* find entry */
        TAILQ_FOREACH(pflow, &(phash->tbl[slot].odfq_head), odf_chain) {
                if (!memcmp(pspec, &(pflow->spec), sizeof(struct odflow_spec))) {
			is_found = 1;
                        break;
		}
	}

	if (is_found != 1) {
		pflow = odflow_alloc();
		if (pflow != NULL) {
			memcpy(&pflow->spec, pspec, sizeof(struct odflow_spec));
			TAILQ_INSERT_HEAD(&phash->tbl[slot].odfq_head, pflow, odf_chain);
			phash->tbl[slot].nrecord++;
			phash->nrecord++;
		}
	}

	return (pflow);
}

void
hash_free(struct odflow_hash *phash)
{
	hash_reset(phash);
	free(phash);
}

void
hash_reset(struct odflow_hash *phash)
{
	int i;
	struct odflow *pflow;

	if (phash->nrecord == 0)
		return;

        for (i = 0; i < NBUCKETS; i++) {
                while ((pflow = TAILQ_FIRST(&phash->tbl[i].odfq_head)) != NULL) {
			TAILQ_REMOVE(&phash->tbl[i].odfq_head, pflow, odf_chain);
			phash->tbl[i].nrecord--;
			odflow_free(pflow);
		}
	}

	phash->nrecord = 0;
	phash->byte = 0;
	phash->packet = 0;
}

uint32_t
hash_add(struct odflow_hash *phash, struct odflow *pflow)
{
        struct odflow *_pflow;
	uint32_t slot;
	uint32_t dupflg = 0;

	slot = calc_slot(pflow->spec.src, pflow->spec.dst);

	/* find entry */
        TAILQ_FOREACH(_pflow, &(phash->tbl[slot].odfq_head), odf_chain) {
                if (!memcmp(&(pflow->spec), &(_pflow->spec), sizeof(struct odflow_spec))) {
			dupflg = 1;
                        break;
		}
	}

	if (dupflg != 1) {
		TAILQ_INSERT_HEAD(&phash->tbl[slot].odfq_head, pflow, odf_chain);
		phash->tbl[slot].nrecord++;
		phash->nrecord++;
	}
	else {
		_pflow->byte  += pflow->byte;
		_pflow->packet += pflow->packet;
	}
	phash->byte  += pflow->byte;
	phash->packet+= pflow->packet;
	return dupflg;
}

static uint32_t
calc_slot(uint8_t *v1, uint8_t *v2)
{
        uint32_t a = 0x9e3779b9, b = 0x9e3779b9, c = 0;
        uint8_t *p; 
    
        p = v1;
        b += p[3];
        b += p[2] << 24; 
        b += p[1] << 16; 
        b += p[0] << 8;
    
        p = v2;
        a += p[3];
        a += p[2] << 24; 
        a += p[1] << 16; 
        a += p[0] << 8;

        mix(a, b, c); 

        return (c & (NBUCKETS-1));
}
