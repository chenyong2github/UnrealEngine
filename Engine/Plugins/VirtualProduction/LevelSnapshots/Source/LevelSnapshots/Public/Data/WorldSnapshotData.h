// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActorSnapshotData.h"
#include "ComponentSnapshotData.h"
#include "ClassDefaultObjectSnapshotData.h"
#include "SubobjectSnapshotData.h"
#include "WorldSnapshotData.generated.h"

class FSnapshotArchive;
class FTakeWorldObjectSnapshotArchive;
class ULevelSnapshotSelectionSet;
struct FPropertySelectionMap;

/* Holds saved world data and handles all logic related to writing to the existing world. */
USTRUCT()
struct LEVELSNAPSHOTS_API FWorldSnapshotData
{
	GENERATED_BODY()
	friend FSnapshotArchive;
	friend FTakeWorldObjectSnapshotArchive;
	friend FActorSnapshotData;
	friend FComponentSnapshotData;

	void OnCreateSnapshotWorld(UWorld* NewTempActorWorld);
	void OnDestroySnapshotWorld();
	
	/* Records the actor in this snapshot */
	void SnapshotWorld(UWorld* World);
	/* Applies the saved properties to WorldActor */
	void ApplyToWorld(UWorld* WorldToApplyTo, const FPropertySelectionMap& PropertiesToSerialize);

	int32 GetNumSavedActors() const;
	void ForEachOriginalActor(TFunction<void(const FSoftObjectPath& ActorPath)> HandleOriginalActorPath) const;
	bool HasMatchingSavedActor(const FSoftObjectPath& OriginalObjectPath) const;
	TOptional<AActor*> GetDeserializedActor(const FSoftObjectPath& OriginalObjectPath);

	/**
	 * Checks whether two pointers point to "equivalent" objects.
	 * TODO: Currently only actors are supported. In future, extend this when subobject support is implemented.
	 */
	bool AreReferencesEquivalent(UObject* SnapshotVersion, UObject* OriginalVersion) const;

private:

	void ApplyToWorld_HandleRemovingActors(const FPropertySelectionMap& PropertiesToSerialize);
	void ApplyToWorld_HandleRecreatingActors(TSet<AActor*>& EvaluatedActors, const FPropertySelectionMap& PropertiesToSerialize);
	void ApplyToWorld_HandleSerializingMatchingActors(TSet<AActor*>& EvaluatedActors, const TArray<FSoftObjectPath>& SelectedPaths, const FPropertySelectionMap& PropertiesToSerialize);
	
	enum class EResolveType
	{
		ResolveForUseInOriginalWorld,
		ResolveForUseInTempWorld
	};
	/* Adds an object dependency without serializing the object's content. Intended for external objects, e.g. to UMaterial in the content browser. */
	int32 AddObjectDependency(UObject* ReferenceFromOriginalObject);
	TOptional<UObject*> ResolveObjectDependency(int32 ObjectPathIndex, EResolveType ResolveType);

	
	/* Adds a subobject dependency. Intended for internal objects which need to store serialized data, e.g. components and other subobjects. Implicitly calls AddObjectDependency.
	 * @return A valid index in SerializedObjectReferences and the corresponding subobject data.
	 */
	int32 AddSubobjectDependency(UObject* ReferenceFromOriginalObject);

	
	void AddClassDefault(UClass* Class);
	UObject* GetClassDefault(UClass* Class);
	/* Gets the Object's class and serializes the saved CDO into it.
	 * This is intended for cases where you cannot specify a template object for new objects. Components are one such use case.
	 */
	void SerializeClassDefaultsInto(UObject* Object);



	
	
	/* The world we will be adding temporary actors to */
	TWeakObjectPtr<UWorld> TempActorWorld;


	
	/* We only save properties with values different from their CDO counterpart.
	 * Because of this, we need to save class defaults in the snapshot.
	 */
	UPROPERTY()
	TMap<FSoftClassPath, FClassDefaultObjectSnapshotData> ClassDefaults;
	
	/* Holds serialized actor data.
	 * Maps the original actor's path to its serialized data.
	 */
	UPROPERTY()
	TMap<FSoftObjectPath, FActorSnapshotData> ActorData;


	
	/* Whenever an object needs to serialize a name, we add it to this array and serialize an index to this array. */
	UPROPERTY()
	TArray<FName> SerializedNames;

	/* Whenever an object needs to serialize an object reference, we keep the object path here and serialize an index to this array.
	 * 
	 * External references, e.g. UDataAssets or UMaterials, are easily handled.
	 * Example: UStaticMesh /Game/Australia/StaticMeshes/MegaScans/Nature_Rock_vbhtdixga/vbhtdixga_LOD0.vbhtdixga_LOD0
	 * 
	 * Internal references, e.g. to subobjects and to other actors in the world, are a bit tricky.
	 * For internal references, we need to do some translation using TranslateOriginalToSnapshotPath:
	 * Example original: UStaticMeshActor::StaticMeshComponent /Game/MapName.MapName:PersistentLevel.StaticMeshActor_42.StaticMeshComponent
	 * Example translated: UStaticMeshActor::StaticMeshComponent /Engine/Transient.World_21:PersistentLevel.StaticMeshActor_42.StaticMeshComponent
	 */
	UPROPERTY()
	TArray<FSoftObjectPath> SerializedObjectReferences;
	
	/*
	 * Key: A valid index in to SerializedObjectReferences. Value: Subobject information for the associated entry in SerializedObjectReferences.
	 * There is only an entry if the associated object is in fact a subobject. Actors and assets in particular do not get any entry.
	 */
	UPROPERTY()
	TMap<int32, FSubobjectSnapshotData> Subobjects;
	
};
