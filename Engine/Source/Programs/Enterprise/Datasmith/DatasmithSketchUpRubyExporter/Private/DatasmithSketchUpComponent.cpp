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
#include "SketchUpAPI/model/entities.h"
#include "SketchUpAPI/model/group.h"
#include "SketchUpAPI/model/layer.h"
#include "SketchUpAPI/model/model.h"
#include "SketchUpAPI/model/component_instance.h"
#include "DatasmithSketchUpSDKCeases.h"

#include "IDatasmithSceneElements.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithUtils.h"

using namespace DatasmithSketchUp;

void FNodeOccurence::ToDatasmith(FExportContext& Context)
{
	FDefinition* EntityDefinition = Entity.GetDefinition();

	if (!EntityDefinition)
	{
		return;
	}

	// Set the effective inherited material ID.
	if (!Entity.GetAssignedMaterial(InheritedMaterialID))
	{
		InheritedMaterialID = ParentNode->InheritedMaterialID;
	}

	EntityDefinition->CreateActor(Context, *this);

	// Process child nodes
	FEntities& Entities = EntityDefinition->GetEntities();
	if (Entities.SourceComponentInstanceCount > 0)
	{

		// Convert the SketchUp normal component instances into sub-hierarchies of Datasmith actors.
		for (SUComponentInstanceRef SComponentInstanceRef : Entities.GetComponentInstances())
		{

			// Get the effective layer of the SketckUp normal component instance.
			SULayerRef SEffectiveLayerRef = DatasmithSketchUpUtils::GetEffectiveLayer(SComponentInstanceRef, EffectiveLayerRef);

			// Get whether or not the SketckUp normal component instance is visible in the current SketchUp scene.
			if (DatasmithSketchUpUtils::IsVisible(SComponentInstanceRef, SEffectiveLayerRef))
			{
				TSharedPtr<FComponentInstance> ComponentInstance = Context.ComponentInstances.AddComponentInstance(SComponentInstanceRef);

				if (ComponentInstance.IsValid())
				{
					TSharedRef<FNodeOccurence> ChildNode = ComponentInstance->CreateNodeOccurrence(Context, /*Parent*/ *this);

					ChildNode->EffectiveLayerRef = SEffectiveLayerRef;
					ChildNode->ToDatasmith(Context);

					// Add the normal component instance metadata into the dictionary of metadata definitions.
					FDatasmithSketchUpMetadata::AddMetadataDefinition(SComponentInstanceRef);
				}
			}
		}
	}

	if (Entities.SourceGroupCount > 0)
	{
		// Convert the SketchUp group component instances into sub-hierarchies of Datasmith actors.
		for (SUGroupRef SGroupRef : Entities.GetGroups())
		{
			SUComponentInstanceRef SComponentInstanceRef = SUGroupToComponentInstance(SGroupRef);

			// Get the effective layer of the SketckUp group component instance.
			SULayerRef SEffectiveLayerRef = DatasmithSketchUpUtils::GetEffectiveLayer(SComponentInstanceRef, EffectiveLayerRef);

			// Get whether or not the SketckUp group component instance is visible in the current SketchUp scene.
			if (DatasmithSketchUpUtils::IsVisible(SComponentInstanceRef, SEffectiveLayerRef))
			{
				TSharedPtr<FComponentInstance> ComponentInstance = Context.ComponentInstances.AddComponentInstance(SComponentInstanceRef);

				if (ComponentInstance.IsValid())
				{
					TSharedRef<FNodeOccurence> ChildNode = ComponentInstance->CreateNodeOccurrence(Context, /*Parent*/ *this);

					ChildNode->EffectiveLayerRef = SEffectiveLayerRef;

					ChildNode->ToDatasmith(Context);
				}
			}
		}
	}

}

