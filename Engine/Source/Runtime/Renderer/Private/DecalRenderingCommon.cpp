// Copyright Epic Games, Inc. All Rights Reserved.

#include "DecalRenderingCommon.h"
#include "RenderUtils.h"

namespace DecalRendering
{
	// Legacy logic involving EDecalBlendMode.
	// EDecalBlendMode will soon be replaced and this code will be removed. But submitting this intermediate step for later reference.
	void InitBlendDesc(EShaderPlatform Platform, EDecalBlendMode DecalBlendMode, bool bWriteNormal, bool bWriteEmissive, FDecalBlendDesc& Desc)
	{
		const bool bIsMobilePlatform = IsMobilePlatform(Platform);
		const bool bIsMobileDeferredPlatform = bIsMobilePlatform && IsMobileDeferredShadingEnabled(Platform);
		const bool bIsDBufferPlatform = !bIsMobilePlatform && IsUsingDBuffers(Platform);
		const bool bIsDBufferMaskPlatform = bIsDBufferPlatform && IsUsingPerPixelDBufferMask(Platform);
		const bool bIsForwardPlatform = IsAnyForwardShadingEnabled(Platform);

		// Convert DBuffer modes to GBuffer.
		bool bWriteBaseColor = true;
		bool bWriteRoughnessSpecularMetallic = true;

		if (!bIsDBufferPlatform && !bIsMobilePlatform)
		{
			switch (DecalBlendMode)
			{
			case DBM_DBuffer_ColorNormalRoughness:
				DecalBlendMode = DBM_Translucent;
				break;
			case DBM_DBuffer_Color:
				DecalBlendMode = DBM_Translucent;
				bWriteNormal = false;
				bWriteRoughnessSpecularMetallic = false;
				break;
			case DBM_DBuffer_ColorNormal:
				DecalBlendMode = DBM_Translucent;
				bWriteRoughnessSpecularMetallic = false;
				break;
			case DBM_DBuffer_ColorRoughness:
				DecalBlendMode = DBM_Translucent;
				bWriteNormal = false;
				break;
			case DBM_DBuffer_NormalRoughness:
				DecalBlendMode = DBM_Translucent;
				bWriteBaseColor = false;
				break;
			case DBM_DBuffer_Normal:
				DecalBlendMode = DBM_Translucent;
				bWriteBaseColor = false;
				bWriteRoughnessSpecularMetallic = false;
				break;
			}
		}

		// Convert GBuffer modes to DBuffer.
		uint32 DBufferStageMask = 1 << (uint32)EDecalRenderStage::BeforeBasePass;
		DBufferStageMask |= bWriteEmissive ? (1 << (uint32)EDecalRenderStage::Emissive) : 0;

		if (bIsDBufferPlatform && bIsForwardPlatform && !bIsMobilePlatform)
		{
			switch (DecalBlendMode)
			{
			case DBM_Translucent:
			case DBM_Stain:
				DecalBlendMode = DBM_DBuffer_ColorNormalRoughness;
				break;
			case DBM_Normal:
				DecalBlendMode = DBM_DBuffer_Normal;
				DBufferStageMask = 1 << (uint32)EDecalRenderStage::BeforeBasePass;
				break;
			case DBM_Emissive:
				DecalBlendMode = DBM_DBuffer_Emissive;
				break;
			case DBM_AlphaComposite:
				DecalBlendMode = DBM_DBuffer_AlphaComposite;
				break;
			}
		}

		// Fill out FDecalBlendDesc.
		switch (DecalBlendMode)
		{
		case DBM_AlphaComposite:
			Desc.BlendMode = BLEND_AlphaComposite;
			Desc.bWriteBaseColor = bWriteBaseColor;
			Desc.bWriteRoughnessSpecularMetallic = bWriteRoughnessSpecularMetallic;
			Desc.bWriteEmissive = bWriteEmissive;
			Desc.RenderStageMask = 1 << (uint32)EDecalRenderStage::BeforeLighting;
			break;
		case DBM_Stain:
			Desc.BlendMode = BLEND_Modulate;
			Desc.bWriteBaseColor = bWriteBaseColor;
			Desc.bWriteNormal = bWriteNormal;
			Desc.bWriteRoughnessSpecularMetallic = bWriteRoughnessSpecularMetallic;
			Desc.bWriteEmissive = bWriteEmissive;
			Desc.RenderStageMask = 1 << (uint32)EDecalRenderStage::BeforeLighting;
			break;
		case DBM_Translucent:
			Desc.BlendMode = BLEND_Translucent;
			Desc.bWriteBaseColor = bWriteBaseColor;
			Desc.bWriteNormal = bWriteNormal;
			Desc.bWriteRoughnessSpecularMetallic = bWriteRoughnessSpecularMetallic;
			Desc.bWriteEmissive = bWriteEmissive;
			Desc.RenderStageMask = 1 << (uint32)EDecalRenderStage::BeforeLighting;
			break;
		case DBM_Normal:
			Desc.BlendMode = BLEND_Translucent;
			Desc.bWriteNormal = true;
			Desc.RenderStageMask = 1 << (uint32)EDecalRenderStage::BeforeLighting;
			break;
		case DBM_Emissive:
			Desc.BlendMode = BLEND_Translucent;
			Desc.bWriteEmissive = true;
			Desc.RenderStageMask = 1 << (uint32)EDecalRenderStage::BeforeLighting;
			break;
		case DBM_DBuffer_ColorNormalRoughness:
			Desc.BlendMode = BLEND_Translucent;
			Desc.bWriteBaseColor = Desc.bWriteNormal = Desc.bWriteRoughnessSpecularMetallic = true;
			Desc.bWriteEmissive = bWriteEmissive;
			Desc.bWriteDBufferMask = bIsDBufferMaskPlatform;
			Desc.RenderStageMask = DBufferStageMask;
			break;
		case DBM_DBuffer_Color:
			Desc.BlendMode = BLEND_Translucent;
			Desc.bWriteBaseColor = true;
			Desc.bWriteEmissive = bWriteEmissive;
			Desc.bWriteDBufferMask = bIsDBufferMaskPlatform;
			Desc.RenderStageMask = DBufferStageMask;
			break;
		case DBM_DBuffer_ColorNormal:
			Desc.BlendMode = BLEND_Translucent;
			Desc.bWriteBaseColor = Desc.bWriteNormal = true;
			Desc.bWriteEmissive = bWriteEmissive;
			Desc.bWriteDBufferMask = bIsDBufferMaskPlatform;
			Desc.RenderStageMask = DBufferStageMask;
			break;
		case DBM_DBuffer_ColorRoughness:
			Desc.BlendMode = BLEND_Translucent;
			Desc.bWriteBaseColor = Desc.bWriteRoughnessSpecularMetallic = true;
			Desc.bWriteEmissive = bWriteEmissive;
			Desc.bWriteDBufferMask = bIsDBufferMaskPlatform;
			Desc.RenderStageMask = DBufferStageMask;
			break;
		case DBM_DBuffer_Normal:
			Desc.BlendMode = BLEND_Translucent;
			Desc.bWriteNormal = true;
			Desc.bWriteEmissive = bWriteEmissive;
			Desc.bWriteDBufferMask = bIsDBufferMaskPlatform;
			Desc.RenderStageMask = DBufferStageMask;
			break;
		case DBM_DBuffer_NormalRoughness:
			Desc.BlendMode = BLEND_Translucent;
			Desc.bWriteNormal = Desc.bWriteRoughnessSpecularMetallic = true;
			Desc.bWriteEmissive = bWriteEmissive;
			Desc.bWriteDBufferMask = bIsDBufferMaskPlatform;
			Desc.RenderStageMask = DBufferStageMask;
			break;
		case DBM_DBuffer_Roughness:
			Desc.BlendMode = BLEND_Translucent;
			Desc.bWriteRoughnessSpecularMetallic = true;
			Desc.bWriteEmissive = bWriteEmissive;
			Desc.bWriteDBufferMask = bIsDBufferMaskPlatform;
			Desc.RenderStageMask = DBufferStageMask;
			break;
		case DBM_DBuffer_Emissive:
			Desc.BlendMode = BLEND_Translucent;
			Desc.bWriteEmissive = true;
			Desc.RenderStageMask = 1 << (uint32)EDecalRenderStage::Emissive;
			break;
		case DBM_DBuffer_AlphaComposite:
			Desc.BlendMode = BLEND_AlphaComposite;
			Desc.bWriteBaseColor = Desc.bWriteRoughnessSpecularMetallic = true;
			Desc.bWriteEmissive = bWriteEmissive;
			Desc.bWriteDBufferMask = bIsDBufferMaskPlatform;
			Desc.RenderStageMask = DBufferStageMask;
			break;
		case DBM_Volumetric_DistanceFunction:
			// Ignore
			break;
		case DBM_AmbientOcclusion:
			Desc.BlendMode = BLEND_Translucent;
			Desc.bWriteAmbientOcclusion = true;
			Desc.RenderStageMask = 1 << (uint32)EDecalRenderStage::AmbientOcclusion;
			break;
		}

		// Fixup for Mobile.
		if (bIsMobileDeferredPlatform)
		{
			Desc.bWriteAmbientOcclusion = false;
			Desc.bWriteDBufferMask = false;
			Desc.RenderStageMask = Desc.bWriteEmissive || Desc.bWriteBaseColor || Desc.bWriteNormal || Desc.bWriteRoughnessSpecularMetallic ? 1 << (uint32)EDecalRenderStage::MobileBeforeLighting : 0;
		}
		else if (bIsMobilePlatform)
		{
			Desc.bWriteNormal = false;
			Desc.bWriteRoughnessSpecularMetallic = false;
			Desc.bWriteAmbientOcclusion = false;
			Desc.bWriteDBufferMask = false;
			Desc.BlendMode = Desc.bWriteEmissive ? BLEND_Translucent : Desc.BlendMode;
			Desc.RenderStageMask = Desc.bWriteEmissive || Desc.bWriteBaseColor ? 1 << (uint32)EDecalRenderStage::Mobile : 0;
		}
	}

