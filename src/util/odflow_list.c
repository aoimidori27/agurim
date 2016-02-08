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

#include "odflow_list.h"

struct odflow_list *
list_alloc(int size)
{
	struct odflow_list *plist;

	if (size == 0){
		printf("XXX arienai %d\n", size);
		while (1);
	}
	plist = malloc(sizeof(struct odflow_list));
	if (plist == NULL) 
		goto end;

	memset(plist, 0, sizeof(struct odflow_list));
	plist->list = calloc(1, sizeof(struct odflow *) * size);
	if (plist->list == NULL) 
		goto err;
	plist->size = 0;
	plist->max_size = size;
end:
	return plist;
err:
	free(plist);
	goto end;
}

void
list_free(struct odflow_list *plist)
{
#if 0
	uint64_t i;
	for (i = 0; i < plist->size; i++) {
		plist->list[i] = NULL; 
	}
#endif
	free(plist->list);
	plist->size = 0;
	free(plist);
}

int
list_add(struct odflow_list *plist, struct odflow* pflow)
{
	int ret = -1;

	if (plist->size == plist->max_size) {
		/* if full, double the size */
		int newsize;

		newsize = plist->max_size * 2;

		plist->list = realloc(plist->list, sizeof(struct odflow *) * newsize);
		if (plist->list == NULL)
			goto end;

		plist->max_size = newsize;
	}

	plist->list[plist->size] = pflow;
	plist->size++;
	ret = 0;
end:
	return ret;
}

struct odflow*
list_lookup(struct odflow_list *plist, struct odflow_spec *pspec)
{
	struct odflow *pflow = NULL;
	uint64_t i;
	
	for (i = 0; i < plist->size; i++) {
                if (!memcmp(pspec, &(pflow->spec), sizeof(struct odflow_spec))) {
			break;
		}
	}
	return pflow;
}
