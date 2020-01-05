// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "CADOptions.h"

#ifdef USE_OPENMODEL

#include "AlAccessTypes.h"
#include "AlShadingFields.h"

class IDatasmithActorElement;
class AlDagNode;
class AlMesh;
class AlPersistentID;
struct FMeshDescription;

typedef double AlMatrix4x4[4][4];


enum class ETesselatorType : uint8
{
	Fast,
	Accurate,
};

enum class EAlShaderModelType : uint8
{
	BLINN,
	LAMBERT,
	LIGHTSOURCE,
	PHONG,
};

namespace OpenModelUtils
{
	const TCHAR * AlObjectTypeToString(AlObjectType type);
	const TCHAR * AlShadingFieldToString(AlShadingFields field);

	void SetActorTransform(const TSharedPtr< IDatasmithActorElement >& ActorElement, AlDagNode& DagNode);

	bool IsValidActor(const TSharedPtr< IDatasmithActorElement >& ActorElement);

	uint32 GetUUIDFromAIPersistentID(AlPersistentID* GroupNodeId);
	uint32 GetUUIDFromAIPersistentID(AlDagNode& GroupNode);
	FString GetPersistentIDString(AlPersistentID* GroupNodeId);
	FString GetUEUUIDFromAIPersistentID(const FString& ParentUEuuid, const FString& CurrentNodePersistentID);

	// Note that Alias file unit is cm like UE
	bool TransferAlMeshToMeshDescription(const AlMesh& Mesh, FMeshDescription& MeshDescription, CADLibrary::FMeshParameters& SymmetricParameters, bool& bHasNormal, bool bMerge = false);

	AlDagNode* TesselateDagLeaf(AlDagNode* DagLeaf, ETesselatorType TessType, double Tolerance);
}


#endif


