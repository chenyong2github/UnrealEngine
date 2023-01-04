// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchDatabaseViewModel.h"
#include "PoseSearchDatabasePreviewScene.h"
#include "SPoseSearchDatabaseAssetList.h"
#include "PoseSearchEditor.h"
#include "PoseSearch/PoseSearchDerivedData.h"
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
#include "GameFramework/PlayerController.h"

namespace UE::PoseSearch
{
	constexpr float StepDeltaTime = 1.0f / 30.0f;

	// FDatabasePreviewActor
	bool FDatabasePreviewActor::IsValid() const
	{
		return Actor != nullptr;
	}

	void FDatabasePreviewActor::Process()
	{
		if (Type == ESearchIndexAssetType::Sequence)
		{
			SequenceSampler.Process();
		}
		else if (Type == ESearchIndexAssetType::BlendSpace)
		{
			BlendSpaceSampler.Process();
		}
	}

	const IAssetSampler* FDatabasePreviewActor::GetSampler() const
	{
		switch (Type)
		{
		case ESearchIndexAssetType::Sequence:
			return &SequenceSampler;
		case ESearchIndexAssetType::BlendSpace:
			return &BlendSpaceSampler;
		}
		return nullptr;
	}

	float FDatabasePreviewActor::GetScaledTime(float Time) const
	{
		float ScaledTime = Time;
		if (Type == ESearchIndexAssetType::BlendSpace)
		{
			ScaledTime = BlendSpaceSampler.GetPlayLength() > UE_KINDA_SMALL_NUMBER ? Time / BlendSpaceSampler.GetPlayLength() : 0.f;
		}
		return ScaledTime;
	}

	UDebugSkelMeshComponent* FDatabasePreviewActor::GetDebugSkelMeshComponent()
	{
		if (IsValid())
		{
			return Cast<UDebugSkelMeshComponent>(Actor->GetRootComponent());
		}
		return nullptr;
	}

	UAnimPreviewInstance* FDatabasePreviewActor::GetAnimPreviewInstance()
	{
		if (UDebugSkelMeshComponent* Mesh = GetDebugSkelMeshComponent())
		{
			return Mesh->PreviewInstance.Get();
		}
		return nullptr;
	}

	// FDatabaseViewModel
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

