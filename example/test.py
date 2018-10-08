#!/usr/bin/env python3

import argparse
import os
import sys
import shutil
import subprocess
import signal
import filecmp

from pathlib import Path


WORK_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = str(Path(WORK_DIR).parent)
EXAMPLE_DIR = os.path.join(PROJECT_DIR, 'example')


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
    parser.add_argument("-m", "--mode", default='async',
                        help="client mode. 'seq' - write only after read and vice versa; 'async' - full duplex")
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
    build_path = build_dir(args)
    if not os.path.exists(build_path):
        os.mkdir(build_path)
    os.chdir(build_path)
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
    os.chdir(EXAMPLE_DIR)


def start_server(args):
    pidof_result = subprocess.run('pidof tcp_server'.split(), stdout=subprocess.PIPE)
    server_pid = pidof_result.stdout
    if server_pid and len(server_pid) != 0:
        print('Server instance is already running. Stopping...\r', end='')
        subprocess.run('kill {}'.format(server_pid.decode('ascii').strip()).split(),
                       stdout=subprocess.DEVNULL)
        print('Server instance is already running. Stopping... Done')
    print('Starting server on port {}...\r'.format(args.port), end='')
    server_exe = os.path.join(build_dir(args), 'bin/tcp_server')
    server_cmd = server_exe + ' -p {} -a 0.0.0.0:{} -m {}'.format(
        args.target_path, args.port, args.mode)
    log_file = open('server.log', 'w')
    pid = subprocess.Popen(server_cmd.split(), stdout=log_file, stderr=log_file)
    pid.log_file = log_file
    print('Starting server on port {}... Done'.format(args.port))
    return pid


def start_client(args):
    print('Starting client...\r', end='')
    client_exe = os.path.join(build_dir(args), 'bin/tcp_client')
    client_cmd = client_exe + ' -p {} -a 127.0.0.1:{} -m {}'.format(
        args.source_path, args.port, args.mode)
    log_file = open('client.log', 'w')
    pid = subprocess.Popen(client_cmd.split(), stdout=log_file, stderr=log_file)
    pid.log_file = log_file
    print('Starting client... Done')
    return pid


def wait_for_done(client_pid, server_pid):
    ret_code = client_pid.wait()
    if ret_code != 0:
        print('Client exited with failure')
    else:
        print('Client exited with success')
    client_pid.log_file.flush()
    client_pid.log_file.close()
    print('Stopping server...\r')
    server_pid.send_signal(signal.SIGTERM)
    server_pid.wait()
    server_pid.log_file.flush()
    server_pid.log_file.close()
    print('Stopping server... Done')


def compare_results(args):
    pjoin = lambda a, b : os.path.join(a, b)
    tp = args.target_path
    sp = args.source_path
    target_dir_contents = [pjoin(tp, f) for f in os.listdir(args.target_path) if os.path.isfile(pjoin(tp, f))]
    source_dir_contents = [pjoin(sp, f) for f in os.listdir(args.target_path) if os.path.isfile(pjoin(sp, f))]
    for sf in source_dir_contents:
        target_file = [f for f in target_dir_contents if os.path.basename(f) == os.path.basename(sf)]
        if len(target_file) == 0:
            raise Exception('File {} not found in the output'.format(sf))
        if not filecmp.cmp(sf, target_file[0], shallow=False):
            with open(sf, 'r') as sfd:
                with open(target_file[0]) as tfd:
                    sf_content = sfd.read()
                    tf_content = tfd.read()
                    for i in range(len(sf_content)):
                        if sf_content[i] != tf_content[i]:
                            print('Source file {} differs from target {}: pos: {}, source char: {} target char: {}'.
                                  format(sf, target_file[0], i, sf_content[i], tf_content[i]))
                            raise Exception('File {} differs from {}'.format(sf, target_file[0]))


def main():
    args = parse_args()
    print('Building...\r', end='')
    build_all(args)
    print('Building... Done')
    new_files_list = [str(i) + '.raw' for i in range(args.count)]
    prepare_initial_dir(args, new_files_list)
    server_pid = start_server(args)
    client_pid = start_client(args)
    wait_for_done(client_pid, server_pid)
    compare_results(args)


if __name__ == '__main__':
    main()
