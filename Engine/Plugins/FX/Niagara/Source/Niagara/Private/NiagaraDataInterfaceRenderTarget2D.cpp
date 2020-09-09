// Copyright Epic Games, Inc. All Rights Reserved.
#include "NiagaraDataInterfaceRenderTarget2D.h"
#include "NiagaraShader.h"
#include "ShaderParameterUtils.h"
#include "ClearQuad.h"
#include "TextureResource.h"
#include "Engine/Texture2D.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraRenderer.h"
#include "Engine/TextureRenderTarget2D.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceRenderTarget2D"

const FString UNiagaraDataInterfaceRenderTarget2D::SizeName(TEXT("Size_"));

const FString UNiagaraDataInterfaceRenderTarget2D::OutputName(TEXT("Output_"));


// Global VM function names, also used by the shaders code generation methods.
const FName UNiagaraDataInterfaceRenderTarget2D::SetValueFunctionName("SetRenderTargetValue");
const FName UNiagaraDataInterfaceRenderTarget2D::GetValueFunctionName("GetRenderTargetValue");
const FName UNiagaraDataInterfaceRenderTarget2D::SetSizeFunctionName("SetRenderTargetSize");
const FName UNiagaraDataInterfaceRenderTarget2D::GetSizeFunctionName("GetRenderTargetSize");


FNiagaraVariableBase UNiagaraDataInterfaceRenderTarget2D::ExposedRTVar;


/*--------------------------------------------------------------------------------------------------------------------------*/
struct FNiagaraDataInterfaceParametersCS_RenderTarget2D : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_RenderTarget2D, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{			
		SizeParam.Bind(ParameterMap, *(UNiagaraDataInterfaceRenderTarget2D::SizeName + ParameterInfo.DataInterfaceHLSLSymbol));

		OutputParam.Bind(ParameterMap, *(UNiagaraDataInterfaceRenderTarget2D::OutputName + ParameterInfo.DataInterfaceHLSLSymbol));

	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		// Get shader and DI
		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
		FNiagaraDataInterfaceProxyRenderTarget2DProxy* VFDI = static_cast<FNiagaraDataInterfaceProxyRenderTarget2DProxy*>(Context.DataInterface);
		
		FRenderTarget2DRWInstanceData_RenderThread* ProxyData = VFDI->SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID);
		check(ProxyData);

		int SizeTmp[2];
		SizeTmp[0] = ProxyData->Size.X;
		SizeTmp[1] = ProxyData->Size.Y;
		SetShaderValue(RHICmdList, ComputeShaderRHI, SizeParam, SizeTmp);	
	
		
		if ( OutputParam.IsUAVBound())
		{

			FRHIUnorderedAccessView* OutputUAV = nullptr;

			if (Context.IsOutputStage && ProxyData->UAV.IsValid())
			{
				OutputUAV = ProxyData->UAV;
				// FIXME: this transition needs to happen in FNiagaraDataInterfaceProxyRenderTarget2DProxy::PreStage so it doesn't break up the overlap group,
				// but for some reason it stops working if I move it in there.
				RHICmdList.Transition(FRHITransitionInfo(OutputUAV, ERHIAccess::SRVMask, ERHIAccess::UAVCompute));
			}
			else
			{
				OutputUAV = Context.Batcher->GetEmptyRWTextureFromPool(RHICmdList, EPixelFormat::PF_A16B16G16R16);
			}
			RHICmdList.SetUAVParameter(ComputeShaderRHI, OutputParam.GetUAVIndex(), OutputUAV);
		}
	}

	void Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const 
	{
		if (OutputParam.IsBound())
		{
			FNiagaraDataInterfaceProxyRenderTarget2DProxy* VFDI = static_cast<FNiagaraDataInterfaceProxyRenderTarget2DProxy*>(Context.DataInterface);
			FRenderTarget2DRWInstanceData_RenderThread* ProxyData = VFDI->SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID);
			OutputParam.UnsetUAV(RHICmdList, Context.Shader.GetComputeShader());
			if (ProxyData)
			{
				// FIXME: move to FNiagaraDataInterfaceProxyRenderTarget2DProxy::PostStage, same as for the transition in Set() above.
				RHICmdList.Transition(FRHITransitionInfo(ProxyData->UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
			}
		}
	}

private:

	LAYOUT_FIELD(FShaderParameter, SizeParam);
	LAYOUT_FIELD(FRWShaderParameter, OutputParam);
};

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_RenderTarget2D);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceRenderTarget2D, FNiagaraDataInterfaceParametersCS_RenderTarget2D);


