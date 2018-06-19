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

/******************************************************************************
*
* GetCentroid
*   main.c
*
* NOTES:
*   In order to disable DHCP and TCP (we are only interested in UDP packets
*   for this application, we make the following changes
*   1) Change LWIP_TCP to 0
*   2) Change LWIP_DHCP to 0
*	3) Modify platform_zynq.c by taking out all DHCP and TCP timer calls.
*	4) In Properties -> C/C++ Build -> Settings -> Tool Settings ->
*	   ARM v7 gcc compiler -> Optimization, set the Optimization level
*	   to -02 to try to achieve maximum UDP throughput.
*	5) In order to make the AXI DMA interrupt work properly with the
*	   GetCentroid interrupt, set the interrupt priority of the latter higher than
*	   the former (0x90 instead of 0xA0, as in all the examples)
*	6) It actually turns out to be slower to use an IF statement in the
*	   Ethernet receive callback to check whether or not we should assign
*	   the IP address to a global variable than it is to just assign the
*	   IP address to a global variable every time
*
******************************************************************************/

//DEFINE STATEMENTS TO INCREASE SPEED
#undef LWIP_TCP
#undef LWIP_DHCP
#undef CHECKSUM_CHECK_UDP
#undef LWIP_CHECKSUM_ON_COPY
#undef CHECKSUM_GEN_UDP

//INCLUDES
#include <stdio.h>
#include "xtime_l.h"

#include "xparameters.h"

#include "netif/xadapter.h"

#include "platform.h"
#include "platform_config.h"
#if defined (__arm__) || defined(__aarch64__)
#include "xil_printf.h"
#endif

#include "lwip/udp.h"
#include "xil_cache.h"

// Includes for DMA and GetCentroid module
#include "xgetcentroid.h"
#include "xaxidma.h"

// Includes for interrupt controller
#include "xscugic.h"
#include "xil_exception.h"

// Include our own definitions
#include "includes.h"

/* Defined by each RAW mode application */
void print_app_header();
int start_application();

/* Missing declaration in lwIP */
void lwip_init();

/* set up netif stuctures */
static struct netif server_netif;
struct netif *echo_netif;

// IP Config Pointers and Handlers
XGetcentroid GetCentroid;
XGetcentroid_Config *GetCentroid_cfg;
XAxiDma axiDMA;
XAxiDma_Config *axiDMA_cfg;

// Interrupt Controller define statements (see xparameters.h)
#define GETCENTROID_INTR_ID		XPAR_FABRIC_GETCENTROID_0_INTERRUPT_INTR
#define TX_INTR_ID			XPAR_FABRIC_AXI_DMA_0_MM2S_INTROUT_INTR
#define INTC_DEVICE_ID 			XPAR_SCUGIC_SINGLE_DEVICE_ID
#define INTC_HANDLER			XScuGic_InterruptHandler
#define INTC				XScuGic
static  INTC Intc;
int	GetCentroidReady;

// Global Variables to store results and handle data flow
int			Centroid;

// Global variables for data flow
volatile u8		IndArrDone;
volatile u32		EthBytesReceived;
volatile u8		SendResults;
volatile u8   		DMA_TX_Busy;
volatile u8		Error;

// Global Variables for Ethernet handling
u16_t    		RemotePort;
struct ip_addr  	RemoteAddr;
struct udp_pcb 		send_pcb;

// Other function prototypes
static void GetCentroidIntrHandler(void *Callback);
static int SetupIntrSystem(INTC * IntcInstancePtr,
		XAxiDma * AxiDmaPtr, u16 TxIntrId,
		XGetcentroid * GetCentroidPtr, u16 GetCentroidIntrId);
static void DisableIntrSystem(INTC * IntcInstancePtr, u16 GetCentroidIntrId,
		XGetcentroid * GetCentroidPtr, u16 TxIntrId);

