/*
 * memtester version 4
 *
 * Very simple but very effective user-space memory tester.
 * Originally by Simon Kirby <sim@stormix.com> <sim@neato.org>
 * Version 2 by Charles Cazabon <charlesc-memtester@pyropus.ca>
 * Version 3 not publicly released.
 * Version 4 rewrite:
 * Copyright (C) 2004-2012 Charles Cazabon <charlesc-memtester@pyropus.ca>
 * Licensed under the terms of the GNU General Public License version 2 (only).
 * See the file COPYING for details.
 *
 */

#define __version__ "4.3.0"

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <cutils/sockets.h>
#include <cutils/log.h>
#include <android/log.h>
#include <pthread.h>

#include "types.h"
#include "sizes.h"
#include "tests.h"

#define EXIT_FAIL_NONSTARTER    0x01
#define EXIT_FAIL_ADDRESSLINES  0x02
#define EXIT_FAIL_OTHERTEST     0x04

#define SOCKET_NAME "memorytester"
static char default_arg[] = "-p 10M";
#define DEF_NUM 5
#define LOG_TAG "memorytester"
#define LOGD(...) ((void)__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__))
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__))

struct test tests[] = {
    { "Random Value", test_random_value },
    { "Compare XOR", test_xor_comparison },
    { "Compare SUB", test_sub_comparison },
    { "Compare MUL", test_mul_comparison },
    { "Compare DIV",test_div_comparison },
    { "Compare OR", test_or_comparison },
    { "Compare AND", test_and_comparison },
    { "Sequential Increment", test_seqinc_comparison },
    { "Solid Bits", test_solidbits_comparison },
    { "Block Sequential", test_blockseq_comparison },
    { "Checkerboard", test_checkerboard_comparison },
    { "Bit Spread", test_bitspread_comparison },
    { "Bit Flip", test_bitflip_comparison },
    { "Walking Ones", test_walkbits1_comparison },
    { "Walking Zeroes", test_walkbits0_comparison },
#ifdef TEST_NARROW_WRITES    
    { "8-bit Writes", test_8bit_wide_random },
    { "16-bit Writes", test_16bit_wide_random },
#endif
    { NULL, NULL }
};

typedef struct
{
    int argc;
    void **argv;
    int socket_fd;
}PARAM;

/* Sanity checks and portability helper macros. */
#ifdef _SC_VERSION
void check_posix_system(void) {
    if (sysconf(_SC_VERSION) < 198808L) {
        LOGD(stderr, "A POSIX system is required.  Don't be surprised if "
            "this craps out.\n");
        LOGD(stderr, "_SC_VERSION is %lu\n", sysconf(_SC_VERSION));
    }
}
#else
#define check_posix_system()
#endif

#ifdef _SC_PAGE_SIZE
int memtester_pagesize(void) {
    int pagesize = sysconf(_SC_PAGE_SIZE);
    if (pagesize == -1) {
        perror("get page size failed");
        exit(EXIT_FAIL_NONSTARTER);
    }
    LOGD("pagesize is %ld\n", (long) pagesize);
    return pagesize;
}
#else
int memtester_pagesize(void) {
    LOGD("sysconf(_SC_PAGE_SIZE) not supported; using pagesize of 8192\n");
    return 8192;
}
#endif

/* Some systems don't define MAP_LOCKED.  Define it to 0 here
   so it's just a no-op when ORed with other constants. */
#ifndef MAP_LOCKED
  #define MAP_LOCKED 0
#endif

/* Function declarations */
void usage(char *me);
void *stop_memtester(void *arg);
void *do_memory_test(void *arg);

/* Global vars - so tests have access to this information */
int use_phys = 0;
off_t physaddrbase = 0;
pthread_t do_memory_test_thread, stop_memtester_thread;

/* Function definitions */
void usage(char *me) {
    LOGD("Usage: %s [-p physaddrbase [-d device]] <mem>[B|K|M|G] [loops]\n",me);
    exit(EXIT_FAIL_NONSTARTER);
}

int cmd_split(char **arg, char *params){
    int num = 0;
    char *word = params;

    if((params == NULL) || (*params == '\0')){
        return 0;
    }
    arg[num] = params;
    LOGD("params:%s\n", params);
    for(word = params; ((word != NULL)&&(*word != '\0')); ++word){
        if(*word == ' '){
            *word = '\0';
            if((*(word+1) != ' ') && (*(word+1) != '\0') && ((word+1) != NULL) && (num < (DEF_NUM*2-1))){
                num++;
                arg[num] = (word+1);
            }
        }
    }
    return (num + 1);
}

