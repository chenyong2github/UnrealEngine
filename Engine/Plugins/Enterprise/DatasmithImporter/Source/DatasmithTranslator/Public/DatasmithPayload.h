// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MeshDescription.h"


struct FDatasmithPayload
{
	TArray<class UDatasmithAdditionalData*> AdditionalData;
};

struct FDatasmithMeshElementPayload : public FDatasmithPayload
{
	TArray<FMeshDescription> LodMeshes;
	FMeshDescription CollisionMesh;
	TArray<FVector> CollisionPointCloud; // compatibility, favor the CollisionMesh member
};

struct FDatasmithLevelSequencePayload : public FDatasmithPayload
{
// #ueent_todo: split element in metadata/payload
};
