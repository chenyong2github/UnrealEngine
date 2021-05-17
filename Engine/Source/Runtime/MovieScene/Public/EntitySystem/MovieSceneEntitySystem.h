// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/TaskGraphInterfaces.h"
#include "Templates/SubclassOf.h"
#include "EntitySystem/MovieSceneEntityManager.h"
#include "EntitySystem/MovieSceneSequenceInstanceHandle.h"
#include "Misc/EnumClassFlags.h"

#include "MovieSceneEntitySystem.generated.h"

class UMovieSceneEntitySystemLinker;

namespace UE
{
namespace MovieScene
{

	struct FSystemTaskPrerequisites;
	struct FSystemSubsequentTasks;

	enum class EEntitySystemContext : uint8 
	{
		None = 0,

		/** This system is relevant to runtime */
		Runtime = 1 << 0,

		/** This system is relevant to interrogation */
		Interrogation = 1 << 1,
	};
	ENUM_CLASS_FLAGS(EEntitySystemContext)

} // namespace MovieScene
} // namespace UE


UCLASS()
class MOVIESCENE_API UMovieSceneEntitySystem : public UObject
{
public:
	GENERATED_BODY()


	template<typename T>
	using TComponentTypeID = UE::MovieScene::TComponentTypeID<T>;
	using FComponentTypeID = UE::MovieScene::FComponentTypeID;
	using FComponentMask   = UE::MovieScene::FComponentMask;

	using FSystemTaskPrerequisites = UE::MovieScene::FSystemTaskPrerequisites;
	using FSystemSubsequentTasks   = UE::MovieScene::FSystemSubsequentTasks;

	UMovieSceneEntitySystem(const FObjectInitializer& ObjInit);
	~UMovieSceneEntitySystem();

	/**
	 * Creates a relationship between the two system types that ensures any systems of type UpstreamSystemType always execute before DownstreamSystemType if they are both present
	 *
	 * @param UpstreamSystemType     The UClass of the system that should always be a prerequisite of DownstreamSystemType (ie, runs first)
	 * @param DownstreamSystemType   The UClass of the system that should always run after UpstreamSystemType
	 */
	static void DefineImplicitPrerequisite(TSubclassOf<UMovieSceneEntitySystem> UpstreamSystemType, TSubclassOf<UMovieSceneEntitySystem> DownstreamSystemType);

	/**
	 * Informs the dependency graph that the specified class type produces components of the specified type.
	 * Any systems set up as consumers of this component type will always be run after
	 *
	 * @param ClassType         The UClass of the system that produces the component type
	 * @param ComponentType     The type of the component produced by the system
	 */
	static void DefineComponentProducer(TSubclassOf<UMovieSceneEntitySystem> ClassType, FComponentTypeID ComponentType);

	/**
	 * Informs the dependency graph that the specified class type consumes components of the specified type, and as such should always execute after any producers of that component type.
	 *
	 * @param ClassType         The UClass of the system that consumes the component type
	 * @param ComponentType     The type of the component consumed by the system
	 */
	static void DefineComponentConsumer(TSubclassOf<UMovieSceneEntitySystem> ClassType, FComponentTypeID ComponentType);

	/**
	 * Ensure that any systems relevant to the specified linker's entity manager are linked
	 */
	static void LinkRelevantSystems(UMovieSceneEntitySystemLinker* InLinker);

public:

	UE::MovieScene::EEntitySystemContext GetExclusionContext() const
	{
		return SystemExclusionContext;
	}

	UE::MovieScene::ESystemPhase GetPhase() const
	{
		return Phase;
	}

	UMovieSceneEntitySystemLinker* GetLinker() const
	{
		return Linker;
	}

	uint16 GetGraphID() const
	{
		return GraphID;
	}
	void SetGraphID(uint16 InGraphID)
	{
		GraphID = InGraphID;
	}

	uint16 GetGlobalDependencyGraphID() const
	{
		return GlobalDependencyGraphID;
	}

	void Unlink();

	void Abandon();

	void Link(UMovieSceneEntitySystemLinker* InLinker);

	void Run(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents);

	bool IsRelevant(UMovieSceneEntitySystemLinker* InLinker) const;

	void ConditionalLinkSystem(UMovieSceneEntitySystemLinker* InLinker) const;

	void TagGarbage();

	void CleanTaggedGarbage();

	/**
	 * Enable this system if it is not already.
	 */
	void Enable();

	/**
	 * Disable this system if it is not already.
	 * Disabled systems will remain in the system graph, and will stay alive as long as they are relevant, but will not be Run.
	 */
	void Disable();

protected:

	virtual bool IsReadyForFinishDestroy() override;
	virtual void FinishDestroy() override;

private:

	virtual void OnLink() {}

	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) { }

	virtual void OnUnlink() {}

	virtual bool IsRelevantImpl(UMovieSceneEntitySystemLinker* InLinker) const;

	virtual void ConditionalLinkSystemImpl(UMovieSceneEntitySystemLinker* InLinker) const;

	virtual void OnTagGarbage() {}

	virtual void OnCleanTaggedGarbage() {}

protected:

	UPROPERTY()
	UMovieSceneEntitySystemLinker* Linker;

protected:

	/** Defines a single component that makes this system automatically linked when it exists in an entity manager. Override IsRelevantImpl for more complex relevancy definitions. */
	FComponentTypeID RelevantComponent;

	UE::MovieScene::ESystemPhase Phase;

	uint16 GraphID;
	uint16 GlobalDependencyGraphID;

	UE::MovieScene::EEntitySystemContext SystemExclusionContext;

	/** When false, this system will not call its OnRun function, but will still be kept alive as long as IsRelevant is true */
	bool bSystemIsEnabled;

#if STATS || ENABLE_STATNAMEDEVENTS
	TStatId StatID;
#endif
};

