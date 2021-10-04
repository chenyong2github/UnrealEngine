// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerEditorData.h"
#include "MLDeformerAsset.h"
#include "MLDeformerComponent.h"
#include "MLDeformerInstance.h"
#include "MLDeformerInputInfo.h"
#include "MLDeformerEditorToolkit.h"
#include "MLDeformer.h"

#include "IPersonaToolkit.h"
#include "IDetailsView.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/Skeleton.h"
#include "Animation/SmartName.h"

#include "Rendering/SkeletalMeshModel.h"
#include "Materials/Material.h"

#include "AnimPreviewInstance.h"

#include "GeometryCache.h"
#include "GeometryCacheComponent.h"
#include "GeometryCacheMeshData.h"

#include "Math/NumericLimits.h"
#include "Misc/SlowTask.h"
#include "ComputeFramework/ComputeGraphComponent.h"
#include "ComputeFramework/ComputeGraph.h"

#include "SSimpleTimeSlider.h"

#define LOCTEXT_NAMESPACE "MLDeformerEditorData"

FMLDeformerEditorData::FMLDeformerEditorData()
{
	Actors.AddDefaulted(5);
}

int32 FMLDeformerEditorData::GetNumEditorActors() const
{
	return Actors.Num();
}

void FMLDeformerEditorData::SetEditorActor(EMLDeformerEditorActorIndex Index, const FMLDeformerEditorActor& Actor)
{
	Actors[static_cast<int32>(Index)] = Actor;
}

const FMLDeformerEditorActor& FMLDeformerEditorData::GetEditorActor(EMLDeformerEditorActorIndex Index) const
{
	return Actors[static_cast<int32>(Index)];
}

FMLDeformerEditorActor& FMLDeformerEditorData::GetEditorActor(EMLDeformerEditorActorIndex Index)
{
	return Actors[static_cast<int32>(Index)];
}

void FMLDeformerEditorData::SetPersonaToolkit(TSharedPtr<IPersonaToolkit> InPersonaToolkit)
{
	PersonaToolkit = InPersonaToolkit;
}

void FMLDeformerEditorData::SetDeformerAsset(UMLDeformerAsset* InAsset)
{
	MLDeformerAsset = InAsset;
}

void FMLDeformerEditorData::SetAnimInstance(UAnimInstance* InAnimInstance)
{
	AnimInstance = InAnimInstance;
}

void FMLDeformerEditorData::SetDetailsView(TSharedPtr<IDetailsView> InDetailsView)
{
	DetailsView = InDetailsView;
}

void FMLDeformerEditorData::SetVizSettingsDetailsView(TSharedPtr<IDetailsView> InDetailsView)
{
	VizSettingsDetailsView = InDetailsView;
}

void FMLDeformerEditorData::SetEditorToolkit(FMLDeformerEditorToolkit* InToolkit)
{
	EditorToolkit = InToolkit;
}

IPersonaToolkit* FMLDeformerEditorData::GetPersonaToolkit() const
{ 
	return PersonaToolkit.Get(); 
}

TSharedPtr<IPersonaToolkit> FMLDeformerEditorData::GetPersonaToolkitPointer() const
{ 
	return PersonaToolkit;
}

FMLDeformerEditorToolkit* FMLDeformerEditorData::GetEditorToolkit() const
{
	return EditorToolkit;
}

UMLDeformerAsset* FMLDeformerEditorData::GetDeformerAsset() const
{ 
	check(MLDeformerAsset.Get());
	return MLDeformerAsset.Get();
}

TWeakObjectPtr<UMLDeformerAsset> FMLDeformerEditorData::GetDeformerAssetPointer() const
{
	return MLDeformerAsset;
}

UAnimInstance* FMLDeformerEditorData::GetAnimInstance() const
{ 
	return AnimInstance; 
}

IDetailsView* FMLDeformerEditorData::GetDetailsView() const
{ 
	return DetailsView.Get();
}

IDetailsView* FMLDeformerEditorData::GetVizSettingsDetailsView() const
{
	return VizSettingsDetailsView.Get();
}

bool FMLDeformerEditorData::IsTestActor(EMLDeformerEditorActorIndex Index) const
{
	return (Index == EMLDeformerEditorActorIndex::Test || Index == EMLDeformerEditorActorIndex::DeformedTest || Index == EMLDeformerEditorActorIndex::GroundTruth);
}

bool FMLDeformerEditorData::IsTrainingActor(EMLDeformerEditorActorIndex Index) const
{
	return (Index == EMLDeformerEditorActorIndex::Base || Index == EMLDeformerEditorActorIndex::Target);
}

