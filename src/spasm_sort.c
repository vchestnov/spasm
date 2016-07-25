/* indent -nfbs -i2 -nip -npsl -di0 -nut spasm_sort.c */
#include <assert.h>
#include "spasm.h"

#define INSERT_SORT_THRESHOLD 42/* TODO : tune this value */


/* sort up to index right, excluded */
static void insertion_sort(const spasm * A, int *p, const int left, const int right) {
  int i, j, u, v;

  /* if (left <= 0) { */
  for (i = left + 1; i < right; i++) {
    u = p[i];
    v = spasm_row_weight(A, p[i]);
    j = i - 1;
    while (j >= 0 && spasm_row_weight(A, p[j]) > v) {
      p[j + 1] = p[j];
      j--;
    }
    p[j + 1] = u;
  }
  /*
   * } else { TODO, possible optimization : if ( left>0 ), then we know that
   * on the left of the current subfile, there is an element smaller than all
   * the elements of the subfile (because this element was a pivot).
   * Therefore, we don't have to check explicitly that we attained the left
   * boundary of the array...
   */
}


/* standard median-of-three pivoting strategy. Returns the pivot index */
static int choose_pivot(const spasm * A, int *p, const int left, const int right) {
  int mid = (left + right) / 2;

  if (spasm_row_weight(A, p[mid - 1]) > spasm_row_weight(A, p[mid])) {
    spasm_swap(p, mid - 1, mid);
  }
  if (spasm_row_weight(A, p[mid - 1]) > spasm_row_weight(A, p[mid + 1])) {
    spasm_swap(p, mid - 1, mid + 1);
  }
  if (spasm_row_weight(A, p[mid]) > spasm_row_weight(A, p[mid + 1])) {
    spasm_swap(p, mid, mid + 1);
  }
  return mid;
}


/* returns final position of pivot */
static int pivoting(const spasm * A, int *p, const int initial_left, const int initial_right, const int pivotIndex) {
  int pivotValue, left, right;

  spasm_swap(p, pivotIndex, initial_right - 1);
  pivotValue = spasm_row_weight(A, p[initial_right - 1]);

  right = initial_right - 2;
  left = initial_left;

  while (left < right) {
    while (spasm_row_weight(A, p[left]) < pivotValue) {
      left++;
    }
    while (spasm_row_weight(A, p[right]) > pivotValue) {
      right--;
    }

    if (left < right) {
      spasm_swap(p, left, right);
      left++;
    }
  }

  if (spasm_row_weight(A, p[right]) < pivotValue) {
    right++;
  }
  spasm_swap(p, right, initial_right - 1);
  return right;
}

static void spasm_quicksort(const spasm * A, int *p, const int left, const int right) {
  int pivotIndex, new_pivotIndex;

  if (right - left > INSERT_SORT_THRESHOLD) {

    pivotIndex = choose_pivot(A, p, left, right);
    new_pivotIndex = pivoting(A, p, left, right, pivotIndex);

    spasm_quicksort(A, p, left, new_pivotIndex);
    spasm_quicksort(A, p, new_pivotIndex + 1, right);
  } else {
    insertion_sort(A, p, left, right);
  }
}


int *spasm_row_sort(const spasm * A) {
  int *p;
  int i, n;

  n = A->n;
  p = spasm_malloc(n * sizeof(int));
  for (i = 0; i < n; i++) {
    p[i] = i;
  }
  spasm_quicksort(A, p, 0, n);
  return p;
}


