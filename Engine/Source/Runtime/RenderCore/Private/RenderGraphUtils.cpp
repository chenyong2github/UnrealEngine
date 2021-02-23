// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderGraphUtils.h"
#include "ClearQuad.h"
#include "ClearReplacementShaders.h"
#include "ShaderParameterUtils.h"
#include "RenderTargetPool.h"
#include <initializer_list>
#include "GlobalShader.h"
#include "PixelShaderUtils.h"

void ClearUnusedGraphResourcesImpl(
	const FShaderParameterBindings& ShaderBindings,
	const FShaderParametersMetadata* ParametersMetadata,
	void* InoutParameters,
	std::initializer_list<FRDGResourceRef> ExcludeList)
{
	const auto& GraphResources = ParametersMetadata->GetLayout().GraphResources;

	int32 ShaderResourceIndex = 0;
	int32 GraphUniformBufferId = 0;
	auto Base = reinterpret_cast<uint8*>(InoutParameters);

	for (int32 GraphResourceIndex = 0, GraphResourceCount = GraphResources.Num(); GraphResourceIndex < GraphResourceCount; GraphResourceIndex++)
	{
		const EUniformBufferBaseType Type = GraphResources[GraphResourceIndex].MemberType;
		const uint16 ByteOffset = GraphResources[GraphResourceIndex].MemberOffset;

		if (Type == UBMT_RDG_TEXTURE ||
			Type == UBMT_RDG_TEXTURE_SRV ||
			Type == UBMT_RDG_TEXTURE_UAV ||
			Type == UBMT_RDG_BUFFER_SRV ||
			Type == UBMT_RDG_BUFFER_UAV)
		{
			const auto& ResourceParameters = ShaderBindings.ResourceParameters;
			const int32 ShaderResourceCount = ResourceParameters.Num();
			for (; ShaderResourceIndex < ShaderResourceCount && ResourceParameters[ShaderResourceIndex].ByteOffset < ByteOffset; ++ShaderResourceIndex)
			{
			}

			if (ShaderResourceIndex < ShaderResourceCount && ResourceParameters[ShaderResourceIndex].ByteOffset == ByteOffset)
			{
				continue;
			}
		}
		else if (Type == UBMT_RDG_UNIFORM_BUFFER)
		{
			if (GraphUniformBufferId < ShaderBindings.GraphUniformBuffers.Num() && ByteOffset == ShaderBindings.GraphUniformBuffers[GraphUniformBufferId].ByteOffset)
			{
				GraphUniformBufferId++;
				continue;
			}

			FRDGUniformBufferRef UniformBuffer = *reinterpret_cast<FRDGUniformBufferRef*>(Base + ByteOffset);
			if (!UniformBuffer || UniformBuffer->IsGlobal())
			{
				continue;
			}
		}
		else
		{
			continue;
		}

		FRDGResourceRef* ResourcePtr = reinterpret_cast<FRDGResourceRef*>(Base + ByteOffset);

		for (FRDGResourceRef ExcludeResource : ExcludeList)
		{
			if (*ResourcePtr == ExcludeResource)
			{
				continue;
			}
		}

		*ResourcePtr = nullptr;
	}
}

