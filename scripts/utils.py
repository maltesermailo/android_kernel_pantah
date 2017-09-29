#!/usr/bin/env python3

import tempfile
import os
import re
import subprocess
from shutil import copyfile


def get_cmd_ret_status(cmd=[], ofile=None):
    try:
        subprocess.check_call(cmd, stdout=ofile)
    except subprocess.CalledProcessError as err:
        return err.returncode
    return 0

def get_cmd_output(cmd=[]):
        return subprocess.check_output(cmd)

def git_find_latest_tag():
    """ Get the latest available tag"""
    git_describe_cmd = ['git', 'describe', '--abbrev=0', '--tags']
    latest_tag = subprocess.check_output(git_describe_cmd).decode("utf-8").strip()
    return latest_tag

def make_kernel():
    """ Build the kernel with current configuration + debug info"""
    make_with_debug_info_cmd = ['make', '-j128', 'CONFIG_DEBUG_INFO=y']
    subprocess.check_call(make_with_debug_info_cmd)

def dump_kernel_abi(out_file, kernel_binary, abidw):
    """ Use abidw to dump kernel's abi"""
    abi_dumper_cmd = [abidw,  kernel_binary, '--out-file', out_file]
    subprocess.check_call(abi_dumper_cmd)

def compare_kernel_abis(reference_dump, current_dump, abidiff, abi_diff_path):
    """ Compare the built kernel's abi with the given reference kernel's abi """
    abi_diff_cmd = [abidiff, reference_dump, current_dump,
                ]
    with open(abi_diff_path, 'w') as f:
        get_cmd_ret_status(abi_diff_cmd, f)

def find_most_recent_reference(reference_dump_dir, arch):
    search_dir = os.path.join(reference_dump_dir, arch)
    if os.path.exists(search_dir) == False:
        print ("search dir", search_dir, 'does not exist')
        return None
    abi_dump_files =\
        [os.path.join(search_dir,f) for f in os.listdir(search_dir) if f.endswith(".abi")]
    for file in abi_dump_files:
      print ("file in ref dir", file)
    return max(abi_dump_files, key = os.path.getctime)

def find_kernel_binary():
    for base, dirnames, filenames in os.walk(os.getcwd()):
        for filename in filenames:
            if filename == 'vmlinux':
                return os.path.join(base, filename)
    return None

def generate_reference_dump_path(tag_or_sha, reference_dump_dir, arch):
    reference_dump_dir_arch = os.path.join(reference_dump_dir, arch)
    return os.path.join(reference_dump_dir_arch, 'kabi_' + tag_or_sha + '.abi')

def copy_reference_dump(src, dst):
    copyfile(src, dst)
    return
