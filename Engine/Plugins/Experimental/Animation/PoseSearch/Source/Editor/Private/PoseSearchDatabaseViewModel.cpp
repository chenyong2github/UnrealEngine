// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchDatabaseViewModel.h"
#include "PoseSearchDatabasePreviewScene.h"
#include "SPoseSearchDatabaseAssetList.h"
#include "PoseSearchEditor.h"
#include "PoseSearch/PoseSearch.h"
#include "Modules/ModuleManager.h"
#include "AnimPreviewInstance.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Animation/MirrorDataTable.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "EngineUtils.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"


namespace UE::PoseSearch
{
	constexpr float StepDeltaTime = 1.0f / 30.0f;

	FDatabaseViewModel::FDatabaseViewModel()
		: PoseSearchDatabase(nullptr)
	{
	}

	FDatabaseViewModel::~FDatabaseViewModel()
	{
		PoseSearchDatabase->UnregisterOnDerivedDataRebuild(this);
	}

	void FDatabaseViewModel::AddReferencedObjects(FReferenceCollector& Collector)
	{
		Collector.AddReferencedObject(PoseSearchDatabase);
	}

	void FDatabaseViewModel::Initialize(
		UPoseSearchDatabase* InPoseSearchDatabase,
		const TSharedRef<FDatabasePreviewScene>& InPreviewScene)
	{
		PoseSearchDatabase = InPoseSearchDatabase;
		PreviewScenePtr = InPreviewScene;

		RemovePreviewActors();

		PoseSearchDatabase->RegisterOnDerivedDataRebuild(
			UPoseSearchDatabase::FOnDerivedDataRebuild::CreateSP(
				this,
				&FDatabaseViewModel::ResetPreviewActors));
	}

	void FDatabaseViewModel::ResetPreviewActors()
	{
		PlayTime = 0.0f;
		DeltaTimeMultiplier = 1.0f;
		RespawnPreviewActors();
	}

	void FDatabaseViewModel::RespawnPreviewActors()
	{
		RemovePreviewActors();

		if (AnimationPreviewMode == EAnimationPreviewMode::None)
		{
			return;
		}

		if (SelectedNodes.Num() > 0)
		{
			TSet<int32> AssociatedAssetIndices;
			for (const TSharedPtr<FDatabaseAssetTreeNode>& SelectedNode : SelectedNodes)
			{
				if (SelectedNode->SourceAssetType == ESearchIndexAssetType::Sequence)
				{
					AssociatedAssetIndices.Add(SelectedNode->SourceAssetIdx);
				}
			}
			
			MaxPreviewPlayLength = 0.0f;

			const FPoseSearchIndex* SearchIndex = PoseSearchDatabase->GetSearchIndex();
			if (SearchIndex)
			{
				for (const FPoseSearchIndexAsset& IndexAsset : SearchIndex->Assets)
				{
					const bool bIsAssociatedToSelection =
						IndexAsset.Type == ESearchIndexAssetType::Sequence &&
						AssociatedAssetIndices.Contains(IndexAsset.SourceAssetIdx);

					const bool bSpawn =
						bIsAssociatedToSelection &&
						(AnimationPreviewMode == EAnimationPreviewMode::OriginalAndMirrored ||
						 !IndexAsset.bMirrored);

					if (bSpawn)
					{
						FDatabasePreviewActor PreviewActor = SpawnPreviewActor(IndexAsset);
						if (PreviewActor.IsValid())
						{
							const UAnimSequence* Sequence =
								Cast<UAnimSequence>(PreviewActor.AnimInstance->GetAnimationAsset());
							if (Sequence)
							{
								MaxPreviewPlayLength = FMath::Max(MaxPreviewPlayLength, Sequence->GetPlayLength());
							}

							PreviewActors.Add(PreviewActor);
						}
					}
				}

				UpdatePreviewActors();
			}
		}

		// todo: do blendspaces afterwards
	}

	void FDatabaseViewModel::BuildSearchIndex()
	{
		PoseSearchDatabase->BeginCacheDerivedData();
	}

	void FDatabaseViewModel::PreviewBackwardEnd()
	{
		PlayTime = 0.0f;
	}

	void FDatabaseViewModel::PreviewBackwardStep()
	{
		PlayTime = FMath::Clamp(PlayTime - StepDeltaTime, 0.0f, MaxPreviewPlayLength);
		DeltaTimeMultiplier = 0.0f;
	}

	void FDatabaseViewModel::PreviewBackward()
	{
		DeltaTimeMultiplier = -1.0f;
	}

	void FDatabaseViewModel::PreviewPause()
	{
		DeltaTimeMultiplier = 0.0f;
	}

	void FDatabaseViewModel::PreviewForward()
	{
		DeltaTimeMultiplier = 1.0f;
	}

	void FDatabaseViewModel::PreviewForwardStep()
	{
		PlayTime = FMath::Clamp(PlayTime + StepDeltaTime, 0.0f, MaxPreviewPlayLength);
		DeltaTimeMultiplier = 0.0f;
	}

