// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Actor/ActorElementAssetDataInterface.h"

#include "ActorElementEditorAssetDataInterface.generated.h"

UCLASS()
class UNREALED_API UActorElementEditorAssetDataInterface : public UActorElementAssetDataInterface
{
	GENERATED_BODY()

public:
	virtual TArray<FAssetData> GetAllReferencedAssetDatas(const FTypedElementHandle& InElementHandle) override;
};
