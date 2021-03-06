/*

 fasttrie - Fast, pure C trie


*/

#include "_fasttrie.h"

// globals
static PyObject *FasttrieError;

// defines
#ifdef IS_PEP393_AVAILABLE
#define FasttrieUnicode PyUnicode_DATA
#define FasttrieUnicode_Size PyUnicode_GET_LENGTH
size_t FasttrieUnicode_CharSize(PyObject *o) 
{
    switch(PyUnicode_KIND(o))
    {
        case PyUnicode_1BYTE_KIND:
            return 1;
        case PyUnicode_2BYTE_KIND:
            return 2;
        case PyUnicode_WCHAR_KIND:
            return 2;
        case PyUnicode_4BYTE_KIND:
            return 4;
    }
    return 0;
} 
#else
#define FasttrieUnicode PyUnicode_AS_UNICODE
#define FasttrieUnicode_Size PyUnicode_GET_SIZE
#define FasttrieUnicode_CharSize(o) sizeof(Py_UNICODE)
#endif

// forwards

// module functions

int _initialize(void)
{
    // module initialization
    return 1;
}

int _IsValid_Unicode(PyObject *s)
{
    if (PyBytes_Check(s)) {
        return 1;
    }

    if (!PyUnicode_Check(s)) {
        return 0;
    }
    
#ifdef IS_PEP393_AVAILABLE
    if (PyUnicode_READY(s) == -1) {
        return 0;
    }
#endif
    
    return 1;
}

PyObject *_Coerce_Unicode(PyObject *s)
{
    if (PyBytes_Check(s)) {
        return PyUnicode_FromString(PyBytes_AsString(s));
    }
    else {
        return s;
    }
}

trie_key_t _PyUnicode_AS_TKEY(PyObject *s)
{   
    trie_key_t k;
    
    k.s = (char *)FasttrieUnicode(s);
    k.size = FasttrieUnicode_Size(s);
    k.char_size = FasttrieUnicode_CharSize(s);

    //debug_print_key(&k);

    return k;
}

PyObject *_TKEY_AS_PyUnicode(trie_key_t *k)
{
    PyObject *r;
#ifdef IS_PEP393_AVAILABLE
    // If PEP393 is available, we always work on UCS4 buffers for 
    // suffix/prefix/correct functions.
    r = PyUnicode_FromKindAndData(PyUnicode_4BYTE_KIND, k->s, k->size);
#else
    r = PyUnicode_FromUnicode((const Py_UNICODE *)k->s, k->size);
#endif
    return r;
}

// module custom types
typedef struct {
    PyObject_HEAD
    trie_t *ptrie;
} TrieObject;

typedef struct {
    PyObject_HEAD

    // function pointers to control underlying iterator
    trie_iter_init_func_t iter_init_func;
    trie_iter_next_func_t iter_next_func;
    trie_iter_reset_func_t iter_reset_func;
    trie_iter_deinit_func_t iter_deinit_func;

    TrieObject *_trieobj; // used for Reference Count
    iter_t *_iter;
} TrieIteratorObject;

static void Trieiter_dealloc(TrieIteratorObject *tio)
{
    if (tio->_iter) {
        tio->iter_deinit_func(tio->_iter);
    }
    Py_XDECREF(tio->_trieobj);
    PyObject_GC_Del(tio);
}

static PyObject *Trieiter_next(TrieIteratorObject *tio)
{
    PyObject *ks;
    iter_t *iter;

    if (!tio->_iter) {
        return NULL;
    }
    
    iter = tio->iter_next_func(tio->_iter);
    if (iter->fail) {
        PyErr_SetString(PyExc_RuntimeError, "trie changed during iteration.");
        return NULL;
    }
    
    if (iter->last) {
        tio->iter_reset_func(tio->_iter);
        return NULL;
    }

    ks = _TKEY_AS_PyUnicode(iter->key);

    return ks;
}

PyObject *Trieiter_selfiter(PyObject *obj)
{
    TrieIteratorObject * tio;

    tio = (TrieIteratorObject *)obj;
    Py_INCREF(obj);
    if (tio->_iter && !tio->_iter->first) {
        tio->iter_reset_func(tio->_iter);
    }
    return obj;
}

