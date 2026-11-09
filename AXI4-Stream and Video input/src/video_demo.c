/************************************************************************/
/*																		*/
/*	video_demo.c	--	ZYBO Video demonstration 						*/
/*																		*/
/************************************************************************/
/*	Author: Sam Bobrowicz												*/
/*	Copyright 2015, Digilent Inc.										*/
/************************************************************************/
/*  Module Description: 												*/
/*																		*/
/*		This file contains code for running a demonstration of the		*/
/*		Video input and output capabilities on the ZYBO. It is a good	*/
/*		example of how to properly use the display_ctrl and				*/
/*		video_capture drivers.											*/
/*																		*/
/*																		*/
/************************************************************************/
/*  Revision History:													*/
/* 																		*/
/*		11/25/2015(SamB): Created										*/
/*																		*/
/************************************************************************/

/* ------------------------------------------------------------ */
/*				Include File Definitions						*/
/* ------------------------------------------------------------ */

#include "video_demo.h"
#include "video_capture/video_capture.h"
#include "display_ctrl/display_ctrl.h"
#include "intc/intc.h"
#include <stdio.h>
#include "xuartps.h"
#include "math.h"
#include <ctype.h>
#include <stdlib.h>
#include "xil_types.h"
#include "xil_cache.h"
#include "timer_ps/timer_ps.h"
#include "xparameters.h"
#include "xtime_l.h"
#include "xil_printf.h"
#include "xil_cache.h"
#include <arm_neon.h>
/*
 * XPAR redefines
 */
#define DYNCLK_BASEADDR 		XPAR_AXI_DYNCLK_0_S_AXI_LITE_BASEADDR
#define VDMA_ID 				XPAR_AXIVDMA_0_DEVICE_ID
#define HDMI_OUT_VTC_ID 		XPAR_V_TC_OUT_DEVICE_ID
#define HDMI_IN_VTC_ID 			XPAR_V_TC_IN_DEVICE_ID
#define HDMI_IN_GPIO_ID 		XPAR_AXI_GPIO_VIDEO_DEVICE_ID
#define HDMI_IN_VTC_IRPT_ID 	XPAR_FABRIC_V_TC_IN_IRQ_INTR
#define HDMI_IN_GPIO_IRPT_ID 	XPAR_FABRIC_AXI_GPIO_VIDEO_IP2INTC_IRPT_INTR
#define SCU_TIMER_ID 			XPAR_SCUTIMER_DEVICE_ID
#define UART_BASEADDR 			XPAR_PS7_UART_1_BASEADDR

/* ------------------------------------------------------------ */
/*				Global Variables								*/
/* ------------------------------------------------------------ */

/*
 * Display and Video Driver structs
 */
DisplayCtrl dispCtrl;
XAxiVdma vdma;
VideoCapture videoCapt;//no need for volatile because the videoCapt's state is either checked just one time
					   //in functions VideoStart and VideoStop, or (in the DemoGetInactiveFrame function) the worst case scenario is
                       //losing the frame with the inverted colors of the frame gotten from the disconnected input
INTC intc;
char fRefresh; //flag used to trigger a refresh of the Menu on video detect

/*
 * Framebuffers for video data
 */
u8 frameBuf[DISPLAY_NUM_FRAMES][DEMO_MAX_FRAME] __attribute__((aligned(0x20)));
u8 *pFrames[DISPLAY_NUM_FRAMES]; //array of pointers to the frame buffers

/*
 * Interrupt vector table
 */
const ivt_t ivt[] = {
	videoGpioIvt(HDMI_IN_GPIO_IRPT_ID, &videoCapt),
	videoVtcIvt(HDMI_IN_VTC_IRPT_ID, &(videoCapt.vtc))
};
#define MIN(a,b) (((a)<(b))?(a):(b))
/* ------------------------------------------------------------ */
/*				Procedure Definitions							*/
/* ------------------------------------------------------------ */

int main(void)
{
	DemoInitialize();

	DemoRun();

	return 0;
}


void BenchmarkSobel(
    u8 *srcFrame,
    u8 *destFrame,
    u32 width,
    u32 height,
    u32 stride
) {
    XTime start, end;

    XTime_GetTime(&start);

    DemoSobelFilter(
        srcFrame, destFrame,
        width, height, stride
    );

    XTime_GetTime(&end);

    xil_printf("Normal C cycles: %llu\r\n",
               (unsigned long long)(end - start));

    xil_printf("Press any key to continue...\r\n");

    while (!XUartPs_IsReceiveData(UART_BASEADDR))
    {
    }

    XUartPs_ReadReg(UART_BASEADDR, XUARTPS_FIFO_OFFSET);
}

void BenchmarkSobelNEON(
    u8 *srcFrame,
    u8 *destFrame,
    u32 width,
    u32 height,
    u32 stride
) {
    XTime start, end;

    XTime_GetTime(&start);

    DemoSobelFilterNEON(
        srcFrame,
        destFrame,
        width,
        height,
        stride
    );

    XTime_GetTime(&end);

    xil_printf("NEON cycles: %llu\r\n",
               (unsigned long long)(end - start));

    xil_printf("Press any key to continue...\r\n");

    while (!XUartPs_IsReceiveData(UART_BASEADDR))
    {
    }

    XUartPs_ReadReg(UART_BASEADDR, XUARTPS_FIFO_OFFSET);
}

void BenchmarkSobelAccelerate(
    u8 *srcFrame,
    u8 *destFrame,
    u32 width,
    u32 height,
    u32 stride
) {
    XTime start, end;

    XTime_GetTime(&start);

    DemoSobelFilter(
        srcFrame, destFrame,
        width, height, stride
    );

    XTime_GetTime(&end);

    xil_printf("Accelerate cycles: %llu\r\n",
               (unsigned long long)(end - start));

    xil_printf("Press any key to continue...\r\n");

    while (!XUartPs_IsReceiveData(UART_BASEADDR))
    {
    }

    XUartPs_ReadReg(UART_BASEADDR, XUARTPS_FIFO_OFFSET);
}