// initPeripherals() - set up the DMA and GetCentroid cores
void initPeripherals(){

	// Initialize GetCentroid core
	printf("Initializing GetCentroid\n\r");
	GetCentroid_cfg = XGetcentroid_LookupConfig(XPAR_GETCENTROID_0_DEVICE_ID);
	if (GetCentroid_cfg){
		int status = XGetcentroid_CfgInitialize(&GetCentroid, GetCentroid_cfg);
		if (status != XST_SUCCESS){
			printf("Error initializing GetCentroid core\n\r");
		}
	}

	// Initialize AxiDMA core
	printf("Initializing AxiDMA\n\r");
	axiDMA_cfg = XAxiDma_LookupConfig(XPAR_AXIDMA_0_DEVICE_ID);
	if (axiDMA_cfg){
		int status = XAxiDma_CfgInitialize(&axiDMA, axiDMA_cfg);
		if (status != XST_SUCCESS){
			printf("Error intializing AxiDMA core\n\r");
		}
	}

	// Disable Interrupts for DMA and GetCentroid before enabling things
	XAxiDma_IntrDisable(&axiDMA, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
	XGetcentroid_InterruptGlobalDisable(&GetCentroid);
	XGetcentroid_InterruptDisable(&GetCentroid, 0x00000003);

	/* Set up Interrupt system  */
	int status = SetupIntrSystem(&Intc, &axiDMA, XPAR_FABRIC_AXI_DMA_0_MM2S_INTROUT_INTR,
			&GetCentroid, GETCENTROID_INTR_ID);
	if (status != XST_SUCCESS) {
		xil_printf("Failed Interrupt Setup for GetCentroid!\r\n");
	}

	/* Enable all interrupts from AXI DMA */
	XAxiDma_IntrEnable(&axiDMA, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);

	// Enable interrupts from GetCentroid
	XGetcentroid_InterruptEnable(&GetCentroid, 0x00000003);
	XGetcentroid_InterruptGlobalEnable(&GetCentroid);

	// Setup and start the GetCentroid module (enable auto-restart to keep data flowing)
    	XGetcentroid_EnableAutoRestart(&GetCentroid);
	XGetcentroid_Start(&GetCentroid);

	// Declare GetCentroid ready to go
	GetCentroidReady = 1;

	// Declare DMA not busy and ready to accept streams
	DMA_TX_Busy = 0;

	// Set the ResultPtr to the right place and set SendResults flag to 0
	SendResults = 0;

	// Set the counters and booleans to 0
	EthBytesReceived = 0;
	IndArrDone	 = 0;

	// Initialize the return value
	Centroid = 0;

}

/* print_ip: function to print out ip address */
void print_ip(char *msg, struct ip_addr *ip)
{
	print(msg);
	xil_printf("%d.%d.%d.%d\n\r", ip4_addr1(ip), ip4_addr2(ip),
			ip4_addr3(ip), ip4_addr4(ip));
}

/* print_ip_settings: function to print out the ip settings */
void print_ip_settings(struct ip_addr *ip, struct ip_addr *mask, struct ip_addr *gw)
{
	print_ip("Board IP: ", ip);
	print_ip("Netmask : ", mask);
	print_ip("Gateway : ", gw);
}

/* print_app_header: function to print a header at start time */
void print_app_header()
{
	xil_printf("\n\r\n\r------lwIP UDP GetCentroid Application------\n\r");
	xil_printf("UDP packets sent to port 7 will be processed\n\r");
}


/* main */
int main()
{

	/* Allocate structures for the ip address, netmask, and gateway */
	struct ip_addr ipaddr, netmask, gw;
	struct pbuf * psnd;
	err_t udpsenderr;
	int status = 0;

	/* The mac address of the board. this should be unique per board */
	unsigned char mac_ethernet_address[] =
	{ 0x00, 0x0a, 0x35, 0x00, 0x01, 0x02 };

	/* Use the same structure for the server and the echo server */
	echo_netif = &server_netif;

	/* Initialize the platform */
	init_platform();

	// Initialize the DMA and GetCentroid peripherals
	initPeripherals();

	/* initialize IP addresses to be used */
	IP4_ADDR(&ipaddr,  192, 168,   1, 10);
	IP4_ADDR(&netmask, 255, 255, 255,  0);
	IP4_ADDR(&gw,      192, 168,   1,  1);

	/* Print out the simple application header */
	print_app_header();

	/* Initialize the lwip for UDP */
	lwip_init();

  	/* Add network interface to the netif_list, and set it as default */
	if (!xemac_add(echo_netif, &ipaddr, &netmask,
			&gw, mac_ethernet_address,
			PLATFORM_EMAC_BASEADDR)) {
		xil_printf("Error adding N/W interface\n\r");
		return -1;
	}
	netif_set_default(echo_netif);

	/* Now enable interrupts */
	platform_enable_interrupts();

	/* Specify that the network if is up */
	netif_set_up(echo_netif);

	/* Print out the ip settings */
	print_ip_settings(&ipaddr, &netmask, &gw);

	/* Start the application (web server, rxtest, txtest, etc..) */
	status = start_application();
	if (status != 0){
		xil_printf("Error in start_application() with code: %d\n\r", status);
		goto ErrorOrDone;
	}

	/* Receive and process packets */
	while (Error==0) {

		/* Receive packets */
		xemacif_input(echo_netif);

		/* Send results back from time to time */
		if (SendResults == 1){

			// Read the results from the FPGA
			Centroid = XGetcentroid_Get_Centroid_V(&GetCentroid);

			// Send out the centroid result over UDP
			psnd = pbuf_alloc(PBUF_TRANSPORT, sizeof(int), PBUF_REF);
			psnd->payload = &Centroid;
			udpsenderr = udp_sendto(&send_pcb, psnd, &RemoteAddr, RemotePort);
			if (udpsenderr != ERR_OK){
				xil_printf("UDP Send failed with Error %d\n\r", udpsenderr);
				goto ErrorOrDone;
			}
			pbuf_free(psnd);

            // Set the boolean back to zero
            SendResults = 0;

		}

	}

	// Jump point for failure
	ErrorOrDone:
	xil_printf("Catastrophic Error! Shutting down and exiting...\n\r");

	// Disable the GetCentroid interrupts and disconnect from the GIC
	DisableIntrSystem(&Intc, GETCENTROID_INTR_ID, &GetCentroid, TX_INTR_ID);

	/* never reached */
	cleanup_platform();

	return 0;
}

/*****************************************************************************/
/*
*
* This is the DMA TX Interrupt handler function.
*
* It gets the interrupt status from the hardware, acknowledges it, and if any
* error happens, it resets the hardware. Otherwise, if a completion interrupt
* is present, then sets the TxDone.flag
*
* @param	Callback is a pointer to TX channel of the DMA engine.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void TxIntrHandler(void *Callback)
{

	u32 IrqStatus;
	int TimeOut;
	XAxiDma *AxiDmaInst = (XAxiDma *)Callback;

	/* Read pending interrupts */
	IrqStatus = XAxiDma_IntrGetIrq(AxiDmaInst, XAXIDMA_DMA_TO_DEVICE);

	/* Acknowledge pending interrupts */
	XAxiDma_IntrAckIrq(AxiDmaInst, IrqStatus, XAXIDMA_DMA_TO_DEVICE);

	/* If no interrupt is asserted, we do not do anything */
	if (!(IrqStatus & XAXIDMA_IRQ_ALL_MASK)) {
		xil_printf("No interrupt in AXI DMA, but stopped anyway!\n");
		return;
	}

	/*
	 * If error interrupt is asserted, raise error flag, reset the
	 * hardware to recover from the error, and return with no further
	 * processing.
	 */
	if ((IrqStatus & XAXIDMA_IRQ_ERROR_MASK)) {
		Error = 1;
		xil_printf("Error with AXI DMA!\n");
		/*
		 * Reset should never fail for transmit channel
		 */
		XAxiDma_Reset(AxiDmaInst);
		TimeOut = RESET_TIMEOUT_COUNTER;
		while (TimeOut) {
			if (XAxiDma_ResetIsDone(AxiDmaInst)) {
				break;
			}
			TimeOut -= 1;
		}
		return;
	}

	/*
	 * If Completion interrupt is asserted, then set the TxDone flag
	 */
	if ((IrqStatus & XAXIDMA_IRQ_IOC_MASK)) {
		DMA_TX_Busy = 0;
	}
}

