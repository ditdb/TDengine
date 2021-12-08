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

#include "index_fst.h"
#include "tcoding.h"
#include "tchecksum.h"
#include "indexInt.h" 


static void fstPackDeltaIn(FstCountingWriter *wrt, CompiledAddr nodeAddr, CompiledAddr transAddr, uint8_t nBytes) {
  CompiledAddr deltaAddr = (transAddr == EMPTY_ADDRESS) ? EMPTY_ADDRESS : nodeAddr - transAddr;
  fstCountingWriterPackUintIn(wrt, deltaAddr, nBytes); 
}
static uint8_t fstPackDetla(FstCountingWriter *wrt, CompiledAddr nodeAddr, CompiledAddr transAddr) {
  uint8_t nBytes = packDeltaSize(nodeAddr, transAddr);
  fstPackDeltaIn(wrt, nodeAddr, transAddr, nBytes); 
  return nBytes;
}

FstUnFinishedNodes *fstUnFinishedNodesCreate() {
  FstUnFinishedNodes *nodes = malloc(sizeof(FstUnFinishedNodes));
  if (nodes == NULL) { return NULL; }

  nodes->stack = (SArray *)taosArrayInit(64, sizeof(FstBuilderNodeUnfinished));
  fstUnFinishedNodesPushEmpty(nodes, false);
  return nodes;
}
void unFinishedNodeDestroyElem(void* elem) {
  FstBuilderNodeUnfinished *b = (FstBuilderNodeUnfinished*)elem;
  fstBuilderNodeDestroy(b->node); 
  free(b->last); 
  b->last = NULL;
}  
void fstUnFinishedNodesDestroy(FstUnFinishedNodes *nodes) {
  if (nodes == NULL) { return; } 

  taosArrayDestroyEx(nodes->stack, unFinishedNodeDestroyElem); 
  free(nodes);
}

void fstUnFinishedNodesPushEmpty(FstUnFinishedNodes *nodes, bool isFinal) {
  FstBuilderNode *node = malloc(sizeof(FstBuilderNode));
  node->isFinal     = isFinal;
  node->finalOutput = 0;
  node->trans       = taosArrayInit(16, sizeof(FstTransition));

  FstBuilderNodeUnfinished un = {.node = node, .last = NULL}; 
  taosArrayPush(nodes->stack, &un);
  
}
FstBuilderNode *fstUnFinishedNodesPopRoot(FstUnFinishedNodes *nodes) {
  assert(taosArrayGetSize(nodes->stack) == 1);

  FstBuilderNodeUnfinished *un = taosArrayPop(nodes->stack);
  assert(un->last == NULL); 
  return un->node;  
}

FstBuilderNode *fstUnFinishedNodesPopFreeze(FstUnFinishedNodes *nodes, CompiledAddr addr) {
  FstBuilderNodeUnfinished *un = taosArrayPop(nodes->stack);
  fstBuilderNodeUnfinishedLastCompiled(un, addr);
  //free(un->last); // TODO add func FstLastTransitionFree()
  //un->last = NULL;
  return un->node; 
}

FstBuilderNode *fstUnFinishedNodesPopEmpty(FstUnFinishedNodes *nodes) {
  FstBuilderNodeUnfinished *un = taosArrayPop(nodes->stack);
  assert(un->last == NULL); 
  return un->node;  
  
}
void fstUnFinishedNodesSetRootOutput(FstUnFinishedNodes *nodes, Output out) {
  FstBuilderNodeUnfinished *un = taosArrayGet(nodes->stack, 0);
  un->node->isFinal     = true;
  un->node->finalOutput = out;
  //un->node->trans       = NULL;  
} 
void fstUnFinishedNodesTopLastFreeze(FstUnFinishedNodes *nodes, CompiledAddr addr) {
  size_t sz = taosArrayGetSize(nodes->stack) - 1; 
  FstBuilderNodeUnfinished *un = taosArrayGet(nodes->stack, sz);
  fstBuilderNodeUnfinishedLastCompiled(un, addr);
}
void fstUnFinishedNodesAddSuffix(FstUnFinishedNodes *nodes, FstSlice bs, Output out) {
  FstSlice *s = &bs;
  if (fstSliceIsEmpty(s)) {
    return;
  }
  size_t sz = taosArrayGetSize(nodes->stack) - 1; 
  FstBuilderNodeUnfinished *un = taosArrayGet(nodes->stack, sz);
  assert(un->last == NULL);

  //FstLastTransition *trn = malloc(sizeof(FstLastTransition)); 
  //trn->inp = s->data[s->start]; 
  //trn->out = out;
  int32_t len = 0;
  uint8_t *data = fstSliceData(s, &len);
  un->last = fstLastTransitionCreate(data[0], out); 

  for (uint64_t i = 1; i < len; i++) {
    FstBuilderNode *n = malloc(sizeof(FstBuilderNode));
    n->isFinal     = false;
    n->finalOutput = 0;
    n->trans       = taosArrayInit(16, sizeof(FstTransition));
    
    //FstLastTransition *trn = malloc(sizeof(FstLastTransition)); 
    //trn->inp = s->data[i];
    //trn->out = out; 
    FstLastTransition *trn = fstLastTransitionCreate(data[i], 0);

    FstBuilderNodeUnfinished un = {.node = n, .last = trn}; 
    taosArrayPush(nodes->stack, &un); 
  }
  fstUnFinishedNodesPushEmpty(nodes, true);  
}


uint64_t fstUnFinishedNodesFindCommPrefix(FstUnFinishedNodes *node, FstSlice bs) {
  FstSlice *s = &bs;

  size_t ssz = taosArrayGetSize(node->stack);  // stack size
  uint64_t count = 0;
  int32_t  lsz;          // data len 
  uint8_t  *data = fstSliceData(s, &lsz);
  for (size_t i = 0; i < ssz && i < lsz; i++) {
    FstBuilderNodeUnfinished *un = taosArrayGet(node->stack, i); 
    if (un->last->inp == data[i]) {
      count++;
    } else {
      break;
    } 
  }
  return count;
}
uint64_t fstUnFinishedNodesFindCommPrefixAndSetOutput(FstUnFinishedNodes *node, FstSlice bs, Output in, Output *out) {
  FstSlice *s = &bs;

  size_t lsz = (size_t)(s->end - s->start + 1);          // data len 
  size_t ssz = taosArrayGetSize(node->stack);  // stack size
  *out = in;
  uint64_t i = 0;
  for (i = 0; i < lsz && i < ssz; i++) {
    FstBuilderNodeUnfinished *un = taosArrayGet(node->stack, i);

    FstLastTransition *t = un->last; 
    uint64_t addPrefix = 0;  
    uint8_t *data = fstSliceData(s, NULL);
    if (t && t->inp == data[i]) {
      uint64_t commPrefix = MIN(t->out, *out);      
      uint64_t tAddPrefix  = t->out - commPrefix; 
      (*out) = (*out) - commPrefix; 
      t->out = commPrefix;
      addPrefix = tAddPrefix; 
    } else {
       break;
    }
    if (addPrefix != 0) {
      if (i + 1 < ssz) {
        FstBuilderNodeUnfinished *unf = taosArrayGet(node->stack, i + 1);
        fstBuilderNodeUnfinishedAddOutputPrefix(unf, addPrefix);  
      }
    }
  }   
  return i;
} 


