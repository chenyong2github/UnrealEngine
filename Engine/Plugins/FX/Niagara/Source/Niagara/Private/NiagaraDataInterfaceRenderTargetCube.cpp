// Copyright Epic Games, Inc. All Rights Reserved.
#include "NiagaraDataInterfaceRenderTargetCube.h"
#include "NiagaraShader.h"
#include "ShaderParameterUtils.h"
#include "ClearQuad.h"
#include "TextureResource.h"
#include "Engine/TextureRenderTargetCube.h"

#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraStats.h"
#include "NiagaraRenderer.h"
#include "NiagaraSettings.h"
#if WITH_EDITOR
#include "NiagaraGpuComputeDebug.h"
#endif

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceRenderTargetCube"

const FString UNiagaraDataInterfaceRenderTargetCube::SizeName(TEXT("Size_"));
const FString UNiagaraDataInterfaceRenderTargetCube::RWOutputName(TEXT("RWOutput_"));
const FString UNiagaraDataInterfaceRenderTargetCube::OutputName(TEXT("Output_"));
const FString UNiagaraDataInterfaceRenderTargetCube::InputName(TEXT("Input_"));

// Global VM function names, also used by the shaders code generation methods.
const FName UNiagaraDataInterfaceRenderTargetCube::SetValueFunctionName("SetRenderTargetValue");
const FName UNiagaraDataInterfaceRenderTargetCube::GetValueFunctionName("GetRenderTargetValue");
const FName UNiagaraDataInterfaceRenderTargetCube::SampleValueFunctionName("SampleRenderTargetValue");
const FName UNiagaraDataInterfaceRenderTargetCube::SetSizeFunctionName("SetRenderTargetSize");
const FName UNiagaraDataInterfaceRenderTargetCube::GetSizeFunctionName("GetRenderTargetSize");
const FName UNiagaraDataInterfaceRenderTargetCube::LinearToIndexName("LinearToIndex");

FNiagaraVariableBase UNiagaraDataInterfaceRenderTargetCube::ExposedRTVar;

/*--------------------------------------------------------------------------------------------------------------------------*/

struct FNDIRenderTargetCubeFunctionVersion
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
struct FNiagaraDataInterfaceParametersCS_RenderTargetCube : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_RenderTargetCube, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{			
		SizeParam.Bind(ParameterMap, *(UNiagaraDataInterfaceRenderTargetCube::SizeName + ParameterInfo.DataInterfaceHLSLSymbol));
		OutputParam.Bind(ParameterMap, *(UNiagaraDataInterfaceRenderTargetCube::OutputName + ParameterInfo.DataInterfaceHLSLSymbol));

		InputParam.Bind(ParameterMap, *(UNiagaraDataInterfaceRenderTargetCube::InputName + ParameterInfo.DataInterfaceHLSLSymbol));
		InputSamplerStateParam.Bind(ParameterMap, *(UNiagaraDataInterfaceRenderTargetCube::InputName + TEXT("SamplerState") + ParameterInfo.DataInterfaceHLSLSymbol));
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		// Get shader and DI
		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
		FNiagaraDataInterfaceProxyRenderTargetCubeProxy* VFDI = static_cast<FNiagaraDataInterfaceProxyRenderTargetCubeProxy*>(Context.DataInterface);
		