void DemoInitialize()
{
	int Status;
	XAxiVdma_Config *vdmaConfig;
	int i;

	/*
	 * Initialize an array of pointers to the 3 frame buffers
	 */
	for (i = 0; i < DISPLAY_NUM_FRAMES; i++)
	{
		pFrames[i] = frameBuf[i];
	}

	/*
	 * Initialize a timer used for a simple delay
	 */
	TimerInitialize(SCU_TIMER_ID);

	/*
	 * Initialize VDMA driver
	 */
	vdmaConfig = XAxiVdma_LookupConfig(VDMA_ID);
	if (!vdmaConfig)
	{
		xil_printf("No video DMA found for ID %d\r\n", VDMA_ID);
		return;
	}
	Status = XAxiVdma_CfgInitialize(&vdma, vdmaConfig, vdmaConfig->BaseAddress);
	if (Status != XST_SUCCESS)
	{
		xil_printf("VDMA Configuration Initialization failed %d\r\n", Status);
		return;
	}

	/*
	 * Initialize the Display controller and start it
	 */
	Status = DisplayInitialize(&dispCtrl, &vdma, HDMI_OUT_VTC_ID, DYNCLK_BASEADDR, pFrames, DEMO_STRIDE);
	if (Status != XST_SUCCESS)
	{
		xil_printf("Display Ctrl initialization failed during demo initialization%d\r\n", Status);
		return;
	}
	Status = DisplayStart(&dispCtrl);
	if (Status != XST_SUCCESS)
	{
		xil_printf("Couldn't start display during demo initialization%d\r\n", Status);
		return;
	}

	/*
	 * Initialize the Interrupt controller and start it.
	 */
	Status = fnInitInterruptController(&intc);
	if(Status != XST_SUCCESS) {
		xil_printf("Error initializing interrupts");
		return;
	}
	fnEnableInterrupts(&intc, &ivt[0], sizeof(ivt)/sizeof(ivt[0]));

	/*
	 * Initialize the Video Capture device
	 */
	Status = VideoInitialize(&videoCapt, &intc, &vdma, HDMI_IN_GPIO_ID, HDMI_IN_VTC_ID, HDMI_IN_VTC_IRPT_ID, pFrames, DEMO_STRIDE, DEMO_START_ON_DET);
	if (Status != XST_SUCCESS)
	{
		xil_printf("Video Ctrl initialization failed during demo initialization%d\r\n", Status);
		return;
	}

	/*
	 * Set the Video Detect callback to trigger the menu to reset, displaying the new detected resolution
	 */
	VideoSetCallback(&videoCapt, DemoISR, &fRefresh);

	DemoPrintTest(dispCtrl.framePtr[dispCtrl.curFrame], dispCtrl.vMode.width, dispCtrl.vMode.height, dispCtrl.stride, DEMO_PATTERN_1);

	return;
}


void DemoRun()
{
	int nextFrame = 0;
	char userInput = 0;

	/* Flush UART FIFO */
	while (XUartPs_IsReceiveData(UART_BASEADDR))
	{
		XUartPs_ReadReg(UART_BASEADDR, XUARTPS_FIFO_OFFSET);
	}

	while (userInput != 'q')
	{
		fRefresh = 0;
		DemoPrintMenu();

		/* Wait for data on UART */
		while (!XUartPs_IsReceiveData(UART_BASEADDR) && !fRefresh)
		{}

		/* Store the first character in the UART receive FIFO and echo it */
		if (XUartPs_IsReceiveData(UART_BASEADDR))
		{
			userInput = XUartPs_ReadReg(UART_BASEADDR, XUARTPS_FIFO_OFFSET);
			xil_printf("%c", userInput);
		}
		else  //Refresh triggered by video detect interrupt
		{
			userInput = 'r';
		}

		switch (userInput)
		{
		case '1':
			DemoChangeRes();
			break;
		case '2':
			nextFrame = dispCtrl.curFrame + 1;
			if (nextFrame >= DISPLAY_NUM_FRAMES)
			{
				nextFrame = 1;
			}
			DisplayChangeFrame(&dispCtrl, nextFrame);
			break;
		case '3':
			DemoPrintTest(pFrames[dispCtrl.curFrame], dispCtrl.vMode.width, dispCtrl.vMode.height, DEMO_STRIDE, DEMO_PATTERN_0);
			break;
		case '4':
			DemoPrintTest(pFrames[dispCtrl.curFrame], dispCtrl.vMode.width, dispCtrl.vMode.height, DEMO_STRIDE, DEMO_PATTERN_1);
			break;
		case '5':
			if (videoCapt.state == VIDEO_STREAMING)
				VideoStop(&videoCapt);
			else
				VideoStart(&videoCapt);
			break;
		case '6':
			nextFrame = videoCapt.curFrame + 1;
			if (nextFrame >= DISPLAY_NUM_FRAMES)
			{
				nextFrame = 1;
			}
			VideoChangeFrame(&videoCapt, nextFrame);
			break;
		case '7':
			Xil_DCacheDisable();
			nextFrame = DemoGetInactiveFrame(&dispCtrl, &videoCapt);
//			nextFrame = videoCapt.curFrame + 1;
//			if (nextFrame >= DISPLAY_NUM_FRAMES)
//			{
//				nextFrame = 1;
//			}
			VideoStop(&videoCapt);
			DemoInvertFrame(pFrames[videoCapt.curFrame], pFrames[nextFrame], videoCapt.timing.HActiveVideo, videoCapt.timing.VActiveVideo, DEMO_STRIDE);
			VideoStart(&videoCapt);
			DisplayChangeFrame(&dispCtrl, nextFrame);
			Xil_DCacheEnable();
			break;
		case '8':
			nextFrame = DemoGetInactiveFrame(&dispCtrl, &videoCapt);
//			nextFrame = videoCapt.curFrame + 1;
//			if (nextFrame >= DISPLAY_NUM_FRAMES)
//			{
//				nextFrame = 1;
//			}
			VideoStop(&videoCapt);
			DemoScaleFrame(pFrames[videoCapt.curFrame], pFrames[nextFrame], videoCapt.timing.HActiveVideo, videoCapt.timing.VActiveVideo, dispCtrl.vMode.width, dispCtrl.vMode.height, DEMO_STRIDE);
			VideoStart(&videoCapt);
			DisplayChangeFrame(&dispCtrl, nextFrame);
			break;
		case '9':
			DemoApplyFilter();
			break;
		case 'a':
			//Xil_DCacheDisable();
			nextFrame = DemoGetInactiveFrame(&dispCtrl, &videoCapt);
			VideoStop(&videoCapt);
			//DemoSobelFilter(pFrames[videoCapt.curFrame], pFrames[nextFrame], videoCapt.timing.HActiveVideo, videoCapt.timing.VActiveVideo, DEMO_STRIDE);

			DemoSobelFilter(pFrames[videoCapt.curFrame], pFrames[nextFrame], videoCapt.timing.HActiveVideo, videoCapt.timing.VActiveVideo, DEMO_STRIDE);
			VideoStart(&videoCapt);
			DisplayChangeFrame(&dispCtrl, nextFrame);
			Xil_DCacheEnable();
			break;
		case 'b':
			Xil_DCacheDisable();
			nextFrame = DemoGetInactiveFrame(&dispCtrl, &videoCapt);
			VideoStop(&videoCapt);
			DemoRobertsCrossFilter(pFrames[videoCapt.curFrame], pFrames[nextFrame], videoCapt.timing.HActiveVideo, videoCapt.timing.VActiveVideo, DEMO_STRIDE);
			VideoStart(&videoCapt);
			DisplayChangeFrame(&dispCtrl, nextFrame);
			Xil_DCacheEnable();
			break;
		case 'c':
			Xil_DCacheDisable();
			nextFrame = DemoGetInactiveFrame(&dispCtrl, &videoCapt);
			VideoStop(&videoCapt);
			DemoPrewittFilter(pFrames[videoCapt.curFrame], pFrames[nextFrame], videoCapt.timing.HActiveVideo, videoCapt.timing.VActiveVideo, DEMO_STRIDE);
			VideoStart(&videoCapt);
			DisplayChangeFrame(&dispCtrl, nextFrame);
			Xil_DCacheEnable();
			break;
		case 'q':
			break;
		case 'r':
			break;
		default :
			xil_printf("\n\rInvalid Selection");
			TimerDelay(500000);
		}
	}

	return;
}

