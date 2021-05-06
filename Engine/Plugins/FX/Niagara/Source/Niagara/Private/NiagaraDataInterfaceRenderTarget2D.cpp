// Copyright Epic Games, Inc. All Rights Reserved.
#include "NiagaraDataInterfaceRenderTarget2D.h"
#include "NiagaraShader.h"
#include "ShaderParameterUtils.h"
#include "ClearQuad.h"
#include "TextureResource.h"
#include "GenerateMips.h"
#include "CanvasTypes.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"

#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraSettings.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraRenderer.h"
#if WITH_EDITOR
#include "NiagaraGpuComputeDebug.h"
#endif
#include "NiagaraStats.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceRenderTarget2D"

const FString UNiagaraDataInterfaceRenderTarget2D::SizeName(TEXT("RWSize_"));
const FString UNiagaraDataInterfaceRenderTarget2D::RWOutputName(TEXT("RWOutput_"));
const FString UNiagaraDataInterfaceRenderTarget2D::OutputName(TEXT("Output_"));
const FString UNiagaraDataInterfaceRenderTarget2D::InputName(TEXT("Input_"));

// Global VM function names, also used by the shaders code generation methods.
const FName UNiagaraDataInterfaceRenderTarget2D::SetValueFunctionName("SetRenderTargetValue");
const FName UNiagaraDataInterfaceRenderTarget2D::GetValueFunctionName("GetRenderTargetValue");
const FName UNiagaraDataInterfaceRenderTarget2D::SampleValueFunctionName("SampleRenderTargetValue");
const FName UNiagaraDataInterfaceRenderTarget2D::SetSizeFunctionName("SetRenderTargetSize");
const FName UNiagaraDataInterfaceRenderTarget2D::GetSizeFunctionName("GetRenderTargetSize");
const FName UNiagaraDataInterfaceRenderTarget2D::LinearToIndexName("LinearToIndex");

FNiagaraVariableBase UNiagaraDataInterfaceRenderTarget2D::ExposedRTVar;

int32 GNiagaraReleaseResourceOnRemove = false;
static FAutoConsoleVariableRef CVarNiagaraReleaseResourceOnRemove(
	TEXT("fx.Niagara.RenderTarget.ReleaseResourceOnRemove"),
	GNiagaraReleaseResourceOnRemove,
	TEXT("Releases the render target resource once it is removed from the manager list rather than waiting for a GC."),
	ECVF_Default
);

float GNiagaraRenderTargetResolutionMultiplier = 1.0f;
static FAutoConsoleVariableRef CVarNiagaraRenderTargetResolutionMultiplier(
	TEXT("fx.Niagara.RenderTarget.ResolutionMultiplier"),
	GNiagaraRenderTargetResolutionMultiplier,
	TEXT("Optional global modifier to Niagara render target resolution."),
	ECVF_Default
);

int GNiagaraRenderTargetAllowReads = 0;
static FAutoConsoleVariableRef CVarNiagaraRenderTargetAllowReads(
	TEXT("fx.Niagara.RenderTarget.AllowReads"),
	GNiagaraRenderTargetAllowReads,
	TEXT("Enables read operations to be visible in the UI, very experimental."),
	ECVF_Default
);

/*--------------------------------------------------------------------------------------------------------------------------*/

struct FNDIRenderTarget2DFunctionVersion
{
	enum Type
	{
		InitialVersion = 0,
		AddedOptionalExecute = 1,

		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};
};

/*--------------------------------------------------------------------------------------------------------------------------*/
struct FNiagaraDataInterfaceParametersCS_RenderTarget2D : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_RenderTarget2D, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{			
		SizeParam.Bind(ParameterMap, *(UNiagaraDataInterfaceRenderTarget2D::SizeName + ParameterInfo.DataInterfaceHLSLSymbol));
		OutputParam.Bind(ParameterMap, *(UNiagaraDataInterfaceRenderTarget2D::OutputName + ParameterInfo.DataInterfaceHLSLSymbol));

		InputParam.Bind(ParameterMap, *(UNiagaraDataInterfaceRenderTarget2D::InputName + ParameterInfo.DataInterfaceHLSLSymbol));
		InputSamplerStateParam.Bind(ParameterMap, *(UNiagaraDataInterfaceRenderTarget2D::InputName + TEXT("SamplerState") + ParameterInfo.DataInterfaceHLSLSymbol));
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		// Get shader and DI
		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
		FNiagaraDataInterfaceProxyRenderTarget2DProxy* VFDI = static_cast<FNiagaraDataInterfaceProxyRenderTarget2DProxy*>(Context.DataInterface);

