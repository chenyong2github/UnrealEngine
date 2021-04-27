// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Object/ObjectElementCounterInterface.h"
#include "Elements/Object/ObjectElementData.h"

void UObjectElementCounterInterface::IncrementCountersForElement(const FTypedElementHandle& InElementHandle, FTypedElementCounter& InOutCounter)
{
	if (const UObject* Object = ObjectElementDataUtil::GetObjectFromHandle(InElementHandle))
	{
		UObjectElementCounterInterface::IncrementCounterForObjectClass(Object, InOutCounter);
	}
}

void UObjectElementCounterInterface::DecrementCountersForElement(const FTypedElementHandle& InElementHandle, FTypedElementCounter& InOutCounter)
{
	if (const UObject* Object = ObjectElementDataUtil::GetObjectFromHandle(InElementHandle))
	{
		UObjectElementCounterInterface::DecrementCounterForObjectClass(Object, InOutCounter);
	}
}

void UObjectElementCounterInterface::IncrementCounterForObjectClass(const UObject* InObject, FTypedElementCounter& InOutCounter)
{
	check(InObject);
	InOutCounter.IncrementCounter(NAME_Class, InObject->GetClass());
}

void UObjectElementCounterInterface::DecrementCounterForObjectClass(const UObject* InObject, FTypedElementCounter& InOutCounter)
{
	check(InObject);
	InOutCounter.DecrementCounter(NAME_Class, InObject->GetClass());
}