void DemoPrintMenu()
{
	xil_printf("\x1B[H"); //Set cursor to top left of terminal
	xil_printf("\x1B[2J"); //Clear terminal
	xil_printf("**************************************************\n\r");
	xil_printf("*                     ZYBO Z7                    *\n\r");
	xil_printf("**************************************************\n\r");
	xil_printf("*Display Resolution: %28s*\n\r", dispCtrl.vMode.label);
	printf("*Display Pixel Clock Freq. (MHz): %15.3f*\n\r", dispCtrl.pxlFreq);
	xil_printf("*Display Frame Index: %27d*\n\r", dispCtrl.curFrame);
	if (videoCapt.state == VIDEO_DISCONNECTED) xil_printf("*Video Capture Resolution: %22s*\n\r", "!HDMI UNPLUGGED!");
	else xil_printf("*Video Capture Resolution: %17dx%-4d*\n\r", videoCapt.timing.HActiveVideo, videoCapt.timing.VActiveVideo);
	xil_printf("*Video Frame Index: %29d*\n\r", videoCapt.curFrame);
	xil_printf("**************************************************\n\r");
	xil_printf("\n\r");
	xil_printf("1 - Change Display Resolution\n\r");
	xil_printf("2 - Change Display Framebuffer Index\n\r");
	xil_printf("3 - Print Blended Test Pattern to Display Framebuffer\n\r");
	xil_printf("4 - Print Color Bar Test Pattern to Display Framebuffer\n\r");
	xil_printf("5 - Start/Stop Video stream into Video Framebuffer\n\r");
	xil_printf("6 - Change Video Framebuffer Index\n\r");
	xil_printf("7 - Grab Video Frame and invert colors\n\r");
	xil_printf("8 - Grab Video Frame and scale to Display resolution\n\r");
	xil_printf("9 - Grab Video Frame and apply Edge Detection Filters\n\r");
	xil_printf("a - Grab Video Frame and apply Edge Detection SOBEL Filters\n\r");
	xil_printf("b - Grab Video Frame and apply Edge Detection ROBERTS CROSS Filters\n\r");
	xil_printf("c - Grab Video Frame and apply Edge Detection PREWITT Filters\n\r");
	xil_printf("q - Quit\n\r");
	xil_printf("\n\r");
	xil_printf("\n\r");
	xil_printf("Enter a selection:");
}

void DemoChangeRes()
{
	int fResSet = 0;
	int status;
	char userInput = 0;

	/* Flush UART FIFO */
	while (XUartPs_IsReceiveData(UART_BASEADDR))
	{
		XUartPs_ReadReg(UART_BASEADDR, XUARTPS_FIFO_OFFSET);
	}

	while (!fResSet)
	{
		DemoCRMenu();

		/* Wait for data on UART */
		while (!XUartPs_IsReceiveData(UART_BASEADDR))
		{}

		/* Store the first character in the UART recieve FIFO and echo it */
		userInput = XUartPs_ReadReg(UART_BASEADDR, XUARTPS_FIFO_OFFSET);
		xil_printf("%c", userInput);
		status = XST_SUCCESS;
		switch (userInput)
		{
		case '1':
			status = DisplayStop(&dispCtrl);
			DisplaySetMode(&dispCtrl, &VMODE_640x480);
			DisplayStart(&dispCtrl);
			fResSet = 1;
			break;
		case '2':
			status = DisplayStop(&dispCtrl);
			DisplaySetMode(&dispCtrl, &VMODE_800x600);
			DisplayStart(&dispCtrl);
			fResSet = 1;
			break;
		case '3':
			status = DisplayStop(&dispCtrl);
			DisplaySetMode(&dispCtrl, &VMODE_1280x720);
			DisplayStart(&dispCtrl);
			fResSet = 1;
			break;
		case '4':
			status = DisplayStop(&dispCtrl);
			DisplaySetMode(&dispCtrl, &VMODE_1280x1024);
			DisplayStart(&dispCtrl);
			fResSet = 1;
			break;
		case '5':
			status = DisplayStop(&dispCtrl);
			DisplaySetMode(&dispCtrl, &VMODE_1600x900);
			DisplayStart(&dispCtrl);
			fResSet = 1;
			break;
		case '6':
			status = DisplayStop(&dispCtrl);
			DisplaySetMode(&dispCtrl, &VMODE_1920x1080);
			DisplayStart(&dispCtrl);
			fResSet = 1;
			break;
		case 'q':
			fResSet = 1;
			break;
		default :
			xil_printf("\n\rInvalid Selection");
			TimerDelay(500000);
		}
		if (status == XST_DMA_ERROR)
		{
			xil_printf("\n\rWARNING: AXI VDMA Error detected and cleared\n\r");
		}
	}
}

void DemoCRMenu()
{
	xil_printf("\x1B[H"); //Set cursor to top left of terminal
	xil_printf("\x1B[2J"); //Clear terminal
	xil_printf("**************************************************\n\r");
	xil_printf("*                     ZYBO Z7                    *\n\r");
	xil_printf("**************************************************\n\r");
	xil_printf("*Current Resolution: %28s*\n\r", dispCtrl.vMode.label);
	printf("*Pixel Clock Freq. (MHz): %23.3f*\n\r", dispCtrl.pxlFreq);
	xil_printf("**************************************************\n\r");
	xil_printf("\n\r");
	xil_printf("1 - %s\n\r", VMODE_640x480.label);
	xil_printf("2 - %s\n\r", VMODE_800x600.label);
	xil_printf("3 - %s\n\r", VMODE_1280x720.label);
	xil_printf("4 - %s\n\r", VMODE_1280x1024.label);
	xil_printf("5 - %s\n\r", VMODE_1600x900.label);
	xil_printf("6 - %s\n\r", VMODE_1920x1080.label);
	xil_printf("q - Quit (don't change resolution)\n\r");
	xil_printf("\n\r");
	xil_printf("Select a new resolution:");
}

void DemoFilterMenu()
{
	xil_printf("\x1B[H"); //Set cursor to top left of terminal
	xil_printf("\x1B[2J"); //Clear terminal
	xil_printf("**************************************************\n\r");
	xil_printf("*                     ZYBO Z7                    *\n\r");
	xil_printf("**************************************************\n\r");
	xil_printf("*Current Resolution: %28s*\n\r", dispCtrl.vMode.label);
	printf("*Pixel Clock Freq. (MHz): %23.3f*\n\r", dispCtrl.pxlFreq);
	xil_printf("**************************************************\n\r");
	xil_printf("\n\r");
	xil_printf("1 - SOBEL filter\n\r");
	xil_printf("2 - ROBERTS CROSS filter\n\r");
	xil_printf("3 - PREWITT filter\n\r");
	xil_printf("4 - NEON SOBEL filter\n\r");
	xil_printf("5 - NEON ROBERTS CROSS filter\n\r");
	xil_printf("6 - NEON PREWITT filter\n\r");
	xil_printf("q - Quit (don't apply filter)\n\r");
	xil_printf("\n\r");
	xil_printf("Select a new edge detection filter:");
}

