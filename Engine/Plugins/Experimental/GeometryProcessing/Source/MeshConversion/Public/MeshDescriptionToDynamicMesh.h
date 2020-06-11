// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh3.h"
#include "MeshDescription.h"

// predeclare tangents template
template<typename RealType> class TMeshTangents;

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

	/** Should we calculate conversion index maps */
	bool bCalculateMaps = true;

	/** Ignore all mesh attributes (e.g. UV/Normal layers, material groups) */
	bool bDisableAttributes = false;

	/** map from triangle ID to (polygon,triangle) pair */
	TArray<FIndex2i> TriToPolyTriMap;


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

	/**
	 * Copy tangents from MeshDescription to a FMeshTangents instance.
	 * @warning Convert() must have been used to create the TargetMesh before calling this function
	 */
	void CopyTangents(const FMeshDescription* SourceMesh, const FDynamicMesh3* TargetMesh, TMeshTangents<float>* TangentsOut);
};