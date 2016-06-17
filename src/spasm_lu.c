#include <assert.h>
#include <stdbool.h>
#include "spasm.h"

#ifdef SPASM_TIMING
uint64_t data_shuffling = 0;
#endif

#define DEBUG

/*
 * /!\ the ``row_permutation'' argument is NOT embedded in P. This means that :
 *  a) L is ***really*** lower-(triangular/trapezoidal)
 *  b) PLUQ = row_permutation*A
 */
spasm_lu * spasm_PLUQ(const spasm *A, const int *row_permutation, int keep_L) {
  int m, i, j, r, px, k;
  int *Up, *Uj, *qinv;
  spasm *U, *L, *LL;
  spasm_lu *N;

  m = A->m;
  N = spasm_LU(A, row_permutation, keep_L);
  L = N->L;
  U = N->U;
  r = U->n;
  Up = U->p;
  Uj = U->j;
  qinv = N->qinv;

  k = 1;
  for(j = 0; j < m; j++) {
    if (qinv[j] == -1) {
      qinv[j] = m - k;
      k++;
    }
  }

  /* permute the columns of U in place.
     U becomes really upper-trapezoidal. */
  for(i = 0; i < r; i++) {
    for(px = Up[i]; px < Up[i + 1]; px++) {
      Uj[px] = qinv[ Uj[px] ];
    }
  }

  if (keep_L) {
    /* permute the rows of L (not in place).
       L becomes really lower-trapezoidal. */
    LL = spasm_permute(L, N->p, SPASM_IDENTITY_PERMUTATION, SPASM_WITH_NUMERICAL_VALUES);
    N->L = LL;
    spasm_csr_free(L);
  }
  
  return N;
}

/**
 *   Computes a random linear combination of A[k:].
 *   returns TRUE iff it belongs to the row-space of U.
 *   This means that with proba >= 1-1/p, all pivots have been found.
 */
int spasm_early_abort(const spasm *A, const int *row_permutation, int k, const spasm *U, int nu) {
  int *Aj, *Ap, *Uj, *Up;
  int i, j, inew, n, m, ok;
  spasm_GFp prime, *y, *Ax, *Ux;

  n = A->n;
  m = A->m;
  prime = A->prime;
  Aj = A->j;
  Ap = A->p;
  Ax = A->x;
  Uj = U->j;
  Up = U->p;
  Ux = U->x;

  y = spasm_malloc(m * sizeof(spasm_GFp));
  for(j = 0; j < m; j++) {
    y[j] = 0;
  }

  for(i = k; i < n; i++) {
    inew = (row_permutation != NULL) ? row_permutation[i] : i;
    //if (inew == n-1)
    //printf("%d --> %d\n", i, inew);
    spasm_scatter(Aj, Ax, Ap[inew], Ap[inew + 1], rand() % prime, y, prime);
  }


  for(i = 0; i < nu; i++) {
    j = Uj[ Up[i] ];
    const spasm_GFp diagonal_entry = Ux[ Up[i] ];
    if (y[j] == 0) {
      continue;
    }
    const spasm_GFp d = (y[j] * spasm_GFp_inverse(diagonal_entry, prime)) % prime;
    spasm_scatter(Uj, Ux, Up[i], Up[i + 1], prime - d, y, prime);
  }

  ok = 1;
  for(j = 0; j < m; j++) {
      //printf("%d : %d\n", j, y[j]);
      if (y[j] != 0) {
        ok = 0;
        break;
      }
  }
  free(y);
  return ok; //0; //ok;
}

/*
 * compute a (somewhat) LU decomposition.
 *
 * r = min(n, m) is an upper-bound on the rank of A
 *
 * L n * r
 * U is r * m
 *
 * L*U == row_permutation*A
 *
 * qinv[j] = i if the pivot on column j is on row i. -1 if no pivot (yet) found
 * on column j.
 *
 */