float FMLDeformerEditorData::GetDuration() const
{
	USkeletalMeshComponent* SkeletalMeshComponent = GetEditorActor(EMLDeformerEditorActorIndex::Base).SkelMeshComponent;
	UGeometryCacheComponent* GeometryCacheComponent = GetEditorActor(EMLDeformerEditorActorIndex::Target).GeomCacheComponent;

	if (SkeletalMeshComponent && GeometryCacheComponent)
	{
		const float GeomCacheDuration = GeometryCacheComponent->GetDuration();
		const float AnimSequenceDuration = MLDeformerAsset->GetAnimSequence()->GetPlayLength();
		if (FMath::IsNearlyEqual(GeomCacheDuration, AnimSequenceDuration, 0.001f))
		{
			return GeomCacheDuration;
		}
		else
		{
			// TODO: Show some visual error, of mismatching durations.
			//UE_LOG(LogMLDeformer, Warning, TEXT("%f, %f"), GeomCacheDuration, AnimSequenceDuration);
		}
	}

	return 0.0f;
}

float FMLDeformerEditorData::GetTimeAtFrame(int32 FrameNumber) const
{
	UGeometryCacheComponent* GeometryCacheComponent = GetEditorActor(EMLDeformerEditorActorIndex::Target).GeomCacheComponent;
	if (GeometryCacheComponent)
	{
		const int32 ClampedFrameNumber = FMath::Clamp(FrameNumber, 0, GeometryCacheComponent->GetNumberOfFrames() - 1);
		return GeometryCacheComponent->GetTimeAtFrame(ClampedFrameNumber);
	}

	return 0.0f;
}

float FMLDeformerEditorData::GetSnappedFrameTime(float InTime) const
{
	UGeometryCacheComponent* GeometryCacheComponent = GetEditorActor(EMLDeformerEditorActorIndex::Target).GeomCacheComponent;
	if (GeometryCacheComponent)
	{
		const int32 FrameIndex = GeometryCacheComponent->GetFrameAtTime(InTime);
		return GeometryCacheComponent->GetTimeAtFrame(FrameIndex);
	}

	return InTime;
}

int32 FMLDeformerEditorData::GetNumImportedVertices() const
{
	UGeometryCacheComponent* GeomCacheComponent = GetEditorActor(EMLDeformerEditorActorIndex::Target).GeomCacheComponent;
	check(GeomCacheComponent);

	UGeometryCache* GeomCache = GeomCacheComponent->GetGeometryCache();
	if (GeomCache == nullptr)
	{
		return 0;
	}

	TArray<FGeometryCacheMeshData> MeshData;
	GeomCache->GetMeshDataAtTime(GeomCacheComponent->GetAnimationTime(), MeshData);

	if (MeshData.Num() > 0)
	{
		const TArray<uint32>& ImportedVertexNumbers = MeshData[0].ImportedVertexNumbers;
		if (ImportedVertexNumbers.Num() > 0)
		{
			// Find the maximum value.
			int32 MaxIndex = -1;
			for (int32 i = 0; i < ImportedVertexNumbers.Num(); ++i)
			{
				MaxIndex = FMath::Max(static_cast<int32>(ImportedVertexNumbers[i]), MaxIndex);
			}
			return (MaxIndex + 1);
		}
		return 0;
	}
	return 0;
}

void FMLDeformerEditorData::UpdateTestAnimPlaySpeed()
{
	UMLDeformerVizSettings* VizSettings = GetDeformerAsset()->GetVizSettings();
	check(VizSettings);

	const float Speed = VizSettings->GetAnimPlaySpeed();

	USkeletalMeshComponent* SkeletalMeshComponent = GetEditorActor(EMLDeformerEditorActorIndex::Test).SkelMeshComponent;
	SkeletalMeshComponent->SetPlayRate(Speed);

	SkeletalMeshComponent = GetEditorActor(EMLDeformerEditorActorIndex::DeformedTest).SkelMeshComponent;
	SkeletalMeshComponent->SetPlayRate(Speed);

	UGeometryCacheComponent* GeomCacheComponent = GetEditorActor(EMLDeformerEditorActorIndex::GroundTruth).GeomCacheComponent;
	GeomCacheComponent->SetPlaybackSpeed(Speed);
}

void FMLDeformerEditorData::UpdateDeformerGraph()
{	
	const FMLDeformerEditorActor& EditorActor = GetEditorActor(EMLDeformerEditorActorIndex::DeformedTest);
	UComputeGraphComponent* ComputeComponent = EditorActor.Actor->FindComponentByClass<UComputeGraphComponent>();
	if (ComputeComponent)
	{
		UMLDeformerVizSettings* VizSettings = MLDeformerAsset->GetVizSettings();
		check(VizSettings);
		ComputeComponent->ComputeGraph = MLDeformerAsset->GetInferenceNeuralNetwork() ? VizSettings->GetDeformerGraph() : nullptr;
		ComputeComponent->CreateDataProviders(true);
	}
}