/* --- Faugère-Lachartre pivot detection ------------------ */
int spasm_find_FL_pivots(const spasm * A, int *p, int *qinv) {
  int i, j, inew, n, m, px, idx_j, *w, *Aj, *Ap, npiv, npiv_fl;
  spasm_GFp *Ax;

  n = A->n;
  m = A->m;
  Ap = A->p;
  Aj = A->j;
  Ax = A->x;
  w = spasm_malloc(m * sizeof(int));  

  for (i = 0; i < n; i++) {
    /* find leftmost entry */
    j = -1;
    for (px = Ap[i]; px < Ap[i + 1]; px++) {
      if (j == -1 || Aj[px] < j) {
        j = Aj[px];
        idx_j = px;
      }
    }
    /* Skip empty rows */
    if (j == -1) {
      continue;
    }
    /* make sure leftmost entry is the first of the row */
    spasm_swap(Aj, Ap[i], idx_j);
    if (Ax != NULL) {
      spasm_swap(Ax, Ap[i], idx_j);
    }
    
    /* check if it is a sparser pivot */
    if (qinv[j] == -1 || spasm_row_weight(A, i) < spasm_row_weight(A, qinv[j])) {
      qinv[j] = i;
    }
  }

  /* count the Faugère-Lachartre pivots, and store their rows in p */
  npiv = 0;
  for (j = 0; j < m; j++) {
    if (qinv[j] != -1) {
      p[npiv++] = qinv[j];
    }
  }
  npiv_fl = npiv;
  fprintf(stderr, "[pivots] found %d Faugère-Lachartre pivots\n", npiv_fl);


/* --- free column pivots ----------------------------------*/
  spasm_vector_set(w, 0, m, 1);
  
  /* scatter previous pivot rows into w */
  for (i = 0; i < npiv; i++) {
    inew = p[i];
    for (px = Ap[inew]; px < Ap[inew + 1]; px++) {
      j = Aj[px];
      w[j] = 0;
    }
  }

  /* find new pivots */
  for (i = 0; i < n; i++) {
    j = Aj[Ap[i]];
    if (qinv[j] == i) {  /* this row is already pivotal: skip */
      continue;
    }
   
    /* does it have an entry on a possible column? */
    for (px = Ap[i]; px < Ap[i + 1]; px++) {
      j = Aj[px];
      if (w[j] == 0) {
        continue; /* this column is closed, skip this entry */
      }

      /* new pivot found! */
      if (qinv[j] == -1) {
        npiv++;
        qinv[j] = i;
        spasm_swap(Aj, Ap[i], px);
        if (Ax != NULL) {
          spasm_swap(Ax, Ap[i], px);
        }
        /* close more columns */     
        for (px = Ap[i]; px < Ap[i + 1]; px++) {
          j = Aj[px];
          w[j] = 0;
        }
        break;   /* this row is done */
      }
    } 
  }
  fprintf(stderr, "[pivots] %d pivots found on free columns\n", npiv - npiv_fl);
  return npiv;
}


/* provide already know pivots, and this looks for more. Updates qinv, but DFS must be performed afterwards */
int spasm_find_cycle_free_pivots(const spasm * A, int *p, int *qinv, int npiv_start) {
  int i, j, px, n, m, I, head, tail, *Aj, *Ap, processed, *w, *queue, v, npiv, surviving;
  spasm_GFp *Ax;

  n = A->n;
  m = A->m;
  Ap = A->p;
  Aj = A->j;
  Ax = A->x;
  v = spasm_max(1, spasm_min(1000, n / 100));

  w = spasm_malloc(m * sizeof(int));
  queue = spasm_malloc(m * sizeof(int));

  processed = 0;
  npiv = npiv_start;

  /* workspace initialization */
  spasm_vector_set(w, 0, m, 0);

  for (i = 0; i < n; i++) {
    /* for each non-pivotal row, computes the columns reachable from its entries by alternating paths.
    * Unreachable entries on the row can be chosen as pivots. The w[] array is used for marking during the graph traversal.
    * Before the search:
    *   w[j] ==  1  for each non-pivotal entry j on the row
    *   w[j] ==  0  otherwise
    * After the search:
    *   w[j] ==  1  for each unreachable non-pivotal entry j on the row
    *   w[j] == -1  column j is reachable by an alternating path, or is pivotal (has entered the queue at some point)
    *   w[j] ==  0  column j was absent and is unreachable
    */

    if (i % v == 0) {
      fprintf(stderr, "\rcheap : %d / %d --- found %d new", processed, n - npiv_start, npiv - npiv_start);
      fflush(stderr);
    }
    if (qinv[Aj[Ap[i]]] == i) {    /* this row is already pivotal: skip */
      continue;
    }
    processed++;

    /* we start reading qinv: begining of transaction */

    /* scatters columns of A[i] into w, enqueue pivotal entries */
    head = 0;
    tail = 0;
    surviving = 0;
    for (px = Ap[i]; px < Ap[i + 1]; px++) {
      j = Aj[px];
      if (qinv[j] == -1) {
        /* original non-pivotal column */
        w[j] = 1;
        surviving++;
      } else {
        /* enqueue the entry, and mark it as reachable */
        w[j] = -1;
        queue[tail++] = j;
      }
    }

    /* BFS */
    while (head < tail && surviving > 0) {
      /* dequeue column j */
      j = queue[head++];
      /* is it pivotal ? */
      I = qinv[j];
      if (I == -1) {
        continue;             /* no: nothing to do */
      }
  
      /* yes: enqueue all the entries on the pivot row */
      for (px = Ap[I]; px < Ap[I + 1]; px++) {
        j = Aj[px];
        if (w[j] < 0) {
          continue;           /* already marked : do not enqueue */
        }
        queue[tail++] = j;
        surviving -= w[j];
        w[j] = -1;
      }
    }

    /* scan w for surviving entries */
    if (surviving > 0) {
      for (px = Ap[i]; px < Ap[i + 1]; px++) {
        j = Aj[px];
        if (w[j] == 1) { /* potential pivot found */
          /* make sure selected pivot entry is the first of the row */
          spasm_swap(Aj, Ap[i], px);
          if (Ax != NULL) {
            spasm_swap(Ax, Ap[i], px);
          }
          /* register the pivot. */
          /* if no more pivots have been found since the start of this row, then it is OK
           * and we can store the newly found pivot. Otherwise*/
          qinv[j] = i;
          p[npiv++] = i;
          break;
        }
      }
    }

    /* reset w back to zero */
    for (px = Ap[i]; px < Ap[i + 1]; px++) {
        j = Aj[px];
        w[j] = 0;
    }
    for (px = 0; px < tail; px++) {
      j = queue[px];
      w[j] = 0;
    }
  }

  fprintf(stderr, "\r[pivots] found %d cheap pivots (greedy search)\n", npiv - npiv_start);
  free(w);
  free(queue);
  return npiv;
}

