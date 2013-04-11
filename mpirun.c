#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <mqueue.h>

#include "mpi.h"
#include "utils.h"

#define SHM_STRUCTURE_NAME 	"structure"
#define SHM_NP_NAME 		"np"
#define MAX_NAME_LEN 		20
#define MAX_BUF_LEN		2048

/*
 *Structure that contains information about a process.
*/
struct mpi_comm {
	pid_t pid;
	int rank;
	mqd_t local_queue;
} *mpi_comm_world;

/*
 *Structure that is used to send and receive information
 *between processes.
*/
struct my_struct {
	int count;
	char buffer[MAX_BUF_LEN];
	int tag;
	int datatype;
	int source;
} struct_to_send, struct_to_rec;

int started = 0;
int ended = 0;
int np;
int my_rank;

int DECLSPEC MPI_Init(int *argc, char ***argv)
{
	int fd_np_shm, fd_structure_shm;
	int size_np_shm, size_structure_shm;
	int pid, i;
	void *np_mem, *structure_mem;

	if(started == 1)
		return MPI_ERR_OTHER;

	//read number of processes from first shared memory
	size_np_shm = sizeof(int);
	fd_np_shm = open_shm(SHM_NP_NAME, size_np_shm, O_RDWR, &np_mem);
	np =  ((int*)np_mem)[0];

	//read the process rank from the second shared memory
	pid = getpid();
	size_structure_shm = np * sizeof(struct mpi_comm);
	fd_structure_shm = open_shm(SHM_STRUCTURE_NAME, size_structure_shm, O_RDWR, &structure_mem);

	i = 0;
	while(((struct mpi_comm*)structure_mem)[i].pid != pid)
		i++;
	my_rank = ((struct mpi_comm*)structure_mem)[i].rank;

	started = 1;
	return MPI_SUCCESS;
}

int DECLSPEC MPI_Initialized(int *flag)
{
	if(started == 1)
		*flag = 1;
	else *flag = 0;
	return MPI_SUCCESS;
}
int DECLSPEC MPI_Comm_size(MPI_Comm comm, int *size)
{
	if(started == 0)
		return MPI_ERR_OTHER;
	if(ended == 1)
		return MPI_ERR_OTHER;
	
	//set the size with the number of processes
	*size = np;

	return MPI_SUCCESS;
}
int DECLSPEC MPI_Comm_rank(MPI_Comm comm, int *rank)
{
	if(started == 0)
		return MPI_ERR_OTHER;
	if(ended == 1)
		return MPI_ERR_OTHER;

	*rank = my_rank;

	return MPI_SUCCESS;
}
int DECLSPEC MPI_Finalize()
{
	if(ended == 1)
		return MPI_ERR_OTHER;

	ended = 1;
	return MPI_SUCCESS;
}
int DECLSPEC MPI_Finalized(int *flag)
{
	if(ended == 1)
		*flag = 1;
	else *flag = 0;
	return MPI_SUCCESS;
}

int DECLSPEC MPI_Send(void *buf, int count, MPI_Datatype datatype, int dest,
		      int tag, MPI_Comm comm)
{
	int size_structure_shm;
	int fd_structure_shm;
	int i, rc;
	char queue_name[MAX_NAME_LEN];
	mqd_t proc_queue;
	
	if(started == 0)
		return MPI_ERR_OTHER;
	if(ended == 1)
		return MPI_ERR_OTHER;

	//set the fields to the structure that will be sended on queue
	struct_to_send.count 	= count;
	struct_to_send.datatype	= datatype; 
	struct_to_send.tag	= tag;
	struct_to_send.source	= my_rank;

	//open the queue for the process that will receive the information
	sprintf(queue_name, "/%i", dest);
	proc_queue = mq_open(queue_name, O_RDWR, 0666, NULL);
	DIE(proc_queue == (mqd_t)-1, "mq_open");

	switch(datatype){
		case MPI_CHAR:
			memcpy(struct_to_send.buffer, buf, count * sizeof(char));
			break;
		case MPI_INT:
			memcpy(struct_to_send.buffer, buf, count * sizeof(int));
			break;
		case MPI_DOUBLE:
			memcpy(struct_to_send.buffer, buf, count * sizeof(double));	
			break;
	}

	//send the structure on queue
	rc = mq_send(proc_queue, (char*)&struct_to_send, sizeof(struct_to_send), 1);
	DIE(rc == -1, "mq_send");

	return MPI_SUCCESS;
}
int DECLSPEC MPI_Recv(void *buf, int count, MPI_Datatype datatype,
		      int source, int tag, MPI_Comm comm, MPI_Status *status)
{
	int rc, prio, i;
	char queue_name[MAX_NAME_LEN];
	mqd_t proc_queue;

	if(started == 0)
		return MPI_ERR_OTHER;
	if(ended == 1)
		return MPI_ERR_OTHER;
	if(source != MPI_ANY_SOURCE)
		return MPI_ERR_RANK;
	if(tag != MPI_ANY_TAG)
		return MPI_ERR_TAG;

	//open the local queue of the process
	sprintf(queue_name, "/%i", my_rank);
	proc_queue = mq_open(queue_name, O_RDWR, 0666, NULL);
	DIE(proc_queue == (mqd_t)-1, "mq_open");

	//receive the structure from the local queue
	rc = mq_receive(proc_queue, (char*)&struct_to_rec, 10000000, &prio);
	DIE(rc == -1, "mq_receive");

	//set the Mpi_status structure
	if(status != MPI_STATUS_IGNORE) {
		(*status).MPI_TAG 	= struct_to_rec.tag;
		(*status).MPI_SOURCE 	= struct_to_rec.source;
		(*status)._size		= count;
	}

	//set the buffer with the wanted information
	switch(struct_to_rec.datatype) {	
		case MPI_CHAR:
			for(i = 0; i < count; i++, buf++)
				*(char*)buf = ((char*)(struct_to_rec.buffer))[i];
			break;
		case MPI_INT:			
			for(i = 0; i < count; i++) 
				((int*)buf)[i] = ((int*)(struct_to_rec.buffer))[i];
			break;
		case MPI_DOUBLE:
			for(i = 0; i < count; i++, buf++) 
				*(double*)buf = ((double*)(struct_to_rec.buffer))[i];
			break;
	}
	return MPI_SUCCESS;
}

