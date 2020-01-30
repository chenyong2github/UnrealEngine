// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithSketchUpComponent.h"

// SketchUp to Datasmith exporter classes.
#include "DatasmithSketchUpMaterial.h"
#include "DatasmithSketchUpMesh.h"
#include "DatasmithSketchUpMetadata.h"
#include "DatasmithSketchUpString.h"
#include "DatasmithSketchUpSummary.h"

// SketchUp SDK.
#include "DatasmithSketchUpSDKBegins.h"
#include "SketchUpAPI/model/component_definition.h"
#include "SketchUpAPI/model/component_instance.h"
#include "SketchUpAPI/model/drawing_element.h"
#include "SketchUpAPI/model/entities.h"
#include "SketchUpAPI/model/entity.h"
#include "SketchUpAPI/model/geometry.h"
#include "SketchUpAPI/model/group.h"
#include "SketchUpAPI/model/layer.h"
#include "SketchUpAPI/model/model.h"
#include "DatasmithSketchUpSDKCeases.h"

// Datasmith SDK.
#include "Containers/Array.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithUtils.h"

// OpenEXR third party library.
#include "ImathMatrixAlgo.h"


#ifdef SKP_SDK_2017
// Multiplies a SketchUp transformation by another transformation.
// SUTransformationMultiply is only available since SketchUp 2018, API 6.0.
void SUTransformationMultiply(
	const struct SUTransformation* InTransform1, // transformation object to be multiplied
	const struct SUTransformation* InTransform2, // transformation object to multiply by
	struct       SUTransformation* OutTransform  // result of the matrix multiplication InTransform1 * InTransform2
)
{
	auto& In1Values = InTransform1->values;
	auto& In2Values = InTransform2->values;
	auto& OutValues = OutTransform->values;

	// Note that the matrix values in SUTransformation are stored in column-major order.

	OutValues[0]  = In1Values[0] * In2Values[0]  + In1Values[4] * In2Values[1]  + In1Values[8]  * In2Values[2]  + In1Values[12] * In2Values[3];
	OutValues[4]  = In1Values[0] * In2Values[4]  + In1Values[4] * In2Values[5]  + In1Values[8]  * In2Values[6]  + In1Values[12] * In2Values[7];
	OutValues[8]  = In1Values[0] * In2Values[8]  + In1Values[4] * In2Values[9]  + In1Values[8]  * In2Values[10] + In1Values[12] * In2Values[11];
	OutValues[12] = In1Values[0] * In2Values[12] + In1Values[4] * In2Values[13] + In1Values[8]  * In2Values[14] + In1Values[12] * In2Values[15];

	OutValues[1]  = In1Values[1] * In2Values[0]  + In1Values[5] * In2Values[1]  + In1Values[9]  * In2Values[2]  + In1Values[13] * In2Values[3];
	OutValues[5]  = In1Values[1] * In2Values[4]  + In1Values[5] * In2Values[5]  + In1Values[9]  * In2Values[6]  + In1Values[13] * In2Values[7];
	OutValues[9]  = In1Values[1] * In2Values[8]  + In1Values[5] * In2Values[9]  + In1Values[9]  * In2Values[10] + In1Values[13] * In2Values[11];
	OutValues[13] = In1Values[1] * In2Values[12] + In1Values[5] * In2Values[13] + In1Values[9]  * In2Values[14] + In1Values[13] * In2Values[15];

	OutValues[2]  = In1Values[2] * In2Values[0]  + In1Values[6] * In2Values[1]  + In1Values[10] * In2Values[2]  + In1Values[14] * In2Values[3];
	OutValues[6]  = In1Values[2] * In2Values[4]  + In1Values[6] * In2Values[5]  + In1Values[10] * In2Values[6]  + In1Values[14] * In2Values[7];
	OutValues[10] = In1Values[2] * In2Values[8]  + In1Values[6] * In2Values[9]  + In1Values[10] * In2Values[10] + In1Values[14] * In2Values[11];
	OutValues[14] = In1Values[2] * In2Values[12] + In1Values[6] * In2Values[13] + In1Values[10] * In2Values[14] + In1Values[14] * In2Values[15];

	OutValues[3]  = In1Values[3] * In2Values[0]  + In1Values[7] * In2Values[1]  + In1Values[11] * In2Values[2]  + In1Values[15] * In2Values[3];
	OutValues[7]  = In1Values[3] * In2Values[4]  + In1Values[7] * In2Values[5]  + In1Values[11] * In2Values[6]  + In1Values[15] * In2Values[7];
	OutValues[11] = In1Values[3] * In2Values[8]  + In1Values[7] * In2Values[9]  + In1Values[11] * In2Values[10] + In1Values[15] * In2Values[11];
	OutValues[15] = In1Values[3] * In2Values[12] + In1Values[7] * In2Values[13] + In1Values[11] * In2Values[14] + In1Values[15] * In2Values[15];
}
#endif


