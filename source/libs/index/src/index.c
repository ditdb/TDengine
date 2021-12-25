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

#include "index.h"
#include "indexInt.h"
#include "index_cache.h"
#include "index_tfile.h"
#include "tdef.h"

#ifdef USE_LUCENE
#include "lucene++/Lucene_c.h"
#endif

static int uidCompare(const void* a, const void* b) {
  uint64_t u1 = *(uint64_t*)a;
  uint64_t u2 = *(uint64_t*)b;
  if (u1 == u2) {
    return 0;
  } else {
    return u1 < u2 ? -1 : 1;
  }
}
typedef struct SIdxColInfo {
  int colId;  // generated by index internal
  int cVersion;
} SIdxColInfo;

static pthread_once_t isInit = PTHREAD_ONCE_INIT;
static void           indexInit();

static int indexTermSearch(SIndex* sIdx, SIndexTermQuery* term, SArray** result);
static int indexFlushCacheTFile(SIndex* sIdx);

static void indexInterResultsDestroy(SArray* results);
static int  indexMergeFinalResults(SArray* interResults, EIndexOperatorType oType, SArray* finalResult);

int indexOpen(SIndexOpts* opts, const char* path, SIndex** index) {
  pthread_once(&isInit, indexInit);
  SIndex* sIdx = calloc(1, sizeof(SIndex));
  if (sIdx == NULL) { return -1; }

#ifdef USE_LUCENE
  index_t* index = index_open(path);
  sIdx->index = index;
#endif

#ifdef USE_INVERTED_INDEX
  sIdx->cache = (void*)indexCacheCreate();
  sIdx->tindex = NULL;
  sIdx->colObj = taosHashInit(8, taosGetDefaultHashFunction(TSDB_DATA_TYPE_BINARY), true, HASH_ENTRY_LOCK);
  sIdx->colId = 1;
  sIdx->cVersion = 1;
  pthread_mutex_init(&sIdx->mtx, NULL);

  *index = sIdx;
  return 0;
#endif

  *index = NULL;
  return -1;
}

void indexClose(SIndex* sIdx) {
#ifdef USE_LUCENE
  index_close(sIdex->index);
  sIdx->index = NULL;
#endif

#ifdef USE_INVERTED_INDEX
  indexCacheDestroy(sIdx->cache);
  taosHashCleanup(sIdx->colObj);
  pthread_mutex_destroy(&sIdx->mtx);
#endif
  free(sIdx);
  return;
}

