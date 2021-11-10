// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithSketchUpComponent.h"

#include "DatasmithSketchUpExportContext.h"
#include "DatasmithSketchUpMaterial.h"
#include "DatasmithSketchUpMetadata.h"
#include "DatasmithSketchUpString.h"
#include "DatasmithSketchUpSummary.h"
#include "DatasmithSketchUpUtils.h"

// SketchUp SDK.
#include "DatasmithSketchUpSDKBegins.h"
#include "SketchUpAPI/model/component_definition.h"
#include "SketchUpAPI/model/drawing_element.h"
#include "SketchUpAPI/model/entities.h"
#include "SketchUpAPI/model/group.h"
#include "SketchUpAPI/model/layer.h"
#include "SketchUpAPI/model/model.h"
#include "SketchUpAPI/model/component_instance.h"

#if !defined(SKP_SDK_2019) && !defined(SKP_SDK_2020)
#include "SketchUpAPI/model/layer_folder.h"
#endif

#include "DatasmithSketchUpSDKCeases.h"

#include "IDatasmithSceneElements.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithUtils.h"

#define REMOVE_MESHES_WHEN_INVISIBLE

using namespace DatasmithSketchUp;

void FNodeOccurence::AddChildOccurrence(FExportContext& Context, FComponentInstance& ChildComponentInstance)
{
	FNodeOccurence& ChildNode = ChildComponentInstance.CreateNodeOccurrence(Context, /*Parent*/ *this);

	// todo: rename ToDatasmith - this just creates hierarchy
	ChildNode.ToDatasmith(Context);
}

void FNodeOccurence::ToDatasmith(FExportContext& Context)
{
	FDefinition* EntityDefinition = Entity.GetDefinition();

	if (!EntityDefinition)
	{
		return;
	}

	// Process child nodes
	FEntities& Entities = EntityDefinition->GetEntities();
	// Convert the SketchUp normal component instances into sub-hierarchies of Datasmith actors.
	for (SUComponentInstanceRef SComponentInstanceRef : Entities.GetComponentInstances())
	{
		TSharedPtr<FComponentInstance> ComponentInstance = Context.ComponentInstances.AddComponentInstance(*EntityDefinition, SComponentInstanceRef);
		if (ComponentInstance.IsValid())
		{
			AddChildOccurrence(Context, *ComponentInstance);
		}
	}

	// Convert the SketchUp group component instances into sub-hierarchies of Datasmith actors.
	for (SUGroupRef SGroupRef : Entities.GetGroups())
	{
		SUComponentInstanceRef SComponentInstanceRef = SUGroupToComponentInstance(SGroupRef);

		TSharedPtr<FComponentInstance> ComponentInstance = Context.ComponentInstances.AddComponentInstance(*EntityDefinition, SComponentInstanceRef);
		if (ComponentInstance.IsValid())
		{
			AddChildOccurrence(Context, *ComponentInstance);
		}
	}
}

void FNodeOccurence::UpdateMeshActors(FExportContext& Context)
{
	// Remove old mesh actors
	// todo: reuse old mesh actors (also can keep instances when removing due to say hidden)
	if (DatasmithActorElement.IsValid())
	{
		for (TSharedPtr<IDatasmithMeshActorElement> MeshActor : MeshActors)
		{
			DatasmithActorElement->RemoveChild(MeshActor);
		}
	}
	else
	{
		for (TSharedPtr<IDatasmithMeshActorElement> MeshActor : MeshActors)
		{
			Context.DatasmithScene->RemoveActor(MeshActor, EDatasmithActorRemovalRule::RemoveChildren);
		}
	}
	MeshActors.Reset();

	if (!bVisible) // no mesh actors for invisible node
	{
		return;
	}

	FDefinition* EntityDefinition = Entity.GetDefinition();
	DatasmithSketchUp::FEntitiesGeometry* EntitiesGeometry = EntityDefinition->GetEntities().EntitiesGeometry.Get();

	if (!EntitiesGeometry)
	{
		return;
	}

	MeshActors.Reset(EntitiesGeometry->GetMeshCount());

	FString ComponentActorName = GetActorName();
	FString MeshActorLabel = GetActorLabel();
	for (int32 MeshIndex = 0; MeshIndex < EntitiesGeometry->GetMeshCount(); ++MeshIndex)
	{
		FString MeshActorName = FString::Printf(TEXT("%ls_%d"), *ComponentActorName, MeshIndex + 1); // Count meshes/mesh actors from 1

		// Create a Datasmith mesh actor for the Datasmith mesh element.
		TSharedPtr<IDatasmithMeshActorElement> DMeshActorPtr = FDatasmithSceneFactory::CreateMeshActor(*MeshActorName);

		MeshActors.Add(DMeshActorPtr);

		// Set the mesh actor label used in the Unreal UI.
		DMeshActorPtr->SetLabel(*MeshActorLabel);

		// Add the Datasmith actor component depth tag.
		// We use component depth + 1 to factor in the added Datasmith scene root once imported in Unreal.
		FString ComponentDepthTag = FString::Printf(TEXT("SU.DEPTH.%d"), Depth + 1);
		DMeshActorPtr->AddTag(*ComponentDepthTag);

		// Add the Datasmith actor component definition GUID tag.
		FString DefinitionGUIDTag = FString::Printf(TEXT("SU.GUID.%ls"), *EntityDefinition->GetSketchupSourceGUID());
		DMeshActorPtr->AddTag(*DefinitionGUIDTag);

		// Add the Datasmith actor component instance path tag.
		FString InstancePathTag = ComponentActorName.Replace(TEXT("SU"), TEXT("SU.PATH.0")).Replace(TEXT("_"), TEXT("."));
		DMeshActorPtr->AddTag(*InstancePathTag);

		// Add the mesh actor to our component Datasmith actor hierarchy.
		if (DatasmithActorElement.IsValid()) 
		{
			DatasmithActorElement->AddChild(DMeshActorPtr);
		}
		else
		{
			Context.DatasmithScene->AddActor(DMeshActorPtr);
		}

		// ADD_TRACE_LINE(TEXT("Actor %ls: %ls %ls %ls"), *MeshActorLabel, *ComponentDepthTag, *DefinitionGUIDTag, *InstancePathTag);

		// Set the Datasmith mesh element used by the mesh actor.
		DMeshActorPtr->SetStaticMeshPathName(EntitiesGeometry->GetMeshElementName(MeshIndex));
	}
}

