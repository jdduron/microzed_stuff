/******************************************************************************
*
* Copyright (C) 2009 - 2014 Xilinx, Inc.  All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* Use of the Software is limited solely to applications:
* (a) running on a Xilinx device, or
* (b) that interact with a Xilinx device through a bus or interconnect.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* XILINX  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
* OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
* Except as contained in this notice, the name of the Xilinx shall not be used
* in advertising or otherwise to promote the sale, use or other dealings in
* this Software without prior written authorization from Xilinx.
*
******************************************************************************/

#include <stdio.h>
#include <string.h>

#include "lwip/err.h"
#include "lwip/udp.h"
#if defined (__arm__) || defined (__aarch64__)
#include "xil_printf.h"
#endif

// Includes for DMA and GetCentroid module
#include "xgetcentroid.h"
#include "xaxidma.h"

// Include for timing
#include "xtime_l.h"

// Include our own definitions
#include "includes.h"

// DMA and GetCentroid global declarations (defined in main.c)
extern XGetcentroid GetCentroid;
extern XGetcentroid_Config *GetCentroid_cfg;
extern XAxiDma axiDMA;
extern XAxiDma_Config *axiDMA_cfg;
extern int GetCentroidReady;

// Global arrays to store results
u16			WaveformArr[WAVE_SIZE_BYTES/2];
u8			IndArr[INDARR_SIZE_BYTES];

// Global variables for data flow
extern volatile u8      IndArrDone;
extern volatile u32	EthBytesReceived;
int*			IndArrPtr;
extern volatile u8	SendResults;
extern volatile u8   	DMA_TX_Busy;
extern volatile u8	Error;

// Global Variables for Ethernet handling
extern u16_t    	RemotePort;
extern struct ip_addr  	RemoteAddr;
extern struct udp_pcb 	send_pcb;

// Function definitions
int WriteIndArr(int* pbufptr);

/* recv_callback: function that handles responding to UDP packets */
void recv_callback(void *arg, struct udp_pcb *upcb,
                              struct pbuf *p, struct ip_addr *addr, u16_t port)
{

	// Set up a timeout counter and a status variable
	int TimeOutCntr = 0;
	int status = 0;

	/* Do not read the packet if we are not in ESTABLISHED state */
	if (!p) {
		udp_disconnect(upcb);
		return;
	}

	/* Assign the Remote IP:port from the callback on each first pulse */
	RemotePort = port;
	RemoteAddr = *addr;

	/* Keep track of the control block so we can send data back in the main while loop */
	send_pcb = *upcb;

	/*************************** WRITE INDEX ARRAY  ********************************/
	if (IndArrDone == 0){

		// Determine the number of bytes received and copy this segment to the temp array
		EthBytesReceived = p->len;
		memcpy(&IndArr[0], (u32*)p->payload, EthBytesReceived);

		// Once we have all the values we need for the index array, send them to FPGA
		if (EthBytesReceived == (INDARR_SIZE_BYTES)){
			xil_printf("Copying %d bytes of the Index Array to memory\n\r", EthBytesReceived);
			// Attempt to write the index array memory on the FPGA
			IndArrPtr = (int*)(&IndArr[0]);
			status = WriteIndArr(IndArrPtr);
			if (status != 0){
				xil_printf("Error while reading/writing index array! Returning...\n\r");
				Error = 1;
			}

			// We are done writing the index array to the FPGA
			IndArrDone = 1;

			// Indicate to the user that we now have the index array loaded
			xil_printf("\n\rIndex Array has been loaded onto the Programmable Logic.\n\r");
			xil_printf("Algorithm is prepared to receive pulse data (256 unsigned shorts)\n\r\n\r");

		}

		// free the received pbuf
		pbuf_free(p);
		return;

	}

	/********************** WAVE ARRAY ********************************/
	// Determine the number of bytes received and copy this segment to the temp array
	EthBytesReceived = p->len;
	memcpy(&WaveformArr[0], (u32*)p->payload, EthBytesReceived);

	/*************************** SEND DATA TO GETCENTROID MODULE **********************************/
	// Send data to the IP core slave
	TimeOutCntr = RESET_TIMEOUT_COUNTER;
	while (DMA_TX_Busy == 1){
	  TimeOutCntr--;
	  if (TimeOutCntr == 0){
		  xil_printf("Error in waiting for DMA\n\r");
		  Error = 1;
		  break;
	  }
	}

