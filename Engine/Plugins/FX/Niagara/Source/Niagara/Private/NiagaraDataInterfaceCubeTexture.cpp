// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceCubeTexture.h"
#include "NiagaraShader.h"
#include "ShaderParameterUtils.h"
#include "NiagaraCustomVersion.h"
#include "Engine/TextureCube.h"
#include "Engine/TextureRenderTargetCube.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraComputeExecutionContext.h"

#define LOCTEXT_NAMESPACE "UNiagaraDataInterfaceCubeTexture"

const FName UNiagaraDataInterfaceCubeTexture::SampleCubeTextureName(TEXT("SampleCubeTexture"));
const FName UNiagaraDataInterfaceCubeTexture::TextureDimsName(TEXT("TextureDimensions"));
const FString UNiagaraDataInterfaceCubeTexture::TextureName(TEXT("Texture_"));
const FString UNiagaraDataInterfaceCubeTexture::SamplerName(TEXT("Sampler_"));
const FString UNiagaraDataInterfaceCubeTexture::DimensionsBaseName(TEXT("Dimensions_"));

struct FNDICubeTextureInstanceData_GameThread
{
	TWeakObjectPtr<UTexture> CurrentTexture = nullptr;
	FIntPoint CurrentTextureSize = FIntPoint::ZeroValue;
	FNiagaraParameterDirectBinding<UObject*> UserParamBinding;
};

struct FNDICubeTextureInstanceData_RenderThread
{
	FSamplerStateRHIRef		SamplerStateRHI;
	FTextureReferenceRHIRef	TextureReferenceRHI;
	FTextureRHIRef			ResolvedTextureRHI;
	FIntPoint				TextureSize;
};

struct FNiagaraDataInterfaceProxyCubeTexture : public FNiagaraDataInterfaceProxy
{
	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override { check(false); }
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return 0; }

	virtual void PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context) override
	{
		if (FNDICubeTextureInstanceData_RenderThread* InstanceData = InstanceData_RT.Find(Context.SystemInstanceID))
		{
			// Because the underlying reference can have a switch in flight on the RHI we get the referenced texture
			// here, ensure it's valid (as it could be queued for delete) and cache until next round.  If we were
			// to release the reference in PostStage / PostSimulate we still stand a chance the the transition we
			// queue will be invalid by the time it is processed on the RHI thread.
			if (Context.SimStageData->bFirstStage && InstanceData->TextureReferenceRHI.IsValid())
			{
				InstanceData->ResolvedTextureRHI = InstanceData->TextureReferenceRHI->GetReferencedTexture();
				if (InstanceData->ResolvedTextureRHI && !InstanceData->ResolvedTextureRHI->IsValid())
				{
					InstanceData->ResolvedTextureRHI = nullptr;
				}
			}
			if (InstanceData->ResolvedTextureRHI)
			{
				// Make sure the texture is readable, we don't know where it's coming from.
				RHICmdList.Transition(FRHITransitionInfo(InstanceData->ResolvedTextureRHI, ERHIAccess::Unknown, ERHIAccess::SRVMask));
			}
		}
	}

	TMap<FNiagaraSystemInstanceID, FNDICubeTextureInstanceData_RenderThread> InstanceData_RT;
}; 

UNiagaraDataInterfaceCubeTexture::UNiagaraDataInterfaceCubeTexture(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, Texture(nullptr)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyCubeTexture());

	FNiagaraTypeDefinition Def(UTexture::StaticClass());
	TextureUserParameter.Parameter.SetType(Def);
}

void UNiagaraDataInterfaceCubeTexture::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
	}
}

bool UNiagaraDataInterfaceCubeTexture::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}
	UNiagaraDataInterfaceCubeTexture* DestinationTexture = CastChecked<UNiagaraDataInterfaceCubeTexture>(Destination);
	DestinationTexture->Texture = Texture;
	DestinationTexture->TextureUserParameter = TextureUserParameter;

	return true;
}

bool UNiagaraDataInterfaceCubeTexture::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceCubeTexture* OtherTexture = CastChecked<const UNiagaraDataInterfaceCubeTexture>(Other);
	return
		OtherTexture->Texture == Texture &&
		OtherTexture->TextureUserParameter == TextureUserParameter;
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
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 4);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceCubeTexture, SampleCubeTexture)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == TextureDimsName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 2);
		OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceCubeTexture::GetTextureDimensions);
	}
}

int32 UNiagaraDataInterfaceCubeTexture::PerInstanceDataSize() const
{
	return sizeof(FNDICubeTextureInstanceData_GameThread);
}

bool UNiagaraDataInterfaceCubeTexture::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDICubeTextureInstanceData_GameThread* InstanceData = new (PerInstanceData) FNDICubeTextureInstanceData_GameThread();
	InstanceData->UserParamBinding.Init(SystemInstance->GetInstanceParameters(), TextureUserParameter.Parameter);
	return true;
}

