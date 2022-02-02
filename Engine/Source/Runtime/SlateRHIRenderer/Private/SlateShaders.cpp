// Copyright Epic Games, Inc. All Rights Reserved.

#include "SlateShaders.h"
#include "Rendering/RenderingCommon.h"
#include "PipelineStateCache.h"

/** Flag to determine if we are running with a color vision deficiency shader on */
EColorVisionDeficiency GSlateColorDeficiencyType = EColorVisionDeficiency::NormalVision;
int32 GSlateColorDeficiencySeverity = 0;
bool GSlateColorDeficiencyCorrection = false;
bool GSlateShowColorDeficiencyCorrectionWithDeficiency = false;

IMPLEMENT_TYPE_LAYOUT(FSlateElementPS);

IMPLEMENT_SHADER_TYPE(, FSlateElementVS, TEXT("/Engine/Private/SlateVertexShader.usf"), TEXT("Main"), SF_Vertex);

IMPLEMENT_SHADER_TYPE(, FSlateDebugOverdrawPS, TEXT("/Engine/Private/SlateElementPixelShader.usf"), TEXT("DebugOverdrawMain"), SF_Pixel );

IMPLEMENT_SHADER_TYPE(, FSlatePostProcessBlurPS, TEXT("/Engine/Private/SlatePostProcessPixelShader.usf"), TEXT("GaussianBlurMain"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(, FSlatePostProcessDownsamplePS, TEXT("/Engine/Private/SlatePostProcessPixelShader.usf"), TEXT("DownsampleMain"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(, FSlatePostProcessUpsamplePS, TEXT("/Engine/Private/SlatePostProcessPixelShader.usf"), TEXT("UpsampleMain"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(, FSlatePostProcessColorDeficiencyPS, TEXT("/Engine/Private/SlatePostProcessColorDeficiencyPixelShader.usf"), TEXT("ColorDeficiencyMain"), SF_Pixel);

IMPLEMENT_SHADER_TYPE(, FSlateMaskingVS, TEXT("/Engine/Private/SlateMaskingShader.usf"), TEXT("MainVS"), SF_Vertex);
IMPLEMENT_SHADER_TYPE(, FSlateMaskingPS, TEXT("/Engine/Private/SlateMaskingShader.usf"), TEXT("MainPS"), SF_Pixel);

IMPLEMENT_SHADER_TYPE(, FSlateDebugBatchingPS, TEXT("/Engine/Private/SlateElementPixelShader.usf"), TEXT("DebugBatchingMain"), SF_Pixel );

#define IMPLEMENT_SLATE_PIXELSHADER_TYPE(ShaderType, bDrawDisabledEffect, bUseTextureAlpha, bIsVirtualTexture) \
	typedef TSlateElementPS<ESlateShader::ShaderType,bDrawDisabledEffect,bUseTextureAlpha,bIsVirtualTexture> TSlateElementPS##ShaderType##bDrawDisabledEffect##bUseTextureAlpha##bIsVirtualTexture##A; \
	IMPLEMENT_SHADER_TYPE(template<>,TSlateElementPS##ShaderType##bDrawDisabledEffect##bUseTextureAlpha##bIsVirtualTexture##A,TEXT("/Engine/Private/SlateElementPixelShader.usf"),TEXT("Main"),SF_Pixel);

#if WITH_EDITOR
IMPLEMENT_SHADER_TYPE(, FHDREditorConvertPS, TEXT("/Engine/Private/CompositeUIPixelShader.usf"), TEXT("HDREditorConvert"), SF_Pixel);
#endif
/**
* All the different permutations of shaders used by slate. Uses #defines to avoid dynamic branches
*/
IMPLEMENT_SLATE_PIXELSHADER_TYPE(Default, false, true, false);
IMPLEMENT_SLATE_PIXELSHADER_TYPE(Default, false, true, true);
IMPLEMENT_SLATE_PIXELSHADER_TYPE(Border, false, true, false);
IMPLEMENT_SLATE_PIXELSHADER_TYPE(Default, true, true, false);
IMPLEMENT_SLATE_PIXELSHADER_TYPE(Default, true, true, true);
IMPLEMENT_SLATE_PIXELSHADER_TYPE(Border, true, true, false);
IMPLEMENT_SLATE_PIXELSHADER_TYPE(Default, false, false, false);
IMPLEMENT_SLATE_PIXELSHADER_TYPE(Default, false, false, true);
IMPLEMENT_SLATE_PIXELSHADER_TYPE(Border, false, false, false);
IMPLEMENT_SLATE_PIXELSHADER_TYPE(Default, true, false, false);
IMPLEMENT_SLATE_PIXELSHADER_TYPE(Default, true, false, true);
IMPLEMENT_SLATE_PIXELSHADER_TYPE(Border, true, false, false);

IMPLEMENT_SLATE_PIXELSHADER_TYPE(GrayscaleFont, false, true, false);
IMPLEMENT_SLATE_PIXELSHADER_TYPE(GrayscaleFont, true, true, false);

IMPLEMENT_SLATE_PIXELSHADER_TYPE(ColorFont, false, true, false);
IMPLEMENT_SLATE_PIXELSHADER_TYPE(ColorFont, true, true, false);

IMPLEMENT_SLATE_PIXELSHADER_TYPE(LineSegment, false, true, false);
IMPLEMENT_SLATE_PIXELSHADER_TYPE(LineSegment, true, true, false);

IMPLEMENT_SLATE_PIXELSHADER_TYPE(RoundedBox, false, true, false);
IMPLEMENT_SLATE_PIXELSHADER_TYPE(RoundedBox, true, true, false);

/** The Slate vertex declaration. */
TGlobalResource<FSlateVertexDeclaration> GSlateVertexDeclaration;
TGlobalResource<FSlateInstancedVertexDeclaration> GSlateInstancedVertexDeclaration;
TGlobalResource<FSlateMaskingVertexDeclaration> GSlateMaskingVertexDeclaration;


/************************************************************************/
/* FSlateVertexDeclaration                                              */
/************************************************************************/
void FSlateVertexDeclaration::InitRHI()
{
	FVertexDeclarationElementList Elements;
	uint32 Stride = sizeof(FSlateVertex);
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FSlateVertex, TexCoords), VET_Float4, 0, Stride));
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FSlateVertex, MaterialTexCoords), VET_Float2, 1, Stride));
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FSlateVertex, Position), VET_Float2, 2, Stride));
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FSlateVertex, Color), VET_Color, 3, Stride));
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FSlateVertex, SecondaryColor), VET_Color, 4, Stride));
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FSlateVertex, PixelSize), VET_UShort2, 5, Stride));

	VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
}

