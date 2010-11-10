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

#include "SKP_Silk_main.h"

/* Decode indices from payload */
void SKP_Silk_decode_indices(
    SKP_Silk_decoder_state      *psDec,             /* I/O  State                                       */
    ec_dec                      *psRangeDec         /* I/O  Compressor data structure                   */
)
{
    SKP_int   i, k, Ix, FrameIndex;
    SKP_int   sigtype, QuantOffsetType, seed_int, nBytesUsed;
    SKP_int   decode_absolute_lagIndex, delta_lagIndex, prev_lagIndex = 0;
    const SKP_Silk_NLSF_CB_struct *psNLSF_CB = NULL;

    for( FrameIndex = 0; FrameIndex < psDec->nFramesInPacket; FrameIndex++ ) {
        /*******************/
        /* Decode VAD flag */
        /*******************/
        SKP_Silk_range_decoder( &psDec->vadFlagBuf[ FrameIndex ], psRangeDec, SKP_Silk_vadflag_CDF, SKP_Silk_vadflag_offset );

        /*******************************************/
        /* Decode signal type and quantizer offset */
        /*******************************************/
        if( FrameIndex == 0 ) {
            /* first frame in packet: independent coding */
            SKP_Silk_range_decoder( &Ix, psRangeDec, SKP_Silk_type_offset_CDF, SKP_Silk_type_offset_CDF_offset );
        } else {
            /* condidtional coding */
            SKP_Silk_range_decoder( &Ix, psRangeDec, SKP_Silk_type_offset_joint_CDF[ psDec->typeOffsetPrev ], 
                    SKP_Silk_type_offset_CDF_offset );
        }
        sigtype               = SKP_RSHIFT( Ix, 1 );
        QuantOffsetType       = Ix & 1;
        psDec->typeOffsetPrev = Ix;

        /****************/
        /* Decode gains */
        /****************/
        /* first subframe */    
        if( FrameIndex == 0 ) {
            /* first frame in packet: independent coding */
            SKP_Silk_range_decoder( &psDec->GainsIndices[ FrameIndex ][ 0 ], psRangeDec, SKP_Silk_gain_CDF[ sigtype ], SKP_Silk_gain_CDF_offset );
        } else {
            /* condidtional coding */
            SKP_Silk_range_decoder( &psDec->GainsIndices[ FrameIndex ][ 0 ], psRangeDec, SKP_Silk_delta_gain_CDF, SKP_Silk_delta_gain_CDF_offset );
        }

        /* remaining subframes */
        for( i = 1; i < psDec->nb_subfr; i++ ) {
            SKP_Silk_range_decoder( &psDec->GainsIndices[ FrameIndex ][ i ], psRangeDec, SKP_Silk_delta_gain_CDF, SKP_Silk_delta_gain_CDF_offset );
        }
        
        /**********************/
        /* Decode LSF Indices */
        /**********************/

        /* Set pointer to LSF VQ CB for the current signal type */
        psNLSF_CB = psDec->psNLSF_CB[ sigtype ];

        /* Arithmetically decode NLSF path */
        for( i = 0; i < psNLSF_CB->nStages; i++ ) {
            SKP_Silk_range_decoder( &psDec->NLSFIndices[ FrameIndex ][ i ], psRangeDec, psNLSF_CB->StartPtr[ i ], psNLSF_CB->MiddleIx[ i ] );
        }
        
        /***********************************/
        /* Decode LSF interpolation factor */
        /***********************************/
        SKP_Silk_range_decoder( &psDec->NLSFInterpCoef_Q2[ FrameIndex ], psRangeDec, SKP_Silk_NLSF_interpolation_factor_CDF, 
            SKP_Silk_NLSF_interpolation_factor_offset );
        
        if( sigtype == SIG_TYPE_VOICED ) {
            /*********************/
            /* Decode pitch lags */
            /*********************/
            /* Get lag index */
            decode_absolute_lagIndex = 1;
            if( FrameIndex > 0 && psDec->sigtype[ FrameIndex - 1 ] == SIG_TYPE_VOICED ) {
                /* Decode Delta index */
                SKP_Silk_range_decoder( &delta_lagIndex, psRangeDec, SKP_Silk_pitch_delta_CDF,  SKP_Silk_pitch_delta_CDF_offset );
                if( delta_lagIndex < ( MAX_DELTA_LAG << 1 ) + 1 ) {
                    delta_lagIndex = delta_lagIndex - MAX_DELTA_LAG;
                    psDec->lagIndex[ FrameIndex ] = prev_lagIndex + delta_lagIndex;
                    decode_absolute_lagIndex = 0;
                }
            }
            if( decode_absolute_lagIndex ) {
                /* Absolute decoding */
                if( psDec->fs_kHz == 8 ) {
                    SKP_Silk_range_decoder( &psDec->lagIndex[ FrameIndex ], psRangeDec, SKP_Silk_pitch_lag_NB_CDF,  SKP_Silk_pitch_lag_NB_CDF_offset );
                } else if( psDec->fs_kHz == 12 ) {
                    SKP_Silk_range_decoder( &psDec->lagIndex[ FrameIndex ], psRangeDec, SKP_Silk_pitch_lag_MB_CDF,  SKP_Silk_pitch_lag_MB_CDF_offset );
                } else if( psDec->fs_kHz == 16 ) {
                    SKP_Silk_range_decoder( &psDec->lagIndex[ FrameIndex ], psRangeDec, SKP_Silk_pitch_lag_WB_CDF,  SKP_Silk_pitch_lag_WB_CDF_offset );
                } else {
                    SKP_Silk_range_decoder( &psDec->lagIndex[ FrameIndex ], psRangeDec, SKP_Silk_pitch_lag_SWB_CDF, SKP_Silk_pitch_lag_SWB_CDF_offset );
                }
            }
            prev_lagIndex = psDec->lagIndex[ FrameIndex ];

            /* Get countour index */
            if( psDec->fs_kHz == 8 ) {
                /* Less codevectors used in 8 khz mode */
                SKP_Silk_range_decoder( &psDec->contourIndex[ FrameIndex ], psRangeDec, SKP_Silk_pitch_contour_NB_CDF, SKP_Silk_pitch_contour_NB_CDF_offset );
            } else {
                /* Joint for 12, 16, and 24 khz */
                SKP_Silk_range_decoder( &psDec->contourIndex[ FrameIndex ], psRangeDec, SKP_Silk_pitch_contour_CDF, SKP_Silk_pitch_contour_CDF_offset );
            }
            
            /********************/
            /* Decode LTP gains */
            /********************/
            /* Decode PERIndex value */
            SKP_Silk_range_decoder( &psDec->PERIndex[ FrameIndex ], psRangeDec, SKP_Silk_LTP_per_index_CDF, SKP_Silk_LTP_per_index_CDF_offset );
            
            for( k = 0; k < psDec->nb_subfr; k++ ) {
                SKP_Silk_range_decoder( &psDec->LTPIndex[ FrameIndex ][ k ], psRangeDec, SKP_Silk_LTP_gain_CDF_ptrs[ psDec->PERIndex[ FrameIndex ] ], 
                    SKP_Silk_LTP_gain_CDF_offsets[ psDec->PERIndex[ FrameIndex ] ] );
            }

            /**********************/
            /* Decode LTP scaling */
            /**********************/
            SKP_Silk_range_decoder( &psDec->LTP_scaleIndex[ FrameIndex ], psRangeDec, SKP_Silk_LTPscale_CDF, SKP_Silk_LTPscale_offset );
        }

        /***************/
        /* Decode seed */
        /***************/
        SKP_Silk_range_decoder( &seed_int, psRangeDec, SKP_Silk_Seed_CDF, SKP_Silk_Seed_offset );
        psDec->Seed[ FrameIndex ] = ( SKP_int32 )seed_int;

        psDec->sigtype[ FrameIndex ]         = sigtype;
        psDec->QuantOffsetType[ FrameIndex ] = QuantOffsetType;
    }

    /**************************************/
    /* Decode Frame termination indicator */
    /**************************************/
    SKP_Silk_range_decoder( &psDec->FrameTermination, psRangeDec, SKP_Silk_FrameTermination_CDF, SKP_Silk_FrameTermination_offset );

    /****************************************/
    /* get number of bytes used so far      */
    /****************************************/
    nBytesUsed = SKP_RSHIFT( ec_dec_tell( psRangeDec, 0 ) + 7, 3 );
    psDec->nBytesLeft = psRangeDec->buf->storage - nBytesUsed;
}