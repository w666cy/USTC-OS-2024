import os
import random
import subprocess
import unittest
from contextlib import contextmanager

from generate_test_files import *

FAT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fat16')

def run_cmd(cmd):
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    (proc_stdout, proc_stdout) = proc.communicate()
    return proc_stdout, proc_stdout

@contextmanager
def pushd(path):
    prev = os.getcwd()
    os.chdir(path)
    try:
        yield
    finally:
        os.chdir(prev)


class Fat16TestCase(unittest.TestCase):
    def check_dir_exist(self, dir: str) -> None:
        self.assertTrue(os.path.exists(dir), f'目录 {os.getcwd()} 下的子目录 {dir} 不存在')
        self.assertTrue(os.path.isdir(dir), f'目录 {os.getcwd()} 下的 {dir} 应该是目录，但它是文件')

    def check_file_exist(self, file: str) -> None:
        self.assertTrue(os.path.exists(file), f'目录 {os.getcwd()} 下的文件 {file} 不存在')
        self.assertTrue(os.path.isfile(file), f'目录 {os.getcwd()} 下的 {file} 应该是文件，但它是目录')
    
    def check_dir_deleted(self, dir: str) -> None:
        self.assertFalse(os.path.exists(dir), f'目录 {os.getcwd()} 下的子目录 {dir} 已被删除，但它仍然存在')
    
    def check_file_deleted(self, file: str) -> None:
        self.assertFalse(os.path.exists(file), f'目录 {os.getcwd()} 下的文件 {file} 已被删除，但它仍然存在')


    def check_file_content(self, file: str, content: bytes) -> None:
        self.check_file_exist(file)
        with open(file, 'rb') as f:
            file_content = f.read()
        file_lines = file_content.split(b'\n')
        content_lines = content.split(b'\n')

        # 检查行数是否一致
        self.assertEqual(len(file_lines), len(content_lines), f'文件 {file} 的行数不正确')

        # 检查每一行是否一致
        for i, (line1, line2) in enumerate(zip(file_lines, content_lines)):
            self.assertEqual(
                line1, line2, 
                f'文件 {file} 的内容与预期内容不符:\n' + \
                f'第 {i+1} 行不一致\n' +\
                f'预期内容: {line2}\n' + \
                f'实际内容: {line1}'
            )
    
    def check_dir_list(self, t: dict, path: str) -> None:
        """检查目录 path 下的文件列表是否和 expected 一致

        Args:
            t (dict): 期望的文件列表
            path (str): 要检查的目录
        """

        self.check_dir_exist(path)
        with pushd(path):
            dir_list = os.listdir()
            for list_name in dir_list:
                if list_name not in IGNORED_FILES:
                    self.assertIn(list_name, t.keys(), f'目录 {os.getcwd()} 中出现不应该存在的文件/目录 {list_name}')
            for name in t.keys():
                self.assertIn(name, dir_list, f'目录 {os.getcwd()} 下的 {name} 不存在')

    def check_dir(self, t: dict, path: str, check_content: bool=False) -> None:
        """检查目录 path 下的文件列表和类型是否和 expected 一致，必要时检查文件内容

        Args:
            t (dict): 期望的文件列表
            path (str): 要检查的目录
            check_content (bool, optional): 是否检查文件内容. Defaults to False.
        """

        self.check_dir_exist(path)
        with pushd(path):
            for list_name in os.listdir():
                if list_name not in IGNORED_FILES:
                    self.assertIn(list_name, t.keys(), f'目录 {path} 中出现不应该存在的文件/目录 {list_name}')
            for name, sub in t.items():
                if isinstance(sub, dict):
                    self.check_dir_exist(name)
                else:
                    self.check_file_exist(name)
                    if check_content:
                        self.check_file_content(name, sub)

    def check_tree(self, t: dict, path: str, check_content:bool=False) -> None:
        """测试 path 目录是否和 t 的结构一致

        Args:
            t (dict): 要测试的目录结构，字典的 key 是文件名，value 是文件内容或字典，若是字典代表为子目录
            path (str): 要测试的目录路径
            check_content (bool, optional): 是否检查文件内容. Defaults to False.
        """
        self.check_dir(t, path, check_content)
        with pushd(path):
            for name, sub in t.items():
                if isinstance(sub, dict):
                    self.check_tree(sub, name, check_content)

