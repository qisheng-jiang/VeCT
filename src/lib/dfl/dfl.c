#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <stdbool.h>
#include <inttypes.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <immintrin.h>

#include <dfl.h>
// I know it is ugly... will reference cfl.h from the dfl.h directory
#include <../cfl/cfl.h>
#include "../../include/dfl_setup.h"

extern volatile bool taken;

// DFL debug level:
// 1: print objects and debug checks
// 2: print all accesses

// We have three types of access helpers:
// 1) plain     : just accesses an element of the right size every DFL_STRIDE bytes
// 2) avx gather: use gathering instructions to access multiple elements with a single load
// 3) avx loads : use a vector load to access a whole cache line 
//                (best if DFL_STRIDE should be like every 4 bytes, we touch all)
// 2 and 3 are both AVX2 and AVX512
// TODO: fixme - 2 and 3 may access the objects OOB of a lot, however masked access
//               will prevent it segfaulting
//      -> known issue: multiple byte loads on page boundary may crash if mask=1

#define DFL_DEBUG 0
#define DPRINT(f_, ...) fprintf(stderr, (f_), __VA_ARGS__)

#define PRINT_M512I_EPI32(s, vec) \
    do { \
        uint32_t arr[16]; \
        _mm512_storeu_si512(arr, vec); \
        fprintf(stderr, "%s m512i in epi32: ", s); \
        for (int i = 0; i < 16; i++) { \
            fprintf(stderr, "%d ", arr[i]); \
        } \
        fprintf(stderr, "\n"); \
    } while (0)

#define PRINT_M512I_EPI64(s, vec) \
    do { \
        uint64_t arr[8]; \
        _mm512_storeu_si512(arr, vec); \
        fprintf(stderr, "%s m512i in epi64: ", s); \
        for (int i = 0; i < 8; i++) { \
            fprintf(stderr, "%lu ", arr[i]); \
        } \
        fprintf(stderr, "\n"); \
    } while (0)

#if DFL_DEBUG == 2
#define DEBUG(f_, ...) fprintf(stderr, (f_), __VA_ARGS__)
#define DEBUG_ASSERT(a) assert(a)
#define DEBUG_STMT(s) s
#elif DFL_DEBUG == 1
#define DEBUG(f_, ...) 
#define DEBUG_ASSERT(a)
#define DEBUG_STMT(s) s
#else
#define DEBUG(f_, ...) 
#define DEBUG_ASSERT(a)
#define DEBUG_STMT(s)
#endif

#ifdef __SIZEOF_INT128__
    typedef __uint128_t uint128_t;
    typedef __int128_t int128_t;
#else
    typedef unsigned long uint128_t __attribute__ ((mode(TI)));
    typedef long int128_t __attribute__ ((mode(TI)));
#endif

/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member) ({				\
	void *__mptr = (void *)(ptr);					\
	_Static_assert(!__builtin_types_compatible_p(typeof(*(ptr)), typeof(((type *)0)->member)) &&	\
			 !__builtin_types_compatible_p(typeof(*(ptr)), void),			\
			 "pointer type mismatch in container_of()");	\
	((type *)(__mptr - offsetof(type, member))); })

typedef struct
__attribute__((aligned(CACHE_LINE_ALIGNMENT))) 
dfl_obj_list {
    struct dfl_obj_list* next;
    struct dfl_obj_list* prev;
    struct dfl_obj_list** head_ptr;
    uint64_t size;
    uint64_t padding0;
    uint64_t padding1;
    uint64_t padding2;
    uint64_t magic;
    unsigned char data[] 
    __attribute__((aligned(CACHE_LINE_ALIGNMENT))); 
} dfl_obj_list_t;

typedef dfl_obj_list_t* dfl_obj_list_head;


// `head -c 8 /dev/urandom | xxd -p`
#define DFL_MAGIC  (0x3aa6bd228ae62271uL)

DFL_FUNC DFL_CONSTRUCTOR __attribute__((used)) __attribute__((noinline)) void dfl_init()
{
    asm volatile ("");
    return;
}

#if DFL_DEBUG >= 1
DFL_FUNC __attribute__((used)) __attribute__((noinline)) void dfl_debug_is_enabled(void) {
}

bool __dfl_matched = false;
// check that either we are not in a taken branch or that the DFL helpers matched
// at least a pointer read/write
DFL_FUNC __attribute__((used)) __attribute__((noinline)) void dfl_debug_check_matched(void) {
    if (taken) assert(__dfl_matched);
    else       assert(!__dfl_matched);
    __dfl_matched = false;
}

#endif

DFL_FUNC_INLINE void dfl_obj_print(dfl_obj_list_t* new_obj, unsigned long size) {
#if DFL_DEBUG >= 1
    // objs are DFL_STRIDE aligned
    unsigned char* ptr = (unsigned char*)(((unsigned long)new_obj->data) & ~(DFL_STRIDE - 1));
    // take into account the size increase due to alignment
    unsigned long orig_size = size;
    size += (new_obj->data - ptr);
    size = ((size + DFL_STRIDE - 1) / DFL_STRIDE) * DFL_STRIDE; // ((n + 9) / 10) * 10
    fprintf(stderr, "DFL OBJ - aligned ptr: %p - obj: %p - size: %lu - orig_size: %lu\n", ptr, new_obj->data, size, orig_size);
#endif
}

DFL_FUNC_INLINE void dfl_glob_obj_print(unsigned char* obj, unsigned long size) {
#if DFL_DEBUG >= 1
    // objs are DFL_STRIDE aligned
    unsigned char* ptr = (unsigned char*)(((unsigned long)obj) & ~(DFL_STRIDE - 1));
    // take into account the size increase due to alignment
    unsigned long orig_size = size;
    size += (obj - ptr);
    size = ((size + DFL_STRIDE - 1) / DFL_STRIDE) * DFL_STRIDE; // ((n + 9) / 10) * 10
    fprintf(stderr, "DFL OBJ - aligned ptr: %p - obj: %p - size: %lu - orig_size: %lu\n", ptr, obj, size, orig_size);
#endif
}

DFL_FUNC __attribute__((used)) __attribute__((noinline)) void dfl_id_print(unsigned long bgid, unsigned long ibid) {
    DEBUG("(%lu, %lu) ", bgid, ibid);
}

DFL_FUNC_INLINE void dfl_obj_list_add(dfl_obj_list_head* head, dfl_obj_list_t* new_obj, unsigned long size) {
    DEBUG("OBJ ADD - head: %p - obj: %p - size: %lu -", head, new_obj, size);

    // objs are DFL_STRIDE aligned
    unsigned char* ptr = (unsigned char*)(((unsigned long)new_obj->data) & ~(DFL_STRIDE - 1));
    // take into account the size increase due to alignment
    size += (new_obj->data - ptr);
    new_obj->size = ((size + DFL_STRIDE - 1) / DFL_STRIDE) * DFL_STRIDE; // ((n + 9) / 10) * 10
    DEBUG(" new size: %lu\n", new_obj->size);

    // Fill obj ptrs
    new_obj->next = *head;
    new_obj->prev = NULL;
    new_obj->head_ptr = head;

    // Set the magic of the object struct
    new_obj->magic = DFL_MAGIC;

    // Set the prev of the head if node present
    if ( (*head) != NULL )
        (*head)->prev = new_obj;

    // Insert object at the head
    *head = new_obj;

    return;
}

DFL_FUNC_INLINE unsigned char* dfl_obj_list_unlink(unsigned char* ptr) {
    DEBUG("OBJ UNLINK - obj: %p -", ptr);
    dfl_obj_list_t* obj = container_of(ptr, dfl_obj_list_t, data);

    // If the magic does not match, this is not a DFL object
    // N.B. this may crash if we get a page aligned address
    // as magic field is before the data field and may result 
    // in an unallocated region, but for ptmalloc should never be the case
    if (obj->magic != DFL_MAGIC)
        return ptr;
    DEBUG(" next: %p - prev: %p -", obj->next, obj->prev);
    
    // Get the head of the linked list
    dfl_obj_list_head* head = obj->head_ptr;
    DEBUG(" head: %p\n", head);

    // if obj is first node of list
    if(obj->prev == NULL)
        *head = obj->next; //the next node will be front of list
    else
        obj->prev->next = obj->next; // otherwise unlink from prev

    // if next is not null change its prev
    if (obj->next != NULL)
        obj->next->prev = obj->prev;

    return (unsigned char*) obj;
}

#define U64_CACHE_LINE_NUM (CACHE_LINE_ALIGNMENT / sizeof(uint64_t))
#define U32_CACHE_LINE_NUM (CACHE_LINE_ALIGNMENT / sizeof(uint32_t))

inline unsigned char* get_real_ptr_uint64_t(unsigned char* obj, unsigned char* ptr, unsigned long* field_offset, unsigned long* field_size, unsigned int *index) {
    // DEBUG("get_real_ptr_uint64_t before obj: %p ptr: %p field_offset: %lu field_size: %lu %lu", obj, ptr, *field_offset, *field_size, (unsigned long)obj % CACHE_LINE_ALIGNMENT);
    assert((unsigned long)obj % CACHE_LINE_ALIGNMENT == 0);
    #if !DFL_READONLY
    if (ptr != NULL && ptr >= obj) {
        unsigned long ptr_offset = (((unsigned long) ptr) - ((unsigned long) obj));
        ptr_offset += (ptr_offset / CACHE_LINE_ALIGNMENT + 1) * sizeof(uint64_t);
        ptr = obj + ptr_offset;
    }
    unsigned long index_addr_offset = 0;
    if (index != NULL) {
        index_addr_offset = *field_offset + *index * sizeof(uint64_t);
        index_addr_offset += (index_addr_offset / CACHE_LINE_ALIGNMENT + 1) * sizeof(uint64_t);
    }
    unsigned long field_end = *field_offset + *field_size;
    *field_offset += (*field_offset / CACHE_LINE_ALIGNMENT + 1) * sizeof(uint64_t);
    field_end += (field_end / CACHE_LINE_ALIGNMENT + 1) * sizeof(uint64_t);
    *field_size = field_end - *field_offset;
    if (index != NULL) {
        *index = (index_addr_offset - *field_offset) / sizeof(uint64_t);
    }
    #endif // DFL_READONLY
    // DEBUG("get_real_ptr_uint64_t after obj: %p ptr: %p field_offset: %lu field_size: %lu %lu %lu", obj, ptr, *field_offset, *field_size, (unsigned long)obj % CACHE_LINE_ALIGNMENT, index != NULL ? *index : 0);
    return ptr;
}

inline unsigned char* get_real_ptr_uint32_t(unsigned char* obj, unsigned char* ptr, unsigned long* field_offset, unsigned long* field_size, unsigned int *index) {
    // DEBUG("get_real_ptr_uint32_t obj: %p ptr: %p field_offset: %lu field_size: %lu %lu", obj, ptr, *field_offset, *field_size, (unsigned long)obj % CACHE_LINE_ALIGNMENT);
    assert((unsigned long)obj % CACHE_LINE_ALIGNMENT == 0);
    #if !DFL_READONLY
    if (ptr != NULL && ptr >= obj) {
        unsigned long ptr_offset = (((unsigned long) ptr) - ((unsigned long) obj));
        ptr_offset += (ptr_offset / CACHE_LINE_ALIGNMENT + 1) * sizeof(uint32_t);
        ptr = obj + ptr_offset;
    }
    unsigned long index_addr_offset = 0;
    if (index != NULL) {
        index_addr_offset = *field_offset + *index * sizeof(uint32_t);
        index_addr_offset += (index_addr_offset / CACHE_LINE_ALIGNMENT + 1) * sizeof(uint32_t);
    }
    unsigned long field_end = *field_offset + *field_size;
    *field_offset += (*field_offset / CACHE_LINE_ALIGNMENT + 1) * sizeof(uint32_t);
    field_end += (field_end / CACHE_LINE_ALIGNMENT + 1) * sizeof(uint32_t);
    *field_size = field_end - *field_offset;
    if (index != NULL) {
        *index = (index_addr_offset - *field_offset) / sizeof(uint32_t);
    }
    #endif // DFL_READONLY
    // DEBUG("get_real_ptr_uint32_t after obj: %p ptr: %p field_offset: %lu field_size: %lu %lu %lu", obj, ptr, *field_offset, *field_size, (unsigned long)obj % CACHE_LINE_ALIGNMENT, index != NULL ? *index : 0);
    return ptr;
}

#if DFL_READONLY
#define REAL_PTR_FUNC(type) inline unsigned char* get_real_ptr_ ## type(unsigned char* obj, unsigned char* ptr, unsigned long* field_offset, unsigned long* field_size, unsigned int* index) { return ptr; }
#else
#define REAL_PTR_FUNC(type) inline unsigned char* get_real_ptr_ ## type(unsigned char* obj, unsigned char* ptr, unsigned long* field_offset, unsigned long* field_size, unsigned int* index) { \
    unsigned long index_addr_offset = 0; \
    if (index != NULL) { \
        index_addr_offset = *field_offset + *index * sizeof(type); \
        index_addr_offset += (index_addr_offset / CACHE_LINE_ALIGNMENT + 1) * sizeof(uint64_t); \
    } \
    ptr = get_real_ptr_uint64_t(obj, ptr, field_offset, field_size, index); \
    if (index != NULL) { \
        *index = (index_addr_offset - *field_offset) / sizeof(type); \
    } \
    return ptr; \
}
#endif // DFL_READONLY

REAL_PTR_FUNC(uint128_t)
REAL_PTR_FUNC(uint16_t)
REAL_PTR_FUNC(uint8_t)

// uint32_t -> 2, uint64_t -> 3
#define TYPE_SHIFT(TYPE) \
    ((sizeof(TYPE) == 1) ? 0 : \
     (sizeof(TYPE) == 2) ? 1 : \
     (sizeof(TYPE) == 4) ? 2 : \
     (sizeof(TYPE) == 8) ? 3 : -1)

#define GET_REAL_PTR(OBJ, PTR, FIELD_OFF, FIELD_SIZE, TYPE) \
    do { \
        const unsigned _type_shift = TYPE_SHIFT(TYPE); \
        assert(_type_shift >= 0); \
        /* unsigned _align_shift = (CACHE_LINE_SHIFT);*/ \
        assert(((unsigned long)(OBJ) % (CACHE_LINE_ALIGNMENT)) == 0); \
        if ((PTR) != NULL && (PTR) >= (OBJ)) { \
            unsigned long _ptr_offset = (unsigned long)(PTR) - (unsigned long)(OBJ); \
            _ptr_offset += (((_ptr_offset) / (CACHE_LINE_ALIGNMENT - sizeof(TYPE))) + 1) << _type_shift; \
            (PTR) = (OBJ) + _ptr_offset; \
        } \
        unsigned long _field_end = (FIELD_OFF) + (FIELD_SIZE); \
        (FIELD_OFF) += (((FIELD_OFF) / (CACHE_LINE_ALIGNMENT - sizeof(TYPE))) + 1) << _type_shift; \
        _field_end += ((_field_end / (CACHE_LINE_ALIGNMENT - sizeof(TYPE))) + 1) << _type_shift; \
        (FIELD_SIZE) = _field_end - (FIELD_OFF); \
    } while (0)


#define DFL_OBJ_LOAD(type) DFL_FUNC_INLINE type type ## _dfl_obj_load(dfl_obj_list_head head, unsigned char* ptr, unsigned long field_off, unsigned long field_size) {  \
    DEBUG("OBJ LOAD: %p - head: %p - sizeof: %lu - off: %u - size: %u ", ptr, head, sizeof(type), field_off, field_size); \
    register type res = 0; \
    DEBUG_STMT(int found = 0); \
    unsigned long copy_field_off = field_off; \
    unsigned long copy_field_size = field_size; \
    while (head) \
    { \
        unsigned char* obj = head->data; \
        DEBUG("OBJ LOAD 0: %p - head: %p - obj: %p - sizeof: %lu - off: %u - size: %u", ptr, head, obj, sizeof(type), field_off, field_size); \
        ptr = get_real_ptr_ ## type(obj, ptr, &field_off, &field_size, NULL); \
        DEBUG("OBJ LOAD 1: %p - head: %p - obj: %p - sizeof: %lu - off: %u - size: %u", ptr, head, obj, sizeof(type), field_off, field_size); \
        unsigned long cache_off = ((unsigned long) ptr) & (DFL_STRIDE-1uL); \
        unsigned char* aligned_ptr = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_STRIDE - 1)); \
        field_size += (((unsigned long)obj + field_off) - (unsigned long)aligned_ptr); \
        field_size = ((field_size + DFL_STRIDE - 1) / DFL_STRIDE) * DFL_STRIDE; \
        unsigned char* _end =  aligned_ptr + field_size; \
        DEBUG("  obj: %p - field_off: %lu - size: %lu ", obj, field_off, field_size); \
        for(volatile unsigned char* _ptr = aligned_ptr + cache_off; _ptr < _end; _ptr = _ptr + DFL_STRIDE) { \
            DEBUG("%s", "."); \
            type _res = *(volatile type*)_ptr; \
            DEBUG("OBJ LOAD RES: ptr %p res %lu _ptr %p _prev_val %lu", ptr, (unsigned long) res, _ptr, (unsigned long) _res); \
            res = (_ptr == ptr)? _res : res; \
            DEBUG_STMT(if(!found) found = (_ptr == ptr && taken)); \
        } \
        head = head->next; \
        field_off = copy_field_off; \
        field_size = copy_field_size; \
        DEBUG("%s", "\n"); \
    } \
    DEBUG("  returned: %lx - %s\n", (unsigned long) res, found != 0? "MATCH": "NO MATCH"); \
    DEBUG_STMT(if (found) assert(!__dfl_matched)); /*assert no multiple matches*/ \
    DEBUG_STMT(__dfl_matched |= found); \
    return res; \
}

#define DFL_SINGLE_OBJ_LOAD(type) DFL_FUNC_INLINE type type ## _dfl_single_obj_load(dfl_obj_list_head head, unsigned char* ptr, unsigned long field_off, unsigned long field_size, unsigned int index) { \
    DEBUG("SINGLE LOAD: %p - head: %p - sizeof: %lu - off: %u - size: %u ", ptr, head, sizeof(type), field_off, field_size); \
    register type res = 0; \
    DEBUG_STMT(int found = 0); \
    unsigned long copy_field_off = field_off; \
    unsigned long copy_field_size = field_size; \
    while (head) \
    { \
        unsigned char* obj = head->data; \
        DEBUG("SINGLE LOAD 0: %p - head: %p - obj: %p - sizeof: %lu - off: %u - size: %u - index: %lu", ptr, head, obj, sizeof(type), field_off, field_size, index); \
        ptr = get_real_ptr_ ## type(obj, ptr, &field_off, &field_size, &index); \
        DEBUG("SINGLE LOAD 1: %p - head: %p - obj: %p - sizeof: %lu - off: %u - size: %u - index: %lu", ptr, head, obj, sizeof(type), field_off, field_size, index); \
        unsigned char* start = obj + field_off; \
        unsigned long _ptr = (unsigned long)&((volatile type*)start)[(index % (field_size/sizeof(type)))]; \
        type _res = *(volatile type*)_ptr; \
        DEBUG("SINLE LOAD RES: ptr %p res %lu _ptr %p _prev_val %lu", ptr, (unsigned long) res, _ptr, (unsigned long) _res); \
        res = (taken && _ptr == (unsigned long)ptr)? _res : res; \
        DEBUG_STMT(if(!found) found = (_ptr == (unsigned long)ptr  && taken)); \
        head = head->next; \
        field_off = copy_field_off; \
        field_size = copy_field_size; \
    } \
    DEBUG("%s", "\n"); \
    DEBUG("  returned: %lx - %s\n", (unsigned long) res, found != 0? "MATCH": "NO MATCH"); \
    DEBUG_STMT(if (found) assert(!__dfl_matched)); /*assert no multiple matches*/ \
    DEBUG_STMT(__dfl_matched |= found); \
    return res; \
}

#define DFL_SINGLE_GLOB_LOAD(type) DFL_FUNC_INLINE type type ## _dfl_single_glob_load(unsigned char* obj, unsigned char* ptr, unsigned long field_off, unsigned long field_size, unsigned int index) { \
    DEBUG("SINGLE LOAD: %p - obj: %p - sizeof: %lu - off: %u - size: %u - index: %u ", ptr, obj, sizeof(type), field_off, field_size, index); \
    ptr = get_real_ptr_ ## type(obj, ptr, &field_off, &field_size, &index); \
    DEBUG("SINGLE LOAD: %p - obj: %p - sizeof: %lu - off: %u - size: %u - index: %u ", ptr, obj, sizeof(type), field_off, field_size, index); \
    unsigned char* start = obj + field_off; \
    unsigned long _ptr = (unsigned long)&((volatile type*)start)[(index % (field_size/sizeof(type)))]; \
    const type _res = *(volatile type*)_ptr; \
    const type res = (taken && _ptr == (unsigned long)ptr)? _res : 0; \
    DEBUG("%s", "\n"); \
    DEBUG("  returned: %lx - %s\n", (unsigned long) res, (_ptr == (unsigned long)ptr && taken)? "MATCH": "NO MATCH"); \
    DEBUG_STMT(if (_ptr == (unsigned long)ptr && taken) assert(!__dfl_matched)); /*assert no multiple matches*/ \
    DEBUG_STMT(__dfl_matched |= (_ptr == (unsigned long)ptr && taken)); \
    return res; \
}

#define DFL_GLOB_LOAD(type) DFL_FUNC_INLINE type type ## _dfl_glob_load(unsigned char* obj, unsigned char* ptr, unsigned long field_off, unsigned long field_size) {  \
    DEBUG("GLOB LOAD 0: %p - size: %lu - ptr: %p - sizeof: %lu - offset: %lu", obj, field_size, ptr, sizeof(type), field_off); \
    register type res = 0; \
    ptr = get_real_ptr_ ## type(obj, ptr, &field_off, &field_size, NULL); \
    DEBUG("GLOB LOAD 1: %p - size: %lu - ptr: %p - sizeof: %lu - offset: %lu", obj, field_size, ptr, sizeof(type), field_off); \
    unsigned long cache_off = ((unsigned long) ptr) & (DFL_STRIDE-1uL); \
    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_STRIDE - 1)); \
    field_size += (((unsigned long)obj + field_off) - (unsigned long)aligned_obj); \
    field_size = ((field_size + DFL_STRIDE - 1) / DFL_STRIDE) * DFL_STRIDE; \
    unsigned char* _end =  aligned_obj + field_size; \
    DEBUG("GLOB LOAD 2: %p - size: %lu - ptr: %p - sizeof: %lu - offset: %lu", obj, field_size, ptr, sizeof(type), field_off); \
    DEBUG_STMT(int found = 0); \
    for(volatile unsigned char* _ptr = aligned_obj + cache_off; _ptr < _end; _ptr = _ptr + DFL_STRIDE) { \
        DEBUG("%s", "."); \
        type _res = *(volatile type*)_ptr; \
        res = (_ptr == ptr)? _res : res; \
        DEBUG_STMT(if(!found) found = (_ptr == ptr && taken)); \
    } \
    DEBUG("%s", "\n"); \
    DEBUG("  returned: %lx - %s\n", (unsigned long) res, found != 0? "MATCH": "NO MATCH"); \
    DEBUG_STMT(if (found) assert(!__dfl_matched)); /*assert no multiple matches*/ \
    DEBUG_STMT(__dfl_matched |= found); \
    return res; \
}

#define DFL_OBJ_STORE(type) DFL_FUNC_INLINE void type ## _dfl_obj_store(dfl_obj_list_head head, unsigned char* ptr, unsigned long field_off, unsigned long field_size, type value) {  \
    DEBUG("OBJ STORE: %p - head: %p - sizeof: %lu - off: %u - size: %u - value: %lu ", ptr, head, sizeof(type), field_off, field_size, (unsigned long) value); \
    DEBUG_STMT(int found = 0); \
    unsigned long copy_field_off = field_off; \
    unsigned long copy_field_size = field_size; \
    while (head) \
    { \
        unsigned char* obj = head->data; \
        DEBUG("OBJ STORE 0: %p - head: %p - obj: %p - sizeof: %lu - off: %u - size: %u - value: %lu", ptr, head, obj, sizeof(type), field_off, field_size, (unsigned long) value); \
        ptr = get_real_ptr_ ## type(obj, ptr, &field_off, &field_size, NULL); \
        DEBUG("OBJ STORE 1: %p - head: %p - obj: %p - sizeof: %lu - off: %u - size: %u - value: %lu", ptr, head, obj, sizeof(type), field_off, field_size, (unsigned long) value); \
        unsigned long cache_off = ((unsigned long) ptr) & (DFL_STRIDE-1uL); \
        unsigned char* aligned_ptr = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_STRIDE - 1)); \
        field_size += (((unsigned long)obj + field_off) - (unsigned long)aligned_ptr); \
        field_size = ((field_size + DFL_STRIDE - 1) / DFL_STRIDE) * DFL_STRIDE; \
        unsigned char* _end =  aligned_ptr + field_size; \
        DEBUG("  obj: %p - field_off: %lu - size: %lu ", obj, field_off, field_size); \
        for(volatile unsigned char* _ptr = aligned_ptr + cache_off; _ptr < _end; _ptr = _ptr + DFL_STRIDE) { \
            DEBUG("%s", "."); \
            type _prev_val = *(volatile type*)_ptr; \
            DEBUG("OBJ STORE RES: ptr %p res %lu _ptr %p _prev_val %lu", ptr, (unsigned long) value, _ptr, (unsigned long) _prev_val); \
            *(volatile type*)_ptr = (_ptr == ptr)? value : _prev_val; \
            DEBUG_STMT(if(!found) found = (_ptr == ptr && taken)); \
        } \
        DEBUG("%s", "\n"); \
        DEBUG("%s\n", found != 0? "MATCH": "NO MATCH"); \
        head = head->next; \
        field_off = copy_field_off; \
        field_size = copy_field_size; \
    } \
    DEBUG_STMT(if (found) assert(!__dfl_matched)); /*assert no multiple matches*/ \
    DEBUG_STMT(__dfl_matched |= found); \
}

