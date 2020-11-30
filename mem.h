#ifndef MEM_INCLUDED
#define MEM_INCLUDED

extern void Mem_ensure_allocated(void *ptr);
extern void *Mem_alloc (long nbytes);
extern void *Mem_calloc(long count, long nbytes);
extern void Mem_free(void *ptr);
extern void *Mem_resize(void *ptr, long nbytes);

#define ALLOC(nbytes) Mem_alloc((nbytes))
#define CALLOC(count, nbytes) Mem_calloc((count), (nbytes))
#define NEW(p) ((p) = ALLOC((long)sizeof *(p)))
#define NEW0(p) ((p) = CALLOC(1, (long)sizeof *(p)))
#define FREE(ptr) ((void)(Mem_free((ptr)), (ptr) = NULL))
#define RESIZE(ptr, nbytes) ((ptr) = Mem_resize((ptr), (nbytes)))

#endif