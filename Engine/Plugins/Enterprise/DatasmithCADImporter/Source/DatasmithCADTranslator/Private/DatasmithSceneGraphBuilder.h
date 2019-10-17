// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#ifdef USE_CORETECH_MT_PARSER

#include "CoreMinimal.h"

#include "CADLibraryOptions.h"
#include "Containers/Map.h"
#include "Containers/Queue.h"
#include "CoreTechHelper.h"
#include "CoreTechTypes.h"
#include "CTSession.h"
#include "DatasmithImportOptions.h"
#include "Misc/Paths.h"

static const FString SPACE(" ");

class FCTRawDataFile;
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


class FCTNode
{
public:
	FCTNode(FCTRawDataFile& InRawData, int32 InLine);

	NODE_TYPE GetNodeType() { return Type; }

	void GetMetaDatas(TMap<FString, FString>& MetaDataMap);
	void GetChildren(TArray<int32>& Children);
	
	void GetNodeReference(int32& OutRefId, FString& OutExternalFile, NODE_TYPE& OutType);
	void AddTransformToActor(TSharedPtr< IDatasmithActorElement > Actor, CADLibrary::FImportParameters& InImportParameters);

	FCTNode* GetCTNode(int32 NodeId);

	uint32 GetStaticMeshUuid();
	void GetMaterialSet(CADLibrary::FCTMaterialPartition& MaterialPartition);

	void GetColorDescription(int32 ColorId, uint8 CtColor[3]);
	void GetMaterialDescription(int32 MaterialId, int32& OutMaterialHash, FString& OutMaterialName, uint8 OutDiffuse[3], uint8 OutAmbient[3], uint8 OutSpecular[3], uint8& OutShininess, uint8& OutTransparency, uint8& OutReflexion, FString& OutTextureName);

protected:
	void SetNodeType();
	FString SceneGraphFile;
	FCTRawDataFile& RawData;
	int32 Line;
	int32 StartMeta;
	int32 EndMeta;
	NODE_TYPE Type;

	uint32 BodyUUID;
	
};

class FCTRawDataFile
{
public:
	FCTRawDataFile(const FString& InFileName);
	~FCTRawDataFile()
	{
	}
	void ReadFile();

	FCTNode* GetCTNode(int32 nodeId)
	{
		return CTIdToRawEntryMap.Find(nodeId);
	}

	const FString& GetString(int32 line)
	{
		return RawData[line];
	}

	const FString& GetFileName()
	{
		return FileName;
	}

	void GetColorDescription(int32 ColorId, uint8 Color[3])
	{
		Color[0] = Color[1] = Color[2] = 255;

		int32* line = ColorIdToLineMap.Find(ColorId);
		if (line == nullptr)
		{
			return;
		}
		const FString& StrColor = GetString(*line);
		TArray<FString> ColorParameters;
		StrColor.ParseIntoArray(ColorParameters, *SPACE);
		for (int32 index = 0; index < 3; index++)
		{
			Color[index] = FCString::Atoi(*ColorParameters[index + 1]);
		}
	}

	void GetMaterialDescription(int32 MaterialId, int32& OutMaterialHash, FString& OutMaterialName, uint8 OutDiffuse[3], uint8 OutAmbient[3], uint8 OutSpecular[3], uint8& OutShininess, uint8& OutTransparency, uint8& OutReflexion, FString& OutTextureName)
	{
		OutDiffuse[0] = OutDiffuse[1] = OutDiffuse[2] = 255;
		OutAmbient[0] = OutAmbient[1] = OutAmbient[2] = 255;
		OutSpecular[0] = OutSpecular[1] = OutSpecular[2] = 255;
		OutShininess = OutReflexion = 0;
		OutTransparency = 255;
		OutMaterialName = TEXT("");
		OutTextureName = TEXT("");

		int32* line = MaterialIdToLineMap.Find(MaterialId);
		if (line == nullptr)
		{
			return;
		}

		const FString& MaterialLine1 = GetString(*line);
		FString MaterialIdStr;
		MaterialLine1.Split(SPACE, &MaterialIdStr, &OutMaterialName);

		const FString& MaterialLine2 = GetString(*line + 1);
		TArray<FString> MaterialParameters;
		MaterialLine2.ParseIntoArray(MaterialParameters, *SPACE);

		OutMaterialHash = FCString::Atoi(*MaterialParameters[1]);
		for (int32 index = 0; index < 3; index++)
		{
			OutDiffuse[index] = FCString::Atoi(*MaterialParameters[index + 2]);
			OutAmbient[index] = FCString::Atoi(*MaterialParameters[index + 5]);
			OutSpecular[index] = FCString::Atoi(*MaterialParameters[index + 8]);
		}
		OutShininess = FCString::Atoi(*MaterialParameters[11]);
		OutTransparency = FCString::Atoi(*MaterialParameters[12]);
		OutReflexion = FCString::Atoi(*MaterialParameters[13]);
		int32 TextureId = FCString::Atoi(*MaterialParameters[14]);

		if (MaterialParameters[14] != TEXT("0"))
		{
			const FString& MaterialLine3 = GetString(*line + 2);
			MaterialLine3.Split(SPACE, &MaterialIdStr, &OutTextureName);
		}
	}

protected:
	FString FileName;
	TArray<FString> RawData;
	TMap<int32, FCTNode> CTIdToRawEntryMap;
	TMap<int32, int32> ColorIdToLineMap;
	TMap<int32, int32> MaterialIdToLineMap;
};