int DECLSPEC MPI_Get_count(MPI_Status *status, MPI_Datatype datatype, int *count)
{
	if(started == 0)
		return MPI_ERR_OTHER;
	if(ended == 1)
		return MPI_ERR_OTHER;

	//return the size from MPI_Status structure
	*count = (*status)._size;

	return MPI_SUCCESS;
}

/*
 *Function that will open a shared memory.
*/
int open_shm(char *shm_name, int shm_size, int flags, void **mem)
{
	int shm_fd;	/* memory descriptor */
	int rc;

	/* create shm */
	shm_fd = shm_open(shm_name, flags, 0644);
	DIE(shm_fd == -1, "shm_open");

	/* resize shm to fit our needs */
	rc = ftruncate(shm_fd, shm_size);
	DIE(rc == -1, "ftruncate");

	*mem = mmap(0, shm_size, PROT_WRITE | PROT_READ, MAP_SHARED, shm_fd, 0);
	DIE(*mem == MAP_FAILED, "mmap");

	return shm_fd; 
}

/*
 *Function that closes and unmaps a shared memory
*/
int close_shm(char *shm_name, void *mem, int shm_fd, int shm_size)
{
	int rc;
	/* unmap shm */
	rc = munmap(mem, shm_size);
	DIE(rc == -1, "munmap");

	/* close descriptor */
	rc = close(shm_fd);
	DIE(rc == -1, "close");

	rc = shm_unlink(shm_name);
	DIE(rc == -1, "unlink");
 
	return 0;
}

int main(int argc, char **argv)
{
	int i, rc, status, np, nr_prog_arg, size_structure_shm, size_np_shm;
	int fd_structure_shm, fd_np_shm;
	char *prog, *command;	
	char **arguments;
	void *structure_mem, *np_mem;
	mqd_t local_queue;
	char local_queue_name[MAX_NAME_LEN];

	if(argc < 4)
	{
		printf("Wrong parameters!\n");
		return -1;
	}

	np = atoi(argv[2]);
	prog = argv[3];

	nr_prog_arg = argc - 4;
	arguments = calloc(nr_prog_arg + 2, sizeof(char*));
	arguments[0] = prog;
	for(i = 0; i < nr_prog_arg; i++)
	{
		arguments[i + 1] = argv[i + 4];
	}
	arguments[i + 1] = NULL;

	//alloc memory for the mpi_comm_world structure
	mpi_comm_world = calloc(np, sizeof(struct mpi_comm));

	//create the first shared memory for the processes number
	size_structure_shm = np * sizeof(struct mpi_comm);
	fd_structure_shm = open_shm(SHM_STRUCTURE_NAME, size_structure_shm, O_CREAT | O_RDWR, &structure_mem);
	memcpy(structure_mem, mpi_comm_world, size_structure_shm);

	//create the shared memory for the mpi_comm_world structure
	size_np_shm = sizeof(int);
	fd_np_shm = open_shm(SHM_NP_NAME, size_np_shm, O_CREAT | O_RDWR, &np_mem);
	((int*)np_mem)[0] = np;

	//create queues for all the processes
	for(i = 0; i < np; i ++) {
		sprintf(local_queue_name, "/%i", i);
		local_queue = mq_open(local_queue_name, O_CREAT | O_RDWR, 0666, NULL);
		DIE(local_queue == (mqd_t)-1, "mq_open");

		mpi_comm_world[i].local_queue = local_queue;
	}

	//create the processes
	for(i = 0; i < np; i++)
	{
		pid_t pid = fork();
		switch(pid) {
			case -1:
				return EXIT_FAILURE;
			case 0:
				((struct mpi_comm*)structure_mem)[i].pid = getpid();
				((struct mpi_comm*)structure_mem)[i].rank = i;

				command = calloc(strlen(prog) + 2, sizeof(char));
				sprintf(command, "./%s", prog);
				execvp(command, (char*const*) arguments);
				free(command);
				free(arguments);
				exit(EXIT_FAILURE);
			default:	
				break;
		}
	}
	free(arguments);

	for(i = 0; i < np; i++)
		waitpid(mpi_comm_world[i].pid, &status, 0);

	//close the shared memories and the queues
	close_shm(SHM_STRUCTURE_NAME, structure_mem, fd_structure_shm, size_structure_shm);
	close_shm(SHM_NP_NAME, np_mem, fd_np_shm, size_np_shm);

	for(i = 0; i < np; i++) {
		rc = mq_close(mpi_comm_world[i].local_queue);
		DIE(rc == -1, "mq_close");

		sprintf(local_queue_name, "/%i", i);
		rc = mq_unlink(local_queue_name);
		DIE(rc == -1, "mq_unlink");
	}

	free(mpi_comm_world);

	return 0;
}