FstState fstStateCreateFrom(FstSlice* slice, CompiledAddr addr) {
  FstState fs = {.state = EmptyFinal, .val = 0};
  if (addr == EMPTY_ADDRESS) {
    return fs; 
  }
  
  uint8_t *data = fstSliceData(slice, NULL);
  uint8_t v = data[addr]; 
  uint8_t t = (v & 0b11000000) >> 6;
  if (t == 0b11) {
    fs.state = OneTransNext;
  } else if (t == 0b10) {
    fs.state = OneTrans; 
  } else {
    fs.state = AnyTrans;  
  }
  fs.val = v;
  return fs;
}

static FstState fstStateDict[] = {
  {.state = OneTransNext, .val = 0b11000000},
  {.state = OneTrans,     .val = 0b10000000},
  {.state = AnyTrans,     .val = 0b00000000},
  {.state = EmptyFinal,   .val = 0b00000000}
};
// debug 
static const char *fstStateStr[] = {"ONE_TRANS_NEXT", "ONE_TRANS", "ANY_TRANS", "EMPTY_FINAL"}; 

FstState fstStateCreate(State state){
  uint8_t idx = (uint8_t)state;
  return fstStateDict[idx];
}
//compile
void fstStateCompileForOneTransNext(FstCountingWriter *w, CompiledAddr addr, uint8_t inp) {
  FstState s = fstStateCreate(OneTransNext);  
  fstStateSetCommInput(&s, inp);

  bool null = false;
  uint8_t v = fstStateCommInput(&s, &null);
  if (null) {
    // w->write_all(&[inp])
    fstCountingWriterWrite(w, &inp, 1);
  }     
  fstCountingWriterWrite(w, &(s.val), 1);
  // w->write_all(&[s.val])
  return;
}
void fstStateCompileForOneTrans(FstCountingWriter *w, CompiledAddr addr, FstTransition* trn) {
  Output out = trn->out;   
  uint8_t outPackSize = (out == 0 ? 0 : fstCountingWriterPackUint(w, out));        
  uint8_t transPackSize = fstPackDetla(w, addr, trn->addr);      
  PackSizes packSizes = 0;

  FST_SET_OUTPUT_PACK_SIZE(packSizes, outPackSize);
  FST_SET_TRANSITION_PACK_SIZE(packSizes, transPackSize);
  fstCountingWriterWrite(w, (char *)&packSizes, sizeof(packSizes)); 

  FstState st = fstStateCreate(OneTrans); 
  
  fstStateSetCommInput(&st, trn->inp);
  bool null = false;
  uint8_t inp = fstStateCommInput(&st, &null);   
  if (null == true) {
    fstCountingWriterWrite(w, (char *)&trn->inp, sizeof(trn->inp));
  }
  fstCountingWriterWrite(w, (char *)(&(st.val)), sizeof(st.val));
  return ;

}
void fstStateCompileForAnyTrans(FstCountingWriter *w, CompiledAddr addr, FstBuilderNode *node) {
  size_t sz = taosArrayGetSize(node->trans);  
  assert(sz <= 256);

  uint8_t tSize = 0;
  uint8_t oSize = packSize(node->finalOutput) ;  
  
  // finalOutput.is_zero()
  bool anyOuts = (node->finalOutput != 0) ;
  for (size_t i = 0; i < sz; i++) {
    FstTransition *t = taosArrayGet(node->trans, i); 
    tSize = MAX(tSize, packDeltaSize(addr, t->addr)); 
    oSize = MAX(oSize, packSize(t->out));
    anyOuts = anyOuts || (t->out != 0); 
  }

  PackSizes packSizes = 0; 
  if (anyOuts) { FST_SET_OUTPUT_PACK_SIZE(packSizes, oSize); }
  else { FST_SET_OUTPUT_PACK_SIZE(packSizes, 0); }

  FST_SET_TRANSITION_PACK_SIZE(packSizes, tSize);
  
  FstState st = fstStateCreate(AnyTrans);
  fstStateSetFinalState(&st, node->isFinal); 
  fstStateSetStateNtrans(&st, (uint8_t)sz);
   
  if (anyOuts) {
    if (FST_BUILDER_NODE_IS_FINAL(node)) {
      fstCountingWriterPackUintIn(w, node->finalOutput, oSize);
    }
    for (int32_t i = sz - 1; i >= 0; i--) {
      FstTransition *t = taosArrayGet(node->trans, i);
      fstCountingWriterPackUintIn(w, t->out, oSize);
    }
  } 
  for (int32_t i = sz - 1; i >= 0; i--) {
      FstTransition *t = taosArrayGet(node->trans, i);
      fstPackDeltaIn(w, addr, t->addr, tSize);
  }
  for (int32_t i = sz - 1; i >= 0; i--) {
     FstTransition *t = taosArrayGet(node->trans, i);
     fstCountingWriterWrite(w, (char *)&t->inp, 1);
      //fstPackDeltaIn(w, addr, t->addr, tSize);
  }
  if (sz > TRANS_INDEX_THRESHOLD) {
    // A value of 255 indicates that no transition exists for the byte
    // at that index. (Except when there are 256 transitions.) Namely,
    // any value greater than or equal to the number of transitions in
    // this node indicates an absent transition.
    uint8_t *index = (uint8_t *)malloc(sizeof(uint8_t) * 256); 
    for (uint8_t i = 0; i < 256; i++) {
      index[i] = 255;
    }
    for (size_t i = 0; i < sz; i++) {
      FstTransition *t = taosArrayGet(node->trans, i);
      index[t->inp] = i;
      fstCountingWriterWrite(w, (char *)index, sizeof(index)); 
      //fstPackDeltaIn(w, addr, t->addr, tSize);
    }
    free(index);
  }
  fstCountingWriterWrite(w, (char *)&packSizes, 1);
  bool null = false;
  fstStateStateNtrans(&st, &null);
  if (null == true) {
     // 256 can't be represented in a u8, so we abuse the fact that
     // the # of transitions can never be 1 here, since 1 is always
     // encoded in the state byte.
    uint8_t v = 1;
    if (sz == 256) { fstCountingWriterWrite(w, (char *)&v, 1); }
    else { fstCountingWriterWrite(w, (char *)&sz, 1); }
  }
  fstCountingWriterWrite(w, (char *)(&(st.val)), 1);
  return;
}

