/*
 *      BCHCode.c
 *
 *      Copyright (C) 2015 Craig Shelley (craig@microtron.org.uk)
 *
 *      BCH Encoder/Decoder - Adapted from GNURadio for use with Multimon
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <pager/bch_code.h>
#include <tsl/assert.h>
#include <tsl/safe_alloc.h>

#include <math.h>
#include <stdlib.h>

struct bch_code {
    int *p;          // coefficients of primitive polynomial used to generate GF(2**5)
    int m;           // order of the field GF(2**5) = 5
    int n;           // 2**5 - 1 = 31
    int k;           // n - deg(g(x)) = 21 = dimension
    int t;           // 2 = error correcting capability
    int *alpha_to;   // log table of GF(2**5)
    int *index_of;   // antilog table of GF(2**5)
    int *g;          // coefficients of generator polynomial, g(x) [n - k + 1]=[11]
    int *bb;         // coefficients of redundancy polynomial ( x**(10) i(x) ) modulo g(x)
};

static
void generate_gf(struct bch_code *bch_code_data)
{
    TSL_BUG_ON(NULL == bch_code_data);
    /*
     * generate GF(2**m) from the irreducible polynomial p(X) in p[0]..p[m]
     * lookup tables:  index->polynomial form   alpha_to[] contains j=alpha**i;
     * polynomial form -> index form  index_of[j=alpha**i] = i alpha=2 is the
     * primitive element of GF(2**m)
     */

    register int    i, mask;
    mask = 1;
    bch_code_data->alpha_to[bch_code_data->m] = 0;
    for (i = 0; i < bch_code_data->m; i++) {
        bch_code_data->alpha_to[i] = mask;
        bch_code_data->index_of[bch_code_data->alpha_to[i]] = i;
        if (bch_code_data->p[i] != 0)
            bch_code_data->alpha_to[bch_code_data->m] ^= mask;
        mask <<= 1;
    }
    bch_code_data->index_of[bch_code_data->alpha_to[bch_code_data->m]] = bch_code_data->m;
    mask >>= 1;
    for (i = bch_code_data->m + 1; i < bch_code_data->n; i++) {
        if (bch_code_data->alpha_to[i - 1] >= mask)
            bch_code_data->alpha_to[i] = bch_code_data->alpha_to[bch_code_data->m] ^ ((bch_code_data->alpha_to[i - 1] ^ mask) << 1);
        else
            bch_code_data->alpha_to[i] = bch_code_data->alpha_to[i - 1] << 1;
        bch_code_data->index_of[bch_code_data->alpha_to[i]] = i;
    }
    bch_code_data->index_of[0] = -1;
}