PyTypeObject TrieIteratorType = {
#ifdef IS_PY3K
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                              /*ob_size*/
#endif
    "TrieIterator",                 /* tp_name */
    sizeof(TrieIteratorObject),     /* tp_basicsize */
    0,                              /* tp_itemsize */
    (destructor)Trieiter_dealloc,   /* tp_dealloc */
    0,                              /* tp_print */
    0,                              /* tp_getattr */
    0,                              /* tp_setattr */
    0,                              /* tp_reserved */
    0,                              /* tp_repr */
    0,                              /* tp_as_number */
    0,                              /* tp_as_sequence */
    0,                              /* tp_as_mapping */
    0,                              /* tp_hash */
    0,                              /* tp_call */
    0,                              /* tp_str */
    PyObject_GenericGetAttr,        /* tp_getattro */
    0,                              /* tp_setattro */
    0,                              /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC, /* tp_flags */
    0,                              /* tp_doc */
    0,                              /* tp_traverse */
    0,                              /* tp_clear */
    0,                              /* tp_richcompare */
    0,                              /* tp_weaklistoffset */
    Trieiter_selfiter,              /* tp_iter */
    (iternextfunc)Trieiter_next,    /* tp_iternext */
    0,                              /* tp_methods */
    0,
};

// Trie methods
static Py_ssize_t Trie_length(TrieObject *mp)
{
    return mp->ptrie->item_count;
}

static PyObject *Trie_subscript(TrieObject *mp, PyObject *key)
{
    trie_key_t k;
    PyObject *v;
    trie_node_t *w;

    if (!_IsValid_Unicode(key)) {
        PyErr_SetString(FasttrieError, "key must be a valid unicode string.");
        return NULL;
    }
    
    k = _PyUnicode_AS_TKEY(_Coerce_Unicode(key));
    w = trie_search(mp->ptrie, &k);
    if (!w) {
        PyErr_SetObject(PyExc_KeyError, key);
        return NULL;
    }
    
    v = (PyObject *)w->value;
    Py_INCREF(v);
    return v;
}

/* Return 0 on success, and -1 on error. */
static int Trie_ass_sub(TrieObject *mp, PyObject *key, PyObject *val)
{
    trie_key_t k;
    trie_node_t *w;
    
    if (!_IsValid_Unicode(key)) {
        PyErr_SetString(FasttrieError, "key must be a valid unicode string.");
        return -1;
    }
    
    k = _PyUnicode_AS_TKEY(_Coerce_Unicode(key));
    if (val == NULL) {
        //search and dec. ref. count
        w = trie_search(mp->ptrie, &k);
        if(!w) {
            PyErr_SetObject(PyExc_KeyError, key);
            return -1;
        }
        Py_DECREF((PyObject *)w->value);
        
        trie_del(mp->ptrie, &k);// no need for ret check as we already done above.
    } else {
        if(!trie_add(mp->ptrie, &k, (TRIE_DATA)val)) {
            PyErr_SetString(FasttrieError, "key cannot be added.");
            return -1;
        }
        Py_INCREF(val);
    }
    return 0;
}

static PyObject* Trie_mem_usage(TrieObject* self)
{
    return Py_BuildValue("l", trie_mem_usage(self->ptrie));
}

static PyObject* Trie_node_count(TrieObject* self)
{
    return Py_BuildValue("l", self->ptrie->node_count);
}

static PyObject *Trie_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    TrieObject *self;

    self = (TrieObject *)type->tp_alloc(type, 0);
    if (self != NULL) {
        self->ptrie = trie_create();
        if (!self->ptrie) {
            return NULL;
        }
    }

    return (PyObject *)self;
}

static int Trie_init(PyObject *self, PyObject *args, PyObject *kwds) 
{
    PyObject *result;
    // Temporarily increase refcount so args and kwds doesn't get GC'd
    Py_XINCREF(args);
    Py_XINCREF(kwds);

    result = Trie_update(self, args, kwds);
    if (result == NULL || PyErr_Occurred())
        return -1;

    Py_XDECREF(args);
    Py_XDECREF(kwds);
    return 0;
}

// Return 1 if `key` is in trie `op`, 0 if not, and -1 on error. 
int Trie_contains(PyObject *op, PyObject *key)
{
    trie_key_t k;
    TrieObject *mp;
    
    mp = (TrieObject *)op;
    
    if (!_IsValid_Unicode(key)) {
        return 0; // do not return exception here.
    }
    
    k = _PyUnicode_AS_TKEY(_Coerce_Unicode(key));
    
    if(!trie_search(mp->ptrie, &k)) {
        return 0;
    }
    
    return 1;
}