void ClearUnusedGraphResourcesImpl(
	TArrayView<const FShaderParameterBindings*> ShaderBindingsList,
	const FShaderParametersMetadata* ParametersMetadata,
	void* InoutParameters,
	std::initializer_list<FRDGResourceRef> ExcludeList)
{
	const auto& GraphResources = ParametersMetadata->GetLayout().GraphResources;

	TArray<int32, TInlineAllocator<SF_NumFrequencies>> ShaderResourceIds;
	TArray<int32, TInlineAllocator<SF_NumFrequencies>> GraphUniformBufferIds;
	ShaderResourceIds.SetNumZeroed(ShaderBindingsList.Num());
	GraphUniformBufferIds.SetNumZeroed(ShaderBindingsList.Num());

	auto Base = reinterpret_cast<uint8*>(InoutParameters);

	for (int32 GraphResourceIndex = 0, GraphResourceCount = GraphResources.Num(); GraphResourceIndex < GraphResourceCount; GraphResourceIndex++)
	{
		EUniformBufferBaseType Type = GraphResources[GraphResourceIndex].MemberType;
		uint16 ByteOffset = GraphResources[GraphResourceIndex].MemberOffset;
		bool bResourceIsUsed = false;

		if (Type == UBMT_RDG_TEXTURE ||
			Type == UBMT_RDG_TEXTURE_SRV ||
			Type == UBMT_RDG_TEXTURE_UAV ||
			Type == UBMT_RDG_BUFFER_SRV ||
			Type == UBMT_RDG_BUFFER_UAV)
		{
			for (int32 Index = 0; Index < ShaderBindingsList.Num(); ++Index)
			{
				const auto& ResourceParameters = ShaderBindingsList[Index]->ResourceParameters;
				int32& ShaderResourceId = ShaderResourceIds[Index];
				for (; ShaderResourceId < ResourceParameters.Num() && ResourceParameters[ShaderResourceId].ByteOffset < ByteOffset; ++ShaderResourceId)
				{
				}
				bResourceIsUsed |= ShaderResourceId < ResourceParameters.Num() && ByteOffset == ResourceParameters[ShaderResourceId].ByteOffset;
			}
		}
		else if (Type == UBMT_RDG_UNIFORM_BUFFER)
		{
			for (int32 Index = 0; Index < ShaderBindingsList.Num(); ++Index)
			{
				const auto& GraphUniformBuffers = ShaderBindingsList[Index]->GraphUniformBuffers;
				int32& GraphUniformBufferId = GraphUniformBufferIds[Index];
				for (; GraphUniformBufferId < GraphUniformBuffers.Num() && GraphUniformBuffers[GraphUniformBufferId].ByteOffset < ByteOffset; ++GraphUniformBufferId)
				{
				}
				bResourceIsUsed |= GraphUniformBufferId < GraphUniformBuffers.Num() && ByteOffset == GraphUniformBuffers[GraphUniformBufferId].ByteOffset;
			}

			FRDGUniformBufferRef UniformBuffer = *reinterpret_cast<FRDGUniformBufferRef*>(Base + ByteOffset);
			if (!UniformBuffer || UniformBuffer->IsGlobal())
			{
				continue;
			}
		}
		else
		{
			// Not a resource we care about.
			continue;
		}

		if (bResourceIsUsed)
		{
			continue;
		}

		FRDGResourceRef* ResourcePtr = reinterpret_cast<FRDGResourceRef*>(Base + ByteOffset);

		for (FRDGResourceRef ExcludeResource : ExcludeList)
		{
			if (*ResourcePtr == ExcludeResource)
			{
				continue;
			}
		}

		*ResourcePtr = nullptr;
	}
}

FRDGTextureRef RegisterExternalTextureWithFallback(
	FRDGBuilder& GraphBuilder,
	const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture,
	const TRefCountPtr<IPooledRenderTarget>& FallbackPooledTexture,
	ERenderTargetTexture ExternalTexture,
	ERenderTargetTexture FallbackTexture)
{
	ensureMsgf(FallbackPooledTexture.IsValid(), TEXT("RegisterExternalTextureWithDummyFallback() requires a valid fallback pooled texture."));
	if (ExternalPooledTexture.IsValid())
	{
		return GraphBuilder.RegisterExternalTexture(ExternalPooledTexture, ExternalTexture);
	}
	else
	{
		return GraphBuilder.RegisterExternalTexture(FallbackPooledTexture, FallbackTexture);
	}
}

RENDERCORE_API FRDGTextureMSAA CreateTextureMSAA(
	FRDGBuilder& GraphBuilder,
	FRDGTextureDesc Desc,
	const TCHAR* Name,
	ETextureCreateFlags ResolveFlagsToAdd)
{
	FRDGTextureMSAA Texture(GraphBuilder.CreateTexture(Desc, Name));

	if (Desc.NumSamples > 1)
	{
		Desc.NumSamples = 1;
		ETextureCreateFlags ResolveFlags = TexCreate_ShaderResource;
		if (EnumHasAnyFlags(Desc.Flags, TexCreate_DepthStencilTargetable))
		{
			ResolveFlags |= TexCreate_DepthStencilResolveTarget;
		}
		else
		{
			ResolveFlags |= TexCreate_ResolveTargetable;
		}
		Desc.Flags = ResolveFlags | ResolveFlagsToAdd;
		Texture.Resolve = GraphBuilder.CreateTexture(Desc, Name);
	}

	return Texture;
}

