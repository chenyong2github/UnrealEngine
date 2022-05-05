// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceVolumeTexture.h"
#include "NiagaraComputeExecutionContext.h"
#include "NiagaraShader.h"
#include "NiagaraShaderParametersBuilder.h"
#include "NiagaraSystemInstance.h"

#include "Engine/TextureRenderTargetVolume.h"
#include "Engine/VolumeTexture.h"
#include "ShaderParameterUtils.h"

#define LOCTEXT_NAMESPACE "UNiagaraDataInterfaceVolumeTexture"

const TCHAR* UNiagaraDataInterfaceVolumeTexture::TemplateShaderFilePath = TEXT("/Plugin/FX/Niagara/Private/NiagaraDataInterfaceVolumeTextureTemplate.ush");
const FName UNiagaraDataInterfaceVolumeTexture::SampleVolumeTextureName(TEXT("SampleVolumeTexture"));
const FName UNiagaraDataInterfaceVolumeTexture::TextureDimsName(TEXT("TextureDimensions3D"));

struct FNDIVolumeTextureInstanceData_GameThread
{
	TWeakObjectPtr<UTexture> CurrentTexture = nullptr;
	FIntVector CurrentTextureSize = FIntVector::ZeroValue;
	FNiagaraParameterDirectBinding<UObject*> UserParamBinding;
};

struct FNDIVolumeTextureInstanceData_RenderThread
{
	FSamplerStateRHIRef		SamplerStateRHI;
	FTextureReferenceRHIRef	TextureReferenceRHI;
	FTextureRHIRef			ResolvedTextureRHI;
	FVector3f				TextureSize;
};

struct FNiagaraDataInterfaceProxyVolumeTexture : public FNiagaraDataInterfaceProxy
{
	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override { check(false); }
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return 0; }

	virtual void PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context) override
	{
		if (FNDIVolumeTextureInstanceData_RenderThread* InstanceData = InstanceData_RT.Find(Context.SystemInstanceID))
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

	TMap<FNiagaraSystemInstanceID, FNDIVolumeTextureInstanceData_RenderThread> InstanceData_RT;
};

UNiagaraDataInterfaceVolumeTexture::UNiagaraDataInterfaceVolumeTexture(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, Texture(nullptr)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyVolumeTexture());

	FNiagaraTypeDefinition Def(UTexture::StaticClass());
	TextureUserParameter.Parameter.SetType(Def);
}

void UNiagaraDataInterfaceVolumeTexture::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
	}
}

bool UNiagaraDataInterfaceVolumeTexture::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}
	UNiagaraDataInterfaceVolumeTexture* DestinationTexture = CastChecked<UNiagaraDataInterfaceVolumeTexture>(Destination);
	DestinationTexture->Texture = Texture;
	DestinationTexture->TextureUserParameter = TextureUserParameter;

	return true;
}

bool UNiagaraDataInterfaceVolumeTexture::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceVolumeTexture* OtherTexture = CastChecked<const UNiagaraDataInterfaceVolumeTexture>(Other);
	return	
		OtherTexture->Texture == Texture &&
		OtherTexture->TextureUserParameter == TextureUserParameter;
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
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 4);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceVolumeTexture, SampleVolumeTexture)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == TextureDimsName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 3);
		OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceVolumeTexture::GetTextureDimensions);
	}
}

int32 UNiagaraDataInterfaceVolumeTexture::PerInstanceDataSize() const
{
	return sizeof(FNDIVolumeTextureInstanceData_GameThread);
}

bool UNiagaraDataInterfaceVolumeTexture::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIVolumeTextureInstanceData_GameThread* InstanceData = new (PerInstanceData) FNDIVolumeTextureInstanceData_GameThread();
	InstanceData->UserParamBinding.Init(SystemInstance->GetInstanceParameters(), TextureUserParameter.Parameter);
	return true;
}

void UNiagaraDataInterfaceVolumeTexture::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIVolumeTextureInstanceData_GameThread* InstanceData = static_cast<FNDIVolumeTextureInstanceData_GameThread*>(PerInstanceData);
	InstanceData->~FNDIVolumeTextureInstanceData_GameThread();

	ENQUEUE_RENDER_COMMAND(NDITexture_RemoveInstance)
	(
		[RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyVolumeTexture>(), RT_InstanceID = SystemInstance->GetId()](FRHICommandListImmediate&)
		{
			RT_Proxy->InstanceData_RT.Remove(RT_InstanceID);
		}
	);
}

