// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

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

