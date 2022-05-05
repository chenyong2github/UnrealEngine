// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceCubeTexture.h"
#include "NiagaraComputeExecutionContext.h"
#include "NiagaraShader.h"
#include "NiagaraShaderParametersBuilder.h"
#include "NiagaraSystemInstance.h"

#include "Engine/TextureCube.h"
#include "Engine/TextureRenderTargetCube.h"
#include "ShaderParameterUtils.h"

#define LOCTEXT_NAMESPACE "UNiagaraDataInterfaceCubeTexture"

const TCHAR* UNiagaraDataInterfaceCubeTexture::TemplateShaderFilePath = TEXT("/Plugin/FX/Niagara/Private/NiagaraDataInterfaceCubeTextureTemplate.ush");
const FName UNiagaraDataInterfaceCubeTexture::SampleCubeTextureName(TEXT("SampleCubeTexture"));
const FName UNiagaraDataInterfaceCubeTexture::TextureDimsName(TEXT("TextureDimensions"));

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
bool UNiagaraDataInterfaceCubeTexture::AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const
{
	bool bSuccess = Super::AppendCompileHash(InVisitor);
	InVisitor->UpdateString(TEXT("UNiagaraDataInterfaceCubeTextureHLSLSource"), GetShaderFileHash(TemplateShaderFilePath, EShaderPlatform::SP_PCD3D_SM5).ToString());
	bSuccess &= InVisitor->UpdateShaderParameters<FShaderParameters>();
	return bSuccess;
}

void UNiagaraDataInterfaceCubeTexture::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	TMap<FString, FStringFormatArg> TemplateArgs =
	{
		{TEXT("ParameterName"),	ParamInfo.DataInterfaceHLSLSymbol},
	};

	FString TemplateFile;
	LoadShaderSourceFile(TemplateShaderFilePath, EShaderPlatform::SP_PCD3D_SM5, &TemplateFile, nullptr);
	OutHLSL += FString::Format(*TemplateFile, TemplateArgs);
}

bool UNiagaraDataInterfaceCubeTexture::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	if ((FunctionInfo.DefinitionName == SampleCubeTextureName) ||
		(FunctionInfo.DefinitionName == TextureDimsName))
	{
		return true;
	}
	return false;
}
#endif

void UNiagaraDataInterfaceCubeTexture::BuildShaderParameters(FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const
{
	ShaderParametersBuilder.AddNestedStruct<FShaderParameters>();
}

void UNiagaraDataInterfaceCubeTexture::SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext& Context) const
{
	const FNiagaraDataInterfaceProxyCubeTexture& TextureProxy = Context.GetProxy<FNiagaraDataInterfaceProxyCubeTexture>();
	const FNDICubeTextureInstanceData_RenderThread* InstanceData = TextureProxy.InstanceData_RT.Find(Context.GetSystemInstanceID());

	FShaderParameters* Parameters = Context.GetParameterNestedStruct<FShaderParameters>();
	if (InstanceData && InstanceData->ResolvedTextureRHI.IsValid())
	{
		Parameters->TextureSize		= InstanceData->TextureSize;
		Parameters->Texture			= InstanceData->ResolvedTextureRHI;
		Parameters->TextureSampler	= InstanceData->SamplerStateRHI ? InstanceData->SamplerStateRHI : GBlackTextureCube->SamplerStateRHI;
	}
	else
	{
		Parameters->TextureSize		= FIntPoint::ZeroValue;
		Parameters->Texture			= GBlackTextureCube->TextureRHI;
		Parameters->TextureSampler	= GBlackTextureCube->SamplerStateRHI;
	}
}

void UNiagaraDataInterfaceCubeTexture::SetTexture(UTextureCube* InTexture)
{
	if (InTexture)
	{
		Texture = InTexture;
	}
}

#undef LOCTEXT_NAMESPACE