		FRenderTargetCubeRWInstanceData_RenderThread* ProxyData = VFDI->SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID);
		check(ProxyData);

		SetShaderValue(RHICmdList, ComputeShaderRHI, SizeParam, ProxyData->Size);	
		
		if ( OutputParam.IsUAVBound())
		{
			FRHIUnorderedAccessView* OutputUAV = ProxyData->UnorderedAccessViewRHI;
			if (OutputUAV)
			{
				RHICmdList.Transition(FRHITransitionInfo(OutputUAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
			}
			else
			{
				OutputUAV = Context.Batcher->GetEmptyUAVFromPool(RHICmdList, EPixelFormat::PF_A16B16G16R16, ENiagaraEmptyUAVType::Texture2DArray);
			}
			RHICmdList.SetUAVParameter(ComputeShaderRHI, OutputParam.GetUAVIndex(), OutputUAV);
		}

		if (InputParam.IsBound())
		{
			FRHITexture* TextureRHI = ProxyData->TextureRHI;
			if (!ensureMsgf(!OutputParam.IsUAVBound(), TEXT("NiagaraDIRenderTargetCube(%s) is bound as both read & write, read will be ignored."), *Context.DataInterface->SourceDIName.ToString()))
			{
				//-TODO: Feedback to the user that read & write is bound
				TextureRHI = nullptr;
			}

			if (TextureRHI == nullptr)
			{
				TextureRHI = GBlackTextureCube->TextureRHI;
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

			FNiagaraDataInterfaceProxyRenderTargetCubeProxy* VFDI = static_cast<FNiagaraDataInterfaceProxyRenderTargetCubeProxy*>(Context.DataInterface);
			if (FRenderTargetCubeRWInstanceData_RenderThread* ProxyData = VFDI->SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID))
			{
				if (FRHIUnorderedAccessView* OutputUAV = ProxyData->UnorderedAccessViewRHI)
				{
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

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_RenderTargetCube);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceRenderTargetCube, FNiagaraDataInterfaceParametersCS_RenderTargetCube);

/*--------------------------------------------------------------------------------------------------------------------------*/

#if STATS
void FRenderTargetCubeRWInstanceData_RenderThread::UpdateMemoryStats()
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

UNiagaraDataInterfaceRenderTargetCube::UNiagaraDataInterfaceRenderTargetCube(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyRenderTargetCubeProxy());

	FNiagaraTypeDefinition Def(UTextureRenderTarget::StaticClass());
	RenderTargetUserParameter.Parameter.SetType(Def);
}

void UNiagaraDataInterfaceRenderTargetCube::PostInitProperties()
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

void UNiagaraDataInterfaceRenderTargetCube::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	Super::GetFunctions(OutFunctions);

	const int32 EmitterSystemOnlyBitmask = ENiagaraScriptUsageMask::Emitter | ENiagaraScriptUsageMask::System;
	OutFunctions.Reserve(OutFunctions.Num() + 4);

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = GetSizeFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("RenderTarget")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Size")));

		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
	#if WITH_EDITORONLY_DATA
		Sig.FunctionVersion = FNDIRenderTargetCubeFunctionVersion::LatestVersion;
	#endif
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = SetSizeFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("RenderTarget")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Size")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Success")));

		Sig.ModuleUsageBitmask = EmitterSystemOnlyBitmask;
		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresExecPin = true;
		Sig.bRequiresContext = false;
		Sig.bSupportsCPU = true;
		Sig.bSupportsGPU = false;
	#if WITH_EDITORONLY_DATA
		Sig.FunctionVersion = FNDIRenderTargetCubeFunctionVersion::LatestVersion;
	#endif
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = SetValueFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("RenderTarget")));
		Sig.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Enabled"))).SetValue(true);
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Face")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Value")));

		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresExecPin = true;
		Sig.bRequiresContext = false;
		Sig.bWriteFunction = true;
		Sig.bSupportsCPU = false;
		Sig.bSupportsGPU = true;
	#if WITH_EDITORONLY_DATA
		Sig.FunctionVersion = FNDIRenderTargetCubeFunctionVersion::LatestVersion;
	#endif
	}

	extern int GNiagaraRenderTargetAllowReads;
	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = GetValueFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("RenderTarget")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Face")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Value")));

		Sig.bHidden = GNiagaraRenderTargetAllowReads != 1;
		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bWriteFunction = true;
		Sig.bSupportsCPU = false;
		Sig.bSupportsGPU = true;
#if WITH_EDITORONLY_DATA
		Sig.FunctionVersion = FNDIRenderTargetCubeFunctionVersion::LatestVersion;
#endif
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = SampleValueFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("RenderTarget")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("UVW")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Value")));

		Sig.bHidden = GNiagaraRenderTargetAllowReads != 1;
		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bWriteFunction = true;
		Sig.bSupportsCPU = false;
		Sig.bSupportsGPU = true;
#if WITH_EDITORONLY_DATA
		Sig.FunctionVersion = FNDIRenderTargetCubeFunctionVersion::LatestVersion;
