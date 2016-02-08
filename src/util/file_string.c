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
#define _XOPEN_SOURCE
#include <time.h>
#include <ctype.h>

#include "file_string.h"
#include "../agurim_param.h"

static int create_ip(char *buf, void *ip, uint8_t *prefixlen);
static int
create_port(char *proto_buf, char *port_buf, uint8_t *port, uint8_t *portlen);

static time_t parse_time(char *buf);

void
ip_print(uint8_t *ip, uint8_t len)
{
	char buf[BUFSIZ];

	if (len == 0)
		printf("*");
	else {
		inet_ntop(AF_INET, ip, buf, BUFSIZ);
		if (len < 32)
			printf("%s/%u", buf, len);
		else
			printf("%s", buf);
	}
}

void
ip6_print(uint8_t *ip6, uint8_t len)
{
	char buf[BUFSIZ];

	if (len == 0)
		printf("*::");
	else {
		inet_ntop(AF_INET6, ip6, buf, BUFSIZ);
	if (len < 128)
		printf("%s/%u", buf, len);
	else
		printf("%s", buf);
	}
}

void
proto_print(uint8_t proto)
{
	if (proto == 0)
		printf("*");
	else
		printf("%d", proto);
}

void
port_print(uint8_t *pport, uint8_t len)
{
	uint8_t port;

	port = (pport[1] << 8) + pport[2];
	if (port != 0) {
		printf("%d", port);
		if (len < 24) {  /* port range */
			int end = port + (1 << (24 - len)) - 1;
			printf("-%d", end);
		}
	} else
		printf("*");
}

/*
 * read start time and end time in the preamble.
 * also produce output at the end of the current period.
 */
int
is_preamble(char *buf, int *exit_flg, int *agr_flg)
{
	if (buf[0] == '\0' || buf[0] == '#')
		return 1;

	/* parse agurim prefixes in logs */
	if (buf[0] == '%') {
		if (!strncmp("StartTime:", &buf[2], 10)) {
			time_t t = parse_time(buf);
			param_set_starttime(t, exit_flg, agr_flg);
		}
		else if (!strncmp("EndTime:", &buf[2], 8)) {
			time_t t = parse_time(buf);
			param_set_endtime(t);
		}
		return 1;
	}
	/* address line starts with "[rank]" */
	if (buf[0] != '[')
		return 1;

	/* no more processing is allowed due to user filter */
	if (inparam.start_time == 0)
		return 1;

	return 0;
}

/*
 * parse src_ip and dst_ip bytes packets from a src-dst pair line, e.g.,
 * [ 8] 10.178.141.0/24 *: 21817049 (3.19%) 17852 (1.21%)
 * [39] *:: 2001:df0:2ed:::13: 979274 (0.15%)  901 (0.06%)
 */
int
is_ip(char *buf, struct odflow *pflow)
{
	char s_addr[64], src[32], dst[32];
	double _dummy1, _dummy2;
	int i, n;
	char *cp;

	memset(pflow, 0, sizeof(struct odflow));

	cp = buf;
	while (isspace(*cp)){
		cp++;
	}

	/* parse packet and byte count */
	n = sscanf(cp, "[%02d] %[^:]: %d (%lf%%)\t%d (%lf)", 
		&i, s_addr, (int*)&pflow->byte, &_dummy1, (int*)&pflow->packet, &_dummy2);
	if (n != 6)
		goto err;	/* too few for a flow entry */

	/* parse src and dst ip */
	n = sscanf(s_addr, "%s %s", src, dst);
	if (n != 2)
		goto err;	/* too few for a flow entry */

	pflow->af = create_ip(src, &pflow->spec.src, &pflow->spec.srclen);
	
	if (create_ip(dst, &pflow->spec.dst, &pflow->spec.dstlen) != pflow->af)
		goto err;

	return 1;
err:
	return 0;
}

/*
 * parse the protocol spec: e.g.,
 *	[6:80:*]92.8% 77.0% [6:443:49152-49279]1.9% 4.6%
 */
