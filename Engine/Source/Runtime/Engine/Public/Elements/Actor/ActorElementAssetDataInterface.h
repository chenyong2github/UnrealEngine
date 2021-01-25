// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementAssetDataInterface.h"

#include "ActorElementAssetDataInterface.generated.h"

UCLASS()
class ENGINE_API UActorElementAssetDataInterface : public UTypedElementAssetDataInterface
{
	GENERATED_BODY()

public:
	virtual FAssetData GetAssetData(const FTypedElementHandle& InElementHandle) override;
};