FRDGTextureMSAA RegisterExternalTextureMSAAWithFallback(
	FRDGBuilder& GraphBuilder,
	const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture,
	const TRefCountPtr<IPooledRenderTarget>& FallbackPooledTexture)
{
	ensureMsgf(FallbackPooledTexture.IsValid(), TEXT("RegisterExternalTextureWithDummyFallback() requires a valid fallback pooled texture."));
	if (ExternalPooledTexture.IsValid())
	{
		return RegisterExternalTextureMSAA(GraphBuilder, ExternalPooledTexture);
	}
	else
	{
		return RegisterExternalTextureMSAA(GraphBuilder, FallbackPooledTexture);
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FCopyTextureParameters, )
	RDG_TEXTURE_ACCESS(Input,  ERHIAccess::CopySrc)
	RDG_TEXTURE_ACCESS(Output, ERHIAccess::CopyDest)
END_SHADER_PARAMETER_STRUCT()

void AddCopyTexturePass(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef InputTexture,
	FRDGTextureRef OutputTexture,
	const FRHICopyTextureInfo& CopyInfo)
{
	const FRDGTextureDesc& InputDesc = InputTexture->Desc;
	const FRDGTextureDesc& OutputDesc = OutputTexture->Desc;
	checkf(InputDesc.Format == OutputDesc.Format, TEXT("This method does not support format conversion."));

	FCopyTextureParameters* Parameters = GraphBuilder.AllocParameters<FCopyTextureParameters>();
	Parameters->Input = InputTexture;
	Parameters->Output = OutputTexture;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("CopyTexture(%s -> %s)", InputTexture->Name, OutputTexture->Name),
		Parameters,
		ERDGPassFlags::Copy,
		[InputTexture, OutputTexture, CopyInfo](FRHICommandList& RHICmdList)
	{
		RHICmdList.CopyTexture(InputTexture->GetRHI(), OutputTexture->GetRHI(), CopyInfo);
	});
}

BEGIN_SHADER_PARAMETER_STRUCT(FCopyToResolveTargetParameters, )
	RDG_TEXTURE_ACCESS_DYNAMIC(Input)
	RDG_TEXTURE_ACCESS_DYNAMIC(Output)
END_SHADER_PARAMETER_STRUCT()

void AddCopyToResolveTargetPass(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef InputTexture,
	FRDGTextureRef OutputTexture,
	const FResolveParams& ResolveParams)
{
	check(InputTexture && OutputTexture);

	if (InputTexture == OutputTexture)
	{
		return;
	}

	ERHIAccess AccessSource = ERHIAccess::ResolveSrc;
	ERHIAccess AccessDest = ERHIAccess::ResolveDst;

	// This might also just be a copy.
	if (InputTexture->Desc.NumSamples == OutputTexture->Desc.NumSamples)
	{
		AccessSource = ERHIAccess::CopySrc;
		AccessDest = ERHIAccess::CopyDest;
	}

	FCopyToResolveTargetParameters* Parameters = GraphBuilder.AllocParameters<FCopyToResolveTargetParameters>();
	Parameters->Input = FRDGTextureAccess(InputTexture, AccessSource);
	Parameters->Output = FRDGTextureAccess(OutputTexture, AccessDest);

	FResolveParams LocalResolveParams = ResolveParams;
	LocalResolveParams.SourceAccessFinal = AccessSource;
	LocalResolveParams.DestAccessFinal = AccessDest;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("CopyToResolveTarget(%s -> %s)", InputTexture->Name, OutputTexture->Name),
		Parameters,
		ERDGPassFlags::Copy | ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass,
		[InputTexture, OutputTexture, LocalResolveParams](FRHICommandList& RHICmdList)
	{
		RHICmdList.CopyToResolveTarget(InputTexture->GetRHI(), OutputTexture->GetRHI(), LocalResolveParams);
	});
}

BEGIN_SHADER_PARAMETER_STRUCT(FClearBufferUAVParameters, )
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, BufferUAV)
END_SHADER_PARAMETER_STRUCT()

void AddClearUAVPass(FRDGBuilder& GraphBuilder, FRDGBufferUAVRef BufferUAV, uint32 Value)
{
	FClearBufferUAVParameters* Parameters = GraphBuilder.AllocParameters<FClearBufferUAVParameters>();
	Parameters->BufferUAV = BufferUAV;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("ClearBuffer(%s Size=%ubytes)", BufferUAV->GetParent()->Name, BufferUAV->GetParent()->Desc.GetTotalNumBytes()),
		Parameters,
		ERDGPassFlags::Compute,
		[&Parameters, BufferUAV, Value](FRHIComputeCommandList& RHICmdList)
	{
		RHICmdList.ClearUAVUint(BufferUAV->GetRHI(), FUintVector4(Value, Value, Value, Value));
		BufferUAV->MarkResourceAsUsed();
	});
}

void AddClearUAVFloatPass(FRDGBuilder& GraphBuilder, FRDGBufferUAVRef BufferUAV, float Value)
{
	FClearBufferUAVParameters* Parameters = GraphBuilder.AllocParameters<FClearBufferUAVParameters>();
	Parameters->BufferUAV = BufferUAV;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("ClearBuffer(%s Size=%ubytes)", BufferUAV->GetParent()->Name, BufferUAV->GetParent()->Desc.GetTotalNumBytes()),
		Parameters,
		ERDGPassFlags::Compute,
		[&Parameters, BufferUAV, Value](FRHIComputeCommandList& RHICmdList)
		{
			RHICmdList.ClearUAVFloat(BufferUAV->GetRHI(), FVector4(Value, Value, Value, Value));
			BufferUAV->MarkResourceAsUsed();
		});
}

