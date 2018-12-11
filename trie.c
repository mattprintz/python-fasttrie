
#include "trie.h"
#include "string.h"

//#define DEBUG_PRINT

#ifdef DEBUG_PRINT
#define _DPRINT printf
#else
#define _DPRINT(x)
#endif

void *TRIEMALLOC(trie_t *t, unsigned long size)
{
    void *p;

    p = PyMem_Malloc(size + sizeof(unsigned long));
    if (!p) {
        return NULL;
    }
    if(t) {
        t->mem_usage += size;
    }
    *(unsigned long *)p = size;
    return (char *)p + sizeof(unsigned long);
}

void TRIEFREE(trie_t *t, void *p)
{
    assert(t != NULL);
    
    p = (char *)p - sizeof(unsigned long);
    t->mem_usage -= *(unsigned long *)p;
    PyMem_Free(p);
}

void KEY_CHAR_WRITE(trie_key_t *k, unsigned long index, TRIE_CHAR in)
{
    assert(k->char_size >= sizeof(TRIE_CHAR));

    *(TRIE_CHAR *)&k->s[index*k->char_size] = in;
}

void KEY_CHAR_READ(trie_key_t *k, unsigned long index, TRIE_CHAR *out)
{
    assert(index < k->size);
    assert(k->char_size <= sizeof(TRIE_CHAR));

    // TODO: Also, compiler gives warning when TRIE_CHAR is smaller than
    // char_size, so assertion() might not be enough.
    switch(k->char_size)
    {
        case 1:
            *out = *(uint8_t *)&k->s[index*k->char_size];
            break;
        case 2:
            *out = *(uint16_t *)&k->s[index*k->char_size];
            break;
        case 4:
            *out = *(uint32_t *)&k->s[index*k->char_size];
            break;
        default:
            assert(0 == 1); // unsupported char_size
            break;
    }
}

void KEYCPY(trie_key_t *dst, trie_key_t *src, unsigned long dst_index,
        unsigned long src_index, unsigned long length)
{
    unsigned int i,j;
    char *srcb, *dstb;

    if (length == 0) {
        return;
    }

    assert(dst_index+length-1 < dst->size);
    assert(src_index+length-1 < src->size);
    assert(dst->char_size >= src->char_size);

    for (i=0;i<length;i++) {
        srcb = &src->s[(src_index+i) * src->char_size];
        dstb = &dst->s[(dst_index+i) * dst->char_size];
        for (j=0;j<src->char_size;j++) {
            dstb[j] = srcb[j];
        }
    }
}

trie_key_t *KEYCREATE(trie_t *t, unsigned long length, unsigned char char_size)
{
    trie_key_t *k;

    k = (trie_key_t *)TRIEMALLOC(t, sizeof(trie_key_t));
    if (!k) {
        return NULL;
    }
    k->s = (char *)TRIEMALLOC(t, length*char_size);
    if (!k->s) {
        TRIEFREE(t, k);
        return NULL;
    }

    k->size = length;
    k->char_size = char_size;
    k->alloc_size = length;

    return k;
}

void KEYFREE(trie_t* t, trie_key_t *src)
{
    TRIEFREE(t, src->s);
    TRIEFREE(t, src);
}

iter_stack_t * STACKCREATE(trie_t* t, unsigned long size)
{
    iter_stack_t *r;

    r = (iter_stack_t *)TRIEMALLOC(t, sizeof(iter_stack_t));
    if (!r) {
        return NULL;
    }
    r->_elems = (iter_pos_t *)TRIEMALLOC(t, size*sizeof(iter_pos_t));
    if (!r->_elems) {
        TRIEFREE(t, r);
        return NULL;
    }
    r->index = 0;
    r->size = size;

    return r;
}

void STACKFREE(trie_t* t, iter_stack_t *k)
{
    TRIEFREE(t, k->_elems);
    TRIEFREE(t, k);
}

void PUSHI(iter_stack_t *k, iter_pos_t *e)
{
    assert(k->index < k->size);

    k->_elems[k->index] = *e;
    k->index++;
}

iter_pos_t *POPI(iter_stack_t *k)
{
    if (k->index == 0) {
        return NULL;
    }

    k->index--;
    return &k->_elems[k->index];
}

iter_pos_t *PEEKI(iter_stack_t *k)
{
    if (k->index == 0) {
        return NULL;
    }

    return &k->_elems[k->index-1];
}

trie_node_t *NODECREATE(trie_t* t, TRIE_CHAR key, TRIE_DATA value)
{
    trie_node_t *nd;

    nd = (trie_node_t *)TRIEMALLOC(t, sizeof(trie_node_t));
    if (nd) {
        nd->key = key;
        nd->value = value;
        nd->child_count = 0;
        nd->hash_size = TRIE_MIN_HASH_SIZE;
        nd->child_hash = TRIEMALLOC(t, sizeof(trie_node_t *) * nd->hash_size);
        nd->next = NULL;
        for (int i = 0; i < nd->hash_size; i++) nd->child_hash[i] = NULL;
    }
    return nd;
}

void NODEFREE(trie_t* t, trie_node_t *nd)
{
    TRIEFREE(t, nd->child_hash);
    TRIEFREE(t, nd);
}