class Test_Task1_RootDirList(Fat16TestCase):
    def test1_list_root(self):
        with pushd(FAT_DIR):
            self.check_dir_list(TEST_DIR_STRUCTURE, FAT_DIR)


class Test_Task2_RootDirReadSmall(Fat16TestCase):
    def test1_seq_read_root_small_file(self):
        with pushd(FAT_DIR):
            self.check_file_exist(ROOT_SMALL_FILE)
            self.check_file_content(ROOT_SMALL_FILE, ROOT_SMALL_FILE_CONTENT)

class Test_Task3_RootDirCreateFile(Fat16TestCase):
    def test1_create_file(self):
        with pushd(FAT_DIR):
            name = 'newfile.txt'
            os.mknod(name, mode=0o666)
            self.check_file_exist(name)
            self.check_dir(TEST_DIR_STRUCTURE, FAT_DIR)

class Test_Task4_RootDirReadLarge(Fat16TestCase):
    def test1_seq_read_large_file(self):
        with pushd(FAT_DIR):
            self.check_file_exist(LARGE_FILE)
            self.check_file_content(LARGE_FILE, LARGE_FILE_CONTENT)

class Test_Task5_SubDirListAndCreateFile(Fat16TestCase):
    def test1_list_tree(self):
        with pushd(FAT_DIR):
            self.check_tree(TREE, TREE_DIR)

    def test2_subdir_create_file(self):
        with pushd(FAT_DIR):
            self.check_dir_exist(EMPTY_DIR)
            with pushd(EMPTY_DIR):
                name = 'newfile.txt'
                os.mknod(name, mode=0o666)
                self.check_file_exist(name)
                self.check_dir(TEST_DIR_STRUCTURE, FAT_DIR)

class Test_Task6_CreateDir(Fat16TestCase):
    def test1_root_create_dir(self):
        with pushd(FAT_DIR):
            name = 'newdir'
            os.mkdir(name, mode=0o777)
            self.check_dir_exist(name)
            self.check_dir(TEST_DIR_STRUCTURE, FAT_DIR)
            self.check_dir({}, name)

    def test2_subdir_create_dir(self):
        with pushd(FAT_DIR):
            name = 'newdir2'
            os.mkdir(name, mode=0o777)
            self.check_dir_exist(name)
            self.check_dir(TEST_DIR_STRUCTURE, FAT_DIR)

            with pushd(name):
                subname = 'subdir'
                os.mkdir(subname, mode=0o777)
                self.check_dir_exist(subname)
                self.check_dir(TEST_DIR_STRUCTURE, FAT_DIR)
                self.check_dir({subname: {}}, '.')
                self.check_dir({}, subname)
    
    def test3_create_tree(self):
        with pushd(FAT_DIR):
            tree_dir = 'newtree'
            os.mkdir(tree_dir, mode=0o777)
            create_tree(TREE, tree_dir)
            self.check_tree(TREE, tree_dir)

class Test_Task7_Remove(Fat16TestCase):
    def test1_remove_root_file(self):
        with pushd(FAT_DIR):
            name = 'delfile.txt'
            os.mknod(name, mode=0o666)
            self.check_file_exist(name)
            os.remove(name)
            self.check_file_deleted(name)
            self.check_dir(TEST_DIR_STRUCTURE, FAT_DIR)
    
    def test2_remove_root_dir(self):
        with pushd(FAT_DIR):
            name = 'deldir'
            os.mkdir(name, mode=0o777)
            self.check_dir_exist(name)
            os.rmdir(name)
            self.check_dir_deleted(name)
            self.check_dir(TEST_DIR_STRUCTURE, FAT_DIR)

    def test4_remove_not_empty_dir(self):
        with pushd(FAT_DIR):
            name = 'nedir'
            os.mkdir(name, mode=0o777)
            with pushd(name):
                subname = 'subdir'
                os.mkdir(subname, mode=0o777)
                self.check_dir_exist(subname)
            try:
                os.rmdir(name) # 应该失败
            except OSError:
                pass
            self.check_dir_exist(name)
            self.check_dir(TEST_DIR_STRUCTURE.copy() | {name:{}}, FAT_DIR)  # 此时根目录包含原结构和新目录

            with pushd(name):
                os.rmdir(subname)
                self.check_dir_deleted(subname)
            os.rmdir(name)
            self.check_dir_deleted(name)
            self.check_dir(TEST_DIR_STRUCTURE, FAT_DIR)
    
    def test5_remove_tree(self):
        with pushd(FAT_DIR):
            tree_dir = 'deltree'
            os.mkdir(tree_dir, mode=0o777)
            create_tree(TREE, tree_dir)
            self.check_tree(TREE, tree_dir)
            shutil.rmtree(tree_dir)
            self.check_dir_deleted(tree_dir)
            self.check_dir(TEST_DIR_STRUCTURE, FAT_DIR)

