// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Simple archive for updating lazy pointer GUIDs when a sub-level gets loaded or duplicated for PIE
 */

#pragma once

#include "Engine/World.h"
#include "UObject/LazyObjectPtr.h"
#include "Components/InstancedStaticMeshComponent.h"

class FFixupLazyObjectPtrForPIEArchive : public FArchiveUObject
{
	/** Keeps track of objects that have already been serialized */
	TSet<UObject*> VisitedObjects;

public:

	FFixupLazyObjectPtrForPIEArchive()
	{
		ArIsObjectReferenceCollector = true;
		ArIsModifyingWeakAndStrongReferences = true;
		this->SetIsPersistent(false);
		ArIgnoreArchetypeRef = true;
	}

	virtual bool ShouldSkipProperty(const FProperty* InProperty) const override
	{
		return InProperty->IsA<FMulticastDelegateProperty>() || FArchiveUObject::ShouldSkipProperty(InProperty);
	}

	virtual FArchive& operator<<(FLazyObjectPtr& LazyObjectPtr) override
	{
		FArchive& Ar = *this;
		FUniqueObjectGuid ID = LazyObjectPtr.GetUniqueID();

		// Remap unique ID if necessary
		ID = ID.FixupForPIE();

		LazyObjectPtr = ID;
		return Ar;
	}

	virtual FArchive& operator<<(UObject*& Object) override
	{
		if (Object && (Object->IsA(UWorld::StaticClass()) || Object->IsInA(UWorld::StaticClass())) && !VisitedObjects.Contains(Object))
		{
			VisitedObjects.Add(Object);
			UPackage* Outermost = Object->GetOutermost();
			if (!Outermost || Outermost->HasAnyPackageFlags(PKG_PlayInEditor))
			{
				// Skip instanced static mesh component as their impact on serialization is enormous and they don't contain lazy ptrs.
				if (!Cast<UInstancedStaticMeshComponent>(Object))
				{
					Object->Serialize(*this);
				}
			}
		}
		return *this;
	}

	virtual FArchive& operator<<(FSoftObjectPtr& Value) override
	{
		// Explicitly do nothing, we don't want to accidentally do PIE fixups
		return *this;
	}

	virtual FArchive& operator<<(FSoftObjectPath& Value) override
	{
		// Explicitly do nothing, we don't want to accidentally do PIE fixups
		return *this;
	}
};
