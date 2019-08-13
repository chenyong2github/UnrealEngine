// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GenerateMips.h"
#include "RenderGraph.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"

#define MIPSSHADER_NUMTHREADS 8

//Struct for tracking GenerateMip resources that should only need to be created once.
struct FGenerateMipsStruct
{
	TRefCountPtr<IPooledRenderTarget> RenderTarget;
	FSamplerStateInitializerRHI Sampler;
};

//Generate mips compute shader declaration
class FGenerateMipsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGenerateMipsCS)
	SHADER_USE_PARAMETER_STRUCT(FGenerateMipsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector2D, TexelSize)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, MipInSRV)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, MipOutUAV)
		SHADER_PARAMETER_SAMPLER(SamplerState, MipSampler)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGenerateMipsCS, "/Engine/Private/ComputeGenerateMips.usf", "MainCS", SF_Compute);

//Initialise the texture for usage with RenderGraph and ComputeGenerateMips shader
FGenerateMipsStruct* FGenerateMips::SetupTexture(FRHITexture* InTexture,
	const FGenerateMipsParams& InParams)
{
	//Currently only 2D textures supported
	check(InTexture->GetTexture2D());
	if (!InTexture->GenMipsStruct)
	{
		InTexture->GenMipsStruct = MakeShareable(new FGenerateMipsStruct());

		FPooledRenderTargetDesc Desc;
		Desc.Extent.X = InTexture->GetSizeXYZ().X;
		Desc.Extent.Y = InTexture->GetSizeXYZ().Y;
		Desc.TargetableFlags = TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV;
		Desc.Format = InTexture->GetFormat();
		Desc.NumMips = InTexture->GetNumMips();;
		Desc.DebugName = TEXT("GenerateMipPooledRTTexture");

		//Create the Pooled Render Target Resource from the input texture
		FRHIResourceCreateInfo CreateInfo(Desc.DebugName);

		//Initialise a new render target texture for creating an RDG Texture
		FSceneRenderTargetItem RenderTexture;

		//Update all the RenderTexture info
		RenderTexture.TargetableTexture = InTexture;
		RenderTexture.ShaderResourceTexture = InTexture;

		RenderTexture.SRVs.Reserve(Desc.NumMips);
		RenderTexture.MipUAVs.Reserve(Desc.NumMips);
		for (uint8 MipLevel = 0; MipLevel < Desc.NumMips; MipLevel++)
		{
			FRHITextureSRVCreateInfo SRVDesc;
			SRVDesc.MipLevel = MipLevel;
			RenderTexture.SRVs.Add(SRVDesc, RHICreateShaderResourceView((FTexture2DRHIRef&)InTexture, SRVDesc));

			RenderTexture.MipUAVs.Add(RHICreateUnorderedAccessView(InTexture, MipLevel));
		}
		RHIBindDebugLabelName(RenderTexture.TargetableTexture, Desc.DebugName);
		RenderTexture.UAV = RenderTexture.MipUAVs[0];

		//Create the RenderTarget from the PooledRenderTarget Desc and the new RenderTexture object.
		GRenderTargetPool.CreateUntrackedElement(Desc, InTexture->GenMipsStruct->RenderTarget, RenderTexture);

		//Specify the Sampler details based on the input.
		InTexture->GenMipsStruct->Sampler.Filter = InParams.Filter;
		InTexture->GenMipsStruct->Sampler.AddressU = InParams.AddressU;
		InTexture->GenMipsStruct->Sampler.AddressV = InParams.AddressV;
		InTexture->GenMipsStruct->Sampler.AddressW = InParams.AddressW;
	}

	//Return the raw pointer
	return InTexture->GenMipsStruct.Get();
}