TMap<int32, TSharedPtr<FDatasmithSketchUpComponent>> FDatasmithSketchUpComponent::ComponentDefinitionMap;

void FDatasmithSketchUpComponent::InitComponentDefinitionMap(
	SUModelRef InSModelRef
)
{
	// Get the number of normal component definitions in the SketchUp model.
	size_t SComponentDefinitionCount = 0;
	SUModelGetNumComponentDefinitions(InSModelRef, &SComponentDefinitionCount); // we can ignore the returned SU_RESULT

	if (SComponentDefinitionCount > 0)
	{
		// Retrieve the normal component definitions in the SketchUp model.
		TArray<SUComponentDefinitionRef> SComponentDefinitions;
		SComponentDefinitions.Init(SU_INVALID, SComponentDefinitionCount);
		SUModelGetComponentDefinitions(InSModelRef, SComponentDefinitionCount, SComponentDefinitions.GetData(), &SComponentDefinitionCount); // we can ignore the returned SU_RESULT
		SComponentDefinitions.SetNum(SComponentDefinitionCount);

		// Add the normal component definitions to our dictionary.
		for (SUComponentDefinitionRef SComponentDefinitionRef : SComponentDefinitions)
		{
			TSharedPtr<FDatasmithSketchUpComponent> ComponentPtr = TSharedPtr<FDatasmithSketchUpComponent>(new FDatasmithSketchUpComponent(SComponentDefinitionRef));
			ComponentDefinitionMap.Add(ComponentPtr->SSourceID, ComponentPtr);

			// Add the normal component definition metadata into the dictionary of metadata definitions.
			FDatasmithSketchUpMetadata::AddMetadataDefinition(SComponentDefinitionRef);
		}
	}

	// Get the number of group component definitions in the SketchUp model.
	size_t SGroupDefinitionCount = 0;
	SUModelGetNumGroupDefinitions(InSModelRef, &SGroupDefinitionCount); // we can ignore the returned SU_RESULT

	if (SGroupDefinitionCount > 0)
	{
		// Retrieve the group component definitions in the SketchUp model.
		TArray<SUComponentDefinitionRef> SGroupDefinitions;
		SGroupDefinitions.Init(SU_INVALID, SGroupDefinitionCount);
		SUModelGetGroupDefinitions(InSModelRef, SGroupDefinitionCount, SGroupDefinitions.GetData(), &SGroupDefinitionCount); // we can ignore the returned SU_RESULT
		SGroupDefinitions.SetNum(SGroupDefinitionCount);

		// Add the group component definitions to our dictionary.
		for (SUComponentDefinitionRef SGroupDefinitionRef : SGroupDefinitions)
		{
			TSharedPtr<FDatasmithSketchUpComponent> ComponentPtr = TSharedPtr<FDatasmithSketchUpComponent>(new FDatasmithSketchUpComponent(SGroupDefinitionRef));
			ComponentDefinitionMap.Add(ComponentPtr->SSourceID, ComponentPtr);
		}
	}
}

void FDatasmithSketchUpComponent::ClearComponentDefinitionMap()
{
	// Remove all entries from our dictionary of component definitions.
	ComponentDefinitionMap.Empty();
}

FDatasmithSketchUpComponent::FDatasmithSketchUpComponent(
	SUModelRef InSModelRef
):
	SSourceEntitiesRef(SU_INVALID),
	SSourceID(0),
	SSourceGUID(TEXT("MODEL")),
	bSSourceFaceCamera(false),
	bBakeEntitiesDone(false)
{
	// Retrieve the SketchUp model entities.
	SUModelGetEntities(InSModelRef, &SSourceEntitiesRef); // we can ignore the returned SU_RESULT

	// Retrieve the SketchUp model name.
	SU_GET_STRING(SUModelGetName, InSModelRef, SSourceName);

	// Get the number of component instances in the SketchUp model entities.
	SUEntitiesGetNumInstances(SSourceEntitiesRef, &SSourceComponentInstanceCount); // we can ignore the returned SU_RESULT

	// Get the number of groups in the SketchUp model entities.
	SUEntitiesGetNumGroups(SSourceEntitiesRef, &SSourceGroupCount); // we can ignore the returned SU_RESULT

	// Retrieve the default layer in the SketchUp model.
	SULayerRef SDefaultLayerRef = SU_INVALID;
	SUModelGetDefaultLayer(InSModelRef, &SDefaultLayerRef); // we can ignore the returned SU_RESULT

	// Add the model SketchUp entities geometry to the baked component meshes.
	BakeEntities(SDefaultLayerRef);

	// Add the model metadata into the dictionary of metadata definitions.
	FDatasmithSketchUpMetadata::AddMetadataDefinition(InSModelRef);
}