// set_comm_input
void fstStateSetCommInput(FstState* s, uint8_t inp) {
  assert(s->state == OneTransNext || s->state == OneTrans);

  uint8_t val;
  COMMON_INDEX(inp, 0x111111, val); 
  s->val = (s->val & fstStateDict[s->state].val) | val;    
}

// comm_input
uint8_t fstStateCommInput(FstState* s, bool *null) {
  assert(s->state == OneTransNext || s->state == OneTrans);
  uint8_t v = s->val & 0b00111111;
  if (v == 0) { 
    *null = true; 
    return v;
  } 
  //v = 0 indicate that common_input is None
  return v == 0 ? 0 : COMMON_INPUT(v);  
}

// input_len

uint64_t fstStateInputLen(FstState* s) {
  assert(s->state == OneTransNext || s->state == OneTrans);
  bool null = false;
  fstStateCommInput(s, &null); 
  return null ? 1 : 0 ;
} 
  
// end_addr 
uint64_t fstStateEndAddrForOneTransNext(FstState* s, FstSlice *data) {
  assert(s->state == OneTransNext);
  return FST_SLICE_LEN(data) - 1 - fstStateInputLen(s);
}
uint64_t fstStateEndAddrForOneTrans(FstState *s, FstSlice *data, PackSizes sizes) {
  assert(s->state == OneTrans);
  return FST_SLICE_LEN(data) 
                - 1 
                - fstStateInputLen(s) 
                - 1 // pack size 
                - FST_GET_TRANSITION_PACK_SIZE(sizes) 
                - FST_GET_OUTPUT_PACK_SIZE(sizes); 
}
uint64_t fstStateEndAddrForAnyTrans(FstState *state, uint64_t version, FstSlice *date, PackSizes sizes, uint64_t nTrans) {
  uint8_t oSizes = FST_GET_OUTPUT_PACK_SIZE(sizes); 
  uint8_t finalOsize = !fstStateIsFinalState(state) ? 0 : oSizes;    
  return FST_SLICE_LEN(date)  
               - 1 
               - fstStateNtransLen(state) 
               - 1 //pack size 
               - fstStateTotalTransSize(state, version, sizes, nTrans) 
               - nTrans * oSizes  // output values
               - finalOsize;     // final output 
}
// input  
uint8_t  fstStateInput(FstState *s, FstNode *node) {
  assert(s->state == OneTransNext || s->state == OneTrans);
  FstSlice *slice = &node->data;
  bool null = false;
  uint8_t inp = fstStateCommInput(s, &null);
  uint8_t *data = fstSliceData(slice, NULL); 
  return null == false ? inp : data[-1];
}
uint8_t  fstStateInputForAnyTrans(FstState *s, FstNode *node, uint64_t i) {
  assert(s->state == AnyTrans);
  FstSlice *slice = &node->data; 

  uint64_t at = node->start 
            - fstStateNtransLen(s) 
            - 1  // pack size
            - fstStateTransIndexSize(s, node->version, node->nTrans) 
            - i 
            - 1; // the output size 

  uint8_t *data = fstSliceData(slice, NULL);
  return data[at];
}

// trans_addr
CompiledAddr fstStateTransAddr(FstState *s, FstNode *node) {
  assert(s->state == OneTransNext || s->state == OneTrans);
  FstSlice *slice = &node->data;
  if (s->state == OneTransNext) {
    return (CompiledAddr)(node->end) - 1;      
  } else {
    PackSizes sizes = node->sizes; 
    uint8_t tSizes = FST_GET_TRANSITION_PACK_SIZE(sizes);  
    uint64_t i = node->start 
                - fstStateInputLen(s)
                - 1 // PackSizes
                - tSizes;

    // refactor error logic 
    uint8_t *data = fstSliceData(slice, NULL);
    return unpackDelta(data +i, tSizes, node->end);      
  }  
}
CompiledAddr fstStateTransAddrForAnyTrans(FstState *s, FstNode *node, uint64_t i) {
  assert(s->state == AnyTrans);

  FstSlice *slice = &node->data;
  uint8_t tSizes = FST_GET_TRANSITION_PACK_SIZE(node->sizes);
  uint64_t at = node->start  
               - fstStateNtransLen(s)
               - 1
               - fstStateTransIndexSize(s, node->version, node->nTrans)
               - node->nTrans
               - (i * tSizes)
               - tSizes;
  uint8_t *data = fstSliceData(slice, NULL);
  return unpackDelta(data + at, tSizes, node->end); 
}

// sizes 
PackSizes fstStateSizes(FstState *s, FstSlice *slice) {
  assert(s->state == OneTrans || s->state == AnyTrans) ;
  uint64_t i; 
  if (s->state == OneTrans) {
    i = FST_SLICE_LEN(slice) - 1 - fstStateInputLen(s) - 1;  
  } else {
    i = FST_SLICE_LEN(slice) - 1 - fstStateNtransLen(s) - 1;
  }

  uint8_t *data = fstSliceData(slice, NULL);
  return (PackSizes)(*(data +i));
}
// Output 
Output fstStateOutput(FstState *s, FstNode *node) {
  assert(s->state == OneTrans);  
  
  uint8_t oSizes = FST_GET_OUTPUT_PACK_SIZE(node->sizes);
  if (oSizes == 0) {
    return 0;
  }
  FstSlice *slice = &node->data;
  uint8_t tSizes = FST_GET_TRANSITION_PACK_SIZE(node->sizes);

  uint64_t i = node->start 
                - fstStateInputLen(s)
                - 1
                - tSizes 
                - oSizes;
  uint8_t *data = fstSliceData(slice, NULL);              
  return unpackUint64(data + i, oSizes);
  
}
Output fstStateOutputForAnyTrans(FstState *s, FstNode *node, uint64_t i) {
  assert(s->state == AnyTrans);

  uint8_t oSizes = FST_GET_OUTPUT_PACK_SIZE(node->sizes); 
  if (oSizes == 0) {
    return 0;    
  }  
  FstSlice *slice = &node->data;
  uint64_t at = node->start
                - fstStateNtransLen(s)
                - 1 // pack size
                - fstStateTotalTransSize(s, node->version, node->sizes, node->nTrans)
                - (i * oSizes)
                - oSizes;

  uint8_t *data = fstSliceData(slice, NULL);              
  return unpackUint64(data + at, oSizes);
}

// anyTrans specify function

void fstStateSetFinalState(FstState *s, bool yes) {
  assert(s->state == AnyTrans); 
  if (yes) { s->val |= 0b01000000; } 
  return;
}
bool fstStateIsFinalState(FstState *s) {
  assert(s->state == AnyTrans); 
  return (s->val & 0b01000000) == 0b01000000; 
} 

