import sys

_is_py3k = (sys.version_info >= (3, 0))

if _is_py3k:
    xrange = range

def is_str(s):
    if _is_py3k:
        if not isinstance(s, str):
            return False
    else:            
        if not isinstance(s, basestring): # basestring = str + unicode
            return False
    return True
    
def is_py3k():
    return _is_py3k

def uni_escape(s):
    if not _is_py3k:
        return s.decode("unicode-escape")
    return s
