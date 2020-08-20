// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "SceneViewExtension.h"
#include "RHI.h"
#include "RHIResources.h"

class UColorCorrectRegionsSubsystem;
class UMaterialInterface;
class FRDGTexture;

class FColorCorrectRegionsSceneViewExtension : public FSceneViewExtensionBase
{
public:
	FColorCorrectRegionsSceneViewExtension(const FAutoRegister& AutoRegister, UColorCorrectRegionsSubsystem* InWorldSubsystem);
	
	//~ Begin FSceneViewExtensionBase Interface
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {};
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {};
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {};
	virtual void PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) override {};
	virtual void PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) override {};
	virtual void PostRenderBasePass_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) override {};
	virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs) override;
	//~ End FSceneViewExtensionBase Interface

private:
	UColorCorrectRegionsSubsystem* WorldSubsystem;
};
	