int DemoGetInactiveFrame(DisplayCtrl *DispCtrlPtr, VideoCapture *VideoCaptPtr)
{
	int i;
	for (i=1; i<DISPLAY_NUM_FRAMES; i++)
	{
		if (DispCtrlPtr->curFrame == i && DispCtrlPtr->state == DISPLAY_RUNNING)
		{
			continue;
		}
		else if (VideoCaptPtr->curFrame == i && VideoCaptPtr->state == VIDEO_STREAMING)
		{
			continue;
		}
		else
		{
			return i;
		}
	}
	xil_printf("Unreachable error state reached. All buffers are in use.\r\n");
}

void DemoInvertFrame(u8 *srcFrame, u8 *destFrame, u32 width, u32 height, u32 stride)
{
	u32 xcoi, ycoi;
	u32 lineStart = 0;
	Xil_DCacheInvalidateRange((unsigned int) srcFrame, DEMO_MAX_FRAME);
	for(ycoi = 0; ycoi < height; ycoi++)
	{
		for(xcoi = 0; xcoi < (width * 3); xcoi+=3)
		{
			destFrame[xcoi + lineStart] = ~srcFrame[xcoi + lineStart];         //Red
			destFrame[xcoi + lineStart + 1] = ~srcFrame[xcoi + lineStart + 1]; //Blue
			destFrame[xcoi + lineStart + 2] = ~srcFrame[xcoi + lineStart + 2]; //Green
		}
		lineStart += stride;
	}
	/*
	 * Flush the framebuffer memory range to ensure changes are written to the
	 * actual memory, and therefore accessible by the VDMA.
	 */
	Xil_DCacheFlushRange((unsigned int) destFrame, DEMO_MAX_FRAME);

}


/*
 * Bilinear interpolation algorithm. Assumes both frames have the same stride.
 */
void DemoScaleFrame(u8 *srcFrame, u8 *destFrame, u32 srcWidth, u32 srcHeight, u32 destWidth, u32 destHeight, u32 stride)
{
	float xInc, yInc; // Width/height of a destination frame pixel in the source frame coordinate system
	float xcoSrc, ycoSrc; // Location of the destination pixel being operated on in the source frame coordinate system
	float x1y1, x2y1, x1y2, x2y2; //Used to store the color data of the four nearest source pixels to the destination pixel
	int ix1y1, ix2y1, ix1y2, ix2y2; //indexes into the source frame for the four nearest source pixels to the destination pixel
	float xDist, yDist; //distances between destination pixel and x1y1 source pixels in source frame coordinate system

	int xcoDest, ycoDest; // Location of the destination pixel being operated on in the destination coordinate system
	int iy1; //Used to store the index of the first source pixel in the line with y1
	int iDest; //index of the pixel data in the destination frame being operated on

	int i;

	xInc = ((float) srcWidth - 1.0) / ((float) destWidth);
	yInc = ((float) srcHeight - 1.0) / ((float) destHeight);

	ycoSrc = 0.0;
	for (ycoDest = 0; ycoDest < destHeight; ycoDest++)
	{
		iy1 = ((int) ycoSrc) * stride;
		yDist = ycoSrc - ((float) ((int) ycoSrc));

		/*
		 * Save some cycles in the loop below by presetting the destination
		 * index to the first pixel in the current line
		 */
		iDest = ycoDest * stride;

		xcoSrc = 0.0;
		for (xcoDest = 0; xcoDest < destWidth; xcoDest++)
		{
			ix1y1 = iy1 + ((int) xcoSrc) * 3;
			ix2y1 = ix1y1 + 3;
			ix1y2 = ix1y1 + stride;
			ix2y2 = ix1y1 + stride + 3;

			xDist = xcoSrc - ((float) ((int) xcoSrc));

			/*
			 * For loop handles all three colors
			 */
			for (i = 0; i < 3; i++)
			{
				x1y1 = (float) srcFrame[ix1y1 + i];
				x2y1 = (float) srcFrame[ix2y1 + i];
				x1y2 = (float) srcFrame[ix1y2 + i];
				x2y2 = (float) srcFrame[ix2y2 + i];

				/*
				 * Bilinear interpolation function
				 */
				destFrame[iDest] = (u8) ((1.0-yDist)*((1.0-xDist)*x1y1+xDist*x2y1) + yDist*((1.0-xDist)*x1y2+xDist*x2y2));
				iDest++;
			}
			xcoSrc += xInc;
		}
		ycoSrc += yInc;
	}

	/*
	 * Flush the framebuffer memory range to ensure changes are written to the
	 * actual memory, and therefore accessible by the VDMA.
	 */
	Xil_DCacheFlushRange((unsigned int) destFrame, DEMO_MAX_FRAME);

	return;
}