	FDecalBlendDesc ComputeDecalBlendDesc(EShaderPlatform Platform, FMaterial const* Material)
	{
		FDecalBlendDesc Desc;
		InitBlendDesc(Platform, (EDecalBlendMode)Material->GetDecalBlendMode(), Material->HasNormalConnected(), Material->HasEmissiveColorConnected(), Desc);
		return Desc;
	}

	FDecalBlendDesc ComputeDecalBlendDesc(EShaderPlatform Platform, FMaterialShaderParameters const& MaterialShaderParameters)
	{
		FDecalBlendDesc Desc;
		InitBlendDesc(Platform, (EDecalBlendMode)MaterialShaderParameters.DecalBlendMode, MaterialShaderParameters.bHasNormalConnected, MaterialShaderParameters.bHasEmissiveColorConnected, Desc);
		return Desc;
	}

	bool IsCompatibleWithRenderStage(FDecalBlendDesc DecalBlendDesc, EDecalRenderStage DecalRenderStage)
	{
		return (DecalBlendDesc.RenderStageMask & (1 << (uint32)DecalRenderStage)) != 0;
	}

	EDecalRenderStage GetBaseRenderStage(FDecalBlendDesc DecalBlendDesc)
	{
		if (DecalBlendDesc.RenderStageMask & (1 << (uint32)EDecalRenderStage::BeforeBasePass))
		{
			return EDecalRenderStage::BeforeBasePass;
		}
		if (DecalBlendDesc.RenderStageMask & (1 << (uint32)EDecalRenderStage::BeforeLighting))
		{
			return EDecalRenderStage::BeforeLighting;
		}
		if (DecalBlendDesc.RenderStageMask & (1 << (uint32)EDecalRenderStage::Mobile))
		{
			return EDecalRenderStage::Mobile;
		}
		if (DecalBlendDesc.RenderStageMask & (1 << (uint32)EDecalRenderStage::MobileBeforeLighting))
		{
			return EDecalRenderStage::MobileBeforeLighting;
		}

		return EDecalRenderStage::None;
	}

