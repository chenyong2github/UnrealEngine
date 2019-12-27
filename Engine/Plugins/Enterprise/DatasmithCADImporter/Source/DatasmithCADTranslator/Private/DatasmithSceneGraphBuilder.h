// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#ifdef CAD_INTERFACE
#include "CoreMinimal.h"

#include "CADData.h"
#include "CADSceneGraph.h"
#include "CoreTechHelper.h"
#include "CoreTechTypes.h"
#include "CTSession.h"
#include "DatasmithImportOptions.h"

#include "Containers/Map.h"
#include "Containers/Queue.h"
#include "Misc/Paths.h"

static const FString SPACE(" ");

class FCADSceneGraphDescriptionFile;
class FDatasmithSceneSource;
class IDatasmithActorElement;
class IDatasmithMeshElement;
class IDatasmithScene;
class IDatasmithUEPbrMaterialElement;

enum NODE_TYPE
{
	INSTANCE,
	COMPONENT,
	EXTERNALCOMPONENT,
	BODY,
	UNDEFINED,
	UNKNOWN
};

namespace CADLibrary		
{
class FArchiveMockUp;
class ICADArchiveObject;
class FArchiveInstance;
class FArchiveComponent;
class FArchiveBody;
class FArchiveMaterial;
class FArchiveColor;
}

class ActorData  //#ueent_CAD
{
public:
	ActorData(const TCHAR* NodeUuid, const ActorData& ParentData)
		: Uuid(NodeUuid)
		, Material(ParentData.Material)
		, MaterialUuid(ParentData.MaterialUuid)
		, Color(ParentData.Color)
		, ColorUuid(ParentData.ColorUuid)
	{
	}

	ActorData(const TCHAR* NodeUuid)
		: Uuid(NodeUuid)
		, MaterialUuid(0)
		, ColorUuid(0)
	{
	}

	const TCHAR* Uuid;

	CADLibrary::FCADMaterial Material;
	uint32 MaterialUuid;

	FColor Color;
	uint32 ColorUuid;
};




class FDatasmithSceneGraphBuilder
{
public:
	FDatasmithSceneGraphBuilder(
		TMap<FString, FString>& InCADFileToUE4FileMap, 
		const FString& InCachePath, 
		TSharedRef<IDatasmithScene> InScene, 
		const FDatasmithSceneSource& InSource, 
		const CADLibrary::FImportParameters& InImportParameters);

	bool Build();

private:
	void LoadSceneGraphDescriptionFiles();

	TSharedPtr< IDatasmithActorElement > BuildInstance(CADLibrary::FArchiveInstance& Instance, ActorData& ParentData);
	TSharedPtr< IDatasmithActorElement > BuildComponent(CADLibrary::FArchiveComponent& Component, ActorData& ParentData);
	TSharedPtr< IDatasmithActorElement > BuildBody(CADLibrary::FArchiveBody& Body, ActorData& ParentData);

	void AddMetaData(TSharedPtr< IDatasmithActorElement > ActorElement, TMap<FString, FString>& InstanceNodeAttributeSetMap, TMap<FString, FString>& ReferenceNodeAttributeSetMap);
	void AddChildren(TSharedPtr< IDatasmithActorElement > Actor, CADLibrary::FArchiveComponent& Component, ActorData& ParentData);
	bool DoesActorHaveChildrenOrIsAStaticMesh(const TSharedPtr< IDatasmithActorElement >& ActorElement);

	TSharedPtr< IDatasmithUEPbrMaterialElement > GetDefaultMaterial();
	TSharedPtr<IDatasmithMaterialIDElement> FindOrAddMaterial(uint32 MaterialUuid);

	TSharedPtr< IDatasmithActorElement > CreateActor(const TCHAR* ActorUUID, const TCHAR* ActorLabel);
	TSharedPtr< IDatasmithMeshElement > FindOrAddMeshElement(CADLibrary::FArchiveBody& Body, FString& InLabel);

	void GetNodeUUIDAndName(TMap<FString, FString>& InInstanceNodeMetaDataMap, TMap<FString, FString>& InReferenceNodeMetaDataMap, const TCHAR* InParentUEUUID, FString& OutUEUUID, FString& OutName);

protected:
	TMap<FString, FString>& CADFileToSceneGraphDescriptionFile;
	const FString& CachePath;
	TSharedRef<IDatasmithScene> DatasmithScene;
	const FDatasmithSceneSource& Source;
	const CADLibrary::FImportParameters& ImportParameters;
	const uint32 ImportParametersHash;

	TArray<CADLibrary::FArchiveMockUp> ArchiveMockUps;
	TMap<FString, CADLibrary::FArchiveMockUp*> CADFileToArchiveMockUp;

	TMap< CADUUID, TSharedPtr< IDatasmithMeshElement > > BodyUuidToMeshElement;

	TMap< CADUUID, TSharedPtr< IDatasmithUEPbrMaterialElement > > MaterialUuidMap;
	TSharedPtr<IDatasmithUEPbrMaterialElement > DefaultMaterial;

	TMap<CADUUID, CADLibrary::FArchiveColor> ColorNameToColorArchive; 
	TMap<CADUUID, CADLibrary::FArchiveMaterial> MaterialNameToMaterialArchive; 

	CADLibrary::FArchiveMockUp* CurrentMockUp;

	bool bPreferMaterial;
	bool bMaterialPropagationIsTopDown;
};

#endif // CAD_INTERFACE
