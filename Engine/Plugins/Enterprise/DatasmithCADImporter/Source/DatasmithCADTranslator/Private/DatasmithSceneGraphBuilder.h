// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#ifdef CAD_INTERFACE
#include "CoreMinimal.h"

#include "CADData.h"
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


class FSceneNodeDescription
{
public:
	FSceneNodeDescription(FCADSceneGraphDescriptionFile& InRawData, int32 InLine);

	NODE_TYPE GetNodeType() { return Type; }

	void GetMetaDatas(TMap<FString, FString>& MetaDataMap);
	void GetChildren(TArray<int32>& Children);
	
	void GetNodeReference(int32& OutRefId, FString& OutExternalFile, NODE_TYPE& OutType);
	void AddTransformToActor(TSharedPtr< IDatasmithActorElement > Actor, const CADLibrary::FImportParameters& InImportParameters);

	FSceneNodeDescription* GetCTNode(int32 NodeId);

	uint32 GetStaticMeshUuid();
	void GetMaterialSet(TMap<uint32, uint32>& MaterialIdToMaterialHashMap);

	bool GetColor(int32 ColorHId, FColor& Color) const;
	bool GetMaterial(int32 MaterialId, CADLibrary::FCADMaterial& Material) const;

protected:
	void SetNodeType();
	FString SceneGraphFile;
	FCADSceneGraphDescriptionFile& SceneGraphDescription;
	int32 Line;
	int32 StartMeta;
	int32 EndMeta;
	NODE_TYPE Type;

	uint32 BodyUUID;
	
};


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



class FCADSceneGraphDescriptionFile
{
public:
	FCADSceneGraphDescriptionFile(const FString& InFileName);

	void ReadFile();

	FSceneNodeDescription* GetCTNode(int32 nodeId)
	{
		return SceneNodeIdToSceneNodeDescriptionMap.Find(nodeId);
	}

	const FString& GetString(int32 line) const
	{
		return SceneGraphDescriptionData[line];
	}

	const FString& GetFileName() const
	{
		return FileName;
	}

	bool GetColor(uint32 ColorHId, FColor& Color) const;
	bool GetMaterial(int32 MaterialId, CADLibrary::FCADMaterial& material) const;

	void GetMaterialDescription(int32 LineNumber, CADLibrary::FCADMaterial& Material) const;

	void SetMaterialMaps(TMap< uint32, FColor>& MaterialUuidToColor, TMap< uint32, CADLibrary::FCADMaterial>& MaterialUuidToMaterial);

protected:
	FString FileName;
	TArray<FString> SceneGraphDescriptionData;
	TMap<int32, FSceneNodeDescription> SceneNodeIdToSceneNodeDescriptionMap;
	TMap<int32, FColor> ColorIdToColor;
	TMap<int32, CADLibrary::FCADMaterial> MaterialIdToMaterial;
};



class FDatasmithSceneGraphBuilder
{
public:
	FDatasmithSceneGraphBuilder(TMap<FString, FString>& InCADFileToUE4FileMap, TMap< TSharedPtr< IDatasmithMeshElement >, uint32 >& InMeshElementToCTBodyUuidMap, const FString& InCachePath, 
		TSharedRef<IDatasmithScene> InScene, const FDatasmithSceneSource& InSource, const CADLibrary::FImportParameters& InImportParameters);

	bool Build();

protected:
	void LoadSceneGraphDescriptionFiles();

	TSharedPtr< IDatasmithActorElement > BuildNode(FSceneNodeDescription& Node, ActorData& ParentData);
	TSharedPtr< IDatasmithActorElement > BuildInstance(FSceneNodeDescription& Node, ActorData& ParentData);
	TSharedPtr< IDatasmithActorElement > BuildComponent(FSceneNodeDescription& Node, ActorData& ParentData);
	TSharedPtr< IDatasmithActorElement > BuildBody(FSceneNodeDescription& NodeId, ActorData& ParentData);

	void AddMetaData(TSharedPtr< IDatasmithActorElement > ActorElement, TMap<FString, FString>& InstanceNodeAttributeSetMap, TMap<FString, FString>& ReferenceNodeAttributeSetMap);
	void AddChildren(TSharedPtr< IDatasmithActorElement > Actor, FSceneNodeDescription& Node, ActorData& ParentData);
	void LinkActor(TSharedPtr< IDatasmithActorElement > ParentActor, TSharedPtr< IDatasmithActorElement > Actor);

	TSharedPtr< IDatasmithUEPbrMaterialElement > GetDefaultMaterial();
	TSharedPtr<IDatasmithMaterialIDElement> FindOrAddMaterial(uint32 MaterialUuid);

	TSharedPtr< IDatasmithActorElement > CreateActor(const TCHAR* ActorUUID, const TCHAR* ActorLabel);
	TSharedPtr< IDatasmithMeshElement > FindOrAddMeshElement(FSceneNodeDescription& Node, FString& InLabel);

	void GetNodeUUIDAndName(TMap<FString, FString>& InInstanceNodeMetaDataMap, TMap<FString, FString>& InReferenceNodeMetaDataMap, const TCHAR* InParentUEUUID, FString& OutUEUUID, FString& OutName);


protected:
	TMap<FString, FString>& CADFileToSceneGraphDescriptionFile;
	TMap< TSharedPtr< IDatasmithMeshElement >, uint32 >& MeshElementToCADBodyUuidMap;
	const FString& CachePath;
	TSharedRef<IDatasmithScene> DatasmithScene;
	const FDatasmithSceneSource& Source;
	const CADLibrary::FImportParameters& ImportParameters;

	TMap<FString, FCADSceneGraphDescriptionFile*> CADFileToSceneGraphDescription;

	TArray<FCADSceneGraphDescriptionFile> ArrayOfSceneGraphDescription; //#ueent_CAD

	TMap< uint32, TSharedPtr< IDatasmithMeshElement > > BodyUuidToMeshElementMap;

	TMap< uint32, TSharedPtr< IDatasmithUEPbrMaterialElement > > MaterialUuidMap;
	TSharedPtr<IDatasmithUEPbrMaterialElement > DefaultMaterial;

	TMap< uint32, FColor> MaterialUuidToColor;
	TMap< uint32, CADLibrary::FCADMaterial> MaterialUuidToMaterial;


	bool bPreferMaterial;
	bool bMaterialPropagationIsTopDown;
};

#endif // CAD_INTERFACE
