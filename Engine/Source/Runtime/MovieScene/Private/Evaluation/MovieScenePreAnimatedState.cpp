// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluation/MovieScenePreAnimatedState.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStateExtension.h"
#include "Evaluation/PreAnimatedState/MovieSceneRestoreStateParams.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedCaptureSources.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedObjectTokenStorage.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedMasterTokenStorage.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedEntityCaptureSource.h"

DECLARE_CYCLE_STAT(TEXT("Save Pre Animated State"), MovieSceneEval_SavePreAnimatedState, STATGROUP_MovieSceneEval);

namespace UE
{
namespace MovieScene
{

IMovieScenePlayer* FRestoreStateParams::GetTerminalPlayer() const
{
	if (Linker && TerminalInstanceHandle.IsValid())
	{
		return Linker->GetInstanceRegistry()->GetInstance(TerminalInstanceHandle).GetPlayer();
	}

	ensureAlways(false);
	return nullptr;
}

} // namespace MovieScene
} // namespace UE


FScopedPreAnimatedCaptureSource::FScopedPreAnimatedCaptureSource(FMovieScenePreAnimatedState* InPreAnimatedState, const FMovieSceneEvaluationKey& InEvalKey, bool bInWantsRestoreState)
	: Variant(TInPlaceType<FMovieSceneEvaluationKey>(), InEvalKey)
	, PreAnimatedState(InPreAnimatedState)
	, PrevCaptureSource(PreAnimatedState->CaptureSource)
	, bWantsRestoreState(bInWantsRestoreState)
{
	InPreAnimatedState->CaptureSource = this;
}
FScopedPreAnimatedCaptureSource::FScopedPreAnimatedCaptureSource(FMovieScenePreAnimatedState* InPreAnimatedState, const UObject* InEvalHook, FMovieSceneSequenceID InSequenceID, bool bInWantsRestoreState)
	: Variant(TInPlaceType<FEvalHookType>(), FEvalHookType{InEvalHook, InSequenceID} )
	, PreAnimatedState(InPreAnimatedState)
	, PrevCaptureSource(PreAnimatedState->CaptureSource)
	, bWantsRestoreState(bInWantsRestoreState)
{
	InPreAnimatedState->CaptureSource = this;
}
FScopedPreAnimatedCaptureSource::FScopedPreAnimatedCaptureSource(FMovieScenePreAnimatedState* InPreAnimatedState, UMovieSceneTrackInstance* InTrackInstance, bool bInWantsRestoreState)
	: Variant(TInPlaceType<UMovieSceneTrackInstance*>(), InTrackInstance)
	, PreAnimatedState(InPreAnimatedState)
	, PrevCaptureSource(PreAnimatedState->CaptureSource)
	, bWantsRestoreState(bInWantsRestoreState)
{
	InPreAnimatedState->CaptureSource = this;
}
FScopedPreAnimatedCaptureSource::~FScopedPreAnimatedCaptureSource()
{
	PreAnimatedState->CaptureSource = PrevCaptureSource;
}

FMovieScenePreAnimatedState::~FMovieScenePreAnimatedState()
{
}

void FMovieScenePreAnimatedState::Initialize(UMovieSceneEntitySystemLinker* Linker, UE::MovieScene::FInstanceHandle InInstanceHandle)
{
	using namespace UE::MovieScene;

	WeakExtension = nullptr;
	EntityExtensionRef = nullptr;
	WeakObjectStorage = nullptr;
	WeakMasterStorage = nullptr;
	TemplateMetaData = nullptr;
	EvaluationHookMetaData = nullptr;

	WeakLinker = Linker;
	InstanceHandle = InInstanceHandle;
}

void FMovieScenePreAnimatedState::OnEnableGlobalCapture(TSharedPtr<UE::MovieScene::FPreAnimatedStateExtension> InExtension)
{
	InitializeStorage(InExtension);
}

void FMovieScenePreAnimatedState::OnDisableGlobalCapture()
{
	if (!EntityExtensionRef)
	{
		WeakObjectStorage = nullptr;
		WeakMasterStorage = nullptr;

		WeakExtension = nullptr;
	}
}

