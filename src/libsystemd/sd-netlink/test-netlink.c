/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <net/if.h>
#include <netinet/ether.h>
#include <netinet/in.h>
#include <linux/fou.h>
#include <linux/genetlink.h>
#include <linux/if_macsec.h>
#include <linux/l2tp.h>
#include <linux/nl80211.h>
#include <unistd.h>

#include "sd-netlink.h"

#include "alloc-util.h"
#include "ether-addr-util.h"
#include "macro.h"
#include "netlink-genl.h"
#include "netlink-internal.h"
#include "netlink-util.h"
#include "socket-util.h"
#include "stdio-util.h"
#include "string-util.h"
#include "strv.h"
#include "tests.h"

static void test_message_link_bridge(sd_netlink *rtnl) {
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *message = NULL;
        uint32_t cost;

        log_debug("/* %s */", __func__);

        assert_se(sd_rtnl_message_new_link(rtnl, &message, RTM_NEWLINK, 1) >= 0);
        assert_se(sd_rtnl_message_link_set_family(message, AF_BRIDGE) >= 0);
        assert_se(sd_netlink_message_open_container(message, IFLA_PROTINFO) >= 0);
        assert_se(sd_netlink_message_append_u32(message, IFLA_BRPORT_COST, 10) >= 0);
        assert_se(sd_netlink_message_close_container(message) >= 0);

        assert_se(sd_netlink_message_rewind(message, rtnl) >= 0);

        assert_se(sd_netlink_message_enter_container(message, IFLA_PROTINFO) >= 0);
        assert_se(sd_netlink_message_read_u32(message, IFLA_BRPORT_COST, &cost) >= 0);
        assert_se(cost == 10);
        assert_se(sd_netlink_message_exit_container(message) >= 0);
}

static void test_link_configure(sd_netlink *rtnl, int ifindex) {
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *message = NULL, *reply = NULL;
        uint32_t mtu_out;
        const char *name_out;
        struct ether_addr mac_out;

        log_debug("/* %s */", __func__);

        /* we'd really like to test NEWLINK, but let's not mess with the running kernel */
        assert_se(sd_rtnl_message_new_link(rtnl, &message, RTM_GETLINK, ifindex) >= 0);

        assert_se(sd_netlink_call(rtnl, message, 0, &reply) == 1);

        assert_se(sd_netlink_message_read_string(reply, IFLA_IFNAME, &name_out) >= 0);
        assert_se(sd_netlink_message_read_ether_addr(reply, IFLA_ADDRESS, &mac_out) >= 0);
        assert_se(sd_netlink_message_read_u32(reply, IFLA_MTU, &mtu_out) >= 0);
}

static void test_link_get(sd_netlink *rtnl, int ifindex) {
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *m = NULL, *r = NULL;
        const char *str_data;
        uint8_t u8_data;
        uint32_t u32_data;
        struct ether_addr eth_data;

        log_debug("/* %s */", __func__);

        assert_se(sd_rtnl_message_new_link(rtnl, &m, RTM_GETLINK, ifindex) >= 0);
        assert_se(m);

        assert_se(sd_netlink_call(rtnl, m, 0, &r) == 1);

        assert_se(sd_netlink_message_read_string(r, IFLA_IFNAME, &str_data) == 0);

        assert_se(sd_netlink_message_read_u8(r, IFLA_CARRIER, &u8_data) == 0);
        assert_se(sd_netlink_message_read_u8(r, IFLA_OPERSTATE, &u8_data) == 0);
        assert_se(sd_netlink_message_read_u8(r, IFLA_LINKMODE, &u8_data) == 0);

        assert_se(sd_netlink_message_read_u32(r, IFLA_MTU, &u32_data) == 0);
        assert_se(sd_netlink_message_read_u32(r, IFLA_GROUP, &u32_data) == 0);
        assert_se(sd_netlink_message_read_u32(r, IFLA_TXQLEN, &u32_data) == 0);
        assert_se(sd_netlink_message_read_u32(r, IFLA_NUM_TX_QUEUES, &u32_data) == 0);
        assert_se(sd_netlink_message_read_u32(r, IFLA_NUM_RX_QUEUES, &u32_data) == 0);

        assert_se(sd_netlink_message_read_ether_addr(r, IFLA_ADDRESS, &eth_data) == 0);
}

