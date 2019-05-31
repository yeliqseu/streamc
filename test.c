#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <fcntl.h>
#include "streamcodec.h"

int main(int argc, char *argv[])
{
    if (argc != 4) {
        printf("Usage: ./programName snum repfreq epsilon\n");
        exit(1);
    }
    int snum = atoi(argv[1]);
    struct parameters cp;
    cp.gfpower = 8;
    cp.pktsize = 200;
    //cp.repintvl = atoi(argv[2]);
    cp.repfreq = atof(argv[2]);
    double pe = atof(argv[3]);
    // seed PRNG
    struct timeval tv;
    gettimeofday(&tv, NULL);
    srand(tv.tv_sec * 1000 + tv.tv_usec / 1000); // seed use microsec
    // generate random data
    int datasize = snum * cp.pktsize;
    unsigned char *buf = malloc(datasize);
    int rnd=open("/dev/urandom", O_RDONLY);
    read(rnd, buf, datasize);
    close(rnd);
    // create encoder
    struct encoder *ec = initialize_encoder(&cp, buf, datasize);
    struct decoder *dc = initialize_decoder(&cp);
    int nuse = 0;
    while (dc->inorder < snum-1) {
        struct packet *pkt = generate_packet(ec);
        nuse += 1;
        if (rand() % 10000000 >= pe * 10000000) {
            receive_packet(dc, pkt);
        } else {
            printf("Packet %d erased in channel\n", pkt->sourceid);
            free_packet(pkt);
        }
    }

    int correct = 1;
    for (int i=0; i<ec->snum; i++) {
        if (memcmp(ec->srcpkt[i], dc->recovered[i], cp.pktsize) !=0) {
            correct = 0;
            printf("recovered %d is NOT identical to original.\n", i);
        }
    }
    if (correct) {
        printf("All source packets are recovered correctly\n");
        printf("[Summary] snum: %d repfreq: %.3f erasure: %.3f nuses: %d \n", snum, cp.repfreq, pe, nuse);
    }
}