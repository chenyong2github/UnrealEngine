// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SystemTextures.cpp: System textures implementation.
=============================================================================*/

#include "AtmosphereTextures.h"
#include "Atmosphere/AtmosphericFogComponent.h"
#include "RenderTargetPool.h"
#include "ShaderParameterUtils.h"

void FAtmosphereTextures::InitDynamicRHI()
{
	check(PrecomputeParams != NULL);
	// Allocate atmosphere precompute textures...
	{
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

		// todo: Expose
		// Transmittance
		FIntPoint GTransmittanceTexSize(PrecomputeParams->TransmittanceTexWidth, PrecomputeParams->TransmittanceTexHeight);
		FPooledRenderTargetDesc TransmittanceDesc(FPooledRenderTargetDesc::Create2DDesc(GTransmittanceTexSize, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable, false));
		GRenderTargetPool.FindFreeElement(RHICmdList, TransmittanceDesc, AtmosphereTransmittance, TEXT("AtmosphereTransmittance"));
		{
			const FSceneRenderTargetItem& TransmittanceTarget = AtmosphereTransmittance->GetRenderTargetItem();

			RHICmdList.Transition(FRHITransitionInfo(TransmittanceTarget.TargetableTexture, ERHIAccess::Unknown, ERHIAccess::RTV));

			FRHIRenderPassInfo RPInfo(TransmittanceTarget.TargetableTexture, ERenderTargetActions::Clear_Store);
			RHICmdList.BeginRenderPass(RPInfo, TEXT("ClearTransmittance"));
			RHICmdList.EndRenderPass();
			RHICmdList.CopyToResolveTarget(TransmittanceTarget.TargetableTexture, TransmittanceTarget.ShaderResourceTexture, FResolveParams());
		}

		// Irradiance
		FIntPoint GIrradianceTexSize(PrecomputeParams->IrradianceTexWidth, PrecomputeParams->IrradianceTexHeight);
		FPooledRenderTargetDesc IrradianceDesc(FPooledRenderTargetDesc::Create2DDesc(GIrradianceTexSize, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable, false));
		GRenderTargetPool.FindFreeElement(RHICmdList, IrradianceDesc, AtmosphereIrradiance, TEXT("AtmosphereIrradiance"));
		{
			const FSceneRenderTargetItem& IrradianceTarget = AtmosphereIrradiance->GetRenderTargetItem();

			RHICmdList.Transition(FRHITransitionInfo(IrradianceTarget.TargetableTexture, ERHIAccess::Unknown, ERHIAccess::RTV));

			FRHIRenderPassInfo RPInfo(IrradianceTarget.TargetableTexture, ERenderTargetActions::Clear_Store);
			RHICmdList.BeginRenderPass(RPInfo, TEXT("ClearIrradiance"));
			RHICmdList.EndRenderPass();
			RHICmdList.CopyToResolveTarget(IrradianceTarget.TargetableTexture, IrradianceTarget.ShaderResourceTexture, FResolveParams());
		}
		// DeltaE
		GRenderTargetPool.FindFreeElement(RHICmdList, IrradianceDesc, AtmosphereDeltaE, TEXT("AtmosphereDeltaE"));

		// 3D Texture
		// Inscatter
		FPooledRenderTargetDesc InscatterDesc(FPooledRenderTargetDesc::CreateVolumeDesc(PrecomputeParams->InscatterMuSNum * PrecomputeParams->InscatterNuNum, PrecomputeParams->InscatterMuNum, PrecomputeParams->InscatterAltitudeSampleNum, PF_FloatRGBA, FClearValueBinding::None, TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable, false));
		GRenderTargetPool.FindFreeElement(RHICmdList, InscatterDesc, AtmosphereInscatter, TEXT("AtmosphereInscatter"));

		// DeltaSR
		GRenderTargetPool.FindFreeElement(RHICmdList, InscatterDesc, AtmosphereDeltaSR, TEXT("AtmosphereDeltaSR"));

		// DeltaSM
		GRenderTargetPool.FindFreeElement(RHICmdList, InscatterDesc, AtmosphereDeltaSM, TEXT("AtmosphereDeltaSM"));

		// DeltaJ
		GRenderTargetPool.FindFreeElement(RHICmdList, InscatterDesc, AtmosphereDeltaJ, TEXT("AtmosphereDeltaJ"));
	}
}

void FAtmosphereTextures::ReleaseDynamicRHI()
{
	AtmosphereTransmittance.SafeRelease();
	AtmosphereIrradiance.SafeRelease();
	AtmosphereDeltaE.SafeRelease();

	AtmosphereInscatter.SafeRelease();
	AtmosphereDeltaSR.SafeRelease();
	AtmosphereDeltaSM.SafeRelease();
	AtmosphereDeltaJ.SafeRelease();

	GRenderTargetPool.FreeUnusedResources();
}
