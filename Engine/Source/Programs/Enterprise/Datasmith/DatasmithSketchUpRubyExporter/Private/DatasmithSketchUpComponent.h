// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DatasmithSketchUpCommon.h"

// Datasmith SDK.
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Templates/SharedPointer.h"

namespace DatasmithSketchUp
{
	class FExportContext;

	class FNodeOccurence;
	class FDatasmithInstantiatedMesh;

	class FCamera;
	class FComponentDefinition;
	class FComponentInstance;
	class FEntities;
	class FEntitiesGeometry;
	class FEntity;
	class FMaterial;
	class FMaterialOccurrence;
	class FTexture;
	class FMetadata;

	// Identifies each occurrence of an Entity(ComponentInstance or Group) within the model graph
	// As each ComponentInstance or Group can appear multiple times in the SketchUp model hierarchy
	// this object represents each specific occurrence of it
	class FNodeOccurence : FNoncopyable
	{
	public:
		FNodeOccurence(FEntity& InEntity)
			: ParentNode(nullptr)
			, Entity(InEntity)
			, Depth(0)
			,  bVisibilityInvalidated(true)
			,  bPropertiesInvalidated(true)
			,  bMeshActorsInvalidated(true)
			,  bHierarchyInvalidated(true)
		{}

		FNodeOccurence(FNodeOccurence* InParentNode, FEntity& InEntity)
			: ParentNode(InParentNode)
			, Entity(InEntity)
			, Depth(InParentNode->Depth + 1)
			, bVisibilityInvalidated(true)
			, bPropertiesInvalidated(true)
			, bMeshActorsInvalidated(true)
			, bHierarchyInvalidated(true)
		{}

		// Update node hierarchy, bForceUpdate - to make update node and descendants when transform changes
		void UpdateVisibility(FExportContext& Context);
		void Update(FExportContext& Context);

		void AddChildOccurrence(FExportContext& Context, FComponentInstance& ComponentInstance);

		void UpdateMeshActors(FExportContext& Context); // Create/Free Datasmith Mesh Actors for meshes in the node's Entities

		void InvalidateProperties(); // Invalidate name and transform. Invalidate propagates down the hierarchy - child transforms depend on the parent
		void InvalidateMeshActors();
		void SetVisibility(bool);

		void RemoveDatasmithActorHierarchy(FExportContext& Context);

		void ToDatasmith(FExportContext& Context); // Build actor hierarchy 

		FString GetActorName();

		FString GetActorLabel();

		void RemoveOccurrence(FExportContext& Context);

		void ResetMetadataElement(FExportContext& Context); // Reset properties of actor's metadata to fill it anew

		FNodeOccurence* ParentNode;

		FEntity& Entity; // SketchUp entity this Node is an occurrence of

		TSet<FNodeOccurence*> Children;

		// Data that is computed from the hierarchy where Entity occurrence resides
		int32 Depth;
		SUTransformation WorldTransform;
		FMaterialIDType InheritedMaterialID;
		SULayerRef EffectiveLayerRef = SU_INVALID;
		bool bVisible = true; // Computed visibility for this occurrence(affecting descendants)

		// Datasmith elements this Node spawns
		FString DatasmithActorName;
		FString DatasmithActorLabel;
		TSharedPtr<IDatasmithActorElement> DatasmithActorElement;
		TSharedPtr<IDatasmithMetaDataElement> DatasmithMetadataElement;
		TArray<TSharedPtr<IDatasmithMeshActorElement>> MeshActors; // Mesh actors for Loose geometry

		FMaterial* MaterialOverride = nullptr; // Material used by this node

		// Flags indicating which Datasmith elements need to be updated from SketchUp
		// todo: do we really need so many flags for node? Just one flag to rebuild whole hierarchy of Datasmith actors?
		// Note - this doesn't mean that all needs to be recreated, literally, reuse if possible. 
		uint8 bVisibilityInvalidated:1;
		uint8 bPropertiesInvalidated:1; // Whether this occurrence properties(transform, name) need to be updated
		uint8 bMeshActorsInvalidated:1; // Whether this occurrence MeshActors need updating. Happens initially when node was added and when node geometry is invalidated
		uint8 bHierarchyInvalidated:1; // Children need to be rebuilt
	};

