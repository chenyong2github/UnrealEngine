// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Component/ComponentElementSelectionInterface.h"
#include "ComponentElementEditorSelectionInterface.generated.h"

class UActorComponent;

UCLASS()
class UComponentElementEditorSelectionInterface : public UComponentElementSelectionInterface
{
	GENERATED_BODY()

public:
	virtual bool IsElementSelected(const FTypedElementHandle& InElementHandle, const UTypedElementList* InSelectionSet, const FTypedElementIsSelectedOptions& InSelectionOptions) override;
	virtual bool ShouldPreventTransactions(const FTypedElementHandle& InElementHandle) override;
	virtual TUniquePtr<ITypedElementTransactedElement> CreateTransactedElementImpl() override;

	static bool IsComponentSelected(const UActorComponent* InComponent, const UTypedElementList* InSelectionSet, const FTypedElementIsSelectedOptions& InSelectionOptions);
};
