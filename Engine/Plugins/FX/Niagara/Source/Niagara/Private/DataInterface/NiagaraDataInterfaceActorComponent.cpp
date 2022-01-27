// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceActorComponent.h"
#include "NiagaraTypes.h"
#include "NiagaraCustomVersion.h"
#include "NiagaraSystemInstance.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "ShaderCompilerCore.h"
#include "ShaderParameterUtils.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceActorComponent"

struct FNiagaraActorDIFunctionVersion
{
	enum Type
	{
		InitialVersion = 0,
		LWCConversion = 1,

		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};
};

namespace NDIActorComponentLocal
{
	static const TCHAR* TemplateShaderFile = TEXT("/Plugin/FX/Niagara/Private/NiagaraDataInterfaceActorComponentTemplate.ush");

	static const FName	GetMatrixName(TEXT("GetMatrix"));
	static const FName	GetTransformName(TEXT("GetTransform"));

	static const FString ValidString(TEXT("Valid"));
	static const FString MatrixString(TEXT("Matrix"));
	static const FString RotationString(TEXT("Rotation"));
	static const FString ScaleString(TEXT("Scale"));

	struct FInstanceData_GameThread
	{
		FNiagaraParameterDirectBinding<UObject*>	UserParamBinding;
		bool										bCachedValid = false;
		FTransform									CachedTransform = FTransform::Identity;
	};

	struct FGameToRenderInstanceData
	{
		bool		bCachedValid = false;
		FTransform	CachedTransform = FTransform::Identity;
	};

	struct FInstanceData_RenderThread
	{
		bool		bCachedValid = false;
		FTransform	CachedTransform = FTransform::Identity;
	};

	struct FNDIProxy : public FNiagaraDataInterfaceProxy
	{
		virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return sizeof(FGameToRenderInstanceData); }

		static void ProvidePerInstanceDataForRenderThread(void* InDataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance)
		{
			const FInstanceData_GameThread* InstanceData = reinterpret_cast<FInstanceData_GameThread*>(PerInstanceData);
			FGameToRenderInstanceData* DataForRenderThread = reinterpret_cast<FGameToRenderInstanceData*>(InDataForRenderThread);
			DataForRenderThread->bCachedValid = InstanceData->bCachedValid;
			DataForRenderThread->CachedTransform = InstanceData->CachedTransform;
		}

		virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& InstanceID) override
		{
			FGameToRenderInstanceData* InstanceDataFromGT = reinterpret_cast<FGameToRenderInstanceData*>(PerInstanceData);

			FInstanceData_RenderThread& InstanceData = SystemInstancesToInstanceData_RT.FindOrAdd(InstanceID);
			InstanceData.bCachedValid = InstanceDataFromGT->bCachedValid;
			InstanceData.CachedTransform = InstanceDataFromGT->CachedTransform;
		}

		TMap<FNiagaraSystemInstanceID, FInstanceData_RenderThread> SystemInstancesToInstanceData_RT;
	};
}

//////////////////////////////////////////////////////////////////////////
// Compute Shader Binding
struct FNDIActorComponentCS : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNDIActorComponentCS, NonVirtual);

public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{
		using namespace NDIActorComponentLocal;

		ValidParam.Bind(ParameterMap, *(NDIActorComponentLocal::ValidString + TEXT("_") + ParameterInfo.DataInterfaceHLSLSymbol));
		MatrixParam.Bind(ParameterMap, *(NDIActorComponentLocal::MatrixString + TEXT("_") + ParameterInfo.DataInterfaceHLSLSymbol));
		RotationParam.Bind(ParameterMap, *(NDIActorComponentLocal::RotationString + TEXT("_") + ParameterInfo.DataInterfaceHLSLSymbol));
		ScaleParam.Bind(ParameterMap, *(NDIActorComponentLocal::ScaleString + TEXT("_") + ParameterInfo.DataInterfaceHLSLSymbol));
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		using namespace NDIActorComponentLocal;

		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
		FNDIProxy* DataInterfaceProxy = static_cast<FNDIProxy*>(Context.DataInterface);
		FInstanceData_RenderThread* InstanceData = DataInterfaceProxy->SystemInstancesToInstanceData_RT.Find(Context.SystemInstanceID);
		check(InstanceData != nullptr);

		const FMatrix44f InstanceMatrix = (FMatrix44f)InstanceData->CachedTransform.ToMatrixWithScale();
		const FQuat4f InstanceRotation = (FQuat4f)InstanceData->CachedTransform.GetRotation();
		const FVector3f InstanceScale = (FVector3f)InstanceData->CachedTransform.GetScale3D();
		SetShaderValue(RHICmdList, ComputeShaderRHI, ValidParam, InstanceData->bCachedValid ? 1 : 0);
		SetShaderValue(RHICmdList, ComputeShaderRHI, MatrixParam, InstanceMatrix);
		SetShaderValue(RHICmdList, ComputeShaderRHI, RotationParam, InstanceRotation);
		SetShaderValue(RHICmdList, ComputeShaderRHI, ScaleParam, InstanceScale);
	}