void FMLDeformerEditorData::InitAssets()
{
	// Force the training sequence to use Step interpolation and sample raw animation data.
	UAnimSequence* TrainingAnimSequence = MLDeformerAsset->GetAnimSequence();
	TrainingAnimSequence->bUseRawDataOnly = true;

	USkeletalMeshComponent* SkeletalMeshComponent = GetEditorActor(EMLDeformerEditorActorIndex::Base).SkelMeshComponent;
	SkeletalMeshComponent->SetSkeletalMesh(MLDeformerAsset->GetSkeletalMesh());
	if (PersonaToolkit)
	{
		PersonaToolkit->GetPreviewScene()->SetPreviewMesh(MLDeformerAsset->GetSkeletalMesh());
	}
	SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	SkeletalMeshComponent->SetAnimation(TrainingAnimSequence);
	SkeletalMeshComponent->SetPosition(0.0f);
	SkeletalMeshComponent->SetPlayRate(0.0f);
	SkeletalMeshComponent->Play(false);

	UGeometryCacheComponent* GeometryCacheComponent = GetEditorActor(EMLDeformerEditorActorIndex::Target).GeomCacheComponent;
	GeometryCacheComponent->SetGeometryCache(MLDeformerAsset->GetGeometryCache());
	GeometryCacheComponent->ResetAnimationTime();
	GeometryCacheComponent->SetLooping(false);
	GeometryCacheComponent->SetManualTick(true);
	GeometryCacheComponent->Play();

	UMLDeformerVizSettings* VizSettings = MLDeformerAsset->GetVizSettings();
	check(VizSettings);
	const float TestAnimSpeed = VizSettings->GetAnimPlaySpeed();

	SkeletalMeshComponent = GetEditorActor(EMLDeformerEditorActorIndex::Test).SkelMeshComponent;
	SkeletalMeshComponent->SetSkeletalMesh(MLDeformerAsset->GetSkeletalMesh());
	SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	SkeletalMeshComponent->SetAnimation(VizSettings->GetTestAnimSequence());
	SkeletalMeshComponent->SetPosition(0.0f);
	SkeletalMeshComponent->SetPlayRate(TestAnimSpeed);
	SkeletalMeshComponent->Play(true);

	SkeletalMeshComponent = GetEditorActor(EMLDeformerEditorActorIndex::DeformedTest).SkelMeshComponent;
	SkeletalMeshComponent->SetSkeletalMesh(MLDeformerAsset->GetSkeletalMesh());
	SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	SkeletalMeshComponent->SetAnimation(VizSettings->GetTestAnimSequence());
	SkeletalMeshComponent->SetPosition(0.0f);
	SkeletalMeshComponent->SetPlayRate(TestAnimSpeed);
	SkeletalMeshComponent->Play(true);

	GeometryCacheComponent = GetEditorActor(EMLDeformerEditorActorIndex::GroundTruth).GeomCacheComponent;
	GeometryCacheComponent->SetGeometryCache(VizSettings->GetGroundTruth());
	GeometryCacheComponent->ResetAnimationTime();
	GeometryCacheComponent->SetLooping(true);
	GeometryCacheComponent->SetManualTick(SkeletalMeshComponent->bPauseAnims);
	GeometryCacheComponent->SetPlaybackSpeed(TestAnimSpeed);
	GeometryCacheComponent->Play();

	bIsVertexDeltaNormalized = false;
	CurrentFrame = -1;

	UpdateTimeSlider();
	MLDeformerAsset->UpdateCachedNumVertices();
	UpdateDeformerGraph();
}

void FMLDeformerEditorData::ExtractBoneTransforms(TArray<FMatrix44f>& OutBoneMatrices, TArray<FTransform>& OutBoneTransforms) const
{
	USkeletalMeshComponent* SkelComp = GetEditorActor(EMLDeformerEditorActorIndex::Base).SkelMeshComponent;
	check(SkelComp);

	SkelComp->CacheRefToLocalMatrices(OutBoneMatrices);
	OutBoneTransforms = SkelComp->GetBoneSpaceTransforms();
}

void FMLDeformerEditorData::ExtractSkinnedPositions(int32 LODIndex, TArray<FMatrix44f>& InBoneMatrices, TArray<FVector3f>& TempPositions, TArray<FVector3f>& OutPositions, bool bImportedVertices) const
{
	OutPositions.Reset();
	TempPositions.Reset();

	UDebugSkelMeshComponent* SkelMeshComponent = GetEditorActor(EMLDeformerEditorActorIndex::Base).SkelMeshComponent;
	if (SkelMeshComponent == nullptr)
	{
		return;
	}

	USkeletalMesh* Mesh = SkelMeshComponent->SkeletalMesh;
	if (Mesh == nullptr)
	{
		return;
	}

	FSkeletalMeshLODRenderData& SkelMeshLODData = Mesh->GetResourceForRendering()->LODRenderData[LODIndex];
	FSkinWeightVertexBuffer* SkinWeightBuffer = SkelMeshComponent->GetSkinWeightBuffer(LODIndex);
	USkeletalMeshComponent::ComputeSkinnedPositions(SkelMeshComponent, TempPositions, InBoneMatrices, SkelMeshLODData, *SkinWeightBuffer);

	if (bImportedVertices)
	{
		// Get the originally imported vertex numbers from the DCC.
		const FSkeletalMeshModel* SkeletalMeshModel = Mesh->GetImportedModel();
		const TArray<int32>& ImportedVertexNumbers = SkeletalMeshModel->LODModels[/*LODIndex=*/0].MeshToImportVertexMap;
		if (ImportedVertexNumbers.Num() > 0)
		{
			const int32 MaxIndex = SkeletalMeshModel->LODModels[/*LODIndex=*/0].MaxImportVertex;

			// Store the vertex positions for the original imported vertices (8 vertices for a cube).
			OutPositions.AddZeroed(MaxIndex + 1);
			for (int32 Index = 0; Index < TempPositions.Num(); ++Index)
			{
				const int32 ImportedVertex = ImportedVertexNumbers[Index];
				OutPositions[ImportedVertex] = TempPositions[Index];
			}
		}
	}
	else
	{
		OutPositions = TempPositions;
	}
}

