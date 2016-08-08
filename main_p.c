/*Main entry point for PARALLEL mergesort*/
#include <stdlib.h>
#include <mpi.h>
#include "mergesort.h"

	
#define DATA_TYPE double				//Change the data type for the values held in the database here
#define MPI_DATA_TYPE MPI_DOUBLE		//MPI data type must be equivalent to DATA_TYPE. A custom MPI data type might be needed.


int compfunc(DATA_TYPE *a, DATA_TYPE *b)
{
	DATA_TYPE deref_a = *a;
	DATA_TYPE deref_b = *b;
	
	//printf("COMPFUNC. A:%f, B:%f\n", deref_a, deref_b);
	
	if(deref_a < deref_b) 
		return -1;
	else if(deref_a > deref_b) 
		return 1;
	else 
		return 0;
}

DATA_TYPE* readDataFromFile(char *filename, int *elements_read)
{
	FILE *infile;
	size_t filesize;
	DATA_TYPE *all_data; 
	
	int elements, retval;
	
	//Open up the data file
	infile = fopen(filename, "r");
	
	if(infile == NULL)
	{
		perror("readDataFromFile: Failed to open data file");
		return NULL;
	}
	
	//Get the filesize
	fseek(infile, 0L, SEEK_END);
	filesize = ftell(infile);
	rewind(infile);	
	
	//Get the size of the input file, and allocate the data buffer based on this size.
	//The resulting buffer will be a bit bigger than needed, but that's okay for now.
	all_data = (DATA_TYPE*) malloc(filesize);
	
	//Read the data file into the buffer until we hit end of the file
	for(elements = 0, retval = 1; retval > 0; elements++)
		retval = fscanf(infile, "%lf\n", &all_data[elements]);		//NOTE: Change the format identifier here if DATA_TYPE has changed!
	fclose(infile);
	
	//Trim the unused memory out of the data buffer.
	//The last element is also removed since the previous file reading method adds an extra zero element
	elements -= 1;
	all_data = realloc(all_data, sizeof(DATA_TYPE)*elements);
	
	printf("Read in %d elements from the data file %s\n", elements, filename);
	
	//Return the pointer and number of elements
	*elements_read = elements;
	return all_data;
}

/*
Do not edit anything below this point unless you know what you're doing!
*/

/*Global definitions and variables*/
#define NO_OUTPUT_ARG "-benchmark"
#define NO_VERIFY_ARG "-noverify"

int p;		//total number of processes

//Struct containing the interpreted input arguments for the main master process
typedef struct{
	DATA_TYPE *data;		//Pointer to the data that was read in from the input file
	int elements;			//How many elements are in data?
	int output_enabled;		//Do we output the results once sorted?
	int no_verify;			//Skip the verification step after sorting?
}ParsedArgs;

//A struct of arguments instructing a node where to send/receive data during phase 2 merge
typedef struct{
	enum {NONE = 0, SEND, RECV} op;		//Should the node send data, receive data, or do nothing?
	int elements;						//# of elements to receive or send for this operation
	int target;							//The node to send/receive from
	int completed;						//Does node 1 have a completed merged list?
} MergeInstr;

ParsedArgs parseArgs(int argc, char *argv[])
{
	int i;
	ParsedArgs parsed_args;

	//Default values for args to be returned
	parsed_args.output_enabled = 1; 
	parsed_args.no_verify = 0;
	
	//If there are more than 2 args, the user simply just want to sort a file
	if(argc == 2)
	{
		//Read in data from the specified file using argument 2
		parsed_args.data = readDataFromFile(argv[1], &parsed_args.elements);
	}
	
	//If there are more than 2 args, the user has specified an option
	else
	{
		//Treat arguments 1 to n-2 as all options
		for(i=1; i<argc-1; i++)
		{
			if(strncmp(argv[i], NO_OUTPUT_ARG, strlen(NO_OUTPUT_ARG)) == 0)
				parsed_args.output_enabled = 0; 
			
			else if(strncmp(argv[i], NO_VERIFY_ARG, strlen(NO_VERIFY_ARG)) == 0)
				parsed_args.no_verify = 1; 
			
			else
				printf("Ignored unknown option \"%s\"\n", argv[i]);
		}

		//Treat the argument n-1 as the filename, and read in data from the specified file
		parsed_args.data = readDataFromFile(argv[argc-1], &parsed_args.elements);
	}
	
	//Check if the file read was sucessful
	if(parsed_args.data == NULL)
	{
			printf("readDataFromFile returned a NULL pointer!");
			MPI_Abort(MPI_COMM_WORLD, -1);
	}
	
	return parsed_args;
}

