// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterface2DArrayTexture.h"
#include "NiagaraShader.h"
#include "ShaderParameterUtils.h"
#include "NiagaraCustomVersion.h"
#include "Engine/Texture2DArray.h"


#define LOCTEXT_NAMESPACE "UNiagaraDataInterface2DArrayTexture"

const FName UNiagaraDataInterface2DArrayTexture::SampleTextureName(TEXT("SampleTexture"));
const FName UNiagaraDataInterface2DArrayTexture::TextureDimsName(TEXT("TextureDimensions"));
const FString UNiagaraDataInterface2DArrayTexture::TextureName(TEXT("Texture_"));
const FString UNiagaraDataInterface2DArrayTexture::SamplerName(TEXT("Sampler_"));
const FString UNiagaraDataInterface2DArrayTexture::DimensionsBaseName(TEXT("Dimensions_"));

UNiagaraDataInterface2DArrayTexture::UNiagaraDataInterface2DArrayTexture(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, Texture(nullptr)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxy2DArrayTexture());
	MarkRenderDataDirty();
}

void UNiagaraDataInterface2DArrayTexture::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
	}

	MarkRenderDataDirty();
}

void UNiagaraDataInterface2DArrayTexture::PostLoad()
{
	Super::PostLoad();

	// Not safe since the UTexture might not have yet PostLoad() called and so UpdateResource() called.
	// This will affect whether the SamplerStateRHI will be available or not.
	MarkRenderDataDirty();
}

#if WITH_EDITOR

void UNiagaraDataInterface2DArrayTexture::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	MarkRenderDataDirty();
}

#endif

bool UNiagaraDataInterface2DArrayTexture::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}
	UNiagaraDataInterface2DArrayTexture* DestinationTexture = CastChecked<UNiagaraDataInterface2DArrayTexture>(Destination);
	DestinationTexture->Texture = Texture;
	DestinationTexture->MarkRenderDataDirty();

	return true;
}

bool UNiagaraDataInterface2DArrayTexture::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterface2DArrayTexture* OtherTexture = CastChecked<const UNiagaraDataInterface2DArrayTexture>(Other);
	return OtherTexture->Texture == Texture;
}

void UNiagaraDataInterface2DArrayTexture::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	OutFunctions.Reserve(OutFunctions.Num() + 2);

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = SampleTextureName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bSupportsCPU = false;
		Sig.bSupportsGPU = true;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Texture")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("UVW")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("MipLevel")));
		Sig.SetDescription(LOCTEXT("TextureSampleVolumeTextureDesc", "Sample the specified mip level of the input texture at the specified UVW coordinates. Where W is the slice to sample (0,1,2, etc) and UV are the coordinates into the slice."));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("Value")));
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = TextureDimsName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Texture")));
		Sig.SetDescription(LOCTEXT("TextureDimsDesc", "Get the dimensions of mip 0 of the texture."));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Dimensions")));
	}
}

void UNiagaraDataInterface2DArrayTexture::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == TextureDimsName)
	{
		check(BindingInfo.GetNumInputs() == 0 && BindingInfo.GetNumOutputs() == 3);
		OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterface2DArrayTexture::GetTextureDimensions);
	}
}

bool UNiagaraDataInterface2DArrayTexture::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	const FIntVector CurrentTextureSize = Texture != nullptr ? FIntVector(Texture->GetSizeX(), Texture->GetSizeY(), Texture->GetNumSlices()) : FIntVector::ZeroValue;
	if ( CurrentTextureSize != TextureSize )
	{
		TextureSize = CurrentTextureSize;
		MarkRenderDataDirty();
	}
	return false;
}

void UNiagaraDataInterface2DArrayTexture::GetTextureDimensions(FVectorVMContext& Context)
{
	FNDIOutputParam<FVector> DimensionsOut(Context);

	const FVector FloatTextureSize(TextureSize.X, TextureSize.Y, TextureSize.Z);
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		DimensionsOut.SetAndAdvance(FloatTextureSize);
	}
}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterface2DArrayTexture::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	if (FunctionInfo.DefinitionName == SampleTextureName)
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

void UNiagaraDataInterface2DArrayTexture::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	FString HLSLTextureName = TextureName + ParamInfo.DataInterfaceHLSLSymbol;
	FString HLSLSamplerName = SamplerName + ParamInfo.DataInterfaceHLSLSymbol;
	OutHLSL += TEXT("Texture2DArray ") + HLSLTextureName + TEXT(";\n");
	OutHLSL += TEXT("SamplerState ") + HLSLSamplerName + TEXT(";\n");
	OutHLSL += TEXT("float3 ") + DimensionsBaseName + ParamInfo.DataInterfaceHLSLSymbol + TEXT(";\n");
}
#endif

struct FNiagaraDataInterfaceParametersCS_2DArrayTexture : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_INLINE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_2DArrayTexture, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{
		FString TexName = UNiagaraDataInterface2DArrayTexture::TextureName + ParameterInfo.DataInterfaceHLSLSymbol;
		FString SampleName = (UNiagaraDataInterface2DArrayTexture::SamplerName + ParameterInfo.DataInterfaceHLSLSymbol);
		TextureParam.Bind(ParameterMap, *TexName);
		SamplerParam.Bind(ParameterMap, *SampleName);

		Dimensions.Bind(ParameterMap, *(UNiagaraDataInterface2DArrayTexture::DimensionsBaseName + ParameterInfo.DataInterfaceHLSLSymbol));

	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
		FNiagaraDataInterfaceProxy2DArrayTexture* TextureDI = static_cast<FNiagaraDataInterfaceProxy2DArrayTexture*>(Context.DataInterface);

		if (TextureDI && TextureDI->TextureRHI)
		{
			FRHISamplerState* SamplerStateRHI = TextureDI->SamplerStateRHI;
			if (!SamplerStateRHI)
			{
				// Fallback required because PostLoad() order affects whether RHI resources 
				// are initalized in UNiagaraDataInterface2DArrayTexture::PushToRenderThreadImpl().
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
				GBlackArrayTexture->SamplerStateRHI,
				GBlackArrayTexture->TextureRHI
			);
			SetShaderValue(RHICmdList, ComputeShaderRHI, Dimensions, FVector::ZeroVector);
		}
	}
private:
	LAYOUT_FIELD(FShaderResourceParameter, TextureParam);
	LAYOUT_FIELD(FShaderResourceParameter, SamplerParam);
	LAYOUT_FIELD(FShaderParameter, Dimensions);
};

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterface2DArrayTexture, FNiagaraDataInterfaceParametersCS_2DArrayTexture);

void UNiagaraDataInterface2DArrayTexture::PushToRenderThreadImpl()
{
	FNiagaraDataInterfaceProxy2DArrayTexture* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxy2DArrayTexture>();

	TextureSize = FIntVector::ZeroValue;
	if (Texture)
	{
		TextureSize.X = Texture->GetSizeX();
		TextureSize.Y = Texture->GetSizeY();
		TextureSize.Z = Texture->GetNumSlices();
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

void UNiagaraDataInterface2DArrayTexture::SetTexture(UTexture2DArray* InTexture)
{
	if (InTexture)
	{
		Texture = InTexture;
		MarkRenderDataDirty();
	}
}

#undef LOCTEXT_NAMESPACE