#endif
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = LinearToIndexName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("RenderTarget")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Linear")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Face")));

		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bWriteFunction = true;
		Sig.bSupportsCPU = false;
		Sig.bSupportsGPU = true;
	#if WITH_EDITORONLY_DATA
		Sig.FunctionVersion = FNDIRenderTargetCubeFunctionVersion::LatestVersion;
	#endif
	}
}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfaceRenderTargetCube::UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature)
{
	bool bWasChanged = false;

	if (FunctionSignature.FunctionVersion < FNDIRenderTargetCubeFunctionVersion::AddedOptionalExecute)
	{
		if (FunctionSignature.Name == SetValueFunctionName)
		{
			check(FunctionSignature.Inputs.Num() == 5);
			FunctionSignature.Inputs.Insert_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Enabled")), 1).SetValue(true);
			bWasChanged = true;
		}
	}

	// Set latest version
	FunctionSignature.FunctionVersion = FNDIRenderTargetCubeFunctionVersion::LatestVersion;

	return bWasChanged;
}
#endif

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceRenderTargetCube, GetSize);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceRenderTargetCube, SetSize);
void UNiagaraDataInterfaceRenderTargetCube::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	Super::GetVMExternalFunction(BindingInfo, InstanceData, OutFunc);
	if (BindingInfo.Name == GetSizeFunctionName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceRenderTargetCube, GetSize)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SetSizeFunctionName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceRenderTargetCube, SetSize)::Bind(this, OutFunc);
	}
}

bool UNiagaraDataInterfaceRenderTargetCube::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	
	const UNiagaraDataInterfaceRenderTargetCube* OtherTyped = CastChecked<const UNiagaraDataInterfaceRenderTargetCube>(Other);
	return
		OtherTyped != nullptr &&
#if WITH_EDITORONLY_DATA
		OtherTyped->bPreviewRenderTarget == bPreviewRenderTarget &&
#endif
		OtherTyped->RenderTargetUserParameter == RenderTargetUserParameter &&
		OtherTyped->Size == Size &&
		OtherTyped->OverrideRenderTargetFormat == OverrideRenderTargetFormat &&
		OtherTyped->bInheritUserParameterSettings == bInheritUserParameterSettings &&
		OtherTyped->bOverrideFormat == bOverrideFormat;
}

bool UNiagaraDataInterfaceRenderTargetCube::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if ( !Super::CopyToInternal(Destination) )
	{
		return false;
	}

	UNiagaraDataInterfaceRenderTargetCube* DestinationTyped = CastChecked<UNiagaraDataInterfaceRenderTargetCube>(Destination);
	if (!DestinationTyped)
	{
		return false;
	}

	DestinationTyped->Size = Size;
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
void UNiagaraDataInterfaceRenderTargetCube::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	Super::GetParameterDefinitionHLSL(ParamInfo, OutHLSL);

	static const TCHAR *FormatDeclarations = TEXT(R"(				
		RWTexture2DArray<float4> {OutputName};
		TextureCube<float4> {InputName};
		SamplerState {InputName}SamplerState;
		int {SizeName};
	)");
	TMap<FString, FStringFormatArg> ArgsDeclarations =
	{
		{TEXT("OutputName"),	RWOutputName + ParamInfo.DataInterfaceHLSLSymbol},
		{TEXT("InputName"),		InputName + ParamInfo.DataInterfaceHLSLSymbol },
		{TEXT("SizeName"),		SizeName + ParamInfo.DataInterfaceHLSLSymbol},
	};
	OutHLSL += FString::Format(FormatDeclarations, ArgsDeclarations);
}

