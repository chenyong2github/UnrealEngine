// Copyright Epic Games, Inc. All Rights Reserved.
#include "NiagaraDataInterfaceRenderTargetVolume.h"
#include "NiagaraShader.h"
#include "ShaderParameterUtils.h"
#include "ClearQuad.h"
#include "TextureResource.h"
#include "TextureRenderTargetVolumeResource.h"
#include "Engine/TextureRenderTargetVolume.h"

#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraStats.h"
#include "NiagaraRenderer.h"
#include "NiagaraSettings.h"
#if WITH_EDITOR
#include "NiagaraGpuComputeDebug.h"
#endif

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceRenderTargetVolume"

const FString UNiagaraDataInterfaceRenderTargetVolume::SizeName(TEXT("Size_"));
const FString UNiagaraDataInterfaceRenderTargetVolume::RWOutputName(TEXT("RWOutput_"));
const FString UNiagaraDataInterfaceRenderTargetVolume::OutputName(TEXT("Output_"));
const FString UNiagaraDataInterfaceRenderTargetVolume::InputName(TEXT("Input_"));

// Global VM function names, also used by the shaders code generation methods.
const FName UNiagaraDataInterfaceRenderTargetVolume::SetValueFunctionName("SetRenderTargetValue");
const FName UNiagaraDataInterfaceRenderTargetVolume::GetValueFunctionName("GetRenderTargetValue");
const FName UNiagaraDataInterfaceRenderTargetVolume::SampleValueFunctionName("SampleRenderTargetValue");
const FName UNiagaraDataInterfaceRenderTargetVolume::SetSizeFunctionName("SetRenderTargetSize");
const FName UNiagaraDataInterfaceRenderTargetVolume::GetSizeFunctionName("GetRenderTargetSize");
const FName UNiagaraDataInterfaceRenderTargetVolume::LinearToIndexName("LinearToIndex");

FNiagaraVariableBase UNiagaraDataInterfaceRenderTargetVolume::ExposedRTVar;

/*--------------------------------------------------------------------------------------------------------------------------*/

struct FNDIRenderTargetVolumeFunctionVersion
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
struct FNiagaraDataInterfaceParametersCS_RenderTargetVolume : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_RenderTargetVolume, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{			
		SizeParam.Bind(ParameterMap, *(UNiagaraDataInterfaceRenderTargetVolume::SizeName + ParameterInfo.DataInterfaceHLSLSymbol));
		OutputParam.Bind(ParameterMap, *(UNiagaraDataInterfaceRenderTargetVolume::OutputName + ParameterInfo.DataInterfaceHLSLSymbol));

		InputParam.Bind(ParameterMap, *(UNiagaraDataInterfaceRenderTargetVolume::InputName + ParameterInfo.DataInterfaceHLSLSymbol));
		InputSamplerStateParam.Bind(ParameterMap, *(UNiagaraDataInterfaceRenderTargetVolume::InputName + TEXT("SamplerState") + ParameterInfo.DataInterfaceHLSLSymbol));
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		// Get shader and DI
		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
		FNiagaraDataInterfaceProxyRenderTargetVolumeProxy* VFDI = static_cast<FNiagaraDataInterfaceProxyRenderTargetVolumeProxy*>(Context.DataInterface);
		
		FRenderTargetVolumeRWInstanceData_RenderThread* ProxyData = VFDI->SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID);
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
				OutputUAV = Context.Batcher->GetEmptyUAVFromPool(RHICmdList, EPixelFormat::PF_A16B16G16R16, ENiagaraEmptyUAVType::Texture3D);
			}
			RHICmdList.SetUAVParameter(ComputeShaderRHI, OutputParam.GetUAVIndex(), OutputUAV);
		}

		if (InputParam.IsBound())
		{
			FRHITexture* TextureRHI = ProxyData->TextureRHI;
			if (!ensureMsgf(!OutputParam.IsUAVBound(), TEXT("NiagaraDIRenderTargetVolume(%s) is bound as both read & write, read will be ignored."), *Context.DataInterface->SourceDIName.ToString()))
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

			FNiagaraDataInterfaceProxyRenderTargetVolumeProxy* VFDI = static_cast<FNiagaraDataInterfaceProxyRenderTargetVolumeProxy*>(Context.DataInterface);
			if (FRenderTargetVolumeRWInstanceData_RenderThread* ProxyData = VFDI->SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID))
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

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_RenderTargetVolume);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceRenderTargetVolume, FNiagaraDataInterfaceParametersCS_RenderTargetVolume);

/*--------------------------------------------------------------------------------------------------------------------------*/

