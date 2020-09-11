// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IPropertyAccess.generated.h"

// The various types of property copy
UENUM()
enum class EPropertyAccessCopyBatch : uint8
{
	// A copy of internal->internal data, unbatched
	InternalUnbatched,

	// A copy of external->internal data, unbatched
	ExternalUnbatched,

	// A copy of internal->internal data, batched
	InternalBatched,

	// A copy of external->internal data, batched
	ExternalBatched,

	Count
};

UINTERFACE(MinimalAPI)
class UPropertyAccess : public UInterface
{
	GENERATED_BODY()
};

class IPropertyAccess : public IInterface
{
	GENERATED_BODY()

public:
	/** 
	 * Process a 'tick' of a property access instance. 
	 * Note internally allocates via FMemStack and pushes its own FMemMark
	 */
	virtual void ProcessCopies(UObject* InObject, EPropertyAccessCopyBatch InBatchType) const = 0;

	/** 
	 * Process a single copy 
	 * Note that this can potentially allocate via FMemStack, so inserting FMemMark before a number of these calls is recommended
	 */
	virtual void ProcessCopy(UObject* InObject, EPropertyAccessCopyBatch InBatchType, int32 InCopyIndex, TFunctionRef<void(const FProperty*, void*)> InPostCopyOperation) const = 0;

	/** Bind all event-type accesses to their respective objects */
	virtual void BindEvents(UObject* InObject) const = 0;

	/** Resolve a path to an event Id for the specified class */
	virtual int32 GetEventId(const UClass* InClass, TArrayView<const FName> InPath) const = 0;
};
