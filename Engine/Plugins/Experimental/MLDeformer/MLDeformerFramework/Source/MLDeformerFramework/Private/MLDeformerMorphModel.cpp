// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerMorphModel.h"
#include "MLDeformerMorphModelInstance.h"
#include "MLDeformerModelInstance.h"
#include "MLDeformerComponent.h"
#include "UObject/Object.h"
#include "UObject/UObjectGlobals.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Rendering/MorphTargetVertexInfoBuffers.h"

#define LOCTEXT_NAMESPACE "MLDeformerMorphModel"

UMLDeformerMorphModel::UMLDeformerMorphModel(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	MorphTargetSet = MakeShared<FExternalMorphSet>();
	MorphTargetSet->Name = GetClass()->GetFName();
}

void UMLDeformerMorphModel::Serialize(FArchive& Archive)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UMLDeformerMorphModel::Serialize)

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
		Archive << MorphTargetSet->MorphBuffers;
	}
}

UMLDeformerModelInstance* UMLDeformerMorphModel::CreateModelInstance(UMLDeformerComponent* Component)
{
	return NewObject<UMLDeformerMorphModelInstance>(Component);
}

void UMLDeformerMorphModel::PostMLDeformerComponentInit(UMLDeformerModelInstance* ModelInstance)
{
	if (ModelInstance->HasPostInitialized())
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(UMLDeformerMorphModel::PostMLDeformerComponentInit)

	Super::PostMLDeformerComponentInit(ModelInstance);

	// Register the external morph targets buffer to the skinned mesh component.
	USkeletalMeshComponent* SkelMeshComponent = ModelInstance->GetSkeletalMeshComponent();
	if (SkelMeshComponent && SkelMeshComponent->GetSkeletalMeshAsset())
	{	
		// If this triggers, please set the ExternalMorphSetID in your model's constructor to a value that represents your model type.
		checkf(ExternalMorphSetID != -1, TEXT("Please configure your ExternalMorphSetID member to a unique ID."));

		// Register the morph set. This overwrites the existing one for this model, if it already exists.
		// Only add to LOD 0 for now.
		const int32 LOD = 0;
		SkelMeshComponent->AddExternalMorphSet(LOD, ExternalMorphSetID, MorphTargetSet);

		// When we're in editor mode, keep the CPU data around, so we can re-initialize when needed.
#if WITH_EDITOR
		MorphTargetSet->MorphBuffers.SetEmptyMorphCPUDataOnInitRHI(false);
#else
		MorphTargetSet->MorphBuffers.SetEmptyMorphCPUDataOnInitRHI(true);
#endif

		// Release the render resources, but only in an editor build.
		// The non-editor build shouldn't do this, as then it can't initialize again. The non-editor build assumes
		// that the data doesn't change and we don't need to re-init.
		// In the editor build we have to re-initialize the render resources as the morph targets can change after (re)training, so
		// that is why we release them here, and intialize them again after.
		FMorphTargetVertexInfoBuffers& MorphBuffers = MorphTargetSet->MorphBuffers;
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

		// Update the weight information in the Skeletal Mesh.
		SkelMeshComponent->RefreshExternalMorphTargetWeights();

		ModelInstance->SetHasPostInitialized(true);
	}
}

void UMLDeformerMorphModel::SetMorphTargetDeltas(const TArray<float>& Deltas)
{
	FloatArrayToVector3Array(Deltas, MorphTargetDeltas);
}

int32 UMLDeformerMorphModel::GetMorphTargetDeltaStartIndex(int32 BlendShapeIndex) const
{
	if (MorphTargetDeltas.Num() == 0)
	{
		return INDEX_NONE;
	}

	return GetNumBaseMeshVerts() * BlendShapeIndex;
}

void UMLDeformerMorphModel::BeginDestroy()
{
	if (MorphTargetSet.IsValid())
	{
		// Release and flush, waiting for the release to have completed, 
		// If we don't do this we can get an error that we destroy a render resource that is still initialized,
		// as the release happens in another thread.
		ReleaseResourceAndFlush(&MorphTargetSet->MorphBuffers);
		MorphTargetSet.Reset();
	}
	Super::BeginDestroy();
}

#undef LOCTEXT_NAMESPACE
