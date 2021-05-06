#include "hashset.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

void HashSetNew(hashset *h, int elemSize, int numBuckets,
		HashSetHashFunction hashfn, HashSetCompareFunction comparefn, HashSetFreeFunction freefn) {

	assert(elemSize > 0);
	assert(numBuckets > 0);
	assert(comparefn != NULL);
	assert(hashfn != NULL);

	h->elem_size = elemSize;
  	h->num_buckets = numBuckets;	
	h->hash_func = hashfn;
  	h->comp_func = comparefn;
 	h->free_func = freefn;
 	h->data = malloc(numBuckets * sizeof(vector));
	assert(h->data != NULL);

	for (int i = 0; i < numBuckets; i++) {
		vector v;
		VectorNew(&v, elemSize, freefn, 0);
		h->data[i] = v;
	}	
	h->num_elems = 0;
	
}

void HashSetDispose(hashset *h) {
	for (int i = 0; i < h->num_buckets; i++) {
		VectorDispose(&(h->data[i]));
	}
	free (h->data);
}

int HashSetCount(const hashset *h) {	
	return h->num_elems;
}

void HashSetMap(hashset *h, HashSetMapFunction mapfn, void *auxData) {
	assert(mapfn != NULL);
	for (int i = 0; i < h->num_buckets; i++) {
		VectorMap(&(h->data[i]), mapfn, auxData);
	}

}

void HashSetEnter(hashset *h, const void *elemAddr) {
	assert(elemAddr != NULL);
	int idx = h->hash_func(elemAddr, h->num_buckets);
	assert(idx >= 0 && idx < h->num_buckets);
	
	int find = VectorSearch(&((h->data)[idx]), elemAddr, h->comp_func, 0, false);
	if (find < 0) {
		VectorAppend(&((h->data)[idx]), elemAddr);
	} else {
		VectorReplace(&((h->data)[idx]), elemAddr, find);
	}
}

void *HashSetLookup(const hashset *h, const void *elemAddr) {
	assert(elemAddr != NULL);
	int idx = h->hash_func(elemAddr, h->num_buckets);
	assert(idx >= 0 && idx < h->num_buckets);

	int find = VectorSearch(&((h->data)[idx]), elemAddr, h->comp_func, 0, false);
	if (find >= 0) return VectorNth(&((h->data)[idx]), find);
	return NULL;
}
