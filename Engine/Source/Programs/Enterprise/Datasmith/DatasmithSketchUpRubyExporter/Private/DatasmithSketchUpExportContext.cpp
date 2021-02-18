// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithSketchUpExportContext.h"
#include "DatasmithSketchUpUtils.h"
#include "DatasmithSketchUpComponent.h"
#include "DatasmithSketchUpCamera.h"
#include "DatasmithSketchUpMaterial.h"
#include "DatasmithSketchUpMaterial.h"
#include "DatasmithSketchUpMesh.h"
#include "DatasmithSketchUpMetadata.h"
#include "DatasmithSketchUpString.h"
#include "DatasmithSketchUpSummary.h"

#include "DatasmithExportOptions.h"
#include "DatasmithMesh.h"
#include "DatasmithMeshExporter.h"
#include "DatasmithSceneExporter.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithUtils.h"
#include "IDatasmithSceneElements.h"

#include "Misc/SecureHash.h"
#include "Misc/Paths.h"


// SketchUp SDK.
#include "DatasmithSketchUpSDKBegins.h"

#include "SketchUpAPI/model/camera.h"
#include "SketchUpAPI/model/component_definition.h"
#include "SketchUpAPI/model/component_instance.h"
#include "SketchUpAPI/model/drawing_element.h"
#include "SketchUpAPI/model/edge.h"
#include "SketchUpAPI/model/entities.h"
#include "SketchUpAPI/model/entity.h"
#include "SketchUpAPI/model/face.h"
#include "SketchUpAPI/model/geometry.h"
#include "SketchUpAPI/model/group.h"
#include "SketchUpAPI/model/layer.h"
#include "SketchUpAPI/model/mesh_helper.h"
#include "SketchUpAPI/model/model.h"
#include "SketchUpAPI/model/scene.h"
#include "SketchUpAPI/model/texture.h"
#include "SketchUpAPI/model/uv_helper.h"

#include "SketchUpAPI/application/application.h"

#include "DatasmithSketchUpSDKCeases.h"

using namespace DatasmithSketchUp;


FExportContext::FExportContext()
	: ComponentDefinitions(*this)
	, ComponentInstances(*this)
	, EntitiesObjects(*this)
	, Materials(*this)
	, Scenes(*this)
	, Textures(*this)

{

}

const TCHAR* FExportContext::GetAssetsOutputPath() const
{
	return SceneExporter->GetAssetsOutputPath();
}

void FExportContext::Populate()
{
	// Get Active Model
	SUResult su_api_result = SUApplicationGetActiveModel(&ModelRef);
	if (SUIsInvalid(ModelRef)) {
		return;
	}

	SUTransformation WorldTransform = { 1.0, 0.0, 0.0, 0.0,
										0.0, 1.0, 0.0, 0.0,
										0.0, 0.0, 1.0, 0.0,
										0.0, 0.0, 0.0, 1.0 };

	// Set up root 'Node'
	ModelDefinition = MakeShared<DatasmithSketchUp::FModelDefinition>(ModelRef);
	ModelDefinition->Parse(*this);

	// Retrieve the default layer in the SketchUp model.
	SULayerRef DefaultLayerRef = SU_INVALID;
	SUModelGetDefaultLayer(ModelRef, &DefaultLayerRef);

	// Setup root Node, based on Model
	Model = MakeShared<DatasmithSketchUp::FModel>(*ModelDefinition);
	RootNode = MakeShared<DatasmithSketchUp::FNodeOccurence>(*Model);
	RootNode->WorldTransform = WorldTransform;
	RootNode->EffectiveLayerRef = DefaultLayerRef;
	// Name and label for root loose mesh actors
	RootNode->DatasmithActorName = TEXT("SU");
	RootNode->DatasmithActorLabel = TEXT("Model");

	// Parse/convert Model
	Materials.PopulateFromModel(ModelRef);
	Scenes.PopulateFromModel(ModelRef);
	ComponentDefinitions.PopulateFromModel(ModelRef);

	// Add the model metadata into the dictionary of metadata definitions.
	FDatasmithSketchUpMetadata::AddMetadataDefinition(ModelRef);

	RootNode->ToDatasmith(*this);

	Textures.ConvertToDatasmith();
}

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

	// Make sure SketchUp loose geometry(faces, contained in Entities) is parsed first, to define number of Datasmith meshes and...
	EntityDefinition->UpdateGeometry(Context);
	// ...only then create mesh actors for those meshes
	CreateMeshActors(Context);

	// Update node local properties(including datasmith mesh actors)
	// This needs to be called before parsing hierarchy - descendant nodes are using parent transform
	Update(Context);

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

					// TSharedRef<FNodeOccurence> ChildNode = AddChildComponentInstanceOccurrence(*this, SComponentInstanceRef, ComponentInstance);
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