bool UNiagaraDataInterfaceRenderTargetCube::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	bool ParentRet = Super::GetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, OutHLSL);
	if (ParentRet)
	{
		return true;
	} 

	TMap<FString, FStringFormatArg> ArgsDeclarations =
	{
		{TEXT("FunctionName"),	FunctionInfo.InstanceName},
		{TEXT("OutputName"),	RWOutputName + ParamInfo.DataInterfaceHLSLSymbol},
		{TEXT("InputName"),		InputName + ParamInfo.DataInterfaceHLSLSymbol },
		{TEXT("SizeName"),		SizeName + ParamInfo.DataInterfaceHLSLSymbol},
	};

	if (FunctionInfo.DefinitionName == SetValueFunctionName)
	{
		static const TCHAR* FormatBounds = TEXT(R"(
			void {FunctionName}(bool bEnabled, int IndexX, int IndexY, int Face, float4 Value)
			{
				if ( bEnabled )
				{
					{OutputName}[int3(IndexX, IndexY, Face)] = Value;
				}
			}
		)");
		OutHLSL += FString::Format(FormatBounds, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetValueFunctionName)
	{
		static const TCHAR* FormatBounds = TEXT(R"(
			void {FunctionName}(int In_IndexX, int In_IndexY, int In_Face, out float4 Out_Value)
			{			
				Out_Value = {InputName}.Load(int4(In_IndexX, In_IndexY, In_Face, 0));
			}
		)");
		OutHLSL += FString::Format(FormatBounds, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleValueFunctionName)
	{
		static const TCHAR* FormatBounds = TEXT(R"(
			void {FunctionName}(float3 UVW, out float4 Out_Value)
			{			
				Out_Value = {InputName}.SampleLevel({InputName}SamplerState, UVW, 0.0f);
			}
		)");
		OutHLSL += FString::Format(FormatBounds, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == LinearToIndexName)
	{
		static const TCHAR* FormatBounds = TEXT(R"(
			void {FunctionName}(int Linear, out int OutIndexX, out int OutIndexY, out int OutFace)
			{
				OutIndexX = Linear % {SizeName};
				OutIndexY = (Linear / {SizeName}) % {SizeName};
				OutFace = Linear / ({SizeName} * {SizeName});
			}
		)");
		OutHLSL += FString::Format(FormatBounds, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetSizeFunctionName)
	{
		static const TCHAR* FormatBounds = TEXT(R"(
			void {FunctionName}(out int OutSize)
			{			
				OutSize = {SizeName};
			}
		)");
		OutHLSL += FString::Format(FormatBounds, ArgsDeclarations);
		return true;
	}

	return false;
}
#endif

bool UNiagaraDataInterfaceRenderTargetCube::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	check(Proxy);

	extern float GNiagaraRenderTargetResolutionMultiplier;
	FRenderTargetCubeRWInstanceData_GameThread* InstanceData = new (PerInstanceData) FRenderTargetCubeRWInstanceData_GameThread();
	InstanceData->Size = FMath::Clamp<int>(int(float(Size) * GNiagaraRenderTargetResolutionMultiplier), 1, GMaxCubeTextureDimensions);
	InstanceData->Format = GetPixelFormatFromRenderTargetFormat(bOverrideFormat ? OverrideRenderTargetFormat : GetDefault<UNiagaraSettings>()->DefaultRenderTargetFormat);
	InstanceData->RTUserParamBinding.Init(SystemInstance->GetInstanceParameters(), RenderTargetUserParameter.Parameter);
#if WITH_EDITORONLY_DATA
	InstanceData->bPreviewTexture = bPreviewRenderTarget;
#endif

	return true;
}

void UNiagaraDataInterfaceRenderTargetCube::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FRenderTargetCubeRWInstanceData_GameThread* InstanceData = static_cast<FRenderTargetCubeRWInstanceData_GameThread*>(PerInstanceData);

	InstanceData->~FRenderTargetCubeRWInstanceData_GameThread();

	FNiagaraDataInterfaceProxyRenderTargetCubeProxy* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyRenderTargetCubeProxy>();
	ENQUEUE_RENDER_COMMAND(FNiagaraDIDestroyInstanceData) (
		[RT_Proxy, InstanceID=SystemInstance->GetId(), Batcher=SystemInstance->GetBatcher()](FRHICommandListImmediate& CmdList)
		{
#if STATS
			if (FRenderTargetCubeRWInstanceData_RenderThread* TargetData = RT_Proxy->SystemInstancesToProxyData_RT.Find(InstanceID))
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
	extern int32 GNiagaraReleaseResourceOnRemove;
	UTextureRenderTargetCube* ExistingRenderTarget = nullptr;
	if (ManagedRenderTargets.RemoveAndCopyValue(SystemInstance->GetId(), ExistingRenderTarget) && GNiagaraReleaseResourceOnRemove)
	{
		ExistingRenderTarget->ReleaseResource();
	}
}


void UNiagaraDataInterfaceRenderTargetCube::GetExposedVariables(TArray<FNiagaraVariableBase>& OutVariables) const
{
	OutVariables.Emplace(ExposedRTVar);
}

bool UNiagaraDataInterfaceRenderTargetCube::GetExposedVariableValue(const FNiagaraVariableBase& InVariable, void* InPerInstanceData, FNiagaraSystemInstance* InSystemInstance, void* OutData) const
{
	FRenderTargetCubeRWInstanceData_GameThread* InstanceData = static_cast<FRenderTargetCubeRWInstanceData_GameThread*>(InPerInstanceData);
	if (InVariable.IsValid() && InVariable == ExposedRTVar && InstanceData && InstanceData->TargetTexture)
	{
		UObject** Var = (UObject**)OutData;
		*Var = InstanceData->TargetTexture;
		return true;
	}
	return false;
}

void UNiagaraDataInterfaceRenderTargetCube::SetSize(FVectorVMContext& Context)
{
	// This should only be called from a system or emitter script due to a need for only setting up initially.
	VectorVM::FUserPtrHandler<FRenderTargetCubeRWInstanceData_GameThread> InstData(Context);
	FNDIInputParam<int> InSize(Context);
	FNDIOutputParam<FNiagaraBool> OutSuccess(Context);

	extern float GNiagaraRenderTargetResolutionMultiplier;
	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		const int NewSize = InSize.GetAndAdvance();
		const bool bSuccess = (InstData.Get() != nullptr && Context.NumInstances == 1 && NewSize > 0);
		OutSuccess.SetAndAdvance(bSuccess);
		if (bSuccess)
		{
			InstData->Size = FMath::Clamp<int>(int(float(NewSize) * GNiagaraRenderTargetResolutionMultiplier), 1, GMaxCubeTextureDimensions);
		}
	}
}

void UNiagaraDataInterfaceRenderTargetCube::GetSize(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FRenderTargetCubeRWInstanceData_GameThread> InstData(Context);
	FNDIOutputParam<int> OutSize(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		OutSize.SetAndAdvance(InstData->Size);
	}
}


bool UNiagaraDataInterfaceRenderTargetCube::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	FRenderTargetCubeRWInstanceData_GameThread* InstanceData = static_cast<FRenderTargetCubeRWInstanceData_GameThread*>(PerInstanceData);

	// Pull from user parameter
	UTextureRenderTargetCube* UserTargetTexture = InstanceData->RTUserParamBinding.GetValue<UTextureRenderTargetCube>();
	if (UserTargetTexture && (InstanceData->TargetTexture != UserTargetTexture))
	{
		InstanceData->TargetTexture = UserTargetTexture;

		extern int32 GNiagaraReleaseResourceOnRemove;
		UTextureRenderTargetCube* ExistingRenderTarget = nullptr;
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
			InstanceData->Size = UserTargetTexture->SizeX;
			//if (UserTargetTexture->bAutoGenerateMips)
			//{
			//	// We have to take a guess at user intention
			//	InstanceData->MipMapGeneration = MipMapGeneration == ENiagaraMipMapGeneration::Disabled ? ENiagaraMipMapGeneration::PostStage : MipMapGeneration;
			//}
			//else
			//{
			//	InstanceData->MipMapGeneration = ENiagaraMipMapGeneration::Disabled;
			//}
			InstanceData->Format = InstanceData->TargetTexture->OverrideFormat;
		}
		else
		{
			UE_LOG(LogNiagara, Error, TEXT("RenderTarget UserParam is required but invalid."));
		}
	}
		
	return false;
}

