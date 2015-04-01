/*
 * File         : CloudProbe.c
 * System       : Generic System
 * Version      : 04.00
 * Version Date : 12-11-13
 * Designer     : Wang Cheng
 * Programmer   : Wang Cheng
 * Copyright    : Wang Cheng 1993-2013 All Rights Reserved
 * Descriptions : C source file.
 * Remarks      : Portable version which supports WIN32 and Linux platform
 *                For Linux need to add '-lrt -pthread' to linker to link with the multi-thread & time lib
 *
 */

#define _CRT_SECURE_NO_DEPRECATE
#include "tinycthread.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "md5.h"
#ifdef WIN32 // Windows
#include <io.h>
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <sys/types.h>
#include <direct.h>
typedef long long int64_t;
typedef unsigned long long uint64_t;
WORD wVersionRequested;
WSADATA wsaData;
int err;
#else // Assume Linux
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <netdb.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <dirent.h>
typedef unsigned long DWORD;
#define SOCKET int
#define _chdir(path) chdir(path)
#define _access(path, mode) access(path, mode)
#define _mkdir(path) mkdir(path, S_IRWXG)
#define _getcwd(buf, size) getcwd(buf, size)
#define SOCKET_ERROR -1
#define WSAGetLastError() (errno)
#define closesocket(s) close(s)
#define ioctlsocket ioctl
#define WSAEWOULDBLOCK EWOULDBLOCK
#define Sleep(t) usleep((t)*1000)
#endif
#define MAXCONN 10000

typedef struct TPOOL_TASK
{
	void* (*Cloud_tpthread)(void*);
	void* arg;
	struct TPOOL_TASK* next;
} tpooltask;

typedef struct tpool_t
{
    tpooltask *firsttask;
    mtx_t lock;
	cnd_t ready;
	int	threadnum;
	thrd_t *threadpool;
} Tpool;

typedef struct MyFilelist
{
	char name[100];
	long writetime;
	char hash[35];
	int flag;
	long size;
} Mylist;

Tpool *tpool = NULL;
thrd_t thrdpool, thrdlist[50];
mtx_t gMutex, clientlock, parallellock;
cnd_t parallelover;
int updating = 0, taskover = 0, filenum;
SOCKET so_cloudc, so_cloudm;
FILE *fpu = NULL;
Mylist getlist[50], postlist[50];
char serverip[50] = {0}, mycookie[50] = {0};


static void *Cloud_tpthread(void *);
static int Cloud_pool(void *);
static int Cloud_servertp(char **);
static int Cloud_Menu();
static int Cloud_addwork(void* (*Cloud_tpthread)(void *), void *arg);
static int Cloud_isvalidhostname(char *); 
static int Cloud_isvalidnum(char * );
static int Cloud_isvalidip(char * );
static int Cloud_ishostname(char *);
static int Cloud_chkcookie(char *);
static int Cloud_valic(char **);
static int Cloud_locallist(Mylist *);
static int Cloud_client(char **);
static int Cloud_servertp(char **);
static int Cloud_serveron(char **);
static int Cloud_onthread(void *);
static int Cloud_newGet(char *, char *, char *);
static int Cloud_deli(char *);
static int Cloud_reply(char *, long, char *, char *);
static int Cloud_getlist(Mylist *, char *);
static int Cloud_processlist(Mylist *, Mylist *, int, int, int);
static int Cloud_initlist(Mylist *);
static int Cloud_send(SOCKET , char *, long);
static int Cloud_recvBigfile(SOCKET , char* , long);
static int Cloud_newPost(char *, long, char *, char *, char *, char *);
static int Cloud_trigUpdate(SOCKET ,char *, long, long);
static int Cloud_trigRemove(SOCKET ,char *);
static int Cloud_getpara(char *, char *);
#ifdef WIN32
static int Cloud_monitor(void *);
static int Cloud_parallelget(void *);
static int Cloud_parallelpost(void *);

static int 
Cloud_parallelget(void * ii)
{
	struct sockaddr_in cloudsaddr;
	SOCKET s;
	int error;
	int i = (int)ii, tmp, iResult;
	char *filebuf, path[100], fileurl[100], httpget[1000], *ptr;
	FILE *fp;
	
	s = socket(AF_INET, SOCK_STREAM, 0);

	memset(&cloudsaddr, 0, sizeof(cloudsaddr));
	cloudsaddr.sin_family = AF_INET;
	cloudsaddr.sin_addr.S_un.S_addr = inet_addr(serverip);

	cloudsaddr.sin_port = htons(80);
	error = connect(s, (const struct sockaddr *)&cloudsaddr, sizeof(cloudsaddr));
	if (error == SOCKET_ERROR)
		{
			printf("connect() failed.\nfatal error: %d\n", WSAGetLastError());
			closesocket(s);
			return 0;
		}
	filebuf = (char *)malloc(sizeof(char) * (getlist[i].size + 500));
	memset(filebuf, 0, sizeof(char) * (getlist[i].size + 500));
	memset(fileurl, 0, sizeof(char) * 100);
	memset(httpget, 0, sizeof(char) * 1000);
	memset(path, 0, sizeof(char) * 100);
	strcat(path, "cloud");
	Cloud_deli(path);
	strcat(fileurl, "/cloud/");
	strcat(fileurl, getlist[i].name);
	if (getlist[i].flag == 2)
		strcat(path, "Conflict_");
	strcat(path, getlist[i].name);
	printf("%s got\n", path); 
	Cloud_newGet(fileurl, httpget, mycookie);
	Cloud_send(s, httpget, strlen(httpget));
	printf("%s send %d\n", path, s);
	iResult = recv(s, filebuf, getlist[i].size + 500, 0);
	printf("%s receive %d \n", path, s);
	ptr = strstr(filebuf, "\r\n\r\n");
	ptr += 3;
	ptr[0] = '\0';
	tmp = strlen(filebuf) + 1;
	ptr[0] = '\n';
	ptr = filebuf + iResult;
	if (iResult - tmp < getlist[i].size)
		Cloud_recvBigfile(s, ptr, getlist[i].size - (iResult - tmp));
	ptr = strstr(filebuf, "\r\n\r\n");
	if (ptr == NULL)
	{
		mtx_lock(&clientlock);
			++taskover;
			if (taskover == filenum)
				cnd_signal(&parallelover);
		mtx_unlock(&clientlock);
		free(filebuf);
		return 1;
	}
	fp = fopen(path, "wb");		
	ptr = ptr + 4;
	fwrite(ptr, getlist[i].size, 1, fp);
	fclose(fp);
	free(filebuf);
	closesocket(s);
	printf("%s saved %d\n", path, s);
	mtx_lock(&clientlock);
		++taskover;
		if (taskover == filenum)
			cnd_signal(&parallelover);
		printf("%d %d\n", taskover, s);
	mtx_unlock(&clientlock);

	return 1;
}