void FSceneCollection::PopulateFromModel(SUModelRef InModelRef)
{
	// Get the number of scenes in the SketchUp model.
	size_t SceneCount = 0;
	SUModelGetNumScenes(InModelRef, &SceneCount); // we can ignore the returned SU_RESULT

	if (SceneCount > 0)
	{
		// Retrieve the scenes in the SketchUp model.
		TArray<SUSceneRef> Scenes;
		Scenes.Init(SU_INVALID, SceneCount);
		SUResult SResult = SUModelGetScenes(InModelRef, SceneCount, Scenes.GetData(), &SceneCount);
		Scenes.SetNum(SceneCount);
		// Make sure the SketchUp model has scenes to retrieve (no SU_ERROR_NO_DATA).
		if (SResult == SU_ERROR_NONE)
		{
			for (SUSceneRef SceneRef : Scenes)
			{
				// Make sure the SketchUp scene uses a camera.
				bool bSceneUseCamera = false;
				SUSceneGetUseCamera(SceneRef, &bSceneUseCamera); // we can ignore the returned SU_RESULT

				if (bSceneUseCamera)
				{
					TSharedPtr<DatasmithSketchUp::FCamera> Camera = FCamera::Create(Context, SceneRef);
					SceneIdToCameraMap.Add(DatasmithSketchUpUtils::GetSceneID(SceneRef), Camera);
				}
			}
		}
	}
}


void FComponentDefinitionCollection::PopulateFromModel(SUModelRef InModelRef)
{
	// Get the number of normal component definitions in the SketchUp model.
	size_t SComponentDefinitionCount = 0;
	SUModelGetNumComponentDefinitions(InModelRef, &SComponentDefinitionCount); // we can ignore the returned SU_RESULT

	if (SComponentDefinitionCount > 0)
	{
		// Retrieve the normal component definitions in the SketchUp model.
		TArray<SUComponentDefinitionRef> SComponentDefinitions;
		SComponentDefinitions.Init(SU_INVALID, SComponentDefinitionCount);
		SUModelGetComponentDefinitions(InModelRef, SComponentDefinitionCount, SComponentDefinitions.GetData(), &SComponentDefinitionCount); // we can ignore the returned SU_RESULT
		SComponentDefinitions.SetNum(SComponentDefinitionCount);

		// Add the normal component definitions to our dictionary.
		for (SUComponentDefinitionRef SComponentDefinitionRef : SComponentDefinitions)
		{
			AddComponentDefinition(SComponentDefinitionRef);

			// Add the normal component definition metadata into the dictionary of metadata definitions.
			FDatasmithSketchUpMetadata::AddMetadataDefinition(SComponentDefinitionRef);
		}
	}

	// Get the number of group component definitions in the SketchUp model.
	size_t SGroupDefinitionCount = 0;
	SUModelGetNumGroupDefinitions(InModelRef, &SGroupDefinitionCount); // we can ignore the returned SU_RESULT

	if (SGroupDefinitionCount > 0)
	{
		// Retrieve the group component definitions in the SketchUp model.
		TArray<SUComponentDefinitionRef> SGroupDefinitions;
		SGroupDefinitions.Init(SU_INVALID, SGroupDefinitionCount);
		SUModelGetGroupDefinitions(InModelRef, SGroupDefinitionCount, SGroupDefinitions.GetData(), &SGroupDefinitionCount); // we can ignore the returned SU_RESULT
		SGroupDefinitions.SetNum(SGroupDefinitionCount);

		// Add the group component definitions to our dictionary.
		for (SUComponentDefinitionRef SGroupDefinitionRef : SGroupDefinitions)
		{
			AddComponentDefinition(SGroupDefinitionRef);
		}
	}
}