	EDecalRenderTargetMode GetRenderTargetMode(FDecalBlendDesc DecalBlendDesc, EDecalRenderStage DecalRenderStage)
	{
		switch(DecalRenderStage)
		{
			case EDecalRenderStage::BeforeBasePass:
				return EDecalRenderTargetMode::DBuffer;
			case EDecalRenderStage::BeforeLighting:
				return DecalBlendDesc.bWriteNormal ? EDecalRenderTargetMode::SceneColorAndGBuffer : EDecalRenderTargetMode::SceneColorAndGBufferNoNormal;
			case EDecalRenderStage::Mobile:
				return EDecalRenderTargetMode::SceneColor;
			case EDecalRenderStage::MobileBeforeLighting:
				return EDecalRenderTargetMode::SceneColorAndGBuffer;
			case EDecalRenderStage::Emissive:
				return EDecalRenderTargetMode::SceneColor;
			case EDecalRenderStage::AmbientOcclusion:
				return EDecalRenderTargetMode::AmbientOcclusion;
		}

		return EDecalRenderTargetMode::None;
	}

	uint32 GetRenderTargetCount(FDecalBlendDesc DecalBlendDesc, EDecalRenderTargetMode RenderTargetMode)
	{
		switch (RenderTargetMode)
		{
		case EDecalRenderTargetMode::DBuffer:
			return DecalBlendDesc.bWriteDBufferMask ? 4 : 3;
		case EDecalRenderTargetMode::SceneColorAndGBuffer:
			return 4;
		case EDecalRenderTargetMode::SceneColorAndGBufferNoNormal:
			return 3;
		case EDecalRenderTargetMode::SceneColor:
			return 1;
		case EDecalRenderTargetMode::AmbientOcclusion:
			return 1;
		}

		return 0;
	}

