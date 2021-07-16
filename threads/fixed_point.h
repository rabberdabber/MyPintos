#include <stdint.h>

#define f (1 << 14)

int 
int_to_fixed_pt(int n);

int 
fixed_pt_to_int_zero(int x);

int 
fixed_pt_to_int_nearest(int x);

int
add_fixed_pts(int x,int y);

int 
sub_fixed_pts(int x,int y);

int
add_fixed_pt_with_int(int n,int x);

int 
sub_fixed_pt_with_int(int n,int x);

int
mul_fixed_pts(int x, int y);

int 
mul_fixed_pt_with_int(int x,int n);

int
div_fixed_pts(int x, int y);

int 
div_fixed_pt_with_int(int x,int n);


int 
int_to_fixed_pt(int n){
    return n * f;
}

int 
fixed_pt_to_int_zero(int x){
    return x / f;
}

int 
fixed_pt_to_int_nearest(int x){
    return x >= 0 ? (x + f / 2) / f : (x - f / 2) / f;
}

int
add_fixed_pts(int x,int y){
    return x + y;
}

int 
sub_fixed_pts(int x,int y){
    return x - y;
}

int
add_fixed_pt_with_int(int n,int x){
   return  x + int_to_fixed_pt(n);
}

int 
sub_fixed_pt_with_int(int n,int x){
   return  x - int_to_fixed_pt(n);
}

int
mul_fixed_pts(int x, int y){
    return ((int64_t) x) * y / f;
}

int 
mul_fixed_pt_with_int(int x,int n){
    return x * n;
}

int
div_fixed_pts(int x, int y){
    return ((int64_t) x) * f / y;
}

int 
div_fixed_pt_with_int(int x,int n){
    return x / n;
}