static int
Cloud_parallelpost(void *ii)
{
	struct sockaddr_in cloudsaddr;
	SOCKET s;
	int error;
	int i = (int)ii, tmp;
	char *filebuf, path[100], fileurl[100], *httppost, chartmp[100], mtime[100];
	FILE *fp;
	
	s = socket(AF_INET, SOCK_STREAM, 0);

	memset(&cloudsaddr, 0, sizeof(cloudsaddr));
	cloudsaddr.sin_family = AF_INET;
	cloudsaddr.sin_addr.S_un.S_addr = inet_addr(serverip);

	cloudsaddr.sin_port = htons(80);
	error = connect(s, (const struct sockaddr *)&cloudsaddr, sizeof(cloudsaddr));
	if (error == SOCKET_ERROR)
		{
			printf("connect() failed.\nfatal error: %d\n", WSAGetLastError());
			closesocket(s);
			return 0;
		}

	memset(chartmp, 0, sizeof(char) * 100);
	filebuf = (char *)malloc(sizeof(char) * (postlist[i].size + 500));
	httppost = (char *)malloc(sizeof(char) * (postlist[i].size + 1000));
	memset(filebuf, 0, sizeof(char) * (postlist[i].size + 500));
	memset(fileurl, 0, sizeof(char) * 100);
	memset(mtime, 0, sizeof(char) * 100);
	memset(httppost, 0, sizeof(char) * (postlist[i].size + 1000));
	memset(path, 0, sizeof(char) * 100);
	strcat(path, "cloud");
	Cloud_deli(path);
	strcat(fileurl, "/cloud/");
	strcat(fileurl, postlist[i].name);
	strcat(path, postlist[i].name);
	printf("%s uploaded\n", path);
	fp = fopen(path, "rb");
	if (fp == NULL)
	{
		mtx_lock(&clientlock);
			++taskover;
			if (taskover == filenum)
				cnd_signal(&parallelover);
		mtx_unlock(&clientlock);

		free(httppost);
		free(filebuf);
		return 1;
	}
	fread(filebuf, postlist[i].size, 1, fp);
	fclose(fp);
	strcpy(mtime, "Last-Modified: ");
	sprintf(chartmp, "%ld\r\n", postlist[i].writetime);
	strcat(mtime, chartmp);
	tmp = Cloud_newPost(filebuf, postlist[i].size, fileurl, httppost, mycookie, mtime);
	Cloud_send(s, httppost, tmp);
	recv(s, filebuf, 500, 0);
	free(httppost);
	free(filebuf);
	closesocket(s);
	mtx_lock(&clientlock);
		++taskover;
		if (taskover == filenum)
			cnd_signal(&parallelover);
	mtx_unlock(&clientlock);
	return 1;
}

static int 
Cloud_monitor(void * path)
{
	char *FilePath = (char *)path;
	CHAR   buffer[1024] = {0}, oldpath[100] = {0};   
    HANDLE hDirectory; 
    int    iRet, mynum, i, res = 0, flag = 0;
	DWORD  lasttime, nexttime;
    BOOL   bRet;
    PFILE_NOTIFY_INFORMATION NotifyInfo = (PVOID)buffer; 
	Mylist mylist[50];
	printf("monitoring\n");

    hDirectory = CreateFileA( FilePath, 
                    FILE_LIST_DIRECTORY,
                    FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, 
                    NULL, 
                    OPEN_EXISTING, 
                    FILE_FLAG_BACKUP_SEMANTICS, 
                    NULL ); 
	printf("monitoring1\n");
    while ( hDirectory != INVALID_HANDLE_VALUE ) 
    { 
		lasttime = nexttime = GetTickCount();
		printf("monitoring2\n");
        RtlZeroMemory( buffer, sizeof(char) * 1000 );
        bRet = ReadDirectoryChangesW( hDirectory, 
                    NotifyInfo, 
                    sizeof(buffer), 
                    TRUE, 
					FILE_NOTIFY_CHANGE_LAST_WRITE|FILE_NOTIFY_CHANGE_CREATION |FILE_NOTIFY_CHANGE_FILE_NAME ,  
                    (PDWORD)&iRet, 
                    NULL, 
                    NULL);
		printf("monitoring3\n");
        if( 1 ) 
        { 
			printf("monitoring4\n");
            while ( TRUE )
            {
                RtlZeroMemory( FilePath, sizeof(char) * 1000 );
                iRet = WideCharToMultiByte( CP_ACP, 
                                    0, 
                                    NotifyInfo->FileName, 
                                    wcslen(NotifyInfo->FileName), 
                                    FilePath, 
                                    MAX_PATH, 
                                    NULL, 
                                    NULL );
                printf("monitoring5\n");
                if( iRet <= 0 )
					break;
                switch( NotifyInfo->Action ) 
                { 
				case FILE_ACTION_ADDED:
					lasttime = GetTickCount();//debouncing.if I drag and drop a file into client side(small file), it will return one ADDED message and two MODIFIED messages.
											  // To omit this distortion, I use this debouncing code.
					 if (updating == 1)
						break;
					Cloud_initlist(mylist);
					mynum = Cloud_locallist(mylist);
					flag = 0;
					for (i = 0; i < mynum; i++)
						if (strncmp(FilePath, mylist[i].name, strlen(mylist[i].name)) == 0)
						{
							flag = 1;
							break;
						}
					if (flag)
						Cloud_trigUpdate(so_cloudm, mylist[i].name, mylist[i].size, mylist[i].writetime);
					printf("%s added!\n", FilePath);
					RtlZeroMemory( FilePath, sizeof(char) * 1000 );
                    break; 
				case FILE_ACTION_MODIFIED:
					nexttime = GetTickCount();
					if (nexttime - lasttime < 500)
						break;
					else
						lasttime = nexttime;
					if (updating == 1)
						break;
					Cloud_initlist(mylist);
					mynum = Cloud_locallist(mylist);
					flag = 0;
					for (i = 0; i < mynum; i++)
						if (strncmp(FilePath, mylist[i].name, strlen(mylist[i].name)) == 0)
						{
							flag = 1;
							break;
						}
					if (flag)
						Cloud_trigUpdate(so_cloudm, mylist[i].name, mylist[i].size, mylist[i].writetime);
					printf("%s modified!\n", FilePath);
					RtlZeroMemory( FilePath, sizeof(char) * 1000 );

                    break; 
                case FILE_ACTION_REMOVED:
					lasttime = GetTickCount();
					Cloud_initlist(mylist);
					mynum = Cloud_locallist(mylist);
					if (updating == 1)
						break;
					Cloud_trigRemove(so_cloudm, FilePath);
					RtlZeroMemory( FilePath, sizeof(char) * 1000 );
                    break; 
                case FILE_ACTION_RENAMED_OLD_NAME: 
					RtlZeroMemory( FilePath, sizeof(char) * 1000 );
					break; 
                case FILE_ACTION_RENAMED_NEW_NAME: 
					RtlZeroMemory( FilePath, sizeof(char) * 1000 );
                    break; 
                default: 
                    break; 
                }

				if ( NotifyInfo->NextEntryOffset == 0 )
                {
                    break;
                }

                (PUCHAR)NotifyInfo = (PUCHAR)NotifyInfo + NotifyInfo->NextEntryOffset ;
            }
        }
    }

    if (  hDirectory != INVALID_HANDLE_VALUE )
    {
        CloseHandle(  hDirectory );
    }
	printf("exit\n");
    return 0;
}
#else
unsigned long GetTickCount()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);

}

#endif

static int
Cloud_trigUpdate(SOCKET s, char* name, long filesize, long writetime)
{
	char *filebuf, *httppost, fileurl[100], path[100], cookie[100], mtime[100], chartmp[100],buf[10000];
	int tmp;

	filebuf = (char *)malloc(sizeof(char) * (filesize + 500));
	httppost = (char *)malloc(sizeof(char) * (filesize + 1000));
	memset(httppost, 0, sizeof(char) * (filesize + 1000));
	memset(filebuf, 0, sizeof(char) * (filesize + 500));
	memset(fileurl, 0, sizeof(char) * 100);
	memset(cookie, 0, sizeof(char) * 100);
	memset(chartmp, 0, sizeof(char) * 100);
	memset(mtime, 0, sizeof(char) * 100);
	memset(path, 0, sizeof(char) * 100);
	Cloud_chkcookie(cookie);
	strcat(path, "cloud");
	Cloud_deli(path);
	strcat(fileurl, "/cloud/");
	strcat(fileurl, name);
	strcat(path, name);
	fpu = NULL;
	fpu = fopen(path, "rb");
	if (fpu == NULL)
	{
		free(httppost);
		free(filebuf);
		return 1;
	}
	else
	{
		fread(filebuf, filesize, 1, fpu);
		fclose(fpu);
	}
	strcpy(mtime, "Last-Modified: ");
	sprintf(chartmp, "%ld\r\n", writetime);
	strcat(mtime, chartmp);
	tmp = Cloud_newPost(filebuf, filesize, fileurl, httppost, cookie, mtime);
	Cloud_send(s, httppost, tmp);
	recv(s, buf, 10000, 0);//receive ACK/NAK,
	free(httppost);
	free(filebuf);
	return 1;
}

