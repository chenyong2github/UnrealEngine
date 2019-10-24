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

FDatasmithMeshBuilder::FDatasmithMeshBuilder(TMap<FString, FString>& InCADFileToUE4GeomMap, TMap< TSharedPtr< IDatasmithMeshElement >, uint32 >& InMeshElementToCTBodyUuidMap)
	: CADFileToUE4GeomMap(InCADFileToUE4GeomMap)
	, MeshElementToBodyUuidMap(InMeshElementToCTBodyUuidMap)
{
}

void FDatasmithMeshBuilder::Init(const FString& InCachePath)
{
	CachePath = InCachePath;
}

void FDatasmithMeshBuilder::Clear()
{
	RawDataArray.Empty();
	BodyUuidToCADBRepMap.Empty();
}

void FDatasmithMeshBuilder::LoadRawDataGeom()
{
	RawDataArray.Reserve(CADFileToUE4GeomMap.Num());
	for (const auto& FilePair : CADFileToUE4GeomMap)
	{
		FString RawDataFile = FPaths::Combine(CachePath, TEXT("mesh"), FilePair.Value + TEXT(".gm"));
		if (!IFileManager::Get().FileExists(*RawDataFile))
		{
			continue;
		}

		int32 index = RawDataArray.Emplace(RawDataFile, BodyUuidToCADBRepMap);
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

	FBody*const* PPBody = BodyUuidToCADBRepMap.Find(*BodyUuid);
	if(PPBody == nullptr || *PPBody == nullptr)
	{
		return TOptional<FMeshDescription>();
	}

	FBody& Body = **PPBody;

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
