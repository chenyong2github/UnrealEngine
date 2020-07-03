// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"
#include "Containers/SortedMap.h"
#include "IMovieScenePlayer.h"
#include "MovieSceneSequence.h"
#include "MovieSceneSequence.h"
#include "Compilation/MovieSceneCompiledDataManager.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneEntitySystemRunner.h"
#include "Evaluation/Instances/MovieSceneTrackEvaluator.h"

#include "Sections/MovieSceneSubSection.h"

#include "UObject/Package.h"
#include "Engine/World.h"

DECLARE_CYCLE_STAT(TEXT("Entire Evaluation Cost"), MovieSceneEval_EntireEvaluationCost, STATGROUP_MovieSceneEval);


FMovieSceneRootEvaluationTemplateInstance::FMovieSceneRootEvaluationTemplateInstance()
	: CompiledDataManager(nullptr)
	, EntitySystemLinker(nullptr)
	, RootID(MovieSceneSequenceID::Root)
{
}

void FMovieSceneRootEvaluationTemplateInstance::BeginDestroy()
{
	if (EntitySystemLinker && !EntitySystemLinker->IsPendingKill() && !EntitySystemLinker->HasAnyFlags(RF_BeginDestroyed))
	{
		EntitySystemLinker->GetInstanceRegistry()->DestroyInstance(RootInstanceHandle);
	}

	CompiledDataManager = nullptr;
	EntitySystemLinker = nullptr;
}

FMovieSceneRootEvaluationTemplateInstance::~FMovieSceneRootEvaluationTemplateInstance()
{
	BeginDestroy();
}

UMovieSceneEntitySystemLinker* FMovieSceneRootEvaluationTemplateInstance::ConstructEntityLinker(IMovieScenePlayer& Player)
{
	UMovieSceneSequence* RootSequence = Player.GetEvaluationTemplate().GetRootSequence();
	check(RootSequence);

	UMovieSceneEntitySystemLinker* Linker = RootSequence->GetEntitySystemLinker(Player);
	if (Linker)
	{
		return Linker;
	}

	if (EnumHasAnyFlags(RootSequence->GetFlags(), EMovieSceneSequenceFlags::BlockingEvaluation))
	{
		Linker = NewObject<UMovieSceneEntitySystemLinker>(GetTransientPackage());
		return Linker;
	}

	UObject* PlaybackContext = Player.GetPlaybackContext();
	return UGlobalEntitySystemLinker::Get(PlaybackContext);
}

void FMovieSceneRootEvaluationTemplateInstance::Initialize(UMovieSceneSequence& InRootSequence, IMovieScenePlayer& Player, UMovieSceneCompiledDataManager* InCompiledDataManager)
{
	bool bReinitialize = (WeakRootSequence.Get() == nullptr);

	const UMovieSceneCompiledDataManager* PreviousCompiledDataManager = CompiledDataManager;
	if (InCompiledDataManager)
	{
		CompiledDataManager = InCompiledDataManager;
	}
	else
	{
		CompiledDataManager = UMovieSceneCompiledDataManager::GetPrecompiledData();
	}
	bReinitialize |= (PreviousCompiledDataManager != CompiledDataManager);

	if (UMovieSceneSequence* ExistingSequence = WeakRootSequence.Get())
	{
		if (ExistingSequence != &InRootSequence)
		{
			Finish(Player);

			bReinitialize = true;
		}
	}

	CompiledDataID = CompiledDataManager->GetDataID(&InRootSequence);
	WeakRootSequence = &InRootSequence;
	RootID = MovieSceneSequenceID::Root;

	if (bReinitialize)
	{
		if (RootInstanceHandle.IsValid() && EntitySystemLinker)
		{
			EntitySystemLinker->GetInstanceRegistry()->DestroyInstance(RootInstanceHandle);
		}

		Player.State.PersistentEntityData.Reset();
		Player.State.PersistentSharedData.Reset();

		if (EntitySystemRunner.IsAttachedToLinker())
		{
			EntitySystemRunner.DetachFromLinker();
		}
		EntitySystemLinker = ConstructEntityLinker(Player);
		EntitySystemRunner.AttachToLinker(EntitySystemLinker);
		RootInstanceHandle = EntitySystemLinker->GetInstanceRegistry()->AllocateRootInstance(&Player);
	}
}

void FMovieSceneRootEvaluationTemplateInstance::Evaluate(FMovieSceneContext Context, IMovieScenePlayer& Player, FMovieSceneSequenceID OverrideRootID)
{
	SCOPE_CYCLE_COUNTER(MovieSceneEval_EntireEvaluationCost);

	check(EntitySystemLinker);

	if (EntitySystemRunner.IsAttachedToLinker())
	{
		EntitySystemRunner.Update(Context, RootInstanceHandle, OverrideRootID);
	}
}

void FMovieSceneRootEvaluationTemplateInstance::Finish(IMovieScenePlayer& Player)
{
	if (EntitySystemRunner.IsAttachedToLinker())
	{
		EntitySystemRunner.FinishInstance(RootInstanceHandle);
	}

	DirectorInstances.Reset();
}