#define DFL_GLOB_STORE(type) DFL_FUNC_INLINE void type ## _dfl_glob_store(unsigned char* obj, unsigned char* ptr, unsigned long field_off, unsigned long field_size, type value) {  \
    DEBUG("GLOB STORE 0: %p - size: %lu - ptr: %p - val: %lu - sizeof: %lu - offset: %lu", obj, field_size, ptr, (unsigned long) value, sizeof(type), field_off); \
    ptr = get_real_ptr_ ## type(obj, ptr, &field_off, &field_size, NULL); \
    DEBUG("GLOB STORE 1: %p - size: %lu - ptr: %p - val: %lu - sizeof: %lu - offset: %lu", obj, field_size, ptr, (unsigned long) value, sizeof(type), field_off); \
    unsigned long cache_off = ((unsigned long) ptr) & (DFL_STRIDE-1uL); \
    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_STRIDE - 1)); \
    field_size += (((unsigned long)obj + field_off) - (unsigned long)aligned_obj); \
    field_size = ((field_size + DFL_STRIDE - 1) / DFL_STRIDE) * DFL_STRIDE; \
    unsigned char* _end =  aligned_obj + field_size; \
    DEBUG("GLOB STORE 2: %p - size: %lu - ptr: %p - val: %lu - sizeof: %lu - offset: %lu", obj, field_size, ptr, (unsigned long) value, sizeof(type), field_off); \
    DEBUG_STMT(int found = 0); \
    for(volatile unsigned char* _ptr = aligned_obj + cache_off; _ptr < _end; _ptr = _ptr + DFL_STRIDE) { \
        DEBUG("%s", "."); \
        type _prev_val = *(volatile type*)_ptr; \
        *(volatile type*)_ptr = (_ptr == ptr)? value : _prev_val; \
        DEBUG_STMT(if(!found) found = (_ptr == ptr && taken)); \
    } \
    DEBUG("%s", "\n"); \
    DEBUG("%s\n", found != 0? "MATCH": "NO MATCH"); \
    DEBUG_STMT(if (found) assert(!__dfl_matched)); /*assert no multiple matches*/ \
    DEBUG_STMT(__dfl_matched |= found); \
}

// The following two are the only handlers in DFL that actually check the `taken`
// variable value. This violates the common DFL_API which only takes a target pointer
// and the objects to stride, but allows to optimize accesses based on loop induction
// variables to touch memory and not writing it. Moreover it does not slow down accesses
// on single variables since it just moves the `taken` check from the cfl_wrap_ptr function
// (which will not be called) to this function.
#define DFL_SINGLE_OBJ_STORE(type) DFL_FUNC_INLINE void type ## _dfl_single_obj_store(dfl_obj_list_head head, unsigned char* ptr, unsigned long field_off, unsigned long field_size, type value, unsigned int index) { \
    DEBUG("SINGLE STORE: %p - head: %p - sizeof: %lu - off: %u - size: %u - value: %lu ", ptr, head, sizeof(type), field_off, field_size, (unsigned long) value); \
    DEBUG_STMT(int found = 0); \
    unsigned long copy_field_off = field_off; \
    unsigned long copy_field_size = field_size; \
    while (head) \
    { \
        unsigned char* obj = head->data; \
        DEBUG("SINGLE STORE 0: %p - head: %p - obj: %p - sizeof: %lu - off: %u - size: %u - index: %lu - value: %lu", ptr, head, obj, sizeof(type), field_off, field_size, index, (unsigned long) value); \
        ptr = get_real_ptr_ ## type(obj, ptr, &field_off, &field_size, &index); \
        DEBUG("SINGLE STORE 1: %p - head: %p - obj: %p - sizeof: %lu - off: %u - size: %u - index: %lu - value: %lu", ptr, head, obj, sizeof(type), field_off, field_size, index, (unsigned long) value); \
        unsigned char* start = obj + field_off; \
        unsigned long _ptr = (unsigned long)&((volatile type*)start)[(index % (field_size/sizeof(type)))]; \
        const type _prev_val = *(volatile type*)_ptr; \
        DEBUG("SINLE STORE RES: ptr %p res %lu _ptr %p _prev_val %lu", ptr, (unsigned long) value, _ptr, (unsigned long) _prev_val); \
        *(volatile type*)_ptr = (taken && _ptr == (unsigned long)ptr)? value : _prev_val; \
        DEBUG_STMT(if(!found) found = (_ptr == (unsigned long)ptr && taken)); \
        head = head->next; \
        field_off = copy_field_off; \
        field_size = copy_field_size; \
    } \
    DEBUG("%s", "\n"); \
    DEBUG("%s\n", found? "MATCH": "NO MATCH"); \
    DEBUG_STMT(if (found) assert(!__dfl_matched)); /*assert no multiple matches*/ \
    DEBUG_STMT(__dfl_matched |= found); \
}

#define DFL_SINGLE_GLOB_STORE(type) DFL_FUNC_INLINE void type ## _dfl_single_glob_store(unsigned char* obj, unsigned char* ptr, unsigned long field_off, unsigned long field_size, type value, unsigned int index) { \
    DEBUG("SINGLE STORE: %p - obj: %p - sizeof: %lu - off: %u - size: %u - value: %lu ", ptr, obj, sizeof(type), field_off, field_size, (unsigned long) value); \
    ptr = get_real_ptr_ ## type(obj, ptr, &field_off, &field_size, &index); \
    DEBUG("SINGLE STORE: %p - obj: %p - sizeof: %lu - off: %u - size: %u - value: %lu ", ptr, obj, sizeof(type), field_off, field_size, (unsigned long) value); \
    unsigned char* start = obj + field_off; \
    unsigned long _ptr = (unsigned long)&((volatile type*)start)[(index % (field_size/sizeof(type)))]; \
    const type _prev_val = *(volatile type*)_ptr; \
    *(volatile type*)_ptr = (taken && _ptr == (unsigned long)ptr)? value : _prev_val; \
    DEBUG("%s", "\n"); \
    DEBUG("%s\n", (_ptr == (unsigned long)ptr && taken)? "MATCH": "NO MATCH"); \
    DEBUG_STMT(if (_ptr == (unsigned long)ptr && taken) assert(!__dfl_matched)); /*assert no multiple matches*/ \
    DEBUG_STMT(__dfl_matched |= (_ptr == (unsigned long)ptr && taken)); \
}

#if defined(__AVX2__)
// ------------------------- AVX2 FUNCTIONS -------------------------

// horizontally sum all 64bit values to obtain a 64 bit value
DFL_FUNC_INLINE uint64_t mm256_hadd_to_64(__m256i v) {
    __m128i vlow    = _mm256_castsi256_si128(v);
    __m128i vhigh   = _mm256_extracti128_si256(v, 1);
            vlow    = _mm_add_epi64(vlow, vhigh);
    __m128i vhigh64 = _mm_unpackhi_epi64(vlow, vlow);
    return _mm_cvtsi128_si64(_mm_add_epi64(vlow, vhigh64));
}

DFL_FUNC_INLINE uint32_t hsum_epi32_avx(__m128i x)
{
    __m128i hi64  = _mm_unpackhi_epi64(x, x); // 3-operand non-destructive AVX lets us save a byte without needing a movdqa
    __m128i sum64 = _mm_add_epi32(hi64, x);
    __m128i hi32  = _mm_shuffle_epi32(sum64, _MM_SHUFFLE(2, 3, 0, 1)); // Swap the low two elements
    __m128i sum32 = _mm_add_epi32(sum64, hi32);
    return _mm_cvtsi128_si32(sum32);       // movd
}

// horizontally sum all 32bit values to obtain a 32 bit value
DFL_FUNC_INLINE uint32_t mm256_hadd_to_32(__m256i v)
{
    __m128i sum128 = _mm_add_epi32( 
                 _mm256_castsi256_si128(v),
                 _mm256_extracti128_si256(v, 1));
    return hsum_epi32_avx(sum128);
}

DFL_FUNC uint64_t uint64_t_avx2_gather_dfl_obj_load(dfl_obj_list_head head, unsigned char* ptr, unsigned long field_off, unsigned long field_size) { 
    // set the index vector to 0,1,2,3 "little" endian
    // so that 0 is in index[0:31], ecc
    __m256i index = _mm256_setr_epi64x(0*DFL_STRIDE, 1*DFL_STRIDE, 2*DFL_STRIDE, 3*DFL_STRIDE);
    __m256i res   = _mm256_setzero_si256();

    __m256i target    = _mm256_set1_epi64x((unsigned long) ptr);
    __m256i increment = _mm256_set1_epi64x(4*DFL_STRIDE);

    unsigned long cache_off = ((unsigned long) ptr) & (DFL_STRIDE-1uL);

    while(head) {
        unsigned char* aligned_obj = (unsigned char*)(((unsigned long)head->data + field_off) & ~(DFL_STRIDE - 1));
        field_size += (((unsigned long)head->data + field_off) - (unsigned long)aligned_obj);
        field_size = ((field_size + 4*DFL_STRIDE - 1) / (4*DFL_STRIDE)) * 4*DFL_STRIDE;
        unsigned char* _end =  aligned_obj + field_size;

        // initialize the current avx ptrs for each iteration
        __m256i current = _mm256_set1_epi64x((unsigned long) aligned_obj + cache_off);
        current  = _mm256_add_epi64(current, index);

        for(volatile unsigned char* _ptr = aligned_obj + cache_off; _ptr < _end; _ptr = _ptr + 4*DFL_STRIDE) {
            // the mask will select which value will get actually loaded
            __m256i mask = _mm256_cmpeq_epi64(target, current);
            current = _mm256_add_epi64(current, increment);
            __m256i loaded = _mm256_i64gather_epi64(_ptr, index, 1);
            res = _mm256_blendv_epi8(res, loaded, mask);
        }
        head = head->next;
    }
    return mm256_hadd_to_64(res);
}

DFL_FUNC uint64_t uint64_t_avx2_gather_dfl_glob_load(unsigned char* obj, unsigned char* ptr, unsigned long field_off, unsigned long field_size) { 
    // set the index vector to 0,1,2,3 "little" endian
    // so that 0 is in index[0:31], ecc
    __m256i index = _mm256_setr_epi64x(0*DFL_STRIDE, 1*DFL_STRIDE, 2*DFL_STRIDE, 3*DFL_STRIDE);
    __m256i res   = _mm256_setzero_si256();

    __m256i target    = _mm256_set1_epi64x((unsigned long) ptr);
    __m256i increment = _mm256_set1_epi64x(4*DFL_STRIDE);

    unsigned long cache_off = ((unsigned long) ptr) & (DFL_STRIDE-1uL);

    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_STRIDE - 1));

    field_size += (((unsigned long)obj + field_off) - (unsigned long)aligned_obj);
    field_size = ((field_size + 4*DFL_STRIDE - 1) / (4*DFL_STRIDE)) * 4*DFL_STRIDE;
    unsigned char* _end =  aligned_obj + field_size;

    DEBUG("GLOB AVX2 LOAD: %p - size: %lu - ptr: %p - sizeof: %lu\n", obj, field_size, ptr, sizeof(unsigned long));

    // initialize the current avx ptrs for each iteration
    __m256i current = _mm256_set1_epi64x((unsigned long) aligned_obj + cache_off);
    current  = _mm256_add_epi64(current, index);

    for(volatile unsigned char* _ptr = aligned_obj + cache_off; _ptr < _end; _ptr = _ptr + 4*DFL_STRIDE) {
        // the mask will select which value will get actually loaded
        __m256i mask = _mm256_cmpeq_epi64(target, current);
        current = _mm256_add_epi64(current, increment);
        __m256i loaded = _mm256_i64gather_epi64(_ptr, index, 1);
        res = _mm256_blendv_epi8(res, loaded, mask);
    }
    return mm256_hadd_to_64(res);
}

DFL_FUNC uint32_t uint32_t_avx2_gather_dfl_obj_load(dfl_obj_list_head head, unsigned char* ptr, unsigned long field_off, unsigned long field_size) { 
    uint32_t partial_res = 0;

    // if the ptr is 64 bit wide we must use the slow version since it does not fit 8 times in the vectors
    if (((unsigned long) ptr != (unsigned int) ptr))
        return (uint32_t) uint64_t_avx2_gather_dfl_obj_load(head, ptr, field_off, field_size);
        
    // set the index vector to 0,1,2,3 "little" endian
    // so that 0 is in index[0:31], ecc
    __m256i index = _mm256_setr_epi32(0*DFL_STRIDE, 1*DFL_STRIDE, 2*DFL_STRIDE, 3*DFL_STRIDE,
                                       4*DFL_STRIDE, 5*DFL_STRIDE, 6*DFL_STRIDE, 7*DFL_STRIDE);
    __m256i res   = _mm256_setzero_si256();

    __m256i target    = _mm256_set1_epi32((unsigned long) ptr);
    __m256i increment = _mm256_set1_epi32(8*DFL_STRIDE);

    unsigned long cache_off = ((unsigned long) ptr) & (DFL_STRIDE-1uL);

    while(head) {
        unsigned char* aligned_obj = (unsigned char*)(((unsigned long)head->data + field_off) & ~(DFL_STRIDE - 1));
        field_size += (((unsigned long)head->data + field_off) - (unsigned long)aligned_obj);
        field_size = ((field_size + 8*DFL_STRIDE - 1) / (8*DFL_STRIDE)) * 8*DFL_STRIDE;
        unsigned char* _end =  aligned_obj + field_size;

        // 64-bit size check as at the beginning on obj;
        if (((unsigned long) aligned_obj != (unsigned int) aligned_obj)) {
            partial_res |= uint64_t_avx2_gather_dfl_glob_load(aligned_obj, ptr, field_off, field_size);
            head = head->next;
            continue;
        }

        // initialize the current avx ptrs for each iteration
        __m256i current = _mm256_set1_epi32((unsigned long) aligned_obj + cache_off);
        current  = _mm256_add_epi32(current, index);

        for(volatile unsigned char* _ptr = aligned_obj + cache_off; _ptr < _end; _ptr = _ptr + 8*DFL_STRIDE) {
            // the mask will select which value will get actually loaded
            __m256i mask = _mm256_cmpeq_epi32(target, current);
            current = _mm256_add_epi32(current, increment);
            __m256i loaded = _mm256_i32gather_epi32(_ptr, index, 1);
            res = _mm256_blendv_epi8(res, loaded, mask);
        }
        head = head->next;
    }
    return partial_res | mm256_hadd_to_32(res);
}

DFL_FUNC uint32_t uint32_t_avx2_gather_dfl_glob_load(unsigned char* obj, unsigned char* ptr, unsigned long field_off, unsigned long field_size) { 

    // if the ptr is 64 bit wide we must use the slow version since it does not fit 8 times in the vectors
    if (((unsigned long) ptr != (unsigned int) ptr) || ((unsigned long) obj != (unsigned int) obj))
        return (uint32_t) uint64_t_avx2_gather_dfl_glob_load(obj, ptr, field_off, field_size);
        
    // set the index vector to 0,1,2,3 "little" endian
    // so that 0 is in index[0:31], ecc
    __m256i index = _mm256_setr_epi32(0*DFL_STRIDE, 1*DFL_STRIDE, 2*DFL_STRIDE, 3*DFL_STRIDE,
                                       4*DFL_STRIDE, 5*DFL_STRIDE, 6*DFL_STRIDE, 7*DFL_STRIDE);
    __m256i res   = _mm256_setzero_si256();

    __m256i target    = _mm256_set1_epi32((unsigned long) ptr);
    __m256i increment = _mm256_set1_epi32(8*DFL_STRIDE);

    unsigned long cache_off = ((unsigned long) ptr) & (DFL_STRIDE-1uL);

    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_STRIDE - 1));

    field_size += (((unsigned long)obj + field_off) - (unsigned long)aligned_obj);
    field_size = ((field_size + 8*DFL_STRIDE - 1) / (8*DFL_STRIDE)) * 8*DFL_STRIDE;
    unsigned char* _end =  aligned_obj + field_size;

    // initialize the current avx ptrs for each iteration
    __m256i current = _mm256_set1_epi32((unsigned long) aligned_obj + cache_off);
    current  = _mm256_add_epi32(current, index);

    for(volatile unsigned char* _ptr = aligned_obj + cache_off; _ptr < _end; _ptr = _ptr + 8*DFL_STRIDE) {
        // the mask will select which value will get actually loaded
        __m256i mask = _mm256_cmpeq_epi32(target, current);
        current = _mm256_add_epi32(current, increment);
        __m256i loaded = _mm256_i32gather_epi32(_ptr, index, 1);
        res = _mm256_blendv_epi8(res, loaded, mask);
    }
    return mm256_hadd_to_32(res);
}

#define AVX2_LINESIZE 32
#define AVX_INCREMENT (8uL)
#if DFL_STRIDE > AVX2_LINESIZE
 #define DFL_FIXED_AVX_STRIDE DFL_STRIDE
#else
 #define DFL_FIXED_AVX_STRIDE AVX2_LINESIZE
#endif
DFL_FUNC uint64_t uint64_t_avx2_linear_dfl_glob_load(unsigned char* obj, unsigned char* ptr, unsigned long field_off, unsigned long field_size) { 
    // set the index vector to 0,8,16,24 "little" endian
    // so that 0 is in index[0:31], ecc
    __m256i index = _mm256_setr_epi64x(AVX_INCREMENT*0, AVX_INCREMENT*1, AVX_INCREMENT*2, AVX_INCREMENT*3);
    __m256i res   = _mm256_setzero_si256();

#if DFL_STRIDE < AVX_INCREMENT
    // if DFL_STRIDE is lower than the index granularity, we should align the target properly.
    // this masks the relevant bits for AVX_INCREMENT, but not the ones that will be reintroduced while adding `cache_off`
    __m256i target    = _mm256_set1_epi64x(((unsigned long) ptr & ~(AVX_INCREMENT-DFL_STRIDE)));
#else
    __m256i target    = _mm256_set1_epi64x(((unsigned long) ptr));
#endif
    __m256i increment = _mm256_set1_epi64x(DFL_FIXED_AVX_STRIDE);

    unsigned long cache_off = ((unsigned long) ptr) & (DFL_STRIDE-1uL);

    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_STRIDE - 1));

    field_size += (((unsigned long)obj + field_off) - (unsigned long)aligned_obj);
    field_size = ((field_size + DFL_FIXED_AVX_STRIDE - 1) / (DFL_FIXED_AVX_STRIDE)) * DFL_FIXED_AVX_STRIDE;
    unsigned char* _end =  aligned_obj + field_size;

    // initialize the current avx ptrs for each iteration
    __m256i current = _mm256_set1_epi64x((unsigned long) aligned_obj + cache_off);
    current  = _mm256_add_epi64(current, index);

    for(volatile unsigned char* _ptr = aligned_obj + cache_off; _ptr < _end; _ptr = _ptr + DFL_FIXED_AVX_STRIDE) {
        // the mask will select which value will get actually loaded
        __m256i mask = _mm256_cmpeq_epi64(target, current);
        current = _mm256_add_epi64(current, increment);
        res = _mm256_blendv_epi8(res, _mm256_loadu_si256((__m256i *)_ptr), mask);
    }
    // if the target was aligned, shift the result to get the right value
    // NOTICE: this assumes that accesses to `TYPE` are aligned to `sizeof(TYPE)`
#if DFL_STRIDE < AVX_INCREMENT
    return mm256_hadd_to_64(res) >> (8*(((unsigned long) ptr) & (AVX_INCREMENT-DFL_STRIDE)));
#else
    return mm256_hadd_to_64(res);
#endif
}

DFL_FUNC void uint64_t_avx2_scatter_dfl_glob_store(unsigned char* obj, unsigned char* ptr, unsigned long field_off, unsigned long field_size, uint64_t value) { 
        
    // set the index vector to 0,1,2,3 "little" endian
    // so that 0 is in index[0:31], ecc
    __m256i index = _mm256_setr_epi64x(0*DFL_STRIDE, 1*DFL_STRIDE, 2*DFL_STRIDE, 3*DFL_STRIDE);

    __m256i target    = _mm256_set1_epi64x((unsigned long) ptr);
    __m256i increment = _mm256_set1_epi64x(4*DFL_STRIDE);

    unsigned long cache_off = ((unsigned long) ptr) & (DFL_STRIDE-1uL);

    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_STRIDE - 1));

    field_size += (((unsigned long)obj + field_off) - (unsigned long)aligned_obj);
    field_size = ((field_size + 4*DFL_STRIDE - 1) / (4*DFL_STRIDE)) * 4*DFL_STRIDE;
    unsigned char* _end =  aligned_obj + field_size;

    // initialize the current avx ptrs for each iteration
    __m256i current = _mm256_set1_epi64x((unsigned long) aligned_obj + cache_off);
    current  = _mm256_add_epi64(current, index);

    __m256i valuev = _mm256_set1_epi64x(value);

    for(volatile unsigned char* _ptr = aligned_obj + cache_off; _ptr < _end; _ptr = _ptr + 4*DFL_STRIDE) {
        // the mask will select which value will get actually loaded
        __mmask8 mask = _mm256_cmpeq_epi64_mask(target, current);
        current = _mm256_add_epi64(current, increment);

        _mm256_mask_i64scatter_epi64((long long *)_ptr, mask, index, valuev, 1);
    }
}

DFL_FUNC void uint32_t_avx2_scatter_dfl_glob_store(unsigned char* obj, unsigned char* ptr, unsigned long field_off, unsigned long field_size, uint64_t value) { 

    // if the ptr is 64 bit wide we must use the slow version since it does not fit 8 times in the vectors
    if (((unsigned long) ptr != (unsigned int) ptr) || ((unsigned long) obj != (unsigned int) obj))
        return uint64_t_avx2_scatter_dfl_glob_store(obj, ptr, field_off, field_size, value);
        
    // set the index vector to 0,1,2,3 "little" endian
    // so that 0 is in index[0:31], ecc
    __m256i index = _mm256_setr_epi32(0*DFL_STRIDE, 1*DFL_STRIDE, 2*DFL_STRIDE, 3*DFL_STRIDE,
                                       4*DFL_STRIDE, 5*DFL_STRIDE, 6*DFL_STRIDE, 7*DFL_STRIDE);

    __m256i target    = _mm256_set1_epi32((unsigned long) ptr);
    __m256i increment = _mm256_set1_epi32(8*DFL_STRIDE);

    unsigned long cache_off = ((unsigned long) ptr) & (DFL_STRIDE-1uL);

    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_STRIDE - 1));

    field_size += (((unsigned long)obj + field_off) - (unsigned long)aligned_obj);
    field_size = ((field_size + 8*DFL_STRIDE - 1) / (8*DFL_STRIDE)) * 8*DFL_STRIDE;
    unsigned char* _end =  aligned_obj + field_size;

    // initialize the current avx ptrs for each iteration
    __m256i current = _mm256_set1_epi32((unsigned long) aligned_obj + cache_off);
    current  = _mm256_add_epi32(current, index);

    __m256i valuev = _mm256_set1_epi32(value);

    for(volatile unsigned char* _ptr = aligned_obj + cache_off; _ptr < _end; _ptr = _ptr + 8*DFL_STRIDE) {
        // the mask will select which value will get actually loaded
        __mmask8 mask = _mm256_cmpeq_epi32_mask(target, current);
        current = _mm256_add_epi32(current, increment);

        _mm256_mask_i32scatter_epi32((long long *)_ptr, mask, index, valuev, 1);
    }
}

#define DFL_AVX2_SCATTER_GLOB_STORE(type) DFL_FUNC void type ## _avx2_scatter_dfl_glob_store(unsigned char* obj, unsigned char* ptr, unsigned long field_off, unsigned long field_size, type value) { \
    if (((unsigned long) ptr != (unsigned int) ptr) || ((unsigned long) obj != (unsigned int) obj))\
        return uint64_t_avx2_scatter_dfl_glob_store(obj, ptr, field_off, field_size, value);\
    __m256i index = _mm256_setr_epi32(0*DFL_STRIDE, 1*DFL_STRIDE, 2*DFL_STRIDE, 3*DFL_STRIDE,\
                                       4*DFL_STRIDE, 5*DFL_STRIDE, 6*DFL_STRIDE, 7*DFL_STRIDE);\
    __m256i target    = _mm256_set1_epi32((unsigned long) ptr);\
    __m256i increment = _mm256_set1_epi32(8*DFL_STRIDE);\
    unsigned long cache_off = ((unsigned long) ptr) & (8*DFL_STRIDE-1uL);\
    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(8*DFL_STRIDE - 1));\
    field_size += (((unsigned long)obj + field_off) - (unsigned long)aligned_obj); \
    field_size = ((field_size + 8*DFL_STRIDE - 1) / (8*DFL_STRIDE)) * 8*DFL_STRIDE;\
    unsigned char* _end =  aligned_obj + field_size;\
    __m256i current = _mm256_set1_epi32((unsigned long) aligned_obj + cache_off);\
    current  = _mm256_add_epi32(current, index);\
    uint64_t write_mask = (1uL << (sizeof(value) * 8)) - 1uL; \
    for(volatile unsigned char* _ptr = aligned_obj + cache_off; _ptr < _end; _ptr = _ptr + 8*DFL_STRIDE) {\
        __mmask8 mask = _mm256_cmpeq_epi32_mask(target, current);\
        current = _mm256_add_epi32(current, increment);\
        uint64_t writev_ = (*((unsigned long *)_ptr) & (~write_mask)) | value; \
        __m256i writev = _mm256_setr_epi32(writev_, 0, 0, 0, 0, 0, 0, 0); \
        _mm256_mask_i32scatter_epi32((long long *)_ptr, mask, index, writev, 1);\
    }\
}

