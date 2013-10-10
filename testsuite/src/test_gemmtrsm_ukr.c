/*

   BLIS    
   An object-based framework for developing high-performance BLAS-like
   libraries.

   Copyright (C) 2013, The University of Texas

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:
    - Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    - Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    - Neither the name of The University of Texas nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include "blis.h"
#include "test_libblis.h"


// Static variables.
static char*     op_str                    = "gemmtrsm_ukr";
static char*     o_types                   = "m";  // c11
static char*     p_types                   = "u";  // uploa
static thresh_t  thresh[BLIS_NUM_FP_TYPES] = { { 1e-04, 1e-05 },   // warn, pass for s
                                               { 1e-04, 1e-05 },   // warn, pass for c
                                               { 1e-13, 1e-14 },   // warn, pass for d
                                               { 1e-13, 1e-14 } }; // warn, pass for z

// Local prototypes.
void libblis_test_gemmtrsm_ukr_deps( test_params_t* params,
                                     test_op_t*     op );

void libblis_test_gemmtrsm_ukr_experiment( test_params_t* params,
                                           test_op_t*     op,
                                           mt_impl_t      impl,
                                           num_t          datatype,
                                           char*          pc_str,
                                           char*          sc_str,
                                           unsigned int   p_cur,
                                           double*        perf,
                                           double*        resid );

void libblis_test_gemmtrsm_ukr_impl( mt_impl_t impl,
                                     side_t    side,
                                     obj_t*    alpha,
                                     obj_t*    a1x,
                                     obj_t*    a11,
                                     obj_t*    bdx1,
                                     obj_t*    bd11,
                                     obj_t*    b11,
                                     obj_t*    c11 );

void libblis_test_gemmtrsm_ukr_check( side_t  side,
                                      obj_t*  alpha,
                                      obj_t*  a1x,
                                      obj_t*  a11,
                                      obj_t*  bx1,
                                      obj_t*  b11,
                                      obj_t*  c11,
                                      obj_t*  c11_save,
                                      double* resid );

void bli_gemmtrsm_ukr_make_subparts( dim_t  k,
                                     obj_t* a,
                                     obj_t* b,
                                     obj_t* bd,
                                     obj_t* a1x,
                                     obj_t* a11,
                                     obj_t* bx1,
                                     obj_t* b11,
                                     obj_t* bdx1,
                                     obj_t* bd11 );


void libblis_test_gemmtrsm_ukr_deps( test_params_t* params, test_op_t* op )
{
	libblis_test_randv( params, &(op->ops->randv) );
	libblis_test_randm( params, &(op->ops->randm) );
	libblis_test_setv( params, &(op->ops->setv) );
	libblis_test_fnormv( params, &(op->ops->fnormv) );
	libblis_test_subv( params, &(op->ops->subv) );
	libblis_test_scalv( params, &(op->ops->scalv) );
	libblis_test_copym( params, &(op->ops->copym) );
	libblis_test_scalm( params, &(op->ops->scalm) );
	libblis_test_gemv( params, &(op->ops->gemv) );
	libblis_test_trsv( params, &(op->ops->trsv) );
}



void libblis_test_gemmtrsm_ukr( test_params_t* params, test_op_t* op )
{

	// Return early if this test has already been done.
	if ( op->test_done == TRUE ) return;

	// Return early if operation is disabled.
	if ( op->op_switch == DISABLE_ALL ||
	     op->ops->l3ukr_over == DISABLE_ALL ) return;

	// Call dependencies first.
	if ( TRUE ) libblis_test_gemmtrsm_ukr_deps( params, op );

	// Execute the test driver for each implementation requested.
	if ( op->front_seq == ENABLE )
	{
		libblis_test_op_driver( params,
		                        op,
		                        BLIS_TEST_SEQ_UKERNEL,
		                        op_str,
		                        p_types,
		                        o_types,
		                        thresh,
		                        libblis_test_gemmtrsm_ukr_experiment );
	}
}


// Import the register blocksizes used by the micro-kernel(s).
extern blksz_t* gemm_mr;
extern blksz_t* gemm_nr;
extern blksz_t* gemm_kr;

void libblis_test_gemmtrsm_ukr_experiment( test_params_t* params,
                                           test_op_t*     op,
                                           mt_impl_t      impl,
                                           num_t          datatype,
                                           char*          pc_str,
                                           char*          sc_str,
                                           unsigned int   p_cur,
                                           double*        perf,
                                           double*        resid )
{
	unsigned int n_repeats = params->n_repeats;
	unsigned int i;

	double       time_min  = 1e9;
	double       time;

	dim_t        m, n, k;

	char         sc_a = 'c';
	char         sc_b = 'r';

	side_t       side = BLIS_LEFT;
	uplo_t       uploa;

	obj_t        kappa;
	obj_t        alpha;
	obj_t        a_big, a, b, bd;
	obj_t        b11, c11;
	obj_t        ap, bp;
	obj_t        a1xp, a11p, bdx1, bd11, bx1p, b11p;
	obj_t        c11_save;


	// Map the dimension specifier to actual dimensions.
	k = libblis_test_get_dim_from_prob_size( op->dim_spec[0], p_cur );

	// Fix m and n to MR and NR, respectively.
	m = bli_blksz_for_type( datatype, gemm_mr );
	n = bli_blksz_for_type( datatype, gemm_nr );

	// Store the register blocksizes so that the driver can retrieve the
	// values later when printing results.
	op->dim_aux[0] = m;
	op->dim_aux[1] = n;

	// Map parameter characters to BLIS constants.
	bli_param_map_char_to_blis_uplo( pc_str[0], &uploa );

	// Create test scalars.
	bli_obj_init_scalar( datatype, &kappa );
	bli_obj_init_scalar( datatype, &alpha );

	// Create test operands (vectors and/or matrices).
	libblis_test_mobj_create( params, datatype, BLIS_NO_TRANSPOSE,
	                          sc_a,      k+m, k+m, &a_big );
	libblis_test_mobj_create( params, datatype, BLIS_NO_TRANSPOSE,
	                          sc_b,      k+m, n,   &b );
	libblis_test_mobj_create( params, datatype, BLIS_NO_TRANSPOSE,
	                          sc_str[0], m,   n,   &c11 );
	libblis_test_mobj_create( params, datatype, BLIS_NO_TRANSPOSE,
	                          sc_str[0], m,   n,   &c11_save );
	libblis_test_mobj_create( params, datatype, BLIS_NO_TRANSPOSE,
	                          sc_b,      k+m, 4*n, &bd );

	// Set alpha.
	if ( bli_obj_is_real( b ) )
	{
		bli_setsc(  2.0,  0.0, &alpha );
	}
	else
	{
		bli_setsc(  2.0,  0.0, &alpha );
	}

	// Set the structure, uplo, and diagonal offset properties of A.
	bli_obj_set_struc( BLIS_TRIANGULAR, a_big );
	bli_obj_set_uplo( uploa, a_big );

	// Randomize A and make it densely triangular.
	bli_randm( &a_big );

	// Normalize B and save.
	bli_randm( &b );
	bli_setsc( 1.0/( double )m, 0.0, &kappa );
	bli_scalm( &kappa, &b );

	// Use the last m rows of A_big as A.
	bli_acquire_mpart_t2b( BLIS_SUBPART1, k, m, &a_big, &a );

	// Locate the B11 block of B, copy to C11, and save.
	if ( bli_obj_is_lower( a ) ) 
		bli_acquire_mpart_t2b( BLIS_SUBPART1, k, m, &b, &b11 );
	else
		bli_acquire_mpart_t2b( BLIS_SUBPART1, 0, m, &b, &b11 );
	bli_copym( &b11, &c11 );
	bli_copym( &c11, &c11_save );


	// Initialize pack objects.
	bli_obj_init_pack( &ap );
	bli_obj_init_pack( &bp );

	// Create pack objects for a and b.
	libblis_test_pobj_create( gemm_mr,
	                          gemm_mr,
	                          BLIS_INVERT_DIAG,
	                          BLIS_PACKED_ROW_PANELS,
	                          BLIS_BUFFER_FOR_A_BLOCK,
	                          &a, &ap );
	libblis_test_pobj_create( gemm_mr,
	                          gemm_nr,
	                          BLIS_NO_INVERT_DIAG,
	                          BLIS_PACKED_COL_PANELS,
	                          BLIS_BUFFER_FOR_B_PANEL,
	                          &b, &bp );

	// Pack the contents of a to ap.
	bli_packm_blk_var3( &BLIS_ONE, &a, &ap );

	// Pack the contents of b to bp.
	bli_packm_blk_var2( &BLIS_ONE, &b, &bp );


	// Create subpartitions from the a and b panels.
	bli_gemmtrsm_ukr_make_subparts( k, &ap, &bp, &bd,
	                                &a1xp, &a11p, &bx1p, &b11p, &bdx1, &bd11 );


	// Repeat the experiment n_repeats times and record results. 
	for ( i = 0; i < n_repeats; ++i )
	{
		bli_copym( &c11_save, &c11 );

		// Re-pack the contents of b to bp.
		bli_packm_blk_var2( &BLIS_ONE, &b, &bp );

		time = bli_clock();

		libblis_test_gemmtrsm_ukr_impl( impl, side, &alpha,
		                                &a1xp, &a11p, &bdx1, &bd11, &b11p, &c11 );

		time_min = bli_clock_min_diff( time_min, time );
	}

	// Estimate the performance of the best experiment repeat.
	*perf = ( 2.0 * m * n * k + 1.0 * m * m * n ) / time_min / FLOPS_PER_UNIT_PERF;
	if ( bli_obj_is_complex( b ) ) *perf *= 4.0;

	// Perform checks.
	libblis_test_gemmtrsm_ukr_check( side, &alpha,
	                                 &a1xp, &a11p, &bx1p, &b11p, &c11, &c11_save, resid );

	// Zero out performance and residual if output matrix is empty.
	//libblis_test_check_empty_problem( &c11, perf, resid );

	// Release packing buffers within pack objects.
	bli_obj_release_pack( &ap );
	bli_obj_release_pack( &bp );

	// Free the test objects.
	bli_obj_free( &a_big );
	bli_obj_free( &b );
	bli_obj_free( &c11 );
	bli_obj_free( &c11_save );
	bli_obj_free( &bd );
}



void libblis_test_gemmtrsm_ukr_impl( mt_impl_t impl,
                                     side_t    side,
                                     obj_t*    alpha,
                                     obj_t*    a1x,
                                     obj_t*    a11,
                                     obj_t*    bdx1,
                                     obj_t*    bd11,
                                     obj_t*    b11,
                                     obj_t*    c11 )
{
	switch ( impl )
	{
		case BLIS_TEST_SEQ_UKERNEL:
		bli_gemmtrsm_ukr( alpha, a1x, a11, bdx1, bd11, b11, c11 );
		break;

		default:
		libblis_test_printf_error( "Invalid implementation type.\n" );
	}
}



void libblis_test_gemmtrsm_ukr_check( side_t  side,
                                      obj_t*  alpha,
                                      obj_t*  a1x,
                                      obj_t*  a11,
                                      obj_t*  bx1,
                                      obj_t*  b11,
                                      obj_t*  c11,
                                      obj_t*  c11_orig,
                                      double* resid )
{
	num_t  dt      = bli_obj_datatype( *b11 );
	num_t  dt_real = bli_obj_datatype_proj_to_real( *b11 );

	dim_t  m       = bli_obj_length( *b11 );
	dim_t  n       = bli_obj_width( *b11 );
	dim_t  k       = bli_obj_width( *a1x );

	obj_t  kappa, norm;
	obj_t  t, v, w, z;

	double junk;

	//
	// Pre-conditions:
	// - a1x, a11, bx1, c11_orig are randomized; a11 is triangular.
	// - contents of b11 == contents of c11.
	// - side == BLIS_LEFT.
	//
	// Under these conditions, we assume that the implementation for
	//
	//   B := inv(A11) * ( alpha * B11 - A1x * Bx1 )       (side = left)
	//
	// is functioning correctly if
	//
	//   fnorm( v - z )
	//
	// is negligible, where
	//
	//   v = B11 * t
	//
	//   z = ( inv(A11) * ( alpha * B11_orig - A1x * Bx1 ) ) * t
	//     = inv(A11) * ( alpha * B11_orig * t - A1x * Bx1 * t )
	//     = inv(A11) * ( alpha * B11_orig * t - A1x * w )
	//

	bli_obj_init_scalar( dt,      &kappa );
	bli_obj_init_scalar( dt_real, &norm );

	if ( bli_is_left( side ) )
	{
		bli_obj_create( dt, n, 1, 0, 0, &t );
		bli_obj_create( dt, m, 1, 0, 0, &v );
		bli_obj_create( dt, k, 1, 0, 0, &w );
		bli_obj_create( dt, m, 1, 0, 0, &z );
	}
	else // else if ( bli_is_left( side ) )
	{
		// BLIS does not currently support right-side micro-kernels.
		bli_check_error_code( BLIS_NOT_YET_IMPLEMENTED );
	}

	bli_randv( &t );
	bli_setsc( 1.0/( double )n, 0.0, &kappa );
	bli_scalv( &kappa, &t );

	bli_gemv( &BLIS_ONE, b11, &t, &BLIS_ZERO, &v );

	// Restore the diagonal of a11 to its original, un-inverted state
	// (needed for trsv).
	bli_invertd( a11 );

	if ( bli_is_left( side ) )
	{
		bli_gemv( &BLIS_ONE, bx1, &t, &BLIS_ZERO, &w );
		bli_gemv( alpha, c11_orig, &t, &BLIS_ZERO, &z );
		bli_gemv( &BLIS_MINUS_ONE, a1x, &w, &BLIS_ONE, &z );
		bli_trsv( &BLIS_ONE, a11, &z );
	}
	else // else if ( bli_is_left( side ) )
	{
		// BLIS does not currently support right-side micro-kernels.
		bli_check_error_code( BLIS_NOT_YET_IMPLEMENTED );
	}

	bli_subv( &z, &v );
	bli_fnormv( &v, &norm );
	bli_getsc( &norm, resid, &junk );

	bli_obj_free( &t );
	bli_obj_free( &v );
	bli_obj_free( &w );
	bli_obj_free( &z );
}



void bli_gemmtrsm_ukr_make_subparts( dim_t  k,
                                     obj_t* a,
                                     obj_t* b,
                                     obj_t* bd,
                                     obj_t* a1x,
                                     obj_t* a11,
                                     obj_t* bx1,
                                     obj_t* b11,
                                     obj_t* bdx1,
                                     obj_t* bd11 )
{
	dim_t mr = bli_obj_length( *a );
	dim_t nr = bli_obj_width( *b );

	dim_t off_a1x, off_a11;
	dim_t off_bx1, off_b11;
	dim_t off_bdx1, off_bd11;

	if ( bli_obj_is_lower( *a ) )
	{
		off_a1x = 0;
		off_a11 = k;
		off_bx1 = 0;
		off_b11 = k;
		off_bdx1 = 0;
		off_bd11 = k;
	}
	else
	{
		off_a1x = mr;
		off_a11 = 0;
		off_bx1 = mr;
		off_b11 = 0;
		off_bdx1 = mr;
		off_bd11 = 0;
	}

	bli_obj_init_subpart_from( *a, *a1x );
	bli_obj_set_dims( mr, k, *a1x );
	bli_obj_inc_offs( 0, off_a1x, *a1x );

	bli_obj_init_subpart_from( *a, *a11 );
	bli_obj_set_dims( mr, mr, *a11 );
	bli_obj_inc_offs( 0, off_a11, *a11 );

	bli_obj_init_subpart_from( *b, *bx1 );
	bli_obj_set_dims( k, nr, *bx1 );
	bli_obj_inc_offs( off_bx1, 0, *bx1 );

	bli_obj_init_subpart_from( *b, *b11 );
	bli_obj_set_dims( mr, nr, *b11 );
	bli_obj_inc_offs( off_b11, 0, *b11 );

	// Mark a1x as having general structure (which overwrites the triangular
	// property it inherited from a).
	bli_obj_set_struc( BLIS_GENERAL, *a1x );

	// Set the diagonal offset of a11 to 0 (which overwrites the diagonal
	// offset value it inherited from a).
	bli_obj_set_diag_offset( 0, *a11 );

	// If duplication is disabled, alias bdxx objects to bxx.
	if ( TRUE )
	{
		bli_obj_alias_to( *bx1, *bdx1 );
		bli_obj_alias_to( *b11, *bd11 );
	}
	else // if duplication is enabled
	{
		bli_check_error_code( BLIS_NOT_YET_IMPLEMENTED );

		bli_obj_init_subpart_from( *b, *bdx1 );
		bli_obj_set_dims( k, nr, *bdx1 );
		bli_obj_inc_offs( off_bdx1, 0, *bdx1 );

		bli_obj_init_subpart_from( *b, *bd11 );
		bli_obj_set_dims( mr, nr, *bd11 );
		bli_obj_inc_offs( off_bd11, 0, *bd11 );

		// Now update the buffer fields of bdx1, bd11, and then call
		// bli_dupl().
	}
}



//
// Define object-wrapper to GEMMTRSM_L_UKERNEL, GEMMTRSM_U_UKERNEL
// micro-kernels.
//

#undef  FUNCPTR_T
#define FUNCPTR_T gemmtrsm_ukr_fp

typedef void (*FUNCPTR_T)(
                           dim_t   k,
                           void*   alpha,
                           void*   a1x,
                           void*   a11,
                           void*   bdx1,
                           void*   bd11,
                           void*   b11,
                           void*   c11, inc_t rs_c, inc_t cs_c,
                           void*   a_next,
                           void*   b_next
                         );

static FUNCPTR_T GENARRAY(ftypes_l,gemmtrsm_l_ukr);
static FUNCPTR_T GENARRAY(ftypes_u,gemmtrsm_u_ukr);


void bli_gemmtrsm_ukr( obj_t*  alpha,
                       obj_t*  a1x,
                       obj_t*  a11,
                       obj_t*  bdx1,
                       obj_t*  bd11,
                       obj_t*  b11,
                       obj_t*  c11 )
{
	dim_t     k         = bli_obj_width( *a1x );

    num_t     dt        = bli_obj_datatype( *c11 );

    void*     buf_a1x   = bli_obj_buffer_at_off( *a1x );

    void*     buf_a11   = bli_obj_buffer_at_off( *a11 );

    void*     buf_bdx1  = bli_obj_buffer_at_off( *bdx1 );

    void*     buf_bd11  = bli_obj_buffer_at_off( *bd11 );

    void*     buf_b11   = bli_obj_buffer_at_off( *b11 );

    void*     buf_c11   = bli_obj_buffer_at_off( *c11 );
    inc_t     rs_c      = bli_obj_row_stride( *c11 );
    inc_t     cs_c      = bli_obj_col_stride( *c11 );

	void*     buf_alpha = bli_obj_scalar_buffer( dt, *alpha );

    FUNCPTR_T f;

    // Index into the type combination array to extract the correct
    // function pointer.
    if ( bli_obj_is_lower( *a11 ) ) f = ftypes_l[dt];
    else                            f = ftypes_u[dt];

    // Invoke the function.
    f( k,
	   buf_alpha,
	   buf_a1x,
	   buf_a11,
	   buf_bdx1,
	   buf_bd11,
       buf_b11,
       buf_c11, rs_c, cs_c,
	   buf_a1x,
	   buf_bdx1 );
}


#undef  GENTFUNC
#define GENTFUNC( ctype, ch, varname, ukrname ) \
\
void PASTEMAC(ch,varname)( \
                           dim_t   k, \
                           void*   alpha, \
                           void*   a1x, \
                           void*   a11, \
                           void*   bdx1, \
                           void*   bd11, \
                           void*   b11, \
                           void*   c11, inc_t rs_c, inc_t cs_c, \
                           void*   a_next, \
                           void*   b_next  \
                         ) \
{ \
    PASTEMAC(ch,ukrname)( k, \
                          alpha, \
                          a1x, \
                          a11, \
                          bdx1, \
                          bd11, \
                          b11, \
                          c11, rs_c, cs_c, \
	                      a_next, \
	                      b_next ); \
}

INSERT_GENTFUNC_BASIC( gemmtrsm_l_ukr, GEMMTRSM_L_UKERNEL )
INSERT_GENTFUNC_BASIC( gemmtrsm_u_ukr, GEMMTRSM_U_UKERNEL )

