// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RenderGraphUtils.h"
#include "ClearQuad.h"
#include "ClearReplacementShaders.h"
#include <initializer_list>

/** Adds a render graph tracked buffer suitable for use as a copy destination. */
#define RDG_BUFFER_COPY_DEST(MemberName) \
	INTERNAL_SHADER_PARAMETER_EXPLICIT(UBMT_RDG_BUFFER_COPY_DEST, TShaderResourceParameterTypeInfo<FRDGBufferRef>, FRDGBufferRef,MemberName,, = nullptr,EShaderPrecisionModifier::Float,TEXT(""),false)

/** Adds a render graph tracked texture suitable for use as a copy destination. */
#define RDG_TEXTURE_COPY_DEST(MemberName) \
	INTERNAL_SHADER_PARAMETER_EXPLICIT(UBMT_RDG_TEXTURE_COPY_DEST, TShaderResourceParameterTypeInfo<FRDGTextureRef>, FRDGTextureRef,MemberName,, = nullptr,EShaderPrecisionModifier::Float,TEXT(""),false)

void ClearUnusedGraphResourcesImpl(
	const FShaderParameterBindings& ShaderBindings,
	const FShaderParametersMetadata* ParametersMetadata,
	void* InoutParameters,
	std::initializer_list< FRDGResourceRef > ExcludeList)
{
	int32 GraphTextureId = 0;
	int32 GraphSRVId = 0;
	int32 GraphUAVId = 0;

	uint8* Base = reinterpret_cast<uint8*>(InoutParameters);

	for (int32 ResourceIndex = 0, Num = ParametersMetadata->GetLayout().Resources.Num(); ResourceIndex < Num; ResourceIndex++)
	{
		EUniformBufferBaseType Type = ParametersMetadata->GetLayout().Resources[ResourceIndex].MemberType;
		uint16 ByteOffset = ParametersMetadata->GetLayout().Resources[ResourceIndex].MemberOffset;

		if (Type == UBMT_RDG_TEXTURE)
		{
			if (GraphTextureId < ShaderBindings.GraphTextures.Num() && ByteOffset == ShaderBindings.GraphTextures[GraphTextureId].ByteOffset)
			{
				GraphTextureId++;
				continue;
			}
		}
		else if (Type == UBMT_RDG_TEXTURE_SRV || Type == UBMT_RDG_BUFFER_SRV)
		{
			if (GraphSRVId < ShaderBindings.GraphSRVs.Num() && ByteOffset == ShaderBindings.GraphSRVs[GraphSRVId].ByteOffset)
			{
				GraphSRVId++;
				continue;
			}
		}
		else if (Type == UBMT_RDG_TEXTURE_UAV || Type == UBMT_RDG_BUFFER_UAV)
		{
			if (GraphUAVId < ShaderBindings.GraphUAVs.Num() && ByteOffset == ShaderBindings.GraphUAVs[GraphUAVId].ByteOffset)
			{
				GraphUAVId++;
				continue;
			}
		}
		else
		{
			continue;
		}

		for( FRDGResourceRef ExcludeResource : ExcludeList )
		{
			auto Resource = *reinterpret_cast<const FRDGResource* const*>(Base + ByteOffset);
			if( Resource == ExcludeResource )
			{
				continue;
			}
		}

		void** ResourcePointerAddress = reinterpret_cast<void**>(Base + ByteOffset);
		*ResourcePointerAddress = nullptr;
	}
}

