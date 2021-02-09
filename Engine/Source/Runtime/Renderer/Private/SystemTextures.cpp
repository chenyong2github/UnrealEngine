// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SystemTextures.cpp: System textures implementation.
=============================================================================*/

#include "SystemTextures.h"
#include "Math/RandomStream.h"
#include "Math/Sobol.h"
#include "RenderTargetPool.h"
#include "ClearQuad.h"
#include "LTC.h"

/*-----------------------------------------------------------------------------
SystemTextures
-----------------------------------------------------------------------------*/

/** The global render targets used for scene rendering. */
TGlobalResource<FSystemTextures> GSystemTextures;

void FSystemTextures::InitializeCommonTextures(FRHICommandListImmediate& RHICmdList)
{
	// First initialize textures that are common to all feature levels. This is always done the first time we come into this function, as doesn't care about the
	// requested feature level

		// Create a WhiteDummy texture
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(1, 1), PF_B8G8R8A8, FClearValueBinding::White, TexCreate_HideInVisualizeTexture, TexCreate_RenderTargetable | TexCreate_NoFastClear | TexCreate_ShaderResource, false));
			Desc.AutoWritable = false;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, WhiteDummy, TEXT("WhiteDummy"), ERenderTargetTransience::NonTransient);

			RHICmdList.Transition(FRHITransitionInfo(WhiteDummy->GetRenderTargetItem().TargetableTexture, ERHIAccess::SRVMask, ERHIAccess::RTV));

			FRHIRenderPassInfo RPInfo(WhiteDummy->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::Clear_Store);
			RHICmdList.BeginRenderPass(RPInfo, TEXT("WhiteDummy"));
			RHICmdList.EndRenderPass();
			RHICmdList.CopyToResolveTarget(WhiteDummy->GetRenderTargetItem().TargetableTexture, WhiteDummy->GetRenderTargetItem().ShaderResourceTexture, FResolveParams());

			WhiteDummySRV = RHICreateShaderResourceView((FRHITexture2D*)WhiteDummy->GetRenderTargetItem().ShaderResourceTexture.GetReference(), 0);
		}

		// Create a BlackDummy texture
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(1, 1), PF_B8G8R8A8, FClearValueBinding::Transparent, TexCreate_HideInVisualizeTexture, TexCreate_RenderTargetable | TexCreate_NoFastClear | TexCreate_ShaderResource, false));
			Desc.AutoWritable = false;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, BlackDummy, TEXT("BlackDummy"), ERenderTargetTransience::NonTransient);

			RHICmdList.Transition(FRHITransitionInfo(BlackDummy->GetRenderTargetItem().TargetableTexture, ERHIAccess::SRVMask, ERHIAccess::RTV));

			FRHIRenderPassInfo RPInfo(BlackDummy->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::Clear_Store);
			RHICmdList.BeginRenderPass(RPInfo, TEXT("BlackDummy"));
			RHICmdList.EndRenderPass();
			RHICmdList.CopyToResolveTarget(BlackDummy->GetRenderTargetItem().TargetableTexture, BlackDummy->GetRenderTargetItem().ShaderResourceTexture, FResolveParams());
		}
	

		// Create a texture that is a single UInt32 value set to 0
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(1,1), PF_R32_UINT, FClearValueBinding::Transparent, TexCreate_HideInVisualizeTexture, TexCreate_RenderTargetable | TexCreate_NoFastClear | TexCreate_ShaderResource, false));
			Desc.AutoWritable = false;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, ZeroUIntDummy, TEXT("ZeroUIntDummy"), ERenderTargetTransience::NonTransient);

			RHICmdList.Transition(FRHITransitionInfo(ZeroUIntDummy->GetRenderTargetItem().TargetableTexture, ERHIAccess::SRVMask, ERHIAccess::RTV));

			FRHIRenderPassInfo RPInfo(ZeroUIntDummy->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::Clear_Store);
			RHICmdList.BeginRenderPass(RPInfo, TEXT("ClearZeroUIntDummy"));
			RHICmdList.EndRenderPass();
			RHICmdList.CopyToResolveTarget(ZeroUIntDummy->GetRenderTargetItem().TargetableTexture, ZeroUIntDummy->GetRenderTargetItem().ShaderResourceTexture, FResolveParams());
		}

		// Create a BlackAlphaOneDummy texture
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(1, 1), PF_B8G8R8A8, FClearValueBinding::Black, TexCreate_HideInVisualizeTexture, TexCreate_RenderTargetable | TexCreate_NoFastClear | TexCreate_ShaderResource, false));
			Desc.AutoWritable = false;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, BlackAlphaOneDummy, TEXT("BlackAlphaOneDummy"), ERenderTargetTransience::NonTransient);

			RHICmdList.Transition(FRHITransitionInfo(BlackAlphaOneDummy->GetRenderTargetItem().TargetableTexture, ERHIAccess::SRVMask, ERHIAccess::RTV));

			FRHIRenderPassInfo RPInfo(BlackAlphaOneDummy->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::Clear_Store);
			RHICmdList.BeginRenderPass(RPInfo, TEXT("BlackAlphaOneDummy"));
			RHICmdList.EndRenderPass();
			RHICmdList.CopyToResolveTarget(BlackAlphaOneDummy->GetRenderTargetItem().TargetableTexture, BlackAlphaOneDummy->GetRenderTargetItem().ShaderResourceTexture, FResolveParams());
		}

		// Create a GreenDummy texture
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(1, 1), PF_B8G8R8A8, FClearValueBinding::Green, TexCreate_HideInVisualizeTexture, TexCreate_RenderTargetable | TexCreate_NoFastClear | TexCreate_ShaderResource, false));
			Desc.AutoWritable = false;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, GreenDummy, TEXT("GreenDummy"), ERenderTargetTransience::NonTransient);

			RHICmdList.Transition(FRHITransitionInfo(GreenDummy->GetRenderTargetItem().TargetableTexture, ERHIAccess::SRVMask, ERHIAccess::RTV));

			FRHIRenderPassInfo RPInfo(GreenDummy->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::Clear_Store);
			RHICmdList.BeginRenderPass(RPInfo, TEXT("GreenDummy"));
			RHICmdList.EndRenderPass();
			RHICmdList.CopyToResolveTarget(GreenDummy->GetRenderTargetItem().TargetableTexture, GreenDummy->GetRenderTargetItem().ShaderResourceTexture, FResolveParams());
		}

		// Create a DefaultNormal8Bit texture
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(1, 1), PF_B8G8R8A8, FClearValueBinding::DefaultNormal8Bit, TexCreate_HideInVisualizeTexture, TexCreate_RenderTargetable | TexCreate_NoFastClear | TexCreate_ShaderResource, false));
			Desc.AutoWritable = false;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, DefaultNormal8Bit, TEXT("DefaultNormal8Bit"), ERenderTargetTransience::NonTransient);

			RHICmdList.Transition(FRHITransitionInfo(DefaultNormal8Bit->GetRenderTargetItem().TargetableTexture, ERHIAccess::SRVMask, ERHIAccess::RTV));

			FRHIRenderPassInfo RPInfo(DefaultNormal8Bit->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::Clear_Store);
			RHICmdList.BeginRenderPass(RPInfo, TEXT("DefaultNormal8Bit"));
			RHICmdList.EndRenderPass();
			RHICmdList.CopyToResolveTarget(DefaultNormal8Bit->GetRenderTargetItem().TargetableTexture, DefaultNormal8Bit->GetRenderTargetItem().ShaderResourceTexture, FResolveParams());
		}

		// Create the PerlinNoiseGradient texture
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(128, 128), PF_B8G8R8A8, FClearValueBinding::None, TexCreate_HideInVisualizeTexture, TexCreate_None | TexCreate_NoFastClear | TexCreate_ShaderResource, false));
			Desc.AutoWritable = false;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, PerlinNoiseGradient, TEXT("PerlinNoiseGradient"), ERenderTargetTransience::NonTransient);
			// Write the contents of the texture.
			uint32 DestStride;
			uint8* DestBuffer = (uint8*)RHICmdList.LockTexture2D((FTexture2DRHIRef&)PerlinNoiseGradient->GetRenderTargetItem().ShaderResourceTexture, 0, RLM_WriteOnly, DestStride, false);
			// seed the pseudo random stream with a good value
			FRandomStream RandomStream(12345);
			// Values represent float3 values in the -1..1 range.
			// The vectors are the edge mid point of a cube from -1 .. 1
			static uint32 gradtable[] =
			{
				0x88ffff, 0xff88ff, 0xffff88,
				0x88ff00, 0xff8800, 0xff0088,
				0x8800ff, 0x0088ff, 0x00ff88,
				0x880000, 0x008800, 0x000088,
			};
			for (int32 y = 0; y < Desc.Extent.Y; ++y)
			{
				for (int32 x = 0; x < Desc.Extent.X; ++x)
				{
				uint32* Dest = (uint32*)(DestBuffer + x * sizeof(uint32) + y * DestStride);

					// pick a random direction (hacky way to overcome the quality issues FRandomStream has)
					*Dest = gradtable[(uint32)(RandomStream.GetFraction() * 11.9999999f)];
				}
			}
			RHICmdList.UnlockTexture2D((FTexture2DRHIRef&)PerlinNoiseGradient->GetRenderTargetItem().ShaderResourceTexture, 0, false);
		}

	if (GPixelFormats[PF_FloatRGBA].Supported)
	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(1, 1), PF_FloatRGBA, FClearValueBinding(FLinearColor(65500.0f, 65500.0f, 65500.0f, 65500.0f)), TexCreate_HideInVisualizeTexture, TexCreate_RenderTargetable | TexCreate_NoFastClear | TexCreate_ShaderResource, false));
		Desc.AutoWritable = false;
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, MaxFP16Depth, TEXT("MaxFP16Depth"), ERenderTargetTransience::NonTransient);

		RHICmdList.Transition(FRHITransitionInfo(MaxFP16Depth->GetRenderTargetItem().TargetableTexture, ERHIAccess::SRVMask, ERHIAccess::RTV));

		FRHIRenderPassInfo RPInfo(MaxFP16Depth->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::Clear_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("MaxFP16Depth"));
		RHICmdList.EndRenderPass();
		RHICmdList.CopyToResolveTarget(MaxFP16Depth->GetRenderTargetItem().TargetableTexture, MaxFP16Depth->GetRenderTargetItem().ShaderResourceTexture, FResolveParams());
	}

	// Create dummy 1x1 depth texture		
	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(1, 1), PF_DepthStencil, FClearValueBinding::DepthFar, TexCreate_None, TexCreate_DepthStencilTargetable, false));
		Desc.AutoWritable = false;
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, DepthDummy, TEXT("DepthDummy"), ERenderTargetTransience::NonTransient);

		RHICmdList.Transition(FRHITransitionInfo(DepthDummy->GetRenderTargetItem().TargetableTexture, ERHIAccess::SRVMask, ERHIAccess::DSVWrite));

		FRHIRenderPassInfo RPInfo(DepthDummy->GetRenderTargetItem().TargetableTexture, EDepthStencilTargetActions::ClearDepthStencil_StoreDepthStencil, nullptr, FExclusiveDepthStencil::DepthWrite_StencilWrite);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("DepthDummy"));
		RHICmdList.EndRenderPass();
		RHICmdList.CopyToResolveTarget(DepthDummy->GetRenderTargetItem().TargetableTexture, DepthDummy->GetRenderTargetItem().ShaderResourceTexture, FResolveParams());
	}

	// Create a dummy stencil SRV.
	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(1, 1), PF_R8G8B8A8_UINT, FClearValueBinding::White, TexCreate_HideInVisualizeTexture, TexCreate_RenderTargetable | TexCreate_NoFastClear, false));
		Desc.AutoWritable = false;
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, StencilDummy, TEXT("StencilDummy"), ERenderTargetTransience::NonTransient);

		RHICmdList.Transition(FRHITransitionInfo(StencilDummy->GetRenderTargetItem().TargetableTexture, ERHIAccess::SRVMask, ERHIAccess::RTV));

		FRHIRenderPassInfo RPInfo(StencilDummy->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::Clear_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("StencilDummy"));
		RHICmdList.EndRenderPass();
		RHICmdList.CopyToResolveTarget(StencilDummy->GetRenderTargetItem().TargetableTexture, StencilDummy->GetRenderTargetItem().ShaderResourceTexture, FResolveParams());

		StencilDummySRV = RHICreateShaderResourceView((FRHITexture2D*)StencilDummy->GetRenderTargetItem().ShaderResourceTexture.GetReference(), 0);
	}

	if (!GSupportsShaderFramebufferFetch && GPixelFormats[PF_FloatRGBA].Supported)
	{
		// PF_FloatRGBA to encode exactly the 0.5.
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(1, 1), PF_FloatRGBA, FClearValueBinding(FLinearColor(0.5f, 0.5f, 0.5f, 0.5f)), TexCreate_HideInVisualizeTexture, TexCreate_RenderTargetable | TexCreate_NoFastClear | TexCreate_ShaderResource, false));
		Desc.AutoWritable = false;
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, MidGreyDummy, TEXT("MidGreyDummy"), ERenderTargetTransience::NonTransient);

		RHICmdList.Transition(FRHITransitionInfo(MidGreyDummy->GetRenderTargetItem().TargetableTexture, ERHIAccess::SRVMask, ERHIAccess::RTV));

		FRHIRenderPassInfo RPInfo(MidGreyDummy->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::Clear_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("MidGreyDummy"));
		RHICmdList.EndRenderPass();
		RHICmdList.CopyToResolveTarget(MidGreyDummy->GetRenderTargetItem().TargetableTexture, MidGreyDummy->GetRenderTargetItem().ShaderResourceTexture, FResolveParams());
	}
}

