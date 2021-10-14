// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDetailsInterface.h"
#include "ActorElementDetailsInterface.generated.h"

UCLASS()
class UNREALED_API UActorElementDetailsInterface : public UObject, public ITypedElementDetailsInterface
{
	GENERATED_BODY()

public:
	virtual TUniquePtr<ITypedElementDetailsObject> GetDetailsObject(const FTypedElementHandle& InElementHandle) override;
};