	uint32 GetRenderTargetWriteMask(FDecalBlendDesc DecalBlendDesc, EDecalRenderStage DecalRenderStage, EDecalRenderTargetMode RenderTargetMode)
	{
		if (RenderTargetMode == EDecalRenderTargetMode::DBuffer)
		{
			return (DecalBlendDesc.bWriteBaseColor ? 1 : 0) + (DecalBlendDesc.bWriteNormal ? 2 : 0) + (DecalBlendDesc.bWriteRoughnessSpecularMetallic ? 4 : 0) + (DecalBlendDesc.bWriteDBufferMask ? 8 : 0);
		}
		else if (RenderTargetMode == EDecalRenderTargetMode::SceneColorAndGBuffer)
		{
			return (DecalBlendDesc.bWriteEmissive ? 1 : 0) + (DecalBlendDesc.bWriteNormal ? 2 : 0) + (DecalBlendDesc.bWriteRoughnessSpecularMetallic ? 4 : 0) + (DecalBlendDesc.bWriteBaseColor ? 8 : 0);
		}
		else if (RenderTargetMode == EDecalRenderTargetMode::SceneColorAndGBufferNoNormal)
		{
			return (DecalBlendDesc.bWriteEmissive ? 1 : 0) + (DecalBlendDesc.bWriteRoughnessSpecularMetallic ? 2 : 0) + (DecalBlendDesc.bWriteBaseColor ? 4 : 0);
		}
		else if (RenderTargetMode == EDecalRenderTargetMode::SceneColor)
		{
			if (DecalRenderStage == EDecalRenderStage::Mobile)
			{
				return ((DecalBlendDesc.bWriteEmissive || DecalBlendDesc.bWriteBaseColor) ? 1 : 0);
			}
			return (DecalBlendDesc.bWriteEmissive ? 1 : 0);
		}
		else if (RenderTargetMode == EDecalRenderTargetMode::AmbientOcclusion)
		{
			return (DecalBlendDesc.bWriteAmbientOcclusion ? 1 : 0);
		}

		// Enable all render targets by default.
		return (1 << GetRenderTargetCount(DecalBlendDesc, RenderTargetMode)) - 1;
	}

