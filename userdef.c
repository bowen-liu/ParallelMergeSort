#include "userdef.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
USER SPECIFIED FUNCTION userInit
*/
void userInit()
{
	return;
}

/*
USER SPECIFIED FUNCTION compFunc
*/
int compFunc(DATA_TYPE *a, DATA_TYPE *b)
{
	DATA_TYPE deref_a = *a;
	DATA_TYPE deref_b = *b;
	
	if(deref_a < deref_b) 
		return -1;
	else if(deref_a > deref_b) 
		return 1;
	else 
		return 0;
}

/*
USER SPECIFIED FUNCTION readDataFromFile
*/
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
USER SPECIFIED FUNCTION writeDataToFile
*/
void writeDataToFile(DATA_TYPE *data, int n, char *filename)
{
	int i;
	FILE *outfile;
	
	outfile = fopen(filename, "w");
	if(outfile == NULL)
	{
		perror("writeDataToFile: Failed to create/open output file");
		return;
	}
	
	printf("Outputting %d sorted elements to file %s\n", n, filename);
	for(i=0; i<n; i++) 
		fprintf(outfile, "%f\n", data[i]);
	
	fclose(outfile);
}