static int 
Cloud_trigRemove(SOCKET s, char* name)
{
	char httppost[600], fileurl[100], cookie[100], mtime[100], chartmp[100], buf[5000];
	int tmp;
	time_t writetime;

	writetime = time(NULL);
	memset(mtime, 0, sizeof(char) * 100);
	memset(chartmp, 0, sizeof(char) * 100);
	memset(fileurl, 0, sizeof(char) * 100);
	memset(cookie, 0, sizeof(char) * 100);
	memset(httppost, 0, sizeof(char) * 600);
	Cloud_chkcookie(cookie);
	strcat(fileurl, "/cloud/");
	strcat(fileurl, name);
	strcpy(mtime, "Last-Modified: ");
	sprintf(chartmp, "%ld\r\n", writetime);
	strcat(mtime, chartmp);
	tmp = Cloud_newPost("REMOVE", 0, fileurl, httppost, cookie, mtime);
	Cloud_send(s, httppost, tmp);
	recv(s, buf, 5000, 0);
	return 1;
}

//static int 
//Cloud_trigRename(SOCKET s, char* oldname, char* newname)
//{
//	char httppost[600], fileurl[100], cookie[100], mtime[100], chartmp[100], buf[500];
//	int tmp;
//	time_t writetime;
//
//	writetime = time(NULL);
//	memset(mtime, 0, sizeof(char) * 100);
//	memset(chartmp, 0, sizeof(char) * 100);
//	memset(fileurl, 0, sizeof(char) * 100);
//	memset(cookie, 0, sizeof(char) * 100);
//	memset(httppost, 0, sizeof(char) * 600);
//	Cloud_chkcookie(cookie);
//	strcat(fileurl, "/cloud/");
//	strcat(fileurl, oldname);
//
//	strcpy(mtime, "Last-Modified: ");
//	sprintf(chartmp, "%ld\r\n", writetime);
//	strcat(mtime, chartmp);
//
//	memset(chartmp, 0, sizeof(char) * 100);
//	strcpy(chartmp, "RENAME\t");
//	strcat(chartmp, newname);
//	tmp = Cloud_newPost(chartmp, 0, fileurl, httppost, cookie, mtime);
//	Cloud_send(s, httppost, tmp);
//	recv(s, buf, 500, 0);
//	return 1;
//
//}

static int 
Cloud_client(char ** argv)
{
#ifdef WIN32
	thrd_t t;
	struct sockaddr_in cloudsaddr;
	int error, flag, i, mynum, servernum, firstconn = 1, number, res = 0;
	FILE *fp;
	char buffer[1000] = {0}, httpget[1000], cookie[30], fileurl[100], listurl[100], sendurl[100], recvbuf[50000], *ptr = NULL, path[100];
	Mylist mylist[50], serverlist[50];
	
	so_cloudc = socket(AF_INET, SOCK_STREAM, 0);

	memset(&cloudsaddr, 0, sizeof(cloudsaddr));
	cloudsaddr.sin_family = AF_INET;
	cloudsaddr.sin_addr.S_un.S_addr = inet_addr(argv[2]);

	cloudsaddr.sin_port = htons(80);
	error = connect(so_cloudc, (const struct sockaddr *)&cloudsaddr, sizeof(cloudsaddr));
	if (error == SOCKET_ERROR)
		{
			printf("connect() failed.\nfatal error: %d\n", WSAGetLastError());
			closesocket(so_cloudc);
			return 0;
		}

	strcpy(serverip, argv[2]);
	so_cloudm = socket(AF_INET, SOCK_STREAM, 0);

	memset(&cloudsaddr, 0, sizeof(cloudsaddr));
	cloudsaddr.sin_family = AF_INET;
	cloudsaddr.sin_addr.S_un.S_addr = inet_addr(argv[2]);

	cloudsaddr.sin_port = htons(80);
	memset(cookie, 0, sizeof(char) * 30);
	strcpy(buffer, argv[3]);
	Cloud_deli(buffer);
	strcat(buffer, "cloud");
	Cloud_deli(buffer);
	cloudsaddr.sin_port = htons(80);
	error = connect(so_cloudm, (const struct sockaddr *)&cloudsaddr, sizeof(cloudsaddr));
	if (error == SOCKET_ERROR)
		{
			printf("connect() failed.\nfatal error: %d\n", WSAGetLastError());
			closesocket(so_cloudm);
			return 0;
		}
	flag = Cloud_chkcookie(cookie);
	for ( ; ; )
	{
		mtx_lock(&clientlock);
			updating = 1;
		mtx_unlock(&clientlock);
		memset(path, 0, sizeof(char) * 100);
		memset(fileurl, 0, sizeof(char) * 100);
		memset(listurl, 0, sizeof(char) * 100);
		memset(httpget, 0, sizeof(char) * 1000);
		memset(sendurl, 0, sizeof(char) * 100);
		memset(recvbuf, 0, sizeof(char) * 50000);

		Cloud_initlist(getlist);
		Cloud_initlist(postlist);
		Cloud_deli(listurl);
		strcat(sendurl, "/synlist.txt");
		strcat(listurl,"synlist.txt");
		if (flag)
			Cloud_newGet(sendurl, httpget, cookie);
		else
			Cloud_newGet(sendurl, httpget, NULL);
		Cloud_send(so_cloudc, httpget, strlen(httpget));
		printf("http request sending out\n");
		printf("Waiting for reply\n");
		recv(so_cloudc, recvbuf, 20000, 0);
		if ((ptr = strstr(recvbuf, "Set-Cookie")) != NULL)
		{
			number = Cloud_getpara(recvbuf, "Set-Cookie: ");
			strcat(cookie, "Cookie: ");
			sprintf(cookie + 9, "%d\r\n", number);
			fp = fopen("cookie.txt", "wb");
			fprintf(fp, "%d\n", cookie);
			fclose(fp);
		}
		strcpy(cookie, mycookie);
		printf("reply received!\n");
		ptr = strstr(recvbuf, "\r\n\r\n");
		if (ptr == NULL)
			continue;
		ptr += 4;
		strcpy(httpget, ptr);
		Cloud_initlist(mylist);
		Cloud_initlist(serverlist);
		mynum = Cloud_locallist(mylist);
		servernum = Cloud_getlist(serverlist, httpget);
		for (i = 0; i < mynum; i++)
			printf("%d\t%s\t%s\t%ld\t%ld\t\n", mylist[i].flag, mylist[i].hash, mylist[i].name, mylist[i].size, mylist[i].writetime);
		for (i = 0; i < servernum; i++)
			printf("%d\t%s\t%s\t%ld\t%ld\t\n", serverlist[i].flag, serverlist[i].hash, serverlist[i].name, serverlist[i].size, serverlist[i].writetime);
		mynum = Cloud_processlist(serverlist, mylist, servernum, mynum, flag);
		filenum = 0;
		for (i = 0; i < mynum; i++)//parallel get
			if (mylist[i].flag == 1 || mylist[i].flag == 2)
			{
				strcpy(getlist[filenum].name, mylist[i].name);
				getlist[filenum].writetime = mylist[i].writetime;
				strcpy(getlist[filenum].hash, mylist[i].hash);
				getlist[filenum].flag = 1;
				getlist[filenum].size = mylist[i].size;
				filenum++;
			}
		if (filenum > 0)
		{			
			mtx_lock(&clientlock);
				taskover = 0;
			mtx_unlock(&clientlock);

			for (i = 0; i < filenum; i++)
				if (thrd_create(&thrdlist[i], Cloud_parallelget, (void *)i) != thrd_success)
				{
					printf("Thread creating failed\n");
					closesocket(so_cloudc);
					exit(1);
				}
			cnd_wait(&parallelover, &parallellock);
		}
			
		printf("parallel get over!");
		filenum = 0;

		for (i = 0; i < mynum; i++)//parallel get
			if (mylist[i].flag == 0)
			{
				strcpy(postlist[filenum].name, mylist[i].name);
				postlist[filenum].writetime = mylist[i].writetime;
				strcpy(postlist[filenum].hash, mylist[i].hash);
				postlist[filenum].flag = 1;
				postlist[filenum].size = mylist[i].size;
				filenum++;
			}
		if (filenum > 0)
		{			
			mtx_lock(&clientlock);
				taskover = 0;
			mtx_unlock(&clientlock);

			for (i = 0; i < filenum; i++)
				if (thrd_create(&thrdlist[i], Cloud_parallelpost, (void *)i) != thrd_success)
				{
					printf("Thread creating failed\n");
					closesocket(so_cloudc);
					exit(1);
				}
			cnd_wait(&parallelover, &parallellock);
		}
			
		printf("parallel post over!");

		for (i = 0; i < mynum; i++)
		{
			if (mylist[i].flag == 4)
			{
				memset(path, 0, sizeof(char) * 100);
				strcat(path, "cloud");
				Cloud_deli(path);
				strcat(path, mylist[i].name);
				printf("%s removed\n", path);
				remove(path);
			}
	
		}
		Cloud_initlist(mylist);
		mynum = Cloud_locallist(mylist);
		mtx_lock(&clientlock);
			updating = 0;
		mtx_unlock(&clientlock);
		if (firstconn == 1)
			if (thrd_create(&t, Cloud_monitor, (void *)buffer) != thrd_success)
			{
				printf("Thread creating failed\n");
				closesocket(so_cloudc);
				exit(1);
			}
			else
				printf("started!\n");
		firstconn = 0;
		Sleep(10000);
	}
#endif
	return 1;
}

