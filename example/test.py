#!/usr/bin/env python3

import argparse
import os
import sys
import shutil
import subprocess
from pathlib import Path


WORK_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = str(Path(WORK_DIR).parent)


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source_path", default="/tmp/cio_example_in_data",
                        help="path to the data folder client will use to get files from")
    parser.add_argument("--target_path", default="/tmp/cio_example_out_data",
                        help="path to the data folder server will use to store received data into")
    parser.add_argument("--port", type=int, default=23452,
                        help="tcp port to use")
    parser.add_argument("-c", "--count", type=int, default=100,
                        help="number of files to generate")
    parser.add_argument("-s", "--size", type=int, default=100,
                        help="size of each file in Mb")
    parser.add_argument("-b", "--build_dir", default='.build',
                        help="build directory relative to the project root")
    return parser.parse_args()


def prepare_initial_dir(args, new_files_list):
    new_file_size = args.size*1024*1024
    pattern = b'hello'*1024*1024
    if not os.path.exists(args.source_path):
        os.mkdir(args.source_path)
    for entry in os.scandir(args.source_path):
        if entry.name not in new_files_list or new_file_size != entry.stat().st_size:
            os.remove(os.path.join(args.source_path, entry.name))
        else:
            new_files_list.remove(entry.name)
    for new_file in new_files_list:
        print('\rWriting... {}'.format(new_file), end='')
        with open(os.path.join(args.source_path, new_file), 'wb') as f:
            written = 0
            while written < new_file_size:
                write_size = min(len(pattern), new_file_size - written)
                f.write(pattern[:write_size])
                written += write_size
    if len(new_files_list) != 0:
        print('\r' + ' '*30, end='')
        print('\rWriting... Done'.format(new_file))


def build_dir(args):
    return os.path.join(PROJECT_DIR, args.build_dir)


def build_all(args):
    if not shutil.which('cmake'):
        raise Exception('Cmake not found')
    os.chdir(build_dir(args))
    if os.path.exists('CMakeCache.txt'):
        os.remove('CMakeCache.txt')
    if shutil.which('ninja'):
        if subprocess.run('cmake -G Ninja -DwithExamples=ON ..'.split(),
                          stdout=subprocess.DEVNULL).returncode != 0:
            raise Exception('Cmake failed')
        if subprocess.run('ninja'.split(), stdout=subprocess.DEVNULL).returncode != 0:
            raise Exception('Ninja failed')
    elif shutil.which('make'):
        cmd = 'cmake -G'.split() + ['Unix Makefiles'] + '-DwithExamples=ON ..'.split()
        if subprocess.run(cmd, stdout=subprocess.DEVNULL).returncode != 0:
            raise Exception('Cmake failed')
        if subprocess.run('make'.split(), stdout=subprocess.DEVNULL).returncode != 0:
            raise Exception('Make failed')
    else:
        raise Exception('No build system found')
    os.chdir(PROJECT_DIR)


def start_server(args):
    print('Starting server on port {}...\r'.format(args.port), end='')
    server_exe = os.path.join(build_dir(args), 'bin/tcp_server')
    server_cmd = server_exe + ' -p {} -a 0.0.0.0:{}'.format(args.target_path, args.port)
    subprocess.Popen(server_cmd.split(), stdout=subprocess.DEVNULL)
    print('Starting server on port {}... Done'.format(args.port))


def start_client(args):
    print('Starting client...\r', end='')
    client_exe = os.path.join(build_dir(args), 'bin/tcp_client')
    client_cmd = client_exe + ' -p {} -a 0.0.0.0:{}'.format(args.source_path, args.port)
    subprocess.Popen(client_cmd.split())
    print('Starting client... Done')


def main():
    args = parse_args()
    print('Building...\r', end='')
    build_all(args)
    print('Building... Done')
    new_files_list = [str(i) + '.raw' for i in range(args.count)]
    prepare_initial_dir(args, new_files_list)
    start_server(args)
    start_client(args)


if __name__ == '__main__':
    main()