trie_t *trie_create(void)
{
    trie_t *t;

    t = (trie_t *)TRIEMALLOC(NULL, sizeof(trie_t));
    if (t) {
        t->mem_usage += sizeof(trie_t);
        t->root = NODECREATE(t, (TRIE_CHAR)0, (TRIE_DATA)0); // root is a dummy node
        t->node_count = 1;
        t->item_count = 0;
        t->height = 1;
        t->dirty = 0;
        t->mem_usage = 0;
    }
    return t;
}

void trie_destroy_node(trie_t *t, trie_node_t *node) {
    trie_node_t *curr = node;
    trie_node_t *prev;
    // Recursively destroy all children nodes
    while (node->child_count > 0) {
        for (int i = 0; i < node->hash_size; i++) {
            if (node->child_hash[i]) {
                prev = NULL;  // Reset value of prev
                curr = node->child_hash[i];
                // Advance to last in linked list, if it exists
                while (curr->next) {
                    prev = curr;
                    curr = curr->next;
                }
                // Remove reference to data being removed, either from next or child_hash
                if (prev)
                    prev->next = NULL;
                else
                    node->child_hash[i] = NULL;

                trie_destroy_node(t, curr);
                node->child_count--;
            }
        }
    }
    NODEFREE(t, node);
    t->node_count--;
}

void trie_destroy(trie_t *t) {
    trie_destroy_node(t, t->root);
    TRIEFREE(t, t);
}

unsigned long trie_mem_usage(trie_t *t)
{
    return t->mem_usage;
}

trie_node_t *_trie_prefix(trie_node_t *t, trie_key_t *key)
{
    TRIE_CHAR ch;
    unsigned int i;
    trie_node_t *curr, *parent;

    if (!t){
        return NULL;
    }
    i = 0;
    parent = curr = t;
    while(i < key->size)
    {
        KEY_CHAR_READ(key, i, &ch);
        curr = trie_get_child(parent, ch);

        while(curr && curr->key != ch) {
            curr = curr->next;
        }
        if (!curr) {
            return NULL;
        }

        parent = curr;
        i++;
    }

    return parent;
}

trie_node_t *trie_search(trie_t *t, trie_key_t *key)
{
    trie_node_t *r;

    r = _trie_prefix(t->root, key);
    if (r && !r->value)
    {
        return NULL;
    }

    return r;
}
trie_node_t *trie_get_child(trie_node_t *node, TRIE_CHAR ch) {
    unsigned int pos = (int)ch % node->hash_size;
    trie_node_t * curr = node->child_hash[pos];
    while (curr && curr->key != ch) {
        curr = curr->next;
    }
    return curr;
}

int trie_add_child(trie_t *t, trie_node_t *parent, trie_node_t *child) {

    if ((parent->child_count + 1 > parent->hash_size) && (parent->hash_size < TRIE_MAX_HASH_SIZE)) {
        trie_node_hash_resize(t, parent, parent->hash_size * 2);
    }
    unsigned int pos = (int)child->key % parent->hash_size;
    child->next = parent->child_hash[pos];
    parent->child_hash[pos] = child;
    parent->child_count++;
    t->node_count++;
    return 1;
}

int trie_remove_child(trie_t *t, trie_node_t *parent, trie_node_t *child) {
    trie_node_t *curr, *prev = NULL;
    unsigned int pos = (int)child->key % parent->hash_size;
    curr = parent->child_hash[pos];
    while (curr && curr != child) {
        prev = curr;
        curr = curr->next;
    }
    if (curr == NULL) {
        // Not found. Can't be removed.
        return 0;
    }

    if (prev) {
        prev->next = curr->next;
    }
    else {
        parent->child_hash[pos] = curr->next;
    }

    TRIEFREE(t, child);
    parent->child_count--;
    t->node_count--;
    return 1;
}

int trie_add(trie_t *t, trie_key_t *key, TRIE_DATA value)
{
    TRIE_CHAR ch;
    unsigned int i;
    trie_node_t *curr, *parent, *prev;

    i = 0;
    parent = t->root;
    while(i < key->size)
    {
        prev = NULL;
        KEY_CHAR_READ(key, i, &ch);

        unsigned int pos = (int)ch % parent->hash_size;
        curr = trie_get_child(parent, ch);
        if (!curr) {
            curr = NODECREATE(t, ch, (TRIE_DATA)0);
            trie_add_child(t, parent, curr);
        }

        parent = curr;
        i++;
    }

    if (!parent->value) {
        t->item_count++;
        t->dirty = 1;
    }

    if (key->size > t->height) {
        t->height = key->size;
    }

    parent->value = value;
    return 1;
}

