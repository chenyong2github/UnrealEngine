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

// TODO: Move all functions into static helper functions in private part of module. Helps separation of concerns.

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

	/* Given a path to a world actor, gets the equivalent allocated actor. */
	TOptional<AActor*> GetDeserializedActor(const FSoftObjectPath& OriginalObjectPath, UPackage* LocalisationSnapshotPackage);
	/* Gets the state of the CDO from when the snapshot was taken. CDO is saved when a snapshot is taken so CDO changes can be detected. */
	TOptional<FObjectSnapshotData*> GetSerializedClassDefaults(UClass* Class);  

	/**
	 * Checks whether two pointers point to "equivalent" objects.
	 */
	bool AreReferencesEquivalent(UObject* SnapshotPropertyValue, UObject* OriginalPropertyValue, AActor* SnapshotActor, AActor* OriginalActor) const;
	
public: /* Serialisation functions */
	
	
	/* Adds an object dependency without serializing the object's content. Intended for external objects, e.g. to UMaterial in the content browser. */
	int32 AddObjectDependency(UObject* ReferenceFromOriginalObject);

	/* Resolves an object dependency for use in the snapshot world. If the object is a subobject, it gets fully serialized. */
	UObject* ResolveObjectDependencyForSnapshotWorld(int32 ObjectPathIndex);
	
	/* Resolves an object dependency for use in the editor world. If the object is a subobject, it is serialized.
	 * Steps for serializing subobject:
	 *  - If an equivalent object with the saved name and class exists, use that and serialize the properties in SelectionMap into it.
	 *  - Otherwise allocate a new object and serialize all properties into it
	 *  - Replaces all references to the trashed object with the new one
	 */
	UObject* ResolveObjectDependencyForEditorWorld(int32 ObjectPathIndex, const FPropertySelectionMap& SelectionMap);

	/* Resolves an object depedency when restoring a class default object. Simply resolves without further checks. */
	UObject* ResolveObjectDependencyForClassDefaultObject(int32 ObjectPathIndex);
	
	
	/**
	 * Adds a subobject dependency. Intended for internal objects which need to store serialized data, e.g. components and other subobjects. Implicitly calls AddObjectDependency.
	 * @return A valid index in SerializedObjectReferences and the corresponding subobject data.
	 */
	int32 AddSubobjectDependency(UObject* ReferenceFromOriginalObject);

	/**
	 * Adds a subobject to SerializedObjectReferences and CustomSubobjectSerializationData.
	 * @return A valid index in SerializedObjectReferences and the corresponding subobject data.
	 */
	int32 AddCustomSubobjectDependency(UObject* ReferenceFromOriginalObject);
	FCustomSerializationData* GetCustomSubobjectData_ForSubobject(const FSoftObjectPath& ReferenceFromOriginalObject);
	const FCustomSerializationData* GetCustomSubobjectData_ForActorOrSubobject(UObject* OriginalObject) const;

	
	void AddClassDefault(UClass* Class);
	UObject* GetClassDefault(UClass* Class);
	
	/* Gets the Object's class and serializes the saved CDO into it.
	* This is intended for cases where you cannot specify a template object for new objects. Components are one such use case.
	*/
	void SerializeClassDefaultsInto(UObject* Object);


	const FSnapshotVersionInfo& GetSnapshotVersionInfo() const;
	
	//~ Begin TStructOpsTypeTraits Interface
	void PostSerialize(const FArchive& Ar);
	//~ End TStructOpsTypeTraits Interface
	
public:

	UObject* ResolveExternalReference(const FSoftObjectPath& ObjectPath);
	
	void ApplyToWorld_HandleRemovingActors(UWorld* WorldToApplyTo, const FPropertySelectionMap& PropertiesToSerialize);
	void ApplyToWorld_HandleRecreatingActors(TSet<AActor*>& EvaluatedActors, UPackage* LocalisationSnapshotPackage, const FPropertySelectionMap& PropertiesToSerialize);
	void ApplyToWorld_HandleSerializingMatchingActors(TSet<AActor*>& EvaluatedActors, const TArray<FSoftObjectPath>& SelectedPaths, UPackage* LocalisationSnapshotPackage, const FPropertySelectionMap& PropertiesToSerialize);

	
	
	/* The world we will be adding temporary actors to */
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
	 * For internal references, we need to do some translation using TranslateOriginalToSnapshotPath:
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
};

template<>
struct TStructOpsTypeTraits<FWorldSnapshotData> : public TStructOpsTypeTraitsBase2<FWorldSnapshotData>
{
	enum 
	{ 
		WithPostSerialize = true
	};
};

