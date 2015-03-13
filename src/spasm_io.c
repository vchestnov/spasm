#include <assert.h>
#include "spasm.h"

// set prime = -1 to avoid loading values
spasm_triplet * spasm_load_sms(FILE *f, int prime) {
    int i, j ;   /* use double for integers to avoid int conflicts */
    spasm_GFp x ;
    spasm_triplet *T;
    char type;
    assert(f != NULL);

    if (fscanf(f, "%d %d %c\n", &i, &j, &type) != 3) {
      fprintf(stderr, "[spasm_load_sms] bad SMS file (header)\n");
      exit(0);
    }

    if (prime != -1 && type != 'M') {
      fprintf(stderr, "[spasm_load_sms] only ``Modular'' type supported\n");
      exit(0);
    }

    /* allocate result */
    T = spasm_triplet_alloc (i, j, 1, prime, prime != -1);

    while (fscanf (f, "%d %d %d\n", &i, &j, &x) == 3) {
      if (i == 0 && j == 0 && x == 0) {
	break;
      }
      assert(i != 0);
      assert(j != 0);
      spasm_add_entry(T, i - 1, j - 1, x);
    }

    return T;
}


void spasm_save_csr(FILE *f, const spasm *A) {
  int i, n, m, p, prime;
  int *Aj, *Ap;
  spasm_GFp *Ax, x;

    assert(f != NULL);
    assert(A != NULL);

    Aj = A->j;
    Ap = A->p;
    Ax = A->x;
    n  = A->n;
    m  = A->m;
    prime = A->prime;

    fprintf(f, "%d %d M\n", n, m);
    for(i = 0; i < n; i++) {
      for(p = Ap[i]; p < Ap[i + 1]; p++) {
	x = (Ax != NULL) ? Ax[p] : 1;
	x = (x > prime / 2) ? x - prime : x;
	fprintf(f, "%d %d %d\n", i + 1, Aj[p] + 1, x);
      }
    }

    fprintf(f, "0 0 0\n");
}

void spasm_save_triplet(FILE *f, const spasm_triplet *A) {
  int i, nz, n, m;
  int *Ai, *Aj;
  spasm_GFp *Ax;

    assert(f != NULL);
    assert(A != NULL);
    Ai = A->i;
    Aj = A->j;
    Ax = A->x;
    nz = A->nz;
    n  = A->n;
    m  = A->m;

    fprintf(f, "%d %d M\n", n, m);

    for(i = 0; i < nz; i++) {
      fprintf(f, "%d %d %d\n", Ai[i] + 1, Aj[i] + 1, (Ax != NULL) ? Ax[i] : 1);
    }

    fprintf(f, "0 0 0\n");
}

/* Saves a PBM (bitmap) file with one pixel per entry of A */
void spasm_save_pbm(FILE *f, const spasm *A) {
  int i, j, n, m, p;
  int *Aj, *Ap, *x;

  assert(f != NULL);
  assert(A != NULL);

  Aj = A->j;
  Ap = A->p;
  n  = A->n;
  m  = A->m;
  x = spasm_malloc(m * sizeof(int));
  for(j = 0; j < m; j++) {
    x[j] = 0;
  }

  fprintf(f, "P1\n");
  fprintf(f, "%d %d\n", m, n);
  for(i = 0; i < n; i++) {

    // scatters row i to x
    for(p = Ap[i]; p < Ap[i + 1]; p++) {
      x[ Aj[p] ] = 1;
    }

    // print row i
    for(j = 0; j < m; j++) {
      fprintf(f, "%d ", x[j]);
    }

    // reset x
    for(p = Ap[i]; p < Ap[i + 1]; p++) {
      x[ Aj[p] ] = 0;
    }
  }

  free(x);
}

/* Saves a PBM (graymap) of specified dimensions of A */
void spasm_save_pgm(FILE *f, int x, int y, const spasm *A) {
  int i, j, k, n, m, t, p;
  int *Aj, *Ap, *w;
  double max;

  assert(f != NULL);
  assert(A != NULL);

  Aj = A->j;
  Ap = A->p;
  n  = A->n;
  m  = A->m;
  x = spasm_min(x, m);
  y = spasm_min(y, n);

  w = spasm_malloc(x * sizeof(int));
  for(j = 0; j < x; j++) {
    w[j] = 0;
  }

  fprintf(f, "P2\n");
  fprintf(f, "%d %d\n", x, y);
  fprintf(f, "255\n");

  max = (1.0 * m / x) * (1.0 * n / y);
  t = 0;
  i = 0;
  while(i < n) {
    for(k = 0; k < spasm_max(1, n / y) && i < n; k++) {

      // scatters row i to x
      for(p = Ap[i]; p < Ap[i + 1]; p++) {
	w[ (Aj[p] * x) / m ]++;
      }
      i++;
    }

    // print row
    for(j = 0; j < x; j++) {
      double intensity = 1.0 - w[j] / max;
      assert( 0 <= intensity && intensity <= 1.0 );
      fprintf(f, "%.0f ", 255.0 * intensity);
      t++;
      if ((t & 31) == 0) {
	fprintf(f, "\n");
      }
    }

    // reset x
    for(j = 0; j < x; j++) {
      w[j] = 0;
    }
  }

  free(w);
}