void FSystemTextures::InitializeFeatureLevelDependentTextures(FRHICommandListImmediate& RHICmdList, const ERHIFeatureLevel::Type InFeatureLevel)
{
	// this function will be called every time the feature level will be updated and some textures require a minimum feature level to exist
	// the below declared variable (CurrentFeatureLevel) will guard against reinitialization of those textures already created in a previous call
	// if FeatureLevelInitializedTo has its default value (ERHIFeatureLevel::Num) it means that setup was never performed and all textures are invalid
	// thus CurrentFeatureLevel will be set to ERHIFeatureLevel::ES2_REMOVED to validate all 'is valid' branching conditions below
    ERHIFeatureLevel::Type CurrentFeatureLevel = FeatureLevelInitializedTo == ERHIFeatureLevel::Num ? ERHIFeatureLevel::ES2_REMOVED : FeatureLevelInitializedTo;

		// Create the SobolSampling texture
	if (CurrentFeatureLevel < ERHIFeatureLevel::ES3_1 && InFeatureLevel >= ERHIFeatureLevel::ES3_1 && GPixelFormats[PF_R16_UINT].Supported)
	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(32, 16), PF_R16_UINT, FClearValueBinding::None, TexCreate_HideInVisualizeTexture, TexCreate_NoFastClear | TexCreate_ShaderResource, false));
		Desc.AutoWritable = false;
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SobolSampling, TEXT("SobolSampling"));
		// Write the contents of the texture.
		uint32 DestStride;
		uint8* DestBuffer = (uint8*)RHICmdList.LockTexture2D((FTexture2DRHIRef&)SobolSampling->GetRenderTargetItem().ShaderResourceTexture, 0, RLM_WriteOnly, DestStride, false);

		uint16 *Dest;
		for (int y = 0; y < 16; ++y)
		{
			Dest = (uint16*)(DestBuffer + y * DestStride);

			// 16x16 block starting at 0,0 = Sobol X,Y from bottom 4 bits of cell X,Y
			for (int x = 0; x < 16; ++x, ++Dest)
			{
				*Dest = FSobol::ComputeGPUSpatialSeed(x, y, /* Index = */ 0);
			}

			// 16x16 block starting at 16,0 = Sobol X,Y from 2nd 4 bits of cell X,Y
			for (int x = 0; x < 16; ++x, ++Dest)
			{
				*Dest = FSobol::ComputeGPUSpatialSeed(x, y, /* Index = */ 1);
			}
		}
		RHICmdList.UnlockTexture2D((FTexture2DRHIRef&)SobolSampling->GetRenderTargetItem().ShaderResourceTexture, 0, false);
	}

	// Create a VolumetricBlackDummy texture
	if (CurrentFeatureLevel < ERHIFeatureLevel::SM5 && InFeatureLevel >= ERHIFeatureLevel::SM5)
	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::CreateVolumeDesc(1, 1, 1, PF_B8G8R8A8, FClearValueBinding::Transparent, TexCreate_HideInVisualizeTexture, TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_NoFastClear, false));
		Desc.AutoWritable = false;
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, VolumetricBlackDummy, TEXT("VolumetricBlackDummy"), ERenderTargetTransience::NonTransient);

		const uint8 BlackBytes[4] = { 0, 0, 0, 0 };
		FUpdateTextureRegion3D Region(0, 0, 0, 0, 0, 0, Desc.Extent.X, Desc.Extent.Y, Desc.Depth);
		RHICmdList.UpdateTexture3D(
			(FTexture3DRHIRef&)VolumetricBlackDummy->GetRenderTargetItem().ShaderResourceTexture,
			0,
			Region,
			Desc.Extent.X * sizeof(BlackBytes),
			Desc.Extent.X * Desc.Extent.Y * sizeof(BlackBytes),
			BlackBytes);
	}

	if (CurrentFeatureLevel < ERHIFeatureLevel::SM5 && InFeatureLevel >= ERHIFeatureLevel::SM5)
	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::CreateVolumeDesc(1, 1, 1, PF_B8G8R8A8, FClearValueBinding::Transparent, TexCreate_HideInVisualizeTexture, TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_NoFastClear, false));
		Desc.AutoWritable = false;
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, HairLUT0, TEXT("HairLUT0"), ERenderTargetTransience::NonTransient);

		// Init with dummy textures. The texture will be initialize with real values if needed
		const uint8 BlackBytes[4] = { 0, 0, 0, 0 };
		FUpdateTextureRegion3D Region(0, 0, 0, 0, 0, 0, Desc.Extent.X, Desc.Extent.Y, Desc.Depth);
		RHICmdList.UpdateTexture3D((FTexture3DRHIRef&)HairLUT0->GetRenderTargetItem().ShaderResourceTexture, 0, Region, Desc.Extent.X * sizeof(BlackBytes), Desc.Extent.X * Desc.Extent.Y * sizeof(BlackBytes), BlackBytes);

		RHICmdList.Transition(FRHITransitionInfo(HairLUT0->GetRenderTargetItem().ShaderResourceTexture, ERHIAccess::Unknown, ERHIAccess::SRVMask));
		HairLUT1 = HairLUT0;
		HairLUT2 = HairLUT0;
	}

	// The PreintegratedGF maybe used on forward shading inluding mobile platorm, intialize it anyway.
	{
		// for testing, with 128x128 R8G8 we are very close to the reference (if lower res is needed we might have to add an offset to counter the 0.5f texel shift)
		const bool bReference = false;

		EPixelFormat Format = PF_R8G8;
		// for low roughness we would get banding with PF_R8G8 but for low spec it could be used, for now we don't do this optimization
		if (GPixelFormats[PF_G16R16].Supported)
		{
			Format = PF_G16R16;
		}

		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(128, 32), Format, FClearValueBinding::None, TexCreate_None, TexCreate_ShaderResource, false));
		Desc.AutoWritable = false;
		if (bReference)
		{
			Desc.Extent.X = 128;
			Desc.Extent.Y = 128;
		}

		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, PreintegratedGF, TEXT("PreintegratedGF"), ERenderTargetTransience::NonTransient);
		// Write the contents of the texture.
		uint32 DestStride;
		uint8* DestBuffer = (uint8*)RHICmdList.LockTexture2D((FTexture2DRHIRef&)PreintegratedGF->GetRenderTargetItem().ShaderResourceTexture, 0, RLM_WriteOnly, DestStride, false);

		// x is NoV, y is roughness
		for (int32 y = 0; y < Desc.Extent.Y; y++)
		{
			float Roughness = (float)(y + 0.5f) / Desc.Extent.Y;
			float m = Roughness * Roughness;
			float m2 = m * m;

			for (int32 x = 0; x < Desc.Extent.X; x++)
			{
				float NoV = (float)(x + 0.5f) / Desc.Extent.X;

				FVector V;
				V.X = FMath::Sqrt(1.0f - NoV * NoV);	// sin
				V.Y = 0.0f;
				V.Z = NoV;								// cos

				float A = 0.0f;
				float B = 0.0f;
				float C = 0.0f;

				const uint32 NumSamples = 128;
				for (uint32 i = 0; i < NumSamples; i++)
				{
					float E1 = (float)i / NumSamples;
					float E2 = (double)ReverseBits(i) / (double)0x100000000LL;

					{
						float Phi = 2.0f * PI * E1;
						float CosPhi = FMath::Cos(Phi);
						float SinPhi = FMath::Sin(Phi);
						float CosTheta = FMath::Sqrt((1.0f - E2) / (1.0f + (m2 - 1.0f) * E2));
						float SinTheta = FMath::Sqrt(1.0f - CosTheta * CosTheta);

						FVector H(SinTheta * FMath::Cos(Phi), SinTheta * FMath::Sin(Phi), CosTheta);
						FVector L = 2.0f * (V | H) * H - V;

						float NoL = FMath::Max(L.Z, 0.0f);
						float NoH = FMath::Max(H.Z, 0.0f);
						float VoH = FMath::Max(V | H, 0.0f);

						if (NoL > 0.0f)
						{
							float Vis_SmithV = NoL * (NoV * (1 - m) + m);
							float Vis_SmithL = NoV * (NoL * (1 - m) + m);
							float Vis = 0.5f / (Vis_SmithV + Vis_SmithL);

							float NoL_Vis_PDF = NoL * Vis * (4.0f * VoH / NoH);
							float Fc = 1.0f - VoH;
							Fc *= FMath::Square(Fc*Fc);
							A += NoL_Vis_PDF * (1.0f - Fc);
							B += NoL_Vis_PDF * Fc;
						}
					}

					{
						float Phi = 2.0f * PI * E1;
						float CosPhi = FMath::Cos(Phi);
						float SinPhi = FMath::Sin(Phi);
						float CosTheta = FMath::Sqrt(E2);
						float SinTheta = FMath::Sqrt(1.0f - CosTheta * CosTheta);

						FVector L(SinTheta * FMath::Cos(Phi), SinTheta * FMath::Sin(Phi), CosTheta);
						FVector H = (V + L).GetUnsafeNormal();

						float NoL = FMath::Max(L.Z, 0.0f);
						float NoH = FMath::Max(H.Z, 0.0f);
						float VoH = FMath::Max(V | H, 0.0f);

						float FD90 = 0.5f + 2.0f * VoH * VoH * Roughness;
						float FdV = 1.0f + (FD90 - 1.0f) * pow(1.0f - NoV, 5);
						float FdL = 1.0f + (FD90 - 1.0f) * pow(1.0f - NoL, 5);
						C += FdV * FdL;// * ( 1.0f - 0.3333f * Roughness );
					}
				}
				A /= NumSamples;
				B /= NumSamples;
				C /= NumSamples;

				if (Desc.Format == PF_A16B16G16R16)
				{
					uint16* Dest = (uint16*)(DestBuffer + x * 8 + y * DestStride);
					Dest[0] = (int32)(FMath::Clamp(A, 0.0f, 1.0f) * 65535.0f + 0.5f);
					Dest[1] = (int32)(FMath::Clamp(B, 0.0f, 1.0f) * 65535.0f + 0.5f);
					Dest[2] = (int32)(FMath::Clamp(C, 0.0f, 1.0f) * 65535.0f + 0.5f);
				}
				else if (Desc.Format == PF_G16R16)
				{
					uint16* Dest = (uint16*)(DestBuffer + x * 4 + y * DestStride);
					Dest[0] = (int32)(FMath::Clamp(A, 0.0f, 1.0f) * 65535.0f + 0.5f);
					Dest[1] = (int32)(FMath::Clamp(B, 0.0f, 1.0f) * 65535.0f + 0.5f);
				}
				else
				{
					check(Desc.Format == PF_R8G8);

					uint8* Dest = (uint8*)(DestBuffer + x * 2 + y * DestStride);
					Dest[0] = (int32)(FMath::Clamp(A, 0.0f, 1.0f) * 255.9999f);
					Dest[1] = (int32)(FMath::Clamp(B, 0.0f, 1.0f) * 255.9999f);
				}
			}
		}
		RHICmdList.UnlockTexture2D((FTexture2DRHIRef&)PreintegratedGF->GetRenderTargetItem().ShaderResourceTexture, 0, false);
	}

	if (CurrentFeatureLevel < ERHIFeatureLevel::SM5 && InFeatureLevel >= ERHIFeatureLevel::SM5)
	{
	    // Create the PerlinNoise3D texture (similar to http://prettyprocs.wordpress.com/2012/10/20/fast-perlin-noise/)
	    {
		    uint32 Extent = 16;
    
		    const uint32 Square = Extent * Extent;
    
		    FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::CreateVolumeDesc(Extent, Extent, Extent, PF_B8G8R8A8, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_HideInVisualizeTexture | TexCreate_NoTiling, TexCreate_ShaderResource, false));
		    Desc.AutoWritable = false;
		    GRenderTargetPool.FindFreeElement(RHICmdList, Desc, PerlinNoise3D, TEXT("PerlinNoise3D"), ERenderTargetTransience::NonTransient);
		    // Write the contents of the texture.
		    TArray<uint32> DestBuffer;
    
		    DestBuffer.AddZeroed(Extent * Extent * Extent);
		    // seed the pseudo random stream with a good value
		    FRandomStream RandomStream(0x1234);
		    // Values represent float3 values in the -1..1 range.
		    // The vectors are the edge mid point of a cube from -1 .. 1
		    // -1:0 0:7f 1:fe, can be reconstructed with * 512/254 - 1
		    // * 2 - 1 cannot be used because 0 would not be mapped
			    static uint32 gradtable[] =
		    {
			    0x7ffefe, 0xfe7ffe, 0xfefe7f,
			    0x7ffe00, 0xfe7f00, 0xfe007f,
			    0x7f00fe, 0x007ffe, 0x00fe7f,
			    0x7f0000, 0x007f00, 0x00007f,
		    };
		    // set random directions
		    {
				    for (uint32 z = 0; z < Extent - 1; ++z)
			    {
					    for (uint32 y = 0; y < Extent - 1; ++y)
				    {
						    for (uint32 x = 0; x < Extent - 1; ++x)
					    {
						    uint32& Value = DestBuffer[x + y * Extent + z * Square];
    
						    // pick a random direction (hacky way to overcome the quality issues FRandomStream has)
						    Value = gradtable[(uint32)(RandomStream.GetFraction() * 11.9999999f)];
					    }
				    }
			    }
		    }
		    // replicate a border for filtering
		    {
			    uint32 Last = Extent - 1;
    
				    for (uint32 z = 0; z < Extent; ++z)
			    {
					    for (uint32 y = 0; y < Extent; ++y)
				    {
					    DestBuffer[Last + y * Extent + z * Square] = DestBuffer[0 + y * Extent + z * Square];
				    }
			    }
				for (uint32 z = 0; z < Extent; ++z)
				{
					for (uint32 x = 0; x < Extent; ++x)
					{
					    DestBuffer[x + Last * Extent + z * Square] = DestBuffer[x + 0 * Extent + z * Square];
				    }
			    }
				for (uint32 y = 0; y < Extent; ++y)
				{
					for (uint32 x = 0; x < Extent; ++x)
				    {
					    DestBuffer[x + y * Extent + Last * Square] = DestBuffer[x + y * Extent + 0 * Square];
				    }
			    }
		    }
		    // precompute gradients
			{
			    uint32* Dest = DestBuffer.GetData();
    
				for (uint32 z = 0; z < Desc.Depth; ++z)
			    {
					for (uint32 y = 0; y < (uint32)Desc.Extent.Y; ++y)
				    {
						for (uint32 x = 0; x < (uint32)Desc.Extent.X; ++x)
					    {
						    uint32 Value = *Dest;
    
						    // todo: check if rgb order is correct
						    int32 r = Value >> 16;
						    int32 g = (Value >> 8) & 0xff;
						    int32 b = Value & 0xff;
    
						    int nx = (r / 0x7f) - 1;
						    int ny = (g / 0x7f) - 1;
						    int nz = (b / 0x7f) - 1;
    
						    int32 d = nx * x + ny * y + nz * z;
    
						    // compress in 8bit
						    uint32 a = d + 127;
    
						    *Dest++ = Value | (a << 24);
					    }
				    }
			    }
		    }
    
		    FUpdateTextureRegion3D Region(0, 0, 0, 0, 0, 0, Desc.Extent.X, Desc.Extent.Y, Desc.Depth);
    
		    RHICmdList.UpdateTexture3D(
			    (FTexture3DRHIRef&)PerlinNoise3D->GetRenderTargetItem().ShaderResourceTexture,
			    0,
			    Region,
			    Desc.Extent.X * sizeof(uint32),
			    Desc.Extent.X * Desc.Extent.Y * sizeof(uint32),
			    (const uint8*)DestBuffer.GetData());
		} // end Create the PerlinNoise3D texture

		// GTAO Randomization texture	
		{
			{
				FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(4, 4), PF_R8G8B8A8, FClearValueBinding::None, TexCreate_HideInVisualizeTexture, TexCreate_None | TexCreate_NoFastClear, false));
				Desc.AutoWritable = false;
				GRenderTargetPool.FindFreeElement(RHICmdList, Desc,GTAORandomization, TEXT("GTAORandomization"), ERenderTargetTransience::NonTransient);
				// Write the contents of the texture.
				uint32 DestStride;
				uint8* DestBuffer = (uint8*)RHICmdList.LockTexture2D((FTexture2DRHIRef&)GTAORandomization->GetRenderTargetItem().ShaderResourceTexture, 0, RLM_WriteOnly, DestStride, false);

				for(int32 y = 0; y < Desc.Extent.Y; ++y)
				{
					for(int32 x = 0; x < Desc.Extent.X; ++x)
					{
						uint8* Dest = (uint8*)(DestBuffer + x * sizeof(uint32) + y * DestStride);
			
						float Angle  = (PI/16.0f)  * ((((x + y) & 0x3) << 2) + (x & 0x3));
						float Step   = (1.0f / 4.0f) * ((y - x) & 0x3);

						float ScaleCos = FMath::Cos(Angle) ;
						float ScaleSin = FMath::Sin(Angle) ;
			
						Dest[0] = (uint8_t)(ScaleCos*127.5f + 127.5f);
						Dest[1] = (uint8_t)(ScaleSin*127.5f + 127.5f);
						Dest[2] = (uint8_t)(Step*255.0f);
						Dest[3] = 0;
					}
				}
			}
			RHICmdList.UnlockTexture2D((FTexture2DRHIRef&)GTAORandomization->GetRenderTargetItem().ShaderResourceTexture, 0, false);
			
		    {
			    FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(LTC_Size, LTC_Size), PF_FloatRGBA, FClearValueBinding::None, TexCreate_FastVRAM, TexCreate_ShaderResource, false));
			    Desc.AutoWritable = false;
    
			    GRenderTargetPool.FindFreeElement(RHICmdList, Desc, LTCMat, TEXT("LTCMat"));
			    // Write the contents of the texture.
			    uint32 DestStride;
			    uint8* DestBuffer = (uint8*)RHICmdList.LockTexture2D((FTexture2DRHIRef&)LTCMat->GetRenderTargetItem().ShaderResourceTexture, 0, RLM_WriteOnly, DestStride, false);
    
				    for (int32 y = 0; y < Desc.Extent.Y; ++y)
			    {
					    for (int32 x = 0; x < Desc.Extent.X; ++x)
				    {
					    uint16* Dest = (uint16*)(DestBuffer + x * 4 * sizeof(uint16) + y * DestStride);
    
						    for (int k = 0; k < 4; k++)
							    Dest[k] = FFloat16(LTC_Mat[4 * (x + y * LTC_Size) + k]).Encoded;
				    }
			    }
			    RHICmdList.UnlockTexture2D((FTexture2DRHIRef&)LTCMat->GetRenderTargetItem().ShaderResourceTexture, 0, false);
		    }
    
		    {
			    FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(LTC_Size, LTC_Size), PF_G16R16F, FClearValueBinding::None, TexCreate_FastVRAM, TexCreate_ShaderResource, false));
			    Desc.AutoWritable = false;
    
			    GRenderTargetPool.FindFreeElement(RHICmdList, Desc, LTCAmp, TEXT("LTCAmp"));
			    // Write the contents of the texture.
			    uint32 DestStride;
			    uint8* DestBuffer = (uint8*)RHICmdList.LockTexture2D((FTexture2DRHIRef&)LTCAmp->GetRenderTargetItem().ShaderResourceTexture, 0, RLM_WriteOnly, DestStride, false);
    
				for (int32 y = 0; y < Desc.Extent.Y; ++y)
			    {
					for (int32 x = 0; x < Desc.Extent.X; ++x)
				    {
					    uint16* Dest = (uint16*)(DestBuffer + x * 2 * sizeof(uint16) + y * DestStride);
    
						for (int k = 0; k < 2; k++)
						    Dest[k] = FFloat16(LTC_Amp[4 * (x + y * LTC_Size) + k]).Encoded;
				    }
			    }
			    RHICmdList.UnlockTexture2D((FTexture2DRHIRef&)LTCAmp->GetRenderTargetItem().ShaderResourceTexture, 0, false);
		    }
		} // end Create the GTAO  randomization texture
	} // end if (FeatureLevelInitializedTo < ERHIFeatureLevel::SM5 && InFeatureLevel >= ERHIFeatureLevel::SM5)

	// Create the SSAO randomization texture
	static const auto MobileAmbientOcclusionCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.AmbientOcclusion"));
	if ((CurrentFeatureLevel < ERHIFeatureLevel::SM5 && InFeatureLevel >= ERHIFeatureLevel::SM5) ||
		(CurrentFeatureLevel < ERHIFeatureLevel::ES3_1 && InFeatureLevel >= ERHIFeatureLevel::ES3_1 && MobileAmbientOcclusionCVar != nullptr && MobileAmbientOcclusionCVar->GetValueOnAnyThread()>0))
	{
		{
			float g_AngleOff1 = 127;
			float g_AngleOff2 = 198;
			float g_AngleOff3 = 23;

			FColor Bases[16];

			for (int32 Pos = 0; Pos < 16; ++Pos)
			{
				// distribute rotations over 4x4 pattern
						//			int32 Reorder[16] = { 0, 8, 2, 10, 12, 6, 14, 4, 3, 11, 1, 9, 15, 5, 13, 7 };
				int32 Reorder[16] = { 0, 11, 7, 3, 10, 4, 15, 12, 6, 8, 1, 14, 13, 2, 9, 5 };
				int32 w = Reorder[Pos];

				// ordered sampling of the rotation basis (*2 is missing as we use mirrored samples)
				float ww = w / 16.0f * PI;

				// randomize base scale
				float lenm = 1.0f - (FMath::Sin(g_AngleOff2 * w * 0.01f) * 0.5f + 0.5f) * g_AngleOff3 * 0.01f;
				float s = FMath::Sin(ww) * lenm;
				float c = FMath::Cos(ww) * lenm;

				Bases[Pos] = FColor(FMath::Quantize8SignedByte(c), FMath::Quantize8SignedByte(s), 0, 0);
			}

			{
				// could be PF_V8U8 to save shader instructions but that doesn't work on all hardware
				FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(64, 64), PF_R8G8, FClearValueBinding::None, TexCreate_HideInVisualizeTexture, TexCreate_NoFastClear | TexCreate_ShaderResource, false));
				Desc.AutoWritable = false;
				GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SSAORandomization, TEXT("SSAORandomization"), ERenderTargetTransience::NonTransient);
				// Write the contents of the texture.
				uint32 DestStride;
				uint8* DestBuffer = (uint8*)RHICmdList.LockTexture2D((FTexture2DRHIRef&)SSAORandomization->GetRenderTargetItem().ShaderResourceTexture, 0, RLM_WriteOnly, DestStride, false);

				for (int32 y = 0; y < Desc.Extent.Y; ++y)
				{
					for (int32 x = 0; x < Desc.Extent.X; ++x)
					{
						uint8* Dest = (uint8*)(DestBuffer + x * sizeof(uint16) + y * DestStride);

						uint32 Index = (x % 4) + (y % 4) * 4;

						Dest[0] = Bases[Index].R;
						Dest[1] = Bases[Index].G;
					}
				}
			}
			RHICmdList.UnlockTexture2D((FTexture2DRHIRef&)SSAORandomization->GetRenderTargetItem().ShaderResourceTexture, 0, false);
		}
	}
		
	static const auto MobileGTAOPreIntegratedTextureTypeCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.GTAOPreIntegratedTextureType"));

	if (CurrentFeatureLevel < ERHIFeatureLevel::ES3_1 && InFeatureLevel >= ERHIFeatureLevel::ES3_1 && MobileGTAOPreIntegratedTextureTypeCVar && MobileGTAOPreIntegratedTextureTypeCVar->GetValueOnAnyThread() > 0)
	{
		uint32 Extent = 16; // should be consistent with LUTSize in PostprocessMobile.usf

		const uint32 Square = Extent * Extent;

		bool bGTAOPreIngegratedUsingVolumeLUT = MobileGTAOPreIntegratedTextureTypeCVar->GetValueOnAnyThread() == 2;

		FPooledRenderTargetDesc Desc;
		if (bGTAOPreIngegratedUsingVolumeLUT)
		{
			Desc = FPooledRenderTargetDesc(FPooledRenderTargetDesc::CreateVolumeDesc(Extent, Extent, Extent, PF_R16F, FClearValueBinding::None, TexCreate_HideInVisualizeTexture | TexCreate_NoTiling | TexCreate_ShaderResource, TexCreate_ShaderResource, false));
		}
		else
		{
			Desc = FPooledRenderTargetDesc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(Square, Extent), PF_R16F, FClearValueBinding::None, TexCreate_HideInVisualizeTexture | TexCreate_NoTiling | TexCreate_ShaderResource, TexCreate_ShaderResource, false));
		}
		
		Desc.AutoWritable = false;
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, GTAOPreIntegrated, TEXT("GTAOPreIntegrated"), ERenderTargetTransience::NonTransient);

		// Write the contents of the texture.
		TArray<FFloat16> TempBuffer;
		TempBuffer.AddZeroed(Extent * Extent * Extent);

		FFloat16* DestBuffer = nullptr;

		if (bGTAOPreIngegratedUsingVolumeLUT)
		{
			DestBuffer = TempBuffer.GetData();
		}
		else
		{
			uint32 DestStride;
			DestBuffer = (FFloat16*)RHICmdList.LockTexture2D((FTexture2DRHIRef&)GTAOPreIntegrated->GetRenderTargetItem().ShaderResourceTexture, 0, RLM_WriteOnly, DestStride, false);
		}

		for (uint32 z = 0; z < Extent; ++z)
		{
			for (uint32 y = 0; y < Extent; ++y)
			{
				for (uint32 x = 0; x < Extent; ++x)
				{
					uint32 DestBufferIndex = 0;

					if (bGTAOPreIngegratedUsingVolumeLUT)
					{
						DestBufferIndex = x + y * Extent + z * Square;
					}
					else
					{
						DestBufferIndex = (x + z * Extent) + y * Square;
					}
					FFloat16& Value = DestBuffer[DestBufferIndex];

					float cosAngle1 = ((x + 0.5f) / (Extent) - 0.5f) * 2;
					float cosAngle2 = ((y + 0.5f) / (Extent) - 0.5f) * 2;
					float cosAng = ((z + 0.5f) / (Extent) - 0.5f) * 2;

					float Gamma = FMath::Acos(cosAng) - HALF_PI;
					float CosGamma = FMath::Cos(Gamma);
					float SinGamma = cosAng * -2.0f;

					float Angle1 = FMath::Acos(cosAngle1);
					float Angle2 = FMath::Acos(cosAngle2);
					// clamp to normal hemisphere 
					Angle1 = Gamma + FMath::Max(-Angle1 - Gamma, -(HALF_PI));
					Angle2 = Gamma + FMath::Min(Angle2 - Gamma, (HALF_PI));

					float AO = (0.25f *
						((Angle1 * SinGamma + CosGamma - cos((2.0 * Angle1) - Gamma)) +
						(Angle2 * SinGamma + CosGamma - cos((2.0 * Angle2) - Gamma))));

					Value = AO;
				}
			}
		}

		if (bGTAOPreIngegratedUsingVolumeLUT)
		{
			FUpdateTextureRegion3D Region(0, 0, 0, 0, 0, 0, Desc.Extent.X, Desc.Extent.Y, Desc.Depth);

			RHICmdList.UpdateTexture3D(
				(FTexture3DRHIRef&)GTAOPreIntegrated->GetRenderTargetItem().ShaderResourceTexture,
				0,
				Region,
				Desc.Extent.X * sizeof(FFloat16),
				Desc.Extent.X * Desc.Extent.Y * sizeof(FFloat16),
				(const uint8*)DestBuffer);
		}
		else
		{
			RHICmdList.UnlockTexture2D((FTexture2DRHIRef&)GTAOPreIntegrated->GetRenderTargetItem().ShaderResourceTexture, 0, false);
		}
	}

	// Initialize textures only once.
	FeatureLevelInitializedTo = InFeatureLevel;
}