	FRHIBlendState* GetDecalBlendState_DBuffer(FDecalBlendDesc DecalBlendDesc, EDecalRenderStage DecalRenderStage)
	{
		// Ignore DBuffer mask bit and always set that MRT active.
		const uint32 Mask = GetRenderTargetWriteMask(DecalBlendDesc, DecalRenderStage, EDecalRenderTargetMode::DBuffer) & 0x7;

		if (DecalBlendDesc.BlendMode == BLEND_Translucent)
		{
			switch (Mask)
			{
			case 1:
				return TStaticBlendState<
					CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha, // BaseColor
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,										// Normal
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,										// Metallic, Specular, Roughness
					CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One											// DBuffer mask
				>::GetRHI();
			case 2:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,										// BaseColor
					CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha, // Normal
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,										// Metallic, Specular, Roughness
					CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One											// DBuffer mask
				>::GetRHI();
			case 3:
				return TStaticBlendState<
					CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha, // BaseColor
					CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha,	// Normal
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,										// Metallic, Specular, Roughness
					CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One											// DBuffer mask
				>::GetRHI();
			case 4:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,										// BaseColor
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,										// Normal
					CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha,	// Metallic, Specular, Roughness
					CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One											// DBuffer mask
				>::GetRHI();
			case 5:
				return TStaticBlendState<
					CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha, // BaseColor
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,										// Normal
					CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha,	// Metallic, Specular, Roughness
					CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One											// DBuffer mask
				>::GetRHI();
			case 6:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,										// BaseColor
					CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha,	// Normal
					CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha,	// Metallic, Specular, Roughness
					CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One											// DBuffer mask
				>::GetRHI();
			case 7:
				return TStaticBlendState<
					CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha,	// BaseColor
					CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha,	// Normal
					CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha,	// Metallic, Specular, Roughness
					CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One											// DBuffer mask
				>::GetRHI();
			}
		}
		else if (DecalBlendDesc.BlendMode == BLEND_AlphaComposite)
		{
			ensure((Mask & 2) == 0); // AlphaComposite shouldn't write normal.

			switch (Mask)
			{
			case 1:
				return TStaticBlendState<
					CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha,			// BaseColor
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,										// Normal
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,										// Metallic, Specular, Roughness
					CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One											// DBuffer mask
				>::GetRHI();
			case 4:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,										// BaseColor
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,										// Normal
					CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha,			// Metallic, Specular, Roughness
					CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One											// DBuffer mask
				>::GetRHI();
			case 5:
				return TStaticBlendState<
					CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha,			// BaseColor
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,										// Normal
					CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha,			// Metallic, Specular, Roughness
					CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One											// DBuffer mask
				>::GetRHI();
			}
		}

		ensure(0);
		return TStaticBlendState<>::GetRHI();
	}