UNiagaraDataInterfaceRenderTarget2D::UNiagaraDataInterfaceRenderTarget2D(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyRenderTarget2DProxy());

	//FNiagaraTypeDefinition Def(UObject::StaticClass());
	//RenderTargetUserParameter.Parameter.SetType(Def);
}


void UNiagaraDataInterfaceRenderTarget2D::PostInitProperties()
{
	Super::PostInitProperties();

	//Can we register data interfaces as regular types and fold them into the FNiagaraVariable framework for UI and function calls etc?
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), /*bCanBeParameter*/ true, /*bCanBePayload*/ false, /*bIsUserDefined*/ false);

		ExposedRTVar = FNiagaraVariableBase(FNiagaraTypeDefinition(UTexture::StaticClass()), TEXT("RenderTarget"));
	}
}

void UNiagaraDataInterfaceRenderTarget2D::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	Super::GetFunctions(OutFunctions);

	const int32 EmitterSystemOnlyBitmask = ENiagaraScriptUsageMask::Emitter | ENiagaraScriptUsageMask::System;

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetSizeFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("RenderTarget")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Width")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Height")));

		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
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
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SetValueFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("RenderTarget")));
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
		OutFunctions.Add(Sig);
	}


}


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
	//const UNiagaraDataInterfaceRenderTarget2D* OtherTyped = CastChecked<const UNiagaraDataInterfaceRenderTarget2D>(Other);

	//return OtherTyped != nullptr && OtherTyped->RenderTargetUserParameter == RenderTargetUserParameter && OtherTyped->bCreateRenderTarget == bCreateRenderTarget;
	return true;
}

