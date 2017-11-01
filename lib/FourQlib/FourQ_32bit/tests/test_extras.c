/***********************************************************************************
* FourQlib: a high-performance crypto library based on the elliptic curve FourQ
*
*    Copyright (c) Microsoft Corporation. All rights reserved.
*
* Abstract: utility functions for tests
************************************************************************************/  

#include "../FourQ_internal.h"
#include "test_extras.h"
#if (OS_TARGET == OS_WIN)
    #include <windows.h>
    #include <intrin.h>
#endif
#if (OS_TARGET == OS_LINUX) && (TARGET == TARGET_ARM)
    #include <time.h>
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


int64_t cpu_nseconds(void)
{ // Access system counter for benchmarking
#if (OS_TARGET == OS_WIN) && (TARGET == TARGET_x86)
    return __rdtsc();
#elif (OS_TARGET == OS_WIN) && (TARGET == TARGET_ARM)
    return __rdpmccntr64();
#elif (OS_TARGET == OS_LINUX) && (TARGET == TARGET_x86)
    unsigned int hi, lo;

    asm volatile ("rdtsc\n\t" : "=a" (lo), "=d"(hi));
    return ((int64_t)lo) | (((int64_t)hi) << 32);
#elif (OS_TARGET == OS_LINUX) && (TARGET == TARGET_ARM)
    struct timespec time;

    clock_gettime(CLOCK_REALTIME, &time);
    return (int64_t)(time.tv_sec*1e9 + time.tv_nsec);
#else
    return 0;            
#endif
}


int vcompare32(uint32_t* a, uint32_t* b)
{ // Comparing uint32_t digits of two field elements, a=b? : (0) equal, (1) unequal
  // NOTE: this function does not have constant-time execution. TO BE USED FOR TESTING ONLY.
    unsigned int i;

    for (i = 0; i < VWORDS_FIELD; i++) {
        if (a[i] != b[i]) return 1;
    }
    
    return 0; 
}


int v2compare32(uint32_t* a, uint32_t* b)
{ // Comparing uint32_t digits of two quadratic extension field elements, a=b? : (0) equal, (1) unequal
  // NOTE: this function does not have constant-time execution. TO BE USED FOR TESTING ONLY.
    unsigned int i;

    for (i = 0; i < 2*VWORDS_FIELD; i++) {
        if (a[i] != b[i]) return 1;
    }
    
    return 0; 
}


int fpcompare64(uint64_t* a, uint64_t* b)
{ // Comparing uint64_t digits of two field elements, a=b? : (0) equal, (1) unequal
  // NOTE: this function does not have constant-time execution. TO BE USED FOR TESTING ONLY.
    unsigned int i;

    for (i = 0; i < (NWORDS64_FIELD); i++) {
        if (a[i] != b[i]) return 1;
    }
    
    return 0; 
}


int fp2compare64(uint64_t* a, uint64_t* b)
{ // Comparing uint64_t digits of two quadratic extension field elements, ai=bi? : (0) equal, (1) unequal
  // NOTE: this function does not have constant-time execution. TO BE USED FOR TESTING ONLY.
    unsigned int i;

    for (i = 0; i < (2*NWORDS64_FIELD); i++) {
        if (a[i] != b[i]) return 1;
    }
    
    return 0; 
}


void random_scalar_test(uint64_t* a)
{ // Generating a pseudo-random scalar value in [0, 2^256-1] 
  // NOTE: distribution is not fully uniform. TO BE USED FOR TESTING ONLY.
    unsigned char* string = (unsigned char*)&a[0];
    unsigned int i;

    for (i = 0; i < (sizeof(uint64_t)*NWORDS64_ORDER); i++) {
        string[i] = (unsigned char)rand();             
    }
}


void fp2random1271_test(f2elm_t a)
{ // Generating a pseudo-random GF(p^2) element a+b*i, where a,b in [0, 2^127-1] 
  // NOTE: distribution is not fully uniform. TO BE USED FOR TESTING ONLY.
    digit_t mask_7fff = (digit_t)-1 >> 1;

    random_scalar_test((uint64_t*)&a[0]);
    a[0][NWORDS_FIELD-1] &= mask_7fff;
    a[1][NWORDS_FIELD-1] &= mask_7fff;
}


