// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MovieSceneEntityIDs.h"
#include "MovieSceneSequenceID.h"
#include "Evaluation/MovieScenePlayback.h"
#include "EntitySystem/MovieSceneEntityManager.h"
#include "EntitySystem/MovieSceneInstanceRegistry.h"
#include "Tickable.h"
#include "UObject/ObjectKey.h"
#include "Async/TaskGraphInterfaces.h"
#include "EntitySystem/MovieSceneEntitySystemTypes.h"
#include "EntitySystem/MovieSceneEntitySystemGraphs.h"
#include "EntitySystem/MovieSceneSequenceInstance.h"

#include "MovieSceneEntitySystemLinker.generated.h"

class FMovieSceneEntitySystemRunner;
class UMovieSceneEntitySystem;
class UMovieSceneCompiledDataManager;

namespace UE
{
namespace MovieScene
{
	struct FComponentRegistry;
	enum class EEntitySystemContext : uint8;

	enum class EAutoLinkRelevantSystems : uint8
	{
		Enabled,
		Disable,
	};
}
}

DECLARE_MULTICAST_DELEGATE_OneParam(FMovieSceneEntitySystemLinkerEvent, UMovieSceneEntitySystemLinker*);
DECLARE_MULTICAST_DELEGATE_TwoParams(FMovieSceneEntitySystemLinkerAROEvent, UMovieSceneEntitySystemLinker*, FReferenceCollector&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FMovieSceneEntitySystemLinkerWorldEvent, UMovieSceneEntitySystemLinker*, UWorld*);

UCLASS()
class MOVIESCENE_API UMovieSceneEntitySystemLinker
	: public UObject
{
public:

	template<typename T>
	using TComponentTypeID    = UE::MovieScene::TComponentTypeID<T>;
	using FEntityManager      = UE::MovieScene::FEntityManager;
	using FComponentTypeID    = UE::MovieScene::FComponentTypeID;
	using FInstanceHandle     = UE::MovieScene::FInstanceHandle;
	using FMovieSceneEntityID = UE::MovieScene::FMovieSceneEntityID;
	using FInstanceRegistry   = UE::MovieScene::FInstanceRegistry;
	using FComponentRegistry  = UE::MovieScene::FComponentRegistry;

	FEntityManager EntityManager;

	UPROPERTY()
	FMovieSceneEntitySystemGraph SystemGraph;

public:

	GENERATED_BODY()

	UMovieSceneEntitySystemLinker(const FObjectInitializer& ObjInit);

	static FComponentRegistry* GetComponents();

	static UMovieSceneEntitySystemLinker* FindOrCreateLinker(UObject* PreferredOuter, const TCHAR* Name = TEXT("DefaultMovieSceneEntitySystemLinker"));
	static UMovieSceneEntitySystemLinker* CreateLinker(UObject* PreferredOuter);

	FInstanceRegistry* GetInstanceRegistry()
	{
		check(InstanceRegistry.IsValid());
		return InstanceRegistry.Get();
	}

	const FInstanceRegistry* GetInstanceRegistry() const
	{
		check(InstanceRegistry.IsValid());
		return InstanceRegistry.Get();
	}

	void FinishInstance(FInstanceHandle InstanceHandle);

	template<typename SystemType>
	SystemType* LinkSystem()
	{
		return CastChecked<SystemType>(LinkSystem(SystemType::StaticClass()));
	}

	template<typename SystemType>
	SystemType* FindSystem() const
	{
		return CastChecked<SystemType>(FindSystem(SystemType::StaticClass()), ECastCheckedType::NullAllowed);
	}

	UMovieSceneEntitySystem* LinkSystem(TSubclassOf<UMovieSceneEntitySystem> InClassType);
	UMovieSceneEntitySystem* FindSystem(TSubclassOf<UMovieSceneEntitySystem> Class) const;

	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	bool ShouldCaptureGlobalState() const
	{
		return GlobalStateCaptureToken.Pin().IsValid();
	}

	TSharedRef<bool> CaptureGlobalState();


	/**
	 * Retrieve this linker's context, specifying what kinds of systems should be allowed or disallowed
	 */
	UE::MovieScene::EEntitySystemContext GetSystemContext() const
	{
		return SystemContext;
	}


	/**
	 * Set the system context for this linker allowing some systems to be excluded based on the context.
	 *
	 * @param InSystemContext    The new system context for this linker
	 */
	void SetSystemContext(UE::MovieScene::EEntitySystemContext InSystemContext)
	{
		SystemContext = InSystemContext;
	}


	/**
	 * Completely reset this linker back to its default state, abandoning all systems and destroying all entities
	 */
	void Reset();

public:

	// Internal API
	
	void SystemLinked(UMovieSceneEntitySystem* InSystem);
	void SystemUnlinked(UMovieSceneEntitySystem* InSystem);

	bool HasLinkedSystem(const uint16 GlobalDependencyGraphID);

	void LinkRelevantSystems();
	void AutoLinkRelevantSystems();

	void InvalidateObjectBinding(const FGuid& ObjectBindingID, FInstanceHandle InstanceHandle);
	void CleanupInvalidBoundObjects();

	bool StartEvaluation(FMovieSceneEntitySystemRunner& InRunner);
	FMovieSceneEntitySystemRunner* GetActiveRunner() const;
	void EndEvaluation(FMovieSceneEntitySystemRunner& InRunner);

private:

	void HandlePostGarbageCollection();

	void TagInvalidBoundObjects();
	void CleanGarbage();

	void OnWorldCleanup(UWorld* InWorld, bool bSessionEnded, bool bCleanupResources);

	virtual void BeginDestroy() override;

private:

	TUniquePtr<FInstanceRegistry> InstanceRegistry;

	TSparseArray<UMovieSceneEntitySystem*> EntitySystemsByGlobalGraphID;

	struct FActiveRunnerInfo
	{
		FMovieSceneEntitySystemRunner* Runner;
		bool bIsReentrancyAllowed;
	};
	TArray<FActiveRunnerInfo> ActiveRunners;

	friend struct FMovieSceneEntitySystemEvaluationReentrancyWindow;

public:

	struct
	{
		FMovieSceneEntitySystemLinkerEvent      TagGarbage;
		FMovieSceneEntitySystemLinkerEvent      CleanTaggedGarbage;
		FMovieSceneEntitySystemLinkerAROEvent   AddReferencedObjects;
		FMovieSceneEntitySystemLinkerEvent      AbandonLinker;
		FMovieSceneEntitySystemLinkerWorldEvent CleanUpWorld;
	} Events;

private:

	uint64 LastSystemLinkVersion;

	TWeakPtr<bool> GlobalStateCaptureToken;

protected:

	UE::MovieScene::EAutoLinkRelevantSystems AutoLinkMode;
	UE::MovieScene::EEntitySystemContext SystemContext;
};

/**
 * Structure for making it possible to make re-entrant evaluation on a linker.
 */
struct FMovieSceneEntitySystemEvaluationReentrancyWindow
{
	UMovieSceneEntitySystemLinker& Linker;
	int32 CurrentLevel;

	FMovieSceneEntitySystemEvaluationReentrancyWindow(UMovieSceneEntitySystemLinker& InLinker);
	~FMovieSceneEntitySystemEvaluationReentrancyWindow();
};
