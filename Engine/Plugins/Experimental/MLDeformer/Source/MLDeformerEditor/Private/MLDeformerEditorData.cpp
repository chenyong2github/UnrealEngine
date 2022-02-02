// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerEditorData.h"
#include "MLDeformerAsset.h"
#include "MLDeformerComponent.h"
#include "MLDeformerInstance.h"
#include "MLDeformerInputInfo.h"
#include "MLDeformerEditorToolkit.h"
#include "MLDeformer.h"
#include "MLDeformerFrameCache.h"

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
#include "Misc/ScopedSlowTask.h"
#include "ComputeFramework/ComputeGraph.h"

#include "SSimpleTimeSlider.h"
#include "Animation/MeshDeformer.h"

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
	UDebugSkelMeshComponent* SkelMeshComponent = EditorActor.Actor->FindComponentByClass<UDebugSkelMeshComponent>();
	if (SkelMeshComponent)
	{
		UMLDeformerVizSettings* VizSettings = MLDeformerAsset->GetVizSettings();
		check(VizSettings);
		UMeshDeformer* MeshDeformer = MLDeformerAsset->GetInferenceNeuralNetwork() ? VizSettings->GetDeformerGraph() : nullptr;
		
		const bool bUseHeatMapDeformer = VizSettings->GetShowHeatMap();
		SkelMeshComponent->SetMeshDeformer(bUseHeatMapDeformer ? HeatMapDeformerGraph.Get() : MeshDeformer);
	}
	
}

void FMLDeformerEditorData::InitAssets()
{
	// Force the training sequence to use Step interpolation and sample raw animation data.
	UAnimSequence* TrainingAnimSequence = MLDeformerAsset->GetAnimSequence();
	if (TrainingAnimSequence)
	{
		TrainingAnimSequence->bUseRawDataOnly = true;
		TrainingAnimSequence->Interpolation = EAnimInterpolationType::Step;
	}

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

	ClampFrameIndex();

	UMLDeformerComponent* DeformerComponent = GetEditorActor(EMLDeformerEditorActorIndex::DeformedTest).MLDeformerComponent;
	if (DeformerComponent)
	{
		DeformerComponent->GetDeformerInstance().UpdateCompatibilityStatus();
	}

	// Reinit the single frame cache.
	FMLDeformerFrameCache::FInitSettings InitSettings;
	InitSettings.CacheSizeInBytes = 0;	// This makes it just just use one frame, which is the minimum.
	InitSettings.DeformerAsset = MLDeformerAsset.Get();
	InitSettings.World = GetWorld();
	InitSettings.bLogCacheStats = false;
	InitSettings.DeltaMode = EDeltaMode::PostSkinning;
	SingleFrameCache.Init(InitSettings);

	UpdateIsReadyForTrainingState();
}