static PyObject *_create_iterator(TrieObject *trieobj, trie_key_t *key, 
    unsigned long max_depth, trie_iter_init_func_t init_func, 
    trie_iter_next_func_t next_func, trie_iter_reset_func_t reset_func,
    trie_iter_deinit_func_t deinit_func)
{
    TrieIteratorObject *tio;
    
    tio = PyObject_GC_New(TrieIteratorObject, &TrieIteratorType);
    if (tio == NULL) {
        return NULL;
    }
    
    tio->_trieobj = trieobj;
    Py_INCREF(tio->_trieobj);
    PyObject_GC_Track(tio);

    tio->iter_init_func = init_func;
    tio->iter_next_func = next_func;
    tio->iter_reset_func = reset_func;
    tio->iter_deinit_func = deinit_func;
    tio->_iter = init_func(trieobj->ptrie, key, max_depth);

    return (PyObject *)tio;
}

int _parse_traverse_args(TrieObject *t, PyObject *args, trie_key_t *k, 
    unsigned long *d)
{
    PyObject *pfx;
    unsigned long max_depth;
#ifdef IS_PEP393_AVAILABLE
    Py_UCS4 *_ucs4_buf;
#endif

    max_depth = 0;
    pfx = NULL;
    if (!PyArg_ParseTuple(args, "|Ok", &pfx, &max_depth)) {
        return 0;
    }

    // if max_depth == zero, set it to trie height which is the max. possible
    // depth. 
    if(!max_depth || max_depth > t->ptrie->height) {
        max_depth = t->ptrie->height;
    }
    *d = max_depth;
    
    if (!pfx) {
        memset(k, 0, sizeof(trie_key_t));
    } else {
        if (!_IsValid_Unicode(pfx)) {
            PyErr_SetString(FasttrieError, "key must be a valid unicode string.");
            return 0;
        }
        
#ifdef IS_PEP393_AVAILABLE
        // if PEP393 is available, we try to create a UCS4 buffer from the given 
        // string object. Some computation heavy functions in the Trie manipulates 
        // the key buffer, and so we make this conversion to simplify the process.
        // With this conversion we can safely write from TRIE_CHAR(trie) to 
        // trie_key_t (our out key) Otherwise, copying key buffers will be 
        // complex and slow.
        _ucs4_buf = PyUnicode_AsUCS4Copy(pfx);
        if (!_ucs4_buf) {
            return 0; // exception was set above. 
        }
        
        k->s = (char *)_ucs4_buf;
        k->size = FasttrieUnicode_Size(pfx);
        k->char_size = sizeof(Py_UCS4);
#else
        *k = _PyUnicode_AS_TKEY(_Coerce_Unicode(pfx));
#endif
    }
    
    return 1;
}

int _enum_keys(trie_key_t *k, trie_node_t *n, void *arg)
{
    PyList_Append((PyObject *)arg, _TKEY_AS_PyUnicode(k));
    return 0;
}

int _enum_items(trie_key_t *k, trie_node_t *n, void *arg)
{
    PyObject *tup = PyTuple_New(2);
    PyTuple_SetItem(tup, 0, _TKEY_AS_PyUnicode(k));
    PyTuple_SetItem(tup, 1, (PyObject *)n->value);
    PyList_Append((PyObject *)arg, tup);
    return 0;
}

int _enum_values(trie_key_t *k, trie_node_t *n, void *arg)
{
    PyList_Append((PyObject *)arg, (PyObject *)n->value);
    return 0;
}

int _set_items(trie_key_t *k, trie_node_t *n, void *arg)
{
    Trie_ass_sub((TrieObject *) arg, _TKEY_AS_PyUnicode(k), (PyObject *)n->value);
    return 0;
}

int _dec_ref_count(trie_key_t *k, trie_node_t *n, void *arg)
{
    Py_XDECREF((PyObject *)n->value);
}

static PyObject *Trie_keys(PyObject* selfobj, PyObject *args)
{
    trie_key_t k;
    unsigned long max_depth;
    PyObject *sfxs;

    if (!_parse_traverse_args((TrieObject *)selfobj, args, &k, &max_depth))
    {
        return NULL;
    }
    
    sfxs = PyList_New(0);
    trie_suffixes(((TrieObject *)selfobj)->ptrie, &k, max_depth, _enum_keys, sfxs);
    
    return sfxs;
}

