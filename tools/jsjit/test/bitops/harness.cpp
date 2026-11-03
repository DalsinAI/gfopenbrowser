// Patch 0101's bit operation code under qemu-m68k -cpu m68040: the JIT's
// own Builder class and FastPaths methods (extracted.inc, which extract.py
// cuts from M68KTemplateJIT.cpp) write &, |, ^, <<, >>, >>>, ~ and
// op_unsigned for int32 and double operands, with the int32 code, the
// float path and the slow path, and every result is checked against
// JavaScript's ToInt32. run.sh builds and runs it.
// MIT, Copyright (c) 2026 The AmiCradle Team.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <bit>
#include <sys/mman.h>
#define ASSERT(x) do{}while(0)
#define RELEASE_ASSERT(x) do{ if(!(x)){ fprintf(stderr,"assert %s line %d\n",#x,__LINE__); abort(); } }while(0)
#define RELEASE_ASSERT_NOT_REACHED() abort()
template<typename T> struct Vector {
    std::vector<T> v;
    size_t size() const { return v.size(); }
    void append(const T& x) { v.push_back(x); }
    T& operator[](size_t i) { return v[i]; }
    const T& operator[](size_t i) const { return v[i]; }
    void shrink(size_t n) { v.resize(n); }
    void grow(size_t n) { v.resize(n); }
    bool isEmpty() const { return v.empty(); }
    auto begin() { return v.begin(); } auto end() { return v.end(); }
    struct Span { T* p; T* data() { return p; } };
    Span mutableSpan() { return { v.data() }; }
    Span span() { return { v.data() }; }
};
enum OpcodeID { op_bitand, op_bitor, op_bitxor, op_lshift, op_rshift, op_urshift, op_bitnot, op_unsigned };
constexpr uint32_t int32Tag = 0xfffe0000;
struct JSValue { static constexpr uint64_t DoubleEncodeOffset = 1ull << 49; };
static const double PNaN = std::bit_cast<double>(0x7ff8000000000000ull);
#include "extracted.inc"

static int32_t toInt32(double d){ if(!std::isfinite(d)) return 0; double t=std::trunc(d); double m=t-4294967296.0*std::floor(t/4294967296.0); return (int32_t)(uint32_t)m; }
static uint64_t enc(double d){ return std::bit_cast<uint64_t>(d)+JSValue::DoubleEncodeOffset; }
static uint64_t encI(int32_t i){ return ((uint64_t)int32Tag<<32)|(uint32_t)i; }
struct Val { uint64_t bits; bool isNumber; double num; };
static Val I(int32_t i){ return {encI(i),true,(double)i}; }
static Val D(double d){ return {enc(d),true,d}; }
static Val Cell(){ return {0x0000000000123450ull,false,0}; }