void DemoPrintTest(u8 *frame, u32 width, u32 height, u32 stride, int pattern)
{
	u32 xcoi, ycoi;
	u32 iPixelAddr;
	u8 wRed, wBlue, wGreen;
	u32 wCurrentInt;
	double fRed, fBlue, fGreen, fColor;
	u32 xLeft, xMid, xRight, xInt;
	u32 yMid, yInt;
	double xInc, yInc;


	switch (pattern)
	{
	case DEMO_PATTERN_0:

		xInt = width / 4; //Four intervals, each with width/4 pixels
		xLeft = xInt * 3;
		xMid = xInt * 2 * 3;
		xRight = xInt * 3 * 3;
		xInc = 256.0 / ((double) xInt); //256 color intensities are cycled through per interval (overflow must be caught when color=256.0)

		yInt = height / 2; //Two intervals, each with width/2 lines
		yMid = yInt;
		yInc = 256.0 / ((double) yInt); //256 color intensities are cycled through per interval (overflow must be caught when color=256.0)

		fBlue = 0.0;
		fRed = 256.0;
		for(xcoi = 0; xcoi < (width*3); xcoi+=3)
		{
			/*
			 * Convert color intensities to integers < 256, and trim values >=256
			 */
			wRed = (fRed >= 256.0) ? 255 : ((u8) fRed);
			wBlue = (fBlue >= 256.0) ? 255 : ((u8) fBlue);
			iPixelAddr = xcoi;
			fGreen = 0.0;
			for(ycoi = 0; ycoi < height; ycoi++)
			{

				wGreen = (fGreen >= 256.0) ? 255 : ((u8) fGreen);
				frame[iPixelAddr] = wRed;
				frame[iPixelAddr + 1] = wBlue;
				frame[iPixelAddr + 2] = wGreen;
				if (ycoi < yMid)
				{
					fGreen += yInc;
				}
				else
				{
					fGreen -= yInc;
				}

				/*
				 * This pattern is printed one vertical line at a time, so the address must be incremented
				 * by the stride instead of just 1.
				 */
				iPixelAddr += stride;
			}

			if (xcoi < xLeft)
			{
				fBlue = 0.0;
				fRed -= xInc;
			}
			else if (xcoi < xMid)
			{
				fBlue += xInc;
				fRed += xInc;
			}
			else if (xcoi < xRight)
			{
				fBlue -= xInc;
				fRed -= xInc;
			}
			else
			{
				fBlue += xInc;
				fRed = 0;
			}
		}
		/*
		 * Flush the framebuffer memory range to ensure changes are written to the
		 * actual memory, and therefore accessible by the VDMA.
		 */
		Xil_DCacheFlushRange((unsigned int) frame, DEMO_MAX_FRAME);
		break;
	case DEMO_PATTERN_1:

		xInt = width / 7; //Seven intervals, each with width/7 pixels
		xInc = 256.0 / ((double) xInt); //256 color intensities per interval. Notice that overflow is handled for this pattern.

		fColor = 0.0;
		wCurrentInt = 1;
		for(xcoi = 0; xcoi < (width*3); xcoi+=3)
		{

			/*
			 * Just draw white in the last partial interval (when width is not divisible by 7)
			 */
			if (wCurrentInt > 7)
			{
				wRed = 255;
				wBlue = 255;
				wGreen = 255;
			}
			else
			{
				if (wCurrentInt & 0b001)
					wRed = (u8) fColor;
				else
					wRed = 0;

				if (wCurrentInt & 0b010)
					wBlue = (u8) fColor;
				else
					wBlue = 0;

				if (wCurrentInt & 0b100)
					wGreen = (u8) fColor;
				else
					wGreen = 0;
			}

			iPixelAddr = xcoi;

			for(ycoi = 0; ycoi < height; ycoi++)
			{
				frame[iPixelAddr] = wRed;
				frame[iPixelAddr + 1] = wBlue;
				frame[iPixelAddr + 2] = wGreen;
				/*
				 * This pattern is printed one vertical line at a time, so the address must be incremented
				 * by the stride instead of just 1.
				 */
				iPixelAddr += stride;
			}

			fColor += xInc;
			if (fColor >= 256.0)
			{
				fColor = 0.0;
				wCurrentInt++;
			}
		}
		/*
		 * Flush the framebuffer memory range to ensure changes are written to the
		 * actual memory, and therefore accessible by the VDMA.
		 */
		Xil_DCacheFlushRange((unsigned int) frame, DEMO_MAX_FRAME);
		break;
	default :
		xil_printf("Error: invalid pattern passed to DemoPrintTest");
	}
}

void DemoISR(void *callBackRef, void *pVideo)
{
	char *data = (char *) callBackRef;
	*data = 1; //set fRefresh to 1
}

void DemoSobelFilter(u8 *srcFrame, u8 *destFrame, u32 width, u32 height, u32 stride)
{
	int gx, gy, gray;
	int x, y, i, j;
	int sobelX[3][3] = {
		{-1, 0, 1},
		{-2, 0, 2},
		{-1, 0, 1}
	};
	int sobelY[3][3] = {
		{-1, -2, -1},
		{ 0,  0,  0},
		{ 1,  2,  1}
	};
	Xil_DCacheInvalidateRange((unsigned int)srcFrame, DEMO_MAX_FRAME);

	for (y = 1; y < height - 1; y++) {
		for (x = 1; x < width - 1; x++) {
			gx = 0;
			gy = 0;

			for (i = -1; i <= 1; i++) {
				for (j = -1; j <= 1; j++) {
					int pixelIndex = ((y + i) * stride + (x + j) * 3);
					int grayValue = (srcFrame[pixelIndex] * 7471 +
									 srcFrame[pixelIndex + 1] * 38470 +
									 srcFrame[pixelIndex + 2] * 19595) >> 16;

					gx += sobelX[i + 1][j + 1] * grayValue;
					gy += sobelY[i + 1][j + 1] * grayValue;
				}
			}

			gray = (gx * gx + gy * gy) >> 4;
			gray = (gray > 255) ? 255 : gray;

			int outPixelIndex = (y * stride + x * 3);
			destFrame[outPixelIndex] = gray;       // Blue
			destFrame[outPixelIndex + 1] = gray;  // Green
			destFrame[outPixelIndex + 2] = gray;  // Red
		}
	}

	Xil_DCacheFlushRange((unsigned int)destFrame, DEMO_MAX_FRAME);
}

void DemoRobertsCrossFilter(u8 *srcFrame, u8 *destFrame, u32 width, u32 height, u32 stride)
{
	int gx, gy, gray;
	int x, y;
	int robertsX[2][2] = {
		{ 1,  0},
		{ 0, -1}
	};
	int robertsY[2][2] = {
		{ 0,  1},
		{-1,  0}
	};

	Xil_DCacheInvalidateRange((unsigned int)srcFrame, DEMO_MAX_FRAME);

	for (y = 0; y < height - 1; y++) {
		for (x = 0; x < width - 1; x++) {
			gx = 0;
			gy = 0;

			// Compute gradients using 2x2 kernels
			for (int i = 0; i < 2; i++) {
				for (int j = 0; j < 2; j++) {
					int pixelIndex = ((y + i) * stride + (x + j) * 3);
					int grayValue = (srcFrame[pixelIndex] * 7471 +
									 srcFrame[pixelIndex + 1] * 38470 +
									 srcFrame[pixelIndex + 2] * 19595) >> 16;

					gx += robertsX[i][j] * grayValue;
					gy += robertsY[i][j] * grayValue;
				}
			}

			// Compute the gradient magnitude
			gray = (gx * gx + gy * gy) >> 4;
			gray = (gray > 255) ? 255 : gray;

			// Write to the destination frame
			int outPixelIndex = (y * stride + x * 3);
			destFrame[outPixelIndex] = gray;       // Blue
			destFrame[outPixelIndex + 1] = gray;  // Green
			destFrame[outPixelIndex + 2] = gray;  // Red
		}
	}

	Xil_DCacheFlushRange((unsigned int)destFrame, DEMO_MAX_FRAME);
}

void DemoPrewittFilter(u8 *srcFrame, u8 *destFrame, u32 width, u32 height, u32 stride)
{
	int gx, gy, gray;
	int x, y, i, j;
	int prewittX[3][3] = {
		{-1, 0, 1},
		{-1, 0, 1},
		{-1, 0, 1}
	};
	int prewittY[3][3] = {
		{-1, -1, -1},
		{ 0,  0,  0},
		{ 1,  1,  1}
	};

	Xil_DCacheInvalidateRange((unsigned int)srcFrame, DEMO_MAX_FRAME);

	for (y = 1; y < height - 1; y++) {
		for (x = 1; x < width - 1; x++) {
			gx = 0;
			gy = 0;

			// Compute gradients using 3x3 Prewitt kernels
			for (i = -1; i <= 1; i++) {
				for (j = -1; j <= 1; j++) {
					int pixelIndex = ((y + i) * stride + (x + j) * 3);
					int grayValue = (srcFrame[pixelIndex] * 7471 +
									 srcFrame[pixelIndex + 1] * 38470 +
									 srcFrame[pixelIndex + 2] * 19595) >> 16;

					gx += prewittX[i + 1][j + 1] * grayValue;
					gy += prewittY[i + 1][j + 1] * grayValue;
				}
			}

			// Compute the gradient magnitude
			gray = (gx * gx + gy * gy) >> 4;
			gray = (gray > 255) ? 255 : gray;

			// Write to the destination frame
			int outPixelIndex = (y * stride + x * 3);
			destFrame[outPixelIndex] = gray;       // Blue
			destFrame[outPixelIndex + 1] = gray;  // Green
			destFrame[outPixelIndex + 2] = gray;  // Red
		}
	}

	Xil_DCacheFlushRange((unsigned int)destFrame, DEMO_MAX_FRAME);
}

