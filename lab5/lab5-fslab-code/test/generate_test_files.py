#!/bin/env python
import os, sys
import shutil
import random

LARGE_FILE = 'large.txt'
LARGE_FILE_SIZE = 1997773
LARGE_FILE_CONTENT = b''.join([f'{i:_<9}\n'.encode('ascii') for i in range(LARGE_FILE_SIZE // 10)]) + b'end'
assert(len(LARGE_FILE_CONTENT) == LARGE_FILE_SIZE)

SMALL_DIR = 'small'
SMALL_FILES = [f's{i:02}.txt' for i in range(20)]
SMALL_FILES_SIZE = [(i + 1)*4 for i in range(20)]
SMALL_FILES_CONTENT = [f'{i:04}'.encode('ascii') * (i + 1) for i in range(20)]

ROOT_SMALL_FILE = 'small.txt'
ROOT_SMALL_FILE_CONTENT = b"""Shall I compare thee to a summer's day?
Thou art more lovely and more temperate:
Rough winds do shake the darling buds of May,
And summer's lease hath all too short a date;
Sometime too hot the eye of heaven shines,
And often is his gold complexion dimm'd;
And every fair from fair sometime declines,
By chance or nature's changing course untrimm'd;
But thy eternal summer shall not fade,
Nor lose possession of that fair thou ow'st;
Nor shall death brag thou wander'st in his shade,
When in eternal lines to time thou grow'st:
   So long as men can breathe or eyes can see,
   So long lives this, and this gives life to thee.
"""

CHARS = 'abcdefghijklmnopqrstuvwxyz0123456789'
def random_name(l, r: random.Random):
    return ''.join(r.choices(CHARS, k=l))

def generate_tree_depth(depth, prefix="", r=random.Random(0)):
    if depth == 0:
        return r.choice([None, {}])
    tree = {}
    n = r.randint(1, 4)
    for i in range(n):
        d = depth - 1 if n == 0 else r.randint(0, depth - 1)
        p = prefix + chr(ord('a') + i)
        tree[p] = generate_tree_depth(d, p, r)
    return tree

TREE_DIR = 'tree'
TREE = generate_tree_depth(6)

EMPTY_DIR = 'emptydir'

TEST_DIR_STRUCTURE = {
    SMALL_DIR: dict(zip(SMALL_FILES, SMALL_FILES_CONTENT)),
    ROOT_SMALL_FILE: ROOT_SMALL_FILE_CONTENT,
    LARGE_FILE: LARGE_FILE_CONTENT,
    TREE_DIR: TREE,
    EMPTY_DIR: {},
}

IGNORED_FILES = ['newfile.txt', 'newdir', 'newdir2', 'newtree']

def create_tree(t: dict, path: str):
    for name, sub in t.items():
        if isinstance(sub, dict):
            os.mkdir(os.path.join(path, name), mode=0o777)
            create_tree(sub, os.path.join(path, name))
        else:
            os.mknod(os.path.join(path, name), mode=0o666)
            if sub is not None:
                with open(os.path.join(path, name), 'wb') as f:
                    f.write(sub)

        # if sub is None:
        #     os.mknod(os.path.join(path, name), mode=0o666)
        # else:
        #     os.mkdir(os.path.join(path, name), mode=0o777)
        #     create_tree(sub, os.path.join(path, name))

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print('Usage: python3 generate_test_files.py <dir>')
        exit(1)

    tmp_dir = sys.argv[1]
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    shutil.rmtree(tmp_dir, ignore_errors=True)
    os.makedirs(tmp_dir, exist_ok=True)

    os.chdir(tmp_dir)
    create_tree(TEST_DIR_STRUCTURE, '.')