void FSystemTextures::ReleaseDynamicRHI()
{
	WhiteDummySRV.SafeRelease();
	WhiteDummy.SafeRelease();
	BlackDummy.SafeRelease();
	BlackAlphaOneDummy.SafeRelease();
	PerlinNoiseGradient.SafeRelease();
	PerlinNoise3D.SafeRelease();
	SobolSampling.SafeRelease();
	SSAORandomization.SafeRelease();
	GTAORandomization.SafeRelease();
	GTAOPreIntegrated.SafeRelease();
	PreintegratedGF.SafeRelease();
	HairLUT0.SafeRelease();
	HairLUT1.SafeRelease();
	HairLUT2.SafeRelease();
	LTCMat.SafeRelease();
	LTCAmp.SafeRelease();
	MaxFP16Depth.SafeRelease();
	DepthDummy.SafeRelease();
	GreenDummy.SafeRelease();
	DefaultNormal8Bit.SafeRelease();
	VolumetricBlackDummy.SafeRelease();
	ZeroUIntDummy.SafeRelease();
	MidGreyDummy.SafeRelease();
	StencilDummy.SafeRelease();
	StencilDummySRV.SafeRelease();
	GTAOPreIntegrated.SafeRelease();

	GRenderTargetPool.FreeUnusedResources();

	// Indicate that textures will need to be reinitialized.
	FeatureLevelInitializedTo = ERHIFeatureLevel::Num;
}

