// Copyright Epic Games, Inc. All Rights Reserved.

#include "VertexDeltaEditorModel.h"
#include "VertexDeltaModel.h"
#include "VertexDeltaModelVizSettings.h"
#include "VertexDeltaTrainingModel.h"
#include "VertexDeltaEditorModelActor.h"
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

#define LOCTEXT_NAMESPACE "VertexDeltaEditorModel"

namespace UE::VertexDeltaModel
{
	using namespace UE::MLDeformer;

	FVertexDeltaEditorModel::~FVertexDeltaEditorModel()
	{
		Model->OnPostEditChangeProperty().Unbind();
	}

	FMLDeformerEditorModel* FVertexDeltaEditorModel::MakeInstance()
	{
		return new FVertexDeltaEditorModel();
	}

	void FVertexDeltaEditorModel::AddReferencedObjects(FReferenceCollector& Collector)
	{
		Collector.AddReferencedObject(EditorInputInfo);
	}

	FMLDeformerSampler* FVertexDeltaEditorModel::CreateSampler() const 
	{ 
		FMLDeformerGeomCacheSampler* NewSampler = new FMLDeformerGeomCacheSampler();
		NewSampler->OnGetGeometryCache().BindLambda([this] { return GetVertexDeltaModel()->GetGeometryCache(); });
		return NewSampler;
	}

	void FVertexDeltaEditorModel::Init(const InitSettings& InitSettings)
	{
		FMLDeformerEditorModel::Init(InitSettings);
		Model->OnPostEditChangeProperty().BindRaw(this, &FVertexDeltaEditorModel::OnPostEditChangeProperty);
	}