#if STATS
void FRenderTargetVolumeRWInstanceData_RenderThread::UpdateMemoryStats()
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

UNiagaraDataInterfaceRenderTargetVolume::UNiagaraDataInterfaceRenderTargetVolume(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyRenderTargetVolumeProxy());

	FNiagaraTypeDefinition Def(UTextureRenderTarget::StaticClass());
	RenderTargetUserParameter.Parameter.SetType(Def);
}

void UNiagaraDataInterfaceRenderTargetVolume::PostInitProperties()
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

void UNiagaraDataInterfaceRenderTargetVolume::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
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
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Depth")));

		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
	#if WITH_EDITORONLY_DATA
		Sig.FunctionVersion = FNDIRenderTargetVolumeFunctionVersion::LatestVersion;
	#endif
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = SetSizeFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("RenderTarget")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Width")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Height")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Depth")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Success")));

		Sig.ModuleUsageBitmask = EmitterSystemOnlyBitmask;
		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresExecPin = true;
		Sig.bRequiresContext = false;
		Sig.bSupportsCPU = true;
		Sig.bSupportsGPU = false;
	#if WITH_EDITORONLY_DATA
		Sig.FunctionVersion = FNDIRenderTargetVolumeFunctionVersion::LatestVersion;
	#endif
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = SetValueFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("RenderTarget")));
		Sig.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Enabled"))).SetValue(true);
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexZ")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Value")));

		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresExecPin = true;
		Sig.bRequiresContext = false;
		Sig.bWriteFunction = true;
		Sig.bSupportsCPU = false;
		Sig.bSupportsGPU = true;
	#if WITH_EDITORONLY_DATA
		Sig.FunctionVersion = FNDIRenderTargetVolumeFunctionVersion::LatestVersion;
	#endif
	}

	extern int GNiagaraRenderTargetAllowReads;
	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = GetValueFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("RenderTarget")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexZ")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Value")));

		Sig.bHidden = GNiagaraRenderTargetAllowReads != 1;
		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bWriteFunction = true;
		Sig.bSupportsCPU = false;
		Sig.bSupportsGPU = true;
#if WITH_EDITORONLY_DATA
		Sig.FunctionVersion = FNDIRenderTargetVolumeFunctionVersion::LatestVersion;
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
		Sig.FunctionVersion = FNDIRenderTargetVolumeFunctionVersion::LatestVersion;
#endif
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = LinearToIndexName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("RenderTarget")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Linear")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexZ")));

		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bWriteFunction = true;
		Sig.bSupportsCPU = false;
		Sig.bSupportsGPU = true;
	#if WITH_EDITORONLY_DATA
		Sig.FunctionVersion = FNDIRenderTargetVolumeFunctionVersion::LatestVersion;
	#endif
	}
}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfaceRenderTargetVolume::UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature)
{
	bool bWasChanged = false;

	if (FunctionSignature.FunctionVersion < FNDIRenderTargetVolumeFunctionVersion::AddedOptionalExecute)
	{
		if (FunctionSignature.Name == SetValueFunctionName)
		{
			check(FunctionSignature.Inputs.Num() == 5);
			FunctionSignature.Inputs.Insert_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Enabled")), 1).SetValue(true);
			bWasChanged = true;
		}
	}

	// Set latest version
	FunctionSignature.FunctionVersion = FNDIRenderTargetVolumeFunctionVersion::LatestVersion;

	return bWasChanged;
}
#endif

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceRenderTargetVolume, GetSize);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceRenderTargetVolume, SetSize);
void UNiagaraDataInterfaceRenderTargetVolume::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	Super::GetVMExternalFunction(BindingInfo, InstanceData, OutFunc);
	if (BindingInfo.Name == GetSizeFunctionName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceRenderTargetVolume, GetSize)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SetSizeFunctionName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceRenderTargetVolume, SetSize)::Bind(this, OutFunc);
	}
}

bool UNiagaraDataInterfaceRenderTargetVolume::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	
	const UNiagaraDataInterfaceRenderTargetVolume* OtherTyped = CastChecked<const UNiagaraDataInterfaceRenderTargetVolume>(Other);
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

