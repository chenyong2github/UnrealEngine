// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Actor/ActorElementWorldInterface.h"
#include "ActorElementEditorWorldInterface.generated.h"

UCLASS()
class UActorElementEditorWorldInterface : public UActorElementWorldInterface
{
	GENERATED_BODY()

public:
	virtual void NotifyMovementStarted(const FTypedElementHandle& InElementHandle) override;
	virtual void NotifyMovementOngoing(const FTypedElementHandle& InElementHandle) override;
	virtual void NotifyMovementEnded(const FTypedElementHandle& InElementHandle) override;
	virtual void DuplicateElements(TArrayView<const FTypedElementHandle> InElementHandles, UWorld* InWorld, bool bOffsetLocations, TArray<FTypedElementHandle>& OutNewElements) override;
};