//Compute shader execution function for generating mips in real time.
void FGenerateMips::Compute(FRHICommandListImmediate& RHIImmCmdList, FRHITexture* InTexture)
{
	check(IsInRenderingThread());	
	//Currently only 2D textures supported
	check(InTexture->GetTexture2D());

	//Ensure the generate mips structure has been initialised correctly.
	FGenerateMipsStruct* GenMipsStruct = SetupTexture(InTexture);
	check(GenMipsStruct);

	//Begin rendergraph for executing the compute shader
	FRDGBuilder GraphBuilder(RHIImmCmdList);
	FRDGTextureRef GraphTexture = GraphBuilder.RegisterExternalTexture(GenMipsStruct->RenderTarget, TEXT("GenerateMipsGraphTexture"));
	TShaderMapRef<FGenerateMipsCS> ComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));

	//Loop through each level of the mips that require creation and add a dispatch pass per level,.
	for (uint8 MipLevel = 1; MipLevel < InTexture->GetNumMips(); MipLevel++)
	{
		int DestTextureSizeX = InTexture->GetSizeXYZ().X >> MipLevel;
		int DestTextureSizeY = InTexture->GetSizeXYZ().Y >> MipLevel;

		//Create the RDG viewable SRV, of a complete Mip, to read from
		FRDGTextureSRVDesc SRVDesc = FRDGTextureSRVDesc::CreateForMipLevel(GraphTexture, MipLevel - 1);
		//Create the RDG writeable UAV for the next mip level to be written to.
		FRDGTextureUAVDesc UAVDesc(GraphTexture, MipLevel);

		FGenerateMipsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGenerateMipsCS::FParameters>();
		//Texel size is 1/the total length of a side.
		PassParameters->TexelSize = FVector2D(1.0f / DestTextureSizeX, 1.0f / DestTextureSizeY);
		PassParameters->MipInSRV = GraphBuilder.CreateSRV(SRVDesc);
		PassParameters->MipOutUAV = GraphBuilder.CreateUAV(UAVDesc);
		PassParameters->MipSampler = RHIImmCmdList.CreateSamplerState(GenMipsStruct->Sampler);

		//Dispatch count is the destination's mip texture dimensions, so only the number required is executed.
		FIntVector GenMipsGroupCount(
			FMath::Max(DestTextureSizeX / MIPSSHADER_NUMTHREADS, 1),
			FMath::Max(DestTextureSizeY / MIPSSHADER_NUMTHREADS, 1),
			1);
		//Pass added per mip level to be written.
		ClearUnusedGraphResources(*ComputeShader, PassParameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("Generate2DTextureMips DestMipLevel=%d", MipLevel),
			PassParameters,
			ERDGPassFlags::Compute | ERDGPassFlags::GenerateMips,
			[PassParameters, ComputeShader, GenMipsGroupCount](FRHICommandList& RHICmdList)
		{
			FComputeShaderUtils::Dispatch(RHICmdList, *ComputeShader, *PassParameters, GenMipsGroupCount);
		});
	}
	GraphBuilder.QueueTextureExtraction(GraphTexture, &GenMipsStruct->RenderTarget);
	GraphBuilder.Execute();	
}

//Public execute function for calling the generate mips compute shader. Handles everything per platform.
void FGenerateMips::Execute(FRHICommandListImmediate& RHICmdList, FRHITexture* InTexture,
	const FGenerateMipsParams& InParams)
{
	//Only executes if mips are required.
	if (InTexture->GetNumMips() > 1)
	{
		//Checks whether the platform requires the compute shader. If not,executes that RHI's native generate mips call.
		if (RHIRequiresComputeGenerateMips())
		{
			//Generate the RenderGraph texture if required.
			if (!InTexture->GenMipsStruct)
			{
				SetupTexture(InTexture, InParams);
			}
			Compute(RHICmdList, InTexture);
		}
		else
		{
			RHICmdList.GenerateMips(InTexture);
		}
	}
}

void FGenerateMips::Execute(FRDGBuilder* GraphBuilder, FRDGTextureRef InGraphTexture, FRHISamplerState* InSampler)
{
	check(IsInRenderingThread());
	check(GraphBuilder);
	check(InGraphTexture);
	check(InSampler);

	TShaderMapRef<FGenerateMipsCS> ComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
	
	//Loop through each level of the mips that require creation and add a dispatch pass per level,.
	for (uint8 MipLevel = 1; MipLevel < InGraphTexture->Desc.NumMips; MipLevel++)
	{
		int DestTextureSizeX = InGraphTexture->Desc.Extent.X >> MipLevel;
		int DestTextureSizeY = InGraphTexture->Desc.Extent.Y >> MipLevel;

		//Create the RDG viewable SRV, of a complete Mip, to read from
		FRDGTextureSRVDesc SRVDesc = FRDGTextureSRVDesc::CreateForMipLevel(InGraphTexture, MipLevel - 1);
		//Create the RDG writeable UAV for the next mip level to be written to.
		FRDGTextureUAVDesc UAVDesc(InGraphTexture, MipLevel);

		FGenerateMipsCS::FParameters* PassParameters = GraphBuilder->AllocParameters<FGenerateMipsCS::FParameters>();
		//Texel size is 1/the total length of a side.
		PassParameters->TexelSize = FVector2D(1.0f / DestTextureSizeX, 1.0f / DestTextureSizeY);
		PassParameters->MipInSRV = GraphBuilder->CreateSRV(SRVDesc);
		PassParameters->MipOutUAV = GraphBuilder->CreateUAV(UAVDesc);
		PassParameters->MipSampler = InSampler;

		//Dispatch count is the destination's mip texture dimensions, so only the number required is executed.
		FIntVector GenMipsGroupCount(
			FMath::Max((DestTextureSizeX + MIPSSHADER_NUMTHREADS -1)/ MIPSSHADER_NUMTHREADS, 1),
			FMath::Max((DestTextureSizeY + MIPSSHADER_NUMTHREADS -1)/ MIPSSHADER_NUMTHREADS, 1),
			1);
		
		//Pass added per mip level to be written.
		ClearUnusedGraphResources(*ComputeShader, PassParameters);

		GraphBuilder->AddPass(
			RDG_EVENT_NAME("Generate2DTextureMips DestMipLevel=%d", MipLevel),
			PassParameters,
			ERDGPassFlags::Compute | ERDGPassFlags::GenerateMips,
			[PassParameters, ComputeShader, GenMipsGroupCount](FRHICommandList& RHICmdList)
		{
			FComputeShaderUtils::Dispatch(RHICmdList, *ComputeShader, *PassParameters, GenMipsGroupCount);
		});

	}
}
