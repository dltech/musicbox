/* Copyright (C) 2002-2006 Jean-Marc Valin
   File: filters.c
   Various analysis/synthesis filters

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   - Neither the name of the Xiph.org Foundation nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "filters.h"
#include "stack_alloc.h"
#include "arch.h"
#include "math_approx.h"
#include "ltp.h"




void bw_lpc(spx_word16_t gamma, const spx_coef_t *lpc_in, spx_coef_t *lpc_out, int order)
{
   int i;
   spx_word16_t tmp=gamma;
   for (i=0;i<order;i++)
   {
      lpc_out[i] = (spx_coef_t)MULT16_16_P15(tmp,lpc_in[i]);
      tmp = (spx_word16_t)MULT16_16_P15(tmp, gamma);
   }
}

void sanitize_values32(spx_word32_t *vec, spx_word32_t min_val, spx_word32_t max_val, int len)
{
   int i;
   for (i=0;i<len;i++)
   {
      /* It's important we do the test that way so we can catch NaNs, which are neither greater nor smaller */
      if (!(vec[i]>=min_val && vec[i] <= max_val))
      {
         if (vec[i] < min_val)
            vec[i] = min_val;
         else if (vec[i] > max_val)
            vec[i] = max_val;
         else /* Has to be NaN */
            vec[i] = 0;
      }
   }
}

//void highpass(const spx_word16_t *x, spx_word16_t *y, int len, int filtID, spx_mem_t *mem)
//{
//   int i;
//
//   const spx_word16_t Pcoef[5][3] = {{16384, -31313, 14991}, {16384, -31569, 15249}, {16384, -31677, 15328}, {16384, -32313, 15947}, {16384, -22446, 6537}};
//   const spx_word16_t Zcoef[5][3] = {{15672, -31344, 15672}, {15802, -31601, 15802}, {15847, -31694, 15847}, {16162, -32322, 16162}, {14418, -28836, 14418}};
//
//   const spx_word16_t *den, *num;
//   if (filtID>4)
//      filtID=4;
//
//   den = Pcoef[filtID]; num = Zcoef[filtID];
//   /*return;*/
//   for (i=0;i<len;i++)
//   {
//      spx_word16_t yi;
//      spx_word32_t vout = ADD32(MULT16_16(num[0], x[i]),mem[0]);
//      yi = EXTRACT16(SATURATE(PSHR32(vout,14),32767));
//      mem[0] = ADD32(MAC16_16(mem[1], num[1],x[i]), SHL32(MULT16_32_Q15(-den[1],vout),1));
//      mem[1] = ADD32(MULT16_16(num[2],x[i]), SHL32(MULT16_32_Q15(-den[2],vout),1));
//      y[i] = yi;
//   }
//}



/* FIXME: These functions are ugly and probably introduce too much error */
void signal_mul(const spx_sig_t *x, spx_sig_t *y, spx_word32_t scale, int len)
{
   int i;
   for (i=0;i<len;i++)
   {
      y[i] = SHL32(MULT16_32_Q14(EXTRACT16(SHR32(x[i],7)),scale),7);
   }
}


spx_word16_t compute_rms16(const spx_word16_t *x, int len)
{
   int i;
   spx_word16_t max_val=10;

   for (i=0;i<len;i++)
   {
      spx_sig_t tmp = x[i];
      if (tmp<0)
         tmp = -tmp;
      if (tmp > max_val)
         max_val = (spx_word16_t)tmp;
   }
   if (max_val>16383)
   {
      spx_word32_t sum=0;
      for (i=0;i<len;i+=4)
      {
         spx_word32_t sum2=0;
         sum2 = MAC16_16(sum2,SHR16(x[i],1),SHR16(x[i],1));
         sum2 = MAC16_16(sum2,SHR16(x[i+1],1),SHR16(x[i+1],1));
         sum2 = MAC16_16(sum2,SHR16(x[i+2],1),SHR16(x[i+2],1));
         sum2 = MAC16_16(sum2,SHR16(x[i+3],1),SHR16(x[i+3],1));
         sum = ADD32(sum,SHR32(sum2,6));
      }
      return SHL16(spx_sqrt(DIV32(sum,len)),4);
   } else {
      spx_word32_t sum=0;
      int sig_shift=0;
      if (max_val < 8192)
         sig_shift=1;
      if (max_val < 4096)
         sig_shift=2;
      if (max_val < 2048)
         sig_shift=3;
      for (i=0;i<len;i+=4)
      {
         spx_word32_t sum2=0;
         sum2 = MAC16_16(sum2,SHL16(x[i],sig_shift),SHL16(x[i],sig_shift));
         sum2 = MAC16_16(sum2,SHL16(x[i+1],sig_shift),SHL16(x[i+1],sig_shift));
         sum2 = MAC16_16(sum2,SHL16(x[i+2],sig_shift),SHL16(x[i+2],sig_shift));
         sum2 = MAC16_16(sum2,SHL16(x[i+3],sig_shift),SHL16(x[i+3],sig_shift));
         sum = ADD32(sum,SHR32(sum2,6));
      }
      return SHL16(spx_sqrt(DIV32(sum,len)),3-sig_shift);
   }
}


