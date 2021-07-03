#include "streamcodec.h"
#include "galois.h"
#define ENC_ALLOC   50

struct encoder *initialize_encoder(struct parameters *cp, unsigned char *buf, int nbytes)
{
    struct encoder *ec = calloc(1, sizeof(struct encoder));
    ec->cp = cp;
    ec->count = 0;
    ec->nextsid = 0;
    ec->rcount = 0;
    if (nbytes == 0) {
        ec->bufsize = ENC_ALLOC;
        ec->srcpkt = calloc(ENC_ALLOC, sizeof(GF_ELEMENT*));
        ec->snum = 0;
        ec->head = -1;
        ec->tail = -1;
        ec->headsid = -1;
        ec->tailsid = -1;
    } else {
        int snum = ALIGN(nbytes, cp->pktsize);
        ec->snum = snum;
        int blocks = ALIGN(snum, ENC_ALLOC);
        ec->srcpkt = calloc(blocks*ENC_ALLOC, sizeof(GF_ELEMENT*));
        ec->bufsize = blocks * ENC_ALLOC;
        // Load source packets
        int pktsize = ec->cp->pktsize;
        int hasread = 0;
        for (int i=0; i<snum; i++) {
            int toread = (hasread+pktsize) <= nbytes ? pktsize : nbytes-hasread;
            ec->srcpkt[i] = calloc(pktsize, sizeof(GF_ELEMENT));
            memcpy(ec->srcpkt[i], buf+hasread, toread*sizeof(GF_ELEMENT));
            hasread += toread;
        }
        ec->head = 0;
        ec->tail = ec->snum - 1;
        ec->headsid = 0;
        ec->tailsid = ec->snum - 1;
    }
    // Initialize random number generator for coding coefficients
    ec->prng.mti = N+1;
    ec->cp->seed = rand();      // use random number of libc to seed mt19937
    mt19937_init(ec->cp->seed, ec->prng.mt);
    // Construct finite field
    constructField(cp->gfpower);
    return ec;
}

// Used when a random arrival model of source packets is considered
// It is assumed that the source packet id's are continuous
int enqueue_packet(struct encoder *ec, int sourceid, GF_ELEMENT *syms)
{
    int pktsize = ec->cp->pktsize;
    int bufsize = ec->bufsize;
    int i;
    // printf("[Encoder] Enqueuing source packet %d [ head_pos: %d tail_pos: %d headsid: %d tailsid: %d ]\n", sourceid, ec->head, ec->tail, ec->headsid, ec->tailsid);
    // the buffer is empty
    if (ec->head == -1) {
        ec->srcpkt[0] = calloc(pktsize, sizeof(GF_ELEMENT));
        memcpy(ec->srcpkt[0], syms, pktsize);
        ec->head = 0;
        ec->tail = 0;
        ec->headsid = sourceid;
        ec->tailsid = sourceid;
        ec->snum += 1;
        //printf("[Encoder] Enqueued source packet %d [ head_pos: %d tail_pos: %d headsid: %d tailsid: %d snum: %d]\n", sourceid, ec->head, ec->tail, ec->headsid, ec->tailsid, ec->snum);
        return 0;   
    }
    // if the buffer is full, has to enlarge it before saving the packet
    if ((ec->tail+1) % bufsize == ec->head) {
        ec->srcpkt = realloc(ec->srcpkt, bufsize*2*sizeof(GF_ELEMENT*));
        // initialize the added memory
        for (i=0; i<bufsize; i++) {
            ec->srcpkt[bufsize+i] = NULL;
        }
        // re-locate the bufferred source packet pointers so that the packets are consistently stored
        if (ec->tail < ec->head) {
            for (i=0; i<ec->tail+1; i++) {
                ec->srcpkt[bufsize+i] = ec->srcpkt[i];
                ec->srcpkt[i] = NULL;
            }
            ec->tail = ec->head -1 + bufsize;
        }
        ec->bufsize = bufsize * 2;
    }
    bufsize = ec->bufsize;                  // bufsize may have been changed
    int pos = (ec->tail + 1) % bufsize;     // location to enqueue
    ec->srcpkt[pos] = calloc(pktsize, sizeof(GF_ELEMENT));
    memcpy(ec->srcpkt[pos], syms, pktsize);
    ec->tail = pos;
    ec->tailsid = sourceid;
    ec->snum += 1;
    // printf("[Encoder] Enqueued source packet %d [ head_pos: %d tail_pos: %d headsid: %d tailsid: %d snum: %d]\n", sourceid, ec->head, ec->tail, ec->headsid, ec->tailsid, ec->snum);
    return 0;
}