UMovieSceneSequence* FMovieSceneRootEvaluationTemplateInstance::GetSequence(FMovieSceneSequenceIDRef SequenceID) const
{
	if (SequenceID == MovieSceneSequenceID::Root)
	{
		return WeakRootSequence.Get();
	}
	else if (CompiledDataID.IsValid())
	{
		const FMovieSceneSequenceHierarchy* Hierarchy       = CompiledDataManager->FindHierarchy(CompiledDataID);
		const FMovieSceneSubSequenceData*   SubSequenceData = Hierarchy ? Hierarchy->FindSubData(SequenceID) : nullptr;
		if (SubSequenceData)
		{
			return SubSequenceData->GetSequence();
		}
	}
	return nullptr;
}

UMovieSceneEntitySystemLinker* FMovieSceneRootEvaluationTemplateInstance::GetEntitySystemLinker() const
{
	return EntitySystemLinker;
}

FMovieSceneEntitySystemRunner& FMovieSceneRootEvaluationTemplateInstance::GetEntitySystemRunner()
{
	return EntitySystemRunner;
}

bool FMovieSceneRootEvaluationTemplateInstance::HasEverUpdated() const
{
	if (EntitySystemLinker)
	{
		const UE::MovieScene::FSequenceInstance* SequenceInstance = &EntitySystemLinker->GetInstanceRegistry()->GetInstance(RootInstanceHandle);
		return SequenceInstance && SequenceInstance->HasEverUpdated();
	}

	return false;
}

const UE::MovieScene::FSequenceInstance* FMovieSceneRootEvaluationTemplateInstance::FindInstance(FMovieSceneSequenceID SequenceID) const
{
	using namespace UE::MovieScene;

	const FSequenceInstance* SequenceInstance = &EntitySystemLinker->GetInstanceRegistry()->GetInstance(RootInstanceHandle);

	if (SequenceID == MovieSceneSequenceID::Root)
	{
		return SequenceInstance;
	}

	FInstanceHandle SubHandle = SequenceInstance->FindSubInstance(SequenceID);
	if (SubHandle.IsValid())
	{
		return &EntitySystemLinker->GetInstanceRegistry()->GetInstance(SubHandle);
	}

	return nullptr;
}

UE::MovieScene::FMovieSceneEntityID FMovieSceneRootEvaluationTemplateInstance::FindEntityFromOwner(UObject* Owner, uint32 EntityID, FMovieSceneSequenceID SequenceID) const
{
	using namespace UE::MovieScene;

	if (const FSequenceInstance* SequenceInstance = FindInstance(SequenceID))
	{
		return SequenceInstance->FindEntity(Owner, EntityID);
	}

	return FMovieSceneEntityID::Invalid();
}

UObject* FMovieSceneRootEvaluationTemplateInstance::GetOrCreateDirectorInstance(FMovieSceneSequenceIDRef SequenceID, IMovieScenePlayer& Player)
{
	UObject* ExistingDirectorInstance = DirectorInstances.FindRef(SequenceID);
	if (ExistingDirectorInstance)
	{
		return ExistingDirectorInstance;
	}

	UObject* NewDirectorInstance = nullptr;
	if (SequenceID == MovieSceneSequenceID::Root)
	{
		if (UMovieSceneSequence* Sequence = WeakRootSequence.Get())
		{
			NewDirectorInstance = Sequence->CreateDirectorInstance(Player);
		}
	}
	else if (const FMovieSceneSequenceHierarchy* Hierarchy = CompiledDataManager->FindHierarchy(CompiledDataID))
	{
		const FMovieSceneSubSequenceData* SubData = Hierarchy->FindSubData(SequenceID);
		check(SubData);
		NewDirectorInstance = SubData->GetSequence()->CreateDirectorInstance(Player);
	}

	if (NewDirectorInstance)
	{
		DirectorInstances.Add(SequenceID, NewDirectorInstance);
	}

	return NewDirectorInstance;
}

void FMovieSceneRootEvaluationTemplateInstance::PlaybackContextChanged(IMovieScenePlayer& Player)
{
	if (EntitySystemLinker && !EntitySystemLinker->IsPendingKill() && !EntitySystemLinker->HasAnyFlags(RF_BeginDestroyed))
	{
		EntitySystemLinker->CleanupInvalidBoundObjects();

		Finish(Player);
		EntitySystemLinker->GetInstanceRegistry()->DestroyInstance(RootInstanceHandle);
	}

	if (EntitySystemRunner.IsAttachedToLinker())
	{
		EntitySystemRunner.DetachFromLinker();
	}
	EntitySystemLinker = ConstructEntityLinker(Player);
	EntitySystemRunner.AttachToLinker(EntitySystemLinker);

	RootInstanceHandle = EntitySystemLinker->GetInstanceRegistry()->AllocateRootInstance(&Player);
	DirectorInstances.Reset();
}


const FMovieSceneSubSequenceData* FMovieSceneRootEvaluationTemplateInstance::FindSubData(FMovieSceneSequenceIDRef SequenceID) const
{
	const FMovieSceneSequenceHierarchy* Hierarchy = CompiledDataManager->FindHierarchy(CompiledDataID);
	return Hierarchy ? Hierarchy->FindSubData(SequenceID) : nullptr;
}

void FMovieSceneRootEvaluationTemplateInstance::CopyActuators(FMovieSceneBlendingAccumulator& Accumulator) const
{
	using namespace UE::MovieScene;

	const FSequenceInstance& SequenceInstance = EntitySystemLinker->GetInstanceRegistry()->GetInstance(RootInstanceHandle);
	const FMovieSceneTrackEvaluator* LegacyEvaluator = SequenceInstance.GetLegacyEvaluator();

	if (LegacyEvaluator)
	{
		LegacyEvaluator->CopyActuators(Accumulator);
	}
}
