/***********************************************************************
Copyright (c) 2006-2010, Skype Limited. All rights reserved. 
Redistribution and use in source and binary forms, with or without 
modification, (subject to the limitations in the disclaimer below) 
are permitted provided that the following conditions are met:
- Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright 
notice, this list of conditions and the following disclaimer in the 
documentation and/or other materials provided with the distribution.
- Neither the name of Skype Limited, nor the names of specific 
contributors, may be used to endorse or promote products derived from 
this software without specific prior written permission.
NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED 
BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND 
CONTRIBUTORS ''AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND 
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE 
COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF 
USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON 
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***********************************************************************/

/*****************************************************************************
*
* Pitch analyser function
*
******************************************************************************/
#include "SKP_Silk_SigProc_FLP.h"
#include "SKP_Silk_SigProc_FIX.h"
#include "SKP_Silk_pitch_est_defines_FLP.h"
#include "SKP_Silk_common_pitch_est_defines.h"

#define SCRATCH_SIZE    22

/************************************************************/
/* Definitions                                              */
/************************************************************/
#define eps                     1.192092896e-07f

/* using log2() helps the fixed-point conversion */
SKP_INLINE SKP_float SKP_P_log2(double x) { return (SKP_float)(3.32192809488736 * log10(x)); }

/************************************************************/
/* Internally used functions                                */
/************************************************************/
static void SKP_P_Ana_calc_corr_st3(
    SKP_float cross_corr_st3[ PE_MAX_NB_SUBFR ][ PE_NB_CBKS_STAGE3_MAX ][ PE_NB_STAGE3_LAGS ], /* O 3 DIM correlation array */
    const SKP_float signal[],           /* I vector to correlate                                            */
    SKP_int         start_lag,          /* I start lag                                                      */
    SKP_int         sf_length,          /* I sub frame length                                               */
    SKP_int         nb_subfr,           /* I number of subframes                                            */
    SKP_int         complexity          /* I Complexity setting                                             */
);