static PyObject *Trie_items(PyObject *selfobj, PyObject *args)
{
    trie_key_t k;
    unsigned long max_depth;
    PyObject *values;

    if (!_parse_traverse_args((TrieObject *)selfobj, args, &k, &max_depth))
    {
        return NULL;
    }
    
    values = PyList_New(0);
    trie_suffixes(((TrieObject *)selfobj)->ptrie, &k, max_depth, _enum_items, values);
    
    return values;
}

static PyObject *Trie_values(PyObject* selfobj, PyObject *args)
{
    trie_key_t k;
    unsigned long max_depth;
    PyObject *values;

    if (!_parse_traverse_args((TrieObject *)selfobj, args, &k, &max_depth))
    {
        return NULL;
    }
    
    values = PyList_New(0);
    trie_suffixes(((TrieObject *)selfobj)->ptrie, &k, max_depth, _enum_values, values);
    
    return values;
}

static PyObject *Trie_get(PyObject* selfobj, PyObject *args, PyObject *kwds)
{
    const PyObject *default_value = Py_None;
    char *key;
    PyObject *value;
    static char *kwlist[] = {"key", "default", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|O", kwlist, &key, &default_value)) {
        return NULL;
    }
    // Get value using subscript func
    value = Trie_subscript(selfobj, key);
    if (value == NULL) {
        PyErr_Clear();
        value = default_value;
        Py_INCREF(value);
    }
    
    return value;
}

static PyObject *Trie_update(PyObject* selfobj, PyObject *args, PyObject *kwds)
{
    int i, tup_size;
    PyObject *arg = NULL, *item = NULL, *key = NULL, *value = NULL;
    PyObject *seq = NULL, *dict = NULL;

    Py_ssize_t pos;

    if (!PyArg_ParseTuple(args, "|O", &arg)) {
        return NULL;
    }

    if (arg) {
        if (PyDict_Check(arg)) {
            pos = 0;
            while (PyDict_Next(arg, &pos, &key, &value)) {
                Trie_ass_sub((TrieObject *) selfobj, key, value);
            }
        }
        // Check if arg is a Trie. Probably a better way to check this, but works for now.
        // TODO: Investigate better way to compare: PyObject_TypeCheck() ??
        else if (strcmp(arg->ob_type->tp_name, "Trie") == 0) {
            // TODO: Replace this with something cleaner?
            trie_key_t k;
            unsigned long max_depth;
            if (!_parse_traverse_args((TrieObject *)selfobj, PyTuple_New(0), &k, &max_depth)) return NULL;
            trie_suffixes(((TrieObject *)arg)->ptrie, &k, max_depth, _set_items, selfobj);
        }

        if (PySequence_Check(arg))  {
            for (i = 0; i < PySequence_Length(arg); i++) {
                item = PySequence_GetItem(arg, i);
                tup_size = PySequence_Length(item);
                if (!(PySequence_Check(item) && tup_size == 2)) {
                    PyErr_Format(PyExc_ValueError,
                                "trie update sequence element #%zd "
                                "has length %zd; 2 is required",
                                i, tup_size);
                    return NULL;
                }
                key = PySequence_GetItem(item, 0);
                value = PySequence_GetItem(item, 1);
                Trie_ass_sub((TrieObject *) selfobj, key, value);
            }
        }
    }
    if (kwds) {
        pos = 0;
        while (PyDict_Next(kwds, &pos, &key, &value)) {
            Trie_ass_sub((TrieObject *) selfobj, key, value);
        }
    }

    Py_RETURN_NONE;
}

static PyObject *Trie_clear(PyObject* selfobj, PyObject *args)
{
    trie_key_t k;
    unsigned long max_depth;

    if (!_parse_traverse_args((TrieObject *)selfobj, args, &k, &max_depth))
    {
        return NULL;
    }

    // Decrement refcount for all values in trie
    trie_suffixes(((TrieObject *)selfobj)->ptrie, &k, max_depth, _dec_ref_count, NULL);

    // Destroy existing trie and create fresh version
    trie_destroy(((TrieObject *)selfobj)->ptrie);
    ((TrieObject *)selfobj)->ptrie = trie_create();

    Py_RETURN_NONE;
}