		FRenderTarget2DRWInstanceData_RenderThread* ProxyData = VFDI->SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID);
		check(ProxyData);

		SetShaderValue(RHICmdList, ComputeShaderRHI, SizeParam, ProxyData->Size);
	
		if (OutputParam.IsUAVBound())
		{
			FRHIUnorderedAccessView* OutputUAV = ProxyData->UnorderedAccessViewRHI;
			if (OutputUAV)
			{
				// FIXME: this transition needs to happen in FNiagaraDataInterfaceProxyRenderTarget2DProxy::PreStage so it doesn't break up the overlap group,
				// but for some reason it stops working if I move it in there.
				RHICmdList.Transition(FRHITransitionInfo(OutputUAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
				ProxyData->bWasWrittenTo = true;
			}
			else
			{
				OutputUAV = Context.Batcher->GetEmptyUAVFromPool(RHICmdList, EPixelFormat::PF_A16B16G16R16, ENiagaraEmptyUAVType::Buffer);
			}

			RHICmdList.SetUAVParameter(ComputeShaderRHI, OutputParam.GetUAVIndex(), OutputUAV);
		}

		if ( InputParam.IsBound() )
		{
			FRHITexture* TextureRHI = ProxyData->TextureRHI;
			if (!ensureMsgf(!OutputParam.IsUAVBound(), TEXT("NiagaraDIRenderTarget2D(%s) is bound as both read & write, read will be ignored."), *Context.DataInterface->SourceDIName.ToString()))
			{
				//-TODO: Feedback to the user that read & write is bound
				TextureRHI = nullptr;
			}

			if (TextureRHI == nullptr)
			{
				TextureRHI = GBlackTexture->TextureRHI;
			}

			FRHISamplerState* SamplerStateRHI = ProxyData->SamplerStateRHI;
			if (SamplerStateRHI == nullptr)
			{
				SamplerStateRHI = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			}

			SetTextureParameter(
				RHICmdList,
				ComputeShaderRHI,
				InputParam,
				InputSamplerStateParam,
				SamplerStateRHI,
				TextureRHI
			);
		}
	}

	void Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const 
	{
		if (OutputParam.IsBound())
		{
			OutputParam.UnsetUAV(RHICmdList, Context.Shader.GetComputeShader());

			FNiagaraDataInterfaceProxyRenderTarget2DProxy* VFDI = static_cast<FNiagaraDataInterfaceProxyRenderTarget2DProxy*>(Context.DataInterface);
			if ( FRenderTarget2DRWInstanceData_RenderThread* ProxyData = VFDI->SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID) )
			{
				if (FRHIUnorderedAccessView* OutputUAV = ProxyData->UnorderedAccessViewRHI)
				{
					// FIXME: move to FNiagaraDataInterfaceProxyRenderTarget2DProxy::PostStage, same as for the transition in Set() above.
					RHICmdList.Transition(FRHITransitionInfo(OutputUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
				}
			}
		}
	}

private:
	LAYOUT_FIELD(FShaderParameter, SizeParam);
	LAYOUT_FIELD(FRWShaderParameter, OutputParam);

	LAYOUT_FIELD(FShaderResourceParameter, InputParam);
	LAYOUT_FIELD(FShaderResourceParameter, InputSamplerStateParam);
};

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_RenderTarget2D);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceRenderTarget2D, FNiagaraDataInterfaceParametersCS_RenderTarget2D);

/*--------------------------------------------------------------------------------------------------------------------------*/

#if STATS
void FRenderTarget2DRWInstanceData_RenderThread::UpdateMemoryStats()
{
	DEC_MEMORY_STAT_BY(STAT_NiagaraRenderTargetMemory, MemorySize);

	MemorySize = 0;
	if (FRHITexture* RHITexture = TextureRHI)
	{
		MemorySize = RHIComputeMemorySize(RHITexture);
	}

	INC_MEMORY_STAT_BY(STAT_NiagaraRenderTargetMemory, MemorySize);
}
#endif

/*--------------------------------------------------------------------------------------------------------------------------*/