BEGIN_SHADER_PARAMETER_STRUCT(FClearTextureUAVParameters, )
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, TextureUAV)
END_SHADER_PARAMETER_STRUCT()

void AddClearUAVPass(FRDGBuilder& GraphBuilder, FRDGTextureUAVRef TextureUAV, const FUintVector4& ClearValues)
{
	check(TextureUAV);

	FClearTextureUAVParameters* Parameters = GraphBuilder.AllocParameters<FClearTextureUAVParameters>();
	Parameters->TextureUAV = TextureUAV;

	FRDGTextureRef Texture = TextureUAV->GetParent();

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("ClearTextureUint(%s %s %dx%d Mip=%d)",
			Texture->Name,
			GPixelFormats[Texture->Desc.Format].Name,
			Texture->Desc.Extent.X, Texture->Desc.Extent.Y,
			int32(TextureUAV->Desc.MipLevel)),
		Parameters,
		ERDGPassFlags::Compute,
		[&Parameters, TextureUAV, ClearValues](FRHIComputeCommandList& RHICmdList)
	{
		const FRDGTextureDesc& LocalTextureDesc = TextureUAV->GetParent()->Desc;

		FRHIUnorderedAccessView* RHITextureUAV = TextureUAV->GetRHI();

		RHICmdList.ClearUAVUint(RHITextureUAV, ClearValues);
		TextureUAV->MarkResourceAsUsed();
	});
}

void AddClearUAVPass(FRDGBuilder& GraphBuilder, FRDGTextureUAVRef TextureUAV, const FVector4& ClearValues)
{
	check(TextureUAV);

	FClearTextureUAVParameters* Parameters = GraphBuilder.AllocParameters<FClearTextureUAVParameters>();
	Parameters->TextureUAV = TextureUAV;

	const FRDGTextureDesc& TextureDesc = TextureUAV->GetParent()->Desc;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("ClearTextureFloat(%s) %dx%d", TextureUAV->GetParent()->Name, TextureDesc.Extent.X, TextureDesc.Extent.Y),
		Parameters,
		ERDGPassFlags::Compute,
		[&Parameters, TextureUAV, ClearValues](FRHIComputeCommandList& RHICmdList)
	{
		const FRDGTextureDesc& LocalTextureDesc = TextureUAV->GetParent()->Desc;

		FRHIUnorderedAccessView* RHITextureUAV = TextureUAV->GetRHI();

		RHICmdList.ClearUAVFloat(RHITextureUAV, ClearValues);
		TextureUAV->MarkResourceAsUsed();
	});
}

void AddClearUAVPass(FRDGBuilder& GraphBuilder, FRDGTextureUAVRef TextureUAV, const uint32(&ClearValues)[4])
{
	AddClearUAVPass(GraphBuilder, TextureUAV, FUintVector4(ClearValues[0], ClearValues[1], ClearValues[2], ClearValues[3]));
}

void AddClearUAVPass(FRDGBuilder& GraphBuilder, FRDGTextureUAVRef TextureUAV, const float(&ClearValues)[4])
{
	AddClearUAVPass(GraphBuilder, TextureUAV, FVector4(ClearValues[0], ClearValues[1], ClearValues[2], ClearValues[3]));
}

void AddClearUAVPass(FRDGBuilder& GraphBuilder, FRDGTextureUAVRef TextureUAV, const FLinearColor& ClearColor)
{
	AddClearUAVPass(GraphBuilder, TextureUAV, FVector4(ClearColor.R, ClearColor.G, ClearColor.B, ClearColor.A));
}

class FClearUAVRectsPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearUAVRectsPS);
	SHADER_USE_PARAMETER_STRUCT(FClearUAVRectsPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FUintVector4, ClearValue)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, ClearResource)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		int32 ResourceType = RHIGetPreferredClearUAVRectPSResourceType(Parameters.Platform);

		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("ENABLE_CLEAR_VALUE"), 1);
		OutEnvironment.SetDefine(TEXT("RESOURCE_TYPE"), ResourceType);
		OutEnvironment.SetDefine(TEXT("VALUE_TYPE"), TEXT("uint4"));
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearUAVRectsPS, "/Engine/Private/ClearReplacementShaders.usf", "ClearTextureRWPS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FClearUAVRectsParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FPixelShaderUtils::FRasterizeToRectsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FClearUAVRectsPS::FParameters, PS)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void AddClearUAVPass(FRDGBuilder& GraphBuilder, FRDGTextureUAVRef TextureUAV, const uint32(&ClearValues)[4], FRDGBufferSRVRef RectMinMaxBufferSRV, uint32 NumRects)
{
	if (NumRects == 0)
	{
		AddClearUAVPass(GraphBuilder, TextureUAV, ClearValues);
		return;
	}

	check(TextureUAV && RectMinMaxBufferSRV);

	FClearUAVRectsParameters* PassParameters = GraphBuilder.AllocParameters<FClearUAVRectsParameters>();

	PassParameters->PS.ClearValue.X = ClearValues[0];
	PassParameters->PS.ClearValue.Y = ClearValues[1];
	PassParameters->PS.ClearValue.Z = ClearValues[2];
	PassParameters->PS.ClearValue.W = ClearValues[3];
	PassParameters->PS.ClearResource = TextureUAV;

	auto* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	auto PixelShader = ShaderMap->GetShader<FClearUAVRectsPS>();

	const FRDGTextureRef Texture = TextureUAV->GetParent();
	const FIntPoint TextureSize = Texture->Desc.Extent;

	FPixelShaderUtils::AddRasterizeToRectsPass<FClearUAVRectsPS>(GraphBuilder,
		ShaderMap,
		RDG_EVENT_NAME("ClearTextureRects(%s %s %dx%d Mip=%d)",
			Texture->Name,
			GPixelFormats[Texture->Desc.Format].Name,
			Texture->Desc.Extent.X, Texture->Desc.Extent.Y,
			int32(TextureUAV->Desc.MipLevel)),
		PixelShader,
		PassParameters,
		TextureSize,
		RectMinMaxBufferSRV,
		NumRects,
		TStaticBlendState<>::GetRHI(),
		TStaticRasterizerState<>::GetRHI(),
		TStaticDepthStencilState<false, CF_Always>::GetRHI());
}

void AddClearRenderTargetPass(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture)
{
	check(Texture);

	FRenderTargetParameters* Parameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
	Parameters->RenderTargets[0] = FRenderTargetBinding(Texture, ERenderTargetLoadAction::EClear);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("ClearRenderTarget(%s) %dx%d ClearAction", Texture->Name, Texture->Desc.Extent.X, Texture->Desc.Extent.Y),
		Parameters,
		ERDGPassFlags::Raster,
		[](FRHICommandList& RHICmdList) {});
}

void AddClearRenderTargetPass(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture, const FLinearColor& ClearColor)
{
	if (Texture->Desc.ClearValue.ColorBinding == EClearBinding::EColorBound && Texture->Desc.ClearValue.GetClearColor() == ClearColor)
	{
		AddClearRenderTargetPass(GraphBuilder, Texture);
	}
	else
	{
		AddClearRenderTargetPass(GraphBuilder, Texture, ClearColor, FIntRect(FIntPoint::ZeroValue, Texture->Desc.Extent));
	}
}

void AddClearRenderTargetPass(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture, const FLinearColor& ClearColor, FIntRect Viewport)
{
	check(Texture);

	FRenderTargetParameters* Parameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
	Parameters->RenderTargets[0] = FRenderTargetBinding(Texture, ERenderTargetLoadAction::ENoAction);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("ClearRenderTarget(%s) [(%d, %d), (%d, %d)] ClearQuad", Texture->Name, Viewport.Min.X, Viewport.Min.Y, Viewport.Max.X, Viewport.Max.Y),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, ClearColor, Viewport](FRHICommandList& RHICmdList)
	{
		RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);
		DrawClearQuad(RHICmdList, ClearColor);
	});
}

void AddClearDepthStencilPass(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef Texture,
	bool bClearDepth,
	float Depth,
	bool bClearStencil,
	uint8 Stencil)
{
	check(Texture);

	FExclusiveDepthStencil ExclusiveDepthStencil;
	ERenderTargetLoadAction DepthLoadAction = ERenderTargetLoadAction::ELoad;
	ERenderTargetLoadAction StencilLoadAction = ERenderTargetLoadAction::ENoAction;

	const bool bHasStencil = Texture->Desc.Format == PF_DepthStencil;

	// We can't clear stencil if we don't have it.
	bClearStencil &= bHasStencil;

	if (bClearDepth)
	{
		ExclusiveDepthStencil.SetDepthWrite();
		DepthLoadAction = ERenderTargetLoadAction::ENoAction;
	}

	if (bHasStencil)
	{
		if (bClearStencil)
		{
			ExclusiveDepthStencil.SetStencilWrite();
			StencilLoadAction = ERenderTargetLoadAction::ENoAction;
		}
		else
		{
			// Preserve stencil contents.
			StencilLoadAction = ERenderTargetLoadAction::ELoad;
		}
	}

	FRenderTargetParameters* Parameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
	Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(Texture, DepthLoadAction, StencilLoadAction, ExclusiveDepthStencil);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("ClearDepthStencil(%s) %dx%d", Texture->Name, Texture->Desc.Extent.X, Texture->Desc.Extent.Y),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, bClearDepth, Depth, bClearStencil, Stencil](FRHICommandList& RHICmdList)
	{
		DrawClearQuad(RHICmdList, false, FLinearColor(), bClearDepth, Depth, bClearStencil, Stencil);
	});
}

