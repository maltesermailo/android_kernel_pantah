#!/usr/bin/env python3
import argparse
import os

from utils import git_find_latest_tag
from utils import make_kernel
from utils import dump_kernel_abi
from utils import compare_kernel_abis
from utils import copy_reference_dump
from utils import get_cmd_ret_status
from utils import find_most_recent_reference
from utils import find_kernel_binary
from utils import generate_reference_dump_path

SCRIPT_DIR = os.path.abspath(os.path.dirname(__file__))
TOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, *['..'] * 1))
ARCH = os.getenv('ARCH', 'arm64')
DEFAULT_OUT_FILE_PATH = os.path.abspath(os.path.join(TOT_DIR, ARCH + '.abi'))
DEFAULT_DIFF_REPORT_PATH = os.path.abspath(os.path.join(TOT_DIR, ARCH + '.abidiff'))
DEFAULT_REFERENCE_DUMP_DIR = os.path.abspath(os.path.join(TOT_DIR, 'abi-dumps'))

DEFAULT_ABIDW_PATH = 'abidw'
DEFAULT_ABIDIFF_PATH = 'abidiff'

class KernelTree(object):
    def __init__(self, args):
        self.kernel_tree = TOT_DIR
        self.kernel_binary = None
        if args.kernel_binary is not None:
            self.kernel_binary = os.abs.path(args.kernel_binary)
        self.reference_dump_dir = os.path.abspath(args.reference_dump_dir)
        self.abi_report_path = os.path.abspath(args.abi_report)
        self.out_file = os.path.abspath(args.out_file)
        self.abidw_path = os.path.abspath(args.abidw_path)
        self.abidiff_path = os.path.abspath(args.abidiff_path)
        self.verbose = args.verbose
        self.create_reference_dump = args.create_reference_dump

    def LOG(self, log_str, log_arg_str=''):
        if self.verbose == True:
            print('LOG:', log_str + ' '+ log_arg_str)

    def build_kernel(self):
        """ Builds the kernel with the current configuration"""
        os.chdir(os.path.abspath(self.kernel_tree))
        # Find the new tag and sync to it. If a new tag isn't specified and
        # use-tags was specified on the command line, sync to the latest
        # available tag.
        make_kernel()

    def compare_last_kernel_abi(self):
        """ Find the reference abi dump with the latest time stamp. If it
            exists, compare the current kernel's abi with the reference found.
        """
        os.chdir(os.path.abspath(self.kernel_tree))
        current_tag_or_sha = git_find_latest_tag()
        self.LOG("Looking for most recent abi dump in ",
                 self.reference_dump_dir)
        reference_dump_path =\
            find_most_recent_reference(self.reference_dump_dir, ARCH)
        if reference_dump_path is not None:
            self.LOG("Found most recent abi dump", reference_dump_path)
        kernel_binary = self.kernel_binary
        if kernel_binary is None:
            kernel_binary = find_kernel_binary()
        assert(kernel_binary is not None)
        self.LOG('Found kernel binary: ', kernel_binary)
        dump_kernel_abi(self.out_file, kernel_binary, self.abidw_path)
        if reference_dump_path is not None:
            self.LOG('Found most recent reference abi dump: ',
                     reference_dump_path)
            self.LOG('Comparing abis')
            compare_kernel_abis(reference_dump_path, self.out_file,
                                self.abidiff_path, self.abi_report_path)
            self.LOG('abidiff report can be found at ', self.abi_report_path)
        if self.create_reference_dump:
            os.makedirs(os.path.join(self.reference_dump_dir, ARCH), exist_ok=True)
            output_path = generate_reference_dump_path(current_tag_or_sha,
                                                       self.reference_dump_dir,
                                                       ARCH)
            copy_reference_dump(self.out_file, output_path)
            self.LOG('Reference dump created at', output_path)


def main():
    """ Build the linux kernel, freshly cloning if needed"""
    parser = argparse.ArgumentParser()
    parser.add_argument('--abi-report', help='Path to store abi diff report',
                        default=DEFAULT_DIFF_REPORT_PATH)
    parser.add_argument('--reference-dump-dir', help='Directory which houses\
                        reference dumps', default=DEFAULT_REFERENCE_DUMP_DIR)
    parser.add_argument('--out-file', help='Output abi dump file',
                        default=DEFAULT_OUT_FILE_PATH)
    parser.add_argument('--verbose', action='store_true', help='Verbose mode')
    parser.add_argument('--abidw-path', help='Path to abidw',
                        default=DEFAULT_ABIDW_PATH)
    parser.add_argument('--abidiff-path', help='Path to abidiff',
                        default=DEFAULT_ABIDIFF_PATH)
    parser.add_argument('--kernel-binary', help='Path to kernel binary')
    parser.add_argument('--create-reference-dump', action='store_true',
                        help='copy the produced abi dump into\
                        reference-dump-dir')
    args = parser.parse_args()
    kernel_tree = KernelTree(args)
    kernel_tree.build_kernel()
    kernel_tree.compare_last_kernel_abi()


if __name__ == '__main__':
    main()
