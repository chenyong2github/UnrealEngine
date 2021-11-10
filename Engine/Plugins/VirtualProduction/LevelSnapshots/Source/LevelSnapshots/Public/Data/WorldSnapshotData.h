// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActorSnapshotData.h"
#include "ComponentSnapshotData.h"
#include "ClassDefaultObjectSnapshotData.h"
#include "CustomSerializationData.h"
#include "SnapshotVersion.h"
#include "SubobjectSnapshotData.h"
#include "UObject/Class.h"
#include "WorldSnapshotData.generated.h"

class AActor;
class FSnapshotArchive;
struct FPropertySelectionMap;
struct FActorSnapshotData;

/* Holds saved world data and handles all logic related to writing to the existing world. */
USTRUCT()
struct LEVELSNAPSHOTS_API FWorldSnapshotData
{
	GENERATED_BODY()
	friend FSnapshotArchive;
	friend FActorSnapshotData;

	void OnCreateSnapshotWorld(UWorld* NewTempActorWorld);
	void OnDestroySnapshotWorld();
	
	/* Records the actor in this snapshot */
	void SnapshotWorld(UWorld* World);
	/* Applies the saved properties to WorldActor */
	void ApplyToWorld(UWorld* WorldToApplyTo, UPackage* LocalisationSnapshotPackage, const FPropertySelectionMap& PropertiesToSerialize);

	int32 GetNumSavedActors() const;
	void ForEachOriginalActor(TFunction<void(const FSoftObjectPath& ActorPath, const FActorSnapshotData& SavedData)> HandleOriginalActorPath) const;
	bool HasMatchingSavedActor(const FSoftObjectPath& OriginalObjectPath) const;

	/** Gets the actor's display label */
	FString GetActorLabel(const FSoftObjectPath& OriginalObjectPath) const;
	
	/** Same as GetPreallocatedActor, only that all data will be serialized into it. */
	TOptional<AActor*> GetDeserializedActor(const FSoftObjectPath& OriginalObjectPath, UPackage* LocalisationSnapshotPackage);
	/* Gets the state of the CDO from when the snapshot was taken. CDO is saved when a snapshot is taken so CDO changes can be detected. */
	TOptional<FObjectSnapshotData*> GetSerializedClassDefaults(UClass* Class);  
	
public: /* Serialisation functions */
	
	void AddClassDefault(UClass* Class);
	UObject* GetClassDefault(UClass* Class);
	
	/* Gets the Object's class and serializes the saved CDO into it.
	* This is intended for cases where you cannot specify a template object for new objects. Components are one such use case.
	*/
	void SerializeClassDefaultsInto(UObject* Object);


	const FSnapshotVersionInfo& GetSnapshotVersionInfo() const;
	
	//~ Begin TStructOpsTypeTraits Interface
	bool Serialize(FArchive& Ar);
	void PostSerialize(const FArchive& Ar);
	//~ End TStructOpsTypeTraits Interface

	void CollectReferencesAndNames(FArchive& Ar);
	void CollectActorReferences(FArchive& Ar);
	void CollectClassDefaultReferences(FArchive& Ar);
	
public:

	void PreloadClassesForRestore(const FPropertySelectionMap& SelectionMap);
	void ApplyToWorld_HandleRemovingActors(UWorld* WorldToApplyTo, const FPropertySelectionMap& PropertiesToSerialize);
	void ApplyToWorld_HandleRecreatingActors(TSet<AActor*>& EvaluatedActors, UPackage* LocalisationSnapshotPackage, const FPropertySelectionMap& PropertiesToSerialize);
	void ApplyToWorld_HandleSerializingMatchingActors(TSet<AActor*>& EvaluatedActors, const TArray<FSoftObjectPath>& SelectedPaths, UPackage* LocalisationSnapshotPackage, const FPropertySelectionMap& PropertiesToSerialize);

	
	
	/* The world we will be adding temporary actors to */
	UPROPERTY(Transient)
	TWeakObjectPtr<UWorld> TempActorWorld;

	/**
	 * Stores versioning information we inject into archives.
	 * This is to support asset migration, like FArchive::UsingCustomVersion.
	 */
	UPROPERTY()
	FSnapshotVersionInfo SnapshotVersionInfo;
	
	
	
	/**
	 * We only save properties with values different from their CDO counterpart.
	 * Because of this, we need to save class defaults in the snapshot.
	 */
	UPROPERTY()
	TMap<FSoftClassPath, FClassDefaultObjectSnapshotData> ClassDefaults;
	
	/**
	 * Holds serialized actor data.
	 * Maps the original actor's path to its serialized data.
	 */
	UPROPERTY()
	TMap<FSoftObjectPath, FActorSnapshotData> ActorData;

	

	
	/** Whenever an object needs to serialize a name, we add it to this array and serialize an index to this array. */
	UPROPERTY()
	TArray<FName> SerializedNames;

	/**
	 * Whenever an object needs to serialize an object reference, we keep the object path here and serialize an index to this array.
	 * 
	 * External references, e.g. UDataAssets or UMaterials, are easily handled.
	 * Example: UStaticMesh /Game/Australia/StaticMeshes/MegaScans/Nature_Rock_vbhtdixga/vbhtdixga_LOD0.vbhtdixga_LOD0
	 * 
	 * Internal references, e.g. to subobjects and to other actors in the world, are a bit tricky.
	 * For internal references, we need to do some translation:
	 * Example original: UStaticMeshActor::StaticMeshComponent /Game/MapName.MapName:PersistentLevel.StaticMeshActor_42.StaticMeshComponent
	 * Example translated: UStaticMeshActor::StaticMeshComponent /Engine/Transient.World_21:PersistentLevel.StaticMeshActor_42.StaticMeshComponent
	 */
	UPROPERTY()
	TArray<FSoftObjectPath> SerializedObjectReferences;
	
	/**
	 * Key: A valid index in to SerializedObjectReferences.
	 * Value: Subobject information for the associated entry in SerializedObjectReferences.
	 * There is only an entry if the associated object is in fact a subobject. Actors and assets in particular do not get any entry.
	 */
	UPROPERTY()
	TMap<int32, FSubobjectSnapshotData> Subobjects;

	/**
	 * Key: A valid index in to SerializedObjectReferences
	 * Value: Data that was generated by some ICustomObjectSnapshotSerializer.
	 */
	UPROPERTY()
	TMap<int32, FCustomSerializationData> CustomSubobjectSerializationData;


	/** Binds every entry in SerializedNames to its index. Speeds up adding unique names. */
	UPROPERTY(Transient)
	TMap<FName, int32> NameToIndex;
	
	/** Binds every entry in SerializedObjectReferences to its index. Speeds up adding unique references. */
    UPROPERTY(Transient)
    TMap<FSoftObjectPath, int32> ReferenceToIndex;
};

template<>
struct TStructOpsTypeTraits<FWorldSnapshotData> : public TStructOpsTypeTraitsBase2<FWorldSnapshotData>
{
	enum 
	{
		WithSerializer = true,
		WithPostSerialize = true
	};
};