void AddClearStencilPass(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture)
{
	auto* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(Texture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::EClear, FExclusiveDepthStencil::DepthRead_StencilWrite);
	GraphBuilder.AddPass(RDG_EVENT_NAME("ClearStencil (%s)", Texture->Name), PassParameters, ERDGPassFlags::Raster, [](FRHICommandList&) {});
}

BEGIN_SHADER_PARAMETER_STRUCT(FEnqueueCopyTexturePass, )
	RDG_TEXTURE_ACCESS(Texture, ERHIAccess::CopySrc)
END_SHADER_PARAMETER_STRUCT()

void AddEnqueueCopyPass(FRDGBuilder& GraphBuilder, FRHIGPUTextureReadback* Readback, FRDGTextureRef SourceTexture, FResolveRect Rect)
{
	FEnqueueCopyTexturePass* PassParameters = GraphBuilder.AllocParameters<FEnqueueCopyTexturePass>();
	PassParameters->Texture = SourceTexture;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("EnqueueCopy(%s)", SourceTexture->Name),
		PassParameters,
		ERDGPassFlags::Readback,
		[Readback, SourceTexture, Rect](FRHICommandList& RHICmdList)
	{
		Readback->EnqueueCopyRDG(RHICmdList, SourceTexture->GetRHI(), Rect);
	});
}

BEGIN_SHADER_PARAMETER_STRUCT(FEnqueueCopyBufferPass, )
	RDG_BUFFER_ACCESS(Buffer, ERHIAccess::CopySrc)
END_SHADER_PARAMETER_STRUCT()

void AddEnqueueCopyPass(FRDGBuilder& GraphBuilder, FRHIGPUBufferReadback* Readback, FRDGBufferRef SourceBuffer, uint32 NumBytes)
{
	FEnqueueCopyBufferPass* PassParameters = GraphBuilder.AllocParameters<FEnqueueCopyBufferPass>();
	PassParameters->Buffer = SourceBuffer;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("EnqueueCopy(%s)", SourceBuffer->Name),
		PassParameters,
		ERDGPassFlags::Readback,
		[Readback, SourceBuffer, NumBytes](FRHICommandList& RHICmdList)
	{
		Readback->EnqueueCopy(RHICmdList, SourceBuffer->GetRHIVertexBuffer(), NumBytes);
	});
}

class FClearUAVUIntCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearUAVUIntCS)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, UAV)
		SHADER_PARAMETER(uint32, ClearValue)
		SHADER_PARAMETER(uint32, NumEntries)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5;
	}

	FClearUAVUIntCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		Bindings.BindForLegacyShaderParameters(this, Initializer.PermutationId, Initializer.ParameterMap, *FParameters::FTypeInfo::GetStructMetadata());
	}

	FClearUAVUIntCS()
	{
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearUAVUIntCS, "/Engine/Private/Tools/ClearUAV.usf", "ClearUAVUIntCS", SF_Compute);

void FComputeShaderUtils::ClearUAV(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, FRDGBufferUAVRef UAV, uint32 ClearValue)
{
	FClearUAVUIntCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearUAVUIntCS::FParameters>();
	PassParameters->UAV = UAV;
	PassParameters->ClearValue = ClearValue;
	check(UAV->Desc.Format == PF_R32_UINT);
	PassParameters->NumEntries = UAV->Desc.Buffer->Desc.NumElements;
	check(PassParameters->NumEntries > 0);

	auto ComputeShader = ShaderMap->GetShader<FClearUAVUIntCS>();

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("ClearUAV"),
		ComputeShader,
		PassParameters,
		FIntVector(FMath::DivideAndRoundUp<int32>(PassParameters->NumEntries, 64), 1, 1));
}

class FClearUAVFloatCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearUAVFloatCS)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float4>, UAVFloat)
		SHADER_PARAMETER(FVector4, ClearValueFloat)
		SHADER_PARAMETER(uint32, NumEntries)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5;
	}

	FClearUAVFloatCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		Bindings.BindForLegacyShaderParameters(this, Initializer.PermutationId, Initializer.ParameterMap, *FParameters::FTypeInfo::GetStructMetadata());
	}

	FClearUAVFloatCS()
	{
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearUAVFloatCS, "/Engine/Private/Tools/ClearUAV.usf", "ClearUAVFloatCS", SF_Compute);