bool UNiagaraDataInterfaceRenderTargetVolume::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if ( !Super::CopyToInternal(Destination) )
	{
		return false;
	}

	UNiagaraDataInterfaceRenderTargetVolume* DestinationTyped = CastChecked<UNiagaraDataInterfaceRenderTargetVolume>(Destination);
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
void UNiagaraDataInterfaceRenderTargetVolume::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	Super::GetParameterDefinitionHLSL(ParamInfo, OutHLSL);

	static const TCHAR *FormatDeclarations = TEXT(R"(				
		RWTexture3D<float4> {OutputName};
		Texture3D<float4> {InputName};
		SamplerState {InputName}SamplerState;
		int3 {SizeName};
	)");
	TMap<FString, FStringFormatArg> ArgsDeclarations =
	{
		{TEXT("OutputName"),	RWOutputName + ParamInfo.DataInterfaceHLSLSymbol},
		{TEXT("InputName"),		InputName + ParamInfo.DataInterfaceHLSLSymbol },
		{TEXT("SizeName"),		SizeName + ParamInfo.DataInterfaceHLSLSymbol},
	};
	OutHLSL += FString::Format(FormatDeclarations, ArgsDeclarations);
}

bool UNiagaraDataInterfaceRenderTargetVolume::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
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
			void {FunctionName}(bool bEnabled, int IndexX, int IndexY, int IndexZ, float4 Value)
			{
				if ( bEnabled )
				{
					{OutputName}[int3(IndexX, IndexY, IndexZ)] = Value;
				}
			}
		)");
		OutHLSL += FString::Format(FormatBounds, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetValueFunctionName)
	{
		static const TCHAR* FormatBounds = TEXT(R"(
			void {FunctionName}(int In_IndexX, int In_IndexY, int In_IndexZ, out float4 Out_Value)
			{			
				Out_Value = {InputName}.Load(int4(In_IndexX, In_IndexY, In_IndexZ, 0));
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
			void {FunctionName}(int Linear, out int OutIndexX, out int OutIndexY, out int OutIndexZ)
			{
				OutIndexX = Linear % {SizeName}.x;
				OutIndexY = (Linear / {SizeName}.x) % {SizeName}.y;
				OutIndexZ = Linear / ({SizeName}.x * {SizeName}.y);
			}
		)");
		OutHLSL += FString::Format(FormatBounds, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetSizeFunctionName)
	{
		static const TCHAR* FormatBounds = TEXT(R"(
			void {FunctionName}(out int OutWidth, out int OutHeight, out int OutDepth)
			{			
				OutWidth = {SizeName}.x;
				OutHeight = {SizeName}.y;
				OutDepth = {SizeName}.z;
			}
		)");
		OutHLSL += FString::Format(FormatBounds, ArgsDeclarations);
		return true;
	}

	return false;
}
#endif

bool UNiagaraDataInterfaceRenderTargetVolume::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	check(Proxy);

	extern float GNiagaraRenderTargetResolutionMultiplier;
	FRenderTargetVolumeRWInstanceData_GameThread* InstanceData = new (PerInstanceData) FRenderTargetVolumeRWInstanceData_GameThread();
	InstanceData->Size.X = FMath::Clamp<int>(int(float(Size.X) * GNiagaraRenderTargetResolutionMultiplier), 1, GMaxVolumeTextureDimensions);
	InstanceData->Size.Y = FMath::Clamp<int>(int(float(Size.Y) * GNiagaraRenderTargetResolutionMultiplier), 1, GMaxVolumeTextureDimensions);
	InstanceData->Size.Z = FMath::Clamp<int>(int(float(Size.Z) * GNiagaraRenderTargetResolutionMultiplier), 1, GMaxVolumeTextureDimensions);
	InstanceData->Format = GetPixelFormatFromRenderTargetFormat(bOverrideFormat ? OverrideRenderTargetFormat : GetDefault<UNiagaraSettings>()->DefaultRenderTargetFormat);
	InstanceData->RTUserParamBinding.Init(SystemInstance->GetInstanceParameters(), RenderTargetUserParameter.Parameter);
#if WITH_EDITORONLY_DATA
	InstanceData->bPreviewTexture = bPreviewRenderTarget;
#endif

	return true;
}

