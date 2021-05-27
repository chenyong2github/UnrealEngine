// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComponentSnapshotData.h"
#include "ObjectSnapshotData.h"
#include "ActorSnapshotData.generated.h"

class UActorComponent;
class ULevelSnapshotSelectionSet;
class UPackage;
struct FPropertySelectionMap;
struct FWorldSnapshotData;

USTRUCT()
struct LEVELSNAPSHOTS_API FActorSnapshotData
{
	GENERATED_BODY()

	static FActorSnapshotData SnapshotActor(AActor* OriginalActor, FWorldSnapshotData& WorldData);
	
	void DeserializeIntoExistingWorldActor(UWorld* SnapshotWorld, AActor* OriginalActor, FWorldSnapshotData& WorldData, UPackage* InLocalisationSnapshotPackage, const FPropertySelectionMap& SelectedProperties);
	void DeserializeIntoRecreatedEditorWorldActor(UWorld* SnapshotWorld, AActor* OriginalActor, FWorldSnapshotData& WorldData, UPackage* InLocalisationSnapshotPackage, const FPropertySelectionMap& SelectedProperties);

	TOptional<AActor*> GetPreallocatedIfValidButDoNotAllocate() const;
	TOptional<AActor*> GetPreallocated(UWorld* SnapshotWorld, FWorldSnapshotData& WorldData) const;
	TOptional<AActor*> GetDeserialized(UWorld* SnapshotWorld, FWorldSnapshotData& WorldData, UPackage* InLocalisationSnapshotPackage);

	FSoftClassPath GetActorClass() const;

private:

	using FSerializeActor = TFunction<void(AActor* OriginalActor, AActor* DerserializedActor)>;
	using FSerializeComponent = TFunction<void(FObjectSnapshotData& SerializedCompData, FComponentSnapshotData& CompData, UActorComponent* Original, UActorComponent* Deserialized)>;
	
	void DeserializeIntoWorldActor(UWorld* SnapshotWorld, AActor* OriginalActor, FWorldSnapshotData& WorldData, UPackage* InLocalisationSnapshotPackage, FSerializeActor SerializeActor, FSerializeComponent SerializeComponent);
	void DeserializeComponents(AActor* IntoActor, FWorldSnapshotData& WorldData, TFunction<void(FObjectSnapshotData& SerializedCompData, FComponentSnapshotData&,UActorComponent*, FWorldSnapshotData&)>&& Callback);
	
	/* We cache the actor to avoid recreating it all the time */
	UPROPERTY(Transient)
	mutable TWeakObjectPtr<AActor> CachedSnapshotActor = nullptr;

	/* Whether we already serialised the snapshot data into the actor.
	 * 
	 * This exists because sometimes we need to preallocate an actor without serialisation.
	 * Example: When serializing another actor which referenced this actor.
	 */
	UPROPERTY(Transient)
	mutable bool bReceivedSerialisation = false;
	
	UPROPERTY()
	FSoftClassPath ActorClass;
	
	UPROPERTY()
	FObjectSnapshotData SerializedActorData;
	
	UPROPERTY()
	TMap<int32, FComponentSnapshotData> ComponentData;
	
};