static
void gen_poly(struct bch_code *bch_code_data)
{
    TSL_BUG_ON(NULL == bch_code_data);
    /*
     * Compute generator polynomial of BCH code of length = 31, redundancy = 10
     * (OK, this is not very efficient, but we only do it once, right? :)
     */

    int             ii, jj, ll, kaux;
    int             test, aux, nocycles, root, noterms, rdncy;
    int             cycle[15][6], size[15], min[11], zeros[11];
    /* Generate cycle sets modulo 31 */
    cycle[0][0] = 0; size[0] = 1;
    cycle[1][0] = 1; size[1] = 1;
    jj = 1;         /* cycle set index */
    do {
        /* Generate the jj-th cycle set */
        ii = 0;
        do {
            ii++;
            cycle[jj][ii] = (cycle[jj][ii - 1] * 2) % bch_code_data->n;
            size[jj]++;
            aux = (cycle[jj][ii] * 2) % bch_code_data->n;
        } while (aux != cycle[jj][0]);
        /* Next cycle set representative */
        ll = 0;
        do {
            ll++;
            test = 0;
            for (ii = 1; ((ii <= jj) && (!test)); ii++) {
                /* Examine previous cycle sets */
                for (kaux = 0; ((kaux < size[ii]) && (!test)); kaux++) {
                    if (ll == cycle[ii][kaux]) {
                        test = 1;
                    }
                }
            }
        } while ((test) && (ll < (bch_code_data->n - 1)));
        if (!(test)) {
            jj++;   /* next cycle set index */
            cycle[jj][0] = ll;
            size[jj] = 1;
        }
    } while (ll < (bch_code_data->n - 1));
    nocycles = jj;      /* number of cycle sets modulo bch_code_data->n */
    /* Search for roots 1, 2, ..., bch_code_data->d-1 in cycle sets */
    kaux = 0;
    rdncy = 0;
    for (ii = 1; ii <= nocycles; ii++) {
        min[kaux] = 0;
        for (jj = 0; jj < size[ii]; jj++) {
            for (root = 1; root < (2*bch_code_data->t + 1); root++) {
                if (root == cycle[ii][jj]) {
                    min[kaux] = ii;
                }
            }
        }
        if (min[kaux]) {
            rdncy += size[min[kaux]];
            kaux++;
        }
    }
    noterms = kaux;
    kaux = 1;
    for (ii = 0; ii < noterms; ii++) {
        for (jj = 0; jj < size[min[ii]]; jj++) {
            zeros[kaux] = cycle[min[ii]][jj];
            kaux++;
        }
    }
    //printf("This is a (%d, %d, %d) binary BCH code\n", bch_code_data->n, bch_code_data->k, bch_code_data->d);
    /* Compute generator polynomial */
    bch_code_data->g[0] = bch_code_data->alpha_to[zeros[1]];
    bch_code_data->g[1] = 1;     /* g(x) = (X + zeros[1]) initially */
    for (ii = 2; ii <= rdncy; ii++) {
        bch_code_data->g[ii] = 1;
        for (jj = ii - 1; jj > 0; jj--) {
            if (bch_code_data->g[jj] != 0)
                bch_code_data->g[jj] = bch_code_data->g[jj - 1] ^ bch_code_data->alpha_to[(bch_code_data->index_of[bch_code_data->g[jj]] + zeros[ii]) % bch_code_data->n];
            else
                bch_code_data->g[jj] = bch_code_data->g[jj - 1];
        }
        bch_code_data->g[0] = bch_code_data->alpha_to[(bch_code_data->index_of[bch_code_data->g[0]] + zeros[ii]) % bch_code_data->n];
    }
    //printf("g(x) = ");
    //for (ii = 0; ii <= rdncy; ii++) {
    //  printf("%d", bch_code_data->g[ii]);
    //  if (ii && ((ii % 70) == 0)) {
    //      printf("\n");
    //  }
    //}
    //printf("\n");
}


void bch_code_encode(struct bch_code *bch_code_data, int data[])
{
    TSL_BUG_ON(NULL == bch_code_data);
    /*
     * Calculate redundant bits bb[], codeword is c(X) = data(X)*X**(n-k)+ bb(X)
     */

    register int    i, j;
    register int    feedback;
    for (i = 0; i < bch_code_data->n - bch_code_data->k; i++) {
        bch_code_data->bb[i] = 0;
    }
    for (i = bch_code_data->k - 1; i >= 0; i--) {
        feedback = data[i] ^ bch_code_data->bb[bch_code_data->n - bch_code_data->k - 1];
        if (feedback != 0) {
            for (j = bch_code_data->n - bch_code_data->k - 1; j > 0; j--) {
                if (bch_code_data->g[j] != 0) {
                    bch_code_data->bb[j] = bch_code_data->bb[j - 1] ^ feedback;
                } else {
                    bch_code_data->bb[j] = bch_code_data->bb[j - 1];
                }
            }
            bch_code_data->bb[0] = bch_code_data->g[0] && feedback;
        } else {
            for (j = bch_code_data->n - bch_code_data->k - 1; j > 0; j--) {
                bch_code_data->bb[j] = bch_code_data->bb[j - 1];
            }
            bch_code_data->bb[0] = 0;
        };
    };
};

