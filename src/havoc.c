/*
 * Copyright (c) 2013 by Kyle Isom <kyle@tyrfingr.is>.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL INTERNET SOFTWARE
 * CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

#include <sys/types.h>
#include <err.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define INIT_TIME       12
#define RNGPUT          1
#define NSAMP           256

#define RBG0            "/dev/arandom"
#define RBG1            "/dev/urandom"

unsigned char   median0;    // median value for ANALOG0
unsigned char   median1;    // median value for ANALOG1
unsigned int    debug;
unsigned int    mode32;


/*
 * readRBG0 and readRBG1 read a bit from their respective RBG; if
 * this is above the median, the function returns a -1 to indicate
 * the value is to be discarded. If the value is below the median,
 * a 0 is returned. If the value is above the median, a 1 is returned.
 */
static int
readRBG0(int calibrate)
{
        static int rbg0fd = 0;
        unsigned char   b;

        if (rbg0fd <= 0) {
                if ((rbg0fd = open(RBG0, O_RDONLY)) == 1) {
                        warn("RBG0 fault");
                }
        }

        if (1 != read(rbg0fd, &b, 1)) {
                warn("RBG0 fault");
                return -1;
        }

        if (calibrate == 1)
                return (int)b;

        if (b > median0)
                return 1;
        if (b < median0)
                return 0;
        return -1;
}

static int
readRBG1(int calibrate)
{
        static int rbg1fd = 0;
        unsigned char   b;

        if (rbg1fd <= 0) {
                if ((rbg1fd = open(RBG1, O_RDONLY)) == 1) {
                        warn("RBG0 fault");
                }
        }

        if (1 != read(rbg1fd, &b, 1)) {
                warn("RBG0 fault");
                return -1;
        }

        if (calibrate == 1)
                return (int)b;

        if (b > median1)
                return 1;
        if (b < median1)
                return 0;
        return -1;

}

/*
 * setup collects samples for 10 seconds, taking the median value
 * from each for calibration.
 */
static void
setup(void)
{
        int i = 0;
        int samples = 0;
        unsigned int samp0[NSAMP];
        unsigned int samp1[NSAMP];
        int sum0 = 0;
        int sum1 = 0;
        unsigned char half0, half1;
        unsigned char cal0 = 0;
        unsigned char cal1 = 0;
        time_t start = time(NULL);
        time_t stop = start + INIT_TIME;
        time_t cur, lastt;

        if (debug)
                printf("[+] starting initialisation\n");
        for (i = 0; i < NSAMP; i++) {
                samp0[i] = 0;
                samp1[i] = 0;
        }

        if (debug) 
                printf("\t[*] collecting reference samples\n");
        start = time(NULL);
        stop = start + INIT_TIME;
        lastt = start;
        while (time(NULL) < stop) {
                samp0[readRBG0(1)]++;
                samp1[readRBG1(1)]++;
                cur = time(NULL);
                if (cur != lastt) {
                        if (debug)
                                printf("\t[*] %d seconds left\n",
                                        (int)(stop - cur));
                        lastt = cur;
                }
                usleep(1000);
                samples++;
        }

        if (debug) {
                for (i = 0; i < NSAMP; i++) {
                        printf("\t%d\t\tsamp0: %d\t\tsamp1: %d\n", i, samp0[i],
                            samp1[i]);
                }
                sleep(4);
        }


        if (debug) {
                printf("\t[*] collected %d samples\n", samples);
                printf("\t[*] calculating median\n");
        }

        median0 = sum0 = 0;
        median1 = sum1 = 0;
        for (i = 0; i < NSAMP; i++) {
                sum0 += samp0[i];
                sum1 += samp1[i];
        }
        half0 = sum1 >> 1;
        half1 = sum1 >> 1;
        sum0 = sum1 = 0;
        cal0 = cal1 = 0;
        for (i = 0; i < NSAMP; i++) {
                if (!cal0) {
                        sum0 += samp0[i];
                        if (sum0 > half0)
                                cal0 = i;
                }

                if (!cal1) {
                        sum1 += samp1[i];
                        if (sum1 > half1)
                                cal1 = i;
                }
                if (cal0 && cal1)
                        break;
        }
        median0 = samp0[i];
        median1 = samp1[i];
        median0 = 128;
        median1 = 128;
        if (debug) {
                printf("[+] initialisation complete\n");
                printf("\t[*] RBG0 median: %d\n", median0);
                printf("\t[*] RBG1 median: %d\n", median1);
        }
}

/*
 * readRBG compares the two values coming from the RBGs. If either bit is
 * a failure or the two bits are equal, return a failure. This failure
 * does indicate a fault in the RBG, but rather that the result was discarded
 * and a new result should be taken.
 */
static int
readRBG(void)
{
        int b0 = readRBG0(0);
        int b1 = readRBG1(0);

        if (b0 != -1)
        if (b1 != -1)
        if (b0 != b1)
                return b0;
        return -1;
}

/*
 * loop8 begins once Havoc has been calibrated. It builds a byte at a time;
 * once a full byte has been built, it will be written out to the serial
 * port.
 */
static void
loop8(void)
{
        static unsigned char rval = 0;   // The random byte being built.
        static int n = 0;       // The current bit position in the byte.
        int rbit = readRBG();

        if (rbit != -1) {
                if (debug)
                        printf("\t%d . %02x\n", n, rbit);
                rval |= (rbit << n);
                n++;
                if (n == 8) {
                        if (debug)
                                printf("1 %02x\n", rval);
                        else
                                printf("%c\n", rval);
                        rval = 0;
                        n = 0;
                }
        } else {
                if (debug)
                        printf("\t%d !\n", n);
        }
}


/*
 * loop32 begins once Havoc has been calibrated. It builds 32-bit
 * unsigned intenger at a time; once a full byte has been built,
 * it will be written out to the serial port.
 */
static void
loop32(void)
{
        static uint32_t rval = 0;       /* Random int being built. */
        static int      n = 0;          /* The current bit position. */
        int rbit = readRBG();

        if (rbit != -1) {
                if (debug)
                        printf("\t%d . %02x\n", n, rbit);
                rval |= (rbit << n);
                n++;
                if (n == 32) {
                        if (debug)
                                printf("1 %u\n", rval);
                        else
                                printf("%u\n", rval);
                        rval = 0;
                        n = 0;
                }
        } else {
                if (debug)
                        printf("\t%d !\n", n);
        }
}

int
main(int argc, char *argv[])
{
        int ch;
        while ((ch = getopt(argc, argv, "dn")) != -1) {
                switch (ch) {
                case 'd':
                        debug = 1;
                        break;
                case 'n':
                        mode32 = 1;
                        break;
                default:
                        abort();
                }
        }

        setup();
        while (1) {
                if (mode32)
                        loop32();
                else
                        loop8();
                if (debug)
                        sleep(1);
        }
}