void UNiagaraDataInterfaceRenderTargetVolume::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FRenderTargetVolumeRWInstanceData_GameThread* InstanceData = static_cast<FRenderTargetVolumeRWInstanceData_GameThread*>(PerInstanceData);

	InstanceData->~FRenderTargetVolumeRWInstanceData_GameThread();

	FNiagaraDataInterfaceProxyRenderTargetVolumeProxy* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyRenderTargetVolumeProxy>();
	ENQUEUE_RENDER_COMMAND(FNiagaraDIDestroyInstanceData) (
		[RT_Proxy, InstanceID=SystemInstance->GetId(), Batcher=SystemInstance->GetBatcher()](FRHICommandListImmediate& CmdList)
		{
#if STATS
			if (FRenderTargetVolumeRWInstanceData_RenderThread* TargetData = RT_Proxy->SystemInstancesToProxyData_RT.Find(InstanceID))
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
	UTextureRenderTargetVolume* ExistingRenderTarget = nullptr;
	if (ManagedRenderTargets.RemoveAndCopyValue(SystemInstance->GetId(), ExistingRenderTarget) && GNiagaraReleaseResourceOnRemove)
	{
		ExistingRenderTarget->ReleaseResource();
	}
}


void UNiagaraDataInterfaceRenderTargetVolume::GetExposedVariables(TArray<FNiagaraVariableBase>& OutVariables) const
{
	OutVariables.Emplace(ExposedRTVar);
}

bool UNiagaraDataInterfaceRenderTargetVolume::GetExposedVariableValue(const FNiagaraVariableBase& InVariable, void* InPerInstanceData, FNiagaraSystemInstance* InSystemInstance, void* OutData) const
{
	FRenderTargetVolumeRWInstanceData_GameThread* InstanceData = static_cast<FRenderTargetVolumeRWInstanceData_GameThread*>(InPerInstanceData);
	if (InVariable.IsValid() && InVariable == ExposedRTVar && InstanceData && InstanceData->TargetTexture)
	{
		UObject** Var = (UObject**)OutData;
		*Var = InstanceData->TargetTexture;
		return true;
	}
	return false;
}

void UNiagaraDataInterfaceRenderTargetVolume::SetSize(FVectorVMContext& Context)
{
	// This should only be called from a system or emitter script due to a need for only setting up initially.
	VectorVM::FUserPtrHandler<FRenderTargetVolumeRWInstanceData_GameThread> InstData(Context);
	FNDIInputParam<int> InSizeX(Context);
	FNDIInputParam<int> InSizeY(Context);
	FNDIInputParam<int> InSizeZ(Context);
	FNDIOutputParam<FNiagaraBool> OutSuccess(Context);

	extern float GNiagaraRenderTargetResolutionMultiplier;
	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		const int SizeX = InSizeX.GetAndAdvance();
		const int SizeY = InSizeY.GetAndAdvance();
		const int SizeZ = InSizeZ.GetAndAdvance();
		const bool bSuccess = (InstData.Get() != nullptr && Context.NumInstances == 1 && SizeX > 0 && SizeY > 0 && SizeZ > 0);
		OutSuccess.SetAndAdvance(bSuccess);
		if (bSuccess)
		{
			InstData->Size.X = FMath::Clamp<int>(int(float(SizeX) * GNiagaraRenderTargetResolutionMultiplier), 1, GMaxVolumeTextureDimensions);
			InstData->Size.Y = FMath::Clamp<int>(int(float(SizeY) * GNiagaraRenderTargetResolutionMultiplier), 1, GMaxVolumeTextureDimensions);
			InstData->Size.Z = FMath::Clamp<int>(int(float(SizeZ) * GNiagaraRenderTargetResolutionMultiplier), 1, GMaxVolumeTextureDimensions);
		}
	}
}

void UNiagaraDataInterfaceRenderTargetVolume::GetSize(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FRenderTargetVolumeRWInstanceData_GameThread> InstData(Context);
	FNDIOutputParam<int> OutSizeX(Context);
	FNDIOutputParam<int> OutSizeY(Context);
	FNDIOutputParam<int> OutSizeZ(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		OutSizeX.SetAndAdvance(InstData->Size.X);
		OutSizeY.SetAndAdvance(InstData->Size.Y);
		OutSizeZ.SetAndAdvance(InstData->Size.Z);
	}
}


