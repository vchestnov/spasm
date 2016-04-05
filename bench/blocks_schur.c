#include <assert.h>
#include <stdio.h>
#include "spasm.h"

/*
 * Dans toute la suite, X[i:j] désigne les élements de X d'indice i <= ... < j,
 * C'est-à-dire de i inclus à j exclus. Il s'ensuit que i:j désigne l'intervalle [i; j[
 *
 * Cette notation bien pratique est héritée du langage Python.
 */



/****************** Structures ***********************/


/*
 * type de données qui décrit un bloc rectangulaire.
 */
typedef struct {
  int i0; //  Le bloc est M[i0:i1, j0:j1], c'est-à-dire
  int j0; //     les lignes   de l'intervalle [i0; i1[
  int i1; //  et les colonnes de l'intervalle [j0; j1[
  int j1;
  int r;  // le rang du bloc
} block_t;


/*********************** Décomposition par bloc *****************************/


/*
 * Renvoie le rang de M[a:c, b:d]. 
 */
spasm_lu * submatrix_LU(const spasm *M, int a, int b, int c, int d) {
  spasm *C;
  //int *p;
  spasm_lu *LU;

  // extrait la sous-matrice
  C = spasm_submatrix(M, a, c, b, d, SPASM_WITH_NUMERICAL_VALUES);
  if (spasm_nnz(C) == 0) {
    spasm_csr_free(C);
    return NULL;
  }

  // calcule la décomposition LU. 
  // p = spasm_cheap_pivots(C); 
  LU = spasm_LU(C, SPASM_IDENTITY_PERMUTATION, SPASM_KEEP_L); // on garde L
  // free(p);


  // libère la sous-matrice et la mémoire dont on n'a plus besoin.
  spasm_csr_free(C);

  return LU;
}


/*
 * Renvoie "start" + (le nombre de blocs décrits par les composantes connexes dans "Y").
 *
 * Si blocks != NULL, stocke les blocs dans "blocks" à partir de l'indice *start (la valeur est modifiée)
 *
 * La position du coin inférieur droit du dernier bloc rencontrée doit être passée dans *last_i et *last_j. Ces valeurs sont modifiées.
 *
 */
void count_blocks(spasm_cc *Y, block_t *blocks, int *start) {
  int i, j, a,b,c,d;

  for(i = 0; i < Y->CC->nr; i++) {
    if (Y->SCC[i] != NULL) {
      for(j = 0; j < Y->SCC[i]->nr; j++) {
        a = Y->SCC[i]->rr[j];
        b = Y->SCC[i]->cc[j];
        c = Y->SCC[i]->rr[j + 1];
        d = Y->SCC[i]->cc[j + 1];

        if (blocks != NULL) {
          blocks[*start].i0 = a;
          blocks[*start].j0 = b;
          blocks[*start].i1 = c;
          blocks[*start].j1 = d;
        }
        (*start)++;
      }
    }
  }
}

/*
 * Etant donné une matrice M déjà permutée sous forme triangulaire par
 * blocs, et la decsription de la décomposition, détermine la liste
 * des blocs diagonaux.
 *
 * Ceci est rendu nécessaire par le fait que la structure de donnée
 * choisie pour stocker la décomposition n'est pas du tout pratique.
 *
 * le nombre de blocs est renvoyé. Il faut passer un pointeur vers une
 * liste de blocs, qui est modifiée.
 */
int block_list(const spasm *M, const spasm_dm *DM, block_t **blocks_ptr, spasm_lu ***LU_ptr) {
  int i, k;
  block_t *blocks;
  spasm_lu **LU;
  
  // étape 1 : détermine le nombre de blocs
  k = 0;
  if (DM->H != NULL) {
    count_blocks(DM->H, NULL, &k);
  }
  if (DM->S != NULL) {
    count_blocks(DM->S, NULL, &k);
  }
  if (DM->V != NULL) {
    count_blocks(DM->V, NULL, &k);
  }

  // étape 2 : allouer la liste des blocs
  blocks = spasm_malloc(sizeof(block_t) * k);
  *blocks_ptr = blocks;
  LU = spasm_malloc(k * sizeof(spasm_lu *));
  *LU_ptr = LU;
  
  // étape 3 : remplir la liste des blocs
  k = 0;
  if (DM->H != NULL) {
    count_blocks(DM->H, blocks, &k);
  }
  if (DM->S != NULL) {
    count_blocks(DM->S, blocks, &k);
  }
  if (DM->V != NULL) {
    count_blocks(DM->V, blocks, &k);
  }

  // étape 4 : "malaxer" la liste des blocs (il faut que leurs coins se touchent).
  blocks[0].i0 = 0;
  blocks[0].j0 = 0;
  for (i = 1; i < k; i++) {
    blocks[i - 1].i1 = blocks[i].i0;
    blocks[i - 1].j1 = blocks[i].j0;
  }
  blocks[k-1].i1 = M->n;
  blocks[k-1].j1 = M->m;

  // étape 5 : calculer les rangs
  for (i = 0; i < k; i++) {
    LU[i] = submatrix_LU(M, blocks[i].i0, blocks[i].j0, blocks[i].i1, blocks[i].j1);
    blocks[i].r = LU[i]->U->n;
  }

  return k;
}



