// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "CADOptions.h"

#ifdef USE_OPENMODEL

#include "AlAccessTypes.h"
#include "AlShadingFields.h"
#include "AlDagNode.h"
#include "AlPersistentID.h"


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

	inline FString UuidToString(const uint32& Uuid)
	{
		return FString::Printf(TEXT("0x%08x"), Uuid);
	}

	inline uint32 GetTypeHash(AlPersistentID& GroupNodeId)
	{
		int IdA, IdB, IdC, IdD;
		GroupNodeId.id(IdA, IdB, IdC, IdD);
		return HashCombine(IdA, HashCombine(IdB, HashCombine(IdC, IdD)));
	}

	inline uint32 GetAlDagNodeUuid(AlDagNode& GroupNode)
	{
		if (GroupNode.hasPersistentID() == sSuccess)
		{
			AlPersistentID* PersistentID;
			GroupNode.persistentID(PersistentID);
			return GetTypeHash(*PersistentID);
		}
		FString Label = GroupNode.name();
		return GetTypeHash(Label);
	}

	// Note that Alias file unit is cm like UE
	bool TransferAlMeshToMeshDescription(const AlMesh& Mesh, FMeshDescription& MeshDescription, CADLibrary::FMeshParameters& SymmetricParameters, bool& bHasNormal, bool bMerge = false);

	AlDagNode* TesselateDagLeaf(AlDagNode* DagLeaf, ETesselatorType TessType, double Tolerance);
}


#endif


