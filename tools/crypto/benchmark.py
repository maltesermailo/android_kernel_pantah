#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# Crypto algorithm benchmark and testing script
#
# Userspace utility for /proc/cryptobench (CONFIG_CRYPTO_BENCHMARK).
#
# Copyright 2018 Google LLC
#

import argparse
import subprocess
import sys

KNOWN_CIPHERS = [
    {
        'name': 'aes',
        'blocksize': 16,
        'keysizes': [16, 24, 32],
        'impls': ['generic', 'aesni', 'ce', 'neonbs', 'neon'],
    }, {
        'name': 'blowfish',
        'blocksize': 8,
        'keysizes': [4, 16, 24, 32, 56],
        'impls': ['generic', 'asm'],
    }, {
        'name': 'camellia',
        'blocksize': 16,
        'keysizes': [16, 24, 32],
        'impls': ['generic', 'asm', 'aesni', 'aesni-avx'],
    }, {
        'name': 'cast5',
        'blocksize': 8,
        'keysizes': [5, 12, 16],
        'impls': ['generic', 'avx'],
    }, {
        'name': 'cast6',
        'blocksize': 16,
        'keysizes': [16, 24, 32],
        'impls': ['generic', 'avx'],
    }, {
        'name': 'chacha20',
        'keysizes': [32],
        'impls': ['generic', 'simd', 'neon', 'arm'],
    }, {
        'name': 'des3_ede',
        'blocksize': 8,
        'keysizes': [24],
        'impls': ['generic', 'asm']
    }, {
        'name': 'lea',
        'blocksize': 16,
        'keysizes': [16, 24, 32],
        'impls': ['generic', 'neon'],
    }, {
        'name': 'salsa20',
        'keysizes': [32],
        'impls': ['generic'],
    }, {
        'name': 'serpent',
        'blocksize': 16,
        'keysizes': [16, 24, 32],
        'impls': ['generic', 'sse2', 'avx', 'avx2'],
    }, {
        'name': 'twofish',
        'blocksize': 16,
        'keysizes': [16, 24, 32],
        'impls': ['generic', '3way', 'avx'],
    }
]

def find_cipher(name):
    for cipher in KNOWN_CIPHERS:
        if cipher['name'] == name:
            return cipher
    return None

def error(msg):
    sys.stderr.write(msg + '\n')
    sys.exit(1)

def MB_per_s(nbytes, ns_elapsed):
    return (nbytes * 1000) / ns_elapsed

class BenchmarkError(Exception):
    def __init__(self, message, error_type):
        super(BenchmarkError, self).__init__(message)
        self.error_type = error_type

def proc_cryptobench(cmd, args):
    if args.adb:
        sh_cmd = ''
        if args.cpu_mask:
            sh_cmd += ' taskset ' + args.cpu_mask
        sh_cmd += f' sh -c "echo -e \'{cmd}\' > /proc/cryptobench; cat /proc/cryptobench"'
        return str(subprocess.check_output(['adb', 'shell', sh_cmd]), 'utf-8')
    else:
        if args.cpu_mask:
            raise ValueError('TODO: support --cpu-mask without --adb')
        with open('/proc/cryptobench', 'rt+') as f:
            f.write(cmd)
            f.seek(0)
            return f.readline()

def do_kernel_benchmark(algtype, algname, keysize, args):
    cmd = ''
    cmd += ' algtype=' + algtype
    cmd += ' algname=' + algname
    cmd += ' keysize=' + str(keysize)
    cmd += ' niter=' + str(args.niter)
    cmd += ' bufsize=' + str(args.bufsize)
    if args.sgl_fuzz:
        cmd += ' sgl_fuzz'
    if args.inplace:
        cmd += ' inplace'
    cmd += '\n'

    fields = proc_cryptobench(cmd, args).split()
    if fields[0] == 'ERROR':
        raise BenchmarkError(f'error with algorithm {algname}', fields[1])

    results = {}
    for item in fields[1:]:
        (key, value) = item.split('=')
        results[key] = value
    return results

def check_measurement(prev_measurement, results, algname):
    measurement = results['measurement']
    if prev_measurement is not None and measurement != prev_measurement:
        error(f'Algorithm {algname} (driver: {results["driver_name"]}) gave inconsistent results!')
    return measurement

def benchmark_skcipher(algname, friendly_name, keysize, args):
    enc_time = 2**64
    dec_time = 2**64
    measurement = None
    for _try in range(args.ntries):
        try:
            results = do_kernel_benchmark('skcipher', algname, keysize, args)
        except BenchmarkError as e:
            if e.error_type != 'ALG_NOT_FOUND':
                print(f'{algname} {e.error_type}')
            return
        measurement = check_measurement(measurement, results, algname)
        enc_time = min(enc_time, int(results['enc_time']))
        dec_time = min(dec_time, int(results['dec_time']))
    print('{:17} {:30} {:30} {:4.2f} {:4.2f} {:17}'.format(
          friendly_name,
          algname,
          results['driver_name'],
          MB_per_s(int(args.niter) * int(args.bufsize), enc_time),
          MB_per_s(int(args.niter) * int(args.bufsize), dec_time),
          measurement))