void FMLDeformerEditorData::SetAnimFrame(int32 FrameNumber)
{
	if (CurrentFrame == FrameNumber)
	{
		return;
	}

	ClampFrameIndex();

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

void FMLDeformerEditorData::UpdateIsReadyForTrainingState()
{
	UMLDeformerAsset* Asset = GetDeformerAsset();

	bIsReadyForTraining = false;

	// Make sure we have picked required assets.
	if (Asset == nullptr ||
		Asset->GetGeometryCache() == nullptr ||
		GetDeformerAsset()->GetAnimSequence() == nullptr ||
		GetDeformerAsset()->GetSkeletalMesh() == nullptr)
	{
		return;
	}

	// There are no training frames.
	if (GetDeformerAsset()->GetNumFrames() == 0)
	{
		return;
	}

	// Now make sure the assets are compatible.
	if (!Asset->GetVertexErrorText(Asset->GetSkeletalMesh(), Asset->GetGeometryCache(), FText(), FText()).IsEmpty() ||
		!Asset->GetGeomCacheErrorText(Asset->GetGeometryCache()).IsEmpty())
	{
		return;
	}

	// Make sure we have inputs.
	if (Asset->CreateInputInfo().IsEmpty())
	{
		return;
	}

	// Make sure every skeletal imported mesh has some geometry track.
	const int32 NumGeomCacheTracks = GetDeformerAsset()->GetGeometryCache() ?  GetDeformerAsset()->GetGeometryCache()->Tracks.Num() : 0;
	int32 NumSkelMeshes = 0;
	if (GetDeformerAsset()->GetSkeletalMesh())
	{
		FSkeletalMeshModel* Model = GetDeformerAsset()->GetSkeletalMesh()->GetImportedModel();
		if (Model)
		{
			NumSkelMeshes = Model->LODModels[0].ImportedMeshInfos.Num();		
		}
	}

	if (NumGeomCacheTracks != 1 || NumSkelMeshes != 1)	// Allow the special case where there is just one mesh and track.
	{
		if (!GetSingleFrameCache().GetSampler().GetFailedImportedMeshNames().IsEmpty())
		{
			return;
		}
	}

	bIsReadyForTraining = true;
}

bool FMLDeformerEditorData::IsReadyForTraining() const
{
	return bIsReadyForTraining;
}

bool FMLDeformerEditorData::GenerateDeltas(uint32 LODIndex, uint32 FrameNumber, TArray<float>& OutDeltas)
{
	OutDeltas.Reset();

	if (!IsReadyForTraining())
	{
		return false;
	}

	const FMLDeformerTrainingFrame& TrainingFrame = SingleFrameCache.GetTrainingFrameForAnimFrame(FrameNumber);
	OutDeltas = TrainingFrame.GetVertexDeltas();

	// Let's also directly extract the positions of the vertices, since we already calculated them.
	const FMLDeformerSamplerData& SamplerData = SingleFrameCache.GetSampler().GetSamplerData();
	LinearSkinnedPositions = SamplerData.GetSkinnedVertexPositions();
	DebugVectors = SamplerData.GetDebugVectors();
	DebugVectors2 = SamplerData.GetDebugVectors2();

	return true;
}

void FMLDeformerEditorData::UpdateVertexDeltaMeanAndScale(const FMLDeformerTrainingFrame& TrainingFrame, FVector3f& InOutMeanVertexDelta, FVector3f& InOutVertexDeltaScale, float& InOutCount)
{
	check(!TrainingFrame.GetVertexDeltas().IsEmpty());
	const int32 NumVertices = TrainingFrame.GetNumVertices();

	FVector MeanDelta = FVector::ZeroVector;
	FVector MinDelta(TNumericLimits<float>::Max());
	FVector MaxDelta(-TNumericLimits<float>::Max());
	const TArray<float>& VertexDeltas = TrainingFrame.GetVertexDeltas();
	for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
	{
		const int Offset = VertexIndex * 3;
		const FVector Delta(VertexDeltas[Offset], VertexDeltas[Offset+1], VertexDeltas[Offset+2]);
		MeanDelta += Delta;
		MinDelta = MinDelta.ComponentMin(Delta);
		MaxDelta = MaxDelta.ComponentMax(Delta);
	}

	const float ValidVertexCount = (float)NumVertices;
	if (NumVertices > 0)
	{
		MeanDelta /= (float)NumVertices;
	}

	// Update global mean.
	InOutCount += 1.0f;
	FVector TempDiff = MeanDelta - (FVector)InOutMeanVertexDelta;
	InOutMeanVertexDelta = InOutMeanVertexDelta + FVector3f(TempDiff / InOutCount);

	// Update global scale.
	TempDiff = MaxDelta - MinDelta;
	InOutVertexDeltaScale = InOutVertexDeltaScale.ComponentMax((FVector3f)TempDiff.GetAbs());
}

bool FMLDeformerEditorData::ComputeVertexDeltaStatistics(uint32 LODIndex, FMLDeformerFrameCache* InFrameCache)
{
	check(InFrameCache);
	if (!IsReadyForTraining() || bIsVertexDeltaNormalized)
	{
		return true;
	}

	// Calculate the number of frames to output.
	UGeometryCacheComponent* GeometryCacheComponent = GetEditorActor(EMLDeformerEditorActorIndex::Target).GeomCacheComponent;
	const uint32 AnimNumFrames = MLDeformerAsset->GetNumFramesForTraining();

	// Show some dialog with progress.
	const FText Title(LOCTEXT("PreprocessTrainingDataMessage", "Calculating data statistics"));
	FScopedSlowTask Task((float)AnimNumFrames, Title);
	Task.MakeDialog(true);

	// Initialize mean vertex delta and vertex delta scale.
	FVector3f VertexDeltaMean = FVector3f::ZeroVector;
	FVector3f VertexDeltaScale = FVector3f::OneVector;
	float Count = 0.0f;
	USkeletalMeshComponent* SkeletalMeshComponent = GetEditorActor(EMLDeformerEditorActorIndex::Base).SkelMeshComponent;
	const int32 FrameCount = MLDeformerAsset->GetNumFramesForTraining();
	for (int32 FrameIndex = 0; FrameIndex < FrameCount; FrameIndex++)
	{
		// Get the deltas updated for this frame, and then update the mean and scale.
		const FMLDeformerTrainingFrame& TrainingFrame = InFrameCache->GetTrainingFrameForAnimFrame(FrameIndex);
		FMLDeformerEditorData::UpdateVertexDeltaMeanAndScale(TrainingFrame, VertexDeltaMean, VertexDeltaScale, Count);

		// Forward the progress bar and check if we want to cancel.
		Task.EnterProgressFrame();
		if (Task.ShouldCancel())
		{
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
			return FText::FromString( DeformerComponent->GetDeformerInstance().GetCompatibilityErrorText() );
		}
	}

	return FText::GetEmpty();
}

void FMLDeformerEditorData::CreateHeatMapAssets()
{
	// Could be better to explicitly expose in UI. More flexibility and no sync load required here?

	const FString HeatMapMaterialPath = TEXT("/MLDeformer/Materials/HeatMap.HeatMap");
	UObject* MaterialObject = StaticLoadObject(UMaterial::StaticClass(), nullptr, *HeatMapMaterialPath);
	HeatMapMaterial = Cast<UMaterial>(MaterialObject);
	check(HeatMapMaterial);

	const FString HeatMapDeformerPath = TEXT("/MLDeformer/Deformers/DebugMLDeformerGraph.DebugMLDeformerGraph");
	UObject* DeformerObject = StaticLoadObject(UMeshDeformer::StaticClass(), nullptr, *HeatMapDeformerPath);
	HeatMapDeformerGraph = Cast<UMeshDeformer>(DeformerObject);
	check(HeatMapDeformerGraph);
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
	return FString(TEXT("/MLDeformer/Deformers/DefaultMLDeformerGraph.DefaultMLDeformerGraph"));
}

UMeshDeformer* FMLDeformerEditorData::LoadDefaultDeformerGraph()
{
	const FString GraphAssetPath = FMLDeformerEditorData::GetDefaultDeformerGraphAssetPath();
	UObject* Object = StaticLoadObject(UMeshDeformer::StaticClass(), nullptr, *GraphAssetPath);
	UMeshDeformer* DeformerGraph = Cast<UMeshDeformer>(Object);
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
		UMeshDeformer* DefaultGraph = LoadDefaultDeformerGraph();
		GetDeformerAsset()->GetVizSettings()->SetDeformerGraph(DefaultGraph);
	}
}

UWorld* FMLDeformerEditorData::GetWorld() const
{
	return World;
}

void FMLDeformerEditorData::SetWorld(UWorld* InWorld)
{
	World = InWorld;
}

void FMLDeformerEditorData::ClampFrameIndex()
{
	UMLDeformerVizSettings* VizSettings = GetDeformerAsset()->GetVizSettings();
	if (MLDeformerAsset->GetNumFrames() > 0)
	{
		VizSettings->FrameNumber = FMath::Min(VizSettings->FrameNumber, static_cast<uint32>(GetDeformerAsset()->GetNumFrames() - 1));
	}
	else
	{
		VizSettings->FrameNumber = 0;
	}
}

#undef LOCTEXT_NAMESPACE