void FMLDeformerEditorData::ExtractGeomCachePositions(int32 LODIndex, TArray<FVector3f>& TempPositions, TArray<FVector3f>& OutPositions) const
{
	OutPositions.Reset();
	TempPositions.Reset();

	UGeometryCacheComponent* GeomCacheComponent = GetEditorActor(EMLDeformerEditorActorIndex::Target).GeomCacheComponent;
	UGeometryCache* GeomCache = GeomCacheComponent->GetGeometryCache();
	if (GeomCache == nullptr)
	{
		return;
	}

	const float Time = GeomCacheComponent->GetAnimationTime();
	TArray<FGeometryCacheMeshData> MeshData;
	GeomCache->GetMeshDataAtTime(Time, MeshData);

	if (MeshData.Num() > 0)
	{
		const TArray<uint32>& ImportedVertexNumbers = MeshData[0].ImportedVertexNumbers;
		if (ImportedVertexNumbers.Num() > 0)
		{
			TempPositions = MeshData[0].Positions;

			// Find the maximum value.
			int32 MaxIndex = -1;
			for (int32 Index = 0; Index < ImportedVertexNumbers.Num(); ++Index)
			{
				MaxIndex = FMath::Max(static_cast<int32>(ImportedVertexNumbers[Index]), MaxIndex);
			}
			check(MaxIndex > -1);

			OutPositions.AddZeroed(MaxIndex + 1);

			// Transform the positions with the alignment transform.
			const FTransform& Transform = GetDeformerAsset()->AlignmentTransform;
			for (int32 Index = 0; Index < TempPositions.Num(); ++Index)
			{
				const int32 ImportedVertex = static_cast<int32>(ImportedVertexNumbers[Index]);
				const FVector Pos = TempPositions[Index];
				OutPositions[ImportedVertex] = Transform.TransformPosition(Pos);
			}
		}
	}
}

void FMLDeformerEditorData::SetAnimFrame(int32 FrameNumber)
{
	if (CurrentFrame == FrameNumber)
	{
		return;
	}

	// Calculate the time in the animation data to visualize.
	const float TimeOffset = GetTimeAtFrame(FrameNumber);

	USkeletalMeshComponent* SkeletalMeshComponent = GetEditorActor(EMLDeformerEditorActorIndex::Base).SkelMeshComponent;
	if (SkeletalMeshComponent)
	{
		SkeletalMeshComponent->SetPosition(TimeOffset);
		SkeletalMeshComponent->bPauseAnims = true;
		SkeletalMeshComponent->RefreshBoneTransforms();
	}

	UGeometryCacheComponent* GeometryCacheComponent = GetEditorActor(EMLDeformerEditorActorIndex::Target).GeomCacheComponent;
	if (GeometryCacheComponent && GeometryCacheComponent->GetGeometryCache())
	{
		GeometryCacheComponent->SetManualTick(true);
		GeometryCacheComponent->TickAtThisTime(TimeOffset, false, false, false);
	}

	CurrentFrame = FrameNumber;
}

void FMLDeformerEditorData::UpdateDebugPointData()
{
	if (GetDeformerAsset()->GetVizSettings()->GetVisualizationMode() != EMLDeformerVizMode::TrainingData)
	{
		return;
	}

	const FMLDeformerEditorActor& BaseEditorActor = GetEditorActor(EMLDeformerEditorActorIndex::Base);
	if (BaseEditorActor.SkelMeshComponent == nullptr || BaseEditorActor.SkelMeshComponent->SkeletalMesh == nullptr)
	{
		return;
	}

	// Extract the bone transforms and skinned vertex positions.
	ExtractBoneTransforms(CurrentBoneMatrices, CurrentBoneTransforms);
	ExtractSkinnedPositions(/*LODIndex=*/0, CurrentBoneMatrices, TempVectorBuffer, LinearSkinnedPositions);

	const FMLDeformerEditorActor& TargetEditorActor = GetEditorActor(EMLDeformerEditorActorIndex::Target);
	if (TargetEditorActor.GeomCacheComponent == nullptr || TargetEditorActor.GeomCacheComponent->GetGeometryCache() == nullptr)
	{
		return;
	}

	// Extract Geometry Cache positions.
	ExtractGeomCachePositions(/*LODIndex=*/0, TempVectorBuffer, GeomCachePositions);
}