int trie_del(trie_t *t, trie_key_t *key) {
    int i = 0, found = 1;
    trie_node_t *curr, *prev, *parent;
    TRIE_CHAR ch;
    trie_node_t **parents = malloc(sizeof(trie_node_t *) * key->size + 1);
    parents[0] = curr = t->root;
    for(int j = 1; j < key->size; j++) parents[j] = NULL;

    while(i < key->size) {
        KEY_CHAR_READ(key, i, &ch);
        curr = trie_get_child(curr, ch);
        prev = NULL;

        while(curr && curr->key != ch) {
            prev = curr;
            curr = curr->next;
        }
        if (!curr) {
            found = 0;
            break;
        }

        parents[++i] = curr;
    }

    found = found && curr->value;
    if (found) {
        curr->value = 0;
    }

    if (found) {
        t->item_count--;
        t->dirty = 1;
    }

    while (i) {
        curr = parents[i];

        if ((!curr->child_count) && (!curr->value)) {
            // TODO: Check response for success
            trie_remove_child(t, parents[i-1], curr);
        }
        i--;
    }

    free(parents);
    return found;
}

int trie_node_hash_resize(trie_t *t, trie_node_t *node, int new_size) {
    TRIE_CHILD_HASH old_hash = node->child_hash;
    unsigned short int old_size = node->hash_size;

    TRIE_CHILD_HASH new_hash = TRIEMALLOC(t, sizeof(trie_node_t*) * new_size);
    for (int i = 0; i < new_size; i++) new_hash[i] = NULL;

    unsigned short int pos;
    trie_node_t *current;
    for(int i = 0; i < old_size; i++) {
        current = old_hash[i];
        while (current) {
            pos = (int) current->key % new_size;
            new_hash[pos] = current;
            current = current->next;
        }
    }
    node->child_hash = new_hash;
    node->hash_size = new_size;
    TRIEFREE(t, old_hash);
}

trie_node_t **trie_node_children(trie_node_t *node) {
    if (node->child_count == 0) return NULL;
    unsigned int children_offset = 0;
    trie_node_t **children = (trie_node_t*)malloc(sizeof(trie_node_t*) * node->child_count);
    for (int i = 0; i < node->child_count; i++) {
        children[i] = NULL;
    }
    trie_node_t *result_head = NULL, *current, *hash_head;
    for(int i = 0; i < node->hash_size; i++) {
        current = node->child_hash[i];
        while (current) {
            children[children_offset++] = current;
            current = current->next;
        }
    }
    return children;

}

int trie_node_serializer(trie_node_t *t, char *s, unsigned long *node_offset, TRIE_DATA *value_ptrs, unsigned long *value_offset) {
    unsigned long s_offset = *node_offset * TRIE_NODE_SIZE;
    unsigned long value_idx;
    unsigned char child_count = t->child_count;
    char * i_ptr = s + s_offset;
    trie_node_t *child;

    if (t->value != 0) {
        *value_offset = *value_offset + 1;
        value_ptrs[*value_offset] = t->value;
        value_idx = *value_offset;
    }
    else {
        value_idx = 0;
    }

    memcpy(i_ptr, &t->key, sizeof(char));
    memcpy(i_ptr + 1, &value_idx, sizeof(unsigned long));
    memcpy(i_ptr + 1 + sizeof(value_idx), &child_count, sizeof(char));
    *node_offset = *node_offset + 1;

    trie_node_t ** children = trie_node_children(t);
    for (int i = 0; i < child_count; i++) {
        child = children[i];
        trie_node_serializer(child, s, node_offset, value_ptrs, value_offset);
    }
    free(children);

    return 0;
}

trie_node_t *trie_node_deserializer(trie_t *trie, char *s, unsigned long *node_offset, TRIE_DATA *value_ptrs) {
    unsigned long s_offset = *node_offset * TRIE_NODE_SIZE;
    unsigned long value_idx;
    char key;
    char child_count;
    char * i_ptr = s + s_offset;

    memcpy(&key, i_ptr, sizeof(char));
    memcpy(&value_idx, i_ptr + 1, sizeof(unsigned long));
    memcpy(&child_count, i_ptr + 1 + sizeof(unsigned long), sizeof(char));

    TRIE_DATA *value = value_ptrs[value_idx];

    trie_node_t *node = NODECREATE(trie, key, value);
    for(int i = 0; i < child_count; i++) {
        *node_offset = *node_offset + 1;
        trie_add_child(trie, node, (trie_node_t *)trie_node_deserializer(trie, s, node_offset, value_ptrs));
    }

    return node;
}

trie_serialized_t *trie_serialize(trie_t *t) {
    trie_serialized_t *repr = (trie_serialized_t *)TRIEMALLOC(NULL, sizeof(trie_serialized_t));
    unsigned long s_size, value_size, node_offset, value_offset;

    s_size = t->node_count * TRIE_NODE_SIZE;
    value_size = (t->item_count + 1) * sizeof(TRIE_DATA);
    node_offset = value_offset = 0;

    char *s = (char *)TRIEMALLOC(NULL, s_size);
    TRIE_DATA *value_ptrs = (TRIE_DATA *)TRIEMALLOC(NULL, value_size);
    value_ptrs[0] = 0;

    trie_node_serializer(t->root, s, &node_offset, value_ptrs, &value_offset);

    repr->s = s;
    repr->value_ptrs = value_ptrs;
    repr->s_length = s_size;
    repr->value_length = t->item_count + 1;

    return repr;
}

trie_t *trie_deserialize(trie_serialized_t *t) {
    unsigned long node_offset = 0;
    trie_node_t *root_node = trie_node_deserializer(t, t->s, &node_offset, t->value_ptrs);
    trie_t *trie = trie_create();
    NODEFREE(trie, trie->root);
    trie->root = root_node;

    return trie;

}

