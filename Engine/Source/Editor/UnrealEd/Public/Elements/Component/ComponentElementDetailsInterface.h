// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDetailsInterface.h"
#include "ComponentElementDetailsInterface.generated.h"

UCLASS()
class UNREALED_API UComponentElementDetailsInterface : public UObject, public ITypedElementDetailsInterface
{
	GENERATED_BODY()

public:
	virtual bool IsTopLevelElement(const FTypedElementHandle& InElementHandle) override { return false; }
	virtual TUniquePtr<ITypedElementDetailsObject> GetDetailsObject(const FTypedElementHandle& InElementHandle) override;
};