FRDGTextureRef FSystemTextures::GetBlackDummy(FRDGBuilder& GraphBuilder) const
{
	return GraphBuilder.RegisterExternalTexture(BlackDummy, TEXT("BlackDummy"));
}

FRDGTextureRef FSystemTextures::GetBlackAlphaOneDummy(FRDGBuilder& GraphBuilder) const
{
	return GraphBuilder.RegisterExternalTexture(BlackAlphaOneDummy, TEXT("BlackAlphaOneDummy"));
}

FRDGTextureRef FSystemTextures::GetWhiteDummy(FRDGBuilder& GraphBuilder) const
{
	return GraphBuilder.RegisterExternalTexture(WhiteDummy, TEXT("WhiteDummy"));
}

FRDGTextureRef FSystemTextures::GetPerlinNoiseGradient(FRDGBuilder& GraphBuilder) const
{
	return GraphBuilder.RegisterExternalTexture(PerlinNoiseGradient, TEXT("PerlinNoiseGradient"));
}

FRDGTextureRef FSystemTextures::GetPerlinNoise3D(FRDGBuilder& GraphBuilder) const
{
	return GraphBuilder.RegisterExternalTexture(PerlinNoise3D, TEXT("PerlinNoise3D"));
}

FRDGTextureRef FSystemTextures::GetSobolSampling(FRDGBuilder& GraphBuilder) const
{
	return GraphBuilder.RegisterExternalTexture(SobolSampling, TEXT("SobolSampling"));
}