iter_t * ITERATORCREATE(trie_t *t, trie_key_t *key, unsigned long max_depth, 
    unsigned long alloc_size, unsigned long stack_size1, unsigned long stack_size2)
{
    iter_t *r;
    iter_stack_t *k0,*k1;
    trie_key_t *kp;

    // alloc a key that can hold size + max_depth chars.
    kp = KEYCREATE(t, alloc_size, sizeof(TRIE_CHAR));
    if (!kp) {
        return NULL;
    }

    KEYCPY(kp, key, 0, 0, key->size);
    kp->size = key->size;

    // allocate stacks
    k0 = STACKCREATE(t, stack_size1);
    if (!k0) {
        KEYFREE(t, kp);
        return NULL;
    }
    k1 = STACKCREATE(t, stack_size2);
    if (!k1) {
        KEYFREE(t, kp);
        STACKFREE(t, k0);
        return NULL;
    }

    // alloc iterator obj
    r = (iter_t *)TRIEMALLOC(t, sizeof(iter_t));
    if (!r) {
        KEYFREE(t, kp);
        STACKFREE(t, k0);
        STACKFREE(t, k1);
        return NULL;
    }

    r->first = 1;
    r->last = 0;
    r->fail = 0;
    r->fail_reason = UNDEFINED;
    r->key = kp;
    r->stack0 = k0;
    r->stack1 = k1;
    r->max_depth = max_depth;
    r->trie = t;
    t->dirty = 0; // reset dirty flag just before iteration
    r->keylen_reached = 0;
    r->depth_reached = 0;
    
    return r;
}

void ITERATORFREE(trie_t *t, iter_t *iter)
{
    KEYFREE(t, iter->key);
    STACKFREE(t, iter->stack0);
    STACKFREE(t, iter->stack1);
    TRIEFREE(t, iter);
}

void iterator_deinit(iter_t *iter)
{
    assert(iter != NULL);

    ITERATORFREE(iter->trie, iter);
}

void _suffixes(trie_node_t *p, trie_key_t *key, unsigned long index, 
    trie_enum_cbk_t cbk, void* cbk_arg)
{
    if (p->value) {
        cbk(key, p, cbk_arg);
    }
    
    if (index == key->alloc_size) {
        return;
    }
    trie_node_t ** children = trie_node_children(p);
    for(int i = 0; i < p->child_count; i++) {
        KEY_CHAR_WRITE(key, index, children[i]->key);
        key->size = index+1;
        
        _suffixes(children[i], key, index+1, cbk, cbk_arg);
    }
    free(children);
}

void trie_suffixes(trie_t *t, trie_key_t *key, unsigned long max_depth, 
    trie_enum_cbk_t cbk, void* cbk_arg)
{
    trie_key_t *kp;
    trie_node_t *prefix;
    unsigned long index;

    // first search key
    prefix = _trie_prefix(t->root, key);
    if (!prefix) {
        return;
    }

    // alloc a key that can hold size + max_depth chars.
    kp = KEYCREATE(t, (key->size + max_depth), sizeof(TRIE_CHAR));
    if (!kp) {
        return;
    }
    KEYCPY(kp, key, 0, 0, key->size);
    kp->size = key->size;
    
    index = 0;
    if (kp->size > 0) {
        index = kp->size;
    }

    _suffixes(prefix, kp, index, cbk, cbk_arg);

    KEYFREE(t, kp);
}

iter_t *trie_itersuffixes_init(trie_t *t, trie_key_t *key, unsigned long max_depth)
{
    iter_t *iter;
    trie_node_t *prefix;

    // first search key
    prefix = _trie_prefix(t->root, key);
    if (!prefix) {
        return NULL;
    }

    // create the iterator obj
    iter = ITERATORCREATE(t, key, max_depth, (key->size + max_depth), 
        max_depth, 0);
    if (!iter) {
        return NULL;
    }
    trie_itersuffixes_reset(iter);

    return iter;
}

void trie_itersuffixes_deinit(iter_t *iter)
{
    iterator_deinit(iter);
}

iter_t *trie_itersuffixes_reset(iter_t *iter)
{
    trie_node_t *prefix;
    iter_pos_t ipos;

    // pop all elems first
    while(POPI(iter->stack0))
        ;

    // return key->size to original
    iter->key->size = iter->key->alloc_size-iter->max_depth;

    // get prefix in the trie
    prefix = _trie_prefix(iter->trie->root, iter->key);
    if (!prefix) {
        return NULL;
    }

    // push the first iter_pos
    ipos.iptr = prefix;
    ipos.pos = 0;
    ipos.op.index = iter->key->size-1;
    PUSHI(iter->stack0, &ipos);

    iter->first = 1;
    iter->last = 0;
    iter->fail = 0;
    iter->fail_reason = UNDEFINED;
    iter->trie->dirty = 0;

    return iter;
}