private:
	LAYOUT_FIELD(FShaderParameter, ValidParam);
	LAYOUT_FIELD(FShaderParameter, MatrixParam);
	LAYOUT_FIELD(FShaderParameter, RotationParam);
	LAYOUT_FIELD(FShaderParameter, ScaleParam);
};

IMPLEMENT_TYPE_LAYOUT(FNDIActorComponentCS);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceActorComponent, FNDIActorComponentCS);

//////////////////////////////////////////////////////////////////////////
// Data Interface
UNiagaraDataInterfaceActorComponent::UNiagaraDataInterfaceActorComponent(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Proxy.Reset(new NDIActorComponentLocal::FNDIProxy());

	FNiagaraTypeDefinition Def(UObject::StaticClass());
	ActorOrComponentParameter.Parameter.SetType(Def);
}

void UNiagaraDataInterfaceActorComponent::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
	}
}

UActorComponent* UNiagaraDataInterfaceActorComponent::ResolveComponent(const void* PerInstanceData) const
{
	using namespace NDIActorComponentLocal;

	FInstanceData_GameThread* InstanceData = (FInstanceData_GameThread*)PerInstanceData;
	if (UObject* ObjectBinding = InstanceData->UserParamBinding.GetValue())
	{
		if (UActorComponent* ComponentBinding = Cast<UActorComponent>(ObjectBinding))
		{
			return ComponentBinding;
		}
		else if (AActor* ActorBinding = Cast<AActor>(ObjectBinding))
		{
			return ActorBinding->GetRootComponent();
		}
	}

	return SourceActor.IsValid() ? SourceActor->GetRootComponent() : nullptr;
}

void UNiagaraDataInterfaceActorComponent::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	using namespace NDIActorComponentLocal;
	{
		FNiagaraFunctionSignature& FunctionSignature = OutFunctions.AddDefaulted_GetRef();
		FunctionSignature.Name = GetMatrixName;
		FunctionSignature.SetDescription(LOCTEXT("GetMatrix", "Returns the current matrix for the component if valid."));
		FunctionSignature.SetFunctionVersion(FNiagaraActorDIFunctionVersion::LatestVersion);
		FunctionSignature.bMemberFunction = true;
		FunctionSignature.bRequiresContext = false;
		FunctionSignature.bSupportsGPU = true;
		FunctionSignature.Inputs.Emplace(FNiagaraTypeDefinition(GetClass()), TEXT("ActorComponent"));
		FunctionSignature.Outputs.Emplace(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsValid"));
		FunctionSignature.Outputs.Emplace(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Matrix"));
	}
	{
		FNiagaraFunctionSignature& FunctionSignature = OutFunctions.AddDefaulted_GetRef();
		FunctionSignature.Name = GetTransformName;
		FunctionSignature.SetDescription(LOCTEXT("GetTransform", "Returns the current transform for the component if valid."));
		FunctionSignature.SetFunctionVersion(FNiagaraActorDIFunctionVersion::LatestVersion);
		FunctionSignature.bMemberFunction = true;
		FunctionSignature.bRequiresContext = false;
		FunctionSignature.bSupportsGPU = true;
		FunctionSignature.Inputs.Emplace(FNiagaraTypeDefinition(GetClass()), TEXT("ActorComponent"));
		FunctionSignature.Outputs.Emplace(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsValid"));
		FunctionSignature.Outputs.Emplace(FNiagaraTypeDefinition::GetPositionDef(), TEXT("Position"));
		FunctionSignature.Outputs.Emplace(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Rotation"));
		FunctionSignature.Outputs.Emplace(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Scale"));
	}
}

void UNiagaraDataInterfaceActorComponent::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc)
{
	using namespace NDIActorComponentLocal;
	if (BindingInfo.Name == GetMatrixName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->VMGetMatrix(Context); });
	}
	else if (BindingInfo.Name == GetTransformName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->VMGetTransform(Context); });
	}
}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfaceActorComponent::AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const
{
	bool bSuccess = Super::AppendCompileHash(InVisitor);
	FSHAHash Hash = GetShaderFileHash(NDIActorComponentLocal::TemplateShaderFile, EShaderPlatform::SP_PCD3D_SM5);
	InVisitor->UpdateString(TEXT("NiagaraDataInterfaceActorComponentTemplateHLSLSource"), Hash.ToString());
	return bSuccess;
}