void fstStateSetStateNtrans(FstState *s, uint8_t n) {
  assert(s->state == AnyTrans); 
  if (n <= 0b00111111) {
    s->val = (s->val & 0b11000000) | n;  
  } 
  return;
}
// state_ntrans
uint8_t fstStateStateNtrans(FstState *s, bool *null) {
  assert(s->state == AnyTrans); 
  *null = false;
  uint8_t n = s->val & 0b00111111; 

  if (n == 0) {
    *null = true; // None
  }  
  return n;
}
uint64_t fstStateTotalTransSize(FstState *s, uint64_t version, PackSizes sizes, uint64_t nTrans) {
  assert(s->state == AnyTrans); 
  uint64_t idxSize = fstStateTransIndexSize(s, version, nTrans); 
  return nTrans + (nTrans * FST_GET_TRANSITION_PACK_SIZE(sizes)) + idxSize;
}
uint64_t fstStateTransIndexSize(FstState *s, uint64_t version, uint64_t nTrans) {
  assert(s->state == AnyTrans); 
  return (version >= 2 &&nTrans > TRANS_INDEX_THRESHOLD) ?  256 : 0;
}
uint64_t fstStateNtransLen(FstState *s) {
  assert(s->state == AnyTrans);
  bool null = false;
  fstStateStateNtrans(s, &null);
  return null == true ?  1 : 0; 
}
uint64_t fstStateNtrans(FstState *s, FstSlice *slice) {
  bool null = false; 
  uint8_t n = fstStateStateNtrans(s, &null);
  if (null != true) {
    return n;   
  }  
  int32_t len;
  uint8_t *data = fstSliceData(slice, &len);
  n = data[len - 2];
  //n = data[slice->end - 1]; // data[data.len() - 2]
  return n == 1 ? 256: n; // // "1" is never a normal legal value here, because if there, // is only 1 transition, then it is encoded in the state byte  
}
Output  fstStateFinalOutput(FstState *s, uint64_t version, FstSlice *slice, PackSizes sizes, uint64_t nTrans) {
   uint8_t oSizes = FST_GET_OUTPUT_PACK_SIZE(sizes);
   if (oSizes == 0 || !fstStateIsFinalState(s)) {
      return 0;
   }
   
   uint64_t at = FST_SLICE_LEN(slice) 
                 - 1 
                 - fstStateNtransLen(s)
                 - fstStateTotalTransSize(s, version, sizes, nTrans)
                 - (nTrans * oSizes)
                 - oSizes;
  uint8_t *data = fstSliceData(slice, NULL);
  return unpackUint64(data + at, (uint8_t)oSizes);    

}
uint64_t fstStateFindInput(FstState *s, FstNode *node, uint8_t b, bool *null) {
  assert(s->state == AnyTrans);
  FstSlice *slice = &node->data;
  if (node->version >= 2 && node->nTrans > TRANS_INDEX_THRESHOLD) {
    uint64_t at = node->start
                  - fstStateNtransLen(s)
                  - 1 // pack size 
                  - fstStateTransIndexSize(s, node->version, node->nTrans);
    int32_t dlen = 0; 
    uint8_t *data = fstSliceData(slice, &dlen);
    uint64_t i = data[at + b];
    //uint64_t i = slice->data[slice->start + at + b];  
    if (i >= node->nTrans) {
      *null = true;
    } 
    return i;
  } else {
    uint64_t start = node->start 
                    - fstStateNtransLen(s)
                    - 1 // pack size
                    - node->nTrans;
    uint64_t end =  start + node->nTrans;
    FstSlice t = fstSliceCopy(slice, start, end - 1);
    int32_t len = 0; 
    uint8_t *data = fstSliceData(&t, &len);
    for(int i = 0; i < len; i++) {
      //uint8_t v = slice->data[slice->start + i];
      ////slice->data[slice->start + i];
      uint8_t v = data[i]; 
      if (v == b) {
        return node->nTrans - i - 1; // bug  
      }
    } 
  } 
}


// fst node function 

FstNode *fstNodeCreate(int64_t version, CompiledAddr addr, FstSlice *slice) {
  FstNode *n = (FstNode *)malloc(sizeof(FstNode)); 
  if (n == NULL) { return NULL; }

  FstState st = fstStateCreateFrom(slice, addr);  

  if (st.state == EmptyFinal) {
     n->data    = fstSliceCreate(NULL, 0);   
     n->version = version;
     n->state   = st;  
     n->start   = EMPTY_ADDRESS;
     n->end     = EMPTY_ADDRESS;  
     n->isFinal = true; 
     n->nTrans  = 0;
     n->sizes   = 0;  
     n->finalOutput = 0;  
  } else if (st.state == OneTransNext) {
     n->data    = fstSliceCopy(slice, 0, addr);     
     n->version = version;
     n->state   = st;  
     n->start   = addr;
     n->end     = fstStateEndAddrForOneTransNext(&st, &n->data); //? s.end_addr(data); 
     n->isFinal = false;
     n->sizes   = 0;
     n->nTrans  = 1;
     n->finalOutput = 0;
  } else if (st.state == OneTrans) {
     FstSlice data = fstSliceCopy(slice, 0, addr); 
     PackSizes sz = fstStateSizes(&st, &data);
     n->data    =   fstSliceCopy(slice, 0, addr); 
     n->version = version; 
     n->state   = st; 
     n->start   = addr;
     n->end     = fstStateEndAddrForOneTrans(&st, &data, sz); // s.end_addr(data, sz);
     n->isFinal = false; 
     n->nTrans  = 1; 
     n->sizes   = sz;   
     n->finalOutput = 0; 
  } else {
     FstSlice data = fstSliceCopy(slice, 0, addr);
     uint64_t sz = fstStateSizes(&st, &data);    // s.sizes(data)
     uint32_t nTrans = fstStateNtrans(&st, &data); // s.ntrans(data)  
     n->data    = data; 
     n->version = version;
     n->state   = st;
     n->start   = addr;
     n->end     = fstStateEndAddrForAnyTrans(&st, version, &data, sz, nTrans); // s.end_addr(version, data, sz, ntrans);
     n->isFinal = fstStateIsFinalState(&st); // s.is_final_state();
     n->nTrans  = nTrans;
     n->sizes   = sz;
     n->finalOutput = fstStateFinalOutput(&st, version, &data, sz, nTrans); // s.final_output(version, data, sz, ntrans);
  }
   return n; 
}

// debug state transition
static const char *fstNodeState(FstNode *node) {
  FstState *st = &node->state; 
  return fstStateStr[st->state]; 
}


void fstNodeDestroy(FstNode *node) {
  fstSliceDestroy(&node->data); 
  free(node);
}
FstTransitions* fstNodeTransitions(FstNode *node) {
  FstTransitions *t = malloc(sizeof(FstTransitions));
  if (NULL == t) {
    return NULL; 
  }
  FstRange range = {.start = 0, .end = FST_NODE_LEN(node)};
  t->range = range;
  t->node  = node;
  return t; 
} 