	FRHIBlendState* GetDecalBlendState_SceneColorAndGBuffer(FDecalBlendDesc DecalBlendDesc, EDecalRenderStage DecalRenderStage)
	{
		const uint32 Mask = GetRenderTargetWriteMask(DecalBlendDesc, DecalRenderStage, EDecalRenderTargetMode::SceneColorAndGBuffer);

		if (DecalBlendDesc.BlendMode == BLEND_Translucent)
		{
			switch (Mask)
			{
			case 1:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Normal
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Metallic, Specular, Roughness
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One						// BaseColor
				>::GetRHI();
			case 2:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Emissive
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Normal
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Metallic, Specular, Roughness
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One						// BaseColor
				>::GetRHI();
			case 3:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Normal
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Metallic, Specular, Roughness
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One						// BaseColor
				>::GetRHI();
			case 4:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Normal
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One						// BaseColor
				>::GetRHI();
			case 5:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Normal
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One						// BaseColor
				>::GetRHI();
			case 6:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Emissive
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Normal
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One						// BaseColor
				>::GetRHI();
			case 7:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Normal
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One						// BaseColor
				>::GetRHI();
			case 8:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Normal
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
				>::GetRHI();
			case 9:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Normal
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
				>::GetRHI();
			case 10:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Emissive
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Normal
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
				>::GetRHI();
			case 11:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Normal
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
				>::GetRHI();
			case 12:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Normal
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
				>::GetRHI();
			case 13:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Normal
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
				>::GetRHI();
			case 14:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Emissive
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Normal
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
				>::GetRHI();
			case 15:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Normal
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
				>::GetRHI();
			}
		}
		else if (DecalBlendDesc.BlendMode == BLEND_AlphaComposite)
		{
			ensure((Mask & 2) == 0); // AlphaComposite shouldn't write normal.

			switch (Mask)
			{
			case 1:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Normal
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Metallic, Specular, Roughness
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One						// BaseColor
				>::GetRHI();
			case 4:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Normal
					CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,			// Metallic, Specular, Roughness
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One						// BaseColor
				>::GetRHI();
			case 5:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Normal
					CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,			// Metallic, Specular, Roughness
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One						// BaseColor
				>::GetRHI();
			case 8:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Normal
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One			// BaseColor
				>::GetRHI();
			case 9:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Normal
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One			// BaseColor
				>::GetRHI();
			case 12:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Normal
					CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,			// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One			// BaseColor
				>::GetRHI();
			case 13:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Normal
					CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,			// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One			// BaseColor
				>::GetRHI();
			}
		}
		else if (DecalBlendDesc.BlendMode == BLEND_Modulate)
		{
			switch (Mask)
			{
			case 1:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Normal
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Metallic, Specular, Roughness
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One						// BaseColor
				>::GetRHI();
			case 2:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Emissive
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Normal
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Metallic, Specular, Roughness
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One						// BaseColor
				>::GetRHI();
			case 3:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Normal
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Metallic, Specular, Roughness
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One						// BaseColor
				>::GetRHI();
			case 4:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Normal
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One						// BaseColor
				>::GetRHI();
			case 5:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Normal
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One						// BaseColor
				>::GetRHI();
			case 6:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Emissive
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Normal
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One						// BaseColor
				>::GetRHI();
			case 7:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Normal
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One						// BaseColor
				>::GetRHI();
			case 8:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Normal
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_DestColor, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
				>::GetRHI();
			case 9:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Normal
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_DestColor, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
				>::GetRHI();
			case 10:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Emissive
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Normal
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_DestColor, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
				>::GetRHI();
			case 11:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Normal
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_DestColor, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
				>::GetRHI();
			case 12:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Normal
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_DestColor, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
				>::GetRHI();
			case 13:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Normal
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_DestColor, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
				>::GetRHI();
			case 14:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Emissive
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Normal
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_DestColor, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
				>::GetRHI();
			case 15:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Normal
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_DestColor, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
				>::GetRHI();
			}
		}

		ensure(0);
		return TStaticBlendState<>::GetRHI();
	}

	FRHIBlendState* GetDecalBlendState_SceneColorAndGBufferNoNormal(FDecalBlendDesc DecalBlendDesc, EDecalRenderStage DecalRenderStage)
	{
		const uint32 Mask = GetRenderTargetWriteMask(DecalBlendDesc, DecalRenderStage, EDecalRenderTargetMode::SceneColorAndGBufferNoNormal);

		if (DecalBlendDesc.BlendMode == BLEND_Translucent)
		{
			switch (Mask)
			{
			case 1:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Metallic, Specular, Roughness
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One						// BaseColor
				>::GetRHI();
			case 2:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Emissive
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One						// BaseColor
				>::GetRHI();
			case 3:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One						// BaseColor
				>::GetRHI();
			case 4:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
				>::GetRHI();
			case 5:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
				>::GetRHI();
			case 6:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Emissive
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
				>::GetRHI();
			case 7:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
				>::GetRHI();
			}
		}
		else if (DecalBlendDesc.BlendMode == BLEND_AlphaComposite)
		{
			switch (Mask)
			{
			case 1:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Metallic, Specular, Roughness
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One						// BaseColor
				>::GetRHI();
			case 2:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Emissive
					CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,			// Metallic, Specular, Roughness
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One						// BaseColor
				>::GetRHI();
			case 3:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,			// Metallic, Specular, Roughness
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One						// BaseColor
				>::GetRHI();
			case 4:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One			// BaseColor
				>::GetRHI();
			case 5:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One			// BaseColor
				>::GetRHI();
			case 6:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Emissive
					CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,			// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One			// BaseColor
				>::GetRHI();
			case 7:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,			// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One			// BaseColor
				>::GetRHI();
			}
		}
		else if (DecalBlendDesc.BlendMode == BLEND_Modulate)
		{
			switch (Mask)
			{
			case 1:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Metallic, Specular, Roughness
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One						// BaseColor
				>::GetRHI();
			case 2:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Emissive
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One						// BaseColor
				>::GetRHI();
			case 3:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One						// BaseColor
				>::GetRHI();
			case 4:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_DestColor, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
				>::GetRHI();
			case 5:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_DestColor, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
				>::GetRHI();
			case 6:
				return TStaticBlendState<
					CW_NONE, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						// Emissive
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_DestColor, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
				>::GetRHI();
			case 7:
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
					CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_DestColor, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
				>::GetRHI();
			}
		}

		ensure(0);
		return TStaticBlendState<>::GetRHI();
	}

