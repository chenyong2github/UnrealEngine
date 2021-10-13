// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Object/ObjectElementSelectionInterface.h"
#include "ObjectElementEditorSelectionInterface.generated.h"

UCLASS()
class UNREALED_API UObjectElementEditorSelectionInterface : public UObjectElementSelectionInterface
{
	GENERATED_BODY()

public:
	virtual bool ShouldPreventTransactions(const FTypedElementHandle& InElementHandle) override;
	virtual TUniquePtr<ITypedElementTransactedElement> CreateTransactedElementImpl() override;

	static bool ShouldObjectPreventTransactions(const UObject* InObject);
};