static int 
Cloud_send(SOCKET s, char * httpget, long filesize)
{
	int iResult, byte_left, y;
	iResult = 0;
	byte_left = y = filesize;

	while (byte_left > 0)
	{
		iResult = send( s, httpget - byte_left + y, byte_left, 0);
		if (iResult < 0)
		{
			closesocket(s);
			printf("send() failed.\nfatal error: %d\n", WSAGetLastError());
			return 0;
		}
		byte_left -= iResult;
	}
	return 1;
}

static int
Cloud_recvBigfile(SOCKET s, char* recvbuf, long size)
{
	int iResult, byte_left, y;
	iResult = 0;
	byte_left = y = size;
	while (byte_left > 5)
	{
		iResult = recv( s, recvbuf - byte_left + y, byte_left, 0);
		if (iResult < 0)
		{
			closesocket(s);
			printf("recv() failed.\nfatal error: %d\n", WSAGetLastError());
		}
		byte_left -= iResult;
	}
	return 0;
}

static void *
Cloud_tpthread(void * ii)
{
	char *reply, *filebuf, recv_buf[200000]={0};
	FILE* fp;
	char *filepath, newfile[100], setcookie[100], path[100], *ptr, *ptr1, newpath[100];
	long s, filesize, i, iResult, headersize, tmp, mtime, flag, j;
	int cookie, isset, mynum;
	Mylist mylist[50];

    #ifdef WIN32
    #else
        pthread_detach(pthread_self());
    #endif
    s = (int)ii;
	memset(newfile, 0, sizeof(char) * 100);
	memset(setcookie, 0, sizeof(char) * 100);
	for ( ; ; )
	{
		memset(recv_buf, 0 ,sizeof(char) * 200000);
		iResult = 0;
		while (iResult < 50)
			iResult = recv(s, recv_buf, 200000, 0);
		Cloud_initlist(mylist);
		mynum = Cloud_locallist(mylist);
		ptr = strstr(recv_buf, "Cookie");
		if (ptr == NULL)
			isset = 0;
		else
			isset = 1;
		if (strlen(recv_buf) == 0)
		{
			closesocket(s);
			return NULL;
		}
		if (recv_buf[0] == 'G')
		//Process packet type 1 & 3
		{
			j = 0;
			while (recv_buf[j] != '\r')
			{
				if (recv_buf[j] == ' ' && j < 5)
				{
					recv_buf[j] = '\0';
					filepath = &recv_buf[j+1];
				}
				j++;
			}
			ptr1 = strstr(filepath, " HTTP");
			ptr1[0] = '\0';
			if (strcmp(filepath, "/synlist.txt") == 0)
			{
			//Process packet type 1
				if (!isset)
				{
					memset(setcookie, 0, sizeof(char) * 100);
					cookie = time(NULL) % 239641;
					strcat(setcookie, "Set-Cookie: ");
					sprintf(setcookie + 12, "%d\r\n", cookie);
					fp = fopen("cookies.txt", "a");
					fprintf(fp, "%d\r\n", cookie);
					fclose(fp);
				}
				fp = fopen("synlist.txt", "rb");
				for (i = 0; i < 100; i++)
				{
					if (fp != NULL)
						break;
					else
					{
						Sleep(100);
						fp = fopen(path, "rb");
					} 
				}
				if (fp == NULL)
				{
					closesocket(s);
					exit(1);
				}
				fseek(fp ,0 ,SEEK_END);
				filesize = ftell(fp);
				fseek(fp ,0 ,SEEK_SET);
				filebuf = (char *)malloc(sizeof(char) * (filesize + 500));
				reply = (char *)malloc(sizeof(char) * (filesize + 1000));
				memset(filebuf, 0, sizeof(char) * (filesize + 500));
				memset(reply, 0, sizeof(char) * (filesize + 1000));
				fread(filebuf, filesize, 1, fp);
				fclose(fp);
				if (!isset)
					headersize = Cloud_reply(filebuf, filesize, setcookie, reply);
				else
					headersize = Cloud_reply(filebuf, filesize, NULL, reply);
				free(filebuf);
				printf("%s", reply);
				Cloud_send(s, reply, headersize);
				printf("reply sending out\n");
				free(reply);

			}
			else
			{
			//Process packet 3
				memset(path, 0, sizeof(char) * 100);
				filepath = filepath + 7;
				strcat(path, "cloud");
				Cloud_deli(path);
				strcat(path, filepath);
				fp = fopen(path, "rb");
				if (fp == NULL)
					continue;
				fseek(fp ,0 ,SEEK_END);
				filesize = ftell(fp);
				fseek(fp ,0 ,SEEK_SET);
				filebuf = (char *)malloc(sizeof(char) * (filesize + 500));
				reply = (char *)malloc(sizeof(char) * (filesize + 1000));
				memset(filebuf, 0, sizeof(char) * (filesize + 500));
				memset(reply, 0, sizeof(char) * (filesize + 1000));
				fread(filebuf, filesize, 1, fp);
				fclose(fp);
				headersize = Cloud_reply(filebuf, filesize, NULL, reply);
				printf("%s", reply);
				Cloud_send(s, reply, headersize);
				printf("reply sending out\n");
				free(filebuf);
				free(reply);
			}
		}
		else
		{
		//Process packet 4, 5 & 6
			memset(path, 0, sizeof(char) * 100);
			filesize = Cloud_getpara(recv_buf, "Content-Length: ");
			mtime = Cloud_getpara(recv_buf, "Last-Modified: ");
			ptr = strstr(recv_buf, "\r\n\r\n");
			ptr = ptr + 4;
			if (filesize == 0 && ptr[0] == 'R')
			{
		    //Process packet 5 & 6
				reply = (char *)malloc(sizeof(char) * 1000);
				memset(reply, 0, sizeof(char) * 1000);
				j = 0;
				while (recv_buf[j] != '\r')
				{
					if (recv_buf[j] == ' ' && j < 5)
					{
						recv_buf[j] = '\0';
						filepath = &recv_buf[j+1];
					}
					j++;
				}
				ptr1 = strstr(filepath, " HTTP");
				ptr1[0] = '\0';
				filepath = filepath + 7;
				strcat(path, "cloud");
				Cloud_deli(path);
				strcat(path, filepath);

				flag = 0;
				for (i = 0; i < mynum; i++)
				{
					if (strcmp(filepath, mylist[i].name) == 0)
					{
						flag = 1;
						break;
					}
				}
				if (strncmp(ptr, "REMOVE", 6) == 0)
				{
					if (flag)
						remove(path);
				}
				else
					if (flag && mylist[i].writetime < mtime)
					{
						memset(newpath, 0, sizeof(char) * 100);
						ptr = ptr + 7;
						strcat(newpath, "cloud");
						Cloud_deli(newpath);
						strcat(newpath, ptr);
						rename(path, newpath);
					}
				headersize = Cloud_reply("ACK", 3, NULL, reply);
				Cloud_send(s, reply, headersize);
				printf("reply sending out\n");
				free(reply);
			}
			else
			{
		    //Process packet 4
				filebuf = (char *)malloc(sizeof(char) * (filesize + 500));
				reply = (char *)malloc(sizeof(char) * (filesize + 1000));
				memset(filebuf, 0, sizeof(char) * (filesize + 500));
				memset(reply, 0, sizeof(char) * (filesize + 1000));
				memcpy(filebuf, recv_buf, iResult);
				ptr = strstr(filebuf, "\r\n\r\n");
				ptr += 3;
				ptr[0] = '\0';
				tmp = strlen(filebuf) + 1;
				ptr[0] = '\n';
				ptr = filebuf + iResult;
				if (iResult - tmp < filesize)
					Cloud_recvBigfile(s, ptr, filesize - (iResult - tmp));
				j = 0;
				while (recv_buf[j] != '\r')
				{
					if (recv_buf[j] == ' ' && j < 5)
					{
						recv_buf[j] = '\0';
						filepath = &recv_buf[j+1];
					}
					j++;
				}
				ptr1 = strstr(filepath, " HTTP");
				ptr1[0] = '\0';
				filepath = filepath + 7;
				strcat(path, "cloud");
				Cloud_deli(path);
				strcat(path, filepath);
				fp = fopen(path, "wb");
				for (i = 0; i < 100; i++)
				{
					if (fp != NULL)
						break;
					else
					{
						Sleep(100);
						fp = fopen(path, "wb");
					} 
				}
				if (fp != NULL)
				{
					for (i = 0; i < mynum; i++)
						if (strcmp(filepath, mylist[i].name) == 0)
							break;
					fwrite(filebuf + tmp, filesize, 1, fp);
					fclose(fp);
					headersize = Cloud_reply("ACK", 3, NULL, reply);
				}
				else
				{
					headersize = Cloud_reply("NAK", 3, NULL, reply);
				}
				Cloud_send(s, reply, headersize);
				printf("reply sending out\n");
				free(filebuf);
				free(reply);
			}
		}
	}
	return 0;
}

