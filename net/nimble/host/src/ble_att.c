/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <stddef.h>
#include <errno.h>
#include "bsp/bsp.h"
#include "ble_hs_priv.h"

static bssnz_t uint8_t ble_att_flat_buf[BLE_ATT_ATTR_MAX_LEN];

static uint16_t ble_att_preferred_mtu;

/** Dispatch table for incoming ATT requests.  Sorted by op code. */
typedef int ble_att_rx_fn(uint16_t conn_handle, struct os_mbuf **om);
struct ble_att_rx_dispatch_entry {
    uint8_t bde_op;
    ble_att_rx_fn *bde_fn;
};

/** Dispatch table for incoming ATT commands.  Must be ordered by op code. */
static const struct ble_att_rx_dispatch_entry ble_att_rx_dispatch[] = {
    { BLE_ATT_OP_ERROR_RSP,            ble_att_clt_rx_error },
    { BLE_ATT_OP_MTU_REQ,              ble_att_svr_rx_mtu },
    { BLE_ATT_OP_MTU_RSP,              ble_att_clt_rx_mtu },
    { BLE_ATT_OP_FIND_INFO_REQ,        ble_att_svr_rx_find_info },
    { BLE_ATT_OP_FIND_INFO_RSP,        ble_att_clt_rx_find_info },
    { BLE_ATT_OP_FIND_TYPE_VALUE_REQ,  ble_att_svr_rx_find_type_value },
    { BLE_ATT_OP_FIND_TYPE_VALUE_RSP,  ble_att_clt_rx_find_type_value },
    { BLE_ATT_OP_READ_TYPE_REQ,        ble_att_svr_rx_read_type },
    { BLE_ATT_OP_READ_TYPE_RSP,        ble_att_clt_rx_read_type },
    { BLE_ATT_OP_READ_REQ,             ble_att_svr_rx_read },
    { BLE_ATT_OP_READ_RSP,             ble_att_clt_rx_read },
    { BLE_ATT_OP_READ_BLOB_REQ,        ble_att_svr_rx_read_blob },
    { BLE_ATT_OP_READ_BLOB_RSP,        ble_att_clt_rx_read_blob },
    { BLE_ATT_OP_READ_MULT_REQ,        ble_att_svr_rx_read_mult },
    { BLE_ATT_OP_READ_MULT_RSP,        ble_att_clt_rx_read_mult },
    { BLE_ATT_OP_READ_GROUP_TYPE_REQ,  ble_att_svr_rx_read_group_type },
    { BLE_ATT_OP_READ_GROUP_TYPE_RSP,  ble_att_clt_rx_read_group_type },
    { BLE_ATT_OP_WRITE_REQ,            ble_att_svr_rx_write },
    { BLE_ATT_OP_WRITE_RSP,            ble_att_clt_rx_write },
    { BLE_ATT_OP_PREP_WRITE_REQ,       ble_att_svr_rx_prep_write },
    { BLE_ATT_OP_PREP_WRITE_RSP,       ble_att_clt_rx_prep_write },
    { BLE_ATT_OP_EXEC_WRITE_REQ,       ble_att_svr_rx_exec_write },
    { BLE_ATT_OP_EXEC_WRITE_RSP,       ble_att_clt_rx_exec_write },
    { BLE_ATT_OP_NOTIFY_REQ,           ble_att_svr_rx_notify },
    { BLE_ATT_OP_INDICATE_REQ,         ble_att_svr_rx_indicate },
    { BLE_ATT_OP_INDICATE_RSP,         ble_att_clt_rx_indicate },
    { BLE_ATT_OP_WRITE_CMD,            ble_att_svr_rx_write_no_rsp },
};

#define BLE_ATT_RX_DISPATCH_SZ \
    (sizeof ble_att_rx_dispatch / sizeof ble_att_rx_dispatch[0])