static void SKP_P_Ana_calc_energy_st3(
    SKP_float energies_st3[ PE_MAX_NB_SUBFR ][ PE_NB_CBKS_STAGE3_MAX ][ PE_NB_STAGE3_LAGS ], /* O 3 DIM correlation array */
    const SKP_float signal[],           /* I vector to correlate                                            */
    SKP_int         start_lag,          /* I start lag                                                      */
    SKP_int         sf_length,          /* I sub frame length                                               */
    SKP_int         nb_subfr,           /* I number of subframes                                            */
    SKP_int         complexity          /* I Complexity setting                                             */
);

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
//%             CORE PITCH ANALYSIS FUNCTION                %
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
SKP_int SKP_Silk_pitch_analysis_core_FLP( /* O voicing estimate: 0 voiced, 1 unvoiced                       */
    const SKP_float *signal,            /* I signal of length PE_FRAME_LENGTH_MS*Fs_kHz              */
    SKP_int         *pitch_out,         /* O 4 pitch lag values                                             */
    SKP_int         *lagIndex,          /* O lag Index                                                      */
    SKP_int         *contourIndex,      /* O pitch contour Index                                            */
    SKP_float       *LTPCorr,           /* I/O normalized correlation; input: value from previous frame     */
    SKP_int         prevLag,            /* I last lag of previous frame; set to zero is unvoiced            */
    const SKP_float search_thres1,      /* I first stage threshold for lag candidates 0 - 1                 */
    const SKP_float search_thres2,      /* I final threshold for lag candidates 0 - 1                       */
    const SKP_int   Fs_kHz,             /* I sample frequency (kHz)                                         */
    const SKP_int   complexity,         /* I Complexity setting, 0-2, where 2 is highest                    */
    const SKP_int   nb_subfr            /* I    number of 5 ms subframes                                    */
)
{
    SKP_float signal_8kHz[ PE_MAX_FRAME_LENGTH_MS * 8 ];
    SKP_float signal_4kHz[ PE_MAX_FRAME_LENGTH_MS * 4 ];
    SKP_float scratch_mem[ PE_MAX_FRAME_LENGTH * 3 ];
    SKP_float filt_state[ PE_MAX_DECIMATE_STATE_LENGTH ];
    SKP_int   i, k, d, j;
    SKP_float threshold, contour_bias;
    SKP_float C[ PE_MAX_NB_SUBFR][ (PE_MAX_LAG >> 1) + 5 ]; /* use to be +2 but then valgrind reported errors for SWB */
    SKP_float CC[ PE_NB_CBKS_STAGE2_EXT ];
    const SKP_float *target_ptr, *basis_ptr;
    double    cross_corr, normalizer, energy, energy_tmp;
    SKP_int   d_srch[ PE_D_SRCH_LENGTH ];
    SKP_int16 d_comp[ (PE_MAX_LAG >> 1) + 5 ];
    SKP_int   length_d_srch, length_d_comp;
    SKP_float Cmax, CCmax, CCmax_b, CCmax_new_b, CCmax_new;
    SKP_int   CBimax, CBimax_new, lag, start_lag, end_lag, lag_new;
    SKP_int   cbk_offset, cbk_size;
    SKP_float lag_log2, prevLag_log2, delta_lag_log2_sqr;
    SKP_float energies_st3[ PE_MAX_NB_SUBFR ][ PE_NB_CBKS_STAGE3_MAX ][ PE_NB_STAGE3_LAGS ];
    SKP_float cross_corr_st3[ PE_MAX_NB_SUBFR ][ PE_NB_CBKS_STAGE3_MAX ][ PE_NB_STAGE3_LAGS ];
    SKP_int   diff, lag_counter;
    SKP_int   frame_length, frame_length_8kHz, frame_length_4kHz;
    SKP_int   sf_length, sf_length_8kHz, sf_length_4kHz;
    SKP_int   min_lag, min_lag_8kHz, min_lag_4kHz;
    SKP_int   max_lag, max_lag_8kHz, max_lag_4kHz;
    SKP_int   nb_cbk_search;
    const SKP_int8 *Lag_CB_ptr;

    /* Check for valid sampling frequency */
    SKP_assert( Fs_kHz == 8 || Fs_kHz == 12 || Fs_kHz == 16 || Fs_kHz == 24 );

    /* Check for valid complexity setting */
    SKP_assert( complexity >= SigProc_PE_MIN_COMPLEX );
    SKP_assert( complexity <= SigProc_PE_MAX_COMPLEX );

    SKP_assert( search_thres1 >= 0.0f && search_thres1 <= 1.0f );
    SKP_assert( search_thres2 >= 0.0f && search_thres2 <= 1.0f );

    /* Setup frame lengths max / min lag for the sampling frequency */
    frame_length      = ( PE_LTP_MEM_LENGTH_MS + nb_subfr * PE_SUBFR_LENGTH_MS ) * Fs_kHz;
    frame_length_4kHz = ( PE_LTP_MEM_LENGTH_MS + nb_subfr * PE_SUBFR_LENGTH_MS ) * 4;
    frame_length_8kHz = ( PE_LTP_MEM_LENGTH_MS + nb_subfr * PE_SUBFR_LENGTH_MS ) * 8;
    sf_length         = PE_SUBFR_LENGTH_MS * Fs_kHz;
    sf_length_4kHz    = PE_SUBFR_LENGTH_MS * 4;
    sf_length_8kHz    = PE_SUBFR_LENGTH_MS * 8;
    min_lag           = PE_MIN_LAG_MS * Fs_kHz;
    min_lag_4kHz      = PE_MIN_LAG_MS * 4;
    min_lag_8kHz      = PE_MIN_LAG_MS * 8;
    max_lag           = PE_MAX_LAG_MS * Fs_kHz;
    max_lag_4kHz      = PE_MAX_LAG_MS * 4;
    max_lag_8kHz      = PE_MAX_LAG_MS * 8;

    SKP_memset(C, 0, sizeof(SKP_float) * nb_subfr * ((PE_MAX_LAG >> 1) + 5));
    
    /* Resample from input sampled at Fs_kHz to 8 kHz */
    if( Fs_kHz == 12 ) {
        SKP_int16 signal_12[ 12 * PE_MAX_FRAME_LENGTH_MS ];
        SKP_int16 signal_8[   8 * PE_MAX_FRAME_LENGTH_MS ];
        SKP_int32 R23[ 6 ];

        /* Resample to 12 -> 8 khz */
        SKP_memset( R23, 0, 6 * sizeof( SKP_int32 ) );
        SKP_float2short_array( signal_12, signal, frame_length );
        SKP_Silk_resampler_down2_3( R23, signal_8, signal_12, frame_length );
        SKP_short2float_array( signal_8kHz, signal_8, frame_length_8kHz );
    } else if( Fs_kHz == 16 ) {
        if( complexity == SigProc_PE_MAX_COMPLEX ) {
            SKP_assert( 4 <= PE_MAX_DECIMATE_STATE_LENGTH );
            SKP_memset( filt_state, 0, 4 * sizeof(SKP_float) );

            SKP_Silk_decimate2_coarse_FLP( signal, filt_state, signal_8kHz, 
                scratch_mem, frame_length_8kHz );
        } else {
            SKP_assert( 2 <= PE_MAX_DECIMATE_STATE_LENGTH );
            SKP_memset( filt_state, 0, 2 * sizeof(SKP_float) );
            
            SKP_Silk_decimate2_coarsest_FLP( signal, filt_state, signal_8kHz, 
                scratch_mem, frame_length_8kHz );
        }
    } else if( Fs_kHz == 24 ) {
        SKP_int16 signal_24[ PE_MAX_FRAME_LENGTH ];
        SKP_int16 signal_8[ 8 * PE_MAX_FRAME_LENGTH_MS ];
        SKP_int32 filt_state_fix[ 8 ];

        /* Resample to 24 -> 8 khz */
        SKP_float2short_array( signal_24, signal, frame_length );
        SKP_memset( filt_state_fix, 0, 8 * sizeof(SKP_int32) );
        SKP_Silk_resampler_down3( filt_state_fix, signal_8, signal_24, frame_length );
        SKP_short2float_array( signal_8kHz, signal_8, frame_length_8kHz );
    } else {
        SKP_assert( Fs_kHz == 8 );
        SKP_memcpy( signal_8kHz, signal, frame_length_8kHz * sizeof(SKP_float) );
    }

    /* Decimate again to 4 kHz. Set mem to zero */
    if( complexity == SigProc_PE_MAX_COMPLEX ) {
        SKP_assert( 4 <= PE_MAX_DECIMATE_STATE_LENGTH );
        SKP_memset( filt_state, 0, 4 * sizeof(SKP_float) );
        SKP_Silk_decimate2_coarse_FLP( signal_8kHz, filt_state, 
            signal_4kHz, scratch_mem, frame_length_4kHz );
    } else {
        SKP_assert( 2 <= PE_MAX_DECIMATE_STATE_LENGTH );
        SKP_memset( filt_state, 0, 2 * sizeof(SKP_float) ); 
        SKP_Silk_decimate2_coarsest_FLP( signal_8kHz, filt_state, 
            signal_4kHz, scratch_mem, frame_length_4kHz );
    }

    /* Low-pass filter */
    for( i = frame_length_4kHz - 1; i > 0; i-- ) {
        signal_4kHz[ i ] += signal_4kHz[ i - 1 ];
    }

    /******************************************************************************
    * FIRST STAGE, operating in 4 khz
    ******************************************************************************/
    target_ptr = &signal_4kHz[ SKP_LSHIFT( sf_length_4kHz, 2 ) ];
    for( k = 0; k < nb_subfr >> 1; k++ ) {
        /* Check that we are within range of the array */
        SKP_assert( target_ptr >= signal_4kHz );
        SKP_assert( target_ptr + sf_length_8kHz <= signal_4kHz + frame_length_4kHz );

        basis_ptr = target_ptr - min_lag_4kHz;

        /* Check that we are within range of the array */
        SKP_assert( basis_ptr >= signal_4kHz );
        SKP_assert( basis_ptr + sf_length_8kHz <= signal_4kHz + frame_length_4kHz );

        /* Calculate first vector products before loop */
        cross_corr = SKP_Silk_inner_product_FLP( target_ptr, basis_ptr, sf_length_8kHz );
        normalizer = SKP_Silk_energy_FLP( basis_ptr, sf_length_8kHz ) + sf_length_8kHz * 4000.0f;

        C[ 0 ][ min_lag_4kHz ] += (SKP_float)(cross_corr / sqrt(normalizer));

        /* From now on normalizer is computed recursively */
        for(d = min_lag_4kHz + 1; d <= max_lag_4kHz; d++) {
            basis_ptr--;

            /* Check that we are within range of the array */
            SKP_assert( basis_ptr >= signal_4kHz );
            SKP_assert( basis_ptr + sf_length_8kHz <= signal_4kHz + frame_length_4kHz );

            cross_corr = SKP_Silk_inner_product_FLP(target_ptr, basis_ptr, sf_length_8kHz);

            /* Add contribution of new sample and remove contribution from oldest sample */
            normalizer +=
                basis_ptr[ 0 ] * basis_ptr[ 0 ] - 
                basis_ptr[ sf_length_8kHz ] * basis_ptr[ sf_length_8kHz ];
            C[ 0 ][ d ] += (SKP_float)(cross_corr / sqrt( normalizer ));
        }
        /* Update target pointer */
        target_ptr += sf_length_8kHz;
    }

    /* Apply short-lag bias */
    for( i = max_lag_4kHz; i >= min_lag_4kHz; i-- ) {
        C[ 0 ][ i ] -= C[ 0 ][ i ] * i / 4096.0f;
    }

    /* Sort */
    length_d_srch = 4 + 2 * complexity;
    SKP_assert( 3 * length_d_srch <= PE_D_SRCH_LENGTH );
    SKP_Silk_insertion_sort_decreasing_FLP( &C[ 0 ][ min_lag_4kHz ], d_srch, max_lag_4kHz - min_lag_4kHz + 1, length_d_srch );

    /* Escape if correlation is very low already here */
    Cmax = C[ 0 ][ min_lag_4kHz ];
    target_ptr = &signal_4kHz[ SKP_SMULBB( sf_length_4kHz, nb_subfr ) ];
    energy = 1000.0f;
    for( i = 0; i < SKP_LSHIFT( sf_length_4kHz, 2 ); i++ ) {
        energy += target_ptr[i] * target_ptr[i];
    }
    threshold = Cmax * Cmax; 
    if( energy / 16.0f > threshold ) {
        SKP_memset( pitch_out, 0, nb_subfr * sizeof( SKP_int ) );
        *LTPCorr      = 0.0f;
        *lagIndex     = 0;
        *contourIndex = 0;
        return 1;
    }

    threshold = search_thres1 * Cmax;
    for( i = 0; i < length_d_srch; i++ ) {
        /* Convert to 8 kHz indices for the sorted correlation that exceeds the threshold */
        if( C[ 0 ][ min_lag_4kHz + i ] > threshold ) {
            d_srch[ i ] = SKP_LSHIFT( d_srch[ i ] + min_lag_4kHz, 1 );
        } else {
            length_d_srch = i;
            break;
        }
    }
    SKP_assert( length_d_srch > 0 );

    for( i = min_lag_8kHz - 5; i < max_lag_8kHz + 5; i++ ) {
        d_comp[ i ] = 0;
    }
    for( i = 0; i < length_d_srch; i++ ) {
        d_comp[ d_srch[ i ] ] = 1;
    }

    /* Convolution */
    for( i = max_lag_8kHz + 3; i >= min_lag_8kHz; i-- ) {
        d_comp[ i ] += d_comp[ i - 1 ] + d_comp[ i - 2 ];
    }

    length_d_srch = 0;
    for( i = min_lag_8kHz; i < max_lag_8kHz + 1; i++ ) {    
        if( d_comp[ i + 1 ] > 0 ) {
            d_srch[ length_d_srch ] = i;
            length_d_srch++;
        }
    }

    /* Convolution */
    for( i = max_lag_8kHz + 3; i >= min_lag_8kHz; i-- ) {
        d_comp[ i ] += d_comp[ i - 1 ] + d_comp[ i - 2 ] + d_comp[ i - 3 ];
    }

    length_d_comp = 0;
    for( i = min_lag_8kHz; i < max_lag_8kHz + 4; i++ ) {    
        if( d_comp[ i ] > 0 ) {
            d_comp[ length_d_comp ] = i - 2;
            length_d_comp++;
        }
    }

    /**********************************************************************************
    ** SECOND STAGE, operating at 8 kHz, on lag sections with high correlation
    *************************************************************************************/
    /********************************************************************************* 
    * Find energy of each subframe projected onto its history, for a range of delays
    *********************************************************************************/
    SKP_memset( C, 0, PE_MAX_NB_SUBFR*((PE_MAX_LAG >> 1) + 5) * sizeof(SKP_float)); // Is this needed?
    
    target_ptr = &signal_8kHz[ PE_LTP_MEM_LENGTH_MS * 8 ];
    for( k = 0; k < nb_subfr; k++ ) {

        /* Check that we are within range of the array */
        SKP_assert( target_ptr >= signal_8kHz );
        SKP_assert( target_ptr + sf_length_8kHz <= signal_8kHz + frame_length_8kHz );

        energy_tmp = SKP_Silk_energy_FLP( target_ptr, sf_length_8kHz );
        for( j = 0; j < length_d_comp; j++ ) {
            d = d_comp[ j ];
            basis_ptr = target_ptr - d;

            /* Check that we are within range of the array */
            SKP_assert( basis_ptr >= signal_8kHz );
            SKP_assert( basis_ptr + sf_length_8kHz <= signal_8kHz + frame_length_8kHz );
        
            cross_corr = SKP_Silk_inner_product_FLP( basis_ptr, target_ptr, sf_length_8kHz );
            energy     = SKP_Silk_energy_FLP( basis_ptr, sf_length_8kHz );
            if (cross_corr > 0.0f) {
                C[ k ][ d ] = (SKP_float)(cross_corr * cross_corr / (energy * energy_tmp + eps));
            } else {
                C[ k ][ d ] = 0.0f;
            }
        }
        target_ptr += sf_length_8kHz;
    }

    /* search over lag range and lags codebook */
    /* scale factor for lag codebook, as a function of center lag */

    CCmax   = 0.0f; /* This value doesn't matter */
    CCmax_b = -1000.0f;

    CBimax = 0; /* To avoid returning undefined lag values */
    lag = -1;   /* To check if lag with strong enough correlation has been found */

    if( prevLag > 0 ) {
        if( Fs_kHz == 12 ) {
            prevLag = SKP_LSHIFT( prevLag, 1 ) / 3;
        } else if( Fs_kHz == 16 ) {
            prevLag = SKP_RSHIFT( prevLag, 1 );
        } else if( Fs_kHz == 24 ) {
            prevLag = prevLag / 3;
        }
        prevLag_log2 = SKP_P_log2((SKP_float)prevLag);
    } else {
        prevLag_log2 = 0;
    }

    /* Setup stage 2 codebook based on number of subframes */
    if( nb_subfr == PE_MAX_NB_SUBFR ) {
        cbk_size   = PE_NB_CBKS_STAGE2_EXT;
        Lag_CB_ptr = &SKP_Silk_CB_lags_stage2[ 0 ][ 0 ];
        if( Fs_kHz == 8 && complexity > SigProc_PE_MIN_COMPLEX ) {
            /* If input is 8 khz use a larger codebook here because it is last stage */
            nb_cbk_search = PE_NB_CBKS_STAGE2_EXT;
        } else {
            nb_cbk_search = PE_NB_CBKS_STAGE2;
        }
    } else {
        cbk_size       = PE_NB_CBKS_STAGE2_10MS;
        Lag_CB_ptr     = &SKP_Silk_CB_lags_stage2_10_ms[ 0 ][ 0 ];
        nb_cbk_search  = PE_NB_CBKS_STAGE2_10MS;
    }

    for( k = 0; k < length_d_srch; k++ ) {
        d = d_srch[ k ];
        for( j = 0; j < nb_cbk_search; j++ ) {
            CC[j] = 0.0f;
            for( i = 0; i < nb_subfr; i++ ) {
                /* Try all codebooks */
                CC[ j ] += C[ i ][ d + matrix_ptr( Lag_CB_ptr, i, j, cbk_size )];
            }
        }
        /* Find best codebook */
        CCmax_new  = -1000.0f;
        CBimax_new = 0;
        for( i = 0; i < nb_cbk_search; i++ ) {
            if( CC[ i ] > CCmax_new ) {
                CCmax_new = CC[ i ];
                CBimax_new = i;
            }
        }
        CCmax_new = SKP_max_float(CCmax_new, 0.0f); /* To avoid taking square root of negative number later */
        CCmax_new_b = CCmax_new;

        /* Bias towards shorter lags */
        lag_log2 = SKP_P_log2((SKP_float)d);
        CCmax_new_b -= PE_FLP_SHORTLAG_BIAS * nb_subfr * lag_log2;

        /* Bias towards previous lag */
        if ( prevLag > 0 ) {
            delta_lag_log2_sqr = lag_log2 - prevLag_log2;
            delta_lag_log2_sqr *= delta_lag_log2_sqr;
            CCmax_new_b -= PE_FLP_PREVLAG_BIAS * nb_subfr * (*LTPCorr) * delta_lag_log2_sqr / (delta_lag_log2_sqr + 0.5f);
        }

        if ( CCmax_new_b > CCmax_b                                      && /* Find maximum biased correlation                  */
             CCmax_new > nb_subfr * search_thres2 * search_thres2       && /* Correlation needs to be high enough to be voiced */
             SKP_Silk_CB_lags_stage2[ 0 ][ CBimax_new ] <= min_lag_8kHz    /* Lag must be in range                             */
            ) {
            CCmax_b = CCmax_new_b;
            CCmax   = CCmax_new;
            lag     = d;
            CBimax  = CBimax_new;
        }
    }

    if( lag == -1 ) {
        /* No suitable candidate found */
        SKP_memset( pitch_out, 0, PE_MAX_NB_SUBFR * sizeof(SKP_int) );
        *LTPCorr      = 0.0f;
        *lagIndex     = 0;
        *contourIndex = 0;
        return 1;
    }

    if( Fs_kHz > 8 ) {
        /* Search in original signal */

        /* Compensate for decimation */
        SKP_assert( lag == SKP_SAT16( lag ) );
        if( Fs_kHz == 12 ) {
            lag = SKP_RSHIFT_ROUND( SKP_SMULBB( lag, 3 ), 1 );
        } else if( Fs_kHz == 16 ) {
            lag = SKP_LSHIFT( lag, 1 );
        } else {
            lag = SKP_SMULBB( lag, 3 );
        }

        lag = SKP_LIMIT_int( lag, min_lag, max_lag );
        start_lag = SKP_max_int( lag - 2, min_lag );
        end_lag   = SKP_min_int( lag + 2, max_lag );
        lag_new   = lag;                                    /* to avoid undefined lag */
        CBimax    = 0;                                      /* to avoid undefined lag */
        SKP_assert( CCmax >= 0.0f ); 
        *LTPCorr = (SKP_float)sqrt( CCmax / nb_subfr );     /* Output normalized correlation */

        CCmax = -1000.0f;

        /* Calculate the correlations and energies needed in stage 3 */
        SKP_P_Ana_calc_corr_st3( cross_corr_st3, signal, start_lag, sf_length, nb_subfr, complexity );
        SKP_P_Ana_calc_energy_st3( energies_st3, signal, start_lag, sf_length, nb_subfr, complexity );

        lag_counter = 0;
        SKP_assert( lag == SKP_SAT16( lag ) );
        contour_bias = PE_FLP_FLATCONTOUR_BIAS / lag;

        /* Setup cbk parameters acording to complexity setting and frame length */
        if( nb_subfr == PE_MAX_NB_SUBFR ) {
            nb_cbk_search   = (SKP_int)SKP_Silk_nb_cbk_searchs_stage3[   complexity ];
            cbk_size        = PE_NB_CBKS_STAGE3_MAX;
            cbk_offset      = (SKP_int)SKP_Silk_cbk_offsets_stage3[ complexity ];
            Lag_CB_ptr      = &SKP_Silk_CB_lags_stage3[ 0 ][ 0 ];
        } else {
            nb_cbk_search   = PE_NB_CBKS_STAGE3_10MS;
            cbk_size    = PE_NB_CBKS_STAGE3_10MS;
            cbk_offset = 0;
            Lag_CB_ptr = &SKP_Silk_CB_lags_stage3_10_ms[ 0 ][ 0 ];
        }

        for( d = start_lag; d <= end_lag; d++ ) {
            for( j = cbk_offset; j < ( cbk_offset + nb_cbk_search ); j++ ) {
                cross_corr = 0.0;
                energy = eps;
                for( k = 0; k < nb_subfr; k++ ) {
                    energy     +=   energies_st3[ k ][ j ][ lag_counter ];
                    cross_corr += cross_corr_st3[ k ][ j ][ lag_counter ];
                }
                if( cross_corr > 0.0 ) {
                    CCmax_new = (SKP_float)(cross_corr * cross_corr / energy);
                    /* Reduce depending on flatness of contour */
                    diff = j - SKP_RSHIFT( cbk_size, 1 );
                    CCmax_new *= ( 1.0f - contour_bias * diff * diff );
                } else {
                    CCmax_new = 0.0f;               
                }

                if( CCmax_new > CCmax &&
                   ( d + (SKP_int)SKP_Silk_CB_lags_stage3[ 0 ][ j ] ) <= max_lag  
                   ) {
                    CCmax   = CCmax_new;
                    lag_new = d;
                    CBimax  = j;
                }
            }
            lag_counter++;
        }

        for( k = 0; k < nb_subfr; k++ ) {
            pitch_out[ k ] = lag_new + matrix_ptr( Lag_CB_ptr, k, CBimax, cbk_size );
        }
        *lagIndex = lag_new - min_lag;
        *contourIndex = CBimax;
    } else {
        /* Save Lags and correlation */
        SKP_assert( CCmax >= 0.0f );
        *LTPCorr = (SKP_float)sqrt( CCmax / nb_subfr ); /* Output normalized correlation */
        for( k = 0; k < nb_subfr; k++ ) {
            pitch_out[ k ] = lag + SKP_Silk_CB_lags_stage2[ k ][ CBimax ];
        }
        *lagIndex = lag - min_lag;
        *contourIndex = CBimax;
    }
    SKP_assert( *lagIndex >= 0 );
    /* return as voiced */
    return 0;
}