void DemoApplyFilter()
{
	int nextFrame = 0;
	int fResSet = 0;
	int status;
	char userInput = 0;

	/* Flush UART FIFO */
	while (XUartPs_IsReceiveData(UART_BASEADDR))
	{
		XUartPs_ReadReg(UART_BASEADDR, XUARTPS_FIFO_OFFSET);
	}

	while (!fResSet)
	{
		DemoFilterMenu();

		/* Wait for data on UART */
		while (!XUartPs_IsReceiveData(UART_BASEADDR))
		{}

		/* Store the first character in the UART recieve FIFO and echo it */
		userInput = XUartPs_ReadReg(UART_BASEADDR, XUARTPS_FIFO_OFFSET);
		xil_printf("%c", userInput);
		status = XST_SUCCESS;
		switch (userInput)
		{
		case '1':
			//Xil_DCacheDisable();
			nextFrame = DemoGetInactiveFrame(&dispCtrl, &videoCapt);
			VideoStop(&videoCapt);
			//DemoSobelFilter(pFrames[videoCapt.curFrame], pFrames[nextFrame], videoCapt.timing.HActiveVideo, videoCapt.timing.VActiveVideo, DEMO_STRIDE);

		    BenchmarkSobel(
		        pFrames[videoCapt.curFrame],
		        pFrames[nextFrame],
		        videoCapt.timing.HActiveVideo,
		        videoCapt.timing.VActiveVideo,
		        DEMO_STRIDE
		    );
			VideoStart(&videoCapt);
			DisplayChangeFrame(&dispCtrl, nextFrame);
			//Xil_DCacheEnable();
			break;
		case '2':
			Xil_DCacheDisable();
			nextFrame = DemoGetInactiveFrame(&dispCtrl, &videoCapt);
			VideoStop(&videoCapt);
			DemoRobertsCrossFilter(pFrames[videoCapt.curFrame], pFrames[nextFrame], videoCapt.timing.HActiveVideo, videoCapt.timing.VActiveVideo, DEMO_STRIDE);
			VideoStart(&videoCapt);
			DisplayChangeFrame(&dispCtrl, nextFrame);
			Xil_DCacheEnable();
			break;
		case '3':
			Xil_DCacheDisable();
			nextFrame = DemoGetInactiveFrame(&dispCtrl, &videoCapt);
			VideoStop(&videoCapt);
			DemoPrewittFilter(pFrames[videoCapt.curFrame], pFrames[nextFrame], videoCapt.timing.HActiveVideo, videoCapt.timing.VActiveVideo, DEMO_STRIDE);
			VideoStart(&videoCapt);
			DisplayChangeFrame(&dispCtrl, nextFrame);
			Xil_DCacheEnable();
			break;
		case '4':
			//Xil_DCacheDisable();
			nextFrame = DemoGetInactiveFrame(&dispCtrl, &videoCapt);
			VideoStop(&videoCapt);
			//DemoSobelFilterNEON(pFrames[videoCapt.curFrame], pFrames[nextFrame], videoCapt.timing.HActiveVideo, videoCapt.timing.VActiveVideo, DEMO_STRIDE);

		    BenchmarkSobelNEON(
		        pFrames[videoCapt.curFrame],
		        pFrames[nextFrame],
		        videoCapt.timing.HActiveVideo,
		        videoCapt.timing.VActiveVideo,
		        DEMO_STRIDE
		    );
			VideoStart(&videoCapt);
			DisplayChangeFrame(&dispCtrl, nextFrame);
			//Xil_DCacheEnable();
			break;
		case '5':
			Xil_DCacheDisable();
			nextFrame = DemoGetInactiveFrame(&dispCtrl, &videoCapt);
			VideoStop(&videoCapt);
			DemoRobertsCrossFilterNEON(pFrames[videoCapt.curFrame], pFrames[nextFrame], videoCapt.timing.HActiveVideo, videoCapt.timing.VActiveVideo, DEMO_STRIDE);
			VideoStart(&videoCapt);
			DisplayChangeFrame(&dispCtrl, nextFrame);
			Xil_DCacheEnable();
			break;
		case '6':
			Xil_DCacheDisable();
			nextFrame = DemoGetInactiveFrame(&dispCtrl, &videoCapt);
			VideoStop(&videoCapt);
			DemoPrewittFilterNEON(pFrames[videoCapt.curFrame], pFrames[nextFrame], videoCapt.timing.HActiveVideo, videoCapt.timing.VActiveVideo, DEMO_STRIDE);
			VideoStart(&videoCapt);
			DisplayChangeFrame(&dispCtrl, nextFrame);
			Xil_DCacheEnable();
			break;
		case 'q':
			fResSet = 1;
			break;
		default :
			xil_printf("\n\rInvalid Selection");
			TimerDelay(500000);
		}
		if (status == XST_DMA_ERROR)
		{
			xil_printf("\n\rWARNING: AXI VDMA Error detected and cleared\n\r");
		}
	}
}

static inline void convert_rgb_to_gray_neon(const uint8_t *src, uint16_t *gray, uint32_t width, uint32_t height, uint32_t stride) {
    // Coefficients scaled by 2^16: R=7471, G=38470, B=19595
    // Gray = (R*0.299 + G*0.587 + B*0.114) in fixed-point
    const uint16x8_t coeffR = vdupq_n_u16(7471);
    const uint16x8_t coeffG = vdupq_n_u16(38470);
    const uint16x8_t coeffB = vdupq_n_u16(19595);

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *row = src + y * stride;
        uint16_t *grayRow = gray + y * width;

        for (uint32_t x = 0; x < width; x += 8) {
            uint8x8x3_t pix = vld3_u8(row + x*3);

            // Extend to 16-bit
            uint16x8_t R = vmovl_u8(pix.val[2]);
            uint16x8_t G = vmovl_u8(pix.val[1]);
            uint16x8_t B = vmovl_u8(pix.val[0]);

            // gray_val = (R*coeffR + G*coeffG + B*coeffB) >> 16
            uint32x4_t loR = vmull_u16(vget_low_u16(R), vget_low_u16(coeffR));
            uint32x4_t loG = vmull_u16(vget_low_u16(G), vget_low_u16(coeffG));
            uint32x4_t loB = vmull_u16(vget_low_u16(B), vget_low_u16(coeffB));

            uint32x4_t hiR = vmull_u16(vget_high_u16(R), vget_high_u16(coeffR));
            uint32x4_t hiG = vmull_u16(vget_high_u16(G), vget_high_u16(coeffG));
            uint32x4_t hiB = vmull_u16(vget_high_u16(B), vget_high_u16(coeffB));

            uint32x4_t loSum = vaddq_u32(vaddq_u32(loR, loG), loB);
            uint32x4_t hiSum = vaddq_u32(vaddq_u32(hiR, hiG), hiB);

            uint16x8_t grayVal = vcombine_u16(
                vshrn_n_u32(loSum, 16),
                vshrn_n_u32(hiSum, 16)
            );

            vst1q_u16(grayRow + x, grayVal);
        }
    }
}

