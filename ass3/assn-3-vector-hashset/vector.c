#include "vector.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <search.h>

void VectorNew(vector *v, int elemSize, VectorFreeFunction freeFn, int initialAllocation) {
    assert(elemSize > 0);
    assert(initialAllocation >= 0);
    v->alloc_len = initialAllocation;
    if (v->alloc_len == 0) v->alloc_len = VECTOR_INIT_ALLOC;
    v->log_len = 0;
    v->elem_size = elemSize;
    v->free_func = freeFn;
    v->data = malloc(v->alloc_len * elemSize);
    assert(v->data != NULL);
}

void VectorDispose(vector *v) {
    if (v->free_func != NULL) {
        for (int i = 0; i < v->log_len; i++) {
            v->free_func((char*)(v->data) + i * v->elem_size);
        }
    }
    free(v->data);
}

int VectorLength(const vector *v) {
    return v->log_len;
}

void *VectorNth(const vector *v, int position) {
    assert(position >= 0 && position < v->log_len);    
    return ((char*)v->data + position * v->elem_size);
}

void VectorReplace(vector *v, const void *elemAddr, int position) {
    assert(position >= 0 && position < v->log_len); 
    if (v->free_func != NULL) v->free_func((char*)(v->data) + position * v->elem_size); 
    memcpy((char*)(v->data) + position * v->elem_size, elemAddr, v->elem_size);
}

void VectorInsert(vector *v, const void *elemAddr, int position) {
    assert(position >= 0 && position <= v->log_len);    
    if (v->log_len == v->alloc_len) {           // grow        
        v->alloc_len *= 2;
        v->data = realloc(v->data, v->elem_size * v->alloc_len);
        assert(v != NULL);
    }
    if (position < v->log_len) {
        memmove((char*)(v->data) + (position + 1) * v->elem_size, (char*)(v->data) + position * v->elem_size, v->elem_size * (v->log_len - position)); 
    }
    memcpy((char*)(v->data) + position * v->elem_size, elemAddr, v->elem_size);
    v->log_len++;
}

void VectorAppend(vector *v, const void *elemAddr) {
    VectorInsert(v, elemAddr, v->log_len);
}

void VectorDelete(vector *v, int position) {
    assert(position >= 0 && position < v->log_len); 
    // free memory if needed
    if (v->free_func != NULL) v->free_func((char*)(v->data) + position * v->elem_size); 
    if (position < v->log_len - 1) {
        memmove((char*)(v->data) + position * v->elem_size, (char*)(v->data) + (position + 1) * v->elem_size, v->elem_size * (v->log_len - position - 1));
    } // if we delete the last element there's nothing to move
    v->log_len--;  
}

void VectorSort(vector *v, VectorCompareFunction compare) {
    assert(compare != NULL);
    qsort(v->data, v->log_len, v->elem_size, compare);
}

void VectorMap(vector *v, VectorMapFunction mapFn, void *auxData) {
    assert(mapFn != NULL);
    for (int i = 0; i < v->log_len; i++) {
        mapFn((char*)(v->data) + i * v->elem_size, auxData);
    }
}

static const int kNotFound = -1;
int VectorSearch(const vector *v, const void *key, VectorCompareFunction searchFn, int startIndex, bool isSorted) {
    assert(startIndex >= 0 && startIndex <= v->log_len);
    assert(key != NULL);
    assert(searchFn != NULL);
    void* found;
     if (!isSorted) {
        found = bsearch(key, (char*)(v->data) + startIndex * v->elem_size, v->log_len - startIndex, v->elem_size, searchFn);
    } else {
        size_t arr_len = v->log_len - startIndex;
        found = lfind(key, (char*)(v->data) + startIndex * v->elem_size, &arr_len, v->elem_size, searchFn);
    }

    if (found != NULL) {
        return ((char*)found - (char*)(v->data)) / v->elem_size;   
    } else {
        return kNotFound;
    }
} 