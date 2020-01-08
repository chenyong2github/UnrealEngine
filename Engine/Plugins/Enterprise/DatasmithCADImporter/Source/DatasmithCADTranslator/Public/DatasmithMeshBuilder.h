// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "CADData.h"
#include "CADOptions.h"

#include "MeshDescription.h"


class IDatasmithMeshElement;


class DATASMITHCADTRANSLATOR_API FDatasmithMeshBuilder
{
public:
	FDatasmithMeshBuilder(TMap<FString, FString>& InCADFileToMeshFileMap, const FString& InCachePath, const CADLibrary::FImportParameters& InImportParameters);

	TOptional<FMeshDescription> GetMeshDescription(TSharedRef<IDatasmithMeshElement> OutMeshElement, CADLibrary::FMeshParameters& OutMeshParameters);

protected:
	FString CachePath;

	void LoadMeshFiles();

	TMap<FString, FString>& CADFileToMeshFile;
	TArray<TArray<CADLibrary::FBodyMesh>> BodyMeshes;
	TMap<CADUUID, CADLibrary::FBodyMesh*> MeshActorNameToBodyMesh;

	CADLibrary::FImportParameters ImportParameters;
};