#ifdef MERGE_FILTERS
const spx_word16_t zeros[10] = {0,0,0,0,0,0,0,0,0,0};
#endif  /* MERGE_FILTERS */

#if !defined(OVERRIDE_FILTER_MEM16) && !defined(DISABLE_ENCODER)
void filter_mem16(const spx_word16_t *x, const spx_coef_t *num, const spx_coef_t *den, spx_word16_t *y, int N, int ord, spx_mem_t *mem, char *stack)
{
   int i,j;
   spx_word16_t xi,yi,nyi;
   for (i=0;i<N;i++)
   {
      xi= x[i];
      yi = EXTRACT16(SATURATE(ADD32(EXTEND32(x[i]),PSHR32(mem[0],LPC_SHIFT)),32767));
      nyi = NEG16(yi);
      for (j=0;j<ord-1;j++)
      {
         mem[j] = MAC16_16(MAC16_16(mem[j+1], num[j],xi), den[j],nyi);
      }
      mem[ord-1] = ADD32(MULT16_16(num[ord-1],xi), MULT16_16(den[ord-1],nyi));
      y[i] = yi;
   }
   (void)stack;
}
#endif /* !defined(OVERRIDE_FILTER_MEM16) && !defined(DISABLE_ENCODER) */

#ifndef OVERRIDE_IIR_MEM16
void iir_mem16(const spx_word16_t *x, const spx_coef_t *den, spx_word16_t *y, int N, int ord, spx_mem_t *mem, char *stack)
{
   int i,j;
   spx_word16_t yi,nyi;

   for (i=0;i<N;i++)
   {
      yi = EXTRACT16(SATURATE(ADD32(EXTEND32(x[i]),PSHR32(mem[0],LPC_SHIFT)),32767));
      nyi = NEG16(yi);
      for (j=0;j<ord-1;j++)
      {
         mem[j] = MAC16_16(mem[j+1],den[j],nyi);
      }
      mem[ord-1] = MULT16_16(den[ord-1],nyi);
      y[i] = yi;
   }
   (void)stack;
}
#endif

#if !defined(OVERRIDE_FIR_MEM16) && !defined(DISABLE_ENCODER)
void fir_mem16(const spx_word16_t *x, const spx_coef_t *num, spx_word16_t *y, int N, int ord, spx_mem_t *mem, char *stack)
{
   int i,j;
   spx_word16_t xi,yi;

   for (i=0;i<N;i++)
   {
      xi=x[i];
      yi = EXTRACT16(SATURATE(ADD32(EXTEND32(x[i]),PSHR32(mem[0],LPC_SHIFT)),32767));
      for (j=0;j<ord-1;j++)
      {
         mem[j] = MAC16_16(mem[j+1], num[j],xi);
      }
      mem[ord-1] = MULT16_16(num[ord-1],xi);
      y[i] = yi;
   }
   (void)stack;
}
#endif /* !defined(OVERRIDE_FIR_MEM16) && !defined(DISABLE_ENCODER) */







