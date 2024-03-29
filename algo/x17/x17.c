#include "x17-gate.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "algo/blake/sph_blake.h"
#include "algo/bmw/sph_bmw.h"
#include "algo/jh/sph_jh.h"
#include "algo/keccak/sph_keccak.h"
#include "algo/skein/sph_skein.h"
#include "algo/shavite/sph_shavite.h"
#include "algo/luffa/sph_luffa.h"
#include "algo/cubehash/sph_cubehash.h"
#include "algo/simd/sph_simd.h"
#include "algo/hamsi/sph_hamsi.h"
#include "algo/fugue/sph_fugue.h"
#include "algo/shabal/sph_shabal.h"
#include "algo/whirlpool/sph_whirlpool.h"
#include "algo/haval/sph-haval.h"
#include "algo/luffa/luffa_for_sse2.h" 
#include "algo/cubehash/cubehash_sse2.h"
#include "algo/simd/nist.h"
#include "algo/blake/sse2/blake.c"
#include "algo/bmw/sse2/bmw.c"
#include "algo/keccak/sse2/keccak.c"
#include "algo/skein/sse2/skein.c"
#include "algo/jh/sse2/jh_sse2_opt64.h"
#include <openssl/sha.h>
#if defined(__AES__)
  #include "algo/echo/aes_ni/hash_api.h"
  #include "algo/groestl/aes_ni/hash-groestl.h"
#else
  #include "algo/groestl/sph_groestl.h"
  #include "algo/echo/sph_echo.h"
#endif

typedef struct {
#if defined(__AES__)
        hashState_groestl       groestl;
        hashState_echo          echo;
#else
        sph_groestl512_context  groestl;
        sph_echo512_context     echo;
#endif
        hashState_luffa         luffa;
        cubehashParam           cubehash;
        sph_shavite512_context  shavite;
        hashState_sd            simd;
        sph_hamsi512_context    hamsi;
        sph_fugue512_context    fugue;
        sph_shabal512_context   shabal;
        sph_whirlpool_context   whirlpool;
        SHA512_CTX              sha512;
        sph_haval256_5_context  haval;
} x17_ctx_holder;

x17_ctx_holder x17_ctx __attribute__ ((aligned (64)));

void init_x17_ctx()
{
#if defined(__AES__)
        init_groestl( &x17_ctx.groestl, 64 );
        init_echo( &x17_ctx.echo, 512 );
#else
        sph_groestl512_init(&x17_ctx.groestl );
        sph_echo512_init(&x17_ctx.echo);
#endif
        init_luffa( &x17_ctx.luffa, 512 );
        cubehashInit( &x17_ctx.cubehash, 512, 16, 32 );
        sph_shavite512_init( &x17_ctx.shavite );
        init_sd( &x17_ctx.simd, 512 );
        sph_hamsi512_init( &x17_ctx.hamsi );
        sph_fugue512_init( &x17_ctx.fugue );
        sph_shabal512_init( &x17_ctx.shabal );
        sph_whirlpool_init( &x17_ctx.whirlpool );
        SHA512_Init( &x17_ctx.sha512 );
        sph_haval256_5_init(&x17_ctx.haval);
};

