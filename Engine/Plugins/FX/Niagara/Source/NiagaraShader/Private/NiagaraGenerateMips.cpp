// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraGenerateMips.h"
#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"

class FNiagaraGenerateMipsCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FNiagaraGenerateMipsCS)
	SHADER_USE_PARAMETER_STRUCT(FNiagaraGenerateMipsCS, FGlobalShader)

	class FGenMipsSRGB : SHADER_PERMUTATION_BOOL("GENMIPS_SRGB");
	class FGaussianBlur : SHADER_PERMUTATION_BOOL("GENMIPS_GAUSSIAN");
	using FPermutationDomain = TShaderPermutationDomain<FGenMipsSRGB, FGaussianBlur>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector2f, SrcTexelSize)
		SHADER_PARAMETER(FVector2f, DstTexelSize)
		SHADER_PARAMETER(int32, KernelHWidth)
		SHADER_PARAMETER(FIntPoint, MipOutSize)
		SHADER_PARAMETER_UAV(RWTexture2D<float4>, MipOutUAV)
		SHADER_PARAMETER_SRV(Texture2D, MipInSRV)
		SHADER_PARAMETER_SAMPLER(SamplerState, MipInSampler)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsComputeShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE"), FComputeShaderUtils::kGolden2DGroupSize);
	}
};

IMPLEMENT_GLOBAL_SHADER(FNiagaraGenerateMipsCS, "/Plugin/FX/Niagara/Private/NiagaraGenerateMips.usf", "MainCS", SF_Compute);

void NiagaraGenerateMips::GenerateMips(FRHICommandList& RHICmdList, FRHITexture2D* TextureRHI, ENiagaraMipMapGenerationType GenType)
{
	SCOPED_DRAW_EVENT(RHICmdList, NiagaraGenerateMips);

	// Select compute shader variant (normal vs. sRGB etc.)
	bool bMipsSRGB = EnumHasAnyFlags(TextureRHI->GetFlags(), TexCreate_SRGB);
#if PLATFORM_ANDROID
	if (IsVulkanPlatform(GMaxRHIShaderPlatform))
	{
		// Vulkan Android seems to skip sRGB->Lin conversion when sampling texture in compute
		bMipsSRGB = false;
	}
#endif

	const bool bBlurMips = GenType >= ENiagaraMipMapGenerationType::Blur1;

	FNiagaraGenerateMipsCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FNiagaraGenerateMipsCS::FGenMipsSRGB>(bMipsSRGB);
	PermutationVector.Set<FNiagaraGenerateMipsCS::FGaussianBlur>(bBlurMips);
	TShaderMapRef<FNiagaraGenerateMipsCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
	FRHIComputeShader* ShaderRHI = ComputeShader.GetComputeShader();

	const int32 KernelHWidth = bBlurMips ? (int32(GenType) + 1 - int32(ENiagaraMipMapGenerationType::Blur1)) : 1;

	const int32 NumMips = TextureRHI->GetNumMips();
	for ( int32 iDstMip=1; iDstMip < NumMips; ++iDstMip)
	{
		const int32 iSrcMip = iDstMip - 1;
		const FIntPoint SrcMipSize(FMath::Max<int32>(TextureRHI->GetSizeX() >> iSrcMip, 1), FMath::Max<int32>(TextureRHI->GetSizeY() >> iSrcMip, 1));
		const FIntPoint DstMipSize(FMath::Max<int32>(TextureRHI->GetSizeX() >> iDstMip, 1), FMath::Max<int32>(TextureRHI->GetSizeY() >> iDstMip, 1));

		FShaderResourceViewRHIRef MipInSRV = RHICreateShaderResourceView(TextureRHI, iSrcMip);
		FUnorderedAccessViewRHIRef MipOutUAV = RHICreateUnorderedAccessView(TextureRHI, iDstMip);

		FNiagaraGenerateMipsCS::FParameters PassParameters;
		PassParameters.SrcTexelSize	= FVector2f(1.0f / float(SrcMipSize.X), 1.0f / float(SrcMipSize.Y));
		PassParameters.DstTexelSize = FVector2f(1.0f / float(DstMipSize.X), 1.0f / float(DstMipSize.Y));
		PassParameters.KernelHWidth = KernelHWidth;
		PassParameters.MipOutSize	= DstMipSize;
		PassParameters.MipInSRV		= MipInSRV;
		PassParameters.MipInSampler	= GenType == ENiagaraMipMapGenerationType::Unfiltered ? TStaticSamplerState<SF_Point>::GetRHI() : TStaticSamplerState<SF_Bilinear>::GetRHI();
		PassParameters.MipOutUAV	= MipOutUAV;

		const FIntVector NumThreadGroups = FComputeShaderUtils::GetGroupCount(DstMipSize, FComputeShaderUtils::kGolden2DGroupSize);

		RHICmdList.Transition(FRHITransitionInfo(MipOutUAV, ERHIAccess::SRVMask, ERHIAccess::UAVCompute));

		RHICmdList.SetComputeShader(ShaderRHI);
		SetShaderParameters(RHICmdList, ComputeShader, ShaderRHI, PassParameters);
		RHICmdList.DispatchComputeShader(NumThreadGroups.X, NumThreadGroups.Y, NumThreadGroups.Z);
		UnsetShaderUAVs(RHICmdList, ComputeShader, ShaderRHI);

		RHICmdList.Transition(FRHITransitionInfo(MipOutUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
	}
}
