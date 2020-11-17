#ifndef LIST_INCLUDED
#define LIST_INCLUDED

typedef struct List_T *List_T;
struct List_T {
	List_T rest;
	void *first;
};

extern List_T List_push   (List_T list, void *x);
extern List_T List_append (List_T list, List_T tail);
extern List_T List_copy   (List_T list);
extern List_T List_pop    (List_T list, void **x);
extern List_T List_reverse(List_T list);
extern int    List_length (List_T list);
extern void   List_free   (List_T *list);
extern void   List_map    (List_T list, void apply(void **x, void *cl), void *cl);
extern void **List_toArray(List_T list, void *end);

#endif