	void FDatabaseViewModel::Initialize(UPoseSearchDatabase* InPoseSearchDatabase, const TSharedRef<FDatabasePreviewScene>& InPreviewScene)
	{
		PoseSearchDatabase = InPoseSearchDatabase;
		PreviewScenePtr = InPreviewScene;

		RemovePreviewActors();

		PoseSearchDatabase->RegisterOnDerivedDataRebuild(UPoseSearchDatabase::FOnDerivedDataRebuild::CreateSP(this, &FDatabaseViewModel::ResetPreviewActors));
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

		MaxPreviewPlayLength = 0.0f;

		if (AnimationPreviewMode == EAnimationPreviewMode::None && SelectedNodes.IsEmpty())
		{
			return;
		}

		TSet<int32> AssociatedSequencesAssetIndices;
		TSet<int32> AssociatedBlendSpacesAssetIndices;
		for (const TSharedPtr<FDatabaseAssetTreeNode>& SelectedNode : SelectedNodes)
		{
			if (SelectedNode->SourceAssetType == ESearchIndexAssetType::Sequence)
			{
				AssociatedSequencesAssetIndices.Add(SelectedNode->SourceAssetIdx);
			}
			else if (SelectedNode->SourceAssetType == ESearchIndexAssetType::BlendSpace)
			{
				AssociatedBlendSpacesAssetIndices.Add(SelectedNode->SourceAssetIdx);
			}
		}

		if (FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(PoseSearchDatabase, ERequestAsyncBuildFlag::ContinueRequest))
		{
			FBoneContainer BoneContainer;
			BoneContainer.InitializeTo(PoseSearchDatabase->Schema->BoneIndicesWithParents, FCurveEvaluationOption(false), *PoseSearchDatabase->Schema->Skeleton);

			const FPoseSearchIndex& SearchIndex = PoseSearchDatabase->GetSearchIndex();
			for (int32 IndexAssetIndex = 0; IndexAssetIndex < SearchIndex.Assets.Num(); ++IndexAssetIndex)
			{
				const FPoseSearchIndexAsset& IndexAsset = SearchIndex.Assets[IndexAssetIndex];

				bool bSpawn = false;
				if (AnimationPreviewMode == EAnimationPreviewMode::OriginalAndMirrored || !IndexAsset.bMirrored)
				{
					bool bIsAssociatedToSelection = false;
					if (IndexAsset.Type == ESearchIndexAssetType::Sequence)
					{
						bSpawn = AssociatedSequencesAssetIndices.Contains(IndexAsset.SourceAssetIdx);
					}
					else if (IndexAsset.Type == ESearchIndexAssetType::BlendSpace)
					{
						bSpawn = AssociatedBlendSpacesAssetIndices.Contains(IndexAsset.SourceAssetIdx);
					}
				}

				if (bSpawn)
				{
					FDatabasePreviewActor PreviewActor = SpawnPreviewActor(IndexAssetIndex, BoneContainer);
					if (PreviewActor.IsValid())
					{
						const UAnimationAsset* AnimationAsset = PreviewActor.GetAnimPreviewInstance()->GetAnimationAsset();
						if (AnimationAsset)
						{
							MaxPreviewPlayLength = FMath::Max(MaxPreviewPlayLength, AnimationAsset->GetPlayLength());
						}
						
						PreviewActors.Add(PreviewActor);
					}
				}
			}

			ParallelFor(PreviewActors.Num(), [this](int32 PreviewActorIndex) { PreviewActors[PreviewActorIndex].Process(); }, ParallelForFlags);

			UpdatePreviewActors();
		}
	}

