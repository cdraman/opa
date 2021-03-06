/* Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <sys/types.h>
#include <asm/unistd.h>
#include <unistd.h>
#include <assert.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <signal.h>
#include <linux/bpf.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>
#include "libbpf.h"
#include "bpf_load.h"

#define MAX_CNT 1000000

static __u64 time_get_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

#define HASH_PREALLOC		(1 << 0)
#define PERCPU_HASH_PREALLOC	(1 << 1)
#define HASH_KMALLOC		(1 << 2)
#define PERCPU_HASH_KMALLOC	(1 << 3)
#define LRU_HASH_PREALLOC	(1 << 4)
#define PERCPU_LRU_HASH_PREALLOC	(1 << 5)
#define LPM_KMALLOC		(1 << 6)
#define HASH_LOOKUP		(1 << 7)
#define ARRAY_LOOKUP		(1 << 8)

static int test_flags = ~0;

static void test_hash_prealloc(int cpu)
{
	__u64 start_time;
	int i;

	start_time = time_get_ns();
	for (i = 0; i < MAX_CNT; i++)
		syscall(__NR_getuid);
	printf("%d:hash_map_perf pre-alloc %lld events per sec\n",
	       cpu, MAX_CNT * 1000000000ll / (time_get_ns() - start_time));
}

static void test_lru_hash_prealloc(int cpu)
{
	__u64 start_time;
	int i;

	start_time = time_get_ns();
	for (i = 0; i < MAX_CNT; i++)
		syscall(__NR_getpid);
	printf("%d:lru_hash_map_perf pre-alloc %lld events per sec\n",
	       cpu, MAX_CNT * 1000000000ll / (time_get_ns() - start_time));
}

static void test_percpu_lru_hash_prealloc(int cpu)
{
	__u64 start_time;
	int i;

	start_time = time_get_ns();
	for (i = 0; i < MAX_CNT; i++)
		syscall(__NR_getppid);
	printf("%d:lru_hash_map_perf pre-alloc %lld events per sec\n",
	       cpu, MAX_CNT * 1000000000ll / (time_get_ns() - start_time));
}

static void test_percpu_hash_prealloc(int cpu)
{
	__u64 start_time;
	int i;

	start_time = time_get_ns();
	for (i = 0; i < MAX_CNT; i++)
		syscall(__NR_geteuid);
	printf("%d:percpu_hash_map_perf pre-alloc %lld events per sec\n",
	       cpu, MAX_CNT * 1000000000ll / (time_get_ns() - start_time));
}

static void test_hash_kmalloc(int cpu)
{
	__u64 start_time;
	int i;

	start_time = time_get_ns();
	for (i = 0; i < MAX_CNT; i++)
		syscall(__NR_getgid);
	printf("%d:hash_map_perf kmalloc %lld events per sec\n",
	       cpu, MAX_CNT * 1000000000ll / (time_get_ns() - start_time));
}

static void test_percpu_hash_kmalloc(int cpu)
{
	__u64 start_time;
	int i;

	start_time = time_get_ns();
	for (i = 0; i < MAX_CNT; i++)
		syscall(__NR_getegid);
	printf("%d:percpu_hash_map_perf kmalloc %lld events per sec\n",
	       cpu, MAX_CNT * 1000000000ll / (time_get_ns() - start_time));
}

static void test_lpm_kmalloc(int cpu)
{
	__u64 start_time;
	int i;

	start_time = time_get_ns();
	for (i = 0; i < MAX_CNT; i++)
		syscall(__NR_gettid);
	printf("%d:lpm_perf kmalloc %lld events per sec\n",
	       cpu, MAX_CNT * 1000000000ll / (time_get_ns() - start_time));
}

static void test_hash_lookup(int cpu)
{
	__u64 start_time;
	int i;

	start_time = time_get_ns();
	for (i = 0; i < MAX_CNT; i++)
		syscall(__NR_getpgid, 0);
	printf("%d:hash_lookup %lld lookups per sec\n",
	       cpu, MAX_CNT * 1000000000ll * 64 / (time_get_ns() - start_time));
}

static void test_array_lookup(int cpu)
{
	__u64 start_time;
	int i;

	start_time = time_get_ns();
	for (i = 0; i < MAX_CNT; i++)
		syscall(__NR_getpgrp, 0);
	printf("%d:array_lookup %lld lookups per sec\n",
	       cpu, MAX_CNT * 1000000000ll * 64 / (time_get_ns() - start_time));
}

static void loop(int cpu)
{
	cpu_set_t cpuset;

	CPU_ZERO(&cpuset);
	CPU_SET(cpu, &cpuset);
	sched_setaffinity(0, sizeof(cpuset), &cpuset);

	if (test_flags & HASH_PREALLOC)
		test_hash_prealloc(cpu);

	if (test_flags & PERCPU_HASH_PREALLOC)
		test_percpu_hash_prealloc(cpu);

	if (test_flags & HASH_KMALLOC)
		test_hash_kmalloc(cpu);

	if (test_flags & PERCPU_HASH_KMALLOC)
		test_percpu_hash_kmalloc(cpu);

	if (test_flags & LRU_HASH_PREALLOC)
		test_lru_hash_prealloc(cpu);

	if (test_flags & PERCPU_LRU_HASH_PREALLOC)
		test_percpu_lru_hash_prealloc(cpu);

	if (test_flags & LPM_KMALLOC)
		test_lpm_kmalloc(cpu);

	if (test_flags & HASH_LOOKUP)
		test_hash_lookup(cpu);

	if (test_flags & ARRAY_LOOKUP)
		test_array_lookup(cpu);
}

static void run_perf_test(int tasks)
{
	pid_t pid[tasks];
	int i;

	for (i = 0; i < tasks; i++) {
		pid[i] = fork();
		if (pid[i] == 0) {
			loop(i);
			exit(0);
		} else if (pid[i] == -1) {
			printf("couldn't spawn #%d process\n", i);
			exit(1);
		}
	}
	for (i = 0; i < tasks; i++) {
		int status;

		assert(waitpid(pid[i], &status, 0) == pid[i]);
		assert(status == 0);
	}
}

static void fill_lpm_trie(void)
{
	struct bpf_lpm_trie_key *key;
	unsigned long value = 0;
	unsigned int i;
	int r;

	key = alloca(sizeof(*key) + 4);
	key->prefixlen = 32;

	for (i = 0; i < 512; ++i) {
		key->prefixlen = rand() % 33;
		key->data[0] = rand() & 0xff;
		key->data[1] = rand() & 0xff;
		key->data[2] = rand() & 0xff;
		key->data[3] = rand() & 0xff;
		r = bpf_map_update_elem(map_fd[6], key, &value, 0);
		assert(!r);
	}

	key->prefixlen = 32;
	key->data[0] = 192;
	key->data[1] = 168;
	key->data[2] = 0;
	key->data[3] = 1;
	value = 128;

	r = bpf_map_update_elem(map_fd[6], key, &value, 0);
	assert(!r);
}

int main(int argc, char **argv)
{
	struct rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
	char filename[256];
	int num_cpu = 8;

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);
	setrlimit(RLIMIT_MEMLOCK, &r);

	if (argc > 1)
		test_flags = atoi(argv[1]) ? : test_flags;

	if (argc > 2)
		num_cpu = atoi(argv[2]) ? : num_cpu;

	if (load_bpf_file(filename)) {
		printf("%s", bpf_log_buf);
		return 1;
	}

	fill_lpm_trie();

	run_perf_test(num_cpu);

	return 0;
}
