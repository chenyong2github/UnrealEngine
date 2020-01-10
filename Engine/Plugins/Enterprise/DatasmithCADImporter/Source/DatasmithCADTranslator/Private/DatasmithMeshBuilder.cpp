// Copyright Epic Games, Inc. All Rights Reserved.
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

FDatasmithMeshBuilder::FDatasmithMeshBuilder(TMap<FString, FString>& InCADFileToUE4GeomMap, const FString& InCachePath, const CADLibrary::FImportParameters& InImportParameters)
	: CachePath(InCachePath)
	, CADFileToMeshFile(InCADFileToUE4GeomMap)
	, ImportParameters(InImportParameters)
{
	LoadMeshFiles();
}

void FDatasmithMeshBuilder::LoadMeshFiles()
{
	BodyMeshes.Reserve(CADFileToMeshFile.Num());

	for (const auto& FilePair : CADFileToMeshFile)
	{
		FString MeshFile = FPaths::Combine(CachePath, TEXT("mesh"), FilePair.Value + TEXT(".gm"));
		if (!IFileManager::Get().FileExists(*MeshFile))
		{
			continue;
		}
		TArray<CADLibrary::FBodyMesh>& BodyMeshSet = BodyMeshes.Emplace_GetRef();
		DeserializeBodyMeshFile(*MeshFile, BodyMeshSet);
		for (FBodyMesh& Body : BodyMeshSet)
		{
			MeshActorNameToBodyMesh.Emplace(Body.MeshActorName, &Body);
		}
	}
}

TOptional<FMeshDescription> FDatasmithMeshBuilder::GetMeshDescription(TSharedRef<IDatasmithMeshElement> OutMeshElement, CADLibrary::FMeshParameters& OutMeshParameters)
{
#ifdef CAD_INTERFACE
	const TCHAR* NameLabel = OutMeshElement->GetName();
	CADUUID BodyUuid = (CADUUID) FCString::Atoi64(OutMeshElement->GetName()+2);  // +2 to remove 2 first char (Ox)
	if (BodyUuid == 0)
	{
		return TOptional<FMeshDescription>();
	}

	FBodyMesh** PPBody = MeshActorNameToBodyMesh.Find(BodyUuid);
	if(PPBody == nullptr || *PPBody == nullptr)
	{
		return TOptional<FMeshDescription>();
	}

	FBodyMesh& Body = **PPBody;

	FMeshDescription MeshDescription;
	DatasmithMeshHelper::PrepareAttributeForStaticMesh(MeshDescription);

	if (ConvertCTBodySetToMeshDescription(ImportParameters, OutMeshParameters, Body, MeshDescription))
	{
		return MoveTemp(MeshDescription);
	}
#endif // CAD_INTERFACE

	return TOptional<FMeshDescription>();
}