DFL_FUNC void uint64_t_avx2_linear_dfl_glob_store(unsigned char* obj, unsigned char* ptr, unsigned long field_off, unsigned long field_size, uint64_t value) { 
    // set the index vector to 0,8,16,24 "little" endian
    // so that 0 is in index[0:31], ecc
    __m256i index = _mm256_setr_epi64x(AVX_INCREMENT*0, AVX_INCREMENT*1, AVX_INCREMENT*2, AVX_INCREMENT*3);

    __m256i target    = _mm256_set1_epi64x((unsigned long) ptr);
    __m256i increment = _mm256_set1_epi64x(DFL_FIXED_AVX_STRIDE);
    __m256i writev = _mm256_setr_epi64x(value, value, value, value);

    unsigned long cache_off = ((unsigned long) ptr) & (DFL_STRIDE-1uL);

    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_STRIDE - 1));

    field_size += (((unsigned long)obj + field_off) - (unsigned long)aligned_obj);
    field_size = ((field_size + DFL_FIXED_AVX_STRIDE - 1) / (DFL_FIXED_AVX_STRIDE)) * DFL_FIXED_AVX_STRIDE;
    unsigned char* _end =  aligned_obj + field_size;

    // initialize the current avx ptrs for each iteration
    __m256i current = _mm256_set1_epi64x((unsigned long) aligned_obj + cache_off);
    current  = _mm256_add_epi64(current, index);

    for(volatile unsigned char* _ptr = aligned_obj + cache_off; _ptr < _end; _ptr = _ptr + DFL_FIXED_AVX_STRIDE) {
        __m256i mask = _mm256_cmpeq_epi64(target, current);
        current = _mm256_add_epi64(current, increment);
        _mm256_storeu_si256((__m256i *)_ptr, _mm256_blendv_epi8(_mm256_loadu_si256((__m256i *)_ptr), writev, mask));
    }
}

#if DFL_STRIDE < AVX_INCREMENT
    // if DFL_STRIDE is lower than the index granularity, we should align the target properly.
    // this masks the relevant bits for AVX_INCREMENT, but not the ones that will be reintroduced while adding `cache_off`
    #define DFL_CORRECTION (AVX_INCREMENT-DFL_STRIDE)
#else
    #define DFL_CORRECTION (0uL)
#endif
#define DFL_AVX2_LINEAR_GLOB_STORE(type) DFL_FUNC void type ## _avx2_linear_dfl_glob_store(unsigned char* obj, unsigned char* ptr, unsigned long field_off, unsigned long field_size, type value) { \
    __m256i index = _mm256_setr_epi64x(AVX_INCREMENT*0, AVX_INCREMENT*1, AVX_INCREMENT*2, AVX_INCREMENT*3); \
    __m256i target    = _mm256_set1_epi64x((unsigned long) ptr & ~DFL_CORRECTION); \
    __m256i increment = _mm256_set1_epi64x(DFL_FIXED_AVX_STRIDE); \
    uint64_t write_mask_ = ((1uL << (sizeof(value) * 8)) - 1uL) << (8*(((unsigned long) ptr) & (DFL_CORRECTION))); \
    __m256i write_mask   = _mm256_set1_epi64x(write_mask_); \
    uint64_t shifted_value = ((uint64_t)value) << (8*(((unsigned long) ptr) & (DFL_CORRECTION))); \
    __m256i writev = _mm256_setr_epi64x(shifted_value, shifted_value, shifted_value, shifted_value); \
    unsigned long cache_off = ((unsigned long) ptr) & (DFL_STRIDE-1uL); \
    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_STRIDE - 1)); \
    field_size += (((unsigned long)obj + field_off) - (unsigned long)aligned_obj); \
    field_size = ((field_size + DFL_FIXED_AVX_STRIDE - 1) / (DFL_FIXED_AVX_STRIDE)) * DFL_FIXED_AVX_STRIDE; \
    unsigned char* _end =  aligned_obj + field_size; \
    __m256i current = _mm256_set1_epi64x((unsigned long) aligned_obj + cache_off); \
    current  = _mm256_add_epi64(current, index); \
    for(volatile unsigned char* _ptr = aligned_obj + cache_off; _ptr < _end; _ptr = _ptr + DFL_FIXED_AVX_STRIDE) { \
        __m256i mask = _mm256_and_si256(_mm256_cmpeq_epi64(target, current), write_mask); \
        current = _mm256_add_epi64(current, increment); \
        _mm256_storeu_si256((__m256i *)_ptr, _mm256_blendv_epi8(_mm256_loadu_si256((__m256i *)_ptr), writev, mask)); \
    } \
}

DFL_FUNC uint64_t uint64_t_avx2_linear_dfl_obj_load(dfl_obj_list_head head, unsigned char* ptr, unsigned long field_off, unsigned long field_size) { 
    // set the index vector to 0,8,16,24 "little" endian
    // so that 0 is in index[0:31], ecc
    __m256i index = _mm256_setr_epi64x(AVX_INCREMENT*0, AVX_INCREMENT*1, AVX_INCREMENT*2, AVX_INCREMENT*3);
    __m256i res   = _mm256_setzero_si256();

#if DFL_STRIDE < AVX_INCREMENT
    // if DFL_STRIDE is lower than the index granularity, we should align the target properly.
    // this masks the relevant bits for AVX_INCREMENT, but not the ones that will be reintroduced while adding `cache_off`
    __m256i target    = _mm256_set1_epi64x(((unsigned long) ptr & ~(AVX_INCREMENT-DFL_STRIDE)));
#else
    __m256i target    = _mm256_set1_epi64x(((unsigned long) ptr));
#endif
    __m256i increment = _mm256_set1_epi64x(DFL_FIXED_AVX_STRIDE);

    unsigned long cache_off = ((unsigned long) ptr) & (DFL_STRIDE-1uL);

    while(head) {
        unsigned char* aligned_obj = (unsigned char*)(((unsigned long)head->data + field_off) & ~(DFL_STRIDE - 1));

        field_size += (((unsigned long)head->data + field_off) - (unsigned long)aligned_obj);
        field_size = ((field_size + DFL_FIXED_AVX_STRIDE - 1) / (DFL_FIXED_AVX_STRIDE)) * DFL_FIXED_AVX_STRIDE;
        unsigned char* _end =  aligned_obj + field_size;

        // initialize the current avx ptrs for each iteration
        __m256i current = _mm256_set1_epi64x((unsigned long) aligned_obj + cache_off);
        current  = _mm256_add_epi64(current, index);

        for(volatile unsigned char* _ptr = aligned_obj + cache_off; _ptr < _end; _ptr = _ptr + DFL_FIXED_AVX_STRIDE) {
            // the mask will select which value will get actually loaded
            __m256i mask = _mm256_cmpeq_epi64(target, current);
            current = _mm256_add_epi64(current, increment);
            res = _mm256_blendv_epi8(res, _mm256_loadu_si256((__m256i *)_ptr), mask);
        }
        head = head->next;
    }
    // if the target was aligned, shift the result to get the right value
    // NOTICE: this assumes that accesses to `TYPE` are aligned to `sizeof(TYPE)`
#if DFL_STRIDE < AVX_INCREMENT
    return mm256_hadd_to_64(res) >> (8*(((unsigned long) ptr) & (AVX_INCREMENT-DFL_STRIDE)));
#else
    return mm256_hadd_to_64(res);
#endif
}

DFL_AVX2_SCATTER_GLOB_STORE(uint16_t)
DFL_AVX2_SCATTER_GLOB_STORE(uint8_t)

#endif /* __AVX2__ */

#if defined(__AVX512F__)

DFL_FUNC uint64_t uint64_t_avx512_gather_dfl_glob_load(unsigned char* obj, unsigned char* ptr, unsigned long field_off, unsigned long field_size) { 
    // set the index vector to 0,1,2,3... "little" endian
    // so that 0 is in index[0:31], ecc
    __m512i index = _mm512_setr_epi64(0*DFL_STRIDE, 1*DFL_STRIDE, 2*DFL_STRIDE, 3*DFL_STRIDE,
                                      4*DFL_STRIDE, 5*DFL_STRIDE, 6*DFL_STRIDE, 7*DFL_STRIDE);
    __m512i res   = _mm512_setzero_si512();
    #if !DFL_READONLY
    GET_REAL_PTR(obj, ptr, field_off, field_size, uint64_t);
    #endif
    __m512i target    = _mm512_set1_epi64((unsigned long) ptr);
    __m512i increment = _mm512_set1_epi64(8*DFL_STRIDE);

    unsigned long cache_off = ((unsigned long) ptr) & (DFL_STRIDE-1uL);
    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_STRIDE - 1));

    unsigned char* _end = obj + field_off + field_size;
    __m512i endv    = _mm512_set1_epi64((unsigned long) _end);

    // initialize the current avx ptrs for each iteration
    __m512i current = _mm512_set1_epi64((unsigned long) aligned_obj + cache_off);
    current  = _mm512_add_epi64(current, index);

    for(volatile unsigned char* _ptr = aligned_obj + cache_off; _ptr < _end; _ptr = _ptr + 8*DFL_STRIDE) {
        // the mask will select which value will get actually loaded
        __mmask8 mask = _mm512_cmpeq_epi64_mask(target, current);
        /* we use an oob mask to avoid making accesses outside the object */
        __mmask8 oob_mask = _mm512_cmplt_epi64_mask(current, endv);
        current = _mm512_add_epi64(current, increment);
        __m512i loaded = _mm512_mask_i64gather_epi64(res, oob_mask, index, _ptr, 1);
        res = _mm512_mask_blend_epi64(mask, res, loaded);
    }
    return _mm512_reduce_add_epi64(res);
}

DFL_FUNC uint32_t uint32_t_avx512_gather_dfl_glob_load(unsigned char* obj, unsigned char* ptr, unsigned long field_off, unsigned long field_size) { 

    // if the ptr is 64 bit wide we must use the slow version since it does not fit 8 times in the vectors
    if (((unsigned long) ptr != (unsigned int) ptr) || ((unsigned long) obj != (unsigned int) obj))
        return (uint32_t) uint64_t_avx512_gather_dfl_glob_load(obj, ptr, field_off, field_size);
        
    // set the index vector to 0,1,2,3... "little" endian
    // so that 0 is in index[0:31], ecc
    __m512i index = _mm512_setr_epi32(0*DFL_STRIDE, 1*DFL_STRIDE, 2*DFL_STRIDE, 3*DFL_STRIDE,
                                       4*DFL_STRIDE, 5*DFL_STRIDE, 6*DFL_STRIDE, 7*DFL_STRIDE,
                                       8*DFL_STRIDE, 9*DFL_STRIDE, 10*DFL_STRIDE, 11*DFL_STRIDE,
                                       12*DFL_STRIDE, 13*DFL_STRIDE, 14*DFL_STRIDE, 15*DFL_STRIDE);
    __m512i res   = _mm512_setzero_si512();
    #if !DFL_READONLY
    GET_REAL_PTR(obj, ptr, field_off, field_size, uint32_t);
    #endif
    __m512i target    = _mm512_set1_epi32((unsigned long) ptr);
    __m512i increment = _mm512_set1_epi32(16*DFL_STRIDE);

    unsigned long cache_off = ((unsigned long) ptr) & (DFL_STRIDE-1uL);
    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_STRIDE - 1));

    unsigned char* _end =  obj + field_off + field_size;
    __m512i endv    = _mm512_set1_epi32((unsigned long) _end);

    // initialize the current avx ptrs for each iteration
    __m512i current = _mm512_set1_epi32((unsigned long) aligned_obj + cache_off);
    current  = _mm512_add_epi32(current, index);

    for(volatile unsigned char* _ptr = aligned_obj + cache_off; _ptr < _end; _ptr = _ptr + 16*DFL_STRIDE) {
        // the mask will select which value will get actually loaded
        __mmask16 mask = _mm512_cmpeq_epi32_mask(target, current);
        /* we use an oob mask to avoid making accesses outside the object */
        __mmask16 oob_mask = _mm512_cmplt_epi32_mask(current, endv);
        current = _mm512_add_epi32(current, increment);
        __m512i loaded = _mm512_mask_i32gather_epi32(res, oob_mask, index, _ptr, 1);
        res = _mm512_mask_blend_epi32(mask, res, loaded);
    }
    return _mm512_reduce_add_epi32(res);
}

DFL_FUNC uint64_t uint64_t_avx512_gather_dfl_obj_load(dfl_obj_list_head head, unsigned char* ptr, unsigned long field_off, unsigned long field_size) { 
    // set the index vector to 0,1,2,3... "little" endian
    // so that 0 is in index[0:31], ecc
    __m512i index = _mm512_setr_epi64(0*DFL_STRIDE, 1*DFL_STRIDE, 2*DFL_STRIDE, 3*DFL_STRIDE,
                                      4*DFL_STRIDE, 5*DFL_STRIDE, 6*DFL_STRIDE, 7*DFL_STRIDE);
    __m512i res   = _mm512_setzero_si512();
    __m512i increment = _mm512_set1_epi64(8*DFL_STRIDE);

    unsigned long copy_field_off = field_off;
    unsigned long copy_field_size = field_size;
    while(head) {
        unsigned char* obj = head->data;
        #if !DFL_READONLY
        GET_REAL_PTR(obj, ptr, field_off, field_size, uint64_t);
        #endif
        __m512i target = _mm512_set1_epi64((unsigned long) ptr);

        unsigned long cache_off = ((unsigned long) ptr) & (DFL_STRIDE-1uL);
        unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_STRIDE - 1));

        unsigned char* _end = obj + field_off + field_size;
        __m512i endv    = _mm512_set1_epi64((unsigned long) _end);

        // initialize the current avx ptrs for each iteration
        __m512i current = _mm512_set1_epi64((unsigned long) aligned_obj + cache_off);
        current  = _mm512_add_epi64(current, index);

        for(volatile unsigned char* _ptr = aligned_obj + cache_off; _ptr < _end; _ptr = _ptr + 8*DFL_STRIDE) {
            // the mask will select which value will get actually loaded
            __mmask8 mask = _mm512_cmpeq_epi64_mask(target, current);
            /* we use an oob mask to avoid making accesses outside the object */
            __mmask8 oob_mask = _mm512_cmplt_epi64_mask(current, endv);
            current = _mm512_add_epi64(current, increment);
            __m512i loaded = _mm512_mask_i64gather_epi64(res, oob_mask, index, _ptr, 1);
            res = _mm512_mask_blend_epi64(mask, res, loaded);
        }
        head = head->next;
        field_off = copy_field_off;
        field_size = copy_field_size;
    }
    return _mm512_reduce_add_epi64(res);
}

DFL_FUNC uint32_t uint32_t_avx512_gather_dfl_obj_load(dfl_obj_list_head head, unsigned char* ptr, unsigned long field_off, unsigned long field_size) { 
    // uint32_t partial_res = 0;

    // if the ptr is 64 bit wide we must use the slow version since it does not fit 8 times in the vectors
    if (((unsigned long) ptr != (unsigned int) ptr))
        return (uint32_t) uint64_t_avx512_gather_dfl_obj_load(head, ptr, field_off, field_size);
        
    // set the index vector to 0,1,2,3... "little" endian
    // so that 0 is in index[0:31], ecc
    __m512i index = _mm512_setr_epi32(0*DFL_STRIDE, 1*DFL_STRIDE, 2*DFL_STRIDE, 3*DFL_STRIDE,
                                       4*DFL_STRIDE, 5*DFL_STRIDE, 6*DFL_STRIDE, 7*DFL_STRIDE,
                                       8*DFL_STRIDE, 9*DFL_STRIDE, 10*DFL_STRIDE, 11*DFL_STRIDE,
                                       12*DFL_STRIDE, 13*DFL_STRIDE, 14*DFL_STRIDE, 15*DFL_STRIDE);
    __m512i res   = _mm512_setzero_si512();
    __m512i increment = _mm512_set1_epi32(16*DFL_STRIDE);

    unsigned long copy_field_off = field_off;
    unsigned long copy_field_size = field_size;
    while(head) {
        unsigned char* obj = head->data;
        #if !DFL_READONLY
        GET_REAL_PTR(obj, ptr, field_off, field_size, uint32_t);
        #endif
        __m512i target = _mm512_set1_epi32((unsigned long) ptr);

        unsigned long cache_off = ((unsigned long) ptr) & (DFL_STRIDE-1uL);
        unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_STRIDE - 1));

        unsigned char* _end = obj + field_off + field_size;
        __m512i endv = _mm512_set1_epi32((unsigned long) _end);

        // // Align the real end to DFL_STRIDE
        // __m512i endv    = _mm512_set1_epi32((unsigned long) aligned_obj + (((field_size + DFL_STRIDE - 1) / (DFL_STRIDE)) * DFL_STRIDE));

        // field_size = ((field_size + 16*DFL_STRIDE - 1) / (16*DFL_STRIDE)) * 16*DFL_STRIDE;
        // unsigned char* _end =  aligned_obj + field_size;

        // // 64-bit size check as at the beginning on obj;
        // if (((unsigned long) aligned_obj != (unsigned int) aligned_obj)) {
        //     partial_res |= uint64_t_avx512_gather_dfl_glob_load(aligned_obj, ptr, field_off, field_size);
        //     head = head->next;
        //     continue;
        // }

        // initialize the current avx ptrs for each iteration
        __m512i current = _mm512_set1_epi32((unsigned long) aligned_obj + cache_off);
        current  = _mm512_add_epi32(current, index);

        for(volatile unsigned char* _ptr = aligned_obj + cache_off; _ptr < _end; _ptr = _ptr + 16*DFL_STRIDE) {
            // the mask will select which value will get actually loaded
            __mmask16 mask = _mm512_cmpeq_epi32_mask(target, current);
            /* we use an oob mask to avoid making accesses outside the object */
            __mmask16 oob_mask = _mm512_cmplt_epi32_mask(current, endv);
            current = _mm512_add_epi32(current, increment);
            __m512i loaded = _mm512_mask_i32gather_epi32(res, oob_mask, index, _ptr, 1);
            res = _mm512_mask_blend_epi32(mask, res, loaded);
        }
        head = head->next;
        field_off = copy_field_off;
        field_size = copy_field_size;
    }
    return /*partial_res |*/ _mm512_reduce_add_epi32(res);
}

#define AVX2_LINESIZE 32
#define AVX_INCREMENT (8uL)
#define AVX_INCREMENT_4 (4uL)
#if DFL_STRIDE > AVX2_LINESIZE
 #define DFL_FIXED_AVX_STRIDE DFL_STRIDE
#else
 #define DFL_FIXED_AVX_STRIDE AVX2_LINESIZE
#endif
#if DFL_STRIDE < AVX_INCREMENT
    #define DFL_CORRECTION (AVX_INCREMENT-DFL_STRIDE)
#else
    #define DFL_CORRECTION (0uL)
#endif
#define AVX512_LINESIZE 64
#if DFL_STRIDE > AVX512_LINESIZE
 #define DFL_FIXED_AVX512_STRIDE DFL_STRIDE
#else
 #define DFL_FIXED_AVX512_STRIDE AVX512_LINESIZE
#endif

DFL_FUNC uint64_t uint64_t_avx512_linear_dfl_glob_load(unsigned char* obj, unsigned char* ptr, unsigned long field_off, unsigned long field_size) { 
    // set the index vector to 0,8,16,24... "little" endian
    // so that 0 is in index[0:31], ecc
    __m512i index = _mm512_setr_epi64(AVX_INCREMENT*0, AVX_INCREMENT*1, AVX_INCREMENT*2, AVX_INCREMENT*3, AVX_INCREMENT*4, AVX_INCREMENT*5, AVX_INCREMENT*6, AVX_INCREMENT*7);
    __m512i res   = _mm512_setzero_si512();
    #if !DFL_READONLY
    GET_REAL_PTR(obj, ptr, field_off, field_size, uint64_t);
    #endif

// #if DFL_STRIDE < AVX_INCREMENT
//     // if DFL_STRIDE is lower than the index granularity, we should align the target properly.
//     // this masks the relevant bits for AVX_INCREMENT, but not the ones that will be reintroduced while adding `cache_off`
//     __m512i target    = _mm512_set1_epi64(((unsigned long) ptr & ~(AVX_INCREMENT-DFL_STRIDE)));
// #else
    __m512i target    = _mm512_set1_epi64(((unsigned long) ptr));
// #endif
    __m512i increment = _mm512_set1_epi64(DFL_FIXED_AVX512_STRIDE);

    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_FIXED_AVX512_STRIDE - 1));
    unsigned char* _end = obj + field_off + field_size;

    // initialize the current avx ptrs for each iteration
    __m512i current = _mm512_set1_epi64((unsigned long) aligned_obj);
    current  = _mm512_add_epi64(current, index);

    for(volatile unsigned char* _ptr = aligned_obj; _ptr < _end; _ptr = _ptr + DFL_FIXED_AVX512_STRIDE) {
        // the mask will select which value will get actually loaded
        __mmask8 mask = _mm512_cmpeq_epi64_mask(target, current);
        current = _mm512_add_epi64(current, increment);
        res = _mm512_mask_load_epi64(res, mask, _ptr);
    }
    // if the target was aligned, shift the result to get the right value
    // NOTICE: this assumes that accesses to `TYPE` are aligned to `sizeof(TYPE)`
// #if DFL_STRIDE < AVX_INCREMENT
//     return _mm512_reduce_add_epi64(res) >> (8*(((unsigned long) ptr) & (AVX_INCREMENT-DFL_STRIDE)));
// #else
    return _mm512_reduce_add_epi64(res);
// #endif
}

DFL_FUNC uint32_t uint32_t_avx512_linear_dfl_glob_load(unsigned char* obj, unsigned char* ptr, unsigned long field_off, unsigned long field_size) {
    if (((unsigned long) ptr != (unsigned int) ptr) || ((unsigned long) obj != (unsigned int) obj))
        return (uint32_t) uint64_t_avx512_linear_dfl_glob_load(obj, ptr, field_off, field_size);
        
    // set the index vector to 0,1,2,3... "little" endian
    // so that 0 is in index[0:31], ecc
    __m512i index = _mm512_setr_epi32(0*AVX_INCREMENT_4, 1*AVX_INCREMENT_4, 2*AVX_INCREMENT_4, 3*AVX_INCREMENT_4,
                                    4*AVX_INCREMENT_4, 5*AVX_INCREMENT_4, 6*AVX_INCREMENT_4, 7*AVX_INCREMENT_4,
                                    8*AVX_INCREMENT_4, 9*AVX_INCREMENT_4, 10*AVX_INCREMENT_4, 11*AVX_INCREMENT_4,
                                    12*AVX_INCREMENT_4, 13*AVX_INCREMENT_4, 14*AVX_INCREMENT_4, 15*AVX_INCREMENT_4);
    __m512i res   = _mm512_setzero_si512();
    DEBUG("U32 linear load 0: ptr: %p - obj: %p - offset: %lu - size: %lu\n", ptr, obj, field_off, field_size);
    #if !DFL_READONLY
    GET_REAL_PTR(obj, ptr, field_off, field_size, uint32_t);
    #endif
    DEBUG("U32 linear load 1: ptr: %p - obj: %p - offset: %lu - size: %lu\n", ptr, obj, field_off, field_size);
    __m512i target    = _mm512_set1_epi32((unsigned long) ptr);
    __m512i increment = _mm512_set1_epi32(DFL_FIXED_AVX512_STRIDE);

    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_FIXED_AVX512_STRIDE - 1));
    unsigned char* _end = obj + field_off + field_size;

    __m512i current = _mm512_set1_epi32((unsigned long) aligned_obj);
    current  = _mm512_add_epi32(current, index);

    for(volatile unsigned char* _ptr = aligned_obj; _ptr < _end; _ptr = _ptr + DFL_FIXED_AVX512_STRIDE) {
        __mmask16 mask = _mm512_cmpeq_epi32_mask(target, current);
        current = _mm512_add_epi32(current, increment);
        res = _mm512_mask_load_epi32(res, mask, _ptr);
    }
    return _mm512_reduce_add_epi32(res);
}

DFL_FUNC uint64_t uint64_t_avx512_linear_dfl_obj_load(dfl_obj_list_head head, unsigned char* ptr, unsigned long field_off, unsigned long field_size) { 
    // set the index vector to 0,8,16,24... "little" endian
    // so that 0 is in index[0:31], ecc
    __m512i index = _mm512_setr_epi64(AVX_INCREMENT*0, AVX_INCREMENT*1, AVX_INCREMENT*2, AVX_INCREMENT*3, AVX_INCREMENT*4, AVX_INCREMENT*5, AVX_INCREMENT*6, AVX_INCREMENT*7);
    __m512i res   = _mm512_setzero_si512();
    __m512i increment = _mm512_set1_epi64(DFL_FIXED_AVX512_STRIDE);

    unsigned long copy_field_off = field_off;
    unsigned long copy_field_size = field_size;
    while(head) {
        unsigned char* obj = head->data;
        #if !DFL_READONLY
        GET_REAL_PTR(obj, ptr, field_off, field_size, uint64_t);
        #endif
// #if DFL_STRIDE < AVX_INCREMENT
//         // if DFL_STRIDE is lower than the index granularity, we should align the target properly.
//         // this masks the relevant bits for AVX_INCREMENT, but not the ones that will be reintroduced while adding `cache_off`
//         __m512i target    = _mm512_set1_epi64(((unsigned long) ptr & ~(AVX_INCREMENT-DFL_STRIDE)));
// #else
        __m512i target    = _mm512_set1_epi64(((unsigned long) ptr));
// #endif

        unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_FIXED_AVX512_STRIDE - 1));
        unsigned char* _end =  obj + field_off + field_size;

        // initialize the current avx ptrs for each iteration
        __m512i current = _mm512_set1_epi64((unsigned long) aligned_obj);
        current  = _mm512_add_epi64(current, index);

        for(volatile unsigned char* _ptr = aligned_obj; _ptr < _end; _ptr = _ptr + DFL_FIXED_AVX512_STRIDE) {
            // the mask will select which value will get actually loaded
            __mmask8 mask = _mm512_cmpeq_epi64_mask(target, current);
            current = _mm512_add_epi64(current, increment);
            res = _mm512_mask_load_epi64(res, mask, _ptr);
        }
        head = head->next;
        field_off = copy_field_off;
        field_size = copy_field_size;
    }
    // if the target was aligned, shift the result to get the right value
    // NOTICE: this assumes that accesses to `TYPE` are aligned to `sizeof(TYPE)`
// #if DFL_STRIDE < AVX_INCREMENT
//     return _mm512_reduce_add_epi64(res) >> (8*(((unsigned long) ptr) & (AVX_INCREMENT-DFL_STRIDE)));
// #else
    return _mm512_reduce_add_epi64(res);
// #endif
}

