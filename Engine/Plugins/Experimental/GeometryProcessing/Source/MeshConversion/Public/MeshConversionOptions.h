// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


struct MESHCONVERSION_API FConversionToMeshDescriptionOptions
{
public:
	/** Should triangle groups be transfered to MeshDescription via custom PolyTriGroups attribute */
	bool bSetPolyGroups = true;
};