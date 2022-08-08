// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralMorphEditorModel.h"
#include "NeuralMorphModelModule.h"
#include "NeuralMorphModel.h"
#include "NeuralMorphModelVizSettings.h"
#include "NeuralMorphTrainingModel.h"
#include "NeuralMorphEditorModelActor.h"
#include "MLDeformerEditorToolkit.h"
#include "MLDeformerEditorStyle.h"
#include "MLDeformerInputInfo.h"
#include "MLDeformerGeomCacheSampler.h"
#include "MLDeformerGeomCacheHelpers.h"
#include "GeometryCache.h"
#include "GeometryCacheComponent.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Animation/MorphTarget.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Engine/SkeletalMesh.h"

#define LOCTEXT_NAMESPACE "NeuralMorphEditorModel"

namespace UE::NeuralMorphModel
{
	using namespace UE::MLDeformer;

	FNeuralMorphEditorModel::~FNeuralMorphEditorModel()
	{
		Model->OnPostEditChangeProperty().Unbind();
	}

	FMLDeformerEditorModel* FNeuralMorphEditorModel::MakeInstance()
	{
		return new FNeuralMorphEditorModel();
	}

	void FNeuralMorphEditorModel::AddReferencedObjects(FReferenceCollector& Collector)
	{
		Collector.AddReferencedObject(EditorInputInfo);
	}

	FMLDeformerSampler* FNeuralMorphEditorModel::CreateSampler() const 
	{ 
		FMLDeformerGeomCacheSampler* NewSampler = new FMLDeformerGeomCacheSampler();
		NewSampler->OnGetGeometryCache().BindLambda([this] { return GetNeuralMorphModel()->GetGeometryCache(); });
		return NewSampler;
	}

	void FNeuralMorphEditorModel::Init(const InitSettings& InitSettings)
	{
		FMLDeformerEditorModel::Init(InitSettings);
		Model->OnPostEditChangeProperty().BindRaw(this, &FNeuralMorphEditorModel::OnPostEditChangeProperty);
	}

