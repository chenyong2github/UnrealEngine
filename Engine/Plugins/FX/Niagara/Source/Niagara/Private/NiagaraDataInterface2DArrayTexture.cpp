// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterface2DArrayTexture.h"
#include "NiagaraComputeExecutionContext.h"
#include "NiagaraShader.h"
#include "NiagaraShaderParametersBuilder.h"
#include "NiagaraSystemInstance.h"

#include "Engine/Texture2DArray.h"
#include "Engine/TextureRenderTarget2DArray.h"
#include "ShaderParameterUtils.h"

#define LOCTEXT_NAMESPACE "UNiagaraDataInterface2DArrayTexture"

const TCHAR* UNiagaraDataInterface2DArrayTexture::TemplateShaderFilePath = TEXT("/Plugin/FX/Niagara/Private/NiagaraDataInterfaceTexture2DArrayTemplate.ush");
const FName UNiagaraDataInterface2DArrayTexture::SampleTextureName(TEXT("SampleTexture"));
const FName UNiagaraDataInterface2DArrayTexture::TextureDimsName(TEXT("TextureDimensions"));

struct FNDITexture2DArrayInstanceData_GameThread
{
	TWeakObjectPtr<UTexture> CurrentTexture = nullptr;
	FIntVector CurrentTextureSize = FIntVector::ZeroValue;
	FNiagaraParameterDirectBinding<UObject*> UserParamBinding;
};

struct FNDITexture2DArrayInstanceData_RenderThread
{
	FSamplerStateRHIRef		SamplerStateRHI;
	FTextureReferenceRHIRef	TextureReferenceRHI;
	FTextureRHIRef			ResolvedTextureRHI;
	FVector3f				TextureSize;
};

struct FNiagaraDataInterfaceProxyTexture2DArray : public FNiagaraDataInterfaceProxy
{
	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override { check(false); }
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return 0; }

	virtual void PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context) override
	{
		if (FNDITexture2DArrayInstanceData_RenderThread* InstanceData = InstanceData_RT.Find(Context.SystemInstanceID))
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

	TMap<FNiagaraSystemInstanceID, FNDITexture2DArrayInstanceData_RenderThread> InstanceData_RT;
};

UNiagaraDataInterface2DArrayTexture::UNiagaraDataInterface2DArrayTexture(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, Texture(nullptr)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyTexture2DArray());

	FNiagaraTypeDefinition Def(UTexture::StaticClass());
	TextureUserParameter.Parameter.SetType(Def);
}

void UNiagaraDataInterface2DArrayTexture::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
	}
}

bool UNiagaraDataInterface2DArrayTexture::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}
	UNiagaraDataInterface2DArrayTexture* DestinationTexture = CastChecked<UNiagaraDataInterface2DArrayTexture>(Destination);
	DestinationTexture->Texture = Texture;
	DestinationTexture->TextureUserParameter = TextureUserParameter;

	return true;
}

bool UNiagaraDataInterface2DArrayTexture::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterface2DArrayTexture* OtherTexture = CastChecked<const UNiagaraDataInterface2DArrayTexture>(Other);
	return
		OtherTexture->Texture == Texture &&
		OtherTexture->TextureUserParameter == TextureUserParameter;
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
		Sig.SetDescription(LOCTEXT("TextureSample2DArrayTextureDesc", "Sample the specified mip level of the input texture at the specified UVW coordinates. Where W is the slice to sample (0,1,2, etc) and UV are the coordinates into the slice."));
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
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 3);
		OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterface2DArrayTexture::GetTextureDimensions);
	}
}

int32 UNiagaraDataInterface2DArrayTexture::PerInstanceDataSize() const
{
	return sizeof(FNDITexture2DArrayInstanceData_GameThread);
}

bool UNiagaraDataInterface2DArrayTexture::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDITexture2DArrayInstanceData_GameThread* InstanceData = new (PerInstanceData) FNDITexture2DArrayInstanceData_GameThread();
	InstanceData->UserParamBinding.Init(SystemInstance->GetInstanceParameters(), TextureUserParameter.Parameter);
	return true;
}

void UNiagaraDataInterface2DArrayTexture::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDITexture2DArrayInstanceData_GameThread* InstanceData = static_cast<FNDITexture2DArrayInstanceData_GameThread*>(PerInstanceData);
	InstanceData->~FNDITexture2DArrayInstanceData_GameThread();

	ENQUEUE_RENDER_COMMAND(NDITexture_RemoveInstance)
	(
		[RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyTexture2DArray>(), RT_InstanceID = SystemInstance->GetId()](FRHICommandListImmediate&)
		{
			RT_Proxy->InstanceData_RT.Remove(RT_InstanceID);
		}
	);
}