	void FVertexDeltaEditorModel::OnPostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
	{
		const FProperty* Property = PropertyChangedEvent.Property;
		if (Property == nullptr)
		{
			return;
		}

		HandleDefaultPropertyChanges(PropertyChangedEvent);

		// When we change one of these properties below, restart animations etc.
		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UVertexDeltaModel, GeometryCache) || 
			Property->GetFName() == GET_MEMBER_NAME_CHECKED(UVertexDeltaModelVizSettings, GroundTruth))
		{
			TriggerInputAssetChanged(true);
		}
		else
		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UVertexDeltaModel, DeltaCutoffLength) || 
			Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerModel, AlignmentTransform))
		{
			if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ValueSet)
			{
				SetResamplingInputOutputsNeeded(true);
				SampleDeltas();
			}
		}
	}

	void FVertexDeltaEditorModel::OnInputAssetsChanged()
	{
		// Update the skeletal mesh components of the training, test base, and ml deformed actor.
		FMLDeformerEditorModel::OnInputAssetsChanged();

		UVertexDeltaModelVizSettings* VizSettings = GetVertexDeltaModelVizSettings();
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

		UVertexDeltaModel* VertexDeltaModel = GetVertexDeltaModel();
		VertexDeltaModel->MeshMappings.Reset();
	}

	void FVertexDeltaEditorModel::CreateTrainingGroundTruthActor(UWorld* World)
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

	void FVertexDeltaEditorModel::CreateTestGroundTruthActor(UWorld* World)
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

	void FVertexDeltaEditorModel::CreateGeomCacheActor(UWorld* World, int32 ActorID, const FName& Name, UGeometryCache* GeomCache, FLinearColor LabelColor, FLinearColor WireframeColor, const FText& LabelText, bool bIsTrainingActor)
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
		FVertexDeltaEditorModelActor* EditorActor = static_cast<FVertexDeltaEditorModelActor*>(CreateEditorActor(Settings));
		EditorActor->SetGeometryCacheComponent(GeomCacheComponent);
		EditorActors.Add(EditorActor);
	} 

	FMLDeformerEditorActor* FVertexDeltaEditorModel::CreateEditorActor(const FMLDeformerEditorActor::FConstructSettings& Settings) const
	{
		return new FVertexDeltaEditorModelActor(Settings);
	}

	FVertexDeltaEditorModelActor* FVertexDeltaEditorModel::FindVertexDeltaModelEditorActor(int32 TypeID) const
	{
		return static_cast<FVertexDeltaEditorModelActor*>(FindEditorActor(TypeID));
	}

	UVertexDeltaModelVizSettings* FVertexDeltaEditorModel::GetVertexDeltaModelVizSettings() const
	{
		return Cast<UVertexDeltaModelVizSettings>(GetVertexDeltaModel()->GetVizSettings());
	}

	double FVertexDeltaEditorModel::GetTrainingTimeAtFrame(int32 FrameNumber) const
	{
		const FVertexDeltaEditorModelActor* EditorActor = static_cast<FVertexDeltaEditorModelActor*>(FindEditorActor(ActorID_Train_GroundTruth));
		if (EditorActor && EditorActor->GetGeometryCacheComponent() && EditorActor->GetGeometryCacheComponent()->GeometryCache.Get())
		{
			return EditorActor->GetGeometryCacheComponent()->GetTimeAtFrame(FrameNumber);
		}

		return FMLDeformerEditorModel::GetTrainingTimeAtFrame(FrameNumber);
	}

	int32 FVertexDeltaEditorModel::GetTrainingFrameAtTime(double TimeInSeconds) const
	{
		const FVertexDeltaEditorModelActor* EditorActor = static_cast<FVertexDeltaEditorModelActor*>(FindEditorActor(ActorID_Train_GroundTruth));
		if (EditorActor && EditorActor->GetGeometryCacheComponent() && EditorActor->GetGeometryCacheComponent()->GeometryCache.Get())
		{
			return EditorActor->GetGeometryCacheComponent()->GetFrameAtTime(TimeInSeconds);
		}

		return FMLDeformerEditorModel::GetTrainingFrameAtTime(TimeInSeconds);
	}

	double FVertexDeltaEditorModel::GetTestTimeAtFrame(int32 FrameNumber) const
	{
		const FVertexDeltaEditorModelActor* EditorActor = static_cast<FVertexDeltaEditorModelActor*>(FindEditorActor(ActorID_Test_GroundTruth));
		if (EditorActor && EditorActor->GetGeometryCacheComponent() && EditorActor->GetGeometryCacheComponent()->GeometryCache.Get())
		{
			return EditorActor->GetGeometryCacheComponent()->GetTimeAtFrame(FrameNumber);
		}

		return FMLDeformerEditorModel::GetTestTimeAtFrame(FrameNumber);
	}

	int32 FVertexDeltaEditorModel::GetTestFrameAtTime(double TimeInSeconds) const
	{
		const FVertexDeltaEditorModelActor* EditorActor = static_cast<FVertexDeltaEditorModelActor*>(FindEditorActor(ActorID_Test_GroundTruth));
		if (EditorActor && EditorActor->GetGeometryCacheComponent() && EditorActor->GetGeometryCacheComponent()->GeometryCache.Get())
		{
			return EditorActor->GetGeometryCacheComponent()->GetFrameAtTime(TimeInSeconds);
		}

		return FMLDeformerEditorModel::GetTestFrameAtTime(TimeInSeconds);
	}

	int32 FVertexDeltaEditorModel::GetNumTrainingFrames() const
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

	void FVertexDeltaEditorModel::UpdateIsReadyForTrainingState()
	{
		bIsReadyForTraining = false;

		// Do some basic checks first, like if there is a skeletal mesh, ground truth, anim sequence, and if there are frames.
		if (!IsEditorReadyForTrainingBasicChecks())
		{
			return;
		}

		// Now make sure the assets are compatible.
		UVertexDeltaModel* VertexDeltaModel = GetVertexDeltaModel();
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

	ETrainingResult FVertexDeltaEditorModel::Train()
	{
		return TrainModel<UVertexDeltaTrainingModel>(this);
	}

	FString FVertexDeltaEditorModel::GetTrainedNetworkOnnxFile() const
	{
		return FString(FPaths::ProjectIntermediateDir() + TEXT("VertexDeltaModel/VertexDeltaModel.onnx"));
	}
}	// namespace UE::VertexDeltaModel

#undef LOCTEXT_NAMESPACE
