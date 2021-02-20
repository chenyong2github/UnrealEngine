// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/SMInstance/SMInstanceElementSelectionInterface.h"
#include "SMInstanceElementEditorSelectionInterface.generated.h"

UCLASS()
class USMInstanceElementEditorSelectionInterface : public USMInstanceElementSelectionInterface
{
	GENERATED_BODY()

public:
	virtual bool IsElementSelected(const FTypedElementHandle& InElementHandle, const UTypedElementList* InSelectionSet, const FTypedElementIsSelectedOptions& InSelectionOptions) override;
	virtual bool ShouldPreventTransactions(const FTypedElementHandle& InElementHandle) override;
	virtual TUniquePtr<ITypedElementTransactedElement> CreateTransactedElementImpl() override;
};