	// For SketchUp's Definition that provides access to Entities and converts to Datasmith
	class FDefinition : FNoncopyable
	{
	public:

		FDefinition()
			: bMeshesAdded(false)
			, bGeometryInvalidated(true)
			, bPropertiesInvalidated(true)
		{}


		virtual ~FDefinition() {}

		virtual void Parse(FExportContext& Context) = 0;
		virtual void CreateActor(FExportContext& Context, FNodeOccurence& Node) = 0; // Create Datasmith actor for node occurrence
		virtual void UpdateGeometry(FExportContext& Context) = 0; // Convert definition's Entities geometry to Datasmith Mesh
		virtual void UpdateMetadata(FExportContext& Context) = 0; 

		void EntityVisible(FEntity* Entity, bool bVisible);

		// Modfication methods
		virtual void AddInstance(FExportContext& Context, TSharedPtr<FComponentInstance> Instance) = 0; // Register child CompoenntInstance Entity of Definition's Entities
		virtual void InvalidateInstancesGeometry(FExportContext& Context) = 0; // Mark that all instances(and their occurrences) needed to be updated
		virtual void InvalidateInstancesMetadata(FExportContext& Context) = 0; // Mark that all instances(and their occurrences) needed to be updated
		virtual void FillOccurrenceActorMetadata(FNodeOccurence& Node) = 0;
		
		virtual FString GetSketchupSourceGUID() = 0;
		virtual FString GetSketchupSourceName() = 0;

		
		FEntities& GetEntities()
		{
			return *Entities;
		}

		void InvalidateDefinitionGeometry()
		{
			bGeometryInvalidated = true;
		}

		void UpdateDefinition(FExportContext& Context);

	protected:
		virtual void BuildNodeNames(FNodeOccurence& Node) = 0;

		TSharedPtr<DatasmithSketchUp::FEntities> Entities;

		TSet<FEntity*> VisibleEntities;
		uint8 bMeshesAdded:1;
		uint8 bGeometryInvalidated:1;
		uint8 bPropertiesInvalidated:1;
	};


	// Associated with SketchUp ComponentDefinition
	class FComponentDefinition : public FDefinition
	{
	public:
		FComponentDefinition(
			SUComponentDefinitionRef InComponentDefinitionRef // source SketchUp component definition
		);

		// Begin FDefinition
		void Parse(FExportContext& Context);
		void CreateActor(FExportContext& Context, FNodeOccurence& Node) override;
		void BuildNodeNames(FNodeOccurence& Node) override;
		void UpdateGeometry(FExportContext& Context) override;
		void UpdateMetadata(FExportContext& Context) override;

		void AddInstance(FExportContext& Context, TSharedPtr<FComponentInstance> Instance);
		void InvalidateInstancesGeometry(FExportContext& Context) override;
		void InvalidateInstancesMetadata(FExportContext& Context) override;
		void FillOccurrenceActorMetadata(FNodeOccurence& Node) override;

		FString GetSketchupSourceGUID() override;
		FString GetSketchupSourceName()  override;
		// End FDefinition


		// Register/unregister instanced of this definition
		void LinkComponentInstance(FComponentInstance* ComponentInstance);
		void UnlinkComponentInstance(FComponentInstance* ComponentInstance);
		void RemoveComponentDefinition(FExportContext& Context);

		// Source SketchUp component ID.
		FComponentDefinitionIDType SketchupSourceID;
		TSet<FComponentInstance*> Instances; // Tracked instances of this ComponentDefinition

	private:
		SUComponentDefinitionRef ComponentDefinitionRef;

		TUniquePtr<FMetadata> ParsedMetadata; // Shared metadata parsed from source SU component to be added to each occurrence actor's datasmith metatada element

		// Whether or not the source SketchUp component behaves like a billboard, always presenting a 2D surface perpendicular to the direction of camera.
		bool bSketchupSourceFaceCamera = false;
	};