void FSlateVertexDeclaration::ReleaseRHI()
{
	VertexDeclarationRHI.SafeRelease();
}


/************************************************************************/
/* FSlateInstancedVertexDeclaration                                     */
/************************************************************************/
void FSlateInstancedVertexDeclaration::InitRHI()
{
	FVertexDeclarationElementList Elements;
	uint32 Stride = sizeof(FSlateVertex);
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FSlateVertex, TexCoords), VET_Float4, 0, Stride));
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FSlateVertex, MaterialTexCoords), VET_Float2, 1, Stride));
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FSlateVertex, Position), VET_Float2, 2, Stride));
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FSlateVertex, Color), VET_Color, 3, Stride));
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FSlateVertex, SecondaryColor), VET_Color, 4, Stride));
	Elements.Add(FVertexElement(1, 0, VET_Float4, 5, sizeof(FVector4f), true));
	
	VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
}

void FSlateElementPS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.HDR.Display.OutputDevice"));
	OutEnvironment.SetDefine(TEXT("USE_709"), CVar ? (CVar->GetValueOnGameThread() == 1) : 1);
}


/************************************************************************/
/* FSlateMaskingVertexDeclaration                                              */
/************************************************************************/
void FSlateMaskingVertexDeclaration::InitRHI()
{
	FVertexDeclarationElementList Elements;
	uint32 Stride = sizeof(uint32);
	Elements.Add(FVertexElement(0, 0, VET_UByte4, 0, Stride));

	VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
}

void FSlateMaskingVertexDeclaration::ReleaseRHI()
{
	VertexDeclarationRHI.SafeRelease();
}


