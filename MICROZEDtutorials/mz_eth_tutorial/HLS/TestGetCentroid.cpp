// NAME:
//   TestGetCentroid.cpp
//
// PURPOSE:
//   This is the test bench file used to test the GetCentroid algorithm.
//
// INCLUDES:
#include "includes.h"

using namespace std;

// Global arrays that we will pass to the functions
fp_data_t IndArrIn[WAVESIZE];
float     IndArrInTmp[WAVESIZE];
unsigned short WaveData[WAVESIZE];
fp_data_t CentroidOut;

int main()
{

	// Define a stream for the input data to the GetCentroid algorithm
	hls::stream<uintSdChIn> inputStream;

	// Index variable
	int i = 0;

	// Variable used to pack data
	uintSdChIn valIn;

	// Values used to read files
	float* dataptr;				// Pointer for index array
	char filename[MAXFILENAMESIZE];		//the string to hold the filename
	FILE *fp;				//the file pointer for each file

	// *********************************************************************************
	// 1) OBTAIN THE INDICES FROM THE INDEX FILE
	sprintf(filename, INDFILENAME);

	// Open the file
	fp = fopen(filename, "r");
	if (fp == NULL){
		printf("Could not open file %s. Exiting...\n", filename);
		return -1;
	}

	// Get the number of exposures from the command line
	printf("Reading %d lines from file : %s\n\r", WAVESIZE, filename);

	// Assign a pointer to count and return the pointer to the calling function
	dataptr  = &IndArrInTmp[0];

	// Go through and get the indices
	for (i=0; i < WAVESIZE; ++i){
		fscanf(fp, "%f\n", dataptr);
		dataptr++;
	}

#ifdef PRINT_ARRAYS
	for (i=0; i < WAVESIZE; i++){
		printf("Index %d : %12.7f\n", i, IndArrInTmp[i]);
	}
	printf("\n\n");
#endif

	// Close the file
	fclose(fp);

	// Convert to fixed point
	for (i = 0; i < WAVESIZE; ++i){
		IndArrIn[i] = (fp_data_t)IndArrInTmp[i];
	}

	// *********************************************************************************
	// 2) CREATE A FAKE INPUT STREAM
	for (i=0; i < WAVESIZE; ++i){
		WaveData[i] = i;
	}

	// Populate input stream
	for (i=0; i < (WAVESIZE/NUMCHANNELS); i++){

		// Create a pointer to a 32 bit unsigned integer and point that to the short array
		uintSdVal *value;
		value = (uintSdVal*)(&WaveData[NUMCHANNELS*i]);

		// Now read this into the stream
		valIn.data = *value;
		inputStream << valIn;

	}

	// The chosen pulse data is packed into inputStream, so call the GetCentroid function
	GetCentroid(inputStream, IndArrIn, &CentroidOut);

#ifdef VERBOSE
	// Print out the pulse number
	cout.precision(17);
	cout << endl;
	cout << "----------------------------------------------" << endl;
	cout << "Centroid : --------- : " << CentroidOut << endl;
	cout << endl;
#endif

	// Return a success (!!!MUST HAVE FOR HLS COSIMULATION TO REPORT SUCCESS!!!)
	return 0;

}