/*****************************************************************************/
/*
*
* This is the GetCentroid interrupt handler function
*
* It gets the interrupt status from the hardware, acknowledges it, and if any
* error happens, it resets the hardware.
*
* If this is an ap_done interrupt, we set SendResults=1 to indicate a value
* is ready to be sent.
*
* If this is an ap_ready interrupt, we set GetCentroidReady to indicate that
* it is ready for a new calculation.
*
* @param	Callback is a pointer to the instance of the GetCentroid object
*
* @return	None.
*
* @note		None.
*
******************************************************************************/

static void GetCentroidIntrHandler(void *Callback)
{
	u32 IrqStatus;
	XGetcentroid *XGetcentroidInst = (XGetcentroid *)Callback;

	/* Read pending interrupts */
	IrqStatus = XGetcentroid_InterruptGetStatus(XGetcentroidInst);

	// If it is an ap_done interrupt, we have values to read
	if (IrqStatus & 0x00000001){

		// Indicate that we've filled the arrays and are ready to send results
		SendResults = 1;

		// Clear the ap_done interrupt
		XGetcentroid_InterruptClear(XGetcentroidInst, 0x00000001);
	}

	// If it is an ap_ready interrupt, the algorithm is ready for new input data
	if (IrqStatus & 0x00000002){;
		// Indicate we are ready for more data in the algorithm
		GetCentroidReady = 1;
		// Clear the ap_ready interrupt
		XGetcentroid_InterruptClear(XGetcentroidInst, 0x00000002);
	}

}

