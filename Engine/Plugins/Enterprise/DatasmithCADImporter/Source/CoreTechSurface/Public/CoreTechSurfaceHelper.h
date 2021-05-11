// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

namespace CADLibrary
{
	struct FImportParameters;
	struct FMeshParameters;
}

class IDatasmithMeshElement;
struct FDatasmithMeshElementPayload;
struct FDatasmithTessellationOptions;
struct FMeshDescription;

namespace CoreTechSurface
{
	CORETECHSURFACE_API void AddSurfaceDataForMesh(const TSharedRef<IDatasmithMeshElement>& InMeshElement, const CADLibrary::FImportParameters& InSceneParameters, const CADLibrary::FMeshParameters&, const FDatasmithTessellationOptions& InTessellationOptions, FDatasmithMeshElementPayload& OutMeshPayload);
	CORETECHSURFACE_API bool LoadFile(const FString& FileName, FMeshDescription& MeshDescription, const CADLibrary::FImportParameters& ImportParameters, CADLibrary::FMeshParameters& MeshParameters);
	CORETECHSURFACE_API bool Tessellate(uint64 MainObjectId, const CADLibrary::FImportParameters& ImportParams, FMeshDescription& Mesh, CADLibrary::FMeshParameters& MeshParameters);
}