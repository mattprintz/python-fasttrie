Pyctrie
===================
Fast, pure C [Trie](http://en.wikipedia.org/wiki/Trie) dictionary

Features:
===================
  * Very fast. Same performance characteristics with Python's **dict**.
  * Supports fast **suffix**, **prefix**, **correction** (spell) operations.
  * Supports Python 2.6 <= x <= 3.4

Example:
===================

It is just like a dict:
```python
import triez
tr = triez.Trie()
tr[u"foo"] = 1
del trie[u"foo"]
```

But with extra features:
```python
tr[u"foo"] = 1
tr.corrections(u"fo")
{'foo'}
tr[u"foobar"] = 1
tr.prefixes(u"foobar")
{'foo', 'foobar'}
tr.suffixes(u"foo")
{'foo', 'foobar'}
```

Generator support:
```python
tr[u"foo"] = 1
tr[u"foobar"] = 1
for x in tr.iter_suffixes(u"foo"):
    print(x)
foo
foobar
```

License
===================

MIT