void DemoSobelFilterNEON(uint8_t *srcFrame, uint8_t *destFrame, uint32_t width, uint32_t height, uint32_t stride) {
    // Invalidate cache for source
    Xil_DCacheInvalidateRange((unsigned int) srcFrame, stride * height);

    // Allocate space for a grayscale representation (16-bit to hold intermediate results)
    // For larger resolutions, consider dynamic allocation.
    static uint16_t grayBuffer[1920*1080]; // adjust if necessary or use malloc if dynamic allocation allowed.

    // Convert to grayscale
    convert_rgb_to_gray_neon(srcFrame, grayBuffer, width, height, stride);

    // Sobel kernels:
    // Gx:
    //  [-1  0  1]
    //  [-2  0  2]
    //  [-1  0  1]
    //
    // Gy:
    //  [-1 -2 -1]
    //   [ 0  0  0]
    //   [ 1  2  1]

    // Process line by line, skipping the first and last row
    for (uint32_t y = 1; y < height - 1; y++) {
        const uint16_t *top    = grayBuffer + (y - 1) * width;
        const uint16_t *middle = grayBuffer + y * width;
        const uint16_t *bottom = grayBuffer + (y + 1) * width;
        uint8_t *outRow = destFrame + y * stride;

        // Process in chunks of 8 pixels to utilize NEON
        for (uint32_t x = 1; x + 7 < width - 1; x += 8) {
            // Load 3x3 neighborhood around each pixel (in parallel)
            uint16x8_t tLeft   = vld1q_u16(top    + x - 1);
            uint16x8_t tMid    = vld1q_u16(top    + x    );
            uint16x8_t tRight  = vld1q_u16(top    + x + 1);

            uint16x8_t mLeft   = vld1q_u16(middle + x - 1);
            uint16x8_t mRight  = vld1q_u16(middle + x + 1);

            uint16x8_t bLeft   = vld1q_u16(bottom + x - 1);
            uint16x8_t bMid    = vld1q_u16(bottom + x    );
            uint16x8_t bRight  = vld1q_u16(bottom + x + 1);

            // Convert these to signed for arithmetic
            int16x8_t tL = vreinterpretq_s16_u16(tLeft);
            int16x8_t tM = vreinterpretq_s16_u16(tMid);
            int16x8_t tR = vreinterpretq_s16_u16(tRight);

            int16x8_t mL = vreinterpretq_s16_u16(mLeft);
            int16x8_t mR = vreinterpretq_s16_u16(mRight);

            int16x8_t bL = vreinterpretq_s16_u16(bLeft);
            int16x8_t bM = vreinterpretq_s16_u16(bMid);
            int16x8_t bR = vreinterpretq_s16_u16(bRight);

            // Compute Gx:
            // Gx = (tR - tL) + 2*(mR - mL) + (bR - bL)
            int16x8_t gx = vsubq_s16(tR, tL);
            gx = vaddq_s16(gx, vshlq_n_s16(vsubq_s16(mR, mL), 1));
            gx = vaddq_s16(gx, vsubq_s16(bR, bL));

            // Compute Gy:
            // Gy = (bL + 2*bM + bR) - (tL + 2*tM + tR)
            // Start from 0 and add/subtract terms:
            int16x8_t gy = vsubq_s16(vaddq_s16(vaddq_s16(bL, bR), vshlq_n_s16(bM,1)),
                                     vaddq_s16(vaddq_s16(tL, tR), vshlq_n_s16(tM,1)));

            // Magnitude approximation: mag = |gx| + |gy|
            int16x8_t abs_gx = vabsq_s16(gx);
            int16x8_t abs_gy = vabsq_s16(gy);
            int16x8_t mag = vaddq_s16(abs_gx, abs_gy);

            // Clamp to [0,255] and convert to u8
            uint8x8_t result = vqmovun_s16(vcombine_s16(vget_low_s16(mag), vget_high_s16(mag)));

            // Store as RGB = Gray
            uint8x8x3_t outPix;
            outPix.val[0] = result;
            outPix.val[1] = result;
            outPix.val[2] = result;
            vst3_u8(outRow + x*3, outPix);
        }

        // Handle the remaining pixels at the end of the row with a scalar fallback
        uint32_t remainderStart = ((width - 1) & ~7);
        for (uint32_t x_tail = remainderStart; x_tail < width - 1; x_tail++) {
            int gx = (-top[x_tail - 1] + top[x_tail + 1])
                     + (-2 * middle[x_tail - 1] + 2 * middle[x_tail + 1])
                     + (-bottom[x_tail - 1] + bottom[x_tail + 1]);

            int gy = (-top[x_tail - 1] - 2*top[x_tail] - top[x_tail + 1])
                     + (bottom[x_tail - 1] + 2*bottom[x_tail] + bottom[x_tail + 1]);

            int mag = abs(gx) + abs(gy);
            if (mag > 255) mag = 255;

            uint8_t g = (uint8_t) mag;
            uint32_t outIndex = y * stride + x_tail * 3;
            destFrame[outIndex] = g;
            destFrame[outIndex + 1] = g;
            destFrame[outIndex + 2] = g;
        }
    }

    // Flush destination cache
    Xil_DCacheFlushRange((unsigned int) destFrame, stride * height);
}

