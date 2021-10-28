// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceTexture.h"
#include "NiagaraShader.h"
#include "ShaderParameterUtils.h"
#include "NiagaraCustomVersion.h"


#define LOCTEXT_NAMESPACE "UNiagaraDataInterfaceTexture"

const FName UNiagaraDataInterfaceTexture::SampleTexture2DName(TEXT("SampleTexture2D"));
const FName UNiagaraDataInterfaceTexture::SampleVolumeTextureName(TEXT("SampleVolumeTexture"));
const FName UNiagaraDataInterfaceTexture::SamplePseudoVolumeTextureName(TEXT("SamplePseudoVolumeTexture"));
const FName UNiagaraDataInterfaceTexture::TextureDimsName(TEXT("TextureDimensions2D"));
const FString UNiagaraDataInterfaceTexture::TextureName(TEXT("Texture_"));
const FString UNiagaraDataInterfaceTexture::SamplerName(TEXT("Sampler_"));
const FString UNiagaraDataInterfaceTexture::DimensionsBaseName(TEXT("Dimensions_"));

struct FNiagaraDataInterfaceProxyTexture : public FNiagaraDataInterfaceProxy
{
	FSamplerStateRHIRef SamplerStateRHI;
	FTextureReferenceRHIRef TextureReferenceRHI;
	FVector2D TexDims;

	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override
	{
		checkNoEntry();
	}

	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override
	{
		return 0;
	}

	virtual void PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context) override
	{
		if (TextureReferenceRHI.IsValid())
		{
			// Make sure the texture is readable, we don't know where it's coming from.
			RHICmdList.Transition(FRHITransitionInfo(TextureReferenceRHI->GetReferencedTexture(), ERHIAccess::Unknown, ERHIAccess::SRVMask));
		}
	}
};

UNiagaraDataInterfaceTexture::UNiagaraDataInterfaceTexture(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, Texture(nullptr)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyTexture());
	MarkRenderDataDirty();
}

void UNiagaraDataInterfaceTexture::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
	}

	MarkRenderDataDirty();
}

void UNiagaraDataInterfaceTexture::PostLoad()
{
	Super::PostLoad();
#if WITH_EDITOR
	const int32 NiagaraVer = GetLinkerCustomVersion(FNiagaraCustomVersion::GUID);
	if (NiagaraVer < FNiagaraCustomVersion::TextureDataInterfaceUsesCustomSerialize)
	{
		if (Texture != nullptr)
		{
			Texture->ConditionalPostLoad();
		}
	}
#endif
	// Not safe since the UTexture might not have yet PostLoad() called and so UpdateResource() called.
	// This will affect whether the SamplerStateRHI will be available or not.
	TextureSize = Texture != nullptr ? FIntPoint(Texture->GetSurfaceWidth(), Texture->GetSurfaceHeight()) : FIntPoint::ZeroValue;
	MarkRenderDataDirty();
}

#if WITH_EDITOR

void UNiagaraDataInterfaceTexture::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	TextureSize = Texture != nullptr ? FIntPoint(Texture->GetSurfaceWidth(), Texture->GetSurfaceHeight()) : FIntPoint::ZeroValue;
	MarkRenderDataDirty();
}

#endif

void UNiagaraDataInterfaceTexture::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	if (Ar.IsLoading() == false || Ar.CustomVer(FNiagaraCustomVersion::GUID) >= FNiagaraCustomVersion::TextureDataInterfaceUsesCustomSerialize)
	{
		TArray<uint8> StreamData;
		Ar << StreamData;
	}
	Ar.UsingCustomVersion(FNiagaraCustomVersion::GUID);
}

bool UNiagaraDataInterfaceTexture::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}
	UNiagaraDataInterfaceTexture* DestinationTexture = CastChecked<UNiagaraDataInterfaceTexture>(Destination);
	DestinationTexture->Texture = Texture;
	DestinationTexture->TextureSize = Texture != nullptr ? FIntPoint(Texture->GetSurfaceWidth(), Texture->GetSurfaceHeight()) : FIntPoint::ZeroValue;
	DestinationTexture->MarkRenderDataDirty();

	return true;
}

bool UNiagaraDataInterfaceTexture::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceTexture* OtherTexture = CastChecked<const UNiagaraDataInterfaceTexture>(Other);
	return OtherTexture->Texture == Texture;
}