bool FMLDeformerEditorData::IsReadyForTraining() const
{
	UMLDeformerAsset* Asset = GetDeformerAsset();

	// Make sure we have picked required assets.
	if (Asset == nullptr ||
		Asset->GetGeometryCache() == nullptr ||
		GetDeformerAsset()->GetAnimSequence() == nullptr ||
		GetDeformerAsset()->GetSkeletalMesh() == nullptr)
	{
		return false;
	}

	// Now make sure the assets are compatible.
	if (!Asset->GetVertexErrorText(Asset->GetSkeletalMesh(), Asset->GetGeometryCache(), FText(), FText()).IsEmpty() ||
		!Asset->GetGeomCacheErrorText(Asset->GetGeometryCache()).IsEmpty())
	{
		return false;
	}

	// Make sure we have inputs.
	if (Asset->CreateInputInfo().IsEmpty())
	{
		return false;
	}

	return true;
}

bool FMLDeformerEditorData::GenerateTrainingData(uint32 LODIndex, uint32 FrameNumber, float DeltaCutoffLength, TArray<float>& OutDeltas, TArray<FTransform>& OutBoneTransforms, TArray<float>& OutCurveValues)
{
	OutDeltas.Reset();
	OutBoneTransforms.Reset();
	OutCurveValues.Reset();

	if (!IsReadyForTraining())
	{
		return false;
	}

	// Calculate the number of frames to output.
	UGeometryCacheComponent* GeometryCacheComponent = GetEditorActor(EMLDeformerEditorActorIndex::Target).GeomCacheComponent;
	USkeletalMeshComponent* SkelMeshComponent = GetEditorActor(EMLDeformerEditorActorIndex::Base).SkelMeshComponent;
	const uint32 AnimNumFrames = static_cast<uint32>(GeometryCacheComponent->GetNumberOfFrames());
	const uint32 FrameIndex = FMath::Min(AnimNumFrames, FrameNumber);

	// Calculate the vertex positions (and required transforms in order to perform skinning).
	SetAnimFrame(static_cast<int32>(FrameIndex));
	ExtractBoneTransforms(CurrentBoneMatrices, OutBoneTransforms);	// We just use our OutBoneTransforms array here, it will later be overwritten again.
	ExtractSkinnedPositions(LODIndex, CurrentBoneMatrices, TempVectorBuffer, LinearSkinnedPositions);
	ExtractGeomCachePositions(LODIndex, TempVectorBuffer, GeomCachePositions);

	// Grab the actual required bone transforms
	ExtractInputBoneTransforms(OutBoneTransforms);	// This internally resizes the output bone transforms array, so we overwrite existing contents from before.
	ExtractInputCurveValues(OutCurveValues);

	// Calculate the deltas.
	if (LinearSkinnedPositions.Num() == GeomCachePositions.Num())
	{
		const int32 NumVertices = LinearSkinnedPositions.Num();
		OutDeltas.AddUninitialized(3 * NumVertices);

		for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
		{
			const FVector Delta = GeomCachePositions[VertexIndex] - LinearSkinnedPositions[VertexIndex];
			const int32 ArrayIndex = 3 * VertexIndex;
			if (Delta.Length() < DeltaCutoffLength)
			{
				OutDeltas[ArrayIndex] = Delta.X;
				OutDeltas[ArrayIndex + 1] = Delta.Y;
				OutDeltas[ArrayIndex + 2] = Delta.Z;
			}
			else
			{
				OutDeltas[ArrayIndex] = 0.0f;
				OutDeltas[ArrayIndex + 1] = 0.0f;
				OutDeltas[ArrayIndex + 2] = 0.0f;
			}
		}
	}

	return true;
}

bool FMLDeformerEditorData::GenerateDeltas(uint32 LODIndex, uint32 FrameNumber, float DeltaCutoffLength, TArray<float>& OutDeltas)
{
	return GenerateTrainingData(LODIndex, FrameNumber, DeltaCutoffLength, OutDeltas, CurrentBoneTransforms, CurrentCurveWeights);
}

void FMLDeformerEditorData::UpdateVertexDeltaMeanAndScale(
	const TArray<FVector3f>& InGeomCachePositions,
	const TArray<FVector3f>& InLinearSkinnedPositions,
	float DeltaCutoffLength,
	FVector3f& InOutMeanVertexDelta,
	FVector3f& InOutVertexDeltaScale,
	float& InOutCount) const
{
	const int32 NumVertices = LinearSkinnedPositions.Num();
	check(InGeomCachePositions.Num() == InLinearSkinnedPositions.Num());

	FVector MeanDelta = FVector::ZeroVector;
	FVector MinDelta(TNumericLimits<float>::Max());
	FVector MaxDelta(-TNumericLimits<float>::Max());
	float ValidVertexCount = 0.0f;
	for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
	{
		const FVector Delta = GeomCachePositions[VertexIndex] - LinearSkinnedPositions[VertexIndex];
		if (Delta.Length() < DeltaCutoffLength)
		{
			MeanDelta += Delta;
			ValidVertexCount += 1.0f;

			// Get minimum dimensions.
			MinDelta = MinDelta.ComponentMin(Delta);
			// Get maximum dimensions.
			MaxDelta = MaxDelta.ComponentMax(Delta);
		}
	}

	if (ValidVertexCount > 0.0f)
	{
		MeanDelta /= ValidVertexCount;
	}

	// Update global mean.
	InOutCount += 1.0f;
	FVector TempDiff = MeanDelta - InOutMeanVertexDelta;
	InOutMeanVertexDelta = InOutMeanVertexDelta + (TempDiff / InOutCount);

	// Update global scale.
	TempDiff = MaxDelta - MinDelta;
	InOutVertexDeltaScale = InOutVertexDeltaScale.ComponentMax(TempDiff.GetAbs());
}