void FNodeOccurence::CreateMeshActors(FExportContext& Context)
{
	FDefinition* EntityDefinition = Entity.GetDefinition();

	// Create Mesh Actors for loose Entities geometry
	FString ComponentActorName = GetActorName();
	FString MeshActorLabel = GetActorLabel();
	DatasmithSketchUp::FEntitiesGeometry& EntitiesGeometry = *EntityDefinition->GetEntities().EntitiesGeometry;

	// Remove old mesh actors
	// todo: reuse old mesh actors
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
	MeshActors.Reset(EntitiesGeometry.GetMeshCount());

	for (int32 MeshIndex = 0; MeshIndex < EntitiesGeometry.GetMeshCount(); ++MeshIndex)
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
		DMeshActorPtr->SetStaticMeshPathName(EntitiesGeometry.GetMeshElementName(MeshIndex));
	}
}

void FNodeOccurence::Update(FExportContext& Context)
{
	// todo: Is it possible not to traverse whole scene when only part of it changes?
	// - one way is to collect all nodes that need to be updated
	// - the other - only topmost invalidated nodes. and them traverse from them only, not from the top. 
	//   E.g. when a node is invalidated - traverse its subtree to invalidate all the nodes below. Also when a node is invalidated check  
	//   its parent - if its not invalidated this means any ancestor is not invalidated. This way complexity would be O(n) where n is number of nodes that need update, not number of all nodes

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

void FNodeOccurence::InvalidateProperties(FExportContext& Context)
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
		Child->InvalidateProperties(Context);
	}
}

FString FNodeOccurence::GetActorName()
{
	return DatasmithActorName;
}

FString FNodeOccurence::GetActorLabel()
{
	return DatasmithActorLabel;
}

void FNodeOccurence::Remove(FExportContext& Context)
{
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

	if (const TSharedPtr<IDatasmithActorElement>& ParentActor = DatasmithActorElement->GetParentActor())
	{
		ParentActor->RemoveChild(DatasmithActorElement);
	}
	else
	{
		Context.DatasmithScene->RemoveActor(DatasmithActorElement, EDatasmithActorRemovalRule::RemoveChildren);
	}

	if (ParentNode)
	{
		ParentNode->Children.Remove(this);
	}
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

void FModelDefinition::InvalidateInstancesGeometry(FExportContext& Context)
{
	Context.Model->InvalidateEntityGeometry();
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
		// Create a Datasmith metadata element for the SketckUp component instance metadata definition.
		FString MetadataElementName = FString::Printf(TEXT("%ls_DATA"), Node.DatasmithActorElement->GetName());
		// todo: metadata
		//TSharedPtr<IDatasmithMetaDataElement> DMetadataElementPtr = FDatasmithSketchUpMetadata::CreateMetadataElement(InSComponentInstanceRef, MetadataElementName);
		//if (DMetadataElementPtr.IsValid())
		//{
		//	// Set the Datasmith actor associated with the Datasmith metadata element.
		//	DMetadataElementPtr->SetAssociatedElement(DActorPtr);

		//	// Add the Datasmith metadata element to the Datasmith scene.
		//	DatasmithSceneRef->AddMetaData(DMetadataElementPtr);
		//}
		//Instance->DatasmithMetadataElement = DMetadataElementPtr;

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

	TArray<SUComponentInstanceRef> Instances;
	Instances.Init(SU_INVALID, InstanceCount);
	SUComponentDefinitionGetInstances(ComponentDefinitionRef, InstanceCount, Instances.GetData(), &InstanceCount);
	Instances.SetNum(InstanceCount);

	for (const SUComponentInstanceRef& InstanceRef : Instances)
	{
		Context.ComponentInstances.InvalidateComponentInstanceGeometry(DatasmithSketchUpUtils::GetComponentInstanceID(InstanceRef));
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



void FEntity::UpdateOccurrence(FExportContext& Context, FNodeOccurence& Node)
{
	FString EffectiveLayerName = SuGetString(SULayerGetName, Node.EffectiveLayerRef);

	FDefinition* EntityDefinition = GetDefinition();
	DatasmithSketchUp::FEntitiesGeometry& EntitiesGeometry = *EntityDefinition->GetEntities().EntitiesGeometry;

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
				MeshActor->AddMaterialOverride(Material->GetName(), FMaterial::INHERITED_MATERIAL_ID.EntityID);
			}
		}
	}
}