/**************** Fonction main *********************/

int main() {
  // charge la matrice depuis l'entrée standard
  spasm_triplet * T = spasm_load_sms(stdin, 42013);
  spasm * A = spasm_compress(T);
  spasm_triplet_free(T);


  // calcule la décomposition de A
  spasm_dm * x = spasm_dulmage_mendelsohn(A);

  // B = A permutée sous forme triangulaire par blocs
  int * qinv = spasm_pinv(x->DM->q, A->m);
  spasm * B = spasm_permute(A, x->DM->p, qinv, SPASM_WITH_NUMERICAL_VALUES);
  free(qinv);

  printf("input matrix : %d x %d with %d NNZ\n", A->n, A->m, spasm_nnz(A));

  spasm_csr_free(A);

  // calcule la liste des blocs diagonaux et leur LU (sans permutation des lignes)
  block_t *blocks;
  spasm_lu **LU;
  int n_blocks = block_list(B, x, &blocks, &LU);
  
  free(x); // le vieil objet DM : exit

  printf("blocs diagonaux : %d\n", n_blocks);

  /* Réassemble les petites matrices L, pour obtenir une grande matrice L */
  
  int *P = spasm_calloc(B->n, sizeof(int));
  int *Qinv = spasm_calloc(B->m, sizeof(int));
  int pivots = 0;
  int non_pivots = B->n - 1;
  spasm *L = spasm_csr_alloc(B->n, B->n, B->nzmax, B->prime, SPASM_WITH_NUMERICAL_VALUES);
  int *Lj = L->j;
  int *Lp = L->p;
  int lnz = 0;
  spasm_GFp *Lx = L->x;
  Lp[0] = 0;
  int plop = 0;

  // a priori, aucun pivot trouvé
  for(int j = 0; j < A->m; j++) {
    Qinv[j] = -1;
  }


  for(int k = 0; k < n_blocks; k++) {

    int r = blocks[k].r;
    int block_n = blocks[k].i1 - blocks[k].i0;
    int block_m = blocks[k].j1 - blocks[k].j0;
    int a = blocks[k].i0;
    int b = blocks[k].j0;

    plop += block_n;

    printf("%d : (%d, %d) -- (%d, %d) {%d x %d} [rang %d]...\n", k, a, b, blocks[k].i1, blocks[k].j1, block_n, block_m, r);


    /* locate the pivots in the diagonal block */
    assert(r > 0);

    /* le seul truc sûr, c'est que ceci marque les colonnes qui contiennent des pivots. Mais sur quelles lignes seront-ils ? */
    for(int j = 0; j < block_m; j++) {
      if (LU[k]->qinv[j] >= 0) {
        Qinv[b + j] = a + LU[k]->qinv[j];
      }      
    }

    for(int i = 0; i < r; i++) {
      /* les lignes d'indices p[i] de C contiennent un pivot */
      P[pivots] = a + LU[k]->p[i];
      //printf("row %d is pivot\n", a + LU[k]->p[i]);
      pivots++;
    }
    for(int i = r; i < block_n; i++) {
      /* les lignes d'indices p[i] de C ne contiennent pas de pivot */
      P[non_pivots] = a + LU[k]->p[i];
      //printf("row %d is non-pivot\n", a + LU[k]->p[i]);
      non_pivots--;
    }

    /* copier les petites lignes de L trouvées dans le grand L */
    int *small_p = LU[k]->L->p;
    int *small_j = LU[k]->L->j;
    spasm_GFp *small_x = LU[k]->L->x;

    // attention, il se peut que L ne soit pas assez gros
    if (lnz + spasm_nnz(LU[k]->L) > L->nzmax) {
      spasm_csr_realloc(L, 2 * L->nzmax + spasm_nnz(LU[k]->L));
      Lj = L->j;
      Lx = L->x;
    }
    
    for(int i = 0; i < block_n; i++) {
      int big_t = Lp[a + i];
      for(int small_t = small_p[i]; small_t < small_p[i+1]; small_t++) {
        Lj[big_t] = small_j[small_t];
        Lx[big_t] = small_x[small_t];
        big_t++;
      }
      Lp[a + i + 1] = big_t;
    }
  }

  assert(pivots == non_pivots+1);

  printf("le complément de Schur est de taille %d x %d et de rang <= %d\n", B->n - pivots, B->m - pivots, spasm_min(B->n, B->m) - pivots);

  x = e_k * L.I
  x*L = e_k

  /* détermine U.  U[k] = e_k * (L.I) Ak
  x = malloc(Mn * sizeof(spasm_GFp));
  xi = malloc(3*Mn * sizeof(int));
  spasm_vector_zero(xi, 3*Mn);
  spasm_vector_zero(x, Mn);

  I = spasm_identity(Mn, prime); // <--- Identity matrix

  top = spasm_sparse_backward_solve(L, I, k, xi, x, pinv,0);




  return 0;
}