static void test_address_get(sd_netlink *rtnl, int ifindex) {
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *m = NULL, *r = NULL;
        struct in_addr in_data;
        struct ifa_cacheinfo cache;
        const char *label;

        log_debug("/* %s */", __func__);

        assert_se(sd_rtnl_message_new_addr(rtnl, &m, RTM_GETADDR, ifindex, AF_INET) >= 0);
        assert_se(m);
        assert_se(sd_netlink_message_set_request_dump(m, true) >= 0);
        assert_se(sd_netlink_call(rtnl, m, -1, &r) == 1);

        assert_se(sd_netlink_message_read_in_addr(r, IFA_LOCAL, &in_data) == 0);
        assert_se(sd_netlink_message_read_in_addr(r, IFA_ADDRESS, &in_data) == 0);
        assert_se(sd_netlink_message_read_string(r, IFA_LABEL, &label) == 0);
        assert_se(sd_netlink_message_read_cache_info(r, IFA_CACHEINFO, &cache) == 0);
}

static void test_route(sd_netlink *rtnl) {
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *req = NULL;
        struct in_addr addr, addr_data;
        uint32_t index = 2, u32_data;
        int r;

        log_debug("/* %s */", __func__);

        r = sd_rtnl_message_new_route(rtnl, &req, RTM_NEWROUTE, AF_INET, RTPROT_STATIC);
        if (r < 0) {
                log_error_errno(r, "Could not create RTM_NEWROUTE message: %m");
                return;
        }

        addr.s_addr = htobe32(INADDR_LOOPBACK);

        r = sd_netlink_message_append_in_addr(req, RTA_GATEWAY, &addr);
        if (r < 0) {
                log_error_errno(r, "Could not append RTA_GATEWAY attribute: %m");
                return;
        }

        r = sd_netlink_message_append_u32(req, RTA_OIF, index);
        if (r < 0) {
                log_error_errno(r, "Could not append RTA_OIF attribute: %m");
                return;
        }

        assert_se(sd_netlink_message_rewind(req, rtnl) >= 0);

        assert_se(sd_netlink_message_read_in_addr(req, RTA_GATEWAY, &addr_data) >= 0);
        assert_se(addr_data.s_addr == addr.s_addr);

        assert_se(sd_netlink_message_read_u32(req, RTA_OIF, &u32_data) >= 0);
        assert_se(u32_data == index);

        assert_se((req = sd_netlink_message_unref(req)) == NULL);
}

static void test_multiple(void) {
        sd_netlink *rtnl1, *rtnl2;

        log_debug("/* %s */", __func__);

        assert_se(sd_netlink_open(&rtnl1) >= 0);
        assert_se(sd_netlink_open(&rtnl2) >= 0);

        rtnl1 = sd_netlink_unref(rtnl1);
        rtnl2 = sd_netlink_unref(rtnl2);
}

static int link_handler(sd_netlink *rtnl, sd_netlink_message *m, void *userdata) {
        char *ifname = userdata;
        const char *data;

        assert_se(rtnl);
        assert_se(m);
        assert_se(userdata);

        log_info("%s: got link info about %s", __func__, ifname);
        free(ifname);

        assert_se(sd_netlink_message_read_string(m, IFLA_IFNAME, &data) >= 0);
        assert_se(streq(data, "lo"));

        return 1;
}

static void test_event_loop(int ifindex) {
        _cleanup_(sd_event_unrefp) sd_event *event = NULL;
        _cleanup_(sd_netlink_unrefp) sd_netlink *rtnl = NULL;
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *m = NULL;
        char *ifname;

        log_debug("/* %s */", __func__);

        ifname = strdup("lo2");
        assert_se(ifname);

        assert_se(sd_netlink_open(&rtnl) >= 0);
        assert_se(sd_rtnl_message_new_link(rtnl, &m, RTM_GETLINK, ifindex) >= 0);

        assert_se(sd_netlink_call_async(rtnl, NULL, m, link_handler, NULL, ifname, 0, NULL) >= 0);

        assert_se(sd_event_default(&event) >= 0);

        assert_se(sd_netlink_attach_event(rtnl, event, 0) >= 0);

        assert_se(sd_event_run(event, 0) >= 0);

        assert_se(sd_netlink_detach_event(rtnl) >= 0);

        assert_se((rtnl = sd_netlink_unref(rtnl)) == NULL);
}