UNiagaraDataInterfaceRenderTarget2D::UNiagaraDataInterfaceRenderTarget2D(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyRenderTarget2DProxy());

	FNiagaraTypeDefinition Def(UTextureRenderTarget::StaticClass());
	RenderTargetUserParameter.Parameter.SetType(Def);
}


void UNiagaraDataInterfaceRenderTarget2D::PostInitProperties()
{
	Super::PostInitProperties();

	//Can we register data interfaces as regular types and fold them into the FNiagaraVariable framework for UI and function calls etc?
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);

		ExposedRTVar = FNiagaraVariableBase(FNiagaraTypeDefinition(UTexture::StaticClass()), TEXT("RenderTarget"));
	}
}

void UNiagaraDataInterfaceRenderTarget2D::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	Super::GetFunctions(OutFunctions);

	const int32 EmitterSystemOnlyBitmask = ENiagaraScriptUsageMask::Emitter | ENiagaraScriptUsageMask::System;
	OutFunctions.Reserve(OutFunctions.Num() + 4);

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = GetSizeFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("RenderTarget")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Width")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Height")));
		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
	#if WITH_EDITORONLY_DATA
		Sig.FunctionVersion = FNDIRenderTarget2DFunctionVersion::LatestVersion;
	#endif
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = SetSizeFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("RenderTarget")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Width")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Height")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Success")));

		Sig.ModuleUsageBitmask = EmitterSystemOnlyBitmask;
		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresExecPin = true;
		Sig.bRequiresContext = false;
		Sig.bSupportsCPU = true;
		Sig.bSupportsGPU = false;
	#if WITH_EDITORONLY_DATA
		Sig.FunctionVersion = FNDIRenderTarget2DFunctionVersion::LatestVersion;
	#endif
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = SetValueFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("RenderTarget")));
		Sig.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Enabled"))).SetValue(true);
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Value")));

		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresExecPin = true;
		Sig.bRequiresContext = false;
		Sig.bWriteFunction = true;
		Sig.bSupportsCPU = false;
		Sig.bSupportsGPU = true;
	#if WITH_EDITORONLY_DATA
		Sig.FunctionVersion = FNDIRenderTarget2DFunctionVersion::LatestVersion;
	#endif
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = GetValueFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("RenderTarget")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Value")));

		Sig.bHidden = GNiagaraRenderTargetAllowReads != 1;
		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bWriteFunction = true;
		Sig.bSupportsCPU = false;
		Sig.bSupportsGPU = true;
#if WITH_EDITORONLY_DATA
		Sig.FunctionVersion = FNDIRenderTarget2DFunctionVersion::LatestVersion;
#endif
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = SampleValueFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("RenderTarget")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("UV")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Value")));

		Sig.bHidden = GNiagaraRenderTargetAllowReads != 1;
		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bWriteFunction = true;
		Sig.bSupportsCPU = false;
		Sig.bSupportsGPU = true;
#if WITH_EDITORONLY_DATA
		Sig.FunctionVersion = FNDIRenderTarget2DFunctionVersion::LatestVersion;
#endif
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = LinearToIndexName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("RenderTarget")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Linear")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));

		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bWriteFunction = true;
		Sig.bSupportsCPU = false;
		Sig.bSupportsGPU = true;
	#if WITH_EDITORONLY_DATA
		Sig.FunctionVersion = FNDIRenderTarget2DFunctionVersion::LatestVersion;
	#endif
	}
}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfaceRenderTarget2D::UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature)
{
	bool bWasChanged = false;

	if ( FunctionSignature.FunctionVersion < FNDIRenderTarget2DFunctionVersion::AddedOptionalExecute )
	{
		if ( FunctionSignature.Name == SetValueFunctionName )
		{
			check(FunctionSignature.Inputs.Num() == 4);
			FunctionSignature.Inputs.Insert_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Enabled")), 1).SetValue(true);
			bWasChanged = true;
		}
	}

	// Set latest version
	FunctionSignature.FunctionVersion = FNDIRenderTarget2DFunctionVersion::LatestVersion;

	return bWasChanged;
}
#endif

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceRenderTarget2D, GetSize);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceRenderTarget2D, SetSize);
void UNiagaraDataInterfaceRenderTarget2D::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	Super::GetVMExternalFunction(BindingInfo, InstanceData, OutFunc);
	if (BindingInfo.Name == GetSizeFunctionName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 2);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceRenderTarget2D, GetSize)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SetSizeFunctionName)
	{
		check(BindingInfo.GetNumInputs() == 3 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceRenderTarget2D, SetSize)::Bind(this, OutFunc);
	}

}

