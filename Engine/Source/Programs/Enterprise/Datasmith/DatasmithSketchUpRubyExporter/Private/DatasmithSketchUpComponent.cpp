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


void FNodeOccurence::CreateMeshActors(FExportContext& Context)
{
	FDefinition* EntityDefinition = Entity.GetDefinition();

	// Create Mesh Actors for loose Entities geometry
	FString ComponentActorName = GetActorName();
	FString MeshActorLabel = GetActorLabel();
	DatasmithSketchUp::FEntitiesGeometry& EntitiesGeometry = *EntityDefinition->GetEntities().EntitiesGeometry;
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
			DMeshActorPtr->SetScale(DatasmithActorElement->GetScale());
			DMeshActorPtr->SetRotation(DatasmithActorElement->GetRotation());
			DMeshActorPtr->SetTranslation(DatasmithActorElement->GetTranslation());

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
	Entity.UpdateNode(Context, *this);
}

FString FNodeOccurence::GetActorName()
{
	return DatasmithActorName;
}

FString FNodeOccurence::GetActorLabel()
{
	return DatasmithActorLabel;
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

	if (Node.ParentNode && Node.ParentNode->DatasmithActorElement)
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
	// Bake meshes before considering setting up an occurrence 
	// todo: bake meshes before node instance even created(at the ConvertNodeToDatasmith call site)?
	if (bBakeEntitiesDone)
	{
		return;
	}
	Entities->UpdateGeometry(Context);
	bBakeEntitiesDone = true;
}

void FComponentDefinition::BuildNodeNames(FNodeOccurence& Node)
{
	// Get the SketckUp component instance persistent ID.
	int64 SketchupPersistentID = Node.Entity.GetPersistentId();
	Node.DatasmithActorName = FString::Printf(TEXT("%ls_%lld"), *Node.ParentNode->GetActorName(), SketchupPersistentID);

	FString EntityName = Node.Entity.GetName();
	Node.DatasmithActorLabel = FDatasmithUtils::SanitizeObjectName(EntityName.IsEmpty() ? Node.Entity.GetDefinition()->GetSketchupSourceName() : EntityName);
}

void FComponentDefinition::UpdateInstances(FExportContext& Context)
{
	size_t InstanceCount = 0;
	SUComponentDefinitionGetNumInstances(ComponentDefinitionRef, &InstanceCount);

	TArray<SUComponentInstanceRef> Instances;
	Instances.Init(SU_INVALID, InstanceCount);
	SUComponentDefinitionGetInstances(ComponentDefinitionRef, InstanceCount, Instances.GetData(), &InstanceCount);
	Instances.SetNum(InstanceCount);

	for (const SUComponentInstanceRef& InstanceRef : Instances)
	{
		TArray<TSharedPtr<FNodeOccurence>>* NodesPtr = Context.ComponentInstances.GetOccurrencesForComponentInstance(DatasmithSketchUpUtils::GetComponentInstanceID(InstanceRef));
		if (NodesPtr)
		{
			for (const TSharedPtr<FNodeOccurence>& Node : *NodesPtr)
			{
				Node->Update(Context);
			}
		}
	}

}

void FEntity::UpdateNode(FExportContext& Context, FNodeOccurence& Node)
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

void FComponentInstance::UpdateNode(FExportContext& Context, FNodeOccurence& Node)
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
	// ADD_TRACE_LINE(TEXT("Actor %ls: %ls %ls %ls"), *ActorLabel, *ComponentDepthTag, *DefinitionGUIDTag, *InstancePathTag);

	if (Node.DatasmithMetadataElement.IsValid())
	{
		// Set the metadata element label used in the Unreal UI.
		Node.DatasmithMetadataElement->SetLabel(*Node.DatasmithActorLabel);
	}


	Super::UpdateNode(Context, Node);
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
	Context.ComponentInstances.AddOccurrence(DatasmithSketchUpUtils::GetComponentInstanceID(SUComponentInstanceFromEntity(EntityRef)), ChildNode);
	return ChildNode;
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

