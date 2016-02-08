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
#include <err.h>
#include <assert.h>

#include "agurim_param.h"
#include "agurim_odflow.h"
#include "util/odflow_hash.h"
#include "util/odflow_list.h"
#include "util/file_string.h"

static uint8_t prefixmask[8]
    = { 0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe };

/* NOTE: this API does not allocate flow cache. */
struct odflow*
odflow_alloc(void)
{
	struct odflow *pflow;

	pflow = malloc(sizeof(struct odflow));
	if (pflow != NULL){
		memset(pflow, 0, sizeof(struct odflow));
		pflow->subflow = NULL;
		pflow->cache = NULL;
	}

	return pflow;
}

struct odflow*
odflow_addcount(struct odflow *pflow)
{
	struct odflow_hash *phash = NULL;
	struct odflow *_pflow;

	if (pflow->af == AF_INET)
		phash = ip_hash;
	else if  (pflow->af == AF_INET6)
		phash = ip6_hash;
	else
		phash = proto_hash;

	assert(phash != NULL);

	_pflow = hash_find(phash, &pflow->spec);
	_pflow->af      = pflow->af;
	_pflow->byte   += pflow->byte;
	_pflow->packet += pflow->packet;

	return _pflow;
}

/* add counts to lower odflow (odproto) */
void
subodflow_addcount(struct odflow *pflow, struct odflow *psubflow)
{
	struct odflow *_psubflow;

	_psubflow = list_lookup(pflow->subflow, &psubflow->spec);
	if (_psubflow == NULL){
		_psubflow = odflow_alloc();
		if (_psubflow  != NULL){
			if (pflow->subflow == NULL){
				int n = 2;
				pflow->subflow = list_alloc(n); // FIXME parameter optimization
			}
			memcpy(&_psubflow->spec, &psubflow->spec, sizeof(struct odflow_spec));
			_psubflow->af = psubflow->af;
			list_add(pflow->subflow, _psubflow);
		}
	}
	_psubflow->byte += psubflow->byte;
	_psubflow->packet += psubflow->packet;
	
}

void
odflow_reset(void)
{
	hash_reset(ip_hash);
	hash_reset(ip6_hash);
	hash_reset(proto_hash);
}

void
odflow_free(struct odflow* pflow)
{
	if (pflow->cache != NULL)
		list_free(pflow->cache);
	free(pflow);
}

void
odflow_copy(struct odflow *dst, struct odflow *src)
{
	uint64_t i;

	memset(dst, 0, sizeof(struct odflow));
	memcpy(&dst->spec, &src->spec, sizeof(struct odflow_spec));
	dst->packet = src->packet; 
	dst->byte = src->byte; 
	dst->af = src->af; 
	if (src->cache != NULL){
		dst->cache = list_alloc(src->cache->size);
		for (i = 0; i < src->cache->size; i++) {
			dst->cache->list[i] = src->cache->list[i];
			dst->cache->size++;
		}
	}
}

void
odflow_print(struct odflow *pflow)
{
	if (pflow->af == AF_INET) {
		ip_print(pflow->spec.src, pflow->spec.srclen);
		printf(" ");
		ip_print(pflow->spec.dst, pflow->spec.dstlen);
	} 
	else if (pflow->af == AF_INET6) {
		ip6_print(pflow->spec.src, pflow->spec.srclen);
		printf(" ");
		ip6_print(pflow->spec.dst, pflow->spec.dstlen);
	}
	else if (pflow->af == AF_LOCAL) {
		odproto_print(pflow);
	}
	else {
		printf("%s: not support af=%d\n", pflow->af);
		while(1);
	}
}

void
odproto_print(struct odflow *pproto)
{
	int port;

	if (pproto->spec.src[0] == 0)
		printf("*:");
	else
		printf("%d:", pproto->spec.src[0]);
	port = (pproto->spec.src[1] << 8) + pproto->spec.src[2];
	if (port != 0) {
		printf("%d", port);
		if (pproto->spec.srclen < 24) {  /* port range */
			int end = port + (1 << (24 - pproto->spec.srclen)) - 1;
			printf("-%d", end);
		}
	} else
		printf("*");
	printf(":");

	port = (pproto->spec.dst[1] << 8) + pproto->spec.dst[2];
	if (port != 0)  {
		printf("%d", port);
		if (pproto->spec.dstlen < 24) {  /* port range */
			int end = port + (1 << (24 - pproto->spec.dstlen)) - 1;
			printf("-%d", end);
		}
	} else
		printf("*");
}

struct odflow_spec
create_spec(struct odflow_spec *pspec, const int *label, int bytesize)
{
	struct odflow_spec spec; 
	
	memset(&(spec), 0, sizeof(struct odflow_spec));
	spec.srclen = label[0];
	spec.dstlen = label[1];
	prefix_set(pspec->src, spec.srclen, spec.src, bytesize);
	prefix_set(pspec->dst, spec.dstlen, spec.dst, bytesize);

	return (spec);
}

/*
 * create new flow record r1 with specific prefix length
 * based on the flow record r0
 */
void
prefix_set(uint8_t *r0, uint8_t len, uint8_t *r1, int bytesize)
{
	uint8_t bits, bytes = len / 8;
	uint8_t pad = bytesize - bytes;

	bits = len & 7;
	if (bits)
		pad--;

	while (bytes-- != 0)
		*r1++ = *r0++;

	if (bits != 0)
		*r1++= *r0 & prefixmask[bits]; 

	while (pad--)
		*r1++ = 0;
}

/* compare prefixes for the given length */
int
prefix_comp(uint8_t *r, uint8_t *r2, uint8_t len)
{
	uint8_t bytes, bits, mask;

	if (len == 0)
		return (0);

	bytes = len / 8;
	bits = len & 7;

	while (bytes-- != 0) {
		if (*r++ != *r2++)
			return (*--r - *--r2);
	}

	if ((mask = prefixmask[bits]) == 0)
		return (0);

	return ((*r & mask) - (*r2 & mask));
}
