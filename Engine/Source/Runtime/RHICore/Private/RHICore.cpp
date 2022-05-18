// Copyright Epic Games, Inc. All Rights Reserved.

#include "RHICore.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogRHICore);
IMPLEMENT_MODULE(FDefaultModuleImpl, RHICore);

namespace UE
{
namespace RHICore
{

void ResolveRenderPassTargets(IRHICommandContext& Context, const FRHIRenderPassInfo& Info)
{
	const auto ResolveTexture = [&](FRHITexture* Target, FRHITexture* Resolve, uint8 MipIndex, int32 ArraySlice)
	{
		if (!Target || !Resolve || Target == Resolve)
		{
			return;
		}

		const FRHITextureDesc& TargetDesc  = Target->GetDesc();
		const FRHITextureDesc& ResolveDesc = Resolve->GetDesc();

		check(TargetDesc.IsTextureCube() == ResolveDesc.IsTextureCube());
		check(TargetDesc.IsMultisample() && !ResolveDesc.IsMultisample());
		check(!TargetDesc.IsTextureArray() || ArraySlice >= 0);

		ArraySlice = FMath::Max(ArraySlice, 0);

		int32 CubeFaceIndex = 0;

		if (TargetDesc.IsTextureCube())
		{
			CubeFaceIndex = ArraySlice % CubeFace_MAX;
			ArraySlice    = ArraySlice / CubeFace_MAX;
		}

		FResolveParams Params;
		Params.CubeFace          = (ECubeFace)CubeFaceIndex;
		Params.Rect              = Info.ResolveRect;
		Params.DestRect          = Info.ResolveRect;
		Params.MipIndex          = MipIndex;
		Params.SourceAccessFinal = ERHIAccess::RTV;
		Params.DestAccessFinal   = ERHIAccess::ResolveDst;
		Params.SourceArrayIndex  = ArraySlice;
		Params.DestArrayIndex    = ArraySlice;

		Context.RHICopyToResolveTarget(Target, Resolve, Params);
	};

	for (int32 Index = 0; Index < MaxSimultaneousRenderTargets; ++Index)
	{
		const auto& RTV = Info.ColorRenderTargets[Index];
		ResolveTexture(RTV.RenderTarget, RTV.ResolveTarget, RTV.MipIndex, RTV.ArraySlice);
	}

	const auto& DSV = Info.DepthStencilRenderTarget;
	ResolveTexture(DSV.DepthStencilTarget, DSV.ResolveTarget, 0, 0);
}

void ResolveRenderPassTargets(const FRHIRenderPassInfo& RenderPassInfo, TFunction<void(FResolveTextureInfo)> ResolveFunction)
{
	const auto ResolveTexture = [&](FResolveTextureInfo ResolveInfo)
	{
		if (!ResolveInfo.SourceTexture || !ResolveInfo.DestTexture || ResolveInfo.SourceTexture == ResolveInfo.DestTexture)
		{
			return;
		}

		const FRHITextureDesc& SourceDesc = ResolveInfo.SourceTexture->GetDesc();
		const FRHITextureDesc& DestDesc   = ResolveInfo.DestTexture->GetDesc();

		check(SourceDesc.Format == DestDesc.Format);
		check(SourceDesc.Extent == DestDesc.Extent);
		check(SourceDesc.IsMultisample() && !DestDesc.IsMultisample());
		check(SourceDesc.Format != PF_DepthStencil || (SourceDesc.IsTexture2D() && DestDesc.IsTexture2D()));
		check(!SourceDesc.IsTexture3D() && !DestDesc.IsTexture3D());

		ResolveFunction(ResolveInfo);
	};

	for (int32 Index = 0; Index < MaxSimultaneousRenderTargets; ++Index)
	{
		const auto& RTV = RenderPassInfo.ColorRenderTargets[Index];
		ResolveTexture({ RTV.RenderTarget, RTV.ResolveTarget, RTV.MipIndex, RTV.ArraySlice, RenderPassInfo.ResolveRect });
	}

	const auto& DSV = RenderPassInfo.DepthStencilRenderTarget;
	ResolveTexture({ DSV.DepthStencilTarget, DSV.ResolveTarget, 0, 0, RenderPassInfo.ResolveRect });
}

} //! RHICore
} //! UE