static int 
Cloud_onthread(void *ii)
{
	char *reply, *filebuf, recv_buf[200000]={0};
	FILE* fp;
	char *filepath, newfile[100], setcookie[100], path[100], *ptr, *ptr1, newpath[100];
	long s, filesize, i, iResult, headersize, tmp, mtime, flag, j;
	int cookie, isset, mynum;
	Mylist mylist[50];

    #ifdef WIN32
    #else
        pthread_detach(pthread_self());
    #endif
    s = (int)ii;
	memset(newfile, 0, sizeof(char) * 100);
	memset(setcookie, 0, sizeof(char) * 100);
	for ( ; ; )
	{
		memset(recv_buf, 0 ,sizeof(char) * 200000);
		iResult = 0;
		while (iResult < 50)
			iResult = recv(s, recv_buf, 200000, 0);
		Cloud_initlist(mylist);
		mynum = Cloud_locallist(mylist);
		ptr = strstr(recv_buf, "Cookie");
		if (ptr == NULL)
			isset = 0;
		else
			isset = 1;
		if (strlen(recv_buf) == 0)
		{
			closesocket(s);
			return 1;
		}
		if (recv_buf[0] == 'G')
		//Process packet type 1 & 3
		{
			j = 0;
			while (recv_buf[j] != '\r')
			{
				if (recv_buf[j] == ' ' && j < 5)
				{
					recv_buf[j] = '\0';
					filepath = &recv_buf[j+1];
				}
				j++;
			}
			ptr1 = strstr(filepath, " HTTP");
			ptr1[0] = '\0';
			if (strcmp(filepath, "/synlist.txt") == 0)
			{
			//Process packet type 1
				if (!isset)
				{
					memset(setcookie, 0, sizeof(char) * 100);
					cookie = time(NULL) % 239641;
					strcat(setcookie, "Set-Cookie: ");
					sprintf(setcookie + 12, "%d\r\n", cookie);
					fp = fopen("cookies.txt", "a");
					fprintf(fp, "%d\r\n", cookie);
					fclose(fp);
				}
				fp = fopen("synlist.txt", "rb");
				for (i = 0; i < 100; i++)
				{
					if (fp != NULL)
						break;
					else
					{
						Sleep(100);
						fp = fopen(path, "rb");
					} 
				}
				if (fp == NULL)
				{
					closesocket(s);
					exit(1);
				}
				fseek(fp ,0 ,SEEK_END);
				filesize = ftell(fp);
				fseek(fp ,0 ,SEEK_SET);
				filebuf = (char *)malloc(sizeof(char) * (filesize + 500));
				reply = (char *)malloc(sizeof(char) * (filesize + 1000));
				memset(filebuf, 0, sizeof(char) * (filesize + 500));
				memset(reply, 0, sizeof(char) * (filesize + 1000));
				fread(filebuf, filesize, 1, fp);
				fclose(fp);
				if (!isset)
					headersize = Cloud_reply(filebuf, filesize, setcookie, reply);
				else
					headersize = Cloud_reply(filebuf, filesize, NULL, reply);
				free(filebuf);
				printf("%s", reply);
				Cloud_send(s, reply, headersize);
				printf("reply sending out\n");
				free(reply);

			}
			else
			{
			//Process packet 3
				memset(path, 0, sizeof(char) * 100);
				filepath = filepath + 7;
				strcat(path, "cloud");
				Cloud_deli(path);
				strcat(path, filepath);
				fp = fopen(path, "rb");
				if (fp == NULL)
					continue;
				fseek(fp ,0 ,SEEK_END);
				filesize = ftell(fp);
				fseek(fp ,0 ,SEEK_SET);
				filebuf = (char *)malloc(sizeof(char) * (filesize + 500));
				reply = (char *)malloc(sizeof(char) * (filesize + 1000));
				memset(filebuf, 0, sizeof(char) * (filesize + 500));
				memset(reply, 0, sizeof(char) * (filesize + 1000));
				fread(filebuf, filesize, 1, fp);
				fclose(fp);
				headersize = Cloud_reply(filebuf, filesize, NULL, reply);
				printf("%s", reply);
				Cloud_send(s, reply, headersize);
				printf("reply sending out\n");
				free(filebuf);
				free(reply);
			}
		}
		else
		{
		//Process packet 4, 5 & 6
			memset(path, 0, sizeof(char) * 100);
			filesize = Cloud_getpara(recv_buf, "Content-Length: ");
			mtime = Cloud_getpara(recv_buf, "Last-Modified: ");
			ptr = strstr(recv_buf, "\r\n\r\n");
			ptr = ptr + 4;
			if (filesize == 0 && ptr[0] == 'R')
			{
		    //Process packet 5 & 6
				reply = (char *)malloc(sizeof(char) * 1000);
				memset(reply, 0, sizeof(char) * 1000);
				j = 0;
				while (recv_buf[j] != '\r')
				{
					if (recv_buf[j] == ' ' && j < 5)
					{
						recv_buf[j] = '\0';
						filepath = &recv_buf[j+1];
					}
					j++;
				}
				ptr1 = strstr(filepath, " HTTP");
				ptr1[0] = '\0';
				filepath = filepath + 7;
				strcat(path, "cloud");
				Cloud_deli(path);
				strcat(path, filepath);

				flag = 0;
				for (i = 0; i < mynum; i++)
				{
					if (strcmp(filepath, mylist[i].name) == 0)
					{
						flag = 1;
						break;
					}
				}
				if (strncmp(ptr, "REMOVE", 6) == 0)
				{
					if (flag)
						remove(path);
				}
				else
					if (flag && mylist[i].writetime < mtime)
					{
						memset(newpath, 0, sizeof(char) * 100);
						ptr = ptr + 7;
						strcat(newpath, "cloud");
						Cloud_deli(newpath);
						strcat(newpath, ptr);
						rename(path, newpath);
					}
				headersize = Cloud_reply("ACK", 3, NULL, reply);
				Cloud_send(s, reply, headersize);
				printf("reply sending out\n");
				free(reply);
			}
			else
			{
		    //Process packet 4
				filebuf = (char *)malloc(sizeof(char) * (filesize + 500));
				reply = (char *)malloc(sizeof(char) * (filesize + 1000));
				memset(filebuf, 0, sizeof(char) * (filesize + 500));
				memset(reply, 0, sizeof(char) * (filesize + 1000));
				memcpy(filebuf, recv_buf, iResult);
				ptr = strstr(filebuf, "\r\n\r\n");
				ptr += 3;
				ptr[0] = '\0';
				tmp = strlen(filebuf) + 1;
				ptr[0] = '\n';
				ptr = filebuf + iResult;
				if (iResult - tmp < filesize)
					Cloud_recvBigfile(s, ptr, filesize - (iResult - tmp));
				j = 0;
				while (recv_buf[j] != '\r')
				{
					if (recv_buf[j] == ' ' && j < 5)
					{
						recv_buf[j] = '\0';
						filepath = &recv_buf[j+1];
					}
					j++;
				}
				ptr1 = strstr(filepath, " HTTP");
				ptr1[0] = '\0';
				filepath = filepath + 7;
				strcat(path, "cloud");
				Cloud_deli(path);
				strcat(path, filepath);
				fp = fopen(path, "wb");
				for (i = 0; i < 100; i++)
				{
					if (fp != NULL)
						break;
					else
					{
						Sleep(100);
						fp = fopen(path, "wb");
					} 
				}
				if (fp != NULL)
				{
					for (i = 0; i < mynum; i++)
						if (strcmp(filepath, mylist[i].name) == 0)
							break;
					fwrite(filebuf + tmp, filesize, 1, fp);
					fclose(fp);
					headersize = Cloud_reply("ACK", 3, NULL, reply);
				}
				else
				{
					headersize = Cloud_reply("NAK", 3, NULL, reply);
				}
				Cloud_send(s, reply, headersize);
				printf("reply sending out\n");
				free(filebuf);
				free(reply);
			}
		}
	}
}