void FNodeOccurence::UpdateVisibility(FExportContext& Context)
{
	if (bHierarchyInvalidated)
	{
		// todo: move hierarchy creation here?
		bHierarchyInvalidated = false;
	}

	if (bVisibilityInvalidated)
	{
		Entity.UpdateOccurrenceVisibility(Context, *this);

		bVisibilityInvalidated = false;
	}

	for (FNodeOccurence* ChildNode : Children)
	{
		ChildNode->UpdateVisibility(Context);
	}
}

void FNodeOccurence::Update(FExportContext& Context)
{
	// todo: Is it possible not to traverse whole scene when only part of it changes?
	// - one way is to collect all nodes that need to be updated
	// - the other - only topmost invalidated nodes. and them traverse from them only, not from the top. 
	//   E.g. when a node is invalidated - traverse its subtree to invalidate all the nodes below. Also when a node is invalidated check  
	//   its parent - if its not invalidated this means any ancestor is not invalidated. This way complexity would be O(n) where n is number of nodes that need update, not number of all nodes

	if (bMeshActorsInvalidated)
	{
		UpdateMeshActors(Context);
		bMeshActorsInvalidated = false;
	}

	if (bPropertiesInvalidated)
	{
		Entity.UpdateOccurrence(Context, *this);
		bPropertiesInvalidated = false;
	}

	for (FNodeOccurence* ChildNode : Children)
	{
		ChildNode->Update(Context);
	}
}

void FNodeOccurence::InvalidateProperties()
{
	if (bPropertiesInvalidated)
	{
		// if node is invalidated no need to traverse further - it's already done
		return;
	}

	bPropertiesInvalidated = true;

	// todo: register invalidated?

	for (FNodeOccurence* Child : Children)
	{
		Child->InvalidateProperties();
	}
}

void FNodeOccurence::InvalidateMeshActors()
{
	bMeshActorsInvalidated = true;
}

FString FNodeOccurence::GetActorName()
{
	return DatasmithActorName;
}

FString FNodeOccurence::GetActorLabel()
{
	return DatasmithActorLabel;
}

void FNodeOccurence::RemoveOccurrence(FExportContext& Context)
{
	// RemoveOccurrence is called from Entity only(i.e. it doesn't remove occurrence from the Entity itself, it's done there)

	Entity.EntityOccurrenceVisible(this, false);

	if (MaterialOverride)
	{
		MaterialOverride->UnregisterInstance(Context, this);
	}

	for (FNodeOccurence* Child : Children)
	{
		Child->RemoveOccurrence(Context);
		Child->Entity.DeleteOccurrence(Context, Child);
	}
	Children.Reset(); // don't need this is node it deallocated?

	for (const TSharedPtr<IDatasmithMeshActorElement>& MeshActor : MeshActors) 
	{
		if (const TSharedPtr<IDatasmithActorElement>& ParentActor = MeshActor->GetParentActor())
		{
			ParentActor->RemoveChild(MeshActor);
		}
		else
		{
			Context.DatasmithScene->RemoveActor(MeshActor, EDatasmithActorRemovalRule::RemoveChildren);
		}
	}

	if (DatasmithActorElement)
	{
		if (const TSharedPtr<IDatasmithActorElement>& ParentActor = DatasmithActorElement->GetParentActor())
		{
			ParentActor->RemoveChild(DatasmithActorElement);
		}
		else
		{
			Context.DatasmithScene->RemoveActor(DatasmithActorElement, EDatasmithActorRemovalRule::RemoveChildren);
		}
	}
}

