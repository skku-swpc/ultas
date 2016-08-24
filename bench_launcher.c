#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <string.h>
#include "specshell.h"
#include "cpu_struct.h"

int run_hmmer (struct bench_package *pack)
{
	pid_t pid;
	pid_t *pid_set;

	int i;
	int status;
	int num;
	int err;
	int loop = 0;		//iteration count

	char log[100];
	char message[100];
	size_t cpu_size;
	cpu_set_t *cpu_setp;

	int shid;
	void *shared = (void *)0;

	struct bench_struct *b;

	if (pack == NULL)
	{
		logging("Pack is null", RUN_LABEL);

		shell_fault();

		exit(-1);
	}

	logging("Run_hmmer", RUN_LABEL);

	cpu_setp = CPU_ALLOC(8);
	cpu_size = CPU_ALLOC_SIZE(pack->bench_num);

	CPU_ZERO_S(cpu_size, cpu_setp);

	logging("Forking childeren", RUN_LABEL);

	pid_set = (pid_t *)malloc(sizeof(pid_t) * pack->bench_num);
	pid = fork();

	if (pid)
	{
		logging("Forking is successfully completed", RUN_LABEL);

		return 0;
	}

	while (loop++ < pack->iter)
	{
		sprintf(log, "%d's iteration", loop);
		fprintf(stdout, "%s\n", log);
		//logging(log, RUN_LABEL);

		for (i = 0;i < pack->bench_num;i++)
		{
			pid = fork();

			if (pid)
			{
				pid_set[i] = pid;

				CPU_SET(pack->benches[i].cpu, cpu_setp);
				sched_setaffinity(pid_set[i], cpu_size, cpu_setp);
				CPU_CLR(pack->benches[i].cpu, cpu_setp);
			}
			else
			{
				num = i;
				b = &pack->benches[i];

				break;
			}
		}

		if (pid)
		{
			/*send pid list to balancer */
			int str_len = 0;
			char temp[20];
			int j = 0;

			shid = shmget((key_t)PID_QUEUE_KEY, PID_QUEUE_SIZE, 0);
			shared = shmat(shid, (void *)0, 0666|IPC_CREAT);

			sprintf(message, "%s", (char *)shared);

			while (CHAR_TO_NUM(message[0]) != MESSAGE_FIN)
				sprintf(message, "%s", (char *)shared);

			sprintf(message, "%d ", MESSAGE_INS);
			j += 2;

			for (i = 0;i < pack->bench_num;i++)
			{
				sprintf(temp, "%d@%d-%d:", pid_set[i], pack->benches[i].cpu, pack->benches[i].bench_type);
				sprintf(message + j, "%s", temp);
				j += strlen(temp);
			}

			sprintf(message + j, "\0");

			sprintf((char *)shared, "%s\0", message);
		}

		/*Child process*/
		if (!pid)
		{
			char *argv[13] = {"hmmer", "--fixed", NULL, "--mean", NULL, "--num", NULL, "--sd", NULL, "--seed", NULL, NULL, NULL};

			char *argp[] =  {"PWD=/home/sebuns/work/specshell/specset/hmmer/", NULL};

			char arg_input[10], arg_fixed[10], arg_mean[10], arg_num[10], arg_sd[10], arg_seed[10];
			char out_name[20], err_name[20];
			int out_fd, err_fd;
			struct tm	*t;

			if (b == NULL)
				exit(-1);

			sprintf(arg_input, "%s_%d.hmm", b->hmmer_conf->input, num+1);
			sprintf(arg_fixed, "%d", b->hmmer_conf->fixed);
			sprintf(arg_mean, "%d", b->hmmer_conf->mean);
			sprintf(arg_num, "%d", b->hmmer_conf->num);
			sprintf(arg_sd, "%d", b->hmmer_conf->sd);
			sprintf(arg_seed, "%d", b->hmmer_conf->seed);

			/*Set the argument for hmmer*/

			argv[2] = arg_fixed;
			argv[4] = arg_mean;
			argv[6] = arg_num;
			argv[8] = arg_sd;
			argv[10] = arg_seed;
			argv[11] = arg_input;

			/*Constructing arguments is complete*/

			TIME_STAMP(t);

			sprintf(out_name, "hmmer_%d%d%d_%d.out", t->tm_hour, t->tm_min, t->tm_sec, num);
			sprintf(err_name, "hmmer_%d%d%d_%d.err", t->tm_hour, t->tm_min, t->tm_sec, num);

			out_fd = open(out_name, O_WRONLY | O_CREAT);
			err_fd = open(err_name, O_WRONLY | O_CREAT);

			chdir(SPECSHELL_HOME);
			err = chdir(HMMER_PATH);

			if (err < 0)
			{
				fprintf(stdout, "error\n");

				exit(-1);
			}

			/*redirecting stdout & stderr */

			dup2(out_fd, STDOUT_FILENO);
			dup2(err_fd, STDERR_FILENO);

			/*Freeing fds, because of these fds don't need to be exist*/

			fchmod(out_fd, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
			fchmod(err_fd, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

			close(out_fd);
			close(err_fd);

			/*run hmmer*/

			execve("/home/sebuns/work/specshell/specset/hmmer/hmmer", argv, argp);
		}
		else
		{
			/*
			 * Bench task's parent context
			 */

			int str_len = 0;
			char temp[20];
			int j = 0;

			struct rusage *usage;

			usage = (struct rusage*)malloc(sizeof(struct rusage) * pack->bench_num);

			for (i = 0;i < pack->bench_num;i++)
			{
				pid = wait3(&status, 0, &usage[i]);

				fprintf(stdout, "exit : %d\n", pid);
			}

			sprintf(message, "%d ", MESSAGE_DEL);
			j += 2;

			for (i = 0;i < pack->bench_num;i++)
			{
				sprintf(temp, "%d@%d-%d:", pid_set[i], pack->benches[i].cpu, pack->benches[i].bench_type);
				sprintf(message + j, "%s", temp);
				j += strlen(temp);
			}

			sprintf(message + j, "\0");

			sprintf((char *)shared, "%s\0", message);

			shid = shmget((key_t)PID_QUEUE_KEY, PID_QUEUE_SIZE, 0);

			sprintf(log, "Thresh : %d", cpu_conf.temp_thresh);
			logging(log, BENCH_LABEL);

			for (i = 0;i < pack->bench_num;i++)
			{
				sprintf(log, "system : %ld%06ldusec", usage[i].ru_stime.tv_sec, usage[i].ru_stime.tv_usec);
				logging(log, BENCH_LABEL);
				sprintf(log, "user : %ld%06ldusec", usage[i].ru_utime.tv_sec, usage[i].ru_utime.tv_usec);
				logging(log, BENCH_LABEL);
				sprintf(log, "rss :%ld", usage[i].ru_maxrss);
				logging(log, BENCH_LABEL);
				sprintf(log, "shared :%ld", usage[i].ru_ixrss);
				logging(log, BENCH_LABEL);
				sprintf(log, "page fault :%ld", usage[i].ru_majflt);
				logging(log, BENCH_LABEL);
				sprintf(log, "context switch :%ld", usage[i].ru_nvcsw);
				logging(log, BENCH_LABEL);
				sprintf(log, "forced context switch :%ld\n", usage[i].ru_nivcsw);
				logging(log, BENCH_LABEL);

			}


			free(usage);

			sleep(2);
		}

		/* clear pid set */
		for (i = 0;i < pack->bench_num;i++)
			pid_set[i] = 0;
	}	//end of while loop

	free(pid_set);

	exit(0);

	return 0;
}

int run_gobmk (struct bench_package *pack)
{
	pid_t pid;
	pid_t *pid_set;

	int i;
	int status;
	int num;
	int err;

	size_t cpu_size;
	cpu_set_t *cpu_setp;

	struct bench_struct *b;

	if (pack == NULL)
	{
		logging("Pack is null", RUN_LABEL);

		shell_fault();

		exit(-1);
	}

	logging("Run_gobmk", RUN_LABEL);

	cpu_setp = CPU_ALLOC(8);
	cpu_size = CPU_ALLOC_SIZE(pack->bench_num);

	CPU_ZERO_S(cpu_size, cpu_setp);

	logging("Forking childeren", RUN_LABEL);

	pid_set = (pid_t *)malloc(sizeof(pid_t) * pack->bench_num);
	pid = fork();

	if (pid)
	{
		logging("Forking is successfully completed", RUN_LABEL);

		return 0;
	}

	for (i = 0;i < pack->bench_num;i++)
	{
		pid = fork();

		if (pid)
		{
			pid_set[i] = pid;

			CPU_SET(pack->benches[i].cpu, cpu_setp);
			sched_setaffinity(pid_set[i], cpu_size, cpu_setp);
			CPU_CLR(pack->benches[i].cpu, cpu_setp);
		}
		else
		{
			num = i;
			b = &pack->benches[i];
			break;
		}
	}

	if (pid)
	{
		/*send pid list to balancer */
		int shid;
		void *shared = (void *)0;
		int str_len = 0;
		char message[100];
		char temp[20];
		int j = 0;

		sprintf(message, "%d ", MESSAGE_INS);
		j += 2;

		for (i = 0;i < pack->bench_num;i++)
		{
			sprintf(temp, "%d@%d\0", pid_set[i], pack->benches[i].cpu);
			sprintf(message + j, "%s:", temp);
			j += strlen(temp);
		}

		sprintf(message + j, "-1\0");

		shid = shmget((key_t)PID_QUEUE_KEY, PID_QUEUE_SIZE, 0);
		shared = shmat(shid, (void *)0, 0666|IPC_CREAT);

		sprintf((char *)shared, "%s\0", message);
	}

	/*Child process*/
	if (!pid)
	{
		char *argv[4] = {"gobmk", "--quiet", "--mode", "gtp"};

		char *argp[] =  {"PWD=/home/sebuns/work/specshell/specset/gobmk/", NULL};

		char in_name[10];
		char out_name[20], err_name[20];
		int in_fd, out_fd, err_fd;

		struct tm	*t;

		if (b == NULL)
			exit(-1);

		TIME_STAMP(t);


		sprintf(out_name, "gobmk_%d%d%d_%d.out", t->tm_hour, t->tm_min, t->tm_sec, num);
		sprintf(err_name, "gobmk_%d%d%d_%d.err", t->tm_hour, t->tm_min, t->tm_sec, num);

		out_fd = open(out_name, O_WRONLY | O_CREAT);
		err_fd = open(err_name, O_WRONLY | O_CREAT);

		chdir(SPECSHELL_HOME);
		err = chdir(HMMER_PATH);

		if (err < 0)
		{
			fprintf(stdout, "error\n");

			exit(-1);
		}

		/*redirecting stdout & stderr */

		dup2(out_fd, STDOUT_FILENO);
		dup2(err_fd, STDERR_FILENO);

		/*Freeing fds, because of these fds don't need to be exist*/

		fchmod(out_fd, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		fchmod(err_fd, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

		close(out_fd);
		close(err_fd);

		/*run hmmer*/

		execve("/home/sebuns/work/specshell/specset/hmmer/hmmer", argv, argp);
	}
	else
	{
		int shid;
		void *shared = (void *)0;
		int str_len = 0;
		char message[100];
		char temp[20];
		int j = 0;
		struct rusage *usage;

		usage = (struct rusage*)malloc(sizeof(struct rusage*) * pack->bench_num);

		for (i = 0;i < pack->bench_num;i++)
		{
			pid = wait3(&status, WEXITED, &usage[i]);

			fprintf(stdout, "exit : %d\n", pid);
		}

		sprintf(message, "%d ", MESSAGE_DEL);
		j += 2;

		for (i = 0;i < pack->bench_num;i++)
		{
			sprintf(temp, "%d@%d-%d:", pid_set[i], pack->benches[i].cpu, pack->benches[i].bench_type);
			sprintf(message + j, "%s", temp);
			j += strlen(temp);
		}

		sprintf(message + j, "-1\0");

		sprintf((char *)shared, "%s\0", message);

		shid = shmget((key_t)PID_QUEUE_KEY, PID_QUEUE_SIZE, 0);
		shmdt((void *)shared);

		exit(0);
	}

	return 0;

	return 0;
}

int run_mcf (struct bench_package *pack)
{
	return 0;
}

int run_bzip  (struct bench_package *pack)
{
	return 0;
}

int run_gcc (struct bench_package *pack)
{
	return 0;
}


int do_run(struct bench_package *pack)
{
	struct bench_struct *b;
	struct hmmer_config *h;

	int err;

	logging("Do-run phase", RUN_LABEL);

	err = chdir(SPECSHELL_HOME);
	err = chdir(SPEC_LOG);

	if (err < 0)
	{
		mkdir(SPEC_LOG, 0774);
		chdir(SPEC_LOG);
	}

	logging("CHDIR -> SPEC_LOG", RUN_LABEL);

	b = pack->benches;

	switch(b->bench_type)
	{
		case HMMER:
			pack->bop->run_hmmer(pack);

			return 0;

			break;
		case GOBMK:
			pack->bop->run_gobmk(pack);
		case MCF:
		case BZIP2:
		case GCC:

		default:
			break;
	}

	return 1;
}