spasm_lu *spasm_LU(const spasm * A, const int *row_permutation, int keep_L) {
    spasm *L, *U;
    spasm_lu *N;
    spasm_GFp *Lx, *Ux, *x;
    int *Lp, *Lj, *Up, *Uj, *p, *qinv, *xi;
    int n, m, r, ipiv, i, inew, j, top, px, lnz, unz, old_unz, prime, defficiency, verbose_step;
    int rows_since_last_pivot, early_abort_done;

#ifdef SPASM_TIMING
    uint64_t start;
#endif

    /* check inputs */
    assert(A != NULL);

    n = A->n;
    m = A->m;
    r = spasm_min(n, m);
    prime = A->prime;
    defficiency = 0;
    verbose_step = spasm_max(1, n / 1000);

    /* educated guess of the size of L,U */
    lnz = 4 * spasm_nnz(A) + n;
    unz = 4 * spasm_nnz(A) + n;

    /* get GFp workspace */
    x = spasm_malloc(m * sizeof(spasm_GFp));

    /* get int workspace */
    xi = spasm_malloc(3 * m * sizeof(int));
    spasm_vector_zero(xi, 3*m);

    /* allocate result */
    N = spasm_malloc(sizeof(spasm_lu));
    N->L = L = (keep_L) ? spasm_csr_alloc(n, r, lnz, prime, true) : NULL;
    N->U = U = spasm_csr_alloc(r, m, unz, prime, true);
    N->qinv = qinv = spasm_malloc(m * sizeof(int));
    N->p = p = spasm_malloc(n * sizeof(int));

    Lp = (keep_L) ? L->p : NULL;
    Up = U->p;

    for (i = 0; i < m; i++) {
      /* clear workspace */
      x[i] = 0;
      /* no rows pivotal yet */
      qinv[i] = -1;
    }

    for (i = 0; i < n; i++) {
      /* no rows exchange yet */
      p[i] = i;
    }

    /* no rows of U yet */
    for (i = 0; i <= r; i++) {
        Up[i] = 0;
    }
    old_unz = lnz = unz = 0;

    /* initialize early abort */
    rows_since_last_pivot = 0;
    early_abort_done = 0;

    /* --- Main loop : compute L[i] and U[i] ------------------- */
    for (i = 0; i < n; i++) {
      if (!keep_L && i - defficiency == r) {
        fprintf(stderr, "\n[LU] full rank reached ; early abort\n");
        break;
      }
 
      if (!keep_L && !early_abort_done && rows_since_last_pivot > 10 && (rows_since_last_pivot > (n/100))) {
          fprintf(stderr, "\n[LU] testing for early abort...");
          fflush(stderr);
          if (spasm_early_abort(A, row_permutation, i+1, U, i-defficiency)) {
            fprintf(stderr, "SUCCESS\n");
            break;
          } else {
            fprintf(stderr, "FAILED\n");
            fflush(stderr);
          }
          early_abort_done = 1;
      }

        /* --- Triangular solve: x * U = A[i] ---------------------------------------- */
      if (keep_L) {
        Lp[i] = lnz;                          /* L[i] starts here */
      }
      Up[i - defficiency] = unz;            /* U[i] starts here */

      /* not enough room in L/U ? realloc twice the size */
      if (keep_L && lnz + m > L->nzmax) {
          spasm_csr_realloc(L, 2 * L->nzmax + m);
      }
      if (unz + m > U->nzmax) {
          spasm_csr_realloc(U, 2 * U->nzmax + m);
      }
      Lj = (keep_L) ? L->j : NULL;
      Lx = (keep_L) ? L->x : NULL;
      Uj = U->j;
      Ux = U->x;

      inew = (row_permutation != NULL) ? row_permutation[i] : i;
      top = spasm_sparse_forward_solve(U, A, inew, xi, x, qinv);


      /* --- Find pivot and dispatch coeffs into L and U -------------------------- */
#ifdef SPASM_TIMING
      start = spasm_ticks();
#endif
      ipiv = -1;
      /* index of best pivot so far.*/

      for (px = top; px < m; px++) {
        /* x[j] is (generically) nonzero */
        j = xi[px];

        /* if x[j] == 0 (numerical cancelation), we just ignore it */
        if (x[j] == 0) {
            continue;
        }

        if (qinv[j] < 0) {
          /* column j is not yet pivotal ? */

          /* have found the pivot on row i yet ? */
          if (ipiv == -1 || j < ipiv) {
            ipiv = j;
          }
        } else if (keep_L) {
          /* column j is pivotal */
          /* x[j] is the entry L[i, qinv[j] ] */
          Lj[lnz] = qinv[j];
          Lx[lnz] = x[j];
          lnz++;
        }
      }

      /* pivot found ? */
      if (ipiv != -1) {
        old_unz = unz;
        //    printf("\n pivot found on row %d of A at column %d\n", inew, ipiv);

        /* L[i,i] <--- 1. Last entry of the row ! */
        if (keep_L) {
          Lj[lnz] = i - defficiency;
          Lx[lnz] = 1;
          lnz++;
        }

        qinv[ ipiv ] = i - defficiency;
        p[i - defficiency] = i;

        /* pivot must be the first entry in U[i] */
        Uj[unz] = ipiv;
        Ux[unz] = x[ ipiv ];
        unz++;

        /* send remaining non-pivot coefficients into U */
        for (px = top; px < m; px++) {
          j = xi[px];

          if (qinv[j] < 0) {
            Uj[unz] = j;
            Ux[unz] = x[j];
            unz++;
          }
        }

        /* reset early abort */
        rows_since_last_pivot = 0;
        early_abort_done = 0;
      } else {
        defficiency++;
        p[n - defficiency] = i;
        rows_since_last_pivot++;
      }

#ifdef SPASM_TIMING
      data_shuffling += spasm_ticks() - start;
#endif

      if ((i % verbose_step) == 0) {
        fprintf(stderr, "\rLU : %d / %d [|L| = %d / |U| = %d] -- current density= (%.3f vs %.3f) --- rank >= %d", i, n, lnz, unz, 1.0 * (m-top) / (m), 1.0 * (unz-old_unz) / m, i - defficiency);
        fflush(stderr);
      }
    }
    
    /* --- Finalize L and U ------------------------------------------------- */
    fprintf(stderr, "\n");

    /* remove extra space from L and U */
    Up[i - defficiency] = unz;
    spasm_csr_resize(U, i - defficiency, m);
    spasm_csr_realloc(U, -1);

    if (keep_L) {
      Lp[n] = lnz;
      spasm_csr_resize(L, n, n - defficiency);
      spasm_csr_realloc(L, -1);
    }

    free(x);
    free(xi);
    return N;
}