static void test_async_destroy(void *userdata) {
}

static void test_async(int ifindex) {
        _cleanup_(sd_netlink_unrefp) sd_netlink *rtnl = NULL;
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *m = NULL, *r = NULL;
        _cleanup_(sd_netlink_slot_unrefp) sd_netlink_slot *slot = NULL;
        sd_netlink_destroy_t destroy_callback;
        const char *description;
        char *ifname;

        log_debug("/* %s */", __func__);

        ifname = strdup("lo");
        assert_se(ifname);

        assert_se(sd_netlink_open(&rtnl) >= 0);

        assert_se(sd_rtnl_message_new_link(rtnl, &m, RTM_GETLINK, ifindex) >= 0);

        assert_se(sd_netlink_call_async(rtnl, &slot, m, link_handler, test_async_destroy, ifname, 0, "hogehoge") >= 0);

        assert_se(sd_netlink_slot_get_netlink(slot) == rtnl);
        assert_se(sd_netlink_slot_get_userdata(slot) == ifname);
        assert_se(sd_netlink_slot_get_destroy_callback(slot, &destroy_callback) == 1);
        assert_se(destroy_callback == test_async_destroy);
        assert_se(sd_netlink_slot_get_floating(slot) == 0);
        assert_se(sd_netlink_slot_get_description(slot, &description) == 1);
        assert_se(streq(description, "hogehoge"));

        assert_se(sd_netlink_wait(rtnl, 0) >= 0);
        assert_se(sd_netlink_process(rtnl, &r) >= 0);

        assert_se((rtnl = sd_netlink_unref(rtnl)) == NULL);
}

static void test_slot_set(int ifindex) {
        _cleanup_(sd_netlink_unrefp) sd_netlink *rtnl = NULL;
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *m = NULL, *r = NULL;
        _cleanup_(sd_netlink_slot_unrefp) sd_netlink_slot *slot = NULL;
        sd_netlink_destroy_t destroy_callback;
        const char *description;
        char *ifname;

        log_debug("/* %s */", __func__);

        ifname = strdup("lo");
        assert_se(ifname);

        assert_se(sd_netlink_open(&rtnl) >= 0);

        assert_se(sd_rtnl_message_new_link(rtnl, &m, RTM_GETLINK, ifindex) >= 0);

        assert_se(sd_netlink_call_async(rtnl, &slot, m, link_handler, NULL, NULL, 0, NULL) >= 0);

        assert_se(sd_netlink_slot_get_netlink(slot) == rtnl);
        assert_se(!sd_netlink_slot_get_userdata(slot));
        assert_se(!sd_netlink_slot_set_userdata(slot, ifname));
        assert_se(sd_netlink_slot_get_userdata(slot) == ifname);
        assert_se(sd_netlink_slot_get_destroy_callback(slot, NULL) == 0);
        assert_se(sd_netlink_slot_set_destroy_callback(slot, test_async_destroy) >= 0);
        assert_se(sd_netlink_slot_get_destroy_callback(slot, &destroy_callback) == 1);
        assert_se(destroy_callback == test_async_destroy);
        assert_se(sd_netlink_slot_get_floating(slot) == 0);
        assert_se(sd_netlink_slot_set_floating(slot, 1) == 1);
        assert_se(sd_netlink_slot_get_floating(slot) == 1);
        assert_se(sd_netlink_slot_get_description(slot, NULL) == 0);
        assert_se(sd_netlink_slot_set_description(slot, "hogehoge") >= 0);
        assert_se(sd_netlink_slot_get_description(slot, &description) == 1);
        assert_se(streq(description, "hogehoge"));

        assert_se(sd_netlink_wait(rtnl, 0) >= 0);
        assert_se(sd_netlink_process(rtnl, &r) >= 0);

        assert_se((rtnl = sd_netlink_unref(rtnl)) == NULL);
}

struct test_async_object {
        unsigned n_ref;
        char *ifname;
};

static struct test_async_object *test_async_object_free(struct test_async_object *t) {
        assert_se(t);

        free(t->ifname);
        return mfree(t);
}