DFL_FUNC uint32_t uint32_t_avx512_linear_dfl_obj_load(dfl_obj_list_head head, unsigned char* ptr, unsigned long field_off, unsigned long field_size) { 
    if (((unsigned long) ptr != (unsigned int) ptr))
        return (uint32_t) uint64_t_avx512_linear_dfl_obj_load(head, ptr, field_off, field_size);
    
    __m512i index = _mm512_setr_epi32(0*AVX_INCREMENT_4, 1*AVX_INCREMENT_4, 2*AVX_INCREMENT_4, 3*AVX_INCREMENT_4,
                                    4*AVX_INCREMENT_4, 5*AVX_INCREMENT_4, 6*AVX_INCREMENT_4, 7*AVX_INCREMENT_4,
                                    8*AVX_INCREMENT_4, 9*AVX_INCREMENT_4, 10*AVX_INCREMENT_4, 11*AVX_INCREMENT_4,
                                    12*AVX_INCREMENT_4, 13*AVX_INCREMENT_4, 14*AVX_INCREMENT_4, 15*AVX_INCREMENT_4);
    __m512i res   = _mm512_setzero_si512();
    __m512i increment = _mm512_set1_epi32(DFL_FIXED_AVX512_STRIDE);

    unsigned long copy_field_off = field_off;
    unsigned long copy_field_size = field_size;
    while(head) {
        unsigned char* obj = head->data;
        // DEBUG("U32 obj linear load 0: ptr: %p - obj: %p - offset: %lu - size: %lu\n", ptr, obj, field_off, field_size);
        #if !DFL_READONLY
        GET_REAL_PTR(obj, ptr, field_off, field_size, uint32_t);
        #endif
        // DEBUG("U32 obj linear load 1: ptr: %p - obj: %p - offset: %lu - size: %lu\n", ptr, obj, field_off, field_size);
        __m512i target = _mm512_set1_epi32((unsigned long) ptr);

        unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_STRIDE - 1));
        unsigned char* _end = obj + field_off + field_size;

        __m512i current = _mm512_set1_epi32((unsigned long) aligned_obj);
        current  = _mm512_add_epi32(current, index);

        for(volatile unsigned char* _ptr = aligned_obj; _ptr < _end; _ptr = _ptr + DFL_FIXED_AVX512_STRIDE) {
            __mmask16 mask = _mm512_cmpeq_epi32_mask(target, current);
            // DEBUG("U32 obj linear load mask: %x\n", mask);
            // print_m512i_epi32("\tcurrent: ", current);
            // print_m512i_epi32("\ttarget: ", target);
            // print_m512i_epi32("\tincrement: ", increment);
            // print_m512i_epi32("\tres before: ", res);
            current = _mm512_add_epi32(current, increment);
            res = _mm512_mask_load_epi32(res, mask, _ptr);
            // print_m512i_epi32("\tres after: ", res);
        }
        head = head->next;
        field_off = copy_field_off;
        field_size = copy_field_size;
    }
    return _mm512_reduce_add_epi32(res);
}

DFL_FUNC void uint64_t_avx512_scatter_dfl_glob_store(unsigned char* obj, unsigned char* ptr, unsigned long field_off, unsigned long field_size, uint64_t value) { 
    // set the index vector to 0,1,2,3 "little" endian
    // so that 0 is in index[0:31], ecc
    __m512i index = _mm512_setr_epi64(0*DFL_STRIDE, 1*DFL_STRIDE, 2*DFL_STRIDE, 3*DFL_STRIDE,
                                       4*DFL_STRIDE, 5*DFL_STRIDE, 6*DFL_STRIDE, 7*DFL_STRIDE);

    GET_REAL_PTR(obj, ptr, field_off, field_size, uint64_t);
    __m512i target    = _mm512_set1_epi64((unsigned long) ptr);
    __m512i increment = _mm512_set1_epi64(8*DFL_STRIDE);

    unsigned long cache_off = ((unsigned long) ptr) & (DFL_STRIDE-1uL);
    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_STRIDE - 1));
    __m512i real_index = _mm512_set1_epi64((unsigned long)(ptr - aligned_obj) % (8*DFL_STRIDE));

    unsigned char* _end = obj + field_off + field_size;
    __m512i endv = _mm512_set1_epi64((unsigned long) _end);

    __m512i current = _mm512_set1_epi64((unsigned long) aligned_obj + cache_off);
    current  = _mm512_add_epi64(current, index);

    __m512i valuev = _mm512_set1_epi64(value);

    for(volatile unsigned char* _ptr = aligned_obj; _ptr < _end; _ptr = _ptr + 8*DFL_STRIDE) {
        __mmask8 mask = _mm512_cmpeq_epi64_mask(target, current);
        __mmask8 oob_mask = _mm512_cmplt_epi64_mask(current, endv);
        current = _mm512_add_epi64(current, increment);
        __m512i store_index = _mm512_mask_blend_epi64(mask, index, real_index);
        _mm512_mask_i64scatter_epi64((long long *)_ptr, oob_mask, store_index, valuev, 1);
    }
}

DFL_FUNC void uint32_t_avx512_scatter_dfl_glob_store(unsigned char* obj, unsigned char* ptr, unsigned long field_off, unsigned long field_size, uint64_t value) { 
    // if the ptr is 64 bit wide we must use the slow version since it does not fit 8 times in the vectors
    if (((unsigned long) ptr != (unsigned int) ptr) || ((unsigned long) obj != (unsigned int) obj))
        return uint64_t_avx512_scatter_dfl_glob_store(obj, ptr, field_off, field_size, value);
    
    // set the index vector to 0,1,2,3 "little" endian
    // so that 0 is in index[0:31], ecc
    __m512i index = _mm512_setr_epi32(0*DFL_STRIDE, 1*DFL_STRIDE, 2*DFL_STRIDE, 3*DFL_STRIDE,
                                       4*DFL_STRIDE, 5*DFL_STRIDE, 6*DFL_STRIDE, 7*DFL_STRIDE,
                                       8*DFL_STRIDE, 9*DFL_STRIDE, 10*DFL_STRIDE, 11*DFL_STRIDE,
                                       12*DFL_STRIDE, 13*DFL_STRIDE, 14*DFL_STRIDE, 15*DFL_STRIDE);
    // DEBUG("U32 scatter store 0: ptr: %p - obj: %p - offset: %lu - size: %lu - value: %lu\n", ptr, obj, field_off, field_size, value);
    GET_REAL_PTR(obj, ptr, field_off, field_size, uint32_t);
    // DEBUG("U32 scatter store 1: ptr: %p - obj: %p - offset: %lu - size: %lu - value: %lu\n", ptr, obj, field_off, field_size, value);
    __m512i target    = _mm512_set1_epi32((unsigned long) ptr);
    __m512i increment = _mm512_set1_epi32(16*DFL_STRIDE);

    unsigned long cache_off = ((unsigned long) ptr) & (DFL_STRIDE-1uL);
    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_STRIDE - 1));
    __m512i real_index = _mm512_set1_epi32((unsigned long)(ptr - aligned_obj) % (16*DFL_STRIDE));
    
    unsigned char* _end = obj + field_off + field_size;
    DEBUG("U32 scatter store 2: ptr: %p - aligned_obj: %p - _end: %p - offset: %lu - size: %lu\n", ptr, aligned_obj, _end, field_off, field_size);
    // print_m512i_epi32("U32 scatter index", index);
    // print_m512i_epi32("U32 scatter real index", real_index);
    __m512i endv = _mm512_set1_epi32((unsigned long) _end);
    // print_m512i_epi32("U32 scatter endv", endv);

    __m512i current = _mm512_set1_epi32((unsigned long) aligned_obj + cache_off);
    current  = _mm512_add_epi32(current, index);

    __m512i valuev = _mm512_set1_epi32(value);

    for(volatile unsigned char* _ptr = aligned_obj; _ptr < _end; _ptr = _ptr + 16*DFL_STRIDE) {
        __mmask16 mask = _mm512_cmpeq_epi32_mask(target, current);
        __mmask16 oob_mask = _mm512_cmplt_epi32_mask(current, endv);
        current = _mm512_add_epi32(current, increment);
        __m512i store_index = _mm512_mask_blend_epi32(mask, index, real_index);
        DEBUG("U32 scatter: mask: %x - oob_mask: %x\n", mask, oob_mask);
        // print_m512i_epi32("U32 store_index", store_index);
        _mm512_mask_i32scatter_epi32((long long *)_ptr, oob_mask, store_index, valuev, 1);
    }
}

#define DFL_AVX512_SCATTER_GLOB_STORE(type) DFL_FUNC void type ## _avx512_scatter_dfl_glob_store(unsigned char* obj, unsigned char* ptr, unsigned long field_off, unsigned long field_size, type value) { \
    if (((unsigned long) ptr != (unsigned int) ptr) || ((unsigned long) obj != (unsigned int) obj))\
        return uint64_t_avx512_scatter_dfl_glob_store(obj, ptr, field_off, field_size, value);\
    __m512i index = _mm512_setr_epi32(0*DFL_STRIDE, 1*DFL_STRIDE, 2*DFL_STRIDE, 3*DFL_STRIDE,\
                                       4*DFL_STRIDE, 5*DFL_STRIDE, 6*DFL_STRIDE, 7*DFL_STRIDE,\
                                       8*DFL_STRIDE, 9*DFL_STRIDE, 10*DFL_STRIDE, 11*DFL_STRIDE,\
                                       12*DFL_STRIDE, 13*DFL_STRIDE, 14*DFL_STRIDE, 15*DFL_STRIDE);\
    ptr = get_real_ptr_uint32_t(obj, ptr, &field_off, &field_size, NULL);\
    __m512i target    = _mm512_set1_epi32((unsigned long) ptr);\
    __m512i increment = _mm512_set1_epi32(16*DFL_STRIDE);\
    uint64_t write_mask_ = (1uL << sizeof(value)) - 1uL; \
    write_mask_ |= ((write_mask_ << 4) | (write_mask_ << 8) | (write_mask_ << 12) | (write_mask_ << 16) | (write_mask_ << 20) | (write_mask_ << 24) | (write_mask_ << 28) | (write_mask_ << 32) | (write_mask_ << 36) | (write_mask_ << 40) | (write_mask_ << 44) | (write_mask_ << 48) | (write_mask_ << 52) | (write_mask_ << 56)| (write_mask_ << 60)); \
    __mmask64 write_mask   = _cvtu64_mask64(write_mask_); \
    __m512i valuev = _mm512_set1_epi32(value); \
    unsigned long cache_off = ((unsigned long) ptr) & (DFL_STRIDE-1uL);\
    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_STRIDE - 1));\
    field_size += (((unsigned long)obj + field_off) - (unsigned long)aligned_obj);\
    field_size = ((field_size + 16*DFL_STRIDE - 1) / (16*DFL_STRIDE)) * 16*DFL_STRIDE;\
    unsigned char* _end =  aligned_obj + field_size;\
    __m512i current = _mm512_set1_epi32((unsigned long) aligned_obj + cache_off);\
    current  = _mm512_add_epi32(current, index);\
    for(volatile unsigned char* _ptr = aligned_obj + cache_off; _ptr < _end; _ptr = _ptr + 16*DFL_STRIDE) {\
        __mmask16 idx_mask = _mm512_cmpeq_epi32_mask(target, current); \
        __m512i   loaded   = _mm512_i32gather_epi32(index, _ptr, 1); \
        __m512i writev     = _mm512_mask_blend_epi32(idx_mask, loaded, valuev); \
        writev             = _mm512_mask_blend_epi8(write_mask, loaded, writev); \
        current = _mm512_add_epi32(current, increment);\
        _mm512_i32scatter_epi32((long long *)_ptr, index, writev, 1);\
    }\
}

DFL_FUNC void uint64_t_avx512_scatter_dfl_obj_store(dfl_obj_list_head head, unsigned char* ptr, unsigned long field_off, unsigned long field_size, uint64_t value) { 
    // set the index vector to 0,1,2,3 "little" endian
    // so that 0 is in index[0:31], ecc
    __m512i index = _mm512_setr_epi64(0*DFL_STRIDE, 1*DFL_STRIDE, 2*DFL_STRIDE, 3*DFL_STRIDE,
                                      4*DFL_STRIDE, 5*DFL_STRIDE, 6*DFL_STRIDE, 7*DFL_STRIDE);
    __m512i increment = _mm512_set1_epi64(8*DFL_STRIDE);
    __m512i valuev = _mm512_set1_epi64(value);

    unsigned long copy_field_off = field_off;
    unsigned long copy_field_size = field_size;
    while(head) {
        unsigned char* obj = head->data;
        GET_REAL_PTR(obj, ptr, field_off, field_size, uint64_t);
        __m512i target    = _mm512_set1_epi64((unsigned long) ptr);
        unsigned long cache_off = ((unsigned long) ptr) & (DFL_STRIDE-1uL);
        unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_STRIDE - 1));
        __m512i real_index = _mm512_set1_epi64((unsigned long)(ptr - aligned_obj) % (8*DFL_STRIDE));

        unsigned char* _end =  obj + field_off + field_size;
        __m512i endv    = _mm512_set1_epi64((unsigned long) _end);
        __m512i current = _mm512_set1_epi64((unsigned long) aligned_obj + cache_off);
        current  = _mm512_add_epi64(current, index);

        for(volatile unsigned char* _ptr = aligned_obj; _ptr < _end; _ptr = _ptr + 8*DFL_STRIDE) {
            __mmask8 mask = _mm512_cmpeq_epi64_mask(target, current);
            __mmask8 oob_mask = _mm512_cmplt_epi64_mask(current, endv);
            current = _mm512_add_epi64(current, increment);
            __m512i store_index = _mm512_mask_blend_epi64(mask, index, real_index);
            _mm512_mask_i64scatter_epi64((long long *)_ptr, oob_mask, store_index, valuev, 1);
        }
        head = head->next;
        field_off = copy_field_off;
        field_size = copy_field_size;
    }
}

DFL_FUNC void uint32_t_avx512_scatter_dfl_obj_store(dfl_obj_list_head head, unsigned char* ptr, unsigned long field_off, unsigned long field_size, uint32_t value) {
    // if the ptr is 64 bit wide we must use the slow version since it does not fit 8 times in the vectors
    if (((unsigned long) ptr != (unsigned int) ptr) || ((unsigned long) head->data != (unsigned int) head->data))
        return uint64_t_avx512_scatter_dfl_obj_store(head, ptr, field_off, field_size, value);
    
    // set the index vector to 0,1,2,3 "little" endian
    // so that 0 is in index[0:31], ecc
    __m512i index = _mm512_setr_epi32(0*DFL_STRIDE, 1*DFL_STRIDE, 2*DFL_STRIDE, 3*DFL_STRIDE,
                                       4*DFL_STRIDE, 5*DFL_STRIDE, 6*DFL_STRIDE, 7*DFL_STRIDE,
                                       8*DFL_STRIDE, 9*DFL_STRIDE, 10*DFL_STRIDE, 11*DFL_STRIDE,
                                       12*DFL_STRIDE, 13*DFL_STRIDE, 14*DFL_STRIDE, 15*DFL_STRIDE);
    __m512i increment = _mm512_set1_epi32(16*DFL_STRIDE);
    __m512i valuev = _mm512_set1_epi32(value);

    unsigned long copy_field_off = field_off;
    unsigned long copy_field_size = field_size;
    while(head) {
        unsigned char* obj = head->data;
        GET_REAL_PTR(obj, ptr, field_off, field_size, uint32_t);
        __m512i target = _mm512_set1_epi32((unsigned long) ptr);
        unsigned long cache_off = ((unsigned long) ptr) & (DFL_STRIDE-1uL);
        unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_STRIDE - 1));
        __m512i real_index = _mm512_set1_epi32((unsigned long)(ptr - aligned_obj) % (16*DFL_STRIDE));

        field_size += (((unsigned long)obj + field_off) - (unsigned long)aligned_obj);
        // Align the real end to DFL_STRIDE
        __m512i endv = _mm512_set1_epi32((unsigned long) aligned_obj + (((field_size + DFL_STRIDE - 1) / (DFL_STRIDE)) * DFL_STRIDE));

        field_size = ((field_size + 16*DFL_STRIDE - 1) / (16*DFL_STRIDE)) * 16*DFL_STRIDE;
        unsigned char* _end =  aligned_obj+ field_size;
        __m512i current = _mm512_set1_epi32((unsigned long) aligned_obj + cache_off);
        current  = _mm512_add_epi32(current, index);

        for(volatile unsigned char* _ptr = aligned_obj; _ptr < _end; _ptr = _ptr + 16*DFL_STRIDE) {
            __mmask16 mask = _mm512_cmpeq_epi32_mask(target, current);
            __mmask16 oob_mask = _mm512_cmplt_epi32_mask(current, endv);
            current = _mm512_add_epi32(current, increment);
            __m512i store_index = _mm512_mask_blend_epi32(mask, index, real_index);
            _mm512_mask_i32scatter_epi32((long long *)_ptr, oob_mask, store_index, valuev, 1);
        }
        head = head->next;
        field_off = copy_field_off;
        field_size = copy_field_size;
    }
}

#define DFL_AVX512_SCATTER_OBJ_STORE(type) DFL_FUNC void type ## _avx512_scatter_dfl_obj_store(dfl_obj_list_head head, unsigned char* ptr, unsigned long field_off, unsigned long field_size, type value) { \
    if (((unsigned long) ptr != (unsigned int) ptr))\
        return uint64_t_avx512_scatter_dfl_obj_store(head, ptr, field_off, field_size, value);\
    __m512i index = _mm512_setr_epi32(0*DFL_STRIDE, 1*DFL_STRIDE, 2*DFL_STRIDE, 3*DFL_STRIDE,\
                                       4*DFL_STRIDE, 5*DFL_STRIDE, 6*DFL_STRIDE, 7*DFL_STRIDE,\
                                       8*DFL_STRIDE, 9*DFL_STRIDE, 10*DFL_STRIDE, 11*DFL_STRIDE,\
                                       12*DFL_STRIDE, 13*DFL_STRIDE, 14*DFL_STRIDE, 15*DFL_STRIDE);\
    __m512i increment = _mm512_set1_epi32(16*DFL_STRIDE);\
    uint64_t write_mask_ = (1uL << sizeof(value)) - 1uL; \
    write_mask_ |= ((write_mask_ << 4) | (write_mask_ << 8) | (write_mask_ << 12) | (write_mask_ << 16) | (write_mask_ << 20) | (write_mask_ << 24) | (write_mask_ << 28) | (write_mask_ << 32) | (write_mask_ << 36) | (write_mask_ << 40) | (write_mask_ << 44) | (write_mask_ << 48) | (write_mask_ << 52) | (write_mask_ << 56)| (write_mask_ << 60)); \
    __mmask64 write_mask   = _cvtu64_mask64(write_mask_); \
    __m512i valuev = _mm512_set1_epi32(value); \
    unsigned long cache_off = ((unsigned long) ptr) & (DFL_STRIDE-1uL);\
    unsigned long copy_field_off = field_off;\
    unsigned long copy_field_size = field_size;\
    while(head) {\
        unsigned char* obj = head->data;\
        ptr = get_real_ptr_uint32_t(obj, ptr, &field_off, &field_size, NULL);\
        __m512i target = _mm512_set1_epi32((unsigned long) ptr);\
        unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_STRIDE - 1));\
        field_size += (((unsigned long)obj + field_off) - (unsigned long)aligned_obj);\
        field_size = ((field_size + 16*DFL_STRIDE - 1) / (16*DFL_STRIDE)) * 16*DFL_STRIDE;\
        unsigned char* _end =  aligned_obj + field_size;\
        __m512i current = _mm512_set1_epi32((unsigned long) aligned_obj + cache_off);\
        current  = _mm512_add_epi32(current, index);\
        for(volatile unsigned char* _ptr = aligned_obj + cache_off; _ptr < _end; _ptr = _ptr + 16*DFL_STRIDE) {\
            __mmask16 idx_mask = _mm512_cmpeq_epi32_mask(target, current); \
            __m512i   loaded   = _mm512_i32gather_epi32(index, _ptr, 1); \
            __m512i writev     = _mm512_mask_blend_epi32(idx_mask, loaded, valuev); \
            writev             = _mm512_mask_blend_epi8(write_mask, loaded, writev); \
            current = _mm512_add_epi32(current, increment);\
            _mm512_i32scatter_epi32((long long *)_ptr, index, writev, 1);\
        }\
        head = head->next;\
        field_off = copy_field_off;\
        field_size = copy_field_size;\
    }\
}

DFL_FUNC void uint64_t_avx512_linear_dfl_glob_store(unsigned char* obj, unsigned char* ptr, unsigned long field_off, unsigned long field_size, uint64_t value) { 
    // set the index vector to 0,8,16,24 "little" endian
    // so that 0 is in index[0:31], ecc
    __m512i index = _mm512_setr_epi64(AVX_INCREMENT*0, AVX_INCREMENT*1, AVX_INCREMENT*2, AVX_INCREMENT*3, 
        AVX_INCREMENT*4, AVX_INCREMENT*5, AVX_INCREMENT*6, AVX_INCREMENT*7);
    GET_REAL_PTR(obj, ptr, field_off, field_size, uint64_t);
    __m512i target    = _mm512_set1_epi64((unsigned long) ptr);
    __m512i increment = _mm512_set1_epi64(DFL_FIXED_AVX512_STRIDE);
    __m512i writev = _mm512_set1_epi64(value);

    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_FIXED_AVX512_STRIDE - 1));
    unsigned char* _end =  obj + field_off + field_size;

    // initialize the current avx ptrs for each iteration
    __m512i current = _mm512_set1_epi64((unsigned long) aligned_obj);
    current  = _mm512_add_epi64(current, index);

    for(volatile unsigned char* _ptr = aligned_obj; _ptr < _end; _ptr = _ptr + DFL_FIXED_AVX512_STRIDE) {
        __mmask8 mask = _mm512_cmpeq_epi64_mask(target, current) | 1;
        // __mmask8 is_zero = (mask == 0);
        // mask |= is_zero;
        current = _mm512_add_epi64(current, increment);
        _mm512_mask_store_epi64((long long *)_ptr, mask, writev);
    }
}

DFL_FUNC void uint32_t_avx512_linear_dfl_glob_store(unsigned char* obj, unsigned char* ptr, unsigned long field_off, unsigned long field_size, uint32_t value) { 
    if (((unsigned long) ptr != (unsigned int) ptr) || ((unsigned long) obj != (unsigned int) obj))
        return uint64_t_avx512_linear_dfl_glob_store(obj, ptr, field_off, field_size, value);
    
    // set the index vector to 0,8,16,24 "little" endian
    // so that 0 is in index[0:31], ecc
    __m512i index = _mm512_setr_epi32(AVX_INCREMENT_4*0, AVX_INCREMENT_4*1, 
        AVX_INCREMENT_4*2, AVX_INCREMENT_4*3, AVX_INCREMENT_4*4, AVX_INCREMENT_4*5, 
        AVX_INCREMENT_4*6, AVX_INCREMENT_4*7, AVX_INCREMENT_4*8, AVX_INCREMENT_4*9, 
        AVX_INCREMENT_4*10, AVX_INCREMENT_4*11,AVX_INCREMENT_4*12, AVX_INCREMENT_4*13, 
        AVX_INCREMENT_4*14, AVX_INCREMENT_4*15);
    // fprintf(stderr, "U32 linear store 0: ptr: %p - obj: %p - offset: %lu - size: %lu - value: %lu\n", ptr, obj, field_off, field_size, value);
    GET_REAL_PTR(obj, ptr, field_off, field_size, uint32_t);
    // fprintf(stderr, "U32 linear store 1: ptr: %p - obj: %p - offset: %lu - size: %lu - value: %lu - pos: %lu\n", ptr, obj, field_off, field_size, value, ((unsigned long) ptr) % CACHE_LINE_ALIGNMENT);
    __m512i target    = _mm512_set1_epi32((unsigned long) ptr);
    __m512i increment = _mm512_set1_epi32(DFL_FIXED_AVX512_STRIDE);
    __m512i writev = _mm512_set1_epi32(value);

    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_FIXED_AVX512_STRIDE - 1));
    unsigned char* _end =  obj + field_off + field_size;

    // initialize the current avx ptrs for each iteration
    __m512i current = _mm512_set1_epi32((unsigned long) aligned_obj);
    current  = _mm512_add_epi32(current, index);

    for(volatile unsigned char* _ptr = aligned_obj; _ptr < _end; _ptr = _ptr + DFL_FIXED_AVX512_STRIDE) {
        __mmask16 mask = _mm512_cmpeq_epi32_mask(target, current) | 1;
        // __mmask16 is_zero = (mask == 0);
        // mask |= is_zero;
        // fprintf(stderr, "U32 linear store %lu: mask: %x\n", (unsigned long)_ptr, mask);
        current = _mm512_add_epi32(current, increment);
        // PRINT_M512I_EPI32("\tcurrent: ", current);
        // PRINT_M512I_EPI32("\tincrement: ", increment);
        _mm512_mask_store_epi32((long long *)_ptr, mask, writev);
        // fprintf(stderr, "U32 linear store %lu\n", (unsigned long)_ptr);
    }
}

#define DFL_AVX512_LINEAR_GLOB_STORE(type) DFL_FUNC void type ## _avx512_linear_dfl_glob_store(unsigned char* obj, unsigned char* ptr, unsigned long field_off, unsigned long field_size, type value) { \
    __m512i index = _mm512_setr_epi64(AVX_INCREMENT*0, AVX_INCREMENT*1, AVX_INCREMENT*2, AVX_INCREMENT*3, AVX_INCREMENT*4, AVX_INCREMENT*5, AVX_INCREMENT*6, AVX_INCREMENT*7); \
    ptr = get_real_ptr_uint64_t(obj, ptr, &field_off, &field_size, NULL); \
    __m512i target    = _mm512_set1_epi64((unsigned long) ptr & ~DFL_CORRECTION); \
    __m512i increment = _mm512_set1_epi64(DFL_FIXED_AVX512_STRIDE); \
    uint64_t write_mask_ = ((1uL << sizeof(value)) - 1uL) << ((((unsigned long) ptr) & (DFL_CORRECTION))); \
    write_mask_ |= ((write_mask_ << 8) | (write_mask_ << 16) | (write_mask_ << 24) | (write_mask_ << 32) | (write_mask_ << 40) | (write_mask_ << 48) | (write_mask_ << 56)); \
    __mmask64 write_mask   = _cvtu64_mask64(write_mask_); \
    uint64_t shifted_value = ((uint64_t)value) << (8*(((unsigned long) ptr) & (DFL_CORRECTION))); \
    __m512i valuev = _mm512_set1_epi64(shifted_value); \
    unsigned long cache_off = ((unsigned long) ptr) & (DFL_STRIDE-1uL); \
    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_STRIDE - 1)); \
    field_size += (((unsigned long)obj + field_off) - (unsigned long)aligned_obj); \
    field_size = ((field_size + DFL_FIXED_AVX512_STRIDE - 1) / (DFL_FIXED_AVX512_STRIDE)) * DFL_FIXED_AVX512_STRIDE; \
    unsigned char* _end =  aligned_obj + field_size; \
    __m512i current = _mm512_set1_epi64((unsigned long) aligned_obj + cache_off); \
    current  = _mm512_add_epi64(current, index); \
    for(volatile unsigned char* _ptr = aligned_obj + cache_off; _ptr < _end; _ptr = _ptr + DFL_FIXED_AVX512_STRIDE) { \
        __mmask8 idx_mask = _mm512_cmpeq_epi64_mask(target, current); \
        __m512i  loaded   = _mm512_loadu_si512((long long *)_ptr); \
        __m512i writev    = _mm512_mask_blend_epi64(idx_mask, loaded, valuev); \
        writev            = _mm512_mask_blend_epi8(write_mask, loaded, writev); \
        current = _mm512_add_epi64(current, increment); \
        _mm512_storeu_si512((long long *)_ptr, writev); \
    } \
}