int main(int argc, char ** argv) {
    int connect_number = 6;
    int fdListen = -1, new_fd = -1;
    int ret;
    struct sockaddr_un peeraddr;
    socklen_t socklen = sizeof (peeraddr);
    int numbytes;
    char buff[256];
    int key = -1;
    int argcs;
    int len;
    char* cmd;
    char* argvs[20];
    int i = 0;
    PARAM param;

    // get the socket define in init.rc
    fdListen = android_get_control_socket(SOCKET_NAME);
    if(fdListen < 0){
        LOGD("Failed to get socket(%d) %s errno:%d,%s", fdListen, SOCKET_NAME, errno, strerror(errno));
        exit(-1);
    }

    ret = listen(fdListen, connect_number);

    LOGD("Listen result %d",ret);

    if(ret < 0){
         LOGD("listem errno:%d,%s", errno, strerror(errno));
         exit(-1);
    }

    while(1){
        //wait for client
        new_fd = accept(fdListen, (struct sockaddr *) &peeraddr, &socklen);
        LOGD("Accept_fd %d",new_fd);

        if(new_fd < 0){
            LOGD("accept errno:%d,%s", errno, strerror(errno));
            exit(-1);
        }

        if((numbytes = recv(new_fd,buff,sizeof(buff),0))==-1){
            LOGD("recv errno:%d,%s", errno, strerror(errno));
        }

        LOGD("%s", buff);

        LOGD("receive command, do memory test");
        /* if from client parameter is null, use default command */
        if(buff == NULL || *buff == '\0'){
            len = strlen(default_arg);
        }else{
            len = strlen(buff);
        }
        cmd = (char *) malloc(len + 1);
        if(buff == NULL || *buff == '\0'){
            strcpy(cmd, default_arg);
        }else{
            strcpy(cmd, buff);
        }
        argcs = cmd_split(argvs, cmd);
        for(i = 0; i < argcs; i ++){
            LOGD("argvs[%d] = %d\n", i, argvs[i]);
        }
        ret =  pthread_create(&stop_memtester_thread, NULL, stop_memtester, new_fd);
        if(ret != 0){
            LOGD("create stop thread failed!");
            exit(-1);
        }
        param.argc = argcs;
        param.argv = argvs;
        param.socket_fd = new_fd;
        ret = pthread_create(&do_memory_test_thread, NULL, do_memory_test, &param);
        if(do_memory_test_thread != 0){
            pthread_join(do_memory_test_thread, NULL);
            LOGD("memory test thread is finish, so continue..");
        }
        LOGD("memory test is finish, close socket connect ....");
        close(new_fd);
    }
}

void *stop_memtester(void *arg) {
    int client_socket;
    int numbytes;
    int ret = -1;
    char buff[256] = {0};
    client_socket = (int)arg;
    while(1){
        memset(buff, sizeof(buff), 0);
        if((numbytes = recv(client_socket, buff, sizeof(buff), 0))==-1){
            LOGD("recv errno:%d,%s", errno, strerror(errno));
            exit(-1);
        }
        LOGD("receive message : %s\n", buff);
        ret = strcmp(buff, "stop\n");
        LOGD("ret = %d\n", ret);
        if(ret == 0){
            LOGD("receive stop memory test messge, we exit()");
            exit(0);
        }
    }
}

