// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Types.h"

namespace CADKernel
{
class FModelMesh;

class CADKERNEL_API FModelMeshReport
{
	const TArray<const FModelMesh*>& ModelMeshes;

	bool GoodOrientation = true;
	int32 BorderEdgeCount = 0;
	int32 NonManifoldEdgeCount = 0;
	int32 CycleCount = 0;
	int32 ChainCount = 0;
	double MaxAngle = 0;

public:
	FModelMeshReport(const TArray<const FModelMesh*>& InModelMeshes)
		: ModelMeshes(InModelMeshes)
	{
	}

	void Run();

	void Print();

};

}

