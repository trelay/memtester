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
#include <unistd.h>
#include <sys/sysinfo.h>
#include <pthread.h>

#include "types.h"
#include "sizes.h"
#include "tests.h"

#define EXIT_FAIL_NONSTARTER    0x01
#define EXIT_FAIL_ADDRESSLINES  0x02
#define EXIT_FAIL_OTHERTEST     0x04


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

/* Sanity checks and portability helper macros. */
#ifdef _SC_VERSION
void check_posix_system(void) {
    if (sysconf(_SC_VERSION) < 198808L) {
        fprintf(stderr, "A POSIX system is required.  Don't be surprised if "
            "this craps out.\n");
        fprintf(stderr, "_SC_VERSION is %lu\n", sysconf(_SC_VERSION));
    }
}
#else
#define check_posix_system()
#endif

size_t memtester_pagesize(void) {
    size_t pagesize = sysconf(_SC_PAGE_SIZE);
    if (pagesize == -1) {
        perror("get page size failed");
        exit(EXIT_FAIL_NONSTARTER);
    }
    return pagesize;
}


/* Some systems don't define MAP_LOCKED.  Define it to 0 here
   so it's just a no-op when ORed with other constants. */
#ifndef MAP_LOCKED
  #define MAP_LOCKED 0
#endif

/* Function declarations */
void usage(char *me);

/* Function definitions */
void usage(char *me) {
    fprintf(stderr, "\n"
            "Usage: %s [loops]\n",me);
    exit(EXIT_FAIL_NONSTARTER);
}


int t_thread;
int remaining_cores;
pthread_mutex_t lock;


//struct for the parameter to memtest thread.
typedef struct _thread_data_t {
    size_t pagesize;
    int core;
    ul loops;
    } thread_data_t;

void * showprogress(void * arg)
{
    unsigned long  PROGRESSOFTEN=2500;
    unsigned long PROGRESSLEN=4;
    char progress[] = "-\\|/";
    unsigned long j = 0;
    unsigned long i;
    char msg[20];
    strcpy(msg, (char *) arg);
    fflush(stdout);
    printf(msg);
    fflush(stdout);
    sleep(5);
    putchar(' ');
    fflush(stdout);
    while(1) {
        if (!(i % PROGRESSOFTEN)) {
            putchar('\b');
            putchar(progress[++j % PROGRESSLEN]);
            fflush(stdout);
        }
        i++;
        sleep(0.5);
        if (t_thread<1) break;
    }
}

void * submemtest(void *arg_pages)
{
    int do_mlock = 1, done_mem = 0;
    int exit_code = 0;
    ul loops, loop,i;
    void volatile *buf, *aligned;
    size_t wantbytes, pagesize, wantbytes_orig, bufsize,
         halflen, count;
    ptrdiff_t pagesizemask;

    thread_data_t *testprmt = (thread_data_t *)arg_pages;
    loops = testprmt->loops;

    char log_name[10];
    sprintf(log_name,"test%d.log", testprmt->core);

    FILE *f;
    f = fopen(log_name, "a+");
    ulv *bufa, *bufb;
    ul testmask = 0;
    buf = NULL;

    //lock the process of allocating memory and lock it allocated.
    pthread_mutex_lock(&lock);

    signed long long avpages = sysconf(_SC_AVPHYS_PAGES);
    signed long long pagesforcore = avpages/remaining_cores;
    //pagesforcore = pagesforcore*0.001; //for test purpose, shorten the test time.
    pagesize = testprmt->pagesize;
    fprintf(f,"pagesize is %ld\n", (size_t) pagesize);

    wantbytes = pagesforcore*pagesize;
    pagesizemask = (ptrdiff_t) ~(pagesize - 1);
    fprintf(f,"Start test with cpu-%d, got memory:%uMB.\n", testprmt->core, (ull) wantbytes >> 20);
    fprintf(f,"pagesizemask is 0x%tx\n", pagesizemask);
    fflush(f);

    while (!done_mem) {
        while (!buf && wantbytes) {
            buf = (void volatile *) malloc(wantbytes);
            if (!buf) wantbytes -= pagesize;
        }
        bufsize = wantbytes;
        fprintf(f,"got  %lluMB (%llu bytes)", (ull) wantbytes >> 20,
            (ull) wantbytes);
        fflush(f);
        if (do_mlock) {
            fprintf(f,", trying mlock ...");
            fflush(f);
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
                        fprintf(f,"over system/pre-process limit, reducing...\n");
                        free((void *) buf);
                        buf = NULL;
                        wantbytes -= pagesize;
                        break;
                    case ENOMEM:
                        fprintf(f,"too many pages, reducing...\n");
                        free((void *) buf);
                        buf = NULL;
                        wantbytes -= pagesize;
                        break;
                    case EPERM:
                        fprintf(f,"insufficient permission.\n");
                        fprintf(f,"Trying again, unlocked:\n");
                        do_mlock = 0;
                        free((void *) buf);
                        buf = NULL;
                        wantbytes = wantbytes_orig;
                        break;
                    default:
                        fprintf(f,"failed for unknown reason.\n");
                        do_mlock = 0;
                        done_mem = 1;
                }
            } else {
                fprintf(f,"locked.\n");
                fflush(f);
                printf("Memory:%uMB was locked by cpu-%d.\n", (ull) wantbytes >> 20, testprmt->core);
                fflush(stdout);
                t_thread += 1;
                remaining_cores = remaining_cores-1;
                done_mem = 1;
            }
        } else {
            done_mem = 1;
            fprintf(f,"\n");
        }

    }

    //lock the process of allocating memory and lock it allocated.
    pthread_mutex_unlock(&lock);

    if (!do_mlock) fprintf(f, "Continuing with unlocked memory; testing "
                           "will be slower and less reliable.\n");

    halflen = bufsize / 2;
    count = halflen / sizeof(ul);
    bufa = (ulv *) aligned;
    bufb = (ulv *) ((size_t) aligned + halflen);

    for(loop=1; ((!loops) || loop <= loops); loop++) {
        fprintf(f,"Loop %lu", loop);
        if (loops) {
            fprintf(f,"/%lu", loops);
        }
        fprintf(f,":\n");
        fprintf(f,"  %s: ", "Stuck Address");
        fflush(f);
        if (!test_stuck_address(aligned, bufsize / sizeof(ul),f)) {
             fprintf(f,"ok\n");
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
            fprintf(f,"  %s: ", tests[i].name);
            if (!tests[i].fp(bufa, bufb, count,f)) {
                fprintf(f,"ok\n");
            } else {
                exit_code |= EXIT_FAIL_OTHERTEST;
            }
            fflush(f);
        }
        fprintf(f,"\n");
        fflush(f);
    }
    if (do_mlock) munlock((void *) aligned, bufsize);

    pthread_mutex_lock(&lock);
    t_thread = t_thread-1;
    pthread_mutex_unlock(&lock);

    fprintf(f,"Done.\n");
    fflush(f);
    printf("Memory:%uMB was completed test by cpu-%d.\n", (ull) wantbytes >> 20, testprmt->core);
    fflush(stdout);
}

