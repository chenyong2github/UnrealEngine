// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDetailsInterface.h"
#include "ObjectElementDetailsInterface.generated.h"

UCLASS()
class UObjectElementDetailsInterface : public UTypedElementDetailsInterface
{
	GENERATED_BODY()

public:
	virtual TUniquePtr<ITypedElementDetailsObject> GetDetailsObject(const FTypedElementHandle& InElementHandle) override;
};
