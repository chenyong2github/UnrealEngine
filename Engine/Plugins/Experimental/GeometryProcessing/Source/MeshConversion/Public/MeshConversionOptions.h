// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


struct MESHCONVERSION_API FConversionToMeshDescriptionOptions
{
public:
	/** Should triangle groups be transfered to MeshDescription via custom PolyTriGroups attribute */
	bool bSetPolyGroups = true;


	/** Should Positions of vertices in MeshDescription be updated */
	bool bUpdatePositions = true;

	/** Should normals of MeshDescription be updated, if available and relevant */
	bool bUpdateNormals = true;

	/** Should UVs of MeshDescription be updated, if available and relevant */
	bool bUpdateUVs = false;
};