DEFINE_PRIVATE_TRIVIAL_REF_UNREF_FUNC(struct test_async_object, test_async_object, test_async_object_free);
DEFINE_TRIVIAL_CLEANUP_FUNC(struct test_async_object *, test_async_object_unref);

static int link_handler2(sd_netlink *rtnl, sd_netlink_message *m, void *userdata) {
        struct test_async_object *t = userdata;
        const char *data;

        assert_se(rtnl);
        assert_se(m);
        assert_se(userdata);

        log_info("%s: got link info about %s", __func__, t->ifname);

        assert_se(sd_netlink_message_read_string(m, IFLA_IFNAME, &data) >= 0);
        assert_se(streq(data, "lo"));

        return 1;
}

static void test_async_object_destroy(void *userdata) {
        struct test_async_object *t = userdata;

        assert_se(userdata);

        log_info("%s: n_ref=%u", __func__, t->n_ref);
        test_async_object_unref(t);
}

static void test_async_destroy_callback(int ifindex) {
        _cleanup_(sd_netlink_unrefp) sd_netlink *rtnl = NULL;
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *m = NULL, *r = NULL;
        _cleanup_(test_async_object_unrefp) struct test_async_object *t = NULL;
        _cleanup_(sd_netlink_slot_unrefp) sd_netlink_slot *slot = NULL;
        char *ifname;

        log_debug("/* %s */", __func__);

        assert_se(t = new(struct test_async_object, 1));
        assert_se(ifname = strdup("lo"));
        *t = (struct test_async_object) {
                .n_ref = 1,
                .ifname = ifname,
        };

        assert_se(sd_netlink_open(&rtnl) >= 0);

        /* destroy callback is called after processing message */
        assert_se(sd_rtnl_message_new_link(rtnl, &m, RTM_GETLINK, ifindex) >= 0);
        assert_se(sd_netlink_call_async(rtnl, NULL, m, link_handler2, test_async_object_destroy, t, 0, NULL) >= 0);

        assert_se(t->n_ref == 1);
        assert_se(test_async_object_ref(t));
        assert_se(t->n_ref == 2);

        assert_se(sd_netlink_wait(rtnl, 0) >= 0);
        assert_se(sd_netlink_process(rtnl, &r) == 1);
        assert_se(t->n_ref == 1);

        assert_se(!sd_netlink_message_unref(m));

        /* destroy callback is called when asynchronous call is cancelled, that is, slot is freed. */
        assert_se(sd_rtnl_message_new_link(rtnl, &m, RTM_GETLINK, ifindex) >= 0);
        assert_se(sd_netlink_call_async(rtnl, &slot, m, link_handler2, test_async_object_destroy, t, 0, NULL) >= 0);

        assert_se(t->n_ref == 1);
        assert_se(test_async_object_ref(t));
        assert_se(t->n_ref == 2);

        assert_se(!(slot = sd_netlink_slot_unref(slot)));
        assert_se(t->n_ref == 1);

        assert_se(!sd_netlink_message_unref(m));

        /* destroy callback is also called by sd_netlink_unref() */
        assert_se(sd_rtnl_message_new_link(rtnl, &m, RTM_GETLINK, ifindex) >= 0);
        assert_se(sd_netlink_call_async(rtnl, NULL, m, link_handler2, test_async_object_destroy, t, 0, NULL) >= 0);

        assert_se(t->n_ref == 1);
        assert_se(test_async_object_ref(t));
        assert_se(t->n_ref == 2);

        assert_se((rtnl = sd_netlink_unref(rtnl)) == NULL);
        assert_se(t->n_ref == 1);
}

static int pipe_handler(sd_netlink *rtnl, sd_netlink_message *m, void *userdata) {
        int *counter = userdata;
        int r;

        (*counter)--;

        r = sd_netlink_message_get_errno(m);

        log_info_errno(r, "%d left in pipe. got reply: %m", *counter);

        assert_se(r >= 0);

        return 1;
}