static uint8_t* place(Builder& code){
    for (auto& f : code.fixups.v) {
        size_t target = f.targetBuffer;
        if (f.relative) code.patchLong(f.at, (uint32_t)((int32_t)target - (int32_t)f.from));
        else RELEASE_ASSERT(false);
    }
    uint8_t* mem=(uint8_t*)mmap(nullptr,65536,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    memcpy(mem,code.bytes.v.data(),code.size());
    __builtin___clear_cache((char*)mem,(char*)mem+code.size());
    return mem;
}
static int run(uint8_t* code, uint64_t* frame){
    register uint64_t* f __asm__("a1") = frame; register uint8_t* c __asm__("a0") = code; int r;
    __asm__ volatile("movem.l %%d2-%%d7/%%a2-%%a5,-(%%sp)\n\tmove.l %%a1,%%a5\n\tjsr (%%a0)\n\tmovem.l (%%sp)+,%%d2-%%d7/%%a2-%%a5\n\tmove.l %%d0,%1"
        : "+a"(c), "=g"(r), "+a"(f) : : "d0","d1","d2","d3","d4","d5","d6","d7","fp0","fp1","memory","cc");
    return r;
}
static void endWith(Builder& b, int8_t v){ b.moveq(v,0); b.word(0x4e75); }
enum Shape { BothFrame, RhsConst, DstIsLhs };
// One compiled instruction, as bitBinary/bitNot/toUnsigned write it.
static uint8_t* compile(OpcodeID op, Shape shape, int32_t rhsConst){
    Builder b; Paths p(b);
    Vector<size_t> slow; FloatPath fpth;
    p.m_slow=&slow; p.m_bail=&slow; p.m_float=&fpth;
    Operand lhs; lhs.valid=true; lhs.hi=0; lhs.lo=4;
    Operand rhs; rhs.valid=true; rhs.hi=8; rhs.lo=12;
    if (shape==RhsConst){ rhs=Operand(); rhs.valid=true; rhs.isConstant=true; rhs.constant=rhsConst; }
    Operand dst; dst.valid=true; dst.hi=16; dst.lo=20;
    if (shape==DstIsLhs) dst=lhs;
    bool unary = op==op_bitnot || op==op_unsigned;
    if (unary) rhs=lhs;
    p.useFloatPath(op,dst,lhs,rhs);
    p.checkInt32(lhs);
    if (op==op_bitnot){ b.loadFrame(lhs.lo,0); b.notReg(0); p.storeInt32(0,dst,Paths::same(dst,lhs)); }
    else if (op==op_unsigned){ b.tstFrame(lhs.lo); p.bailUnless(Builder::Minus); if(!Paths::same(dst,lhs)){ b.copyFrame(lhs.hi,dst.hi); b.copyFrame(lhs.lo,dst.lo);} }
    else { p.checkInt32(rhs); p.load(lhs,0); p.bitOperation(op,0,rhs,~0u); p.storeInt32(0,dst,Paths::same(dst,lhs)||Paths::same(dst,rhs)); }
    endWith(b,0);
    for (size_t i : fpth.branches.v) b.fixups[i].targetBuffer=b.size();
    p.m_slow=&slow;
    p.floatBitOperation(op,dst,lhs,rhs);
    endWith(b,1);
    for (size_t i : slow.v) b.fixups[i].targetBuffer=b.size();
    endWith(b,2);
    return place(b);
}
static bool expectSlow(const Val& v){ if(!v.isNumber) return true; if(v.bits>>32==int32Tag) return false; double d=v.num; return !(d>=-2147483648.0 && d<4294967296.0); }
int main(){
    std::vector<Val> vals={I(0),I(1),I(-1),I(5),I(31),I(32),I(33),I(-33),I(0x7fffffff),I((int32_t)0x80000000),I(123456789),I(-987654321),
        D(0.0),D(-0.0),D(1.5),D(-1.5),D(2.9),D(-2.9),D(31.99),D(2147483647.0),D(2147483647.9),D(2147483648.0),D(2147483648.5),D(3000000000.7),
        D(4294967295.0),D(4294967295.99),D(4294967296.0),D(-2147483648.0),D(-2147483647.5),D(-2147483648.5),D(-3e9),D(1e300),D(NAN),D(INFINITY),D(-INFINITY),D(5e-324),D(65536.25),Cell()};
    const char* names[]={"&","|","^","<<",">>",">>>","~","unsigned"};
    int bad=0, cases=0, fast=0, flt=0, slw=0;
    uint32_t initial = getenv("FPCR") ? strtoul(getenv("FPCR"), nullptr, 0) : 0x80;
    { uint32_t set=initial; __asm__ volatile("fmove.l %0,%%fpcr"::"d"(set)); }
    for (int o=0;o<8;o++){ OpcodeID op=(OpcodeID)o; bool unary=o>=6;
      for (int shape=0; shape<3; shape++){
        if (unary && shape==RhsConst) continue;
        std::vector<int32_t> consts = shape==RhsConst ? std::vector<int32_t>{0,1,3,8,9,31,32,33,-1,255,(int32_t)0xffffffff,0x12345678} : std::vector<int32_t>{0};
        for (int32_t rc : consts){
          uint8_t* code=compile(op,(Shape)shape,rc);
          for (auto& a : vals) for (auto& bv : (unary||shape==RhsConst) ? std::vector<Val>{I(0)} : vals){
            uint64_t frame[3]={a.bits, bv.bits, 0xdeadbeefdeadbeefull};
            int r=run(code,frame); cases++;
            { uint32_t c; __asm__ volatile("fmove.l %%fpcr,%0":"=d"(c)); if (c!=initial){ printf("FPCR %x after %s shape%d a=%016llx b=%016llx r=%d\n",c,names[o],shape,(unsigned long long)a.bits,(unsigned long long)bv.bits,r); return 3; } }
            uint64_t out = shape==DstIsLhs ? frame[0] : frame[2];
            bool rhsIsNum = shape==RhsConst || unary || bv.isNumber;
            bool shouldSlow = op==op_unsigned ? (a.bits>>32)!=int32Tag : (expectSlow(a) || (!unary && shape!=RhsConst && expectSlow(bv)));
            if (r==2){ slw++; if(!shouldSlow){ printf("UNEXPECTED SLOW %s a=%016llx b=%016llx\n",names[o],(unsigned long long)a.bits,(unsigned long long)bv.bits); bad++; } continue; }
            if (shouldSlow){ printf("SHOULD BE SLOW %s a=%016llx b=%016llx r=%d\n",names[o],(unsigned long long)a.bits,(unsigned long long)bv.bits,r); bad++; continue; }
            (void)rhsIsNum;
            if (r==0) fast++; else flt++;
            int32_t x=toInt32(a.num), y = shape==RhsConst ? rc : toInt32(bv.num);
            uint64_t want;
            switch(op){
            case op_bitand: want=encI(x&y); break; case op_bitor: want=encI(x|y); break; case op_bitxor: want=encI(x^y); break;
            case op_lshift: want=encI((int32_t)((uint32_t)x<<(y&31))); break; case op_rshift: want=encI(x>>(y&31)); break;
            case op_urshift: want=encI((int32_t)((uint32_t)x>>(y&31))); break; case op_bitnot: want=encI(~x); break;
            default: { // op_unsigned: an int32 operand only (after >>>); doubles go slow in the fast code? no: float path checks int32 tag
                if ((a.bits>>32)!=int32Tag){ want=0; break; }
                uint32_t u=(uint32_t)x; want = (int32_t)u>=0 ? encI((int32_t)u) : enc((double)u); }
            }
            if (op==op_unsigned && (a.bits>>32)!=int32Tag){ printf("unsigned of double should be slow\n"); bad++; continue; }
            if (out!=want){ printf("BAD %s shape%d rc=%d a=%016llx b=%016llx got %016llx want %016llx path %d\n",names[o],shape,rc,(unsigned long long)a.bits,(unsigned long long)bv.bits,(unsigned long long)out,(unsigned long long)want,r); bad++; }
          }
        }
      }
    }
    // FPCR left as it was?
    uint32_t fpcr; __asm__ volatile("fmove.l %%fpcr,%0":"=d"(fpcr)); 
    if (fpcr!=initial) bad++;
    printf("%d cases: %d int32 code, %d float path, %d slow path; FPCR %08x; %s\n",cases,fast,flt,slw,fpcr,bad?"FAIL":"PASS");
    return bad!=0;
}