int
is_proto(char *buf, uint64_t byte, uint64_t packet, struct odflow *pproto)
{
	char s_proto[8], s_sport[16], s_dport[16];
	double fbyte, fpacket;
	int n = 0, ncp, m;
	char *cp;

	memset(pproto, 0, sizeof(struct odflow));

	cp = buf;
	while (isspace(*cp)){
		cp++;
	}

	/* parse packet and byte count */
	while (1) {
    		m = sscanf(cp, "[%[^':']:%[^':']:%[^]]]%lf%% %lf%%%n", 
			s_proto, s_sport, s_dport, &fbyte, &fpacket, &ncp);
		if (m != 5)	break;
		memset(&pproto[n], 0 , sizeof(struct odflow));
		pproto[n].af = AF_LOCAL;
		create_port(s_proto, s_sport, pproto[n].spec.src, &pproto[n].spec.srclen);
		create_port(s_proto, s_dport, pproto[n].spec.dst, &pproto[n].spec.dstlen);
		pproto[n].byte = fbyte * byte / 100;
		pproto[n].packet = fpacket * packet / 100;
		n++;
		cp += ncp;
		cp++; // remove space
	}
	return n;
}

static int
create_ip(char *buf, void *ip, uint8_t *prefixlen)
{
	char *cp, *ap;
	uint8_t len;
	int i, af = AF_UNSPEC;

	cp = buf;
	if (cp[0] == '*') {
		if (cp[1] == ':' && cp[2] == ':') {
			/* "*::" is the wildcard for IPv6 */
			af = AF_INET6;
			ap = "::";
			len = 0;
		} else {
			/* "*" is the wildcard for IPv4 */
			af = AF_INET;
			ap = "0.0.0.0";
			len = 0;
		}
	} else {
		ap = cp;
		/* check the first 5 chars for address family (v4 or v6) */
		for (i = 1; i < 5; i++) {
			if (cp[i] == '.') {
				af = AF_INET;
				break;
			} else if (cp[i] == ':') {
				af = AF_INET6;
				break;
			}
		}
		if (af == AF_UNSPEC)
			return (-1);

		if ((cp = strchr(ap, '/')) != NULL) {
			*cp++ = '\0';
			len = strtol(cp, NULL, 10);
		} else {
			if (af == AF_INET)
				len = 32;
			else if (af == AF_INET6)
				len = 128;
		}
	}
	if (inet_pton(af, ap, ip) < 0)
		return (-1);
	*prefixlen = len;

	return (af);
}


static int
create_port(char *proto_buf, char *port_buf, uint8_t *port, uint8_t *portlen)
{
	char *cp, *sp;
	long val;

	/* protocol */
	sp = proto_buf;
	if (sp[0] == '*')
		port[0] = 0;	/* note: no prefix notation for protocol */
	else 
		port[0] = atoi(sp);

	/* port */
	sp = port_buf;
	cp = strsep(&sp, "-");
	if (sp != NULL) {
		/* port range */
		uint16_t end;

		val = strtol(cp, NULL, 10);
		port[1] = val >> 8;
		port[2] = val & 0xff;
		end = strtol(sp, NULL, 10);
		*portlen = 8 + 17 - ffs(end - val + 1);
	} else {
		/* single port */
		val = strtol(port_buf, NULL, 10);
		if (val == 0) {
			if (port[0] == 0)
				*portlen = 0;
			else
				*portlen = 8;
		} else {
			port[1] = val >> 8;
			port[2] = val & 0xff;
			*portlen = 24;
		}
	}
	
	return AF_LOCAL;
}

#if 0
static int
match_filter(struct odflow_spec *odfsp)
{
	int len, len2;

	if (odfsp->srclen < query.f.srclen || odfsp->dstlen < query.f.dstlen)
		return (0);
	len = prefix_comp(odfsp->src, query.f.src, query.f.srclen); 
	if (len != 0)
		return (0);
	len2 = prefix_comp(odfsp->dst, query.f.dst, query.f.dstlen);
	if (len2 != 0)
		return (0);
	return (1);
}
#endif

static time_t
parse_time(char *buf)
{
        struct tm tm;
	char *cp;
	time_t t;

	cp = strchr(&buf[2], ':');
	cp++;
	cp += strspn(cp, " \t");

	memset(&tm, 0, sizeof(tm));

	if (!strptime(cp, "%a %b %d %T %Y", &tm))
		fprintf(stderr, "date format is incorrect.");

	if ((t = mktime(&tm)) < 0)
		fprintf(stderr, "mktime failed.");

	return t;
}