//Calculates the count and displacement used by mpi_scatterv
void calcCountDisp(int *node_status, int *node_disp, int p, int elements)
{
		int i, j;
		int elements_per_node = elements/p;
		
		if(elements < p)
		{
			memset(node_disp, 0, sizeof(int)*p);	//Set all node_disp[i] = 0
			memset(node_status, 0, sizeof(int)*p);	//Set all node_status[i] = 0
			node_status[0] = elements;
			return;
		}
		
		for(i=0; i<p; i++)
		{
			if(i == p-1)
				node_status[i] = elements - i*elements_per_node;
			else
				node_status[i] = elements_per_node;
			
			node_disp[i] = i*elements_per_node;
		}
}

MergeInstr planMerge(int *status, int id)
{
	int i, j;
	int has_data_count = 0, handled = 0;
	MergeInstr instr;
	
	//Default values
	instr.op = NONE;
	instr.elements = 0;
	instr.target = -1;
	instr.completed = 0;
	
	//Count how many nodes have data during this iteration
	for(i=1; i<p; i++)
	{
		if(status[i] > 0)
			has_data_count++;
	}
	has_data_count++;		//Add 1 to count since node 0 always has data and is ignored in the previosu loop.	
	
	if(has_data_count == 1)
	{
		instr.op = NONE;
		instr.completed = 1;
		return instr;
	}
	
	for(i=0; i<p; i++)
	{
		//No need to process nodes that do not have data left
		if(status[i] <= 0)
			continue;
		
		//If all nodes have data, then this must be the first iteration.
		if(has_data_count == p)
		{
			//If this is the last node and there are even number of nodes, don't merge on the first iteration
			if(i == p-1 && p % 2 == 1)
			{
				if(i == id )
				{
					instr.op = NONE;
					instr.completed = 0;
				}
			}
			
			//If i'm an odd numbered node, send data to node i-1
			else if(i%2 == 1)
			{
				if(i == id)
				{
					instr.op = SEND;
					instr.target = i-1;
					instr.elements = status[i];
					instr.completed = 0;
				}
				status[i] = 0;
			}
			
			//If i'm an even numbered node, receive data from node i+1
			else if (i%2 == 0)
			{
				if(i == id)
				{
					instr.op = RECV;
					instr.target = i+1;
					instr.elements = status[i+1];
					instr.completed = 0;
				}
				status[i] += status[i+1];
			}
		}
		
		//Any other iterations with a odd number of nodes having data
		else if (has_data_count % 2 == 1)
		{
			handled++;
			
			//If i'm the last node with data, do nothing for the next iteration.
			if(handled == has_data_count)
			{
				if(i == id)
				{
					instr.op = NONE;
					instr.completed = 0;
				}
			}
			
			//On a node with data, if handled is odd, this will be a receiving node for the next iteration
			else if(handled % 2 == 1)
			{
				//Find the next node with data 
				for(j=i+1; j<p; j++)
					if(status[j] > 0) break;
				
				status[i] += status[j];
				
				if(i == id)
				{
					instr.op = RECV;
					instr.target = j;
					instr.elements = status[j];
					instr.completed = 0;
				}
			}
			//On a node with data, if handled is even, this will be a sending node for the next iteration
			else if(handled % 2 == 0)
			{
				if(i == id)
				{
					//Find the next node with data 
					for(j=i-1; j>=0; j--)
						if(status[j] > 0) break;
				
					instr.op = SEND;
					instr.elements = status[i];
					instr.target = j;
					instr.completed = 0;
				}
				status[i] = 0;
			}
		}
		
		//Any other iterations with an even number of nodes having data
		else if (has_data_count % 2 == 0)
		{
			handled++;
			
			//On a node with data, if handled is odd, this will be a receiving node for the next iteration
			if(handled % 2 == 1)
			{
				//Find the next node with data 
				for(j=i+1; j<p; j++)
					if(status[j] > 0) break;
				
				status[i] += status[j];
				
				if(i == id)
				{
					instr.op = RECV;
					instr.elements = status[j];
					instr.target = j;
					instr.completed = 0;
				}
			}
			//On a node with data, if handled is even, this will be a sending node for the next iteration
			else if(handled % 2 == 0)
			{
				if(i == id)
				{
					//Find the next node with data 
					for(j=i-1; j>=0; j--)
						if(status[j] > 0) break;
				
					instr.op = SEND;
					instr.elements = status[i];
					instr.target = j;
					instr.completed = 0;
				}
				status[i] = 0;
			}
		}
	}
	return instr;
}