void FDatasmithSketchUpComponent::ConvertEntities(
	int32                              InComponentDepth,
	SUTransformation const&            InSWorldTransform,
	SULayerRef                         InSInheritedLayerRef,
	int32                              InSInheritedMaterialID,
	TSharedRef<IDatasmithScene>        IODSceneRef,
	TSharedPtr<IDatasmithActorElement> IODComponentActorPtr
)
{
	if (SSourceComponentInstanceCount > 0)
	{
		// Retrieve the component instances in the source SketchUp entities.
		TArray<SUComponentInstanceRef> SComponentInstances;
		SComponentInstances.Init(SU_INVALID, SSourceComponentInstanceCount);
		SUEntitiesGetInstances(SSourceEntitiesRef, SSourceComponentInstanceCount, SComponentInstances.GetData(), &SSourceComponentInstanceCount); // we can ignore the returned SU_RESULT
		SComponentInstances.SetNum(SSourceComponentInstanceCount);

		// Convert the SketchUp normal component instances into sub-hierarchies of Datasmith actors.
		for (SUComponentInstanceRef SComponentInstanceRef : SComponentInstances)
		{
			// Get the effective layer of the SketckUp normal component instance.
			SULayerRef SEffectiveLayerRef = GetEffectiveLayer(SComponentInstanceRef, InSInheritedLayerRef);

			// Get whether or not the SketckUp normal component instance is visible in the current SketchUp scene.
			if (IsVisible(SComponentInstanceRef, SEffectiveLayerRef))
			{
				ConvertInstance(InComponentDepth, InSWorldTransform, SEffectiveLayerRef, InSInheritedMaterialID, SComponentInstanceRef, IODSceneRef, IODComponentActorPtr);
			}
		}
	}

	if (SSourceGroupCount > 0)
	{
		// Retrieve the groups in the source SketchUp entities.
		TArray<SUGroupRef> SGroups;
		SGroups.Init(SU_INVALID, SSourceGroupCount);
		SUEntitiesGetGroups(SSourceEntitiesRef, SSourceGroupCount, SGroups.GetData(), &SSourceGroupCount); // we can ignore the returned SU_RESULT
		SGroups.SetNum(SSourceGroupCount);

		// Convert the SketchUp group component instances into sub-hierarchies of Datasmith actors.
		for (SUGroupRef SGroupRef : SGroups)
		{
			SUComponentInstanceRef SComponentInstanceRef = SUGroupToComponentInstance(SGroupRef);

			// Get the effective layer of the SketckUp group component instance.
			SULayerRef SEffectiveLayerRef = GetEffectiveLayer(SComponentInstanceRef, InSInheritedLayerRef);

			// Get whether or not the SketckUp group component instance is visible in the current SketchUp scene.
			if (IsVisible(SComponentInstanceRef, SEffectiveLayerRef))
			{
				ConvertInstance(InComponentDepth, InSWorldTransform, SEffectiveLayerRef, InSInheritedMaterialID, SComponentInstanceRef, IODSceneRef, IODComponentActorPtr);
			}
		}
	}

	for (auto const& BakedMeshPtr : BakedMeshes)
	{
		// Whether or not the Datasmith actor for the component is already a Datasmith mesh actor.
		TSharedPtr<IDatasmithMeshActorElement> DMeshActorPtr;
		if (IODComponentActorPtr->IsA(EDatasmithElementType::StaticMeshActor))
		{
			// The Datasmith mesh actor was already created to avoid an intermediate component instance actor.
			DMeshActorPtr = StaticCastSharedPtr<IDatasmithMeshActorElement>(IODComponentActorPtr);
		}
		else // IODComponentActorPtr is a EDatasmithElementType::Actor
		{
			FString ComponentActorName = IODComponentActorPtr->GetName();
			FString MeshActorName = FString::Printf(TEXT("%ls_%d"), *ComponentActorName, BakedMeshPtr->GetMeshIndex());
			FString MeshActorLabel = IODComponentActorPtr->GetLabel();

			// Create a Datasmith mesh actor for the Datasmith mesh element.
			DMeshActorPtr = FDatasmithSceneFactory::CreateMeshActor(*MeshActorName);

			// Set the mesh actor label used in the Unreal UI.
			DMeshActorPtr->SetLabel(*MeshActorLabel);

			// Set the Datasmith mesh actor layer name.
			DMeshActorPtr->SetLayer(IODComponentActorPtr->GetLayer());

			// Set the Datasmith mesh actor world transform.
			DMeshActorPtr->SetScale(IODComponentActorPtr->GetScale());
			DMeshActorPtr->SetRotation(IODComponentActorPtr->GetRotation());
			DMeshActorPtr->SetTranslation(IODComponentActorPtr->GetTranslation());

			// Add the Datasmith actor component depth tag.
			// We use component depth + 1 to factor in the added Datasmith scene root once imported in Unreal.
			FString ComponentDepthTag = FString::Printf(TEXT("SU.DEPTH.%d"), InComponentDepth + 1);
			DMeshActorPtr->AddTag(*ComponentDepthTag);

			// Add the Datasmith actor component definition GUID tag.
			FString DefinitionGUIDTag = FString::Printf(TEXT("SU.GUID.%ls"), *SSourceGUID);
			DMeshActorPtr->AddTag(*DefinitionGUIDTag);

			// Add the Datasmith actor component instance path tag.
			FString InstancePathTag = ComponentActorName.Replace(TEXT("SU"), TEXT("SU.PATH.0")).Replace(TEXT("_"), TEXT("."));
			DMeshActorPtr->AddTag(*InstancePathTag);

			// Add the mesh actor to our component Datasmith actor hierarchy.
			if (InComponentDepth == 0)
			{
				IODSceneRef->AddActor(DMeshActorPtr);
			}
			else
			{
				IODComponentActorPtr->AddChild(DMeshActorPtr);
			}

			// ADD_TRACE_LINE(TEXT("Actor %ls: %ls %ls %ls"), *MeshActorLabel, *ComponentDepthTag, *DefinitionGUIDTag, *InstancePathTag);
		}

		// Set the Datasmith mesh element used by the mesh actor.
		DMeshActorPtr->SetStaticMeshPathName(*BakedMeshPtr->GetMeshElementName());

		// Add the inherited materiel used by the Datasmith mesh element.
		if (BakedMeshPtr->UsesInheritedMaterialID())
		{
			// Get the material name sanitized for Datasmith.
			FString const& MeshMaterialName = FDatasmithSketchUpMaterial::GetInheritedMaterialName(InSInheritedMaterialID);

			// Add the material to the Datasmith mesh actor.
			DMeshActorPtr->AddMaterialOverride(*MeshMaterialName, FDatasmithSketchUpMaterial::INHERITED_MATERIAL_ID);
		}
	}
}

