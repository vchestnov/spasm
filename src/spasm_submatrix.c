#include <assert.h>
#include "spasm.h"

/**
 * returns A[r_0:r_1, c_0:c_1]
 */
spasm * spasm_submatrix(const spasm *A, int r_0, int r_1, int c_0, int c_1, int with_values) {
  spasm *B;
  int Bn, Bm, Bnz, i, j, px, k;
  int *Ap, *Aj, *Ax, *Bp, *Bj, *Bx;

  assert(A != NULL);
  Ap = A->p;
  Aj = A->j;
  Ax = A->x;

  Bn = spasm_max(0, r_1 - r_0);
  Bm = spasm_max(0, c_1 - c_0);
  Bnz = spasm_max(0, Ap[r_1] - Ap[r_0]);
  B = spasm_csr_alloc(Bn, Bm, Bnz, A->prime, (A->x != NULL) && with_values);
  Bp = B->p;
  Bj = B->j;
  Bx = B->x;

  k = 0;
  for(i = r_0; i < r_1; i++) {
    Bp[i - r_0] = k;
    for(px = Ap[i]; px < Ap[i + 1]; px++) {
      j = Aj[px];
      if (c_0 <= j && j < c_1) {
	Bj[k] = j - c_0;
	if (Bx != NULL) {
	  Bx[k] = Ax[px];
	}
	k++;
      }
    }
  }

  /* finalize */
  Bp[r_1 - r_0] = k;

  /* shrink */
  spasm_csr_realloc(B, -1);
  return B;
}

/*
 * returns A[r0 : r1, c0 : c1] when the column of A are sorted.  
 */
spasm * sorted_spasm_submatrix(const spasm *A, int r0, int r1, int c0, int c1, int *py, int with_values) {
  spasm *B;
  int Bn, Bm, Bnz, i, j, px, k;
  int *Ap, *Aj, *Ax, *Bp, *Bj, *Bx;

  assert(A != NULL);
  Ap = A->p;
  Aj = A->j;
  Ax = A->x;

  Bn = spasm_max(0, r1 - r0);
  Bm = spasm_max(0, c1 - c0);
  Bnz = spasm_max(0, Ap[r1] - Ap[r0]);
  B = spasm_csr_alloc(Bn, Bm, Bnz, A->prime, (A->x != NULL) && with_values);
  Bp = B->p;
  Bj = B->j;
  Bx = B->x;

 k = 0;
  for(i = r0; i < r1; i++) {
    Bp[i - r0] = k;

    px = py[i]; //<--- pointer on the first entry on row i
    
    if(px != -1){
      if(Aj[px] < c0) {
	printf("error row %d ----> Aj[%d] = %d\n", i, px, Aj[px]);
	// assert(Aj[px] >= c0);
	exit(-1);
      }
 
      while(px < Ap[i+1] && Aj[px] < c1) {
	j = Aj[px];
     
      	Bj[k] = j - c0;
	
	if (Bx != NULL) {
	  Bx[k] = Ax[px];
	}
	px++;
	k++;
      }
      if (px == Ap[i+1]) py[i] = -1; // <-- no more entries on row i.
      else py[i] = px; // Update py[i];
      if (px < Ap[i+1] && Aj[px] < c1) {
	printf("row %d Error\n", i);
      }
    }
  }
  /* finalize */
  Bp[r1 - r0] = k;

  /* shrink */
  spasm_csr_realloc(B, -1);
  return B;
}

/*
 * Given a matrix A, a table Q such that Q[j] is the index 
 * of the interval of columns of j, and a table T, such that 
 * T[k] is the first columns of interval J_k. return a table
 * of matrices B, such that B[k] = A[:J_k]; 
 */
void spasm_columns_submatrices(const spasm *A, const int *Q, const int *T, int N, spasm **B, int with_values){
  int k, i, j, px, An, prime;
  int *Aj, *Ap, *Bnz, *Bm;
  int **Bj, **Bp;
  spasm_GFp *Ax;
  spasm_GFp **Bx;

  assert(A != NULL);
  assert(Q != NULL);
  assert(T != NULL);

  An = A->n;
  Aj = A->j;
  Ax = A->x;
  Ap = A->p;
  prime = A->prime;

  // Get workspace
  Bj = spasm_malloc(N * sizeof(int*));
  Bp = spasm_malloc(N * sizeof(int*));
  Bx = spasm_malloc(N * sizeof(spasm_GFp*));
  Bnz = spasm_malloc(N * sizeof(int));
  Bm = spasm_malloc(N * sizeof(int));

  // Initialize matrices.
  for(k = 0; k< N; k++){
    Bm[k] = T[k+1] - T[k]; // <--- T[N+1] = A->m.

    assert(Bm[k] >= 0);

    Bnz[k] = An + Bm[k] + k; // <--- educated gess.
    B[k] = spasm_csr_alloc(An, Bm[k], Bnz[k], prime, (Ax != NULL) && with_values);

    Bj[k] = B[k]->j;
    Bp[k] = B[k]->p;
    Bx[k] = B[k]->x;

    Bnz[k] = 0;
  }

  for(i = 0; i < An; i++){

    //Initialise Bp :
    for(k = 0; k < N; k++){
      Bp[k][i] = Bnz[k];
    }

    for(px = Ap[i]; px < Ap[i+1]; i++){
      j = Aj[px];
      k = Q[j]; // <-- index of the interval where j is.

      // update matrix B[k] :

      //reallocate memory if needed :
      if(Bnz[k] + 1 > B[k]->nzmax){
	spasm_csr_realloc(B[k], 2 * (Bnz[k] + 1) );
      }

      Bj[k][Bnz[k]] = j - T[k]; // corresponding column in B[k]
      if(Ax != NULL){
	Bx[k][Bnz[k]] = Ax[px];
      }
      Bnz[k]++;
    }
  }

  //Finalise B :

  for(k = 0; k < N; k++){
    Bp[k][N] = Bnz[k];
    spasm_csr_realloc(B[k], -1);
  }

}
