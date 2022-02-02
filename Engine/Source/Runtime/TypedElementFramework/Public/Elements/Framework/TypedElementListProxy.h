// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Elements/Framework/TypedElementListFwd.h"
#include "TypedElementListProxy.generated.h"

/**
 * A list of managed element handles (proxy to a FManagedTypedElementList instance).
 * Provides high-level access to groups of elements, including accessing elements that implement specific interfaces.
 * 
 * Note: the managed list proxy use should be avoided when not using it for the script exposure apis.
 * The weak model for the handles come with an additional cost to the runtime performance and the memory usage.
 */
USTRUCT(BlueprintType, DisplayName="Typed Element List", meta=(ScriptName="TypedElementList"))
struct TYPEDELEMENTFRAMEWORK_API FManagedTypedElementListProxy
{
	GENERATED_BODY()

public:
	FManagedTypedElementListProxy() = default;

	FManagedTypedElementListProxy(FScriptTypedElementListRef InElementList)
		: ElementList(MoveTemp(InElementList))
	{
	}

	FManagedTypedElementListProxy(FScriptTypedElementListConstRef InElementList)
		: ElementList(ConstCastSharedRef<FScriptTypedElementList>(InElementList))
	{
	}

	FScriptTypedElementListPtr GetElementList()
	{
		return ElementList;
	}

	FScriptTypedElementListConstPtr GetElementList() const
	{
		return ElementList;
	}

private:
	FScriptTypedElementListPtr ElementList;
};