int32 FDatasmithSketchUpComponent::GetComponentID(
	SUComponentDefinitionRef InSComponentDefinitionRef
)
{
	// Get the SketckUp component definition ID.
	int32 SComponentID = 0;
	SUEntityGetID(SUComponentDefinitionToEntity(InSComponentDefinitionRef), &SComponentID); // we can ignore the returned SU_RESULT

	return SComponentID;
}

int64 FDatasmithSketchUpComponent::GetComponentPID(
	SUComponentInstanceRef InSComponentInstanceRef
)
{
	// Get the SketckUp component instance persistent ID.
	int64 SPersistentID = 0;
	SUEntityGetPersistentID(SUComponentInstanceToEntity(InSComponentInstanceRef), &SPersistentID); // we can ignore the returned SU_RESULT

	return SPersistentID;
}

FDatasmithSketchUpComponent::FDatasmithSketchUpComponent(
	SUComponentDefinitionRef InSComponentDefinitionRef
) :
	SSourceEntitiesRef(SU_INVALID),
	bBakeEntitiesDone(false)
{
	// Retrieve the SketchUp component definition entities.
	SUComponentDefinitionGetEntities(InSComponentDefinitionRef, &SSourceEntitiesRef); // we can ignore the returned SU_RESULT

	// Get the component ID of the SketckUp component definition.
	SSourceID = GetComponentID(InSComponentDefinitionRef);

	// Retrieve the SketchUp component definition IFC GUID.
	SU_GET_STRING(SUComponentDefinitionGetGuid, InSComponentDefinitionRef, SSourceGUID);

	// Retrieve the SketchUp component definition name.
	SU_GET_STRING(SUComponentDefinitionGetName, InSComponentDefinitionRef, SSourceName);

	// Get the number of component instances in the SketchUp component definition entities.
	SUEntitiesGetNumInstances(SSourceEntitiesRef, &SSourceComponentInstanceCount); // we can ignore the returned SU_RESULT

	// Get the number of groups in the SketchUp component definition entities.
	SUEntitiesGetNumGroups(SSourceEntitiesRef, &SSourceGroupCount); // we can ignore the returned SU_RESULT

	// Retrieve the SketchUp component definition behavior in the rendering scene.
	SUComponentBehavior SComponentBehavior;
	SUComponentDefinitionGetBehavior(InSComponentDefinitionRef, &SComponentBehavior); // we can ignore the returned SU_RESULT

	// Get whether or not the source SketchUp component behaves like a billboard.
	bSSourceFaceCamera = SComponentBehavior.component_always_face_camera;
}