void *do_memory_test(void *arg) {
    int argc, client_socket;
    char **argv;
    PARAM* param = (PARAM *)arg;
    ul loops, loop, i;
    size_t pagesize, wantraw, wantmb, wantbytes, wantbytes_orig, bufsize,
         halflen, count;
    char *memsuffix, *addrsuffix, *loopsuffix;
    ptrdiff_t pagesizemask;
    void volatile *buf, *aligned;
    ulv *bufa, *bufb;
    int do_mlock = 1, done_mem = 0;
    int exit_code = 0;
    int memfd, opt, memshift;
    size_t maxbytes = -1; /* addressable memory, in bytes */
    size_t maxmb = (maxbytes >> 20) + 1; /* addressable memory, in MB */
    /* Device to mmap memory from with -p, default is normal core */
    char *device_name = "/dev/mem";
    struct stat statbuf;
    int device_specified = 0;
    char *env_testmask = 0;
    ul testmask = 0;
    char buffer[4096];
    int start_test_flag = -1;

    argc = param->argc;
    argv = param->argv;
    client_socket = param->socket_fd;
    LOGD("memtester version " __version__ " (%d-bit)\n", UL_LEN);
    memset(buffer, sizeof(buffer), 0);
    sprintf(buffer, "memtester version " __version__ " (%d-bit)\n", UL_LEN);
    send(client_socket, buffer, strlen(buffer), 0);
    LOGD("Copyright (C) 2001-2012 Charles Cazabon.\n");
    memset(buffer, sizeof(buffer), 0);
    sprintf(buffer, "Copyright (C) 2001-2012 Charles Cazabon.\n");
    send(client_socket, buffer, strlen(buffer), 0);
    LOGD("Licensed under the GNU General Public License version 2 (only).\n");
    memset(buffer, sizeof(buffer), 0);
    sprintf(buffer, "Licensed under the GNU General Public License version 2 (only).\n");
    send(client_socket, buffer, strlen(buffer), 0);
    LOGD("\n");
    check_posix_system();
    pagesize = memtester_pagesize();
    pagesizemask = (ptrdiff_t) ~(pagesize - 1);
    LOGD("pagesizemask is 0x%tx\n", pagesizemask);
    memset(buffer, sizeof(buffer), 0);
    sprintf(buffer, "pagesizemask is 0x%tx\n", pagesizemask);
    send(client_socket, buffer, strlen(buffer), 0);
    
    /* If MEMTESTER_TEST_MASK is set, we use its value as a mask of which
       tests we run.
     */
    if (env_testmask = getenv("MEMTESTER_TEST_MASK")) {
        errno = 0;
        testmask = strtoul(env_testmask, 0, 0);
        if (errno) {
            LOGD(stderr, "error parsing MEMTESTER_TEST_MASK %s: %s\n", 
                    env_testmask, strerror(errno));
            usage(argv[0]); /* doesn't return */
        }
        LOGD("using testmask 0x%lx\n", testmask);
    }

    while ((opt = getopt(argc, argv, "p:d:")) != -1) {
        switch (opt) {
            case 'p':
                errno = 0;
                physaddrbase = (off_t) strtoull(optarg, &addrsuffix, 16);
                if (errno != 0) {
                    LOGD(stderr,
                            "failed to parse physaddrbase arg; should be hex "
                            "address (0x123...)\n");
                    usage(argv[0]); /* doesn't return */
                }
                if (*addrsuffix != '\0') {
                    /* got an invalid character in the address */
                    LOGD(stderr,
                            "failed to parse physaddrbase arg; should be hex "
                            "address (0x123...)\n");
                    usage(argv[0]); /* doesn't return */
                }
                if (physaddrbase & (pagesize - 1)) {
                    LOGD(stderr,
                            "bad physaddrbase arg; does not start on page "
                            "boundary\n");
                    usage(argv[0]); /* doesn't return */
                }
                /* okay, got address */
                use_phys = 1;
                break;
            case 'd':
                if (stat(optarg,&statbuf)) {
                    LOGD(stderr, "can not use %s as device: %s\n", optarg, 
                            strerror(errno));
                    usage(argv[0]); /* doesn't return */
                } else {
                    if (!S_ISCHR(statbuf.st_mode)) {
                        LOGD(stderr, "can not mmap non-char device %s\n", 
                                optarg);
                        usage(argv[0]); /* doesn't return */
                    } else {
                        device_name = optarg;
                        device_specified = 1;
                    }
                }
                break;              
            default: /* '?' */
                usage(argv[0]); /* doesn't return */
        }
    }

    if (device_specified && !use_phys) {
        LOGD(stderr, 
                "for mem device, physaddrbase (-p) must be specified\n");
        usage(argv[0]); /* doesn't return */
    }
    
    if (optind >= argc) {
        LOGD(stderr, "need memory argument, in MB\n");
        usage(argv[0]); /* doesn't return */
    }

    errno = 0;
    wantraw = (size_t) strtoul(argv[optind], &memsuffix, 0);
    if (errno != 0) {
        LOGD(stderr, "failed to parse memory argument");
        usage(argv[0]); /* doesn't return */
    }
    switch (*memsuffix) {
        case 'G':
        case 'g':
            memshift = 30; /* gigabytes */
            break;
        case 'M':
        case 'm':
            memshift = 20; /* megabytes */
            break;
        case 'K':
        case 'k':
            memshift = 10; /* kilobytes */
            break;
        case 'B':
        case 'b':
            memshift = 0; /* bytes*/
            break;
        case '\0':  /* no suffix */
            memshift = 20; /* megabytes */
            break;
        default:
            /* bad suffix */
            usage(argv[0]); /* doesn't return */
    }
    wantbytes_orig = wantbytes = ((size_t) wantraw << memshift);
    wantmb = (wantbytes_orig >> 20);
    optind++;
    if (wantmb > maxmb) {
        LOGD(stderr, "This system can only address %llu MB.\n", (ull) maxmb);
        exit(EXIT_FAIL_NONSTARTER);
    }
    if (wantbytes < pagesize) {
        LOGD(stderr, "bytes %ld < pagesize %ld -- memory argument too large?\n",
                wantbytes, pagesize);
        exit(EXIT_FAIL_NONSTARTER);
    }

    if (optind >= argc) {
        loops = 0;
    } else {
        errno = 0;
        loops = strtoul(argv[optind], &loopsuffix, 0);
        if (errno != 0) {
            LOGD(stderr, "failed to parse number of loops");
            usage(argv[0]); /* doesn't return */
        }
        if (*loopsuffix != '\0') {
            LOGD(stderr, "loop suffix %c\n", *loopsuffix);
            usage(argv[0]); /* doesn't return */
        }
    }

    LOGD("want %lluMB (%llu bytes)\n", (ull) wantmb, (ull) wantbytes);
    memset(buffer, sizeof(buffer), 0);
    sprintf(buffer, "want %lluMB (%llu bytes)\n", (ull) wantmb, (ull) wantbytes);
    send(client_socket, buffer, strlen(buffer), 0);
    buf = NULL;

    if (use_phys) {
        memfd = open(device_name, O_RDWR | O_SYNC);
        if (memfd == -1) {
            LOGD(stderr, "failed to open %s for physical memory: %s\n",
                    device_name, strerror(errno));
            exit(EXIT_FAIL_NONSTARTER);
        }
        buf = (void volatile *) mmap(0, wantbytes, PROT_READ | PROT_WRITE,
                                     MAP_SHARED | MAP_LOCKED, memfd,
                                     physaddrbase);
        if (buf == MAP_FAILED) {
            LOGD(stderr, "failed to mmap %s for physical memory: %s\n",
                    device_name, strerror(errno));
            exit(EXIT_FAIL_NONSTARTER);
        }

        if (mlock((void *) buf, wantbytes) < 0) {
            LOGD(stderr, "failed to mlock mmap'ed space\n");
            do_mlock = 0;
        }

        bufsize = wantbytes; /* accept no less */
        aligned = buf;
        done_mem = 1;
    }

    while (!done_mem) {
        while (!buf && wantbytes) {
            buf = (void volatile *) malloc(wantbytes);
            if (!buf) wantbytes -= pagesize;
        }
        bufsize = wantbytes;
        LOGD("got  %lluMB (%llu bytes)", (ull) wantbytes >> 20,(ull) wantbytes);
        memset(buffer, sizeof(buffer), 0);
        sprintf(buffer, "got  %lluMB (%llu bytes)\n", (ull) wantbytes >> 20,(ull) wantbytes);
        send(client_socket, buffer, strlen(buffer), 0);
        fflush(stdout);
        if (do_mlock) {
            LOGD(", trying mlock ...");
            memset(buffer, sizeof(buffer), 0);
            sprintf(buffer, "trying mlock ...\n");
            send(client_socket, buffer, strlen(buffer), 0);
            fflush(stdout);
            if ((size_t) buf % pagesize) {
                /* printf("aligning to page -- was 0x%tx\n", buf); */
                aligned = (void volatile *) ((size_t) buf & pagesizemask) + pagesize;
                /* printf("  now 0x%tx -- lost %d bytes\n", aligned,
                 *      (size_t) aligned - (size_t) buf);
                 */
                bufsize -= ((size_t) aligned - (size_t) buf);
            } else {
                aligned = buf;
            }
            /* Try mlock */
            if (mlock((void *) aligned, bufsize) < 0) {
                switch(errno) {
                    case EAGAIN: /* BSDs */
                        LOGD("over system/pre-process limit, reducing...\n");
                        memset(buffer, sizeof(buffer), 0);
                        sprintf(buffer, "over system/pre-process limit, reducing...\n");
                        send(client_socket, buffer, strlen(buffer), 0);
                        free((void *) buf);
                        buf = NULL;
                        wantbytes -= pagesize;
                        break;
                    case ENOMEM:
                        LOGD("too many pages, reducing...\n");
                        memset(buffer, sizeof(buffer), 0);
                        sprintf(buffer, "too many pages, reducing...\n");
                        send(client_socket, buffer, strlen(buffer), 0);
                        free((void *) buf);
                        buf = NULL;
                        wantbytes -= pagesize;
                        break;
                    case EPERM:
                        LOGD("insufficient permission.\n");
                        memset(buffer, sizeof(buffer), 0);
                        sprintf(buffer, "insufficient permission.");
                        send(client_socket, buffer, strlen(buffer), 0);
                        LOGD("Trying again, unlocked:\n");
                        memset(buffer, sizeof(buffer), 0);
                        sprintf(buffer, "Trying again, unlocked:\n");
                        send(client_socket, buffer, strlen(buffer), 0);
                        do_mlock = 0;
                        free((void *) buf);
                        buf = NULL;
                        wantbytes = wantbytes_orig;
                        break;
                    default:
                        LOGD("failed for unknown reason.\n");
                        memset(buffer, sizeof(buffer), 0);
                        sprintf(buffer, "failed for unknown reason.\n");
                        send(client_socket, buffer, strlen(buffer), 0);
                        do_mlock = 0;
                        done_mem = 1;
                }
            } else {
                LOGD("locked.\n");
                memset(buffer, sizeof(buffer), 0);
                sprintf(buffer, "locked.\n");
                send(client_socket, buffer, strlen(buffer), 0);
                done_mem = 1;
            }
        } else {
            done_mem = 1;
            printf("\n");
        }
    }

    if (!do_mlock) LOGD(stderr, "Continuing with unlocked memory; testing "
                           "will be slower and less reliable.\n");

    halflen = bufsize / 2;
    count = halflen / sizeof(ul);
    bufa = (ulv *) aligned;
    bufb = (ulv *) ((size_t) aligned + halflen);

    for(loop=1; ((!loops) || loop <= loops); loop++) {
        LOGD("Loop %lu", loop);
        if (loops) {
            LOGD("/%lu", loops);
            memset(buffer, sizeof(buffer), 0);
            sprintf(buffer, "Loop %lu / %lu\n", loop,  loops);
            send(client_socket, buffer, strlen(buffer), 0);
        }else{
            memset(buffer, sizeof(buffer), 0);
            sprintf(buffer, "Loop %lu\n", loop);
            send(client_socket, buffer, strlen(buffer), 0);
        }
        LOGD(":\n");
        LOGD("  %-20s: ", "Stuck Address");
        fflush(stdout);
        if (!test_stuck_address(aligned, bufsize / sizeof(ul))) {
             LOGD("ok\n");
             memset(buffer, sizeof(buffer), 0);
             sprintf(buffer, "  %-20s: ok!\n", "Stuck Address");
             send(client_socket, buffer, strlen(buffer), 0);
        } else {
            exit_code |= EXIT_FAIL_ADDRESSLINES;
        }
        for (i=0;;i++) {
            if (!tests[i].name) break;
            /* If using a custom testmask, only run this test if the
               bit corresponding to this test was set by the user.
             */
            if (testmask && (!((1 << i) & testmask))) {
                continue;
            }
            LOGD("  %-20s: ", tests[i].name);
            if (!tests[i].fp(bufa, bufb, count)) {
                LOGD("ok\n");
                memset(buffer, sizeof(buffer), 0);
                sprintf(buffer,"  %-20s: ok!\n", tests[i].name);
                send(client_socket, buffer, strlen(buffer), 0);
            } else {
                exit_code |= EXIT_FAIL_OTHERTEST;
            }
            fflush(stdout);
        }
        LOGD("\n");
        fflush(stdout);
    }
    if (do_mlock) munlock((void *) aligned, bufsize);
    LOGD("Done.\n");
    memset(buffer, sizeof(buffer), 0);
    sprintf(buffer, "Done.\n");
    send(client_socket, buffer, strlen(buffer), 0);
    fflush(stdout);
    exit(exit_code);
}
