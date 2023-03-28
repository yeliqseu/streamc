#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <math.h>
#include <fcntl.h>
#include "streamcodec.h"

// batch packet loss model
#define CH_GOOD 0
#define CH_BAD  1
#define MAX_BATCH 5
int channel_state = CH_GOOD;
double dist[5] = {0.2, 0.5, 0.2, 0.1, 0.0};   // batch size distribution
int batchremain = 0;

int slot = -1;                      // time slot
int *arrival_time  = NULL;          // time arriving at the sending queue
int *sent_time     = NULL;          // sending time of each source packet uncoded
int *deliver_time  = NULL;          // in-order delivery of each source packet at decoder      
int *inorder_delay = NULL;          // in-order delay := deli_time - sent_time[i];

static struct packet *generate_packet(struct encoder *ec);
static int time_to_send_repair(struct encoder *ec);
static int batch_erasure(double pbatch);
static int draw_batch_size(void);

// Used to track the "status" of the decoder
// The decoder can detect packet loss only when an out-of-order packet triggers the
// activation of the decoder. But if we mark this time point as the start of the active (i.e., busy) 
// period, we would have miss counted several times slots. The actual start of the busy period
// should be when an in-order source packet is erased. The following two global variables are used
// to track the times (like a genie).
int last_inactive_time = -1;
int last_active_time   = -1;

char usage[] = "Usage: ./programName snum repfreq epsilon\n\
                       snum     - maximum number of source packets to transmit\n\
                       arrival  - Bernoulli arrival rate at the sending queue, value: [0, 1]\n\
                                  0 - all source packets available before time 0\n\
                       repfreq  - frequency of inserting repair packets\n\
                                  random insertion of repair packets if repfreq < 1\n\
                                  fixed-interval insertion if repfreq >= 1 (must be integer)\n\
                       p_batch  - probability that a batch error occurs\n\
                       Tp       - propagation delay of channel\n";
int main(int argc, char *argv[])
{
    if (argc != 6) {
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

    double pbatch = atof(argv[4]);
    int T_P = atoi(argv[5]);
    int T_ACK = 1;          // feedback i_ord every T_ACK time slots

    // queues to simulate channel propagation delay
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
        // simulate packet sending & propagation
        if (!batch_erasure(pbatch)) {
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
            free_serialized_packet(pktstr);
            if (queue[pos1] != NULL) {
                free(queue[pos1]);
                queue[pos1] = NULL;
            }
        }
        free_packet(pkt);
        // simulate packet receiving
        int pos2 = (slot-T_P) % (T_P+1);    // which packet in the queue to be received at the current slot (due to propagation delay)
        if (slot >= T_P && queue[pos2] != NULL) {
            printf("[Channel] Packet queued at pos %d of the buffer is processed at time %d by the decoder\n", pos2, slot);
            struct packet *rpkt = deserialize_packet(dc, queue[pos2]);
            int isRepair = 0;
            if (rpkt->repairid >= 0 && dc->active) {
                isRepair = 1;
                printf("[Observation] repair packet %d with EW width %d arrives, and sees DW width %d\n", rpkt->repairid, rpkt->win_e-rpkt->win_s+1, (dc->win_e-dc->win_s+1)*(dc->active));
            }
            receive_packet(dc, rpkt);
            free(queue[pos2]);
            queue[pos2] = NULL;
        }
        // simulate flush sending queue using decoder's feedback
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
    int ncheck = ec->snum > DEC_ALLOC ? DEC_ALLOC : ec->snum;
    for (int i=ec->snum-1; i>ec->snum-ncheck; i--) {
        if (memcmp(buf+i*cp.pktsize, dc->recovered[i%DEC_ALLOC], cp.pktsize) !=0) {
            correct = 0;
            printf("[Warning] recovered %d is NOT identical to original.\n", i);
        }
    }
    if (correct) {
        printf("[Summary] All source packets are recovered correctly\n");
        printf("[Summary] snum: %d repfreq: %.3f nuses: %d \n", snum, cp.repfreq, nuse);
    }
    /*
    for (int i=0; i<snum; i++) {
        printf("[Summary] Arrival of source packet %d : %d\t sent at %d\t delivered at\t %d sent-to-deliver-delay: %d \t total-delay: %d\n", \
                    i, arrival_time[i], sent_time[i], deliver_time[i], deliver_time[i]-sent_time[i], deliver_time[i]-arrival_time[i]);
    }
    */
    printf("[Summary] Free encoder...\n");
    free_encoder(ec);
    printf("[Summary] Free decoder...\n");
    free_decoder(dc);
}

// Determine whether packet loss occurs for link i
static int batch_erasure(double p_batch)
{
    // Determine channel state
    if (channel_state == CH_GOOD) {
        if (rand() % 1000 < p_batch * 1000) {
            // transition from Good to Bad
            int size = draw_batch_size();
            batchremain = size;
            if (--batchremain == 0) {
                channel_state = CH_GOOD;
            } else {
                channel_state = CH_BAD;
            }
            DEBUG_PRINT(("[Channel] new batch of %d losses, batch size remaining: %d\n", size, batchremain));
            return 1;
        }
    } else {
        if (batchremain > 0) {
            if (--batchremain==0) {
                channel_state = CH_GOOD;
            }
            DEBUG_PRINT(("[Channel] batch size remaining: %d\n", batchremain));
            return 1;
        }
    }
    return 0;
}

struct packet *generate_packet(struct encoder *ec)
{
    if (ec->snum == 0 || ec->head == -1) {
        // no packet has been queued or all previously queued packets have been flushed
        return NULL;
    }
    // Generate a packet
    // Send a repair packet if 1) all the buffered packets have been sent in uncoded form once or 2) if we have sent 
    // at least one uncoded packet and a coin-toss (or timer for regular insertation)determines to send a repair packet
    int i, pos;
    struct packet *pkt;
    if (time_to_send_repair(ec)) {
        pkt = output_repair_packet(ec);
    } else {
        // send a source packet
        pkt = output_source_packet(ec);
        sent_time[pkt->sourceid] = slot;    // record transmit time of the source packet
    }
    return pkt;
}


static int time_to_send_repair(struct encoder *ec)
{
    int nextsid = ec->nextsid;
    if ( nextsid >= ec->snum 
        || (nextsid > 0 && nextsid > ec->headsid && ec->cp->repfreq < 1 && rand() % 1000 <= ec->cp->repfreq*1000)
        || (nextsid > 0 && nextsid > ec->headsid && ec->cp->repfreq >=1 && (ec->count + 1) % ((int)ec->cp->repfreq+1) == 0) ) {
        return 1;
    } else {
        return 0;
    }
}

// 从raptor代码借过来degree distribution采样函数
static int draw_batch_size(void)
{
    int ds = 1;
    int dm = MAX_BATCH;
    int Precision = 100000;
    int degree = 1;
    double degree_int[dm-ds+1];
    for (int i=0; i<dm-ds+1; i++)
        degree_int[i] = dist[i] * Precision;

    int choice = rand() % Precision;
    double lower_bound = 0;
    double upper_bound = lower_bound + degree_int[0];
    for (int j=0; j<dm-ds+1; j++) {
        if (choice >= lower_bound && choice < upper_bound) {
            degree = ds + j;
            break;
        } else {
            lower_bound += degree_int[j];
            upper_bound = lower_bound + degree_int[j+1];
        }
    }
    return degree;
}