static void test_pipe(int ifindex) {
        _cleanup_(sd_netlink_unrefp) sd_netlink *rtnl = NULL;
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *m1 = NULL, *m2 = NULL;
        int counter = 0;

        log_debug("/* %s */", __func__);

        assert_se(sd_netlink_open(&rtnl) >= 0);

        assert_se(sd_rtnl_message_new_link(rtnl, &m1, RTM_GETLINK, ifindex) >= 0);
        assert_se(sd_rtnl_message_new_link(rtnl, &m2, RTM_GETLINK, ifindex) >= 0);

        counter++;
        assert_se(sd_netlink_call_async(rtnl, NULL, m1, pipe_handler, NULL, &counter, 0, NULL) >= 0);

        counter++;
        assert_se(sd_netlink_call_async(rtnl, NULL, m2, pipe_handler, NULL, &counter, 0, NULL) >= 0);

        while (counter > 0) {
                assert_se(sd_netlink_wait(rtnl, 0) >= 0);
                assert_se(sd_netlink_process(rtnl, NULL) >= 0);
        }

        assert_se((rtnl = sd_netlink_unref(rtnl)) == NULL);
}

static void test_container(sd_netlink *rtnl) {
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *m = NULL;
        uint16_t u16_data;
        uint32_t u32_data;
        const char *string_data;

        log_debug("/* %s */", __func__);

        assert_se(sd_rtnl_message_new_link(rtnl, &m, RTM_NEWLINK, 0) >= 0);

        assert_se(sd_netlink_message_open_container(m, IFLA_LINKINFO) >= 0);
        assert_se(sd_netlink_message_open_container_union(m, IFLA_INFO_DATA, "vlan") >= 0);
        assert_se(sd_netlink_message_append_u16(m, IFLA_VLAN_ID, 100) >= 0);
        assert_se(sd_netlink_message_close_container(m) >= 0);
        assert_se(sd_netlink_message_append_string(m, IFLA_INFO_KIND, "vlan") >= 0);
        assert_se(sd_netlink_message_close_container(m) >= 0);
        assert_se(sd_netlink_message_close_container(m) == -EINVAL);

        assert_se(sd_netlink_message_rewind(m, rtnl) >= 0);

        assert_se(sd_netlink_message_enter_container(m, IFLA_LINKINFO) >= 0);
        assert_se(sd_netlink_message_read_string(m, IFLA_INFO_KIND, &string_data) >= 0);
        assert_se(streq("vlan", string_data));

        assert_se(sd_netlink_message_enter_container(m, IFLA_INFO_DATA) >= 0);
        assert_se(sd_netlink_message_read_u16(m, IFLA_VLAN_ID, &u16_data) >= 0);
        assert_se(sd_netlink_message_exit_container(m) >= 0);

        assert_se(sd_netlink_message_read_string(m, IFLA_INFO_KIND, &string_data) >= 0);
        assert_se(streq("vlan", string_data));
        assert_se(sd_netlink_message_exit_container(m) >= 0);

        assert_se(sd_netlink_message_read_u32(m, IFLA_LINKINFO, &u32_data) < 0);

        assert_se(sd_netlink_message_exit_container(m) == -EINVAL);
}

static void test_match(void) {
        _cleanup_(sd_netlink_slot_unrefp) sd_netlink_slot *s1 = NULL, *s2 = NULL;
        _cleanup_(sd_netlink_unrefp) sd_netlink *rtnl = NULL;

        log_debug("/* %s */", __func__);

        assert_se(sd_netlink_open(&rtnl) >= 0);

        assert_se(sd_netlink_add_match(rtnl, &s1, RTM_NEWLINK, link_handler, NULL, NULL, NULL) >= 0);
        assert_se(sd_netlink_add_match(rtnl, &s2, RTM_NEWLINK, link_handler, NULL, NULL, NULL) >= 0);
        assert_se(sd_netlink_add_match(rtnl, NULL, RTM_NEWLINK, link_handler, NULL, NULL, NULL) >= 0);

        assert_se(!(s1 = sd_netlink_slot_unref(s1)));
        assert_se(!(s2 = sd_netlink_slot_unref(s2)));

        assert_se((rtnl = sd_netlink_unref(rtnl)) == NULL);
}