STATS_SECT_DECL(ble_att_stats) ble_att_stats;
STATS_NAME_START(ble_att_stats)
    STATS_NAME(ble_att_stats, error_rsp_rx)
    STATS_NAME(ble_att_stats, error_rsp_tx)
    STATS_NAME(ble_att_stats, mtu_req_rx)
    STATS_NAME(ble_att_stats, mtu_req_tx)
    STATS_NAME(ble_att_stats, mtu_rsp_rx)
    STATS_NAME(ble_att_stats, mtu_rsp_tx)
    STATS_NAME(ble_att_stats, find_info_req_rx)
    STATS_NAME(ble_att_stats, find_info_req_tx)
    STATS_NAME(ble_att_stats, find_info_rsp_rx)
    STATS_NAME(ble_att_stats, find_info_rsp_tx)
    STATS_NAME(ble_att_stats, find_type_value_req_rx)
    STATS_NAME(ble_att_stats, find_type_value_req_tx)
    STATS_NAME(ble_att_stats, find_type_value_rsp_rx)
    STATS_NAME(ble_att_stats, find_type_value_rsp_tx)
    STATS_NAME(ble_att_stats, read_type_req_rx)
    STATS_NAME(ble_att_stats, read_type_req_tx)
    STATS_NAME(ble_att_stats, read_type_rsp_rx)
    STATS_NAME(ble_att_stats, read_type_rsp_tx)
    STATS_NAME(ble_att_stats, read_req_rx)
    STATS_NAME(ble_att_stats, read_req_tx)
    STATS_NAME(ble_att_stats, read_rsp_rx)
    STATS_NAME(ble_att_stats, read_rsp_tx)
    STATS_NAME(ble_att_stats, read_blob_req_rx)
    STATS_NAME(ble_att_stats, read_blob_req_tx)
    STATS_NAME(ble_att_stats, read_blob_rsp_rx)
    STATS_NAME(ble_att_stats, read_blob_rsp_tx)
    STATS_NAME(ble_att_stats, read_mult_req_rx)
    STATS_NAME(ble_att_stats, read_mult_req_tx)
    STATS_NAME(ble_att_stats, read_mult_rsp_rx)
    STATS_NAME(ble_att_stats, read_mult_rsp_tx)
    STATS_NAME(ble_att_stats, read_group_type_req_rx)
    STATS_NAME(ble_att_stats, read_group_type_req_tx)
    STATS_NAME(ble_att_stats, read_group_type_rsp_rx)
    STATS_NAME(ble_att_stats, read_group_type_rsp_tx)
    STATS_NAME(ble_att_stats, write_req_rx)
    STATS_NAME(ble_att_stats, write_req_tx)
    STATS_NAME(ble_att_stats, write_rsp_rx)
    STATS_NAME(ble_att_stats, write_rsp_tx)
    STATS_NAME(ble_att_stats, prep_write_req_rx)
    STATS_NAME(ble_att_stats, prep_write_req_tx)
    STATS_NAME(ble_att_stats, prep_write_rsp_rx)
    STATS_NAME(ble_att_stats, prep_write_rsp_tx)
    STATS_NAME(ble_att_stats, exec_write_req_rx)
    STATS_NAME(ble_att_stats, exec_write_req_tx)
    STATS_NAME(ble_att_stats, exec_write_rsp_rx)
    STATS_NAME(ble_att_stats, exec_write_rsp_tx)
    STATS_NAME(ble_att_stats, notify_req_rx)
    STATS_NAME(ble_att_stats, notify_req_tx)
    STATS_NAME(ble_att_stats, indicate_req_rx)
    STATS_NAME(ble_att_stats, indicate_req_tx)
    STATS_NAME(ble_att_stats, indicate_rsp_rx)
    STATS_NAME(ble_att_stats, indicate_rsp_tx)
    STATS_NAME(ble_att_stats, write_cmd_rx)
    STATS_NAME(ble_att_stats, write_cmd_tx)
STATS_NAME_END(ble_att_stats)

