// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Map.h"
#include "Containers/SortedMap.h"
#include "Containers/SparseArray.h"
#include "Evaluation/MovieScenePlayback.h"
#include "EntitySystem/MovieSceneSequenceInstance.h"
#include "EntitySystem/MovieSceneComponentDebug.h"


class UMovieSceneEntitySystemLinker;

namespace UE
{
namespace MovieScene
{

class FEntityManager;

struct FEntityRange;
struct FInstanceRegistry;
struct FResolveObjectTask;
struct FBoundObjectInstances;



/** Core concept that is required by all entities and systems - should be located on the core system manager itself */
struct FInstanceRegistry
{
	MOVIESCENE_API FInstanceRegistry(UMovieSceneEntitySystemLinker* InLinker);

	MOVIESCENE_API ~FInstanceRegistry();

	FInstanceRegistry(const FInstanceRegistry&) = delete;
	void operator=(const FInstanceRegistry&) = delete;

	const TSparseArray<FSequenceInstance>& GetSparseInstances() const
	{
		return Instances;
	}

	UMovieSceneEntitySystemLinker* GetLinker() const
	{
		return Linker;
	}

	bool IsHandleValid(FInstanceHandle InstanceHandle) const
	{
		return Instances.IsValidIndex(InstanceHandle.InstanceID) && Instances[InstanceHandle.InstanceID].GetSerialNumber() == InstanceHandle.InstanceSerial;
	}

	const FSequenceInstance& GetInstance(FInstanceHandle InstanceHandle) const
	{
		checkfSlow(IsHandleValid(InstanceHandle), TEXT("Attempting to access an invalid instance handle."));
		return Instances[InstanceHandle.InstanceID];
	}

	FSequenceInstance& MutateInstance(FInstanceHandle InstanceHandle)
	{
		checkfSlow(IsHandleValid(InstanceHandle), TEXT("Attempting to access an invalid instance handle."));
		return Instances[InstanceHandle.InstanceID];
	}

	const FMovieSceneContext& GetContext(FInstanceHandle InstanceHandle) const
	{
		return GetInstance(InstanceHandle).GetContext();
	}

	MOVIESCENE_API FInstanceHandle AllocateRootInstance(IMovieScenePlayer* Player);

	MOVIESCENE_API FInstanceHandle AllocateSubInstance(IMovieScenePlayer* Player, FMovieSceneSequenceID SequenceID, FInstanceHandle RootInstance);

	MOVIESCENE_API void DestroyInstance(FInstanceHandle InstanceHandle);

	MOVIESCENE_API void CleanupLinkerEntities(const TSet<FMovieSceneEntityID>& LinkerEntities);

	void InvalidateObjectBinding(const FGuid& ObjectBindingID, FInstanceHandle InstanceHandle)
	{
		InvalidatedObjectBindings.Add(MakeTuple(ObjectBindingID, InstanceHandle));
	}

	bool IsBindingInvalidated(const FGuid& ObjectBindingID, FInstanceHandle InstanceHandle) const
	{
		// The binding is invalidated if it is contained within the invalid set, or if an empty GUID with the same instance handle exists (implying _all_ bindings are invalidated for that instance handle)
		return InvalidatedObjectBindings.Contains(MakeTuple(ObjectBindingID, InstanceHandle)) || InvalidatedObjectBindings.Contains(MakeTuple(FGuid(), InstanceHandle));
	}

	bool HasInvalidatedBindings() const
	{
		return InvalidatedObjectBindings.Num() != 0;
	}

	bool RemoveInvalidHandles();
	void PostInstantation();

	void FinalizeFrame();

	void TagGarbage();

	void WorldCleanup(UWorld* World);

private:

	UMovieSceneEntitySystemLinker* Linker;

	/** Authoritate array of unique instance combinations */
	TSparseArray<FSequenceInstance> Instances;
	uint16 InstanceSerialNumber;

	/** Set of invalidated object bindings by their instance handle. Empty guids indicate that all bindings for that instance handle are invalid */
	TSet<TTuple<FGuid, FInstanceHandle>> InvalidatedObjectBindings;
};


} // namespace MovieScene
} // namespace UE