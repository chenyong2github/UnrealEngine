// Copyright 1998-2019 Epic Games, Inc.All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FRenderTarget;
class FRHICommandListImmediate;
class FSceneRenderer;
class FTexture;
struct FResolveParams;
struct FGenerateMipsParams;

void UpdateSceneCaptureContentMobile_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	FSceneRenderer* SceneRenderer,
	FRenderTarget* RenderTarget,
	FTexture* RenderTargetTexture,
	const FString& OwnerName,
	const FResolveParams& ResolveParams,
	bool bGenerateMips,
	const FGenerateMipsParams& GenerateMipsParams);