bool UNiagaraDataInterface2DArrayTexture::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	FNDITexture2DArrayInstanceData_GameThread* InstanceData = static_cast<FNDITexture2DArrayInstanceData_GameThread*>(PerInstanceData);

	UTexture* CurrentTexture = InstanceData->UserParamBinding.GetValueOrDefault<UTexture>(Texture);
	if ( InstanceData->CurrentTexture != CurrentTexture )
	{
		UTexture2DArray* CurrentTextureArray = Cast<UTexture2DArray>(CurrentTexture);
		UTextureRenderTarget2DArray* CurrentTextureRT = Cast<UTextureRenderTarget2DArray>(CurrentTexture);
		if (CurrentTextureArray || CurrentTextureRT)
		{
			const FIntVector CurrentTextureSize = CurrentTextureArray ?
				FIntVector(CurrentTextureArray->GetSizeX(), CurrentTextureArray->GetSizeY(), CurrentTextureArray->GetArraySize()) :
				FIntVector(CurrentTextureRT->SizeX, CurrentTextureRT->SizeY, CurrentTextureRT->Slices);

			InstanceData->CurrentTexture = CurrentTexture;
			InstanceData->CurrentTextureSize = CurrentTextureSize;

			ENQUEUE_RENDER_COMMAND(NDITexture_UpdateInstance)
			(
				[RT_Proxy=GetProxyAs<FNiagaraDataInterfaceProxyTexture2DArray>(), RT_InstanceID=SystemInstance->GetId(), RT_Texture=CurrentTexture, RT_TextureSize=CurrentTextureSize](FRHICommandListImmediate&)
				{
					FNDITexture2DArrayInstanceData_RenderThread& InstanceData = RT_Proxy->InstanceData_RT.FindOrAdd(RT_InstanceID);
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

void UNiagaraDataInterface2DArrayTexture::GetTextureDimensions(FVectorVMExternalFunctionContext& Context)
{
	VectorVM::FUserPtrHandler<FNDITexture2DArrayInstanceData_GameThread> InstData(Context);
	FNDIOutputParam<FVector3f> DimensionsOut(Context);

	const FVector FloatTextureSize(InstData->CurrentTextureSize.X, InstData->CurrentTextureSize.Y, InstData->CurrentTextureSize.Z);
	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		DimensionsOut.SetAndAdvance((FVector3f)FloatTextureSize);
	}
}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterface2DArrayTexture::AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const
{
	bool bSuccess = Super::AppendCompileHash(InVisitor);
	InVisitor->UpdateString(TEXT("UNiagaraDataInterface2DArrayTextureHLSLSource"), GetShaderFileHash(TemplateShaderFilePath, EShaderPlatform::SP_PCD3D_SM5).ToString());
	bSuccess &= InVisitor->UpdateShaderParameters<FShaderParameters>();
	return bSuccess;
}

void UNiagaraDataInterface2DArrayTexture::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	TMap<FString, FStringFormatArg> TemplateArgs =
	{
		{TEXT("ParameterName"),	ParamInfo.DataInterfaceHLSLSymbol},
	};

	FString TemplateFile;
	LoadShaderSourceFile(TemplateShaderFilePath, EShaderPlatform::SP_PCD3D_SM5, &TemplateFile, nullptr);
	OutHLSL += FString::Format(*TemplateFile, TemplateArgs);
}

bool UNiagaraDataInterface2DArrayTexture::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	if ((FunctionInfo.DefinitionName == SampleTextureName) ||
		(FunctionInfo.DefinitionName == TextureDimsName) )
	{
		return true;
	}
	return false;
}
#endif

void UNiagaraDataInterface2DArrayTexture::BuildShaderParameters(FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const
{
	ShaderParametersBuilder.AddNestedStruct<FShaderParameters>();
}

void UNiagaraDataInterface2DArrayTexture::SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext& Context) const
{
	const FNiagaraDataInterfaceProxyTexture2DArray& TextureProxy = Context.GetProxy<FNiagaraDataInterfaceProxyTexture2DArray>();
	const FNDITexture2DArrayInstanceData_RenderThread* InstanceData = TextureProxy.InstanceData_RT.Find(Context.GetSystemInstanceID());

	FShaderParameters* Parameters = Context.GetParameterNestedStruct<FShaderParameters>();
	if (InstanceData && InstanceData->ResolvedTextureRHI.IsValid())
	{
		Parameters->TextureSize		= InstanceData->TextureSize;
		Parameters->Texture			= InstanceData->ResolvedTextureRHI;
		Parameters->TextureSampler	= InstanceData->SamplerStateRHI ? InstanceData->SamplerStateRHI : GBlackArrayTexture->SamplerStateRHI;
	}
	else
	{
		Parameters->TextureSize		= FVector3f::ZeroVector;
		Parameters->Texture			= GBlackArrayTexture->TextureRHI;
		Parameters->TextureSampler	= GBlackArrayTexture->SamplerStateRHI;
	}
}

void UNiagaraDataInterface2DArrayTexture::SetTexture(UTexture2DArray* InTexture)
{
	if (InTexture)
	{
		Texture = InTexture;
	}
}

#undef LOCTEXT_NAMESPACE
