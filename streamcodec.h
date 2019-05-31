#include <stdio.h>
#include <string.h>
#include "galois.h"

#define ALIGN(a, b) ((a) % (b) == 0 ? (a)/(b) : (a)/(b) + 1)

struct parameters {
    int     gfpower;                // n of GF(2^n)
    int     pktsize;                // number of bytes per packet
    //int     repintvl;               // interval of repair packets
    double  repfreq;                // frequency (probability) of sending repair packet
};

struct packet {
    int     sourceid;               // source packet id
    int     repairid;               // repair packet id, -1 if it's a source packet
    int     win_s;                  // start of repair encoding window
    int     win_e;                  // end of repair encoding window
    GF_ELEMENT  *coes;              // (win_e-win_s+1) encoding coefficients 
    GF_ELEMENT  *syms;              // source or coded symbols
};

struct encoder {
    struct parameters *cp;          // code parameter
    int         count;              // total number of sent packets
    int         nextsid;            // id of source packet next to send
    int         rcount;             // number of sent repair packets 
    int         snum;               // number of actual available source packets (snum<=bufsize, realloc(ENC_ALLOC) when needed)
    int         bufsize;            // buffer size for source packets
    GF_ELEMENT  **srcpkt;           // available source packets for encoding
};

typedef struct row_vector {
    int         len;
    GF_ELEMENT  *elem;
} ROW_VEC;

struct decoder {
    struct parameters *cp;           // code parameter    
    int         active;             // whether the decoder is active
    int         inorder;            // index of the last in-order received packet
    int         win_s;              // start of decoding window
    int         win_e;              // end of decoding window
    int         dof;                // received DoF in the decoding window
    ROW_VEC     **row;
    GF_ELEMENT  **message;
    GF_ELEMENT  **recovered;
};

// encoder functions
struct encoder *initialize_encoder(struct parameters *cp, unsigned char *buf, int nbytes);
int enqueue_packet(struct encoder *ec);
struct packet *generate_packet(struct encoder *ec);
void free_packet(struct packet *pkt);

// decoder functions
struct decoder *initialize_decoder(struct parameters *cp);
int activate_decoder(struct decoder *dc, struct packet *pkt);
int receive_packet(struct decoder *dc, struct packet *pkt);
int process_packet(struct decoder *dc, struct packet *pkt);