static void test_get_addresses(sd_netlink *rtnl) {
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *req = NULL, *reply = NULL;
        sd_netlink_message *m;

        log_debug("/* %s */", __func__);

        assert_se(sd_rtnl_message_new_addr(rtnl, &req, RTM_GETADDR, 0, AF_UNSPEC) >= 0);
        assert_se(sd_netlink_message_set_request_dump(req, true) >= 0);
        assert_se(sd_netlink_call(rtnl, req, 0, &reply) >= 0);

        for (m = reply; m; m = sd_netlink_message_next(m)) {
                uint16_t type;
                unsigned char scope, flags;
                int family, ifindex;

                assert_se(sd_netlink_message_get_type(m, &type) >= 0);
                assert_se(type == RTM_NEWADDR);

                assert_se(sd_rtnl_message_addr_get_ifindex(m, &ifindex) >= 0);
                assert_se(sd_rtnl_message_addr_get_family(m, &family) >= 0);
                assert_se(sd_rtnl_message_addr_get_scope(m, &scope) >= 0);
                assert_se(sd_rtnl_message_addr_get_flags(m, &flags) >= 0);

                assert_se(ifindex > 0);
                assert_se(IN_SET(family, AF_INET, AF_INET6));

                log_info("got IPv%i address on ifindex %i", family == AF_INET ? 4 : 6, ifindex);
        }
}

static void test_message(sd_netlink *rtnl) {
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *m = NULL;

        log_debug("/* %s */", __func__);

        assert_se(message_new_synthetic_error(rtnl, -ETIMEDOUT, 1, &m) >= 0);
        assert_se(sd_netlink_message_get_errno(m) == -ETIMEDOUT);
}

static void test_array(void) {
        _cleanup_(sd_netlink_unrefp) sd_netlink *genl = NULL;
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *m = NULL;

        log_debug("/* %s */", __func__);

        assert_se(sd_genl_socket_open(&genl) >= 0);
        assert_se(sd_genl_message_new(genl, CTRL_GENL_NAME, CTRL_CMD_GETFAMILY, &m) >= 0);

        assert_se(sd_netlink_message_open_container(m, CTRL_ATTR_MCAST_GROUPS) >= 0);
        for (unsigned i = 0; i < 10; i++) {
                char name[STRLEN("hoge") + DECIMAL_STR_MAX(uint32_t)];
                uint32_t id = i + 1000;

                xsprintf(name, "hoge%" PRIu32, id);
                assert_se(sd_netlink_message_open_array(m, i + 1) >= 0);
                assert_se(sd_netlink_message_append_u32(m, CTRL_ATTR_MCAST_GRP_ID, id) >= 0);
                assert_se(sd_netlink_message_append_string(m, CTRL_ATTR_MCAST_GRP_NAME, name) >= 0);
                assert_se(sd_netlink_message_close_container(m) >= 0);
        }
        assert_se(sd_netlink_message_close_container(m) >= 0);

        message_seal(m);
        assert_se(sd_netlink_message_rewind(m, genl) >= 0);

        assert_se(sd_netlink_message_enter_container(m, CTRL_ATTR_MCAST_GROUPS) >= 0);
        for (unsigned i = 0; i < 10; i++) {
                char expected[STRLEN("hoge") + DECIMAL_STR_MAX(uint32_t)];
                const char *name;
                uint32_t id;

                assert_se(sd_netlink_message_enter_array(m, i + 1) >= 0);
                assert_se(sd_netlink_message_read_u32(m, CTRL_ATTR_MCAST_GRP_ID, &id) >= 0);
                assert_se(sd_netlink_message_read_string(m, CTRL_ATTR_MCAST_GRP_NAME, &name) >= 0);
                assert_se(sd_netlink_message_exit_container(m) >= 0);

                assert_se(id == i + 1000);
                xsprintf(expected, "hoge%" PRIu32, id);
                assert_se(streq(name, expected));
        }
        assert_se(sd_netlink_message_exit_container(m) >= 0);
}