static PyObject *Trie_copy(PyObject* selfobj, PyObject *args)
{
    TrieObject *copy = Trie_new((PyTypeObject *)selfobj->ob_type, PyTuple_New(0), PyDict_New());
    trie_key_t k;
    unsigned long max_depth;
    if (!_parse_traverse_args((TrieObject *)selfobj, PyTuple_New(0), &k, &max_depth)) return NULL;
    trie_suffixes(((TrieObject *)selfobj)->ptrie, &k, max_depth, _set_items, copy);
    return copy;
}

static PyObject *Trie_itersuffixes(PyObject* selfobj, PyObject *args)
{
    trie_key_t k;
    unsigned long max_depth;

    if (!_parse_traverse_args((TrieObject *)selfobj, args, &k, &max_depth)) {
        return NULL;
    }

    return _create_iterator((TrieObject *)selfobj, &k, max_depth, 
        trie_itersuffixes_init, trie_itersuffixes_next, trie_itersuffixes_reset, trie_itersuffixes_deinit);
}

static PyObject *Trie_prefixes(PyObject* selfobj, PyObject *args)
{
    trie_key_t k;
    unsigned long max_depth;
    PyObject *sfxs;

    if (!_parse_traverse_args((TrieObject *)selfobj, args, &k, &max_depth))
    {
        return NULL;
    }
    
    sfxs = PyList_New(0);
    trie_prefixes(((TrieObject *)selfobj)->ptrie, &k, max_depth, _enum_keys, sfxs);
    
    return sfxs;
}

static PyObject *Trie_iterprefixes(PyObject* selfobj, PyObject *args)
{
    trie_key_t k;
    unsigned long max_depth;

    if (!_parse_traverse_args((TrieObject *)selfobj, args, &k, &max_depth)) {
        return NULL;
    }

    return _create_iterator((TrieObject *)selfobj, &k, max_depth, 
        trie_iterprefixes_init, trie_iterprefixes_next, trie_iterprefixes_reset, 
        trie_iterprefixes_deinit);
}

static PyObject *Trie_corrections(PyObject* selfobj, PyObject *args)
{
    trie_key_t k;
    unsigned long max_depth;
    PyObject *sfxs;

    if (!_parse_traverse_args((TrieObject *)selfobj, args, &k, &max_depth))
    {
        return NULL;
    }
    
    sfxs = PyList_New(0);
    trie_corrections(((TrieObject *)selfobj)->ptrie, &k, max_depth, _enum_keys, sfxs);
    
    return sfxs;
}

static PyObject *Trie_itercorrections(PyObject* selfobj, PyObject *args)
{
    trie_key_t k;
    unsigned long max_depth;

    if (!_parse_traverse_args((TrieObject *)selfobj, args, &k, &max_depth)) {
        return NULL;
    }

    return _create_iterator((TrieObject *)selfobj, &k, max_depth, 
        trie_itercorrections_init, trie_itercorrections_next, 
        trie_itercorrections_reset, trie_itercorrections_deinit);
}

// Iterate keys start from root, depth is trie's height.
PyObject *Trie_iter(PyObject *obj)
{
    TrieObject *self;
    trie_key_t k;

    self = (TrieObject *)obj;

    // create an empty string
    k.s = "";
    k.size = 0;
#ifdef IS_PEP393_AVAILABLE
    k.char_size = sizeof(Py_UCS4);
#else
    k.char_size = sizeof(Py_UNICODE);
#endif
    
    return _create_iterator(self, &k, self->ptrie->height, 
        trie_itersuffixes_init, trie_itersuffixes_next, trie_itersuffixes_reset, trie_itersuffixes_deinit);
}

static void Trie_dealloc(TrieObject* self)
{
    PyObject *items = Trie_items(self, PyTuple_New(0));
    for (int i = 0; i < PySequence_Size(items); i++) {
        Py_DECREF(PySequence_GetItem(items, i));
    }
    trie_destroy(self->ptrie);
}