void FNodeOccurence::ResetMetadataElement(FExportContext& Context)
{
	// Create a Datasmith metadata element for the SketckUp component instance metadata definition.
	FString MetadataElementName = FString::Printf(TEXT("%ls_DATA"), DatasmithActorElement->GetName());
	
	if (!DatasmithMetadataElement.IsValid())
	{
		DatasmithMetadataElement = FDatasmithSceneFactory::CreateMetaData(*MetadataElementName);
		DatasmithMetadataElement->SetAssociatedElement(DatasmithActorElement);
		Context.DatasmithScene->AddMetaData(DatasmithMetadataElement);
	}
	else
	{
		DatasmithMetadataElement->SetName(*MetadataElementName);
	}
	DatasmithMetadataElement->SetLabel(*DatasmithActorLabel);
	DatasmithMetadataElement->ResetProperties();
}

void FNodeOccurence::SetVisibility(bool bValue)
{
	bVisible = bValue;
	Entity.EntityOccurrenceVisible(this, bVisible);
}

void FNodeOccurence::RemoveDatasmithActorHierarchy(FExportContext& Context)
{
	if (!DatasmithActorElement)
	{
		return;
	}

	// Remove depth-first
	for (FNodeOccurence* ChildNode : Children)
	{
		ChildNode->RemoveDatasmithActorHierarchy(Context);
	}

	for (TSharedPtr<IDatasmithMeshActorElement> MeshActor : MeshActors)
	{
		DatasmithActorElement->RemoveChild(MeshActor);
	}
	MeshActors.Reset();

	if (const TSharedPtr<IDatasmithActorElement>& ParentActor = DatasmithActorElement->GetParentActor())
	{
		ParentActor->RemoveChild(DatasmithActorElement);
	}
	else
	{
		Context.DatasmithScene->RemoveActor(DatasmithActorElement, EDatasmithActorRemovalRule::RemoveChildren);
	}
	DatasmithActorElement.Reset();

	if (DatasmithMetadataElement)
	{
		Context.DatasmithScene->RemoveMetaData(DatasmithMetadataElement);
	}
	DatasmithMetadataElement.Reset();
}

FModelDefinition::FModelDefinition(SUModelRef InModel) : Model(InModel)
{
}

void FModelDefinition::Parse(FExportContext& Context)
{
	SUEntitiesRef EntitiesRef = SU_INVALID;
	// Retrieve the SketchUp model entities.
	SUModelGetEntities(Model, &EntitiesRef);
	Entities = Context.EntitiesObjects.AddEntities(*this, EntitiesRef);
}

void FModelDefinition::UpdateGeometry(FExportContext& Context)
{
	Entities->UpdateGeometry(Context);
}

void FModelDefinition::UpdateMetadata(FExportContext& Context)
{
}

void FModelDefinition::InvalidateInstancesGeometry(FExportContext& Context)
{
	Context.Model->InvalidateEntityGeometry();
}

void FModelDefinition::InvalidateInstancesMetadata(FExportContext& Context)
{
}

void FModelDefinition::FillOccurrenceActorMetadata(FNodeOccurence& Node)
{
}

void FModelDefinition::CreateActor(FExportContext& Context, FNodeOccurence& Node)
{
	// Don't create single root Actor for model
}

void FModelDefinition::BuildNodeNames(FNodeOccurence& Node)
{
	// Get the SketckUp component instance persistent ID.
	int64 SketchupPersistentID = Node.Entity.GetPersistentId();
	Node.DatasmithActorName = FString::Printf(TEXT("%ls_%lld"), *GetSketchupSourceName(), SketchupPersistentID);

	Node.DatasmithActorLabel = GetSketchupSourceName();
}

FString FModelDefinition::GetSketchupSourceName()
{
	FString SketchupSourceName = SuGetString(SUModelGetName, Model);
	if (SketchupSourceName.IsEmpty())
	{
		SketchupSourceName = TEXT("SketchUp_Model");
	}
	return SketchupSourceName;
}

FString FModelDefinition::GetSketchupSourceGUID()
{
	return TEXT("MODEL");
}

void FModelDefinition::AddInstance(FExportContext& Context, TSharedPtr<FComponentInstance> Instance)
{
	Context.RootNode->AddChildOccurrence(Context, *Instance);
}

FComponentDefinition::FComponentDefinition(
	SUComponentDefinitionRef InComponentDefinitionRef)
	: ComponentDefinitionRef(InComponentDefinitionRef)
{
}

