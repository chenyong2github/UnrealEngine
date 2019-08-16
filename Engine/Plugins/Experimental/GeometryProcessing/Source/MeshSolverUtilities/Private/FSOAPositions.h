// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if defined(_MSC_VER) && USING_CODE_ANALYSIS
#pragma warning(push)
#pragma warning(disable : 6011)
#pragma warning(disable : 6387)
#pragma warning(disable : 6313)
#pragma warning(disable : 6294)
#endif
THIRD_PARTY_INCLUDES_START
#include <Eigen/Core>
THIRD_PARTY_INCLUDES_END
#if defined(_MSC_VER) && USING_CODE_ANALYSIS
#pragma warning(pop)
#endif

#include "FSparseMatrixD.h"

/**
* A struct of arrays representation used to hold vertex positions 
* in three vectors that can interface with the eigen library
*/
class FSOAPositions
{
public:
	typedef typename FSparseMatrixD::Scalar  ScalarType;
	typedef Eigen::Matrix<ScalarType, Eigen::Dynamic, 1>  VectorType;


	FSOAPositions(int32 Size)
		: XVector(Size)
		, YVector(Size)
		, ZVector(Size)
	{}

	FSOAPositions()
	{}

	VectorType& Array(int32 i)
	{
		return (i == 0) ? XVector : (i == 1) ? YVector : ZVector;
	}
	const VectorType& Array(int32 i) const
	{
		return (i == 0) ? XVector : (i == 1) ? YVector : ZVector;
	}

	VectorType XVector;
	VectorType YVector;
	VectorType ZVector;

	void SetZero(int32 NumElements)
	{
		XVector.setZero(NumElements);
		YVector.setZero(NumElements);
		ZVector.setZero(NumElements);
	}

	// Test that all the arrays have the same given size.
	bool bHasSize(int32 Size) const
	{
		return (XVector.rows() == Size && YVector.rows() == Size && ZVector.rows() == Size);
	}

	int32 Num() const
	{
		int32 Size = XVector.rows();
		if (!bHasSize(Size))
		{
			Size = -1;
		}
		return Size;
	}

};