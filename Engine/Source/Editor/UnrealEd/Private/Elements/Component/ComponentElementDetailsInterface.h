// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDetailsInterface.h"
#include "ComponentElementDetailsInterface.generated.h"

UCLASS()
class UComponentElementDetailsInterface : public UTypedElementDetailsInterface
{
	GENERATED_BODY()

public:
	virtual bool IsTopLevelElement(const FTypedElementHandle& InElementHandle) override { return false; }
	virtual TUniquePtr<ITypedElementDetailsObject> GetDetailsObject(const FTypedElementHandle& InElementHandle) override;
};