static int 
Cloud_getpara(char* header, char* option)
{
	char number[20]={0};
	char *ptr;
	int i, y;
	ptr = strstr(header, option);
	ptr = ptr + strlen(option) - 2;
	i = 0;
	y = 0;
	while (ptr[i] != '\r')
	{
		if (ptr[i] >= '0' && ptr[i] <= '9')
		{
			number[y] = ptr[i];
			y++;
		}
		i++;
	}
	if (number[0] == '\0')
		return 0;
	else
		return atoi(number);
}

static int
Cloud_locallist(Mylist mylist[])
{
#ifdef WIN32
	FILE *fp;
	FILE *fp1;
	int i = 0, j = 0;
	long handle;
	char str1[33];
	char str2[10];
	struct _finddata_t FILEinfo;
	unsigned char* fcon;
	char path[100]={0};
	char cloudpath[100]={0};
	unsigned char fhash[50]={0};
	MD5_CTX md5;
	

	char cloud[100]="\\cloud\\*.*";

	if (_access("cloud", 0) == -1)
		_mkdir("cloud");
	while ((fp1 = fopen("synlist.txt", "w")) == NULL)
		Sleep(50);
	_getcwd(path, sizeof(char) * 100);
	strcat(path, cloud);
	strcpy(cloudpath, path);
	if ((handle = (_findfirst(path, &FILEinfo))) != -1)	
	{
		_findnext(handle, &FILEinfo);
		while (_findnext(handle, &FILEinfo) == 0)
		{
			strcpy(path, cloudpath);
			path[strlen(path) - 3] = '\0';
			strcat(path, FILEinfo.name);

			fp = fopen(path, "rb");
			if (fp == NULL)
				continue;
			strcpy(mylist[j].name, FILEinfo.name);
			mylist[j].writetime = (long)FILEinfo.time_write;
			fprintf(fp1, "%s\t\t%ld\t\t", FILEinfo.name, FILEinfo.time_write);
			strcpy(path, cloudpath);
			path[strlen(path) - 3] = '\0';
			strcat(path, FILEinfo.name);
			fcon = (unsigned char *)malloc(sizeof(char) * (FILEinfo.size + 1));
			memset(fcon, 0, sizeof(char) * (FILEinfo.size + 1));
			fread(fcon, FILEinfo.size, 1, fp);
			fclose(fp);
			MD5Init(&md5);         		
			MD5Update(&md5,fcon,strlen((char *)fcon));
			MD5Final(&md5,fhash);        
			strcpy(str1,"");
			for(i = 0; i < 16; i++)
			{
				sprintf(str2, "%02x", fhash[i] & 0x0ff);
				strcat(str1, str2);
			}
			mylist[j].size = FILEinfo.size;
			fprintf(fp1, "%ld\t\t%s\t\t", mylist[j].size, str1);
			strcpy(mylist[j].hash, str1);
			free(fcon);
			j++;
		}
		_findclose(handle);
		fclose(fp1);
	}

	return j;
#else
	 DIR *dp;
    struct dirent *entry;
    struct stat statbuf;
    char path[100], cloudpath[100], newpath[100];
    char str1[33];
	char str2[10];
    FILE* fp, *fp1;
    int i, j = 0;
    unsigned char fhash[50]={0};
    unsigned char* fcon;
	MD5_CTX md5;

    memset(path, 100, sizeof(char) * 100);
    memset(newpath, 100, sizeof(char) * 100);
    while ((fp1 = fopen("synlist.txt", "w")) == NULL)
        Sleep(50);
    getcwd(path, sizeof(char) * 100);
	strcat(path, "/cloud");
	strcpy(newpath, path);
    if((dp = opendir(path)) == NULL) {
        fprintf(stdout,"cannot open directory: %s\n", path);
        return 1;
    }
    while((entry = readdir(dp)) != NULL)
     {
        strcpy(cloudpath, "cloud/");
        strcat(cloudpath, entry->d_name);
        lstat(cloudpath,&statbuf);
        if(!S_ISDIR(statbuf.st_mode) && strncmp(entry->d_name, ".", 1) && strncmp(entry->d_name, "..", 2))
        {
            memset(fhash, 50, sizeof(char) * 50);
            fprintf(fp1, "%s\t\t",entry->d_name);
            fprintf(fp1, "%ld\t\t%ld\t\t", statbuf.st_mtime, statbuf.st_size);
			strcpy(mylist[j].name, entry->d_name);
			mylist[j].writetime = (long)statbuf.st_mtime;
			mylist[j].size = (long)statbuf.st_size;
            fp = fopen(cloudpath, "rb");
            if (fp == NULL)
                continue;
            fcon = (unsigned char *)malloc(sizeof(char) * (statbuf.st_size + 1));
			memset(fcon, 0, sizeof(char) * (statbuf.st_size + 1));
			fread(fcon, statbuf.st_size, 1, fp);
			fclose(fp);
			MD5Init(&md5);
			MD5Update(&md5,fcon,strlen((char *)fcon));
			MD5Final(&md5,fhash);
			strcpy(str1,"");
			for(i = 0; i < 16; i++)
			{
				sprintf(str2, "%02x", fhash[i] & 0x0ff);
				strcat(str1, str2);
			}
			fprintf(fp1, "%s\t\t", str1);
			strcpy(mylist[j].hash, str1);
			free(fcon);
			j++;
        }

    }
    closedir(dp);
    fclose(fp1);
	return j;

#endif
}

static int
Cloud_initlist(Mylist mylist[])
{
	int i;
	for (i = 0; i < 50; i++)
	{
		memset(mylist[i].name, 0, sizeof(char) * 100);
		mylist[i].writetime = 0;
		memset(mylist[i].hash, 0, sizeof(char) * 35);
		mylist[i].flag = -1;
		mylist[i].size = 0;
	}
	return 1;
}

