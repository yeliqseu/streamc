/*-------------------- galois.h ---------------------------------
 * Internal header file of Galois field implementation. 
 *
 * Targeting on byte-based computation systems, we only implement 
 * GF(2^8). GF(2) is naturally included.
 *--------------------------------------------------------------*/
#ifndef GALOIS_H
#define GALOIS_H
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#ifndef GALOIS
#define GALOIS
typedef unsigned char GF_ELEMENT;
#endif
// Galois field arithmetic routines
int constructField(int gf_power);
uint8_t galois_add(uint8_t a, uint8_t b);
uint8_t galois_sub(uint8_t a, uint8_t b);
uint8_t galois_multiply(uint8_t a, uint8_t b);
uint8_t galois_divide(uint8_t a, uint8_t b);
void galois_multiply_region(uint8_t *src, uint8_t multiplier, int bytes);
void galois_multiply_add_region(uint8_t *dst, uint8_t *src, uint8_t multiplier, int bytes);
#endif
