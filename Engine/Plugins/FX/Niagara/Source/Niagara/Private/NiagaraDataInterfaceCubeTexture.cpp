// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceCubeTexture.h"
#include "NiagaraShader.h"
#include "ShaderParameterUtils.h"
#include "NiagaraCustomVersion.h"
#include "Engine/TextureCube.h"


#define LOCTEXT_NAMESPACE "UNiagaraDataInterfaceCubeTexture"

const FName UNiagaraDataInterfaceCubeTexture::SampleCubeTextureName(TEXT("SampleCubeTexture"));
const FName UNiagaraDataInterfaceCubeTexture::TextureDimsName(TEXT("TextureDimensions"));
const FString UNiagaraDataInterfaceCubeTexture::TextureName(TEXT("Texture_"));
const FString UNiagaraDataInterfaceCubeTexture::SamplerName(TEXT("Sampler_"));
const FString UNiagaraDataInterfaceCubeTexture::DimensionsBaseName(TEXT("Dimensions_"));

struct FNiagaraDataInterfaceProxyCubeTexture : public FNiagaraDataInterfaceProxy
{
	FSamplerStateRHIRef SamplerStateRHI;
	FTextureRHIRef TextureRHI;
	FIntPoint TexDims;

	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override { check(false); }
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return 0; }
};

UNiagaraDataInterfaceCubeTexture::UNiagaraDataInterfaceCubeTexture(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, Texture(nullptr)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyCubeTexture());
	MarkRenderDataDirty();
}

void UNiagaraDataInterfaceCubeTexture::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
	}

	MarkRenderDataDirty();
}

void UNiagaraDataInterfaceCubeTexture::PostLoad()
{
	Super::PostLoad();

	// Not safe since the UTexture might not have yet PostLoad() called and so UpdateResource() called.
	// This will affect whether the SamplerStateRHI will be available or not.
	MarkRenderDataDirty();
}

#if WITH_EDITOR

void UNiagaraDataInterfaceCubeTexture::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	MarkRenderDataDirty();
}

#endif

bool UNiagaraDataInterfaceCubeTexture::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}
	UNiagaraDataInterfaceCubeTexture* DestinationTexture = CastChecked<UNiagaraDataInterfaceCubeTexture>(Destination);
	DestinationTexture->Texture = Texture;
	DestinationTexture->MarkRenderDataDirty();

	return true;
}

bool UNiagaraDataInterfaceCubeTexture::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceCubeTexture* OtherTexture = CastChecked<const UNiagaraDataInterfaceCubeTexture>(Other);
	return OtherTexture->Texture == Texture;
}

void UNiagaraDataInterfaceCubeTexture::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = SampleCubeTextureName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bSupportsCPU = false;
		Sig.bSupportsGPU = true;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Texture")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("UVW")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("MipLevel")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("Value")));
		Sig.SetDescription(LOCTEXT("TextureSampleCubeTextureDesc", "Sample the specified mip level of the input cube texture at the specified UVW coordinates"));
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = TextureDimsName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Texture")));
		Sig.SetDescription(LOCTEXT("TextureDimsDesc", "Get the dimensions of mip 0 of the texture."));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Width")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Height")));
	}
}

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceCubeTexture, SampleCubeTexture)
void UNiagaraDataInterfaceCubeTexture::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == SampleCubeTextureName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 4);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceCubeTexture, SampleCubeTexture)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == TextureDimsName)
	{
		check(BindingInfo.GetNumInputs() == 0 && BindingInfo.GetNumOutputs() == 2);
		OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceCubeTexture::GetTextureDimensions);
	}
}

bool UNiagaraDataInterfaceCubeTexture::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	const FIntPoint CurrentTextureSize = Texture != nullptr ? FIntPoint(Texture->GetSizeX(), Texture->GetSizeY()) : FIntPoint::ZeroValue;
	if ( CurrentTextureSize != TextureSize )
	{
		TextureSize = CurrentTextureSize;
		MarkRenderDataDirty();
	}
	return false;
}

void UNiagaraDataInterfaceCubeTexture::GetTextureDimensions(FVectorVMContext& Context)
{
	FNDIOutputParam<int32> OutWidth(Context);
	FNDIOutputParam<int32> OutHeight(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutWidth.SetAndAdvance(TextureSize.X);
		OutHeight.SetAndAdvance(TextureSize.Y);
	}
}