	class FModelDefinition : public FDefinition
	{
	public:
		FModelDefinition(SUModelRef Model);

		// Being FDefinition
		void Parse(FExportContext& Context);
		void CreateActor(FExportContext& Context, FNodeOccurence& Node) override;
		void BuildNodeNames(FNodeOccurence& Node) override;
		void UpdateGeometry(FExportContext& Context) override;
		void UpdateMetadata(FExportContext& Context) override;

		void AddInstance(FExportContext& Context, TSharedPtr<FComponentInstance> Instance);
		void InvalidateInstancesGeometry(FExportContext& Context) override;
		void InvalidateInstancesMetadata(FExportContext& Context) override;
		void FillOccurrenceActorMetadata(FNodeOccurence& Node) override;

		FString GetSketchupSourceGUID() override;
		FString GetSketchupSourceName()  override;
		// End FDefinition

	private:
		SUModelRef Model;
	};

	// In SketchUp Entities that reside in a ComponentDefinition/Model can be ComponentInstances, Groups, Faces (and others)
	// ComponentInstances and Groups create model hierarchy, Faces constitute the geometry("meat"!) of Entities
	class FEntities : FNoncopyable
	{
	public:

		FDefinition& Definition;

		FEntities(FDefinition& InDefinition) : Definition(InDefinition) {}

		void UpdateGeometry(FExportContext& Context);
		void AddMeshesToDatasmithScene(FExportContext& Context);
		void RemoveMeshesFromDatasmithScene(FExportContext& Context);

		TSharedPtr<IDatasmithMeshElement> CreateMeshElement(FExportContext& Context, FDatasmithMesh& DatasmithMesh);

		TArray<SUGroupRef> GetGroups();

		TArray<SUComponentInstanceRef> GetComponentInstances();

		// Source SketchUp component entities.
		SUEntitiesRef EntitiesRef = SU_INVALID;

		TSharedPtr<FEntitiesGeometry> EntitiesGeometry;
	};

	// Represents an SketchUp's Entities'(not Entity's!) loose geometry
	class FEntitiesGeometry : FNoncopyable
	{
	public:

		int32 GetMeshCount()
		{
			return Meshes.Num();
		}

		const TCHAR* GetMeshElementName(int32 MeshIndex);
		void UpdateMesh(FExportContext& Context, FDatasmithInstantiatedMesh& Mesh, TSharedPtr<IDatasmithMeshElement>& DatasmithMesh, const TSet<FEntityIDType>& MaterialsUsed);
		bool IsMeshUsingInheritedMaterial(int32 MeshIndex);

		TArray<TSharedPtr<FDatasmithInstantiatedMesh>> Meshes;
		TSet<int32> FaceIds; // EntityId of all the VISIBLE faces composing the mesh
		TSet<DatasmithSketchUp::FEntityIDType> Layers; // EntityId of all layers assigned to geometry faces(needed to identify if geometry needs to be rebuilt when layer visibility changes)
		TSet<FMaterialIDType> MaterialsUsed;

		// todo: update reusing datasmith elements? 
		// todo: merge ALL faces that are present in Entities into single mesh? do we really need separate mesh for every isolated set of faces?
		// todo: occurrences using these entities must ne referenced
		// todo: update occurrences that used this entities - MeshActors need to be refreshed in accordance to OR this could be done on a level higher?
	};

	// Interface to implement SketchUp Entity Node(i.e. an instance of a ComponentDefinition - ComponentInstance or Group) access
	// todo: rename this to NodeEntity? This class represents on any entity but only those that build scene hierarchy(Model, ComponentInstance, Group)
	class FEntity : FNoncopyable
	{
	public:

		FEntity() 
			: bGeometryInvalidated(true)
			, bPropertiesInvalidated(true)
		{}

		virtual ~FEntity() {}