	void FDatabaseViewModel::BuildSearchIndex()
	{
		using namespace UE::PoseSearch;
		FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(PoseSearchDatabase, ERequestAsyncBuildFlag::NewRequest);
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

	FDatabasePreviewActor FDatabaseViewModel::SpawnPreviewActor(int32 IndexAssetIndex, const FBoneContainer& BoneContainer)
	{
		FDatabasePreviewActor PreviewActor;

		check(PoseSearchDatabase);
		const FPoseSearchIndex& SearchIndex = PoseSearchDatabase->GetSearchIndex();
		const FPoseSearchIndexAsset& IndexAsset = SearchIndex.Assets[IndexAssetIndex];
		UAnimationAsset* PreviewAsset = nullptr;
		if (IndexAsset.Type == ESearchIndexAssetType::Sequence)
		{
			FPoseSearchDatabaseSequence DatabaseSequence = PoseSearchDatabase->Sequences[IndexAsset.SourceAssetIdx];
			PreviewAsset = DatabaseSequence.Sequence;
			if (!PreviewAsset)
			{
				return PreviewActor;
			}

			FSequenceSampler::FInput Input;
			Input.ExtrapolationParameters = PoseSearchDatabase->ExtrapolationParameters;
			Input.Sequence = DatabaseSequence.Sequence;

			PreviewActor.SequenceSampler.Init(Input);
		}
		else if (IndexAsset.Type == ESearchIndexAssetType::BlendSpace)
		{
			FPoseSearchDatabaseBlendSpace DatabaseBlendSpace = PoseSearchDatabase->BlendSpaces[IndexAsset.SourceAssetIdx];
			PreviewAsset = DatabaseBlendSpace.BlendSpace;
			if (!PreviewAsset)
			{
				return PreviewActor;
			}

			FBlendSpaceSampler::FInput Input;
			Input.BoneContainer = BoneContainer;
			Input.ExtrapolationParameters = PoseSearchDatabase->ExtrapolationParameters;
			Input.BlendSpace = DatabaseBlendSpace.BlendSpace;
			Input.BlendParameters = IndexAsset.BlendParameters;

			PreviewActor.BlendSpaceSampler.Init(Input);
		}

		PreviewActor.Type = IndexAsset.Type;
		PreviewActor.IndexAssetIndex = IndexAssetIndex;
		PreviewActor.CurrentPoseIndex = INDEX_NONE;

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		PreviewActor.Actor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		PreviewActor.Actor->SetFlags(RF_Transient);

		UDebugSkelMeshComponent* Mesh = NewObject<UDebugSkelMeshComponent>(PreviewActor.Actor.Get());
		Mesh->RegisterComponentWithWorld(GetWorld());

		UAnimPreviewInstance* AnimInstance = NewObject<UAnimPreviewInstance>(Mesh);

		Mesh->PreviewInstance = AnimInstance;
		AnimInstance->InitializeAnimation();

		Mesh->SetSkeletalMesh(PoseSearchDatabase->Schema->Skeleton->GetPreviewMesh(true));
		Mesh->EnablePreview(true, PreviewAsset);
		
		AnimInstance->SetAnimationAsset(PreviewAsset, false, 0.0f);
		AnimInstance->SetBlendSpacePosition(IndexAsset.BlendParameters);
		
		if (IndexAsset.bMirrored && PoseSearchDatabase->Schema)
		{
			AnimInstance->SetMirrorDataTable(PoseSearchDatabase->Schema->MirrorDataTable);
		}

		AnimInstance->PlayAnim(false, 0.0f);

		if (!PreviewActor.Actor->GetRootComponent())
		{
			PreviewActor.Actor->SetRootComponent(Mesh);
		}

		UE_LOG(LogPoseSearchEditor, Log, TEXT("Spawned preview Actor: %s"), *GetNameSafe(PreviewActor.Actor.Get()));
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

		if (FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(PoseSearchDatabase, ERequestAsyncBuildFlag::ContinueRequest))
		{
			UpdatePreviewActors(true);
		}
	}

	void FDatabaseViewModel::UpdatePreviewActors(bool bInTickPlayTime)
	{
		const FPoseSearchIndex& SearchIndex = PoseSearchDatabase->GetSearchIndex();
		TSharedPtr<FDatabasePreviewScene> PreviewScene = PreviewScenePtr.Pin();
		for (FDatabasePreviewActor& PreviewActor : GetPreviewActors())
		{
			if (PreviewActor.IndexAssetIndex < SearchIndex.Assets.Num())
			{
				if (const UAnimationAsset* PreviewAsset = PreviewActor.GetAnimPreviewInstance()->GetAnimationAsset())
				{
					if (const IAssetSampler* Sampler = PreviewActor.GetSampler())
					{
						float CurrentTime = 0.f;
						FAnimationRuntime::AdvanceTime(false, PlayTime, CurrentTime, PreviewAsset->GetPlayLength());

						FTransform RootMotion = Sampler->ExtractRootTransform(CurrentTime);
						if (PreviewActor.GetAnimPreviewInstance()->GetMirrorDataTable())
						{
							RootMotion = MirrorRootMotion(RootMotion, PreviewActor.GetAnimPreviewInstance()->GetMirrorDataTable());
						}
						PreviewActor.Actor->SetActorTransform(RootMotion);

						const float CurrentScaledTime = PreviewActor.GetScaledTime(CurrentTime);
						PreviewActor.GetAnimPreviewInstance()->SetPosition(CurrentScaledTime);

						float PlayRate = 1.f;
						if (FMath::IsNearlyZero(DeltaTimeMultiplier) || CurrentScaledTime <= UE_SMALL_NUMBER || CurrentScaledTime >= (PreviewAsset->GetPlayLength() - UE_SMALL_NUMBER))
						{
							PlayRate = 0.f;
						}
						PreviewActor.GetAnimPreviewInstance()->SetPlayRate(PlayRate);

						const FPoseSearchIndexAsset& SearchIndexAsset = SearchIndex.Assets[PreviewActor.IndexAssetIndex];
						PreviewActor.CurrentPoseIndex = PoseSearchDatabase->GetPoseIndexFromTime(CurrentTime, SearchIndexAsset);
					}
				}
			}
		}
	}

	void FDatabaseViewModel::RemovePreviewActors()
	{
		for (auto PreviewActor : PreviewActors)
		{
			// @todo: PreviewActor.Actor is a TWeakObjectPtr so it can be null.
			PreviewActor.Actor->Destroy();
		}
		PreviewActors.Reset();

		MaxPreviewPlayLength = 0.0f;
	}

	FTransform FDatabaseViewModel::MirrorRootMotion(
		FTransform RootMotion,
		const UMirrorDataTable* MirrorDataTable)
	{
		const FTransform RootReferenceTransform = PoseSearchDatabase->Schema->Skeleton->GetReferenceSkeleton().GetRefBonePose()[0];
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
		return EnumHasAnyFlags(PoseFeaturesDrawMode, DrawMode);
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
		return EnumHasAnyFlags(AnimationPreviewMode, PreviewMode);
	}

	void FDatabaseViewModel::OnToggleDisplayRootMotionSpeed()
	{
		DisplayRootMotionSpeed = !DisplayRootMotionSpeed;
	}

	void FDatabaseViewModel::AddSequenceToDatabase(UAnimSequence* AnimSequence)
	{
		FPoseSearchDatabaseSequence& NewDbSequence = PoseSearchDatabase->Sequences.AddDefaulted_GetRef();
		NewDbSequence.Sequence = AnimSequence;
	}

	void FDatabaseViewModel::AddBlendSpaceToDatabase(UBlendSpace* BlendSpace)
	{
		FPoseSearchDatabaseBlendSpace& NewDbBlendSpace = PoseSearchDatabase->BlendSpaces.AddDefaulted_GetRef();
		NewDbBlendSpace.BlendSpace = BlendSpace;
	}

	void FDatabaseViewModel::DeleteSequenceFromDatabase(int32 SequenceIdx)
	{
		PoseSearchDatabase->Sequences.RemoveAt(SequenceIdx);
	}

	void FDatabaseViewModel::DeleteBlendSpaceFromDatabase(int32 BlendSpaceIdx)
	{
		PoseSearchDatabase->BlendSpaces.RemoveAt(BlendSpaceIdx);
	}

	void FDatabaseViewModel::SetSelectedSequenceEnabled(int32 SequenceIndex, bool bEnabled)
	{
		PoseSearchDatabase->Sequences[SequenceIndex].bEnabled = bEnabled;
	}	
	
	void FDatabaseViewModel::SetSelectedBlendSpaceEnabled(int32 BlendSpaceIndex, bool bEnabled)
	{
		PoseSearchDatabase->BlendSpaces[BlendSpaceIndex].bEnabled = bEnabled;
	}

	bool FDatabaseViewModel::IsSelectedSequenceEnabled(int32 SequenceIndex) const
	{
		return PoseSearchDatabase->Sequences[SequenceIndex].bEnabled;
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
		SelectedActorIndexAssetIndex = INDEX_NONE;
		if (const FDatabasePreviewActor* SelectedPreviewActor = PreviewActors.FindByPredicate([Actor](const FDatabasePreviewActor& PreviewActor) { return PreviewActor.Actor == Actor; }))
		{
			SelectedActorIndexAssetIndex = SelectedPreviewActor->IndexAssetIndex;
		}
	}

	const FPoseSearchIndexAsset* FDatabaseViewModel::GetSelectedActorIndexAsset() const
	{
		if (FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(PoseSearchDatabase, ERequestAsyncBuildFlag::ContinueRequest))
		{
			const FPoseSearchIndex& SearchIndex = PoseSearchDatabase->GetSearchIndex();
			if (SearchIndex.Assets.IsValidIndex(SelectedActorIndexAssetIndex))
			{
				return &SearchIndex.Assets[SelectedActorIndexAssetIndex];
			}
		}
		return nullptr;
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