void UNiagaraDataInterfaceActorComponent::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	TMap<FString, FStringFormatArg> TemplateArgs =
	{
		{TEXT("ParameterName"),	ParamInfo.DataInterfaceHLSLSymbol},
	};

	FString TemplateFile;
	LoadShaderSourceFile(NDIActorComponentLocal::TemplateShaderFile, EShaderPlatform::SP_PCD3D_SM5, &TemplateFile, nullptr);
	OutHLSL += FString::Format(*TemplateFile, TemplateArgs);
}

bool UNiagaraDataInterfaceActorComponent::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	using namespace NDIActorComponentLocal;
	return (FunctionInfo.DefinitionName == GetMatrixName) || (FunctionInfo.DefinitionName == GetTransformName);
}

bool UNiagaraDataInterfaceActorComponent::UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature)
{
	// LWC upgrades
	if (FunctionSignature.FunctionVersion < FNiagaraActorDIFunctionVersion::LWCConversion)
	{
		TArray<FNiagaraFunctionSignature> AllFunctions;
		GetFunctions(AllFunctions);
		for (const FNiagaraFunctionSignature& Sig : AllFunctions)
		{
			if (FunctionSignature.Name == Sig.Name)
			{
				FunctionSignature = Sig;
				return true;
			}
		}
	}
	return false;
}
#endif

bool UNiagaraDataInterfaceActorComponent::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	using namespace NDIActorComponentLocal;

	FInstanceData_GameThread* InstanceData = new (PerInstanceData) FInstanceData_GameThread;
	InstanceData->UserParamBinding.Init(SystemInstance->GetInstanceParameters(), ActorOrComponentParameter.Parameter);

	return true;
}

void UNiagaraDataInterfaceActorComponent::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	using namespace NDIActorComponentLocal;

	FInstanceData_GameThread* InstanceData = (FInstanceData_GameThread*)PerInstanceData;
	InstanceData->~FInstanceData_GameThread();

	ENQUEUE_RENDER_COMMAND(RemoveProxy)
	(
		[RT_Proxy=GetProxyAs<FNDIProxy>(), InstanceID=SystemInstance->GetId()](FRHICommandListImmediate& CmdList)
		{
			RT_Proxy->SystemInstancesToInstanceData_RT.Remove(InstanceID);
		}
	);
}

int32 UNiagaraDataInterfaceActorComponent::PerInstanceDataSize() const
{
	return sizeof(NDIActorComponentLocal::FInstanceData_GameThread);
}

bool UNiagaraDataInterfaceActorComponent::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	using namespace NDIActorComponentLocal;

	check(SystemInstance);
	FInstanceData_GameThread* InstanceData = (FInstanceData_GameThread*)PerInstanceData;
	if (!InstanceData)
	{
		return true;
	}

	InstanceData->bCachedValid = false;
	InstanceData->CachedTransform = FTransform::Identity;
	if ( UActorComponent* ActorComponent = ResolveComponent(PerInstanceData) )
	{
		if (USceneComponent* SceneComponent = Cast<USceneComponent>(ActorComponent) )
		{
			InstanceData->bCachedValid = true;
			InstanceData->CachedTransform = SceneComponent->GetComponentToWorld();
			InstanceData->CachedTransform.AddToTranslation(FVector(SystemInstance->GetLWCTile()) * -FLargeWorldRenderScalar::GetTileSize());
		}
		else if (AActor* OwnerActor = ActorComponent->GetOwner())
		{
			InstanceData->bCachedValid = true;
			InstanceData->CachedTransform = OwnerActor->GetTransform();
			InstanceData->CachedTransform.AddToTranslation(FVector(SystemInstance->GetLWCTile()) * -FLargeWorldRenderScalar::GetTileSize());
		}
	}

	return false;
}

