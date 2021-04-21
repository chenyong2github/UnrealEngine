// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DynamicMeshAABBTree3.h"
#include "Selection/DynamicMeshSelection.h"

#include "UVToolStateObjects.generated.h"

// This object store system is a simple way for tools to share intermediate
// structures. The tool builder gets pointed to a state object store, which
// is a simple map that the tool can query on whether it has a particular type
// of state object. If it does, the tool can get it out and use it. If it doesn't,
// it can create it itself and put it in the store for other tools to use later.

// TODO: We should probably have a way to check that the store object is still relevant.
// The AABBTrees already have change stamps built in, but the selection objects should
// probably have topology change stamps.
// We're also not currently bothering to clear the store. It's not really necessary for
// the current use in the UV editor, but we could have the store track number of accesses
// and throw away things that don't get accessed much. 

/** Base class to inherit from, mostly to be held together with other state objects. */
UCLASS()
class UVEDITORTOOLS_API UUVToolStateObject : public UObject
{
	GENERATED_BODY()
public:
};

/** Stores a UV mesh AABB tree */
UCLASS()
class UVEDITORTOOLS_API UUVMeshAABBTrees : public UUVToolStateObject
{
	GENERATED_BODY()
public:

	TArray<TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3>> AABBTrees;
};

/** Stores a UV mesh selection */
UCLASS()
class UVEDITORTOOLS_API UUVMeshSelection : public UUVToolStateObject
{
	GENERATED_BODY()
public:

	TSharedPtr<UE::Geometry::FDynamicMeshSelection> Selection;
};


// TODO: If we do end up keeping this, it should probably go in its own file, and
// it doesn't need to be UV specific. We'd also probably need to decide whether it
// should continue to provide editable objects or give const ones.
UCLASS()
class UVEDITORTOOLS_API UUVToolStateObjectStore : public UObject
{
	GENERATED_BODY()
public:

	template<typename DesiredClassOfUObject>
	TObjectPtr<DesiredClassOfUObject> GetToolStateObject()
	{
		TObjectPtr<UUVToolStateObject>* Result = ToolStateObjectMap.Find(DesiredClassOfUObject::StaticClass());
		return Result ? Cast<DesiredClassOfUObject>(*Result) : nullptr;
	}
	void SetToolStateObject(TObjectPtr<UUVToolStateObject> StateObject)
	{
		ToolStateObjectMap.Add(StateObject->GetClass(), StateObject);
	}
	void RemoveToolStateObject(TObjectPtr<UClass> StateObjectClass)
	{
		ToolStateObjectMap.Remove(StateObjectClass);
	}
	void Clear()
	{
		ToolStateObjectMap.Empty();
	}
protected:

	UPROPERTY()
	TMap<TObjectPtr<UClass>, TObjectPtr<UUVToolStateObject>> ToolStateObjectMap;
};