// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchDatabaseViewModel.h"
#include "PoseSearchEditor.h"
#include "PoseSearch/PoseSearch.h"
#include "Modules/ModuleManager.h"
#include "PoseSearchDatabasePreviewScene.h"
#include "AnimPreviewInstance.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Animation/MirrorDataTable.h"
#include "EngineUtils.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"

FPoseSearchDatabaseViewModel::FPoseSearchDatabaseViewModel()
	: PoseSearchDatabase(nullptr)
{
}

FPoseSearchDatabaseViewModel::~FPoseSearchDatabaseViewModel()
{
}

void FPoseSearchDatabaseViewModel::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(PoseSearchDatabase);
}

void FPoseSearchDatabaseViewModel::Initialize(
	UPoseSearchDatabase* InPoseSearchDatabase,
	const TSharedRef<FPoseSearchDatabasePreviewScene>& InPreviewScene)
{
	PoseSearchDatabase = InPoseSearchDatabase;
	PreviewScenePtr = InPreviewScene;

	RemovePreviewActors();

	PoseSearchDatabase->RegisterOnDerivedDataRebuild(
		UPoseSearchDatabase::FOnDerivedDataRebuild::CreateLambda([this]()
	{
		ResetPreviewActors();
	}));
}

void FPoseSearchDatabaseViewModel::ResetPreviewActors()
{
	PlayTime = 0.0f;
	RespawnPreviewActors();
}

void FPoseSearchDatabaseViewModel::RespawnPreviewActors()
{
	RemovePreviewActors();
	const FPoseSearchIndex* SearchIndex = PoseSearchDatabase->GetSearchIndex();
	for (const FPoseSearchIndexAsset& IndexAsset : SearchIndex->Assets)
	{
		if (AnimationPreviewMode == EAnimationPreviewMode::OriginalAndMirrored || !IndexAsset.bMirrored)
		{
			FPoseSearchDatabasePreviewActor PreviewActor = SpawnPreviewActor(IndexAsset);
			if (PreviewActor.IsValid())
			{
				PreviewActors.Add(PreviewActor);
			}
		}
	}
	UpdatePreviewActors();

	// todo: do blendspaces afterwards
}

void FPoseSearchDatabaseViewModel::BuildSearchIndex()
{
	PoseSearchDatabase->BeginCacheDerivedData();
}

FPoseSearchDatabasePreviewActor FPoseSearchDatabaseViewModel::SpawnPreviewActor(const FPoseSearchIndexAsset& IndexAsset)
{
	USkeleton* Skeleton = PoseSearchDatabase->Schema->Skeleton;

	FPoseSearchDatabaseSequence DatabaseSequence = PoseSearchDatabase->Sequences[IndexAsset.SourceAssetIdx];

	FPoseSearchDatabasePreviewActor PreviewActor;

	if (!Skeleton || !DatabaseSequence.Sequence)
	{
		return PreviewActor;
	}

	PreviewActor.IndexAsset = &IndexAsset;
	PreviewActor.CurrentPoseIndex = INDEX_NONE;

	// todo: use preview when possible
	UClass* PreviewClass = AActor::StaticClass();
	const FTransform SpawnTransform = FTransform::Identity;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	PreviewActor.Actor = GetWorld()->SpawnActor<AActor>(PreviewClass, SpawnTransform, Params);
	PreviewActor.Actor->SetFlags(RF_Transient);

	PreviewActor.Mesh = NewObject<UDebugSkelMeshComponent>(PreviewActor.Actor.Get());
	PreviewActor.Mesh->RegisterComponentWithWorld(GetWorld());

	PreviewActor.AnimInstance = NewObject<UAnimPreviewInstance>(PreviewActor.Mesh.Get());
	PreviewActor.Mesh->PreviewInstance = PreviewActor.AnimInstance.Get();
	PreviewActor.AnimInstance->InitializeAnimation();

	PreviewActor.Mesh->SetSkeletalMesh(Skeleton->GetPreviewMesh(true));
	PreviewActor.Mesh->EnablePreview(true, DatabaseSequence.Sequence);
	PreviewActor.AnimInstance->SetAnimationAsset(DatabaseSequence.Sequence);
	if (IndexAsset.bMirrored && PoseSearchDatabase->Schema)
	{
		PreviewActor.AnimInstance->SetMirrorDataTable(PoseSearchDatabase->Schema->MirrorDataTable);
	}
	
	PreviewActor.AnimInstance->PlayAnim(false);

	if (!PreviewActor.Actor->GetRootComponent())
	{
		PreviewActor.Actor->SetRootComponent(PreviewActor.Mesh.Get());
	}

	UE_LOG(
		LogPoseSearchEditor, Log, TEXT("Spawned preview Actor: %s at Loc: %s Rot: %s"),
		*GetNameSafe(PreviewActor.Actor.Get()),
		*SpawnTransform.GetLocation().ToString(), 
		*SpawnTransform.Rotator().ToString());

	return PreviewActor;
}

