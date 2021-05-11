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
#include "EntitySystem/MovieSceneEntitySystemLinkerExtension.h"

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

	/**
	 * Register a new extension type for use with any instance of a UMovieSceneEntitySystemLinker
	 */
	template<typename ExtensionType>
	static UE::MovieScene::TEntitySystemLinkerExtensionID<ExtensionType> RegisterExtension()
	{
		return UE::MovieScene::TEntitySystemLinkerExtensionID<ExtensionType>(RegisterExtension().ID);
	}

	/**
	 * Add an extension to this linker.
	 *
	 * @param InID        The unique identifier for the type of extension (retrieved from RegisterExtension)
	 * @param InExtension Pointer to the extension to register - must be kept alive externally - only a raw ptr is kept in this class
	 */
	template<typename ExtensionType>
	void AddExtension(UE::MovieScene::TEntitySystemLinkerExtensionID<ExtensionType> InID, ExtensionType* InExtension)
	{
		const int32 Index = InID.ID;
		if (!ExtensionsByID.IsValidIndex(Index))
		{
			ExtensionsByID.Insert(Index, InExtension);
		}
		else
		{
			check(ExtensionsByID[Index] == InExtension);
		}
	}

	/**
	 * Add an extension to this linker.
	 *
	 * @param InID        The unique identifier for the type of extension (retrieved from RegisterExtension)
	 * @param InExtension Pointer to the extension to register - must be kept alive externally - only a raw ptr is kept in this class
	 */
	template<typename ExtensionType>
	void AddExtension(ExtensionType* InExtension)
	{
		AddExtension(ExtensionType::GetExtensionID(), InExtension);
	}

	/**
	 * Attempt to find an extension to this linker by its ID
	 *
	 * @param InID        The unique identifier for the type of extension (retrieved from RegisterExtension)
	 * @return A pointer to the extension, or nullptr if it is not active.
	 */
	template<typename ExtensionType>
	ExtensionType* FindExtension(UE::MovieScene::TEntitySystemLinkerExtensionID<ExtensionType> InID) const
	{
		const int32 Index = InID.ID;
		if (ExtensionsByID.IsValidIndex(Index))
		{
			return static_cast<ExtensionType*>(ExtensionsByID[Index]);
		}

		return nullptr;
	}


	/**
	 * Attempt to find an extension to this linker by its ID
	 *
	 * @param InID        The unique identifier for the type of extension (retrieved from RegisterExtension)
	 * @return A pointer to the extension, or nullptr if it is not active.
	 */
	template<typename ExtensionType>
	ExtensionType* FindExtension() const
	{
		const int32 Index = ExtensionType::GetExtensionID().ID;
		if (ExtensionsByID.IsValidIndex(Index))
		{
			return static_cast<ExtensionType*>(ExtensionsByID[Index]);
		}

		return nullptr;
	}


	/**
	 * Remove an extension, if it exists
	 *
	 * @param InID        The unique identifier for the type of extension (retrieved from RegisterExtension)
	 */
	void RemoveExtension(UE::MovieScene::FEntitySystemLinkerExtensionID ExtensionID)
	{
		const int32 Index = ExtensionID.ID;
		if (ExtensionsByID.IsValidIndex(Index))
		{
			ExtensionsByID.RemoveAt(Index);
		}
	}

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

	static UE::MovieScene::FEntitySystemLinkerExtensionID RegisterExtension();

private:

	TUniquePtr<FInstanceRegistry> InstanceRegistry;

	TSparseArray<UMovieSceneEntitySystem*> EntitySystemsByGlobalGraphID;

	struct FActiveRunnerInfo
	{
		FMovieSceneEntitySystemRunner* Runner;
		bool bIsReentrancyAllowed;
	};
	TArray<FActiveRunnerInfo> ActiveRunners;

	TSparseArray<void*> ExtensionsByID;

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
