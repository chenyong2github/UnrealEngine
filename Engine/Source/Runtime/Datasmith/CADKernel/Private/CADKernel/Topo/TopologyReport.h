// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Chrono.h"
#include "CADKernel/Core/Types.h"
#include "CADKernel/UI/Message.h"

namespace CADKernel
{

class FBody;
class FTopologicalEdge;
class FTopologicalFace;
class FTopologicalEntity;
class FShell;

class FTopologyReport
{
private:
	int32 BodyCount = 0;
	int32 ShellCount = 0;
	int32 FaceCount = 0;
	int32 EdgeCount = 0;

	int32 CoedgeCount = 0;
	int32 NonManifoldEdgeCount = 0;
	int32 SurfaceEdgeCount = 0;
	int32 BorderEdgeCount = 0;

	int32 LoopCount = 0;
	int32 ChainCount = 0;

	TArray<const FTopologicalEdge*> Edges;
	TArray<const FTopologicalEntity*> Entities;

	bool HasMarker(const FTopologicalEntity* Entity);

public:

	FTopologyReport()
	{
		Entities.Reserve(100000);
		Edges.Reserve(20000);
	}

	void Add(const FBody* Body);
	void Add(const FTopologicalEdge* Edge);
	void Add(const FTopologicalFace* Face);
	void Add(const FShell* Shell);

	void CountLoops();

	void Print();
};

}