FDefinition* FComponentInstance::GetDefinition()
{
	return &Definition;
}

bool FComponentInstance::GetAssignedMaterial(FMaterialIDType& MaterialId)
{
	SUComponentInstanceRef ComponentInstanceRef = SUComponentInstanceFromEntity(EntityRef);
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
	SUComponentInstanceRef InSComponentInstanceRef = SUComponentInstanceFromEntity(EntityRef);

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

	if (Node.DatasmithMetadataElement.IsValid())
	{
		// Set the metadata element label used in the Unreal UI.
		Node.DatasmithMetadataElement->SetLabel(*Node.DatasmithActorLabel);
	}

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
	SUComponentInstanceRef ComponentInstanceRef = SUComponentInstanceFromEntity(EntityRef);
	return DatasmithSketchUpUtils::GetComponentPID(ComponentInstanceRef);
}

FString FComponentInstance::GetName()
{
	SUComponentInstanceRef InSComponentInstanceRef = SUComponentInstanceFromEntity(EntityRef);
	FString SComponentInstanceName;
	return SuGetString(SUComponentInstanceGetName, InSComponentInstanceRef);
}

TSharedRef<FNodeOccurence> FComponentInstance::CreateNodeOccurrence(FExportContext& Context, FNodeOccurence& ParentNode)
{
	TSharedRef<FNodeOccurence> ChildNode = MakeShared<FNodeOccurence>(&ParentNode, *this);
	ParentNode.Children.Add(&ChildNode.Get());
	Context.ComponentInstances.AddOccurrence(GetComponentInstanceId(), ChildNode);
	return ChildNode;
}

void FComponentInstance::UpdateOccurrencesGeometry(FExportContext& Context)
{
	TArray<TSharedPtr<FNodeOccurence>>* NodesPtr = Context.ComponentInstances.GetOccurrencesForComponentInstance(GetComponentInstanceId());
	if (NodesPtr)
	{
		for (const TSharedPtr<FNodeOccurence>& Node : *NodesPtr)
		{
			Node->CreateMeshActors(Context); // todo: reuse mesh actors?

			// Should invalidate transform to trigger transform update for mesh actors 
			// todo: can simplify this
			// - separate Transform invalidation from other properties? If it should give any improvement?
			// - or just update mesh actors transforms? we can't do it here though as transform can be invalidated by ancestors change later when occurrences are updated
			// - add another flag to invalidate just mesh actors properties and update them separately
			Node->InvalidateProperties(Context);
		}
	}
}

void FComponentInstance::InvalidateOccurrencesProperties(FExportContext& Context)
{
	TArray<TSharedPtr<FNodeOccurence>>* NodesPtr = Context.ComponentInstances.GetOccurrencesForComponentInstance(GetComponentInstanceId());
	if (NodesPtr)
	{
		for (const TSharedPtr<FNodeOccurence>& Node : *NodesPtr)
		{
			Node->InvalidateProperties(Context);
		}
	}
}

FComponentInstanceIDType FComponentInstance::GetComponentInstanceId()
{
	return DatasmithSketchUpUtils::GetComponentInstanceID(SUComponentInstanceFromEntity(EntityRef));
}

void FComponentInstance::RemoveOccurrences(FExportContext& Context)
{
	if (TArray<TSharedPtr<FNodeOccurence>>* OccurrencesPtr = Context.ComponentInstances.GetOccurrencesForComponentInstance(GetComponentInstanceId()))
	{
		for (const TSharedPtr<FNodeOccurence>& Occurrence : *OccurrencesPtr)
		{
			Occurrence->Remove(Context);
		}
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
   
void FModel::UpdateOccurrencesGeometry(FExportContext& Context)
{
	Context.RootNode->CreateMeshActors(Context);
	Context.RootNode->InvalidateProperties(Context);
}

void FModel::InvalidateOccurrencesProperties(FExportContext& Context)
{
	Context.RootNode->InvalidateProperties(Context);
}
