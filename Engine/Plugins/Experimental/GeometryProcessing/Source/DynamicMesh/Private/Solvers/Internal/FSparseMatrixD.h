// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util/ElementLinearization.h"   // TVector3Arrays


// According to http://eigen.tuxfamily.org/index.php?title=Main_Page 
// SimplicialCholesky, AMD ordering, and constrained_cg are disabled.

#ifndef EIGEN_MPL2_ONLY
#define EIGEN_MPL2_ONLY
#endif

#if defined(_MSC_VER) && USING_CODE_ANALYSIS
#pragma warning(push)
#pragma warning(disable : 6011)
#pragma warning(disable : 6387)
#pragma warning(disable : 6313)
#pragma warning(disable : 6294)
#endif
THIRD_PARTY_INCLUDES_START
#include <Eigen/Sparse>
THIRD_PARTY_INCLUDES_END
#if defined(_MSC_VER) && USING_CODE_ANALYSIS
#pragma warning(pop)
#endif


// NB: The LU solver likes ColMajor but the CG sovler likes RowMajor
//     Also, to change everything to float / double just change the scalar type here

typedef Eigen::SparseMatrix<double, Eigen::ColMajor>  FSparseMatrixD;



/**
* A struct of arrays representation used to hold vertex positions
* in three vectors that can interface with the eigen library
*/
class FSOAPositions : public TVector3Arrays<double>
{
public:
	typedef typename FSparseMatrixD::Scalar ScalarType;
	typedef Eigen::Matrix<ScalarType, Eigen::Dynamic, 1>  RealVectorType;
	typedef Eigen::Map<RealVectorType> VectorType;
	typedef Eigen::Map<const RealVectorType> ConstVectorType;

	FSOAPositions(int32 Size) : TVector3Arrays<double>(Size)
	{ }

	FSOAPositions()
	{}

	VectorType Array(int32 i)
	{
		TArray<ScalarType>& Column = (i == 0) ? XVector : (i == 1) ? YVector : ZVector;
		return (Column.Num() > 0) ? VectorType(&Column[0], Column.Num()) : VectorType(nullptr, 0);
	}
	ConstVectorType Array(int32 i) const
	{
		const TArray<ScalarType>& Column = (i == 0) ? XVector : (i == 1) ? YVector : ZVector;
		return (Column.Num() > 0) ? ConstVectorType(&Column[0], Column.Num()) : ConstVectorType(nullptr, 0);
	}

};