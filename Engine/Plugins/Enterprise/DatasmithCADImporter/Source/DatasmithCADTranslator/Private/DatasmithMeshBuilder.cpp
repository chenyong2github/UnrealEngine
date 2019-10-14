// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "DatasmithMeshBuilder.h"

#ifdef USE_CORETECH_MT_PARSER

#include "CoreTechHelper.h"
#include "DatasmithMeshHelper.h"
#include "IDatasmithSceneElements.h"
#include "MeshDescription.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

using namespace CADLibrary;

FCoretechBody::FCoretechBody(uint32 InBodyID)
	: TriangleCount(0)
	, BodyID(InBodyID)
{
}

void FCoretechBody::Add(FCTTessellation* InFaceTessellation)
{
	if (InFaceTessellation == nullptr)
	{
		return;
	}
	FaceTessellationSet.Add(InFaceTessellation);
	TriangleCount += InFaceTessellation->IndexCount / 3;
	MaterialToPartition.LinkMaterialId2MaterialHash(InFaceTessellation->MaterialId, InFaceTessellation->MaterialHash);
}

FDatasmithMeshBuilder::FDatasmithMeshBuilder(const FString& InCachePath, TMap<FString, FString>& InCADFileToUE4GeomMap, TMap< TSharedPtr< IDatasmithMeshElement >, uint32 >& InMeshElementToCTBodyUuidMap)
	: CachePath(InCachePath)
	, CADFileToUE4GeomMap(InCADFileToUE4GeomMap)
	, MeshElementToCTBodyUuidMap(InMeshElementToCTBodyUuidMap)
{
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

		int32 index = RawDataArray.Emplace(RawDataFile, BodyUuidToCoretechBodyMap);
	}
}

FCTRawGeomFile::FCTRawGeomFile(FString& InFileName, TMap< uint32, FCoretechBody* >& InBodyUuidToCTBodyMap)
	: BodyUuidToCTBodyMap(InBodyUuidToCTBodyMap)
{
	uint32 NbBody = 0;

	FFileHelper::LoadFileToArray(RawData, *InFileName);
	ReadTessellationSetFromFile(RawData, NbBody, FaceTessellationSet);
	CTBodySet.Reserve(NbBody);

	for (int index = 0; index < FaceTessellationSet.Num(); ++index)
	{
		uint32* PBodyIndex = CTIdToBodyMap.Find(FaceTessellationSet[index].BodyId);
		if (!PBodyIndex)
		{
			uint32 BodyIndex = CTBodySet.Emplace(FaceTessellationSet[index].BodyId);
			PBodyIndex = &BodyIndex;
			CTIdToBodyMap.Emplace(FaceTessellationSet[index].BodyId, BodyIndex);
			BodyUuidToCTBodyMap.Emplace(FaceTessellationSet[index].BodyUuId, &CTBodySet[BodyIndex]);
		}
		CTBodySet[*PBodyIndex].Add(&FaceTessellationSet[index]);
	}
}

TOptional<FMeshDescription> FDatasmithMeshBuilder::GetMeshDescription(TSharedRef<IDatasmithMeshElement> OutMeshElement, CADLibrary::FMeshParameters& OutMeshParameters)
{
	uint32* BodyUuid = MeshElementToCTBodyUuidMap.Find(OutMeshElement);
	if (BodyUuid == nullptr)
	{
		return TOptional<FMeshDescription>();
	}

	FCoretechBody*const* PPBody = BodyUuidToCoretechBodyMap.Find(*BodyUuid);
	if(PPBody == nullptr || *PPBody == nullptr)
	{
		return TOptional<FMeshDescription>();
	}

	FCoretechBody& Body = **PPBody;

	FCTMaterialPartition Material2Partition;

	FMeshDescription MeshDescription;
	DatasmithMeshHelper::PrepareAttributeForStaticMesh(MeshDescription);

	if(!ConvertCTBodySetToMeshDescription(ScaleFactor, OutMeshParameters, Body.GetTriangleCount(), Body.GetTessellationSet(), Material2Partition, MeshDescription))
	{
		return TOptional<FMeshDescription>();
	}

	return MoveTemp(MeshDescription);
}


#endif  // USE_CORETECH_MT_PARSER