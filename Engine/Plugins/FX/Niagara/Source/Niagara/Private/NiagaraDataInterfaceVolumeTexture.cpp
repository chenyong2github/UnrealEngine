// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceVolumeTexture.h"
#include "NiagaraShader.h"
#include "ShaderParameterUtils.h"
#include "NiagaraCustomVersion.h"
#include "Engine/VolumeTexture.h"


#define LOCTEXT_NAMESPACE "UNiagaraDataInterfaceVolumeTexture"

const FName UNiagaraDataInterfaceVolumeTexture::SampleVolumeTextureName(TEXT("SampleVolumeTexture"));
const FName UNiagaraDataInterfaceVolumeTexture::TextureDimsName(TEXT("TextureDimensions3D"));
const FString UNiagaraDataInterfaceVolumeTexture::TextureName(TEXT("Texture_"));
const FString UNiagaraDataInterfaceVolumeTexture::SamplerName(TEXT("Sampler_"));
const FString UNiagaraDataInterfaceVolumeTexture::DimensionsBaseName(TEXT("Dimensions_"));

UNiagaraDataInterfaceVolumeTexture::UNiagaraDataInterfaceVolumeTexture(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, Texture(nullptr)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyVolumeTexture());
	MarkRenderDataDirty();
}

void UNiagaraDataInterfaceVolumeTexture::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
	}

	MarkRenderDataDirty();
}

void UNiagaraDataInterfaceVolumeTexture::PostLoad()
{
	Super::PostLoad();

	// Not safe since the UTexture might not have yet PostLoad() called and so UpdateResource() called.
	// This will affect whether the SamplerStateRHI will be available or not.
	MarkRenderDataDirty();
}

#if WITH_EDITOR

void UNiagaraDataInterfaceVolumeTexture::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	MarkRenderDataDirty();
}

#endif

bool UNiagaraDataInterfaceVolumeTexture::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}
	UNiagaraDataInterfaceVolumeTexture* DestinationTexture = CastChecked<UNiagaraDataInterfaceVolumeTexture>(Destination);
	DestinationTexture->Texture = Texture;
	DestinationTexture->MarkRenderDataDirty();

	return true;
}

bool UNiagaraDataInterfaceVolumeTexture::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceVolumeTexture* OtherTexture = CastChecked<const UNiagaraDataInterfaceVolumeTexture>(Other);
	return OtherTexture->Texture == Texture;
}

void UNiagaraDataInterfaceVolumeTexture::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleVolumeTextureName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bSupportsCPU = false;
		Sig.bSupportsGPU = true;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Texture")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("UVW")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("MipLevel")));
		Sig.SetDescription(LOCTEXT("TextureSampleVolumeTextureDesc", "Sample the specified mip level of the input 3d texture at the specified UVW coordinates. The UVW origin (0, 0, 0) is in the bottom left hand corner of the volume."));
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
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Dimensions3D")));
		//Sig.Owner = *GetName();

		OutFunctions.Add(Sig);
	}
}

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceVolumeTexture, SampleVolumeTexture)
void UNiagaraDataInterfaceVolumeTexture::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == SampleVolumeTextureName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 4);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceVolumeTexture, SampleVolumeTexture)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == TextureDimsName)
	{
		check(BindingInfo.GetNumInputs() == 0 && BindingInfo.GetNumOutputs() == 3);
		OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceVolumeTexture::GetTextureDimensions);
	}
}

bool UNiagaraDataInterfaceVolumeTexture::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	const FIntVector CurrentTextureSize = Texture != nullptr ? FIntVector(Texture->GetSizeX(), Texture->GetSizeY(), Texture->GetSizeZ()) : FIntVector::ZeroValue;
	if ( CurrentTextureSize !=  TextureSize )
	{
		TextureSize = CurrentTextureSize;
		MarkRenderDataDirty();
	}
	return false;
}

void UNiagaraDataInterfaceVolumeTexture::GetTextureDimensions(FVectorVMContext& Context)
{
	FNDIOutputParam<float> OutWidth(Context);
	FNDIOutputParam<float> OutHeight(Context);
	FNDIOutputParam<float> OutDepth(Context);

	FVector FloatTextureSize(TextureSize.X, TextureSize.Y, TextureSize.Z);
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutWidth.SetAndAdvance(FloatTextureSize.X);
		OutHeight.SetAndAdvance(FloatTextureSize.Y);
		OutDepth.SetAndAdvance(FloatTextureSize.Z);
	}
}

