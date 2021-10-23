#ifndef NANOJVM_JVM_H
#define NANOJVM_JVM_H
#include "core.h"

enum { DOVAR = 0xCA, DOLIT, DOSTR }; 	/// Forth opcodes
///
/// VM namespace functions
///
char *next_word();
char *scan(char c);
void words();
void ss_dump();
void mem_dump(IU p0, DU sz);
///
/// macros for parameter memory access
///
#define STR(a) ((char*)&pmem[a])  /** fetch string pointer to parameter memory */
#define HERE   (pmem.idx)         /** current parameter memory index           */
///
/// Memory Pool Manager
/// Note:
///    ucode is fused into vt for now, it can stay in ROM
///
struct Pool {
    List<U8, HEAP_SZ>   pmem;          /// parameter memory i.e. heap
    List<Method, VT_SZ> dict;          /// dictionary i.e. member function pool
    List<DU, CONST_SZ>  cnst;          /// constant pool
    IU cls_root = 0;

    int register_class(const char *name, int sz, Method *vt) {
        add_iu(cls_root);              /// previous class link
        add_du(dict.idx);              /// vtbl index
        add_du(sz);                    /// vtbl sz
        add_str(name);                 /// class name
        for (int i=0; i<sz; i++) {
            dict.push(vt[i]);
        }
        cls_root = pmem.idx;           /// class root
    }
    void colon(const char *name, U8 cls_id=0) {
        char *nfa = STR(pmem.idx);     /// current pmem pointer
        add_str(name);                 /// setup raw name field
        Method m(nfa, NULL);
        m.def = 1;                     /// specify a colon word
        m.cid = cls_id;                /// class id
        m.pfa = HERE;                  /// capture code field index
        dict.push(m);                  /// deep copy Code struct into dictionary
    };
    int get_method(const char *s) {
        for (int i=0; i<dict.idx; i++) {
            if (strcmp(s, dict[i].name)==0) return i;
        }
        return -1;
    }
    int get_class(IU w) { return 0; }  /// CC:TODO
    U8  *get_pfa(IU w)  { return (U8*)&pmem[dict[w].pfa]; }

    template <typename T>
    int add_const(T *v) { return cnst.push(*(DU*)v); }
    ///
    /// compiler methods
    ///
    void add_iu(IU i) { pmem.push((U8*)&i, sizeof(IU)); }
    void add_du(DU v) { pmem.push((U8*)&v, sizeof(DU)); }
    void add_str(const char *s) { int sz = STRLEN(s); pmem.push((U8*)s,  sz); }
};
extern Pool gPool;

#endif // NANOJVM_JVM_H