static void test_strv(sd_netlink *rtnl) {
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *m = NULL;
        _cleanup_strv_free_ char **names_in = NULL, **names_out;
        const char *p;

        log_debug("/* %s */", __func__);

        assert_se(sd_rtnl_message_new_link(rtnl, &m, RTM_NEWLINKPROP, 1) >= 0);

        for (unsigned i = 0; i < 10; i++) {
                char name[STRLEN("hoge") + DECIMAL_STR_MAX(uint32_t)];

                xsprintf(name, "hoge%" PRIu32, i + 1000);
                assert_se(strv_extend(&names_in, name) >= 0);
        }

        assert_se(sd_netlink_message_open_container(m, IFLA_PROP_LIST) >= 0);
        assert_se(sd_netlink_message_append_strv(m, IFLA_ALT_IFNAME, (const char**) names_in) >= 0);
        assert_se(sd_netlink_message_close_container(m) >= 0);

        message_seal(m);
        assert_se(sd_netlink_message_rewind(m, rtnl) >= 0);

        assert_se(sd_netlink_message_read_strv(m, IFLA_PROP_LIST, IFLA_ALT_IFNAME, &names_out) >= 0);
        assert_se(strv_equal(names_in, names_out));

        assert_se(sd_netlink_message_enter_container(m, IFLA_PROP_LIST) >= 0);
        assert_se(sd_netlink_message_read_string(m, IFLA_ALT_IFNAME, &p) >= 0);
        assert_se(streq(p, "hoge1009"));
        assert_se(sd_netlink_message_exit_container(m) >= 0);
}

static int genl_ctrl_match_callback(sd_netlink *genl, sd_netlink_message *m, void *userdata) {
        const char *name;
        uint16_t id;
        uint8_t cmd;

        assert_se(genl);
        assert_se(m);

        assert_se(sd_genl_message_get_family_name(genl, m, &name) >= 0);
        assert_se(streq(name, CTRL_GENL_NAME));

        assert_se(sd_genl_message_get_command(genl, m, &cmd) >= 0);

        switch (cmd) {
        case CTRL_CMD_NEWFAMILY:
        case CTRL_CMD_DELFAMILY:
                assert_se(sd_netlink_message_read_string(m, CTRL_ATTR_FAMILY_NAME, &name) >= 0);
                assert_se(sd_netlink_message_read_u16(m, CTRL_ATTR_FAMILY_ID, &id) >= 0);
                log_debug("%s: %s (id=%"PRIu16") family is %s.",
                          __func__, name, id, cmd == CTRL_CMD_NEWFAMILY ? "added" : "removed");
                break;
        case CTRL_CMD_NEWMCAST_GRP:
        case CTRL_CMD_DELMCAST_GRP:
                assert_se(sd_netlink_message_read_string(m, CTRL_ATTR_FAMILY_NAME, &name) >= 0);
                assert_se(sd_netlink_message_read_u16(m, CTRL_ATTR_FAMILY_ID, &id) >= 0);
                log_debug("%s: multicast group for %s (id=%"PRIu16") family is %s.",
                          __func__, name, id, cmd == CTRL_CMD_NEWMCAST_GRP ? "added" : "removed");
                break;
        default:
                log_debug("%s: received nlctrl message with unknown command '%"PRIu8"'.", __func__, cmd);
        }

        return 0;
}

static void test_genl(void) {
        _cleanup_(sd_event_unrefp) sd_event *event = NULL;
        _cleanup_(sd_netlink_unrefp) sd_netlink *genl = NULL;
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *m = NULL;
        const char *name;
        uint8_t cmd;
        int r;

        log_debug("/* %s */", __func__);

        assert_se(sd_genl_socket_open(&genl) >= 0);
        assert_se(sd_event_default(&event) >= 0);
        assert_se(sd_netlink_attach_event(genl, event, 0) >= 0);

        assert_se(sd_genl_message_new(genl, CTRL_GENL_NAME, CTRL_CMD_GETFAMILY, &m) >= 0);
        assert_se(sd_genl_message_get_family_name(genl, m, &name) >= 0);
        assert_se(streq(name, CTRL_GENL_NAME));
        assert_se(sd_genl_message_get_command(genl, m, &cmd) >= 0);
        assert_se(cmd == CTRL_CMD_GETFAMILY);

        assert_se(sd_genl_add_match(genl, NULL, CTRL_GENL_NAME, "notify", 0, genl_ctrl_match_callback, NULL, NULL, "genl-ctrl-notify") >= 0);

        m = sd_netlink_message_unref(m);
        assert_se(sd_genl_message_new(genl, "should-not-exist", CTRL_CMD_GETFAMILY, &m) < 0);
        assert_se(sd_genl_message_new(genl, "should-not-exist", CTRL_CMD_GETFAMILY, &m) == -EOPNOTSUPP);

        /* These families may not be supported by kernel. Hence, ignore results. */
        (void) sd_genl_message_new(genl, FOU_GENL_NAME, 0, &m);
        m = sd_netlink_message_unref(m);
        (void) sd_genl_message_new(genl, L2TP_GENL_NAME, 0, &m);
        m = sd_netlink_message_unref(m);
        (void) sd_genl_message_new(genl, MACSEC_GENL_NAME, 0, &m);
        m = sd_netlink_message_unref(m);
        (void) sd_genl_message_new(genl, NL80211_GENL_NAME, 0, &m);
        m = sd_netlink_message_unref(m);
        (void) sd_genl_message_new(genl, NETLBL_NLTYPE_UNLABELED_NAME, 0, &m);

        for (;;) {
                r = sd_event_run(event, 500 * USEC_PER_MSEC);
                assert_se(r >= 0);
                if (r == 0)
                        return;
        }
}