FRDGTextureRef RegisterExternalTextureWithFallback(
	FRDGBuilder& GraphBuilder,
	const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture,
	const TRefCountPtr<IPooledRenderTarget>& FallbackPooledTexture,
	const TCHAR* ExternalPooledTextureName)
{
	ensureMsgf(FallbackPooledTexture.IsValid(), TEXT("RegisterExternalTextureWithDummyFallback() requires a valid fallback pooled texture."));
	if (ExternalPooledTexture.IsValid())
	{
		return GraphBuilder.RegisterExternalTexture(ExternalPooledTexture, ExternalPooledTextureName);
	}
	else
	{
		return GraphBuilder.RegisterExternalTexture(FallbackPooledTexture);
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FCopyTextureParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(, Input)
	RDG_TEXTURE_COPY_DEST(Output)
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

	GraphBuilder.AddPass(RDG_EVENT_NAME("CopyTexture"), Parameters, ERDGPassFlags::Copy,
		[InputTexture, OutputTexture, CopyInfo](FRHICommandList& RHICmdList)
	{
        // Manually mark as used since we aren't invoking any shaders.
		InputTexture->MarkResourceAsUsed();
		OutputTexture->MarkResourceAsUsed();

		RHICmdList.CopyTexture(InputTexture->GetRHI(), OutputTexture->GetRHI(), CopyInfo);
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
		RDG_EVENT_NAME("ClearBuffer(%s)", BufferUAV->GetParent()->Name),
		Parameters,
		ERDGPassFlags::Compute,
		[&Parameters, BufferUAV, Value](FRHICommandList& RHICmdList)
	{
		// This might be called if using ClearTinyUAV.
		BufferUAV->MarkResourceAsUsed();

		::ClearUAV(RHICmdList, BufferUAV->GetRHI(), BufferUAV->GetParent()->Desc.GetTotalNumBytes(), Value);
	});
}

BEGIN_SHADER_PARAMETER_STRUCT(FClearTextureUAVParameters, )
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, TextureUAV)
END_SHADER_PARAMETER_STRUCT()

template <typename T>
void AddClearUAVPass_T(FRDGBuilder& GraphBuilder, FRDGTextureUAVRef TextureUAV, const T(&ClearValues)[4])
{
	check(TextureUAV);

	FClearTextureUAVParameters* Parameters = GraphBuilder.AllocParameters<FClearTextureUAVParameters>();
	Parameters->TextureUAV = TextureUAV;

	const FRDGTextureDesc& TextureDesc = TextureUAV->GetParent()->Desc;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("ClearTexture(%s) %dx%d", TextureUAV->GetParent()->Name, TextureDesc.Extent.X, TextureDesc.Extent.Y),
		Parameters,
		ERDGPassFlags::Compute,
		[&Parameters, TextureUAV, ClearValues](FRHICommandList& RHICmdList)
	{
		const FRDGTextureDesc& TextureDesc = TextureUAV->GetParent()->Desc;

		FRHIUnorderedAccessView* RHITextureUAV = TextureUAV->GetRHI();

		if (TextureDesc.Is2DTexture())
		{
			if (TextureDesc.IsArray())
			{
				TShaderMapRef<FClearTexture2DArrayReplacementCS<T>> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				FRHIComputeShader* ShaderRHI = ComputeShader->GetComputeShader();
				RHICmdList.SetComputeShader(ShaderRHI);
				ComputeShader->SetParameters(RHICmdList, RHITextureUAV, ClearValues);
				uint32 x = (TextureDesc.Extent.X + 7) / 8;
				uint32 y = (TextureDesc.Extent.Y + 7) / 8;
				uint32 z = TextureDesc.ArraySize;
				RHICmdList.DispatchComputeShader(x, y, z);
				ComputeShader->FinalizeParameters(RHICmdList, RHITextureUAV);
			}
			else
			{
				TShaderMapRef<FClearTexture2DReplacementCS<T>> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				FRHIComputeShader* ShaderRHI = ComputeShader->GetComputeShader();
				RHICmdList.SetComputeShader(ShaderRHI);
				ComputeShader->SetParameters(RHICmdList, RHITextureUAV, ClearValues);
				const uint32 X = (TextureDesc.Extent.X + 7) / 8;
				const uint32 Y = (TextureDesc.Extent.Y + 7) / 8;
				RHICmdList.DispatchComputeShader(X, Y, 1);
				ComputeShader->FinalizeParameters(RHICmdList, RHITextureUAV);
			}
		}
		else if (TextureDesc.IsCubemap())
		{
			TShaderMapRef<FClearTexture2DArrayReplacementCS<T>> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FRHIComputeShader* ShaderRHI = ComputeShader->GetComputeShader();
			RHICmdList.SetComputeShader(ShaderRHI);
			ComputeShader->SetParameters(RHICmdList, RHITextureUAV, ClearValues);
			uint32 x = (TextureDesc.Extent.X + 7) / 8;
			uint32 y = (TextureDesc.Extent.Y + 7) / 8;
			RHICmdList.DispatchComputeShader(x, y, 6);
			ComputeShader->FinalizeParameters(RHICmdList, RHITextureUAV);
		}
		else if (TextureDesc.Is3DTexture())
		{
			TShaderMapRef<FClearVolumeReplacementCS<T>> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FRHIComputeShader* ShaderRHI = ComputeShader->GetComputeShader();
			RHICmdList.SetComputeShader(ShaderRHI);
			ComputeShader->SetParameters(RHICmdList, RHITextureUAV, ClearValues);
			uint32 x = (TextureDesc.Extent.X + 3) / 4;
			uint32 y = (TextureDesc.Extent.Y + 3) / 4;
			uint32 z = (TextureDesc.Depth + 3) / 4;
			RHICmdList.DispatchComputeShader(x, y, z);
			ComputeShader->FinalizeParameters(RHICmdList, RHITextureUAV);
		}
		else
		{
			check(0);
		}
	});
}

void AddClearUAVPass(FRDGBuilder& GraphBuilder, FRDGTextureUAVRef TextureUAV, const float(&ClearValues)[4])
{
	AddClearUAVPass_T(GraphBuilder, TextureUAV, ClearValues);
}

void AddClearUAVPass(FRDGBuilder& GraphBuilder, FRDGTextureUAVRef TextureUAV, const uint32(&ClearValues)[4])
{
	AddClearUAVPass_T(GraphBuilder, TextureUAV, ClearValues);
}

void AddClearUAVPass(FRDGBuilder& GraphBuilder, FRDGTextureUAVRef TextureUAV, const FLinearColor& ClearColor)
{
	AddClearUAVPass_T(GraphBuilder, TextureUAV, reinterpret_cast<const float(&)[4]>(ClearColor));
}

void AddClearRenderTargetPass(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture, const FLinearColor& ClearColor)
{
	check(Texture);

	FRenderTargetParameters* Parameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
	Parameters->RenderTargets[0] = FRenderTargetBinding(Texture, ERenderTargetLoadAction::ENoAction);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("ClearRenderTarget(%s) %dx%d", Texture->Name, Texture->Desc.Extent.X, Texture->Desc.Extent.Y),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, ClearColor](FRHICommandList& RHICmdList)
	{
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