#!/usr/bin/env python

import os
from setuptools import setup
from distutils.core import Extension

f = open('README.md')
long_description = f.read()

HOMEPAGE = "http://bitbucket.org/sumerc/pyctrie/"
NAME = "pyctrie"
VERSION = "0.1"
DEBUG = False

user_macros = []
user_libraries = []
compile_args = []
link_args = []

if DEBUG:
    if os.name == 'posix':
        compile_args.append('-g')
    elif os.name == 'nt':
        compile_args.append('/UNDEBUG')
        link_args.append('/DEBUG')
        link_args.append('/MAP')
        compile_args.append('/Zi')
   
CLASSIFIERS = [
    'Development Status :: 5 - Production/Stable',
    'Environment :: Console',
    'Intended Audience :: Developers',
    'License :: OSI Approved :: MIT License',
    'Programming Language :: Python',
    'Programming Language :: Python :: 2',    
    'Programming Language :: Python :: 2.6',
    'Programming Language :: Python :: 2.7',
    'Programming Language :: Python :: 3.1',
    'Programming Language :: Python :: 3.2',
    'Programming Language :: Python :: 3.3',
    'Programming Language :: Python :: 3.4',
    'Programming Language :: Python :: Implementation :: CPython',
    'Operating System :: OS Independent',
    'Topic :: Software Development :: Libraries',
    'Topic :: Software Development :: Libraries :: Python Modules',
]

setup(name=NAME, 
    version=VERSION,
    author="Sumer Cip",
    author_email="sumerc@gmail.com",
    ext_modules = [Extension(
        "_triez",
        sources = ["_triez.c", "trie.c"],
        define_macros = user_macros,
        libraries = user_libraries,
        extra_compile_args = compile_args,
        extra_link_args = link_args,
        )],
    py_modules =  ["triez"],
    #entry_points = {
    #'console_scripts': [
    #    'triez = triez:main',
    #    ],
    #},
    description="Fast, pure C trie",
    long_description = long_description,
    keywords = "tree trie data structure dictionary",
    classifiers=CLASSIFIERS,
    license = "MIT",
    url = HOMEPAGE,
    download_url = "%s/%s-%s.tar.gz" % (HOMEPAGE, NAME, VERSION),
    test_suite = 'nose.collector'
)

