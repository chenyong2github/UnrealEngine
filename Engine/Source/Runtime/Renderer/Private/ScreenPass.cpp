// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ScreenPass.h"
#include "EngineGlobals.h"
#include "ScenePrivate.h"
#include "RendererModule.h"
#include "IXRTrackingSystem.h"
#include "IHeadMountedDisplay.h"

IMPLEMENT_GLOBAL_SHADER(FScreenPassVS, "/Engine/Private/ScreenPass.usf", "ScreenPassVS", SF_Vertex);

const FTextureRHIRef& GetMiniFontTexture()
{
	if (GEngine->MiniFontTexture)
	{
		return GEngine->MiniFontTexture->Resource->TextureRHI;
	}
	else
	{
		return GSystemTextures.WhiteDummy->GetRenderTargetItem().TargetableTexture;
	}
}

bool IsHMDHiddenAreaMaskActive()
{
	// Query if we have a custom HMD post process mesh to use
	static const auto* const HiddenAreaMaskCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("vr.HiddenAreaMask"));

	return (HiddenAreaMaskCVar != nullptr &&
		HiddenAreaMaskCVar->GetValueOnRenderThread() == 1 &&
		GEngine &&
		GEngine->XRSystem.IsValid() && GEngine->XRSystem->GetHMDDevice() &&
		GEngine->XRSystem->GetHMDDevice()->HasVisibleAreaMesh());
}

FScreenPassCommonParameters GetScreenPassCommonParameters(FRHICommandListImmediate& RHICmdList, const FViewInfo& View)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	const FIntRect ViewportRect = View.ViewRect;
	const FIntPoint ViewportOffset = ViewportRect.Min;
	const FIntPoint ViewportExtent = ViewportRect.Size();

	FScreenPassCommonParameters Parameters;
	Parameters.ViewportRect = ViewportRect;
	Parameters.ViewportSize = FVector4(
		ViewportExtent.X,
		ViewportExtent.Y,
		1.0f / ViewportExtent.X,
		1.0f / ViewportExtent.Y);

	Parameters.ScreenPosToPixelValue = FVector4(
		ViewportExtent.X * 0.5f,
		-ViewportExtent.Y * 0.5f,
		ViewportExtent.X * 0.5f - 0.5f + ViewportOffset.X,
		ViewportExtent.Y * 0.5f - 0.5f + ViewportOffset.Y);

	Parameters.BilinearTextureSampler0 = TStaticSamplerState<SF_Bilinear>::GetRHI();

	Parameters.ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters.SceneUniformBuffer = CreateSceneTextureUniformBuffer(
		SceneContext, View.FeatureLevel, ESceneTextureSetupMode::All, EUniformBufferUsage::UniformBuffer_SingleFrame);

	return Parameters;
}

FScreenPassInput GetScreenPassInputParameters(FRDGTextureRef Texture, FSamplerStateRHIParamRef SamplerState)
{
	check(Texture);
	check(SamplerState);

	const FVector2D Size(Texture->Desc.Extent.X, Texture->Desc.Extent.Y);

	FScreenPassInput Input;
	Input.Size = FVector4(Size.X, Size.Y, 1.0f / Size.X, 1.0f / Size.Y);
	Input.Texture = Texture;
	Input.Sampler = SamplerState;
	return Input;
}

FScreenPassContext* FScreenPassContext::Create(FRHICommandListImmediate& RHICmdList, const FViewInfo& InView)
{
	return new(FMemStack::Get()) FScreenPassContext(RHICmdList, InView);
}

FScreenPassContext::FScreenPassContext(FRHICommandListImmediate& RHICmdList, const FViewInfo& InView)
	: View(InView)
	, ViewFamily(*View.Family)
	, ViewportRect(View.ViewRect)
	, StereoPass(View.StereoPass)
	, bHasHMDMask(IsHMDHiddenAreaMaskActive())
	, ShaderMap(View.ShaderMap)
	, ScreenPassVS(View.ShaderMap)
	, ScreenPassCommonParameters(GetScreenPassCommonParameters(RHICmdList, View))
{}