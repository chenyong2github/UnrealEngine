// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "DatasmithCore.h"
#include "MeshDescription.h"
#include "Misc/SecureHash.h"

class FArchive;


struct FDatasmithMeshModels
{
	FString MeshName;
	bool bIsCollisionMesh = false;
	TArray<FMeshDescription> SourceModels;

	DATASMITHCORE_API friend void operator << (FArchive& Ar, FDatasmithMeshModels& Models);
};

struct DATASMITHCORE_API FDatasmithPackedMeshes
{
	TArray<FDatasmithMeshModels> Meshes;

	FMD5Hash Serialize(FArchive& Ar, bool bSaveCompressed=true);
};

DATASMITHCORE_API FDatasmithPackedMeshes GetDatasmithMeshFromFile(const FString& MeshPath);
DATASMITHCORE_API FDatasmithPackedMeshes GetDatasmithClothFromFile(const FString& Path);

