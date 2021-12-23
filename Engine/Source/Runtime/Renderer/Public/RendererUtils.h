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
	public:
		FGaussianBlurPS() = default;
		FGaussianBlurPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
			FGlobalShader(Initializer)
		{}

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return true;
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		}

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, SourceTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, SourceTextureSampler)
			SHADER_PARAMETER(FVector4, BufferSizeAndInvSize)
			RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()
	};

	class FHorizontalBlurPS : public FGaussianBlurPS
	{
	public:
		DECLARE_GLOBAL_SHADER(FHorizontalBlurPS);
		SHADER_USE_PARAMETER_STRUCT(FHorizontalBlurPS, FGaussianBlurPS);
	};

	class FVerticalBlurPS : public FGaussianBlurPS
	{
	public:
		DECLARE_GLOBAL_SHADER(FVerticalBlurPS);
		SHADER_USE_PARAMETER_STRUCT(FVerticalBlurPS, FGaussianBlurPS);
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
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, SourceTexture)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWOutputTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, SourceTextureSampler)
			SHADER_PARAMETER(FVector4, BufferSizeAndInvSize)			
		END_SHADER_PARAMETER_STRUCT()
	};

	class FHorizontalBlurCS : public FGaussianBlurCS
	{
	public:
		DECLARE_GLOBAL_SHADER(FHorizontalBlurCS);
		SHADER_USE_PARAMETER_STRUCT(FHorizontalBlurCS, FGaussianBlurCS);

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
	};

	class FVerticalBlurCS : public FGaussianBlurCS
	{
	public:
		DECLARE_GLOBAL_SHADER(FVerticalBlurCS);
		SHADER_USE_PARAMETER_STRUCT(FVerticalBlurCS, FGaussianBlurCS);

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
	};

	extern void AddGaussianBlurFilter_InternalPS(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FRDGTextureSRVRef InTexture,
		FRDGTextureRef OutTexture,
		TShaderRef<FGaussianBlurPS> PixelShader);

	extern void AddGaussianBlurFilter_InternalCS(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FRDGTextureSRVRef InTexture,
		FRDGTextureUAVRef OutTexture,
		TShaderRef<FGaussianBlurCS> ComputeShader,
		FIntPoint TexelsPerThreadGroup);

	extern void AddGaussianBlurFilter(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FRDGTextureRef SourceTexture,
		FRDGTextureRef HorizontalBlurTexture,
		FRDGTextureRef VerticalBlurTexture,
		bool bUseComputeShader = false);

}