FRDGTextureRef FSystemTextures::GetSSAORandomization(FRDGBuilder& GraphBuilder) const
{
	return GraphBuilder.RegisterExternalTexture(SSAORandomization, TEXT("SSAORandomization"));
}

FRDGTextureRef FSystemTextures::GetPreintegratedGF(FRDGBuilder& GraphBuilder) const
{
	return GraphBuilder.RegisterExternalTexture(PreintegratedGF, TEXT("PreintegratedGF"));
}

FRDGTextureRef FSystemTextures::GetLTCMat(FRDGBuilder& GraphBuilder) const
{
	return GraphBuilder.RegisterExternalTexture(LTCMat, TEXT("LTCMat"));
}

FRDGTextureRef FSystemTextures::GetLTCAmp(FRDGBuilder& GraphBuilder) const
{
	return GraphBuilder.RegisterExternalTexture(LTCAmp, TEXT("LTCAmp"));
}

FRDGTextureRef FSystemTextures::GetMaxFP16Depth(FRDGBuilder& GraphBuilder) const
{
	return GraphBuilder.RegisterExternalTexture(MaxFP16Depth, TEXT("MaxFP16Depth"));
}

FRDGTextureRef FSystemTextures::GetDepthDummy(FRDGBuilder& GraphBuilder) const
{
	return GraphBuilder.RegisterExternalTexture(DepthDummy, TEXT("DepthDummy"));
}