static int 
Cloud_getlist(Mylist serverlist[], char packet[])
{
	char* item;
	int j = 0;
	item = strtok(packet, "\t\t");

	while (item != NULL)
	{
		strcpy(serverlist[j].name, item);
		printf("%s\t", item);
		item = strtok(NULL, "\t\t");
		if (item != NULL)
			serverlist[j].writetime = atoi(item);
		else
			serverlist[j].writetime = GetTickCount();
		printf("%s\t", item);
		item = strtok(NULL, "\t\t");
		printf("%s\t", item);
		if (item != NULL)
			serverlist[j].size = atoi(item);
		else
			serverlist[j].size = 0;
		item = strtok(NULL, "\t\t");
		printf("%s\t", item);
		strncpy(serverlist[j].hash, item, 32);
		serverlist[j].hash[32] = '\0';
		item = strtok(NULL, "\t\t");
		j++;
	}
	return j;
}

static int 
Cloud_processlist(Mylist serverlist[], Mylist mylist[], int servernum, int mynum, int state)
{
	int i = 0, j = 0, flag = 0;
	char path[50];
	FILE * fp = NULL;
	for (i = 0; i < servernum; i++)
	{
		memset(path, 0, sizeof(char) * 50);
		strcpy(path, "cloud");
		Cloud_deli(path);
		strcat(path, serverlist[i].name);
		if ((flag = _access(path, 0) == -1))
		{
			strcpy(mylist[mynum].name, serverlist[i].name);
			mylist[mynum].writetime = serverlist[i].writetime;
			strcpy(mylist[mynum].hash, serverlist[i].hash);
			mylist[mynum].flag = 1;
			mylist[mynum].size = serverlist[i].size;
			mynum++;
		}
		else
		{
			for (j = 0; j < mynum; j++)
				if (strcmp(mylist[j].name, serverlist[i].name) == 0)
					break;
			if (strcmp(mylist[j].hash, serverlist[i].hash) == 0)
				mylist[j].flag = 3;
			else
			{
				if (mylist[j].writetime < serverlist[i].writetime)
				{
					mylist[j].flag = 1;
					mylist[j].size = serverlist[i].size;
					memset(path, 0, sizeof(char) * 50);
					strcpy(path, "cloud");
					Cloud_deli(path);
					strcat(path, serverlist[i].name);
					fp = fopen(path, "a");
					if (fp == NULL)
						mylist[j].flag = 2;
					else
						fclose(fp);
				}
				else
					mylist[j].flag = 0;		
			}
		}
	}
	for (i = 0; i < mynum; i++)
		if (mylist[i].flag == -1)
			mylist[i].flag = state;
	for (i = 0; i < mynum; i++)
		if (strncmp(mylist[i].name, "Conflict", 8) == 0)
			mylist[i].flag = 3;

	return mynum;
}

static int
Cloud_chkcookie(char cookie[])
{
	FILE *fp;
	char tmp[30];
	fp = fopen("cookie.txt","rb");
	if (fp == NULL)
	{
		fp = fopen("cookie.txt","wb");
		fclose(fp);
		return 0;
	}
	else
	{
		fgets(tmp, 30, fp);
		strcat(cookie, "Cookie: ");
		strcat(cookie, tmp);
		fclose(fp);
		return 4;
	}
}

static int
Cloud_newGet (char* fileurl, char * header, char * cookie)
{
	strcat(header, "GET ");
	strcat(header, fileurl);
	strcat(header, " HTTP/1.1\r\n");
	if (cookie != NULL)
		strcat(header, cookie);
	strcat(header, "Accept: */*\r\n");
	strcat(header, "Connection: close\r\n\r\n");
	return 1;
}

static int 
Cloud_newPost(char* file, long filesize, char* fileurl, char* header, char* cookie, char *mtime)
{
	char *ptr;
	char length[40] = {0};
	int tmp;
	strcat(header, "POST ");
	strcat(header, fileurl);
	strcat(header, " HTTP/1.1\r\n");
	strcat(header, "Content-Type: application/x-www-form-urlencoded\r\n");
	if (cookie != NULL)
		strcat(header, cookie);
	sprintf(length, "Content-Length: %ld\r\n", filesize);
	strcat(header, mtime);
	strcat(header, length);
	strcat(header, "Connection: close\r\n\r\n");
	ptr = &header[strlen(header)];
	tmp = strlen(header) + filesize; 
	if ((filesize == 0)  && (strncmp(file, "REMOVE", 6) == 0))
	{
		memcpy(ptr, file, strlen(file));
		tmp += 7;
	}
	else
		memcpy(ptr, file, filesize);
	return tmp;
}

static int
Cloud_reply(char *file, long filesize, char *setcookie, char *reply)
{
	char length[100];
	char *ptr;
	int tmp;

	memset(length, 0, sizeof(char) * 100);
	strcat(reply, "HTTP/1.1 200 OK\r\nServer: IERG4180_WangCheng\r\nContent-Type: text/plain\r\n");
	if (setcookie != NULL)
		strcat(reply, setcookie);
	if (file != NULL)
	{
		sprintf(length, "Content-Length: %d\r\n", filesize);
		strcat(reply, length);
	}
	strcat(reply, "Connection: close\r\n\r\n");
	tmp = strlen(reply);
	ptr = &reply[strlen(reply)];
	if (file != NULL)
		memcpy(ptr, file, filesize);
	return tmp + filesize;
}

static int
Cloud_serveron(char **argv)
{

	struct sockaddr_in cloudsaddr, clientaddr;
	thrd_t t;
	int s, len;
	SOCKET so_server, so_client;
	so_server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (so_server == SOCKET_ERROR)
	{
		printf("socket() failed.\nfatal error: %d\n", WSAGetLastError());
		return 0;
	}
	so_client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (so_client == SOCKET_ERROR)
	{
		printf("socket() failed.\nfatal error: %d\n", WSAGetLastError());
		return 0;
	}


	memset(&cloudsaddr, 0, sizeof(cloudsaddr));
	cloudsaddr.sin_family = AF_INET;
#ifdef WIN32
	cloudsaddr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");
#else
	cloudsaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
#endif
	cloudsaddr.sin_port = htons(80);

	if (_access("cloud", 0) == -1)
		_mkdir("cloud");
	len = sizeof(struct sockaddr);

	if (bind( so_server, (struct sockaddr *) &cloudsaddr, sizeof(cloudsaddr)) == SOCKET_ERROR)
	{
		printf("bind() failed.\nfatal error: %d\n", WSAGetLastError());
		closesocket(so_server);
		return 1;
	}

	if (listen( so_server, MAXCONN ) == SOCKET_ERROR)
	{
		printf("listen() failed.\nfatal error: %d\n", WSAGetLastError());
		closesocket(so_server);
		return 1;
	}

	for (; ;)
	{

		if ((s = accept( so_server, (struct sockaddr *)&clientaddr, &len)) == SOCKET_ERROR)
		{
			printf("accept() failed.\nfatal error: %d\n", WSAGetLastError());
			closesocket(so_server);
			return 1;
		}
		if (thrd_create(&t, Cloud_onthread, (void *)s) != thrd_success)
		{
			printf("Thread creating failed\n");
			closesocket(so_server);
			exit(1);
		}

	}

}

static int
Cloud_deli(char *url)
{
#ifdef WIN32
	strcat(url, "\\");
#else
	strcat(url, "/");
#endif
	return 1;
}

static int
Cloud_pool(void *arg)
{
	tpooltask *work = NULL;
	for (; ;)
	{
		mtx_lock(&tpool->lock);
		while (tpool->firsttask == NULL)
			cnd_wait(&tpool->ready, &tpool->lock);

		printf("Waiter thread waking up...\n");
		work = tpool->firsttask;
		tpool->firsttask = tpool->firsttask->next;
		mtx_unlock(&tpool->lock);

		work->Cloud_tpthread(work->arg);
		free(work);
	}

	return 0;

}

