// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MatrixSolver.h"

IMatrixSolverBase::~IMatrixSolverBase() {};
IIterativeMatrixSolverBase::~IIterativeMatrixSolverBase() {};

TUniquePtr<IMatrixSolverBase> ContructMatrixSolver(const EMatrixSolverType& MatrixSolverType)
{
	TUniquePtr<IMatrixSolverBase> ResultPtr;

	switch (MatrixSolverType)
	{
	default:

	case  EMatrixSolverType::LU:
		ResultPtr.Reset(new FLUMatrixSolver());
		break;
	case EMatrixSolverType::QR:
		ResultPtr.Reset(new FQRMatrixSolver());
		break;
	case EMatrixSolverType::PCG:
		ResultPtr.Reset(new FPCGMatrixSolver());
		break;	
	case EMatrixSolverType::BICGSTAB:
		ResultPtr.Reset(new FBiCGMatrixSolver());
		break;
#ifndef EIGEN_MPL2_ONLY
	case EMatrixSolverType::LDLT:
		ResultPtr.Reset(new FLDLTMatrixSolver());
		break;
#endif
	}

	return ResultPtr;
}