void FComponentDefinition::Parse(FExportContext& Context)
{
	SUEntitiesRef EntitiesRef = SU_INVALID;
	// Retrieve the SketchUp component definition entities.
	SUComponentDefinitionGetEntities(ComponentDefinitionRef, &EntitiesRef); // we can ignore the returned SU_RESULT

	Entities = Context.EntitiesObjects.AddEntities(*this, EntitiesRef);

	// Get the component ID of the SketckUp component definition.
	SketchupSourceID = DatasmithSketchUpUtils::GetComponentID(ComponentDefinitionRef);

	// Retrieve the SketchUp component definition behavior in the rendering scene.
	SUComponentBehavior SComponentBehavior;
	SUComponentDefinitionGetBehavior(ComponentDefinitionRef, &SComponentBehavior); // we can ignore the returned SU_RESULT

	// Get whether or not the source SketchUp component behaves like a billboard.
	bSketchupSourceFaceCamera = SComponentBehavior.component_always_face_camera;
}

void FComponentDefinition::CreateActor(FExportContext& Context, FNodeOccurence& Node)
{
	BuildNodeNames(Node);

	// Create a Datasmith actor for the component instance.
	Node.DatasmithActorElement = FDatasmithSceneFactory::CreateActor(*Node.DatasmithActorName); // a EDatasmithElementType::Actor

	{
		// Add the Datasmith actor component depth tag.
		// We use component depth + 1 to factor in the added Datasmith scene root once imported in Unreal.
		FString ComponentDepthTag = FString::Printf(TEXT("SU.DEPTH.%d"), Node.Depth);
		Node.DatasmithActorElement->AddTag(*ComponentDepthTag);

		// Add the Datasmith actor component definition GUID tag.
		FString DefinitionGUIDTag = FString::Printf(TEXT("SU.GUID.%ls"), *GetSketchupSourceGUID());
		Node.DatasmithActorElement->AddTag(*DefinitionGUIDTag);

		// Add the Datasmith actor component instance path tag.
		FString InstancePathTag = Node.DatasmithActorName.Replace(TEXT("SU"), TEXT("SU.PATH.0")).Replace(TEXT("_"), TEXT("."));
		Node.DatasmithActorElement->AddTag(*InstancePathTag);

		// Add the Datasmith actor component instance face camera tag when required.
		if (bSketchupSourceFaceCamera)
		{
			Node.DatasmithActorElement->AddTag(TEXT("SU.BEHAVIOR.FaceCamera"));
		}
	}

	// ComponentInstance occurrence always has parent node(Model it at top)
	if (Node.ParentNode->DatasmithActorElement)
	{
		Node.ParentNode->DatasmithActorElement->AddChild(Node.DatasmithActorElement);
	}
	else
	{
		Context.DatasmithScene->AddActor(Node.DatasmithActorElement);
	}
}

void FComponentDefinition::UpdateGeometry(FExportContext& Context)
{
	Entities->UpdateGeometry(Context);
}

void FComponentDefinition::UpdateMetadata(FExportContext& Context)
{
	ParsedMetadata = MakeUnique<FMetadata>(SUComponentDefinitionToEntity(ComponentDefinitionRef));;
}

void FComponentDefinition::BuildNodeNames(FNodeOccurence& Node)
{
	// Get the SketckUp component instance persistent ID.
	int64 SketchupPersistentID = Node.Entity.GetPersistentId();
	Node.DatasmithActorName = FString::Printf(TEXT("%ls_%lld"), *Node.ParentNode->GetActorName(), SketchupPersistentID);

	FString EntityName = Node.Entity.GetName();
	Node.DatasmithActorLabel = FDatasmithUtils::SanitizeObjectName(EntityName.IsEmpty() ? Node.Entity.GetDefinition()->GetSketchupSourceName() : EntityName);
}

void FComponentDefinition::InvalidateInstancesGeometry(FExportContext& Context)
{
	// todo: keep all instances or incapsulate enumeration(duplicated) of FComponentInstance
	size_t InstanceCount = 0;
	SUComponentDefinitionGetNumInstances(ComponentDefinitionRef, &InstanceCount);

	TArray<SUComponentInstanceRef> InstanceRefs;
	InstanceRefs.Init(SU_INVALID, InstanceCount);
	SUComponentDefinitionGetInstances(ComponentDefinitionRef, InstanceCount, InstanceRefs.GetData(), &InstanceCount);
	InstanceRefs.SetNum(InstanceCount);

	for (const SUComponentInstanceRef& InstanceRef : InstanceRefs)
	{
		Context.ComponentInstances.InvalidateComponentInstanceGeometry(DatasmithSketchUpUtils::GetComponentInstanceID(InstanceRef));
	}
}