void DemoRobertsCrossFilterNEON(uint8_t *srcFrame, uint8_t *destFrame, uint32_t width, uint32_t height, uint32_t stride) {
    // Invalidate cache for source frame to ensure coherency
    Xil_DCacheInvalidateRange((unsigned int) srcFrame, stride * height);

    // Statically allocated grayscale buffer. For large frames, consider dynamic allocation.
    static uint16_t grayBuffer[1920*1080]; // Adjust if necessary.

    // Convert input RGB frame to grayscale (16-bit for precision)
    convert_rgb_to_gray_neon(srcFrame, grayBuffer, width, height, stride);

    // Roberts Cross operator uses a 2x2 neighborhood:
    // For pixel at (x,y):
    // Gx = Gray(y, x) - Gray(y+1, x+1)
    // Gy = Gray(y, x+1) - Gray(y+1, x)
    //
    // The operator calculates diagonally adjacent differences.
    // Only process until width-1 and height-1 because of the 2x2 region.

    for (uint32_t y = 0; y < height - 1; y++) {
        uint8_t *outRow = destFrame + y * stride;
        const uint16_t *curLine  = grayBuffer + y * width;
        const uint16_t *nextLine = grayBuffer + (y + 1) * width;

        // Process multiple pixels using NEON where possible
        for (uint32_t x = 0; x + 8 < width; x += 8) {
            // Load current and next line segments
            uint16x8_t c  = vld1q_u16(curLine + x);
            uint16x8_t n  = vld1q_u16(nextLine + x);
            uint16x8_t c1 = vld1q_u16(curLine + x + 1);
            uint16x8_t n1 = vld1q_u16(nextLine + x + 1);

            // Compute Gx = current - next shifted right
            int16x8_t gx = vreinterpretq_s16_u16(c);
            gx = vsubq_s16(gx, vreinterpretq_s16_u16(n1));

            // Compute Gy = current shifted right - next
            int16x8_t gy = vreinterpretq_s16_u16(c1);
            gy = vsubq_s16(gy, vreinterpretq_s16_u16(n));

            // Approximate magnitude = |gx| + |gy|
            int16x8_t abs_gx = vabsq_s16(gx);
            int16x8_t abs_gy = vabsq_s16(gy);
            int16x8_t mag = vaddq_s16(abs_gx, abs_gy);

            // Clamp to [0,255] and convert to 8-bit
            uint8x8_t result = vqmovun_s16(vcombine_s16(vget_low_s16(mag), vget_high_s16(mag)));

            // Store as R=G=B=gray
            uint8x8x3_t outPix = { result, result, result };
            vst3_u8(outRow + x*3, outPix);
        }

        // Handle remainder pixels scalar
        uint32_t remainderStart = ((width) & ~7U);
        for (uint32_t x_tail = remainderStart; x_tail < width - 1; x_tail++) {
            int gx = (int)curLine[x_tail] - (int)nextLine[x_tail + 1];
            int gy = (int)curLine[x_tail + 1] - (int)nextLine[x_tail];

            int mag = abs(gx) + abs(gy);
            if (mag > 255) mag = 255;

            uint8_t g = (uint8_t)mag;
            uint32_t outIndex = y * stride + x_tail * 3;
            destFrame[outIndex] = g;
            destFrame[outIndex + 1] = g;
            destFrame[outIndex + 2] = g;
        }
    }

    // Flush destination frame cache so changes are visible
    Xil_DCacheFlushRange((unsigned int)destFrame, stride * height);
}

void DemoPrewittFilterNEON(uint8_t *srcFrame, uint8_t *destFrame, uint32_t width, uint32_t height, uint32_t stride) {
    // Invalidate cache for source frame to ensure coherency
    Xil_DCacheInvalidateRange((unsigned int) srcFrame, stride * height);

    // Statically allocated grayscale buffer. For large frames, consider dynamic allocation.
    static uint16_t grayBuffer[1920*1080]; // Adjust if necessary.

    // Convert input RGB frame to grayscale (16-bit for precision)
    convert_rgb_to_gray_neon(srcFrame, grayBuffer, width, height, stride);

    // Prewitt operator uses a 3x3 neighborhood similar to Sobel, but with different coefficients:
    //
    // Gx:
    //  [-1  0  1]
    //  [-1  0  1]
    //  [-1  0  1]
    //
    // Gy:
    //  [-1 -1 -1]
    //   [ 0  0  0]
    //   [ 1  1  1]
    //
    // It measures gradients without weighting the center row or column more heavily like Sobel.

    for (uint32_t y = 1; y < height - 1; y++) {
        uint8_t *outRow = destFrame + y * stride;
        const uint16_t *top = grayBuffer + (y - 1) * width;
        const uint16_t *mid = grayBuffer + y * width;
        const uint16_t *bot = grayBuffer + (y + 1) * width;

        // Process multiple pixels using NEON where possible
        for (uint32_t x = 1; x + 7 < width - 1; x += 8) {
            // Load top row
            uint16x8_t tL = vld1q_u16(top + x - 1);
            uint16x8_t tX = vld1q_u16(top + x);
            uint16x8_t tR = vld1q_u16(top + x + 1);

            // Load middle row
            uint16x8_t mL = vld1q_u16(mid + x - 1);
            uint16x8_t mR = vld1q_u16(mid + x + 1);

            // Load bottom row
            uint16x8_t bL = vld1q_u16(bot + x - 1);
            uint16x8_t bX = vld1q_u16(bot + x);
            uint16x8_t bR = vld1q_u16(bot + x + 1);

            // Convert to signed for arithmetic
            int16x8_t TL = vreinterpretq_s16_u16(tL);
            int16x8_t TX = vreinterpretq_s16_u16(tX);
            int16x8_t TR = vreinterpretq_s16_u16(tR);

            int16x8_t ML = vreinterpretq_s16_u16(mL);
            int16x8_t MR = vreinterpretq_s16_u16(mR);

            int16x8_t BL = vreinterpretq_s16_u16(bL);
            int16x8_t BX = vreinterpretq_s16_u16(bX);
            int16x8_t BR = vreinterpretq_s16_u16(bR);

            // Gx = (TR+MR+BR) - (TL+ML+BL)
            int16x8_t sumLeft = vaddq_s16(vaddq_s16(TL, ML), BL);
            int16x8_t sumRight = vaddq_s16(vaddq_s16(TR, MR), BR);
            int16x8_t gx = vsubq_s16(sumRight, sumLeft);

            // Gy = (BL+BX+BR) - (TL+TX+TR)
            int16x8_t sumTop = vaddq_s16(vaddq_s16(TL, TX), TR);
            int16x8_t sumBot = vaddq_s16(vaddq_s16(BL, BX), BR);
            int16x8_t gy = vsubq_s16(sumBot, sumTop);

            // Approximate magnitude = |gx| + |gy|
            int16x8_t abs_gx = vabsq_s16(gx);
            int16x8_t abs_gy = vabsq_s16(gy);
            int16x8_t mag = vaddq_s16(abs_gx, abs_gy);

            // Clamp to [0,255] and convert to u8
            uint8x8_t result = vqmovun_s16(vcombine_s16(vget_low_s16(mag), vget_high_s16(mag)));

            // Store as grayscale in all RGB channels
            uint8x8x3_t outPix = { result, result, result };
            vst3_u8(outRow + x*3, outPix);
        }

        // Scalar remainder for trailing pixels
        uint32_t remainderStart = ((width - 1) & ~7U);
        for (uint32_t x_tail = remainderStart; x_tail < width - 1; x_tail++) {
            int gx = (- (int)top[x_tail - 1] - (int)mid[x_tail - 1] - (int)bot[x_tail - 1])
                    + ((int)top[x_tail + 1] + (int)mid[x_tail + 1] + (int)bot[x_tail + 1]);

            int gy = ((int)bot[x_tail - 1] + (int)bot[x_tail] + (int)bot[x_tail + 1])
                    - ((int)top[x_tail - 1] + (int)top[x_tail] + (int)top[x_tail + 1]);

            int mag = abs(gx) + abs(gy);
            if (mag > 255) mag = 255;

            uint8_t g = (uint8_t)mag;
            uint32_t outIndex = y * stride + x_tail * 3;
            destFrame[outIndex] = g;
            destFrame[outIndex + 1] = g;
            destFrame[outIndex + 2] = g;
        }
    }

    // Flush destination frame cache
    Xil_DCacheFlushRange((unsigned int)destFrame, stride * height);
}

