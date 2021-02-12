// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Actor/ActorElementSelectionInterface.h"
#include "ActorElementEditorSelectionInterface.generated.h"

class AActor;

UCLASS()
class UActorElementEditorSelectionInterface : public UActorElementSelectionInterface
{
	GENERATED_BODY()

public:
	virtual bool IsElementSelected(const FTypedElementHandle& InElementHandle, const UTypedElementList* InSelectionSet, const FTypedElementIsSelectedOptions& InSelectionOptions) override;
	virtual bool ShouldPreventTransactions(const FTypedElementHandle& InElementHandle) override;
	virtual TUniquePtr<ITypedElementTransactedElement> CreateTransactedElementImpl() override;

	static bool IsActorSelected(const AActor* InActor, const UTypedElementList* InSelectionSet, const FTypedElementIsSelectedOptions& InSelectionOptions);
};