/*****************************************************************************/
/*
*
* This function sets up the interrupt system so interrupts can occur for the
* GetCentroid custom IP module.
*
* @param	IntcInstancePtr is a pointer to the instance of the INTC.
* @param	AxiDmaPtr is a pointer to the instance of the DMA engine
* @param	TxIntrId is the TX channel Interrupt ID.
* @param	GetCentroidPtr is a pointer to the instance of the DMA engine
* @param	GetCentroidIntrId is the GetCentroid Device Interrupt ID.
*
* @return
*		- XST_SUCCESS if successful,
*		- XST_FAILURE.if not succesful
*
* @note		None.
*
******************************************************************************/
static int SetupIntrSystem(INTC * IntcInstancePtr,
		XAxiDma * AxiDmaPtr, u16 TxIntrId,
		XGetcentroid * GetCentroidPtr, u16 GetCentroidIntrId)
{

	// Declare variables for storage
	int Status;

	// Generic Interrupt Controller Config and Init
	XScuGic_Config *IntcConfig;
	Xil_ExceptionInit();

	/* Enable interrupts from the hardware */
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
			(Xil_ExceptionHandler)INTC_HANDLER,
			(void *)IntcInstancePtr);

	Xil_ExceptionEnable();

	/*
	 * Initialize the interrupt controller driver so that it is ready to
	 * use (look it up by its device ID, which is given in xparameters_ps.h
	 * as #define XPAR_SCUGIC_SINGLE_DEVICE_ID 0U.
	 */
	xil_printf("Initializing Interrupts\n\r");
	IntcConfig = XScuGic_LookupConfig(INTC_DEVICE_ID);
	if (NULL == IntcConfig) {
		return XST_FAILURE;
	}

	Status = XScuGic_CfgInitialize(IntcInstancePtr, IntcConfig,
					IntcConfig->CpuBaseAddress);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/*
	 * Set priority for the TX DMA and GetCentroid Interrupts
	 */
	XScuGic_SetPriorityTriggerType(IntcInstancePtr, GetCentroidIntrId, 0x90, 0x3);
	XScuGic_SetPriorityTriggerType(IntcInstancePtr, TxIntrId, 0xA0, 0x3);

	/*
	 * AXI DMA - TX INTERRUPT ON COMPLETE
	 * Connect the device driver handler that will be called when an
	 * interrupt for the device occurs, the handler defined above performs
	 * the specific interrupt processing for the device.
	 */
	Status = XScuGic_Connect(IntcInstancePtr, TxIntrId,
				(Xil_InterruptHandler)TxIntrHandler,
				AxiDmaPtr);
	if (Status != XST_SUCCESS) {
		return Status;
	}

	/*
	 * GetCentroid - INTERRUPT ON READY AND DONE
	 * Connect the device driver handler that will be called when an
	 * interrupt for the device occurs, the handler defined above performs
	 * the specific interrupt processing for the device.
	 */
	Status = XScuGic_Connect(IntcInstancePtr, GetCentroidIntrId,
				(Xil_InterruptHandler)GetCentroidIntrHandler,
				GetCentroidPtr);
	if (Status != XST_SUCCESS) {
		return Status;
	}

	/*
	 * Enable the interrupts
	 */
	XScuGic_Enable(IntcInstancePtr, TxIntrId);
	XScuGic_Enable(IntcInstancePtr, GetCentroidIntrId);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* This function disables the interrupts for DMA engine.
*
* @param	IntcInstancePtr is the pointer to the INTC component instance
* @param	GetCentroidIntrId is interrupt ID associated GetCentroid
* @param	GetCentroidPtr is a pointer to the GetCentroid object
* @param	TxIntrId is interrupt ID associated with AXI DMA TX
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void DisableIntrSystem(INTC * IntcInstancePtr, u16 GetCentroidIntrId,
		XGetcentroid * GetCentroidPtr, u16 TxIntrId)
{

	// Disable interrupts from GetCentroid first of all
	XGetcentroid_InterruptGlobalDisable(GetCentroidPtr);
	XGetcentroid_InterruptEnable(GetCentroidPtr, 0x1);

	// Now disconnect the interrupt systems from the GIC
	XScuGic_Disconnect(IntcInstancePtr, TxIntrId);
	XScuGic_Disconnect(IntcInstancePtr, GetCentroidIntrId);

}