void FComponentDefinition::InvalidateInstancesMetadata(FExportContext& Context)
{
	// todo: keep all instances or incapsulate enumeration(duplicated) of FComponentInstance
	size_t InstanceCount = 0;
	SUComponentDefinitionGetNumInstances(ComponentDefinitionRef, &InstanceCount);

	TArray<SUComponentInstanceRef> InstanceRefs;
	InstanceRefs.Init(SU_INVALID, InstanceCount);
	SUComponentDefinitionGetInstances(ComponentDefinitionRef, InstanceCount, InstanceRefs.GetData(), &InstanceCount);
	InstanceRefs.SetNum(InstanceCount);

	for (const SUComponentInstanceRef& InstanceRef : InstanceRefs)
	{
		Context.ComponentInstances.InvalidateComponentInstanceMetadata(DatasmithSketchUpUtils::GetComponentInstanceID(InstanceRef));
	}
}

void FComponentDefinition::FillOccurrenceActorMetadata(FNodeOccurence& Node)
{
	if (ParsedMetadata)
	{
		ParsedMetadata->AddMetadata(Node.DatasmithMetadataElement);
	}
}

FString FComponentDefinition::GetSketchupSourceName()
{
	// Retrieve the SketchUp component definition name.
	return SuGetString(SUComponentDefinitionGetName, ComponentDefinitionRef);
}

FString FComponentDefinition::GetSketchupSourceGUID()
{
	// Retrieve the SketchUp component definition IFC GUID.
	return SuGetString(SUComponentDefinitionGetGuid, ComponentDefinitionRef);
}

void FComponentDefinition::LinkComponentInstance(FComponentInstance* ComponentInstance)
{
	Instances.Add(ComponentInstance);
}

void FComponentDefinition::UnlinkComponentInstance(FComponentInstance* ComponentInstance)
{
	Instances.Remove(ComponentInstance);
}

void FComponentDefinition::RemoveComponentDefinition(FExportContext& Context)
{
	// Remove ComponentDefinition that doesn't have tracked instances 
	ensure(!Instances.Num());
	
	// todo: might better keep in the Definition's Entities all ComponentInstanceIDs of the tracked entities
	// this way we don't need to check whether we are tracking them (inside RemoveComponentInstance) 
	for (SUComponentInstanceRef ComponentInstanceRef : GetEntities().GetComponentInstances())
	{
		Context.ComponentInstances.RemoveComponentInstance(
			DatasmithSketchUpUtils::GetComponentID(ComponentDefinitionRef), 
			DatasmithSketchUpUtils::GetComponentInstanceID(ComponentInstanceRef));
	}

	for (SUGroupRef GroupRef : GetEntities().GetGroups())
	{
		Context.ComponentInstances.RemoveComponentInstance(
			DatasmithSketchUpUtils::GetComponentID(ComponentDefinitionRef),
			DatasmithSketchUpUtils::GetGroupID(GroupRef));
	}

	Context.Materials.UnregisterGeometry(GetEntities().EntitiesGeometry.Get());
	Context.EntitiesObjects.UnregisterEntities(GetEntities());
}

void FComponentDefinition::AddInstance(FExportContext& Context, TSharedPtr<FComponentInstance> Instance)
{
	for (FComponentInstance* ParentInstance : Instances)
	{
		for (FNodeOccurence* ParentOccurrence : ParentInstance->Occurrences)
		{
			ParentOccurrence->AddChildOccurrence(Context, *Instance);
		}
	}
}

void FEntity::UpdateOccurrence(FExportContext& Context, FNodeOccurence& Node)
{
	if (Node.MaterialOverride)
	{
		Node.MaterialOverride->UnregisterInstance(Context, &Node);
	}

	FString EffectiveLayerName = SuGetString(SULayerGetName, Node.EffectiveLayerRef);

	FDefinition* EntityDefinition = GetDefinition();
	DatasmithSketchUp::FEntitiesGeometry& EntitiesGeometry = *EntityDefinition->GetEntities().EntitiesGeometry;

	// Set the effective inherited material ID.
	if (!GetAssignedMaterial(Node.InheritedMaterialID))
	{
		Node.InheritedMaterialID = Node.ParentNode->InheritedMaterialID;
	}

	// Update Datasmith Mesh Actors
	for (int32 MeshIndex = 0; MeshIndex < Node.MeshActors.Num(); ++MeshIndex)
	{
		const TSharedPtr<IDatasmithMeshActorElement>& MeshActor = Node.MeshActors[MeshIndex];
		MeshActor->SetLayer(*EffectiveLayerName);

		// Update Override(Inherited)  Material
		// todo: set inherited material only on mesh actors that have faces with default material, right now setting on every mesh, hot harmful but excessive
		if (EntitiesGeometry.IsMeshUsingInheritedMaterial(MeshIndex))
		{
			if (FMaterialOccurrence* Material = Context.Materials.RegisterInstance(Node.InheritedMaterialID, &Node))
			{
				// SketchUp has 'material override' only for single('Default') material. 
				// So we reset overrides on the actor to remove this single override(if it was set) and re-add new override
				MeshActor->ResetMaterialOverrides(); // Clear previous override if was set
				MeshActor->AddMaterialOverride(Material->GetName(), EntitiesGeometry.GetInheritedMaterialOverrideSlotId());
			}
		}
	}
}