bool UNiagaraDataInterfaceRenderTargetVolume::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	FRenderTargetVolumeRWInstanceData_GameThread* InstanceData = static_cast<FRenderTargetVolumeRWInstanceData_GameThread*>(PerInstanceData);

	// Pull from user parameter
	UTextureRenderTargetVolume* UserTargetTexture = InstanceData->RTUserParamBinding.GetValue<UTextureRenderTargetVolume>();
	if (UserTargetTexture && (InstanceData->TargetTexture != UserTargetTexture))
	{
		InstanceData->TargetTexture = UserTargetTexture;

		extern int32 GNiagaraReleaseResourceOnRemove;
		UTextureRenderTargetVolume* ExistingRenderTarget = nullptr;
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
			InstanceData->Size.Z = UserTargetTexture->SizeZ;
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

bool UNiagaraDataInterfaceRenderTargetVolume::PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	// Update InstanceData as the texture may have changed
	FRenderTargetVolumeRWInstanceData_GameThread* InstanceData = static_cast<FRenderTargetVolumeRWInstanceData_GameThread*>(PerInstanceData);
#if WITH_EDITORONLY_DATA
	InstanceData->bPreviewTexture = bPreviewRenderTarget;
#endif

	// Do we need to create a new texture?
	if (!bInheritUserParameterSettings && (InstanceData->TargetTexture == nullptr))
	{
		InstanceData->TargetTexture = NewObject<UTextureRenderTargetVolume>(this);
		InstanceData->TargetTexture->bCanCreateUAV = true;
		//InstanceData->TargetTexture->bAutoGenerateMips = InstanceData->MipMapGeneration != ENiagaraMipMapGeneration::Disabled;
		InstanceData->TargetTexture->ClearColor = FLinearColor(0.0, 0, 0, 0);
		InstanceData->TargetTexture->Init(InstanceData->Size.X, InstanceData->Size.Y, InstanceData->Size.Z, InstanceData->Format);
		InstanceData->TargetTexture->UpdateResourceImmediate(true);

		ManagedRenderTargets.Add(SystemInstance->GetId()) = InstanceData->TargetTexture;
	}

	// Do we need to update the existing texture?
	if (InstanceData->TargetTexture)
	{
		//const bool bAutoGenerateMips = InstanceData->MipMapGeneration != ENiagaraMipMapGeneration::Disabled;
		if ((InstanceData->TargetTexture->SizeX != InstanceData->Size.X) || (InstanceData->TargetTexture->SizeY != InstanceData->Size.Y) || (InstanceData->TargetTexture->SizeZ != InstanceData->Size.Z) ||
			(InstanceData->TargetTexture->OverrideFormat != InstanceData->Format) ||
			!InstanceData->TargetTexture->bCanCreateUAV ||
			//(InstanceData->TargetTexture->bAutoGenerateMips != bAutoGenerateMips) ||
			!InstanceData->TargetTexture->Resource )
		{
			// resize RT to match what we need for the output
			InstanceData->TargetTexture->bCanCreateUAV = true;
			//InstanceData->TargetTexture->bAutoGenerateMips = bAutoGenerateMips;
			InstanceData->TargetTexture->Init(InstanceData->Size.X, InstanceData->Size.Y, InstanceData->Size.Z, InstanceData->Format);
			InstanceData->TargetTexture->UpdateResourceImmediate(true);
		}
	}

	//-TODO: We could avoid updating each frame if we cache the resource pointer or a serial number
	bool bUpdateRT = true;
	if (bUpdateRT)
	{
		FNiagaraDataInterfaceProxyRenderTargetVolumeProxy* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyRenderTargetVolumeProxy>();
		FTextureRenderTargetResource* RT_TargetTexture = InstanceData->TargetTexture ? InstanceData->TargetTexture->GameThread_GetRenderTargetResource() : nullptr;
		ENQUEUE_RENDER_COMMAND(NDIRenderTargetVolumeUpdate)
		(
			[RT_Proxy, RT_InstanceID=SystemInstance->GetId(), RT_InstanceData=*InstanceData, RT_TargetTexture](FRHICommandListImmediate& RHICmdList)
			{
				FRenderTargetVolumeRWInstanceData_RenderThread* TargetData = &RT_Proxy->SystemInstancesToProxyData_RT.FindOrAdd(RT_InstanceID);
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
					if (FTextureRenderTargetVolumeResource* ResourceVolume = RT_TargetTexture->GetTextureRenderTargetVolumeResource())
					{
						TargetData->SamplerStateRHI = ResourceVolume->SamplerStateRHI;
						TargetData->TextureRHI = ResourceVolume->GetTextureRHI();
						TargetData->UnorderedAccessViewRHI = ResourceVolume->GetUnorderedAccessViewRHI();
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

void FNiagaraDataInterfaceProxyRenderTargetVolumeProxy::PostSimulate(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceArgs& Context)
{
#if NIAGARA_COMPUTEDEBUG_ENABLED
	FRenderTargetVolumeRWInstanceData_RenderThread* ProxyData = SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID);

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

FIntVector FNiagaraDataInterfaceProxyRenderTargetVolumeProxy::GetElementCount(FNiagaraSystemInstanceID SystemInstanceID) const
{
	if ( const FRenderTargetVolumeRWInstanceData_RenderThread* TargetData = SystemInstancesToProxyData_RT.Find(SystemInstanceID) )
	{
		return TargetData->Size;
	}
	return FIntVector::ZeroValue;
}

#undef LOCTEXT_NAMESPACE