void UNiagaraDataInterfaceCubeTexture::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDICubeTextureInstanceData_GameThread* InstanceData = static_cast<FNDICubeTextureInstanceData_GameThread*>(PerInstanceData);
	InstanceData->~FNDICubeTextureInstanceData_GameThread();

	ENQUEUE_RENDER_COMMAND(NDITexture_RemoveInstance)
	(
		[RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyCubeTexture>(), RT_InstanceID = SystemInstance->GetId()](FRHICommandListImmediate&)
		{
			RT_Proxy->InstanceData_RT.Remove(RT_InstanceID);
		}
	);
}

bool UNiagaraDataInterfaceCubeTexture::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	FNDICubeTextureInstanceData_GameThread* InstanceData = static_cast<FNDICubeTextureInstanceData_GameThread*>(PerInstanceData);

	UTexture* CurrentTexture = InstanceData->UserParamBinding.GetValueOrDefault<UTexture>(Texture);
	if ( InstanceData->CurrentTexture != CurrentTexture )
	{
		UTextureCube* CurrentTextureCube = Cast<UTextureCube>(CurrentTexture);
		UTextureRenderTargetCube* CurrentTextureRT = Cast<UTextureRenderTargetCube>(CurrentTexture);
		if (CurrentTextureCube || CurrentTextureRT)
		{
			const FIntPoint CurrentTextureSize = CurrentTextureCube != nullptr ?
				FIntPoint(CurrentTextureCube->GetSizeX(), CurrentTextureCube->GetSizeY()) :
				FIntPoint(CurrentTextureRT->SizeX, CurrentTextureRT->SizeX);

			InstanceData->CurrentTexture = CurrentTexture;
			InstanceData->CurrentTextureSize = CurrentTextureSize;

			ENQUEUE_RENDER_COMMAND(NDITexture_UpdateInstance)
			(
				[RT_Proxy=GetProxyAs<FNiagaraDataInterfaceProxyCubeTexture>(), RT_InstanceID=SystemInstance->GetId(), RT_Texture=CurrentTexture, RT_TextureSize=CurrentTextureSize](FRHICommandListImmediate&)
				{
					FNDICubeTextureInstanceData_RenderThread& InstanceData = RT_Proxy->InstanceData_RT.FindOrAdd(RT_InstanceID);
					if (RT_Texture)
					{
						InstanceData.TextureReferenceRHI = RT_Texture->TextureReference.TextureReferenceRHI;
						InstanceData.SamplerStateRHI = RT_Texture->GetResource() ? RT_Texture->GetResource()->SamplerStateRHI : nullptr;
					}
					else
					{
						InstanceData.TextureReferenceRHI = nullptr;
						InstanceData.SamplerStateRHI = nullptr;
					}
					InstanceData.TextureSize = RT_TextureSize;
				}
			);
		}
	}
	return false;
}

void UNiagaraDataInterfaceCubeTexture::GetTextureDimensions(FVectorVMExternalFunctionContext& Context)
{
	VectorVM::FUserPtrHandler<FNDICubeTextureInstanceData_GameThread> InstData(Context);
	FNDIOutputParam<int32> OutWidth(Context);
	FNDIOutputParam<int32> OutHeight(Context);

	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		OutWidth.SetAndAdvance(InstData->CurrentTextureSize.X);
		OutHeight.SetAndAdvance(InstData->CurrentTextureSize.Y);
	}
}

void UNiagaraDataInterfaceCubeTexture::SampleCubeTexture(FVectorVMExternalFunctionContext& Context)
{
	VectorVM::FUserPtrHandler<FNDICubeTextureInstanceData_GameThread> InstData(Context);
	FNDIInputParam<FVector3f> InCoord(Context);
	FNDIInputParam<float> InMipLevel(Context);
	FNDIOutputParam<FVector4f> OutColor(Context);

	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		OutColor.SetAndAdvance(FVector4f(1.0f, 0.0f, 1.0f, 1.0f));
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
		FNDICubeTextureInstanceData_RenderThread* InstanceData = TextureDI->InstanceData_RT.Find(Context.SystemInstanceID);

		if (InstanceData && InstanceData->ResolvedTextureRHI.IsValid())
		{
			FRHISamplerState* SamplerStateRHI = InstanceData->SamplerStateRHI ? InstanceData->SamplerStateRHI : GBlackTexture->SamplerStateRHI;
			SetTextureParameter(
				RHICmdList,
				ComputeShaderRHI,
				TextureParam,
				SamplerParam,
				SamplerStateRHI,
				InstanceData->ResolvedTextureRHI
			);
			SetShaderValue(RHICmdList, ComputeShaderRHI, Dimensions, InstanceData->TextureSize);
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
			SetShaderValue(RHICmdList, ComputeShaderRHI, Dimensions, FVector3f::ZeroVector);
		}
	}
private:
	LAYOUT_FIELD(FShaderResourceParameter, TextureParam);
	LAYOUT_FIELD(FShaderResourceParameter, SamplerParam);
	LAYOUT_FIELD(FShaderParameter, Dimensions);
};

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_CubeTexture);
IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceCubeTexture, FNiagaraDataInterfaceParametersCS_CubeTexture);

void UNiagaraDataInterfaceCubeTexture::SetTexture(UTextureCube* InTexture)
{
	if (InTexture)
	{
		Texture = InTexture;
	}
}

#undef LOCTEXT_NAMESPACE