		virtual FDefinition* GetDefinition() = 0;
		virtual bool GetAssignedMaterial(FMaterialIDType& MaterialId) = 0; // Get material of this entity
		virtual void InvalidateOccurrencesGeometry(FExportContext& Context) = 0;
		virtual void UpdateOccurrence(FExportContext& Context, FNodeOccurence& Node); // Update occurrence of this entity
		virtual void InvalidateOccurrencesProperties(FExportContext& Context) = 0;
		virtual int64 GetPersistentId() = 0;
		virtual FString GetName() = 0;
		virtual void UpdateOccurrenceVisibility(FExportContext& Context, FNodeOccurence&) = 0;
		virtual void DeleteOccurrence(FExportContext& Context, FNodeOccurence* Node) = 0;
		virtual void UpdateMetadata(FExportContext& Context) = 0;

		void EntityOccurrenceVisible(FNodeOccurence* Node, bool bUses);


		// Invalidates transform, name
		void InvalidateEntityProperties()
		{
			bPropertiesInvalidated = true;
		}

		void InvalidateEntityGeometry()
		{
			bGeometryInvalidated = true;
		}

		void UpdateEntityProperties(FExportContext& Context);
		void UpdateEntityGeometry(FExportContext& Context);

		TSet<FNodeOccurence*> VisibleNodes;

		uint8 bGeometryInvalidated:1;
		uint8 bPropertiesInvalidated:1;
	};

	class FComponentInstance : public FEntity
	{
		using Super = FEntity;
	public:
		SUEntityRef EntityRef = SU_INVALID;
		FComponentDefinition& Definition;

		FComponentInstance(SUEntityRef InEntityRef, FComponentDefinition& InDefinition)
			: EntityRef(InEntityRef)
			, Definition(InDefinition)
		{}

		// >>> FEntity
		FDefinition* GetDefinition() override;
		bool GetAssignedMaterial(FMaterialIDType& MaterialId) override;
		void InvalidateOccurrencesGeometry(FExportContext& Context) override;
		void UpdateOccurrence(FExportContext& Context, FNodeOccurence& Node) override;
		void InvalidateOccurrencesProperties(FExportContext& Context) override;
		int64 GetPersistentId() override;
		FString GetName() override;
		void UpdateOccurrenceVisibility(FExportContext& Context, FNodeOccurence&) override;
		void DeleteOccurrence(FExportContext& Context, FNodeOccurence* Node) override;
		void UpdateMetadata(FExportContext& Context) override;
		// <<< FEntity

		// Set Definition which Entities contains this entity
		void SetParentDefinition(FExportContext& Context, FDefinition* InParent);
		bool IsParentDefinition(FDefinition* InParent)
		{
			return Parent == InParent;
		}

		void RemoveComponentInstance(FExportContext& Context);

		// Create an occurrence of this ComponentInstance (component instance can appear multiple times in SketchUp hierarchy)
		FNodeOccurence& CreateNodeOccurrence(FExportContext& Context, FNodeOccurence& ParentNode);
		void RemoveOccurrences(FExportContext& Context);

		FComponentInstanceIDType GetComponentInstanceId();
		SUComponentInstanceRef GetComponentInstanceRef(); 

		void FillOccurrenceActorMetadata(FNodeOccurence& Node);

		bool bHidden = false;
		SULayerRef LayerRef = SU_INVALID;
		bool bLayerVisible = true;

		TArray<FNodeOccurence*> Occurrences;

		FDefinition* Parent = nullptr;

		TUniquePtr<FMetadata> ParsedMetadata;
	};

	class FModel : public FEntity
	{
	public:
		FModel(class FModelDefinition& InDefinition);

		// >>> FEntity
		virtual FDefinition* GetDefinition() override;
		bool GetAssignedMaterial(FMaterialIDType& MaterialId) override;
		void InvalidateOccurrencesGeometry(FExportContext& Context) override;
		// void UpdateOccurrence(FExportContext& Context, FNodeOccurence& Node) override;
		void InvalidateOccurrencesProperties(FExportContext& Context) override;
		int64 GetPersistentId() override;
		FString GetName() override;
		void UpdateOccurrenceVisibility(FExportContext& Context, FNodeOccurence&) override;
		void DeleteOccurrence(FExportContext& Context, FNodeOccurence* Node) override;
		void UpdateMetadata(FExportContext& Context) override;
		// <<< FEntity
	private:
		FModelDefinition& Definition;
	};

}
