/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#if 0
#undef TD_MSG_INFO_
#undef TD_MSG_NUMBER_
#undef TD_MSG_DICT_
#undef TD_MSG_SEG_CODE_
#endif

#if defined(TD_MSG_INFO_)

#define TD_NEW_MSG_SEG(TYPE) "null",
#define TD_DEF_MSG_TYPE(TYPE, MSG, REQ, RSP) MSG, MSG "-rsp",

char *tMsgInfo[] = {

#elif defined(TD_MSG_NUMBER_)

#define TD_NEW_MSG_SEG(TYPE) TYPE##_NUM,
#define TD_DEF_MSG_TYPE(TYPE, MSG, REQ, RSP) TYPE##_NUM, TYPE##_RSP_NUM,

enum {

#elif defined(TD_MSG_DICT_)

#define TD_NEW_MSG_SEG(TYPE) TYPE##_NUM,
#define TD_DEF_MSG_TYPE(TYPE, MSG, REQ, RSP)

int tMsgDict[] = {

#elif defined(TD_MSG_SEG_CODE_)

#define TD_NEW_MSG_SEG(TYPE) TYPE##_SEG_CODE,
#define TD_DEF_MSG_TYPE(TYPE, MSG, REQ, RSP)

enum {

#else

#define TD_NEW_MSG_SEG(TYPE) TYPE = ((TYPE##_SEG_CODE) << 8),
#define TD_DEF_MSG_TYPE(TYPE, MSG, REQ, RSP) TYPE, TYPE##_RSP,

enum {
#endif
  // Requests handled by DNODE
  TD_NEW_MSG_SEG(TDMT_DND_MSG)
  TD_DEF_MSG_TYPE(TDMT_DND_NETWORK_TEST, "dnode-nettest", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_DND_CREATE_VNODE, "dnode-create-vnode", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_DND_ALTER_VNODE, "dnode-alter-vnode", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_DND_DROP_VNODE, "dnode-drop-vnode", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_DND_AUTH_VNODE, "dnode-auth-vnode", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_DND_SYNC_VNODE, "dnode-sync-vnode", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_DND_COMPACT_VNODE, "dnode-compact-vnode", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_DND_CREATE_MNODE, "dnode-create-mnode", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_DND_ALTER_MNODE, "dnode-alter-mnode", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_DND_DROP_MNODE, "dnode-drop-mnode", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_DND_CONFIG_DNODE, "dnode-config-dnode", NULL, NULL)

  // Requests handled by MNODE
  TD_NEW_MSG_SEG(TDMT_MND_MSG)
  TD_DEF_MSG_TYPE(TDMT_MND_CONNECT, "mnode-connect", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_MND_CREATE_TABLE, "mnode-create-table", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_MND_DROP_TABLE, "mnode-drop-table", NULL, NULL)	
  TD_DEF_MSG_TYPE(TDMT_MND_CREATE_ACCT, "mnode-create-acct", NULL, NULL)	
  TD_DEF_MSG_TYPE(TDMT_MND_ALTER_ACCT, "mnode-alter-acct", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_MND_DROP_ACCT, "mnode-drop-acct", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_MND_CREATE_USER, "mnode-create-user", NULL, NULL)	
  TD_DEF_MSG_TYPE(TDMT_MND_ALTER_USER, "mnode-alter-user", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_MND_DROP_USER, "mnode-drop-user", NULL, NULL) 
  TD_DEF_MSG_TYPE(TDMT_MND_CREATE_DNODE, "mnode-create-dnode", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_MND_CONFIG_DNODE, "mnode-config-dnode", NULL, NULL) 
  TD_DEF_MSG_TYPE(TDMT_MND_DROP_DNODE, "mnode-drop-dnode", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_MND_CREATE_MNODE, "mnode-create-mnode", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_MND_DROP_MNODE, "mnode-drop-mnode", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_MND_CREATE_DB, "mnode-create-db", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_MND_DROP_DB, "mnode-drop-db", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_MND_USE_DB, "mnode-use-db", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_MND_ALTER_DB, "mnode-alter-db", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_MND_SYNC_DB, "mnode-sync-db", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_MND_COMPACT_DB, "mnode-compact-db", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_MND_CREATE_FUNCTION, "mnode-create-function", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_MND_RETRIEVE_FUNCTION, "mnode-retrieve-function", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_MND_DROP_FUNCTION, "mnode-drop-function", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_MND_CREATE_STB, "mnode-create-stb", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_MND_ALTER_STB, "mnode-alter-stb", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_MND_DROP_STB, "mnode-drop-stb", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_MND_VGROUP_LIST, "mnode-vgroup-list", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_MND_KILL_QUERY, "mnode-kill-query", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_MND_KILL_CONN, "mnode-kill-conn", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_MND_HEARTBEAT, "mnode-heartbeat", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_MND_SHOW, "mnode-show", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_MND_SHOW_RETRIEVE, "mnode-retrieve", NULL, NULL)
  TD_DEF_MSG_TYPE(TSDB_MSG_TYPE_STATUS, "mnode-status", NULL, NULL)
  TD_DEF_MSG_TYPE(TSDB_MSG_TYPE_GRANT, "mnode-grant", NULL, NULL)
  TD_DEF_MSG_TYPE(TSDB_MSG_TYPE_AUTH, "mnode-auth", NULL, NULL)

  // Requests handled by VNODE
  TD_NEW_MSG_SEG(TDMT_VND_MSG)
  TD_DEF_MSG_TYPE(TDMT_VND_SUBMIT, "vnode-submit", SSubmitReq, SSubmitRsp)
  TD_DEF_MSG_TYPE(TDMT_VND_QUERY, "vnode-query", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_VND_FETCH, "vnode-fetch", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_VND_ALTER_TABLE, "vnode-alter-table", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_VND_UPDATE_TAG_VAL, "vnode-update-tag-val", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_VND_TABLE_META, "vnode-table-meta", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_VND_TABLES_META, "vnode-tables-meta", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_VND_MQ_CONSUME, "vnode-mq-consume", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_VND_MQ_QUERY, "vnode-mq-query", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_VND_MQ_CONNECT, "vnode-mq-connect", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_VND_MQ_DISCONNECT, "vnode-mq-disconnect", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_VND_MQ_SET_CUR, "vnode-mq-set-cur", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_VND_RES_READY, "vnode-res-ready", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_VND_TASKS_STATUS, "vnode-tasks-status", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_VND_CANCEL_TASK, "vnode-cancel-task", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_VND_DROP_TASK, "vnode-drop-task", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_VND_CREATE_STB, "vnode-create-stb", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_VND_ALTER_STB, "vnode-alter-stb", NULL, NULL)
  TD_DEF_MSG_TYPE(TDMT_VND_DROP_STB, "vnode-drop-stb", NULL, NULL)

  // Requests handled by QNODE
  TD_NEW_MSG_SEG(TDMT_QND_MSG)

  // Requests handled by SNODE
  TD_NEW_MSG_SEG(TDMT_SND_MSG)

#if defined(TD_MSG_NUMBER_)
  TDMT_MAX
#endif
};