static const struct ble_att_rx_dispatch_entry *
ble_att_rx_dispatch_entry_find(uint8_t op)
{
    const struct ble_att_rx_dispatch_entry *entry;
    int i;

    for (i = 0; i < BLE_ATT_RX_DISPATCH_SZ; i++) {
        entry = ble_att_rx_dispatch + i;
        if (entry->bde_op == op) {
            return entry;
        }

        if (entry->bde_op > op) {
            break;
        }
    }

    return NULL;
}

int
ble_att_conn_chan_find(uint16_t conn_handle, struct ble_hs_conn **out_conn,
                       struct ble_l2cap_chan **out_chan)
{
    int rc;

    rc = ble_hs_misc_conn_chan_find_reqd(conn_handle, BLE_L2CAP_CID_ATT,
                                         out_conn, out_chan);
    return rc;
}

void
ble_att_inc_tx_stat(uint8_t att_op)
{
    switch (att_op) {
    case BLE_ATT_OP_ERROR_RSP:
        STATS_INC(ble_att_stats, error_rsp_tx);
        break;

    case BLE_ATT_OP_MTU_REQ:
        STATS_INC(ble_att_stats, mtu_req_tx);
        break;

    case BLE_ATT_OP_MTU_RSP:
        STATS_INC(ble_att_stats, mtu_rsp_tx);
        break;

    case BLE_ATT_OP_FIND_INFO_REQ:
        STATS_INC(ble_att_stats, find_info_req_tx);
        break;

    case BLE_ATT_OP_FIND_INFO_RSP:
        STATS_INC(ble_att_stats, find_info_rsp_tx);
        break;

    case BLE_ATT_OP_FIND_TYPE_VALUE_REQ:
        STATS_INC(ble_att_stats, find_type_value_req_tx);
        break;

    case BLE_ATT_OP_FIND_TYPE_VALUE_RSP:
        STATS_INC(ble_att_stats, find_type_value_rsp_tx);
        break;

    case BLE_ATT_OP_READ_TYPE_REQ:
        STATS_INC(ble_att_stats, read_type_req_tx);
        break;

    case BLE_ATT_OP_READ_TYPE_RSP:
        STATS_INC(ble_att_stats, read_type_rsp_tx);
        break;

    case BLE_ATT_OP_READ_REQ:
        STATS_INC(ble_att_stats, read_req_tx);
        break;

    case BLE_ATT_OP_READ_RSP:
        STATS_INC(ble_att_stats, read_rsp_tx);
        break;

    case BLE_ATT_OP_READ_BLOB_REQ:
        STATS_INC(ble_att_stats, read_blob_req_tx);
        break;

    case BLE_ATT_OP_READ_BLOB_RSP:
        STATS_INC(ble_att_stats, read_blob_rsp_tx);
        break;

    case BLE_ATT_OP_READ_MULT_REQ:
        STATS_INC(ble_att_stats, read_mult_req_tx);
        break;

    case BLE_ATT_OP_READ_MULT_RSP:
        STATS_INC(ble_att_stats, read_mult_rsp_tx);
        break;

    case BLE_ATT_OP_READ_GROUP_TYPE_REQ:
        STATS_INC(ble_att_stats, read_group_type_req_tx);
        break;

    case BLE_ATT_OP_READ_GROUP_TYPE_RSP:
        STATS_INC(ble_att_stats, read_group_type_rsp_tx);
        break;

    case BLE_ATT_OP_WRITE_REQ:
        STATS_INC(ble_att_stats, write_req_tx);
        break;

    case BLE_ATT_OP_WRITE_RSP:
        STATS_INC(ble_att_stats, write_rsp_tx);
        break;

    case BLE_ATT_OP_PREP_WRITE_REQ:
        STATS_INC(ble_att_stats, prep_write_req_tx);
        break;

    case BLE_ATT_OP_PREP_WRITE_RSP:
        STATS_INC(ble_att_stats, prep_write_rsp_tx);
        break;

    case BLE_ATT_OP_EXEC_WRITE_REQ:
        STATS_INC(ble_att_stats, exec_write_req_tx);
        break;

    case BLE_ATT_OP_EXEC_WRITE_RSP:
        STATS_INC(ble_att_stats, exec_write_rsp_tx);
        break;

    case BLE_ATT_OP_NOTIFY_REQ:
        STATS_INC(ble_att_stats, notify_req_tx);
        break;

    case BLE_ATT_OP_INDICATE_REQ:
        STATS_INC(ble_att_stats, indicate_req_tx);
        break;

    case BLE_ATT_OP_INDICATE_RSP:
        STATS_INC(ble_att_stats, indicate_rsp_tx);
        break;

    case BLE_ATT_OP_WRITE_CMD:
        STATS_INC(ble_att_stats, write_cmd_tx);
        break;

    default:
        break;
    }
}

