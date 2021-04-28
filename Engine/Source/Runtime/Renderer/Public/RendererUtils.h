// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraph.h"
#include "GlobalShader.h"

class RENDERER_API FRenderTargetWriteMask
{
public:
	static void Decode(
		FRHICommandListImmediate& RHICmdList,
		FGlobalShaderMap* ShaderMap,
		TArrayView<IPooledRenderTarget* const> InRenderTargets,
		TRefCountPtr<IPooledRenderTarget>& OutRTWriteMask,
		ETextureCreateFlags RTWriteMaskFastVRamConfig,
		const TCHAR* RTWriteMaskDebugName);

	static void Decode(
		FRDGBuilder& GraphBuilder,
		FGlobalShaderMap* ShaderMap,
		TArrayView<FRDGTextureRef const> InRenderTargets,
		FRDGTextureRef& OutRTWriteMask,
		ETextureCreateFlags RTWriteMaskFastVRamConfig,
		const TCHAR* RTWriteMaskDebugName);
};

namespace RendererUtils
{
	/*
	* Shaders used to do gaussian blur
	*/
	class FScreenRectangleVS : public FGlobalShader
	{
		DECLARE_SHADER_TYPE(FScreenRectangleVS, Global);
	public:
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return true;
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		}

		FScreenRectangleVS() {}
		FScreenRectangleVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
			FGlobalShader(Initializer)
		{}
	};

	class FGaussianBlurPS : public FGlobalShader
	{
		DECLARE_INLINE_TYPE_LAYOUT(FGaussianBlurPS, NonVirtual);
	public:
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return true;
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		}

		FGaussianBlurPS() {}
		FGaussianBlurPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
			FGlobalShader(Initializer)
		{
			SourceTexture.Bind(Initializer.ParameterMap, TEXT("SourceTexture"));
			SourceTextureSampler.Bind(Initializer.ParameterMap, TEXT("SourceTextureSampler"));
			BufferSizeAndInvSize.Bind(Initializer.ParameterMap, TEXT("BufferSizeAndInvSize"));
		}

		void SetParameters(FRHICommandList& RHICmdList, FRHITexture* InSourceTexture, const FVector4& InBufferSizeAndInvSize)
		{
			FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();
			FRHISamplerState* SamplerState = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

			SetTextureParameter(RHICmdList, ShaderRHI, SourceTexture, InSourceTexture);
			SetSamplerParameter(RHICmdList, ShaderRHI, SourceTextureSampler, SamplerState);

			SetShaderValue(RHICmdList, ShaderRHI, BufferSizeAndInvSize, InBufferSizeAndInvSize);
		}

	private:
		LAYOUT_FIELD(FShaderResourceParameter, SourceTexture);
		LAYOUT_FIELD(FShaderResourceParameter, SourceTextureSampler);
		LAYOUT_FIELD(FShaderParameter, BufferSizeAndInvSize);
	};

	class FHorizontalBlurPS : public FGaussianBlurPS
	{
		DECLARE_SHADER_TYPE(FHorizontalBlurPS, Global);
	public:
		FHorizontalBlurPS() = default;
		FHorizontalBlurPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
			: FGaussianBlurPS(Initializer)
		{}
	};

	class FVerticalBlurPS : public FGaussianBlurPS
	{
		DECLARE_SHADER_TYPE(FVerticalBlurPS, Global);
	public:
		FVerticalBlurPS() = default;
		FVerticalBlurPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
			: FGaussianBlurPS(Initializer)
		{}
	};

	// Compute Shader
	class FGaussianBlurCS : public FGlobalShader
	{
	public:
		FGaussianBlurCS() = default;
		FGaussianBlurCS(const CompiledShaderInitializerType& Initializer)
			: FGlobalShader(Initializer)
		{}

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return RHISupportsComputeShaders(Parameters.Platform);
		}

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_TEXTURE(Texture2D, SourceTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, SourceTextureSampler)
			SHADER_PARAMETER(FVector4, BufferSizeAndInvSize)
			SHADER_PARAMETER_UAV(RWTexture2D<float4>, RWOutputTexture)
		END_SHADER_PARAMETER_STRUCT()
	};

	class FHorizontalBlurCS : public FGaussianBlurCS
	{
	public:
		static const uint32 ThreadGroupSizeX = 64;
		static const uint32 ThreadGroupSizeY = 1;

		// The number of texels on each axis processed by a single thread group.
		static const FIntPoint TexelsPerThreadGroup;

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

			OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), ThreadGroupSizeX);
			OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), ThreadGroupSizeY);
		}

		DECLARE_GLOBAL_SHADER(FHorizontalBlurCS);
		SHADER_USE_PARAMETER_STRUCT(FHorizontalBlurCS, FGaussianBlurCS);
	};

	class FVerticalBlurCS : public FGaussianBlurCS
	{
	public:
		static const uint32 ThreadGroupSizeX = 1;
		static const uint32 ThreadGroupSizeY = 64;

		// The number of texels on each axis processed by a single thread group.
		static const FIntPoint TexelsPerThreadGroup;

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

			OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), ThreadGroupSizeX);
			OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), ThreadGroupSizeY);
		}

		DECLARE_GLOBAL_SHADER(FVerticalBlurCS);
		SHADER_USE_PARAMETER_STRUCT(FVerticalBlurCS, FGaussianBlurCS);
	};

	extern void AddGaussianBlurFilter_InternalPS(
		FRHICommandListImmediate& RHICmdList,
		const FViewInfo& View,
		FRHITexture* SourceTexture,
		TRefCountPtr<IPooledRenderTarget>& RenderTarget,
		TShaderRef<FGaussianBlurPS> PixelShader);

	extern void AddGaussianBlurFilter_InternalCS(
		FRHICommandListImmediate& RHICmdList,
		const FViewInfo& View,
		FRHITexture* SourceTexture,
		TRefCountPtr<IPooledRenderTarget>& RenderTarget,
		TShaderRef<FGaussianBlurCS> ComputeShader,
		FIntPoint TexelsPerThreadGroup);

	extern void AddGaussianBlurFilter(
		FRHICommandListImmediate& RHICmdList,
		const FViewInfo& View,
		FRHITexture* SourceTexture,
		TRefCountPtr<IPooledRenderTarget>& HorizontalRenderTarget,
		TRefCountPtr<IPooledRenderTarget>& VerticalRenderTarget,
		bool bIsComputePass = false);
}