void FDatasmithSketchUpComponent::BakeEntities(
	SULayerRef InSInheritedLayerRef
)
{
	// Bake the entities only once.
	if (bBakeEntitiesDone)
	{
		return;
	}

	if (SSourceComponentInstanceCount > 0)
	{
		// Retrieve the component instances in the source SketchUp entities.
		TArray<SUComponentInstanceRef> SComponentInstances;
		SComponentInstances.Init(SU_INVALID, SSourceComponentInstanceCount);
		SUEntitiesGetInstances(SSourceEntitiesRef, SSourceComponentInstanceCount, SComponentInstances.GetData(), &SSourceComponentInstanceCount); // we can ignore the returned SU_RESULT
		SComponentInstances.SetNum(SSourceComponentInstanceCount);

		// Add the SketchUp component instance geometries to the baked component meshes.
		for (SUComponentInstanceRef SComponentInstanceRef : SComponentInstances)
		{
			// Get the effective layer of the SketckUp component instance.
			SULayerRef SEffectiveLayerRef = GetEffectiveLayer(SComponentInstanceRef, InSInheritedLayerRef);

			// Get whether or not the SketckUp component instance is visible in the current SketchUp scene.
			if (IsVisible(SComponentInstanceRef, SEffectiveLayerRef))
			{
				// Retrieve the normal component definition in the dictionary of component definitions.
				TSharedPtr<FDatasmithSketchUpComponent> ComponentPtr = GetComponentDefinition(SComponentInstanceRef);

				if (ComponentPtr.IsValid())
				{
					// Add the component SketchUp entities geometry to its baked component mesh.
					ComponentPtr->BakeEntities(SEffectiveLayerRef);

					// Add the normal component instance metadata into the dictionary of metadata definitions.
					FDatasmithSketchUpMetadata::AddMetadataDefinition(SComponentInstanceRef);
				}
			}
		}
	}

	if (SSourceGroupCount > 0)
	{
		// Retrieve the groups in the source SketchUp entities.
		TArray<SUGroupRef> SGroups;
		SGroups.Init(SU_INVALID, SSourceGroupCount);
		SUEntitiesGetGroups(SSourceEntitiesRef, SSourceGroupCount, SGroups.GetData(), &SSourceGroupCount); // we can ignore the returned SU_RESULT
		SGroups.SetNum(SSourceGroupCount);

		// Add the SketchUp group geometries to the baked component meshes.
		for (SUGroupRef SGroupRef : SGroups)
		{
			SUComponentInstanceRef SComponentInstanceRef = SUGroupToComponentInstance(SGroupRef);

			// Get the effective layer of the SketckUp group.
			SULayerRef SEffectiveLayerRef = GetEffectiveLayer(SComponentInstanceRef, InSInheritedLayerRef);

			// Get whether or not the SketckUp group is visible in the current SketchUp scene.
			if (IsVisible(SComponentInstanceRef, SEffectiveLayerRef))
			{
				// Retrieve the group component definition in the dictionary of component definitions.
				TSharedPtr<FDatasmithSketchUpComponent> ComponentPtr = GetComponentDefinition(SComponentInstanceRef);

				if (ComponentPtr.IsValid())
				{
					// Add the component SketchUp entities geometry to its baked component mesh.
					ComponentPtr->BakeEntities(SEffectiveLayerRef);
				}
			}
		}
	}

	// Get the number of faces in the source SketchUp entities.
	size_t SFaceCount = 0;
	SUEntitiesGetNumFaces(SSourceEntitiesRef, &SFaceCount); // we can ignore the returned SU_RESULT

	if (SFaceCount > 0)
	{
		// Retrieve the faces in the source SketchUp entities.
		TArray<SUFaceRef> SFaces;
		SFaces.Init(SU_INVALID, SFaceCount);
		SUEntitiesGetFaces(SSourceEntitiesRef, SFaceCount, SFaces.GetData(), &SFaceCount); // we can ignore the returned SU_RESULT
		SFaces.SetNum(SFaceCount);

		// Bake the SketchUp component definition faces into a list of component meshes.
		FDatasmithSketchUpMesh::BakeMeshes(*SSourceGUID, *SSourceName, InSInheritedLayerRef, SFaces, BakedMeshes);
	}

	bBakeEntitiesDone = true;
}