void UNiagaraDataInterfaceCubeTexture::SampleCubeTexture(FVectorVMContext& Context)
{
	FNDIInputParam<FVector> InCoord(Context);
	FNDIInputParam<float> InMipLevel(Context);
	FNDIOutputParam<FVector4> OutColor(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutColor.SetAndAdvance(FVector4(1.0f, 0.0f, 1.0f, 1.0f));
	}

}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfaceCubeTexture::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	if (FunctionInfo.DefinitionName == SampleCubeTextureName)
	{
		const FString HLSLTextureName = TextureName + ParameterInfo.DataInterfaceHLSLSymbol;
		const FString HLSLSamplerName = SamplerName + ParameterInfo.DataInterfaceHLSLSymbol;

		OutHLSL.Appendf(TEXT("void %s(in float3 In_UVW, in float MipLevel, out float4 Out_Value)\n"), *FunctionInfo.InstanceName);
		OutHLSL.Append(TEXT("{\n"));
		OutHLSL.Appendf(TEXT("\tOut_Value = %s.SampleLevel(%s, In_UVW, MipLevel);\n"), *HLSLTextureName, *HLSLSamplerName);
		OutHLSL.Append(TEXT("}\n"));
		return true;
	}
	else if (FunctionInfo.DefinitionName == TextureDimsName)
	{
		const FString DimsVar = DimensionsBaseName + ParameterInfo.DataInterfaceHLSLSymbol;

		OutHLSL.Appendf(TEXT("void %s(out int Out_Width, out int Out_Height)\n"), *FunctionInfo.InstanceName);
		OutHLSL.Append (TEXT("{\n"));
		OutHLSL.Appendf(TEXT("\tOut_Width = %s.x;\n"), *DimsVar);
		OutHLSL.Appendf(TEXT("\tOut_Width = %s.y;\n"), *DimsVar);
		OutHLSL.Append(TEXT("}\n"));
		return true;
	}
	return false;
}

void UNiagaraDataInterfaceCubeTexture::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, FString& OutHLSL)
{
	const FString HLSLTextureName = TextureName + ParameterInfo.DataInterfaceHLSLSymbol;
	const FString HLSLSamplerName = SamplerName + ParameterInfo.DataInterfaceHLSLSymbol;
	const FString DimsVar = DimensionsBaseName + ParameterInfo.DataInterfaceHLSLSymbol;

	OutHLSL.Appendf(TEXT("TextureCube %s;\n"), *HLSLTextureName);
	OutHLSL.Appendf(TEXT("SamplerState %s;\n"), *HLSLSamplerName);
	OutHLSL.Appendf(TEXT("int2 %s;\n"), *DimsVar);
}
#endif

struct FNiagaraDataInterfaceParametersCS_CubeTexture : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_CubeTexture, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{
		const FString HLSLTextureName = UNiagaraDataInterfaceCubeTexture::TextureName + ParameterInfo.DataInterfaceHLSLSymbol;
		const FString HLSLSamplerName = UNiagaraDataInterfaceCubeTexture::SamplerName + ParameterInfo.DataInterfaceHLSLSymbol;
		const FString DimsVar = UNiagaraDataInterfaceCubeTexture::DimensionsBaseName + ParameterInfo.DataInterfaceHLSLSymbol;

		TextureParam.Bind(ParameterMap, *HLSLTextureName);
		SamplerParam.Bind(ParameterMap, *HLSLSamplerName);
		Dimensions.Bind(ParameterMap, *DimsVar);
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
		FNiagaraDataInterfaceProxyCubeTexture* TextureDI = static_cast<FNiagaraDataInterfaceProxyCubeTexture*>(Context.DataInterface);

		if (TextureDI && TextureDI->TextureRHI)
		{
			FRHISamplerState* SamplerStateRHI = TextureDI->SamplerStateRHI;
			if (!SamplerStateRHI)
			{
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
				GBlackTextureCube->SamplerStateRHI,
				GBlackTextureCube->TextureRHI
			);
			SetShaderValue(RHICmdList, ComputeShaderRHI, Dimensions, FVector::ZeroVector);
		}
	}
private:
	LAYOUT_FIELD(FShaderResourceParameter, TextureParam);
	LAYOUT_FIELD(FShaderResourceParameter, SamplerParam);
	LAYOUT_FIELD(FShaderParameter, Dimensions);
};

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_CubeTexture);
IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceCubeTexture, FNiagaraDataInterfaceParametersCS_CubeTexture);

void UNiagaraDataInterfaceCubeTexture::PushToRenderThreadImpl()
{
	FNiagaraDataInterfaceProxyCubeTexture* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyCubeTexture>();

	TextureSize = FIntPoint::ZeroValue;
	if (Texture)
	{
		TextureSize.X = Texture->GetSizeX();
		TextureSize.Y = Texture->GetSizeY();
	}

	ENQUEUE_RENDER_COMMAND(FPushDITextureToRT)
	(
		[RT_Proxy, RT_Resource=Texture ? Texture->Resource : nullptr, RT_TexDims=TextureSize](FRHICommandListImmediate& RHICmdList)
		{
			RT_Proxy->TextureRHI = RT_Resource ? RT_Resource->TextureRHI : nullptr;
			RT_Proxy->SamplerStateRHI = RT_Resource ? RT_Resource->SamplerStateRHI : nullptr;
			RT_Proxy->TexDims = RT_TexDims;
		}
	);
}

void UNiagaraDataInterfaceCubeTexture::SetTexture(UTextureCube* InTexture)
{
	if (InTexture)
	{
		Texture = InTexture;
		MarkRenderDataDirty();
	}
}

#undef LOCTEXT_NAMESPACE
