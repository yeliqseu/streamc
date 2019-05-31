#include "streamcodec.h"
#define ENC_ALLOC   2000

struct encoder *initialize_encoder(struct parameters *cp, unsigned char *buf, int nbytes)
{
    struct encoder *ec = calloc(1, sizeof(struct encoder));
    ec->cp = cp;
    ec->count = 0;
    ec->nextsid = 0;
    ec->rcount = 0;
    int snum = ALIGN(nbytes, cp->pktsize);
    ec->snum = snum;
    ec->bufsize = ENC_ALLOC;
    ec->srcpkt = calloc(ENC_ALLOC, sizeof(GF_ELEMENT));
    // Load source packets
    int pktsize = ec->cp->pktsize;
    int hasread = 0;
    for (int i=0; i<snum; i++) {
        int toread = (hasread+pktsize) <= nbytes ? pktsize : nbytes-hasread;
        ec->srcpkt[i] = calloc(pktsize, sizeof(GF_ELEMENT));
        memcpy(ec->srcpkt[i], buf+hasread, toread*sizeof(GF_ELEMENT));
        hasread += toread;
    }
    constructField(cp->gfpower);
    return ec;
}

// Used when a random arrival model of source packets is considered
int enqueue_packet(struct encoder *ec)
{
    return;
}

struct packet *generate_packet(struct encoder *ec)
{
    int pktsize = ec->cp->pktsize;
    struct packet *pkt = calloc(1, sizeof(struct packet));
    pkt->syms = calloc(pktsize, sizeof(GF_ELEMENT));
    //if ((ec->count % ec->cp->repintvl == 0 && ec->count != 0) || ec->nextsid >= ec->snum)  {
    if ((ec->count != 0 && rand() % 1000 <= ec->cp->repfreq*1000) || ec->nextsid >= ec->snum)  {
        // send a coded packet
        pkt->sourceid = -1;
        pkt->repairid = ec->rcount;
        pkt->win_s = 0;
        pkt->win_e = ec->nextsid - 1;
        ec->count  += 1;
        ec->rcount += 1;
        int width = pkt->win_e - pkt->win_s + 1;
        pkt->coes = calloc(width, sizeof(GF_ELEMENT));
        for (int i=0; i<width; i++) {
            GF_ELEMENT co = rand() % (1 << ec->cp->gfpower);
            pkt->coes[i] = co;
            galois_multiply_add_region(pkt->syms, ec->srcpkt[i+pkt->win_s], co, pktsize);
        }
    } else {
        // send a source packet
        pkt->sourceid = ec->nextsid;
        pkt->repairid = -1;
        memcpy(pkt->syms, ec->srcpkt[pkt->sourceid], pktsize*sizeof(GF_ELEMENT));
        ec->count += 1;
        ec->nextsid += 1;
    }
    return pkt;
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