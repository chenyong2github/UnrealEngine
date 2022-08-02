// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseClassDefaultArchive.h"

#include "Data/Util/Property/PropertyUtil.h"
#include "Data/Util/WorldData/SnapshotObjectUtil.h"
#include "WorldSnapshotData.h"

#include "Util/Property/WorldReferenceCheckingUtil.h"

bool UE::LevelSnapshots::Private::FBaseClassDefaultArchive::ShouldSkipProperty(const FProperty* InProperty) const
{
	const bool bSuperWantsToSkip = Super::ShouldSkipProperty(InProperty);
	
	// Avoid any object references owned by a CDO. References are automatically replaced by object instance creation flow.
	return bSuperWantsToSkip || IsPropertyReferenceToSubobjectOrClassDefaults(InProperty);
}

UObject* UE::LevelSnapshots::Private::FBaseClassDefaultArchive::ResolveObjectDependency(int32 ObjectIndex) const
{
	return ResolveObjectDependencyForClassDefaultObject(GetSharedData(), ObjectIndex);
}

UE::LevelSnapshots::Private::FBaseClassDefaultArchive::FBaseClassDefaultArchive(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, bool bIsLoading, UObject* InObjectToRestore)
	:
	Super(InObjectData, InSharedData, bIsLoading, InObjectToRestore)
{
	// Description of CPF_Transient: "Property is transient: shouldn't be saved or loaded, except for Blueprint CDOs."
	ExcludedPropertyFlags = CPF_BlueprintAssignable | CPF_Deprecated
		// Do not save any instanced references when serialising CDOs
		| CPF_ContainsInstancedReference | CPF_InstancedReference | CPF_PersistentInstance;
	
	// Otherwise we are not allowed to serialize transient properties
	ArSerializingDefaults = true;
}

bool UE::LevelSnapshots::Private::FBaseClassDefaultArchive::IsPropertyReferenceToSubobjectOrClassDefaults(const FProperty* InProperty) const
{
	return UE::LevelSnapshots::Private::ContainsSubobjectOrSatisfiesPredicate(GetSerializedObject(), GetSerializedPropertyChain(), InProperty, [this](UObject* ContainedPtr)
	{
		if (ContainedPtr)
		{
			const bool bIsClassDefault = ContainedPtr->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject);
			const bool bIsPointingToDefaultSubobject = ContainedPtr->HasAnyFlags(RF_DefaultSubObject);
			const bool bIsPointingToSelf = ContainedPtr == GetSerializedObject();
			const bool bIsPointingToSubobject = ContainedPtr->IsIn(GetSerializedObject());
			return bIsClassDefault || bIsPointingToDefaultSubobject || bIsPointingToSelf || bIsPointingToSubobject;
		}
		return false;
	}); 
}