// #if 0
// const spx_word16_t shift_filt[3][7] = {{-33,    1043,   -4551,   19959,   19959,   -4551,    1043},
//                                  {-98,    1133,   -4425,   29179,    8895,   -2328,     444},
//                                  {444,   -2328,    8895,   29179,   -4425,    1133,     -98}};
// #else
const spx_word16_t shift_filt[3][7] = {{-390,    1540,   -4993,   20123,   20123,   -4993,    1540},
                                {-1064,    2817,   -6694,   31589,    6837,    -990,    -209},
                                 {-209,    -990,    6837,   31589,   -6694,    2817,   -1064}};

 static int interp_pitch(
 spx_word16_t *exc,          /*decoded excitation*/
 spx_word16_t *interp,          /*decoded excitation*/
 int pitch,               /*pitch period*/
 int len
 )
 {
    int i,j,k;
    spx_word32_t corr[4][7];
    spx_word32_t maxcorr;
    int maxi, maxj;
    for (i=0;i<7;i++)
    {
       corr[0][i] = inner_prod(exc, exc-pitch-3+i, len);
    }
    for (i=0;i<3;i++)
    {
       for (j=0;j<7;j++)
       {
          int i1, i2;
          spx_word32_t tmp=0;
          i1 = 3-j;
          if (i1<0)
             i1 = 0;
          i2 = 10-j;
          if (i2>7)
             i2 = 7;
          for (k=i1;k<i2;k++)
             tmp += MULT16_32_Q15(shift_filt[i][k],corr[0][j+k-3]);
          corr[i+1][j] = tmp;
       }
    }
    maxi=maxj=0;
    maxcorr = corr[0][0];
    for (i=0;i<4;i++)
    {
       for (j=0;j<7;j++)
       {
          if (corr[i][j] > maxcorr)
          {
             maxcorr = corr[i][j];
             maxi=i;
             maxj=j;
          }
       }
    }
    for (i=0;i<len;i++)
    {
       spx_word32_t tmp = 0;
       if (maxi>0)
       {
          for (k=0;k<7;k++)
          {
             tmp += MULT16_16(exc[i-(pitch-maxj+3)+k-3],shift_filt[maxi-1][k]);
          }
       } else {
          tmp = SHL32(exc[i-(pitch-maxj+3)],15);
       }
       interp[i] = (spx_word16_t)PSHR32(tmp,15);
    }
    return pitch-maxj+3;
 }

