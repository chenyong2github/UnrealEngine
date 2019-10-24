// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "CADData.h"
#include "CoretechHelper.h"


class IDatasmithMeshElement;

struct FMeshParameters;


class FDatasmithMeshBuilder
{
public:
	FDatasmithMeshBuilder(TMap<FString, FString>& InCADFileToUE4GeomMap, TMap< TSharedPtr< IDatasmithMeshElement >, uint32 >& InMeshElementToCTBodyUuidMap);
	
	void Init(const FString& InCachePath);

	TOptional<FMeshDescription> GetMeshDescription(TSharedRef<IDatasmithMeshElement> OutMeshElement, CADLibrary::FMeshParameters& OutMeshParameters);
	void LoadRawDataGeom();
	void Clear();

	void SetImportParameters(const CADLibrary::FImportParameters& InImportParameters)
	{
		ImportParameters = InImportParameters;
	}

protected:
	//const FDatasmithSceneSource& Source;
	FString CachePath;

	TArray<CADLibrary::FCTRawGeomFile> RawDataArray;

	/** Map linking Cad file to RawGeom file (*.gm) */
	TMap<FString, FString>& CADFileToUE4GeomMap;

	/** Datasmith mesh elements to BodyUuid */
	TMap< TSharedPtr< IDatasmithMeshElement >, uint32 >& MeshElementToBodyUuidMap;

	/** BodyUuid to CTBody */
	TMap< uint32, CADLibrary::FBody* > BodyUuidToCADBRepMap;

	CADLibrary::FImportParameters ImportParameters;
};