DATA_TYPE* recvAndMerge(DATA_TYPE *data, int n, MergeInstr instr)
{
	//printf("NODE: %d Iter: %d OP: RECV, Target: %d Before: %d Elements to S/R: %d After: %d Comp: %d\n", id, i++, instr.target, node_status[id], instr.elements, merge_status[id], instr.completed);
	//printf("Node: %d, RECV buffer size: %d\n", id, node_status[id] + instr.elements);

	DATA_TYPE *tdata1;			//Temporary buffer to hold the current data at the node
	DATA_TYPE *tdata2;			//Temporary buffer to hold the incoming data 
	MPI_Status recv_status;	
	
	tdata1 = (DATA_TYPE*) malloc(sizeof(DATA_TYPE)*n);					
	tdata2 = (DATA_TYPE*) malloc(sizeof(DATA_TYPE)*instr.elements);		

	memcpy(tdata1, data, (sizeof(DATA_TYPE)*n));						//tdata1 = data
	
	//Receive the expected data
	MPI_Recv(tdata2, instr.elements, MPI_DATA_TYPE, instr.target, 0, MPI_COMM_WORLD, &recv_status);
	
	//Allocate a bigger chunk of memory for "data" to hold the merged results of tdata1 and tdata2
	free(data);
	data = (DATA_TYPE*) malloc(sizeof(DATA_TYPE)*(n + instr.elements));	
	
	//Merge "tdata1" and "tdata2" together back into "data"
	Merge(data, tdata1, n, tdata2, instr.elements, sizeof(DATA_TYPE), compfunc);
	
	free(tdata1);
	free(tdata2);
	
	return data;
}

int checkSorted(DATA_TYPE *data, int n, int (*comp)(void *, void *))
{
	int i;
	int sorted = 1;
	
	for(i=1; i<n; i++)
	{
		if(((*comp)(&data[i-1], &data[i])) == 1)		//if(data[i-1] > data[i])
		{
			sorted = 0;
			break;
		}
	}
	printf("VERIFICATION: The array %s sorted!\n", sorted? "is":"is NOT");
	return sorted;
}

//STUB FOR NOW
void outputResult(DATA_TYPE *data, int n)
{
	//int i;
	
	//printing all elements in the array once its sorted.
	/*for(i = 0; i<elements; i++) 
	{
		printf("%f\n", data[i]);
	}*/
	
	printf("Output result stub is called!\n");
}