	FRHIBlendState* GetDecalBlendState_SceneColor(FDecalBlendDesc DecalBlendDesc, EDecalRenderStage DecalRenderStage)
	{
		if (DecalRenderStage == EDecalRenderStage::Mobile)
		{
			if (DecalBlendDesc.bWriteEmissive)
			{
				// Treat blend as emissive
				return TStaticBlendState<CW_RGB, BO_Add, BF_SourceAlpha, BF_One>::GetRHI();
			}
			else
			{
				// Treat blend as non-emissive
				if (DecalBlendDesc.BlendMode == BLEND_Translucent)
				{
					return TStaticBlendState<CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha>::GetRHI();
				}
				else if (DecalBlendDesc.BlendMode == BLEND_AlphaComposite)
				{
					return TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
				}
				else if (DecalBlendDesc.BlendMode == BLEND_Modulate)
				{
					return TStaticBlendState<CW_RGB, BO_Add, BF_DestColor, BF_InverseSourceAlpha>::GetRHI();
				}
			}
		}
		else
		{
			return TStaticBlendState<CW_RGB, BO_Add, BF_SourceAlpha, BF_One>::GetRHI();
		}

		ensure(0);
		return TStaticBlendState<>::GetRHI();
	}

	FRHIBlendState* GetDecalBlendState_AmbientOcclusion(FDecalBlendDesc DecalBlendDesc, EDecalRenderStage DecalRenderStage)
	{
		// Modulate with AO target.
		return TStaticBlendState<CW_RED, BO_Add, BF_DestColor, BF_Zero>::GetRHI();
	}

	FRHIBlendState* GetDecalBlendState(FDecalBlendDesc DecalBlendDesc, EDecalRenderStage DecalRenderStage, EDecalRenderTargetMode RenderTargetMode)
	{
		// As we force the opacity in the shader we don't always _need_ to set different blend states per MRT.
		// But we want to give the driver as much information as possible about where output isn't required.
		// An alternative is to call SetRenderTarget per state change. But that is likely be slower (would need testing on various platforms to confirm that).

		switch(RenderTargetMode)
		{
			case EDecalRenderTargetMode::DBuffer:
				return GetDecalBlendState_DBuffer(DecalBlendDesc, DecalRenderStage);
			case EDecalRenderTargetMode::SceneColorAndGBuffer:
				return GetDecalBlendState_SceneColorAndGBuffer(DecalBlendDesc, DecalRenderStage);
			case EDecalRenderTargetMode::SceneColorAndGBufferNoNormal:
				return GetDecalBlendState_SceneColorAndGBufferNoNormal(DecalBlendDesc, DecalRenderStage);
			case EDecalRenderTargetMode::SceneColor:
				return GetDecalBlendState_SceneColor(DecalBlendDesc, DecalRenderStage);
			case EDecalRenderTargetMode::AmbientOcclusion:
				return GetDecalBlendState_AmbientOcclusion(DecalBlendDesc, DecalRenderStage);
		}

		return TStaticBlendState<>::GetRHI();
	}

	EDecalRasterizerState GetDecalRasterizerState(bool bInsideDecal, bool bIsInverted, bool ViewReverseCulling)
	{
		bool bClockwise = bInsideDecal;

		if (ViewReverseCulling)
		{
			bClockwise = !bClockwise;
		}

		if (bIsInverted)
		{
			bClockwise = !bClockwise;
		}
		
		return bClockwise ? EDecalRasterizerState::CW : EDecalRasterizerState::CCW;
	}

	FRHIRasterizerState* GetDecalRasterizerState(EDecalRasterizerState DecalRasterizerState)
	{
		switch (DecalRasterizerState)
		{
		case EDecalRasterizerState::CW:
			return TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI();
		case EDecalRasterizerState::CCW:
			return TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI();
		}

		check(0); 
		return nullptr;
	}

