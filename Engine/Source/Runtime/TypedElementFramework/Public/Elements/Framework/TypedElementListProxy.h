// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Elements/Framework/TypedElementListFwd.h"
#include "TypedElementListProxy.generated.h"

/**
 * A list of element handles (proxy to a FTypedElementList instance).
 * Provides high-level access to groups of elements, including accessing elements that implement specific interfaces.
 */
USTRUCT(BlueprintType, DisplayName="TypedElementList", meta=(ScriptName="TypedElementList"))
struct TYPEDELEMENTFRAMEWORK_API FTypedElementListProxy
{
	GENERATED_BODY()
	
public:
	FTypedElementListProxy() = default;

	FTypedElementListProxy(FTypedElementListRef InElementList)
		: ElementList(MoveTemp(InElementList))
	{
	}

	FTypedElementListProxy(FTypedElementListConstRef InElementList)
		: ElementList(ConstCastSharedRef<FTypedElementList>(InElementList))
	{
	}

	FTypedElementListPtr GetElementList()
	{
		return ElementList;
	}

	FTypedElementListConstPtr GetElementList() const
	{
		return ElementList;
	}
	
private:
	FTypedElementListPtr ElementList;
};