static void
ble_att_inc_rx_stat(uint8_t att_op)
{
    switch (att_op) {
    case BLE_ATT_OP_ERROR_RSP:
        STATS_INC(ble_att_stats, error_rsp_rx);
        break;

    case BLE_ATT_OP_MTU_REQ:
        STATS_INC(ble_att_stats, mtu_req_rx);
        break;

    case BLE_ATT_OP_MTU_RSP:
        STATS_INC(ble_att_stats, mtu_rsp_rx);
        break;

    case BLE_ATT_OP_FIND_INFO_REQ:
        STATS_INC(ble_att_stats, find_info_req_rx);
        break;

    case BLE_ATT_OP_FIND_INFO_RSP:
        STATS_INC(ble_att_stats, find_info_rsp_rx);
        break;

    case BLE_ATT_OP_FIND_TYPE_VALUE_REQ:
        STATS_INC(ble_att_stats, find_type_value_req_rx);
        break;

    case BLE_ATT_OP_FIND_TYPE_VALUE_RSP:
        STATS_INC(ble_att_stats, find_type_value_rsp_rx);
        break;

    case BLE_ATT_OP_READ_TYPE_REQ:
        STATS_INC(ble_att_stats, read_type_req_rx);
        break;

    case BLE_ATT_OP_READ_TYPE_RSP:
        STATS_INC(ble_att_stats, read_type_rsp_rx);
        break;

    case BLE_ATT_OP_READ_REQ:
        STATS_INC(ble_att_stats, read_req_rx);
        break;

    case BLE_ATT_OP_READ_RSP:
        STATS_INC(ble_att_stats, read_rsp_rx);
        break;

    case BLE_ATT_OP_READ_BLOB_REQ:
        STATS_INC(ble_att_stats, read_blob_req_rx);
        break;

    case BLE_ATT_OP_READ_BLOB_RSP:
        STATS_INC(ble_att_stats, read_blob_rsp_rx);
        break;

    case BLE_ATT_OP_READ_MULT_REQ:
        STATS_INC(ble_att_stats, read_mult_req_rx);
        break;

    case BLE_ATT_OP_READ_MULT_RSP:
        STATS_INC(ble_att_stats, read_mult_rsp_rx);
        break;

    case BLE_ATT_OP_READ_GROUP_TYPE_REQ:
        STATS_INC(ble_att_stats, read_group_type_req_rx);
        break;

    case BLE_ATT_OP_READ_GROUP_TYPE_RSP:
        STATS_INC(ble_att_stats, read_group_type_rsp_rx);
        break;

    case BLE_ATT_OP_WRITE_REQ:
        STATS_INC(ble_att_stats, write_req_rx);
        break;

    case BLE_ATT_OP_WRITE_RSP:
        STATS_INC(ble_att_stats, write_rsp_rx);
        break;

    case BLE_ATT_OP_PREP_WRITE_REQ:
        STATS_INC(ble_att_stats, prep_write_req_rx);
        break;

    case BLE_ATT_OP_PREP_WRITE_RSP:
        STATS_INC(ble_att_stats, prep_write_rsp_rx);
        break;

    case BLE_ATT_OP_EXEC_WRITE_REQ:
        STATS_INC(ble_att_stats, exec_write_req_rx);
        break;

    case BLE_ATT_OP_EXEC_WRITE_RSP:
        STATS_INC(ble_att_stats, exec_write_rsp_rx);
        break;

    case BLE_ATT_OP_NOTIFY_REQ:
        STATS_INC(ble_att_stats, notify_req_rx);
        break;

    case BLE_ATT_OP_INDICATE_REQ:
        STATS_INC(ble_att_stats, indicate_req_rx);
        break;

    case BLE_ATT_OP_INDICATE_RSP:
        STATS_INC(ble_att_stats, indicate_rsp_rx);
        break;

    case BLE_ATT_OP_WRITE_CMD:
        STATS_INC(ble_att_stats, write_cmd_rx);
        break;

    default:
        break;
    }
}