	void ModifyCompilationEnvironment(FDecalBlendDesc DecalBlendDesc, EDecalRenderStage DecalRenderStage, FShaderCompilerEnvironment& OutEnvironment)
	{
		DecalRenderStage = DecalRenderStage != EDecalRenderStage::None ? DecalRenderStage : GetBaseRenderStage(DecalBlendDesc);
		check(DecalRenderStage != EDecalRenderStage::None);

		const EDecalRenderTargetMode RenderTargetMode = GetRenderTargetMode(DecalBlendDesc, DecalRenderStage);
		check(RenderTargetMode != EDecalRenderTargetMode::None);

		const uint32 RenderTargetCount = GetRenderTargetCount(DecalBlendDesc, RenderTargetMode);
		const uint32 RenderTargetWriteMask = GetRenderTargetWriteMask(DecalBlendDesc, DecalRenderStage, RenderTargetMode);

		OutEnvironment.SetDefine(TEXT("IS_DECAL"), 1);
		OutEnvironment.SetDefine(TEXT("IS_DBUFFER_DECAL"), DecalRenderStage == EDecalRenderStage::BeforeBasePass ? 1 : 0);

		OutEnvironment.SetDefine(TEXT("DECAL_RENDERSTAGE"), (uint32)DecalRenderStage);
		OutEnvironment.SetDefine(TEXT("DECAL_RENDERTARGETMODE"), (uint32)RenderTargetMode);
		OutEnvironment.SetDefine(TEXT("DECAL_RENDERTARGET_COUNT"), RenderTargetCount);

		OutEnvironment.SetDefine(TEXT("DECAL_OUT_MRT0"), (RenderTargetWriteMask & 1) != 0 ? 1 : 0);
		OutEnvironment.SetDefine(TEXT("DECAL_OUT_MRT1"), (RenderTargetWriteMask & 2) != 0 ? 1 : 0);
		OutEnvironment.SetDefine(TEXT("DECAL_OUT_MRT2"), (RenderTargetWriteMask & 4) != 0 ? 1 : 0);
		OutEnvironment.SetDefine(TEXT("DECAL_OUT_MRT3"), (RenderTargetWriteMask & 8) != 0 ? 1 : 0);

		OutEnvironment.SetDefine(TEXT("DECAL_RENDERSTAGE_BEFOREBASEPASS"), (uint32)EDecalRenderStage::BeforeBasePass);
		OutEnvironment.SetDefine(TEXT("DECAL_RENDERSTAGE_BEFORELIGHTING"), (uint32)EDecalRenderStage::BeforeLighting);
		OutEnvironment.SetDefine(TEXT("DECAL_RENDERSTAGE_MOBILE"), (uint32)EDecalRenderStage::Mobile);
		OutEnvironment.SetDefine(TEXT("DECAL_RENDERSTAGE_MOBILEBEFORELIGHTING"), (uint32)EDecalRenderStage::MobileBeforeLighting);
		OutEnvironment.SetDefine(TEXT("DECAL_RENDERSTAGE_EMISSIVE"), (uint32)EDecalRenderStage::Emissive);
		OutEnvironment.SetDefine(TEXT("DECAL_RENDERSTAGE_AO"), (uint32)EDecalRenderStage::AmbientOcclusion);

		OutEnvironment.SetDefine(TEXT("DECAL_RENDERTARGETMODE_DBUFFER"), (uint32)EDecalRenderTargetMode::DBuffer);
		OutEnvironment.SetDefine(TEXT("DECAL_RENDERTARGETMODE_GBUFFER"), (uint32)EDecalRenderTargetMode::SceneColorAndGBuffer);
		OutEnvironment.SetDefine(TEXT("DECAL_RENDERTARGETMODE_GBUFFER_NONORMAL"), (uint32)EDecalRenderTargetMode::SceneColorAndGBufferNoNormal);
		OutEnvironment.SetDefine(TEXT("DECAL_RENDERTARGETMODE_SCENECOLOR"), (uint32)EDecalRenderTargetMode::SceneColor);
		OutEnvironment.SetDefine(TEXT("DECAL_RENDERTARGETMODE_AO"), (uint32)EDecalRenderTargetMode::AmbientOcclusion);
	}
}
