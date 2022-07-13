// Copyright Epic Games, Inc. All Rights Reserved.
#include "NeuralMorphModel.h"
#include "NeuralMorphModelVizSettings.h"
#include "NeuralMorphModelInstance.h"
#include "MLDeformerGeomCacheHelpers.h"
#include "MLDeformerModelInstance.h"
#include "MLDeformerComponent.h"
#include "UObject/Object.h"
#include "UObject/UObjectGlobals.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Components/SkeletalMeshComponent.h"

#define LOCTEXT_NAMESPACE "NeuralMorphModel"

// The morph target set ID for this model. This has to be unique for every different model.
int32 UNeuralMorphModel::NeuralMorphsExternalMorphSetID = 0;

UNeuralMorphModel::UNeuralMorphModel(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	VizSettings = ObjectInitializer.CreateEditorOnlyDefaultSubobject<UNeuralMorphModelVizSettings>(this, TEXT("VizSettings"));
#endif

	MorphTargetSet = MakeShared<FExternalMorphTargetSet>();
	MorphTargetSet->Name = FName(TEXT("NeuralBlendShapes"));
}

void UNeuralMorphModel::Serialize(FArchive& Archive)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UNeuralMorphModel::Serialize)
	Super::Serialize(Archive);

	// Check if we have initialized our compressed morph buffers.
	bool bHasMorphData = false;
	if (Archive.IsSaving())
	{
		bHasMorphData = MorphTargetSet.IsValid() ? MorphTargetSet->MorphBuffers.IsMorphCPUDataValid() : false;
	}
	Archive << bHasMorphData;

	// Load or save the compressed morph buffers, if they exist.
	if (bHasMorphData)
	{
		check(MorphTargetSet.IsValid());
		if (Archive.IsLoading())
		{
			Archive << MorphTargetSet->MorphBuffers;

			// When we're in editor mode, keep the CPU data around, so we can re-initialize when needed.
			#if WITH_EDITOR
				MorphTargetSet->MorphBuffers.SetEmptyMorphCPUDataOnInitRHI(false);
			#endif
		}
		else
		{
			Archive << MorphTargetSet->MorphBuffers;
		}
	}
}

UMLDeformerModelInstance* UNeuralMorphModel::CreateModelInstance(UMLDeformerComponent* Component)
{
	return NewObject<UNeuralMorphModelInstance>(Component);
}

#if WITH_EDITOR
	void UNeuralMorphModel::UpdateNumTargetMeshVertices()
	{
		NumTargetMeshVerts = UE::MLDeformer::ExtractNumImportedGeomCacheVertices(GeometryCache);
	}
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
	void UNeuralMorphModel::SampleGroundTruthPositions(float SampleTime, TArray<FVector3f>& OutPositions)
	{
		UNeuralMorphModelVizSettings* VertexVizSettings = Cast<UNeuralMorphModelVizSettings>(VizSettings);
		check(VertexVizSettings);

		UGeometryCache* GeomCache = VertexVizSettings->GetTestGroundTruth();
		if (GeomCache == nullptr)
		{
			OutPositions.Reset();
			return;
		}

		if (MeshMappings.IsEmpty())
		{
			TArray<FString> FailedImportedMeshnames;
			GenerateGeomCacheMeshMappings(SkeletalMesh, GeomCache, MeshMappings, FailedImportedMeshnames);
		}

		UE::MLDeformer::SampleGeomCachePositions(
			0,
			SampleTime,
			MeshMappings,
			SkeletalMesh,
			GeomCache,
			AlignmentTransform,
			OutPositions);
	}
#endif

void UNeuralMorphModel::PostMLDeformerComponentInit(UMLDeformerModelInstance* ModelInstance)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UNeuralMorphModel::PostMLDeformerComponentInit)

	Super::PostMLDeformerComponentInit(ModelInstance);

	// Register the external morph targets buffer to the render data of the skeletal mesh.
	USkeletalMeshComponent* SkelMeshComponent = ModelInstance->GetSkeletalMeshComponent();
	if (SkelMeshComponent)
	{
		FSkeletalMeshRenderData* RenderData = SkelMeshComponent->GetSkeletalMeshRenderData();
		FMorphTargetVertexInfoBuffers& MorphBuffers = MorphTargetSet->MorphBuffers;
		if (RenderData)
		{
			// Register the morph set. This overwrites the existing one for this model, if it already exists.
			// Only add to LOD 0 for now.
			const int32 LOD = 0;
			RenderData->LODRenderData[LOD].AddExternalMorphSet(UNeuralMorphModel::NeuralMorphsExternalMorphSetID, MorphTargetSet);

			// Release the render resources, but only in an editor build.
			// The non-editor build shouldn't do this, as then it can't initialize again. The non-editor build assumes
			// that the data doesn't change and we don't need to re-init.
			// In the editor build we have to re-initialize the render resources as the morph targets can change after (re)training, so
			// that is why we release them here, and intialize them again after.
			#if WITH_EDITOR
				BeginReleaseResource(&MorphBuffers);
			#endif

			// Reinitialize the GPU compressed buffers.
			if (MorphBuffers.IsMorphCPUDataValid() && MorphBuffers.GetNumMorphs() > 0)
			{
				// In a non-editor build this will clear the CPU data.
				// That also means it can't re-init the resources later on again.
				BeginInitResource(&MorphBuffers);
			}
		}

		// Update the weight information in the Skeletal Mesh.
		SkelMeshComponent->RefreshExternalMorphTargetWeights();
	}
}

void UNeuralMorphModel::SetMorphTargetDeltas(const TArray<float>& Deltas)
{
	FloatArrayToVector3Array(Deltas, MorphTargetDeltas);
}

int32 UNeuralMorphModel::GetMorphTargetDeltaStartIndex(int32 BlendShapeIndex) const
{
	if (MorphTargetDeltas.Num() == 0)
	{
		return INDEX_NONE;
	}

	return GetNumBaseMeshVerts() * BlendShapeIndex;
}

void UNeuralMorphModel::BeginDestroy()
{
	if (MorphTargetSet.IsValid())
	{
		BeginReleaseResource(&MorphTargetSet->MorphBuffers);
	}
	Super::BeginDestroy();
}

#undef LOCTEXT_NAMESPACE