PyObject *Trie_getstate(PyObject *selfobj)
{
    trie_t *trie = ((TrieObject *)selfobj)->ptrie;
    trie_serialized_t *repr = trie_serialize(trie);

    PyObject *value_tup = PyTuple_New(repr->value_length - 1);
    for(int i = 1; i < repr->value_length; i++) {
        Py_INCREF((PyObject *) repr->value_ptrs[i]);
        PyTuple_SetItem(value_tup, (i - 1), (PyObject *) repr->value_ptrs[i]);
    }

    PyObject *state_tup = PyTuple_New(5);
    PyTuple_SetItem(state_tup, 0, PyLong_FromUnsignedLong(trie->node_count));
    PyTuple_SetItem(state_tup, 1, PyLong_FromUnsignedLong(trie->height));
    PyTuple_SetItem(state_tup, 2, PyLong_FromUnsignedLong(trie->mem_usage));
    PyTuple_SetItem(state_tup, 3, PyBytes_FromStringAndSize(repr->s, repr->s_length));
    PyTuple_SetItem(state_tup, 4, value_tup);
    
    // TODO: free memory for repr and its items

    return state_tup;
}

PyObject *Trie_setstate(PyObject *selfobj, PyObject *args)
{
    PyObject *state;
    if (!PyArg_ParseTuple(args, "O", &state)) {
        return -1;
    }
    unsigned long node_count;
    unsigned long item_count;
    unsigned long height;
    unsigned long mem_usage;
    PyObject *trie_byte_repr;
    PyObject *value_list;

    PyArg_ParseTuple(state, "lllOO", &node_count, &height, &mem_usage, 
        &trie_byte_repr, &value_list);

    trie_t *trie = ((TrieObject *)selfobj)->ptrie;
    trie_destroy(trie);
    trie_serialized_t *repr = (trie_serialized_t *)malloc(sizeof(trie_serialized_t));
    item_count = PySequence_Size(value_list);

    TRIE_DATA *value_ptrs = (TRIE_DATA *)malloc((item_count + 1) * sizeof(TRIE_DATA));
    value_ptrs[0] = NULL;
    for(int i = 0; i < item_count; i++) {
        PyObject *value = (PyObject *)PyTuple_GetItem(value_list, i);
        Py_INCREF(value);
        value_ptrs[i+1] = (void *)value;
    }

    repr->s = PyBytes_AsString(trie_byte_repr);
    repr->s_length = PyBytes_Size(trie_byte_repr);
    repr->value_ptrs = value_ptrs;
    repr->value_length = item_count + 1;

    trie = trie_deserialize(repr);

    ((TrieObject *)selfobj)->ptrie = trie;
    
    trie->node_count = node_count;
    trie->item_count = item_count;
    trie->height = height;
    trie->mem_usage = mem_usage;

    Py_RETURN_NONE;
}

/* Hack to implement "key in trie" */
static PySequenceMethods Trie_as_sequence = {
    0,                              /* sq_length */
    0,                              /* sq_concat */
    0,                              /* sq_repeat */
    0,                              /* sq_item */
    0,                              /* sq_slice */
    0,                              /* sq_ass_item */
    0,                              /* sq_ass_slice */
    Trie_contains,                  /* sq_contains */
    0,                              /* sq_inplace_concat */
    0,                              /* sq_inplace_repeat */
};

static PyMappingMethods Trie_as_mapping = {
    (lenfunc)Trie_length,           /*mp_length*/
    (binaryfunc)Trie_subscript,     /*mp_subscript*/
    (objobjargproc)Trie_ass_sub,    /*mp_ass_subscript*/
};

static PyMethodDef Trie_methods[] = {
    {"mem_usage", (PyCFunction)Trie_mem_usage, METH_NOARGS, 
        "Memory usage of the trie. Used for debugging purposes."},
    {"node_count", (PyCFunction)Trie_node_count, METH_NOARGS, 
        "Node count of the trie. Used for debugging purposes."},

    {"keys", Trie_keys, METH_VARARGS, "List of keys"},
    {"values", Trie_values, METH_VARARGS, "List of values"},
    {"items", Trie_items, METH_VARARGS, "List of items (key, value)"},
    {"get", Trie_get, METH_VARARGS | METH_KEYWORDS, "Get an item from the trie"},
    {"clear", Trie_clear, METH_VARARGS , "Clear all items from trie"},
    {"update", Trie_update, METH_VARARGS | METH_KEYWORDS, "Update a trie"},
    {"copy", Trie_copy, METH_NOARGS , "Return a shallow copy of trie with all keys/values."},
    // {"iter_suffixes", Trie_itersuffixes, METH_VARARGS, 
        // "T.iter_suffixes() -> a set-like object providing a view on T's suffixes"},
    // {"suffixes", Trie_keys, METH_VARARGS, 
        // "T.suffixes() -> a list containing T's suffixes"},
    // {"iter_prefixes", Trie_iterprefixes, METH_VARARGS, 
        // "T.iter_prefixes() -> a set-like object providing a view on T's prefixes"},
    // {"prefixes", Trie_prefixes, METH_VARARGS, 
        // "T.prefixes() -> a list containing T's prefixes"},
    {"iter_corrections", Trie_itercorrections, METH_VARARGS, 
        "T.iter_corrections() -> a set-like object providing a view on T's corrections"},
    {"corrections", Trie_corrections, METH_VARARGS, 
        "T.corrections() -> a list containing T's corrections"},
    
    // Methods for pickling/unpickling
    {"__getstate__", (PyCFunction)Trie_getstate, METH_NOARGS, "Internal state for pickling"},
    {"__setstate__", Trie_setstate, METH_VARARGS, "Return trie from pickled state"},
    {NULL}  /* Sentinel */
};