// Returns the transition at index `i`. 
bool fstNodeGetTransitionAt(FstNode *node, uint64_t i, FstTransition *trn) {
  bool s = true;
  FstState *st = &node->state;
  if (st->state == OneTransNext) {
    trn->inp  = fstStateInput(st, node); 
    trn->out  = 0;
    trn->addr = fstStateTransAddr(st, node); 
  } else if (st->state == OneTrans) {
    trn->inp  = fstStateInput(st, node);    
    trn->out  = fstStateOutput(st, node);
    trn->addr = fstStateTransAddr(st, node); 
  } else if (st->state == AnyTrans) {
    trn->inp  = fstStateInputForAnyTrans(st, node, i);
    trn->out  = fstStateOutputForAnyTrans(st, node, i);
    trn->addr = fstStateTransAddrForAnyTrans(st, node, i); 
  } else {
    s = false;
  }
  return s;
} 

// Returns the transition address of the `i`th transition
bool fstNodeGetTransitionAddrAt(FstNode *node, uint64_t i, CompiledAddr *res) {
  bool s = true;
  FstState *st = &node->state;
  if (st->state == OneTransNext) {
    assert(i == 0);
    fstStateTransAddr(st, node);
  } else if (st->state == OneTrans) {
    assert(i == 0); 
    fstStateTransAddr(st, node);
  } else if (st->state == AnyTrans) {
    fstStateTransAddrForAnyTrans(st, node, i);
  } else if (FST_STATE_EMPTY_FINAL(node)){
    s = false;
  } else {
    assert(0);
  }
  return s;
}

//  Finds the `i`th transition corresponding to the given input byte.
//  If no transition for this byte exists, then `false` is returned. 
bool fstNodeFindInput(FstNode *node, uint8_t b, uint64_t *res) {
  bool s = true;
  FstState *st = &node->state;
  if (st->state == OneTransNext) {
    if (fstStateInput(st,node) == b) { *res = 0; } 
    else { s = false; } } 
  else if (st->state == OneTrans) {
    if (fstStateInput(st, node) == b) { *res = 0 ;}
    else {  s = false; }
  } else if (st->state == AnyTrans) {
    bool null = false;
    uint64_t out = fstStateFindInput(st, node, b, &null); 
    if (null == false) { *res = out; } 
    else { s = false;}
  }
  return s;
} 

bool fstNodeCompile(FstNode *node, void *w, CompiledAddr lastAddr, CompiledAddr addr, FstBuilderNode *builderNode) {
  size_t sz = taosArrayGetSize(builderNode->trans);  
  assert(sz < 256);
  if (sz == 0 && builderNode->isFinal && builderNode->finalOutput == 0) {
    return true; 
  } else if (sz != 1 || builderNode->isFinal) {
     fstStateCompileForAnyTrans(w, addr, builderNode); 
    // AnyTrans->Compile(w, addr, node);
  } else {
    FstTransition *tran = taosArrayGet(builderNode->trans, 0);   
    if (tran->addr == lastAddr && tran->out == 0) {
       fstStateCompileForOneTransNext(w, addr, tran->inp);
       //OneTransNext::compile(w, lastAddr, tran->inp);
       return true;
    } else {
       fstStateCompileForOneTrans(w, addr, tran);
      //OneTrans::Compile(w, lastAddr, *tran);
       return true;
    } 
  } 
  return true; 
} 

bool fstBuilderNodeCompileTo(FstBuilderNode *b, FstCountingWriter *wrt, CompiledAddr lastAddr, CompiledAddr startAddr) {
  return fstNodeCompile(NULL, wrt, lastAddr, startAddr, b);
}



FstBuilder *fstBuilderCreate(void *w, FstType ty) {
  FstBuilder *b = malloc(sizeof(FstBuilder));  
  if (NULL == b) { return b; }

   
  b->wrt = fstCountingWriterCreate(w, false);
  b->unfinished = fstUnFinishedNodesCreate();   
  b->registry   = fstRegistryCreate(10000, 2) ;
  b->last       = fstSliceCreate(NULL, 0);
  b->lastAddr   = NONE_ADDRESS; 
  b->len        = 0;
  
  char buf64[8] = {0}; 
  void *pBuf64 = buf64;
  taosEncodeFixedU64(&pBuf64, VERSION); 
  fstCountingWriterWrite(b->wrt, buf64, sizeof(buf64));
  
  memset(buf64, 0, sizeof(buf64)); 
  pBuf64 = buf64;
  taosEncodeFixedU64(&pBuf64, ty); 
  fstCountingWriterWrite(b->wrt, buf64, sizeof(buf64));

  return b;
}
void fstBuilderDestroy(FstBuilder *b) {
  if (b == NULL) { return; }

  fstCountingWriterDestroy(b->wrt); 
  fstUnFinishedNodesDestroy(b->unfinished); 
  fstRegistryDestroy(b->registry);
  free(b);
}


bool fstBuilderInsert(FstBuilder *b, FstSlice bs, Output in) {
  OrderType t = fstBuilderCheckLastKey(b, bs, true);  
  if (t == Ordered) {
    // add log info
    fstBuilderInsertOutput(b, bs, in); 
    return true; 
  } 
  indexInfo("key must be ordered");
  return false;
}

void fstBuilderInsertOutput(FstBuilder *b, FstSlice bs, Output in) {
   FstSlice *s = &bs;
   if (fstSliceIsEmpty(s)) {
     b->len = 1; 
     fstUnFinishedNodesSetRootOutput(b->unfinished, in);
     return;
   }
   //if (in != 0) { //if let Some(in) = in 
   //   prefixLen = fstUnFinishedNodesFindCommPrefixAndSetOutput(b->unfinished, bs, in, &out);  
   //} else {
   //   prefixLen = fstUnFinishedNodesFindCommPrefix(b->unfinished, bs);
   //   out = 0;
   //}
   Output out;  
   uint64_t prefixLen = fstUnFinishedNodesFindCommPrefixAndSetOutput(b->unfinished, bs, in, &out);
  
   if (prefixLen == FST_SLICE_LEN(s)) {
      assert(out == 0);
      return;
   }

   b->len += 1;
   fstBuilderCompileFrom(b, prefixLen); 
   
   FstSlice sub = fstSliceCopy(s, prefixLen, s->end);
   fstUnFinishedNodesAddSuffix(b->unfinished, sub, out);
   fstSliceDestroy(&sub);
   return;
 }