void FComputeShaderUtils::ClearUAV(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, FRDGBufferUAVRef UAV, FVector4 ClearValue)
{
	FClearUAVFloatCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearUAVFloatCS::FParameters>();
	PassParameters->UAVFloat = UAV;
	PassParameters->ClearValueFloat = ClearValue;
	check(UAV->Desc.Format == PF_A32B32G32R32F || UAV->Desc.Format == PF_FloatRGBA);
	PassParameters->NumEntries = UAV->Desc.Buffer->Desc.NumElements;
	check(PassParameters->NumEntries > 0);

	auto ComputeShader = ShaderMap->GetShader<FClearUAVFloatCS>();

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("ClearUAV"),
		ComputeShader,
		PassParameters,
		FIntVector(FMath::DivideAndRoundUp<int32>(PassParameters->NumEntries, 64), 1, 1));
}

BEGIN_SHADER_PARAMETER_STRUCT(FCopyBufferParameters, )
	RDG_BUFFER_ACCESS(Buffer, ERHIAccess::CopyDest)
END_SHADER_PARAMETER_STRUCT()

const void* GetInitialData(FRDGBuilder& GraphBuilder, const void* InitialData, uint64 InitialDataSize, ERDGInitialDataFlags InitialDataFlags)
{
	if ((InitialDataFlags & ERDGInitialDataFlags::NoCopy) != ERDGInitialDataFlags::NoCopy)
	{
		// Allocates memory for the lifetime of the pass, since execution is deferred.
		void* InitialDataCopy = GraphBuilder.Alloc(InitialDataSize, 16);
		FMemory::Memcpy(InitialDataCopy, InitialData, InitialDataSize);
		return InitialDataCopy;
	}

	return InitialData;
}

FRDGBufferRef CreateStructuredBuffer(
	FRDGBuilder& GraphBuilder,
	const TCHAR* Name,
	uint32 BytesPerElement,
	uint32 NumElements,
	const void* InitialData,
	uint64 InitialDataSize,
	ERDGInitialDataFlags InitialDataFlags)
{
	const void* SourcePtr = GetInitialData(GraphBuilder, InitialData, InitialDataSize, InitialDataFlags);

	FRDGBufferRef Buffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(BytesPerElement, NumElements), Name);

	FCopyBufferParameters* PassParameters = GraphBuilder.AllocParameters<FCopyBufferParameters>();
	PassParameters->Buffer = Buffer;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("StructuredBufferUpload(%s)", Buffer->Name),
		PassParameters,
		ERDGPassFlags::Copy,
		[Buffer, SourcePtr, InitialDataSize](FRHICommandListImmediate& RHICmdList)
	{
		FRHIStructuredBuffer* StructuredBuffer = Buffer->GetRHIStructuredBuffer();
		void* DestPtr = RHICmdList.LockStructuredBuffer(StructuredBuffer, 0, InitialDataSize, RLM_WriteOnly);
		FMemory::Memcpy(DestPtr, SourcePtr, InitialDataSize);
		RHICmdList.UnlockStructuredBuffer(StructuredBuffer);
	});

	return Buffer;
}

FRDGBufferRef CreateVertexBuffer(
	FRDGBuilder& GraphBuilder,
	const TCHAR* Name,
	const FRDGBufferDesc& Desc,
	const void* InitialData,
	uint64 InitialDataSize,
	ERDGInitialDataFlags InitialDataFlags)
{
	checkf(Name, TEXT("Buffer must have a name."));
	checkf(Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::VertexBuffer, TEXT("CreateVertexBuffer called with an FRDGBufferDesc underlying type that is not 'VertexBuffer'. Buffer: %s"), Name);

	const void* SourcePtr = GetInitialData(GraphBuilder, InitialData, InitialDataSize, InitialDataFlags);

	FRDGBufferRef Buffer = GraphBuilder.CreateBuffer(Desc, Name);

	FCopyBufferParameters* PassParameters = GraphBuilder.AllocParameters<FCopyBufferParameters>();
	PassParameters->Buffer = Buffer;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("VertexBufferUpload(%s)", Buffer->Name),
		PassParameters,
		ERDGPassFlags::Copy,
		[Buffer, SourcePtr, InitialDataSize](FRHICommandListImmediate& RHICmdList)
	{
		FRHIVertexBuffer* VertexBuffer = Buffer->GetRHIVertexBuffer();
		void* DestPtr = RHICmdList.LockVertexBuffer(VertexBuffer, 0, InitialDataSize, RLM_WriteOnly);
		FMemory::Memcpy(DestPtr, SourcePtr, InitialDataSize);
		RHICmdList.UnlockVertexBuffer(VertexBuffer);
	});

	return Buffer;
}