	void FDatabaseViewModel::PreviewForwardEnd()
	{
		PlayTime = MaxPreviewPlayLength;
	}

	FDatabasePreviewActor FDatabaseViewModel::SpawnPreviewActor(const FPoseSearchIndexAsset& IndexAsset)
	{
		USkeleton* Skeleton = PoseSearchDatabase->Schema->Skeleton;

		FPoseSearchDatabaseSequence DatabaseSequence = PoseSearchDatabase->Sequences[IndexAsset.SourceAssetIdx];

		FDatabasePreviewActor PreviewActor;

		if (!Skeleton || !DatabaseSequence.Sequence)
		{
			return PreviewActor;
		}

		PreviewActor.IndexAsset = IndexAsset;
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
		PreviewActor.AnimInstance->SetAnimationAsset(DatabaseSequence.Sequence, false, 0.0f);
		if (IndexAsset.bMirrored && PoseSearchDatabase->Schema)
		{
			PreviewActor.AnimInstance->SetMirrorDataTable(PoseSearchDatabase->Schema->MirrorDataTable);
		}

		PreviewActor.AnimInstance->PlayAnim(false, 0.0f);

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

	UWorld* FDatabaseViewModel::GetWorld() const
	{
		check(PreviewScenePtr.IsValid());
		return PreviewScenePtr.Pin()->GetWorld();
	}

	UObject* FDatabaseViewModel::GetPlaybackContext() const
	{
		return GetWorld();
	}


	void FDatabaseViewModel::OnPreviewActorClassChanged()
	{
		// todo: implement
	}

	void FDatabaseViewModel::Tick(float DeltaSeconds)
	{
		PlayTime += DeltaSeconds * DeltaTimeMultiplier;
		PlayTime = FMath::Clamp(PlayTime, 0.0f, MaxPreviewPlayLength);
		UpdatePreviewActors();
	}

	void FDatabaseViewModel::UpdatePreviewActors()
	{
		TSharedPtr<FDatabasePreviewScene> PreviewScene = PreviewScenePtr.Pin();
		for (FDatabasePreviewActor& PreviewActor : GetPreviewActors())
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

				PreviewActor.CurrentPoseIndex = PoseSearchDatabase->GetPoseIndexFromTime(CurrentTime, &PreviewActor.IndexAsset);
			}
		}
	}

	void FDatabaseViewModel::RemovePreviewActors()
	{
		for (auto PreviewActor : PreviewActors)
		{
			PreviewActor.Actor->Destroy();
		}
		PreviewActors.Reset();

		MaxPreviewPlayLength = 0.0f;
	}

	FTransform FDatabaseViewModel::MirrorRootMotion(
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

	void FDatabaseViewModel::OnSetPoseFeaturesDrawMode(EFeaturesDrawMode DrawMode)
	{
		PoseFeaturesDrawMode = DrawMode;
	}

	bool FDatabaseViewModel::IsPoseFeaturesDrawMode(EFeaturesDrawMode DrawMode) const
	{
		return PoseFeaturesDrawMode == DrawMode;
	}

	void FDatabaseViewModel::OnSetAnimationPreviewMode(EAnimationPreviewMode PreviewMode)
	{
		if (PreviewMode != AnimationPreviewMode)
		{
			AnimationPreviewMode = PreviewMode;
			RespawnPreviewActors();
		}
	}

	bool FDatabaseViewModel::IsAnimationPreviewMode(EAnimationPreviewMode PreviewMode) const
	{
		return AnimationPreviewMode == PreviewMode;
	}

	void FDatabaseViewModel::AddSequenceToDatabase(UAnimSequence* AnimSequence, int InitialGroupIdx)
	{
		FPoseSearchDatabaseSequence& NewDbSequence = PoseSearchDatabase->Sequences.AddDefaulted_GetRef();
		NewDbSequence.Sequence = AnimSequence;

		if (InitialGroupIdx >= 0)
		{
			const FPoseSearchDatabaseGroup& InitialGroup = PoseSearchDatabase->Groups[InitialGroupIdx];
			NewDbSequence.GroupTags.AddTag(InitialGroup.Tag);
		}
	}

	void FDatabaseViewModel::AddBlendSpaceToDatabase(UBlendSpace* BlendSpace, int InitialGroupIdx)
	{
		FPoseSearchDatabaseBlendSpace& NewDbBlendSpace = PoseSearchDatabase->BlendSpaces.AddDefaulted_GetRef();
		NewDbBlendSpace.BlendSpace = BlendSpace;

		if (InitialGroupIdx >= 0)
		{
			const FPoseSearchDatabaseGroup& InitialGroup = PoseSearchDatabase->Groups[InitialGroupIdx];
			NewDbBlendSpace.GroupTags.AddTag(InitialGroup.Tag);
		}
	}

	void FDatabaseViewModel::AddGroupToDatabase()
	{
		PoseSearchDatabase->Groups.AddDefaulted();
	}

	void FDatabaseViewModel::DeleteSequenceFromDatabase(int32 SequenceIdx)
	{
		PoseSearchDatabase->Sequences.RemoveAt(SequenceIdx);
	}

	void FDatabaseViewModel::RemoveSequenceFromGroup(int32 SequenceIdx, int32 GroupIdx)
	{
		FPoseSearchDatabaseSequence& DbSequence = PoseSearchDatabase->Sequences[SequenceIdx];
		FPoseSearchDatabaseGroup& DbGroup = PoseSearchDatabase->Groups[GroupIdx];
		if (DbGroup.Tag.IsValid())
		{
			DbSequence.GroupTags.RemoveTag(DbGroup.Tag);
		}
	}

	void FDatabaseViewModel::DeleteBlendSpaceFromDatabase(int32 BlendSpaceIdx)
	{
		PoseSearchDatabase->BlendSpaces.RemoveAt(BlendSpaceIdx);
	}

	void FDatabaseViewModel::RemoveBlendSpaceFromGroup(int32 BlendSpaceIdx, int32 GroupIdx)
	{
		FPoseSearchDatabaseBlendSpace& DbBlendSpace = PoseSearchDatabase->BlendSpaces[BlendSpaceIdx];
		FPoseSearchDatabaseGroup& DbGroup = PoseSearchDatabase->Groups[GroupIdx];
		if (DbGroup.Tag.IsValid())
		{
			DbBlendSpace.GroupTags.RemoveTag(DbGroup.Tag);
		}
	}

	void FDatabaseViewModel::DeleteGroup(int32 GroupIdx)
	{
		FPoseSearchDatabaseGroup& DbGroup = PoseSearchDatabase->Groups[GroupIdx];

		if (DbGroup.Tag.IsValid())
		{
			for (FPoseSearchDatabaseSequence& Sequence : PoseSearchDatabase->Sequences)
			{
				Sequence.GroupTags.RemoveTag(DbGroup.Tag);
			}

			for (FPoseSearchDatabaseBlendSpace& BlendSpace : PoseSearchDatabase->BlendSpaces)
			{
				BlendSpace.GroupTags.RemoveTag(DbGroup.Tag);
			}
		}

		PoseSearchDatabase->Groups.RemoveAt(GroupIdx);
	}

	void FDatabaseViewModel::SetSelectedSequenceEnabled(int32 SequenceIndex, bool bEnabled)
	{
		PoseSearchDatabase->Sequences[SequenceIndex].bEnabled = bEnabled;
		PoseSearchDatabase->NotifyAssetChange();
		PoseSearchDatabase->BeginCacheDerivedData();
	}	
	
	void FDatabaseViewModel::SetSelectedBlendSpaceEnabled(int32 BlendSpaceIndex, bool bEnabled)
	{
		PoseSearchDatabase->BlendSpaces[BlendSpaceIndex].bEnabled = bEnabled;
		PoseSearchDatabase->NotifyAssetChange();
		PoseSearchDatabase->BeginCacheDerivedData();
	}

	bool FDatabaseViewModel::IsSelectedSequenceEnabled(int32 SequenceIndex) const
	{
		return PoseSearchDatabase->Sequences[SequenceIndex].bEnabled;
		PoseSearchDatabase->NotifyAssetChange();
	}

	bool FDatabaseViewModel::IsSelectedBlendSpaceEnabled(int32 BlendSpaceIndex) const
	{
		return PoseSearchDatabase->BlendSpaces[BlendSpaceIndex].bEnabled;
	}

	void FDatabaseViewModel::SetSelectedNodes(const TArrayView<TSharedPtr<FDatabaseAssetTreeNode>>& InSelectedNodes)
	{
		SelectedNodes = InSelectedNodes;
		ResetPreviewActors();
		ProcessSelectedActor(nullptr);
	}

	void FDatabaseViewModel::ProcessSelectedActor(AActor* Actor)
	{
		SelectedActorIndexAsset = nullptr;
		
		const FDatabasePreviewActor* SelectedPreviewActor = PreviewActors.FindByPredicate(
			[Actor](const FDatabasePreviewActor& PreviewActor)
		{
			return PreviewActor.Actor == Actor;
		});

		if (SelectedPreviewActor)
		{
			SelectedActorIndexAsset = &SelectedPreviewActor->IndexAsset;
		}
	}

	float FDatabaseViewModel::GetMaxPreviewPlayLength() const
	{
		return MaxPreviewPlayLength;
	}

	float FDatabaseViewModel::GetPlayTime() const
	{
		const float ClampedPlayTime = FMath::Clamp(PlayTime, 0.0f, MaxPreviewPlayLength);
		return ClampedPlayTime;
	}

	void FDatabaseViewModel::SetPlayTime(float NewPlayTime, bool bInTickPlayTime)
	{
		PlayTime = NewPlayTime;
		DeltaTimeMultiplier = bInTickPlayTime ? DeltaTimeMultiplier : 0.0f;
	}
}