void UNiagaraDataInterfaceTexture::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleTexture2DName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;		
		Sig.bSupportsCPU = false;
		Sig.bSupportsGPU = true;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Texture")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("UV")));
		Sig.SetDescription(LOCTEXT("TextureSampleTexture2DDesc", "Sample mip level 0 of the input 2d texture at the specified UV coordinates. The UV origin (0,0) is in the upper left hand corner of the image."));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("Value")));
		//Sig.Owner = *GetName();

		OutFunctions.Add(Sig);
	}

	//{
	//	FNiagaraFunctionSignature Sig;
	//	Sig.Name = SampleVolumeTextureName;
	//	Sig.bMemberFunction = true;
	//	Sig.bRequiresContext = false;
	//	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Texture")));
	//	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("UVW")));
	//	Sig.SetDescription(LOCTEXT("TextureSampleVolumeTextureDesc", "Sample mip level 0 of the input 3d texture at the specified UVW coordinates. The UVW origin (0,0) is in the bottom left hand corner of the volume."));
	//	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("Value")));
	//	//Sig.Owner = *GetName();

	//	OutFunctions.Add(Sig);
	//}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SamplePseudoVolumeTextureName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bSupportsCPU = false;
		Sig.bSupportsGPU = true;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Texture")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("UVW")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("XYNumFrames")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("TotalNumFrames")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("MipMode")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("MipLevel")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("DDX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("DDY")));
		
		Sig.SetDescription(LOCTEXT("TextureSamplePseudoVolumeTextureDesc", "Return a pseudovolume texture sample.\nUseful for simulating 3D texturing with a 2D texture or as a texture flipbook with lerped transitions.\nTreats 2d layout of frames as a 3d texture and performs bilinear filtering by blending with an offset Z frame.\nTexture = Input Texture Object storing Volume Data\nUVW = Input float3 for Position, 0 - 1\nXYNumFrames = Input float for num frames in x, y directions\nTotalNumFrames = Input float for num total frames\nMipMode = Sampling mode : 0 = use miplevel, 1 = use UV computed gradients, 2 = Use gradients(default = 0)\nMipLevel = MIP level to use in mipmode = 0 (default 0)\nDDX, DDY = Texture gradients in mipmode = 2\n"));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("Value")));
		//Sig.Owner = *GetName();

		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = TextureDimsName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Texture")));
		Sig.SetDescription(LOCTEXT("TextureDimsDesc", "Get the dimensions of mip 0 of the texture."));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Dimensions2D")));
		//Sig.Owner = *GetName();

		OutFunctions.Add(Sig);
	}
}

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceTexture, SampleTexture);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceTexture, SamplePseudoVolumeTexture)
void UNiagaraDataInterfaceTexture::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == SampleTexture2DName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 4);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceTexture, SampleTexture)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SamplePseudoVolumeTextureName)
	{
		check(BindingInfo.GetNumInputs() == 12 && BindingInfo.GetNumOutputs() == 4);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceTexture, SamplePseudoVolumeTexture)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == TextureDimsName)
	{
		check(BindingInfo.GetNumInputs() == 0 && BindingInfo.GetNumOutputs() == 2);
		OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceTexture::GetTextureDimensions);
	}
}

bool UNiagaraDataInterfaceTexture::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	return false;
}

void UNiagaraDataInterfaceTexture::GetTextureDimensions(FVectorVMContext& Context)
{
	FNDIOutputParam<float> OutWidth(Context);
	FNDIOutputParam<float> OutHeight(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutWidth.SetAndAdvance(TextureSize.X);
		OutHeight.SetAndAdvance(TextureSize.Y);
	}
}