void UNiagaraDataInterfaceRenderTarget2D::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	Super::GetParameterDefinitionHLSL(ParamInfo, OutHLSL);

	static const TCHAR *FormatDeclarations = TEXT(R"(				
		RWTexture2D<float4> RW{OutputName};
	)");
	TMap<FString, FStringFormatArg> ArgsDeclarations = {				
		{ TEXT("OutputName"),    OutputName + ParamInfo.DataInterfaceHLSLSymbol },
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
	if (FunctionInfo.DefinitionName == SetValueFunctionName)
	{
		static const TCHAR* FormatBounds = TEXT(R"(
			void {FunctionName}(int In_IndexX, int In_IndexY, float4 In_Value)
			{			
				RW{Output}[int2(In_IndexX, In_IndexY)] = In_Value;
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("Output"), OutputName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	if (FunctionInfo.DefinitionName == GetSizeFunctionName)
	{
		static const TCHAR* FormatBounds = TEXT(R"(
			void {FunctionName}(out int Out_Width, out int Out_Height)
			{			
				uint BufferWidth = 0U;
				uint BufferHeight = 0U;
				RW{Output}.GetDimensions(BufferWidth, BufferHeight);

				Out_Width = (int) BufferWidth;
				Out_Height = (int) BufferHeight;
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("Output"), OutputName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}

	return false;
}

bool UNiagaraDataInterfaceRenderTarget2D::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceRenderTarget2D* OtherTyped = CastChecked<UNiagaraDataInterfaceRenderTarget2D>(Destination);

	return true;
}

bool UNiagaraDataInterfaceRenderTarget2D::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	check(Proxy);

	FRenderTarget2DRWInstanceData_GameThread* InstanceData = new (PerInstanceData) FRenderTarget2DRWInstanceData_GameThread();
	if (!InstanceData->TargetTexture)
	{
		InstanceData->TargetTexture = NewObject<UTextureRenderTarget2D>(this);
		InstanceData->TargetTexture->bCanCreateUAV = true;
		InstanceData->TargetTexture->RenderTargetFormat = RTF_RGBA16f;
		InstanceData->TargetTexture->ClearColor = FLinearColor(0.0, 0, 0, 0);
		InstanceData->TargetTexture->bAutoGenerateMips = false;
		FNiagaraSystemInstanceID SysID = SystemInstance->GetId();
		ManagedRenderTargets.Add(SysID) = InstanceData->TargetTexture;
	}

	// Push Updates to Proxy.
	FNiagaraDataInterfaceProxyRenderTarget2DProxy* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyRenderTarget2DProxy>();
	ENQUEUE_RENDER_COMMAND(FUpdateData)(
		[GridColl = this, TexPtr = InstanceData->TargetTexture, RT_Proxy, InstanceID = SystemInstance->GetId(), RT_InstanceData=*InstanceData](FRHICommandListImmediate& RHICmdList)
	{
		check(!RT_Proxy->SystemInstancesToProxyData_RT.Contains(InstanceID));
		FRenderTarget2DRWInstanceData_RenderThread* TargetData = &RT_Proxy->SystemInstancesToProxyData_RT.Add(InstanceID);

		TargetData->DebugTargetTexture = TexPtr;
		TargetData->Size = RT_InstanceData.Size;

		RT_Proxy->SetElementCount(TargetData->Size.X * TargetData->Size.Y);
		TargetData->RenderTargetToCopyTo = nullptr;
	});
	return true;
}


void UNiagaraDataInterfaceRenderTarget2D::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FRenderTarget2DRWInstanceData_GameThread* InstanceData = static_cast<FRenderTarget2DRWInstanceData_GameThread*>(PerInstanceData);

	InstanceData->~FRenderTarget2DRWInstanceData_GameThread();

	FNiagaraDataInterfaceProxyRenderTarget2DProxy* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyRenderTarget2DProxy>();
	ENQUEUE_RENDER_COMMAND(FNiagaraDIDestroyInstanceData) (
		[RT_Proxy, InstanceID=SystemInstance->GetId(), Batcher=SystemInstance->GetBatcher()](FRHICommandListImmediate& CmdList)
		{
			//check(ThisProxy->SystemInstancesToProxyData.Contains(InstanceID));
			RT_Proxy->SystemInstancesToProxyData_RT.Remove(InstanceID);
		}
	);

	// Make sure to clear out the reference to the render target if we created one.
	FNiagaraSystemInstanceID SysId = SystemInstance->GetId();
	ManagedRenderTargets.Remove(SysId);
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



void UNiagaraDataInterfaceRenderTarget2D::SetSize(FVectorVMContext& Context)
{
	// This should only be called from a system or emitter script due to a need for only setting up initially.
	VectorVM::FUserPtrHandler<FRenderTarget2DRWInstanceData_GameThread> InstData(Context);
	VectorVM::FExternalFuncInputHandler<int> InSizeX(Context);
	VectorVM::FExternalFuncInputHandler<int> InSizeY(Context);
	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> OutSuccess(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		int SizeX = InSizeX.GetAndAdvance();
		int SizeY = InSizeY.GetAndAdvance();
		bool bSuccess = (InstData.Get() != nullptr && Context.NumInstances == 1 && SizeX >= 0 && SizeY >= 0);
		*OutSuccess.GetDestAndAdvance() =  bSuccess;
		if (bSuccess)
		{
			InstData->Size.X = SizeX;
			InstData->Size.Y = SizeY;
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
	return false;
}

bool UNiagaraDataInterfaceRenderTarget2D::PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* InSystemInstance, float DeltaSeconds)
{
	FRenderTarget2DRWInstanceData_GameThread* InstanceData = static_cast<FRenderTarget2DRWInstanceData_GameThread*>(PerInstanceData);
	bool bNeedsReset = false;

	{

		FTextureResource* RT_Resource = nullptr;


		bool bUpdateRT = true;
		if (InstanceData->TargetTexture)
		{
			int32 RTSizeX = FMath::Max(InstanceData->Size.X, 1);
			int32 RTSizeY = FMath::Max(InstanceData->Size.Y, 1);

			if (InstanceData->TargetTexture->SizeX != RTSizeX || InstanceData->TargetTexture->SizeY != RTSizeY || InstanceData->TargetTexture->RenderTargetFormat != RTF_RGBA16f)
			{
				// resize RT to match what we need for the output
				InstanceData->TargetTexture->InitAutoFormat(RTSizeX, RTSizeY);
				InstanceData->TargetTexture->UpdateResourceImmediate(true);
				bUpdateRT = true;
				//TargetTexture->InitCustomFormat(InstanceData->Size.X * InstanceData->NumTiles.X, InstanceData->Size.Y * InstanceData->NumTiles.Y, PF_R32_FLOAT, false);

				/*if (InstanceData->TargetTexture->Resource)
				{
					bNeedsReset = true;
				}*/
			}
			RT_Resource = InstanceData->TargetTexture->Resource;
		}

		if (bUpdateRT)
		{
			FNiagaraDataInterfaceProxyRenderTarget2DProxy* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyRenderTarget2DProxy>();
			ENQUEUE_RENDER_COMMAND(FUpdateData)(
				[GridColl = this, TexPtr = InstanceData->TargetTexture, RT_Resource, RT_Proxy, InstanceID = InSystemInstance->GetId(), RT_InstanceData = *InstanceData](FRHICommandListImmediate& RHICmdList)
			{
				FRenderTarget2DRWInstanceData_RenderThread* TargetData = RT_Proxy->SystemInstancesToProxyData_RT.Find(InstanceID);
				if (!TargetData)
					return;

				TargetData->DebugTargetTexture = TexPtr;
				TargetData->Size = RT_InstanceData.Size;
				RT_Proxy->SetElementCount(TargetData->Size.X* TargetData->Size.Y);
				if (RT_Resource && RT_Resource->TextureRHI.IsValid())
				{
					//if (TargetData->RenderTargetToCopyTo != RT_Resource->TextureRHI)
					{
						TargetData->RenderTargetToCopyTo =RT_Resource->TextureRHI; //  TexPtr->TextureReference.TextureReferenceRHI crashes when creating the UAV
						TargetData->UAV = RHICreateUnorderedAccessView(TargetData->RenderTargetToCopyTo, 0);
					}
				}
				else
				{
					TargetData->RenderTargetToCopyTo = nullptr;
					TargetData->UAV = nullptr;
				}

			});
		}
	}
	return bNeedsReset;
}

void FNiagaraDataInterfaceProxyRenderTarget2DProxy::PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context)
{

}

void FNiagaraDataInterfaceProxyRenderTarget2DProxy::PostStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context)
{	
}

void FNiagaraDataInterfaceProxyRenderTarget2DProxy::PostSimulate(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceArgs& Context)
{

	FRenderTarget2DRWInstanceData_RenderThread* ProxyData = SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID);

	if (ProxyData->RenderTargetToCopyTo != nullptr)
	{

		//FRHICopyTextureInfo CopyInfo;
		//RHICmdList.CopyTexture(ProxyData->CurrentData->GridBuffer.Buffer, ProxyData->RenderTargetToCopyTo, CopyInfo);

		//RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, ProxyData->RenderTargetToCopyTo);
		//RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, ProxyData->UAV);
	}
}

void FNiagaraDataInterfaceProxyRenderTarget2DProxy::ResetData(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceArgs& Context)
{	
}
#undef LOCTEXT_NAMESPACE