/************************************************************************/
/* FSlateDefaultVertexShader                                            */
/************************************************************************/

FSlateElementVS::FSlateElementVS( const ShaderMetaType::CompiledShaderInitializerType& Initializer )
	: FGlobalShader(Initializer)
{
	ViewProjection.Bind(Initializer.ParameterMap, TEXT("ViewProjection"));
	VertexShaderParams.Bind( Initializer.ParameterMap, TEXT("VertexShaderParams"));
	SwitchVerticalAxisMultiplier.Bind( Initializer.ParameterMap, TEXT("SwitchVerticalAxisMultiplier"));
}

void FSlateElementVS::SetViewProjection(FRHICommandList& RHICmdList, const FMatrix44f& InViewProjection )
{
	SetShaderValue(RHICmdList, RHICmdList.GetBoundVertexShader(), ViewProjection, InViewProjection );
}

void FSlateElementVS::SetShaderParameters(FRHICommandList& RHICmdList, const FVector4f& ShaderParams )
{
	SetShaderValue(RHICmdList, RHICmdList.GetBoundVertexShader(), VertexShaderParams, ShaderParams );
}

void FSlateElementVS::SetVerticalAxisMultiplier(FRHICommandList& RHICmdList, float InMultiplier )
{
	SetShaderValue(RHICmdList, RHICmdList.GetBoundVertexShader(), SwitchVerticalAxisMultiplier, InMultiplier );
}

/** Serializes the shader data */
/*bool FSlateElementVS::Serialize( FArchive& Ar )
{
	bool bShaderHasOutdatedParameters = FGlobalShader::Serialize( Ar );

	Ar << ViewProjection;
	Ar << VertexShaderParams;
	Ar << SwitchVerticalAxisMultiplier;

	return bShaderHasOutdatedParameters;
}*/


/************************************************************************/
/* FSlateMaskingVS                                            */
/************************************************************************/

FSlateMaskingVS::FSlateMaskingVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	: FGlobalShader(Initializer)
{
	ViewProjection.Bind(Initializer.ParameterMap, TEXT("ViewProjection"));
	MaskRect.Bind(Initializer.ParameterMap, TEXT("MaskRectPacked"));
	SwitchVerticalAxisMultiplier.Bind(Initializer.ParameterMap, TEXT("SwitchVerticalAxisMultiplier"));
}

void FSlateMaskingVS::SetViewProjection(FRHICommandList& RHICmdList, const FMatrix44f& InViewProjection)
{
	SetShaderValue(RHICmdList, RHICmdList.GetBoundVertexShader(), ViewProjection, InViewProjection);
}

void FSlateMaskingVS::SetVerticalAxisMultiplier(FRHICommandList& RHICmdList, float InMultiplier )
{
	SetShaderValue(RHICmdList, RHICmdList.GetBoundVertexShader(), SwitchVerticalAxisMultiplier, InMultiplier );
}

void FSlateMaskingVS::SetMaskRect(FRHICommandList& RHICmdList, const FVector2D& TopLeft, const FVector2D& TopRight, const FVector2D& BotLeft, const FVector2D& BotRight)
{
	//FVector4f MaskRectVal[4] = { FVector4f(TopLeft, FVector2D::ZeroVector), FVector4f(TopRight, FVector2D::ZeroVector), FVector4f(BotLeft, FVector2D::ZeroVector), FVector4f(BotRight, FVector2D::ZeroVector) };
	FVector4f MaskRectVal[2] = { FVector4f(FVector2f(TopLeft), FVector2f(TopRight)), FVector4f(FVector2f(BotLeft), FVector2f(BotRight)) };	// LWC_TODO: Precision loss

	SetShaderValue(RHICmdList, RHICmdList.GetBoundVertexShader(), MaskRect, MaskRectVal);
}

/** Serializes the shader data */
/*bool FSlateMaskingVS::Serialize(FArchive& Ar)
{
	bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);

	Ar << ViewProjection;
	Ar << MaskRect;
	Ar << SwitchVerticalAxisMultiplier;

	return bShaderHasOutdatedParameters;
}*/
