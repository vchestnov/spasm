#include <assert.h>
#include "spasm.h"


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
  Bnz = spasm_max(0, Ap[r_1 + 1] - Ap[r_0]);
  B = spasm_csr_alloc(Bn, Bm, Bnz, A->prime, (A->x != NULL) &&with_values);
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
	Bx[k] = Ax[px];
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