iter_t *trie_itersuffixes_next(iter_t *iter)
{
    // iter_pos_t *ip;
    // iter_pos_t ipos;
    // int found;
    // TRIE_DATA val;

    // found = 0;
    // while(1)
    // {
    //     // found a candidate?
    //     if (found) {
    //         break;
    //     }

    //     // trie changed during iteration?
    //     if (iter->trie->dirty) {
    //         iter->fail = 1;
    //         iter->fail_reason = CHG_WHILE_ITER;
    //         break;
    //     }

    //     // start processing the stack
    //     ip = PEEKI(iter->stack0);
    //     if (!ip) { // no elem in stack0
    //         iter->last = 1;
    //         break;
    //     }

    //     // check if the search string is already in the trie.
    //     if (iter->first) {
    //         iter->first = 0;
    //         val = ip->iptr->value;
    //         ip->iptr = ip->iptr->children;
    //         ip->op.index++;
    //         if (val) {
    //             break;
    //         }
    //     }

    //     if (!ip->iptr) {
    //         POPI(iter->stack0);
    //         continue;
    //     }

    //     KEY_CHAR_WRITE(iter->key, ip->op.index, ip->iptr->key);
    //     iter->key->size = ip->op.index+1;

    //     if (ip->pos == 0 && ip->iptr->value) {
    //         found = 1;
    //     }

    //     if (ip->pos == 0) {
    //         ip->pos = 1;
    //         if (ip->iptr->children) {
    //             if (ip->op.index+1 < (iter->key->alloc_size)) {
    //                 ipos.iptr = ip->iptr->children;
    //                 ipos.op.index = ip->op.index+1;
    //                 ipos.pos = 0;
    //                 PUSHI(iter->stack0, &ipos);
    //             }
    //         }
    //     } else if (ip->pos == 1) {
    //         POPI(iter->stack0);
    //         if (ip->iptr->next) {
    //             ipos.iptr = ip->iptr->next;
    //             ipos.op.index = ip->op.index;
    //             ipos.pos = 0;
    //             PUSHI(iter->stack0, &ipos);
    //         }
    //     }
    // }

    return iter;
}

void trie_prefixes(trie_t *t, trie_key_t *key, unsigned long max_depth, 
    trie_enum_cbk_t cbk, void* cbk_arg)
{
    trie_key_t *kp;
    trie_key_t k;
    trie_node_t *p;
    unsigned long i;
    TRIE_CHAR ch;

    if (key->size == 0) {
        return;
    }

    // alloc a key that can hold the key itself
    kp = KEYCREATE(t, (key->size), sizeof(TRIE_CHAR));
    if (!kp) {
        return;
    }
    KEYCPY(kp, key, 0, 0, key->size);
    kp->size = 1; // start from first character 

    p = t->root;
    for(i=0;i<key->size;i++)
    {
        if (i == max_depth) {
            break;
        }

        KEY_CHAR_READ(kp, i, &ch);
        k.s = (char *)&ch; k.size = 1; k.char_size = kp->char_size;
        p = _trie_prefix(p, &k);
        if (!p) {
            break;
        }
        if(p->value)
        {
            cbk(kp, p, cbk_arg);
        }
        kp->size++;
    }

    KEYFREE(t, kp);
    
    return;
}

iter_t *trie_iterprefixes_init(trie_t *t, trie_key_t *key, unsigned long max_depth)
{
    iter_t *iter;
    // trie_node_t *prefix;
    // unsigned long real_size;

    // if (key->size == 0) {
    //     return NULL;
    // }

    // // search first char
    // real_size = key->size;
    // key->size = 1;
    // prefix = _trie_prefix(t->root, key);
    // if (!prefix) {
    //     return NULL;
    // }
    // key->size = real_size;

    // // create the iterator obj
    // iter = ITERATORCREATE(t, key, max_depth, key->size, max_depth, 0);
    // if (!iter) {
    //     return NULL;
    // }
    // trie_iterprefixes_reset(iter);

    return iter;
}

void trie_iterprefixes_deinit(iter_t *iter)
{
    iterator_deinit(iter);
}

iter_t *trie_iterprefixes_reset(iter_t *iter)
{
    iter_pos_t ipos;
    trie_node_t *prefix;

    // pop all elems first
    while(POPI(iter->stack0))
        ;

    // search first char
    iter->key->size = 1;
    prefix = _trie_prefix(iter->trie->root, iter->key);
    if (!prefix) {
        return NULL;
    }

    // return key->size to original
    iter->key->size = iter->key->alloc_size; 

    // push the first iter_pos
    ipos.iptr = prefix;
    ipos.op.index = 1;
    ipos.pos = 0;
    PUSHI(iter->stack0, &ipos);

    // set flags
    iter->first = 1;
    iter->last = 0;
    iter->fail = 0;
    iter->fail_reason = UNDEFINED;
    iter->trie->dirty = 0;

    return iter;
}

