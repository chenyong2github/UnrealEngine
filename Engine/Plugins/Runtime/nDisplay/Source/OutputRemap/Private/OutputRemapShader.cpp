// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "OutputRemapShader.h"

#include "CoreMinimal.h"
#include "RHI.h"
#include "RHIResources.h"

#include "CommonRenderResources.h"
#include "PixelShaderUtils.h"

#include "ShaderParameterUtils.h"

#include "HAL/IConsoleManager.h"

#include "OutputRemapMesh.h"

#define OutputRemapShaderFileName TEXT("/Plugin/nDisplay/Private/OutputRemapShaders.usf")


// Select output remap shader
enum class EVarOutputRemapShaderType : uint8
{
	Default,
	Passthrough,
	Disable,
};

static TAutoConsoleVariable<int32> CVarOutputRemapShaderType(
	TEXT("nDisplay.render.output_remap.shader"),
	(int)EVarOutputRemapShaderType::Default,
	TEXT("Select shader for output remap:\n")	
	TEXT(" 0: default remap shader\n")
	TEXT(" 1: pass throught shader, test rect mesh\n")
	TEXT(" 2: Disable remap shaders\n")
	,ECVF_RenderThreadSafe
);


class FOutputRemapVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FOutputRemapVS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	/** Default constructor. */
	FOutputRemapVS() 
	{
	}
public:
	/** Initialization constructor. */
	FOutputRemapVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
	}
};

class FOutputRemapPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FOutputRemapPS, Global);

public:
	FOutputRemapPS()
	{ 
	}

	FOutputRemapPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessInputParameter0.Bind(Initializer.ParameterMap, TEXT("PostprocessInput0"));
		PostprocessInputParameterSampler0.Bind(Initializer.ParameterMap, TEXT("PostprocessInput0Sampler"));
	}

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

public:
	template<typename TShaderRHIParamRef>
	void SetParameters(FRHICommandListImmediate& RHICmdList, const TShaderRHIParamRef ShaderRHI, FRHITexture2D* SourceTexture)
	{
		SetTextureParameter(RHICmdList, ShaderRHI, PostprocessInputParameter0, SourceTexture);
		RHICmdList.SetShaderSampler(ShaderRHI, PostprocessInputParameterSampler0.GetBaseIndex(), TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar
			<< PostprocessInputParameter0
			<< PostprocessInputParameterSampler0
			;

		return bShaderHasOutdatedParameters;
	}

private:
	FShaderResourceParameter PostprocessInputParameter0;
	FShaderResourceParameter PostprocessInputParameterSampler0;
};

// Implement shaders inside UE4
IMPLEMENT_SHADER_TYPE(, FOutputRemapVS, OutputRemapShaderFileName, TEXT("OutputRemap_VS"), SF_Vertex);
IMPLEMENT_SHADER_TYPE(, FOutputRemapPS, OutputRemapShaderFileName, TEXT("OutputRemap_PS"), SF_Pixel);

bool FOutputRemapShader::ApplyOutputRemap_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* ShaderResourceTexture, FRHITexture2D* TargetableTexture, FOutputRemapMesh* MeshData)
{
	check(IsInRenderingThread());

	if (MeshData == nullptr)
	{
		//Handle error
		return false;
	}


	const EVarOutputRemapShaderType ShaderType = (EVarOutputRemapShaderType)CVarOutputRemapShaderType.GetValueOnAnyThread();
	switch (ShaderType)
	{
	case EVarOutputRemapShaderType::Passthrough:
	{
		// Use simple 1:1 test mesh for shader forwarding
		static FOutputRemapMesh TestMesh("Passthrough");
		MeshData = &TestMesh;
	}
	case EVarOutputRemapShaderType::Default:
		break;
	case EVarOutputRemapShaderType::Disable:
		return false;

	default:
		return false;
	};


	{// Do remap single render pass		
		FRHIRenderPassInfo RPInfo(TargetableTexture, ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("DisplayClusterOutputRemapShader"));
		{
			{
				FIntRect DstRect(FIntPoint(0,0), TargetableTexture->GetSizeXY());
				//Clear viewport before render
				RHICmdList.SetViewport(DstRect.Min.X, DstRect.Min.Y, 0.0f, DstRect.Max.X, DstRect.Max.Y, 1.0f);
				//DrawClearQuad(RHICmdList, FLinearColor::Black);
			}

			// Set the graphic pipeline state.
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Never>::GetRHI();
			GraphicsPSOInit.BlendState = TStaticBlendState <>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();

			TShaderMap<FGlobalShaderType>* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
			TShaderMapRef<FOutputRemapVS> VertexShader(ShaderMap);
			TShaderMapRef<FOutputRemapPS> PixelShader(ShaderMap);

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;// GetVertexDeclarationFVector4();
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;


			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			PixelShader->SetParameters(RHICmdList, PixelShader->GetPixelShader(), ShaderResourceTexture);
			MeshData->DrawMesh(RHICmdList);
		}
		RHICmdList.EndRenderPass();
	}

	// Render pass failed, handle error
	return true;
}