bool UNiagaraDataInterfaceRenderTarget2D::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	
	const UNiagaraDataInterfaceRenderTarget2D* OtherTyped = CastChecked<const UNiagaraDataInterfaceRenderTarget2D>(Other);
	return
		OtherTyped != nullptr &&
#if WITH_EDITORONLY_DATA
		OtherTyped->bPreviewRenderTarget == bPreviewRenderTarget &&
#endif
		OtherTyped->RenderTargetUserParameter == RenderTargetUserParameter &&
		OtherTyped->Size == Size &&
		OtherTyped->MipMapGeneration == MipMapGeneration &&
		OtherTyped->OverrideRenderTargetFormat == OverrideRenderTargetFormat &&
		OtherTyped->bInheritUserParameterSettings == bInheritUserParameterSettings &&
		OtherTyped->bOverrideFormat == bOverrideFormat;
}

bool UNiagaraDataInterfaceRenderTarget2D::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceRenderTarget2D* DestinationTyped = CastChecked<UNiagaraDataInterfaceRenderTarget2D>(Destination);
	if (!DestinationTyped)
	{
		return false;
	}

	DestinationTyped->Size = Size;
	DestinationTyped->MipMapGeneration = MipMapGeneration;
	DestinationTyped->OverrideRenderTargetFormat = OverrideRenderTargetFormat;
	DestinationTyped->bInheritUserParameterSettings = bInheritUserParameterSettings;
	DestinationTyped->bOverrideFormat = bOverrideFormat;
#if WITH_EDITORONLY_DATA
	DestinationTyped->bPreviewRenderTarget = bPreviewRenderTarget;
#endif
	DestinationTyped->RenderTargetUserParameter = RenderTargetUserParameter;
	return true;
}

#if WITH_EDITORONLY_DATA
void UNiagaraDataInterfaceRenderTarget2D::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	Super::GetParameterDefinitionHLSL(ParamInfo, OutHLSL);

	static const TCHAR *FormatDeclarations = TEXT(R"(
		RWTexture2D<float4> {OutputName};
		Texture2D<float4> {InputName};
		SamplerState {InputName}SamplerState;
		int2 {SizeName};
	)");
	TMap<FString, FStringFormatArg> ArgsDeclarations =
	{
		{ TEXT("OutputName"),	RWOutputName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("InputName"),	InputName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("SizeName"),		SizeName + ParamInfo.DataInterfaceHLSLSymbol },
	};
	OutHLSL += FString::Format(FormatDeclarations, ArgsDeclarations);
}

bool UNiagaraDataInterfaceRenderTarget2D::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	bool ParentRet = Super::GetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, OutHLSL);
	if (ParentRet)
	{
		return true;
	} 

	const TMap<FString, FStringFormatArg> ArgsBounds =
	{
		{TEXT("FunctionName"),	FunctionInfo.InstanceName},
		{TEXT("OutputName"),	RWOutputName + ParamInfo.DataInterfaceHLSLSymbol},
		{TEXT("InputName"),		InputName + ParamInfo.DataInterfaceHLSLSymbol },
		{TEXT("SizeName"),		SizeName + ParamInfo.DataInterfaceHLSLSymbol },
	};

	if (FunctionInfo.DefinitionName == SetValueFunctionName)
	{
		static const TCHAR* FormatBounds = TEXT(R"(
			void {FunctionName}(bool bEnabled, int In_IndexX, int In_IndexY, float4 In_Value)
			{			
				if ( bEnabled )
				{
					{OutputName}[int2(In_IndexX, In_IndexY)] = In_Value;
				}
			}
		)");
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetValueFunctionName)
	{
		static const TCHAR* FormatBounds = TEXT(R"(
			void {FunctionName}(int In_IndexX, int In_IndexY, out float4 Out_Value)
			{			
				Out_Value = {InputName}.Load(int3(In_IndexX, In_IndexY, 0));
			}
		)");
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleValueFunctionName)
	{
		static const TCHAR* FormatBounds = TEXT(R"(
			void {FunctionName}(float2 UV, out float4 Out_Value)
			{			
				Out_Value = {InputName}.SampleLevel({InputName}SamplerState, UV, 0.0f);
			}
		)");
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	else if (FunctionInfo.DefinitionName == LinearToIndexName)
	{
		static const TCHAR* FormatBounds = TEXT(R"(
			void {FunctionName}(int Linear, out int OutIndexX, out int OutIndexY)
			{
				OutIndexX = Linear % {SizeName}.x;
				OutIndexY = Linear / {SizeName}.x;
			}
		)");
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetSizeFunctionName)
	{
		static const TCHAR* FormatBounds = TEXT(R"(
			void {FunctionName}(out int Out_Width, out int Out_Height)
			{			
				Out_Width = {SizeName}.x;
				Out_Height = {SizeName}.y;
			}
		)");
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}

	return false;
}
#endif

