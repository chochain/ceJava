#include <sstream>      // iostream, stringstream
#include <iomanip>      // setbase
#include <string>       // string class
#include "ucode.h"

extern Ucode uCode;
extern void  ss_dump(Thread &t);
///==========================================================================
/// Thread class implementation
///==========================================================================
///
/// Java class file constant pool access macros
///
#define jOff(j)      J->offset(j - 1)
#define jU16(a)      J->getU16(a)
#define jStrRef(j,s) J->getStr(j, s, true)
#define jStr(j,s)    J->getStr(j, s, false)
#define J16          (wide ? fetch4() : fetch2())
///
/// utilities
///
IU get_nparm(U16 itype, char *parm) {
    char *p = parm+1;
    U16  nparm = itype==2 ? 0 : 1;  /// except static type, all have a object ref
    while (*p != ')') {             /// count number of parameters
        if (*p++=='L') while (*p++ != ';');          /// (Ljava/lang/String;)
        nparm++;
    }
    return nparm;
}
///
/// translate Java bytestream constant pool ref to memory pointers
///
KV Thread::get_refs(IU j, IU itype) {
	IU c_m = jOff(j);               				 /// [02]000f:a=>[12,13]  [class_idx, method_idx]
	IU cj  = jU16(c_m + 1);         				 /// 12 class index
	IU mj  = jU16(c_m + 3);         				 /// 13 method index
	IU rf  = jOff(mj);              				 /// [13]008f:c=>[15,16]  [method_name, parm_name]

	char cls[128], nm[128], parm[32];
	LOG(" "); LOG(jStrRef(cj, cls));        		 /// get class name
	LOG("."); LOG(jStr(jU16(rf + 1), nm));  		 /// get method name
	LOG(":"); LOG(jStr(jU16(rf + 3), parm));		 /// get param list name

    struct KV r;
    r.key = j;                                               /// java class file index
	r.cx  = gPool.get_class(cls);                            /// class ref
	if (itype!=DATA_NA) {
		IU pi   = gPool.get_parm_idx(parm);                  /// parameter list
		r.ref   = gPool.get_method(nm, r.cx, pi, itype!=1);  /// special does not go up to super class
		r.nparm = get_nparm(itype, parm);
	}
	return r;
}
///
/// VM Execution Unit
///
void Thread::na() { LOG(" **NA**"); }/// feature not supported yet
void Thread::init(int jcf) {
	M0  = &gPool.pmem[0];
	J   = Loader::get(jcf);
	cls = J->cls_id;
}
void Thread::dispatch(IU mx, U16 nparm) {
    if (mx==DATA_NA) {
    	mx = gPool.get_method("main", cls);
    }
    Word *w = WORD(mx);
    if (w->java) {                   /// call Java inner interpreter
        IU  addr = *(IU*)w->pfa();
        java_call(addr, nparm);
    }
    else if (w->forth) {             /// user defined Forth word
        gPool.rs.push(IP);           /// setup call frame
        IP = (IU)(w->pfa() - M0);    /// get new IP
        while (IP) {                 /// Forth inner interpreter
            mx = *(IU*)(M0 + IP);    /// fetch next instruction
            LOG("\nm"); LOX4(IP-1); LOG(":"); LOX4(mx);
            LOG(" "); LOG(WORD(mx)->nfa());
            IP += sizeof(IU);        /// too bad, we cannot do IP++
            yield();                 /// gives some cycles to main thread (ESP32)
            dispatch(mx);            /// recursively call
        }
        IP = gPool.rs.pop();         /// restore call frame
    }
    else {
        fop xt = *(fop*)w->pfa();    /// Native method pointer
        xt(*this);
    }
}
///
/// Java core
///
void Thread::java_new()  {
	IU j = fetch2();                /// class index
	char cls[128];
	LOG(" "); LOG(jStrRef(j, cls));
	IU cx = gPool.get_class(cls);
    IU ox = gPool.add_obj(cx);
    push(ox);                       /// save object onto stack
}
void Thread::java_call(IU j, U16 nparm) {   /// Java inner interpreter
    gPool.rs.push(SP);              /// keep caller stack frame
    SP = ss.idx - nparm + 1;        /// adjust local variable base, extra 1=obj ref, TODO: handle types
    U16 n = ss.idx + jU16(j - 6) - nparm;   /// allocate for local variables
    while (ss.idx < n) push(0);     /// setup local variables, TODO: change ss.idx only

    U8 op;                          /// opcode
    gPool.rs.push(IP);              /// save caller instruction pointer
    IP = j;                         /// pointer to class file
    while (IP) {
        yield();                    /// gives main thread some cycles (ESP32)
        ss_dump(*this);
        op = fetch();               /// fetch JVM opcode
        LOG("j"); LOX4(IP-1); LOG(":"); LOX2(op);
        LOG(" "); LOG(uCode.vt[op].name);
        uCode.exec(*this, op);      /// execute JVM opcode (in microcode ROM)
    }
    IP = gPool.rs.pop();            /// restore to caller IP
    // restore caller stack frame
    DU rv = op == OP_RETURN ? 0 : pop(); /// check return value
    while (ss.idx >= SP) pop();     /// clean off stack (optional)
    SP = gPool.rs.pop();        	/// restore SP
    if (op==OP_RETURN) push(rv);    /// add return value if any
}
void Thread::invoke(U16 itype) {    /// invoke type: 0:virtual, 1:special, 2:static, 3:interface, 4:dynamic
    IU j = fetch2();                /// 2 - method index in pool
    if (itype>2) IP += 2;           /// extra 2 for interface and dynamic
    IU mi = gPool.lookup(gPool.vt, j, cls);  /// search cache first
    if (mi != DATA_NA) {
        Word *w = WORD(gPool.vt[mi].ref);
        LOG(" "); LOG(w->nfa());
        dispatch(gPool.vt[mi].ref, gPool.vt[mi].nparm);
        return;
    }
    ///
    /// cache missed, create new lookup entry
    ///
    KV r = get_refs(j, itype);     /// { key=j, cls=cx, val=mx, nparm }

	LOG(" =>$"); LOX(gPool.vt.idx);
    gPool.vt.push(r);

    if (r.ref != DATA_NA) dispatch(r.ref, r.nparm);
    else                  na();
}
///
/// class and instance variable access
///   Note: use gPool.vref is a bit wasteful but avoid the runtime search. TODO:
///
DU *Thread::cls_var() {
	U16 j = J16;
    IU  i = gPool.lookup(gPool.cv, j, cls);
    if (i != DATA_NA) { return (DU*)&gPool.pmem[gPool.cv[i].ref]; }

    /// cache missed, create new lookup entry
    IU   idx = gPool.cv.idx;
    KV   r   = get_refs(j);
    Word *w  = WORD(r.cx);              /// class storage
    DU   *cv = (DU*)w->pfa(PFA_CLS_CV) + idx;
    IU   ref = (IU)((U8*)cv - M0);

    LOG(" =>$"); LOX(idx);
    gPool.cv.push({ j, cls, ref, 0 });  /// create new cache entry
    return cv;
}
DU *Thread::inst_var(IU ox) {
	U16 j   = J16;
    DU  *iv = (DU*)OBJ(ox)->pfa();
    IU  i   = gPool.lookup(gPool.iv, j, cls);
    if (i != DATA_NA) return iv + gPool.iv[i].ref;

    // cache missed, create new lookup entry
    KV r   = get_refs(j);              /// for debug display only
    IU ref = gPool.iv.idx;
    LOG(" =>$"); LOX(ref);
    gPool.iv.push({ j, cls, ref, 0 }); /// create new cache entry
    return iv + ref;
}
///
/// array support
///   Note: use gPool.heap for array storage linked list obj_root
///
void Thread::java_newa(IU n) {      /// create 1-d array
	U8 j  = fetch();                /// fetch atype value
    if (j != 0xa) na();             /// support only integer, TODO: more types
    else {
        IU ax = gPool.add_array(j, n);
        push(ax);
    }
}
///
/// create array of references (i.e. 2-dim array)
/// Note: using DU for ref (IU) is a bit wasteful, but uniform
///
void Thread::java_anewa(IU n) {
	U16 j   = fetch2();             /// fetch 2-dim atype, ignore now, TODO: check type
    IU  c_f = jOff(j);              /// [02]000f =>[I
    IU  t2  = jU16(c_f);
    IU  ax  = gPool.add_array(t2 >> 8, n);   /// allocate array
    push(ax);
}
IU   Thread::alen(IU ax) {                   /// array length
    IU *p = (IU*)OBJ(ax);
    return *(p + 1);
}
void Thread::astore(IU ax, IU idx, DU v) {   /// array store
    DU *a0 = (DU*)OBJ(ax)->data;
    *(a0 + idx) = v;
}
DU *Thread::aload(IU ax, IU idx) {           /// array fetch (load onto stack)
    DU *a0 = (DU*)OBJ(ax)->data;
    return a0 + idx;
}