class FDatasmithSceneGraphBuilder
{
public:
	FDatasmithSceneGraphBuilder(const FString& InCachePath, TSharedRef<IDatasmithScene> InScene, const FDatasmithSceneSource& InSource, TMap<FString, FString>& InCADFileToUE4FileMap, TMap< TSharedPtr< IDatasmithMeshElement >, uint32 >& InMeshElementToCTBodyUuidMap);

	bool Build();

	void SetImportParameters(const CADLibrary::FImportParameters& InImportParameters)
	{
		ImportParameters = InImportParameters;
	};


protected:
	void LoadRawDataFile();

	TSharedPtr< IDatasmithActorElement > BuildNode(FCTNode& Node, const TCHAR* ParentUuid);
	TSharedPtr< IDatasmithActorElement > BuildInstance(FCTNode& Node, const TCHAR* ParentUuid);
	TSharedPtr< IDatasmithActorElement > BuildComponent(FCTNode& Node, const TCHAR* ParentUuid);
	TSharedPtr< IDatasmithActorElement > BuildBody(FCTNode& NodeId, const TCHAR* ParentUuid);

	void AddMetaData(TSharedPtr< IDatasmithActorElement > ActorElement, TMap<FString, FString>& InstanceNodeAttributeSetMap, TMap<FString, FString>& ReferenceNodeAttributeSetMap);
	void AddChildren(TSharedPtr< IDatasmithActorElement > Actor, FCTNode& Node, const TCHAR* ParentUuid);
	void LinkActor(TSharedPtr< IDatasmithActorElement > ParentActor, TSharedPtr< IDatasmithActorElement > Actor);

	TSharedPtr< IDatasmithUEPbrMaterialElement > GetDefaultMaterial();
	TSharedPtr<IDatasmithMaterialIDElement> FindOrAddMaterial(FCTNode& Node, uint32 MaterialID);

	TSharedPtr< IDatasmithActorElement > CreateActor(const TCHAR* ActorUUID, const TCHAR* ActorLabel);
	TSharedPtr< IDatasmithMeshElement > FindOrAddMeshElement(FCTNode& Node, FString& InLabel);
	//TSharedPtr< IDatasmithActorElement > CreateMeshActor(CTNode& Node);

	void GetNodeUUIDAndName(TMap<FString, FString>& InInstanceNodeMetaDataMap, TMap<FString, FString>& InReferenceNodeMetaDataMap, const TCHAR* InParentUEUUID, FString& OutUEUUID, FString& OutName);

	TSharedPtr<IDatasmithUEPbrMaterialElement> CreateUEPbrMaterialFromColorId(FCTNode& Node, uint32 InColorHashId);
	TSharedPtr<IDatasmithUEPbrMaterialElement> CreateUEPbrMaterialFromMaterialId(FCTNode& Node, uint32 InMaterialID, TSharedRef<IDatasmithScene> Scene);


protected:
	TSharedRef<IDatasmithScene> DatasmithScene;
	const FDatasmithSceneSource& Source;
	FString CachePath;

	CADLibrary::FImportParameters ImportParameters;

	TMap<FString, FString>& CADFileToRawDataFile;
	TMap<FString, FCTRawDataFile*> CADFileToRawData;
	TArray<FCTRawDataFile> RawDataArray;


	TMap< uint32, TSharedPtr< IDatasmithMeshElement > > BodyUuidToMeshElementMap;
	TMap< TSharedPtr< IDatasmithMeshElement >, uint32 >& MeshElementToCTBodyUuidMap;

	TMap< uint32, TSharedPtr< IDatasmithUEPbrMaterialElement > > MaterialMap;
	TSharedPtr< IDatasmithUEPbrMaterialElement > DefaultMaterial;

};


#endif  // USE_CORETECH_MT_PARSER