void UNiagaraDataInterfaceTexture::SampleTexture(FVectorVMContext& Context)
{
	VectorVM::FExternalFuncInputHandler<float> XParam(Context);
	VectorVM::FExternalFuncInputHandler<float> YParam(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleR(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleG(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleB(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleA(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		float X = XParam.GetAndAdvance();
		float Y = YParam.GetAndAdvance();
		*OutSampleR.GetDestAndAdvance() = 1.0;
		*OutSampleG.GetDestAndAdvance() = 0.0;
		*OutSampleB.GetDestAndAdvance() = 1.0;
		*OutSampleA.GetDestAndAdvance() = 1.0;
	}

}

void UNiagaraDataInterfaceTexture::SamplePseudoVolumeTexture(FVectorVMContext& Context)
{
	// Noop handler which just returns magenta since this doesn't run on CPU.
	VectorVM::FExternalFuncInputHandler<float> UVW_UParam(Context);
	VectorVM::FExternalFuncInputHandler<float> UVW_VParam(Context);
	VectorVM::FExternalFuncInputHandler<float> UVW_WParam(Context);

	VectorVM::FExternalFuncInputHandler<float> XYNumFrames_XParam(Context);
	VectorVM::FExternalFuncInputHandler<float> XYNumFrames_YParam(Context);
	
	VectorVM::FExternalFuncInputHandler<float> TotalNumFramesParam(Context);

	VectorVM::FExternalFuncInputHandler<int32> MipModeParam(Context);

	VectorVM::FExternalFuncInputHandler<float> MipLevelParam(Context);

	VectorVM::FExternalFuncInputHandler<float> DDX_XParam(Context);
	VectorVM::FExternalFuncInputHandler<float> DDX_YParam(Context);

	VectorVM::FExternalFuncInputHandler<float> DDY_XParam(Context);
	VectorVM::FExternalFuncInputHandler<float> DDY_YParam(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutSampleR(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleG(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleB(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleA(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		UVW_UParam.Advance();
		UVW_VParam.Advance();
		UVW_WParam.Advance();

		XYNumFrames_XParam.Advance();
		XYNumFrames_YParam.Advance();

		TotalNumFramesParam.Advance();

		MipModeParam.Advance();

		MipLevelParam.Advance();

		DDX_XParam.Advance();
		DDX_YParam.Advance();

		DDY_XParam.Advance();
		DDY_YParam.Advance();

		*OutSampleR.GetDestAndAdvance() = 1.0;
		*OutSampleG.GetDestAndAdvance() = 0.0;
		*OutSampleB.GetDestAndAdvance() = 1.0;
		*OutSampleA.GetDestAndAdvance() = 1.0;
	}
}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfaceTexture::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	if (FunctionInfo.DefinitionName == SampleTexture2DName)
	{
		FString HLSLTextureName = TextureName + ParamInfo.DataInterfaceHLSLSymbol;
		FString HLSLSamplerName = SamplerName + ParamInfo.DataInterfaceHLSLSymbol;
		OutHLSL += TEXT("void ") + FunctionInfo.InstanceName + TEXT("(in float2 In_UV, out float4 Out_Value) \n{\n");
		OutHLSL += TEXT("\t Out_Value = ") + HLSLTextureName + TEXT(".SampleLevel(") + HLSLSamplerName + TEXT(", In_UV, 0);\n");
		OutHLSL += TEXT("\n}\n");
		return true;
	}
	/*else if (FunctionInfo.DefinitionName == SampleVolumeTextureName)
	{
		FString HLSLTextureName = TextureName + ParamInfo.DataInterfaceHLSLSymbol;
		FString HLSLSamplerName = SamplerName + ParamInfo.DataInterfaceHLSLSymbol;
		OutHLSL += TEXT("void ") + FunctionInfo.InstanceName + TEXT("(in float3 In_UV, out float4 Out_Value) \n{\n");
		OutHLSL += TEXT("\t Out_Value = ") + HLSLTextureName + TEXT(".SampleLevel(") + HLSLSamplerName + TEXT(", In_UV, 0);\n");
		OutHLSL += TEXT("\n}\n");
		return true;
	}*/
	else if (FunctionInfo.DefinitionName == SamplePseudoVolumeTextureName)
	{
		FString HLSLTextureName = TextureName + ParamInfo.DataInterfaceHLSLSymbol;
		FString HLSLSamplerName = SamplerName + ParamInfo.DataInterfaceHLSLSymbol;
		OutHLSL += TEXT("void ") + FunctionInfo.InstanceName + TEXT("(in float3 In_UVW, in float2 In_XYNumFrames, in float In_TotalNumFrames, in int In_MipMode, in float In_MipLevel, in float2 In_DDX, in float2 In_DDY, out float4 Out_Value) \n{\n");
		OutHLSL += TEXT("\t Out_Value = PseudoVolumeTexture(") + HLSLTextureName + TEXT(", ") + HLSLSamplerName + TEXT(", In_UVW, In_XYNumFrames, In_TotalNumFrames, (uint) In_MipMode, In_MipLevel, In_DDX, In_DDY); \n");
		OutHLSL += TEXT("\n}\n");
		return true;
	}
	else if (FunctionInfo.DefinitionName == TextureDimsName)
	{
		FString DimsVar = DimensionsBaseName + ParamInfo.DataInterfaceHLSLSymbol;
		OutHLSL += TEXT("void ") + FunctionInfo.InstanceName + TEXT("(out float2 Out_Value) \n{\n");
		OutHLSL += TEXT("\t Out_Value = ") + DimsVar + TEXT(";\n");
		OutHLSL += TEXT("\n}\n");
		return true;
	}
	return false;
}

void UNiagaraDataInterfaceTexture::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	FString HLSLTextureName = TextureName + ParamInfo.DataInterfaceHLSLSymbol;
	FString HLSLSamplerName = SamplerName + ParamInfo.DataInterfaceHLSLSymbol;
	OutHLSL += TEXT("Texture2D ") + HLSLTextureName + TEXT(";\n");
	OutHLSL += TEXT("SamplerState ") + HLSLSamplerName + TEXT(";\n");
	OutHLSL += TEXT("float2 ") + DimensionsBaseName + ParamInfo.DataInterfaceHLSLSymbol + TEXT(";\n");
}
#endif

struct FNiagaraDataInterfaceParametersCS_Texture : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_Texture, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{
		FString TexName = UNiagaraDataInterfaceTexture::TextureName + ParameterInfo.DataInterfaceHLSLSymbol;
		FString SampleName = (UNiagaraDataInterfaceTexture::SamplerName + ParameterInfo.DataInterfaceHLSLSymbol);
		TextureParam.Bind(ParameterMap, *TexName);
		SamplerParam.Bind(ParameterMap, *SampleName);
		
		Dimensions.Bind(ParameterMap, *(UNiagaraDataInterfaceTexture::DimensionsBaseName + ParameterInfo.DataInterfaceHLSLSymbol));
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
		FNiagaraDataInterfaceProxyTexture* TextureDI = static_cast<FNiagaraDataInterfaceProxyTexture*>(Context.DataInterface);

		
		if (TextureDI && TextureDI->TextureReferenceRHI.IsValid())
		{
			FRHISamplerState* SamplerStateRHI = TextureDI->SamplerStateRHI;
			if (!SamplerStateRHI)
			{
				// Fallback required because PostLoad() order affects whether RHI resources 
				// are initalized in UNiagaraDataInterfaceTexture::PushToRenderThreadImpl().
				SamplerStateRHI = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			}

			SetTextureParameter(
				RHICmdList,
				ComputeShaderRHI,
				TextureParam,
				SamplerParam,
				SamplerStateRHI,
				TextureDI->TextureReferenceRHI->GetReferencedTexture()
			);
			SetShaderValue(RHICmdList, ComputeShaderRHI, Dimensions, FVector2D(TextureDI->TexDims.X, TextureDI->TexDims.Y));
		}
		else
		{
			SetTextureParameter(
				RHICmdList,
				ComputeShaderRHI,
				TextureParam,
				SamplerParam,
				GBlackTexture->SamplerStateRHI,
				GBlackTexture->TextureRHI
			);
			FVector2D TexDims(EForceInit::ForceInitToZero);
			SetShaderValue(RHICmdList, ComputeShaderRHI, Dimensions, FVector2D::ZeroVector);
		}
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, TextureParam);
	LAYOUT_FIELD(FShaderResourceParameter, SamplerParam);
	LAYOUT_FIELD(FShaderParameter, Dimensions);
};

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_Texture);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceTexture, FNiagaraDataInterfaceParametersCS_Texture);

void UNiagaraDataInterfaceTexture::PushToRenderThreadImpl()
{
	FNiagaraDataInterfaceProxyTexture* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyTexture>();

	TextureSize = FIntPoint::ZeroValue;
	if (Texture)
	{
		TextureSize.X = Texture->GetSurfaceWidth();
		TextureSize.Y = Texture->GetSurfaceHeight();
	}

	ENQUEUE_RENDER_COMMAND(FPushDITextureToRT)
	(
		[RT_Proxy, RT_Texture=Texture, RT_TexDims=TextureSize](FRHICommandListImmediate& RHICmdList)
		{
			if (RT_Texture)
			{
				RT_Proxy->TextureReferenceRHI = RT_Texture->TextureReference.TextureReferenceRHI;
				RT_Proxy->SamplerStateRHI = RT_Texture->Resource ? RT_Texture->Resource->SamplerStateRHI : nullptr;
			}
			else
			{
				RT_Proxy->TextureReferenceRHI = nullptr;
				RT_Proxy->SamplerStateRHI = nullptr;
			}
			RT_Proxy->TexDims = FVector2D(RT_TexDims.X, RT_TexDims.Y);
		}
	);
}

void UNiagaraDataInterfaceTexture::SetTexture(UTexture* InTexture)
{
	Texture = InTexture;
	TextureSize = Texture != nullptr ? FIntPoint(Texture->GetSurfaceWidth(), Texture->GetSurfaceHeight()) : FIntPoint::ZeroValue;
	MarkRenderDataDirty();
}

#undef LOCTEXT_NAMESPACE