DFL_FUNC void uint64_t_avx512_linear_dfl_obj_store(dfl_obj_list_head head, unsigned char* ptr, unsigned long field_off, unsigned long field_size, uint64_t value) {
    __m512i index = _mm512_setr_epi64(AVX_INCREMENT*0, AVX_INCREMENT*1, AVX_INCREMENT*2, AVX_INCREMENT*3, 
        AVX_INCREMENT*4, AVX_INCREMENT*5, AVX_INCREMENT*6, AVX_INCREMENT*7);

    __m512i increment = _mm512_set1_epi64(DFL_FIXED_AVX512_STRIDE);
    __m512i writev = _mm512_set1_epi64(value);

    unsigned long copy_field_off = field_off;
    unsigned long copy_field_size = field_size;
    while(head) {
        unsigned char* obj = head->data;
        GET_REAL_PTR(obj, ptr, field_off, field_size, uint64_t);
        __m512i target = _mm512_set1_epi64((unsigned long) ptr);
        
        unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_STRIDE - 1));
        // field_size += (((unsigned long)obj + field_off) - (unsigned long)aligned_obj);
        // field_size = ((field_size + DFL_FIXED_AVX512_STRIDE - 1) / (DFL_FIXED_AVX512_STRIDE)) * DFL_FIXED_AVX512_STRIDE;
        unsigned char* _end = obj + field_off + field_size;

        // initialize the current avx ptrs for each iteration
        __m512i current = _mm512_set1_epi64((unsigned long) aligned_obj);
        current  = _mm512_add_epi64(current, index);
        for(volatile unsigned char* _ptr = aligned_obj; _ptr < _end; _ptr = _ptr + DFL_FIXED_AVX512_STRIDE) {
            __mmask8 mask = _mm512_cmpeq_epi64_mask(target, current) | 1;
            // __mmask8 is_zero = (mask == 0);
            // mask |= is_zero;
            current = _mm512_add_epi64(current, increment);
            _mm512_mask_store_epi64((long long *)_ptr, mask, writev);
        }
        head = head->next;
        field_off = copy_field_off;
        field_size = copy_field_size;
    }
}

DFL_FUNC void uint32_t_avx512_linear_dfl_obj_store(dfl_obj_list_head head, unsigned char* ptr, unsigned long field_off, unsigned long field_size, uint32_t value) {
    __m512i index = _mm512_setr_epi32( AVX_INCREMENT_4*0, AVX_INCREMENT_4*1, 
        AVX_INCREMENT_4*2, AVX_INCREMENT_4*3, AVX_INCREMENT_4*4, AVX_INCREMENT_4*5, 
        AVX_INCREMENT_4*6, AVX_INCREMENT_4*7, AVX_INCREMENT_4*8, AVX_INCREMENT_4*9, 
        AVX_INCREMENT_4*10, AVX_INCREMENT_4*11,AVX_INCREMENT_4*12, AVX_INCREMENT_4*13, 
        AVX_INCREMENT_4*14, AVX_INCREMENT_4*15);

    __m512i increment = _mm512_set1_epi32(DFL_FIXED_AVX512_STRIDE);
    __m512i writev = _mm512_set1_epi32(value);

    unsigned long copy_field_off = field_off;
    unsigned long copy_field_size = field_size;
    while(head) {
        unsigned char* obj = head->data;
        GET_REAL_PTR(obj, ptr, field_off, field_size, uint32_t);
        __m512i target = _mm512_set1_epi32((unsigned long) ptr);

        unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_STRIDE - 1));
        // field_size += (((unsigned long)obj + field_off) - (unsigned long)aligned_obj);
        // field_size = ((field_size + DFL_FIXED_AVX512_STRIDE - 1) / (DFL_FIXED_AVX512_STRIDE)) * DFL_FIXED_AVX512_STRIDE;
        unsigned char* _end = obj + field_off + field_size;

        // initialize the current avx ptrs for each iteration
        __m512i current = _mm512_set1_epi32((unsigned long) aligned_obj);
        current  = _mm512_add_epi32(current, index);
        for(volatile unsigned char* _ptr = aligned_obj; _ptr < _end; _ptr
            = _ptr + DFL_FIXED_AVX512_STRIDE) {
            __mmask16 mask = _mm512_cmpeq_epi32_mask(target, current) | 1;
            // __mmask16 is_zero = (mask == 0);
            // mask |= is_zero;
            current = _mm512_add_epi32(current, increment);
            _mm512_mask_store_epi32((long long *)_ptr, mask, writev);
        }
        head = head->next;
        field_off = copy_field_off;
        field_size = copy_field_size;
    }
}

#define DFL_AVX512_LINEAR_OBJ_STORE(type) DFL_FUNC void type ## _avx512_linear_dfl_obj_store(dfl_obj_list_head head, unsigned char* ptr, unsigned long field_off, unsigned long field_size, type value) { \
    __m512i index = _mm512_setr_epi64(AVX_INCREMENT*0, AVX_INCREMENT*1, AVX_INCREMENT*2, AVX_INCREMENT*3, AVX_INCREMENT*4, AVX_INCREMENT*5, AVX_INCREMENT*6, AVX_INCREMENT*7); \
    __m512i increment = _mm512_set1_epi64(DFL_FIXED_AVX512_STRIDE); \
    uint64_t write_mask_ = ((1uL << sizeof(value)) - 1uL) << ((((unsigned long) ptr) & (DFL_CORRECTION))); \
    write_mask_ |= ((write_mask_ << 8) | (write_mask_ << 16) | (write_mask_ << 24) | (write_mask_ << 32) | (write_mask_ << 40) | (write_mask_ << 48) | (write_mask_ << 56)); \
    __mmask64 write_mask   = _cvtu64_mask64(write_mask_); \
    uint64_t shifted_value = ((uint64_t)value) << (8*(((unsigned long) ptr) & (DFL_CORRECTION))); \
    __m512i valuev = _mm512_set1_epi64(shifted_value); \
    unsigned long copy_field_off = field_off;\
    unsigned long copy_field_size = field_size;\
    while(head) {\
        unsigned char* obj = head->data;\
        ptr = get_real_ptr_uint64_t(obj, ptr, &field_off, &field_size, NULL);\
        __m512i target = _mm512_set1_epi64((unsigned long) ptr & ~DFL_CORRECTION); \
        unsigned long cache_off = ((unsigned long) ptr) & (DFL_STRIDE-1uL); \
        unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(DFL_STRIDE - 1));\
        field_size += (((unsigned long)obj + field_off) - (unsigned long)aligned_obj);\
        field_size = ((field_size + DFL_FIXED_AVX512_STRIDE - 1) / (DFL_FIXED_AVX512_STRIDE)) * DFL_FIXED_AVX512_STRIDE;\
        unsigned char* _end =  aligned_obj + field_size;\
        __m512i current = _mm512_set1_epi64((unsigned long) aligned_obj + cache_off);\
        current  = _mm512_add_epi64(current, index);\
        for(volatile unsigned char* _ptr = aligned_obj + cache_off; _ptr < _end; _ptr = _ptr + DFL_FIXED_AVX512_STRIDE) { \
            __mmask8 idx_mask = _mm512_cmpeq_epi64_mask(target, current); \
            __m512i  loaded   = _mm512_loadu_si512((long long *)_ptr); \
            __m512i writev    = _mm512_mask_blend_epi64(idx_mask, loaded, valuev); \
            writev            = _mm512_mask_blend_epi8(write_mask, loaded, writev); \
            current = _mm512_add_epi64(current, increment); \
            _mm512_storeu_si512((long long *)_ptr, writev); \
        }\
        head = head->next;\
        field_off = copy_field_off;\
        field_size = copy_field_size;\
    }\
}

#define DFL_AVX512_GATHER_GLOB_LOAD(type) DFL_FUNC type type ## _avx512_gather_dfl_glob_load(unsigned char* obj, unsigned char* ptr, unsigned long field_off, unsigned long field_size) {  \
    return (type) uint32_t_avx512_gather_dfl_glob_load(obj, ptr, field_off, field_size); \
}

#define DFL_AVX512_GATHER_OBJ_LOAD(type) DFL_FUNC type type ## _avx512_gather_dfl_obj_load(dfl_obj_list_head head, unsigned char* ptr, unsigned long field_off, unsigned long field_size) {  \
    return (type) uint32_t_avx512_gather_dfl_obj_load(head, ptr, field_off, field_size); \
}

#define DFL_AVX512_LINEAR_GLOB_LOAD(type) DFL_FUNC type type ## _avx512_linear_dfl_glob_load(unsigned char* obj, unsigned char* ptr, unsigned long field_off, unsigned long field_size) {  \
    return (type) uint64_t_avx512_linear_dfl_glob_load(obj, ptr, field_off, field_size); \
}

#define DFL_AVX512_LINEAR_OBJ_LOAD(type) DFL_FUNC type type ## _avx512_linear_dfl_obj_load(dfl_obj_list_head head, unsigned char* ptr, unsigned long field_off, unsigned long field_size) {  \
    return (type) uint64_t_avx512_linear_dfl_obj_load(head, ptr, field_off, field_size); \
}

DFL_AVX512_GATHER_GLOB_LOAD(uint16_t)
DFL_AVX512_GATHER_GLOB_LOAD(uint8_t)

DFL_AVX512_GATHER_OBJ_LOAD(uint16_t)
DFL_AVX512_GATHER_OBJ_LOAD(uint8_t)

DFL_AVX512_LINEAR_GLOB_LOAD(uint16_t)
DFL_AVX512_LINEAR_GLOB_LOAD(uint8_t)

DFL_AVX512_LINEAR_OBJ_LOAD(uint16_t)
DFL_AVX512_LINEAR_OBJ_LOAD(uint8_t)

DFL_AVX512_SCATTER_GLOB_STORE(uint16_t)
DFL_AVX512_SCATTER_GLOB_STORE(uint8_t)

DFL_AVX512_SCATTER_OBJ_STORE(uint16_t)
DFL_AVX512_SCATTER_OBJ_STORE(uint8_t)

DFL_AVX512_LINEAR_GLOB_STORE(uint16_t)
DFL_AVX512_LINEAR_GLOB_STORE(uint8_t)

DFL_AVX512_LINEAR_OBJ_STORE(uint16_t)
DFL_AVX512_LINEAR_OBJ_STORE(uint8_t)

#define DFL_AVX512_VECTOR_WIDTH_64 8
#define DFL_AVX512_VECTOR_WIDTH_32 16

inline __m512i get_combine_idx_i64(unsigned int length) {
    switch (length) {
    case 2:
        return _mm512_setr_epi64(0, 1, 8+0, 8+1, 8+2, 8+3, 8+4, 8+5);
    case 3:
        return _mm512_setr_epi64(0, 2, 3, 8+0, 8+1, 8+2, 8+3, 8+4);
    case 4:
        return _mm512_setr_epi64(0, 1, 2, 3, 8+0, 8+1, 8+2, 8+3);
    case 5:
        return _mm512_setr_epi64(0, 1, 2, 3, 4, 8+0, 8+1, 8+2);
    case 6:
        return _mm512_setr_epi64(0, 1, 2, 3, 4, 5, 8+0, 8+1);
    case 7:
        return _mm512_setr_epi64(0, 1, 2, 3, 4, 5, 6, 8+0);
    case 8: 
        return _mm512_setr_epi64(0, 1, 2, 3, 4, 5, 6, 7);
    default:
        assert(length > 1 && length < DFL_AVX512_VECTOR_WIDTH_64 && "Invalid length for AVX512 vector indices");
        return _mm512_setr_epi64(0, 1, 2, 3, 4, 5, 6, 7);
    }
}

inline __m512i get_combine_idx_i32(unsigned int length) {
    switch (length) {
    case 2:
        return _mm512_setr_epi32(0, 1, 16+0, 16+1, 16+2, 16+3, 16+4, 16+5,
                                  16+6, 16+7, 16+8, 16+9, 16+10, 16+11, 16+12, 16+13);
    case 3:
        return _mm512_setr_epi32(0, 1, 2, 16+0, 16+1, 16+2, 16+3, 16+4,
                                  16+5, 16+6, 16+7, 16+8, 16+9, 16+10, 16+11, 16+12);
    case 4:
        return _mm512_setr_epi32(0, 1, 2, 3, 16+0, 16+1, 16+2, 16+3,
                                  16+4, 16+5, 16+6, 16+7, 16+8, 16+9, 16+10, 16+11);
    case 5:
        return _mm512_setr_epi32(0, 1, 2, 3, 4, 16+0, 16+1, 16+2,
                                  16+3, 16+4, 16+5, 16+6, 16+7, 16+8, 16+9, 16+10);
    case 6:
        return _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 16+0, 16+1,
                                  16+2, 16+3, 16+4, 16+5, 16+6, 16+7, 16+8, 16+9);
    case 7:
        return _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 16+0,
                                  16+1, 16+2, 16+3, 16+4, 16+5, 16+6, 16+7, 16+8);
    case 8: 
        return _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7,
                                  16+0, 16+1, 16+2, 16+3, 16+4, 16+5, 16+6, 16+7);
    case 9:
        return _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7,
                                  8, 16+0, 16+1, 16+2, 16+3, 16+4, 16+5, 16+6);
    case 10:
        return _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7,
                                  8, 9, 16+0, 16+1, 16+2, 16+3, 16+4, 16+5);
    case 11:
        return _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7,
                                  8, 9, 10 ,16+0 ,16+1, 16+2, 16+3, 16+4);
    case 12:
        return _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7,
                                  8, 9, 10 ,11 ,16+0 ,16+1, 16+2, 16+3);
    case 13:
        return _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7,
                                  8, 9, 10 ,11 ,12 ,16+0 ,16+1, 16+2);
    case 14:
        return _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7,
                                  8, 9, 10 ,11 ,12 ,13 ,16+0 ,16+1);
    case 15:
        return _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7,
                                  8, 9, 10 ,11 ,12 ,13 ,14 ,16+0);
    case 16:
        return _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7,
                                  8, 9, 10 ,11 ,12 ,13 ,14 ,15);
    default:
        assert(length > 1 && length < DFL_AVX512_VECTOR_WIDTH_32 && "Invalid length for AVX512 vector indices");
        return _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7,
                                  8, 9, 10 ,11 ,12 ,13 ,14 ,15);
    }
}

inline __m512i get_real_ptr_vec_uint64_t(unsigned char* obj, __m512i ptr_vec, unsigned long* field_off, unsigned long* field_size, unsigned int length) {
    assert(((uintptr_t)obj % CACHE_LINE_ALIGNMENT) == 0);
    assert(length <= DFL_AVX512_VECTOR_WIDTH_64); 

    __mmask8 mask = (1 << length) - 1;
    __m512i obj_vec = _mm512_set1_epi64((uintptr_t)obj);
    __m512i u64_size = _mm512_set1_epi64(sizeof(uint64_t));

    // offset = ptr - obj
    __m512i offset = _mm512_sub_epi64(ptr_vec, obj_vec);

    // tmp = (offset / CACHE_LINE_ALIGNMENT + 1) * sizeof(uint64_t)
    __m512i offset_div = _mm512_srli_epi64(offset, CACHE_LINE_SHIFT);
    __m512i tmp = _mm512_mullo_epi64(
            _mm512_add_epi64(offset_div, _mm512_set1_epi64(1)), 
            u64_size);

    // adjusted_ptr = obj + offset + tmp
    __m512i adjusted_ptr = _mm512_add_epi64(_mm512_add_epi64(offset, tmp), obj_vec);

    adjusted_ptr = _mm512_maskz_mov_epi64(mask, adjusted_ptr);

    // field_offset and field_size 
    uintptr_t raw_field_off = *field_off;
    uintptr_t field_end = raw_field_off + *field_size;

    uintptr_t aligned_field_offset = raw_field_off + ((raw_field_off / CACHE_LINE_ALIGNMENT) + 1) * sizeof(uint64_t);
    uintptr_t aligned_field_end = field_end + ((field_end / CACHE_LINE_ALIGNMENT) + 1) * sizeof(uint64_t);

    *field_off = aligned_field_offset;
    *field_size = aligned_field_end - aligned_field_offset;

    return adjusted_ptr;
}

#define DIV_i64 _mm512_set1_pd((double)(CACHE_LINE_ALIGNMENT - sizeof(uint64_t)))
// TODO: not correct, need to fix, div by 15 or 7 rather than 16 or 8
#define GET_REAL_PTR_VEC_i64(OBJ, PTR_VEC, FIELD_OFF, FIELD_SIZE, LENGTH) \
    do { \
        assert(((uintptr_t)OBJ % CACHE_LINE_ALIGNMENT) == 0); \
        assert(LENGTH <= DFL_AVX512_VECTOR_WIDTH_64); \
        /* __mmask16 mask = (1 << LENGTH) - 1; \
        __m512i obj_vec = _mm512_set1_epi64((uintptr_t)OBJ); \
        __m512i u64_size = _mm512_set1_epi64(sizeof(uint64_t)); \
        __m512i offset = _mm512_sub_epi64(PTR_VEC, obj_vec); \
        __m512i offset_div = _mm512_srli_epi64(offset, CACHE_LINE_SHIFT);*/ \
        /*__m512i offset_div = _mm512_cvttpd_epi64( \
                _mm512_div_pd(_mm512_cvtepi64_pd(offset), DIV_i64)); */ \
        /*__m512i tmp = _mm512_mullo_epi64( \
                _mm512_add_epi64(offset_div, _mm512_set1_epi64(1)), u64_size); \
        PTR_VEC = _mm512_add_epi64(_mm512_add_epi64(offset, tmp), obj_vec); \
        PTR_VEC = _mm512_maskz_mov_epi64(mask, PTR_VEC);*/ \
        uintptr_t field_end = FIELD_OFF + FIELD_SIZE; \
        (FIELD_OFF) += ((FIELD_OFF / (CACHE_LINE_ALIGNMENT - sizeof(uint64_t))) + 1) * sizeof(uint64_t); \
        field_end += ((field_end / (CACHE_LINE_ALIGNMENT - sizeof(uint64_t))) + 1) * sizeof(uint64_t); \
        (FIELD_SIZE) = field_end - (FIELD_OFF); \
    } while (0)


inline __m512i get_real_ptr_vec_uint32_t(unsigned char* obj, __m512i ptr_vec, unsigned long* field_off, unsigned long* field_size, unsigned int length) {
    assert(((uintptr_t)obj % CACHE_LINE_ALIGNMENT) == 0);
    assert(length <= DFL_AVX512_VECTOR_WIDTH_32);

    __mmask16 mask = (1 << length) - 1;
    __m512i obj_vec = _mm512_set1_epi32((uintptr_t)obj);
    __m512i u32_size = _mm512_set1_epi32(sizeof(uint32_t));

    // offset = ptr - obj
    __m512i offset = _mm512_sub_epi32(ptr_vec, obj_vec);
    // tmp = (offset / CACHE_LINE_ALIGNMENT + 1) * sizeof(uint32_t)
    __m512i offset_div = _mm512_srli_epi32(offset, CACHE_LINE_SHIFT);
    __m512i tmp = _mm512_mullo_epi32(
            _mm512_add_epi32(offset_div, _mm512_set1_epi32(1)), 
            u32_size);
        
    // adjusted_ptr = obj + offset + tmp
    __m512i adjusted_ptr = _mm512_add_epi32(_mm512_add_epi32(offset, tmp), obj_vec);
    adjusted_ptr = _mm512_maskz_mov_epi32(mask, adjusted_ptr);

    // field_offset and field_size 
    uintptr_t raw_field_off = *field_off;
    uintptr_t field_end = raw_field_off + *field_size;

    uintptr_t aligned_field_offset = raw_field_off + ((raw_field_off / CACHE_LINE_ALIGNMENT) + 1) * sizeof(uint32_t);
    uintptr_t aligned_field_end = field_end + ((field_end / CACHE_LINE_ALIGNMENT) + 1) * sizeof(uint32_t);

    *field_off = aligned_field_offset;
    *field_size = aligned_field_end - aligned_field_offset;

    return adjusted_ptr;
}

#define GET_REAL_PTR_VEC_i32(OBJ, PTR_VEC, FIELD_OFF, FIELD_SIZE, LENGTH) \
    do { \
        assert(((uintptr_t)OBJ % CACHE_LINE_ALIGNMENT) == 0); \
        assert(LENGTH <= DFL_AVX512_VECTOR_WIDTH_32); \
        /* __mmask32 mask = (1 << LENGTH) - 1; \
        __m512i obj_vec = _mm512_set1_epi32((uintptr_t)OBJ); \
        __m512i u32_size = _mm512_set1_epi32(sizeof(uint32_t)); \
        __m512i offset = _mm512_sub_epi32(PTR_VEC, obj_vec); \
        __m512i offset_div = _mm512_srli_epi32(offset, CACHE_LINE_SHIFT); \
        __m512i tmp = _mm512_mullo_epi32( \
                _mm512_add_epi32(offset_div, _mm512_set1_epi32(1)), u32_size); \
        PTR_VEC = _mm512_add_epi32(_mm512_add_epi32(offset, tmp), obj_vec); \
        PTR_VEC = _mm512_maskz_mov_epi32(mask, PTR_VEC);*/ \
        uintptr_t field_end = FIELD_OFF + FIELD_SIZE; \
        (FIELD_OFF) += ((FIELD_OFF / (CACHE_LINE_ALIGNMENT - sizeof(uint32_t))) + 1) * sizeof(uint32_t); \
        field_end += ((field_end / (CACHE_LINE_ALIGNMENT - sizeof(uint32_t))) + 1) * sizeof(uint32_t); \
        FIELD_SIZE = field_end - FIELD_OFF; \
    } while (0)


DFL_FUNC __m512i uint64_t_avx512_gather_dfl_glob_vector_load(unsigned char* obj, __m512i ptr_list, unsigned long list_len, unsigned long field_off, unsigned long field_size) {
    // __m512i ptr_list = _mm512_load_si512(ptr_ptr_list);
    // printf("uint64_t_avx512_gather_dfl_glob_vector_load: obj: %p - %lu - %lu - %lu - %lu\n", obj, obj, list_len, field_off, field_size);
    // unsigned char* ptr_indices[DFL_AVX512_VECTOR_WIDTH_64] = {0};
    // ptr_list = get_real_ptr_vec_uint64_t(obj, ptr_list, &field_off, &field_size, list_len);
    #if !DFL_READONLY
    GET_REAL_PTR_VEC_i64(obj, ptr_list, field_off, field_size, list_len);
    #endif
    // PRINT_M512I_EPI64("new ptr_list", ptr_list);
    
    const unsigned long max_cache_lines = DFL_AVX512_VECTOR_WIDTH_64 - list_len + 1;
    const unsigned long bytes_per_ins = max_cache_lines * CACHE_LINE_ALIGNMENT;
    __m512i res = _mm512_setzero_si512();
    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(CACHE_LINE_ALIGNMENT - 1));
    unsigned char* _end = obj + field_off + field_size;
    for (volatile unsigned char* _ptr = aligned_obj; _ptr < _end; _ptr += bytes_per_ins) {
        __m512i base_vec = _mm512_set1_epi64((uintptr_t)_ptr);
        __m512i end_vec  = _mm512_set1_epi64((uintptr_t)_ptr + bytes_per_ins);
        // ptr >= base && ptr < _ptr + bytes_per_ins
        __mmask8 in_range_mask = _mm512_cmpge_epi64_mask(ptr_list, base_vec) & _mm512_cmplt_epi64_mask(ptr_list, end_vec);
        __m512i offset_vec =_mm512_maskz_mov_epi64(in_range_mask,
                _mm512_sub_epi64(ptr_list, base_vec));
        __m512i line_index = _mm512_srli_epi64(offset_vec, CACHE_LINE_SHIFT);
        __mmask8 line_accessed_mask = _mm512_reduce_or_epi64(
            _mm512_sllv_epi64(_mm512_set1_epi64(1), line_index));
        __m512i default_index_vec =  _mm512_setr_epi64(0, 1 * CACHE_LINE_ALIGNMENT, 
                    2 * CACHE_LINE_ALIGNMENT, 3 * CACHE_LINE_ALIGNMENT,
                    4 * CACHE_LINE_ALIGNMENT, 5 * CACHE_LINE_ALIGNMENT, 
                    6 * CACHE_LINE_ALIGNMENT, 7 * CACHE_LINE_ALIGNMENT);
        default_index_vec = _mm512_maskz_mov_epi64(_mm512_cmplt_epi64_mask(default_index_vec, 
                        _mm512_set1_epi64((uintptr_t)_end - (uintptr_t)_ptr)),
                    default_index_vec);
        default_index_vec = _mm512_maskz_compress_epi64(~line_accessed_mask, default_index_vec);
        offset_vec = _mm512_permutex2var_epi64(offset_vec, get_combine_idx_i64(list_len), default_index_vec);

        __m512i vdata = _mm512_i64gather_epi64(offset_vec, _ptr, 1);
        res = _mm512_mask_blend_epi64(in_range_mask, res, vdata);
        // PRINT_M512I_EPI64("vindex", vindex);
        // PRINT_M512I_EPI64("vdata", vdata);
        // PRINT_M512I_EPI64("res", res);
    }

    return res;
}