void FEntity::UpdateEntityGeometry(FExportContext& Context)
{
	if (bGeometryInvalidated)
	{
		InvalidateOccurrencesGeometry(Context);
		bGeometryInvalidated = false;
	}
}

void FEntity::UpdateEntityProperties(FExportContext& Context)
{
	if (bPropertiesInvalidated)
	{
		// We can't just update Occurrence properties
		// When transform changes each node needs its parent transform to be already calculated 
		// So we postpone occurrence nodes updates until we do update with respect to hierarchy(top first)
		InvalidateOccurrencesProperties(Context);
		UpdateMetadata(Context);
		
		bPropertiesInvalidated = false;
	}
}

void FEntity::EntityOccurrenceVisible(FNodeOccurence* Node, bool bVisible)
{
	if (bVisible)
	{
		VisibleNodes.Add(Node);
	}
	else
	{
		if (VisibleNodes.Contains(Node))
		{
			VisibleNodes.Remove(Node);
		}
	}

	GetDefinition()->EntityVisible(this, VisibleNodes.Num() > 0);
}

FDefinition* FComponentInstance::GetDefinition()
{
	return &Definition;
}

bool FComponentInstance::GetAssignedMaterial(FMaterialIDType& MaterialId)
{
	SUComponentInstanceRef ComponentInstanceRef = GetComponentInstanceRef();
	SUMaterialRef MaterialRef = DatasmithSketchUpUtils::GetMaterial(ComponentInstanceRef);

	// Set the effective inherited material ID.
	if (SUIsValid(MaterialRef))
	{
		// Get the material ID of the SketckUp component instance material.
		MaterialId = DatasmithSketchUpUtils::GetMaterialID(MaterialRef);
		return true;
	}
	return false;
}

void FComponentInstance::UpdateOccurrence(FExportContext& Context, FNodeOccurence& Node)
{
	if (!Node.bVisible)
	{
		return;
	}

	SUComponentInstanceRef InSComponentInstanceRef = GetComponentInstanceRef();

	// Set the actor label used in the Unreal UI.
	Node.DatasmithActorElement->SetLabel(*Node.DatasmithActorLabel);

	// Retrieve the SketchUp component instance effective layer name.
	FString SEffectiveLayerName;
	SEffectiveLayerName = SuGetString(SULayerGetName, Node.EffectiveLayerRef);

	// Set the Datasmith actor layer name.
	Node.DatasmithActorElement->SetLayer(*FDatasmithUtils::SanitizeObjectName(SEffectiveLayerName));

	SUTransformation SComponentInstanceWorldTransform = DatasmithSketchUpUtils::GetComponentInstanceTransform(InSComponentInstanceRef, Node.ParentNode->WorldTransform);
	Node.WorldTransform = SComponentInstanceWorldTransform;

	// Set the Datasmith actor world transform.
	DatasmithSketchUpUtils::SetActorTransform(Node.DatasmithActorElement, SComponentInstanceWorldTransform);

	Node.ResetMetadataElement(Context);// todo: can enable/disable metadata export by toggling this code
	FillOccurrenceActorMetadata(Node);
	
	// Update Datasmith Mesh Actors
	for (int32 MeshIndex = 0; MeshIndex < Node.MeshActors.Num(); ++MeshIndex)
	{
		const TSharedPtr<IDatasmithMeshActorElement>& MeshActor = Node.MeshActors[MeshIndex];

		// Set mesh actor transform after node transform
		MeshActor->SetScale(Node.DatasmithActorElement->GetScale());
		MeshActor->SetRotation(Node.DatasmithActorElement->GetRotation());
		MeshActor->SetTranslation(Node.DatasmithActorElement->GetTranslation());
	}

	

	Super::UpdateOccurrence(Context, Node);
}

int64 FComponentInstance::GetPersistentId()
{
	SUComponentInstanceRef ComponentInstanceRef = GetComponentInstanceRef();
	return DatasmithSketchUpUtils::GetComponentPID(ComponentInstanceRef);
}

FString FComponentInstance::GetName()
{
	SUComponentInstanceRef InSComponentInstanceRef = GetComponentInstanceRef();
	FString SComponentInstanceName;
	return SuGetString(SUComponentInstanceGetName, InSComponentInstanceRef);
}

FNodeOccurence& FComponentInstance::CreateNodeOccurrence(FExportContext& Context, FNodeOccurence& ParentNode)
{
	FNodeOccurence* Occurrence = new FNodeOccurence(&ParentNode, *this);
	ParentNode.Children.Add(Occurrence);
	Occurrences.Add(Occurrence);
	return *Occurrence;
}

void FComponentInstance::DeleteOccurrence(FExportContext& Context, FNodeOccurence* Node)
{
	Occurrences.Remove(Node);
	delete Node;
}