bool UNiagaraDataInterfaceRenderTarget2D::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	check(Proxy);

	extern float GNiagaraRenderTargetResolutionMultiplier;
	FRenderTarget2DRWInstanceData_GameThread* InstanceData = new (PerInstanceData) FRenderTarget2DRWInstanceData_GameThread();
	SystemInstancesToProxyData_GT.Emplace(SystemInstance->GetId(), InstanceData);

	InstanceData->Size.X = FMath::Clamp<int>(int(float(Size.X) * GNiagaraRenderTargetResolutionMultiplier), 1, GMaxTextureDimensions);
	InstanceData->Size.Y = FMath::Clamp<int>(int(float(Size.Y) * GNiagaraRenderTargetResolutionMultiplier), 1, GMaxTextureDimensions);
	InstanceData->MipMapGeneration = MipMapGeneration;
	InstanceData->Format = bOverrideFormat ? OverrideRenderTargetFormat : GetDefault<UNiagaraSettings>()->DefaultRenderTargetFormat;
	InstanceData->RTUserParamBinding.Init(SystemInstance->GetInstanceParameters(), RenderTargetUserParameter.Parameter);
#if WITH_EDITORONLY_DATA
	InstanceData->bPreviewTexture = bPreviewRenderTarget;
#endif

	return true;
}

void UNiagaraDataInterfaceRenderTarget2D::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	SystemInstancesToProxyData_GT.Remove(SystemInstance->GetId());

	FRenderTarget2DRWInstanceData_GameThread* InstanceData = static_cast<FRenderTarget2DRWInstanceData_GameThread*>(PerInstanceData);

	InstanceData->~FRenderTarget2DRWInstanceData_GameThread();

	FNiagaraDataInterfaceProxyRenderTarget2DProxy* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyRenderTarget2DProxy>();
	ENQUEUE_RENDER_COMMAND(FNiagaraDIDestroyInstanceData) (
		[RT_Proxy, InstanceID=SystemInstance->GetId(), Batcher=SystemInstance->GetBatcher()](FRHICommandListImmediate& CmdList)
		{
#if STATS
			if (FRenderTarget2DRWInstanceData_RenderThread* TargetData = RT_Proxy->SystemInstancesToProxyData_RT.Find(InstanceID))
			{
				TargetData->SamplerStateRHI = nullptr;
				TargetData->TextureRHI = nullptr;
				TargetData->UpdateMemoryStats();
			}
#endif
			RT_Proxy->SystemInstancesToProxyData_RT.Remove(InstanceID);
		}
	);

	// Make sure to clear out the reference to the render target if we created one.
	UTextureRenderTarget2D* ExistingRenderTarget = nullptr;
	if ( ManagedRenderTargets.RemoveAndCopyValue(SystemInstance->GetId(), ExistingRenderTarget) && GNiagaraReleaseResourceOnRemove)
	{
		ExistingRenderTarget->ReleaseResource();
	}
}


void UNiagaraDataInterfaceRenderTarget2D::GetExposedVariables(TArray<FNiagaraVariableBase>& OutVariables) const
{
	OutVariables.Emplace(ExposedRTVar);
}