void FComponentDefinitionCollection::AddComponentDefinition(SUComponentDefinitionRef InComponentDefinitionRef)
{
	TSharedPtr<FComponentDefinition> Definition = MakeShared<FComponentDefinition>(InComponentDefinitionRef);
	Definition->Parse(Context);
	ComponentDefinitionMap.Add(Definition->SketchupSourceID, Definition);
}

TSharedPtr<FComponentDefinition> FComponentDefinitionCollection::GetComponentDefinition(
	SUComponentInstanceRef InComponentInstanceRef
)
{
	// Retrieve the component definition of the SketchUp component instance.
	SUComponentDefinitionRef SComponentDefinitionRef = SU_INVALID;
	SUComponentInstanceGetDefinition(InComponentInstanceRef, &SComponentDefinitionRef); // we can ignore the returned SU_RESULT

	// Get the component ID of the SketckUp component definition.
	FComponentDefinitionIDType SComponentID = DatasmithSketchUpUtils::GetComponentID(SComponentDefinitionRef);

	// Make sure the SketchUp component definition exists in our dictionary of component definitions.
	if (ComponentDefinitionMap.Contains(SComponentID))
	{
		// Return the component definition.
		return ComponentDefinitionMap[SComponentID];
	}

	// Retrieve the SketchUp component definition name.
	FString SComponentDefinitionName;
	SComponentDefinitionName = SuGetString(SUComponentDefinitionGetName, SComponentDefinitionRef);

	ADD_SUMMARY_LINE(TEXT("WARNING: Cannot find component %ls"), *SComponentDefinitionName);

	return TSharedPtr<FComponentDefinition>();
}

void FEntitiesObjectCollection::RegisterEntitiesFaces(DatasmithSketchUp::FEntities& Entities, const TSet<int32>& FaceIds)
{
	for (int32 FaceId : FaceIds)
	{
		FaceIdForEntitiesMap.Add(FaceId, &Entities);
	}
}

TSharedPtr<FEntities> FEntitiesObjectCollection::AddEntities(FDefinition& InDefinition, SUEntitiesRef EntitiesRef)
{
	TSharedPtr<FEntities> Entities = MakeShared<FEntities>(InDefinition);

	Entities->EntitiesRef = EntitiesRef;
	// Get the number of component instances in the SketchUp model entities.
	SUEntitiesGetNumInstances(EntitiesRef, &Entities->SourceComponentInstanceCount);
	// Get the number of groups in the SketchUp model entities.
	SUEntitiesGetNumGroups(EntitiesRef, &Entities->SourceGroupCount);

	return Entities;
}

DatasmithSketchUp::FEntities* FEntitiesObjectCollection::FindFace(int32 FaceId)
{
	if (DatasmithSketchUp::FEntities** PtrPtr = FaceIdForEntitiesMap.Find(FaceId))
	{
		return *PtrPtr;
	}
	return nullptr;
}


TSharedPtr<FComponentInstance> FComponentInstanceCollection::AddComponentInstance(SUComponentInstanceRef InComponentInstanceRef)
{
	FComponentInstanceIDType ComponentInstanceId = DatasmithSketchUpUtils::GetComponentInstanceID(InComponentInstanceRef);

	if (TSharedPtr<FComponentInstance>* Ptr = ComponentInstanceMap.Find(ComponentInstanceId))
	{
		return *Ptr;
	}

	TSharedPtr<FComponentDefinition> Definition = Context.ComponentDefinitions.GetComponentDefinition(InComponentInstanceRef);
	if (!Definition)
	{
		return TSharedPtr<FComponentInstance>();
	}

	TSharedPtr<FComponentInstance> ComponentInstance = MakeShared<FComponentInstance>(SUComponentInstanceToEntity(InComponentInstanceRef), *Definition);

	ComponentInstanceMap.Add(ComponentInstanceId, ComponentInstance);
	return ComponentInstance;
}