FRDGTextureRef FSystemTextures::GetStencilDummy(FRDGBuilder& GraphBuilder) const
{
	return GraphBuilder.RegisterExternalTexture(StencilDummy, TEXT("StencilDummy"));
}

FRDGTextureRef FSystemTextures::GetGreenDummy(FRDGBuilder& GraphBuilder) const
{
	return GraphBuilder.RegisterExternalTexture(GreenDummy, TEXT("GreenDummy"));
}

FRDGTextureRef FSystemTextures::GetDefaultNormal8Bit(FRDGBuilder& GraphBuilder) const
{
	return GraphBuilder.RegisterExternalTexture(DefaultNormal8Bit, TEXT("DefaultNormal8Bit"));
}

FRDGTextureRef FSystemTextures::GetMidGreyDummy(FRDGBuilder& GraphBuilder) const
{
	return GraphBuilder.RegisterExternalTexture(MidGreyDummy, TEXT("MidGreyDummy"));
}

FRDGTextureRef FSystemTextures::GetVolumetricBlackDummy(FRDGBuilder& GraphBuilder) const
{
	return GraphBuilder.RegisterExternalTexture(VolumetricBlackDummy, TEXT("VolumetricBlackDummy"));
}

FRDGTextureRef FSystemTextures::GetZeroUIntDummy(FRDGBuilder& GraphBuilder) const
{
	return GraphBuilder.RegisterExternalTexture(ZeroUIntDummy, TEXT("ZeroUIntDummy"));
}

