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

class FTopomakerReport
{
private:
	int32 OrientationFixedCount = 0;
	int32 SwappedFaceCount = 0;

public:

	FDuration SewDuration;
	FDuration OrientationDuration;

	FTopomakerReport()
	{
	}

	void AddSwappedFaceCount(int32 InSwappedFaceCount)
	{
		if (InSwappedFaceCount > 0)
		{
			OrientationFixedCount++;
			SwappedFaceCount += InSwappedFaceCount;
		}
	}

	void Print();
};

}
