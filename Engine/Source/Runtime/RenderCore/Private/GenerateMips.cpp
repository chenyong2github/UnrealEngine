// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenerateMips.h"
#include "RenderGraph.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "ShaderPermutation.h"
#include "RHIStaticStates.h"
#include "PipelineStateCache.h"
#include "ShaderParameterUtils.h"

#define MIPSSHADER_NUMTHREADS 8

//Struct for tracking GenerateMip resources that should only need to be created once.
struct FGenerateMipsStruct
{
	// Compute
	TRefCountPtr<IPooledRenderTarget> RenderTarget;

	// Rendering
	FVertexBufferRHIRef VertexBuffer;
	FVertexDeclarationRHIRef VertexDeclaration;

	// Both
	FSamplerStateInitializerRHI Sampler;
};

// -------------------------------------------------------------------------------------------------------------------------------

//Generate mips compute shader declaration
class FGenerateMipsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGenerateMipsCS)

public:
	class FGenMipsSRGB : SHADER_PERMUTATION_BOOL("GENMIPS_SRGB");
	class FGenMipsSwizzle : SHADER_PERMUTATION_BOOL("GENMIPS_SWIZZLE");
	using FPermutationDomain = TShaderPermutationDomain<FGenMipsSRGB, FGenMipsSwizzle>;

	SHADER_USE_PARAMETER_STRUCT(FGenerateMipsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector2D, TexelSize)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, MipInSRV)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, MipOutUAV)
		SHADER_PARAMETER_SAMPLER(SamplerState, MipSampler)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsComputeShaders(Parameters.Platform);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGenerateMipsCS, "/Engine/Private/ComputeGenerateMips.usf", "MainCS", SF_Compute);

// -------------------------------------------------------------------------------------------------------------------------------

class FMipsShadersVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMipsShadersVS);

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES2);
	}

	FMipsShadersVS() { }

	FMipsShadersVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{ }

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		return bShaderHasOutdatedParameters;
	}
};
IMPLEMENT_SHADER_TYPE(, FMipsShadersVS, TEXT("/Engine/Private/ComputeGenerateMips.usf"), TEXT("MainVS"), SF_Vertex);

// -------------------------------------------------------------------------------------------------------------------------------

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FMipsShadersUB, )
SHADER_PARAMETER_TEXTURE(Texture2D, Texture)
SHADER_PARAMETER_SAMPLER(SamplerState, SamplerP)
SHADER_PARAMETER(FVector2D, HalfTexelSize)
SHADER_PARAMETER(float, Level)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

class FMipsShadersPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMipsShadersPS);

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES2);
	}

	FMipsShadersPS() { }

	FMipsShadersPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{ }

	void SetParameters(FRHICommandList& RHICmdList, FTextureRHIRef Texture, FSamplerStateRHIRef SamplerState, const FVector2D & HalfTexelSize, int32 Level)
	{
		FMipsShadersUB UB;
		{
			UB.SamplerP = SamplerState;
			UB.Texture = Texture;
			UB.HalfTexelSize = HalfTexelSize;
			UB.Level = Level;
		}

		TUniformBufferRef<FMipsShadersUB> Data = TUniformBufferRef<FMipsShadersUB>::CreateUniformBufferImmediate(UB, UniformBuffer_SingleFrame);
		SetUniformBufferParameter(RHICmdList, GetPixelShader(), GetUniformBufferParameter<FMipsShadersUB>(), Data);
	}
};

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FMipsShadersUB, "MipsShadersUB");
IMPLEMENT_SHADER_TYPE(, FMipsShadersPS, TEXT("/Engine/Private/ComputeGenerateMips.usf"), TEXT("MainPS"), SF_Pixel);

// -------------------------------------------------------------------------------------------------------------------------------