BEGIN_SHADER_PARAMETER_STRUCT(FTextureAccessDynamicPassParameters, )
	RDG_TEXTURE_ACCESS_DYNAMIC(Texture)
END_SHADER_PARAMETER_STRUCT()

// This is a 4.26 hack to get async compute SSAO to pass validation without multi-pipe transitions.
void AddAsyncComputeSRVTransitionHackPass(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture)
{
	auto* PassParameters = GraphBuilder.AllocParameters<FTextureAccessDynamicPassParameters>();
	PassParameters->Texture = FRDGTextureAccess(Texture, ERHIAccess::SRVMask);
	GraphBuilder.AddPass({}, PassParameters,
		// Use all of the work flags so that any access is valid.
		ERDGPassFlags::Copy |
		ERDGPassFlags::Compute |
		ERDGPassFlags::Raster |
		ERDGPassFlags::SkipRenderPass |
		// We're not writing to anything, so we have to tell the pass not to cull.
		ERDGPassFlags::NeverCull,
		[](FRHICommandList&) {});
}

void ConvertToUntrackedTexture(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef Texture,
	ERHIAccess AccessFinal)
{
	GraphBuilder.SetTextureAccessFinal(Texture, AccessFinal);

	auto* PassParameters = GraphBuilder.AllocParameters<FTextureAccessDynamicPassParameters>();
	PassParameters->Texture = FRDGTextureAccess(Texture, AccessFinal);
	GraphBuilder.AddPass({}, PassParameters,
		// Use all of the work flags so that any access is valid.
		ERDGPassFlags::Copy |
		ERDGPassFlags::Compute |
		ERDGPassFlags::Raster |
		ERDGPassFlags::SkipRenderPass |
		// We're not writing to anything, so we have to tell the pass not to cull.
		ERDGPassFlags::NeverCull,
		[](FRHICommandList&) {});
}

BEGIN_SHADER_PARAMETER_STRUCT(FBufferAccessDynamicPassParameters, )
	RDG_BUFFER_ACCESS_DYNAMIC(Buffer)
END_SHADER_PARAMETER_STRUCT()

void ConvertToUntrackedBuffer(
	FRDGBuilder& GraphBuilder,
	FRDGBufferRef Buffer,
	ERHIAccess AccessFinal)
{
	GraphBuilder.SetBufferAccessFinal(Buffer, AccessFinal);

	auto* PassParameters = GraphBuilder.AllocParameters<FBufferAccessDynamicPassParameters>();
	PassParameters->Buffer = FRDGBufferAccess(Buffer, AccessFinal);
	GraphBuilder.AddPass({}, PassParameters,
		// Use all of the work flags so that any access is valid.
		ERDGPassFlags::Copy |
		ERDGPassFlags::Compute |
		ERDGPassFlags::Raster |
		ERDGPassFlags::SkipRenderPass |
		// We're not writing to anything, so we have to tell the pass not to cull.
		ERDGPassFlags::NeverCull,
		[](FRHICommandList&) {});
}

FRDGTextureRef RegisterExternalOrPassthroughTexture(
	FRDGBuilder* GraphBuilder,
	const TRefCountPtr<IPooledRenderTarget>& PooledRenderTarget,
	ERDGTextureFlags Flags)
{
	check(PooledRenderTarget);
	if (GraphBuilder)
	{
		return GraphBuilder->RegisterExternalTexture(PooledRenderTarget, ERenderTargetTexture::ShaderResource, Flags);
	}
	else
	{
		return FRDGTexture::GetPassthrough(PooledRenderTarget);
	}
}

FRDGWaitForTasksScope::~FRDGWaitForTasksScope()
{
	if (bCondition)
	{
		AddPass(GraphBuilder, [](FRHICommandListImmediate& RHICmdList)
		{
			if (IsRunningRHIInSeparateThread())
			{
				QUICK_SCOPE_CYCLE_COUNTER(STAT_FScopedCommandListWaitForTasks_WaitAsync);
				RHICmdList.ImmediateFlush(EImmediateFlushType::WaitForOutstandingTasksOnly);
			}
			else
			{
				QUICK_SCOPE_CYCLE_COUNTER(STAT_FScopedCommandListWaitForTasks_Flush);
				CSV_SCOPED_TIMING_STAT(RHITFlushes, FRDGWaitForTasksDtor);
				RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
			}
		});
	}
}