static int
Cloud_addwork(void* (*Cloud_tpthread)(void *), void *arg)
{
	tpooltask *work, *tmp;

	if (Cloud_tpthread == NULL)
	{
		printf("No function can be created\n");
		return 0;
	}

	work = (tpooltask *)malloc(sizeof(tpooltask));
	if (work == NULL)
	{
		printf("Can't add more task!\n");
		exit(1);
	}

	work->Cloud_tpthread = Cloud_tpthread;
	work->arg = arg;
	work->next = NULL;

	mtx_lock(&tpool->lock);
	tmp = tpool->firsttask;
	if (tmp != NULL)
	{
		while (tmp->next)
			tmp = tmp ->next;
		tmp->next = work;
	}
	else
		tpool->firsttask = work;

	if (cnd_signal(&tpool->ready) != thrd_success)
		printf("Sorry, no spare resources currently\n");

	mtx_unlock(&tpool->lock);

	return 0;

}

static int
Cloud_Menu( )
{
	printf("netprobe_win7.exe [port] [threadmode] [threadnum]\n");
	printf("port: the port for http server.\n");
	printf("threadmode: 'o' means on-demand thread creation mode.\n");
	printf("threadmode: 'p' means thread-pool model, the number of pool should be specified by threadnum.\n");
	printf("threadnum: the number of threads only for thread-pool mode.\n");
	return 0;
}

static int
Cloud_servertp(char **argv)
{
	SOCKET so_server;
	struct sockaddr_in sendaddr, clientaddr;
	int s;
#ifdef WIN32
	int size;
#else
	unsigned int size;
#endif
	if (_access("cloud", 0) == -1)
		_mkdir("cloud");

	printf("\n");
	size = sizeof(struct sockaddr);
	so_server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (so_server == SOCKET_ERROR)
	{
		printf("socket() failed.\nfatal error: %d\n", WSAGetLastError());
		return 0;
	}

	memset(&sendaddr, 0, sizeof(sendaddr));
	sendaddr.sin_family = AF_INET;
#ifdef WIN32
	sendaddr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");
#else
	sendaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
#endif
	sendaddr.sin_port = htons(80);


	if (bind( so_server, (struct sockaddr *) &sendaddr, sizeof(sendaddr)) == SOCKET_ERROR)
	{
		printf("bind() failed.\nfatal error: %d\n", WSAGetLastError());
		closesocket(so_server);
		return 1;
	}

	if (listen( so_server, MAXCONN ) == SOCKET_ERROR)
	{
		printf("listen() failed.\nfatal error: %d\n", WSAGetLastError());
		closesocket(so_server);
		return 1;
	}

	for (; ;)
	{

		if ((s = accept( so_server, (struct sockaddr *)&clientaddr, &size)) == SOCKET_ERROR)
		{
			printf("accept() failed.\nfatal error: %d\n", WSAGetLastError());
			closesocket(so_server);
			return 1;
		}

		Cloud_addwork(Cloud_tpthread, (void *)s);

	}

}

static int
Cloud_isvalidip(char * str)
{
	unsigned long ad;

	ad = inet_addr(str);
	if (ad == INADDR_NONE)
		return 0;
	return 1;
}

static int
Cloud_ishostname(char * str)
{
	int i;
	for ( i = 0; i < (int)strlen(str); i++ )
		if ( ( str[i] >= 'a' && str[i] <= 'z' ) || ( str[i] >= 'A' && str[i] <= 'Z' ) )
			return 1;
	return 0;
}

static int
Cloud_isvalidnum(char * str)
{
	int i;
	int num = atoi(str);
	if (num <= 0 || str[0] == '0')
		return 0;
	for (i = 0; i < (int)strlen(str); i++)
		if (str[i] < '0' || str[i] > '9')
			return 0;
	return 1;
}

static int 
Cloud_isvalidhostname(char * str)
{
	struct hostent *hostinfo;

	if ( ( hostinfo = gethostbyname( str ) ) == NULL)
        return 0;
	return 1;
}

static int
Cloud_valic(char **argv)
{
	struct hostent *hostinfo;
	char** ptr;
	char str[20];
    if (!Cloud_ishostname(argv[2]))
	{
		if (!Cloud_isvalidip(argv[2]))
		{
			printf("The IP you input is invalid.\n");
			Cloud_Menu();
			exit(1);
		}
	}
	else
	{
		if (!Cloud_isvalidhostname(argv[2]))
		{
			printf("The hostname you input is invalid.\n");
			Cloud_Menu();
			exit(1);
		}
		else
		{
			hostinfo = gethostbyname( argv[2] );
			ptr = hostinfo->h_addr_list;
            strcpy(argv[3], inet_ntop(hostinfo->h_addrtype, *ptr, str, sizeof(str)));
		}
	}

	return 1;
}

int
main(int argc,char *argv[ ])
{
	int i;
	srand((unsigned)time(NULL));
	cnd_init(&parallelover);
	mtx_init(&parallellock, mtx_plain);
#ifdef WIN32
	wVersionRequested = MAKEWORD( 1, 1 );
	err = WSAStartup( wVersionRequested, &wsaData );
	if ( err != 0 )
	{
		return 1;
	}

	if ( LOBYTE( wsaData.wVersion ) != 1 ||	HIBYTE( wsaData.wVersion ) != 1 )
	{
		WSACleanup( );
		return 1;
	}
#endif

	if (strlen(argv[1]) != 1)
	{
		printf("The mode you chose is not s or c, but %s\n", argv[1]);
		Cloud_Menu();
		exit(0);
	}
	switch (argv[1][0])
	{
		case 's':
		case 'S':
				printf("CloudProbe for server mode.\n");
				switch (argv[2][0])
				{
					case 'p':
					case 'P':
							printf("Thread pool mode.\n");
							switch (argc)
							{
								case 5:
										_chdir(argv[4]);
										mtx_init(&gMutex, mtx_plain);
										tpool = (Tpool *)calloc(1, sizeof(Tpool));
										if (tpool == NULL)
										{
											printf("Cannot create a thread pool!\n");
											exit(1);
										}
										tpool->threadnum = atoi(argv[3]);
										if (!Cloud_isvalidnum(argv[3]))
										{
											Cloud_Menu();
											exit(1);
										}
										tpool->firsttask = NULL;
										tpool->threadpool = (thrd_t *)malloc(sizeof(thrd_t) * tpool->threadnum);
										cnd_init(&tpool->ready);
										mtx_init(&tpool->lock, mtx_plain);
										if (tpool->threadpool == NULL)
										{
											printf("Cannot create a thread pool!\n");
											exit(1);
										}
										for (i = 0; i < tpool->threadnum; i++)
											if (thrd_create(&tpool->threadpool[i], Cloud_pool, NULL) != thrd_success)
											{
												printf("Can't create a thread pool!\n");
												exit(1);
											}
										Cloud_servertp(argv);
										mtx_destroy(&gMutex);
										break;

								default:
										printf("The number of the parameter in thread-pool mode you input is 4 but %d\n", argc - 1);
										Cloud_Menu();
										break;
							}
							break;
					case 'o':
					case 'O':
							printf("On-demand thread mode\n");
							switch (argc)
							{
								case 4:
										_chdir(argv[3]);
										Cloud_serveron(argv);
										break;
								default:
										printf("The number of the parameter in Cloud server on-demand mode you input should be 3 but %d is given\n", argc - 1);
										Cloud_Menu();
										break;
							}
							break;
					default:
							printf("The mode identifier you chose is not p or o but %s\n", argv[2]);
							Cloud_Menu();
							break;
				}
				break;
		case 'c':
		case 'C':
				printf("CloudProbe for Client mode.\n");
				switch (argc)
				{
					case 4:
						mtx_init(&clientlock, mtx_plain);
						if (Cloud_valic(argv))
							{
								_chdir(argv[3]);
								Cloud_client(argv);
								mtx_destroy(&clientlock);
								break;
							}

					default:
							printf("The number of the parameter in Cloud client mode you input should be 3 but %d is given\n", argc - 1);
							Cloud_Menu();
							break;
				}
				break;

		default:
				printf("The mode identifier you chose is not s or c but %s\n", argv[1]);
				Cloud_Menu();
				break;
	}
#ifdef WIN32
	WSACleanup( );
#endif
	return 0;

}