static void test_rtnl_set_link_name(sd_netlink *rtnl, int ifindex) {
        _cleanup_strv_free_ char **alternative_names = NULL;
        int r;

        log_debug("/* %s */", __func__);

        if (geteuid() != 0)
                return (void) log_tests_skipped("not root");

        /* Test that the new name (which is currently an alternative name) is
         * restored as an alternative name on error. Create an error by using
         * an invalid device name, namely one that exceeds IFNAMSIZ
         * (alternative names can exceed IFNAMSIZ, but not regular names). */
        r = rtnl_set_link_alternative_names(&rtnl, ifindex, STRV_MAKE("testlongalternativename"));
        if (r == -EPERM)
                return (void) log_tests_skipped("missing required capabilities");

        assert_se(r >= 0);
        assert_se(rtnl_set_link_name(&rtnl, ifindex, "testlongalternativename") == -EINVAL);
        assert_se(rtnl_get_link_alternative_names(&rtnl, ifindex, &alternative_names) >= 0);
        assert_se(strv_contains(alternative_names, "testlongalternativename"));
        assert_se(rtnl_delete_link_alternative_names(&rtnl, ifindex, STRV_MAKE("testlongalternativename")) >= 0);
}

int main(void) {
        sd_netlink *rtnl;
        sd_netlink_message *m;
        sd_netlink_message *r;
        const char *string_data;
        int if_loopback;
        uint16_t type;

        test_setup_logging(LOG_DEBUG);

        test_match();
        test_multiple();

        assert_se(sd_netlink_open(&rtnl) >= 0);
        assert_se(rtnl);

        test_route(rtnl);
        test_message(rtnl);
        test_container(rtnl);
        test_array();
        test_strv(rtnl);

        if_loopback = (int) if_nametoindex("lo");
        assert_se(if_loopback > 0);

        test_async(if_loopback);
        test_slot_set(if_loopback);
        test_async_destroy_callback(if_loopback);
        test_pipe(if_loopback);
        test_event_loop(if_loopback);
        test_link_configure(rtnl, if_loopback);
        test_rtnl_set_link_name(rtnl, if_loopback);

        test_get_addresses(rtnl);
        test_message_link_bridge(rtnl);

        assert_se(sd_rtnl_message_new_link(rtnl, &m, RTM_GETLINK, if_loopback) >= 0);
        assert_se(m);

        assert_se(sd_netlink_message_get_type(m, &type) >= 0);
        assert_se(type == RTM_GETLINK);

        assert_se(sd_netlink_message_read_string(m, IFLA_IFNAME, &string_data) == -EPERM);

        assert_se(sd_netlink_call(rtnl, m, 0, &r) == 1);
        assert_se(sd_netlink_message_get_type(r, &type) >= 0);
        assert_se(type == RTM_NEWLINK);

        assert_se((r = sd_netlink_message_unref(r)) == NULL);

        assert_se(sd_netlink_call(rtnl, m, -1, &r) == -EPERM);
        assert_se((m = sd_netlink_message_unref(m)) == NULL);
        assert_se((r = sd_netlink_message_unref(r)) == NULL);

        test_link_get(rtnl, if_loopback);
        test_address_get(rtnl, if_loopback);

        assert_se((m = sd_netlink_message_unref(m)) == NULL);
        assert_se((r = sd_netlink_message_unref(r)) == NULL);
        assert_se((rtnl = sd_netlink_unref(rtnl)) == NULL);

        test_genl();

        return EXIT_SUCCESS;
}
