// Copyright Epic Games, Inc. All Rights Reserved.


#include "LensFileRendering.h"

#include "Engine/TextureRenderTarget2D.h"
#include "GlobalShader.h"											   
#include "PixelShaderUtils.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "ScreenPass.h"
#include "TextureResource.h"


/** A pixel shader that blends displacement maps together. */
class FDisplacementMapBlendPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDisplacementMapBlendPS);
	SHADER_USE_PARAMETER_STRUCT(FDisplacementMapBlendPS, FGlobalShader);

	class FBlendType : SHADER_PERMUTATION_INT("BLEND_TYPE", 3);
	using FPermutationDomain = TShaderPermutationDomain<FBlendType>;
	
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )

		SHADER_PARAMETER(float, BlendFactor)
		SHADER_PARAMETER(float, MainCoefficient)
		SHADER_PARAMETER(float, DeltaMinX)
		SHADER_PARAMETER(float, DeltaMaxX)
		SHADER_PARAMETER(float, DeltaMinY)
		SHADER_PARAMETER(float, DeltaMaxY)
		SHADER_PARAMETER(FVector2D, FxFyScale)
		SHADER_PARAMETER(FVector2D, PrincipalPoint)
		SHADER_PARAMETER(FIntPoint, OutputTextureExtent)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceTextureOne)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceTextureTwo)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceTextureThree)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceTextureFour)
        SHADER_PARAMETER_SAMPLER(SamplerState, SourceTextureSampler)

        RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()

	// Called by the engine to determine which permutations to compile for this shader
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDisplacementMapBlendPS, "/Plugin/CameraCalibration/Private/DisplacementMapBlending.usf", "BlendPS", SF_Pixel);


