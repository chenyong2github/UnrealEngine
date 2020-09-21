// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraGpuComputeDebug.h"

#include "RHI.h"
#include "ScreenRendering.h"
#include "CanvasTypes.h"
#include "CommonRenderResources.h"
#include "Engine/Font.h"
#include "Modules/ModuleManager.h"

float GNiagaraGpuComputeDebug_MaxTextureHeight = 128.0f;
static FAutoConsoleVariableRef CVarNiagaraGpuComputeDebug_MaxTextureHeight(
	TEXT("fx.Niagara.GpuComputeDebug.MaxTextureHeight"),
	GNiagaraGpuComputeDebug_MaxTextureHeight,
	TEXT("The maximum height we will visualize a texture at, this is to avoid things becoming too large on screen."),
	ECVF_Default
);

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

class FNiagaraVisualizeTexture2DPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FNiagaraVisualizeTexture2DPS, Global);

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	FNiagaraVisualizeTexture2DPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		Texture2DParam.Bind(Initializer.ParameterMap, TEXT("Texture2DObject"), SPF_Mandatory);
		TextureSamplerParam.Bind(Initializer.ParameterMap, TEXT("TextureSampler"));
		NumTextureAttributesParam.Bind(Initializer.ParameterMap, TEXT("NumTextureAttributes"));
		NumAttributesToVisualizeParam.Bind(Initializer.ParameterMap, TEXT("NumAttributesToVisualize"));
		AttributesToVisualizeParam.Bind(Initializer.ParameterMap, TEXT("AttributesToVisualize"));
		DebugParamsParam.Bind(Initializer.ParameterMap, TEXT("DebugParams"));
	}
	FNiagaraVisualizeTexture2DPS() {}

	void SetParameters(FRHICommandList& RHICmdList, const FNiagaraGpuComputeDebug::FNiagaraVisualizeTexture* VisualizeTexture, uint32 TickCounter)
	{
		uint32 DebugParamsValue[2];
		DebugParamsValue[0] = GNiagaraGpuComputeDebug_ShowNaNInf != 0 ? 1 : 0;
		DebugParamsValue[1] = TickCounter;

		FIntVector4 AttributesToVisualize = VisualizeTexture->AttributesToVisualize;

		int32 NumAttributesToVisualizeValue = 0;
		for (NumAttributesToVisualizeValue=0; NumAttributesToVisualizeValue < 4; ++NumAttributesToVisualizeValue)
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
		SetTextureParameter(RHICmdList, PixelShader, Texture2DParam, TextureSamplerParam, TStaticSamplerState<SF_Point>::GetRHI(), VisualizeTexture->Texture);
		SetShaderValue(RHICmdList, PixelShader, NumTextureAttributesParam, VisualizeTexture->NumTextureAttributes);
		SetShaderValue(RHICmdList, PixelShader, NumAttributesToVisualizeParam, NumAttributesToVisualizeValue);
		SetShaderValue(RHICmdList, PixelShader, AttributesToVisualizeParam, AttributesToVisualize);
		SetShaderValue(RHICmdList, PixelShader, DebugParamsParam, DebugParamsValue);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, Texture2DParam);
	LAYOUT_FIELD(FShaderResourceParameter, TextureSamplerParam);
	LAYOUT_FIELD(FShaderParameter, NumTextureAttributesParam);
	LAYOUT_FIELD(FShaderParameter, NumAttributesToVisualizeParam);
	LAYOUT_FIELD(FShaderParameter, AttributesToVisualizeParam);
	LAYOUT_FIELD(FShaderParameter, DebugParamsParam);
};

IMPLEMENT_GLOBAL_SHADER(FNiagaraVisualizeTexture2DPS, "/Plugin/FX/Niagara/Private/NiagaraVisualizeTexture.usf", "Main2D", SF_Pixel);

//////////////////////////////////////////////////////////////////////////

void FNiagaraGpuComputeDebug::AddSystemInstance(FNiagaraSystemInstanceID SystemInstanceID, FString SystemName)
{
	SystemInstancesToWatch.FindOrAdd(SystemInstanceID) = SystemName;
}

void FNiagaraGpuComputeDebug::RemoveSystemInstance(FNiagaraSystemInstanceID SystemInstanceID)
{
	SystemInstancesToWatch.Remove(SystemInstanceID);
	VisualizeTextures.RemoveAll([&SystemInstanceID](const FNiagaraVisualizeTexture& Texture) -> bool { return Texture.SystemInstanceID == SystemInstanceID; });
}

void FNiagaraGpuComputeDebug::OnSystemDeallocated(FNiagaraSystemInstanceID SystemInstanceID)
{
	VisualizeTextures.RemoveAll([&SystemInstanceID](const FNiagaraVisualizeTexture& Texture) -> bool { return Texture.SystemInstanceID == SystemInstanceID; });
}

void FNiagaraGpuComputeDebug::AddTexture(FRHICommandList& RHICmdList, FNiagaraSystemInstanceID SystemInstanceID, FName SourceName, FTexture2DRHIRef Texture2D)
{
	AddAttributeTexture(RHICmdList, SystemInstanceID, SourceName, Texture2D, FIntPoint::ZeroValue, FIntVector4(INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE));
}