void FDatasmithSketchUpComponent::ConvertInstance(
	int32                              InComponentDepth,
	SUTransformation const&            InSWorldTransform,
	SULayerRef                         InSEffectiveLayerRef,
	int32                              InSInheritedMaterialID,
	SUComponentInstanceRef             InSComponentInstanceRef,
	TSharedRef<IDatasmithScene>        IODSceneRef,
	TSharedPtr<IDatasmithActorElement> IODComponentActorPtr
)
{
	// Retrieve the component definition in the dictionary of component definitions.
	TSharedPtr<FDatasmithSketchUpComponent> ComponentPtr = GetComponentDefinition(InSComponentInstanceRef);

	if (!ComponentPtr.IsValid())
	{
		return;
	}

	// Retrieve the SketchUp component instance name.
	FString SComponentInstanceName;
	SU_GET_STRING(SUComponentInstanceGetName, InSComponentInstanceRef, SComponentInstanceName);

	// Get the SketchUp component instance transform.
	SUTransformation SComponentInstanceTransform;
	SUComponentInstanceGetTransform(InSComponentInstanceRef, &SComponentInstanceTransform); // we can ignore the returned SU_RESULT

	// Compute the world transform of the SketchUp component instance.
	SUTransformation SComponentInstanceWorldTransform;
	SUTransformationMultiply(&InSWorldTransform, &SComponentInstanceTransform, &SComponentInstanceWorldTransform); // we can ignore the returned SU_RESULT

	// Retrieve the SketchUp component instance effective layer name.
	FString SEffectiveLayerName;
	SU_GET_STRING(SULayerGetName, InSEffectiveLayerRef, SEffectiveLayerName);

	// Retrieve the SketckUp component instance material.
	SUMaterialRef SComponentInstanceMaterialRef = FDatasmithSketchUpMaterial::GetMaterial(InSComponentInstanceRef);

	// Set the effective inherited material ID.
	if (SUIsValid(SComponentInstanceMaterialRef))
	{
		// Get the material ID of the SketckUp component instance material.
		InSInheritedMaterialID = FDatasmithSketchUpMaterial::GetMaterialID(SComponentInstanceMaterialRef);
	}

	// Get the SketckUp component instance persistent ID.
	int64 SPersistentID = GetComponentPID(InSComponentInstanceRef);

	FString ActorName  = FString::Printf(TEXT("%ls_%lld"), IODComponentActorPtr->GetName(), SPersistentID);
	FString ActorLabel = FDatasmithUtils::SanitizeObjectName(SComponentInstanceName.IsEmpty() ? ComponentPtr->SSourceName : SComponentInstanceName);

	TSharedPtr<IDatasmithActorElement> DActorPtr;
	if (ComponentPtr->SSourceComponentInstanceCount > 0 || ComponentPtr->SSourceGroupCount > 0 || ComponentPtr->BakedMeshes.Num() > 1)
	{
		// Create a Datasmith actor for the component instance.
		DActorPtr = FDatasmithSceneFactory::CreateActor(*ActorName); // a EDatasmithElementType::Actor
	}
	else if (ComponentPtr->BakedMeshes.Num() == 1)
	{
		// Create a Datasmith mesh actor directly to avoid the intermediate component instance actor.
		FString MeshActorName = FString::Printf(TEXT("%ls_1"), *ActorName);
		DActorPtr = FDatasmithSceneFactory::CreateMeshActor(*MeshActorName); // a EDatasmithElementType::StaticMeshActor
	}

	if (DActorPtr.IsValid())
	{
		// Set the actor label used in the Unreal UI.
		DActorPtr->SetLabel(*ActorLabel);

		// Set the Datasmith actor layer name.
		DActorPtr->SetLayer(*FDatasmithUtils::SanitizeObjectName(SEffectiveLayerName));

		// Set the Datasmith actor world transform.
		SetActorTransform(DActorPtr, SComponentInstanceWorldTransform);

		// Add the Datasmith actor component depth tag.
		// We use component depth + 1 to factor in the added Datasmith scene root once imported in Unreal.
		FString ComponentDepthTag = FString::Printf(TEXT("SU.DEPTH.%d"), InComponentDepth + 1);
		DActorPtr->AddTag(*ComponentDepthTag);

		// Add the Datasmith actor component definition GUID tag.
		FString DefinitionGUIDTag = FString::Printf(TEXT("SU.GUID.%ls"), *ComponentPtr->SSourceGUID);
		DActorPtr->AddTag(*DefinitionGUIDTag);

		// Add the Datasmith actor component instance path tag.
		FString InstancePathTag = ActorName.Replace(TEXT("SU"), TEXT("SU.PATH.0")).Replace(TEXT("_"), TEXT("."));
		DActorPtr->AddTag(*InstancePathTag);

		// Add the Datasmith actor component instance face camera tag when required.
		if (ComponentPtr->bSSourceFaceCamera)
		{
			DActorPtr->AddTag(TEXT("SU.BEHAVIOR.FaceCamera"));
		}

		// Add the component instance actor to our component Datasmith actor hierarchy.
		if (InComponentDepth == 0)
		{
			IODSceneRef->AddActor(DActorPtr);
		}
		else
		{
			IODComponentActorPtr->AddChild(DActorPtr);
		}

		// ADD_TRACE_LINE(TEXT("Actor %ls: %ls %ls %ls"), *ActorLabel, *ComponentDepthTag, *DefinitionGUIDTag, *InstancePathTag);

		// Create a Datasmith metadata element for the SketckUp component instance metadata definition.
		FString MetadataElementName = FString::Printf(TEXT("%ls_DATA"), DActorPtr->GetName());
		TSharedPtr<IDatasmithMetaDataElement> DMetadataElementPtr = FDatasmithSketchUpMetadata::CreateMetadataElement(InSComponentInstanceRef, MetadataElementName);

		if (DMetadataElementPtr.IsValid())
		{
			// Set the metadata element label used in the Unreal UI.
			DMetadataElementPtr->SetLabel(*ActorLabel);

			// Set the Datasmith actor associated with the Datasmith metadata element.
			DMetadataElementPtr->SetAssociatedElement(DActorPtr);

			// Add the Datasmith metadata element to the Datasmith scene.
			IODSceneRef->AddMetaData(DMetadataElementPtr);
		}

		// Convert the component descendents into Datasmith actors.
		ComponentPtr->ConvertEntities(InComponentDepth + 1, SComponentInstanceWorldTransform, InSEffectiveLayerRef, InSInheritedMaterialID, IODSceneRef, DActorPtr);
	}
}