DFL_FUNC __m512i uint32_t_avx512_gather_dfl_glob_vector_load(unsigned char* obj, __m512i ptr_list, unsigned long list_len, unsigned long field_off, unsigned long field_size) {
    
    // if ptr is 64 bit wide
    // if (((unsigned long) ptr_list[0] != (unsigned int) ptr_list[0]) || ((unsigned long) obj != (unsigned int) obj))
    //     return uint64_t_avx512_gather_dfl_glob_vector_load(obj, ptr_list, list_len, field_off, field_size);

    // printf("uint32_t_avx512_gather_dfl_glob_vector_load: obj: %p - %lu - %u - %d\n", obj, obj, obj, obj);
    // unsigned char* ptr_indices[DFL_AVX512_VECTOR_WIDTH_32] = {0};
    // PRINT_M512I_EPI32("old ptr_list", ptr_list);
    // ptr_list = get_real_ptr_vec_uint32_t(obj, ptr_list, &field_off, &field_size, list_len);
    #if !DFL_READONLY
    GET_REAL_PTR_VEC_i32(obj, ptr_list, field_off, field_size, list_len);
    #endif
    // PRINT_M512I_EPI32("new ptr_list", ptr_list);

    const unsigned long max_cache_lines = DFL_AVX512_VECTOR_WIDTH_32 - list_len + 1;
    const unsigned long bytes_per_ins = max_cache_lines * CACHE_LINE_ALIGNMENT;
    __m512i res = _mm512_setzero_si512();
    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(CACHE_LINE_ALIGNMENT - 1));
    unsigned char* _end = obj + field_off + field_size;
    for (volatile unsigned char* _ptr = aligned_obj; _ptr < _end; _ptr += bytes_per_ins) {
        __m512i base_vec = _mm512_set1_epi32((uintptr_t)_ptr);
        __m512i end_vec  = _mm512_set1_epi32((uintptr_t)_ptr + bytes_per_ins);
        // ptr >= base && ptr < _ptr + bytes_per_ins
        __mmask16 in_range_mask = _mm512_cmpge_epi32_mask(ptr_list, base_vec) & _mm512_cmplt_epi32_mask(ptr_list, end_vec);
        __m512i offset_vec =_mm512_maskz_mov_epi32(in_range_mask,
                _mm512_sub_epi32(ptr_list, base_vec));
        __m512i line_index = _mm512_srli_epi32(offset_vec, CACHE_LINE_SHIFT);
        __mmask16 line_accessed_mask = _mm512_reduce_or_epi32(
            _mm512_sllv_epi32(_mm512_set1_epi32(1), line_index));
        __m512i default_index_vec =  _mm512_setr_epi32(0, 1 * CACHE_LINE_ALIGNMENT, 
                    2 * CACHE_LINE_ALIGNMENT, 3 * CACHE_LINE_ALIGNMENT,
                    4 * CACHE_LINE_ALIGNMENT, 5 * CACHE_LINE_ALIGNMENT, 
                    6 * CACHE_LINE_ALIGNMENT, 7 * CACHE_LINE_ALIGNMENT,
                    8 * CACHE_LINE_ALIGNMENT, 9 * CACHE_LINE_ALIGNMENT,
                    10 * CACHE_LINE_ALIGNMENT, 11 * CACHE_LINE_ALIGNMENT,
                    12 * CACHE_LINE_ALIGNMENT, 13 * CACHE_LINE_ALIGNMENT,
                    14 * CACHE_LINE_ALIGNMENT, 15 * CACHE_LINE_ALIGNMENT);
        default_index_vec = _mm512_maskz_mov_epi32(_mm512_cmplt_epi32_mask(default_index_vec,
                        _mm512_set1_epi32((uintptr_t)_end - (uintptr_t)_ptr)),
                    default_index_vec);
        default_index_vec = _mm512_maskz_compress_epi32(~line_accessed_mask, default_index_vec);
        offset_vec = _mm512_permutex2var_epi32(offset_vec, get_combine_idx_i32(list_len), default_index_vec);
        
        __m512i vdata = _mm512_i32gather_epi32(offset_vec, _ptr, 1);
        res = _mm512_mask_blend_epi32(in_range_mask, res, vdata);
        // PRINT_M512I_EPI32("vindex", vindex);
        // PRINT_M512I_EPI32("vdata", vdata);
        // PRINT_M512I_EPI32("res", res);
    }

    return res;
}

// TODO: 
DFL_FUNC __m512i uint64_t_avx512_gather_dfl_obj_vector_load(dfl_obj_list_head head, unsigned char** ptr_list, unsigned long list_len, unsigned long field_off, unsigned long field_size) {
    unsigned char* ptr_indices[DFL_AVX512_VECTOR_WIDTH_64] = {0};
    unsigned long copy_field_off = field_off;
    unsigned long copy_field_size = field_size;

    for (unsigned long i = 0; i < list_len; i++) {
        field_off = copy_field_off;
        field_size = copy_field_size;
        ptr_indices[i] = get_real_ptr_uint64_t(head->data, ptr_list[i], &field_off, &field_size, NULL);
    }
    
    unsigned long max_cache_lines = DFL_AVX512_VECTOR_WIDTH_64 - list_len + 1;
    unsigned long bytes_per_ins = max_cache_lines * CACHE_LINE_ALIGNMENT;
    __m512i res = _mm512_setzero_si512();

    while(head) {
        unsigned char* aligned_obj = (unsigned char*)(((unsigned long)head->data + field_off) & ~(CACHE_LINE_ALIGNMENT - 1));
        unsigned char* _end = head->data + field_off + field_size;

        for (volatile unsigned char* _ptr = aligned_obj; _ptr < _end; _ptr += bytes_per_ins) {
            uint64_t indices[DFL_AVX512_VECTOR_WIDTH_64] = {0};
            uint8_t line_accessed_mask = 0;
            uint8_t result_mask = 0;
            volatile unsigned char* curr_end = _ptr + bytes_per_ins;
            for (size_t ptr_index = 0; ptr_index < list_len; ptr_index++) {
                unsigned char* ptr = ptr_indices[ptr_index];
                uint64_t in_range = (ptr >= _ptr && ptr < curr_end);
                result_mask |= (in_range << ptr_index);
                indices[ptr_index] = ((ptr - _ptr) / sizeof(uint64_t)) * in_range;
                size_t line_index = indices[ptr_index] / DFL_AVX512_VECTOR_WIDTH_64;
                line_accessed_mask |= (1u << line_index);
            }
            size_t idx_fill = list_len;
            for (size_t line = 0;
                 line < max_cache_lines; line++) {
                uint64_t cond = ((line_accessed_mask >> line) & 1U) ^ 1U; 
                uint64_t default_index = line * DFL_AVX512_VECTOR_WIDTH_64;
                uint64_t in_bounds = (_ptr + line * CACHE_LINE_ALIGNMENT < _end) ? 1U : 0U;
                default_index = default_index * in_bounds;
                uint64_t original = indices[idx_fill];
                indices[idx_fill] = cond ? default_index : original;
                idx_fill += cond;
            }
            while (idx_fill < DFL_AVX512_VECTOR_WIDTH_64) {
                indices[idx_fill] = 0; // fill the rest with zeros
                idx_fill += 1;
            }
            __m512i vindex = _mm512_loadu_si512((__m512i*)indices);
            __m512i vdata = _mm512_i64gather_epi64(vindex, _ptr, 8);
            res = _mm512_mask_blend_epi64((__mmask8)result_mask, res, vdata);
        }
        head = head->next;
        field_off = copy_field_off;
        field_size = copy_field_size;
    }

    return res;
}

// TODO: 
DFL_FUNC __m512i uint32_t_avx312_gather_dfl_obj_vector_load(dfl_obj_list_head head, unsigned char** ptr_list, unsigned long list_len, unsigned long field_off, unsigned long field_size) {
    // if ptr is 64 bit wide
    if (((unsigned long) ptr_list[0] != (unsigned int) ptr_list[0]) || ((unsigned long) head->data != (unsigned int) head->data))
        return uint64_t_avx512_gather_dfl_obj_vector_load(head, ptr_list, list_len, field_off, field_size);

    unsigned char* ptr_indices[DFL_AVX512_VECTOR_WIDTH_32] = {0};
    unsigned long copy_field_off = field_off;
    unsigned long copy_field_size = field_size;

    unsigned long max_cache_lines = DFL_AVX512_VECTOR_WIDTH_32 - list_len + 1;
    unsigned long bytes_per_ins = max_cache_lines * CACHE_LINE_ALIGNMENT;
    __m512i res = _mm512_setzero_si512();

    while(head) {
        for (unsigned long i = 0; i < list_len; i++) {
            field_off = copy_field_off;
            field_size = copy_field_size;
            ptr_indices[i] = get_real_ptr_uint32_t(head->data, ptr_list[i], &field_off, &field_size, NULL);
        }
        
        unsigned char* aligned_obj = (unsigned char*)(((unsigned long)head->data + field_off) & ~(CACHE_LINE_ALIGNMENT - 1));
        unsigned char* _end = head->data + field_off + field_size;

        for (volatile unsigned char* _ptr = aligned_obj; _ptr < _end; _ptr += bytes_per_ins) {
            uint32_t indices[DFL_AVX512_VECTOR_WIDTH_32] = {0};
            uint16_t line_accessed_mask = 0;
            uint16_t result_mask = 0;
            volatile unsigned char* curr_end = _ptr + bytes_per_ins;
            for (size_t ptr_index = 0; ptr_index < list_len; ptr_index++) {
                unsigned char* ptr = ptr_indices[ptr_index];
                uint32_t in_range = (ptr >= _ptr && ptr < curr_end);
                result_mask |= (in_range << ptr_index);
                indices[ptr_index] = ((ptr - _ptr) / sizeof(uint32_t)) * in_range;
                size_t line_index = indices[ptr_index] / DFL_AVX512_VECTOR_WIDTH_32;
                line_accessed_mask |= (1u << line_index);
            }
            size_t idx_fill = list_len;
            for (size_t line = 0; line < max_cache_lines; line++) {
                uint32_t cond = ((line_accessed_mask >> line) & 1U) ^ 1U; 
                uint32_t default_index = line * DFL_AVX512_VECTOR_WIDTH_32;
                uint32_t in_bounds = (_ptr + line * CACHE_LINE_ALIGNMENT < _end) ? 1U : 0U;
                default_index = default_index * in_bounds;
                uint32_t original = indices[idx_fill];
                indices[idx_fill] = cond ? default_index : original;
                idx_fill += cond;
            }
            while (idx_fill < DFL_AVX512_VECTOR_WIDTH_32) {
                indices[idx_fill] = 0; // fill the rest with zeros
                idx_fill += 1;
            }

            __m512i vindex = _mm512_loadu_si512((__m512i*)indices);
            __m512i vdata = _mm512_i32gather_epi32(vindex, _ptr, 4);
            res = _mm512_mask_blend_epi32((__mmask16)result_mask, res, vdata);
        }
        head = head->next;
        field_off = copy_field_off;
        field_size = copy_field_size;
    }

    return res;
}


DFL_FUNC __m512i uint64_t_avx512_linear_dfl_glob_vector_load(unsigned char* obj, __m512i ptr_list, unsigned long list_len, unsigned long field_off, unsigned long field_size) { 
    #if !DFL_READONLY
    GET_REAL_PTR_VEC_i64(obj, ptr_list, field_off, field_size, list_len);
    #endif
    __m512i res = _mm512_setzero_si512();
    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(CACHE_LINE_ALIGNMENT - 1));
    unsigned char* _end = obj + field_off + field_size;

    __m512i offsetv = _mm512_sub_epi64(ptr_list, _mm512_set1_epi64((uintptr_t)aligned_obj));
    __m512i cachelinev = _mm512_mask_blend_epi64((1 << list_len) - 1,
        _mm512_set1_epi64(-1),
        _mm512_srli_epi64(offsetv, CACHE_LINE_SHIFT));
    __m512i idx = _mm512_and_epi64(offsetv, _mm512_set1_epi64(CACHE_LINE_ALIGNMENT - 1));
    idx = _mm512_srli_epi64(idx, TYPE_SHIFT(uint64_t));
    // PRINT_M512I_EPI64("cachelinev", cachelinev);
    // PRINT_M512I_EPI64("idx", idx);
    int64_t curr_cacheline = 0;
    for (volatile unsigned char* _ptr = aligned_obj; _ptr < _end; _ptr += DFL_FIXED_AVX512_STRIDE) {
        __m512i curr_data = _mm512_load_epi64(_ptr);
        // PRINT_M512I_EPI64("curr_data 1", curr_data);
        __mmask8 cacheline_masks = _mm512_cmpeq_epi64_mask(cachelinev, _mm512_set1_epi64(curr_cacheline));
        __m512i curr_idx = _mm512_maskz_mov_epi64(cacheline_masks, idx);
        // PRINT_M512I_EPI64("curr_idx", curr_idx);
        curr_data = _mm512_maskz_permutexvar_epi64(cacheline_masks, curr_idx, curr_data);
        // PRINT_M512I_EPI64("curr_data 2", curr_data);
        res = _mm512_mask_blend_epi64(cacheline_masks, res, curr_data);
        // PRINT_M512I_EPI64("res", res);
        curr_cacheline += 1;
    }

    return res;
}

DFL_FUNC __m512i uint32_t_avx512_linear_dfl_glob_vector_load(unsigned char* obj, __m512i ptr_list, unsigned long list_len, unsigned long field_off, unsigned long field_size) { 
    #if !DFL_READONLY
    GET_REAL_PTR_VEC_i32(obj, ptr_list, field_off, field_size, list_len);
    #endif
    __m512i res = _mm512_setzero_si512();
    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(CACHE_LINE_ALIGNMENT - 1));
    unsigned char* _end = obj + field_off + field_size;

    __m512i offsetv = _mm512_sub_epi32(ptr_list, _mm512_set1_epi32((uintptr_t)aligned_obj));
    __m512i cachelinev = _mm512_mask_blend_epi32((1 << list_len) - 1,
        _mm512_set1_epi32(-1),
        _mm512_srli_epi32(offsetv, CACHE_LINE_SHIFT));
    __m512i idx = _mm512_and_epi32(offsetv, _mm512_set1_epi32(CACHE_LINE_ALIGNMENT - 1));
    idx = _mm512_srli_epi32(idx, TYPE_SHIFT(uint32_t));

    int32_t curr_cacheline = 0;
    for (volatile unsigned char* _ptr = aligned_obj; _ptr < _end; _ptr += DFL_FIXED_AVX512_STRIDE) {
        __m512i curr_data = _mm512_load_epi32(_ptr);
        __mmask16 cacheline_masks = _mm512_cmpeq_epi32_mask(cachelinev, _mm512_set1_epi32(curr_cacheline));
        __m512i curr_idx = _mm512_maskz_mov_epi32(cacheline_masks, idx);
        curr_data = _mm512_maskz_permutexvar_epi32(cacheline_masks, curr_idx, curr_data);
        res = _mm512_mask_blend_epi32(cacheline_masks, res, curr_data);
        curr_cacheline += 1;
    }

    return res;
}

DFL_FUNC void uint64_t_avx512_scatter_dfl_glob_vector_store(unsigned char* obj, __m512i ptr_list, unsigned long list_len, unsigned long field_off, unsigned long field_size, __m512i valuev) {
    // unsigned char* ptr_indices[DFL_AVX512_VECTOR_WIDTH_64] = {0};
    // uint64_t values[DFL_AVX512_VECTOR_WIDTH_64] = {0};
    // ptr_list = get_real_ptr_vec_uint64_t(obj, ptr_list, &field_off, &field_size, list_len);
    GET_REAL_PTR_VEC_i64(obj, ptr_list, field_off, field_size, list_len);
    
    const unsigned long max_cache_lines = DFL_AVX512_VECTOR_WIDTH_64 - list_len + 1;
    const unsigned long bytes_per_ins = max_cache_lines * CACHE_LINE_ALIGNMENT;
    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(CACHE_LINE_ALIGNMENT - 1));
    unsigned char* _end = obj + field_off + field_size;
    for (volatile unsigned char* _ptr = aligned_obj; _ptr < _end; _ptr += bytes_per_ins) {
        __m512i base_vec = _mm512_set1_epi64((uintptr_t)_ptr);
        __m512i end_vec  = _mm512_set1_epi64((uintptr_t)_ptr + bytes_per_ins);
        // ptr >= base && ptr < _ptr + bytes_per_ins
        __mmask8 in_range_mask = _mm512_cmpge_epi64_mask(ptr_list, base_vec) & _mm512_cmplt_epi64_mask(ptr_list, end_vec);
        __m512i offset_vec =_mm512_maskz_mov_epi64(in_range_mask,
                _mm512_sub_epi64(ptr_list, base_vec));
        __m512i line_index = _mm512_srli_epi64(offset_vec, CACHE_LINE_SHIFT);
        __mmask8 line_accessed_mask = _mm512_reduce_or_epi64(
                    _mm512_sllv_epi64(_mm512_set1_epi64(1), line_index));
        __m512i default_index_vec = _mm512_setr_epi64(0, 1 * CACHE_LINE_ALIGNMENT, 
                    2 * CACHE_LINE_ALIGNMENT, 3 * CACHE_LINE_ALIGNMENT,
                    4 * CACHE_LINE_ALIGNMENT, 5 * CACHE_LINE_ALIGNMENT, 
                    6 * CACHE_LINE_ALIGNMENT, 7 * CACHE_LINE_ALIGNMENT);
        default_index_vec = _mm512_maskz_mov_epi64(_mm512_cmplt_epi64_mask(default_index_vec, 
                        _mm512_set1_epi64((uintptr_t)_end - (uintptr_t)_ptr)),
                    default_index_vec);
        default_index_vec = _mm512_maskz_compress_epi64(~line_accessed_mask, default_index_vec);
        offset_vec = _mm512_permutex2var_epi64(offset_vec, get_combine_idx_i64(list_len), default_index_vec);

        // PRINT_M512I_EPI64("vindex", vindex);
        // PRINT_M512I_EPI64("valuev", valuev);
        _mm512_i64scatter_epi64(_ptr, offset_vec, valuev, 1);
    }
}

DFL_FUNC void uint32_t_avx512_scatter_dfl_glob_vector_store(unsigned char* obj, __m512i ptr_list, unsigned long list_len, unsigned long field_off, unsigned long field_size, __m512i valuev) {

    // if ptr is 64 bit wide
    // if (((unsigned long) ptr_list[0] != (unsigned int) ptr_list[0]) || ((unsigned long) obj != (unsigned int) obj))
    //     return uint64_t_avx512_scatter_dfl_glob_vector_store(obj, ptr_list, list_len, field_off, field_size, value_list);

    // ptr_list = get_real_ptr_vec_uint32_t(obj, ptr_list, &field_off, &field_size, list_len);
    GET_REAL_PTR_VEC_i32(obj, ptr_list, field_off, field_size, list_len);

    const unsigned long max_cache_lines = DFL_AVX512_VECTOR_WIDTH_32 - list_len + 1;
    const unsigned long bytes_per_ins = max_cache_lines * CACHE_LINE_ALIGNMENT;
    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(CACHE_LINE_ALIGNMENT - 1));
    unsigned char* _end = obj + field_off + field_size;
    for (volatile unsigned char* _ptr = aligned_obj; _ptr < _end; _ptr += bytes_per_ins) {
        __m512i base_vec = _mm512_set1_epi32((uintptr_t)_ptr);
        __m512i end_vec  = _mm512_set1_epi32((uintptr_t)_ptr + bytes_per_ins);
        // ptr >= base && ptr < _ptr + bytes_per_ins
        __mmask16 in_range_mask = _mm512_cmpge_epi32_mask(ptr_list, base_vec) & _mm512_cmplt_epi32_mask(ptr_list, end_vec);
        __m512i offset_vec =_mm512_maskz_mov_epi32(in_range_mask,
                _mm512_sub_epi32(ptr_list, base_vec));
        __m512i line_index = _mm512_srli_epi32(offset_vec, CACHE_LINE_SHIFT);
        __mmask16 line_accessed_mask = _mm512_reduce_or_epi32(
                    _mm512_sllv_epi32(_mm512_set1_epi32(1), line_index));
        __m512i default_index_vec = _mm512_setr_epi32(0, 1 * CACHE_LINE_ALIGNMENT, 
                    2 * CACHE_LINE_ALIGNMENT, 3 * CACHE_LINE_ALIGNMENT,
                    4 * CACHE_LINE_ALIGNMENT, 5 * CACHE_LINE_ALIGNMENT, 
                    6 * CACHE_LINE_ALIGNMENT, 7 * CACHE_LINE_ALIGNMENT,
                    8 * CACHE_LINE_ALIGNMENT, 9 * CACHE_LINE_ALIGNMENT,
                    10 * CACHE_LINE_ALIGNMENT, 11 * CACHE_LINE_ALIGNMENT,
                    12 * CACHE_LINE_ALIGNMENT, 13 * CACHE_LINE_ALIGNMENT,
                    14 * CACHE_LINE_ALIGNMENT, 15 * CACHE_LINE_ALIGNMENT);
        default_index_vec = _mm512_maskz_mov_epi32(_mm512_cmplt_epi32_mask(default_index_vec, 
                        _mm512_set1_epi32((uintptr_t)_end - (uintptr_t)_ptr)),
                    default_index_vec);
        default_index_vec = _mm512_maskz_compress_epi32(~line_accessed_mask, default_index_vec);
        offset_vec = _mm512_permutex2var_epi32(offset_vec, get_combine_idx_i32(list_len), default_index_vec);

        // PRINT_M512I_EPI32("vindex", vindex);
        // PRINT_M512I_EPI32("valuev", valuev);
        _mm512_i32scatter_epi32(_ptr, offset_vec, valuev, 1);
    }
}

// TODO:
DFL_FUNC void uint64_t_avx512_scatter_dfl_obj_vector_store(dfl_obj_list_head head, unsigned char** ptr_list, unsigned long list_len, unsigned long field_off, unsigned long field_size, uint64_t *value_list) {
    unsigned char* ptr_indices[DFL_AVX512_VECTOR_WIDTH_64] = {0};
    uint64_t values[DFL_AVX512_VECTOR_WIDTH_64] = {0};
    unsigned long copy_field_off = field_off;
    unsigned long copy_field_size = field_size;

    unsigned long max_cache_lines = DFL_AVX512_VECTOR_WIDTH_64 - list_len + 1;
    unsigned long bytes_per_ins = max_cache_lines * CACHE_LINE_ALIGNMENT;

    for (unsigned long i = 0; i < list_len; i++) {
        values[i] = value_list[i];
    }
    __m512i valuev = _mm512_loadu_si512((__m512i*)values);

    while(head) {
        for (unsigned long i = 0; i < list_len; i++) {
            field_off = copy_field_off;
            field_size = copy_field_size;
            ptr_indices[i] = get_real_ptr_uint64_t(head->data, ptr_list[i], &field_off, &field_size, NULL);
        }

        unsigned char* aligned_obj = (unsigned char*)(((unsigned long)head->data + field_off) & ~(CACHE_LINE_ALIGNMENT - 1));
        unsigned char* _end = head->data + field_off + field_size;

        for (volatile unsigned char* _ptr = aligned_obj; _ptr < _end; _ptr += bytes_per_ins) {
            uint64_t indices[DFL_AVX512_VECTOR_WIDTH_64] = {0};
            uint8_t line_accessed_mask = 0;
            uint8_t result_mask = 0;
            volatile unsigned char* curr_end = _ptr + bytes_per_ins;
            for (size_t ptr_index = 0; ptr_index < list_len; ptr_index++) {
                unsigned char* ptr = ptr_indices[ptr_index];
                uint64_t in_range = (ptr >= _ptr && ptr < curr_end);
                result_mask |= (in_range << ptr_index);
                indices[ptr_index] = ((ptr - _ptr) / sizeof(uint64_t)) * in_range;
                size_t line_index = indices[ptr_index] / DFL_AVX512_VECTOR_WIDTH_64;
                line_accessed_mask |= (1u << line_index);
            }
            size_t idx_fill = list_len;
            for (size_t line = 0; line < max_cache_lines; line++) {
                uint64_t cond = ((line_accessed_mask >> line) & 1U) ^ 1U; 
                uint64_t default_index = line * DFL_AVX512_VECTOR_WIDTH_64;
                uint64_t in_bounds = (_ptr + line * CACHE_LINE_ALIGNMENT < _end) ? 1U : 0U;
                default_index = default_index * in_bounds;
                uint64_t original = indices[idx_fill];
                indices[idx_fill] = cond ? default_index : original;
                idx_fill += cond;
            }
            while (idx_fill < DFL_AVX512_VECTOR_WIDTH_64) {
                indices[idx_fill] = 0; // fill the rest with zeros
                idx_fill += 1;
            }
            
            __m512i vindex = _mm512_loadu_si512((__m512i*)indices);
            _mm512_i64scatter_epi64((long long *)_ptr, vindex, valuev, 8);
        }
        head = head->next;
        field_off = copy_field_off;
        field_size = copy_field_size;
    }
}

