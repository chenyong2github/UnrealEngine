// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Component/ComponentElementWorldInterface.h"
#include "ComponentElementEditorWorldInterface.generated.h"

UCLASS()
class UComponentElementEditorWorldInterface : public UComponentElementWorldInterface
{
	GENERATED_BODY()

public:
	virtual void NotifyMovementStarted(const FTypedElementHandle& InElementHandle) override;
	virtual void NotifyMovementOngoing(const FTypedElementHandle& InElementHandle) override;
	virtual void NotifyMovementEnded(const FTypedElementHandle& InElementHandle) override;
};
