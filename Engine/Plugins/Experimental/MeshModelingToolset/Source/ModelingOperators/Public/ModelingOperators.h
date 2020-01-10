// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh3.h"
#include "TransformTypes.h"
#include "Util/ProgressCancel.h"


/**
 * FDynamicMeshOperator is a base interface for operator implementations that can produce 
 * a FDynamicMesh3 with associated FTransform3d. This class exists so we can write generic code 
 * that works with any type of mesh operation of this style. 
 */
class FDynamicMeshOperator
{
protected:
	TUniquePtr<FDynamicMesh3> ResultMesh;
	FTransform3d ResultTransform;

public:
	FDynamicMeshOperator()
	{
		ResultMesh = MakeUnique<FDynamicMesh3>();
		ResultTransform = FTransform3d::Identity();
	}
	virtual ~FDynamicMeshOperator()
	{
	}

	/**
	 * @return ownership of the internal mesh that CalculateResult() produced
	 */
	TUniquePtr<FDynamicMesh3> ExtractResult()
	{
		return MoveTemp(ResultMesh);
	}

	/**
	 * @return the transform applied to the mesh produced by CalculateResult()
	 */
	const FTransform3d& GetResultTransform()
	{
		return ResultTransform;
	}

	/**
	 * Calculate the result of the operator. This will populate the internal Mesh and Transform.
	 * @param Progress implementors can use this object to report progress and determine if they should halt and terminate expensive computations
	 */
	virtual void CalculateResult(FProgressCancel* Progress) = 0;
};





/**
 * A IDynamicMeshOperatorFactory is a base interface to a factory that
 * creates FDynamicMeshOperators
 */
class IDynamicMeshOperatorFactory
{
public:
	virtual ~IDynamicMeshOperatorFactory() {}

	virtual TSharedPtr<FDynamicMeshOperator> MakeNewOperator() = 0;
};


