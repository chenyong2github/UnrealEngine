// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementWorldInterface.h"
#include "ActorElementWorldInterface.generated.h"

UCLASS()
class ENGINE_API UActorElementWorldInterface : public UTypedElementWorldInterface
{
	GENERATED_BODY()

public:
	virtual UWorld* GetOwnerWorld(const FTypedElementHandle& InElementHandle) override;
	virtual bool GetWorldBounds(const FTypedElementHandle& InElementHandle, FBoxSphereBounds& OutBounds) override;
	virtual bool GetWorldTransform(const FTypedElementHandle& InElementHandle, FTransform& OutTransform) override;
	virtual bool SetWorldTransform(const FTypedElementHandle& InElementHandle, const FTransform& InTransform) override;
};