bool UNiagaraDataInterfaceRenderTargetCube::PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	// Update InstanceData as the texture may have changed
	FRenderTargetCubeRWInstanceData_GameThread* InstanceData = static_cast<FRenderTargetCubeRWInstanceData_GameThread*>(PerInstanceData);
#if WITH_EDITORONLY_DATA
	InstanceData->bPreviewTexture = bPreviewRenderTarget;
#endif

	// Do we need to create a new texture?
	if (!bInheritUserParameterSettings && (InstanceData->TargetTexture == nullptr))
	{
		InstanceData->TargetTexture = NewObject<UTextureRenderTargetCube>(this);
		InstanceData->TargetTexture->bCanCreateUAV = true;
		//InstanceData->TargetTexture->bAutoGenerateMips = InstanceData->MipMapGeneration != ENiagaraMipMapGeneration::Disabled;
		InstanceData->TargetTexture->ClearColor = FLinearColor(0.0, 0, 0, 0);
		InstanceData->TargetTexture->Init(InstanceData->Size, InstanceData->Format);
		InstanceData->TargetTexture->UpdateResourceImmediate(true);

		ManagedRenderTargets.Add(SystemInstance->GetId()) = InstanceData->TargetTexture;
	}

	// Do we need to update the existing texture?
	if (InstanceData->TargetTexture)
	{
		//const bool bAutoGenerateMips = InstanceData->MipMapGeneration != ENiagaraMipMapGeneration::Disabled;
		if ((InstanceData->TargetTexture->SizeX != InstanceData->Size) || 
			(InstanceData->TargetTexture->OverrideFormat != InstanceData->Format) ||
			!InstanceData->TargetTexture->bCanCreateUAV ||
			//(InstanceData->TargetTexture->bAutoGenerateMips != bAutoGenerateMips) ||
			!InstanceData->TargetTexture->Resource )
		{
			// resize RT to match what we need for the output
			InstanceData->TargetTexture->bCanCreateUAV = true;
			//InstanceData->TargetTexture->bAutoGenerateMips = bAutoGenerateMips;
			InstanceData->TargetTexture->Init(InstanceData->Size, InstanceData->Format);
			InstanceData->TargetTexture->UpdateResourceImmediate(true);
		}
	}

	//-TODO: We could avoid updating each frame if we cache the resource pointer or a serial number
	bool bUpdateRT = true;
	if (bUpdateRT)
	{
		FNiagaraDataInterfaceProxyRenderTargetCubeProxy* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyRenderTargetCubeProxy>();
		FTextureRenderTargetResource* RT_TargetTexture = InstanceData->TargetTexture ? InstanceData->TargetTexture->GameThread_GetRenderTargetResource() : nullptr;
		ENQUEUE_RENDER_COMMAND(NDIRenderTargetCubeUpdate)
		(
			[RT_Proxy, RT_InstanceID=SystemInstance->GetId(), RT_InstanceData=*InstanceData, RT_TargetTexture](FRHICommandListImmediate& RHICmdList)
			{
				FRenderTargetCubeRWInstanceData_RenderThread* TargetData = &RT_Proxy->SystemInstancesToProxyData_RT.FindOrAdd(RT_InstanceID);
				TargetData->Size = RT_InstanceData.Size;
				//TargetData->MipMapGeneration = RT_InstanceData.MipMapGeneration;
			#if WITH_EDITORONLY_DATA
				TargetData->bPreviewTexture = RT_InstanceData.bPreviewTexture;
			#endif
				TargetData->SamplerStateRHI.SafeRelease();
				TargetData->TextureRHI.SafeRelease();
				TargetData->UnorderedAccessViewRHI.SafeRelease();
				if (RT_TargetTexture)
				{
					if (FTextureRenderTargetCubeResource* ResourceCube = RT_TargetTexture->GetTextureRenderTargetCubeResource())
					{
						TargetData->SamplerStateRHI = ResourceCube->SamplerStateRHI;
						TargetData->TextureRHI = ResourceCube->GetTextureRHI();
						TargetData->UnorderedAccessViewRHI = ResourceCube->GetUnorderedAccessViewRHI();
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

void FNiagaraDataInterfaceProxyRenderTargetCubeProxy::PostSimulate(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceArgs& Context)
{
#if NIAGARA_COMPUTEDEBUG_ENABLED
	FRenderTargetCubeRWInstanceData_RenderThread* ProxyData = SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID);

	if (ProxyData && ProxyData->bPreviewTexture && ProxyData->TextureRHI.IsValid())
	{
		if (FNiagaraGpuComputeDebug* GpuComputeDebug = Context.Batcher->GetGpuComputeDebug())
		{
			if ( FRHITexture* RHITexture = ProxyData->TextureRHI )
			{
				GpuComputeDebug->AddTexture(RHICmdList, Context.SystemInstanceID, SourceDIName, RHITexture);
			}
		}
	}
#endif
}

FIntVector FNiagaraDataInterfaceProxyRenderTargetCubeProxy::GetElementCount(FNiagaraSystemInstanceID SystemInstanceID) const
{
	if ( const FRenderTargetCubeRWInstanceData_RenderThread* TargetData = SystemInstancesToProxyData_RT.Find(SystemInstanceID) )
	{
		return FIntVector(TargetData->Size, TargetData->Size, 6);
	}
	return FIntVector::ZeroValue;
}

#undef LOCTEXT_NAMESPACE
