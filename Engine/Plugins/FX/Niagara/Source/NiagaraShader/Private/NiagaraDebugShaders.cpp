// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDebugShaders.h"

int GNiagaraGpuComputeDebug_ShowNaNInf = 1;
static FAutoConsoleVariableRef CVarNiagaraGpuComputeDebug_ShowNaNInf(
	TEXT("fx.Niagara.GpuComputeDebug.ShowNaNInf"),
	GNiagaraGpuComputeDebug_ShowNaNInf,
	TEXT("When enabled will show NaNs as flashing colors."),
	ECVF_Default
);

int GNiagaraGpuComputeDebug_FourComponentMode = 0;
static FAutoConsoleVariableRef CVarNiagaraGpuComputeDebug_FourComponentMode(
	TEXT("fx.Niagara.GpuComputeDebug.FourComponentMode"),
	GNiagaraGpuComputeDebug_FourComponentMode,
	TEXT("Adjust how we visualize four component types\n")
	TEXT("0 = Visualize RGB (defaut)\n")
	TEXT("1 = Visualize A\n"),
	ECVF_Default
);

FNiagaraVisualizeTexture2DPS::FNiagaraVisualizeTexture2DPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	: FGlobalShader(Initializer)
{
	Texture2DParam.Bind(Initializer.ParameterMap, TEXT("Texture2DObject"), SPF_Mandatory);
	TextureSamplerParam.Bind(Initializer.ParameterMap, TEXT("TextureSampler"));
	NumTextureAttributesParam.Bind(Initializer.ParameterMap, TEXT("NumTextureAttributes"));
	NumAttributesToVisualizeParam.Bind(Initializer.ParameterMap, TEXT("NumAttributesToVisualize"));
	AttributesToVisualizeParam.Bind(Initializer.ParameterMap, TEXT("AttributesToVisualize"));
	DebugParamsParam.Bind(Initializer.ParameterMap, TEXT("DebugParams"));
}

void FNiagaraVisualizeTexture2DPS::SetParameters(FRHICommandList& RHICmdList, FIntVector4 AttributesToVisualize, FRHITexture* Texture, FIntPoint NumTextureAttributes, uint32 TickCounter)
{
	uint32 DebugParamsValue[2];
	DebugParamsValue[0] = GNiagaraGpuComputeDebug_ShowNaNInf != 0 ? 1 : 0;
	DebugParamsValue[1] = TickCounter;

	int32 NumAttributesToVisualizeValue = 0;
	for (NumAttributesToVisualizeValue = 0; NumAttributesToVisualizeValue < 4; ++NumAttributesToVisualizeValue)
	{
		if (AttributesToVisualize[NumAttributesToVisualizeValue] == INDEX_NONE)
		{
			break;
		}
	}

	if (NumAttributesToVisualizeValue == 4)
	{
		switch (GNiagaraGpuComputeDebug_FourComponentMode)
		{
			// RGB only
		default:
		case 0:
			AttributesToVisualize[3] = INDEX_NONE;
			NumAttributesToVisualizeValue = 3;
			break;

			// Alpha only
			case 1:
				AttributesToVisualize[0] = AttributesToVisualize[3];
				AttributesToVisualize[1] = INDEX_NONE;
				AttributesToVisualize[2] = INDEX_NONE;
				AttributesToVisualize[3] = INDEX_NONE;
				NumAttributesToVisualizeValue = 1;
				break;
		}
	}

	FRHIPixelShader* PixelShader = RHICmdList.GetBoundPixelShader();
	SetTextureParameter(RHICmdList, PixelShader, Texture2DParam, TextureSamplerParam, TStaticSamplerState<SF_Point>::GetRHI(), Texture);
	SetShaderValue(RHICmdList, PixelShader, NumTextureAttributesParam, NumTextureAttributes);
	SetShaderValue(RHICmdList, PixelShader, NumAttributesToVisualizeParam, NumAttributesToVisualizeValue);
	SetShaderValue(RHICmdList, PixelShader, AttributesToVisualizeParam, AttributesToVisualize);
	SetShaderValue(RHICmdList, PixelShader, DebugParamsParam, DebugParamsValue);
}

IMPLEMENT_GLOBAL_SHADER(FNiagaraVisualizeTexture2DPS, "/Plugin/FX/Niagara/Private/NiagaraVisualizeTexture.usf", "Main2D", SF_Pixel);