#if 0
int bch_code_decode(struct bch_code *bch_code_data, int recd[])
{
    TSL_BUG_ON(NULL == bch_code_data);

    /*
     * We do not need the Berlekamp algorithm to decode.
     * We solve before hand two equations in two variables.
     */

    register int    i, j, q;
    int             elp[3], s[5], s3;
    int             count = 0, syn_error = 0;
    int             loc[3], reg[3];
    int             aux;
    int retval=0;
    /* first form the syndromes */
    //  printf("s[] = (");
    for (i = 1; i <= 4; i++) {
        s[i] = 0;
        for (j = 0; j < bch_code_data->n; j++) {
            if (recd[j] != 0) {
                s[i] ^= bch_code_data->alpha_to[(i * j) % bch_code_data->n];
            }
        }
        if (s[i] != 0) {
            syn_error = 1;  /* set flag if non-zero syndrome */
        }
        /* NOTE: If only error detection is needed,
         * then exit the program here...
         */
        /* convert syndrome from polynomial form to index form  */
        s[i] = bch_code_data->index_of[s[i]];
        //printf("%3d ", s[i]);
    };
    //printf(")\n");
    if (syn_error) {    /* If there are errors, try to correct them */
        if (s[1] != -1) {
            s3 = (s[1] * 3) % bch_code_data->n;
            if ( s[3] == s3 ) { /* Was it a single error ? */
                //printf("One error at %d\n", s[1]);
                recd[s[1]] ^= 1; /* Yes: Correct it */
            } else {
                /* Assume two errors occurred and solve
                 * for the coefficients of sigma(x), the
                 * error locator polynomail
                 */
                if (s[3] != -1) {
                    aux = bch_code_data->alpha_to[s3] ^ bch_code_data->alpha_to[s[3]];
                } else {
                    aux = bch_code_data->alpha_to[s3];
                }
                elp[0] = 0;
                elp[1] = (s[2] - bch_code_data->index_of[aux] + bch_code_data->n) % bch_code_data->n;
                elp[2] = (s[1] - bch_code_data->index_of[aux] + bch_code_data->n) % bch_code_data->n;
                //printf("sigma(x) = ");
                //for (i = 0; i <= 2; i++) {
                //  printf("%3d ", elp[i]);
                //}
                //printf("\n");
                //printf("Roots: ");
                /* find roots of the error location polynomial */
                for (i = 1; i <= 2; i++) {
                    reg[i] = elp[i];
                }
                count = 0;
                for (i = 1; i <= bch_code_data->n; i++) { /* Chien search */
                    q = 1;
                    for (j = 1; j <= 2; j++) {
                        if (reg[j] != -1) {
                            reg[j] = (reg[j] + j) % bch_code_data->n;
                            q ^= bch_code_data->alpha_to[reg[j]];
                        }
                    }
                    if (!q) {   /* store error location number indices */
                        loc[count] = i % bch_code_data->n;
                        count++;
                        //printf("%3d ", (i%n));
                    }
                }
                //printf("\n");
                if (count == 2) {
                    /* no. roots = degree of elp hence 2 errors */
                    for (i = 0; i < 2; i++)
                        recd[loc[i]] ^= 1;
                } else {    /* Cannot solve: Error detection */
                    retval=1;
                    //for (i = 0; i < 31; i++) {
                    //  recd[i] = 0;
                    //}
                    //printf("incomplete decoding\n");
                }
            }
        } else if (s[2] != -1) {/* Error detection */
            retval=1;
            //for (i = 0; i < 31; i++) recd[i] = 0;
            //printf("incomplete decoding\n");
        }
    }

    return retval;
}
#endif

int bch_code_decode(struct bch_code *bch_code_data, uint32_t *precd)
{
    TSL_BUG_ON(NULL == bch_code_data);
    TSL_BUG_ON(NULL == precd);

    /*
     * We do not need the Berlekamp algorithm to decode.
     * We solve before hand two equations in two variables.
     */

    register int    i, j, q;
    int             elp[3], s[5], s3;
    int             count = 0, syn_error = 0;
    int             loc[3], reg[3];
    int             aux;
    uint32_t        recd = *precd;
    int retval=0;

    /* first form the syndromes */
    for (i = 1; i <= 4; i++) {
        s[i] = 0;
        for (j = 0; j < bch_code_data->n; j++) {
            if ((recd >> (bch_code_data->n - 1 - j)) & 1) {
                s[i] ^= bch_code_data->alpha_to[(i * j) % bch_code_data->n];
            }
        }
        if (s[i] != 0) {
            syn_error = 1;  /* set flag if non-zero syndrome */
        }
        /* NOTE: If only error detection is needed,
         * then exit the program here...
         */
        /* convert syndrome from polynomial form to index form  */
        s[i] = bch_code_data->index_of[s[i]];
    }

    if (syn_error) {    /* If there are errors, try to correct them */
        if (s[1] != -1) {
            s3 = (s[1] * 3) % bch_code_data->n;
            if ( s[3] == s3 ) { /* Was it a single error ? */
                /* Correct the error */
                recd ^= 1 << (bch_code_data->n - 1 - s[1]);
            } else {
                /* Assume two errors occurred and solve
                 * for the coefficients of sigma(x), the
                 * error locator polynomail
                 */
                if (s[3] != -1) {
                    aux = bch_code_data->alpha_to[s3] ^ bch_code_data->alpha_to[s[3]];
                } else {
                    aux = bch_code_data->alpha_to[s3];
                }
                elp[0] = 0;
                elp[1] = (s[2] - bch_code_data->index_of[aux] + bch_code_data->n) % bch_code_data->n;
                elp[2] = (s[1] - bch_code_data->index_of[aux] + bch_code_data->n) % bch_code_data->n;

                /* find roots of the error location polynomial */
                for (i = 1; i <= 2; i++) {
                    reg[i] = elp[i];
                }
                count = 0;
                for (i = 1; i <= bch_code_data->n; i++) { /* Chien search */
                    q = 1;
                    for (j = 1; j <= 2; j++) {
                        if (reg[j] != -1) {
                            reg[j] = (reg[j] + j) % bch_code_data->n;
                            q ^= bch_code_data->alpha_to[reg[j]];
                        }
                    }
                    if (!q) {   /* store error location number indices */
                        loc[count] = i % bch_code_data->n;
                        count++;
                    }
                }
                if (count == 2) {
                    /* no. roots = degree of elp hence 2 errors */
                    for (i = 0; i < 2; i++) {
                        recd ^= (1 << (bch_code_data->n - 1 - loc[i]));
                    }
                } else {    /* Cannot solve: Error detection */
                    retval=1;
                }
            }
        } else if (s[2] != -1) {/* Error detection */
            retval=1;
        }
    }

    *precd = recd;

    return retval;
}

