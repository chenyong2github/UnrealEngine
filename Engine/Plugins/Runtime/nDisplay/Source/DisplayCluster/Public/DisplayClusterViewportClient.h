// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/GameViewportClient.h"

#include "DisplayClusterViewportClient.generated.h"

UCLASS()
class DISPLAYCLUSTER_API UDisplayClusterViewportClient
	: public UGameViewportClient
{
	GENERATED_BODY()

public:
	UDisplayClusterViewportClient(FVTableHelper& Helper);
	virtual ~UDisplayClusterViewportClient();

	virtual void Init(struct FWorldContext& WorldContext, UGameInstance* OwningGameInstance, bool bCreateNewAudioDevice = true) override;
	virtual void Draw(FViewport* Viewport, FCanvas* SceneCanvas) override;
};