bool UNiagaraDataInterfaceRenderTarget2D::GetExposedVariableValue(const FNiagaraVariableBase& InVariable, void* InPerInstanceData, FNiagaraSystemInstance* InSystemInstance, void* OutData) const
{
	FRenderTarget2DRWInstanceData_GameThread* InstanceData = static_cast<FRenderTarget2DRWInstanceData_GameThread*>(InPerInstanceData);
	if (InVariable.IsValid() && InVariable == ExposedRTVar && InstanceData && InstanceData->TargetTexture)
	{
		UObject** Var = (UObject**)OutData;
		*Var = InstanceData->TargetTexture;
		return true;
	}
	return false;
}

void UNiagaraDataInterfaceRenderTarget2D::GetCanvasVariables(TArray<FNiagaraVariableBase>& OutVariables) const
{
	static const FName NAME_RenderTarget(TEXT("RenderTarget"));
	OutVariables.Emplace(FNiagaraTypeDefinition::GetVec4Def(), NAME_RenderTarget);
}

bool UNiagaraDataInterfaceRenderTarget2D::RenderVariableToCanvas(FNiagaraSystemInstanceID SystemInstanceID, FName VariableName, class FCanvas* Canvas, const FIntRect& DrawRect) const
{
	if (!Canvas)
	{
		return false;
	}

	FRenderTarget2DRWInstanceData_GameThread* GT_InstanceData = SystemInstancesToProxyData_GT.FindRef(SystemInstanceID);
	if (!GT_InstanceData)
	{
		return false;
	}

	if ( !GT_InstanceData->TargetTexture || !GT_InstanceData->TargetTexture->Resource )
	{
		return false;
	}

	Canvas->DrawTile(
		DrawRect.Min.X, DrawRect.Min.Y, DrawRect.Width(), DrawRect.Height(),
		0.0f, 0.0f, 1.0f, 1.0f,
		FLinearColor::White,
		GT_InstanceData->TargetTexture->Resource,
		false
	);

	return true;
}

void UNiagaraDataInterfaceRenderTarget2D::SetSize(FVectorVMContext& Context)
{
	// This should only be called from a system or emitter script due to a need for only setting up initially.
	VectorVM::FUserPtrHandler<FRenderTarget2DRWInstanceData_GameThread> InstData(Context);
	FNDIInputParam<int> InSizeX(Context);
	FNDIInputParam<int> InSizeY(Context);
	FNDIOutputParam<FNiagaraBool> OutSuccess(Context);

	extern float GNiagaraRenderTargetResolutionMultiplier;
	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		const int SizeX = InSizeX.GetAndAdvance();
		const int SizeY = InSizeY.GetAndAdvance();
		const bool bSuccess = (InstData.Get() != nullptr && Context.NumInstances == 1 && SizeX >= 0 && SizeY >= 0);
		OutSuccess.SetAndAdvance(bSuccess);
		if (bSuccess)
		{
			InstData->Size.X = FMath::Clamp<int>(int(float(SizeX) * GNiagaraRenderTargetResolutionMultiplier), 1, GMaxTextureDimensions);
			InstData->Size.Y = FMath::Clamp<int>(int(float(SizeY) * GNiagaraRenderTargetResolutionMultiplier), 1, GMaxTextureDimensions);
		}
	}
}

void UNiagaraDataInterfaceRenderTarget2D::GetSize(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FRenderTarget2DRWInstanceData_GameThread> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int> OutSizeX(Context);
	VectorVM::FExternalFuncRegisterHandler<int> OutSizeY(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutSizeX.GetDestAndAdvance() = InstData->Size.X;
		*OutSizeY.GetDestAndAdvance() = InstData->Size.Y;
	}
}


bool UNiagaraDataInterfaceRenderTarget2D::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	FRenderTarget2DRWInstanceData_GameThread* InstanceData = static_cast<FRenderTarget2DRWInstanceData_GameThread*>(PerInstanceData);

	// Pull from user parameter
	UTextureRenderTarget2D* UserTargetTexture = InstanceData->RTUserParamBinding.GetValue<UTextureRenderTarget2D>();
	if (UserTargetTexture && (InstanceData->TargetTexture != UserTargetTexture))
	{
		InstanceData->TargetTexture = UserTargetTexture;

		UTextureRenderTarget2D* ExistingRenderTarget = nullptr;
		if (ManagedRenderTargets.RemoveAndCopyValue(SystemInstance->GetId(), ExistingRenderTarget) && GNiagaraReleaseResourceOnRemove)
		{
			ExistingRenderTarget->ReleaseResource();
		}
	}

	// Do we inherit the texture parameters from the user supplied texture?
	if (bInheritUserParameterSettings)
	{
		if (UserTargetTexture)
		{
			InstanceData->Size.X = UserTargetTexture->SizeX;
			InstanceData->Size.Y = UserTargetTexture->SizeY;
			if (UserTargetTexture->bAutoGenerateMips)
			{
				// We have to take a guess at user intention
				InstanceData->MipMapGeneration = MipMapGeneration == ENiagaraMipMapGeneration::Disabled ? ENiagaraMipMapGeneration::PostStage : MipMapGeneration;
			}
			else
			{
				InstanceData->MipMapGeneration = ENiagaraMipMapGeneration::Disabled;
			}
			InstanceData->Format = InstanceData->TargetTexture->RenderTargetFormat;
		}
		else
		{
			UE_LOG(LogNiagara, Error, TEXT("RenderTarget UserParam is required but invalid."));
		}
	}

	return false;
}

