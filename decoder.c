#include "streamcodec.h"
#include <assert.h>
#define DEC_ALLOC   100000
#define WIN_ALLOC   100000

extern int slot;
extern int *deliver_time;
extern int *sent_time;

extern int T_P;

struct decoder *initialize_decoder(struct parameters *cp)
{
    struct decoder *dc = calloc(1, sizeof(struct decoder));
    dc->active = 0;
    dc->cp = cp;
    dc->inorder = -1;
    dc->win_s = -1;
    dc->win_e = -1;
    dc->dof = 0;
    dc->row = calloc(WIN_ALLOC, sizeof(ROW_VEC*));
    dc->message = calloc(WIN_ALLOC, sizeof(GF_ELEMENT*));
    dc->recovered = calloc(DEC_ALLOC, sizeof(GF_ELEMENT*));
    dc->prev_rep = -1;
    printf("[Decoder] Coding coefficient PRNG seed: %d\n", cp->seed);
    dc->prng.mti = N + 1;
    mt19937_init(cp->seed, dc->prng.mt);
    return dc;
}

struct packet *deserialize_packet(struct decoder *dc, unsigned char *pktstr)
{
    int pktsize = dc->cp->pktsize;
    struct packet *pkt = calloc(1, sizeof(struct packet));
    pkt->syms = calloc(pktsize, sizeof(GF_ELEMENT));
    memcpy(&pkt->sourceid, pktstr, sizeof(int));
    memcpy(&pkt->repairid, pktstr+sizeof(int), sizeof(int));
    memcpy(&pkt->win_s, pktstr+sizeof(int)*2, sizeof(int));
    memcpy(&pkt->win_e, pktstr+sizeof(int)*3, sizeof(int));
    memcpy(pkt->syms, pktstr+sizeof(int)*4, dc->cp->pktsize);
    if (pkt->sourceid == -1) {
        int curr_rep = pkt->repairid;
        int i, j;
        for (i=dc->prev_rep+1; i<curr_rep; i++) {
            //skip coding coefficients of lost repair packets
            for (j=0; j<EWIN; j++) {
                GF_ELEMENT co_skip = mt19937_randint(dc->prng.mt, &dc->prng.mti) % (1 << dc->cp->gfpower);
            }
        }
        // recover coding coefficients using PRNG
        int width = pkt->win_e - pkt->win_s + 1;
        pkt->coes = calloc(width, sizeof(GF_ELEMENT));
        for (j=0; j<width; j++) {
            GF_ELEMENT co = mt19937_randint(dc->prng.mt, &dc->prng.mti) % (1 << dc->cp->gfpower);
            pkt->coes[j] = co;
        }
        for (j=width; j<EWIN; j++) {
            GF_ELEMENT co_skip = mt19937_randint(dc->prng.mt, &dc->prng.mti) % (1 << dc->cp->gfpower);
        }
    }
    return pkt;
}

int receive_packet(struct decoder *dc, struct packet *pkt)
{
    if (pkt->sourceid == -1) {
        dc->prev_rep = pkt->repairid;
    }
    int pktsize = dc->cp->pktsize;
    if (!dc->active) {
        // Decoder is inactive, in-order receiving
        if (pkt->sourceid >= 0) {
            // A source packet received, check whether it's consistent
            if (pkt->sourceid == dc->inorder + 1) {
                // in-order delievery, store it
                dc->recovered[pkt->sourceid] = calloc(pktsize, sizeof(GF_ELEMENT));
                memcpy(dc->recovered[pkt->sourceid], pkt->syms, pktsize*sizeof(GF_ELEMENT));
                printf("[Decoder] Receive in-order source packet %d \n", pkt->sourceid);
                // printf("[Decoder] delay of source packet %d : 0 \n", pkt->sourceid);
                deliver_time[pkt->sourceid] = slot;    // record in-order delivery time
                dc->inorder += 1;
                return 0;
            } else {
                // missing source packet
                printf("[Decoder] Receives source packet %d but in-order is %d, activating decoder...\n", pkt->sourceid, dc->inorder);
                activate_decoder(dc, pkt);
            }
        } else {
            // A repair packet received 
            if (pkt->win_e == dc->inorder) {
                // encoded from all the recovered packets: i.e., still in-order, just ignore
                printf("[Decoder] Receives repair packet coded across [%d, %d], in-order is %d, just ignore...\n", pkt->win_s, pkt->win_e, dc->inorder);
                return 0;
            } else {
                // it contains some missing source packets, activate decoder
                printf("[Decoder] Receives repair packet coded across [%d, %d], but in-order is %d, activating decoder...\n", pkt->win_s, pkt->win_e, dc->inorder);
                activate_decoder(dc, pkt);
            }
        }
    }
    // Decoder is active, i.e., in-order reception halted, so perform on-the-fly GE
    process_packet(dc, pkt);
    if (dc->dof == (dc->win_e - dc->win_s + 1)) {
        // ready to recover the whole window
        deactivate_decoder(dc);
    }
    return 0; 
}

int activate_decoder(struct decoder *dc, struct packet *pkt)
{
    int pktsize = dc->cp->pktsize;
    if (pkt->sourceid >= 0) {
        // A source packet triggered decoder activation
        // Packets of inorder+1, inorder+2,..., sourceid-1 are missing
        dc->win_s = dc->inorder + 1;
        dc->win_e = pkt->sourceid;
        dc->dof = 0;
    } else {
        dc->win_s = dc->inorder + 1;
        dc->win_e = pkt->win_e;
        dc->dof = 0;
    }
    printf("[Decoder] Decoder activated at time %d, decoding window [%d %d]\n", slot, dc->win_s, dc->win_e);
    dc->active = 1;
    return 0;
}

