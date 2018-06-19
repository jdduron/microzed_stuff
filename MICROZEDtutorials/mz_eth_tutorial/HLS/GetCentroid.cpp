// NAME:
//   GetCentroid.cpp
//
// PURPOSE:
//   This code is intended to be a simple example of how to create an
//   algorithm in Vivado HLS that uses:
//     1) Pipelining
//     2) Loop Unrolling
//     3) Array Partitioning
//     4) HLS Streams
//     5) Fixed Point Floating Numbers
//
//   The algorithm contained within calculates the centroid of a 256
//   element waveform with the weighting contained in IndArr.
//
// INPUTS:
//   inStream: unsigned-int 32-bit AXI-Stream
//     This is the incoming array of:
//       256 unsigned shorts - the digitized waveform. Note
//         they are packed in 32-bit words.
//     The entire length of the input stream is thus 512 bytes
//   IndArr[] : fp_data_t array of WAVESIZE
//     A pointer to the array that will hold the index array/weighting values
//   Centroid: fp_data_t
//     A pointer to the variable that will hold the centroid of the waveform
//
// OUTPUTS:
//   NONE
//
// INCLUDES:
#include "includes.h"

void GetCentroid(hls::stream<uintSdChIn> &inStream, fp_data_t IndArr[WAVESIZE], fp_data_t *Centroid)
{

#pragma HLS INTERFACE axis port=inStream
#pragma HLS INTERFACE s_axilite port=return   bundle=CTRL_BUS
#pragma HLS INTERFACE s_axilite port=IndArr   bundle=CTRL_BUS
DO_PRAGMA(HLS array_partition variable=IndArr cyclic factor=NUMCHANNELS)
#pragma HLS INTERFACE s_axilite port=Centroid bundle=CTRL_BUS

	// Variables used for unpacking the data
	uintSdChIn 	valIn;
	uintSdVal 	value 	= 0;
	ap_uint<16> dataIn	= 0;

	// Variables for calculations
	ap_uint<32> WaveformSum				= 0;
	ap_uint<32> WeightedWaveformSum			= 0;
	ap_uint<32> internal_WaveformSum		= 0;
	ap_uint<32> internal_WeightedWaveformSum	= 0;

	// Set up place-holders and accumulators for the dot product operation
	*Centroid = 0.0;

	// Create loop indices, including one to make the code less cumbersome
	int i  = 0;
	int j  = 0;
	int li = 0;

	/*************************************************************/
	/*            FIRST LOOP: CALCULATE BASELINE                 */
	/*************************************************************/
	// Zero out the tallies
	WaveformSum		= 0;
	WeightedWaveformSum	= 0;

	// We need to indicate a size of this stream
	for (i=0; i < (WAVESIZE/NUMCHANNELS); i++){
#pragma HLS PIPELINE

		// Read the entire 32-bit value and cache (Block here if FIFO sender is empty)
		valIn = inStream.read();
		value = valIn.data;

		// Zero out the internal baseline total
		internal_WaveformSum		= 0;
		internal_WeightedWaveformSum	= 0;

		// Now go through a loop to extract the shorts from the 32-bit element and do the math
		// This loop will automatically be unrolled since it is inside a pipelined loop
		for (j=0; j < NUMCHANNELS; j++){

			// Get the loop index
			li = NUMCHANNELS*i+j;

			// Extract the relevant 16 bits to get the short
			dataIn = value.range((j+1)*16 - 1, j*16);

			// Add to the sum keeping track of the total energy
			internal_WaveformSum += dataIn;

			// Add to the sum keeping track of the weighted energy
			internal_WeightedWaveformSum += dataIn * IndArr[li];

		}

		// Add this to the total baseline sum
		WaveformSum += internal_WaveformSum;
		WeightedWaveformSum += internal_WeightedWaveformSum;

	}

	// Now calculate the centroid
	*Centroid = ((fp_data_t)WeightedWaveformSum)/WaveformSum;

}
