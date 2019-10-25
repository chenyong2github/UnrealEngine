// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "CADData.h"
#include "CADOptions.h"

#include "MeshDescription.h"


class IDatasmithMeshElement;


class FDatasmithMeshBuilder
{
public:
	FDatasmithMeshBuilder(TMap<FString, FString>& InCADFileToMeshFileMap, TMap< TSharedPtr< IDatasmithMeshElement >, uint32 >& InMeshElementToCADBodyUuidMap, const FString& InCachePath, const CADLibrary::FImportParameters& InImportParameters);

	TOptional<FMeshDescription> GetMeshDescription(TSharedRef<IDatasmithMeshElement> OutMeshElement, CADLibrary::FMeshParameters& OutMeshParameters);

protected:
	FString CachePath;

	void LoadMeshFiles();

	TArray<CADLibrary::FCADMeshFile> Meshes;
	TMap<FString, FString>& CADFileToMeshFileMap;
	TMap< TSharedPtr< IDatasmithMeshElement >, uint32 >& MeshElementToBodyUuidMap;
	TMap< uint32, CADLibrary::FBodyMesh* > BodyUuidToMeshMap;

	CADLibrary::FImportParameters ImportParameters;
};