iter_t *trie_iterprefixes_next(iter_t *iter)
{
    iter_pos_t *ip;
    trie_key_t k;
    trie_node_t *p;
    TRIE_CHAR ch;

    while(1)
    {
        // trie changed during iteration?
        if (iter->trie->dirty) {
            iter->fail = 1;
            iter->fail_reason = CHG_WHILE_ITER;
            break;
        }
       
        // return key size to original, again
        iter->key->size = iter->key->alloc_size;

        // peek stack
        ip = POPI(iter->stack0);
        if (!ip) { // no elem in stack0
            iter->last = 1;
            break;
        }

        if (iter->first) {
            iter->first = 0;
        }

        // read and increment index+iptr
        if (ip->op.index > iter->key->size) {
            iter->last = 1;
            break;
        }

        if (ip->pos == 0 && ip->iptr->value)
        {
            ip->pos = 1;
            iter->key->size = ip->op.index;
            PUSHI(iter->stack0, ip);
            break;
        }

        if (ip->op.index < iter->key->size) {
            KEY_CHAR_READ(iter->key, ip->op.index, &ch);

            k.s = (char *)&ch; k.size = 1; k.char_size = iter->key->char_size;
            p = _trie_prefix(ip->iptr, &k);
            if (p) {
                
                ip->op.index++;
                ip->iptr = p;
                ip->pos = 0;
                PUSHI(iter->stack0, ip);
            }
        }
    }

    return iter;
}

void _do(trie_key_t *key, iter_op_t *op)
{
    unsigned long ki,kchsize;
    TRIE_CHAR ch1,ch2;
 
    // init. common used vars
    ki = op->index;
    kchsize = key->char_size;

    switch(op->type)
    {
    case DELETE:
        _DPRINT("DO DELETE\n");
        ki = op->auxindex;
        KEY_CHAR_READ(key, ki, &op->dch);
        key->size -= 1;
        memmove(&key->s[(ki)*kchsize], &key->s[(ki+1) * kchsize], 
            (key->size-ki)*kchsize);
        break;
    case TRANSPOSE:
        _DPRINT("DO TRANSPOSE\n");
        KEY_CHAR_READ(key, ki, &ch1);
        KEY_CHAR_READ(key, ki+1, &ch2);
        KEY_CHAR_WRITE(key, ki, ch2);
        KEY_CHAR_WRITE(key, ki+1, ch1);
        break;
    case INSERT:
        _DPRINT("DO INSERT\n");
        key->size += 1;
        memmove(&key->s[(ki+1) * kchsize], &key->s[(ki)*kchsize], 
            (key->size-ki-1)*key->char_size);
        KEY_CHAR_WRITE(key, ki, op->ich);
        break;
    case CHANGE:
        _DPRINT("DO CHANGE\n");
        KEY_CHAR_READ(key, ki, &op->dch);
        KEY_CHAR_WRITE(key, ki, op->ich);
        break;
    case INDEXCHG:
        break; // do nothing
    default:
        assert(0 == 1); // unsupported operation
        break;
    }
}

void _undo(trie_key_t *key, iter_op_t *op)
{
    unsigned long ki,kchsize;
    TRIE_CHAR ch1,ch2;

    // init. common used vars
    ki = op->index;
    kchsize = key->char_size;
    
    switch(op->type)
    {
    case DELETE:
        _DPRINT("UNDO DELETE\n");
        ki = op->auxindex;
        key->size += 1;
        memmove(&key->s[(ki+1) * kchsize], &key->s[(ki)*kchsize], 
            (key->size-ki)*key->char_size);
        KEY_CHAR_WRITE(key, ki, op->dch);
        break;
    case TRANSPOSE:
        _DPRINT("UNDO TRANSPOSE\n");
        KEY_CHAR_READ(key, ki, &ch1);
        KEY_CHAR_READ(key, ki+1, &ch2);
        KEY_CHAR_WRITE(key, ki, ch2);
        KEY_CHAR_WRITE(key, ki+1, ch1);
        break;
    case INSERT:
        _DPRINT("UNDO INSERT\n");
        key->size -= 1;
        memmove(&key->s[(ki)*kchsize], &key->s[(ki+1) * kchsize], 
            (key->size-ki)*kchsize);
        break;
    case CHANGE:
        _DPRINT("UNDO CHANGE\n");
        KEY_CHAR_WRITE(key, ki, op->dch);
        break;
    case INDEXCHG:
        break; // do nothing
    default:
        assert(0 == 1); // unsupported operation
        break;
    }
}