int indexPut(SIndex* index, SIndexMultiTerm* fVals, uint64_t uid) {
#ifdef USE_LUCENE
  index_document_t* doc = index_document_create();

  char buf[16] = {0};
  sprintf(buf, "%d", uid);

  for (int i = 0; i < taosArrayGetSize(fVals); i++) {
    SIndexTerm* p = taosArrayGetP(fVals, i);
    index_document_add(doc, (const char*)(p->key), p->nKey, (const char*)(p->val), p->nVal, 1);
  }
  index_document_add(doc, NULL, 0, buf, strlen(buf), 0);

  index_put(index->index, doc);
  index_document_destroy(doc);
#endif

#ifdef USE_INVERTED_INDEX

  // TODO(yihao): reduce the lock range
  pthread_mutex_lock(&index->mtx);
  for (int i = 0; i < taosArrayGetSize(fVals); i++) {
    SIndexTerm*  p = taosArrayGetP(fVals, i);
    SIdxColInfo* fi = taosHashGet(index->colObj, p->colName, p->nColName);
    if (fi == NULL) {
      SIdxColInfo tfi = {.colId = index->colId};
      index->cVersion++;
      index->colId++;
      taosHashPut(index->colObj, p->colName, p->nColName, &tfi, sizeof(tfi));
    } else {
      // TODO, del
    }
  }
  pthread_mutex_unlock(&index->mtx);

  for (int i = 0; i < taosArrayGetSize(fVals); i++) {
    SIndexTerm*  p = taosArrayGetP(fVals, i);
    SIdxColInfo* fi = taosHashGet(index->colObj, p->colName, p->nColName);
    assert(fi != NULL);
    int32_t colId = fi->colId;
    int32_t version = index->cVersion;
    int     ret = indexCachePut(index->cache, p, colId, version, uid);
    if (ret != 0) { return ret; }
  }
#endif

  return 0;
}
int indexSearch(SIndex* index, SIndexMultiTermQuery* multiQuerys, SArray* result) {
#ifdef USE_LUCENE
  EIndexOperatorType opera = multiQuerys->opera;

  int    nQuery = taosArrayGetSize(multiQuerys->query);
  char** fields = malloc(sizeof(char*) * nQuery);
  char** keys = malloc(sizeof(char*) * nQuery);
  int*   types = malloc(sizeof(int) * nQuery);

  for (int i = 0; i < nQuery; i++) {
    SIndexTermQuery* p = taosArrayGet(multiQuerys->query, i);
    SIndexTerm*      term = p->field_value;

    fields[i] = calloc(1, term->nKey + 1);
    keys[i] = calloc(1, term->nVal + 1);

    memcpy(fields[i], term->key, term->nKey);
    memcpy(keys[i], term->val, term->nVal);
    types[i] = (int)(p->type);
  }
  int* tResult = NULL;
  int  tsz = 0;
  index_multi_search(index->index, (const char**)fields, (const char**)keys, types, nQuery, opera, &tResult, &tsz);

  for (int i = 0; i < tsz; i++) {
    taosArrayPush(result, &tResult[i]);
  }

  for (int i = 0; i < nQuery; i++) {
    free(fields[i]);
    free(keys[i]);
  }
  free(fields);
  free(keys);
  free(types);
#endif

#ifdef USE_INVERTED_INDEX
  EIndexOperatorType opera = multiQuerys->opera;  // relation of querys

  SArray* interResults = taosArrayInit(4, POINTER_BYTES);
  int     nQuery = taosArrayGetSize(multiQuerys->query);
  for (size_t i = 0; i < nQuery; i++) {
    SIndexTermQuery* qTerm = taosArrayGet(multiQuerys->query, i);
    SArray*          tResult = NULL;
    indexTermSearch(index, qTerm, &tResult);
    taosArrayPush(interResults, (void*)&tResult);
  }
  indexMergeFinalResults(interResults, opera, result);
  indexInterResultsDestroy(interResults);

#endif
  return 1;
}

int indexDelete(SIndex* index, SIndexMultiTermQuery* query) {
#ifdef USE_INVERTED_INDEX
#endif

  return 1;
}
int indexRebuild(SIndex* index, SIndexOpts* opts){
#ifdef USE_INVERTED_INDEX
#endif

}

SIndexOpts* indexOptsCreate() {
#ifdef USE_LUCENE
#endif
  return NULL;
}
void indexOptsDestroy(SIndexOpts* opts){
#ifdef USE_LUCENE
#endif
} /*
   * @param: oper
   *
   */

SIndexMultiTermQuery* indexMultiTermQueryCreate(EIndexOperatorType opera) {
  SIndexMultiTermQuery* p = (SIndexMultiTermQuery*)malloc(sizeof(SIndexMultiTermQuery));
  if (p == NULL) { return NULL; }
  p->opera = opera;
  p->query = taosArrayInit(4, sizeof(SIndexTermQuery));
  return p;
}
void indexMultiTermQueryDestroy(SIndexMultiTermQuery* pQuery) {
  for (int i = 0; i < taosArrayGetSize(pQuery->query); i++) {
    SIndexTermQuery* p = (SIndexTermQuery*)taosArrayGet(pQuery->query, i);
    indexTermDestroy(p->term);
  }
  taosArrayDestroy(pQuery->query);
  free(pQuery);
};
int indexMultiTermQueryAdd(SIndexMultiTermQuery* pQuery, SIndexTerm* term, EIndexQueryType qType) {
  SIndexTermQuery q = {.qType = qType, .term = term};
  taosArrayPush(pQuery->query, &q);
  return 0;
}

