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

namespace UE { namespace MovieScene { struct FComponentRegistry; }}

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
		return static_cast<SystemType*>(SystemGraph.FindSystemOfType(SystemType::StaticClass()));
	}

	UMovieSceneEntitySystem* LinkSystem(TSubclassOf<UMovieSceneEntitySystem> InClassType);

	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	bool ShouldCaptureGlobalState() const
	{
		return GlobalStateCaptureToken.Pin().IsValid();
	}

	TSharedRef<bool> CaptureGlobalState();

public:

	// Internal API
	
	void SystemLinked(UMovieSceneEntitySystem* InSystem);
	void SystemUnlinked(UMovieSceneEntitySystem* InSystem);

	bool HasLinkedSystem(const uint16 GlobalDependencyGraphID);

	void LinkRelevantSystems();

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