void FComponentInstanceCollection::AddOccurrence(FComponentInstanceIDType ComponentInstanceID, const TSharedPtr<DatasmithSketchUp::FNodeOccurence>& Occurrence)
{
	ComponentInstanceOccurencesMap.FindOrAdd(ComponentInstanceID).Add(Occurrence);
}

void FMaterialCollection::PopulateFromModel(SUModelRef InModelRef)
{
	DefaultMaterial = FMaterial::CreateDefaultMaterial(Context);

	// Get the number of material definitions in the SketchUp model.
	size_t SMaterialDefinitionCount = 0;
	SUModelGetNumMaterials(InModelRef, &SMaterialDefinitionCount); // we can ignore the returned SU_RESULT

	if (SMaterialDefinitionCount > 0)
	{
		// Retrieve the material definitions in the SketchUp model.
		TArray<SUMaterialRef> SMaterialDefinitions;
		SMaterialDefinitions.Init(SU_INVALID, SMaterialDefinitionCount);
		SUModelGetMaterials(InModelRef, SMaterialDefinitionCount, SMaterialDefinitions.GetData(), &SMaterialDefinitionCount); // we can ignore the returned SU_RESULT
		SMaterialDefinitions.SetNum(SMaterialDefinitionCount);

		// Add the material definitions to our dictionary.
		for (SUMaterialRef SMaterialDefinitionRef : SMaterialDefinitions)
		{

			TSharedPtr<FMaterial> Material = FMaterial::Create(Context, SMaterialDefinitionRef);

			MaterialDefinitionMap.Emplace(DatasmithSketchUpUtils::GetMaterialID(SMaterialDefinitionRef), Material);
		}
	}
}

FMaterialOccurrence* FMaterialCollection::RegisterInstance(FMaterialIDType MaterialID, FNodeOccurence* NodeOccurrence)
{
	if (const TSharedPtr<DatasmithSketchUp::FMaterial>* Ptr = Find(MaterialID))
	{
		const TSharedPtr<DatasmithSketchUp::FMaterial>& Material = *Ptr;
		return &Material->RegisterInstance(NodeOccurrence);
	}
	else
	{

	}
	return DefaultMaterial.Get();
}

FMaterialOccurrence* FMaterialCollection::RegisterGeometry(FMaterialIDType MaterialID, DatasmithSketchUp::FEntitiesGeometry* EntitiesGeometry)
{
	if (const TSharedPtr<DatasmithSketchUp::FMaterial>* Ptr = Find(MaterialID))
	{
		EntitiesGeometry->MaterialsUsed.Add(MaterialID);

		const TSharedPtr<DatasmithSketchUp::FMaterial>& Material = *Ptr;
		return &Material->RegisterGeometry(EntitiesGeometry);
	}
	return DefaultMaterial.Get();
}

void FMaterialCollection::UnregisterGeometry(DatasmithSketchUp::FEntitiesGeometry* EntitiesGeometry)
{
	TSet<FMaterialIDType>& MaterialsUsed = EntitiesGeometry->MaterialsUsed;

	for (FMaterialIDType MaterialID : MaterialsUsed)
	{
		if (const TSharedPtr<DatasmithSketchUp::FMaterial>* Ptr = Find(MaterialID))
		{
			const TSharedPtr<DatasmithSketchUp::FMaterial>& Material = *Ptr;
			Material->UnregisterGeometry(EntitiesGeometry);
		}
	}

	EntitiesGeometry->MaterialsUsed.Reset();
}