void FComponentInstance::UpdateMetadata(FExportContext& Context)
{
	ParsedMetadata = MakeUnique<FMetadata>(SUComponentInstanceToEntity(GetComponentInstanceRef()));
}

void FComponentInstance::InvalidateOccurrencesGeometry(FExportContext& Context)
{
	for (FNodeOccurence* Node : Occurrences)
	{
		Node->InvalidateMeshActors();

		// Should invalidate transform to trigger transform update for mesh actors 
		// todo: can simplify this
		// - separate Transform invalidation from other properties? If it should give any improvement?
		// - or just update mesh actors transforms? we can't do it here though as transform can be invalidated by ancestors change later when occurrences are updated
		// - add another flag to invalidate just mesh actors properties and update them separately
		Node->InvalidateProperties();
	}
}

void FComponentInstance::InvalidateOccurrencesProperties(FExportContext& Context)
{
	// When ComponentInstance is modified we need to determine if its visibility might have changed foremost
	// because this determines whether corresponding node would exist in the Datasmith scene 
	// Two things affect this - Hidden instance flag and layer(tag):

	bool bNewHidden = false;
	SUDrawingElementRef DrawingElementRef = SUComponentInstanceToDrawingElement(GetComponentInstanceRef());
	SUDrawingElementGetHidden(DrawingElementRef, &bNewHidden);

	SUDrawingElementGetLayer(DrawingElementRef, &LayerRef);
	bool bNewLayerVisible = true;
	SULayerGetVisibility(LayerRef, &bNewLayerVisible);

	// Search for invisible ancestor folder (parent invisibility overrides child's visibility) 
	// LayerFolder introduced in SketchUp 2021
#if !defined(SKP_SDK_2019) && !defined(SKP_SDK_2020)
	SULayerFolderRef LayerFolderRef = SU_INVALID;
	SULayerGetParentLayerFolder(LayerRef, &LayerFolderRef);
	while (SUIsValid(LayerFolderRef))
	{
		bool bLayerFolderVisible = true;
		SULayerFolderGetVisibility(LayerFolderRef, &bLayerFolderVisible);
		bNewLayerVisible = bNewLayerVisible && bLayerFolderVisible;

		SULayerFolderRef ParentLayerFolderRef = SU_INVALID;
		SULayerFolderGetParentLayerFolder(LayerFolderRef, &ParentLayerFolderRef);
		LayerFolderRef = ParentLayerFolderRef;
	}
#endif

	if (bHidden != bNewHidden || bLayerVisible != bNewLayerVisible)
	{
		bHidden = bNewHidden;
		bLayerVisible = bNewLayerVisible;
		for (FNodeOccurence* Node : Occurrences)
		{
			Node->bVisibilityInvalidated = true;
		}
	}

	for (FNodeOccurence* Node : Occurrences)
	{
		Node->InvalidateProperties();
	}
}

FComponentInstanceIDType FComponentInstance::GetComponentInstanceId()
{
	return DatasmithSketchUpUtils::GetComponentInstanceID(GetComponentInstanceRef());
}

void FComponentInstance::RemoveOccurrences(FExportContext& Context)
{
	for (FNodeOccurence* Occurrence : Occurrences)
	{
		Occurrence->RemoveOccurrence(Context);
		Occurrence->ParentNode->Children.Remove(Occurrence);
	}
}

SUComponentInstanceRef FComponentInstance::GetComponentInstanceRef()
{
	return SUComponentInstanceFromEntity(EntityRef);
}

void FComponentInstance::FillOccurrenceActorMetadata(FNodeOccurence& Node)
{
	if (!Node.DatasmithMetadataElement)
	{
		return;
	}

	//SUTransformation SComponentInstanceWorldTransform = DatasmithSketchUpUtils::GetComponentInstanceTransform(GetComponentInstanceRef(), Node.ParentNode->WorldTransform);
	//double Volume;
	//SUComponentInstanceComputeVolume(GetComponentInstanceRef(), &SComponentInstanceWorldTransform, &Volume);

	// Add original instance/component names to metadata
	TSharedPtr<IDatasmithKeyValueProperty> EntityName = FDatasmithSceneFactory::CreateKeyValueProperty(TEXT("Instance"));
	EntityName->SetValue(*GetName());
	Node.DatasmithMetadataElement->AddProperty(EntityName);

	TSharedPtr<IDatasmithKeyValueProperty> DefinitionName = FDatasmithSceneFactory::CreateKeyValueProperty(TEXT("Definition"));
	DefinitionName->SetValue(*GetDefinition()->GetSketchupSourceName());
	Node.DatasmithMetadataElement->AddProperty(DefinitionName);

	// Add instance metadata
	if (ParsedMetadata)
	{
		ParsedMetadata->AddMetadata(Node.DatasmithMetadataElement);
	}

	// Add definition metadata
	GetDefinition()->FillOccurrenceActorMetadata(Node);
}