SIndexTerm* indexTermCreate(int64_t            suid,
                            SIndexOperOnColumn oper,
                            uint8_t            colType,
                            const char*        colName,
                            int32_t            nColName,
                            const char*        colVal,
                            int32_t            nColVal) {
  SIndexTerm* t = (SIndexTerm*)calloc(1, (sizeof(SIndexTerm)));
  if (t == NULL) { return NULL; }

  t->suid = suid;
  t->operType = oper;
  t->colType = colType;

  t->colName = (char*)calloc(1, nColName + 1);
  memcpy(t->colName, colName, nColName);
  t->nColName = nColName;

  t->colVal = (char*)calloc(1, nColVal + 1);
  memcpy(t->colVal, colVal, nColVal);
  t->nColVal = nColVal;
  return t;
}
void indexTermDestroy(SIndexTerm* p) {
  free(p->colName);
  free(p->colVal);
  free(p);
}

SIndexMultiTerm* indexMultiTermCreate() {
  return taosArrayInit(4, sizeof(SIndexTerm*));
}

int indexMultiTermAdd(SIndexMultiTerm* terms, SIndexTerm* term) {
  taosArrayPush(terms, &term);
  return 0;
}
void indexMultiTermDestroy(SIndexMultiTerm* terms) {
  for (int32_t i = 0; i < taosArrayGetSize(terms); i++) {
    SIndexTerm* p = taosArrayGetP(terms, i);
    indexTermDestroy(p);
  }
  taosArrayDestroy(terms);
}

void indexInit() {
  // do nothing
}
static int indexTermSearch(SIndex* sIdx, SIndexTermQuery* query, SArray** result) {
  int32_t      version = -1;
  int16_t      colId = -1;
  SIdxColInfo* colInfo = NULL;

  SIndexTerm* term = query->term;
  const char* colName = term->colName;
  int32_t     nColName = term->nColName;

  pthread_mutex_lock(&sIdx->mtx);
  colInfo = taosHashGet(sIdx->colObj, colName, nColName);
  if (colInfo == NULL) {
    pthread_mutex_unlock(&sIdx->mtx);
    return -1;
  }
  colId = colInfo->colId;
  version = colInfo->cVersion;
  pthread_mutex_unlock(&sIdx->mtx);

  *result = taosArrayInit(4, sizeof(uint64_t));
  // TODO: iterator mem and tidex
  STermValueType s;
  if (0 == indexCacheSearch(sIdx->cache, query, colId, version, *result, &s)) {
    if (s == kTypeDeletion) {
      indexInfo("col: %s already drop by other opera", term->colName);
      // coloum already drop by other oper, no need to query tindex
      return 0;
    } else {
      if (0 != indexTFileSearch(sIdx->tindex, query, *result)) {
        indexError("corrupt at index(TFile) col:%s val: %s", term->colName, term->colVal);
        return -1;
      }
    }
  } else {
    indexError("corrupt at index(cache) col:%s val: %s", term->colName, term->colVal);
    return -1;
  }
  return 0;
}
static void indexInterResultsDestroy(SArray* results) {
  if (results == NULL) { return; }

  size_t sz = taosArrayGetSize(results);
  for (size_t i = 0; i < sz; i++) {
    SArray* p = taosArrayGetP(results, i);
    taosArrayDestroy(p);
  }
  taosArrayDestroy(results);
}
static int indexMergeFinalResults(SArray* interResults, EIndexOperatorType oType, SArray* fResults) {
  // refactor, merge interResults into fResults by oType
  SArray* first = taosArrayGetP(interResults, 0);
  taosArraySort(first, uidCompare);
  taosArrayRemoveDuplicate(first, uidCompare, NULL);

  if (oType == MUST) {
    // just one column index, enhance later
    taosArrayAddAll(fResults, first);
  } else if (oType == SHOULD) {
    // just one column index, enhance later
    taosArrayAddAll(fResults, first);
    // tag1 condistion || tag2 condition
  } else if (oType == NOT) {
    // just one column index, enhance later
    taosArrayAddAll(fResults, first);
    // not use currently
  }
  return 0;
}
static int indexFlushCacheTFile(SIndex* sIdx) {
  if (sIdx == NULL) { return -1; }

  indexWarn("suid %" PRIu64 " merge cache into tindex", sIdx->suid);

  return 0;
}
