// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once


/**
 * Cast VectorRegister4Int in VectorRegister4Float
 *
 * @param V	vector
 * @return		VectorRegister4Float( B.x, A.y, A.z, A.w)
 */
#if !defined(_MSC_VER)|| PLATFORM_ENABLE_VECTORINTRINSICS_NEON
FORCEINLINE VectorRegister4Float VectorCast4IntTo4Float(const VectorRegister4Int& V)
{
	return VectorRegister4Float(V);
}
#else
FORCEINLINE VectorRegister4Float VectorCast4IntTo4Float(const VectorRegister4Int& V)
{
	return _mm_castsi128_ps(V);
}

#endif

/**
 * Cast VectorRegister4Float in VectorRegister4Int
 *
 * @param V	vector
 * @return		VectorCast4FloatTo4Int( B.x, A.y, A.z, A.w)
 */
#if !defined(_MSC_VER) || PLATFORM_ENABLE_VECTORINTRINSICS_NEON
FORCEINLINE VectorRegister4Int VectorCast4FloatTo4Int(const VectorRegister4Float& V)
{
	return VectorRegister4Int(V);
}
#else
FORCEINLINE VectorRegister4Int VectorCast4FloatTo4Int(const VectorRegister4Float& V)
{
	return _mm_castps_si128(V);
}
#endif


#if PLATFORM_ENABLE_VECTORINTRINSICS_NEON

/**
 * Selects and interleaves the lower two SP FP values from A and B.
 *
 * @param A	1st vector
 * @param B	2nd vector
 * @return		VectorRegister4Float( A.x, B.x, A.y, B.y)
 */
FORCEINLINE VectorRegister4Float VectorUnpackLo(const VectorRegister4Float& A, const VectorRegister4Float& B)
{
	return vzip1q_f32(A, B);
}

/**
 * Moves the lower 2 SP FP values of b to the upper 2 SP FP values of the result. The lower 2 SP FP values of a are passed through to the result.
 *
 * @param A	1st vector
 * @param B	2nd vector
 * @return		VectorRegister4Float( A.x, A.y, B.x, B.y)
  */
FORCEINLINE VectorRegister4Float VectorMoveLh(const VectorRegister4Float& A, const VectorRegister4Float& B)
{
	return vzip1q_f64(A, B);
}

/**
 * Return square root.
 *
 * @param A	1st vector
  * @return		VectorRegister4Float( sqrt(A.x), sqrt(A.y), sqrt(A.z), sqrt(A.w))
  */
FORCEINLINE VectorRegister4Float VectorSqrt(const VectorRegister4Float& A)
{
	return vsqrtq_f32(A);
}


#else // PLATFORM_ENABLE_VECTORINTRINSICS_NEON

/**
 * Selects and interleaves the lower two SP FP values from A and B.
 *
 * @param A	1st vector
 * @param B	2nd vector
 * @return		VectorRegister4Float( A.x, B.x, A.y, B.y)
 */
FORCEINLINE VectorRegister4Float VectorUnpackLo(const VectorRegister4Float& A, const VectorRegister4Float& B)
{
	return _mm_unpacklo_ps(A, B);
}

/**
 * Moves the lower 2 SP FP values of b to the upper 2 SP FP values of the result. The lower 2 SP FP values of a are passed through to the result.
 *
 * @param A	1st vector
 * @param B	2nd vector
 * @return		VectorRegister4Float( A.x, A.y, B.x, B.y)
  */
FORCEINLINE VectorRegister4Float VectorMoveLh(const VectorRegister4Float& A, const VectorRegister4Float& B)
{
	return _mm_movelh_ps(A, B);
}

/**
 * Return square root.
 *
 * @param A	1st vector
  * @return		VectorRegister4Float( sqrt(A.x), sqrt(A.y), sqrt(A.z), sqrt(A.w))
  */
FORCEINLINE VectorRegister4Float VectorSqrt(const VectorRegister4Float& A)
{
	return _mm_sqrt_ps(A);
}

#endif // PLATFORM_ENABLE_VECTORINTRINSICS_NEON