static void SKP_P_Ana_calc_corr_st3(
    SKP_float cross_corr_st3[ PE_MAX_NB_SUBFR ][ PE_NB_CBKS_STAGE3_MAX ][ PE_NB_STAGE3_LAGS ], /* O 3 DIM correlation array */
    const SKP_float signal[],           /* I vector to correlate                                            */
    SKP_int         start_lag,          /* I start lag                                                      */
    SKP_int         sf_length,          /* I sub frame length                                               */
    SKP_int         nb_subfr,           /* I number of subframes                                            */
    SKP_int         complexity          /* I Complexity setting                                             */
)
    /***********************************************************************
     Calculates the correlations used in stage 3 search. In order to cover 
     the whole lag codebook for all the searched offset lags (lag +- 2), 
     the following correlations are needed in each sub frame:

     sf1: lag range [-8,...,7] total 16 correlations
     sf2: lag range [-4,...,4] total 9 correlations
     sf3: lag range [-3,....4] total 8 correltions
     sf4: lag range [-6,....8] total 15 correlations

     In total 48 correlations. The direct implementation computed in worst case 
     4*12*5 = 240 correlations, but more likely around 120. 
     **********************************************************************/
{
    const SKP_float *target_ptr, *basis_ptr;
    SKP_int   i, j, k, lag_counter, lag_low, lag_high;
    SKP_int   cbk_offset, nb_cbk_search, delta, idx, cbk_size;
    SKP_float scratch_mem[ SCRATCH_SIZE ];
    const SKP_int8 *Lag_range_ptr, *Lag_CB_ptr;

    SKP_assert( complexity >= SigProc_PE_MIN_COMPLEX );
    SKP_assert( complexity <= SigProc_PE_MAX_COMPLEX );

    if( nb_subfr == PE_MAX_NB_SUBFR ){
        Lag_range_ptr = &SKP_Silk_Lag_range_stage3[ complexity ][ 0 ][ 0 ];
        Lag_CB_ptr    = &SKP_Silk_CB_lags_stage3[ 0 ][ 0 ];
        cbk_offset    = SKP_Silk_cbk_offsets_stage3[ complexity ];
        nb_cbk_search = SKP_Silk_nb_cbk_searchs_stage3[   complexity ];
        cbk_size      = PE_NB_CBKS_STAGE3_MAX;
    } else {
        SKP_assert( nb_subfr == PE_MAX_NB_SUBFR >> 1);
        Lag_range_ptr = &SKP_Silk_Lag_range_stage3_10_ms[ 0 ][ 0 ];
        Lag_CB_ptr    = &SKP_Silk_CB_lags_stage3_10_ms[ 0 ][ 0 ];
        cbk_offset    = 0;
        nb_cbk_search = PE_NB_CBKS_STAGE3_10MS;
        cbk_size      = PE_NB_CBKS_STAGE3_10MS;
    }

    target_ptr = &signal[ SKP_LSHIFT( sf_length, 2 ) ]; /* Pointer to middle of frame */
    for( k = 0; k < nb_subfr; k++ ) {
        lag_counter = 0;

        /* Calculate the correlations for each subframe */
        lag_low  = matrix_ptr( Lag_range_ptr, k, 0, 2 );
        lag_high = matrix_ptr( Lag_range_ptr, k, 1, 2 );
        for( j = lag_low; j <= lag_high; j++ ) {
            basis_ptr = target_ptr - ( start_lag + j );
            SKP_assert( lag_counter < SCRATCH_SIZE );
            scratch_mem[ lag_counter ] = (SKP_float)SKP_Silk_inner_product_FLP( target_ptr, basis_ptr, sf_length );
            lag_counter++;
        }

        delta = matrix_ptr( Lag_range_ptr, k, 0, 2 );
        for( i = cbk_offset; i < ( cbk_offset + nb_cbk_search ); i++ ) { 
            /* Fill out the 3 dim array that stores the correlations for */
            /* each code_book vector for each start lag */
            idx = matrix_ptr( Lag_CB_ptr, k, i, cbk_size ) - delta;
            for( j = 0; j < PE_NB_STAGE3_LAGS; j++ ) {
                SKP_assert( idx + j < SCRATCH_SIZE );
                SKP_assert( idx + j < lag_counter );
                cross_corr_st3[ k ][ i ][ j ] = scratch_mem[ idx + j ];
            }
        }
        target_ptr += sf_length;
    }
}

