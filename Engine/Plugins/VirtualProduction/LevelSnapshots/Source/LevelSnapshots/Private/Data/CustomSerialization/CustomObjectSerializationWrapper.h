// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class AActor;
class FCustomSerializationDataManager;
class ICustomObjectSnapshotSerializer;

struct FCustomSerializationData;
struct FPropertySelectionMap;
struct FWorldSnapshotData;

/* Utility for calling final functions when using custom object serialization. */
class FRestoreObjectScope
{
	friend class FCustomObjectSerializationWrapper;
	using FFinaliseRestorationCallback = TFunction<void()>;

	FFinaliseRestorationCallback Callback;

	FRestoreObjectScope() = delete;
	FRestoreObjectScope(const FRestoreObjectScope& Other) = delete;

public:

	FRestoreObjectScope(FFinaliseRestorationCallback Callback)
		: Callback(MoveTemp(Callback))
	{}

	~FRestoreObjectScope()
	{
		if (Callback)
		{
			Callback();
		}
	}

	FRestoreObjectScope(FRestoreObjectScope&& Other)
		: Callback(Other.Callback)
	{
		Other.Callback = nullptr;
	}
};

/**
 * Encapsulates ICustomObjectSnapshotSerializer from places that serialize objects.
 */
class FCustomObjectSerializationWrapper
{
public:
	
	using FHandleCustomSubobjectPair = TFunction<void(UObject* SnapshotSubobject, UObject* EditorSubobject)>;
	
	static void TakeSnapshotForActor(
		AActor* EditorActor,
		FCustomSerializationData& ActorSerializationData,
		FWorldSnapshotData& WorldData
		);
	
	static void TakeSnapshotForSubobject(
		UObject* Subobject,
		FWorldSnapshotData& WorldData
		);


	
	UE_NODISCARD static FRestoreObjectScope PreActorRestore_SnapshotWorld(
		AActor* EditorActor,
		FCustomSerializationData& ActorSerializationData,
		FWorldSnapshotData& WorldData,
		UPackage* LocalisationSnapshotPackage
		);

	UE_NODISCARD static FRestoreObjectScope PreActorRestore_EditorWorld(
		AActor* EditorActor,
		FCustomSerializationData& ActorSerializationData,
		FWorldSnapshotData& WorldData,
		const FPropertySelectionMap& SelectionMap,
		UPackage* LocalisationSnapshotPackage
		);
	

	UE_NODISCARD static FRestoreObjectScope PreSubobjectRestore_SnapshotWorld(
		UObject* Subobject,
		const FSoftObjectPath& OriginalSubobjectPath,
		FWorldSnapshotData& WorldData,
		UPackage* LocalisationSnapshotPackage
		);
	
	UE_NODISCARD static FRestoreObjectScope PreSubobjectRestore_EditorWorld(
		UObject* SnapshotObject,
		UObject* EditorObject,
		FWorldSnapshotData& WorldData,
		const FPropertySelectionMap& SelectionMap,
		UPackage* LocalisationSnapshotPackage
		);

	static void ForEachMatchingCustomSubobjectPair(const FWorldSnapshotData& WorldData, UObject* SnapshotObject, UObject* WorldObject, FHandleCustomSubobjectPair Callback);
};
	
