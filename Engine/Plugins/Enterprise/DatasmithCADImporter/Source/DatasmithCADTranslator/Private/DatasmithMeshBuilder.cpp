// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "DatasmithMeshBuilder.h"

#ifdef CAD_INTERFACE
#include "CoreTechHelper.h"
#endif // CAD_INTERFACE

#include "CoreTechFileParser.h"
#include "DatasmithMeshHelper.h"
#include "IDatasmithSceneElements.h"

#include "HAL/FileManager.h"
#include "MeshDescription.h"
#include "Misc/Paths.h"

using namespace CADLibrary;

FDatasmithMeshBuilder::FDatasmithMeshBuilder(TMap<FString, FString>& InCADFileToUE4GeomMap, TMap< TSharedPtr< IDatasmithMeshElement >, uint32 >& InMeshElementToCTBodyUuidMap, const FString& InCachePath, const CADLibrary::FImportParameters& InImportParameters)
	: CachePath(InCachePath)
	, CADFileToMeshFileMap(InCADFileToUE4GeomMap)
	, MeshElementToBodyUuidMap(InMeshElementToCTBodyUuidMap)
	, ImportParameters(InImportParameters)
{
	LoadMeshFiles();
}

void FDatasmithMeshBuilder::LoadMeshFiles()
{
	Meshes.Reserve(CADFileToMeshFileMap.Num());
	for (const auto& FilePair : CADFileToMeshFileMap)
	{
		FString MeshFile = FPaths::Combine(CachePath, TEXT("mesh"), FilePair.Value + TEXT(".gm"));
		if (!IFileManager::Get().FileExists(*MeshFile))
		{
			continue;
		}

		Meshes.Emplace(MeshFile, BodyUuidToMeshMap);
	}
}

TOptional<FMeshDescription> FDatasmithMeshBuilder::GetMeshDescription(TSharedRef<IDatasmithMeshElement> OutMeshElement, CADLibrary::FMeshParameters& OutMeshParameters)
{
#ifdef CAD_INTERFACE
	uint32* BodyUuid = MeshElementToBodyUuidMap.Find(OutMeshElement);
	if (BodyUuid == nullptr)
	{
		return TOptional<FMeshDescription>();
	}

	FBodyMesh*const* PPBody = BodyUuidToMeshMap.Find(*BodyUuid);
	if(PPBody == nullptr || *PPBody == nullptr)
	{
		return TOptional<FMeshDescription>();
	}

	FBodyMesh& Body = **PPBody;

	TMap<uint32, uint32> MaterialIdToMaterialHash;

	FMeshDescription MeshDescription;
	DatasmithMeshHelper::PrepareAttributeForStaticMesh(MeshDescription);

	if (ConvertCTBodySetToMeshDescription(ImportParameters, OutMeshParameters, Body.GetTriangleCount(), Body.GetTessellationSet(), MaterialIdToMaterialHash, MeshDescription))
	{
		return MoveTemp(MeshDescription);
	}
#endif // CAD_INTERFACE

	return TOptional<FMeshDescription>();
}
