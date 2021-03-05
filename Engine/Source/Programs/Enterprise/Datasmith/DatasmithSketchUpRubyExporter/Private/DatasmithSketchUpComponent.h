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
	class FComponentDefinition;
	class FEntities;
	class FEntitiesGeometry;
	class FEntity;
	class FMaterial;
	class FMaterialOccurrence;
	class FTexture;

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
		{}

		FNodeOccurence(FNodeOccurence* InParentNode, FEntity& InEntity)
			: ParentNode(InParentNode)
			, Entity(InEntity)
			, Depth(InParentNode->Depth + 1)
		{}

		void Update(FExportContext& Context);

		void CreateMeshActors(FExportContext& Context); // Create Datasmith Mesh Actors for meshes in the node's Entities

		void ToDatasmith(FExportContext& Context);

		FString GetActorName();

		FString GetActorLabel();

		FNodeOccurence* ParentNode;

		FEntity& Entity; // SketchUp entity this Node is an occurrence of

		// Data that is computed from the hierarchy where Entity occurrence resides
		int32 Depth;
		SUTransformation WorldTransform;
		FMaterialIDType InheritedMaterialID;
		SULayerRef EffectiveLayerRef = SU_INVALID;

		// Datasmith elements this Node spawns
		FString DatasmithActorName;
		FString DatasmithActorLabel;
		TSharedPtr<IDatasmithActorElement> DatasmithActorElement;
		TSharedPtr<IDatasmithMetaDataElement> DatasmithMetadataElement;
		TArray<TSharedPtr<IDatasmithMeshActorElement>> MeshActors; // Mesh actors for Loose geometry
	};

	// For SketchUp's Definition that provides access to Entities and converts to Datasmith
	class FDefinition : FNoncopyable
	{
	public:

		virtual ~FDefinition() {}

		virtual void Parse(FExportContext& Context) = 0;
		virtual void UpdateGeometry(FExportContext& Context) = 0; // Convert definition's Entities geometry to Datasmith
		virtual void CreateActor(FExportContext& Context, FNodeOccurence& Node) = 0; // Create Datasmith actor for node occurrence
		virtual void UpdateInstances(FExportContext& Context) = 0; // Updates all instances(and their occurrences) of this Definition

		virtual FString GetSketchupSourceGUID() = 0;
		virtual FString GetSketchupSourceName() = 0;

		FEntities& GetEntities()
		{
			return *Entities;
		}

	protected:
		virtual void BuildNodeNames(FNodeOccurence& Node) = 0;

		TSharedPtr<DatasmithSketchUp::FEntities> Entities;
	};


	// Associated with SketchUp ComponentDefinition
	class FComponentDefinition : public FDefinition
	{
	public:
		FComponentDefinition(
			SUComponentDefinitionRef InComponentDefinitionRef // source SketchUp component definition
		);

		// Being FDefinition
		void Parse(FExportContext& Context);
		void CreateActor(FExportContext& Context, FNodeOccurence& Node) override;
		void BuildNodeNames(FNodeOccurence& Node) override;
		void UpdateGeometry(FExportContext& Context) override;
		void UpdateInstances(FExportContext& Context) override;
		FString GetSketchupSourceGUID() override;
		FString GetSketchupSourceName()  override;
		// End FDefinition

		// Source SketchUp component ID.
		FComponentDefinitionIDType SketchupSourceID;

	private:
		SUComponentDefinitionRef ComponentDefinitionRef;

		// Whether or not the source SketchUp component behaves like a billboard, always presenting a 2D surface perpendicular to the direction of camera.
		bool bSketchupSourceFaceCamera = false;

		// Whether or not the source SketchUp entities geometry was added to the baked component mesh.
		bool bBakeEntitiesDone = false;
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
		virtual void UpdateInstances(FExportContext& Context) override
		{
			// todo:
		}
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
		void CleanEntitiesGeometry(FExportContext& Context);

		TSharedPtr<IDatasmithMeshElement> CreateMeshElement(FExportContext& Context, FDatasmithMesh& DatasmithMesh);

		TArray<SUGroupRef> GetGroups();

		TArray<SUComponentInstanceRef> GetComponentInstances();

		// Source SketchUp component entities.
		SUEntitiesRef EntitiesRef = SU_INVALID;

		// Number of component instances in the source SketchUp entities.
		size_t SourceComponentInstanceCount;

		// Number of groups in the source SketchUp entities.
		size_t SourceGroupCount;

		TSharedPtr<DatasmithSketchUp::FEntitiesGeometry> EntitiesGeometry;
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
		void AddMesh(FExportContext& Context, TSharedPtr<IDatasmithMeshElement>& DatasmithMesh, const TSet<FEntityIDType>& MaterialsUsed);
		bool IsMeshUsingInheritedMaterial(int32 MeshIndex);

		TArray<TSharedPtr<FDatasmithInstantiatedMesh>> Meshes;
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

		virtual ~FEntity() {}

		virtual FDefinition* GetDefinition() = 0;
		virtual bool GetAssignedMaterial(FMaterialIDType& MaterialId) = 0; // Get material of this entity
		virtual void UpdateNode(FExportContext& Context, FNodeOccurence& Node); // Update occurrence of this entity
		virtual int64 GetPersistentId() = 0;
		virtual FString GetName() = 0;
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
		void UpdateNode(FExportContext& Context, FNodeOccurence& Node) override;
		int64 GetPersistentId() override;
		FString GetName() override;
		// <<< FEntity

		// Create an occurrence of this ComponentInstance (component instance can appear multiple times in SketchUp hierarchy)
		TSharedRef<FNodeOccurence> CreateNodeOccurrence(FExportContext& Context, FNodeOccurence& ParentNode);
	};

	class FModel : public FEntity
	{
	public:
		FModel(class FModelDefinition& InDefinition);

		// >>> FEntity
		virtual FDefinition* GetDefinition() override;
		bool GetAssignedMaterial(FMaterialIDType& MaterialId) override;
		// void UpdateNode(FExportContext& Context, FNodeOccurence& Node) override;
		int64 GetPersistentId() override;
		FString GetName() override;
		// <<< FEntity
	private:
		FModelDefinition& Definition;
	};

}
