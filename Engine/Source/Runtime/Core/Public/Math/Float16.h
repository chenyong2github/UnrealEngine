// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Serialization/Archive.h"
#include "Math/UnrealMathUtility.h"
#include "Math/Float32.h"
#include "Serialization/MemoryLayout.h"

/**
* 16 bit float components and conversion
*
*
* IEEE float 16
* Represented by 10-bit mantissa M, 5-bit exponent E, and 1-bit sign S
*
* Specials:
* 
* E=0, M=0			== 0.0
* E=0, M!=0			== Denormalized value (M / 2^10) * 2^-14
* 0<E<31, M=any		== (1 + M / 2^10) * 2^(E-15)
* E=31, M=0			== Infinity
* E=31, M!=0		== NAN
* 
* conversion from 32 bit float is with RTNE (round to nearest even)
*
*/
class FFloat16
{
public:

	uint16 Encoded;

	/** Default constructor */
	FFloat16();

	/** Copy constructor. */
	FFloat16(const FFloat16& FP16Value);

	/** Conversion constructor. Convert from Fp32 to Fp16. */
	FFloat16(float FP32Value);	

	/** Assignment operator. Convert from Fp32 to Fp16. */
	FFloat16& operator=(float FP32Value);

	/** Assignment operator. Copy Fp16 value. */
	FFloat16& operator=(const FFloat16& FP16Value);

	/** Convert from Fp16 to Fp32. */
	operator float() const;

	/** Convert from Fp32 to Fp16. */
	void Set(float FP32Value);
	
	/** Convert from Fp16 to Fp32. */
	float GetFloat() const;

	/** Is the float negative without converting */
	bool IsNegative() const
	{
		// negative if sign bit is on
		// can be tested with int compare
		return (int16)Encoded < 0;
	}

	/**
	 * Serializes the FFloat16.
	 *
	 * @param Ar Reference to the serialization archive.
	 * @param V Reference to the FFloat16 being serialized.
	 *
	 * @return Reference to the Archive after serialization.
	 */
	friend FArchive& operator<<(FArchive& Ar, FFloat16& V)
	{
		return Ar << V.Encoded;
	}
};
template<> struct TCanBulkSerialize<FFloat16> { enum { Value = true }; };

DECLARE_INTRINSIC_TYPE_LAYOUT(FFloat16);

FORCEINLINE FFloat16::FFloat16()
	:	Encoded(0)
{ }


FORCEINLINE FFloat16::FFloat16(const FFloat16& FP16Value)
{
	Encoded = FP16Value.Encoded;
}


FORCEINLINE FFloat16::FFloat16(float FP32Value)
{
	Set(FP32Value);
}	


FORCEINLINE FFloat16& FFloat16::operator=(float FP32Value)
{
	Set(FP32Value);
	return *this;
}


FORCEINLINE FFloat16& FFloat16::operator=(const FFloat16& FP16Value)
{
	Encoded = FP16Value.Encoded;
	return *this;
}


FORCEINLINE FFloat16::operator float() const
{
	return GetFloat();
}


FORCEINLINE void FFloat16::Set(float FP32Value)
{
	// FPlatformMath::StoreHalf follows RTNE (round-to-nearest-even) rounding default convention
	FPlatformMath::StoreHalf(&Encoded, FP32Value);
}



FORCEINLINE float FFloat16::GetFloat() const
{
	return FPlatformMath::LoadHalf(&Encoded);
}