OrderType fstBuilderCheckLastKey(FstBuilder *b, FstSlice bs, bool ckDup) {
  FstSlice *input = &bs;
  if (fstSliceIsEmpty(&b->last)) {
    // deep copy or not
    b->last = fstSliceCopy(&bs, input->start, input->end);
  } else {
    int comp = fstSliceCompare(&b->last, &bs);
    if (comp == 0 && ckDup) {
      return DuplicateKey;  
    } else if (comp == 1) {
      return OutOfOrdered;
    }
    // deep copy or not
    b->last = fstSliceCopy(&bs, input->start, input->end); 
  }       
  return Ordered;
} 
void fstBuilderCompileFrom(FstBuilder *b, uint64_t istate) {
  CompiledAddr addr = NONE_ADDRESS;
  while (istate + 1 < FST_UNFINISHED_NODES_LEN(b->unfinished)) {
    FstBuilderNode *n = NULL;
    if (addr == NONE_ADDRESS) {
      n = fstUnFinishedNodesPopEmpty(b->unfinished);
    } else {
      n = fstUnFinishedNodesPopFreeze(b->unfinished, addr);
    }
    addr = fstBuilderCompile(b, n);
    assert(addr != NONE_ADDRESS);      
    //fstBuilderNodeDestroy(n);
  }
  fstUnFinishedNodesTopLastFreeze(b->unfinished, addr);
  return; 
}
CompiledAddr fstBuilderCompile(FstBuilder *b, FstBuilderNode *bn) {
  if (FST_BUILDER_NODE_IS_FINAL(bn) 
      && FST_BUILDER_NODE_TRANS_ISEMPTY(bn) 
      && FST_BUILDER_NODE_FINALOUTPUT_ISZERO(bn)) {
    return EMPTY_ADDRESS; 
  }
  FstRegistryEntry *entry = fstRegistryGetEntry(b->registry, bn); 
  if (entry->state == FOUND) { 
    CompiledAddr ret = entry->addr;
    fstRegistryEntryDestroy(entry);
    return ret;
  } 
  CompiledAddr startAddr = (CompiledAddr)(FST_WRITER_COUNT(b->wrt));

  fstBuilderNodeCompileTo(bn, b->wrt, b->lastAddr, startAddr);  
  b->lastAddr =  (CompiledAddr)(FST_WRITER_COUNT(b->wrt) - 1);  
  if (entry->state == NOTFOUND) {
    FST_REGISTRY_CELL_INSERT(entry->cell, b->lastAddr);    
  }
  fstRegistryEntryDestroy(entry);
  
  return b->lastAddr;  
}

void* fstBuilderInsertInner(FstBuilder *b) {
  fstBuilderCompileFrom(b, 0);  
  FstBuilderNode *rootNode = fstUnFinishedNodesPopRoot(b->unfinished); 
  CompiledAddr  rootAddr = fstBuilderCompile(b, rootNode);

  char  buf64[8] = {0}; 

  void  *pBuf64 = buf64; 
  taosEncodeFixedU64(&pBuf64, b->len); 
  fstCountingWriterWrite(b->wrt, buf64, sizeof(buf64));  
    
  pBuf64 = buf64;
  taosEncodeFixedU64(&pBuf64, rootAddr);  
  fstCountingWriterWrite(b->wrt, buf64, sizeof(buf64));  

  char buf32[4] = {0};
  void *pBuf32  = buf32; 
  uint32_t sum = fstCountingWriterMaskedCheckSum(b->wrt);
  taosEncodeFixedU32(&pBuf32, sum); 
  fstCountingWriterWrite(b->wrt, buf32, sizeof(buf32));  
  
  fstCountingWriterFlush(b->wrt);
  //fstCountingWriterDestroy(b->wrt);  
  //b->wrt = NULL;
  return b->wrt;
}
void fstBuilderFinish(FstBuilder *b) {
  fstBuilderInsertInner(b);
}



FstSlice fstNodeAsSlice(FstNode *node) {
  FstSlice *slice = &node->data; 
  FstSlice s = fstSliceCopy(slice, slice->end, FST_SLICE_LEN(slice) - 1);   
  return s; 
}

FstLastTransition *fstLastTransitionCreate(uint8_t inp, Output out) {
  FstLastTransition *trn = malloc(sizeof(FstLastTransition));
  if (trn == NULL) { return NULL; }

  trn->inp = inp;
  trn->out = out;
  return trn;
}

void fstLastTransitionDestroy(FstLastTransition *trn) {
  free(trn);
}
void fstBuilderNodeUnfinishedLastCompiled(FstBuilderNodeUnfinished *unNode, CompiledAddr addr) {
  FstLastTransition *trn = unNode->last;       
  if (trn == NULL) { return; }  
  FstTransition t = {.inp = trn->inp, .out = trn->out, .addr = addr};      
  taosArrayPush(unNode->node->trans, &t); 
  fstLastTransitionDestroy(trn); 
  unNode->last = NULL;
  return;
}

void fstBuilderNodeUnfinishedAddOutputPrefix(FstBuilderNodeUnfinished *unNode, Output out) {
  if (FST_BUILDER_NODE_IS_FINAL(unNode->node)) {
    unNode->node->finalOutput += out;  
  }
  size_t sz = taosArrayGetSize(unNode->node->trans);
  for (size_t i = 0; i < sz; i++) {
    FstTransition *trn = taosArrayGet(unNode->node->trans, i); 
    trn->out += out;
  }
  if (unNode->last) {
    unNode->last->out += out;  
  }
  return;
}

Fst* fstCreate(FstSlice *slice) {
  int32_t slen;
  char *buf = fstSliceData(slice, &slen);
  if (slen < 36) { 
    return NULL; 
  }
  uint64_t len = slen;
  uint64_t skip = 0;  

  uint64_t version; 
  taosDecodeFixedU64(buf, &version); 
  skip += sizeof(version); 
  if (version == 0 || version > VERSION) {
    return NULL; 
  }  

  uint64_t type;
  taosDecodeFixedU64(buf + skip, &type);
  skip += sizeof(type); 

  uint32_t checkSum = 0;
  len -= sizeof(checkSum);
  taosDecodeFixedU32(buf + len, &checkSum); 

  CompiledAddr rootAddr;
  len -= sizeof(rootAddr); 
  taosDecodeFixedU64(buf + len, &rootAddr); 

  uint64_t fstLen; 
  len -= sizeof(fstLen); 
  taosDecodeFixedU64(buf + len, &fstLen);
  //TODO(validat root addr)
  // 
  Fst *fst= (Fst *)calloc(1, sizeof(Fst)); 
  if (fst == NULL) { return NULL; }  
  
  fst->meta = (FstMeta *)malloc(sizeof(FstMeta));
  if (NULL == fst->meta) { 
    goto FST_CREAT_FAILED; 
  }

  fst->meta->version   = version;  
  fst->meta->rootAddr = rootAddr; 
  fst->meta->ty       = type;
  fst->meta->len      = fstLen;
  fst->meta->checkSum = checkSum;
  fst->data = slice; 
  return fst;

FST_CREAT_FAILED: 
  free(fst->meta); 
  free(fst);

}
void fstDestroy(Fst *fst) {
  if (fst) { 
    free(fst->meta); 
    fstNodeDestroy(fst->root);  
  } 
  free(fst); 
}