bool UNiagaraDataInterfaceRenderTarget2D::PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	// Update InstanceData as the texture may have changed
	FRenderTarget2DRWInstanceData_GameThread* InstanceData = static_cast<FRenderTarget2DRWInstanceData_GameThread*>(PerInstanceData);
#if WITH_EDITORONLY_DATA
	InstanceData->bPreviewTexture = bPreviewRenderTarget;
#endif

	// Do we need to create a new texture?
	if (!bInheritUserParameterSettings && (InstanceData->TargetTexture == nullptr))
	{
		InstanceData->TargetTexture = NewObject<UTextureRenderTarget2D>(this);
		InstanceData->TargetTexture->bCanCreateUAV = true;
		InstanceData->TargetTexture->bAutoGenerateMips = InstanceData->MipMapGeneration != ENiagaraMipMapGeneration::Disabled;
		InstanceData->TargetTexture->RenderTargetFormat = InstanceData->Format;
		InstanceData->TargetTexture->ClearColor = FLinearColor(0.0, 0, 0, 0);
		InstanceData->TargetTexture->InitAutoFormat(InstanceData->Size.X, InstanceData->Size.Y);
		InstanceData->TargetTexture->UpdateResourceImmediate(true);

		ManagedRenderTargets.Add(SystemInstance->GetId()) = InstanceData->TargetTexture;
	}

	// Do we need to update the existing texture?
	if (InstanceData->TargetTexture)
	{
		const bool bAutoGenerateMips = InstanceData->MipMapGeneration != ENiagaraMipMapGeneration::Disabled;
		if ((InstanceData->TargetTexture->SizeX != InstanceData->Size.X) || (InstanceData->TargetTexture->SizeY != InstanceData->Size.Y) ||
			(InstanceData->TargetTexture->RenderTargetFormat != InstanceData->Format) ||
			!InstanceData->TargetTexture->bCanCreateUAV ||
			(InstanceData->TargetTexture->bAutoGenerateMips != bAutoGenerateMips) ||
			!InstanceData->TargetTexture->Resource )
		{
			// resize RT to match what we need for the output
			InstanceData->TargetTexture->bCanCreateUAV = true;
			InstanceData->TargetTexture->bAutoGenerateMips = bAutoGenerateMips;
			InstanceData->TargetTexture->RenderTargetFormat = InstanceData->Format;
			InstanceData->TargetTexture->InitAutoFormat(InstanceData->Size.X, InstanceData->Size.Y);
			InstanceData->TargetTexture->UpdateResourceImmediate(true);
		}
	}

	//-TODO: We could avoid updating each frame if we cache the resource pointer or a serial number
	bool bUpdateRT = true;
	if (bUpdateRT)
	{
		FNiagaraDataInterfaceProxyRenderTarget2DProxy* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyRenderTarget2DProxy>();
		FTextureRenderTargetResource* RT_TargetTexture = InstanceData->TargetTexture ? InstanceData->TargetTexture->GameThread_GetRenderTargetResource() : nullptr;
		ENQUEUE_RENDER_COMMAND(NDIRenderTarget2DUpdate)
		(
			[RT_Proxy, RT_InstanceID=SystemInstance->GetId(), RT_InstanceData=*InstanceData, RT_TargetTexture](FRHICommandListImmediate& RHICmdList)
			{
				FRenderTarget2DRWInstanceData_RenderThread* TargetData = &RT_Proxy->SystemInstancesToProxyData_RT.FindOrAdd(RT_InstanceID);
				TargetData->Size = RT_InstanceData.Size;
				TargetData->MipMapGeneration = RT_InstanceData.MipMapGeneration;
			#if WITH_EDITORONLY_DATA
				TargetData->bPreviewTexture = RT_InstanceData.bPreviewTexture;
			#endif
				TargetData->SamplerStateRHI.SafeRelease();
				TargetData->TextureRHI.SafeRelease();
				TargetData->UnorderedAccessViewRHI.SafeRelease();
				if (RT_TargetTexture)
				{
					if (FTextureRenderTarget2DResource* Resource2D = RT_TargetTexture->GetTextureRenderTarget2DResource())
					{
						TargetData->SamplerStateRHI = Resource2D->SamplerStateRHI;
						TargetData->TextureRHI = Resource2D->GetTextureRHI();
						TargetData->UnorderedAccessViewRHI = Resource2D->GetUnorderedAccessViewRHI();
					}
				}
			#if STATS
				TargetData->UpdateMemoryStats();
			#endif
			}
		);
	}

	return false;
}

