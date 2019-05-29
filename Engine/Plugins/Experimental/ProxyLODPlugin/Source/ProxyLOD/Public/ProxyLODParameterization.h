// Copyright 1998 - 2019 Epic Games, Inc.All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FMeshDescription;


class PROXYLODMESHREDUCTION_API IPoxyLODParameterization
{
public:
	static TUniquePtr<IPoxyLODParameterization> CreateTool();
	virtual ~IPoxyLODParameterization(){}
	virtual bool ParameterizeMeshDescription(FMeshDescription& MeshDescription, int32 Width, int32 Height, float GutterSpace, 
		                                     float Stretch, int32 ChartNum, bool bUseNormals, bool bRecomputeTangentSpace, bool bPrintDebugMessages) const = 0;
};
