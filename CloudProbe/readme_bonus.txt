=================================================================================
IMPROTANT:									|
										|
If you modify a file on client side, please note that this operation will       |
										|
be synchronized on other clients in AT MOST 10 SECONDS! IT MAY NOT BE AN        |
										|
IMMEDIATE SYNCHRONIZATION.							|
										|
You can see more design details in documentation.docx.				|
										|
Thank you for your attention.							|
										|
										|
=================================================================================

/*
 * File         : CloudProbe_bonus.c
 * System       : Generic System
 * Version      : 04.00
 * Version Date : 13-12-22
 * Designer     : Wang Cheng & Hu Xiaokang
 * Programmer   : Wang Cheng & Hu Xiaokang
 * Copyright    : Wang Cheng & Hu Xiaokang 1993-2013 All Rights Reserved
 * Descriptions : C source file.
 * Remarks      : Portable version which supports VS2013 PREVIEW & VS2012 & VS2010 and Linux platform
 *                For Linux need to add '-lrt -pthread' to linker to link with the multi-thread & time lib
 *
 */

=========================================
					|
 CloudProbe version 4  Bonus part	|
					|
=========================================

The bonus parts we have done are listed following, the corresponding file is CloudProbe_bonus.c

1. Persistent HTTP connection.(which is also used in CloudProbe.c)
2. Parallel HTTP connection. (used in Cloud_parallelget() and Cloud_parallelpost()) 