void FGenerateMips::SetupRendering(FGenerateMipsStruct *GenMipsStruct, FRHITexture* InTexture, const FGenerateMipsParams& InParams)
{
	struct FMipsElementVertex
	{
		FVector4 Position;
		FVector2D TextureCoordinate;

		FMipsElementVertex() { }

		FMipsElementVertex(const FVector4& InPosition, const FVector2D& InTextureCoordinate)
			: Position(InPosition)
			, TextureCoordinate(InTextureCoordinate)
		{ }
	};

	FRHIResourceCreateInfo CreateInfo;
	GenMipsStruct->VertexBuffer = RHICreateVertexBuffer(sizeof(FMipsElementVertex) * 4, BUF_Volatile, CreateInfo);
	void* VoidPtr = RHILockVertexBuffer(GenMipsStruct->VertexBuffer, 0, sizeof(FMipsElementVertex) * 4, RLM_WriteOnly);

	FMipsElementVertex* Vertices = (FMipsElementVertex*)VoidPtr;
	Vertices[0].Position.Set(-1.0f, 1.0f, 1.0f, 1.0f); // Top Left
	Vertices[1].Position.Set(1.0f, 1.0f, 1.0f, 1.0f); // Top Right
	Vertices[2].Position.Set(-1.0f, -1.0f, 1.0f, 1.0f); // Bottom Left
	Vertices[3].Position.Set(1.0f, -1.0f, 1.0f, 1.0f); // Bottom Right
	Vertices[0].TextureCoordinate.Set(0.0f, 0.0f);
	Vertices[1].TextureCoordinate.Set(1.0f, 0.0f);
	Vertices[2].TextureCoordinate.Set(0.0f, 1.0f);
	Vertices[3].TextureCoordinate.Set(1.0f, 1.0f);
	RHIUnlockVertexBuffer(GenMipsStruct->VertexBuffer);

	// Vertex Declaration
	FVertexDeclarationElementList Elements;
	const uint16 Stride = sizeof(FMipsElementVertex);
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FMipsElementVertex, Position), VET_Float4, 0, Stride));
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FMipsElementVertex, TextureCoordinate), VET_Float2, 1, Stride));
	GenMipsStruct->VertexDeclaration = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);

	//Specify the Sampler details based on the input.
	GenMipsStruct->Sampler.Filter = InParams.Filter;
	GenMipsStruct->Sampler.AddressU = InParams.AddressU;
	GenMipsStruct->Sampler.AddressV = InParams.AddressV;
	GenMipsStruct->Sampler.AddressW = InParams.AddressW;
}


// Generate mips via classic rendering (2D textures only)
void FGenerateMips::RenderMips(FRHICommandListImmediate& CommandList, FRHITexture* InTexture, const FGenerateMipsParams& InParams, TSharedPtr<FGenerateMipsStruct> * ExternalMipsStructCache)
{
	check(IsInRenderingThread());
	check(InTexture->GetNumMips() > 1);

	FGenerateMipsStruct *GenMipsStruct;
	FGenerateMipsStruct _LocalGenMipsStruct;
	if (ExternalMipsStructCache)
	{
		if (!*ExternalMipsStructCache)
		{
			*ExternalMipsStructCache = MakeShareable(new FGenerateMipsStruct());
			SetupRendering(ExternalMipsStructCache->Get(), InTexture, InParams);
		}
		GenMipsStruct = ExternalMipsStructCache->Get();
	}
	else
	{
		GenMipsStruct =  &_LocalGenMipsStruct;
		SetupRendering(GenMipsStruct, InTexture, InParams);
	}

	// Same basic general state each loop
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.BlendState = TStaticBlendStateWriteMask<CW_RGBA, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE>::GetRHI();
	GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;
	
	auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FMipsShadersVS> VertexShader(ShaderMap);
	TShaderMapRef<FMipsShadersPS> CopyShader(ShaderMap);

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GenMipsStruct->VertexDeclaration;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*CopyShader);

	uint32 NumMips = InTexture->GetNumMips();
	for (uint32 MipLevel=1; MipLevel<NumMips; ++MipLevel)
	{
		int32 Width = InTexture->GetSizeXYZ().X >> MipLevel;
		int32 Height = InTexture->GetSizeXYZ().Y >> MipLevel;

		FRHIRenderPassInfo RPInfo(InTexture, ERenderTargetActions::DontLoad_Store, nullptr, MipLevel);
		CommandList.BeginRenderPass(RPInfo, TEXT("GenMipsLevel"));
		{
			CommandList.ApplyCachedRenderTargets(GraphicsPSOInit);
			CommandList.SetViewport(0, 0, 0.0f, Width, Height, 1.0f);

			SetGraphicsPipelineState(CommandList, GraphicsPSOInit);
			CopyShader->SetParameters(CommandList, InTexture, CommandList.CreateSamplerState(GenMipsStruct->Sampler), FVector2D(0.5f / Width,  0.5f / Height), MipLevel - 1);

			CommandList.SetStreamSource(0, GenMipsStruct->VertexBuffer, 0);
			CommandList.DrawPrimitive(0, 2, 1);
		}
		CommandList.EndRenderPass();
	}
}