def benchmark_hash(algname, keysize, args):
    time = 2**64
    measurement = None
    for _try in range(args.ntries):
        try:
            results = do_kernel_benchmark('hash', algname, keysize, args)
        except BenchmarkError as e:
            if e.error_type != 'ALG_NOT_FOUND':
                print(f'{algname} {e.error_type}')
            return
        measurement = check_measurement(measurement, results, algname)
        time = min(time, int(results['time']))
    print('{:17} {:30} {:6.1f} {:17}'.format(
          algname,
          results['driver_name'],
          MB_per_s(int(args.niter) * int(args.bufsize), time),
          measurement))

def keysize_for_mode(mode, keysize, blocksize):
    if mode == 'xts':
        return keysize * 2
    if mode == 'lrw':
        return keysize + blocksize
    return keysize

def benchmark_cipher_spec(cipher, keysize, args):
    blocksize = cipher.get('blocksize')
    name = cipher['name']
    if not blocksize or blocksize == 1:
        # stream cipher
        benchmark_skcipher(name, name, keysize, args)
        if args.all_impls:
            for impl in cipher['impls']:
                benchmark_skcipher(f'{name}-{impl}', name, keysize, args)
        return

    # block cipher
    available_modes = ['ecb', 'cbc', 'ctr']
    if blocksize == 16:
        available_modes.extend(['lrw', 'xts'])

    for mode in available_modes if args.modes is None else args.modes:
        algname = f'{mode}({name})'
        friendly_name = f'{name.upper()}-{keysize*8}-{mode.upper()}'
        actual_keysize = keysize_for_mode(mode, keysize, blocksize)
        benchmark_skcipher(algname, friendly_name, actual_keysize, args)

    if args.all_impls:
        for impl in cipher['impls']:
            for mode in available_modes if args.modes is None else args.modes:
                if impl == 'generic':
                    if mode == 'xts' or mode == 'lrw':
                        algname = f'{mode}(ecb({name}-generic))'
                    else:
                        algname = f'{mode}({name}-generic)'
                else:
                    algname = f'{mode}-{name}-{impl}'
                friendly_name = f'{name.upper()}-{keysize*8}-{mode.upper()}'
                actual_keysize = keysize_for_mode(mode, keysize, blocksize)
                benchmark_skcipher(algname, friendly_name, actual_keysize, args)

def parse_algnames(optarg):
    cur_name = ''
    nesting_level = 0
    names = set()
    for c in optarg + ',':
        if c == '(':
            nesting_level += 1
        elif c == ')':
            nesting_level -= 1
            if nesting_level < 0:
                raise ValueError(f"Malformed argument: {optarg}")
        elif c == ',' and nesting_level == 0 and cur_name != '':
            names.add(cur_name)
            cur_name = ''
            continue
        cur_name += c
    return sorted(names)

parser = argparse.ArgumentParser(description='Run cryptographic benchmarks.')

parser.add_argument('--ciphers', action='store', help='ciphers to enable')
parser.add_argument('--hashes', action='store', help='hashes to enable')
parser.add_argument('--modes', action='store', help='cipher modes to enable')
parser.add_argument('--keysizes', action='store', help='keysizes to enable')
parser.add_argument('--all-impls', action='store_true', help='test all impls')
parser.add_argument('--ntries', action='store', type=int, default=1, help='num tries per benchmark')
parser.add_argument('--bufsize', action='store', default=4096, help='buffer size')
parser.add_argument('--niter', action='store', default=4096, help='num iterations per benchmark')
parser.add_argument('--inplace', action='store_true', default=False, help='crypt in place?')
parser.add_argument('--sgl-fuzz', action='store_true', default=False, help='use random sglists')
parser.add_argument('--adb', action='store_true', default=False, help='use connected Android device')
parser.add_argument('--cpu-mask', action='store', help='CPUs to allow (default: all)')

args = parser.parse_args()

if args.ciphers:
    args.ciphers = parse_algnames(args.ciphers)

if args.hashes:
    args.hashes = parse_algnames(args.hashes)

if args.ntries <= 0:
    error('Must have ntries >= 1')

if not (args.ciphers or args.hashes):
    error('Must specify at least one of --ciphers or --hashes')

if args.modes:
    args.modes = sorted(set(x for x in args.modes.split(',')))

if args.keysizes:
    args.keysizes = sorted(set(int(x) for x in args.keysizes.split(',')))

if args.ciphers:
    for cipher_name in args.ciphers:
        cipher = find_cipher(cipher_name)
        if not cipher:
            if args.keysizes:
                cipher = {
                    'name': cipher_name,
                    'keysizes': [args.keysizes],
                    'impls': [],
                }
            else:
                error(f'Cipher "{cipher_name}" not found and --keysizes not specified!')
        for keysize in args.keysizes if args.keysizes else cipher['keysizes']:
            benchmark_cipher_spec(cipher, keysize, args)

if args.hashes:
    args.hashes = sorted(args.hashes)
    for hash_ in args.hashes:
        for keysize in args.keysizes if args.keysizes else [0]:
            benchmark_hash(hash_, keysize, args)
