// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "PropertySnapshot.generated.h"

USTRUCT()
struct FLevelSnapshot_Property
{
	GENERATED_BODY()

	FLevelSnapshot_Property();
	FLevelSnapshot_Property(FProperty* InProperty, uint32 InPropertyDepth);

	/**
	 * Recalculate the property scope offset and size from the newly appended data.
	 * @param InOffset Buffer offset of the newly added data
	 * @param InSize Size of the newly added data.
	 */
	void AppendSerializedData(const uint32 InOffset, const uint32 InSize);

	/**
	 * Add a new name referenced from this property scope.
	 * @param InOffset Buffer offset where the name reference is encountered in this property scope.
	 * @param InNameIndex The array index of the referenced name in the FObjectSnapshot referenced names
	 * @see FObjectSnapshot
	 */
	void AddNameReference(const uint32 InOffset, const uint32 InNameIndex);

	/**
	 * Add a new object referenced from this property scope.
	 * @param InOffset Buffer offset where the object reference is encountered in this property scope.
	 * @param InObjectIndex The array index of the referenced object in the FObjectSnapshot referenced names
	 * @see FObjectSnapshot
	 */
	void AddObjectReference(const uint32 InOffset, const uint32 InObjectIndex);

	/** Base information about this property scope. */
	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	TFieldPath<FProperty> PropertyPath;

	/** Property flags ie. Transient, NonTransactional, etc. */
	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	uint64 PropertyFlags = 0;

	/** Property depth from the recorded snapshot (i.e. 0 -> Root Property) */
	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	uint32 PropertyDepth = 0;

	/** Recorded DataOffset of this property scope in the FObjectSnapshot data buffer. */
	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	uint32 DataOffset = 0;

	/** Recorded DataSize of this property scope in the FObjectSnapshot data buffer. */
	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	uint32 DataSize = 0;

	/** Referenced Names Offset to their NameIndex in the FObjectSnapshot::ReferencedNames array. */
	UPROPERTY()
	TMap<uint32, uint32> ReferencedNamesOffsetToIndex;

	/** Referenced Objects Offset to their ObjectIndex in the FObjectSnapshot::ReferencedObjects. */
	UPROPERTY()
	TMap<uint32, uint32> ReferencedObjectOffsetToIndex;
};