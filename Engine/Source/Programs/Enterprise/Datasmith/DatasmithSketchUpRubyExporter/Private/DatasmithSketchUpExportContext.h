// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DatasmithSketchUpCommon.h"

#include "DatasmithSketchUpCamera.h"

// Datasmith SDK.
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Templates/SharedPointer.h"

class FDatasmithSceneExporter;

class IDatasmithActorElement;
class IDatasmithCameraActorElement;
class IDatasmithMasterMaterialElement;
class IDatasmithMeshElement;
class IDatasmithMetaDataElement;
class IDatasmithScene;
class IDatasmithTextureElement;

namespace DatasmithSketchUp
{
	class FExportContext;

	class FNodeOccurence;

	class FCamera;
	class FComponentDefinition;
	class FComponentInstance;
	class FDefinition;
	class FEntities;
	class FEntitiesGeometry;
	class FEntity;
	class FMaterial;
	class FMaterialOccurrence;
	class FModel;
	class FModelDefinition;
	class FTexture;
	class FTextureImageFile;

	class FComponentInstanceCollection
	{
	public:
		FComponentInstanceCollection(FExportContext& InContext) : Context(InContext) {}

		TSharedPtr<FComponentInstance> AddComponentInstance(SUComponentInstanceRef InComponentInstanceRef);
		bool RemoveComponentInstance(FComponentInstanceIDType ComponentInstanceId);
		void AddOccurrence(FComponentInstanceIDType ComponentInstanceID, const TSharedPtr<DatasmithSketchUp::FNodeOccurence>& Occurrence);

		void InvalidateComponentInstanceProperties(FComponentInstanceIDType ComponentInstanceID);
		void InvalidateComponentInstanceGeometry(FComponentInstanceIDType ComponentInstanceID);
		void Update();

		TSharedPtr<FComponentInstance>* FindComponentInstance(FComponentInstanceIDType ComponentInstanceID)
		{
			return ComponentInstanceMap.Find(ComponentInstanceID);
		}

		TArray<TSharedPtr<DatasmithSketchUp::FNodeOccurence>>* GetOccurrencesForComponentInstance(FComponentInstanceIDType ComponentInstanceID)
		{
			return ComponentInstanceOccurencesMap.Find(ComponentInstanceID);
		}

	private:
		FExportContext& Context;

		TMap<FComponentInstanceIDType, TSharedPtr<FComponentInstance>> ComponentInstanceMap;

		TMap<FComponentInstanceIDType, TArray<TSharedPtr<FNodeOccurence>>> ComponentInstanceOccurencesMap;
	};

	class FComponentDefinitionCollection
	{
	public:
		FComponentDefinitionCollection(FExportContext& InContext) : Context(InContext) {}

		void PopulateFromModel(SUModelRef InSModelRef);

		void AddComponentDefinition(SUComponentDefinitionRef InComponentDefinitionRef);

		TSharedPtr<FComponentDefinition> GetComponentDefinition(SUComponentInstanceRef InSComponentInstanceRef);

		void Update();

	private:
		FExportContext& Context;

		TMap<FEntityIDType, TSharedPtr<FComponentDefinition>> ComponentDefinitionMap;

	};

	class FTextureCollection
	{
	public:
		FTextureCollection(FExportContext& InContext) : Context(InContext) {}

		TSharedPtr<FTexture> FindOrAdd(SUTextureRef);

		FTexture* AddTexture(SUTextureRef TextureRef);

		// This texture is used in a colorized material so image will be saved in material-specific filename
		FTexture* AddColorizedTexture(SUTextureRef TextureRef, FString MaterialName); 

		// Creates single Image File for each separate Texture that uses the same saved image file
		void AddImageFileForTexture(TSharedPtr<FTexture> Texture); 

		void ConvertToDatasmith();

	private:
		FExportContext& Context;

		TMap<FTextureIDType, TSharedPtr<FTexture>> TexturesMap;
		TMap<FString, TSharedPtr<FTextureImageFile>> TextureNameToImageFile; // texture handlers representing same texture
	};

	class FEntitiesObjectCollection
	{
	public:
		FEntitiesObjectCollection(FExportContext& InContext) : Context(InContext) {}

		TSharedPtr<DatasmithSketchUp::FEntities> AddEntities(FDefinition& InDefinition, SUEntitiesRef EntitiesRef);

		void RegisterEntitiesFaces(DatasmithSketchUp::FEntities&, const TSet<int32>& FaceIds);
		DatasmithSketchUp::FEntities* FindFace(int32 FaceId);

	private:
		FExportContext& Context;
		TMap<int32, DatasmithSketchUp::FEntities*> FaceIdForEntitiesMap; // Identify Entities for each Face
	};

	// Tracks information related to SketchUp "Scenes"(or "Pages" in older UI)
	class FSceneCollection
	{
	public:
		FSceneCollection(FExportContext& InContext) : Context(InContext) {}

		// Initialize the dictionary of camera definitions.
		void PopulateFromModel(
			SUModelRef InSModelRef // model containing SketchUp camera definitions
		);

		TMap<FSceneIDType, TSharedPtr<FCamera>> SceneIdToCameraMap;
	private:
		FExportContext& Context;
	};

	class FMaterialCollection
	{
	public:
		FMaterialCollection(FExportContext & InContext) : Context(InContext) {}

		// Initialize the dictionary of material definitions.
		void PopulateFromModel(
			SUModelRef InSModelRef // model containing SketchUp material definitions
		);

		TSharedPtr<DatasmithSketchUp::FMaterial>* Find(FMaterialIDType MaterialID)
		{
			return MaterialDefinitionMap.Find(MaterialID);
		}

		// Tell that this materials is assigned on the node
		FMaterialOccurrence* RegisterInstance(FMaterialIDType MaterialID, FNodeOccurence* NodeOccurrence);

		// Tell that this materials is assigned directly to a face on the geometry
		FMaterialOccurrence* RegisterGeometry(FMaterialIDType MaterialID, DatasmithSketchUp::FEntitiesGeometry* EntitiesGeometry);

		void UnregisterGeometry(DatasmithSketchUp::FEntitiesGeometry * EntitiesGeometry);

	private:
		FExportContext& Context;

		TMap<FMaterialIDType, TSharedPtr<DatasmithSketchUp::FMaterial>> MaterialDefinitionMap;

		TSharedPtr<FMaterialOccurrence> DefaultMaterial;
	};

	// Holds all the data needed during export and incremental updates
	class FExportContext
	{
	public:

		FExportContext();

		const TCHAR* GetAssetsOutputPath() const;

		void Populate(); // Create Datasmith scene from the Model
		void Update(); // Update Datasmith scene to reflect iterative changes done to the Model 

		SUModelRef ModelRef = SU_INVALID;

		TSharedPtr<IDatasmithScene> DatasmithScene;
		TSharedPtr<FDatasmithSceneExporter> SceneExporter;

		TSharedPtr<FNodeOccurence> RootNode;
		TSharedPtr<FModelDefinition> ModelDefinition;
		TSharedPtr<FModel> Model;

		FComponentDefinitionCollection ComponentDefinitions;
		FComponentInstanceCollection ComponentInstances;
		FEntitiesObjectCollection EntitiesObjects;
		FMaterialCollection Materials;
		FSceneCollection Scenes;
		FTextureCollection Textures;
	};
}