/* return the number of pivots found. 
  @param p : row permutations. Pivotal rows are first.
  @param qinv : inverse column permutation. q[j] is the row on which
                the pivot on column j is, or -1 if there is no pivot
                on column j.
  both p and qinv must be preallocated
*/
int spasm_find_pivots(const spasm * A, int *p, int *qinv) {
  int n, m, i, j, k, npiv;
  int *Ap, *Aj;
  spasm_GFp *Ax;

  n = A->n;
  m = A->m;
  Ap = A->p;
  Aj = A->j;
  Ax = A->x;

  spasm_vector_set(qinv, 0, m, -1);
  npiv = spasm_find_FL_pivots(A, p, qinv);
#if 1
  npiv = spasm_find_cycle_free_pivots(A, p, qinv, npiv);
#endif

  /* --- build corresponding row permutation ---------------------- */
  int *xj = spasm_malloc(m * sizeof(int));
  int *marks = spasm_malloc(m * sizeof(int));
  int *pstack = spasm_malloc(n * sizeof(int));
  
  /* DFS */
  spasm_vector_set(marks, 0, m, 0);
  int top = m;
  for (j = 0; j < m; j++) {
    if (qinv[j] != -1 && !marks[j]) {
      top = spasm_dfs(j, A, top, xj, pstack, marks, qinv);
    }
  }

  /* reorders the first n_cheap rows of p */
  k = 0;
  for (j = top; j < m; j++) {
    i = qinv[xj[j]];
    if (i != -1) {
      p[k] = i;
      k++;
    }
  }
  assert(k == npiv);
  free(xj);
  free(pstack);
  free(marks);


  /* put other (non-empty) rows afterwards */
  for (i = 0; i < n; i++) {
    if (Ap[i] == Ap[i + 1]) {
      continue;
    }
    j = Aj[Ap[i]];
    if (qinv[j] != i) { /* row is non-pivotal */
      assert(k < n);
      p[k] = i;
      k++;
    }
  }

  /* put empty rows last */
  for (i = 0; i < n; i++) {
    if (Ap[i] == Ap[i + 1]) {
      p[k] = i;
      k++;
    }
  }

  return npiv;
}

/* returns a permuted version of A where pivots are pushed to the top-left
* and form an upper-triangular principal submatrix */
spasm * spasm_permute_pivots(const spasm *A, int *p, int *qinv, int npiv) {
  int i, j, k, n, m, *Ap, *Aj;

  n = A->n;
  m = A->m;
  Ap = A->p;
  Aj = A->j;

  /* pivotal column first, in row-order */
  k = 0;
  for (i = 0; i < npiv; i++) {
    j = Aj[Ap[p[i]]];         /* the pivot is the first entry of each row */
    qinv[j] = k++;
  }

  /* put remaining non-pivotal columns afterwards, in any order */
  for (j = 0; j < m; j++) {
    if (qinv[j] == -1) {
      qinv[j] = k++;
    }
  }

  return spasm_permute(A, p, qinv, SPASM_WITH_NUMERICAL_VALUES);
}