//Initialise the texture for usage with RenderGraph and ComputeGenerateMips shader
TSharedPtr<FGenerateMipsStruct> FGenerateMips::SetupTexture(FRHITexture* InTexture, const FGenerateMipsParams& InParams)
{
	//Currently only 2D textures supported
	check(InTexture->GetTexture2D());

	TSharedPtr<FGenerateMipsStruct> GenMipsStruct = MakeShareable(new FGenerateMipsStruct());

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
	GRenderTargetPool.CreateUntrackedElement(Desc, GenMipsStruct->RenderTarget, RenderTexture);

		//Specify the Sampler details based on the input.
	GenMipsStruct->Sampler.Filter = InParams.Filter;
	GenMipsStruct->Sampler.AddressU = InParams.AddressU;
	GenMipsStruct->Sampler.AddressV = InParams.AddressV;
	GenMipsStruct->Sampler.AddressW = InParams.AddressW;

	return GenMipsStruct;
}

//Compute shader execution function for generating mips in real time.
void FGenerateMips::Compute(FRHICommandListImmediate& RHIImmCmdList, FRHITexture* InTexture, TSharedPtr<FGenerateMipsStruct> GenMipsStruct)
{
	check(IsInRenderingThread());	
	//Currently only 2D textures supported
	check(InTexture->GetTexture2D());

	//Ensure the generate mips structure has been initialised correctly.
	check(GenMipsStruct);

	//Begin rendergraph for executing the compute shader
	FRDGBuilder GraphBuilder(RHIImmCmdList);
	FRDGTextureRef GraphTexture = GraphBuilder.RegisterExternalTexture(GenMipsStruct->RenderTarget, TEXT("GenerateMipsGraphTexture"));

	//Select compute shader variant (normal vs. sRGB etc.)
#if PLATFORM_ANDROID
	const bool bIsUsingVulkan = FAndroidMisc::ShouldUseVulkan();
	const bool bCanDoSRGB = (GMaxRHIFeatureLevel > ERHIFeatureLevel::ES2);
#else
	const bool bIsUsingVulkan = false;
	const bool bCanDoSRGB = true;
#endif
	FGenerateMipsCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FGenerateMipsCS::FGenMipsSRGB>(!!(InTexture->GetFlags() & TexCreate_SRGB) && bCanDoSRGB);
	// TEMP: On Vulkan we experience RGB being swizzled around, this little switch here circumvents the issue
	PermutationVector.Set<FGenerateMipsCS::FGenMipsSwizzle>(bIsUsingVulkan);
	TShaderMapRef<FGenerateMipsCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);

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
			FMath::Max((DestTextureSizeX + MIPSSHADER_NUMTHREADS - 1) / MIPSSHADER_NUMTHREADS, 1),
			FMath::Max((DestTextureSizeY + MIPSSHADER_NUMTHREADS - 1) / MIPSSHADER_NUMTHREADS, 1),
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
void FGenerateMips::Execute(FRHICommandListImmediate& RHICmdList, FRHITexture* InTexture, const FGenerateMipsParams& InParams, TSharedPtr<FGenerateMipsStruct> * ExternalMipsStructCache, bool bAllowRenderBasedGeneration)
{
	//Only executes if mips are required.
	if (InTexture->GetNumMips() > 1)
	{
		//Checks whether the platform requires the compute shader. If not,executes that RHI's native generate mips call.
		if (RHIRequiresComputeGenerateMips())
		{
#if PLATFORM_ANDROID
			if (bAllowRenderBasedGeneration)
			{
				RenderMips(RHICmdList, InTexture, InParams, ExternalMipsStructCache);
				return;
			}
			check(!"Vulkan, ES2 & ES3.1 do not support suitable compute features (output format selection via HLSL crosscompile) for mip generation currently; use rendering based generation if possible.");
#endif
			//Generate the RenderGraph texture if required.

			// Do we have an external cache for the parameters we will use?
			if (!ExternalMipsStructCache)
			{
				// No: Old path to keep things compatible
				//THIS WILL CAUSE CIRCULAR REFS BETWEEN THE PARAMETERS AND THE TEXTURE -> LEAK!
			if (!InTexture->GenMipsStruct)
			{
					InTexture->GenMipsStruct = SetupTexture(InTexture, InParams);
			}
				Compute(RHICmdList, InTexture, InTexture->GenMipsStruct);
		}
		else
		{
				// Use external location to cache params etc.

				// Already valid?
				if (!(*ExternalMipsStructCache))
				{
					// No, populate...
					*ExternalMipsStructCache = SetupTexture(InTexture, InParams);
				}
				Compute(RHICmdList, InTexture, *ExternalMipsStructCache);
			}
		}
		else
		{
			// Fallback to platform's native implementation
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

	FGenerateMipsCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FGenerateMipsCS::FGenMipsSRGB>(!!(InGraphTexture->Desc.Flags & TexCreate_SRGB));
	PermutationVector.Set<FGenerateMipsCS::FGenMipsSwizzle>(false);
	TShaderMapRef<FGenerateMipsCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
	
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
