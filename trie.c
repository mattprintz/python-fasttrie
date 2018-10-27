
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
        nd->next = NULL;
        nd->children = NULL;
    }
    return nd;
}

void NODEFREE(trie_t* t, trie_node_t *nd)
{
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

void trie_destroy(trie_t *t)
{
    trie_node_t *curr, *parent, *next;

    while(t->node_count > 0)
    {
        curr = t->root;
        parent = NULL;
        while(curr->children) {
            parent = curr;
            curr = curr->children;
        }

        if (parent) {
            next = parent->children->next;
            NODEFREE(t, parent->children);
            parent->children = next;
        } else { // root remaining
            NODEFREE(t, t->root);
        }
        t->node_count--;
    }
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
    parent = t;
    curr = t->children;
    while(i < key->size)
    {
        KEY_CHAR_READ(key, i, &ch);

        while(curr && curr->key != ch) {
            curr = curr->next;
        }
        if (!curr) {
            return NULL;
        }

        parent = curr;
        curr = curr->children;
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

int trie_add(trie_t *t, trie_key_t *key, TRIE_DATA value)
{
    TRIE_CHAR ch;
    unsigned int i;
    trie_node_t *curr, *parent;

    i = 0;
    parent = t->root;
    curr = t->root->children;
    while(i < key->size)
    {
        KEY_CHAR_READ(key, i, &ch);
        
        while(curr && curr->key != ch) {
            curr = curr->next;
        }
        if (!curr) {
            curr = NODECREATE(t, ch, (TRIE_DATA)0);
            if (!curr){
                return 0;
            }

            curr->next = parent->children;
            parent->children = curr;
            t->node_count++;
        }
        parent = curr;
        curr = curr->children;
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

// Algorithm:  First traverse the key and reverse the linked-list of children,
// while reversing, move the branched nodes to the head then traverse back and
// try to delete the node if no children is associated with it. Harder to implement
// however performance is better as we iterate key only once.
// Complexity: O(m)
int trie_del(trie_t *t, trie_key_t *key)
{
    unsigned int i, found;
    trie_node_t *curr, *prev, *it, *parent;
    TRIE_CHAR ch;

    // iterate forward and reverse the linked-list
    found = 1;
    i = 0;
    parent = t->root;
    curr = t->root->children;
    while(i < key->size)
    {
        KEY_CHAR_READ(key, i, &ch);
        prev = it = curr;
        while(it && it->key != ch) {
            prev = it;
            it=it->next;
        }
        if (!it) {
            found = 0;
            break; // should be rollbacked
        }
        if (it != curr) {
            // curr points to head of next-list here. move the branched item
            // to head(by pointing to curr) for latter back-traversing.
            prev->next = it->next;
            it->next = curr;
        }

        curr = it->children;
        it->children = parent;
        parent = it;
        i++;
    }

    // key is found, but do we really have added it?
    found = found && parent->value;

    // remove key
    if (found) {
        parent->value = 0;
    }

    // traverse backwards
    prev = curr;
    curr = parent;
    while(i--)
    {
        it = curr->children;
        curr->children = prev;

        // no-child and no-val and item is added/found?
        if (found && !curr->children && !curr->value) {
            // we know curr == it->children as we move it to head before.
            prev = curr->next;
            NODEFREE(t, curr);
            t->node_count--;
        } else {
            prev = curr;
        }
        curr = it;
    }
    t->root->children = prev;
    if (found) {
        t->item_count--;
        t->dirty = 1;
    }

    return found;
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
        cbk(key, cbk_arg);
    }
    
    if (index == key->alloc_size) {
        return;
    }
    p = p->children;
    while(p){
        KEY_CHAR_WRITE(key, index, p->key);
        key->size = index+1;
        
        _suffixes(p, key, index+1, cbk, cbk_arg);
      
        p = p->next;
    }
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
    iter_pos_t *ip;
    iter_pos_t ipos;
    int found;
    TRIE_DATA val;

    found = 0;
    while(1)
    {
        // found a candidate?
        if (found) {
            break;
        }

        // trie changed during iteration?
        if (iter->trie->dirty) {
            iter->fail = 1;
            iter->fail_reason = CHG_WHILE_ITER;
            break;
        }

        // start processing the stack
        ip = PEEKI(iter->stack0);
        if (!ip) { // no elem in stack0
            iter->last = 1;
            break;
        }

        // check if the search string is already in the trie.
        if (iter->first) {
            iter->first = 0;
            val = ip->iptr->value;
            ip->iptr = ip->iptr->children;
            ip->op.index++;
            if (val) {
                break;
            }
        }

        if (!ip->iptr) {
            POPI(iter->stack0);
            continue;
        }

        KEY_CHAR_WRITE(iter->key, ip->op.index, ip->iptr->key);
        iter->key->size = ip->op.index+1;

        if (ip->pos == 0 && ip->iptr->value) {
            found = 1;
        }

        if (ip->pos == 0) {
            ip->pos = 1;
            if (ip->iptr->children) {
                if (ip->op.index+1 < (iter->key->alloc_size)) {
                    ipos.iptr = ip->iptr->children;
                    ipos.op.index = ip->op.index+1;
                    ipos.pos = 0;
                    PUSHI(iter->stack0, &ipos);
                }
            }
        } else if (ip->pos == 1) {
            POPI(iter->stack0);
            if (ip->iptr->next) {
                ipos.iptr = ip->iptr->next;
                ipos.op.index = ip->op.index;
                ipos.pos = 0;
                PUSHI(iter->stack0, &ipos);
            }
        }
    }

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
            cbk(kp, cbk_arg);
        }
        kp->size++;
    }

    KEYFREE(t, kp);
    
    return;
}

iter_t *trie_iterprefixes_init(trie_t *t, trie_key_t *key, unsigned long max_depth)
{
    iter_t *iter;
    trie_node_t *prefix;
    unsigned long real_size;

    if (key->size == 0) {
        return NULL;
    }

    // search first char
    real_size = key->size;
    key->size = 1;
    prefix = _trie_prefix(t->root, key);
    if (!prefix) {
        return NULL;
    }
    key->size = real_size;

    // create the iterator obj
    iter = ITERATORCREATE(t, key, max_depth, key->size, max_depth, 0);
    if (!iter) {
        return NULL;
    }
    trie_iterprefixes_reset(iter);

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
    unsigned long ksize,kchsize;
    trie_key_t pk;
    trie_node_t *prefix,*p;
    TRIE_CHAR ch;
    iter_op_t op;

    // search prefix
    prefix = pprefix;
    if (c_index > 0) {
        if (c_index-1 >= key->size) {
            return;
        }
        KEY_CHAR_READ(key, c_index-1, &ch);
        pk.s = (char *)&ch; pk.size = 1; pk.char_size = key->char_size;
        prefix = _trie_prefix(pprefix, &pk);
        if (!prefix) {
            return;
        }
    }

    // search suffix (which will complete the search for the full key)
    ksize = key->size;
    kchsize = key->char_size;
    pk.s = &key->s[c_index*kchsize]; pk.size = ksize-c_index; 
    pk.char_size = key->char_size;
    p = _trie_prefix(prefix, &pk);
    if (p && p->value) {
        cbk(key, cbk_arg);
    }

    // check bounds/depth
    if ((c_index > ksize) || (c_depth == 0)) {
        return;
    }

    // deletion
    if (ksize > 1 && c_index < ksize)
    {
        op.type = DELETE; op.index = 0; op.auxindex = c_index;
        _do(key, &op);

        _corrections(t, t->root, key, 0, c_depth-1, cbk, cbk_arg);

        _undo(key, &op);
    }

    // transposition (prefix + suffix[1] + suffix[0] + suffix[2:])
    if (ksize != 0 && c_index < ksize-1) 
    {
        op.type = TRANSPOSE; op.index = c_index;
        _do(key, &op);

        _corrections(t, pprefix, key, c_index, c_depth-1, cbk, cbk_arg);

        _undo(key, &op);
    }

    // insertion (prefix + x + suffix[:])
    p = prefix->children;
    while(p)
    {
        op.type = INSERT; op.index = c_index; op.ich = p->key;
        _do(key, &op);

        _corrections(t, pprefix, key, c_index, c_depth-1, cbk, cbk_arg);

        _undo(key, &op);

        p = p->next;
    }

    // change (prefix + x + suffix[1:])
    if (c_index < ksize)
    {
        p = prefix->children;
        while(p) 
        {
            op.type = CHANGE; op.index = c_index; op.ich = p->key;
            _do(key, &op);

            _corrections(t, pprefix, key, c_index, c_depth-1, cbk, cbk_arg);

            _undo(key, &op);

            p = p->next;
        }
    }

    _corrections(t, prefix, key, c_index+1, c_depth, cbk, cbk_arg);
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
    trie_key_t pk;
    iter_pos_t *ip;
    iter_stack_t *k0,*k1;
    trie_node_t *prefix,*p;
    iter_pos_t ipos;
    int found;
    TRIE_CHAR ch;

    k0 = (iter_stack_t *)iter->stack0;
    k1 = (iter_stack_t *)iter->stack1;
    found = 0;
    while(1)
    {
        if (found)
        {
            return iter;
        }

        ip = PEEKI(k0);
        if (!ip) {
            iter->last = 1;
            return iter;
        }

        // previous key changes are delayed to this iteration.
        if (iter->depth_reached)
        {
            _undo(iter->key, &ip->op);
            POPI(k0);
            iter->depth_reached = 0;
            continue;
        }

        if (iter->keylen_reached)
        {
            ip = POPI(k1);
            _undo(iter->key, &ip->op);
            ip = POPI(k0);
            _undo(iter->key, &ip->op);
            iter->keylen_reached = 0; 
            continue;
        }

        prefix = ip->prefix;
        if (ip->op.index > 0) {
            if (ip->op.index-1 >= iter->key->size) {
                ip = POPI(k1);
                _undo(iter->key, &ip->op);
                ip = POPI(k0);
                _undo(iter->key, &ip->op);
                continue;
            }
            KEY_CHAR_READ(iter->key, ip->op.index-1, &ch);
            pk.s = (char *)&ch; pk.size = 1; pk.char_size = iter->key->char_size;
            prefix = _trie_prefix(ip->prefix, &pk);
            if (!prefix) {
                ip = POPI(k1);
                _undo(iter->key, &ip->op);
                ip = POPI(k0);
                _undo(iter->key, &ip->op);
                continue;
            }
        }

        // "only once" operations go under (pos == 0)
        if (ip->pos == 0)
        {
            _do(iter->key, &ip->op); 

            pk.s = &iter->key->s[ip->op.index*iter->key->char_size]; 
            pk.size = iter->key->size-ip->op.index; 
            pk.char_size = iter->key->char_size;
            p = _trie_prefix(prefix, &pk);
            if (p && p->value) {
                found = 1;
            }
        }

        if (ip->op.depth == 0)
        {
            iter->depth_reached = 1;
            continue;
        }

        if (ip->op.index > iter->key->size)
        {
            iter->keylen_reached = 1;
            continue;
        }

        // hold the doed() but not undoed() non-noop operations in history stack
        // that is processed first time.(in our current implementation we process 
        // the same op multiple times so simply check if it was processed before.
        if (ip->op.type != INDEXCHG && ip->pos == 0)
        {
            PUSHI(k1, ip);
        }

        if (ip->pos == 0) {
            ip->pos = 1;
            if (iter->key->size > 1 && ip->op.index < iter->key->size) {
                ipos.pos = 0; ipos.op.type = DELETE; ipos.op.index = 0; 
                ipos.op.auxindex = ip->op.index; ipos.op.depth = ip->op.depth-1; 
                ipos.iptr = NULL; ipos.prefix = iter->trie->root;
                PUSHI(k0, &ipos);
                continue;
            }
        }
        
        if (ip->pos == 1) {
            ip->pos = 2;
            if (iter->key->size != 0 && ip->op.index+1 < iter->key->size)
            {
                ipos.pos = 0; ipos.op.type = TRANSPOSE; 
                ipos.op.index = ip->op.index; ipos.op.depth = ip->op.depth-1; 
                ipos.iptr = NULL; ipos.prefix = ip->prefix;
                PUSHI(k0, &ipos);
                continue;
            }
        }
            
        if (ip->pos == 2) {
            if (!ip->iptr) {
                ip->iptr = prefix->children;
            } else {
                ip->iptr = ip->iptr->next;
            }           
            
            if (ip->iptr) {
                ipos.pos = 0; ipos.op.type = INSERT; ipos.op.index = ip->op.index; 
                ipos.op.depth = ip->op.depth-1; ipos.iptr = NULL; 
                ipos.op.ich = ip->iptr->key; ipos.prefix = ip->prefix;
                PUSHI(k0, &ipos);
                continue;
            } 

            ip->pos = 3;
        }
        
        if (ip->pos == 3) {
            if (ip->op.index < iter->key->size) 
            {
                if (!ip->iptr) {
                    ip->iptr = prefix->children;
                } else {
                    ip->iptr = ip->iptr->next;
                }            
                
                if (ip->iptr) {
                    ipos.pos = 0; ipos.op.type = CHANGE; 
                    ipos.op.index = ip->op.index; ipos.op.depth = ip->op.depth-1; 
                    ipos.iptr = NULL; ipos.op.ich = ip->iptr->key;
                    ipos.prefix = ip->prefix;
                    PUSHI(k0, &ipos);
                    continue;
                }
            }
            ip->pos = 4;
        }

        POPI(k0);
        ipos.pos = 0; ipos.op.type = INDEXCHG; ipos.op.index = ip->op.index+1; 
        ipos.op.depth = ip->op.depth; ipos.iptr = NULL;
        ipos.prefix = prefix;
        PUSHI(k0, &ipos);
    }
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