class Test_Task8_Write(Fat16TestCase):
    def test1_write_small_file(self):
        with pushd(FAT_DIR):
            name = 'newsmall.txt'
            with open(name, 'wb') as f:
                f.write(ROOT_SMALL_FILE_CONTENT)
            self.check_file_content(name, ROOT_SMALL_FILE_CONTENT)

            os.remove(name)
            self.check_file_deleted(name)
            self.check_dir(TEST_DIR_STRUCTURE, FAT_DIR)

    def test2_write_large_file(self):
        with pushd(FAT_DIR):
            name = 'newlarge.txt'
            content = LARGE_FILE_CONTENT[:4096 * 12 + 123]
            with open(name, 'wb') as f:
                f.write(content)
            self.check_file_content(name, content)

            os.remove(name)
            self.check_file_deleted(name)
            self.check_dir(TEST_DIR_STRUCTURE, FAT_DIR)

    def test3_write_large_file_clusters(self):
        with pushd(FAT_DIR):
            name = 'newlarge.txt'
            content = LARGE_FILE_CONTENT[:4096 * 12]
            cluster_size = 4096

            with open(name, 'wb') as f:
                f.write(content)
            self.check_file_content(name, content)

            random.seed(0)
            with open(name, 'rb+') as f:
                for i in range(100):
                    wlen = cluster_size
                    start = random.randint(0, len(content) - wlen) // cluster_size * cluster_size
                    c = random.randbytes(wlen)
                    self.assertTrue(len(c) == wlen)
                    f.seek(start)
                    f.write(c)
                    content = content[:start] + c + content[start + wlen:]
            
            self.check_file_content(name, content)
            os.remove(name)
            self.check_file_deleted(name)
            self.check_dir(TEST_DIR_STRUCTURE, FAT_DIR)


    def test4_write_large_file_random(self):
        with pushd(FAT_DIR):
            name = 'newlarge.txt'
            content = LARGE_FILE_CONTENT[:4096 * 12 + 123]

            with open(name, 'wb') as f:
                f.write(content)
            self.check_file_content(name, content)

            random.seed(0)
            with open(name, 'rb+') as f:
                for i in range(200):
                    wlen = random.randint(1, 8096)
                    start = random.randint(0, len(content) - wlen)
                    c = random.randbytes(wlen)
                    self.assertTrue(len(c) == wlen)
                    f.seek(start)
                    f.write(c)
                    content = content[:start] + c + content[start + wlen:]
            
            self.check_file_content(name, content)
            os.remove(name)
            self.check_file_deleted(name)
            self.check_dir(TEST_DIR_STRUCTURE, FAT_DIR)
    
    def test5_write_large_file_append(self):
        with pushd(FAT_DIR):
            name = 'newlarge.txt'
            content = LARGE_FILE_CONTENT[:4096 * 12]

            with open(name, 'wb') as f:
                f.write(content)
            self.check_file_content(name, content)

            random.seed(0)
            with open(name, 'rb+') as f:
                f.seek(4096 * 12)
                for i in range(100):
                    wlen = random.randint(1, 8096)
                    c = random.randbytes(wlen)
                    f.write(c)
                    content += c
            
            self.check_file_content(name, content)
            os.remove(name)
            self.check_file_deleted(name)
            self.check_dir(TEST_DIR_STRUCTURE, FAT_DIR)

