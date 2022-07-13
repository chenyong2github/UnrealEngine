// Copyright Epic Games, Inc. All Rights Reserved.

#include "LegacyVertexDeltaEditorModel.h"
#include "LegacyVertexDeltaModel.h"
#include "LegacyVertexDeltaModelVizSettings.h"
#include "LegacyVertexDeltaTrainingModel.h"
#include "LegacyVertexDeltaEditorModelActor.h"
#include "MLDeformerEditorToolkit.h"
#include "MLDeformerEditorStyle.h"
#include "MLDeformerInputInfo.h"
#include "MLDeformerGeomCacheSampler.h"
#include "MLDeformerGeomCacheHelpers.h"
#include "GeometryCache.h"
#include "GeometryCacheComponent.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Engine/SkeletalMesh.h"

#define LOCTEXT_NAMESPACE "LegacyVertexDeltaEditorModel"

namespace UE::LegacyVertexDeltaModel
{
	using namespace UE::MLDeformer;

	FLegacyVertexDeltaEditorModel::~FLegacyVertexDeltaEditorModel()
	{
		Model->OnPostEditChangeProperty().Unbind();
	}

	FMLDeformerEditorModel* FLegacyVertexDeltaEditorModel::MakeInstance()
	{
		return new FLegacyVertexDeltaEditorModel();
	}

	void FLegacyVertexDeltaEditorModel::AddReferencedObjects(FReferenceCollector& Collector)
	{
		Collector.AddReferencedObject(EditorInputInfo);
	}

	FMLDeformerSampler* FLegacyVertexDeltaEditorModel::CreateSampler() const
	{ 
		FMLDeformerGeomCacheSampler* NewSampler = new FMLDeformerGeomCacheSampler();
		NewSampler->OnGetGeometryCache().BindLambda([this] { return GetVertexDeltaModel()->GetGeometryCache(); });
		return NewSampler;
	}

	void FLegacyVertexDeltaEditorModel::Init(const InitSettings& InitSettings)
	{
		FMLDeformerEditorModel::Init(InitSettings);
		Model->OnPostEditChangeProperty().BindRaw(this, &FLegacyVertexDeltaEditorModel::OnPostEditChangeProperty);
	}