bool fstGet(Fst *fst, FstSlice *b, Output *out) {
  FstNode *root = fstGetRoot(fst);    
  Output tOut = 0; 
  int32_t len;
  uint8_t *data = fstSliceData(b, &len);
  for (uint32_t i = 0; i < len; i++) {
    uint8_t inp = data[i];
    Output  res = 0;
    if (false == fstNodeFindInput(root, inp, &res)) {
      return false; 
    }   

    FstTransition trn; 
    fstNodeGetTransitionAt(root, res, &trn);
    tOut += trn.out; 
    root = fstGetNode(fst, trn.addr);
  }
  if (!FST_NODE_IS_FINAL(root)) {
    return false;
  } else {
    tOut = tOut + FST_NODE_FINAL_OUTPUT(root); 
  }
  *out = tOut;
  
  return true; 
}

FstNode *fstGetRoot(Fst *fst) {
  if (fst->root != NULL) {
    return fst->root;
  }
  CompiledAddr rAddr = fstGetRootAddr(fst); 
  fst->root =  fstGetNode(fst, rAddr);
  return fst->root;
}
FstNode* fstGetNode(Fst *fst, CompiledAddr addr) {
  return fstNodeCreate(fst->meta->version, addr, fst->data); 
  
}
FstType fstGetType(Fst *fst) {
  return fst->meta->ty;
}
CompiledAddr fstGetRootAddr(Fst *fst) {
  return fst->meta->rootAddr;
} 

Output fstEmptyFinalOutput(Fst *fst, bool *null) {
  Output res = 0;
  FstNode *node = fst->root;
  if (FST_NODE_IS_FINAL(node)) {
    *null = false;
    res = FST_NODE_FINAL_OUTPUT(node); 
  } else {
    *null = true;
  }
  return res;
}

bool fstVerify(Fst *fst) {
  uint32_t checkSum = fst->meta->checkSum;
  int32_t len;
  uint8_t *data = fstSliceData(fst->data, &len);
  TSCKSUM initSum  = 0;  
  if (!taosCheckChecksumWhole(data, len)) {
    return false;
  }
  return true;
}

// data bound function
FstBoundWithData* fstBoundStateCreate(FstBound type, FstSlice *data) {
  FstBoundWithData *b = calloc(1, sizeof(FstBoundWithData));
  if (b == NULL) { return NULL; }

  if (data != NULL) {
    b->data = fstSliceCopy(data, data->start, data->end);
  } else {
    b->data = fstSliceCreate(NULL, 0);
  }
  b->type = type;

  return b; 
}

bool fstBoundWithDataExceededBy(FstBoundWithData *bound, FstSlice *slice) {
  int comp = fstSliceCompare(slice, &bound->data);
  if (bound->type == Included) {
    return comp > 0 ? true : false;
  } else if (bound->type == Excluded) {
    return comp >= 0 ? true : false;
  } else {
    return true;
  }
}
bool fstBoundWithDataIsEmpty(FstBoundWithData *bound) {
  if (bound->type == Unbounded) {
    return true;
  } else { 
    return fstSliceIsEmpty(&bound->data);  
  }     
}


bool fstBoundWithDataIsIncluded(FstBoundWithData *bound) {
  return bound->type == Included ? true : false;
}

void fstBoundDestroy(FstBoundWithData *bound) {
  free(bound);
}

StreamWithState *streamWithStateCreate(Fst *fst, Automation *automation, FstBoundWithData *min, FstBoundWithData *max) {
  StreamWithState *sws = calloc(1, sizeof(StreamWithState));
  if (sws == NULL) { return NULL; } 

  sws->fst  = fst;
  sws->aut  = automation; 
  sws->inp  = (SArray *)taosArrayInit(256, sizeof(uint8_t));  
  
  sws->emptyOutput.null = false;
  sws->emptyOutput.out  = 0;

  sws->stack = (SArray *)taosArrayInit(256, sizeof(StreamState)); 
  sws->endAt = max; 
  streamWithStateSeekMin(sws, min);

  return sws;
}
void streamWithStateDestroy(StreamWithState *sws) {
  if (sws == NULL) { return; }

  taosArrayDestroy(sws->inp);
  taosArrayDestroyEx(sws->stack, streamStateDestroy);

  free(sws); 
}

bool streamWithStateSeekMin(StreamWithState *sws, FstBoundWithData *min) {
  if (fstBoundWithDataIsEmpty(min)) {
    if (fstBoundWithDataIsIncluded(min)) {
       sws->emptyOutput.out = fstEmptyFinalOutput(sws->fst, &(sws->emptyOutput.null));
    } 
    StreamState s = {.node     = fstGetRoot(sws->fst),
                     .trans    = 0,
                     .out      = {.null = false, .out = 0},
                     .autState = sws->aut->start()}; // auto.start callback 
    taosArrayPush(sws->stack, &s);
    return true;
  } 
  FstSlice *key = NULL;
  bool inclusize = false;;

  if (min->type == Included) {
    key = &min->data;
    inclusize = true;
  } else if (min->type == Excluded) {
    key = &min->data;
  } else {
    return false;
  }

  FstNode *node = fstGetRoot(sws->fst); 
  Output  out = 0;
  void*   autState = sws->aut->start();  

  int32_t len; 
  uint8_t *data = fstSliceData(key, &len);  
  for (uint32_t i = 0; i < len; i++) {
    uint8_t b = data[i];
    uint64_t res = 0;
    bool null = fstNodeFindInput(node, b, &res); 
    if (null == false) {
      FstTransition trn;
      fstNodeGetTransitionAt(node, res, &trn);   
      void *preState = autState;
      autState = sws->aut->accept(preState, b);
      taosArrayPush(sws->inp, &b);
      StreamState s = {.node     = node, 
                       .trans    = res + 1, 
                       .out      = {.null = false, .out = out}, 
                       .autState = preState}; 
      taosArrayPush(sws->stack, &s);
      out += trn.out;
      node = fstGetNode(sws->fst, trn.addr);  
    } else {

      // This is a little tricky. We're in this case if the
      // given bound is not a prefix of any key in the FST.
      // Since this is a minimum bound, we need to find the
      // first transition in this node that proceeds the current
      // input byte. 
      FstTransitions *trans = fstNodeTransitions(node);
      uint64_t i = 0; 
      for (i = trans->range.start; i < trans->range.end; i++) {
        FstTransition trn;
        if (fstNodeGetTransitionAt(node, i, &trn) && trn.inp > b) {
          break;   
        }  
      }
      
      StreamState s = {.node     = node, 
                       .trans    = i, 
                       .out      = {.null = false, .out = out}, 
                       .autState = autState}; 
      taosArrayPush(sws->stack, &s);  
       return true;  
    }
  }
  uint32_t sz = taosArrayGetSize(sws->stack); 
  if (sz != 0) {
    StreamState *s = taosArrayGet(sws->stack, sz - 1); 
    if (inclusize) {
      s->trans -= 1;
      taosArrayPop(sws->inp);
    } else {
      FstNode *n = s->node;  
      uint64_t trans = s->trans; 
      FstTransition trn;  
      fstNodeGetTransitionAt(n, trans - 1, &trn);
      StreamState s = {.node = fstGetNode(sws->fst, trn.addr),
                       .trans= 0,
                       .out  = {.null = false, .out = out},
                       .autState = autState}; 
      taosArrayPush(sws->stack, &s);
      return true; 
   }
   return false;
  } 
}          