int process_packet(struct decoder *dc, struct packet *pkt)
{
    int pktsize = dc->cp->pktsize;
    if (pkt->sourceid>=0) {
        printf("[Decoder] Processing source packet %d, decoding window [%d %d]\n", pkt->sourceid, dc->win_s, dc->win_e);
        // A not-in-order source packet is received
        int index = pkt->sourceid - dc->win_s;
        dc->row[index] = calloc(1, sizeof(ROW_VEC));
        dc->row[index]->len = 1;
        dc->row[index]->elem = calloc(dc->row[index]->len, sizeof(GF_ELEMENT));
        dc->row[index]->elem[0] = 1;
        dc->message[index] = calloc(pktsize, sizeof(GF_ELEMENT));
        memcpy(dc->message[index], pkt->syms, pktsize * sizeof(GF_ELEMENT));
        dc->dof += 1;
        dc->win_e = pkt->sourceid;
    } else {
        printf("[Decoder] Processing repair packet, encoding window [%d, %d]\n", pkt->win_s, pkt->win_e);
        // eliminate those already in-order recovered packets from the repair packets
        for (int i=pkt->win_s; i<=dc->inorder; i++) {
            GF_ELEMENT co = pkt->coes[i-pkt->win_s];
            galois_multiply_add_region(pkt->syms, dc->recovered[i], co, pktsize);
            pkt->coes[i-pkt->win_s] = 0;
        }
        // Packets of inorder+1, inorder+2, ...., pkt->win_e are missing
        dc->win_e = pkt->win_e;                                 // expand decoding window to what we have seen
        int width = dc->win_e - dc->win_s + 1;                  // current decoding window size
        GF_ELEMENT *coes = calloc(width, sizeof(GF_ELEMENT));
        memcpy(coes, &pkt->coes[dc->win_s-pkt->win_s], width);  // full-length EV
        // Process and save the EV to the appropriate row
        GF_ELEMENT quotient;
        for (int i=0; i<width; i++) {
            if (coes[i] != 0) {
                if (dc->row[i] != NULL) {
                    quotient = galois_divide(coes[i], dc->row[i]->elem[0]);
                    galois_multiply_add_region(&coes[i], dc->row[i]->elem, quotient, dc->row[i]->len);
                    galois_multiply_add_region(pkt->syms, dc->message[i], quotient, pktsize);
                } else {
                    dc->row[i] = calloc(1, sizeof(ROW_VEC));
                    dc->row[i]->len = width - i;
                    dc->row[i]->elem = calloc(dc->row[i]->len, sizeof(GF_ELEMENT));
                    memcpy(dc->row[i]->elem, &coes[i], dc->row[i]->len);
                    dc->message[i] = calloc(pktsize, sizeof(GF_ELEMENT));
                    memcpy(dc->message[i], pkt->syms, pktsize * sizeof(GF_ELEMENT));
                    dc->dof += 1;
                    break;
                }
            }
        }
    }
    return 0;
}

int deactivate_decoder(struct decoder *dc)
{
    int pktsize = dc->cp->pktsize;
    int win_s = dc->win_s;
    int win_e = dc->win_e;
    int width = win_e - win_s + 1;
    // Eliminate nonzeros above diagonal elements
    int i, j;
    int len;
    GF_ELEMENT quotient;
    // eliminate all nonzeros above diagonal elements from right to left
    for (i=width-1; i>=0; i--) {
        for (j=0; j<i; j++) {
            len = dc->row[j]->len;
            if (j+len <= i || dc->row[j]->elem[i-j] == 0)
                continue;
            assert(dc->row[i]->elem[0]);
            quotient = galois_divide(dc->row[j]->elem[i-j], dc->row[i]->elem[0]);
            galois_multiply_add_region(dc->message[j], dc->message[i], quotient, pktsize);
            dc->row[j]->elem[i-j] = 0;
        }
        // convert diagonal to 1
        if (dc->row[i]->elem[0] != 1) {
            galois_multiply_region(dc->message[i], galois_divide(1, dc->row[i]->elem[0]), pktsize);
            dc->row[i]->elem[0] = 1;
        }
        // save recovered packet
        int sourceid = win_s + i;
        dc->recovered[sourceid] = calloc(pktsize, sizeof(GF_ELEMENT));
        deliver_time[sourceid] = slot;     // record in-order delivery time
        memcpy(dc->recovered[sourceid], dc->message[i], pktsize*sizeof(GF_ELEMENT));
        free(dc->message[i]);
        dc->message[i] = NULL;
        free(dc->row[i]->elem);
        dc->row[i]->elem = NULL;
        free(dc->row[i]);
        dc->row[i] = NULL;
    }
    // for (i=win_s; i<=win_e; i++) {
    //     printf("[Decoder] delay of source packet %d : %d\n", i, win_e-i+1);
    // }
    // Deactivate
    printf("[Decoder] Inactivating decoder at time %d, DW window [%d, %d] of width: %d active-duration: %d new in-order: %d\n", slot, dc->win_s, dc->win_e, width, slot-(sent_time[dc->win_s]+T_P), win_e);
    dc->inorder = win_e;
    dc->dof = 0;
    dc->win_s = -1;
    dc->win_e = -1;
    dc->active = 0;
    // NOTE: The active time timer is triggered when the #win_s source packet is lost. The busy period time should not 
    // count in that time slot, because the analytical busy period counts from when the state is already in X=w-d=1, rather than 0.
    return 0;
}