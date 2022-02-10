// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

namespace CADLibrary
{
	class FImportParameters;
	struct FMeshParameters;
}

struct FDatasmithMeshElementPayload;
struct FDatasmithTessellationOptions;
struct FMeshDescription;

namespace CoreTechSurface
{
	CORETECHSURFACE_API bool LoadFile(const FString& FileName, const CADLibrary::FImportParameters& InImportParameters, const CADLibrary::FMeshParameters& InMeshParameters, FMeshDescription& OutMeshDescription);
	CORETECHSURFACE_API bool Tessellate(uint64 MainObjectId, const CADLibrary::FImportParameters& InImportParameters, const CADLibrary::FMeshParameters& InMeshParameters, FMeshDescription& OutMesh);
}