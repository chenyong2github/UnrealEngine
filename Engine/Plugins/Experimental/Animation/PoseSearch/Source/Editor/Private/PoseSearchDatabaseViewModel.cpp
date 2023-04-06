// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchDatabaseViewModel.h"
#include "PoseSearchDatabasePreviewScene.h"
#include "PoseSearchDatabaseAssetTreeNode.h"
#include "PoseSearchEditor.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDefines.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "Modules/ModuleManager.h"
#include "AnimPreviewInstance.h"
#include "Animation/AnimComposite.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Animation/MirrorDataTable.h"
#include "Animation/BlendSpace.h"
#include "EngineUtils.h"
#include "InstancedStruct.h"
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
		Sampler.Process();
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

		PoseSearchDatabase->RegisterOnDerivedDataRebuild(UPoseSearchDatabase::FOnDerivedDataRebuild::CreateSP(this, &FDatabaseViewModel::RemovePreviewActors));
	}

	void FDatabaseViewModel::BuildSearchIndex()
	{
		using namespace UE::PoseSearch;
		FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(PoseSearchDatabase, ERequestAsyncBuildFlag::NewRequest);
	}

	void FDatabaseViewModel::PreviewBackwardEnd()
	{
		PlayTime = MinPreviewPlayLength;
	}

	void FDatabaseViewModel::PreviewBackwardStep()
	{
		PlayTime = FMath::Clamp(PlayTime - StepDeltaTime, MinPreviewPlayLength, MaxPreviewPlayLength);
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
		PlayTime = FMath::Clamp(PlayTime + StepDeltaTime, MinPreviewPlayLength, MaxPreviewPlayLength);
		DeltaTimeMultiplier = 0.0f;
	}

	void FDatabaseViewModel::PreviewForwardEnd()
	{
		PlayTime = MaxPreviewPlayLength;
	}

	FDatabasePreviewActor FDatabaseViewModel::SpawnPreviewActor(int32 IndexAssetIndex, const FBoneContainer& BoneContainer, float PlayTimeOffset)
	{
		FDatabasePreviewActor PreviewActor;

		check(PoseSearchDatabase);
		const FPoseSearchIndex& SearchIndex = PoseSearchDatabase->GetSearchIndex();
		const FPoseSearchIndexAsset& IndexAsset = SearchIndex.Assets[IndexAssetIndex];

		const FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAsset = PoseSearchDatabase->GetAnimationAssetBase(IndexAsset.SourceAssetIdx);
		UAnimationAsset* PreviewAsset = DatabaseAnimationAsset->GetAnimationAsset();
		if (!PreviewAsset)
		{
			return PreviewActor;
		}

		const FInstancedStruct& DatabaseAsset = PoseSearchDatabase->GetAnimationAssetStruct(IndexAsset);
		const FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAssetBase = DatabaseAsset.GetPtr<FPoseSearchDatabaseAnimationAssetBase>();
		check(DatabaseAnimationAssetBase);
		PreviewActor.Sampler.Init(DatabaseAnimationAssetBase->GetAnimationAsset(), IndexAsset.BlendParameters, BoneContainer);
		PreviewActor.IndexAssetIndex = IndexAssetIndex;
		PreviewActor.CurrentPoseIndex = INDEX_NONE;
		PreviewActor.PlayTimeOffset = PlayTimeOffset;

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		PreviewActor.Actor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		PreviewActor.Actor->SetFlags(RF_Transient);

		UDebugSkelMeshComponent* Mesh = NewObject<UDebugSkelMeshComponent>(PreviewActor.Actor.Get());
		Mesh->RegisterComponentWithWorld(GetWorld());

		UAnimPreviewInstance* AnimInstance = NewObject<UAnimPreviewInstance>(Mesh);

		Mesh->PreviewInstance = AnimInstance;
		AnimInstance->InitializeAnimation();

		USkeletalMesh* DatabasePreviewMesh = PoseSearchDatabase->PreviewMesh;
		Mesh->SetSkeletalMesh(DatabasePreviewMesh ? DatabasePreviewMesh : PoseSearchDatabase->Schema->Skeleton->GetPreviewMesh(true));
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

		AnimInstance->SetPlayRate(0.f);

		UE_LOG(LogPoseSearchEditor, Log, TEXT("Spawned preview Actor: %s"), *GetNameSafe(PreviewActor.Actor.Get()));
		return PreviewActor;
	}

	UWorld* FDatabaseViewModel::GetWorld()
	{
		check(PreviewScenePtr.IsValid());
		return PreviewScenePtr.Pin()->GetWorld();
	}

	void FDatabaseViewModel::OnPreviewActorClassChanged()
	{
		// todo: implement
	}

	void FDatabaseViewModel::Tick(float DeltaSeconds)
	{
		PlayTime += DeltaSeconds * DeltaTimeMultiplier;
		PlayTime = FMath::Clamp(PlayTime, MinPreviewPlayLength, MaxPreviewPlayLength);

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
			UAnimPreviewInstance* AnimInstance = PreviewActor.GetAnimPreviewInstance();
			if (!AnimInstance || PreviewActor.IndexAssetIndex >= SearchIndex.Assets.Num())
			{
				continue;
			}

			const UAnimationAsset* PreviewAsset = AnimInstance->GetAnimationAsset();
			if (!PreviewAsset || !PreviewActor.Sampler.IsInitialized())
			{
				continue;
			}

			float CurrentTime = 0.f;
			const FPoseSearchIndexAsset& IndexAsset = SearchIndex.Assets[PreviewActor.IndexAssetIndex];
			float CurrentPlayTime = PlayTime + IndexAsset.SamplingInterval.Min + PreviewActor.PlayTimeOffset;
			FAnimationRuntime::AdvanceTime(false, CurrentPlayTime, CurrentTime, IndexAsset.SamplingInterval.Max);

			// time to pose index
			PreviewActor.CurrentPoseIndex = PoseSearchDatabase->GetPoseIndexFromTime(CurrentTime, IndexAsset);

			// pose index to quantized time
			if (PreviewActor.CurrentPoseIndex >= 0)
			{
				CurrentTime = PoseSearchDatabase->GetAssetTime(PreviewActor.CurrentPoseIndex);
			}

			// quantized time to scaled quantized time
			const float CurrentScaledTime = PreviewActor.Sampler.GetScaledTime(CurrentTime);

			AnimInstance->SetPosition(CurrentScaledTime);
			AnimInstance->SetPlayRate(0.f);
			AnimInstance->SetBlendSpacePosition(IndexAsset.BlendParameters);

			FTransform RootMotion = PreviewActor.Sampler.ExtractRootTransform(CurrentTime);
			if (AnimInstance->GetMirrorDataTable())
			{
				RootMotion = MirrorRootMotion(RootMotion, AnimInstance->GetMirrorDataTable());
			}

			if (PreviewActor.PlayTimeOffset != 0.f)
			{
				FTransform OriginRootMotion = PreviewActor.Sampler.ExtractRootTransform(PreviewActor.PlayTimeOffset);
				if (AnimInstance->GetMirrorDataTable())
				{
					OriginRootMotion = MirrorRootMotion(OriginRootMotion, AnimInstance->GetMirrorDataTable());
				}
				RootMotion.SetToRelativeTransform(OriginRootMotion);
			}

			PreviewActor.Actor->SetActorTransform(RootMotion);
		}
	}

	void FDatabaseViewModel::RemovePreviewActors()
	{
		PlayTime = 0.f;
		DeltaTimeMultiplier = 1.f;
		MaxPreviewPlayLength = 0.f;
		MinPreviewPlayLength = 0.f;
		bIsEditorSelection = true;

		for (auto PreviewActor : PreviewActors)
		{
			// @todo: PreviewActor.Actor is a TWeakObjectPtr so it can be null.
			PreviewActor.Actor->Destroy();
		}
		PreviewActors.Reset();
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
			RemovePreviewActors();
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
		FPoseSearchDatabaseSequence NewAsset;
		NewAsset.Sequence = AnimSequence;
		PoseSearchDatabase->AnimationAssets.Add(FInstancedStruct::Make(NewAsset));
	}

	void FDatabaseViewModel::AddBlendSpaceToDatabase(UBlendSpace* BlendSpace)
	{
		FPoseSearchDatabaseBlendSpace NewAsset;
		NewAsset.BlendSpace = BlendSpace;
		PoseSearchDatabase->AnimationAssets.Add(FInstancedStruct::Make(NewAsset));
	}

	void FDatabaseViewModel::AddAnimCompositeToDatabase(UAnimComposite* AnimComposite)
	{
		FPoseSearchDatabaseAnimComposite NewAsset;
		NewAsset.AnimComposite = AnimComposite;
		PoseSearchDatabase->AnimationAssets.Add(FInstancedStruct::Make(NewAsset));
	}

	void FDatabaseViewModel::AddAnimMontageToDatabase(UAnimMontage* AnimMontage)
	{
		FPoseSearchDatabaseAnimMontage NewAsset;
		NewAsset.AnimMontage = AnimMontage;
		PoseSearchDatabase->AnimationAssets.Add(FInstancedStruct::Make(NewAsset));
	}

	void FDatabaseViewModel::DeleteFromDatabase(int32 AnimationAssetIndex)
	{
		PoseSearchDatabase->AnimationAssets.RemoveAt(AnimationAssetIndex);
	}

	void FDatabaseViewModel::SetIsEnabled(int32 AnimationAssetIndex, bool bEnabled)
	{
		if (FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAsset = PoseSearchDatabase->GetMutableAnimationAssetBase(AnimationAssetIndex))
		{
			DatabaseAnimationAsset->SetIsEnabled(bEnabled);
		}
	}

	bool FDatabaseViewModel::IsEnabled(int32 AnimationAssetIndex) const
	{
		if (const FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAsset = PoseSearchDatabase->GetAnimationAssetBase(AnimationAssetIndex))
		{
			return DatabaseAnimationAsset->IsEnabled();
		}

		return false;
	}

	int32 FDatabaseViewModel::SetSelectedNode(int32 PoseIdx, bool bClearSelection)
	{
		int32 SelectedSourceAssetIdx = -1;

		if (bClearSelection)
		{
			RemovePreviewActors();
		}

		bIsEditorSelection = false;

		if (FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(PoseSearchDatabase, ERequestAsyncBuildFlag::ContinueRequest))
		{
			FBoneContainer BoneContainer;
			BoneContainer.InitializeTo(PoseSearchDatabase->Schema->BoneIndicesWithParents, UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::DisallowAll), *PoseSearchDatabase->Schema->Skeleton);

			const FPoseSearchIndex& SearchIndex = PoseSearchDatabase->GetSearchIndex();
			if (SearchIndex.PoseMetadata.IsValidIndex(PoseIdx))
			{
				const uint32 IndexAssetIndex = SearchIndex.PoseMetadata[PoseIdx].GetAssetIndex();
				if (SearchIndex.Assets.IsValidIndex(IndexAssetIndex))
				{
					const FPoseSearchIndexAsset& IndexAsset = SearchIndex.Assets[IndexAssetIndex];
					// clamping just to make sure the time interval is correct
					const float PlayTimeOffset = FMath::Clamp(PoseSearchDatabase->GetAssetTime(PoseIdx), IndexAsset.SamplingInterval.Min, IndexAsset.SamplingInterval.Max);
					FDatabasePreviewActor PreviewActor = SpawnPreviewActor(IndexAssetIndex, BoneContainer, PlayTimeOffset);
					if (PreviewActor.IsValid())
					{
						MaxPreviewPlayLength = FMath::Max(MaxPreviewPlayLength, IndexAsset.SamplingInterval.Max - PlayTimeOffset);
						MinPreviewPlayLength = FMath::Min(MinPreviewPlayLength, IndexAsset.SamplingInterval.Min - PlayTimeOffset);
						PreviewActors.Add(PreviewActor);
						SelectedSourceAssetIdx = IndexAsset.SourceAssetIdx;
					}
				}
			}

			ParallelFor(PreviewActors.Num(), [this](int32 PreviewActorIndex) { PreviewActors[PreviewActorIndex].Process(); }, ParallelForFlags);

			UpdatePreviewActors();

			SetPlayTime(0.f, false);
		}

		ProcessSelectedActor(nullptr);

		return SelectedSourceAssetIdx;
	}

	void FDatabaseViewModel::SetSelectedNodes(const TArrayView<TSharedPtr<FDatabaseAssetTreeNode>>& InSelectedNodes)
	{
		bIsEditorSelection = true;

		RemovePreviewActors();

		if (FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(PoseSearchDatabase, ERequestAsyncBuildFlag::ContinueRequest))
		{
			FBoneContainer BoneContainer;
			BoneContainer.InitializeTo(PoseSearchDatabase->Schema->BoneIndicesWithParents, UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::DisallowAll), *PoseSearchDatabase->Schema->Skeleton);

			TMap<int32, int32> AssociatedAssetIndices;
			for (int32 i = 0; i < InSelectedNodes.Num(); ++i)
			{
				AssociatedAssetIndices.FindOrAdd(InSelectedNodes[i]->SourceAssetIdx) = i;
			}

			const FPoseSearchIndex& SearchIndex = PoseSearchDatabase->GetSearchIndex();
			for (int32 IndexAssetIndex = 0; IndexAssetIndex < SearchIndex.Assets.Num(); ++IndexAssetIndex)
			{
				const FPoseSearchIndexAsset& IndexAsset = SearchIndex.Assets[IndexAssetIndex];
				if (AnimationPreviewMode == EAnimationPreviewMode::OriginalAndMirrored || !IndexAsset.bMirrored)
				{
					if (const int32* SelectedNodesIndex = AssociatedAssetIndices.Find(IndexAsset.SourceAssetIdx))
					{
						FDatabasePreviewActor PreviewActor = SpawnPreviewActor(IndexAssetIndex, BoneContainer, 0.f);
						if (PreviewActor.IsValid())
						{
							MaxPreviewPlayLength = FMath::Max(MaxPreviewPlayLength, IndexAsset.SamplingInterval.Max - IndexAsset.SamplingInterval.Min);
							PreviewActors.Add(PreviewActor);
						}
					}
				}
			}

			ParallelFor(PreviewActors.Num(), [this](int32 PreviewActorIndex) { PreviewActors[PreviewActorIndex].Process(); }, ParallelForFlags);

			UpdatePreviewActors();
		}

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

	TRange<double> FDatabaseViewModel::GetPreviewPlayRange() const
	{
		constexpr double ViewRangeSlack = 0.2;
		return TRange<double>(MinPreviewPlayLength - ViewRangeSlack, MaxPreviewPlayLength + ViewRangeSlack);
	}

	float FDatabaseViewModel::GetPlayTime() const
	{
		const float ClampedPlayTime = FMath::Clamp(PlayTime, MinPreviewPlayLength, MaxPreviewPlayLength);
		return ClampedPlayTime;
	}

	void FDatabaseViewModel::SetPlayTime(float NewPlayTime, bool bInTickPlayTime)
	{
		PlayTime = NewPlayTime;
		DeltaTimeMultiplier = bInTickPlayTime ? DeltaTimeMultiplier : 0.0f;
	}

	bool FDatabaseViewModel::GetAnimationTime(int32 SourceAssetIdx, float& CurrentPlayTime, FVector& BlendParameters) const
	{
		if (FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(PoseSearchDatabase, ERequestAsyncBuildFlag::ContinueRequest))
		{
			const FPoseSearchIndex& SearchIndex = PoseSearchDatabase->GetSearchIndex();
			for (const FDatabasePreviewActor& PreviewActor : GetPreviewActors())
			{
				if (PreviewActor.IndexAssetIndex >= 0 && PreviewActor.IndexAssetIndex < SearchIndex.Assets.Num() && PreviewActor.Sampler.IsInitialized())
				{
					const FPoseSearchIndexAsset& IndexAsset = SearchIndex.Assets[PreviewActor.IndexAssetIndex];
					if (IndexAsset.SourceAssetIdx == SourceAssetIdx)
					{
						CurrentPlayTime = PlayTime + IndexAsset.SamplingInterval.Min + PreviewActor.PlayTimeOffset;
						BlendParameters = IndexAsset.BlendParameters;
						return true;
					}
				}
			}

			for (const FPoseSearchIndexAsset& IndexAsset : SearchIndex.Assets)
			{
				if (IndexAsset.SourceAssetIdx == SourceAssetIdx)
				{
					CurrentPlayTime = PlayTime + IndexAsset.SamplingInterval.Min;
					BlendParameters = IndexAsset.BlendParameters;
					return true;
				}
			}
		}

		CurrentPlayTime = 0.f;
		BlendParameters = FVector::ZeroVector;
		return false;
	}
}