void FMovieScenePreAnimatedState::ConditionalInitializeEntityStorage(bool bOverrideWantsRestoreState)
{
	using namespace UE::MovieScene;

	if (bOverrideWantsRestoreState && EntityExtensionRef == nullptr)
	{
		if (UMovieSceneEntitySystemLinker* Linker = WeakLinker.Get())
		{
			FPreAnimatedStateExtension* Extension = Linker->FindExtension<FPreAnimatedStateExtension>();
			if (Extension)
			{
				EntityExtensionRef = Extension->AsShared();
			}
			else
			{
				EntityExtensionRef = MakeShared<FPreAnimatedStateExtension>(Linker);
			}
		}

		if (EntityExtensionRef)
		{
			InitializeStorage(EntityExtensionRef);
		}
	}
}

void FMovieScenePreAnimatedState::InitializeStorage(TSharedPtr<UE::MovieScene::FPreAnimatedStateExtension> Extension)
{
	using namespace UE::MovieScene;

	WeakExtension = Extension;
	WeakObjectStorage = Extension->GetOrCreateStorage<FAnimTypePreAnimatedStateObjectStorage>();
	WeakMasterStorage = Extension->GetOrCreateStorage<FAnimTypePreAnimatedStateMasterStorage>();
}

void FMovieScenePreAnimatedState::AddSourceMetaData(const UE::MovieScene::FPreAnimatedStateEntry& Entry)
{
	using namespace UE::MovieScene;

	TSharedPtr<FPreAnimatedStateExtension> Extension = WeakExtension.Pin();
	if (!Extension)
	{
		return;
	}

	if (!CaptureSource)
	{
		Extension->EnsureMetaData(Entry);
		return;
	}

	FPreAnimatedStateMetaData MetaData;
	MetaData.Entry = Entry;
	MetaData.RootInstanceHandle = InstanceHandle;
	MetaData.bWantsRestoreState = CaptureSource->bWantsRestoreState;

	if (FMovieSceneEvaluationKey* EvalKey = CaptureSource->Variant.TryGet<FMovieSceneEvaluationKey>())
	{
		// Make the association to this track template key
		if (!TemplateMetaData)
		{
			TemplateMetaData = MakeShared<FPreAnimatedTemplateCaptureSources>(Extension.Get());
			Extension->AddWeakCaptureSource(TemplateMetaData);
		}
		TemplateMetaData->BeginTrackingCaptureSource(*EvalKey, MetaData);
	}
	else if (FScopedPreAnimatedCaptureSource::FEvalHookType* EvalHook = CaptureSource->Variant.TryGet<FScopedPreAnimatedCaptureSource::FEvalHookType>())
	{
		if (!EvaluationHookMetaData)
		{
			EvaluationHookMetaData = MakeShared<FPreAnimatedEvaluationHookCaptureSources>(Extension.Get());
			Extension->AddWeakCaptureSource(EvaluationHookMetaData);
		}
		EvaluationHookMetaData->BeginTrackingCaptureSource(EvalHook->EvalHook, EvalHook->SequenceID, MetaData);
	}
	else if (UMovieSceneTrackInstance* const * TrackInstance = CaptureSource->Variant.TryGet<UMovieSceneTrackInstance*>())
	{
		// Track instance meta-data is shared between all players
		FPreAnimatedTrackInstanceCaptureSources* TrackInstanceMetaData = Extension->GetOrCreateTrackInstanceMetaData();
		TrackInstanceMetaData->BeginTrackingCaptureSource(*TrackInstance, MetaData);
	}
}

void FMovieScenePreAnimatedState::SavePreAnimatedState(UObject& InObject, FMovieSceneAnimTypeID InTokenType, const IMovieScenePreAnimatedTokenProducer& Producer)
{
	using namespace UE::MovieScene;

	TSharedPtr<FAnimTypePreAnimatedStateObjectStorage> ObjectStorage = WeakObjectStorage.Pin();

	const bool bWantsRestoreState = CaptureSource && CaptureSource->bWantsRestoreState;
	if (!bWantsRestoreState && !ObjectStorage)
	{
		return;
	}

	ConditionalInitializeEntityStorage(bWantsRestoreState);
	if (!ObjectStorage)
	{
		// Re-resolve the ptr as it may have changed inside ConditionalInitializeEntityStorage
		ObjectStorage = WeakObjectStorage.Pin();
	}

	if (ObjectStorage)
	{
		FPreAnimatedStateEntry   Entry        = ObjectStorage->MakeEntry(&InObject, InTokenType);
		FPreAnimatedStorageIndex StorageIndex = Entry.ValueHandle.StorageIndex;

		AddSourceMetaData(Entry);

		EPreAnimatedStorageRequirement Requirement = bWantsRestoreState
			? EPreAnimatedStorageRequirement::Transient
			: EPreAnimatedStorageRequirement::Persistent;

		if (!ObjectStorage->IsStorageRequirementSatisfied(StorageIndex, Requirement))
		{
			IMovieScenePreAnimatedTokenPtr Token = Producer.CacheExistingState(InObject);
			if (Token.IsValid())
			{
				const bool bHasEverAninmated = ObjectStorage->HasEverAnimated(StorageIndex);
				if (!bHasEverAninmated)
				{
					Producer.InitializeObjectForAnimation(InObject);
				}

				ObjectStorage->AssignPreAnimatedValue(StorageIndex, Requirement, MoveTemp(Token));
			}
		}
	}
}

