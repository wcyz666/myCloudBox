=================================================================================
IMPORTANT:									|
										|
If you modify a file on client side, please note that this operation will       |
										|
be synchronized on other clients in AT MOST 10 SECONDS! IT MAY NOT BE AN        |
										|
IMMEDIATE SYNCHRONIZATION.(But will be an instant synchronization with server)  |
										|
You can see more design details in documentation.docx.				|
										|
Thank you for your attention.							|
										|
										|
=================================================================================

/*
 * File         : CloudProbe.c
 * System       : Generic System
 * Version      : 04.00
 * Version Date : 13-12-22
 * Designer     : Wang Cheng
 * Programmer   : Wang Cheng
 * Copyright    : Wang Cheng 1993-2013 All Rights Reserved
 * Descriptions : C source file.
 * Remarks      : Portable version which supports VS2013 PREVIEW & VS2012 & VS2010 and Linux platform
 *                For Linux need to add '-lrt -pthread' to linker to link with the multi-thread & time lib
 *
 */

=========================================
					|
  	CloudProbe version 4		|
					|
=========================================

This server part can run on both Windows and Linux platform, while the client side only supports in Windows.

We are sorry for not achieving the function of system service.

-------------------------------------------------------------
Windows version (IDE£ºVisual studio 2010/2012/2013 Preview) |
-------------------------------------------------------------

1. Create a win32 console project Netprobe with [empty project] selected.
2. Add CloudProbe.c, tinycthread.c & md5.c into source file
3. Add tinycthread.h & md5.h into header file.
4. Press F5 to build the project.
5. Run a command line terminal and change its directory to the one which contains the exe file.
6. Using the following command to run this program.

========================================================================
For Server:

Command-line Arguments

CloudProbe.exe s [mode] [number of Thread] [Server folder] 

========================================================================

For Client:

Command-line Arguments

CloudProbe.exe s [Server IP] [Client folder] 

=======================================================================

-------------------------------------------------------------------------
Linux version (Distribution: Ubuntu 12.4.1 ||| IDE£ºCode::blocks & Geany)|
-------------------------------------------------------------------------

1. Create a console project named Netprobe with all configuration keeping default.
2. Add CloudProbe.c & tinycthread.c & tinycthread.h & md5.c & md5.h into source file
3. Select [Build] -> [Build Option...] -> [Debug] -> [Linker options].
4. Add "-pthread -lrt" into the textarea with title [other linker option].
5. Build this project and generate executable file.
6. Run a new terminal and change its directory to the one which contains the executable file.
7. Using the following command to run this program.
========================================================================
For Server:

Command-line Arguments

CloudProbe.exe s [mode] [number of Thread] [Server folder] 


========================================================================