static void SKP_P_Ana_calc_energy_st3(
    SKP_float energies_st3[ PE_MAX_NB_SUBFR ][ PE_NB_CBKS_STAGE3_MAX ][ PE_NB_STAGE3_LAGS ], /* O 3 DIM correlation array */
    const SKP_float signal[],           /* I vector to correlate                                            */
    SKP_int         start_lag,          /* I start lag                                                      */
    SKP_int         sf_length,          /* I sub frame length                                               */
    SKP_int         nb_subfr,           /* I number of subframes                                            */
    SKP_int         complexity          /* I Complexity setting                                             */
)
/****************************************************************
Calculate the energies for first two subframes. The energies are
calculated recursively. 
****************************************************************/
{
    const SKP_float *target_ptr, *basis_ptr;
    double    energy;
    SKP_int   k, i, j, lag_counter;
    SKP_int   cbk_offset, nb_cbk_search, delta, idx, cbk_size, lag_diff;
    SKP_float scratch_mem[ SCRATCH_SIZE ];
    const SKP_int8 *Lag_range_ptr, *Lag_CB_ptr;

    SKP_assert( complexity >= SigProc_PE_MIN_COMPLEX );
    SKP_assert( complexity <= SigProc_PE_MAX_COMPLEX );

    if( nb_subfr == PE_MAX_NB_SUBFR ){
        Lag_range_ptr = &SKP_Silk_Lag_range_stage3[ complexity ][ 0 ][ 0 ];
        Lag_CB_ptr    = &SKP_Silk_CB_lags_stage3[ 0 ][ 0 ];
        cbk_offset    = SKP_Silk_cbk_offsets_stage3[ complexity ];
        nb_cbk_search = SKP_Silk_nb_cbk_searchs_stage3[   complexity ];
        cbk_size      = PE_NB_CBKS_STAGE3_MAX;
    } else {
        SKP_assert( nb_subfr == PE_MAX_NB_SUBFR >> 1);
        Lag_range_ptr = &SKP_Silk_Lag_range_stage3_10_ms[ 0 ][ 0 ];
        Lag_CB_ptr    = &SKP_Silk_CB_lags_stage3_10_ms[ 0 ][ 0 ];
        cbk_offset    = 0;
        nb_cbk_search = PE_NB_CBKS_STAGE3_10MS;
        cbk_size      = PE_NB_CBKS_STAGE3_10MS;
    }

    target_ptr = &signal[ SKP_LSHIFT( sf_length, 2 ) ];
    for( k = 0; k < nb_subfr; k++ ) {
        lag_counter = 0;

        /* Calculate the energy for first lag */
        basis_ptr = target_ptr - ( start_lag + matrix_ptr( Lag_range_ptr, k, 0, 2 ) );
        energy = SKP_Silk_energy_FLP( basis_ptr, sf_length ) + 1e-3;
        SKP_assert( energy >= 0.0 );
        scratch_mem[lag_counter] = (SKP_float)energy;
        lag_counter++;

        lag_diff = ( matrix_ptr( Lag_range_ptr, k, 1, 2 ) -  matrix_ptr( Lag_range_ptr, k, 0, 2 ) + 1 );
        for( i = 1; i < lag_diff; i++ ) {
            /* remove part outside new window */
            energy -= basis_ptr[sf_length - i] * basis_ptr[sf_length - i];
            SKP_assert( energy >= 0.0 );

            /* add part that comes into window */
            energy += basis_ptr[ -i ] * basis_ptr[ -i ];
            SKP_assert( energy >= 0.0 );
            SKP_assert( lag_counter < SCRATCH_SIZE );
            scratch_mem[lag_counter] = (SKP_float)energy;
            lag_counter++;
        }

        delta = matrix_ptr( Lag_range_ptr, k, 0, 2 );
        for( i = cbk_offset; i < ( cbk_offset + nb_cbk_search ); i++ ) { 
            /* Fill out the 3 dim array that stores the correlations for    */
            /* each code_book vector for each start lag                     */
            idx = matrix_ptr( Lag_CB_ptr, k, i, cbk_size ) - delta;
            for( j = 0; j < PE_NB_STAGE3_LAGS; j++ ) {
                SKP_assert( idx + j < SCRATCH_SIZE );
                SKP_assert( idx + j < lag_counter );
                energies_st3[ k ][ i ][ j ] = scratch_mem[ idx + j ];
                SKP_assert( energies_st3[ k ][ i ][ j ] >= 0.0f );
            }
        }
        target_ptr += sf_length;
    }
}