void FMovieScenePreAnimatedState::SavePreAnimatedState(FMovieSceneAnimTypeID InTokenType, const IMovieScenePreAnimatedGlobalTokenProducer& Producer)
{
	using namespace UE::MovieScene;

	TSharedPtr<FAnimTypePreAnimatedStateMasterStorage> MasterStorage = WeakMasterStorage.Pin();

	const bool bWantsRestoreState = CaptureSource && CaptureSource->bWantsRestoreState;
	if (!bWantsRestoreState && !MasterStorage)
	{
		return;
	}

	ConditionalInitializeEntityStorage(bWantsRestoreState);
	if (!MasterStorage)
	{
		// Re-resolve the ptr as it may have changed inside ConditionalInitializeEntityStorage
		MasterStorage = WeakMasterStorage.Pin();
	}

	if (MasterStorage)
	{
		FPreAnimatedStateEntry   Entry        = MasterStorage->MakeEntry(InTokenType);
		FPreAnimatedStorageIndex StorageIndex = Entry.ValueHandle.StorageIndex;

		AddSourceMetaData(Entry);

		EPreAnimatedStorageRequirement Requirement = bWantsRestoreState
			? EPreAnimatedStorageRequirement::Transient
			: EPreAnimatedStorageRequirement::Persistent;

		if (!MasterStorage->IsStorageRequirementSatisfied(StorageIndex, Requirement))
		{
			IMovieScenePreAnimatedGlobalTokenPtr Token = Producer.CacheExistingState();
			if (Token.IsValid())
			{
				const bool bHasEverAninmated = MasterStorage->HasEverAnimated(StorageIndex);
				if (!bHasEverAninmated)
				{
					Producer.InitializeForAnimation();
				}

				MasterStorage->AssignPreAnimatedValue(StorageIndex, Requirement, MoveTemp(Token));
			}
		}
	}
}

void FMovieScenePreAnimatedState::RestorePreAnimatedState()
{
	using namespace UE::MovieScene;

	UMovieSceneEntitySystemLinker* Linker = WeakLinker.Get();
	if (Linker)
	{
		TSharedPtr<FPreAnimatedStateExtension> Extension = WeakExtension.Pin();
		if (Extension)
		{
			Extension->RestoreGlobalState(FRestoreStateParams{Linker, InstanceHandle});
		}
	}
}

void FMovieScenePreAnimatedState::OnFinishedEvaluating(const FMovieSceneEvaluationKey& Key)
{
	if (TemplateMetaData)
	{
		TemplateMetaData->StopTrackingCaptureSource(Key);
	}
}

void FMovieScenePreAnimatedState::OnFinishedEvaluating(const UObject* EvaluationHook, FMovieSceneSequenceID SequenceID)
{
	if (EvaluationHookMetaData)
	{
		EvaluationHookMetaData->StopTrackingCaptureSource(EvaluationHook, SequenceID);
	}
}

void FMovieScenePreAnimatedState::RestorePreAnimatedState(UObject& Object)
{
	using namespace UE::MovieScene;

	UMovieSceneEntitySystemLinker*         Linker    = WeakLinker.Get();
	TSharedPtr<FPreAnimatedStateExtension> Extension = WeakExtension.Pin();
	if (!Linker || !Extension)
	{
		return;
	}

	TSharedPtr<FPreAnimatedObjectGroupManager> ObjectGroupManager = Extension->FindGroupManager<FPreAnimatedObjectGroupManager>();
	if (!ObjectGroupManager)
	{
		return;
	}

	FPreAnimatedStorageGroupHandle Group = ObjectGroupManager->FindGroupForObject(&Object);
	if (!Group)
	{
		return;
	}

	Extension->RestoreStateForGroup(Group, FRestoreStateParams{Linker, InstanceHandle});
}