void _corrections(trie_t * t, trie_node_t *pprefix, trie_key_t *key, 
    unsigned long c_index, unsigned long c_depth, trie_enum_cbk_t cbk, void* cbk_arg)
{
    // unsigned long ksize,kchsize;
    // trie_key_t pk;
    // trie_node_t *prefix,*p;
    // TRIE_CHAR ch;
    // iter_op_t op;

    // // search prefix
    // prefix = pprefix;
    // if (c_index > 0) {
    //     if (c_index-1 >= key->size) {
    //         return;
    //     }
    //     KEY_CHAR_READ(key, c_index-1, &ch);
    //     pk.s = (char *)&ch; pk.size = 1; pk.char_size = key->char_size;
    //     prefix = _trie_prefix(pprefix, &pk);
    //     if (!prefix) {
    //         return;
    //     }
    // }

    // // search suffix (which will complete the search for the full key)
    // ksize = key->size;
    // kchsize = key->char_size;
    // pk.s = &key->s[c_index*kchsize]; pk.size = ksize-c_index; 
    // pk.char_size = key->char_size;
    // p = _trie_prefix(prefix, &pk);
    // if (p && p->value) {
    //     cbk(key, p, cbk_arg);
    // }

    // // check bounds/depth
    // if ((c_index > ksize) || (c_depth == 0)) {
    //     return;
    // }

    // // deletion
    // if (ksize > 1 && c_index < ksize)
    // {
    //     op.type = DELETE; op.index = 0; op.auxindex = c_index;
    //     _do(key, &op);

    //     _corrections(t, t->root, key, 0, c_depth-1, cbk, cbk_arg);

    //     _undo(key, &op);
    // }

    // // transposition (prefix + suffix[1] + suffix[0] + suffix[2:])
    // if (ksize != 0 && c_index < ksize-1) 
    // {
    //     op.type = TRANSPOSE; op.index = c_index;
    //     _do(key, &op);

    //     _corrections(t, pprefix, key, c_index, c_depth-1, cbk, cbk_arg);

    //     _undo(key, &op);
    // }

    // // insertion (prefix + x + suffix[:])
    // p = prefix->children;
    // while(p)
    // {
    //     op.type = INSERT; op.index = c_index; op.ich = p->key;
    //     _do(key, &op);

    //     _corrections(t, pprefix, key, c_index, c_depth-1, cbk, cbk_arg);

    //     _undo(key, &op);

    //     p = p->next;
    // }

    // // change (prefix + x + suffix[1:])
    // if (c_index < ksize)
    // {
    //     p = prefix->children;
    //     while(p) 
    //     {
    //         op.type = CHANGE; op.index = c_index; op.ich = p->key;
    //         _do(key, &op);

    //         _corrections(t, pprefix, key, c_index, c_depth-1, cbk, cbk_arg);

    //         _undo(key, &op);

    //         p = p->next;
    //     }
    // }

    // _corrections(t, prefix, key, c_index+1, c_depth, cbk, cbk_arg);
}

void trie_corrections(trie_t *t, trie_key_t *key, unsigned long max_depth,
    trie_enum_cbk_t cbk, void* cbk_arg)
{
    trie_key_t *kp;

    // alloc a key that can hold size + max_depth chars.
    kp = KEYCREATE(t, (key->size + max_depth), sizeof(TRIE_CHAR));
    if (!kp) {
        return;
    }
    KEYCPY(kp, key, 0, 0, key->size);
    kp->size = key->size;
    
    _corrections(t, t->root, kp, 0, max_depth, cbk, cbk_arg);

    KEYFREE(t, kp);
}

iter_t *trie_itercorrections_init(trie_t *t, trie_key_t *key, unsigned long max_depth)
{
    iter_t *iter;

    iter = ITERATORCREATE(t, key, max_depth, (key->size + max_depth), max_depth+1,
        max_depth);
    if (!iter) {
        return NULL;
    }
    trie_itercorrections_reset(iter);

    return iter;
}

void trie_itercorrections_deinit(iter_t *iter)
{
    iterator_deinit(iter);
}

iter_t *trie_itercorrections_reset(iter_t *iter)
{
    iter_pos_t ipos;

    // clear stacks
    while(POPI(iter->stack0))
        ;
    while(POPI(iter->stack1))
        ;

    // alloc the first iter_pos
    ipos.pos = 0; ipos.op.type = INDEXCHG; ipos.op.index = 0; 
    ipos.op.depth = iter->max_depth; ipos.iptr = NULL;
    ipos.prefix = iter->trie->root;

    PUSHI(iter->stack0, &ipos);
    PUSHI(iter->stack1, &ipos);

    iter->first = 1;
    iter->last = 0;
    iter->fail = 0;
    iter->fail_reason = UNDEFINED;
    iter->trie->dirty = 0;
    iter->keylen_reached = 0;
    iter->depth_reached = 0;

    return iter;
}

