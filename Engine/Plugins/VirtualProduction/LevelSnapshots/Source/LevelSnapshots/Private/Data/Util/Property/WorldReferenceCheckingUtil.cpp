// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldReferenceCheckingUtil.h"

#include "Components/ActorComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Serialization/ArchiveSerializedPropertyChain.h"
#include "UObject/Field.h"
#include "UObject/UnrealType.h"

namespace UE::LevelSnapshots::Private
{
	namespace Internal
	{
		class FSubobjectOrPredicateChecker
		{
			UObject* OwningWorldObject;
			const FArchiveSerializedPropertyChain* PropertyChain;
			const FUObjectFilter UObjectFilterFunc;
		public:

			FSubobjectOrPredicateChecker(UObject* OwningWorldObject, const FArchiveSerializedPropertyChain* PropertyChain, FUObjectFilter UObjectFilterFunc)
				: OwningWorldObject(OwningWorldObject)
				, PropertyChain(PropertyChain)
				, UObjectFilterFunc(UObjectFilterFunc)
			{}

			bool ContainsWorldObjectProperty(const FProperty* InLeafMemberProperty) const
			{
				if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(InLeafMemberProperty))
				{
					return IsWorldObjectReference(ObjectProperty, ObjectProperty, EPropertyType::NormalProperty);
				}
				
				if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(InLeafMemberProperty))
				{
					if (const FObjectPropertyBase* InnerAsObjectProperty = CastField<FObjectPropertyBase>(ArrayProperty->Inner))
					{
						return IsWorldObjectReference(InnerAsObjectProperty, ArrayProperty, EPropertyType::NormalProperty);
					}
				}
				
				if (const FSetProperty* SetProperty = CastField<FSetProperty>(InLeafMemberProperty))
				{
					if (const FObjectPropertyBase* InnerAsObjectProperty = CastField<FObjectPropertyBase>(SetProperty->ElementProp))
					{
						return IsWorldObjectReference(InnerAsObjectProperty, SetProperty, EPropertyType::NormalProperty);
					}
				}

				if (const FMapProperty* MapProperty = CastField<FMapProperty>(InLeafMemberProperty))
				{
					bool bResult = false;
					// In the case where both key and value are object properties, the efficiency could be improved... this API less complex though
					if (const FObjectPropertyBase* KeyAsObjectProperty = CastField<FObjectPropertyBase>(MapProperty->KeyProp))
					{
						bResult |= IsWorldObjectReference(KeyAsObjectProperty, MapProperty, EPropertyType::KeyInMap);
					}
					if (const FObjectPropertyBase* ValueAsObjectProperty = CastField<FObjectPropertyBase>(MapProperty->ValueProp);
						ValueAsObjectProperty && bResult) // Fast path out
					{
						bResult |= IsWorldObjectReference(ValueAsObjectProperty, MapProperty, EPropertyType::ValueInMap);
					}
					return bResult;
				}

				return false;
			}
			
			/**
			 * Loops to the end of the property chain checking whether the ObjectProperty points to a world object.
			 * @param ObjectProperty The property on which to perform the reference check.
			 * @param LeafProperty The property that contains ObjectProperty. If ObjectProperty is not the inner property of an array, set or map then LeafProperty == ObjectProperty.
			 */
			bool IsWorldObjectReference(const FObjectPropertyBase* ObjectProperty, const FProperty* LeafProperty, EPropertyType PropertyType) const
			{
				const bool bIsMarkedAsSubobject = LeafProperty->HasAnyPropertyFlags(CPF_InstancedReference | CPF_ContainsInstancedReference | CPF_PersistentInstance);
				const bool bIsActorOrComponentPtr = PropertyType == EPropertyType::NormalProperty
					&& (ObjectProperty->PropertyClass->IsChildOf(AActor::StaticClass()) || ObjectProperty->PropertyClass->IsChildOf(UActorComponent::StaticClass()));
				if (bIsMarkedAsSubobject || bIsActorOrComponentPtr)
				{
					return true;
				}

				const bool bSatisfiesPredicate = FollowPropertyChainUntilPredicateIsTrue(OwningWorldObject, PropertyChain, LeafProperty, [this, ObjectProperty, PropertyType](void* LeafValuePtr, EPropertyType ChainPropertyType)
				{
					if (PropertyType == ChainPropertyType)
					{
						UObject* ContainedPtr = ObjectProperty->GetObjectPropertyValue(LeafValuePtr);
						return LeafValuePtr ? UObjectFilterFunc(ContainedPtr) : false;
					}
					return false;
				});
				return bSatisfiesPredicate;
			}
		};
	}
	
	bool ContainsSubobjectOrSatisfiesPredicate(UObject* OwningWorldObject, const FArchiveSerializedPropertyChain* PropertyChain, const FProperty* LeafMemberProperty, FUObjectFilter Predicate)
	{
		Internal::FSubobjectOrPredicateChecker Helper(OwningWorldObject, PropertyChain, Predicate);
		return Helper.ContainsWorldObjectProperty(LeafMemberProperty);
	}
}