static PyTypeObject TrieType = {
#ifdef IS_PY3K
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                              /*ob_size*/
#endif
    "Trie",                         /* tp_name */
    sizeof(TrieObject),             /* tp_basicsize */
    0,                              /* tp_itemsize */
    (destructor)Trie_dealloc,       /* tp_dealloc */
    0,                              /* tp_print */
    0,                              /* tp_getattr */
    0,                              /* tp_setattr */
    0,                              /* tp_reserved */
    0,                              /* tp_repr */
    0,                              /* tp_as_number */
    &Trie_as_sequence,              /* tp_as_sequence */
    &Trie_as_mapping,               /* tp_as_mapping */
    PyObject_HashNotImplemented,    /* tp_hash  */
    0,                              /* tp_call */
    0,                              /* tp_str */
    0,                              /* tp_getattro */
    0,                              /* tp_setattro */
    0,                              /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /* tp_flags */
    "Trie objects",                 /* tp_doc */
    0,                              /* tp_traverse */
    0,                              /* tp_clear */
    0,                              /* tp_richcompare */
    0,                              /* tp_weaklistoffset */
    Trie_iter,                      /* tp_iter */
    0,                              /* tp_iternext */
    Trie_methods,                   /* tp_methods */
    0,                              /* tp_members */
    0,                              /* tp_getset */
    0,                              /* tp_base */
    0,                              /* tp_dict */
    0,                              /* tp_descr_get */
    0,                              /* tp_descr_set */
    0,                              /* tp_dictoffset */
    Trie_init,                      /* tp_init */
    0,                              /* tp_alloc */
    Trie_new,                       /* tp_new */
};

static PyMethodDef Fasttrie_methods[] = {
    {NULL, NULL}      /* sentinel */
};

#ifdef IS_PY3K
PyDoc_STRVAR(Fasttrie__doc__, "Fast, pure C, succinct trie");
static struct PyModuleDef Fasttrie_module = {
    PyModuleDef_HEAD_INIT,
    "Fasttrie",
    Fasttrie__doc__,
    -1,
    Fasttrie_methods,
    NULL,
    NULL,
    NULL,
    NULL
};
#endif

PyMODINIT_FUNC
#ifdef IS_PY3K
PyInit__fasttrie(void)
#else
init_fasttrie(void)
#endif
{
    PyObject *m;
    
    if (PyType_Ready(&TrieType) < 0) {
#ifdef IS_PY3K
        return NULL;
#else
        return;
#endif
    }

#ifdef IS_PY3K
    m = PyModule_Create(&Fasttrie_module);
    if (m == NULL)
        return NULL;
#else
    m = Py_InitModule("_fasttrie",  Fasttrie_methods);
    if (m == NULL)
        return;
#endif
    
    Py_INCREF(&TrieType);
    PyModule_AddObject(m, "Trie", (PyObject *)&TrieType);
    
    FasttrieError = PyErr_NewException("Fasttrie.Error", NULL, NULL);
    PyDict_SetItemString(PyModule_GetDict(m), "Error", FasttrieError);
    
    if (!_initialize()) {
        PyErr_SetString(FasttrieError, "fasttrie module cannot be initialized.");
#ifdef IS_PY3K
        return NULL;
#else
        return;
#endif
    }

#ifdef IS_PY3K
    return m;
#endif
}
