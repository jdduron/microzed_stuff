// NAME:
//   includes.h
//
// PURPOSE:
//   This file contains parameters, constants,
//
// *********************** PARAMETERS **********************************************************/
#ifndef INCLUDES_H_INCLUDED
#define INCLUDES_H_INCLUDED

// Include Files
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <ap_fixed.h>
#include <ap_int.h>
#include <hls_stream.h>
#include <ap_axi_sdata.h>

// ************************ RUNTIME SETTINGS ***************************************************/
#define VERBOSE
//#define PRINT_ARRAYS

// *********************** DECLARE PARAMETERS **************************************************/
// Define the pulse size in terms of the number of unsigned-ints
#define WAVESIZE		256	// Number of 16-bit samples in the waveform

//HARDWARE DEFINES
#define NUMCHANNELS 	        2	// Number of parallel operations done on input stream (1 OR 2)
#define BW   			32	// Total number of bits in fixed point data type
#define IW    			24	// Number of bits left of decimal point in fixed point data type

// *********************** DECLARE TYPES *******************************************************/
// Declare 16, 32 or unsigned integer with minimum side-channel (Includes TLAST signal)
#if NUMCHANNELS == 1
  typedef ap_axiu<16,1,1,1> uintSdChIn;
  typedef ap_uint<16> uintSdVal;
#elif NUMCHANNELS == 2
  typedef ap_axiu<32,1,1,1> uintSdChIn;
  typedef ap_uint<32> uintSdVal;
#endif

// Declare fixed point data type
typedef ap_fixed<BW,IW>    fp_data_t;

// ************************************ MACROS *************************************************/
#define PRAGMA_SUB(x) _Pragma (#x)
#define DO_PRAGMA(x) PRAGMA_SUB(x)

// FILENAMES (the file name for the toy array to demonstrate reading in files in HLS)
#define MAXFILENAMESIZE 100
#define INDFILENAME	"IndexFile.txt" 	// Input - Simple Indices

// *********************** FUNCTION DECLARATIONS ***********************************************/
void GetCentroid(hls::stream<uintSdChIn> &inStream, fp_data_t IndArr[WAVESIZE], fp_data_t *Centroid);

#endif
