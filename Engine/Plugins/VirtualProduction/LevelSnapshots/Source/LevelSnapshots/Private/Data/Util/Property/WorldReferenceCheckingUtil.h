// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PropertyUtil.h"

class FProperty;
class UObject;

struct FArchiveSerializedPropertyChain;

namespace UE::LevelSnapshots::Private
{
	using FUObjectFilter = TFunction<bool(UObject*)>;
	
	/**
	 * Helper for detecting whether a property on a serialized world object contains a subobject reference or satifies the predicate.
	 * The predicate is usually something that checks whether the reference is a world object or a class default reference.
	 * This handles single object properties as well as collections (arrays, sets, maps).
	 * 
	 * Examples: calling
	 *  - ContainsWorldObjectReference(SceneComponent, {}, AttachChildren) would return true (if the USceneComponent::AttachChildren is non-empty).
	 *  - ContainsWorldObjectReference(StaticMeshComponent, {}, UStaticMeshComponent::StaticMesh) would return false.
	 *
	 *  @param OwningWorldObject Some object that exists in a world
	 *  @param PropertyChain The properties leading up to LeafProperty - nullptr implies LeafProperty is a root property
	 *  @param LeafMemberProperty The last member property (i.e. not inner of any array, set, or map) at the end of the chain (the last element in the chain does not contain LeafProperty).
	 *  @param Predicate Checks some condition on a UObject that was contained by the property; the passed in UObject can be nullptr.
	 *
	 *  @return Whether any value contained by any LeafProperty contained a world reference. 
	 */
	bool ContainsSubobjectOrSatisfiesPredicate(UObject* OwningWorldObject, const FArchiveSerializedPropertyChain* PropertyChain, const FProperty* LeafMemberProperty, FUObjectFilter Predicate);
};
