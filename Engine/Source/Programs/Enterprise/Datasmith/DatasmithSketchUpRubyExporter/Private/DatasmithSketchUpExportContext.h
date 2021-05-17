// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DatasmithSketchUpCommon.h"

#include "DatasmithSketchUpCamera.h"

// Datasmith SDK.
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Templates/SharedPointer.h"

#include "Async/Future.h"

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

		TSharedPtr<FComponentInstance> AddComponentInstance(FDefinition& ParentDefinition, SUComponentInstanceRef InComponentInstanceRef); // Register ComponentInstance as a child of ParentDefinition
		bool RemoveComponentInstance(FComponentInstanceIDType ParentEntityId, FComponentInstanceIDType ComponentInstanceId); // Take note that ComponentInstance removed from ParentDefinition children
		void RemoveComponentInstance(TSharedPtr<FComponentInstance> ComponentInstance);

		bool InvalidateComponentInstanceProperties(FComponentInstanceIDType ComponentInstanceID);
		void InvalidateComponentInstanceGeometry(FComponentInstanceIDType ComponentInstanceID);
		void InvalidateComponentInstanceMetadata(FComponentInstanceIDType ComponentInstanceID);
		void UpdateProperties();
		void UpdateGeometry();

		TSharedPtr<FComponentInstance>* FindComponentInstance(FComponentInstanceIDType ComponentInstanceID)
		{
			return ComponentInstanceMap.Find(ComponentInstanceID);
		}

	private:
		FExportContext& Context;

		TMap<FComponentInstanceIDType, TSharedPtr<FComponentInstance>> ComponentInstanceMap;
	};

	class FComponentDefinitionCollection
	{
	public:
		FComponentDefinitionCollection(FExportContext& InContext) : Context(InContext) {}

		void PopulateFromModel(SUModelRef InSModelRef);

		TSharedPtr<FComponentDefinition> AddComponentDefinition(SUComponentDefinitionRef InComponentDefinitionRef);

		TSharedPtr<FComponentDefinition> GetComponentDefinition(SUComponentInstanceRef InSComponentInstanceRef);
		TSharedPtr<FComponentDefinition> GetComponentDefinition(SUComponentDefinitionRef ComponentDefinitionRef);
		TSharedPtr<FComponentDefinition>* FindComponentDefinition(FComponentDefinitionIDType ComponentDefinitionID);

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

		FTexture* AddTexture(SUTextureRef TextureRef, FString MaterialName);

		// This texture is used in a colorized material so image will be saved in material-specific filename
		FTexture* AddColorizedTexture(SUTextureRef TextureRef, FString MaterialName); 

		// Creates single Image File for each separate Texture that uses the same saved image file
		void AddImageFileForTexture(TSharedPtr<FTexture> Texture); 

		void Update();

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

		TSharedPtr<FMaterial> CreateMaterial(SUMaterialRef SMaterialDefinitionRef);
		void CreateMaterial(FMaterialIDType MaterialID);
		void InvalidateMaterial(SUMaterialRef SMaterialDefinitionRef);
		bool InvalidateMaterial(FMaterialIDType MateriadId);
		bool RemoveMaterial(FEntityIDType EntityId);


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

		FDefinition* GetDefinition(SUEntityRef Entity);
		FDefinition* GetDefinition(FEntityIDType DefinitionEntityId);

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

		TArray<TFuture<bool>> MeshExportTasks;
	};
}