int main(int argc, char **argv) {
    ul loops, loop;
    size_t pagesize = memtester_pagesize();
    char *memsuffix, *addrsuffix, *loopsuffix;
    ptrdiff_t pagesizemask;

    int exit_code = 0;
    int memfd, opt, memshift;
    size_t maxbytes = -1; /* addressable memory, in bytes */
    size_t maxmb = (maxbytes >> 20) + 1; /* addressable memory, in MB */
    /* Device to mmap memory from with -p, default is normal core */
    char *device_name = "/dev/mem";
    struct stat statbuf;
    char *env_testmask = 0;


    ul testmask = 0;

    if (pthread_mutex_init(&lock, NULL) != 0)
    {
        printf("\n mutex init failed\n");
        return 1;
    }
    

    printf("memtester version " __version__ " (%d-bit)\n", UL_LEN);
    printf("Copyright (C) 2001-2012 Charles Cazabon.\n");
    printf("Midfied by Trelay for Whitebox product.\n");
    printf("%20s\n", "2018.12.8 21:28");
    printf("Licensed under the GNU General Public License version 2 (only).\n");
    
    printf("\n");
    check_posix_system();
    
    
    /* If MEMTESTER_TEST_MASK is set, we use its value as a mask of which
       tests we run.
     */
    if (env_testmask = getenv("MEMTESTER_TEST_MASK")) {
        errno = 0;
        testmask = strtoul(env_testmask, 0, 0);
        if (errno) {
            fprintf(stderr, "error parsing MEMTESTER_TEST_MASK %s: %s\n", 
                    env_testmask, strerror(errno));
            usage(argv[0]); /* doesn't return */
        }
        printf("using testmask 0x%lx\n", testmask);
    }    
    if (optind >= argc) {
        fprintf(stderr, "need cycles you want to test, int\n");
        usage(argv[0]); /* doesn't return */
    }

    errno = 0;

    if (optind >= argc) {
        loops = 0;
    } else {
        errno = 0;
        loops = strtoul(argv[optind], &loopsuffix, 0);
        if (errno != 0) {
            fprintf(stderr, "failed to parse number of loops");
            usage(argv[0]); /* doesn't return */
        }
        if (*loopsuffix != '\0') {
            fprintf(stderr, "loop suffix %c\n", *loopsuffix);
            usage(argv[0]); /* doesn't return */
        }
    }

    //TODO: Trelay add this for maxing memtest
    int avcores = get_nprocs();
    remaining_cores = avcores;
    
    // Create array of pthread_t
    // create a thread_data_t argument array
    pthread_t thr[avcores];
    thread_data_t thr_data[avcores];
 


    pthread_t progress;

    int i, rc;
    /* create threads */
    for (i = 0; i < avcores; ++i) {
        thr_data[i].pagesize =pagesize;
        thr_data[i].loops = loops;
        thr_data[i].core = i;

        if (rc = pthread_create(&thr[i], NULL, submemtest, &thr_data[i])) {
            fprintf(stderr, "error: pthread_create, rc: %d\n", rc);
            return -1;
        }
    }

    //Add following code to show the progress, let user know I'm not dieing...
    char msg[20] = "Starting test...\n";
    if (pthread_create(&progress, NULL, showprogress, msg)) {
            fprintf(stderr, "error: creating show progress\n");
            return -1;
    }
    pthread_join(progress, NULL);

    /* block until all threads complete */
    for (i = 0; i < avcores; ++i) {
        pthread_join(thr[i], NULL);
    }

    //TODO: Trelay add this for maxing memtest
    pthread_mutex_destroy(&lock);
    printf("Memtester Complete.\n");
    exit(0);
}