SULayerRef FDatasmithSketchUpComponent::GetEffectiveLayer(
	SUComponentInstanceRef InSComponentInstanceRef,
	SULayerRef             InSInheritedLayerRef
) const
{
	// Retrieve the SketckUp component instance layer.
	SULayerRef SComponentInstanceLayerRef = SU_INVALID;
	SUDrawingElementGetLayer(SUComponentInstanceToDrawingElement(InSComponentInstanceRef), &SComponentInstanceLayerRef); // we can ignore the returned SU_RESULT

	// Retrieve the SketchUp component instance layer name.
	FString SComponentInstanceLayerName;
	SU_GET_STRING(SULayerGetName, SComponentInstanceLayerRef, SComponentInstanceLayerName);

	// Return the effective layer.
	return SComponentInstanceLayerName.Equals(TEXT("Layer0")) ? InSInheritedLayerRef : SComponentInstanceLayerRef;
}

bool FDatasmithSketchUpComponent::IsVisible(
	SUComponentInstanceRef InSComponentInstanceRef,
	SULayerRef             InSEffectiveLayerRef
) const
{
	// Get the flag indicating whether or not the SketchUp component instance is hidden.
	bool bSComponentInstanceHidden = false;
	SUDrawingElementGetHidden(SUComponentInstanceToDrawingElement(InSComponentInstanceRef), &bSComponentInstanceHidden); // we can ignore the returned SU_RESULT

	// Get the flag indicating whether or not the SketchUp component instance effective layer is visible.
	bool bSEffectiveLayerVisible = true;
	SULayerGetVisibility(InSEffectiveLayerRef, &bSEffectiveLayerVisible); // we can ignore the returned SU_RESULT

	return (!bSComponentInstanceHidden && bSEffectiveLayerVisible);
}