void multicomb(
spx_word16_t *exc,          /*decoded excitation*/
spx_word16_t *new_exc,      /*enhanced excitation*/
spx_coef_t *ak,           /*LPC filter coefs*/
int p,               /*LPC order*/
int nsf,             /*sub-frame size*/
int pitch,           /*pitch period*/
int max_pitch,
spx_word16_t  comb_gain,    /*gain of comb filter*/
char *stack
)
{
   int i;
   VARDECL(spx_word16_t *iexc);
   spx_word16_t old_ener, new_ener;
   int corr_pitch;

   spx_word16_t iexc0_mag, iexc1_mag, exc_mag;
   spx_word32_t corr0, corr1;
   spx_word16_t gain0, gain1;
   spx_word16_t pgain1, pgain2;
   spx_word16_t c1, c2;
   spx_word16_t g1, g2;
   spx_word16_t ngain;
   spx_word16_t gg1, gg2;
   int scaledown=0;
// #if 0 /* Set to 1 to enable full pitch search */
//    int nol_pitch[6];
//    spx_word16_t nol_pitch_coef[6];
//    spx_word16_t ol_pitch_coef;
//    open_loop_nbest_pitch(exc, 20, 120, nsf,
//                          nol_pitch, nol_pitch_coef, 6, stack);
//    corr_pitch=nol_pitch[0];
//    ol_pitch_coef = nol_pitch_coef[0];
//    /*Try to remove pitch multiples*/
//    for (i=1;i<6;i++)
//    {
//       if ((nol_pitch_coef[i]>MULT16_16_Q15(nol_pitch_coef[0],19661)) &&
//
//          (ABS(2*nol_pitch[i]-corr_pitch)<=2 || ABS(3*nol_pitch[i]-corr_pitch)<=3 ||
//          ABS(4*nol_pitch[i]-corr_pitch)<=4 || ABS(5*nol_pitch[i]-corr_pitch)<=5))
//       {
//          corr_pitch = nol_pitch[i];
//       }
//    }
// #else
   corr_pitch = pitch;
//#endif
    (void)ak;
    (void)p;

   ALLOC(iexc, 2*nsf, spx_word16_t);

   interp_pitch(exc, iexc, corr_pitch, 80);
   if (corr_pitch>max_pitch)
      interp_pitch(exc, iexc+nsf, 2*corr_pitch, 80);
   else
      interp_pitch(exc, iexc+nsf, -corr_pitch, 80);


   for (i=0;i<nsf;i++)
   {
      if (ABS16(exc[i])>16383)
      {
         scaledown = 1;
         break;
      }
   }
   if (scaledown)
   {
      for (i=0;i<nsf;i++)
         exc[i] = SHR16(exc[i],1);
      for (i=0;i<2*nsf;i++)
         iexc[i] = SHR16(iexc[i],1);
   }
   /*interp_pitch(exc, iexc+2*nsf, 2*corr_pitch, 80);*/

   /*printf ("%d %d %f\n", pitch, corr_pitch, max_corr*ener_1);*/
   iexc0_mag = spx_sqrt(1000+inner_prod(iexc,iexc,nsf));
   iexc1_mag = spx_sqrt(1000+inner_prod(iexc+nsf,iexc+nsf,nsf));
   exc_mag = spx_sqrt(1+inner_prod(exc,exc,nsf));
   corr0  = inner_prod(iexc,exc,nsf);
   if (corr0<0)
      corr0=0;
   corr1 = inner_prod(iexc+nsf,exc,nsf);
   if (corr1<0)
      corr1=0;
   /* Doesn't cost much to limit the ratio and it makes the rest easier */
   if (SHL32(EXTEND32(iexc0_mag),6) < EXTEND32(exc_mag))
      iexc0_mag = ADD16(1,PSHR16(exc_mag,6));
   if (SHL32(EXTEND32(iexc1_mag),6) < EXTEND32(exc_mag))
      iexc1_mag = ADD16(1,PSHR16(exc_mag,6));

   if (corr0 > MULT16_16(iexc0_mag,exc_mag))
      pgain1 = QCONST16(1., 14);
   else
      pgain1 = PDIV32_16(SHL32(PDIV32(corr0, exc_mag),14),iexc0_mag);
   if (corr1 > MULT16_16(iexc1_mag,exc_mag))
      pgain2 = QCONST16(1., 14);
   else
      pgain2 = PDIV32_16(SHL32(PDIV32(corr1, exc_mag),14),iexc1_mag);
   gg1 = PDIV32_16(SHL32(EXTEND32(exc_mag),8), iexc0_mag);
   gg2 = PDIV32_16(SHL32(EXTEND32(exc_mag),8), iexc1_mag);
   if (comb_gain>0)
   {
      c1 = (spx_word16_t)(MULT16_16_Q15(QCONST16(.4,15),comb_gain)+QCONST16(.07,15));
      c2 = (spx_word16_t)(QCONST16(.5,15)+MULT16_16_Q14(QCONST16(1.72,14),(c1-QCONST16(.07,15))));
   } else
   {
      c1=c2=0;
   }
   g1 = (spx_word16_t)(32767 - MULT16_16_Q13(MULT16_16_Q15(c2, pgain1),pgain1));
   g2 = (spx_word16_t)(32767 - MULT16_16_Q13(MULT16_16_Q15(c2, pgain2),pgain2));

   if (g1<c1)
      g1 = c1;
   if (g2<c1)
      g2 = c1;
   g1 = (spx_word16_t)PDIV32_16(SHL32(EXTEND32(c1),14),(spx_word16_t)g1);
   g2 = (spx_word16_t)PDIV32_16(SHL32(EXTEND32(c1),14),(spx_word16_t)g2);
   if (corr_pitch>max_pitch)
   {
      gain0 = (spx_word16_t)MULT16_16_Q15(QCONST16(.7,15),MULT16_16_Q14(g1,gg1));
      gain1 = (spx_word16_t)MULT16_16_Q15(QCONST16(.3,15),MULT16_16_Q14(g2,gg2));
   } else {
      gain0 = (spx_word16_t)MULT16_16_Q15(QCONST16(.6,15),MULT16_16_Q14(g1,gg1));
      gain1 = (spx_word16_t)MULT16_16_Q15(QCONST16(.6,15),MULT16_16_Q14(g2,gg2));
   }
   for (i=0;i<nsf;i++)
      new_exc[i] = ADD16(exc[i], EXTRACT16(PSHR32(ADD32(MULT16_16(gain0,iexc[i]), MULT16_16(gain1,iexc[i+nsf])),8)));
   /* FIXME: compute_rms16 is currently not quite accurate enough (but close) */
   new_ener = compute_rms16(new_exc, nsf);
   old_ener = compute_rms16(exc, nsf);

   if (old_ener < 1)
      old_ener = 1;
   if (new_ener < 1)
      new_ener = 1;
   if (old_ener > new_ener)
      old_ener = new_ener;
   ngain = PDIV32_16(SHL32(EXTEND32(old_ener),14),new_ener);

   for (i=0;i<nsf;i++)
      new_exc[i] = (spx_word16_t)MULT16_16_Q14(ngain, new_exc[i]);

   if (scaledown)
   {
      for (i=0;i<nsf;i++)
         exc[i] = SHL16(exc[i],1);
      for (i=0;i<nsf;i++)
         new_exc[i] = SHL16(SATURATE16(new_exc[i],16383),1);
   }
}
