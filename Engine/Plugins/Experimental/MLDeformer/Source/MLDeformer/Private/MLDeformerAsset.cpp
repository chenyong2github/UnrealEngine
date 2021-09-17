// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerAsset.h"
#include "MLDeformer.h"
#include "MLDeformerVizSettings.h"
#include "MLDeformerInputInfo.h"
#include "GeometryCache.h"
#include "GeometryCacheMeshData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "NeuralNetwork.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"

#define LOCTEXT_NAMESPACE "MLDeformerAsset"

void FVertexMapBuffer::InitRHI()
{
	if (VertexMap.Num() > 0)
	{
		FRHIResourceCreateInfo CreateInfo(TEXT("FVertexMapBuffer"));

		VertexBufferRHI = RHICreateVertexBuffer(VertexMap.Num() * sizeof(uint32), BUF_Static | BUF_ShaderResource, CreateInfo);
		uint32* Data = reinterpret_cast<uint32*>(RHILockBuffer(VertexBufferRHI, 0, VertexMap.Num() * sizeof(uint32), RLM_WriteOnly));
		for (int32 Index = 0; Index < VertexMap.Num(); ++Index)
		{
			Data[Index] = static_cast<uint32>(VertexMap[Index]);
		}
		RHIUnlockBuffer(VertexBufferRHI);
		VertexMap.Empty();

		ShaderResourceViewRHI = RHICreateShaderResourceView(VertexBufferRHI, 4, PF_R32_UINT);
	}
	else
	{
		VertexBufferRHI = nullptr;
		ShaderResourceViewRHI = nullptr;
	}
}

UMLDeformerAsset::UMLDeformerAsset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	VizSettings = ObjectInitializer.CreateEditorOnlyDefaultSubobject<UMLDeformerVizSettings>(this, TEXT("VizSettings"));
#endif
}

void UMLDeformerAsset::Serialize(FArchive& Archive)
{
#if WITH_EDITOR
	if (Archive.IsSaving() && Archive.IsPersistent())
	{
		InitVertexMap();
		UpdateCachedNumVertices();
	}
#endif

	Super::Serialize(Archive);
}

void UMLDeformerAsset::PostLoad()
{
	Super::PostLoad();
	if (NeuralNetwork)
	{
		NeuralNetwork->SetBackEnd(ENeuralBackEnd::Auto);
		NeuralNetwork->SetDeviceType(/*DeviceType*/ENeuralDeviceType::GPU, /*InputDeviceType*/ENeuralDeviceType::CPU, /*OutputDeviceType*/ENeuralDeviceType::GPU);
	}

#if WITH_EDITOR
	UpdateCachedNumVertices();

	// Initialize the input info if we didn't store any yet.
	// This is just for backward compatibility. Normally this data is always there.
	if (InputInfo.GetNumBones() == 0 && InputInfo.GetNumCurves() == 0)
	{
		InputInfo = CreateInputInfo();
	}
	else
#endif
	{
		InputInfo.UpdateFNames();
	}

	InitGPUData();
}

#if WITH_EDITOR
FText UMLDeformerAsset::GetBaseAssetChangedErrorText() const
{
	FText Result;

	if (SkeletalMesh)
	{
		if (NumSkeletalMeshVerts != GetInputInfo().GetNumBaseMeshVertices() &&
			NumSkeletalMeshVerts > 0 && GetInputInfo().GetNumBaseMeshVertices() > 0)
		{
			Result = FText::Format(LOCTEXT("BaseMeshMismatch", "Number of vertices in base mesh has changed from {0} to {1} vertices since this ML Deformer Asset was saved! {2}"),
				GetInputInfo().GetNumBaseMeshVertices(),
				NumSkeletalMeshVerts,
				NeuralNetwork ? LOCTEXT("BaseMeshMismatchNN", "Neural network needs to be retrained!") : FText());
		}
	}

	return Result;
}

FText UMLDeformerAsset::GetTargetAssetChangedErrorText() const
{
	FText Result;

	if (GeometryCache)
	{
		if (NumGeomCacheVerts != GetInputInfo().GetNumTargetMeshVertices() &&
			NumGeomCacheVerts > 0 && GetInputInfo().GetNumTargetMeshVertices() > 0)
		{
			Result = FText::Format(LOCTEXT("TargetMeshMismatch", "Number of vertices in target mesh has changed from {0} to {1} vertices since this ML Deformer Asset was saved! {2}"),
				GetInputInfo().GetNumTargetMeshVertices(),
				NumGeomCacheVerts,
				NeuralNetwork ? LOCTEXT("BaseMeshMismatchNN", "Neural network needs to be retrained!") : FText());
		}
	}

	return Result;
}

void UMLDeformerAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	static const FName SkeletalMeshName = GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, SkeletalMesh);
	const FName PropertyName = PropertyChangedEvent.Property->GetFName();
	if (PropertyName == SkeletalMeshName)
	{
		InitVertexMap();
		InitGPUData();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

int32 UMLDeformerAsset::GetNumFrames() const
{
	check(GeometryCache != nullptr)
	const int32 StartFrame = GeometryCache->GetStartFrame();
	const int32 EndFrame = GeometryCache->GetEndFrame();
	check(EndFrame >= StartFrame);
	return (EndFrame - StartFrame) + 1;
}
#endif

void UMLDeformerAsset::BeginDestroy()
{
	Super::BeginDestroy();
	BeginReleaseResource(&VertexMapBuffer);
	RenderResourceDestroyFence.BeginFence();
}

bool UMLDeformerAsset::IsReadyForFinishDestroy()
{
	return Super::IsReadyForFinishDestroy() && RenderResourceDestroyFence.IsFenceComplete();
}

#if WITH_EDITOR
void UMLDeformerAsset::InitVertexMap()
{
	VertexMap.Empty();
	if (SkeletalMesh)
	{
		FSkeletalMeshModel* SkeletalMeshModel = SkeletalMesh->GetImportedModel();
		if (SkeletalMeshModel)
		{
			VertexMap = SkeletalMeshModel->LODModels[0].MeshToImportVertexMap;
		}
	}
}

FText UMLDeformerAsset::GetGeomCacheErrorText(UGeometryCache* InGeomCache) const
{
	FText Result;
	if (InGeomCache)
	{
		// Verify that we have imported vertex numbers enabled.
		TArray<FGeometryCacheMeshData> MeshData;
		InGeomCache->GetMeshDataAtTime(0.0f, MeshData);
		if (MeshData.Num() == 0)
		{
			Result = LOCTEXT("TargetMeshNoMeshData", "No geometry data is present.");
		}
		else
		{
			if (MeshData[0].ImportedVertexNumbers.Num() == 0)
			{
				Result = LOCTEXT("TargetMeshNoImportedVertexNumbers", "Please import Geometry Cache with option 'Store Imported Vertex Numbers' enabled!");
			}
		}
	}

	return Result;
}

FText UMLDeformerAsset::GetVertexErrorText(USkeletalMesh* InSkelMesh, UGeometryCache* InGeomCache, const FText& SkelName, const FText& GeomCacheName) const
{
	FText Result;

	if (InSkelMesh && InGeomCache)
	{
		FSkeletalMeshModel* SkeletalMeshModel = InSkelMesh->GetImportedModel();	
		TArray<FGeometryCacheMeshData> MeshData;
		InGeomCache->GetMeshDataAtTime(0.0f, MeshData);
		if (SkeletalMeshModel && MeshData.Num() > 0)
		{
			const TArray<int32>& SkeletalVertexMap = SkeletalMeshModel->LODModels[0].MeshToImportVertexMap;
			const TArray<uint32>& GeomCacheVertexMap = MeshData[0].ImportedVertexNumbers;
			if (!GeomCacheVertexMap.IsEmpty() && !SkeletalVertexMap.IsEmpty())
			{
				// Find the max vertex index.
				int32 MaxGeomCacheVertex = 0;
				for (int32 Index = 0; Index < GeomCacheVertexMap.Num(); ++Index)
				{
					MaxGeomCacheVertex = FMath::Max<int32>(static_cast<int32>(GeomCacheVertexMap[Index]), MaxGeomCacheVertex);
				}

				const int32 MaxSkelMeshVertex = SkeletalMeshModel->LODModels[0].MaxImportVertex;
				if (MaxSkelMeshVertex != MaxGeomCacheVertex)
				{
					Result = FText::Format(
						LOCTEXT("MeshVertexNumVertsMismatch", "Vertex count of {0} doesn't match with {1}!\n\n{2} has {3} verts, while {4} has {5} verts."),
						SkelName,
						GeomCacheName,
						SkelName,
						MaxSkelMeshVertex + 1,
						GeomCacheName,
						MaxGeomCacheVertex + 1);
				}
			}
		}
	}

	return Result;
}

FText UMLDeformerAsset::GetAnimSequenceErrorText(UGeometryCache* InGeomCache, UAnimSequence* InAnimSequence) const
{
	FText Result;
	if (InAnimSequence && InGeomCache)
	{
		const float AnimSeqDuration = InAnimSequence->GetPlayLength();
		const float GeomCacheDuration = InGeomCache->CalculateDuration();
		if (FMath::Abs(AnimSeqDuration - GeomCacheDuration) > 0.001f)
		{
			FNumberFormattingOptions Options;
			Options.SetUseGrouping(false);
			Options.SetMaximumFractionalDigits(4);
			Result = FText::Format(
				LOCTEXT("AnimSeqNumFramesMismatch", "Anim sequence and Geometry Cache durations don't match!\n\nAnimSeq has a duration of {0} seconds, while GeomCache has a duration of {1} seconds.\n\nThis can produce incorrect results."),
				FText::AsNumber(AnimSeqDuration, &Options),
				FText::AsNumber(GeomCacheDuration, &Options));
		}
	}
	return Result;
}

FText UMLDeformerAsset::GetInputsErrorText() const
{
	if (SkeletalMesh && CreateInputInfo().IsEmpty())
	{
		switch (TrainingInputs)			
		{
			case ETrainingInputs::BonesOnly:		return FText(LOCTEXT("InputsEmptyBonesErrorText", "Your base mesh has no bones to train on."));
			case ETrainingInputs::CurvesOnly:		return FText(LOCTEXT("InputsEmptyCurvesErrorText", "Your base mesh has no curves to train on."));
			case ETrainingInputs::BonesAndCurves:	return FText(LOCTEXT("InputsEmptyBonesCurvesErrorText", "Your base mesh has no bones or curves to train on."));
			default: return FText(LOCTEXT("InputsEmptyDefaultErrorText", "There are no inputs to train on. There are no bones, curves or other inputs we can use."));
		}
	}

	return FText();
}

FText UMLDeformerAsset::GetIncompatibleSkeletonErrorText(USkeletalMesh* InSkelMesh, UAnimSequence* InAnimSeq) const
{
	FText Result;
	if (InSkelMesh && InAnimSeq)
	{
		if (!InSkelMesh->GetSkeleton()->IsCompatible(InAnimSeq->GetSkeleton()))
		{
			Result = LOCTEXT("SkeletonMismatch", "The base skeletal mesh and anim sequence use different skeletons. The animation might not play correctly.");
		}
	}
	return Result;
}

bool UMLDeformerAsset::IsCompatibleWithNeuralNet() const
{	
	if (SkeletalMesh == nullptr)
	{
		return true;
	}

	return GetInputInfo().IsCompatible(SkeletalMesh);
}

void UMLDeformerAsset::UpdateCachedNumVertices()
{
	NumSkeletalMeshVerts = 0;
	NumGeomCacheVerts = 0;

	// Extract max num vertices from the geometry cache.
	if (GeometryCache)
	{
		TArray<FGeometryCacheMeshData> MeshData;
		GeometryCache->GetMeshDataAtTime(0.0f, MeshData);
		if (MeshData.Num() > 0)
		{
			const TArray<uint32>& VertexNumbers = MeshData[0].ImportedVertexNumbers;
			for (int32 Index = 0; Index < VertexNumbers.Num(); ++Index)
			{
				NumGeomCacheVerts = FMath::Max<int32>(static_cast<int32>(VertexNumbers[Index]), NumGeomCacheVerts);
			}

			// Add one as they are indices starting from 0.
			if (VertexNumbers.Num() > 0)
			{
				NumGeomCacheVerts += 1;
			}
		}
	}

	// Extract max num vertices from the skeletal mesh.
	if (SkeletalMesh)
	{
		FSkeletalMeshModel* SkeletalMeshModel = SkeletalMesh->GetImportedModel();
		if (SkeletalMeshModel)
		{
			const int32 MaxIndex = SkeletalMeshModel->LODModels[0].MaxImportVertex;
			NumSkeletalMeshVerts = (MaxIndex > 0) ? (MaxIndex + 1) : 0;
		}
	}
}

FMLDeformerInputInfo UMLDeformerAsset::CreateInputInfo() const
{
	FMLDeformerInputInfo Result;

	// Init the inputs.
	FMLDeformerInputInfoInitSettings Settings;
	Settings.SkeletalMesh = SkeletalMesh;
	Settings.TargetMesh = GeometryCache;
	Settings.bIncludeBones = (TrainingInputs == ETrainingInputs::BonesAndCurves || TrainingInputs == ETrainingInputs::BonesOnly);
	Settings.bIncludeCurves = (TrainingInputs == ETrainingInputs::BonesAndCurves || TrainingInputs == ETrainingInputs::CurvesOnly);
	Result.Init(Settings);

	return Result;
}
#endif

void UMLDeformerAsset::InitGPUData()
{
	BeginReleaseResource(&VertexMapBuffer);
	VertexMapBuffer.Init(VertexMap);
	BeginInitResource(&VertexMapBuffer);
}

#undef LOCTEXT_NAMESPACE
