// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDebugShaders.h"

#include "RHI.h"
#include "ScreenRendering.h"
#include "CommonRenderResources.h"
#include "Modules/ModuleManager.h"

#include "RenderGraphBuilder.h"
#include "Runtime/Renderer/Private/ScreenPass.h"

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

		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FNiagaraVisualizeTexturePS, "/Plugin/FX/Niagara/Private/NiagaraVisualizeTexture.usf", "Main", SF_Pixel);

//////////////////////////////////////////////////////////////////////////

void NiagaraDebugShaders::VisualizeTexture(
	class FRDGBuilder& GraphBuilder, const FViewInfo& View, const FScreenPassRenderTarget& Output,
	const FIntPoint& Location, const int32& DisplayHeight,
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

	FRHITexture2D* Texture2D = Texture->GetTexture2D();
	FRHITexture2DArray* Texture2DArray = Texture->GetTexture2DArray();
	FRHITexture3D* Texture3D = Texture->GetTexture3D();

	// Set Shaders & State
	FNiagaraVisualizeTexturePS::FPermutationDomain PermutationVector;
	if (Texture2D != nullptr)
	{
		PermutationVector.Set<FNiagaraVisualizeTexturePS::FTextureType>(0);
	}
	else if (Texture2DArray != nullptr)
	{
		PermutationVector.Set<FNiagaraVisualizeTexturePS::FTextureType>(1);
	}
	else if (Texture3D != nullptr)
	{
		PermutationVector.Set<FNiagaraVisualizeTexturePS::FTextureType>(2);
	}
	else
	{
		// Should never get here, but let's not crash
		return;
	}

	auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
	TShaderMapRef<FNiagaraVisualizeTexturePS> PixelShader(ShaderMap, PermutationVector);

	FIntPoint DisplaySize(TextureSize.X, TextureSize.Y);
	if (DisplayHeight > 0)
	{
		DisplaySize.Y = FMath::Min(TextureSize.Y, DisplayHeight);
		DisplaySize.X = int32(float(TextureSize.X) * (float(DisplaySize.Y) / float(TextureSize.Y)));
	}

	// Display slices
	const FIntPoint RenderTargetSize = View.Family->RenderTarget->GetSizeXY();

	const int32 AvailableWidth = RenderTargetSize.X - Location.X;
	const int32 SlicesWidth = FMath::Clamp(FMath::DivideAndRoundDown(AvailableWidth, DisplaySize.X + 1), 1, TextureSize.Z);

	for (int32 iSlice = 0; iSlice < SlicesWidth; ++iSlice)
	{
		FNiagaraVisualizeTexturePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FNiagaraVisualizeTexturePS::FParameters>();
		{
			PassParameters->NumTextureAttributes = NumTextureAttributes;
			PassParameters->NumAttributesToVisualize = NumAttributesToVisualizeValue;
			PassParameters->AttributesToVisualize = AttributesToVisualize;
			PassParameters->TextureDimensions = TextureSize;
			PassParameters->DebugFlags = GNiagaraGpuComputeDebug_ShowNaNInf != 0 ? 1 : 0;
			PassParameters->TickCounter = TickCounter;
			PassParameters->TextureSlice = iSlice;
			PassParameters->Texture2DObject = Texture2D;
			PassParameters->Texture2DArrayObject = Texture2DArray;
			PassParameters->Texture3DObject = Texture3D;
			PassParameters->TextureSampler = TStaticSamplerState<SF_Point>::GetRHI();
			PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();
		}

		const float OffsetX = float(iSlice) * DisplaySize.X + 1;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("NiagaraVisualizeTexture"),
			PassParameters,
			ERDGPassFlags::Raster,
			[VertexShader, PixelShader, PassParameters, Location=FIntPoint(Location.X+OffsetX, Location.Y), DisplaySize, TextureSize, RenderTargetSize](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.SetViewport(0, 0, 0.0f, RenderTargetSize.X, RenderTargetSize.Y, 1.0f);

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

				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);

				IRendererModule* RendererModule = &FModuleManager::GetModuleChecked<IRendererModule>("Renderer");
				RendererModule->DrawRectangle(
					RHICmdList,
					Location.X, Location.Y,						// Dest X, Y
					DisplaySize.X, DisplaySize.Y,				// Dest Width, Height
					0.0f, 0.0f,									// Source U, V
					TextureSize.X, TextureSize.Y,				// Source USize, VSize
					RenderTargetSize,							// TargetSize
					FIntPoint(TextureSize.X, TextureSize.Y),	// Source texture size
					VertexShader,
					EDRF_Default);
			}
		);
	}
}