// TODO:
DFL_FUNC void uint32_t_avx512_scatter_dfl_obj_vector_store(dfl_obj_list_head head, unsigned char** ptr_list, unsigned long list_len, unsigned long field_off, unsigned long field_size, uint64_t *value_list) {
    // if ptr is 64 bit wide
    if (((unsigned long) ptr_list[0] != (unsigned int) ptr_list[0]) || ((unsigned long) head->data != (unsigned int) head->data))
        return uint64_t_avx512_scatter_dfl_obj_vector_store(head, ptr_list, list_len, field_off, field_size, value_list);

    unsigned char* ptr_indices[DFL_AVX512_VECTOR_WIDTH_32] = {0};
    uint32_t values[DFL_AVX512_VECTOR_WIDTH_32] = {0};
    unsigned long copy_field_off = field_off;
    unsigned long copy_field_size = field_size;

    unsigned long max_cache_lines = DFL_AVX512_VECTOR_WIDTH_32 - list_len + 1;
    unsigned long bytes_per_ins = max_cache_lines * CACHE_LINE_ALIGNMENT;

    for (unsigned long i = 0; i < list_len; i++) {
        values[i] = value_list[i];
    }
    __m512i valuev = _mm512_loadu_si512((__m512i*)values);

    while(head) {
        for (unsigned long i = 0; i < list_len; i++) {
            field_off = copy_field_off;
            field_size = copy_field_size;
            ptr_indices[i] = get_real_ptr_uint32_t(head->data, ptr_list[i], &field_off, &field_size, NULL);
        }

        unsigned char* aligned_obj = (unsigned char*)(((unsigned long)head->data + field_off) & ~(CACHE_LINE_ALIGNMENT - 1));
        unsigned char* _end = head->data + field_off + field_size;

        for (volatile unsigned char* _ptr = aligned_obj; _ptr < _end; _ptr += bytes_per_ins) {
            uint32_t indices[DFL_AVX512_VECTOR_WIDTH_32] = {0};
            uint16_t line_accessed_mask = 0;
            uint16_t result_mask = 0;
            volatile unsigned char* curr_end = _ptr + bytes_per_ins;
            for (size_t ptr_index = 0; ptr_index < list_len; ptr_index++) {
                unsigned char* ptr = ptr_indices[ptr_index];
                uint32_t in_range = (ptr >= _ptr && ptr < curr_end);
                result_mask |= (in_range << ptr_index);
                indices[ptr_index] = ((ptr - _ptr) / sizeof(uint32_t)) * in_range;
                size_t line_index = indices[ptr_index] / DFL_AVX512_VECTOR_WIDTH_32;
                line_accessed_mask |= (1u << line_index);
            }
            size_t idx_fill = list_len;
            for (size_t line = 0; line < max_cache_lines; line++) {
                uint32_t cond = ((line_accessed_mask >> line) & 1U) ^ 1U; 
                uint32_t default_index = line * DFL_AVX512_VECTOR_WIDTH_32;
                uint32_t in_bounds = (_ptr + line * CACHE_LINE_ALIGNMENT < _end) ? 1U : 0U;
                default_index = default_index * in_bounds;
                uint32_t original = indices[idx_fill];
                indices[idx_fill] = cond ? default_index : original;
                idx_fill += cond;
            }
            while (idx_fill < DFL_AVX512_VECTOR_WIDTH_32) {
                indices[idx_fill] = 0; 
                idx_fill += 1;
            }
            __m512i vindex = _mm512_loadu_si512((__m512i*)indices);
            _mm512_i32scatter_epi32((long long *)_ptr, vindex, valuev, 4);
        }
        head = head->next;
        field_off = copy_field_off;
        field_size = copy_field_size;
    }
}

#define GEN_PERM_UPDATE_I64(I) do { \
    dest = _mm_extract_epi64(tmp_128, I % 2); \
    perm_index = _mm512_mask_blend_epi64(dest, perm_index, _mm512_set1_epi64(I)); \
} while (0)

#define GEN_PERM_I64_2() \
    tmp_128 = _mm512_extracti64x2_epi64(curr_idx_masks, 0); \
    GEN_PERM_UPDATE_I64(0); \
    GEN_PERM_UPDATE_I64(1)

#define GEN_PERM_I64_3() \
    GEN_PERM_I64_2(); \
    tmp_128 = _mm512_extracti64x2_epi64(curr_idx_masks, 1); \
    GEN_PERM_UPDATE_I64(2)

#define GEN_PERM_I64_4() GEN_PERM_I64_3(); GEN_PERM_UPDATE_I64(3)

#define GEN_PERM_I64_5() \
    GEN_PERM_I64_4(); \
    tmp_128 = _mm512_extracti64x2_epi64(curr_idx_masks, 2); \
    GEN_PERM_UPDATE_I64(4)

#define GEN_PERM_I64_6() GEN_PERM_I64_5(); GEN_PERM_UPDATE_I64(5)

#define GEN_PERM_I64_7() \
    GEN_PERM_I64_6(); \
    tmp_128 = _mm512_extracti64x2_epi64(curr_idx_masks, 3); \
    GEN_PERM_UPDATE_I64(6)

#define GEN_PERM_I64_8() GEN_PERM_I64_7(); GEN_PERM_UPDATE_I64(7)

#define GEN_I64_AVX512_LINEAR_DFL_GLOB_VECTOR_STORE(list_len) \
    DFL_FUNC void uint64_t_avx512_linear_dfl_glob_vector_store_ ## list_len(unsigned char* obj, __m512i ptr_list, unsigned long _unused, unsigned long field_off, unsigned long field_size, __m512i valuev) { \
        GET_REAL_PTR_VEC_i64(obj, ptr_list, field_off, field_size, list_len); \
        unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(CACHE_LINE_ALIGNMENT - 1)); \
        unsigned char* _end = obj + field_off + field_size; \
        __m512i offsetv = _mm512_sub_epi64(ptr_list, _mm512_set1_epi64((uintptr_t)aligned_obj)); \
        __m512i cachelinev = _mm512_mask_blend_epi64((1 << list_len) - 1, \
            _mm512_set1_epi64(-1), \
            _mm512_srli_epi64(offsetv, CACHE_LINE_SHIFT)); \
        /* offset % 64 == offset & 63 */ \
        __m512i idx = _mm512_and_epi64(offsetv, _mm512_set1_epi64(CACHE_LINE_ALIGNMENT - 1)); \
        /* idx / 8 */ \
        idx = _mm512_srli_epi64(idx, TYPE_SHIFT(uint64_t)); \
        __m512i idx_masks = _mm512_sllv_epi64(_mm512_set1_epi64(1), idx); \
        int64_t curr_cacheline = 0; \
        for (volatile unsigned char* _ptr = aligned_obj; _ptr < _end; _ptr += DFL_FIXED_AVX512_STRIDE) { \
            __mmask8 cacheline_masks = _mm512_cmpeq_epi64_mask(cachelinev, _mm512_set1_epi64(curr_cacheline)); \
            __m512i curr_idx_masks = _mm512_maskz_mov_epi64(cacheline_masks, idx_masks); \
            __m512i perm_index = _mm512_set1_epi64(-1); \
            __m128i tmp_128; \
            uint64_t dest = 0; \
            GEN_PERM_I64_ ## list_len(); \
            __m512i datav = _mm512_permutexvar_epi64(perm_index, valuev); \
            __mmask8 mask = _mm512_reduce_or_epi64(curr_idx_masks) | 1; \
            _mm512_mask_store_epi64(_ptr, mask, datav); \
            curr_cacheline += 1; \
        } \
    }

GEN_I64_AVX512_LINEAR_DFL_GLOB_VECTOR_STORE(2)
GEN_I64_AVX512_LINEAR_DFL_GLOB_VECTOR_STORE(3)
GEN_I64_AVX512_LINEAR_DFL_GLOB_VECTOR_STORE(4)
GEN_I64_AVX512_LINEAR_DFL_GLOB_VECTOR_STORE(5)
GEN_I64_AVX512_LINEAR_DFL_GLOB_VECTOR_STORE(6)
GEN_I64_AVX512_LINEAR_DFL_GLOB_VECTOR_STORE(7)
GEN_I64_AVX512_LINEAR_DFL_GLOB_VECTOR_STORE(8)

// could above 8? than better performance? maybe not? 
DFL_FUNC void uint64_t_avx512_linear_dfl_glob_vector_store(unsigned char* obj, __m512i ptr_list, unsigned long list_len, unsigned long field_off, unsigned long field_size, __m512i valuev) {
    GET_REAL_PTR_VEC_i64(obj, ptr_list, field_off, field_size, list_len);
    __mmask8 length_mask = (1 << list_len) - 1;
    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(CACHE_LINE_ALIGNMENT - 1));
    unsigned char* _end = obj + field_off + field_size;
    __m512i offsetv = _mm512_sub_epi64(ptr_list, _mm512_set1_epi64((uintptr_t)aligned_obj));
    __m512i cachelinev = _mm512_mask_blend_epi64(length_mask,
        _mm512_set1_epi64(-1),
        _mm512_srli_epi64(offsetv, CACHE_LINE_SHIFT));
    // offset % 64 == offset & 63
    __m512i idx = _mm512_and_epi64(offsetv, _mm512_set1_epi64(CACHE_LINE_ALIGNMENT - 1));
    // idx / 8
    idx = _mm512_srli_epi64(idx, TYPE_SHIFT(uint64_t));
    __m512i idx_masks = _mm512_sllv_epi64(_mm512_set1_epi64(1), idx);

    int64_t curr_cacheline = 0;
    // PRINT_M512I_EPI64("cachelinev", cachelinev);
    // PRINT_M512I_EPI64("idx", idx);
    // PRINT_M512I_EPI64("valuev", valuev);
    // PRINT_M512I_EPI64("ptr_list", ptr_list);
    for (volatile unsigned char* _ptr = aligned_obj; _ptr < _end; _ptr += DFL_FIXED_AVX512_STRIDE) {
        __mmask8 cacheline_masks = _mm512_cmpeq_epi64_mask(cachelinev, _mm512_set1_epi64(curr_cacheline));
        __m512i curr_idx_masks = _mm512_maskz_mov_epi64(cacheline_masks, idx_masks);
        __m512i perm_index = _mm512_set1_epi64(-1);
        // __m512i datav = _mm512_setzero_si512();
        // #pragma unroll
        // for (int ix = 0; ix < 8; ix++) {
        //     // __mmask8 ix_mask = (1 << ix);
        //     // __mmask8 store_mask =  _mm512_mask_reduce_add_epi64(ix_mask, curr_idx_masks);
        //     uint64_t dest = _mm_extract_epi64(_mm512_extracti64x2_epi64(idx, ix / 2), ix % 2);
        //     // NOTE: such overhead is simiar to another store? time * 2
        //     // consider special case for no offset overlap? 
        //     perm_index = _mm512_mask_blend_epi64(1 << dest, perm_index, _mm512_set1_epi64(ix));
        //     // int64_t value = _mm512_mask_reduce_add_epi64(ix_mask, valuev);
        //     // datav = _mm512_mask_blend_epi64(store_mask, datav,
        //     //             _mm512_set1_epi64(value));
        // }
        __m128i tmp_128 = _mm512_extracti64x2_epi64(curr_idx_masks, 0);
        uint64_t dest = _mm_extract_epi64(tmp_128, 0);
        // fprintf(stderr, "dest: %lu, ", dest);
        perm_index = _mm512_mask_blend_epi64(dest, perm_index, _mm512_set1_epi64(0));
        dest = _mm_extract_epi64(tmp_128, 1);
        // fprintf(stderr, "%lu, ", dest);
        perm_index = _mm512_mask_blend_epi64(dest, perm_index, _mm512_set1_epi64(1));
        tmp_128 = _mm512_extracti64x2_epi64(curr_idx_masks, 1);
        dest = _mm_extract_epi64(tmp_128, 0);
        // fprintf(stderr, "%lu, ", dest);
        perm_index = _mm512_mask_blend_epi64(dest, perm_index, _mm512_set1_epi64(2));
        dest = _mm_extract_epi64(tmp_128, 1);
        // fprintf(stderr, "%lu, ", dest);
        perm_index = _mm512_mask_blend_epi64(dest, perm_index, _mm512_set1_epi64(3));
        tmp_128 = _mm512_extracti64x2_epi64(curr_idx_masks, 2);
        dest = _mm_extract_epi64(tmp_128, 0);
        // fprintf(stderr, "%lu, ", dest);
        perm_index = _mm512_mask_blend_epi64(dest, perm_index, _mm512_set1_epi64(4));
        dest = _mm_extract_epi64(tmp_128, 1);
        // fprintf(stderr, "%lu, ", dest);
        perm_index = _mm512_mask_blend_epi64(dest, perm_index, _mm512_set1_epi64(5));
        tmp_128 = _mm512_extracti64x2_epi64(curr_idx_masks, 3);
        dest = _mm_extract_epi64(tmp_128, 0);
        // fprintf(stderr, "%lu, ", dest);
        perm_index = _mm512_mask_blend_epi64(dest, perm_index, _mm512_set1_epi64(6));
        dest = _mm_extract_epi64(tmp_128, 1);
        // fprintf(stderr, "%lu\n", dest);
        perm_index = _mm512_mask_blend_epi64(dest, perm_index, _mm512_set1_epi64(7));

        __m512i datav = _mm512_permutexvar_epi64(perm_index, valuev);
        __mmask8 mask = _mm512_reduce_or_epi64(curr_idx_masks) | 1;

        // fprintf(stderr, "ptr: %lu, mask: %llx\n", (unsigned long)_ptr, mask);
        // PRINT_M512I_EPI64("perm_index", perm_index);
        // PRINT_M512I_EPI64("datav", datav);
        _mm512_mask_store_epi64(_ptr, mask, datav);
        curr_cacheline += 1;
    }
}    

#define GEN_PERM_UPDATE_I32(I) do { \
    dest_mask = _mm512_mask_reduce_add_epi64(1 << I, curr_idx_masks); \
    perm_index = _mm512_mask_blend_epi32(dest_mask, perm_index, _mm512_set1_epi32(I)); \
} while (0)

#define GEN_PERM_I32_2() GEN_PERM_UPDATE_I32(0); GEN_PERM_UPDATE_I32(1)
#define GEN_PERM_I32_3() GEN_PERM_I32_2(); GEN_PERM_UPDATE_I32(2)
#define GEN_PERM_I32_4() GEN_PERM_I32_3(); GEN_PERM_UPDATE_I32(3)
#define GEN_PERM_I32_5() GEN_PERM_I32_4(); GEN_PERM_UPDATE_I32(4)
#define GEN_PERM_I32_6() GEN_PERM_I32_5(); GEN_PERM_UPDATE_I32(5)
#define GEN_PERM_I32_7() GEN_PERM_I32_6(); GEN_PERM_UPDATE_I32(6)
#define GEN_PERM_I32_8() GEN_PERM_I32_7(); GEN_PERM_UPDATE_I32(7)
#define GEN_PERM_I32_9() GEN_PERM_I32_8(); GEN_PERM_UPDATE_I32(8)
#define GEN_PERM_I32_10() GEN_PERM_I32_9(); GEN_PERM_UPDATE_I32(9)
#define GEN_PERM_I32_11() GEN_PERM_I32_10(); GEN_PERM_UPDATE_I32(10)
#define GEN_PERM_I32_12() GEN_PERM_I32_11(); GEN_PERM_UPDATE_I32(11)
#define GEN_PERM_I32_13() GEN_PERM_I32_12(); GEN_PERM_UPDATE_I32(12)
#define GEN_PERM_I32_14() GEN_PERM_I32_13(); GEN_PERM_UPDATE_I32(13)
#define GEN_PERM_I32_15() GEN_PERM_I32_14(); GEN_PERM_UPDATE_I32(14)
#define GEN_PERM_I32_16() GEN_PERM_I32_15(); GEN_PERM_UPDATE_I32(15)

#define GEN_I32_AVX512_LINEAR_DFL_GLOB_VECTOR_STORE(list_len) \
    DFL_FUNC void uint32_t_avx512_linear_dfl_glob_vector_store_ ## list_len(unsigned char* obj, __m512i ptr_list, unsigned long _unused, unsigned long field_off, unsigned long field_size, __m512i valuev) { \
        GET_REAL_PTR_VEC_i32(obj, ptr_list, field_off, field_size, list_len); \
        unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(CACHE_LINE_ALIGNMENT - 1)); \
        unsigned char* _end = obj + field_off + field_size; \
        __m512i offsetv = _mm512_sub_epi32(ptr_list, _mm512_set1_epi32((uintptr_t)aligned_obj)); \
        __m512i cachelinev = _mm512_mask_blend_epi32((1 << list_len) - 1, \
            _mm512_set1_epi32(-1), \
            _mm512_srli_epi32(offsetv, CACHE_LINE_SHIFT)); \
        __m512i idx = _mm512_and_epi32(offsetv, _mm512_set1_epi32(CACHE_LINE_ALIGNMENT - 1)); \
        /* idx / 4 */ \
        idx = _mm512_srli_epi32(idx, TYPE_SHIFT(uint32_t)); \
        __m512i idx_masks = _mm512_sllv_epi32(_mm512_set1_epi32(1), idx); \
        int32_t curr_cacheline = 0; \
        for (volatile unsigned char* _ptr = aligned_obj; _ptr < _end; _ptr += DFL_FIXED_AVX512_STRIDE) { \
            __mmask16 cacheline_masks = _mm512_cmpeq_epi32_mask(cachelinev, _mm512_set1_epi32(curr_cacheline)); \
            __m512i curr_idx_masks = _mm512_maskz_mov_epi32(cacheline_masks, idx_masks); \
            __m512i perm_index = _mm512_set1_epi32(-1); \
            __m128i tmp_128; \
            __mmask16 dest_mask = 0; \
            GEN_PERM_I32_ ## list_len(); \
            __m512i datav = _mm512_permutexvar_epi32(perm_index, valuev); \
            __mmask8 mask = _mm512_reduce_or_epi32(curr_idx_masks) | 1; \
            _mm512_mask_store_epi32(_ptr, mask, datav); \
            curr_cacheline += 1; \
        } \
    }

GEN_I32_AVX512_LINEAR_DFL_GLOB_VECTOR_STORE(2)
GEN_I32_AVX512_LINEAR_DFL_GLOB_VECTOR_STORE(3)
GEN_I32_AVX512_LINEAR_DFL_GLOB_VECTOR_STORE(4)
GEN_I32_AVX512_LINEAR_DFL_GLOB_VECTOR_STORE(5)
GEN_I32_AVX512_LINEAR_DFL_GLOB_VECTOR_STORE(6)
GEN_I32_AVX512_LINEAR_DFL_GLOB_VECTOR_STORE(7)
GEN_I32_AVX512_LINEAR_DFL_GLOB_VECTOR_STORE(8)
GEN_I32_AVX512_LINEAR_DFL_GLOB_VECTOR_STORE(9)
GEN_I32_AVX512_LINEAR_DFL_GLOB_VECTOR_STORE(10)
GEN_I32_AVX512_LINEAR_DFL_GLOB_VECTOR_STORE(11)
GEN_I32_AVX512_LINEAR_DFL_GLOB_VECTOR_STORE(12)
GEN_I32_AVX512_LINEAR_DFL_GLOB_VECTOR_STORE(13)
GEN_I32_AVX512_LINEAR_DFL_GLOB_VECTOR_STORE(14)
GEN_I32_AVX512_LINEAR_DFL_GLOB_VECTOR_STORE(15)
GEN_I32_AVX512_LINEAR_DFL_GLOB_VECTOR_STORE(16)

DFL_FUNC void uint32_t_avx512_linear_dfl_glob_vector_store(unsigned char* obj, __m512i ptr_list, unsigned long list_len, unsigned long field_off, unsigned long field_size, __m512i valuev) {
    GET_REAL_PTR_VEC_i32(obj, ptr_list, field_off, field_size, list_len);
    unsigned char* aligned_obj = (unsigned char*)(((unsigned long)obj + field_off) & ~(CACHE_LINE_ALIGNMENT - 1));
    unsigned char* _end = obj + field_off + field_size;

    __m512i offsetv = _mm512_sub_epi32(ptr_list, _mm512_set1_epi32((uintptr_t)aligned_obj));
    __m512i cachelinev = _mm512_mask_blend_epi32((1 << list_len) - 1,
        _mm512_set1_epi32(-1),
        _mm512_srli_epi32(offsetv, CACHE_LINE_SHIFT));
    __m512i idx = _mm512_and_epi32(offsetv, _mm512_set1_epi32(CACHE_LINE_ALIGNMENT - 1));
    // idx / 4
    idx = _mm512_srli_epi32(idx, TYPE_SHIFT(uint32_t));
    __m512i idx_masks = _mm512_sllv_epi32(_mm512_set1_epi32(1), idx);
    int32_t curr_cacheline = 0;
    for (volatile unsigned char* _ptr = aligned_obj; _ptr < _end; _ptr += DFL_FIXED_AVX512_STRIDE) {
        __mmask16 cacheline_masks = _mm512_cmpeq_epi32_mask(cachelinev, _mm512_set1_epi32(curr_cacheline));
        __m512i curr_idx_masks = _mm512_maskz_mov_epi32(cacheline_masks, idx_masks);
        __m512i perm_index = _mm512_set1_epi32(-1);
        __mmask16 dest_mask = _mm512_mask_reduce_add_epi64(1 << 0, curr_idx_masks);
        perm_index = _mm512_mask_blend_epi32(dest_mask, perm_index, _mm512_set1_epi32(0));
        dest_mask = _mm512_mask_reduce_add_epi64(1 << 1, curr_idx_masks);
        perm_index = _mm512_mask_blend_epi32(dest_mask, perm_index, _mm512_set1_epi32(1));
        dest_mask =  _mm512_mask_reduce_add_epi64(1 << 2, curr_idx_masks);
        perm_index = _mm512_mask_blend_epi32(dest_mask, perm_index, _mm512_set1_epi32(2));
        dest_mask =  _mm512_mask_reduce_add_epi64(1 << 3, curr_idx_masks);
        perm_index = _mm512_mask_blend_epi32(dest_mask, perm_index, _mm512_set1_epi32(3));
        dest_mask = _mm512_mask_reduce_add_epi64(1 << 4, curr_idx_masks);
        perm_index = _mm512_mask_blend_epi32(dest_mask, perm_index, _mm512_set1_epi32(4));
        dest_mask = _mm512_mask_reduce_add_epi64(1 << 5, curr_idx_masks);
        perm_index = _mm512_mask_blend_epi32(dest_mask, perm_index, _mm512_set1_epi32(5));
        dest_mask = _mm512_mask_reduce_add_epi64(1 << 6, curr_idx_masks);
        perm_index = _mm512_mask_blend_epi32(dest_mask, perm_index, _mm512_set1_epi32(6));
        dest_mask = _mm512_mask_reduce_add_epi64(1 << 7, curr_idx_masks);
        perm_index = _mm512_mask_blend_epi32(dest_mask, perm_index, _mm512_set1_epi32(7));
        dest_mask = _mm512_mask_reduce_add_epi64(1 << 8, curr_idx_masks);
        perm_index = _mm512_mask_blend_epi32(dest_mask, perm_index, _mm512_set1_epi32(8));
        dest_mask = _mm512_mask_reduce_add_epi64(1 << 9, curr_idx_masks);
        perm_index = _mm512_mask_blend_epi32(dest_mask, perm_index, _mm512_set1_epi32(9));
        dest_mask = _mm512_mask_reduce_add_epi64(1 << 10, curr_idx_masks);
        perm_index = _mm512_mask_blend_epi32(dest_mask, perm_index, _mm512_set1_epi32(10));
        dest_mask = _mm512_mask_reduce_add_epi64(1 << 11, curr_idx_masks);
        perm_index = _mm512_mask_blend_epi32(dest_mask, perm_index, _mm512_set1_epi32(11));
        dest_mask = _mm512_mask_reduce_add_epi64(1 << 12, curr_idx_masks);
        perm_index = _mm512_mask_blend_epi32(dest_mask, perm_index, _mm512_set1_epi32(12));
        dest_mask = _mm512_mask_reduce_add_epi64(1 << 13, curr_idx_masks);
        perm_index = _mm512_mask_blend_epi32(dest_mask, perm_index, _mm512_set1_epi32(13));
        dest_mask = _mm512_mask_reduce_add_epi64(1 << 14, curr_idx_masks);
        perm_index = _mm512_mask_blend_epi32(dest_mask, perm_index, _mm512_set1_epi32(14));
        dest_mask = _mm512_mask_reduce_add_epi64(1 << 15, curr_idx_masks);
        perm_index = _mm512_mask_blend_epi32(dest_mask, perm_index, _mm512_set1_epi32(15));

        __m512i datav = _mm512_permutexvar_epi32(perm_index, valuev);
        __mmask8 mask = _mm512_reduce_or_epi32(curr_idx_masks) | 1;
        _mm512_mask_store_epi32(_ptr, mask, datav);
        curr_cacheline += 1;
    }
}


#endif /* __AVX512F__ */

#if defined(__AVX2__)
#define DFL_AVX2_GATHER_GLOB_LOAD(type) DFL_FUNC type type ## _avx2_gather_dfl_glob_load(unsigned char* obj, unsigned char* ptr, unsigned long field_off, unsigned long field_size) {  \
    return (type) uint32_t_avx2_gather_dfl_glob_load(obj, ptr, field_off, field_size); \
}
#define DFL_AVX2_LINEAR_GLOB_LOAD(type) DFL_FUNC type type ## _avx2_linear_dfl_glob_load(unsigned char* obj, unsigned char* ptr, unsigned long field_off, unsigned long field_size) {  \
    return (type) uint64_t_avx2_linear_dfl_glob_load(obj, ptr, field_off, field_size); \
}

#define DFL_AVX2_LINEAR_OBJ_LOAD(type) DFL_FUNC type type ## _avx2_linear_dfl_obj_load(dfl_obj_list_head head, unsigned char* ptr, unsigned long field_off, unsigned long field_size) {  \
    return (type) uint64_t_avx2_linear_dfl_obj_load(head, ptr, field_off, field_size); \
}

#define DFL_AVX2_GATHER_OBJ_LOAD(type) DFL_FUNC type type ## _avx2_gather_dfl_obj_load(dfl_obj_list_head head, unsigned char* ptr, unsigned long field_off, unsigned long field_size) {  \
    return (type) uint32_t_avx2_gather_dfl_obj_load(head, ptr, field_off, field_size); \
}

DFL_AVX2_GATHER_GLOB_LOAD(uint16_t)
DFL_AVX2_GATHER_GLOB_LOAD(uint8_t)

DFL_AVX2_GATHER_OBJ_LOAD(uint16_t)
DFL_AVX2_GATHER_OBJ_LOAD(uint8_t)

DFL_AVX2_LINEAR_GLOB_LOAD(uint32_t)
DFL_AVX2_LINEAR_GLOB_LOAD(uint16_t)
DFL_AVX2_LINEAR_GLOB_LOAD(uint8_t)

DFL_AVX2_LINEAR_OBJ_LOAD(uint32_t)
DFL_AVX2_LINEAR_OBJ_LOAD(uint16_t)
DFL_AVX2_LINEAR_OBJ_LOAD(uint8_t)

DFL_AVX2_LINEAR_GLOB_STORE(uint32_t)
DFL_AVX2_LINEAR_GLOB_STORE(uint16_t)
DFL_AVX2_LINEAR_GLOB_STORE(uint8_t)
#endif /* __AVX2__ */