double masterProcess(int id, ParsedArgs args)
{
	double elapsed_time;
	
	//Needed by master node only
	int output_enabled = args.output_enabled;	//If we're running a benchmark, don't output the result
	int elements = args.elements;				//How many elements are in the expected data set
	int no_verify = args.no_verify;				//Skip verification after sorting completes?
	DATA_TYPE *all_data = args.data; 			//The master's initial data buffer to hold generated data or data read from file
	
	//Variables related to send and receiving data
	int *node_disp;				//An array used by scatterv to know where to break the data for distribution
	int *node_status;			//An array keeping track of the data size of each node's sorted sublist at the CURRENT time
	int *merge_status;			//An array keeping track of the data size of each node's sorted sublist AFTER one iteration of phase 2 merge
	MergeInstr instr;			//A struct telling a node where to send its sorted sublist, or who to receive it from
	MPI_Status recv_status;	
	
	//Data related variables for all nodes
	int filesize;
	int i,j, retval;
	DATA_TYPE *data, *tdata1, *tdata2;	

	
	/*****************************************************/
	/*  Distributing Data and Sorting Initial Sublists   */
	/*****************************************************/	
	
	//Start recording the sorting time.
	printf("\n======BEGIN=======\n\n");
	elapsed_time = -MPI_Wtime();		
	
	//If there's only one process available, just do a simple serial mergesort
	if(p == 1)
	{
		printf("NOTE: Only 1 process found. Running a serial version of Mergesort instead\n");
		MergeSort(all_data, elements, sizeof(DATA_TYPE), compfunc);

		//Stop recording the sorting time
		elapsed_time += MPI_Wtime();			

		//Verify the data is indeed sorted
		if(!no_verify)
			checkSorted(all_data, elements, compfunc);
		
		//Outputs the results if needed
		if(output_enabled)
				outputResult(all_data, elements);
		
		free(all_data);
		return elapsed_time;
	}
	
	//Proceeding with parallel mergesort if there's more than 1 processes
	
	node_status = (int*) malloc(sizeof(int)*p);
	node_disp = (int*) malloc(sizeof(int)*p);
	merge_status = (int*) malloc(sizeof(int)*p);
	
	//Broadcast the number of elements to all other processes so they can allocate a memory buffer for the task.
	printf("***Broadcasting Size!***\n");
	MPI_Bcast(&elements, 1, MPI_INT, 0, MPI_COMM_WORLD);
	
	//Calculating how many elements each process will be receiving, and its displacement
	calcCountDisp(node_status, node_disp, p, elements);
	data = (DATA_TYPE*) malloc(sizeof(DATA_TYPE) * node_status[id]);
	
	//Send a chunk of the data file to all nodes
	printf("***Distributing Data!***\n");
	MPI_Scatterv(all_data, node_status, node_disp, MPI_DATA_TYPE, data, node_status[id], MPI_DATA_TYPE, 0, MPI_COMM_WORLD);
	free(all_data);
	free(node_disp);
	
	//Calling merge sort to sort the received data portion. 
	printf("***Sorting Sublists!***\n");
	MergeSort(data, node_status[id], sizeof(DATA_TYPE), compfunc);
	MPI_Barrier(MPI_COMM_WORLD);
	
	/*****************************************************/
	/* Beginning merging sublists across nodes (phase 2) */
	/*****************************************************/
	
	printf("***Phase 2 Merging Begins!***\n");
	
	memcpy(merge_status, node_status, sizeof(int)*p);
	instr.completed = 0;
	i = 0;

	while(!instr.completed)
	{
		printf("***Phase 2 iteration %d***\n", i++);

		//Process 0 will only receive data from other Processes at any iteration
		instr = planMerge(merge_status, id);
		if(instr.op == RECV)
			data = recvAndMerge(data, node_status[id], instr);
		
		//Update the current node statuses
		memcpy(node_status, merge_status, sizeof(int)*p);
		
		//Wait for every node to complete this iteration 
		MPI_Barrier(MPI_COMM_WORLD);
	}
	
	//Stop recording the sorting time
	elapsed_time += MPI_Wtime();			

	//Verify the data is indeed sorted
	if(!no_verify)
		checkSorted(data, elements, compfunc);
	
	//Outputs the results if needed
	if(output_enabled)
			outputResult(data, elements);
	
	free(data);
	free(node_status);
	free(merge_status);
	return elapsed_time;
}