namespace LensFileRendering
{
	void ClearDisplacementMap(UTextureRenderTarget2D* OutRenderTarget)
	{
		if (OutRenderTarget != nullptr)
		{
			const FTextureRenderTargetResource* const DestinationTextureResource = OutRenderTarget->GameThread_GetRenderTargetResource();

			ENQUEUE_RENDER_COMMAND(LensFileRendering_ClearDisplacementMap)(
			[DestinationTextureResource](FRHICommandListImmediate& RHICmdList)
			{
				FRDGBuilder GraphBuilder(RHICmdList);

				const FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(DestinationTextureResource->TextureRHI, TEXT("OutputDisplacement")));

				FLinearColor NoDistortionColor(0.0f, 0.0f, 0.0f, 0.0f);
				AddClearRenderTargetPass(GraphBuilder, OutputTexture, NoDistortionColor);

				GraphBuilder.Execute();
			});
		}
	}	
	
	bool DrawBlendedDisplacementMap(UTextureRenderTarget2D* OutRenderTarget
	, const FDisplacementMapBlendingParams& BlendParams
	, UTextureRenderTarget2D* SourceTextureOne
	, UTextureRenderTarget2D* SourceTextureTwo
	, UTextureRenderTarget2D* SourceTextureThree
	, UTextureRenderTarget2D* SourceTextureFour)
{
	if (SourceTextureOne == nullptr || OutRenderTarget == nullptr)
	{
		return false;
	}

	const FTextureRenderTargetResource* const SourceTextureOneResource = SourceTextureOne->GameThread_GetRenderTargetResource();
	const FTextureRenderTargetResource* const SourceTextureTwoResource = SourceTextureTwo ? SourceTextureTwo->GameThread_GetRenderTargetResource() : nullptr;
	const FTextureRenderTargetResource* const SourceTextureThreeResource = SourceTextureThree? SourceTextureThree->GameThread_GetRenderTargetResource() : nullptr;
	const FTextureRenderTargetResource* const SourceTextureFourResource = SourceTextureFour ? SourceTextureFour->GameThread_GetRenderTargetResource() : nullptr;
	const FTextureRenderTargetResource* const DestinationTextureResource = OutRenderTarget->GameThread_GetRenderTargetResource();

	ENQUEUE_RENDER_COMMAND(LensFileRendering_DrawBlendedDisplacementMap)(
		[SourceTextureOneResource, SourceTextureTwoResource, SourceTextureThreeResource, SourceTextureFourResource, BlendParams = BlendParams, DestinationTextureResource](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			FDisplacementMapBlendPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDisplacementMapBlendPS::FParameters>();

			//Setup always used parameters
			FRDGTextureRef TextureOne = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(SourceTextureOneResource->TextureRHI, TEXT("DisplacementMapOne")));
			FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(DestinationTextureResource->TextureRHI, TEXT("OutputDisplacement")));
			PassParameters->OutputTextureExtent = OutputTexture->Desc.Extent;
			PassParameters->SourceTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(); 
			PassParameters->SourceTextureOne = TextureOne;
			PassParameters->FxFyScale = BlendParams.FxFyScale;
			PassParameters->PrincipalPoint = BlendParams.PrincipalPoint;

			//Setup parameters based on blending type
			FDisplacementMapBlendPS::FPermutationDomain PermutationVector;
			switch(BlendParams.BlendType)
			{
			case EDisplacementMapBlendType::Linear:
				{
					PermutationVector.Set<FDisplacementMapBlendPS::FBlendType>(1);
					check(SourceTextureTwoResource);
					FRDGTextureRef TextureTwo = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(SourceTextureTwoResource->TextureRHI, TEXT("DisplacementMapTwo")));
					PassParameters->BlendFactor = BlendParams.LinearBlendFactor;
					PassParameters->SourceTextureTwo = TextureTwo;
					break;
				}
			case EDisplacementMapBlendType::Bilinear:
				{
					PermutationVector.Set<FDisplacementMapBlendPS::FBlendType>(2);
					check(SourceTextureTwoResource && SourceTextureThreeResource && SourceTextureFourResource);
					FRDGTextureRef TextureTwo = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(SourceTextureTwoResource->TextureRHI, TEXT("DisplacementMapTwo")));
					FRDGTextureRef TextureThree = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(SourceTextureThreeResource->TextureRHI, TEXT("DisplacementMapThree")));
					FRDGTextureRef TextureFour = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(SourceTextureFourResource->TextureRHI, TEXT("DisplacementMapFour")));
					PassParameters->MainCoefficient = BlendParams.MainCoefficient;
					PassParameters->DeltaMinX = BlendParams.DeltaMinX;
					PassParameters->DeltaMaxX = BlendParams.DeltaMaxX;
					PassParameters->DeltaMinY = BlendParams.DeltaMinY;
					PassParameters->DeltaMaxY = BlendParams.DeltaMaxY;
					PassParameters->SourceTextureTwo = TextureTwo;
					PassParameters->SourceTextureThree = TextureThree;
					PassParameters->SourceTextureFour = TextureFour;
						
					break;
				}
			case EDisplacementMapBlendType::Passthrough:
			default:
				{
					PermutationVector.Set<FDisplacementMapBlendPS::FBlendType>(0);
					break;
				}
			}

			FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
			const TShaderMapRef<FDisplacementMapBlendPS> PixelShader(GlobalShaderMap, PermutationVector);

			FScreenPassRenderTarget SceneColorRenderTarget(OutputTexture, ERenderTargetLoadAction::ELoad);
			PassParameters->RenderTargets[0] = SceneColorRenderTarget.GetRenderTargetBinding();

			FPixelShaderUtils::AddFullscreenPass(
						GraphBuilder
						, GlobalShaderMap
						, RDG_EVENT_NAME("BlendingLensDisplacementMap")
						, PixelShader
						, PassParameters
						, FIntRect(0, 0, OutputTexture->Desc.Extent.X, OutputTexture->Desc.Extent.Y));            	

			GraphBuilder.Execute();
		});
	
	return true;
}
}