void FComponentInstance::UpdateOccurrenceVisibility(FExportContext& Context, FNodeOccurence& Node)
{
	Node.EffectiveLayerRef = DatasmithSketchUpUtils::GetEffectiveLayer(GetComponentInstanceRef(), Node.ParentNode->EffectiveLayerRef);

	// Parent node, component instance and layer - all should be visible to have node visible
	Node.SetVisibility(Node.ParentNode->bVisible && !bHidden && bLayerVisible);

	if (Node.bVisible)
	{
		if (!Node.DatasmithActorElement)
		{
			FDefinition* EntityDefinition = GetDefinition();
			EntityDefinition->CreateActor(Context, Node);

			// Invalidate actor-dependent 
			Node.InvalidateMeshActors();
			Node.InvalidateProperties();
		}
		Node.InvalidateMeshActors();
	}
	else
	{
		Node.RemoveDatasmithActorHierarchy(Context);
	}

	for (FNodeOccurence* ChildNode : Node.Children)
	{
		ChildNode->bVisibilityInvalidated = true;
	}
}

void FComponentInstance::RemoveComponentInstance(FExportContext& Context)
{
	Definition.EntityVisible(this, false);
	Definition.UnlinkComponentInstance(this);
	RemoveOccurrences(Context);

	// If there's no Instances of this removed ComponentInstance we need to stop tracking Definition's Entities
	// Details:
	// SketchUp api doesn't fire event for those child Entities although they are effectively removed from Model 
	// Sketchup.active_model.definitions.purge_unused will deallocate those dangling Entities leaving references invalid
	// Although SU API tries to notify about this but fails e.g. DefinitionObserver.onComponentInstanceRemoved/onEraseEntity passes already deleted Entity making this callback useless
	if (!Definition.Instances.Num())
	{
		Definition.RemoveComponentDefinition(Context);
	}
}

void FComponentInstance::SetParentDefinition(FExportContext& Context, FDefinition* InParent)
{
	if (!IsParentDefinition(InParent)) // Changing parent
	{
		// If we are re-parenting(i.e. entity was previously owned by another Definition - this happens
		// when say a ComponentInstance was selected in UI and "Make Group" was performed.
		if (Parent)
		{
			RemoveOccurrences(Context);
		}

		Parent = InParent;
	}
}

FModel::FModel(FModelDefinition& InDefinition) : Definition(InDefinition)
{

}

FDefinition* FModel::GetDefinition()
{
	return &Definition;
}

bool FModel::GetAssignedMaterial(FMaterialIDType& MaterialId)
{
	MaterialId = FMaterial::INHERITED_MATERIAL_ID;
	return true;
}

int64 FModel::GetPersistentId()
{
	return 0;
}

FString FModel::GetName()
{
	return "";
}
   
void FModel::InvalidateOccurrencesGeometry(FExportContext& Context)
{
	Context.RootNode->InvalidateMeshActors();
	Context.RootNode->InvalidateProperties();
}

void FModel::InvalidateOccurrencesProperties(FExportContext& Context)
{
	Context.RootNode->InvalidateProperties();
}

void FModel::UpdateOccurrenceVisibility(FExportContext& Context, FNodeOccurence& Node)
{
	Node.SetVisibility(true);
}

void FModel::DeleteOccurrence(FExportContext& Context, FNodeOccurence* Node)
{
	// Model occurrence is not deleted by any parent
}

void FModel::UpdateMetadata(FExportContext& Context)
{
}

void FDefinition::EntityVisible(FEntity* Entity, bool bVisible)
{
	if (bVisible)
	{
		VisibleEntities.Add(Entity);
	}
	else
	{
		if (VisibleEntities.Contains(Entity))
		{
			VisibleEntities.Remove(Entity);
		}
	}
}

void FDefinition::UpdateDefinition(FExportContext& Context)
{
	if (VisibleEntities.Num())
	{
		if (bGeometryInvalidated)
		{
			UpdateGeometry(Context);
			InvalidateInstancesGeometry(Context); // Make sure instances keep up with definition changes
			bMeshesAdded = false;

			bGeometryInvalidated = false;
		}

		if (bPropertiesInvalidated)
		{
			UpdateMetadata(Context);
			InvalidateInstancesMetadata(Context); // Make sure instances keep up with definition changes

			bPropertiesInvalidated = false;
		}

		if (!bMeshesAdded)
		{
			GetEntities().AddMeshesToDatasmithScene(Context);
			bMeshesAdded = true;
		}
	}
	else
	{
		if (bMeshesAdded)
		{
			// Without references meshes will be cleaned from datasmith scene
			// bMeshesAdded = false; // todo: SceneCleanUp - do maintenance myself?
			GetEntities().RemoveMeshesFromDatasmithScene(Context);
			bMeshesAdded = false;
		}
	}
}
