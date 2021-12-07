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

#ifndef _TD_SDB_H_
#define _TD_SDB_H_

#ifdef __cplusplus
extern "C" {
#endif

#define SDB_GET_INT64(pData, pRow, dataPos, val)   \
  {                                                \
    if (sdbGetRawInt64(pRaw, dataPos, val) != 0) { \
      sdbFreeRow(pRow);                            \
      return NULL;                                 \
    }                                              \
    dataPos += sizeof(int64_t);                    \
  }

#define SDB_GET_INT32(pData, pRow, dataPos, val)   \
  {                                                \
    if (sdbGetRawInt32(pRaw, dataPos, val) != 0) { \
      sdbFreeRow(pRow);                            \
      return NULL;                                 \
    }                                              \
    dataPos += sizeof(int32_t);                    \
  }

#define SDB_GET_INT16(pData, pRow, dataPos, val)   \
  {                                                \
    if (sdbGetRawInt16(pRaw, dataPos, val) != 0) { \
      sdbFreeRow(pRow);                            \
      return NULL;                                 \
    }                                              \
    dataPos += sizeof(int16_t);                    \
  }

#define SDB_GET_INT8(pData, pRow, dataPos, val)   \
  {                                               \
    if (sdbGetRawInt8(pRaw, dataPos, val) != 0) { \
      sdbFreeRow(pRow);                           \
      return NULL;                                \
    }                                             \
    dataPos += sizeof(int8_t);                    \
  }

#define SDB_GET_BINARY(pRaw, pRow, dataPos, val, valLen)    \
  {                                                         \
    if (sdbGetRawBinary(pRaw, dataPos, val, valLen) != 0) { \
      sdbFreeRow(pRow);                                     \
      return NULL;                                          \
    }                                                       \
    dataPos += valLen;                                      \
  }

#define SDB_SET_INT64(pRaw, dataPos, val)          \
  {                                                \
    if (sdbSetRawInt64(pRaw, dataPos, val) != 0) { \
      sdbFreeRaw(pRaw);                            \
      return NULL;                                 \
    }                                              \
    dataPos += sizeof(int64_t);                    \
  }

#define SDB_SET_INT32(pRaw, dataPos, val)          \
  {                                                \
    if (sdbSetRawInt32(pRaw, dataPos, val) != 0) { \
      sdbFreeRaw(pRaw);                            \
      return NULL;                                 \
    }                                              \
    dataPos += sizeof(int32_t);                    \
  }

#define SDB_SET_INT16(pRaw, dataPos, val)          \
  {                                                \
    if (sdbSetRawInt16(pRaw, dataPos, val) != 0) { \
      sdbFreeRaw(pRaw);                            \
      return NULL;                                 \
    }                                              \
    dataPos += sizeof(int16_t);                     \
  }

#define SDB_SET_INT8(pRaw, dataPos, val)          \
  {                                               \
    if (sdbSetRawInt8(pRaw, dataPos, val) != 0) { \
      sdbFreeRaw(pRaw);                           \
      return NULL;                                \
    }                                             \
    dataPos += sizeof(int8_t);                    \
  }

#define SDB_SET_BINARY(pRaw, dataPos, val, valLen)          \
  {                                                         \
    if (sdbSetRawBinary(pRaw, dataPos, val, valLen) != 0) { \
      sdbFreeRaw(pRaw);                                     \
      return NULL;                                          \
    }                                                       \
    dataPos += valLen;                                      \
  }

#define SDB_SET_DATALEN(pRaw, dataLen)          \
  {                                             \
    if (sdbSetRawDataLen(pRaw, dataLen) != 0) { \
      sdbFreeRaw(pRaw);                         \
      return NULL;                              \
    }                                           \
  }

typedef struct SMnode  SMnode;
typedef struct SSdbRaw SSdbRaw;
typedef struct SSdbRow SSdbRow;
typedef enum { SDB_KEY_BINARY = 1, SDB_KEY_INT32 = 2, SDB_KEY_INT64 = 3 } EKeyType;
typedef enum {
  SDB_STATUS_CREATING = 1,
  SDB_STATUS_READY = 2,
  SDB_STATUS_DROPPED = 3
} ESdbStatus;

typedef enum {
  SDB_START = 0,
  SDB_TRANS = 1,
  SDB_CLUSTER = 2,
  SDB_MNODE = 3,
  SDB_DNODE = 4,
  SDB_USER = 5,
  SDB_AUTH = 6,
  SDB_ACCT = 7,
  SDB_VGROUP = 9,
  SDB_STABLE = 9,
  SDB_DB = 10,
  SDB_FUNC = 11,
  SDB_MAX = 12
} ESdbType;

typedef struct SSdb SSdb;
typedef int32_t (*SdbInsertFp)(SSdb *pSdb, void *pObj);
typedef int32_t (*SdbUpdateFp)(SSdb *pSdb, void *pSrcObj, void *pDstObj);
typedef int32_t (*SdbDeleteFp)(SSdb *pSdb, void *pObj);
typedef int32_t (*SdbDeployFp)(SMnode *pMnode);
typedef SSdbRow *(*SdbDecodeFp)(SSdbRaw *pRaw);
typedef SSdbRaw *(*SdbEncodeFp)(void *pObj);

typedef struct {
  /**
   * @brief The sdb type of the table.
   *
   */
  ESdbType sdbType;

  /**
   * @brief The key type of the table.
   *
   */
  EKeyType keyType;

  /**
   * @brief The callback function when the table is first deployed.
   *
   */
  SdbDeployFp deployFp;

  /**
   * @brief Encode one row of the table into rawdata.
   *
   */
  SdbEncodeFp encodeFp;

  /**
   * @brief Decode one row of the table from rawdata.
   *
   */
  SdbDecodeFp decodeFp;

  /**
   * @brief The callback function when insert a row to sdb.
   *
   */
  SdbInsertFp insertFp;

  /**
   * @brief The callback function when undate a row in sdb.
   *
   */
  SdbUpdateFp updateFp;

  /**
   * @brief The callback function when delete a row from sdb.
   *
   */
  SdbDeleteFp deleteFp;
} SSdbTable;

typedef struct SSdbOpt {
  /**
   * @brief The path of the sdb file.
   *
   */
  const char *path;

  /**
   * @brief The mnode object.
   *
   */
  SMnode *pMnode;
} SSdbOpt;

/**
 * @brief Initialize and start the sdb.
 *
 * @param pOption Option of the sdb.
 * @return SSdb* The sdb object.
 */
SSdb *sdbInit(SSdbOpt *pOption);

/**
 * @brief Stop and cleanup the sdb.
 *
 * @param pSdb The sdb object to close.
 */
void sdbCleanup(SSdb *pSdb);

/**
 * @brief Set the properties of sdb table.
 *
 * @param pSdb The sdb object.
 * @param table The properties of the table.
 * @return int32_t 0 for success, -1 for failure.
 */
int32_t sdbSetTable(SSdb *pSdb, SSdbTable table);

/**
 * @brief Set the initial rows of sdb.
 *
 * @param pSdb The sdb object.
 * @return int32_t 0 for success, -1 for failure.
 */
int32_t sdbDeploy(SSdb *pSdb);

/**
 * @brief Load sdb from file.
 *
 * @param pSdb The sdb object.
 * @return int32_t 0 for success, -1 for failure.
 */
int32_t sdbReadFile(SSdb *pSdb);

/**
 * @brief Parse and write raw data to sdb, then free the pRaw object
 *
 * @param pSdb The sdb object.
 * @param pRaw The raw data.
 * @return int32_t 0 for success, -1 for failure.
 */
int32_t sdbWrite(SSdb *pSdb, SSdbRaw *pRaw);

/**
 * @brief Parse and write raw data to sdb.
 *
 * @param pSdb The sdb object.
 * @param pRaw The raw data.
 * @return int32_t 0 for success, -1 for failure.
 */
int32_t sdbWriteNotFree(SSdb *pSdb, SSdbRaw *pRaw);

/**
 * @brief Acquire a row from sdb
 *
 * @param pSdb The sdb object.
 * @param type The type of the row.
 * @param pKey The key value of the row.
 * @return void* The object of the row.
 */
void *sdbAcquire(SSdb *pSdb, ESdbType type, void *pKey);

/**
 * @brief Release a row from sdb.
 *
 * @param pSdb The sdb object.
 * @param pObj The object of the row.
 */
void sdbRelease(SSdb *pSdb, void *pObj);

/**
 * @brief Traverse a sdb table
 *
 * @param pSdb The sdb object.
 * @param type The type of the table.
 * @param type The initial iterator of the table.
 * @param pObj The object of the row just fetched.
 * @return void* The next iterator of the table.
 */
void *sdbFetch(SSdb *pSdb, ESdbType type, void *pIter, void **ppObj);

/**
 * @brief Cancel a traversal
 *
 * @param pSdb The sdb object.
 * @param pIter The iterator of the table.
 * @param type The initial iterator of table.
 */
void sdbCancelFetch(SSdb *pSdb, void *pIter);

/**
 * @brief Get the number of rows in the table
 *
 * @param pSdb The sdb object.
 * @param pIter The type of the table.
 * @record int32_t The number of rows in the table
 */
int32_t sdbGetSize(SSdb *pSdb, ESdbType type);

SSdbRaw *sdbAllocRaw(ESdbType type, int8_t sver, int32_t dataLen);
void     sdbFreeRaw(SSdbRaw *pRaw);
int32_t  sdbSetRawInt8(SSdbRaw *pRaw, int32_t dataPos, int8_t val);
int32_t  sdbSetRawInt16(SSdbRaw *pRaw, int32_t dataPos, int16_t val);
int32_t  sdbSetRawInt32(SSdbRaw *pRaw, int32_t dataPos, int32_t val);
int32_t  sdbSetRawInt64(SSdbRaw *pRaw, int32_t dataPos, int64_t val);
int32_t  sdbSetRawBinary(SSdbRaw *pRaw, int32_t dataPos, const char *pVal, int32_t valLen);
int32_t  sdbSetRawDataLen(SSdbRaw *pRaw, int32_t dataLen);
int32_t  sdbSetRawStatus(SSdbRaw *pRaw, ESdbStatus status);
int32_t  sdbGetRawInt8(SSdbRaw *pRaw, int32_t dataPos, int8_t *val);
int32_t  sdbGetRawInt16(SSdbRaw *pRaw, int32_t dataPos, int16_t *val);
int32_t  sdbGetRawInt32(SSdbRaw *pRaw, int32_t dataPos, int32_t *val);
int32_t  sdbGetRawInt64(SSdbRaw *pRaw, int32_t dataPos, int64_t *val);
int32_t  sdbGetRawBinary(SSdbRaw *pRaw, int32_t dataPos, char *pVal, int32_t valLen);
int32_t  sdbGetRawSoftVer(SSdbRaw *pRaw, int8_t *sver);
int32_t  sdbGetRawTotalSize(SSdbRaw *pRaw);

SSdbRow *sdbAllocRow(int32_t objSize);
void     sdbFreeRow(SSdbRow *pRow);
void    *sdbGetRowObj(SSdbRow *pRow);

#ifdef __cplusplus
}
#endif

#endif /*_TD_SDB_H_*/
