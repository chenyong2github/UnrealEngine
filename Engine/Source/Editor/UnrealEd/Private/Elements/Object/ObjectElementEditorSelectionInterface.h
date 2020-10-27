// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Object/ObjectElementSelectionInterface.h"
#include "ObjectElementEditorSelectionInterface.generated.h"

UCLASS()
class UObjectElementEditorSelectionInterface : public UObjectElementSelectionInterface
{
	GENERATED_BODY()

public:
	virtual bool ShouldPreventTransactions(const FTypedElementHandle& InElementHandle) override;
	virtual void WriteTransactedElement(const FTypedElementHandle& InElementHandle, FArchive& InArchive) override;
	virtual FTypedElementHandle ReadTransactedElement(FArchive& InArchive) override;

	static bool ShouldObjectPreventTransactions(const UObject* InObject);
	static void WriteTransactedObject(const UObject* InObject, FArchive& InArchive);
	static FTypedElementHandle ReadTransactedObject(FArchive& InArchive, TFunctionRef<FTypedElementHandle(const UObject*)> InFindElementForObject);
};
