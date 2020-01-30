// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsLUT.h"
#include "HairStrandsUtils.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameters.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "SceneRendering.h"
#include "SystemTextures.h"

static int32 GHairLUTIncidentAngleCount = 64;
static int32 GHairLUTRoughnessCount = 64;
static int32 GHairLUTAbsorptionCount = 16;
static int32 GHairLUTSampleCountScale = 1;
static FAutoConsoleVariableRef CVarHairLUTIncidentAngleCount(TEXT("r.HairStrands.HairLUT.IncidentAngleCount"), GHairLUTIncidentAngleCount, TEXT("Change the number of slices of the hair LUT for the incident angle axis"));
static FAutoConsoleVariableRef CVarHairLUTRoughnessCount(TEXT("r.HairStrands.HairLUT.RoughnessCount"), GHairLUTRoughnessCount, TEXT("Change the number of slices of the hair LUT for the roughness axis"));
static FAutoConsoleVariableRef CVarHairLUTAbsorptionCount(TEXT("r.HairStrands.HairLUT.AbsorptionCount"), GHairLUTAbsorptionCount, TEXT("Change the number of slices of the hair LUT for the absorption axis"));
static FAutoConsoleVariableRef CVarHairLUTSampleCount(TEXT("r.HairStrands.HairLUT.SampleCountScale"), GHairLUTSampleCountScale, TEXT("Change the number of sample used for computing the hair LUT. This is a multiplier, default is 1."));

/////////////////////////////////////////////////////////////////////////////////////////

class FHairLUTCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairLUTCS);
	SHADER_USE_PARAMETER_STRUCT(FHairLUTCS, FGlobalShader);

	class FLUTType : SHADER_PERMUTATION_INT("PERMUTATION_LUT_TYPE", HairLUTTypeCount);
	using FPermutationDomain = TShaderPermutationDomain<FLUTType>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, AbsorptionCount)
		SHADER_PARAMETER(uint32, RoughnessCount)
		SHADER_PARAMETER(uint32, ThetaCount)
		SHADER_PARAMETER(uint32, SampleCountScale)
		SHADER_PARAMETER(FIntVector, OutputResolution)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutputColor)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FHairLUTCS, "/Engine/Private/HairStrands/HairStrandsLUT.usf", "MainCS", SF_Compute);

static FRDGTextureRef AddHairLUTPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FHairLUTType LUTType)
{
	const FIntVector OutputResolution(GHairLUTIncidentAngleCount, GHairLUTRoughnessCount, GHairLUTAbsorptionCount);

	FRDGTextureDesc OutputDesc;
	OutputDesc.Extent.X = OutputResolution.X;
	OutputDesc.Extent.Y = OutputResolution.Y;
	OutputDesc.Depth = OutputResolution.Z;
	OutputDesc.Format = PF_FloatRGBA;
	OutputDesc.NumMips = 1;
	OutputDesc.Flags = TexCreate_ShaderResource;
	OutputDesc.TargetableFlags = TexCreate_UAV | TexCreate_ShaderResource;
	FRDGTextureRef HairLUTTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("HairLUT"));

	FHairLUTCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairLUTCS::FParameters>();
	Parameters->OutputColor = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(HairLUTTexture, 0));
	Parameters->ThetaCount = OutputResolution.X;
	Parameters->RoughnessCount = OutputResolution.Y;
	Parameters->AbsorptionCount = OutputResolution.Z;
	Parameters->SampleCountScale = GHairLUTSampleCountScale;
	Parameters->OutputResolution = OutputResolution;

	FHairLUTCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairLUTCS::FLUTType>(LUTType);

	TShaderMapRef<FHairLUTCS> ComputeShader(View.ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsLUT"),
		*ComputeShader,
		Parameters,
		FComputeShaderUtils::GetGroupCount(OutputResolution, FIntVector(FComputeShaderUtils::kGolden2DGroupSize)));

	return HairLUTTexture;
}

FHairLUT GetHairLUT(FRHICommandListImmediate& RHICmdList, const FViewInfo& View)
{
	// Lazy LUT generation
	const bool bNeedGenerate = 
		GSystemTextures.HairLUT0.GetReference() == nullptr || GSystemTextures.HairLUT1.GetReference() == nullptr ||
		GSystemTextures.HairLUT0.GetReference()->GetRenderTargetItem().ShaderResourceTexture->GetSizeXYZ() != FIntVector(GHairLUTIncidentAngleCount, GHairLUTRoughnessCount, GHairLUTAbsorptionCount);
	if (bNeedGenerate)
	{
		FRDGBuilder GraphBuilder(RHICmdList);

		FRDGTextureRef HairDualScatteringLUTTexture = AddHairLUTPass(GraphBuilder, View, HairLUTType_DualScattering);
		GraphBuilder.QueueTextureExtraction(HairDualScatteringLUTTexture, &GSystemTextures.HairLUT0);

		FRDGTextureRef HairMeanEnergyLUTTexture		= AddHairLUTPass(GraphBuilder, View, HairLUTType_MeanEnergy);		
		GraphBuilder.QueueTextureExtraction(HairMeanEnergyLUTTexture, &GSystemTextures.HairLUT1);

		GraphBuilder.Execute();
	}
	FHairLUT HairLUTData;
	HairLUTData.Textures[HairLUTType_DualScattering] = GSystemTextures.HairLUT0;
	HairLUTData.Textures[HairLUTType_MeanEnergy] = GSystemTextures.HairLUT1;

	return HairLUTData;
}