void slaveProcess(int id)
{
	int *node_disp;				//An array used by scatterv to know where to break the data for distribution
	int *node_status;			//An array keeping track of the data size of each node's sorted sublist at the CURRENT time
	int *merge_status;			//An array keeping track of the data size of each node's sorted sublist AFTER one iteration of phase 2 merge
	MergeInstr instr;			//A struct telling a node where to send its sorted sublist, or who to receive it from	
	
	//Data related variables for all nodes
	int elements;
	int filesize;
	int i,j, retval;
	DATA_TYPE *data, *tdata1, *tdata2; 
	
	//Other initializations
	node_status = (int*) malloc(sizeof(int)*p);
	node_disp = (int*) malloc(sizeof(int)*p);
	merge_status = (int*) malloc(sizeof(int)*p);
	
	//Wait for node 0 to broadcast the number of elements
	MPI_Bcast(&elements, 1, MPI_INT, 0, MPI_COMM_WORLD);
	
	//Calculate how much data everyone (including myself) will be receiving
	calcCountDisp(node_status, node_disp, p, elements);				
	data = (DATA_TYPE*) malloc(sizeof(DATA_TYPE) * node_status[id]);
	
	//Receive a chunk of data from master node 0
	MPI_Scatterv(NULL, node_status, node_disp, MPI_DATA_TYPE, data, node_status[id], MPI_DATA_TYPE, 0, MPI_COMM_WORLD);
	free(node_disp);
	
	//Calling merge sort to sort the received sublist. 
	MergeSort(data, node_status[id], sizeof(DATA_TYPE), compfunc);
	MPI_Barrier(MPI_COMM_WORLD);
	
	/*****************************************************/
	/* Beginning merging sublists across nodes (phase 2) */
	/*****************************************************/

	memcpy(merge_status, node_status, sizeof(int)*p);
	instr.completed = 0;
	i = 0;
	
	while(!instr.completed)
	{
		instr = planMerge(merge_status, id);
		if(instr.op == RECV)
			data = recvAndMerge(data, node_status[id], instr);
		
		//Each slave node will have to send their sorted sublist to another node at some iteration. 
		//Once send, this node will be idle for the rest of the iterations.
		else if(instr.op == SEND)
		{
			//printf("NODE: %d Iter: %d OP: SEND, Target: %d Before: %d Elements to S/R: %d After: %d Comp: %d\n", id, i++, instr.target, node_status[id], instr.elements, merge_status[id], instr.completed);
			MPI_Send(data, node_status[id], MPI_DATA_TYPE, instr.target, 0, MPI_COMM_WORLD);
			free(data);	
		}
		
		//Update the current node statuses
		memcpy(node_status, merge_status, sizeof(int)*p);
		
		//Wait for every node to complete this iteration 
		MPI_Barrier(MPI_COMM_WORLD);
	}
	
	free(node_status);
	free(merge_status);
}

int main(int argc, char *argv[]) 
{
	int id;						//Rank of the current process
	double elapsed_time;		//Records how long the calculation took
	ParsedArgs parsed_args;		//Interpreted input arguments for the main process
	
	if(argc < 2)
	{
		printf("Insufficient arguments!\n");
		printf("Usage: psort <optional args> filename\n");
		printf("Implemented optional args: %s %s\n", NO_OUTPUT_ARG, NO_VERIFY_ARG);
		return 0;
	}
	
	//Initialize MPI on all processes
	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &id);
	MPI_Comm_size(MPI_COMM_WORLD, &p);
	
	//Let rank 0 be the master process.
	if(id == 0)
	{
		//Parse the input arguments and read the data file specified
		parsed_args = parseArgs(argc, argv);
	
		//Start the Mergesort Master
		printf("Processes available: %d\n", p);
		elapsed_time = masterProcess(id, parsed_args);
		printf("Sorting took %lf seconds.\n", elapsed_time);
	}
	
	//Let all other ranks be worker processes
	else	
	{
		//Start the Mergesort Slave
		slaveProcess(id);
	}

	MPI_Finalize();
	return 0;
}