DFL_OBJ_LOAD(uint128_t)
DFL_OBJ_LOAD(uint64_t)
DFL_OBJ_LOAD(uint32_t)
DFL_OBJ_LOAD(uint16_t)
DFL_OBJ_LOAD(uint8_t)

DFL_OBJ_STORE(uint128_t)
DFL_OBJ_STORE(uint64_t)
DFL_OBJ_STORE(uint32_t)
DFL_OBJ_STORE(uint16_t)
DFL_OBJ_STORE(uint8_t)

DFL_GLOB_LOAD(uint128_t)
DFL_GLOB_LOAD(uint64_t)
DFL_GLOB_LOAD(uint32_t)
DFL_GLOB_LOAD(uint16_t)
DFL_GLOB_LOAD(uint8_t)

DFL_GLOB_STORE(uint128_t)
DFL_GLOB_STORE(uint64_t)
DFL_GLOB_STORE(uint32_t)
DFL_GLOB_STORE(uint16_t)
DFL_GLOB_STORE(uint8_t)

DFL_SINGLE_GLOB_LOAD(uint64_t)
DFL_SINGLE_GLOB_LOAD(uint32_t)
DFL_SINGLE_GLOB_LOAD(uint16_t)
DFL_SINGLE_GLOB_LOAD(uint8_t)

DFL_SINGLE_OBJ_LOAD(uint64_t)
DFL_SINGLE_OBJ_LOAD(uint32_t)
DFL_SINGLE_OBJ_LOAD(uint16_t)
DFL_SINGLE_OBJ_LOAD(uint8_t)

DFL_SINGLE_GLOB_STORE(uint64_t)
DFL_SINGLE_GLOB_STORE(uint32_t)
DFL_SINGLE_GLOB_STORE(uint16_t)
DFL_SINGLE_GLOB_STORE(uint8_t)

DFL_SINGLE_OBJ_STORE(uint64_t)
DFL_SINGLE_OBJ_STORE(uint32_t)
DFL_SINGLE_OBJ_STORE(uint16_t)
DFL_SINGLE_OBJ_STORE(uint8_t)

// DFL_FUNC_NOINLINE void dfl_memcpy(void *dest, void *src, size_t n, bool isvolatile) 
// {
//     DEBUG("INTRINSICS memcpy(%p, %p, %ld) taken: %d\n", dest, src, n, taken);
// #if CFL_EXPAND_INTRINSCS
//     // Typecast src and dest addresses to (char *)
//     char *csrc = (char *)src;
//     char *cdest = (char *)dest;

//     // Copy contents of src[] to dest[]
//     for (int i=0; i<n; i++)
//         cdest[i] = csrc[i];
// #else
//     size_t new_size = taken? n : (n>2048? 0 : n);
//     memcpy(cfl_ptr_wrap(dest), cfl_ptr_wrap(src), new_size);
// #endif
//     CFL_UNUSED(isvolatile);
// }

// Optimized version of memcpy to transfer a single field from an object to another
// assumes: dfield_size == sfield_size == n (before being wrapped with dfl_wrap_var) and src/dest == s/dobj + s/dfield_off
DFL_FUNC_NOINLINE void dfl_memcpy_field_glob(void *dest, void *src, volatile size_t n, bool isvolatile, unsigned char* dobj, unsigned long dfield_off, unsigned long dfield_size, unsigned char* sobj, unsigned long sfield_off, unsigned long sfield_size)
{
    DEBUG("INTRINSICS memcpy_field(%p, %p, %ld) - dobj: %p - doff:%lu, dsize:%lu - sobj: %p - soff:%lu, ssize:%lu - taken: %d\n", dest, src, n, dobj, dfield_off, dfield_size, sobj, sfield_off, sfield_size, taken);
    unsigned char* _dptr =  dobj + dfield_off;
    unsigned char* _sptr =  sobj + sfield_off;

    // Copy contents of src[] to dest[]
    #pragma nounroll
    for (unsigned long i = 0; i < dfield_size; ++i) {
        // n is zero when taken == 0
        bool condition = (n > 0);
        volatile unsigned char _prev_val = *(volatile unsigned char*)_dptr;
        volatile unsigned char _new_val = *(volatile unsigned char*)_sptr;
        *(volatile unsigned char*)_dptr = condition? _new_val : _prev_val;
        ++_dptr;
        ++_sptr;
    }
}

// dest: glob, src: obj
DFL_FUNC_NOINLINE void dfl_memcpy_field_glob_obj(void *dest, void *src, volatile size_t n, bool isvolatile, unsigned char* dobj, unsigned long dfield_off, unsigned long dfield_size, dfl_obj_list_head shead, unsigned long sfield_off, unsigned long sfield_size)
{
    unsigned char* _dptr =  dobj + dfield_off;

    while (shead) {
        unsigned char* sobj = (unsigned char*)shead->data;
        unsigned char* _sptr =  sobj + sfield_off;
        DEBUG("INTRINSICS memcpy_field(%p, %p, %ld) - dobj: %p - doff:%lu, dsize:%lu - sobj: %p - soff:%lu, ssize:%lu - taken: %d\n", dest, src, n, dobj, dfield_off, dfield_size, sobj, sfield_off, sfield_size, taken);

        // Copy contents of src[] to dest[]
        #pragma nounroll
        for (unsigned long i = 0; i < dfield_size; ++i) {
            // n is zero when taken == 0
            bool condition = (n > 0);
            volatile unsigned char _prev_val = *(volatile unsigned char*)_dptr;
            volatile unsigned char _new_val = *(volatile unsigned char*)_sptr;
            *(volatile unsigned char*)_dptr = condition? _new_val : _prev_val;
            ++_dptr;
            ++_sptr;
        }
        shead = shead->next;
    }
}

#if defined(__AVX2__)
// Optimized version of memcpy to transfer a single field from an object to another
// assumes: dfield_size == sfield_size == n (before being wrapped with dfl_wrap_var) and src/dest == s/dobj + s/dfield_off
DFL_FUNC_NOINLINE void dfl_memcpy_field_glob_avx(void *dest, void *src, volatile size_t n, bool isvolatile, unsigned char* dobj, unsigned long dfield_off, unsigned long dfield_size, unsigned char* sobj, unsigned long sfield_off, unsigned long sfield_size)
{
    DEBUG("INTRINSICS memcpy_field(%p, %p, %ld) - dobj: %p - doff:%lu, dsize:%lu - sobj: %p - soff:%lu, ssize:%lu - taken: %d\n", dest, src, n, dobj, dfield_off, dfield_size, sobj, sfield_off, sfield_size, taken);
    unsigned char* _dptr =  dobj + dfield_off;
    unsigned char* _sptr =  sobj + sfield_off;

    // Copy contents of src[] to dest[]
    unsigned long i = 0;
    for (; i < dfield_size-AVX2_LINESIZE; i+= AVX2_LINESIZE) {
        // n is zero when taken == 0
        __m256i mask = _mm256_set1_epi8((n > 0)? 0xff : 0);
        __m256i _prev_val = _mm256_loadu_si256((__m256i *)_dptr);
        __m256i _new_val = _mm256_loadu_si256((__m256i *)_sptr);
        _new_val = _mm256_blendv_epi8(_prev_val, _new_val, mask);
        _mm256_storeu_si256((__m256i *)_dptr, _new_val);
        _dptr += AVX2_LINESIZE;
        _sptr += AVX2_LINESIZE;
    }

    // copy the remaining (can do this since dfield_size is not secret)
    #pragma nounroll
    for (; i < dfield_size; ++i) {
        // n is zero when taken == 0
        bool condition = (n > 0);
        volatile unsigned char _prev_val = *(volatile unsigned char*)_dptr;
        volatile unsigned char _new_val = *(volatile unsigned char*)_sptr;
        *(volatile unsigned char*)_dptr = condition? _new_val : _prev_val;
        ++_dptr;
        ++_sptr;
    }
}

// dst: glob, src: obj
DFL_FUNC_NOINLINE void dfl_memcpy_field_glob_obj_avx(void *dest, void *src, volatile size_t n, bool isvolatile, unsigned char* dobj, unsigned long dfield_off, unsigned long dfield_size, dfl_obj_list_head shead, unsigned long sfield_off, unsigned long sfield_size)
{
    while (shead) {
        unsigned char* sobj = (unsigned char*)shead->data;

        DEBUG("INTRINSICS memcpy_field(%p, %p, %ld) - dobj: %p - doff:%lu, dsize:%lu - sobj: %p - soff:%lu, ssize:%lu - taken: %d\n", dest, src, n, dobj, dfield_off, dfield_size, sobj, sfield_off, sfield_size, taken);
        unsigned char* _dptr =  dobj + dfield_off;
        unsigned char* _sptr =  sobj + sfield_off;

        // Copy contents of src[] to dest[]
        unsigned long i = 0;
        for (; i < dfield_size-AVX2_LINESIZE; i+= AVX2_LINESIZE) {
            // n is zero when taken == 0
            __m256i mask = _mm256_set1_epi8((n > 0)? 0xff : 0);
            __m256i _prev_val = _mm256_loadu_si256((__m256i *)_dptr);
            __m256i _new_val = _mm256_loadu_si256((__m256i *)_sptr);
            _new_val = _mm256_blendv_epi8(_prev_val, _new_val, mask);
            _mm256_storeu_si256((__m256i *)_dptr, _new_val);
            _dptr += AVX2_LINESIZE;
            _sptr += AVX2_LINESIZE;
        }

        // copy the remaining (can do this since dfield_size is not secret)
        #pragma nounroll
        for (; i < dfield_size; ++i) {
            // n is zero when taken == 0
            bool condition = (n > 0);
            volatile unsigned char _prev_val = *(volatile unsigned char*)_dptr;
            volatile unsigned char _new_val = *(volatile unsigned char*)_sptr;
            *(volatile unsigned char*)_dptr = condition? _new_val : _prev_val;
            ++_dptr;
            ++_sptr;
        }
        shead = shead->next;
    }
}
#endif /* __AVX2__ */

DFL_FUNC_NOINLINE void dfl_memcpy_glob(void *dest, void *src, size_t n, bool isvolatile, unsigned char* dobj, unsigned long dfield_off, unsigned long dfield_size, unsigned char* sobj, unsigned long sfield_off, unsigned long sfield_size)
{
    DEBUG("INTRINSICS memcpy(%p, %p, %ld) - dobj: %p - doff:%lu, dsize:%lu - sobj: %p - soff:%lu, ssize:%lu - taken: %d\n", dest, src, n, dobj, dfield_off, dfield_size, sobj, sfield_off, sfield_size, taken);
    //type cast from void* to char*
    unsigned char *sptr = (unsigned char*) src;
    unsigned char *dptr = (unsigned char*) dest;

    unsigned char* _dstart =  dobj + dfield_off;
    unsigned char* _dend =  dobj + dfield_off + dfield_size;

    unsigned char* _sstart =  sobj + sfield_off;
    unsigned char* _send =  sobj + sfield_off + sfield_size;

    // Copy contents of src[] to dest[]
    for(volatile unsigned char* _sptr = _sstart; _sptr < _send; ++_sptr) {
        volatile unsigned char _new_val = *(volatile unsigned char*)_sptr;
        for(volatile unsigned char* _dptr = _dstart; _dptr < _dend; ++_dptr) {
            volatile unsigned char _prev_val = *(volatile unsigned char*)_dptr;

            // Check if we should write here
            bool condition = (_dptr == dptr) && (_sptr == sptr) && (n > 0);

            *(volatile unsigned char*)_dptr = condition? _new_val : _prev_val;
            dptr = condition? dptr + 1 : dptr;
            sptr = condition? sptr + 1 : sptr;
            n    = condition? n - 1 : n;
        }
    }
    DFL_UNUSED(isvolatile);
}

// dst: glob, src: obj
DFL_FUNC_NOINLINE void dfl_memcpy_glob_obj(void *dest, void *src, size_t n, bool isvolatile, unsigned char* dobj, unsigned long dfield_off, unsigned long dfield_size, dfl_obj_list_head shead, unsigned long sfield_off, unsigned long sfield_size)
{
    //type cast from void* to char*
    unsigned char *sptr = (unsigned char*) src;
    unsigned char *dptr = (unsigned char*) dest;

    unsigned char* _dstart =  dobj + dfield_off;
    unsigned char* _dend =  dobj + dfield_off + dfield_size;

    while (shead) {
        unsigned char* sobj = (unsigned char*)shead->data;
        unsigned char* _sstart =  sobj + sfield_off;
        unsigned char* _send =  sobj + sfield_off + sfield_size;

        DEBUG("INTRINSICS memcpy(%p, %p, %ld) - dobj: %p - doff:%lu, dsize:%lu - sobj: %p - soff:%lu, ssize:%lu - taken: %d\n", dest, src, n, dobj, dfield_off, dfield_size, sobj, sfield_off, sfield_size, taken);

        // Copy contents of src[] to dest[]
        for(volatile unsigned char* _sptr = _sstart; _sptr < _send; ++_sptr) {
            volatile unsigned char _new_val = *(volatile unsigned char*)_sptr;
            for(volatile unsigned char* _dptr = _dstart; _dptr < _dend; ++_dptr) {
                volatile unsigned char _prev_val = *(volatile unsigned char*)_dptr;

                // Check if we should write here
                bool condition = (_dptr == dptr) && (_sptr == sptr) && (n > 0);

                *(volatile unsigned char*)_dptr = condition? _new_val : _prev_val;
                dptr = condition? dptr + 1 : dptr;
                sptr = condition? sptr + 1 : sptr;
                n    = condition? n - 1 : n;
            }
        }
        shead = shead->next;
    }
    DFL_UNUSED(isvolatile);
}

DFL_FUNC_NOINLINE void dfl_memset_glob(void* str, unsigned char ch, size_t n, bool isvolatile, unsigned char* obj, unsigned long field_off, unsigned long field_size)
{
    DEBUG("INTRINSICS memset(%p, %hhd, %ld) - obj: %p - off:%lu, size:%lu - taken: %d\n", str, ch, n, obj, field_off, field_size, taken);
    //type cast the str from void* to char*
    unsigned char *ptr = (unsigned char*) str;
    unsigned char* _end =  obj + field_off + field_size;
    volatile unsigned char _ch = ch;
    //fill "n" elements/blocks with ch
    for(volatile unsigned char* _ptr = obj + field_off; _ptr < _end; ++_ptr) {
        volatile unsigned char _prev_val = *(volatile unsigned char*)_ptr;
        bool condition = (_ptr >= ptr) && (_ptr < ptr + n);
        *(volatile unsigned char*)_ptr = condition? _ch : _prev_val;
    }
    DFL_UNUSED(isvolatile);
}

#if defined(__AVX2__)
DFL_VAR __attribute__ (( __aligned__(64) )) unsigned char masks_pre[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};
DFL_VAR __attribute__ (( __aligned__(64) )) unsigned char masks_post[] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
};
void print256_num(char* s, __m256i var)
{
    uint64_t val[4];
    memcpy(val, &var, sizeof(val));
    printf("%s: %lx %lx %lx %lx \n", s, 
           val[0], val[1], val[2], val[3]);
}
DFL_FUNC_NOINLINE void dfl_memset_glob_avx(void* str, unsigned char ch, size_t n, bool isvolatile, unsigned char* obj, unsigned long field_off, unsigned long field_size)
{
    DEBUG("INTRINSICS memset(%p, %hhd, %ld) - obj: %p - off:%lu, size:%lu - taken: %d\n", str, ch, n, obj, field_off, field_size, taken);
    // DPRINT("INTRINSICS memset(%p, %hhd, %ld) - obj: %p - off:%lu, size:%lu\n", str, ch, n, obj, field_off, field_size);
    //type cast the str from void* to char*
    unsigned char* ptr = (unsigned char*) str;
    // first and last AVX2 line to fill
    unsigned char* _start =  (unsigned char*)(((unsigned long)obj + field_off) & ~(AVX2_LINESIZE-1uL));
    unsigned char* _end =  (unsigned char*)(((unsigned long)_start + field_size+AVX2_LINESIZE-1) & ~(AVX2_LINESIZE-1uL));

    // `line_ptr_start` is the first AVX2 line where the ptr resides, either full or partial
    unsigned char* line_ptr_start = (unsigned char*) (((unsigned long) ptr) & ~(AVX2_LINESIZE-1uL));
    // `line_ptr_end` is the ending AVX2 line that should be partially filled. If 
    // ptr+n is aligned to a line, line_ptr_end will point to an empty line that will not be filled
    unsigned char* line_ptr_end   = (unsigned char*) (((unsigned long)(ptr+n+AVX2_LINESIZE)) & ~(AVX2_LINESIZE-1uL));

    // masks
    // __m256i vnone = _mm256_setzero_si256();
    // __m256i vfull = _mm256_set1_epi8(0xff);
    
    __m256i chv = _mm256_set1_epi8(ch);

    // Build masks to mask the first and last write that will not take a whole AVX2 line
    // We will load the mask from memory to efficiently build it since AVX2 does 
    // not support horizontal shifts of arbitrary amounts
    // This is oblivious only with respect to an attacker which obseves cache line 
    // accesses, since the variables span a single cache line each.
    // In case DFL_STRIDE<64 we should load the mask using striding helpers
    __m256i mask_pre  = _mm256_loadu_si256((__m256i *) &masks_pre[32 - (((unsigned long) ptr) & (AVX2_LINESIZE-1uL))]); // avx2_shift_right(mask_full, xxx);
    __m256i mask_post = _mm256_loadu_si256((__m256i *)&masks_post[32 - (((unsigned long) ptr+n) & (AVX2_LINESIZE-1uL))]); // avx2_shift_left(mask_full, xxx);

    //fill the blocks with ch
    for(volatile unsigned char* _ptr = _start; _ptr < _end; _ptr += AVX2_LINESIZE) {
        __m256i _prev_val = _mm256_load_si256((__m256i *)_ptr);

        // Generate the masks conditions to select the right mask
        // The masks will select the cells that have to be written or left unchanged
        // mask_condition will be 0xff..ff when the mask must not have any effect in the final `mask`
        __m256i mask_none_condition = _mm256_set1_epi8((_ptr < line_ptr_start || _ptr >= line_ptr_end)? 0 : 0xff);
        __m256i mask_pre_condition = _mm256_set1_epi8(((_ptr >= line_ptr_start) && (_ptr < line_ptr_start + AVX2_LINESIZE))? 0 : 0xff);
        // __m256i mask_full_condition = _mm256_set1_epi8(((_ptr >= line_ptr_start + AVX2_LINESIZE) && (_ptr < line_ptr_end - AVX2_LINESIZE))? 0 : 0xff);
        __m256i mask_post_condition = _mm256_set1_epi8(((_ptr >= line_ptr_end - AVX2_LINESIZE) && (_ptr < line_ptr_end))? 0 : 0xff);

        // Reset masks to 0xff.ff when they should not be applied (so default is full write)
        __m256i mask_none_filtered = mask_none_condition; // mask_none is always zero
        __m256i mask_pre_filtered  = _mm256_or_si256(mask_pre_condition, mask_pre);
        __m256i mask_post_filtered = _mm256_or_si256(mask_post_condition, mask_post);

        // Combine all the masks together to create the mask we will apply to the blend.
        // By default we perform a full write and the filtered masks select which bytes
        // should not be updated
        __m256i mask = _mm256_and_si256(mask_none_filtered, mask_pre_filtered);
        mask         = _mm256_and_si256(mask, mask_post_filtered);

        __m256i _new_val = _mm256_blendv_epi8(_prev_val, chv, mask);

        _mm256_store_si256((__m256i *)_ptr, _new_val);
    }
    DFL_UNUSED(isvolatile);
}
#endif /* __AVX2__ */

DFL_FUNC_NOINLINE void dfl_memset_obj(void* str, unsigned char ch, size_t n, bool isvolatile, dfl_obj_list_head head, unsigned long field_off, unsigned long field_size)
{
    //type cast the str from void* to char*
    unsigned char *ptr = (unsigned char*) str;
    volatile unsigned char _ch = ch;
    while (head) {
        unsigned char* obj = (unsigned char*)head->data;
        DEBUG("INTRINSICS memset(%p, %hhd, %ld) - obj: %p - off:%lu, size:%lu - taken: %d\n", str, ch, n, obj, field_off, field_size, taken);
        unsigned char* _end =  obj + field_off + field_size;
        //fill "n" elements/blocks with ch
        for(volatile unsigned char* _ptr = obj + field_off; _ptr < _end; ++_ptr) {
            volatile unsigned char _prev_val = *(volatile unsigned char*)_ptr;
            bool condition = (_ptr >= ptr) && (_ptr < ptr + n);
            *(volatile unsigned char*)_ptr = condition? _ch : _prev_val;
        }
        head = head->next;
    }
    DFL_UNUSED(isvolatile);
}

#if defined(__AVX2__)
DFL_FUNC_NOINLINE void dfl_memset_obj_avx(void* str, unsigned char ch, size_t n, bool isvolatile, dfl_obj_list_head head, unsigned long field_off, unsigned long field_size)
{
    DEBUG("INTRINSICS memset(%p, %hhd, %ld) - obj: %p - off:%lu, size:%lu - taken: %d\n", str, ch, n, obj, field_off, field_size, taken);
    // DPRINT("INTRINSICS memset(%p, %hhd, %ld) - obj: %p - off:%lu, size:%lu\n", str, ch, n, obj, field_off, field_size);
    //type cast the str from void* to char*
    unsigned char* ptr = (unsigned char*) str;

    // `line_ptr_start` is the first AVX2 line where the ptr resides, either full or partial
    unsigned char* line_ptr_start = (unsigned char*) (((unsigned long) ptr) & ~(AVX2_LINESIZE-1uL));
    // `line_ptr_end` is the ending AVX2 line that should be partially filled. If 
    // ptr+n is aligned to a line, line_ptr_end will point to an empty line that will not be filled
    unsigned char* line_ptr_end   = (unsigned char*) (((unsigned long)(ptr+n+AVX2_LINESIZE)) & ~(AVX2_LINESIZE-1uL));

    // masks
    // __m256i vnone = _mm256_setzero_si256();
    // __m256i vfull = _mm256_set1_epi8(0xff);
    
    __m256i chv = _mm256_set1_epi8(ch);

    // Build masks to mask the first and last write that will not take a whole AVX2 line
    // We will load the mask from memory to efficiently build it since AVX2 does 
    // not support horizontal shifts of arbitrary amounts
    // This is oblivious only with respect to an attacker which obseves cache line 
    // accesses, since the variables span a single cache line each.
    // In case DFL_STRIDE<64 we should load the mask using striding helpers
    __m256i mask_pre  = _mm256_loadu_si256((__m256i *) &masks_pre[32 - (((unsigned long) ptr) & (AVX2_LINESIZE-1uL))]); // avx2_shift_right(mask_full, xxx);
    __m256i mask_post = _mm256_loadu_si256((__m256i *)&masks_post[32 - (((unsigned long) ptr+n) & (AVX2_LINESIZE-1uL))]); // avx2_shift_left(mask_full, xxx);

    while (head) {
        unsigned char* obj = (unsigned char*)head->data;
        // first and last AVX2 line to fill
        unsigned char* _start =  (unsigned char*)(((unsigned long)obj + field_off) & ~(AVX2_LINESIZE-1uL));
        unsigned char* _end =  (unsigned char*)(((unsigned long)_start + field_size+AVX2_LINESIZE-1) & ~(AVX2_LINESIZE-1uL));

        //fill the blocks with ch
        for(volatile unsigned char* _ptr = _start; _ptr < _end; _ptr += AVX2_LINESIZE) {
            __m256i _prev_val = _mm256_load_si256((__m256i *)_ptr);

            // Generate the masks conditions to select the right mask
            // The masks will select the cells that have to be written or left unchanged
            // mask_condition will be 0xff..ff when the mask must not have any effect in the final `mask`
            __m256i mask_none_condition = _mm256_set1_epi8((_ptr < line_ptr_start || _ptr >= line_ptr_end)? 0 : 0xff);
            __m256i mask_pre_condition = _mm256_set1_epi8(((_ptr >= line_ptr_start) && (_ptr < line_ptr_start + AVX2_LINESIZE))? 0 : 0xff);
            // __m256i mask_full_condition = _mm256_set1_epi8(((_ptr >= line_ptr_start + AVX2_LINESIZE) && (_ptr < line_ptr_end - AVX2_LINESIZE))? 0 : 0xff);
            __m256i mask_post_condition = _mm256_set1_epi8(((_ptr >= line_ptr_end - AVX2_LINESIZE) && (_ptr < line_ptr_end))? 0 : 0xff);

            // Reset masks to 0xff.ff when they should not be applied (so default is full write)
            __m256i mask_none_filtered = mask_none_condition; // mask_none is always zero
            __m256i mask_pre_filtered  = _mm256_or_si256(mask_pre_condition, mask_pre);
            __m256i mask_post_filtered = _mm256_or_si256(mask_post_condition, mask_post);

            // Combine all the masks together to create the mask we will apply to the blend.
            // By default we perform a full write and the filtered masks select which bytes
            // should not be updated
            __m256i mask = _mm256_and_si256(mask_none_filtered, mask_pre_filtered);
            mask         = _mm256_and_si256(mask, mask_post_filtered);

            __m256i _new_val = _mm256_blendv_epi8(_prev_val, chv, mask);

            _mm256_store_si256((__m256i *)_ptr, _new_val);
        }
        head = head->next;
    }
    DFL_UNUSED(isvolatile);
}
#endif /* __AVX2__ */

// DFL_FUNC_NOINLINE void dfl_memmove(void *dest, void *src, size_t n, bool isvolatile)
// {
//     DEBUG("INTRINSICS memmove(%p, %p, %ld) taken: %d\n", dest, src, n, taken);
// #if CFL_EXPAND_INTRINSCS
//     register char *dp = dest;
//     register char const *sp = src;
//     if(dp < sp) {
//         while(n-- > 0)
//             *dp++ = *sp++;
//     } else {
//         dp += n;
//         sp += n;
//         while(n-- > 0)
//             *--dp = *--sp;
//     }
// #else
//     size_t new_size = taken? n : (n>2048? 0 : n);
//     memmove(cfl_ptr_wrap(dest), cfl_ptr_wrap(src), new_size);
// #endif
//     CFL_UNUSED(isvolatile);
// }