void UNiagaraDataInterfaceActorComponent::ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance)
{
	NDIActorComponentLocal::FNDIProxy::ProvidePerInstanceDataForRenderThread(DataForRenderThread, PerInstanceData, SystemInstance);
}

ETickingGroup UNiagaraDataInterfaceActorComponent::CalculateTickGroup(const void* PerInstanceData) const
{
	if ( bRequireCurrentFrameData )
	{
		if (UActorComponent* ActorComponent = ResolveComponent(PerInstanceData))
		{
			ETickingGroup FinalTickGroup = FMath::Max(ActorComponent->PrimaryComponentTick.TickGroup, ActorComponent->PrimaryComponentTick.EndTickGroup);
			//-TODO: Do we need to do this?
			//if ( USkeletalMeshComponent* SkelMeshComponent = Cast<USkeletalMeshComponent>(ActorComponent) )
			//{
			//	if (SkelMeshComponent->bBlendPhysics)
			//	{
			//		FinalTickGroup = FMath::Max(FinalTickGroup, TG_EndPhysics);
			//	}
			//}
			FinalTickGroup = FMath::Clamp(ETickingGroup(FinalTickGroup + 1), NiagaraFirstTickGroup, NiagaraLastTickGroup);
			return FinalTickGroup;
		}
	}
	return NiagaraFirstTickGroup;
}

bool UNiagaraDataInterfaceActorComponent::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}

	const UNiagaraDataInterfaceActorComponent* OtherTyped = CastChecked<const UNiagaraDataInterfaceActorComponent>(Other);
	return OtherTyped->SourceActor == SourceActor
		&& OtherTyped->ActorOrComponentParameter == ActorOrComponentParameter
		&& OtherTyped->bRequireCurrentFrameData == bRequireCurrentFrameData;
}

bool UNiagaraDataInterfaceActorComponent::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceActorComponent* OtherTyped = CastChecked<UNiagaraDataInterfaceActorComponent>(Destination);
	OtherTyped->SourceActor = SourceActor;
	OtherTyped->ActorOrComponentParameter = ActorOrComponentParameter;
	OtherTyped->bRequireCurrentFrameData = bRequireCurrentFrameData;
	return true;
}

void UNiagaraDataInterfaceActorComponent::VMGetMatrix(FVectorVMExternalFunctionContext& Context)
{
	using namespace NDIActorComponentLocal;

	VectorVM::FUserPtrHandler<FInstanceData_GameThread> InstanceData(Context);
	FNDIOutputParam<bool>		OutValid(Context);
	FNDIOutputParam<FMatrix44f>	OutMatrix(Context);

	const FMatrix44f InstanceMatrix = FMatrix44f(InstanceData->CachedTransform.ToMatrixWithScale());		// LWC_TODO: Precision loss
	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		OutValid.SetAndAdvance(InstanceData->bCachedValid);
		OutMatrix.SetAndAdvance(InstanceMatrix);
	}
}

void UNiagaraDataInterfaceActorComponent::VMGetTransform(FVectorVMExternalFunctionContext& Context)
{
	using namespace NDIActorComponentLocal;

	VectorVM::FUserPtrHandler<FInstanceData_GameThread> InstanceData(Context);
	FNDIOutputParam<bool>		OutValid(Context);
	FNDIOutputParam<FVector3f>	OutPosition(Context);
	FNDIOutputParam<FQuat4f>	OutRotation(Context);
	FNDIOutputParam<FVector3f>	OutScale(Context);

	for (int32 i=0; i < Context.GetNumInstances(); ++i)
	{
		OutValid.SetAndAdvance(InstanceData->bCachedValid);
		OutPosition.SetAndAdvance((FVector3f)InstanceData->CachedTransform.GetLocation());
		OutRotation.SetAndAdvance((FQuat4f)InstanceData->CachedTransform.GetRotation());
		OutScale.SetAndAdvance((FVector3f)InstanceData->CachedTransform.GetScale3D());
	}
}

#undef LOCTEXT_NAMESPACE