	// Wait until the HLS module is ready to receive more data (ap_ready goes high)
	TimeOutCntr = RESET_TIMEOUT_COUNTER;
	while (GetCentroidReady == 0){
	  TimeOutCntr--;
	  if (TimeOutCntr == 0){
		  xil_printf("Error in waiting for GetCentroid\n\r");
		  Error = 1;
		  break;
	  }
	}

	// DMA Should be ready, so send transfer and reset wait flags
	GetCentroidReady = 0;
	DMA_TX_Busy = 1;
	Xil_DCacheFlushRange((u32)WaveformArr, WAVE_SIZE_BYTES);
	status = XAxiDma_SimpleTransfer(&axiDMA, (u32)&WaveformArr[0], WAVE_SIZE_BYTES,\
			                        XAXIDMA_DMA_TO_DEVICE);
	if (status != XST_SUCCESS){
	  xil_printf("Error with DMA transfer to Device\n\r");
	  Error = 1;
	}

	/* free the received pbuf */
	pbuf_free(p);
	return;

}

/* start_application: function to set up UDP listener */
int start_application()
{

	/* Declare some structures and variables */
	err_t err;
	struct udp_pcb* pcb;
	unsigned port = FF_UDP_PORT;

	/* Create new udp PCB structure */
	pcb = udp_new();
	if (!pcb) {
		xil_printf("Error creating PCB. Out of Memory\n\r");
		return -1;
	}

	/* Bind to specified @port */
	err = udp_bind(pcb, IP_ADDR_ANY, port);
	if (err != ERR_OK) {
		xil_printf("Unable to bind to port %d: err = %d\n\r", port, err);
		return -2;
	}

	/* set the receive callback for this connection */
	udp_recv(pcb, recv_callback, NULL);

	/* Print out information about the connection */
	xil_printf("udp echo server started @ port %d\n\r", port);

    /* Print out a request for an index array */
    xil_printf("\nAlgorithm is prepared to receive index array (256 32-bit fixed point values)\n\r\n\r");

	/* Return success */
	return 0;

}

/* Read in the index array and write it to the FPGA */
int WriteIndArr(int* pbufptr){

	// Declare a test variable
	int ww1 = 0;
	int ww2 = 0;

	// Declare some pointers to populate the templates and matrices
	int *pbufptr1 = pbufptr;
	int *pbufptr2 = pbufptr + 1;

	// Fill in the cyclically partitioned index array
	for (int i=0; i<WAVE_SIZE_BYTES/4; i++){
		ww1 = XGetcentroid_Write_IndArr_0_V_Words(&GetCentroid, i, pbufptr1, 1);
		ww2 = XGetcentroid_Write_IndArr_1_V_Words(&GetCentroid, i, pbufptr2, 1);
		if ((ww1 != 1) || (ww2 !=1)) return -1;
		pbufptr1 += 2; pbufptr2 += 2;
	}

#ifdef READ_BACK_INDEX_ARRAY
	// Declare some temporary holder arrays
	int dummy1[WAVE_SIZE_BYTES/2];
	int dummy2[WAVE_SIZE_BYTES/2];

	// Read the index array back
	ww1 = XGetcentroid_Read_IndArr_0_V_Words(&GetCentroid, 0, &dummy1[0], WAVE_SIZE_BYTES/4);
	ww2 = XGetcentroid_Read_IndArr_1_V_Words(&GetCentroid, 0, &dummy2[0], WAVE_SIZE_BYTES/4);
	if ((ww1 != WAVE_SIZE_BYTES/4) || (ww2 != WAVE_SIZE_BYTES/4)) return -1;
	for (int i=0; i<WAVE_SIZE_BYTES/4; ++i){
		printf("Index Array %d = %16.8f\n", NUMCHANNELS*i,   ((float)dummy1[i])/BITDIV);
		printf("Index Array %d = %16.8f\n", NUMCHANNELS*i+1, ((float)dummy2[i])/BITDIV);
    }
#endif

	// Return a success
	return 0;

}
