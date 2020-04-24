#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <math.h>
#include <fcntl.h>
#include "streamcodec.h"

int slot = -1;                      // time slot
int T_P = 0;
int *arrival_time  = NULL;          // time arriving at the sending queue
int *sent_time     = NULL;          // sending time of each source packet uncoded
int *deliver_time  = NULL;          // in-order delivery of each source packet at decoder      
int *inorder_delay = NULL;          // in-order delay := deli_time - sent_time[i];


// Used for testing irregular insertion of repair packets
// Implemented via specifying position of source packets to save space and looping compleixty
// since we are interested in highly lossy scenarios where repair packets are sent most of the time 
int irreg_range = 0;
int irreg_snum = 0;;
int *irreg_spos = NULL;

char usage[] = "Usage: ./programName snum arrival repfreq epsilon Tp irange pos1 pos2 ... \n\
                       snum     - maximum number of source packets to transmit\n\
                       arrival  - Bernoulli arrival rate at the sending queue, value: [0, 1]\n\
                                  0 - all source packets available before time 0\n\
                       repfreq  - frequency of inserting repair packets\n\
                                  random insertion of repair packets if repfreq < 1\n\
                                  fixed-interval insertion if repfreq >= 1 (must be integer)\n\
                       epsilon  - erasure probability of the end-to-end link\n\
                       Tp       - propagation delay of channel\n\
                       irange   - period of the irregular pattern (0: regular or random depending on repfreq)\n\
                       posX     - positions sending source packets in the irregular range\n";
int main(int argc, char *argv[])
{
    if (argc <7) {
        printf("%s\n", usage);
        exit(1);
    }
    int snum = atoi(argv[1]);
    double arrival = atof(argv[2]);
    struct parameters cp;
    cp.gfpower = 8;
    cp.pktsize = 200;
    cp.repfreq = atof(argv[3]);
    if (cp.repfreq >= 1 && roundf(cp.repfreq) != cp.repfreq) {
        printf("%s\n", usage);
        exit(1);
    }

    double pe = atof(argv[4]);

    int T_ACK = 1;          // feedback i_ord every T_ACK time slots
    T_P = atoi(argv[5]);           // propagation delay. Packet sent at time t, if not erased, is received by destination at time t + T_P

    // Read positions to send source packets in a period of irregular range
    irreg_range = atoi(argv[6]);
    irreg_snum = argc - 7;
    irreg_spos = calloc(irreg_snum, sizeof(int));
    for (int i=0; i<irreg_snum; i++) {
        irreg_spos[i] = atoi(argv[7+i]);
    }

    unsigned char **queue = calloc(T_P+1, sizeof(unsigned char*));      // store propagation delayed packets
    int *feedback = calloc(T_P+1, sizeof(int));                         // store propagation delayed in-order feedback
    memset(feedback, -1, sizeof(int)*(T_P+1));
    
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
    //struct encoder *ec = initialize_encoder(&cp, buf, datasize);
    struct encoder *ec = initialize_encoder(&cp, NULL, 0);
    struct decoder *dc = initialize_decoder(&cp);
    int nuse = 0;
    // initialize timers
    arrival_time = calloc(snum, sizeof(int));
    sent_time    = calloc(snum, sizeof(int));
    deliver_time = calloc(snum, sizeof(int));
    // Enqueue all packets to sending queue at the beginning of the transmission if arrival rate set to 0
    int eqnsid;
    if (arrival == 0) {
        for (eqnsid=0; eqnsid<snum; eqnsid++) {
            enqueue_packet(ec, eqnsid, buf+eqnsid*cp.pktsize);
            arrival_time[eqnsid] = slot;
        }
    }
    while (dc->inorder < snum-1) {
        slot += 1;
        // If arrival rate is not 0, enqueue packets to sending queue as a random process
        if (arrival != 0 && eqnsid < snum && rand() % 1000 < arrival * 1000) {
            enqueue_packet(ec, eqnsid, buf+eqnsid*cp.pktsize);
            arrival_time[eqnsid] = slot;
            eqnsid++;
        }
        //visualize_buffer(ec);
        struct packet *pkt = generate_packet(ec);
        if (pkt==NULL) {
            continue;
        }
        unsigned char *pktstr = serialize_packet(ec, pkt);
        nuse += 1;
        int pos1 = slot % (T_P + 1);        // where in the queue to put the packet transmitted at the current slot
        if (rand() % 10000000 >= pe * 10000000) {
            // not erased, save it in the queue, which is to be process after T_P time slots
            if (queue[pos1] != NULL) {
                free(queue[pos1]);
            }
            queue[pos1] = pktstr;
            printf("[Channel] Non-erased packet queued at pos %d of the buffer at time %d\n", pos1, slot);
        } else {
            if (pkt->sourceid != -1) {
                printf("[Channel] Source packet %d erased in channel at time %d\n", pkt->sourceid, slot);
            } else {
                printf("[Channel] Repair packet %d erased in channel at time %d\n", pkt->repairid, slot);
            }
            free(pktstr);
            if (queue[pos1] != NULL) {
                free(queue[pos1]);
                queue[pos1] = NULL;
            }
        }
        free_packet(pkt);
        // Delayed reception
        int pos2 = (slot-T_P) % (T_P+1);    // which packet in the queue to be received at the current slot (due to propagation delay)
        if (slot >= T_P && queue[pos2] != NULL) {
            printf("[Channel] Packet queued at pos %d of the buffer is processed at time %d by the decoder\n", pos2, slot);
            struct packet *rpkt = deserialize_packet(dc, queue[pos2]);
            receive_packet(dc, rpkt);
        }
        // simulating: flush sending queue using decoder's feedback
        // for simplicity, we assume lossless feedback
        if (dc->inorder >= 0 && slot >= T_P && slot % T_ACK == 0) {
            feedback[pos1] = dc->inorder;
            printf("[Decoder] Decoder feedback in-order %d at time %d\n", dc->inorder, slot);
            if (slot >= T_P && feedback[pos2] != -1) {
                printf("[Encoder] Encoder processes in-order feedback %d at time %d\n", feedback[pos2], slot);
                flush_acked_packets(ec, feedback[pos2]);       // lossless, immediate in-order flush
                feedback[pos2] = -1;
            }
        }
    }

    int correct = 1;
    for (int i=0; i<ec->snum; i++) {
        if (memcmp(buf+i*cp.pktsize, dc->recovered[i], cp.pktsize) !=0) {
            correct = 0;
            printf("[Warning] recovered %d is NOT identical to original.\n", i);
        }
    }
    if (correct) {
        printf("[Summary] All source packets are recovered correctly\n");
        printf("[Summary] snum: %d repfreq: %.3f erasure: %.3f nuses: %d \n", snum, cp.repfreq, pe, nuse);
    }
    for (int i=0; i<snum; i++) {
        printf("[Summary] Arrival of source packet %d : %d\t sent at %d\t delivered at\t %d sent-to-deliver-delay: %d \t total-delay: %d\n", \
                    i, arrival_time[i], sent_time[i], deliver_time[i], deliver_time[i]-sent_time[i], deliver_time[i]-arrival_time[i]);
    }
}