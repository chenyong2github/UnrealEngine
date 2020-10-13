// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDebugShaders.h"

#include "RHI.h"
#include "ScreenRendering.h"
#include "CommonRenderResources.h"
#include "Modules/ModuleManager.h"

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

//////////////////////////////////////////////////////////////////////////

class NIAGARASHADER_API FNiagaraVisualizeTexturePS : public FGlobalShader
{
public:
	DECLARE_SHADER_TYPE(FNiagaraVisualizeTexturePS, Global);
	SHADER_USE_PARAMETER_STRUCT(FNiagaraVisualizeTexturePS, FGlobalShader);

	class FTextureType : SHADER_PERMUTATION_INT("TEXTURE_TYPE", 3);

	using FPermutationDomain = TShaderPermutationDomain<FTextureType>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		return true;
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint,		NumTextureAttributes)
		SHADER_PARAMETER(int32,			NumAttributesToVisualize)
		SHADER_PARAMETER(FIntVector4,	AttributesToVisualize)
		SHADER_PARAMETER(FIntVector,	TextureDimensions)
		SHADER_PARAMETER(uint32,		DebugFlags)
		SHADER_PARAMETER(uint32,		TickCounter)
		SHADER_PARAMETER(uint32,		TextureSlice)

		SHADER_PARAMETER_TEXTURE(Texture2D, Texture2DObject)
		SHADER_PARAMETER_TEXTURE(Texture2DArray, Texture2DArrayObject)
		SHADER_PARAMETER_TEXTURE(Texture3D, Texture3DObject)
		SHADER_PARAMETER_SAMPLER(SamplerState, TextureSampler)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FNiagaraVisualizeTexturePS, "/Plugin/FX/Niagara/Private/NiagaraVisualizeTexture.usf", "Main", SF_Pixel);

//////////////////////////////////////////////////////////////////////////

void NiagaraDebugShaders::VisualizeTexture(
	FRHICommandList& RHICmdList,
	const FIntPoint& Location, const int32& DisplayHeight, const FIntPoint& RenderTargetSize,
	const FIntVector4& InAttributesToVisualize, FRHITexture* Texture, const FIntPoint& NumTextureAttributes, uint32 TickCounter
)
{
	FIntVector TextureSize = Texture->GetSizeXYZ();
	if (NumTextureAttributes.X > 0)
	{
		check(NumTextureAttributes.Y > 0);
		TextureSize.X /= NumTextureAttributes.X;
		TextureSize.Y /= NumTextureAttributes.Y;
	}

	// Determine number of attributes to visualize
	int32 NumAttributesToVisualizeValue = 0;
	FIntVector4 AttributesToVisualize = InAttributesToVisualize;
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

	// Setup parameters
	FNiagaraVisualizeTexturePS::FParameters ShaderParameters;
	{
		ShaderParameters.NumTextureAttributes = NumTextureAttributes;
		ShaderParameters.NumAttributesToVisualize = NumAttributesToVisualizeValue;
		ShaderParameters.AttributesToVisualize = AttributesToVisualize;
		ShaderParameters.TextureDimensions = TextureSize;
		ShaderParameters.DebugFlags = GNiagaraGpuComputeDebug_ShowNaNInf != 0 ? 1 : 0;
		ShaderParameters.TickCounter = TickCounter;
		ShaderParameters.TextureSlice = 0;
		ShaderParameters.Texture2DObject = Texture->GetTexture2D();
		ShaderParameters.Texture2DArrayObject = Texture->GetTexture2DArray();
		ShaderParameters.Texture3DObject = Texture->GetTexture3D();
		ShaderParameters.TextureSampler = TStaticSamplerState<SF_Point>::GetRHI();
	}

	// Set Shaders & State
	auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FScreenVS> VertexShader(ShaderMap);

	FNiagaraVisualizeTexturePS::FPermutationDomain PermutationVector;
	if (ShaderParameters.Texture2DObject != nullptr)
	{
		PermutationVector.Set<FNiagaraVisualizeTexturePS::FTextureType>(0);
	}
	else if (ShaderParameters.Texture2DArrayObject != nullptr)
	{
		PermutationVector.Set<FNiagaraVisualizeTexturePS::FTextureType>(1);
	}
	else if (ShaderParameters.Texture3DObject != nullptr)
	{
		PermutationVector.Set<FNiagaraVisualizeTexturePS::FTextureType>(2);
	}
	else
	{
		// Should never get here, but let's not crash
		return;
	}

	TShaderMapRef<FNiagaraVisualizeTexturePS> PixelShader(ShaderMap, PermutationVector);

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;
	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

	// Render
	IRendererModule* RendererModule = &FModuleManager::GetModuleChecked<IRendererModule>("Renderer");

	FIntPoint DisplaySize(TextureSize.X, TextureSize.Y);
	if (DisplayHeight > 0)
	{
		DisplaySize.Y = FMath::Min(TextureSize.Y, DisplayHeight);
		DisplaySize.X = int32(float(TextureSize.X) * (float(DisplaySize.Y) / float(TextureSize.Y)));
	}

	// Display slices
	const int32 AvailableWidth = RenderTargetSize.X - Location.X;
	const int32 SlicesWidth = FMath::Clamp(FMath::DivideAndRoundDown(AvailableWidth, DisplaySize.X + 1), 1, TextureSize.Z);
	for (int32 iSlice = 0; iSlice < SlicesWidth; ++iSlice)
	{
		ShaderParameters.TextureSlice = iSlice;

		SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), ShaderParameters);

		const float OffsetX = float(iSlice) * DisplaySize.X + 1;

		RendererModule->DrawRectangle(
			RHICmdList,
			Location.X + OffsetX, Location.Y,					// Dest X, Y
			DisplaySize.X, DisplaySize.Y,						// Dest Width, Height
			0.0f, 0.0f,											// Source U, V
			TextureSize.X, TextureSize.Y,						// Source USize, VSize
			FIntPoint(RenderTargetSize.X, RenderTargetSize.Y),	// TargetSize
			FIntPoint(TextureSize.X, TextureSize.Y),			// Source texture size
			VertexShader,
			EDRF_Default);
	}
}
