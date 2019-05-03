// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh3.h"
#include "MeshDescription.h"

/**
 * Convert FMeshDescription to FDynamicMesh3
 * 
 * @todo handle missing UV/normals on MD?
 * @todo be able to ignore UV/Normals
 * @todo handle additional UV layers on MD
 * @todo option to disable UV/Normal welding
 */
class MESHCONVERSION_API FMeshDescriptionToDynamicMesh
{
public:
	/** If true, will print some possibly-helpful debugging spew to output log */
	bool bPrintDebugMessages = false;

	/** Should we initialize triangle groups on output mesh */
	bool bEnableOutputGroups = true;

	/**
	 * Various modes can be used to create output triangle groups
	 */
	enum class EPrimaryGroupMode
	{
		SetToZero,
		SetToPolygonID,
		SetToPolygonGroupID,
		SetToPolyGroup
	};
	/**
	 * Which mode to use to create groups on output mesh. Ignored if bEnableOutputGroups = false.
	 */
	EPrimaryGroupMode GroupMode = EPrimaryGroupMode::SetToPolyGroup;


	/**
	 * Default conversion of MeshDescription to DynamicMesh
	 */
	void Convert(const FMeshDescription* MeshIn, FDynamicMesh3& MeshOut);
};