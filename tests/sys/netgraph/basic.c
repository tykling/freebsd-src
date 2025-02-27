/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2021 Lutz Donnerhacke
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <atf-c.h>
#include <errno.h>
#include <stdio.h>

#include "util.h"

static void get_data(void *data, size_t len, void *ctx);

ATF_TC(send_recv);
ATF_TC_HEAD(send_recv, conf)
{
	atf_tc_set_md_var(conf, "require.user", "root");
}
ATF_TC_BODY(send_recv, dummy)
{
	char msg[] = "test";
	int received;

	ng_init();
	ng_connect(".", "a", ".", "b");
	ng_register_data("b", get_data);
	ng_send_data("a", msg, sizeof(msg));

	received = 0;
	ng_handle_events(50, &received);
	ATF_CHECK(received == 1);
}

ATF_TC(node);
ATF_TC_HEAD(node, conf)
{
	atf_tc_set_md_var(conf, "require.user", "root");
}
ATF_TC_BODY(node, dummy)
{
	char msg[] = "test";
	int received;

	ng_init();
	ng_mkpeer(".", "a", "hub", "a");
	ng_name("a", "test hub");


	ng_connect(".", "b", "test hub:", "b");
	ng_connect(".", "c", "test hub:", "c");
	ng_register_data("a", get_data);
	ng_register_data("b", get_data);
	ng_register_data("c", get_data);

	received = 0;
	ng_send_data("a", msg, sizeof(msg));
	ng_handle_events(50, &received);
	ATF_CHECK(received == 2);

	ng_rmhook(".", "b");
	received = 0;
	ng_send_data("a", msg, sizeof(msg));
	ng_handle_events(50, &received);
	ATF_CHECK(received == 1);

	ng_shutdown("test hub:");
}

ATF_TC(message);
ATF_TC_HEAD(message, conf)
{
	atf_tc_set_md_var(conf, "require.user", "root");
}
ATF_TC_BODY(message, dummy)
{
	ng_init();
	ng_mkpeer(".", "a", "hub", "a");
	ng_name("a", "test hub");

	ng_send_msg("test hub:", "setpersistent");
	ng_rmhook(".", "a");

	ng_shutdown("test hub:");
}

ATF_TC(same_name);
ATF_TC_HEAD(same_name, conf)
{
	atf_tc_set_md_var(conf, "require.user", "root");
}
ATF_TC_BODY(same_name, dummy)
{
	ng_init();
	ng_mkpeer(".", "a", "hub", "a");
	ng_name("a", "test");

	ng_errors(PASS);
	ng_connect(".", "a", ".", "b");
	ATF_CHECK_ERRNO(EEXIST, 1);
	ng_connect(".", "b", ".", "b");
	ATF_CHECK_ERRNO(EEXIST, 1);
	ng_name(".", "test");
	ATF_CHECK_ERRNO(EADDRINUSE, 1);

	ng_errors(FAIL);
	ng_shutdown("test:");
}

ATF_TC(queuelimit);
ATF_TC_HEAD(queuelimit, conf)
{
	atf_tc_set_md_var(conf, "require.user", "root");
}
ATF_TC_BODY(queuelimit, dummy)
{
	int received, i;
	char msg[] = "test";
	const int MAX = 1000;

	ng_init();
	ng_connect(".", "a", ".", "b");
	ng_register_data("b", get_data);

	ng_errors(PASS);
	for (i = 0; i < MAX; i++) {
		ng_send_data("a", msg, sizeof(msg));
		if (errno != 0)
			break;
		/* no ng_handle_events -> messages stall */
	}
	ng_errors(FAIL);
	printf("queued %d\n", i);
	sleep(3);

	received = 0;
	ng_handle_events(50, &received);
	ATF_CHECK(received > 100);
	ATF_CHECK(received == i);
	atf_tc_expect_fail("Queue full (%d)", i);
	ATF_CHECK(received == MAX);
	atf_tc_expect_pass();
}

ATF_TP_ADD_TCS(basic)
{
	ATF_TP_ADD_TC(basic, send_recv);
	ATF_TP_ADD_TC(basic, node);
	ATF_TP_ADD_TC(basic, message);
	ATF_TP_ADD_TC(basic, same_name);
	ATF_TP_ADD_TC(basic, queuelimit);

	return atf_no_error();
}

static void
get_data(void *data, size_t len, void *ctx)
{
	int *cnt = ctx;

	(void)data;
	printf("Got %zu bytes of data.\n", len);
	(*cnt)++;
}