struct packet *output_repair_packet(struct encoder *ec)
{
    int i, pos;
    int pktsize = ec->cp->pktsize;
    struct packet *pkt = calloc(1, sizeof(struct packet));
    pkt->syms = calloc(pktsize, sizeof(GF_ELEMENT));
    pkt->sourceid = -1;
    pkt->repairid = ec->rcount;
    pkt->win_s = ec->headsid;
    pkt->win_e = ec->nextsid - 1;
    ec->count  += 1;
    ec->rcount += 1;
    DEBUG_PRINT(("[Encoder] Transmit repair packet %d across window [%d, %d]\n", pkt->repairid, pkt->win_s, pkt->win_e));
    int width = pkt->win_e - pkt->win_s + 1;
    if (width > EWIN) {
        printf("[Warning] Encoding window size is too large\n");
    }
    pkt->coes = calloc(width, sizeof(GF_ELEMENT));
    for (i=0; i<width; i++) {
        GF_ELEMENT co = mt19937_randint(ec->prng.mt, &ec->prng.mti) % (1 << ec->cp->gfpower);
        pkt->coes[i] = co;
        pos = (ec->head + i) % (ec->bufsize);
        galois_multiply_add_region(pkt->syms, ec->srcpkt[pos], co, pktsize);
    }
    for (i=width; i<EWIN; i++) {
        GF_ELEMENT co_skip = mt19937_randint(ec->prng.mt, &ec->prng.mti) % (1 << ec->cp->gfpower);
    }
    return pkt;
}

struct packet *output_source_packet(struct encoder *ec)
{
    int pos;
    int pktsize = ec->cp->pktsize;
    struct packet *pkt = calloc(1, sizeof(struct packet));
    pkt->syms = calloc(pktsize, sizeof(GF_ELEMENT));
    pkt->sourceid = ec->nextsid;
    pkt->repairid = -1;
    
    pos = (ec->head + (ec->nextsid - ec->headsid)) % (ec->bufsize);
    memcpy(pkt->syms, ec->srcpkt[pos], pktsize*sizeof(GF_ELEMENT));
    ec->count += 1;
    ec->nextsid += 1;
    return pkt;
}

void flush_acked_packets(struct encoder *ec, int ack_sid)
{
    if (ack_sid < ec->headsid) {
        return;
    }
    int count = 0;
    for (int i=ec->headsid; i<=ack_sid; i++) {
        int pos = (ec->head + (i - ec->headsid)) % (ec->bufsize);
        if (ec->srcpkt[pos] != NULL) {
            free(ec->srcpkt[pos]);
            ec->srcpkt[pos] = NULL;
            count += 1;
        }
    }
    DEBUG_PRINT(("[Encoder] Receive ACK i_ord=%d. Current encoding window: [%d %d]. Window width: %d\n", ack_sid, ec->headsid, ec->nextsid-1, (ec->nextsid-ec->headsid)));
    DEBUG_PRINT(("[Encoder] %d source packets up to no. %d are flushed from sending queue\n", count, ack_sid));
    if (ack_sid == ec->tailsid) {
        // all the buffered packets are flushed
        ec->head = -1;
        ec->tail = -1;
        ec->headsid = -1;
        ec->tailsid = -1;
    } else {
        ec->head = (ec->head + (ack_sid - ec->headsid + 1)) % (ec->bufsize);
        ec->headsid = ack_sid + 1;
    }
    return;
}

unsigned char *serialize_packet(struct encoder *ec, struct packet *pkt)
{
    int sym_len  = ec->cp->pktsize;
    int strlen = sizeof(int) * 4 + sym_len;
    unsigned char *pktstr = calloc(strlen, sizeof(unsigned char));
    memcpy(pktstr, &pkt->sourceid, sizeof(int));
    memcpy(pktstr+sizeof(int), &pkt->repairid, sizeof(int));
    memcpy(pktstr+sizeof(int)*2, &pkt->win_s, sizeof(int));
    memcpy(pktstr+sizeof(int)*3, &pkt->win_e, sizeof(int));
    memcpy(pktstr+sizeof(int)*4, pkt->syms, sym_len);
    return pktstr;
}

void visualize_buffer(struct encoder *ec)
{
    printf("enqueued: %d\nbufsize:  %d\nnextsid:  %d\nbuffered: %d\n",\
            ec->snum, ec->bufsize, ec->nextsid, ec->tailsid-ec->headsid+1);
    int i;
    printf("Buffered SourceID:\t");
    for (i=ec->headsid; i<=ec->tailsid; i++) {
        printf(" %d\t ", i);
    }
    printf("\nBuffer Position:\t");
    for (i=ec->headsid; i<=ec->tailsid; i++) {
        printf(" %d\t ", (ec->head + i - ec->headsid) % (ec->bufsize) );
    }
    printf("\n");
    return;
}

void free_packet(struct packet *pkt)
{
    if (pkt->coes != NULL)
        free(pkt->coes);
    if (pkt->syms != NULL)
        free(pkt->syms);
    free(pkt);
    pkt = NULL;
    return;
}