iter_t *trie_itercorrections_next(iter_t *iter)
{
    // trie_key_t pk;
    // iter_pos_t *ip;
    // iter_stack_t *k0,*k1;
    // trie_node_t *prefix,*p;
    // iter_pos_t ipos;
    // int found;
    // TRIE_CHAR ch;

    // k0 = (iter_stack_t *)iter->stack0;
    // k1 = (iter_stack_t *)iter->stack1;
    // found = 0;
    // while(1)
    // {
    //     if (found)
    //     {
    //         return iter;
    //     }

    //     ip = PEEKI(k0);
    //     if (!ip) {
    //         iter->last = 1;
    //         return iter;
    //     }

    //     // previous key changes are delayed to this iteration.
    //     if (iter->depth_reached)
    //     {
    //         _undo(iter->key, &ip->op);
    //         POPI(k0);
    //         iter->depth_reached = 0;
    //         continue;
    //     }

    //     if (iter->keylen_reached)
    //     {
    //         ip = POPI(k1);
    //         _undo(iter->key, &ip->op);
    //         ip = POPI(k0);
    //         _undo(iter->key, &ip->op);
    //         iter->keylen_reached = 0; 
    //         continue;
    //     }

    //     prefix = ip->prefix;
    //     if (ip->op.index > 0) {
    //         if (ip->op.index-1 >= iter->key->size) {
    //             ip = POPI(k1);
    //             _undo(iter->key, &ip->op);
    //             ip = POPI(k0);
    //             _undo(iter->key, &ip->op);
    //             continue;
    //         }
    //         KEY_CHAR_READ(iter->key, ip->op.index-1, &ch);
    //         pk.s = (char *)&ch; pk.size = 1; pk.char_size = iter->key->char_size;
    //         prefix = _trie_prefix(ip->prefix, &pk);
    //         if (!prefix) {
    //             ip = POPI(k1);
    //             _undo(iter->key, &ip->op);
    //             ip = POPI(k0);
    //             _undo(iter->key, &ip->op);
    //             continue;
    //         }
    //     }

    //     // "only once" operations go under (pos == 0)
    //     if (ip->pos == 0)
    //     {
    //         _do(iter->key, &ip->op); 

    //         pk.s = &iter->key->s[ip->op.index*iter->key->char_size]; 
    //         pk.size = iter->key->size-ip->op.index; 
    //         pk.char_size = iter->key->char_size;
    //         p = _trie_prefix(prefix, &pk);
    //         if (p && p->value) {
    //             found = 1;
    //         }
    //     }

    //     if (ip->op.depth == 0)
    //     {
    //         iter->depth_reached = 1;
    //         continue;
    //     }

    //     if (ip->op.index > iter->key->size)
    //     {
    //         iter->keylen_reached = 1;
    //         continue;
    //     }

    //     // hold the doed() but not undoed() non-noop operations in history stack
    //     // that is processed first time.(in our current implementation we process 
    //     // the same op multiple times so simply check if it was processed before.
    //     if (ip->op.type != INDEXCHG && ip->pos == 0)
    //     {
    //         PUSHI(k1, ip);
    //     }

    //     if (ip->pos == 0) {
    //         ip->pos = 1;
    //         if (iter->key->size > 1 && ip->op.index < iter->key->size) {
    //             ipos.pos = 0; ipos.op.type = DELETE; ipos.op.index = 0; 
    //             ipos.op.auxindex = ip->op.index; ipos.op.depth = ip->op.depth-1; 
    //             ipos.iptr = NULL; ipos.prefix = iter->trie->root;
    //             PUSHI(k0, &ipos);
    //             continue;
    //         }
    //     }
        
    //     if (ip->pos == 1) {
    //         ip->pos = 2;
    //         if (iter->key->size != 0 && ip->op.index+1 < iter->key->size)
    //         {
    //             ipos.pos = 0; ipos.op.type = TRANSPOSE; 
    //             ipos.op.index = ip->op.index; ipos.op.depth = ip->op.depth-1; 
    //             ipos.iptr = NULL; ipos.prefix = ip->prefix;
    //             PUSHI(k0, &ipos);
    //             continue;
    //         }
    //     }
            
    //     if (ip->pos == 2) {
    //         if (!ip->iptr) {
    //             ip->iptr = prefix->children;
    //         } else {
    //             ip->iptr = ip->iptr->next;
    //         }           
            
    //         if (ip->iptr) {
    //             ipos.pos = 0; ipos.op.type = INSERT; ipos.op.index = ip->op.index; 
    //             ipos.op.depth = ip->op.depth-1; ipos.iptr = NULL; 
    //             ipos.op.ich = ip->iptr->key; ipos.prefix = ip->prefix;
    //             PUSHI(k0, &ipos);
    //             continue;
    //         } 

    //         ip->pos = 3;
    //     }
        
    //     if (ip->pos == 3) {
    //         if (ip->op.index < iter->key->size) 
    //         {
    //             if (!ip->iptr) {
    //                 ip->iptr = prefix->children;
    //             } else {
    //                 ip->iptr = ip->iptr->next;
    //             }            
                
    //             if (ip->iptr) {
    //                 ipos.pos = 0; ipos.op.type = CHANGE; 
    //                 ipos.op.index = ip->op.index; ipos.op.depth = ip->op.depth-1; 
    //                 ipos.iptr = NULL; ipos.op.ich = ip->iptr->key;
    //                 ipos.prefix = ip->prefix;
    //                 PUSHI(k0, &ipos);
    //                 continue;
    //             }
    //         }
    //         ip->pos = 4;
    //     }

    //     POPI(k0);
    //     ipos.pos = 0; ipos.op.type = INDEXCHG; ipos.op.index = ip->op.index+1; 
    //     ipos.op.depth = ip->op.depth; ipos.iptr = NULL;
    //     ipos.prefix = prefix;
    //     PUSHI(k0, &ipos);
    // }
}

void trie_debug_print_key(trie_key_t *k)
{
    unsigned int i;
    
    printf("key->s:%s, key->size:%lu, key->char_size:%d\n", k->s, k->size, 
        k->char_size);
    for(i=0;i<k->size;i++) {
        if (k->char_size == 1) {
            printf("key->s[%u]:0x%hhx\n", i, *(uint8_t *)&k->s[i*k->char_size]);
        } else if (k->char_size == 2) {
            printf("key->s[%u]:0x%hx\n", i, *(uint16_t *)&k->s[i*k->char_size]);
        } else if (k->char_size == 4) {
            printf("key->s[%u]:0x%x\n", i, *(uint32_t *)&k->s[i*k->char_size]);
        }
    }
}