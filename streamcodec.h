#ifndef STREAMCODEC_H
#define STREAMCODEC_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef DEBUG
# define DEBUG_PRINT(x) printf x
#else
# define DEBUG_PRINT(x) do {} while (0)
#endif

#ifndef GALOIS
#define GALOIS
typedef unsigned char GF_ELEMENT;
#endif
#define N       624                 // used by mt-19937 PRNG
#define EWIN    100                 // "encoding window" for seeding PRNG (for coefficient synchronization)
#define DEC_ALLOC   10000           // buffer space allocated at decoder for recovered packets

#define ALIGN(a, b) ((a) % (b) == 0 ? (a)/(b) : (a)/(b) + 1)

typedef struct mt19937_rng {
    unsigned long   mt[N];          // the array for the state vector
    int             mti;            // initialize to mti==N+1, means mt[N] is not initialized
} MT19937;

struct parameters {
    int     gfpower;                // n of GF(2^n)
    int     pktsize;                // number of bytes per packet
    double  repfreq;                // frequency (probability) of sending repair packet
    int     seed;                   // seed for random coding coefficients
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
    // A ring buffer
    // a) Buffer size is doubled (initial ENC_ALLOC) when buffer is full
    // b) Acknowledged packets are flushed from the buffer
    // c) Encoding window [headsid, nextsid-1] (watch for wrap around of their location)
    int         bufsize;            // current buffer size
    int         snum;               // number of queued source packets (including flushed)
    int         head;               // head index of buffered source packets 
    int         tail;               // tail index of bufferred source packets
    int         headsid;            // source packet id of head
    int         tailsid;            // source packet id of tail
    GF_ELEMENT  **srcpkt;           // available source packets for encoding
    // A mt19973 PRNG for synchronizing encoding coefficients
    MT19937     prng;
};

typedef struct row_vector {
    int         len;
    GF_ELEMENT  *elem;
} ROW_VEC;

struct decoder {
    struct parameters *cp;           // code parameter    
    struct packet *pbuf;            // a single packet buffer for deserialize
    int         active;             // whether the decoder is active
    int         inorder;            // index of the last in-order received packet
    int         win_s;              // start of decoding window
    int         win_e;              // end of decoding window
    int         dof;                // received DoF in the decoding window
    ROW_VEC     **row;
    GF_ELEMENT  **message;
    GF_ELEMENT  **recovered;
    int         prev_rep;           // id of the previous received repair packet
    MT19937     prng;
};

// encoder functions
struct encoder *initialize_encoder(struct parameters *cp, unsigned char *buf, int nbytes);
int enqueue_packet(struct encoder *ec, int sourceid, GF_ELEMENT *syms);
struct packet *output_source_packet(struct encoder *ec);
struct packet *output_repair_packet(struct encoder *ec);
struct packet *output_repair_packet_short(struct encoder *ec, int ew_width);
void flush_acked_packets(struct encoder *ec, int ack_sid);
void visualize_buffer(struct encoder *ec);
void free_packet(struct packet *pkt);
unsigned char *serialize_packet(struct encoder *ec, struct packet *pkt);
void free_serialized_packet(unsigned char *pktstr);
void free_encoder(struct encoder *ec);

// decoder functions
struct decoder *initialize_decoder(struct parameters *cp);
int activate_decoder(struct decoder *dc, struct packet *pkt);
int deactivate_decoder(struct decoder *dc);
int receive_packet(struct decoder *dc, struct packet *pkt);
int process_packet(struct decoder *dc, struct packet *pkt);
struct packet *deserialize_packet(struct decoder *dc, unsigned char *pktstr);
void free_decoder(struct decoder *dc);

#endif  // STREAMCODEC_H