void FMovieScenePreAnimatedState::RestorePreAnimatedState(UClass* GeneratedClass)
{
	using namespace UE::MovieScene;

	UMovieSceneEntitySystemLinker*         Linker    = WeakLinker.Get();
	TSharedPtr<FPreAnimatedStateExtension> Extension = WeakExtension.Pin();
	if (!Linker || !Extension)
	{
		return;
	}

	TSharedPtr<FPreAnimatedObjectGroupManager> ObjectGroupManager = Extension->FindGroupManager<FPreAnimatedObjectGroupManager>();
	if (ObjectGroupManager)
	{
		TArray<FPreAnimatedStorageGroupHandle> Handles;
		ObjectGroupManager->GetGroupsByClass(GeneratedClass, Handles);

		FRestoreStateParams Params{Linker, InstanceHandle};
		for (FPreAnimatedStorageGroupHandle GroupHandle : Handles)
		{
			Extension->RestoreStateForGroup(GroupHandle, Params);
		}
	}
}


void FMovieScenePreAnimatedState::RestorePreAnimatedState(UObject& Object, TFunctionRef<bool(FMovieSceneAnimTypeID)> InFilter)
{
	using namespace UE::MovieScene;

	TSharedPtr<FAnimTypePreAnimatedStateObjectStorage> ObjectStorage = WeakObjectStorage.Pin();
	if (!ObjectStorage)
	{
		return;
	}

	struct FRestoreMask : FAnimTypePreAnimatedStateObjectStorage::IRestoreMask
	{
		TFunctionRef<bool(FMovieSceneAnimTypeID)>* Filter;

		virtual bool CanRestore(const FPreAnimatedObjectTokenTraits::FAnimatedKey& InKey) const override
		{
			return (*Filter)(InKey.AnimTypeID);
		}
	} RestoreMask;
	RestoreMask.Filter = &InFilter;

	ObjectStorage->SetRestoreMask(&RestoreMask);

	RestorePreAnimatedState(Object);

	ObjectStorage->SetRestoreMask(nullptr);
}

void FMovieScenePreAnimatedState::DiscardEntityTokens()
{
	using namespace UE::MovieScene;

	TSharedPtr<FPreAnimatedStateExtension> Extension = WeakExtension.Pin();
	if (Extension)
	{
		Extension->DiscardTransientState();
	}
}

void FMovieScenePreAnimatedState::DiscardAndRemoveEntityTokensForObject(UObject& Object)
{
	using namespace UE::MovieScene;

	UMovieSceneEntitySystemLinker*         Linker    = WeakLinker.Get();
	TSharedPtr<FPreAnimatedStateExtension> Extension = WeakExtension.Pin();

	if (!Linker || !Extension)
	{
		return;
	}

	TSharedPtr<FPreAnimatedObjectGroupManager> ObjectGroupManager = Extension->FindGroupManager<FPreAnimatedObjectGroupManager>();
	if (!ObjectGroupManager)
	{
		return;
	}

	FPreAnimatedStorageGroupHandle Group = ObjectGroupManager->FindGroupForObject(&Object);
	if (!Group)
	{
		return;
	}

	Extension->DiscardStateForGroup(Group);
}

void FMovieScenePreAnimatedState::OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap)
{
	using namespace UE::MovieScene;

	TSharedPtr<FPreAnimatedStateExtension> Extension = WeakExtension.Pin();
	if (!Extension)
	{
		return;
	}

	TSharedPtr<FPreAnimatedObjectGroupManager> ObjectGroupManager = Extension->FindGroupManager<FPreAnimatedObjectGroupManager>();
	if (ObjectGroupManager)
	{
		ObjectGroupManager->OnObjectsReplaced(ReplacementMap);
	}
}

bool FMovieScenePreAnimatedState::ContainsAnyStateForSequence() const
{
	using namespace UE::MovieScene;

	TSharedPtr<FPreAnimatedStateExtension> Extension = WeakExtension.Pin();
	return Extension && InstanceHandle.IsValid() && Extension->ContainsAnyStateForInstanceHandle(InstanceHandle);
}