void UNiagaraDataInterfaceVolumeTexture::SampleVolumeTexture(FVectorVMContext& Context)
{
	VectorVM::FExternalFuncInputHandler<float> XParam(Context);
	VectorVM::FExternalFuncInputHandler<float> YParam(Context);
	VectorVM::FExternalFuncInputHandler<float> ZParam(Context);
	VectorVM::FExternalFuncInputHandler<float> MipLevelParam(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleR(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleG(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleB(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleA(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		float X = XParam.GetAndAdvance();
		float Y = YParam.GetAndAdvance();
		float Z = YParam.GetAndAdvance();
		float Mip = MipLevelParam.GetAndAdvance();
		*OutSampleR.GetDestAndAdvance() = 1.0;
		*OutSampleG.GetDestAndAdvance() = 0.0;
		*OutSampleB.GetDestAndAdvance() = 1.0;
		*OutSampleA.GetDestAndAdvance() = 1.0;
	}

}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfaceVolumeTexture::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	if (FunctionInfo.DefinitionName == SampleVolumeTextureName)
	{
		FString HLSLTextureName = TextureName + ParamInfo.DataInterfaceHLSLSymbol;
		FString HLSLSamplerName = SamplerName + ParamInfo.DataInterfaceHLSLSymbol;
		OutHLSL += TEXT("void ") + FunctionInfo.InstanceName + TEXT("(in float3 In_UV, in float MipLevel, out float4 Out_Value) \n{\n");
		OutHLSL += TEXT("\t Out_Value = ") + HLSLTextureName + TEXT(".SampleLevel(") + HLSLSamplerName + TEXT(", In_UV, MipLevel);\n");
		OutHLSL += TEXT("\n}\n");
		return true;
	}
	else if (FunctionInfo.DefinitionName == TextureDimsName)
	{
		FString DimsVar = DimensionsBaseName + ParamInfo.DataInterfaceHLSLSymbol;
		OutHLSL += TEXT("void ") + FunctionInfo.InstanceName + TEXT("(out float3 Out_Value) \n{\n");
		OutHLSL += TEXT("\t Out_Value = ") + DimsVar + TEXT(";\n");
		OutHLSL += TEXT("\n}\n");
		return true;
	}
	return false;
}

void UNiagaraDataInterfaceVolumeTexture::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	FString HLSLTextureName = TextureName + ParamInfo.DataInterfaceHLSLSymbol;
	FString HLSLSamplerName = SamplerName + ParamInfo.DataInterfaceHLSLSymbol;
	OutHLSL += TEXT("Texture3D ") + HLSLTextureName + TEXT(";\n");
	OutHLSL += TEXT("SamplerState ") + HLSLSamplerName + TEXT(";\n");
	OutHLSL += TEXT("float3 ") + DimensionsBaseName + ParamInfo.DataInterfaceHLSLSymbol + TEXT(";\n");
}
#endif

struct FNiagaraDataInterfaceParametersCS_VolumeTexture : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_INLINE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_VolumeTexture, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{
		FString TexName = UNiagaraDataInterfaceVolumeTexture::TextureName + ParameterInfo.DataInterfaceHLSLSymbol;
		FString SampleName = (UNiagaraDataInterfaceVolumeTexture::SamplerName + ParameterInfo.DataInterfaceHLSLSymbol);
		TextureParam.Bind(ParameterMap, *TexName);
		SamplerParam.Bind(ParameterMap, *SampleName);


		Dimensions.Bind(ParameterMap, *(UNiagaraDataInterfaceVolumeTexture::DimensionsBaseName + ParameterInfo.DataInterfaceHLSLSymbol));

	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
		FNiagaraDataInterfaceProxyVolumeTexture* TextureDI = static_cast<FNiagaraDataInterfaceProxyVolumeTexture*>(Context.DataInterface);

		if (TextureDI && TextureDI->TextureRHI)
		{
			FRHISamplerState* SamplerStateRHI = TextureDI->SamplerStateRHI;
			if (!SamplerStateRHI)
			{
				// Fallback required because PostLoad() order affects whether RHI resources 
				// are initalized in UNiagaraDataInterfaceVolumeTexture::PushToRenderThreadImpl().
				SamplerStateRHI = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			}
			SetTextureParameter(
				RHICmdList,
				ComputeShaderRHI,
				TextureParam,
				SamplerParam,
				SamplerStateRHI,
				TextureDI->TextureRHI
			);
			SetShaderValue(RHICmdList, ComputeShaderRHI, Dimensions, TextureDI->TexDims);
		}
		else
		{
			SetTextureParameter(
				RHICmdList,
				ComputeShaderRHI,
				TextureParam,
				SamplerParam,
				GBlackVolumeTexture->SamplerStateRHI,
				GBlackVolumeTexture->TextureRHI
			);
			SetShaderValue(RHICmdList, ComputeShaderRHI, Dimensions, FVector::ZeroVector);
		}
	}
private:
	LAYOUT_FIELD(FShaderResourceParameter, TextureParam);
	LAYOUT_FIELD(FShaderResourceParameter, SamplerParam);
	LAYOUT_FIELD(FShaderParameter, Dimensions);
};

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceVolumeTexture, FNiagaraDataInterfaceParametersCS_VolumeTexture);

void UNiagaraDataInterfaceVolumeTexture::PushToRenderThreadImpl()
{
	FNiagaraDataInterfaceProxyVolumeTexture* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyVolumeTexture>();

	TextureSize = FIntVector::ZeroValue;
	if (Texture)
	{
		TextureSize.X = Texture->GetSizeX();
		TextureSize.Y = Texture->GetSizeY();
		TextureSize.Z = Texture->GetSizeZ();
	}

	ENQUEUE_RENDER_COMMAND(FPushDITextureToRT)
	(
		[RT_Proxy, RT_Resource=Texture ? Texture->Resource : nullptr, RT_TexDims=TextureSize](FRHICommandListImmediate& RHICmdList)
		{
			RT_Proxy->TextureRHI = RT_Resource ? RT_Resource->TextureRHI : nullptr;
			RT_Proxy->SamplerStateRHI = RT_Resource ? RT_Resource->SamplerStateRHI : nullptr;
			RT_Proxy->TexDims = FVector(RT_TexDims.X, RT_TexDims.Y, RT_TexDims.Z);
		}
	);
}

void UNiagaraDataInterfaceVolumeTexture::SetTexture(UVolumeTexture* InTexture)
{
	if (InTexture)
	{
		Texture = InTexture;
		MarkRenderDataDirty();
	}
}

#undef LOCTEXT_NAMESPACE