void spasm_free_LU(spasm_lu *X) {
  assert( X != NULL );
  spasm_csr_free(X->L);
  spasm_csr_free(X->U);
  free(X->qinv);
  free(X->p);
  free(X);
}


/** Computes the Schur complement, by eliminating the pivots located on
 *  rows p[0] ... p[n_pivots-1] of input matrix A.
 */
spasm *spasm_schur(const spasm *A, const int *p, const int n_pivots) {
  spasm *S;
  int *Sp, *Sj, Sn, Sm, m, n, snz, px, *xi, i, inew, top, j, *qinv, *q, verbose_step, *Ap, *Aj;
  spasm_GFp *Sx, *x;

  /* check inputs */
  assert(A != NULL);
  n = A->n;
  m = A->m;
  assert(n >= n_pivots);
  assert(m >= n_pivots);
  
  /* Get Workspace */
  Sn = n - n_pivots;
  Sm = m - n_pivots;
  snz = 4 * (Sn + Sm); /* educated guess */
  S = spasm_csr_alloc(Sn, Sm, snz, A->prime, SPASM_WITH_NUMERICAL_VALUES);
  qinv = spasm_malloc(m * sizeof(int));
  x = spasm_malloc(m * sizeof(spasm_GFp));
  xi = spasm_malloc(3 * m * sizeof(int));
  verbose_step = spasm_max(1, n / 1000);

  Ap = A->p;
  Aj = A->j;
  Sp = S->p;
  Sj = S->j;
  Sx = S->x;
  
  spasm_vector_zero(xi, 3*m);

  /* build qinv from A [i.e. the "cheap pivots"] */
  for(j = 0; j < m; j++) {
    qinv[j] = -1;
  }
  for(i = 0; i < n_pivots; i++) {
    inew = p[i];
    j = Aj[Ap[inew]];
    qinv[j] = inew;   /* (inew, j) is a pivot */
  }

  /* q sends the non-pivotal columns of A to the columns of S. It is not the inverse of qinv... */
  q = spasm_malloc(m * sizeof(int));
  i = 0;
  for(j=0; j<m; j++) {
    if (qinv[j] < 0) {
      q[j] = i;
      i++;
    } else {
      q[j] = -1;
    }
  }

  /* ---- compute Schur complement -----*/
  snz = 0;   /* non-zero in S */
  Sn = 0;    /* rows in S */
  fprintf(stderr, "Starting Schur complement computation...\n");
  for(i = n_pivots; i < n; i++){
    Sp[Sn] = snz;            /* S[i] starts here */
    
    /* not enough room in S ? realloc twice the size */
    if (snz + Sm > S->nzmax) {
      spasm_csr_realloc(S, 2 * S->nzmax + Sm);
      Sj = S->j;
      Sx = S->x;
    }
    
    /* triangular solve */
    inew = p[i];
    top = spasm_sparse_forward_solve(A, A, inew, xi, x, qinv);

    /* dispatch x in S */
    for (px = top; px < m; px++) {
      j = xi[px];
      
      if (x[j] == 0) {   /* if x[j] == 0 (numerical cancelation), we just ignore it */
	      continue;
      }
      
      /* send non-pivot coefficients into S */
      if (q[j] >= 0) {
	      Sj[snz] = q[j];
	      Sx[snz] = x[j];
	      snz++;
      }
    }
    Sn++;

    if ((i % verbose_step) == 0) {
        fprintf(stderr, "\rSchur : %d / %d [S=%d * %d, %d NNZ] -- current density= (%.3f)", i, n, Sn, Sm, snz, 1.0*snz / (1.0*Sm*Sn));
        fflush(stderr);
      }
  }
  /*finalize S*/
  fprintf(stderr, "\n");
  Sp[S->n] = snz;
  spasm_csr_realloc(S, -1);

  /* free extra workspace*/
  free(qinv);
  free(q);
  free(x);
  free(xi);

  return S;
}