/*
 * Example usage BCH(31,21,5)
 *
 * p[] = coefficients of primitive polynomial used to generate GF(2**5)
 * m = order of the field GF(2**5) = 5
 * n = 2**5 - 1 = 31
 * t = 2 = error correcting capability
 * d = 2*bch_code_data->t + 1 = 5 = designed minimum distance
 * k = n - deg(g(x)) = 21 = dimension
 * g[] = coefficients of generator polynomial, g(x) [n - k + 1]=[11]
 * alpha_to [] = log table of GF(2**5)
 * index_of[] = antilog table of GF(2**5)
 * data[] = coefficients of data polynomial, i(x)
 * bb[] = coefficients of redundancy polynomial ( x**(10) i(x) ) modulo g(x)
 */
aresult_t bch_code_new(struct bch_code **pcode, const int p[], int m, int n, int k, int t)
{
    aresult_t ret = A_OK;

    struct bch_code *bch_code_data=NULL;

    if (FAILED(ret = TZAALLOC(bch_code_data, SYS_CACHE_LINE_LENGTH))) {
        goto done;
    }

    /* FIXME */
    if (bch_code_data!=NULL) {
        bch_code_data->alpha_to=(int *) malloc(sizeof(int) * (n+1));
        bch_code_data->index_of=(int *) malloc(sizeof(int) * (n+1));
        bch_code_data->p=(int *) malloc(sizeof(int) * (m+1));
        bch_code_data->g=(int *) malloc(sizeof(int) * (n-k+1));
        bch_code_data->bb=(int *) malloc(sizeof(int) * (n-k+1));

        if (
                bch_code_data->alpha_to == NULL ||
                bch_code_data->index_of == NULL ||
                bch_code_data->p        == NULL ||
                bch_code_data->g        == NULL ||
                bch_code_data->bb       == NULL
                ) {
            bch_code_delete(&bch_code_data);
        }
    }

    if (bch_code_data!=NULL) {
        int i;
        for (i=0; i<(m+1); i++) {
            bch_code_data->p[i]=p[i];
        }
        bch_code_data->m=m;
        bch_code_data->n=n;
        bch_code_data->k=k;
        bch_code_data->t=t;

        generate_gf(bch_code_data);          /* generate the Galois Field GF(2**m) */
        gen_poly(bch_code_data);             /* Compute the generator polynomial of BCH code */
    }

    *pcode = bch_code_data;

done:
    return ret;
}

void bch_code_delete(struct bch_code **pbch_code_data)
{
    struct bch_code *bch_code_data = NULL;
    TSL_BUG_ON(NULL == pbch_code_data);
    bch_code_data = *pbch_code_data;
    TSL_BUG_ON(NULL == bch_code_data);

    if (bch_code_data->alpha_to != NULL) free(bch_code_data->alpha_to);
    if (bch_code_data->index_of != NULL) free(bch_code_data->index_of);
    if (bch_code_data->p        != NULL) free(bch_code_data->p);
    if (bch_code_data->g        != NULL) free(bch_code_data->g);
    if (bch_code_data->bb       != NULL) free(bch_code_data->bb);

    free(bch_code_data);

    *pbch_code_data = NULL;
}

