// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Map.h"
#include "Templates/UniquePtr.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "MovieSceneSequenceID.h"
#include "Evaluation/MovieSceneRootOverridePath.h"
#include "Compilation/MovieSceneCompiledDataID.h"
#include "EntitySystem/MovieSceneEntitySystemRunner.h"
#include "EntitySystem/MovieSceneSequenceInstanceHandle.h"
#include "MovieSceneEvaluationTemplateInstance.generated.h"

struct FMovieSceneContext;
struct FMovieSceneSubSequenceData;
struct FMovieSceneBlendingAccumulator;

class IMovieScenePlayer;
class UMovieSceneSequence;
class UMovieSceneEntitySystemLinker;
class UMovieSceneCompiledDataManager;

namespace UE { namespace MovieScene { struct FSequenceInstance; } }

/**
 * Root evaluation template instance used to play back any sequence
 */
USTRUCT()
struct FMovieSceneRootEvaluationTemplateInstance
{
public:
	GENERATED_BODY()

	MOVIESCENE_API FMovieSceneRootEvaluationTemplateInstance();

	MOVIESCENE_API ~FMovieSceneRootEvaluationTemplateInstance();

	/**
	 * Initialize this template instance with the specified sequence
	 *
	 * @param RootSequence				The sequence play back
	 * @param Player					The player responsible for playback
	 * @param TemplateStore				Template store responsible for supplying templates for a given sequence
	 */
	MOVIESCENE_API void Initialize(UMovieSceneSequence& RootSequence, IMovieScenePlayer& Player, UMovieSceneCompiledDataManager* InCompiledDataManager);

	/**
	 * Evaluate this sequence in a synchronous way.
	 *
	 * @param Context				Evaluation context containing the time (or range) to evaluate
	 * @param Player				The player responsible for playback
	 */
	MOVIESCENE_API void Evaluate(FMovieSceneContext Context, IMovieScenePlayer& Player);

	/**
	 * Indicate that we're not going to evaluate this instance again, and that we should tear down any current state
	 *
	 * @param Player				The player responsible for playback
	 */
	MOVIESCENE_API void Finish(IMovieScenePlayer& Player);

	bool IsValid() const
	{
		return CompiledDataManager && WeakRootSequence.Get();
	}

	UE::MovieScene::FInstanceHandle GetRootInstanceHandle() const
	{
		return RootInstanceHandle;
	}

	UMovieSceneSequence* GetRootSequence() const
	{
		return WeakRootSequence.Get();
	}

	FMovieSceneCompiledDataID GetCompiledDataID() const
	{
		return CompiledDataID;
	}

	UMovieSceneCompiledDataManager* GetCompiledDataManager() const
	{
		return CompiledDataManager;
	}

	MOVIESCENE_API bool HasEverUpdated() const;

	MOVIESCENE_API UMovieSceneEntitySystemLinker* GetEntitySystemLinker() const;

	MOVIESCENE_API FMovieSceneEntitySystemRunner& GetEntitySystemRunner();

	MOVIESCENE_API const FMovieSceneSequenceHierarchy* GetHierarchy() const;

	MOVIESCENE_API void GetSequenceParentage(const UE::MovieScene::FInstanceHandle InstanceHandle, TArray<UE::MovieScene::FInstanceHandle>& OutParentHandles) const;

	MOVIESCENE_API UE::MovieScene::FSequenceInstance* FindInstance(FMovieSceneSequenceID SequenceID);

	MOVIESCENE_API const UE::MovieScene::FSequenceInstance* FindInstance(FMovieSceneSequenceID SequenceID) const;

	MOVIESCENE_API UE::MovieScene::FMovieSceneEntityID FindEntityFromOwner(UObject* Owner, uint32 EntityID, FMovieSceneSequenceID SequenceID) const;

	MOVIESCENE_API UMovieSceneSequence* GetSequence(FMovieSceneSequenceIDRef SequenceID) const;

	MOVIESCENE_API UObject* GetOrCreateDirectorInstance(FMovieSceneSequenceIDRef SequenceID, IMovieScenePlayer& Player);

	MOVIESCENE_API void PlaybackContextChanged(IMovieScenePlayer& Player);

	MOVIESCENE_API const FMovieSceneSubSequenceData* FindSubData(FMovieSceneSequenceIDRef SequenceID) const;

	MOVIESCENE_API void CopyActuators(FMovieSceneBlendingAccumulator& Accumulator) const;

	MOVIESCENE_API void BeginDestroy();

#if WITH_EDITOR
	MOVIESCENE_API void SetEmulatedNetworkMask(EMovieSceneServerClientMask InNewMask, IMovieScenePlayer& Player);
	MOVIESCENE_API EMovieSceneServerClientMask GetEmulatedNetworkMask() const;
#endif

private:

	static UMovieSceneEntitySystemLinker* ConstructEntityLinker(IMovieScenePlayer& Player);

private:

	UPROPERTY()
	TWeakObjectPtr<UMovieSceneSequence> WeakRootSequence;

	UPROPERTY()
	TObjectPtr<UMovieSceneCompiledDataManager> CompiledDataManager;

	UE::MovieScene::FInstanceHandle RootInstanceHandle;

	UPROPERTY()
	TObjectPtr<UMovieSceneEntitySystemLinker> EntitySystemLinker;

	FMovieSceneEntitySystemRunner EntitySystemRunner;

	/** Map of director instances by sequence ID. Kept alive by this map assuming this struct is reference collected */
	UPROPERTY()
	TMap<FMovieSceneSequenceID, TObjectPtr<UObject>> DirectorInstances;

	FMovieSceneSequenceID RootID;

	FMovieSceneCompiledDataID CompiledDataID;

#if WITH_EDITOR
	EMovieSceneServerClientMask EmulatedNetworkMask;
#endif
};

template<>
struct TStructOpsTypeTraits<FMovieSceneRootEvaluationTemplateInstance> : TStructOpsTypeTraitsBase2<FMovieSceneRootEvaluationTemplateInstance>
{
	enum { WithCopy = false };
};