void vrandom1271_test(velm_t a)
{ // Generating a vectorized pseudo-random GF(p) element in [0, 2^127-1] 
  // NOTE: distribution is not fully uniform. TO BE USED FOR TESTING ONLY.
    v2elm_t temp;
    
    v2random1271_test(temp);
    memmove((unsigned char*)a, (unsigned char*)&temp[0], 20);
}


void v2random1271_test(v2elm_t a)
{ // Generating a vectorized pseudo-random GF(p^2) element a+b*i, where a,b in [0, 2^127-1] 
  // NOTE: distribution is not fully uniform. TO BE USED FOR TESTING ONLY.
    digit_t mask_7fff = (digit_t)-1 >> 1;
    f2elm_t temp;
    
    random_scalar_test((uint64_t*)&temp[0]);
    temp[0][NWORDS_FIELD-1] &= mask_7fff;
    temp[1][NWORDS_FIELD-1] &= mask_7fff;
    from_std_to_ext(temp, a);
}


bool verify_mLSB_recoding(uint64_t* scalar, int* digits)
{ // Verification of the mLSB-set's recoding algorithm used in fixed-base scalar multiplication 
    unsigned int j, l = L_FIXEDBASE, d = D_FIXEDBASE;
    uint64_t temp, temp2, carry, borrow, generated_scalar[NWORDS64_ORDER] = {0};
    int i, digit;

    for (i = (l-1); i >= 0; i--)
    {
        // Shift generated scalar to the left by 1 (multiply by 2)
        temp = ((generated_scalar[0] >> (RADIX64-1)) & 1) ;
        generated_scalar[0] = generated_scalar[0] << 1;

        for (j = 1; j < NWORDS64_ORDER; j++) {
            temp2 = ((generated_scalar[j] >> (RADIX64-1)) & 1) ;
            generated_scalar[j] = (generated_scalar[j] << 1) | temp;
            temp = temp2;
        }
     
        // generated scalar + digit_i
        if (i < (int)d) {
            digit = digits[i] | 1;
            if (digit >= 0) {
                generated_scalar[0] = generated_scalar[0] + digit;
                carry = (generated_scalar[0] < (unsigned int)digit);
                for (j = 1; j < NWORDS64_ORDER; j++)
                {
                    generated_scalar[j] = generated_scalar[j] + carry;    
                    carry = (generated_scalar[j] < carry);
                }
            } else {
                borrow = 0;
                temp = (uint64_t)(-digit);
                for (j = 0; j < NWORDS64_ORDER; j++)
                {
                    temp2 = generated_scalar[j] - temp;
                    carry = (generated_scalar[j] < temp);
                    generated_scalar[j] = temp2 - borrow;
                    borrow = carry || (temp2 < borrow);
                    temp = 0;
                }
            } 
        } else {
            digit = digits[i]*(digits[i-(i/d)*d] | 1);
            if (digit >= 0) {
                generated_scalar[0] = generated_scalar[0] + digit;
                carry = (generated_scalar[0] < (unsigned int)digit);
                for (j = 1; j < NWORDS64_ORDER; j++)
                {
                    generated_scalar[j] = generated_scalar[j] + carry;    
                    carry = (generated_scalar[j] < carry);
                }
            } else {
                borrow = 0;
                temp = (uint64_t)(-digit);
                for (j = 0; j < NWORDS64_ORDER; j++)
                {
                    temp2 = generated_scalar[j] - temp;
                    carry = (generated_scalar[j] < temp);
                    generated_scalar[j] = temp2 - borrow;
                    borrow = carry || (temp2 < borrow);
                    temp = 0;
                }
            } 
        }
    }

    for (j = 0; j < NWORDS64_ORDER; j++)
    {
        if (scalar[j] != generated_scalar[j]) 
            return false;
    }

    return true;
}