void FNiagaraDataInterfaceProxyRenderTarget2DProxy::PostStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context)
{
	if (FRenderTarget2DRWInstanceData_RenderThread* ProxyData = SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID))
	{
		if (ProxyData->bWasWrittenTo && (ProxyData->MipMapGeneration == ENiagaraMipMapGeneration::PostStage))
		{
			ProxyData->bWasWrittenTo = false;

			check(&RHICmdList == &FRHICommandListExecutor::GetImmediateCommandList());

			FMemMark MemMark(FMemStack::Get());
			FRDGBuilder GraphBuilder(FRHICommandListExecutor::GetImmediateCommandList());
			TRefCountPtr<IPooledRenderTarget> PoolRenderTarget = CreateRenderTarget(ProxyData->TextureRHI, TEXT("NiagaraRenderTarget2D"));
			FRDGTextureRef MipOutputTexture = GraphBuilder.RegisterExternalTexture(PoolRenderTarget);
			FGenerateMips::Execute(GraphBuilder, MipOutputTexture);
			GraphBuilder.Execute();
		}
	}
}

void FNiagaraDataInterfaceProxyRenderTarget2DProxy::PostSimulate(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceArgs& Context)
{
	if (FRenderTarget2DRWInstanceData_RenderThread* ProxyData = SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID))
	{
		if (ProxyData->bWasWrittenTo && (ProxyData->MipMapGeneration == ENiagaraMipMapGeneration::PostSimulate))
		{
			ProxyData->bWasWrittenTo = false;

			check(&RHICmdList == &FRHICommandListExecutor::GetImmediateCommandList());

			FMemMark MemMark(FMemStack::Get());
			FRDGBuilder GraphBuilder(FRHICommandListExecutor::GetImmediateCommandList());
			TRefCountPtr<IPooledRenderTarget> PoolRenderTarget = CreateRenderTarget(ProxyData->TextureRHI, TEXT("NiagaraRenderTarget2D"));
			FRDGTextureRef MipOutputTexture = GraphBuilder.RegisterExternalTexture(PoolRenderTarget);
			FGenerateMips::Execute(GraphBuilder, MipOutputTexture);
			GraphBuilder.Execute();
		}

#if NIAGARA_COMPUTEDEBUG_ENABLED
		if (ProxyData->bPreviewTexture)
		{
			if (FNiagaraGpuComputeDebug* GpuComputeDebug = Context.Batcher->GetGpuComputeDebug())
			{
				if (FRHITexture* RHITexture = ProxyData->TextureRHI)
				{
					GpuComputeDebug->AddTexture(RHICmdList, Context.SystemInstanceID, SourceDIName, RHITexture);
				}
			}
		}
#endif
	}
}

FIntVector FNiagaraDataInterfaceProxyRenderTarget2DProxy::GetElementCount(FNiagaraSystemInstanceID SystemInstanceID) const
{
	if ( const FRenderTarget2DRWInstanceData_RenderThread* TargetData = SystemInstancesToProxyData_RT.Find(SystemInstanceID) )
	{
		return FIntVector(TargetData->Size.X, TargetData->Size.Y, 1);
	}
	return FIntVector::ZeroValue;
}

#undef LOCTEXT_NAMESPACE