	void FLegacyVertexDeltaEditorModel::OnPostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
	{
		const FProperty* Property = PropertyChangedEvent.Property;
		if (Property == nullptr)
		{
			return;
		}

		HandleDefaultPropertyChanges(PropertyChangedEvent);

		// When we change one of these properties below, restart animations etc.
		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(ULegacyVertexDeltaModel, GeometryCache) ||
			Property->GetFName() == GET_MEMBER_NAME_CHECKED(ULegacyVertexDeltaModelVizSettings, GroundTruth))
		{
			TriggerInputAssetChanged(true);
		}
		else
		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(ULegacyVertexDeltaModel, DeltaCutoffLength) ||
			Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerModel, AlignmentTransform))
		{
			if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ValueSet)
			{
				SetResamplingInputOutputsNeeded(true);
				SampleDeltas();
			}
		}
	}

	void FLegacyVertexDeltaEditorModel::OnInputAssetsChanged()
	{
		// Update the skeletal mesh components of the training, test base, and ml deformed actor.
		FMLDeformerEditorModel::OnInputAssetsChanged();

		ULegacyVertexDeltaModelVizSettings* VizSettings = GetVertexDeltaModelVizSettings();
		check(VizSettings);
		const float TestAnimSpeed = VizSettings->GetAnimPlaySpeed();
		UAnimSequence* TestAnimSequence = VizSettings->GetTestAnimSequence();

		// Update the training geometry cache.
		UGeometryCacheComponent* GeometryCacheComponent = FindVertexDeltaModelEditorActor(ActorID_Train_GroundTruth)->GetGeometryCacheComponent();
		check(GeometryCacheComponent);
		GeometryCacheComponent->SetGeometryCache(GetVertexDeltaModel()->GetGeometryCache());
		GeometryCacheComponent->ResetAnimationTime();
		GeometryCacheComponent->SetLooping(false);
		GeometryCacheComponent->SetManualTick(true);
		GeometryCacheComponent->SetPlaybackSpeed(TestAnimSpeed);
		GeometryCacheComponent->Play();

		// Update the test geometry cache (ground truth) component.
		GeometryCacheComponent = FindVertexDeltaModelEditorActor(ActorID_Test_GroundTruth)->GetGeometryCacheComponent();
		GeometryCacheComponent->SetGeometryCache(VizSettings->GetTestGroundTruth());
		GeometryCacheComponent->ResetAnimationTime();
		GeometryCacheComponent->SetLooping(true);
		GeometryCacheComponent->SetManualTick(true);
		GeometryCacheComponent->SetPlaybackSpeed(TestAnimSpeed);
		GeometryCacheComponent->Play();

		ULegacyVertexDeltaModel* VertexDeltaModel = GetVertexDeltaModel();
		VertexDeltaModel->MeshMappings.Reset();
	}

	void FLegacyVertexDeltaEditorModel::CreateTrainingGroundTruthActor(UWorld* World)
	{
		UGeometryCache* GeomCache = GetVertexDeltaModel()->GetGeometryCache();
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

	void FLegacyVertexDeltaEditorModel::CreateTestGroundTruthActor(UWorld* World)
	{
		UGeometryCache* GeomCache = GetVertexDeltaModelVizSettings()->GetTestGroundTruth();
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

	void FLegacyVertexDeltaEditorModel::CreateGeomCacheActor(UWorld* World, int32 ActorID, const FName& Name, UGeometryCache* GeomCache, FLinearColor LabelColor, FLinearColor WireframeColor, const FText& LabelText, bool bIsTrainingActor)
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
		FLegacyVertexDeltaEditorModelActor* EditorActor = static_cast<FLegacyVertexDeltaEditorModelActor*>(CreateEditorActor(Settings));
		EditorActor->SetGeometryCacheComponent(GeomCacheComponent);
		EditorActors.Add(EditorActor);
	} 

	FMLDeformerEditorActor* FLegacyVertexDeltaEditorModel::CreateEditorActor(const FMLDeformerEditorActor::FConstructSettings& Settings) const
	{
		return new FLegacyVertexDeltaEditorModelActor(Settings);
	}

	FLegacyVertexDeltaEditorModelActor* FLegacyVertexDeltaEditorModel::FindVertexDeltaModelEditorActor(int32 TypeID) const
	{
		return static_cast<FLegacyVertexDeltaEditorModelActor*>(FindEditorActor(TypeID));
	}

	ULegacyVertexDeltaModelVizSettings* FLegacyVertexDeltaEditorModel::GetVertexDeltaModelVizSettings() const
	{
		return Cast<ULegacyVertexDeltaModelVizSettings>(GetVertexDeltaModel()->GetVizSettings());
	}

	double FLegacyVertexDeltaEditorModel::GetTrainingTimeAtFrame(int32 FrameNumber) const
	{
		const FLegacyVertexDeltaEditorModelActor* EditorActor = static_cast<FLegacyVertexDeltaEditorModelActor*>(GetTimelineEditorActor());
		if (EditorActor && EditorActor->GetGeometryCacheComponent())
		{
			return EditorActor->GetGeometryCacheComponent()->GetTimeAtFrame(FrameNumber);
		}
		return 0.0;
	}

	int32 FLegacyVertexDeltaEditorModel::GetTrainingFrameAtTime(double TimeInSeconds) const
	{
		const FLegacyVertexDeltaEditorModelActor* EditorActor = static_cast<FLegacyVertexDeltaEditorModelActor*>(GetTimelineEditorActor());
		if (EditorActor && EditorActor->GetGeometryCacheComponent())
		{
			return EditorActor->GetGeometryCacheComponent()->GetFrameAtTime(TimeInSeconds);
		}
		return 0;
	}

	int32 FLegacyVertexDeltaEditorModel::GetNumTrainingFrames() const
	{
		const UGeometryCache* GeometryCache = GetVertexDeltaModel()->GetGeometryCache();
		if (GeometryCache == nullptr)
		{
			return 0;
		}
		const int32 StartFrame = GeometryCache->GetStartFrame();
		const int32 EndFrame = GeometryCache->GetEndFrame();
		return (EndFrame - StartFrame) + 1;
	}

	void FLegacyVertexDeltaEditorModel::UpdateIsReadyForTrainingState()
	{
		bIsReadyForTraining = false;

		// Do some basic checks first, like if there is a skeletal mesh, ground truth, anim sequence, and if there are frames.
		if (!IsEditorReadyForTrainingBasicChecks())
		{
			return;
		}

		// Now make sure the assets are compatible.
		ULegacyVertexDeltaModel* VertexDeltaModel = GetVertexDeltaModel();
		UGeometryCache* GeomCache = VertexDeltaModel->GetGeometryCache();
		USkeletalMesh* SkeletalMesh = VertexDeltaModel->GetSkeletalMesh();
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

	ETrainingResult FLegacyVertexDeltaEditorModel::Train()
	{
		return TrainModel<ULegacyVertexDeltaTrainingModel>(this);
	}

	void FLegacyVertexDeltaEditorModel::OnPreTraining()
	{
		// Make a backup of the normalization values, as they get overwritten when training.
		// However, when we abort, we want to restore to the original values again.
		// See OnTrainingAborted for when we restore the backup again.
		VertexDeltaMeanBackup = GetVertexDeltaModel()->GetVertexDeltaMean();
		VertexDeltaScaleBackup = GetVertexDeltaModel()->GetVertexDeltaScale();
	}

	void FLegacyVertexDeltaEditorModel::OnPostTraining(ETrainingResult TrainingResult, bool bUsePartiallyTrainedWhenAborted)
	{
		// Restore the vertex delta mean and scale, as we aborted, and they could have changed when training
		// on a smaller subset of frames/samples. If we don't do this, the mesh will deform incorrectly.
		if (TrainingResult == ETrainingResult::Aborted && !bUsePartiallyTrainedWhenAborted)
		{
			GetVertexDeltaModel()->VertexDeltaMean = VertexDeltaMeanBackup;
			GetVertexDeltaModel()->VertexDeltaScale = VertexDeltaScaleBackup;
		}
	}

	FString FLegacyVertexDeltaEditorModel::GetTrainedNetworkOnnxFile() const
	{
		return FString(FPaths::ProjectIntermediateDir() + TEXT("LegacyVertexDeltaModel/latest_net_G.onnx"));
	}

	FString FLegacyVertexDeltaEditorModel::GetDefaultDeformerGraphAssetPath() const
	{
		return FString(TEXT("/LegacyVertexDeltaModel/Deformers/DG_LegacyVertexDeltaModel.DG_LegacyVertexDeltaModel"));
	}

	FString FLegacyVertexDeltaEditorModel::GetHeatMapDeformerGraphPath() const
	{
		return FString(TEXT("/LegacyVertexDeltaModel/Deformers/DG_LegacyVertexDeltaModel_HeatMap.DG_LegacyVertexDeltaModel_HeatMap"));
	}
}	// namespace UE::LegacyVertexDeltaModel

#undef LOCTEXT_NAMESPACE