TSharedPtr<FDatasmithSketchUpComponent> FDatasmithSketchUpComponent::GetComponentDefinition(
	SUComponentInstanceRef InSComponentInstanceRef
) const
{
	// Retrieve the component definition of the SketchUp component instance.
	SUComponentDefinitionRef SComponentDefinitionRef = SU_INVALID;
	SUComponentInstanceGetDefinition(InSComponentInstanceRef, &SComponentDefinitionRef); // we can ignore the returned SU_RESULT

	// Get the component ID of the SketckUp component definition.
	int32 SComponentID = GetComponentID(SComponentDefinitionRef);

	// Make sure the SketchUp component definition exists in our dictionary of component definitions.
	if (ComponentDefinitionMap.Contains(SComponentID))
	{
		// Return the component definition.
		return ComponentDefinitionMap[SComponentID];
	}

	// Retrieve the SketchUp component definition name.
	FString SComponentDefinitionName;
	SU_GET_STRING(SUComponentDefinitionGetName, SComponentDefinitionRef, SComponentDefinitionName);

	ADD_SUMMARY_LINE(TEXT("WARNING: Cannot find component %ls"), *SComponentDefinitionName);

	return TSharedPtr<FDatasmithSketchUpComponent>();
}

void FDatasmithSketchUpComponent::SetActorTransform(
	TSharedPtr<IDatasmithActorElement> IODActorPtr,
	SUTransformation const&            InSWorldTransform
) const
{
	// We use Imath::extractAndRemoveScalingAndShear() because FMatrix::ExtractScaling() is deemed unreliable.

	// Set up a scaling and rotation matrix.
	auto& SMatrix = InSWorldTransform.values;
	Imath::Matrix44<float> Matrix(float(SMatrix[0]), float(SMatrix[1]), float(SMatrix[2]),  0.0,
	                              float(SMatrix[4]), float(SMatrix[5]), float(SMatrix[6]),  0.0,
	                              float(SMatrix[8]), float(SMatrix[9]), float(SMatrix[10]), 0.0,
	                              0.0,               0.0,               0.0,                1.0);

	// Remove any scaling from the matrix and get the scale vector that was initially present.
	Imath::Vec3<float> Scale;
	Imath::Vec3<float> Shear;
	bool bExtracted = Imath::extractAndRemoveScalingAndShear<float>(Matrix, Scale, Shear, false);

	if (!bExtracted)
	{
		ADD_SUMMARY_LINE(TEXT("WARNING: Actor %ls (%ls) has some zero scaling"), IODActorPtr->GetName(), IODActorPtr->GetLabel());
		return;
	}

	if (SMatrix[15] != 1.0)
	{
		// Apply the extra SketchUp uniform scaling factor.
		Scale *= float(SMatrix[15]);
	}

	// Initialize a rotation quaternion with the rotation matrix.
	Imath::Quat<float> Quaternion = Imath::extractQuat<float>(Matrix);

	// Convert the SketchUp right-handed Z-up coordinate rotation into an Unreal left-handed Z-up coordinate rotation.
	// This is done by inverting the X and Z components of the quaternion to mirror the quaternion on the XZ-plane.
	Quaternion.v.x = -Quaternion.v.x;
	Quaternion.v.z = -Quaternion.v.z;
	Quaternion.normalize();

	// Convert the SketchUp right-handed Z-up coordinate translation into an Unreal left-handed Z-up coordinate translation.
	// To avoid perturbating X, which is forward in Unreal, the handedness conversion is done by flipping the side vector Y.
	// SketchUp uses inches as internal system unit for all 3D coordinates in the model while Unreal uses centimeters.
	const float UnitScale = 2.54; // centimeters per inch
	FVector Translation(float(SMatrix[12] * UnitScale), float(-SMatrix[13] * UnitScale), float(SMatrix[14] * UnitScale));

	// Make sure Unreal will be able to handle the rotation quaternion.
	float              Angle = Quaternion.angle();
	Imath::Vec3<float> Axis  = Quaternion.axis();
	FQuat Rotation(FVector(Axis.x, Axis.y, Axis.z), Angle);

	// Set the world transform of the Datasmith actor.
	IODActorPtr->SetScale(Scale.x, Scale.y, Scale.z);
	IODActorPtr->SetRotation(Rotation);
	IODActorPtr->SetTranslation(Translation);
}