bool FMLDeformerEditorData::ComputeVertexDeltaStatistics(uint32 LODIndex, float DeltaCutoffLength)
{
	if (!IsReadyForTraining() || bIsVertexDeltaNormalized)
	{
		return true;
	}

	// Calculate the number of frames to output.
	UGeometryCacheComponent* GeometryCacheComponent = GetEditorActor(EMLDeformerEditorActorIndex::Target).GeomCacheComponent;
	const uint32 AnimNumFrames = static_cast<uint32>(GeometryCacheComponent->GetNumberOfFrames());

	// Show some dialog with progress.
	FSlowTask Task((float)AnimNumFrames, FText::FromString("Calculating data statistics"));
	Task.Initialize();
	Task.MakeDialog(true);

	// Initialize mean vertex delta and vertex delta scale.
	FVector3f VertexDeltaMean = FVector3f::ZeroVector;
	FVector3f VertexDeltaScale = FVector3f::OneVector;
	float Count = 0.0f;
	TArray<FTransform> BoneTransforms;
	for (uint32 FrameIndex = 0; FrameIndex < AnimNumFrames; FrameIndex++)
	{
		// Set the components to the right time values of the given frame.
		SetAnimFrame(static_cast<int32>(FrameIndex));

		// Extract the bone transforms.
		ExtractBoneTransforms(CurrentBoneMatrices, BoneTransforms);

		// Extract the linear skinned positions.
		ExtractSkinnedPositions(LODIndex, CurrentBoneMatrices, TempVectorBuffer, LinearSkinnedPositions);

		// Extract the geometry cache positions.
		ExtractGeomCachePositions(LODIndex, TempVectorBuffer, GeomCachePositions);

		// Update mean vertex delta.
		UpdateVertexDeltaMeanAndScale(GeomCachePositions, LinearSkinnedPositions, DeltaCutoffLength,
			VertexDeltaMean, VertexDeltaScale, Count);

		// Forward the progress bar and check if we want to cancel.
		Task.EnterProgressFrame();
		if (Task.ShouldCancel())
		{
			Task.Destroy();
			return false;
		}
	}

	// Update the asset with calculated statistics.
	MLDeformerAsset->VertexDeltaMean = VertexDeltaMean;
	if (Count > 0.0f)
	{
		MLDeformerAsset->VertexDeltaScale = FVector3f::OneVector * VertexDeltaScale.GetMax();
		bIsVertexDeltaNormalized = true;
	}

	return true;
}

FText FMLDeformerEditorData::GetOverlayText()
{
	if (GetDeformerAsset() == nullptr)
	{
		return FText::GetEmpty();
	}

	// Check compatibility.
	const UMLDeformerComponent* DeformerComponent = GetEditorActor(EMLDeformerEditorActorIndex::DeformedTest).MLDeformerComponent;
	if (DeformerComponent)
	{
		const FMLDeformerInstance& DeformerInstance = DeformerComponent->GetDeformerInstance();
		if (DeformerInstance.GetSkeletalMeshComponent() && 
			DeformerInstance.GetSkeletalMeshComponent()->SkeletalMesh &&
			!DeformerComponent->GetDeformerInstance().IsCompatible() )
		{
			return FText::FromString(TEXT("Trained deformer asset is not compatible with skeletal mesh!"));
		}
	}

	return FText::GetEmpty();
}

void FMLDeformerEditorData::CreateMaterials()
{
	// Try load the heat map material.
	const FString HeatMapPath = TEXT("/MLDeformer/Materials/HeatMap.HeatMap");
	UObject* Object = StaticLoadObject(UMaterial::StaticClass(), nullptr, *HeatMapPath);
	HeatMapMaterial = Cast<UMaterial>(Object);
	check(HeatMapMaterial);
}

void FMLDeformerEditorData::SetHeatMapMaterialEnabled(bool bEnabled)
{
	USkeletalMeshComponent* Component = GetEditorActor(EMLDeformerEditorActorIndex::DeformedTest).SkelMeshComponent;
	check(Component);

	if (bEnabled)
	{
		for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
		{
			Component->SetMaterial(Index, HeatMapMaterial);
		}
	}
	else
	{
		Component->EmptyOverrideMaterials();
	}
}

void FMLDeformerEditorData::ExtractInputCurveValues(TArray<float>& OutValues) const
{
	USkeletalMeshComponent* SkelMeshComponent = GetEditorActor(EMLDeformerEditorActorIndex::Base).SkelMeshComponent;
	check(SkelMeshComponent);
	const FMLDeformerInputInfo& InputInfo = GetDeformerAsset()->GetInputInfo();
	InputInfo.ExtractCurveValues(SkelMeshComponent->GetAnimInstance(), OutValues);
}