	void FNeuralMorphEditorModel::OnPostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
	{
		const FProperty* Property = PropertyChangedEvent.Property;
		if (Property == nullptr)
		{
			return;
		}

		HandleDefaultPropertyChanges(PropertyChangedEvent);

		// When we change one of these properties below, restart animations etc.
		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UNeuralMorphModel, GeometryCache) || 
			Property->GetFName() == GET_MEMBER_NAME_CHECKED(UNeuralMorphModelVizSettings, GroundTruth))
		{
			TriggerInputAssetChanged(true);
		}
		else
		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UNeuralMorphModel, DeltaCutoffLength) || 
			Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerModel, AlignmentTransform))
		{
			if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ValueSet)
			{
				SetResamplingInputOutputsNeeded(true);
				SampleDeltas();
			}
		}
	}

	void FNeuralMorphEditorModel::OnInputAssetsChanged()
	{
		// Update the skeletal mesh components of the training, test base, and ml deformed actor.
		FMLDeformerEditorModel::OnInputAssetsChanged();

		UNeuralMorphModelVizSettings* VizSettings = GetNeuralMorphModelVizSettings();
		check(VizSettings);
		const float TestAnimSpeed = VizSettings->GetAnimPlaySpeed();
		UAnimSequence* TestAnimSequence = VizSettings->GetTestAnimSequence();

		// Update the training geometry cache.
		UGeometryCacheComponent* GeometryCacheComponent = FindNeuralMorphModelEditorActor(ActorID_Train_GroundTruth)->GetGeometryCacheComponent();
		check(GeometryCacheComponent);
		GeometryCacheComponent->SetGeometryCache(GetNeuralMorphModel()->GetGeometryCache());
		GeometryCacheComponent->SetLooping(false);
		GeometryCacheComponent->SetManualTick(true);
		GeometryCacheComponent->SetPlaybackSpeed(TestAnimSpeed);
		GeometryCacheComponent->Play();

		// Update the test geometry cache (ground truth) component.
		GeometryCacheComponent = FindNeuralMorphModelEditorActor(ActorID_Test_GroundTruth)->GetGeometryCacheComponent();
		GeometryCacheComponent->SetGeometryCache(VizSettings->GetTestGroundTruth());
		GeometryCacheComponent->SetLooping(true);
		GeometryCacheComponent->SetManualTick(true);
		GeometryCacheComponent->SetPlaybackSpeed(TestAnimSpeed);
		GeometryCacheComponent->Play();

		UNeuralMorphModel* NeuralMorphModel = GetNeuralMorphModel();
		NeuralMorphModel->MeshMappings.Reset();
	}

	void FNeuralMorphEditorModel::CreateTrainingGroundTruthActor(UWorld* World)
	{
		UGeometryCache* GeomCache = GetNeuralMorphModel()->GetGeometryCache();
		const FLinearColor LabelColor = FMLDeformerEditorStyle::Get().GetColor("MLDeformer.TargetMesh.LabelColor");
		const FLinearColor WireframeColor = FMLDeformerEditorStyle::Get().GetColor("MLDeformer.TargetMesh.WireframeColor");
		CreateGeomCacheActor(
			World, 
			ActorID_Train_GroundTruth, 
			"Train GroundTruth", 
			GeomCache, 
			LabelColor, 
			WireframeColor, 
			LOCTEXT("TrainGroundTruthActorLabelText", "Target Mesh"),
			true);
	}

	void FNeuralMorphEditorModel::CreateTestGroundTruthActor(UWorld* World)
	{
		UGeometryCache* GeomCache = GetNeuralMorphModelVizSettings()->GetTestGroundTruth();
		const FLinearColor LabelColor = FMLDeformerEditorStyle::Get().GetColor("MLDeformer.GroundTruth.LabelColor");
		const FLinearColor WireframeColor = FMLDeformerEditorStyle::Get().GetColor("MLDeformer.GroundTruth.WireframeColor");
		CreateGeomCacheActor(
			World, 
			ActorID_Test_GroundTruth, 
			"Test GroundTruth", 
			GeomCache, 
			LabelColor, 
			WireframeColor,
			LOCTEXT("TestGroundTruthActorLabelText", "Ground Truth"),
			false);
	}

	void FNeuralMorphEditorModel::CreateGeomCacheActor(UWorld* World, int32 ActorID, const FName& Name, UGeometryCache* GeomCache, FLinearColor LabelColor, FLinearColor WireframeColor, const FText& LabelText, bool bIsTrainingActor)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = MakeUniqueObjectName(World, AActor::StaticClass(), Name);
		AActor* Actor = World->SpawnActor<AActor>(SpawnParams);
		Actor->SetFlags(RF_Transient);

		// Create the Geometry Cache Component.
		UGeometryCacheComponent* GeomCacheComponent = NewObject<UGeometryCacheComponent>(Actor);
		GeomCacheComponent->SetGeometryCache(GeomCache);
		GeomCacheComponent->RegisterComponent();
		GeomCacheComponent->SetOverrideWireframeColor(true);
		GeomCacheComponent->SetWireframeOverrideColor(WireframeColor);
		GeomCacheComponent->MarkRenderStateDirty();
		GeomCacheComponent->SetVisibility(false);
		Actor->SetRootComponent(GeomCacheComponent);

		// Create the editor actor.
		FMLDeformerEditorActor::FConstructSettings Settings;
		Settings.Actor = Actor;
		Settings.TypeID = ActorID;
		Settings.LabelColor = LabelColor;
		Settings.LabelText = LabelText;
		Settings.bIsTrainingActor = bIsTrainingActor;
		FNeuralMorphEditorModelActor* EditorActor = static_cast<FNeuralMorphEditorModelActor*>(CreateEditorActor(Settings));
		EditorActor->SetGeometryCacheComponent(GeomCacheComponent);
		EditorActors.Add(EditorActor);
	} 

	FMLDeformerEditorActor* FNeuralMorphEditorModel::CreateEditorActor(const FMLDeformerEditorActor::FConstructSettings& Settings) const
	{
		return new FNeuralMorphEditorModelActor(Settings);
	}

	FNeuralMorphEditorModelActor* FNeuralMorphEditorModel::FindNeuralMorphModelEditorActor(int32 TypeID) const
	{
		return static_cast<FNeuralMorphEditorModelActor*>(FindEditorActor(TypeID));
	}

	UNeuralMorphModelVizSettings* FNeuralMorphEditorModel::GetNeuralMorphModelVizSettings() const
	{
		return Cast<UNeuralMorphModelVizSettings>(GetNeuralMorphModel()->GetVizSettings());
	}

	double FNeuralMorphEditorModel::GetTrainingTimeAtFrame(int32 FrameNumber) const
	{
		// Try to get the frame from the geometry cache.
		FNeuralMorphEditorModelActor* EditorActor = static_cast<FNeuralMorphEditorModelActor*>(FindEditorActor(ActorID_Train_GroundTruth));
		if (EditorActor && EditorActor->GetGeometryCacheComponent() && EditorActor->GetGeometryCacheComponent()->GeometryCache.Get())
		{
			return EditorActor->GetGeometryCacheComponent()->GetTimeAtFrame(FrameNumber);
		}

		return FMLDeformerEditorModel::GetTrainingTimeAtFrame(FrameNumber);
	}

	int32 FNeuralMorphEditorModel::GetTrainingFrameAtTime(double TimeInSeconds) const
	{
		FNeuralMorphEditorModelActor* EditorActor = static_cast<FNeuralMorphEditorModelActor*>(FindEditorActor(ActorID_Train_GroundTruth));
		if (EditorActor && EditorActor->GetGeometryCacheComponent() && EditorActor->GetGeometryCacheComponent()->GeometryCache.Get())
		{
			return EditorActor->GetGeometryCacheComponent()->GetFrameAtTime(TimeInSeconds);
		}

		return FMLDeformerEditorModel::GetTrainingFrameAtTime(TimeInSeconds);
	}

	double FNeuralMorphEditorModel::GetTestTimeAtFrame(int32 FrameNumber) const
	{
		// Try to get the frame from the geometry cache.
		FNeuralMorphEditorModelActor* EditorActor = static_cast<FNeuralMorphEditorModelActor*>(FindEditorActor(ActorID_Test_GroundTruth));
		if (EditorActor && EditorActor->GetGeometryCacheComponent() && EditorActor->GetGeometryCacheComponent()->GeometryCache.Get())
		{
			return EditorActor->GetGeometryCacheComponent()->GetTimeAtFrame(FrameNumber);
		}

		return FMLDeformerEditorModel::GetTestTimeAtFrame(FrameNumber);
	}

	int32 FNeuralMorphEditorModel::GetTestFrameAtTime(double TimeInSeconds) const
	{
		FNeuralMorphEditorModelActor* EditorActor = static_cast<FNeuralMorphEditorModelActor*>(FindEditorActor(ActorID_Test_GroundTruth));
		if (EditorActor && EditorActor->GetGeometryCacheComponent() && EditorActor->GetGeometryCacheComponent()->GeometryCache.Get())
		{
			return EditorActor->GetGeometryCacheComponent()->GetFrameAtTime(TimeInSeconds);
		}

		return FMLDeformerEditorModel::GetTestFrameAtTime(TimeInSeconds);
	}

	int32 FNeuralMorphEditorModel::GetNumTrainingFrames() const
	{
		const UGeometryCache* GeometryCache = GetNeuralMorphModel()->GetGeometryCache();
		if (GeometryCache == nullptr)
		{
			return 0;
		}
		const int32 StartFrame = GeometryCache->GetStartFrame();
		const int32 EndFrame = GeometryCache->GetEndFrame();
		return (EndFrame - StartFrame) + 1;
	}

	void FNeuralMorphEditorModel::UpdateIsReadyForTrainingState()
	{
		bIsReadyForTraining = false;

		// Do some basic checks first, like if there is a skeletal mesh, ground truth, anim sequence, and if there are frames.
		if (!IsEditorReadyForTrainingBasicChecks())
		{
			return;
		}

		// Now make sure the assets are compatible.
		UNeuralMorphModel* NeuralMorphModel = GetNeuralMorphModel();
		UGeometryCache* GeomCache = NeuralMorphModel->GetGeometryCache();
		USkeletalMesh* SkeletalMesh = NeuralMorphModel->GetSkeletalMesh();
		if (!GetGeomCacheVertexErrorText(SkeletalMesh, GeomCache, FText(), FText()).IsEmpty() ||
			!GetGeomCacheErrorText(SkeletalMesh, GeomCache).IsEmpty())
		{
			return;
		}

		// Make sure every skeletal imported mesh has some geometry track.
		const int32 NumGeomCacheTracks = GeomCache ? GeomCache->Tracks.Num() : 0;
		int32 NumSkelMeshes = 0;
		if (SkeletalMesh)
		{
			FSkeletalMeshModel* ImportedModel = SkeletalMesh->GetImportedModel();
			if (ImportedModel)
			{
				NumSkelMeshes = ImportedModel->LODModels[0].ImportedMeshInfos.Num();		
			}
		}

		// Allow the special case where there is just one mesh and track.
		if (NumGeomCacheTracks != 1 || NumSkelMeshes != 1)
		{
			if (!GetGeomCacheSampler()->GetFailedImportedMeshNames().IsEmpty())
			{
				return;
			}
		}

		bIsReadyForTraining = true;
	}

	ETrainingResult FNeuralMorphEditorModel::Train()
	{
		return TrainModel<UNeuralMorphTrainingModel>(this);
	}

	FString FNeuralMorphEditorModel::GetTrainedNetworkOnnxFile() const
	{
		return FString(FPaths::ProjectIntermediateDir() + TEXT("NeuralMorphModel/NeuralMorphModel.onnx"));
	}

	FString FNeuralMorphEditorModel::GetDefaultDeformerGraphAssetPath() const
	{
		return FString(TEXT("/Script/OptimusCore.OptimusDeformer'/Optimus/Deformers/DG_LinearBlendSkin_Morph_Cloth_RecomputeNormals.DG_LinearBlendSkin_Morph_Cloth_RecomputeNormals'"));
	}

	FString FNeuralMorphEditorModel::GetHeatMapDeformerGraphPath() const
	{
		return FString(TEXT("/MLDeformerFramework/Deformers/DG_MLDeformerModel_GPUMorph_HeatMap.DG_MLDeformerModel_GPUMorph_HeatMap"));
	}

	void FNeuralMorphEditorModel::OnPreTraining()
	{
		// Backup the morph target deltas in case we abort training.
		UNeuralMorphModel* NeuralModel = GetNeuralMorphModel();
		MorphTargetDeltasBackup = NeuralModel->MorphTargetDeltas;
	}

	void FNeuralMorphEditorModel::OnPostTraining(ETrainingResult TrainingResult, bool bUsePartiallyTrainedWhenAborted)
	{
		// We aborted and don't want to use partially trained results, we should restore the deltas that we just overwrote after training.
		if (TrainingResult == ETrainingResult::Aborted && !bUsePartiallyTrainedWhenAborted)
		{
			// Restore the blendshape backup.
			UNeuralMorphModel* NeuralModel = GetNeuralMorphModel();
			NeuralModel->MorphTargetDeltas = MorphTargetDeltasBackup;
		}
		else if (TrainingResult == ETrainingResult::Success || (TrainingResult == ETrainingResult::Aborted && bUsePartiallyTrainedWhenAborted))
		{
			UNeuralMorphModel* NeuralModel = GetNeuralMorphModel();
			if (!NeuralModel->MorphTargetDeltas.IsEmpty())
			{
				// Set deltas with a length equal or below a given threshold to zero, for better compression.
				TArray<FVector3f> MorphTargetDeltas = NeuralModel->MorphTargetDeltas;
				ZeroDeltasByThreshold(MorphTargetDeltas, NeuralModel->MorphTargetDeltaThreshold);

				// Build morph targets inside the engine, using the engine's compression scheme.
				// Add one as we included the means now as extra morph target.
				InitEngineMorphTargets(MorphTargetDeltas);
			}
		}

		// This internally calls InitGPUData() which updates the GPU buffer with the deltas.
		FMLDeformerEditorModel::OnPostTraining(TrainingResult, bUsePartiallyTrainedWhenAborted);
	}

	void FNeuralMorphEditorModel::InitEngineMorphTargets(const TArray<FVector3f>& Deltas)
	{
		const int32 LOD = 0;

		// Turn the delta buffer in a set of engine morph targets.
		UNeuralMorphModel* NeuralModel = GetNeuralMorphModel();
		TArray<UMorphTarget*> MorphTargets;	// These will be garbage collected.
		CreateEngineMorphTargets(MorphTargets, Deltas, TEXT("NeuralMorph_"), LOD, NeuralModel->MorphTargetDeltaThreshold);

		// Now compress the morph targets to GPU friendly buffers.
		check(NeuralModel->MorphTargetSet.IsValid());
		FMorphTargetVertexInfoBuffers& MorphBuffers = NeuralModel->MorphTargetSet->MorphBuffers;
		CompressEngineMorphTargets(MorphBuffers, MorphTargets, LOD, NeuralModel->MorphTargetErrorTolerance);
	}

	void FNeuralMorphEditorModel::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
	{
		FMLDeformerEditorModel::Render(View, Viewport, PDI);

		// Debug draw the selected morph target.
		UNeuralMorphModelVizSettings* VizSettings = GetNeuralMorphModelVizSettings();
		if (VizSettings->bDrawMorphTargets)
		{
			const FVector DrawOffset = -VizSettings->GetMeshSpacingOffsetVector();
			DrawMorphTarget(PDI, GetNeuralMorphModel()->MorphTargetDeltas, VizSettings->MorphTargetDeltaThreshold, VizSettings->MorphTargetNumber, DrawOffset);
		}
	}
}	// namespace UE::NeuralMorphModel

#undef LOCTEXT_NAMESPACE