StreamWithStateResult *streamWithStateNextWith(StreamWithState *sws, StreamCallback callback) {
  FstOutput output = sws->emptyOutput; 
  if (output.null == false) {
    FstSlice emptySlice = fstSliceCreate(NULL, 0);   
    if (fstBoundWithDataExceededBy(sws->endAt, &emptySlice)) {
      taosArrayDestroyEx(sws->stack, streamStateDestroy);
      sws->stack = (SArray *)taosArrayInit(256, sizeof(StreamState)); 
      return NULL;
    }
    void* start = sws->aut->start();
    if (sws->aut->isMatch(start)) { 
      FstSlice s = fstSliceCreate(NULL, 0);
      return swsResultCreate(&s, output, callback(start));
    }
  }
  while (taosArrayGetSize(sws->stack) > 0) {
    StreamState *p = (StreamState *)taosArrayPop(sws->stack);     
    if (p->trans >= FST_NODE_LEN(p->node) || !sws->aut->canMatch(p->autState)) {
      if (FST_NODE_ADDR(p->node) != fstGetRootAddr(sws->fst)) {
        taosArrayPop(sws->inp);
      }
      streamStateDestroy(p);    
      continue;
    }
    FstTransition trn; 
    fstNodeGetTransitionAt(p->node, p->trans, &trn);
    Output out = p->out.out + trn.out;
    void* nextState = sws->aut->accept(p->autState, trn.inp);
    void* tState = callback(nextState);
    bool isMatch = sws->aut->isMatch(nextState);
    FstNode *nextNode = fstGetNode(sws->fst, trn.addr); 
    taosArrayPush(sws->inp, &(trn.inp)); 

    if (FST_NODE_IS_FINAL(nextNode)) {
      void *eofState = sws->aut->acceptEof(nextState); 
      if (eofState != NULL) {
        isMatch = sws->aut->isMatch(eofState); 
      }
    } 
    StreamState s1 = { .node = p->node, .trans = p->trans + 1, .out = p->out, .autState = p->autState};  
    taosArrayPush(sws->stack, &s1);

    StreamState s2 = {.node = nextNode, .trans = 0, .out = {.null = false, .out = out}, .autState = nextState};
    taosArrayPush(sws->stack, &s2);
    
    uint8_t *buf = (uint8_t *)malloc(taosArrayGetSize(sws->inp) * sizeof(uint8_t)); 
    for (uint32_t i = 0; i < taosArrayGetSize(sws->inp); i++) {
      uint8_t *t = (uint8_t *)taosArrayGet(sws->inp, i);
      buf[i] = *t; 
    }
    FstSlice slice = fstSliceCreate(buf, taosArrayGetSize(sws->inp));
    if (fstBoundWithDataExceededBy(sws->endAt, &slice)) {
      taosArrayDestroyEx(sws->stack, streamStateDestroy);
      sws->stack = (SArray *)taosArrayInit(256, sizeof(StreamState)); 
      fstSliceDestroy(&slice);
      return NULL;
    }
    if (FST_NODE_IS_FINAL(nextNode) && isMatch) {
      FstOutput fOutput = {.null = false, .out = out + FST_NODE_FINAL_OUTPUT(nextNode)};
      StreamWithStateResult *result =  swsResultCreate(&slice, fOutput , tState);     
      fstSliceDestroy(&slice);
      return result; 
    }
    fstSliceDestroy(&slice);
  }
  return NULL; 
  
}

StreamWithStateResult *swsResultCreate(FstSlice *data, FstOutput fOut, void *state) {
  StreamWithStateResult *result = calloc(1, sizeof(StreamWithStateResult));  
  if (result == NULL) { return NULL; }
  
  result->data   = fstSliceCopy(data, 0, FST_SLICE_LEN(data) - 1);
  result->out   = fOut;
  result->state = state; 

  return result;
}
void swsResultDestroy(StreamWithStateResult *result) {
  if (NULL == result) { return; }
  
  fstSliceDestroy(&result->data);
  free(result);
}

void streamStateDestroy(void *s) {
  if (NULL == s) { return; }
  StreamState *ss = (StreamState *)s;

  fstNodeDestroy(ss->node);
  //free(s->autoState);
}

FstStreamBuilder *fstStreamBuilderCreate(Fst *fst, Automation *aut) {
  FstStreamBuilder *b = calloc(1, sizeof(FstStreamBuilder));
  if (NULL == b) { return NULL; }

  b->fst = fst;
  b->aut = aut;
  b->min = fstBoundStateCreate(Unbounded, NULL);
  b->max = fstBoundStateCreate(Unbounded, NULL);  
  return b;
}
void fstStreamBuilderDestroy(FstStreamBuilder *b) {
  fstSliceDestroy(&b->min->data);
  fstSliceDestroy(&b->max->data);
  free(b);
}
FstStreamBuilder *fstStreamBuilderRange(FstStreamBuilder *b, FstSlice *val, RangeType type) {
  if (b == NULL) { return NULL; }

  if (type == GE) {
    b->min->type = Included;
    fstSliceDestroy(&(b->min->data));
    b->min->data = fstSliceDeepCopy(val, 0, FST_SLICE_LEN(val) - 1);
  } else if (type == GT) {
    b->min->type = Excluded;
    fstSliceDestroy(&(b->min->data));
    b->min->data = fstSliceDeepCopy(val, 0, FST_SLICE_LEN(val) - 1);
  } else if (type == LE) {
    b->max->type = Included;
    fstSliceDestroy(&(b->max->data));
    b->max->data = fstSliceDeepCopy(val, 0, FST_SLICE_LEN(val) - 1);
  } else if (type == LT) {
    b->max->type = Excluded;
    fstSliceDestroy(&(b->max->data));
    b->max->data = fstSliceDeepCopy(val, 0, FST_SLICE_LEN(val) - 1);
  }
  return b;
}