void FMLDeformerEditorData::ExtractInputBoneTransforms(TArray<FTransform>& OutTransforms) const
{
	USkeletalMeshComponent* SkelMeshComponent = GetEditorActor(EMLDeformerEditorActorIndex::Base).SkelMeshComponent;
	check(SkelMeshComponent);

	// Get a copy of the bone transforms.
	const TArray<FTransform> BoneTransforms = SkelMeshComponent->GetBoneSpaceTransforms();

	const FMLDeformerInputInfo& InputInfo = GetDeformerAsset()->GetInputInfo();
	const int32 NumBones = InputInfo.GetNumBones();
	OutTransforms.Reset(NumBones);

	if (NumBones == BoneTransforms.Num())
	{
		OutTransforms.AddUninitialized(NumBones);

		for (int32 Index = 0; Index < NumBones; ++Index)
		{
			const FName BoneName = InputInfo.GetBoneName(Index);
			const int32 SkelMeshBoneIndex = SkelMeshComponent->GetBoneIndex(BoneName);
			OutTransforms[Index] = BoneTransforms[SkelMeshBoneIndex];
		}
	}
}

bool FMLDeformerEditorData::IsActorVisible(EMLDeformerEditorActorIndex ActorIndex) const
{
	const FMLDeformerEditorActor& EditorActor = GetEditorActor(ActorIndex);
	if (EditorActor.SkelMeshComponent)
	{
		return EditorActor.SkelMeshComponent->IsVisible();
	}

	if (EditorActor.GeomCacheComponent)
	{
		return EditorActor.GeomCacheComponent->IsVisible();
	}

	return true;
}

void FMLDeformerEditorData::SetActorVisibility(EMLDeformerEditorActorIndex ActorIndex, bool bIsVisible)
{
	UDebugSkelMeshComponent* Component = GetEditorActor(ActorIndex).SkelMeshComponent;
	if (Component && bIsVisible != Component->IsVisible())
	{
		Component->SetVisibility(bIsVisible, true);
		UpdateDeformerGraph();
	}

	UGeometryCacheComponent* GeomComponent = GetEditorActor(ActorIndex).GeomCacheComponent;
	if (GeomComponent && bIsVisible != GeomComponent->IsVisible())
	{
		GeomComponent->SetVisibility(bIsVisible, true);
		UpdateDeformerGraph();
	}
}

void FMLDeformerEditorData::SetTimeSlider(TSharedPtr<SSimpleTimeSlider> InTimeSlider)
{
	TimeSlider = InTimeSlider;
}

SSimpleTimeSlider* FMLDeformerEditorData::GetTimeSlider() const
{
	return TimeSlider.Get();
}

void FMLDeformerEditorData::UpdateTimeSlider()
{
	const UMLDeformerAsset* DeformerAsset = GetDeformerAsset();
	const UMLDeformerVizSettings* VizSettings = DeformerAsset->GetVizSettings();
	if (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TrainingData)
	{
		const UAnimSequence* AnimSeq = DeformerAsset->GetAnimSequence();
		const double Duration = AnimSeq ? AnimSeq->GetPlayLength() : 0.0;
		SetTimeSliderRange(0.0, Duration);
	}
	else
	if (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TestData)
	{
		const UAnimSequence* AnimSeq = DeformerAsset->GetVizSettings()->GetTestAnimSequence();
		const double Duration = AnimSeq ? AnimSeq->GetPlayLength() : 0.0;
		SetTimeSliderRange(0.0, Duration);
	}
}

void FMLDeformerEditorData::SetTimeSliderRange(double StartTime, double EndTime)
{
	SSimpleTimeSlider* Slider = GetTimeSlider();
	if (Slider)
	{
		Slider->SetTimeRange(StartTime, EndTime);
		Slider->SetClampRange(StartTime, EndTime);
	}
}

void FMLDeformerEditorData::OnTimeSliderScrubPositionChanged(double NewScrubTime, bool bIsScrubbing)
{
	UMLDeformerAsset* DeformerAsset = GetDeformerAsset();
	UMLDeformerVizSettings* VizSettings = DeformerAsset->GetVizSettings();
	if (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TrainingData)
	{
		UGeometryCacheComponent* GeomCacheComponent = GetEditorActor(EMLDeformerEditorActorIndex::Target).GeomCacheComponent;
		if (GeomCacheComponent)
		{
			const int32 FrameNumber = GeomCacheComponent->GetFrameAtTime(static_cast<float>(NewScrubTime));
			VizSettings->FrameNumber = FrameNumber;
		}
	}
	else
	if (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TestData)
	{
		float PlayOffset = NewScrubTime;

		// If we have a ground truth model, let's actually align to the exact geom cache frame to prevent interpolation.
		UGeometryCacheComponent* GroundTruthGeomCacheComponent = GetEditorActor(EMLDeformerEditorActorIndex::GroundTruth).GeomCacheComponent;
		if (GroundTruthGeomCacheComponent->GetGeometryCache())
		{
			const int32 FrameNumber = GroundTruthGeomCacheComponent->GetFrameAtTime(static_cast<float>(NewScrubTime));
			PlayOffset = GroundTruthGeomCacheComponent->GetTimeAtFrame(FrameNumber);
		}

		UDebugSkelMeshComponent* SkelMeshComponent = GetEditorActor(EMLDeformerEditorActorIndex::Test).SkelMeshComponent;
		SkelMeshComponent->SetPosition(PlayOffset);
		SkelMeshComponent->bPauseAnims = true;
		SkelMeshComponent->RefreshBoneTransforms();

		UDebugSkelMeshComponent* DefSkelMeshComponent = GetEditorActor(EMLDeformerEditorActorIndex::DeformedTest).SkelMeshComponent;
		DefSkelMeshComponent->SetPosition(PlayOffset);
		DefSkelMeshComponent->bPauseAnims = true;
		DefSkelMeshComponent->RefreshBoneTransforms();

		GroundTruthGeomCacheComponent->SetManualTick(true);
		GroundTruthGeomCacheComponent->TickAtThisTime(PlayOffset, false, false, false);
	}	
}