void x17_hash(void *output, const void *input)
{
	unsigned char hash[128] __attribute__ ((aligned (64)));
	#define hashB hash+64

        x17_ctx_holder ctx __attribute__ ((aligned (64)));
        memcpy( &ctx, &x17_ctx, sizeof(x17_ctx) );

        unsigned char hashbuf[128];
        size_t hashptr;
        sph_u64 hashctA;
        sph_u64 hashctB;

        //---blake1---
        
        DECL_BLK;
        BLK_I;
        BLK_W;
        BLK_C;

        //---bmw2---
        DECL_BMW;
        BMW_I;
        BMW_U;

        #define M(x)    sph_dec64le_aligned(data + 8 * (x))
        #define H(x)    (h[x])
        #define dH(x)   (dh[x])

        BMW_C;

        #undef M
        #undef H
        #undef dH

        //---groestl----

#if defined(__AES__)
        update_and_final_groestl( &ctx.groestl, (char*)hash,
                                  (const char*)hash, 512 );
#else
        sph_groestl512( &ctx.groestl, hash, 64 );
        sph_groestl512_close( &ctx.groestl, hash );
#endif

        //---skein4---

        DECL_SKN;
        SKN_I;
        SKN_U;
        SKN_C;

        //---jh5------

        DECL_JH;
        JH_H;

        //---keccak6---

        DECL_KEC;
        KEC_I;
        KEC_U;
        KEC_C;

        //--- luffa7
        update_and_final_luffa( &ctx.luffa, (BitSequence*)hash,
                                (const BitSequence*)hash, 64 );

        // 8 Cube
        cubehashUpdateDigest( &ctx.cubehash, (byte*) hash,
                              (const byte*)hash, 64 );

        // 9 Shavite
        sph_shavite512( &ctx.shavite, hash, 64);
        sph_shavite512_close( &ctx.shavite, hash);

        // 10 Simd
        update_final_sd( &ctx.simd, (BitSequence*)hash,
                         (const BitSequence*)hash, 512 );

        //11---echo---
#if defined(__AES__)
        update_final_echo ( &ctx.echo, (BitSequence*)hash,
                            (const BitSequence*)hash, 512 );
#else
	sph_echo512( &ctx.echo, hash, 64 );
        sph_echo512_close( &ctx.echo, hash );
#endif

        // X13 algos
        // 12 Hamsi
        sph_hamsi512( &ctx.hamsi, hash, 64 );
        sph_hamsi512_close( &ctx.hamsi, hash );

        // 13 Fugue
        sph_fugue512(&ctx.fugue, hash, 64 );
        sph_fugue512_close(&ctx.fugue, hash );

        // X14 Shabal
        sph_shabal512(&ctx.shabal, hash, 64);
        sph_shabal512_close( &ctx.shabal, hash );
       
        // X15 Whirlpool
	sph_whirlpool( &ctx.whirlpool, hash, 64 );
	sph_whirlpool_close( &ctx.whirlpool, hash );

	SHA512_Update( &ctx.sha512, hash, 64 );
        SHA512_Final( (unsigned char*)hash, &ctx.sha512 );

        sph_haval256_5( &ctx.haval, (const void*)hash, 64 );
        sph_haval256_5_close( &ctx.haval, output );
}

int scanhash_x17( int thr_id, struct work *work, uint32_t max_nonce,
	          uint64_t *hashes_done, struct thr_info *mythr)
{
   uint32_t endiandata[20] __attribute__((aligned(64)));
   uint32_t hash64[8] __attribute__((aligned(64)));
   uint32_t *pdata = work->data;
   uint32_t *ptarget = work->target;
   uint32_t n = pdata[19] - 1;
   const uint32_t first_nonce = pdata[19];
   const uint32_t Htarg = ptarget[7];
   /* int */ thr_id = mythr->id;  // thr_id arg is deprecated

   uint64_t htmax[] =
   {
	0,
	0xF,
	0xFF,
	0xFFF,
	0xFFFF,
	0x10000000
   };
   uint32_t masks[] =
   {
	0xFFFFFFFF,
	0xFFFFFFF0,
	0xFFFFFF00,
	0xFFFFF000,
	0xFFFF0000,
	0
   };

   // we need bigendian data...
   casti_m128i( endiandata, 0 ) = mm128_bswap_32( casti_m128i( pdata, 0 ) );
   casti_m128i( endiandata, 1 ) = mm128_bswap_32( casti_m128i( pdata, 1 ) );
   casti_m128i( endiandata, 2 ) = mm128_bswap_32( casti_m128i( pdata, 2 ) );
   casti_m128i( endiandata, 3 ) = mm128_bswap_32( casti_m128i( pdata, 3 ) );
   casti_m128i( endiandata, 4 ) = mm128_bswap_32( casti_m128i( pdata, 4 ) );

#ifdef DEBUG_ALGO
   if ( Htarg != 0 )
	printf( "[%d] Htarg=%X\n", thr_id, Htarg );
#endif
   for ( int m = 0; m < 6; m++ )
   {
	if ( Htarg <= htmax[m] )
	{
	   uint32_t mask = masks[m];
	   do
	   {
		pdata[19] = ++n;
		be32enc( &endiandata[19], n );
		x17_hash( hash64, endiandata );
#ifndef DEBUG_ALGO
		if ( !( hash64[7] & mask ) )
                {
                   if ( fulltest( hash64, ptarget ) )
		   {
		      *hashes_done = n - first_nonce + 1;
		      return true;
                   }
//                 else
//                      applog(LOG_INFO, "Result does not validate on CPU!");
                }
#else
		if ( !( n % 0x1000 ) && !thr_id ) printf(".");
		if ( !( hash64[7] & mask ) )
	       	{
		   printf("[%d]",thr_id);
		   if ( fulltest( hash64, ptarget ) )
		   {
                       work_set_target_ratio( work, hash64 );
		       *hashes_done = n - first_nonce + 1;
		       return true;
		   }
		}
#endif
	    } while (n < max_nonce && !work_restart[thr_id].restart);
			// see blake.c if else to understand the loop on htmax => mask
	    break;
	}
   }
   *hashes_done = n - first_nonce + 1;
   pdata[19] = n;
   return 0;
}