bool UNiagaraDataInterfaceVolumeTexture::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	FNDIVolumeTextureInstanceData_GameThread* InstanceData = static_cast<FNDIVolumeTextureInstanceData_GameThread*>(PerInstanceData);

	UTexture* CurrentTexture = InstanceData->UserParamBinding.GetValueOrDefault<UTexture>(Texture);
	if ( InstanceData->CurrentTexture != CurrentTexture )
	{
		UVolumeTexture* CurrentTextureVolume = Cast<UVolumeTexture>(CurrentTexture);
		UTextureRenderTargetVolume* CurrentTextureRT = Cast<UTextureRenderTargetVolume>(CurrentTexture);
		if (CurrentTextureVolume || CurrentTextureRT)
		{
			const FIntVector CurrentTextureSize = CurrentTextureVolume != nullptr ?
				FIntVector(CurrentTextureVolume->GetSizeX(), CurrentTextureVolume->GetSizeY(), CurrentTextureVolume->GetSizeZ()) :
				FIntVector(CurrentTextureRT->SizeX, CurrentTextureRT->SizeY, CurrentTextureRT->SizeZ);

			InstanceData->CurrentTexture = CurrentTexture;
			InstanceData->CurrentTextureSize = CurrentTextureSize;

			ENQUEUE_RENDER_COMMAND(NDITexture_UpdateInstance)
			(
				[RT_Proxy=GetProxyAs<FNiagaraDataInterfaceProxyVolumeTexture>(), RT_InstanceID=SystemInstance->GetId(), RT_Texture=CurrentTexture, RT_TextureSize=CurrentTextureSize](FRHICommandListImmediate&)
				{
					FNDIVolumeTextureInstanceData_RenderThread& InstanceData = RT_Proxy->InstanceData_RT.FindOrAdd(RT_InstanceID);
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
					InstanceData.TextureSize = FVector3f(RT_TextureSize.X, RT_TextureSize.Y, RT_TextureSize.Z);
				}
			);
		}
	}
	return false;
}

void UNiagaraDataInterfaceVolumeTexture::GetTextureDimensions(FVectorVMExternalFunctionContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIVolumeTextureInstanceData_GameThread> InstData(Context);
	FNDIOutputParam<float> OutWidth(Context);
	FNDIOutputParam<float> OutHeight(Context);
	FNDIOutputParam<float> OutDepth(Context);

	FVector3f FloatTextureSize(InstData->CurrentTextureSize.X, InstData->CurrentTextureSize.Y, InstData->CurrentTextureSize.Z);
	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		OutWidth.SetAndAdvance(FloatTextureSize.X);
		OutHeight.SetAndAdvance(FloatTextureSize.Y);
		OutDepth.SetAndAdvance(FloatTextureSize.Z);
	}
}

void UNiagaraDataInterfaceVolumeTexture::SampleVolumeTexture(FVectorVMExternalFunctionContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIVolumeTextureInstanceData_GameThread> InstData(Context);
	VectorVM::FExternalFuncInputHandler<float> XParam(Context);
	VectorVM::FExternalFuncInputHandler<float> YParam(Context);
	VectorVM::FExternalFuncInputHandler<float> ZParam(Context);
	VectorVM::FExternalFuncInputHandler<float> MipLevelParam(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleR(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleG(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleB(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleA(Context);

	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
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
bool UNiagaraDataInterfaceVolumeTexture::AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const
{
	bool bSuccess = Super::AppendCompileHash(InVisitor);
	InVisitor->UpdateString(TEXT("UNiagaraDataInterfaceVolumeTextureHLSLSource"), GetShaderFileHash(TemplateShaderFilePath, EShaderPlatform::SP_PCD3D_SM5).ToString());
	bSuccess &= InVisitor->UpdateShaderParameters<FShaderParameters>();
	return bSuccess;
}

void UNiagaraDataInterfaceVolumeTexture::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	TMap<FString, FStringFormatArg> TemplateArgs =
	{
		{TEXT("ParameterName"),	ParamInfo.DataInterfaceHLSLSymbol},
	};

	FString TemplateFile;
	LoadShaderSourceFile(TemplateShaderFilePath, EShaderPlatform::SP_PCD3D_SM5, &TemplateFile, nullptr);
	OutHLSL += FString::Format(*TemplateFile, TemplateArgs);
}

bool UNiagaraDataInterfaceVolumeTexture::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	if ((FunctionInfo.DefinitionName == SampleVolumeTextureName) ||
		(FunctionInfo.DefinitionName == TextureDimsName) )
	{
		return true;
	}
	return false;
}
#endif

void UNiagaraDataInterfaceVolumeTexture::BuildShaderParameters(FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const
{
	ShaderParametersBuilder.AddNestedStruct<FShaderParameters>();
}

void UNiagaraDataInterfaceVolumeTexture::SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext& Context) const
{
	const FNiagaraDataInterfaceProxyVolumeTexture& TextureProxy = Context.GetProxy<FNiagaraDataInterfaceProxyVolumeTexture>();
	const FNDIVolumeTextureInstanceData_RenderThread* InstanceData = TextureProxy.InstanceData_RT.Find(Context.GetSystemInstanceID());

	FShaderParameters* Parameters = Context.GetParameterNestedStruct<FShaderParameters>();
	if (InstanceData && InstanceData->ResolvedTextureRHI.IsValid())
	{
		Parameters->TextureSize		= InstanceData->TextureSize;
		Parameters->Texture			= InstanceData->ResolvedTextureRHI;
		Parameters->TextureSampler	= InstanceData->SamplerStateRHI ? InstanceData->SamplerStateRHI : GBlackVolumeTexture->SamplerStateRHI;
	}
	else
	{
		Parameters->TextureSize		= FVector3f::ZeroVector;
		Parameters->Texture			= GBlackVolumeTexture->TextureRHI;
		Parameters->TextureSampler	= GBlackVolumeTexture->SamplerStateRHI;
	}
}

void UNiagaraDataInterfaceVolumeTexture::SetTexture(UVolumeTexture* InTexture)
{
	if (InTexture)
	{
		Texture = InTexture;
	}
}

#undef LOCTEXT_NAMESPACE
