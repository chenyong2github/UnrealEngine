// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ArchetypeTypes.h"
#include "InstancedStruct.h"
#include "EntityView.generated.h"


class UEntitySubsystem;
struct FArchetypeData;
struct FArchetypeHandle;
struct FArchetypeComponentConfig;

/** 
 * The type representing a single entity in a single archetype. It's of a very transient nature so we guarantee it's 
 * validity only within the scope it has been created in. Don't store it. 
 */
USTRUCT()
struct MASSENTITY_API FEntityView
{
	GENERATED_BODY()

	FEntityView() = default;

	/** 
	 *  Resolves Entity against ArchetypeHandle. Note that this approach requires the caller to ensure that Entity
	 *  indeed belongs to ArchetypeHandle. If not the call will fail a check. As a remedy calling the 
	 *  UEntitySubsystem-flavored constructor is recommended since it will first find the appropriate archetype for
	 *  Entity. 
	 */
	FEntityView(const FArchetypeHandle& ArchetypeHandle, FLWEntity Entity);

	/** 
	 *  Finds the archetype Entity belongs to and then resolves against it. The caller is responsible for ensuring
	 *  that the given Entity is in fact a valid ID tied to any of the archetypes 
	 */
	FEntityView(const UEntitySubsystem& EntitySubsystem, FLWEntity Entity);

	FLWEntity GetEntity() const	{ return Entity; }

	/** will fail a check if the viewed entity doesn't have the given component */	
	template<typename T>
	T& GetComponentData() const
	{
		static_assert(!TIsDerivedFrom<T, FComponentTag>::IsDerived,
			"Given struct doesn't represent a valid fragment type but a tag. Use HasTag instead.");
		static_assert(TIsDerivedFrom<T, FComponentTag>::IsDerived || TIsDerivedFrom<T, FLWComponentData>::IsDerived,
			"Given struct doesn't represent a valid fragment type. Make sure to inherit from FLWComponentData or one of its child-types.");

		return *((T*)GetComponentPtrChecked(*T::StaticStruct()));
	}
		
	/** if the viewed entity doesn't have the given component the function will return null */
	template<typename T>
	T* GetComponentDataPtr() const
	{
		static_assert(!TIsDerivedFrom<T, FComponentTag>::IsDerived,
			"Given struct doesn't represent a valid fragment type but a tag. Use HasTag instead.");
		static_assert(TIsDerivedFrom<T, FComponentTag>::IsDerived || TIsDerivedFrom<T, FLWComponentData>::IsDerived,
			"Given struct doesn't represent a valid fragment type. Make sure to inherit from FLWComponentData or one of its child-types.");

		return (T*)GetComponentPtr(*T::StaticStruct());
	}

	FStructView GetComponentDataStruct(const UScriptStruct* ComponentType) const
	{
		check(ComponentType);
		return FStructView(ComponentType, static_cast<uint8*>(GetComponentPtr(*ComponentType)));
	}

	template<typename T>
	bool HasTag() const
	{
		static_assert(TIsDerivedFrom<T, FComponentTag>::IsDerived, "Given struct doesn't represent a valid tag type. Make sure to inherit from FComponentTag or one of its child-types.");
		return HasTag(*T::StaticStruct());
	}

	bool IsSet() const { return Archetype != nullptr && EntityHandle.IsValid(); }
	bool operator==(const FEntityView& Other) const { return Archetype == Other.Archetype && EntityHandle == Other.EntityHandle; }

protected:
	void* GetComponentPtr(const UScriptStruct& ComponentType) const;
	void* GetComponentPtrChecked(const UScriptStruct& ComponentType) const;
	bool HasTag(const UScriptStruct& TagType) const;

private:
	FLWEntity Entity;
	FInternalEntityHandle EntityHandle;
	FArchetypeData* Archetype = nullptr;
};