/**
 * Retrieves a pointer to the global ATT flat buffer.  This buffer is only used
 * by the host parent task, so users can assume exclusive access.
 */
uint8_t *
ble_att_get_flat_buf(void)
{
    BLE_HS_DBG_ASSERT(ble_hs_is_parent_task());
    return ble_att_flat_buf;
}

uint16_t
ble_att_mtu(uint16_t conn_handle)
{
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    uint16_t mtu;

    ble_hs_lock();

    ble_att_conn_chan_find(conn_handle, &conn, &chan);
    if (chan != NULL) {
        mtu = ble_l2cap_chan_mtu(chan);
    } else {
        mtu = 0;
    }

    ble_hs_unlock();

    return mtu;
}

static int
ble_att_rx(uint16_t conn_handle, struct os_mbuf **om)
{
    const struct ble_att_rx_dispatch_entry *entry;
    uint8_t op;
    int rc;

    rc = os_mbuf_copydata(*om, 0, 1, &op);
    if (rc != 0) {
        return BLE_HS_EMSGSIZE;
    }

    entry = ble_att_rx_dispatch_entry_find(op);
    if (entry == NULL) {
        return BLE_HS_EINVAL;
    }

    ble_att_inc_rx_stat(op);

    rc = entry->bde_fn(conn_handle, om);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

void
ble_att_set_peer_mtu(struct ble_l2cap_chan *chan, uint16_t peer_mtu)
{
    if (peer_mtu < BLE_ATT_MTU_DFLT) {
        peer_mtu = BLE_ATT_MTU_DFLT;
    }

    chan->blc_peer_mtu = peer_mtu;
}

int
ble_att_set_preferred_mtu(uint16_t mtu)
{
    if (mtu < BLE_ATT_MTU_DFLT) {
        return BLE_HS_EINVAL;
    }
    if (mtu > BLE_ATT_MTU_MAX) {
        return BLE_HS_EINVAL;
    }

    ble_att_preferred_mtu = mtu;

    /* XXX: Set my_mtu for established connections that haven't exchanged. */

    return 0;
}

struct ble_l2cap_chan *
ble_att_create_chan(void)
{
    struct ble_l2cap_chan *chan;

    chan = ble_l2cap_chan_alloc();
    if (chan == NULL) {
        return NULL;
    }

    chan->blc_cid = BLE_L2CAP_CID_ATT;
    chan->blc_my_mtu = ble_att_preferred_mtu;
    chan->blc_default_mtu = BLE_ATT_MTU_DFLT;
    chan->blc_rx_fn = ble_att_rx;

    return chan;
}

int
ble_att_init(void)
{
    int rc;

    ble_att_preferred_mtu = BLE_ATT_MTU_PREFERRED_DFLT;

    rc = stats_init_and_reg(
        STATS_HDR(ble_att_stats), STATS_SIZE_INIT_PARMS(ble_att_stats,
        STATS_SIZE_32), STATS_NAME_INIT_PARMS(ble_att_stats), "ble_att");
    if (rc != 0) {
        return BLE_HS_EOS;
    }

    return 0;
}