void FMLDeformerEditorData::OnPlayButtonPressed()
{
	UMLDeformerAsset* DeformerAsset = GetDeformerAsset();
	UMLDeformerVizSettings* VizSettings = DeformerAsset->GetVizSettings();
	if (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TestData)
	{
		UDebugSkelMeshComponent* SkelMeshComponent = GetEditorActor(EMLDeformerEditorActorIndex::Test).SkelMeshComponent;
		SkelMeshComponent->bPauseAnims = !SkelMeshComponent->bPauseAnims;
		SkelMeshComponent->RefreshBoneTransforms();

		UDebugSkelMeshComponent* DefSkelMeshComponent = GetEditorActor(EMLDeformerEditorActorIndex::DeformedTest).SkelMeshComponent;
		DefSkelMeshComponent->bPauseAnims = !DefSkelMeshComponent->bPauseAnims;
		DefSkelMeshComponent->RefreshBoneTransforms();

		UGeometryCacheComponent* GroundTruthGeometryCacheComponent = GetEditorActor(EMLDeformerEditorActorIndex::GroundTruth).GeomCacheComponent;
		if (DefSkelMeshComponent->bPauseAnims)
		{
			GroundTruthGeometryCacheComponent->SetManualTick(true);
		}
		else
		{
			GroundTruthGeometryCacheComponent->SetManualTick(false);
		}
	}
}

double FMLDeformerEditorData::CalcTimelinePosition() const
{
	UMLDeformerAsset* DeformerAsset = GetDeformerAsset();
	UMLDeformerVizSettings* VizSettings = DeformerAsset->GetVizSettings();
	UDebugSkelMeshComponent* SkelMeshComponent = nullptr;
	if (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TestData)
	{
		SkelMeshComponent = GetEditorActor(EMLDeformerEditorActorIndex::Test).SkelMeshComponent;
	}
	else if (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TrainingData)
	{
		SkelMeshComponent = GetEditorActor(EMLDeformerEditorActorIndex::Base).SkelMeshComponent;
	}

	check(SkelMeshComponent);
	return SkelMeshComponent->GetPosition();
}

bool FMLDeformerEditorData::IsPlayingAnim() const
{
	UMLDeformerAsset* DeformerAsset = GetDeformerAsset();
	UMLDeformerVizSettings* VizSettings = DeformerAsset->GetVizSettings();
	if (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TestData)
	{
		return !GetEditorActor(EMLDeformerEditorActorIndex::Test).SkelMeshComponent->bPauseAnims;
	}
	return false;
}

FString FMLDeformerEditorData::GetDefaultDeformerGraphAssetPath()
{
	return FString(TEXT("/MLDeformer/Deformers/DefaultMLDeformerGraph.MLDeformerGraph"));
}

UComputeGraph* FMLDeformerEditorData::LoadDefaultDeformerGraph()
{
	const FString GraphAssetPath = FMLDeformerEditorData::GetDefaultDeformerGraphAssetPath();
	UObject* Object = StaticLoadObject(UComputeGraph::StaticClass(), nullptr, *GraphAssetPath);
	UComputeGraph* DeformerGraph = Cast<UComputeGraph>(Object);
	if (DeformerGraph == nullptr)
	{
		UE_LOG(LogMLDeformer, Warning, TEXT("Failed to load default ML Deformer compute graph from: %s"), *GraphAssetPath);
	}
	else
	{
		UE_LOG(LogMLDeformer, Verbose, TEXT("Loaded default ML Deformer compute graph from: %s"), *GraphAssetPath);
	}

	return DeformerGraph;
}


void FMLDeformerEditorData::SetDefaultDeformerGraphIfNeeded()
{
	// Initialize the asset on the default plugin deformer graph.
	if (GetDeformerAsset()->GetVizSettings()->GetDeformerGraph() == nullptr)
	{
		UComputeGraph* DefaultGraph = FMLDeformerEditorData::LoadDefaultDeformerGraph();
		GetDeformerAsset()->GetVizSettings()->SetDeformerGraph(DefaultGraph);
	}
}

#undef LOCTEXT_NAMESPACE