UWorld* FPoseSearchDatabaseViewModel::GetWorld() const
{
	check(PreviewScenePtr.IsValid());
	return PreviewScenePtr.Pin()->GetWorld();
}

UObject* FPoseSearchDatabaseViewModel::GetPlaybackContext() const
{
	return GetWorld();
}


void FPoseSearchDatabaseViewModel::OnPreviewActorClassChanged()
{
	// todo: implement
}

void FPoseSearchDatabaseViewModel::Tick(float DeltaSeconds)
{
	PlayTime += DeltaSeconds;
	UpdatePreviewActors();
}

void FPoseSearchDatabaseViewModel::UpdatePreviewActors()
{
	TSharedPtr<FPoseSearchDatabasePreviewScene> PreviewScene = PreviewScenePtr.Pin();
	for (FPoseSearchDatabasePreviewActor& PreviewActor : GetPreviewActors())
	{
		if (const UAnimSequence* Sequence = Cast<UAnimSequence>(PreviewActor.AnimInstance->GetAnimationAsset()))
		{
			float CurrentTime = 0.0f;
			FAnimationRuntime::AdvanceTime(false, PlayTime, CurrentTime, Sequence->GetPlayLength());

			FTransform RootMotion = Sequence->ExtractRootMotionFromRange(0.0f, CurrentTime);
			if (PreviewActor.AnimInstance->GetMirrorDataTable())
			{
				RootMotion = MirrorRootMotion(RootMotion, PreviewActor.AnimInstance->GetMirrorDataTable());
			}
			PreviewActor.Actor->SetActorTransform(RootMotion);
			PreviewActor.AnimInstance->SetPosition(CurrentTime);

			PreviewActor.CurrentPoseIndex = PoseSearchDatabase->GetPoseIndexFromTime(CurrentTime, PreviewActor.IndexAsset);
		}
	}
}

void FPoseSearchDatabaseViewModel::RemovePreviewActors()
{
	for (auto PreviewActor : PreviewActors)
	{
		PreviewActor.Actor->Destroy();
	}
	PreviewActors.Reset();
}

FTransform FPoseSearchDatabaseViewModel::MirrorRootMotion(
	FTransform RootMotion, 
	const UMirrorDataTable* MirrorDataTable)
{
	const FTransform RootReferenceTransform = 
		PoseSearchDatabase->Schema->Skeleton->GetReferenceSkeleton().GetRefBonePose()[0];
	const FQuat RootReferenceRotation = RootReferenceTransform.GetRotation();

	const EAxis::Type MirrorAxis = MirrorDataTable->MirrorAxis;
	FVector T = RootMotion.GetTranslation();
	T = FAnimationRuntime::MirrorVector(T, MirrorAxis);
	FQuat Q = RootMotion.GetRotation();
	Q = FAnimationRuntime::MirrorQuat(Q, MirrorAxis);
	Q *= FAnimationRuntime::MirrorQuat(RootReferenceRotation, MirrorAxis).Inverse() * RootReferenceRotation;

	FTransform MirroredRootMotion = FTransform(Q, T, RootMotion.GetScale3D());
	return MirroredRootMotion;
}

void FPoseSearchDatabaseViewModel::OnSetPoseFeaturesDrawMode(EPoseSearchFeaturesDrawMode DrawMode)
{
	PoseFeaturesDrawMode = DrawMode;
}

bool FPoseSearchDatabaseViewModel::IsPoseFeaturesDrawMode(EPoseSearchFeaturesDrawMode DrawMode) const
{
	return PoseFeaturesDrawMode == DrawMode;
}

void FPoseSearchDatabaseViewModel::OnSetAnimationPreviewMode(EAnimationPreviewMode PreviewMode)
{
	if (PreviewMode != AnimationPreviewMode)
	{
		AnimationPreviewMode = PreviewMode;
		RespawnPreviewActors();
	}
}

bool FPoseSearchDatabaseViewModel::IsAnimationPreviewMode(EAnimationPreviewMode PreviewMode) const
{
	return AnimationPreviewMode == PreviewMode;
}
