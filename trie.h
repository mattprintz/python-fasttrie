
#ifndef TRIE_H
#define TRIE_H

#include "config.h"
#include "stdio.h"
#include "stdlib.h"

// Note 1:
// trie_key_t->char_size <= sizeof(TRIE_CHAR). This is the only requirement. 
// That is obvious because for example we cannot hold a 6-byte char. in a 4-byte
// character trie. Chars with different character sizes should live together 
// happily in our trie. 

// Note 2:
// Python versions where PEP393 is not available, as UTF16 is used internally,
// some characters will be surrogate pairs. For example: LINEAR B SYLLABLE B008 A
// Unicode character will have 2 chars encoded in UTF16: 0xd800 0xdc00. These chars 
// will be in different trie nodes. So, it will not be correct to assume every node
// will always contain a single char. However, it will be correct if PEP393 is 
// available because PEP393 internally arranges the required character size and use
// appropriate encoding which eliminates the necessity of surrogate pairs. 

// Note 3:
// All access to key->s shall be done with regard to char_size of the key.

// Note 4:
// We may hold surrogate-pairs as different nodes, so the max_depth value in 
// prefix/suffix like functions may mean different things. User may specify 
// max_depth as '1', however our trie does not treat this one as one char. 
// Surrogate pairs may confuse the value.

typedef struct trie_key_s {
    char *s; // encoded string buffer
    unsigned long size; // how many characters (or code points) in encoded string
    unsigned char char_size; // character size of the encoding in bytes
    unsigned long alloc_size; // max allocated size of the string buffer. (in characters)
} trie_key_t;

typedef struct trie_node_s {
    TRIE_CHAR key;
    TRIE_DATA value;
    struct trie_node_s *next;
    struct trie_node_s *children;
} trie_node_t;

typedef struct trie_s {
    int dirty; // externally reset, internally set. Used to detect if trie  
               // changed during iteration
    unsigned long node_count;
    unsigned long item_count;
    unsigned long height; // max height of the trie (max(len(string)))
    unsigned long mem_usage;
    struct trie_node_s *root;
} trie_t;

typedef enum iter_op_type_e {
    NOOP = 0,
    DELETE,
    TRANSPOSE,
    INSERT,
    CHANGE,
    INDEXCHG,
} iter_op_type_t;

typedef enum iter_fail_e {
    UNDEFINED = 0,
    CHG_WHILE_ITER
} iter_fail_t;

// iterator related structs
typedef struct iter_op_s {
    iter_op_type_t type;
    TRIE_CHAR dch; // the deleted char in delete/change ops
    TRIE_CHAR ich; // the deleted char in delete/change ops
    unsigned long index;
    unsigned long auxindex;
    unsigned long depth;
} iter_op_t;

typedef struct iter_pos_s {
    iter_op_t op;
    unsigned long pos; // used for simulating multiple recursive calls in the same body.
    trie_node_t *iptr; // used for holding the current processing node.
    trie_node_t *prefix; // hold for not calculating prefix everytime
} iter_pos_t;

// fast, pre-allocated iter_pos_t stack
typedef struct iter_stack_s {
    iter_pos_t *_elems;
    unsigned long index;
    unsigned long size;
}iter_stack_t;

typedef struct iter_s {
    int first;
    int last;
    int fail;
    iter_fail_t fail_reason;
    int keylen_reached; // flags used for delaying key changes
    int depth_reached; // flags used for delaying key changes
    trie_t *trie;
    trie_key_t *key;
    trie_node_t *prefix;
    iter_stack_t *stack0;
    iter_stack_t *stack1;
    unsigned long max_depth;
} iter_t;

typedef int (*trie_enum_cbk_t)(trie_key_t *key, void *arg);
typedef iter_t *(*trie_iter_init_func_t)(trie_t *t, trie_key_t *key, 
    unsigned long max_depth);
typedef iter_t *(*trie_iter_next_func_t)(iter_t *iter);
typedef iter_t *(*trie_iter_reset_func_t)(iter_t *iter);
typedef void (*trie_iter_deinit_func_t)(iter_t *iter);

// Basic Trie functions
trie_t *trie_create(void);
void trie_destroy(trie_t *t);
unsigned long trie_mem_usage(trie_t *t);
trie_node_t *trie_search(trie_t *t, trie_key_t *key);
int trie_add(trie_t *t, trie_key_t *key, TRIE_DATA value);
int trie_del(trie_t *t, trie_key_t *key);

// Enumeration functions
// Suffix
void trie_suffixes(trie_t *t, trie_key_t *key, unsigned long max_depth, 
    trie_enum_cbk_t cbk, void* cbk_arg);
iter_t *trie_itersuffixes_init(trie_t *t, trie_key_t *key, unsigned long max_depth);
iter_t *trie_itersuffixes_next(iter_t *iter);
iter_t *trie_itersuffixes_reset(iter_t *iter);
void trie_itersuffixes_deinit(iter_t *iter);
// Prefix
void trie_prefixes(trie_t *t, trie_key_t *key, unsigned long max_depth, 
    trie_enum_cbk_t cbk, void* cbk_arg);
iter_t *trie_iterprefixes_init(trie_t *t, trie_key_t *key, unsigned long max_depth);
iter_t *trie_iterprefixes_next(iter_t *iter);
iter_t *trie_iterprefixes_reset(iter_t *iter);
void trie_iterprefixes_deinit(iter_t *iter);
// Correct
void trie_corrections(trie_t *t, trie_key_t *key, unsigned long max_depth,
    trie_enum_cbk_t cbk, void* cbk_arg);
iter_t *trie_itercorrections_init(trie_t *t, trie_key_t *key, unsigned long max_depth);
iter_t *trie_itercorrections_next(iter_t *iter);
iter_t *trie_itercorrections_reset(iter_t *iter);
void trie_itercorrections_deinit(iter_t *iter);

// Debug functions 
void trie_debug_print_key(trie_key_t *k);

#endif
