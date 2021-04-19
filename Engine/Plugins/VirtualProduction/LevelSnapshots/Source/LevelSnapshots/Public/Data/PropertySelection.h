// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Serialization/ArchiveSerializedPropertyChain.h"
#include "UObject/UnrealType.h"

/* Uniquely identifies a property across structs.
 * 
 * Primarily this is used by serialisation code hence we pass in FArchiveSerializedPropertyChain often.
 * However, inheritance from FArchiveSerializedPropertyChain is an implementation detail.
 */
struct LEVELSNAPSHOTS_API FLevelSnapshotPropertyChain : FArchiveSerializedPropertyChain
{
	friend struct FPropertySelection;

	FLevelSnapshotPropertyChain MakeAppended(const FProperty* Property) const;
	void AppendInline(const FProperty* Property);

	/**
	 * Checks whether a given property being serialized corresponds to this chain.
	 *
	 * @param Chain The chain of properties to the most nested owning struct.
	 * @param LeafProperty The leaf property in the struct
	 */
	bool EqualsSerializedProperty(const FArchiveSerializedPropertyChain* ContainerChain, const FProperty* LeafProperty) const;

	bool IsEmpty() const;
};

/* Holds all properties that should be restored for an object. */
struct LEVELSNAPSHOTS_API FPropertySelection
{
	/**
	* Checks whether the given property is in this selection.
	*
	* @param Chain The chain of properties to the most nested owning struct. Expected to be the result of FArchive::GetSerializedPropertyChain.
	* @param LeafProperty The leaf property in the struct
	*/
	bool IsPropertySelected(const FArchiveSerializedPropertyChain* ContainerChain, const FProperty* LeafProperty) const;
	bool IsEmpty() const;

	void AddProperty(const FLevelSnapshotPropertyChain& SelectedProperty);
	void RemoveProperty(const FArchiveSerializedPropertyChain* ContainerChain, const FProperty* LeafProperty);

	/* Gets a flat list of all selected properties. The result contains no information what nested struct a property came from. */
	const TArray<TFieldPath<FProperty>>& GetSelectedLeafProperties() const;

private:

	int32 FindPropertyChain(const FArchiveSerializedPropertyChain* ContainerChain, const FProperty* LeafProperty) const;
	
	/* Duplicate version of SelectedProperties with the struct-path leading to the property left out. Needed to build UI more easily. */
	TArray<TFieldPath<FProperty>> SelectedLeafProperties;

	/* These are the properies that need to be restored.
	 * 
	 * Key: First property name of property in chain.
	 * Value: All chains associated that have the Key property has first property.
	 * Mapping it out like this allows us to search less properties.
	 */
	TArray<FLevelSnapshotPropertyChain> SelectedProperties;
};
		
		

		