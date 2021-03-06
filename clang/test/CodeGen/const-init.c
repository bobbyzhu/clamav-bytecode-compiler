// RUN: %clang_cc1 -triple i386-pc-linux-gnu -ffreestanding -verify -emit-llvm -o - %s | FileCheck %s

#include <stdint.h>

// Brace-enclosed string array initializers
char a[] = { "asdf" };

// Double-implicit-conversions of array/functions (not legal C, but
// clang accepts it for gcc compat).
intptr_t b = a; // expected-warning {{incompatible pointer to integer conversion}}
int c();
void *d = c;
intptr_t e = c; // expected-warning {{incompatible pointer to integer conversion}}

int f, *g = __extension__ &f, *h = (1 != 1) ? &f : &f;

union s2 {
  struct {
    struct { } *f0;
  } f0;
};

int g0 = (int)(&(((union s2 *) 0)->f0.f0) - 0);

// CHECK: @g1x = global {{%.}} { double 1.000000e+00{{[0]*}}, double 0.000000e+00{{[0]*}} }
_Complex double g1x = 1.0f;
// CHECK: @g1y = global {{%.}} { double 0.000000e+00{{[0]*}}, double 1.000000e+00{{[0]*}} }
_Complex double g1y = 1.0fi;
// CHECK: @g1 = global {{%.}} { i8 1, i8 10 }
_Complex char g1 = (char) 1 + (char) 10 * 1i;
// CHECK: @g2 = global %2 { i32 1, i32 10 }
_Complex int g2 = 1 + 10i;
// CHECK: @g3 = global {{%.}} { float 1.000000e+00{{[0]*}}, float 1.000000e+0{{[0]*}}1 }
_Complex float g3 = 1.0 + 10.0i;
// CHECK: @g4 = global {{%.}} { double 1.000000e+00{{[0]*}}, double 1.000000e+0{{[0]*}}1 }
_Complex double g4 = 1.0 + 10.0i;
// CHECK: @g5 = global %2 zeroinitializer
_Complex int g5 = (2 + 3i) == (5 + 7i);
// CHECK: @g6 = global {{%.}} { double -1.100000e+0{{[0]*}}1, double 2.900000e+0{{[0]*}}1 }
_Complex double g6 = (2.0 + 3.0i) * (5.0 + 7.0i);
// CHECK: @g7 = global i32 1
int g7 = (2 + 3i) * (5 + 7i) == (-11 + 29i);
// CHECK: @g8 = global i32 1
int g8 = (2.0 + 3.0i) * (5.0 + 7.0i) == (-11.0 + 29.0i);
// CHECK: @g9 = global i32 0
int g9 = (2 + 3i) * (5 + 7i) != (-11 + 29i);
// CHECK: @g10 = global i32 0
int g10 = (2.0 + 3.0i) * (5.0 + 7.0i) != (-11.0 + 29.0i);

// PR5108
// CHECK: @gv1 = global %4 <{ i32 0, i8 7 }>, align 1
struct {
  unsigned long a;
  unsigned long b:3;
} __attribute__((__packed__)) gv1  = { .a = 0x0, .b = 7,  };

// PR5118
// CHECK: @gv2 = global %5 <{ i8 1, i8* null }>, align 1 
struct {
  unsigned char a;
  char *b;
} __attribute__((__packed__)) gv2 = { 1, (void*)0 };

// Global references
// CHECK: @g11.l0 = internal global i32 ptrtoint (i32 ()* @g11 to i32)
long g11() { 
  static long l0 = (long) g11;
  return l0; 
}

// CHECK: @g12 = global i32 ptrtoint (i8* @g12_tmp to i32)
static char g12_tmp;
long g12 = (long) &g12_tmp;

// CHECK: @g13 = global [1 x %struct.g13_s0] [%struct.g13_s0 { i32 ptrtoint (i8* @g12_tmp to i32) }]
struct g13_s0 {
   long a;
};
struct g13_s0 g13[] = {
   { (long) &g12_tmp }
};

// CHECK: @g14 = global i8* inttoptr (i64 100 to i8*)
void *g14 = (void*) 100;

// CHECK: @g15 = global i32 -1
int g15 = (int) (char) ((void*) 0 + 255);

// CHECK: @g16 = global i64 4294967295
long long g16 = (long long) ((void*) 0xFFFFFFFF);

// CHECK: @g17 = global i32* @g15
int *g17 = (int *) ((long) &g15);

// CHECK: @g18.p = internal global [1 x i32*] [i32* @g19]
void g18(void) {
  extern int g19;
  static int *p[] = { &g19 };
}

// CHECK: @g20.l0 = internal global %struct.g20_s1 { %struct.g20_s0* null, %struct.g20_s0** getelementptr inbounds (%struct.g20_s1* @g20.l0, i32 0, i32 0) }
struct g20_s0;
struct g20_s1 {
  struct g20_s0 *f0, **f1;
};
void *g20(void) {
  static struct g20_s1 l0 = { ((void*) 0), &l0.f0 };
  return l0.f1;
}

// PR4108
struct g21 {int g21;};
const struct g21 g21 = (struct g21){1};

// PR5474
struct g22 {int x;} __attribute((packed));
struct g23 {char a; short b; char c; struct g22 d;};
struct g23 g24 = {1,2,3,4};

// CHECK: @__func__.g25 = private constant [4 x i8] c"g25\00"
// CHECK: @g25.g26 = internal global i8* getelementptr inbounds ([4 x i8]* @__func__.g25, i32 0, i32 0)
int g25() {
  static const char *g26 = __func__;
  return *g26;
}