static void render_block(FILE *f, int m, int *Ap, int *Aj, spasm_partition *CC, spasm_partition **SCC, int *rr, int *cc,
			 int ri, int rj, int ci, int cj, int *colors, int *pixel) {
  int u, i, j, k, l, t, p, CC_n, CC_m, last_CC_row;

  t = 0;
  k = 0; // in which CC are we ?
  l = 0; // in which SCC are we ?
  assert(ci < cj);
  last_CC_row = CC->rr[ CC->nr ];

  for(i = rr[ri]; i < rr[rj]; i++) {

    spasm_vector_set(pixel, 0, m, 0xFFFFFF); // white

    // jump CC / SCC
    while(i < last_CC_row && i >= CC->rr[k+1]) {
      k++;
      l = 0;
    }
    while(i < last_CC_row && i >= SCC[k]->rr[l + 1]) {
      l++;
    }

    // are we in a matched row ?
    CC_n = CC->rr[k + 1] - CC->rr[k];
    CC_m = CC->cc[k + 1] - CC->cc[k];

    if (i < CC->rr[k + 1] - CC_m) {
      // unmatched BG
      for(j = CC->cc[k]; j <  CC->cc[k + 1]; j++) {
	pixel[j] = colors[0];
      }
    } else {
      /* diagonal block of the SCC */
      for(j = SCC[k]->cc[l]; j < SCC[k]->cc[l + 1]; j++) {
	pixel[j] = colors[1];
      }

      /* stuff on the right of the diagonal block, inside the CC */
      for(j = SCC[k]->cc[l + 1]; j < CC->cc[k + 1]; j++) {
	pixel[j] = colors[2];
      }

      for(u = cj; u < 4; u++) {
	// put the rest of the matrix
	for(j = cc[u]; j < cc[u + 1]; j++) {
	  pixel[j] = colors[3 + u - cj];
	}
      }
    }

    // scatters row i to black pixels
    for(p = Ap[i]; p < Ap[i + 1]; p++) {
      pixel[ Aj[p] ] = 0;
    }

    // dump the "pixel" array
    for(j = 0; j < m; j++) {
      fprintf(f, "%d %d %d ", (pixel[j] >> 16) & 0xFF, (pixel[j] >> 8) & 0xFF, pixel[j] & 0xFF);
      t++;
      if ((t & 7) == 0) {
	fprintf(f, "\n");
      }
    }
  }
  fprintf(f, "\n");
}

/* Saves a PPM (color pixmap) of specified dimensions of A, with an optional DM decomposition */
void spasm_save_ppm(FILE *f, const spasm *A, const spasm_dm *X) {
  int i, j, n, m, t;
  int *Aj, *Ap, *rr, *cc, *pixel;
  spasm_cc *H, *S, *V;

  int colors[13] = { 0,        0xFF0000, 0xFF6633, 0xCC0000, 0x990000,
		     0xFFFF66, 0xFFCC00, 0xCC9900,
		     0x669933, 0x99FF99, 0x33CC00};

  assert(f != NULL);
  assert(A != NULL);
  assert(X != NULL);

  Aj = A->j;
  Ap = A->p;
  n  = A->n;
  m  = A->m;

  pixel = spasm_malloc(m * sizeof(int));

  rr = X->DM->rr;
  cc = X->DM->cc;

  fprintf(f, "P3\n");
  fprintf(f, "%d %d\n", m, n);
  fprintf(f, "255\n");

  H = X->H;
  S = X->S;
  V = X->V;

  t = 0;

  /* --- H ---- */
  if (H != NULL) {
    render_block(f, m, Ap, Aj, H->CC, H->SCC, rr, cc, 0, 1, 0, 2, colors, pixel);
  }

  /* --- S ---- */
  if (S != NULL) {
    render_block(f, m, Ap, Aj, S->CC, S->SCC, rr, cc, 1, 2, 2, 3, colors + 4, pixel);
  }

  /* --- V ---- */
  if (V != NULL) {
      render_block(f, m, Ap, Aj, V->CC, V->SCC, rr, cc, 2, 4, 3, 4, colors + 8, pixel);
  } else {
    /* if V is empty, we need to print the empty rows */
    t = 0;
    for(i = rr[2]; i < rr[4]; i++) {
      for(j = 0; j < m; j++) {
	fprintf(f, "255 255 255 ");
	t++;
	if ((t & 7) == 0) {
	  fprintf(f, "\n");
	}
      }
    }
    fprintf(f, "\n");
  }


  fprintf(f, "\n");
  free(pixel);
}
