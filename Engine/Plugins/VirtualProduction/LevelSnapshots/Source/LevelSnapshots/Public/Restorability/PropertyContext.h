// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/PropertySelection.h"

/* Describes the location of a some property. */
struct LEVELSNAPSHOTS_API FPropertyContext
{
	/**
	 * The root class of the UObject containing the property, usually either an AActor or UActorComponent but can also
	 * be the just UObject for subobjects.
	 */
	UClass* RootClass;

	/* The chain of struct or collection (array, set, tmap) properties leading to some leaf property.
	 * Does not contain the leaf property. For collections along the path, the inner property is contained.
	 *
	 * Property examples:
	 *	- UActorComponent::RelativeLocation::X > [0] = 'RelativeLocation'. 
	 *	- UFoo::BarStruct::ArrayOfStructs::SomeProperty > [0] = 'BarStruct' (FStructProperty), [1] = 'ArrayOfStructs' (FArrayProperty), [2] = 'ArrayOfStructs_Inner' (FStructProperty)
	 */
	FLevelSnapshotPropertyChain PathToPropertyContainer;

	FPropertyContext(UClass* RootClass, const FLevelSnapshotPropertyChain& PathToPropertyContainer)
		:
		RootClass(RootClass),
		PathToPropertyContainer(PathToPropertyContainer)
	{}
};