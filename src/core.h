#ifndef NANOJVM_CORE_H
#define NANOJVM_CORE_H
#include "common.h"         /// common types and configuration
#include "loader.h"         /// Java class loader
///
/// Thread class
///
struct Thread {
    List<DU, SS_SZ>  ss;    /// data stack
    Loader &J;              /// Java class loader
    U8     *M0    = NULL;   /// cached base address of memory pool

    bool  compile = false;  /// compile flag
    bool  wide    = false;  /// wide flag
    DU    base    = 10;     /// radix
    IU    cls     = 0;		/// current class, =~ Forth context
    DU    TOS     = -1;     /// top of stack
    IU    IP      = 0;      /// instruction pointer (program counter)
    U16   SP      = 0;      /// local stack frame index

    Thread(Loader &ldr) : J(ldr) {}

    void init(U8 *mem, IU cx) { M0 = mem; cls = cx; }
    ///
    /// VM Execution Unit
    ///
    void dispatch(IU mx, U16 nparm=0);   /// instruction dispatcher dispatch
    ///
    /// Java core
    ///
    void java_new();                     /// instantiate Java object
    void java_call(IU j, U16 nparm=0);   /// execute Java method
    void invoke(U16 itype);              /// invoke type: 0:virtual, 1:special, 2:static, 3:interface, 4:dynamic
    ///
    /// class and instance variable access
    ///
    DU   *cls_var(U16 j);
    DU   *inst_var(IU ox, U16 j);
    ///
    /// Java class file byte fetcher
    ///
    U8   fetch()        { return J.getU8(IP++); }
    U16  fetch2()       { U16 n = J.getU16(IP); IP+=2; return n; }
    U32  fetch4()       { U32 n = J.getU32(IP); IP+=4; return n; }
    ///
    /// branching ops
    ///
    void ret()          { IP = 0; }
    void jmp()          { IP += J.getU16(IP) - 1; }
    void cjmp(bool f)   { IP += f ? J.getU16(IP) - 1 : sizeof(U16); }
    ///
    /// stack ops
    ///
    void push(DU v)     { ss.push(TOS); TOS = v; }
    DU   pop()          { DU n = TOS; TOS = ss.pop(); return n; }
    ///
    /// local variable access
    ///
#if RANGE_CHECK
    void iinc(U8 i, S8 v)  {
    	if ((SP+i) > ss.idx) throw "ERR: iinc > ss.idx";
    	((SP+i)==ss.idx) ? TOS += v : ss[SP + i] += v;
    }
    template<typename T>
    T    load(U16 i, T n)  {
    	if ((SP+i) > ss.idx) throw "ERR: load > ss.idx";
    	return ((SP+i)==ss.idx) ? TOS : *(T*)&ss[SP + i];
    }
    template<typename T>
    void store(U16 i, T n) {
    	if ((SP+i) > ss.idx) throw "ERR: store > ss.idx";
    	((SP+i)==ss.idx) ? TOS = n : *(T*)&ss[SP + i] = n;
    }
#else
    void iinc(U8 i, S8 v)  { ((SP+i)==ss.idx) ? TOS += v : ss[SP + i] += v; }
    template<typename T>
    T    load(U16 i, T n)  { return ((SP+i)==ss.idx) ? TOS : *(T*)&ss[SP + i]); }
    template<typename T>
    void store(U16 i, T n) { ((SP+i)==ss.idx) ? TOS = n : *(T*)&ss[SP + i] = n; }
#endif // RANGE_CHECK
};
typedef void (*fop)(Thread&); /// opcode function pointer
///
/// Method class
///
#define FLAG_IMMD   0x1
#define FLAG_FORTH  0x2
#define FLAG_JAVA   0x4
struct Method {
    const char *name = 0;     /// for debugging, TODO (in const_pool)
#if METHOD_PACKED
    union {
        fop   xt = 0;         /// function pointer (or decayed lambda)
        struct {
            U32 flag: 3;
            U32 rsv:  29;
        };
    };
#else
    fop   xt   = 0;           /// function pointer (or decayed lambda)
    U16   flag = 0;
#endif
    Method(const char *n, fop f, U32 im=0) : name(n), xt(f), flag(im) {}
};
///
/// Word - shared struct for Class and Method
///   class list - linked list of words, dict[cls_root], pfa => next_class
///   vtable     - linked list of words, dict[class.pfa], pfa => next_method
///
#define CLS_SUPR        0
#define CLS_INTF        2
#define CLS_VT          4
#define CLS_CVSZ        6
#define CLS_IVSZ        8
#define CLS_CV          10
struct Word {                /// 4-byte header
    IU  lfa;                 /// link field to previous word
    U8  len;                 /// name of method

    U8  immd:   1;           /// Forth immediate word
    U8  forth:  1;           /// 0:native, 1:composite
    U8  java:   1;           /// Java method
    U8  access: 2;           /// public, private, protected
    U8  ftype:  3;           /// static, finall, virtual, synchronized

    U8  data[];              /// name field + parameter field

    char *nfa()         { return (char*)&data[0];  }
    U8   *pfa(U8 off=0) { return &data[len + off]; }
};
#endif // NANOJVM_CORE_H
