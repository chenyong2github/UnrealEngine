// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseClassDefaultArchive.h"

#include "WorldSnapshotData.h"

bool FBaseClassDefaultArchive::ShouldSkipProperty(const FProperty* InProperty) const
{
	const bool bSuperWantsToSkip = Super::ShouldSkipProperty(InProperty);
	
	// Avoid any object references owned by a CDO. References are automatically replaced by object instance creation flow.
	return bSuperWantsToSkip || IsPropertyReferenceToSubobjectOrClassDefaults(InProperty);
}

UObject* FBaseClassDefaultArchive::ResolveObjectDependency(int32 ObjectIndex) const
{
	return GetSharedData().ResolveObjectDependencyForClassDefaultObject(ObjectIndex);
}

FBaseClassDefaultArchive::FBaseClassDefaultArchive(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, bool bIsLoading, UObject* InObjectToRestore)
	:
	Super(InObjectData, InSharedData, bIsLoading),
	SerializedObject(InObjectToRestore)
{
	// Description of CPF_Transient: "Property is transient: shouldn't be saved or loaded, except for Blueprint CDOs."
	ExcludedPropertyFlags = CPF_BlueprintAssignable | CPF_Deprecated
		// Do not save any instanced references when serialising CDOs
		| CPF_ContainsInstancedReference | CPF_InstancedReference | CPF_PersistentInstance;
	
	// Otherwise we are not allowed to serialize transient properties
	ArSerializingDefaults = true;
}

bool FBaseClassDefaultArchive::IsPropertyReferenceToSubobjectOrClassDefaults(const FProperty* InProperty) const
{
	const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(InProperty);
	if (!ObjectProperty)
	{
		return false;
	}

	const bool bIsMarkedAsSubobject = InProperty->HasAnyPropertyFlags(CPF_InstancedReference | CPF_ContainsInstancedReference | CPF_PersistentInstance);
	const bool bIsActorOrComponentPtr = ObjectProperty->PropertyClass->IsChildOf(AActor::StaticClass()) || ObjectProperty->PropertyClass->IsChildOf(UActorComponent::StaticClass());
	if (bIsMarkedAsSubobject || bIsActorOrComponentPtr)
	{
		return true;
	}

	const FArchiveSerializedPropertyChain* PropertyChain = GetSerializedPropertyChain();
	const void* ContainerPtr = SerializedObject;
	for (int32 i = 0; PropertyChain && i < PropertyChain->GetNumProperties(); ++i)
	{
		ContainerPtr = PropertyChain->GetPropertyFromRoot(i)->ContainerPtrToValuePtr<void>(ContainerPtr);
	}

	const void* LeafValuePtr = InProperty->ContainerPtrToValuePtr<void>(ContainerPtr);
	if (const UObject* ContainedPtr = ObjectProperty->GetObjectPropertyValue(LeafValuePtr))
	{
		const bool bIsClassDefault = ContainedPtr->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject);
		const bool bIsPointingToDefaultSubobject = ContainedPtr->HasAnyFlags(RF_DefaultSubObject);
		const bool bIsPointingToSelf = ContainedPtr == SerializedObject;
		const bool bIsPointingToSubobject = ContainedPtr->IsIn(SerializedObject);
		return bIsClassDefault || bIsPointingToDefaultSubobject || bIsPointingToSelf || bIsPointingToSubobject;
	}
	
	return false;
}