void FNiagaraGpuComputeDebug::AddAttributeTexture(FRHICommandList& RHICmdList, FNiagaraSystemInstanceID SystemInstanceID, FName SourceName, FTexture2DRHIRef Texture2D, FIntPoint NumTextureAttributes, FIntVector4 AttributeIndices)
{
	if (!SystemInstancesToWatch.Contains(SystemInstanceID))
	{
		return;
	}

	if (SourceName.IsNone() || !Texture2D.IsValid())
	{
		return;
	}

	bool bCreateTexture = false;

	const FIntVector SrcSize = Texture2D->GetSizeXYZ();
	const EPixelFormat SrcFormat = Texture2D->GetFormat();

	FNiagaraVisualizeTexture* VisualizeEntry = VisualizeTextures.FindByPredicate([&SourceName, &SystemInstanceID](const FNiagaraVisualizeTexture& Texture) -> bool { return Texture.SystemInstanceID == SystemInstanceID && Texture.SourceName == SourceName; });
	if (!VisualizeEntry)
	{
		VisualizeEntry = &VisualizeTextures.AddDefaulted_GetRef();
		VisualizeEntry->SystemInstanceID = SystemInstanceID;
		VisualizeEntry->SourceName = SourceName;
		bCreateTexture = true;
	}
	else
	{
		bCreateTexture = (VisualizeEntry->Texture->GetSizeXYZ() != SrcSize) || (VisualizeEntry->Texture->GetFormat() != SrcFormat);
	}
	VisualizeEntry->NumTextureAttributes = NumTextureAttributes;
	VisualizeEntry->AttributesToVisualize = AttributeIndices;

	// Do we need to create a texture to copy into?
	FTexture2DRHIRef Destination;
	if ( bCreateTexture )
	{
		FRHIResourceCreateInfo CreateInfo;
		Destination = RHICreateTexture2D(SrcSize.X, SrcSize.Y, SrcFormat, 1, 1, TexCreate_ShaderResource, CreateInfo);
		VisualizeEntry->Texture = Destination;
	}
	else
	{
		Destination = VisualizeEntry->Texture->GetTexture2D();
		check(Destination != nullptr);
	}

	// Copy texture
	RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, Texture2D);
	RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, Destination);

	FRHICopyTextureInfo CopyInfo;
	RHICmdList.CopyTexture(Texture2D, Destination, CopyInfo);

	RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, Destination);
}

bool FNiagaraGpuComputeDebug::ShouldDrawDebug() const
{
	return VisualizeTextures.Num() > 0;
}

void FNiagaraGpuComputeDebug::DrawDebug(FRHICommandListImmediate& RHICmdList, FCanvas* Canvas)
{
	if (VisualizeTextures.Num() == 0)
	{
		return;
	}

	++TickCounter;

	IRendererModule* RendererModule = &FModuleManager::GetModuleChecked<IRendererModule>("Renderer");

	const UFont* Font = GEngine->GetTinyFont();
	const float FontHeight = Font->GetMaxCharHeight();

	FRenderTarget* RenderTarget = Canvas->GetRenderTarget();
	FVector2D RenderTargetSize(RenderTarget->GetSizeXY().X, RenderTarget->GetSizeXY().Y);
	FRHIRenderPassInfo RPInfo(RenderTarget->GetRenderTargetTexture(), ERenderTargetActions::Load_Store);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("NiagaraVisualizeTextures"));

	RHICmdList.SetViewport(0, 0, 0.0f, RenderTargetSize.X, RenderTargetSize.Y, 1.0f);

	float X = 10.0f;
	float Y = RenderTargetSize.Y - 10.0f;

	auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
	TShaderMapRef<FNiagaraVisualizeTexture2DPS> PixelShader2D(ShaderMap);

	for (const FNiagaraVisualizeTexture& VisualizeEntry : VisualizeTextures)
	{
		FIntVector TextureSize = VisualizeEntry.Texture->GetSizeXYZ();
		if ( VisualizeEntry.NumTextureAttributes.X > 0 )
		{
			check(VisualizeEntry.NumTextureAttributes.Y > 0);
			TextureSize.X /= VisualizeEntry.NumTextureAttributes.X;
			TextureSize.Y /= VisualizeEntry.NumTextureAttributes.Y;
		}

		// Get system name
		const FString& SystemName = SystemInstancesToWatch.FindRef(VisualizeEntry.SystemInstanceID);

		// 2D Visualizer
		if (FRHITexture2D* Texture2D = VisualizeEntry.Texture->GetTexture2D())
		{
			FVector2D DisplaySize(TextureSize.X, TextureSize.Y);
			if (GNiagaraGpuComputeDebug_MaxTextureHeight > 0.0f)
			{
				DisplaySize.Y = FMath::Min(TextureSize.Y, int32(GNiagaraGpuComputeDebug_MaxTextureHeight));
				DisplaySize.X = TextureSize.X * (DisplaySize.Y / TextureSize.Y);
			}

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader2D.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			PixelShader2D->SetParameters(RHICmdList, &VisualizeEntry, TickCounter);

			Y -= DisplaySize.Y;

			RendererModule->DrawRectangle(
				RHICmdList,
				X, Y,										// Dest X, Y
				DisplaySize.X, DisplaySize.Y,				// Dest Width, Height
				0.0f, 0.0f,									// Source U, V
				TextureSize.X, TextureSize.Y,				// Source USize, VSize
				FIntPoint(RenderTargetSize.X, RenderTargetSize.Y),	// TargetSize
				FIntPoint(TextureSize.X, TextureSize.Y),	// Source texture size
				VertexShader,
				EDRF_Default);

			Y -= FontHeight;
			Canvas->DrawShadowedString(X, Y, *FString::Printf(TEXT("DataInterface: %s, System: %s"), *VisualizeEntry.SourceName.ToString(), *SystemName), Font, FLinearColor(1, 1, 1));
		}
		else
		{
			//-TODO: Add 3D Visualizer
		}

		Y -= 1.0f;
	}

	RHICmdList.EndRenderPass();
}
