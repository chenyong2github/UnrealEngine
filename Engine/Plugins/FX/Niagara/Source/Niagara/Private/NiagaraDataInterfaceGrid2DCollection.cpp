// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "NiagaraDataInterfaceGrid2DCollection.h"
#include "NiagaraShader.h"
#include "ShaderParameterUtils.h"
#include "ClearQuad.h"
#include "TextureResource.h"
#include "Engine/Texture2D.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraSystemInstance.h"
#include "Engine/TextureRenderTarget2D.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceGrid2DCollection"

static const FString GridName(TEXT("Grid_"));
static const FString OutputGridName(TEXT("OutputGrid_"));


// Global VM function names, also used by the shaders code generation methods.
static const FName SetValueFunctionName("SetGridValue");
static const FName GetValueFunctionName("GetGridValue");

static const FName SampleGridFunctionName("SampleGrid");


/*--------------------------------------------------------------------------------------------------------------------------*/
struct FNiagaraDataInterfaceParametersCS_Grid2DCollection : public FNiagaraDataInterfaceParametersCS
{
	virtual void Bind(const FNiagaraDataInterfaceParamRef& ParamRef, const class FShaderParameterMap& ParameterMap) override
	{			
		NumCellsParam.Bind(ParameterMap, *(NumCellsName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));

		CellSizeParam.Bind(ParameterMap, *(CellSizeName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));

		WorldBBoxMinParam.Bind(ParameterMap, *(WorldBBoxMinName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		WorldBBoxSizeParam.Bind(ParameterMap, *(WorldBBoxSizeName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));

		GridParam.Bind(ParameterMap, *(GridName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		OutputGridParam.Bind(ParameterMap, *(OutputGridName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
	}

	virtual void Serialize(FArchive& Ar)override
	{		
		Ar << NumCellsParam;		
		Ar << CellSizeParam;	
		Ar << WorldBBoxMinParam;
		Ar << WorldBBoxSizeParam;

		Ar << GridParam;
		Ar << OutputGridParam;		
	}

	virtual void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const override
	{
		check(IsInRenderingThread());

		// Get shader and DI
		FRHIComputeShader* ComputeShaderRHI = Context.Shader->GetComputeShader();
		FNiagaraDataInterfaceProxyGrid2DCollection* VFDI = static_cast<FNiagaraDataInterfaceProxyGrid2DCollection*>(Context.DataInterface);
		
		Grid2DCollectionRWInstanceData* ProxyData = VFDI->SystemInstancesToProxyData.Find(Context.SystemInstance);
		check(ProxyData);

		int NumCellsTmp[2];
		NumCellsTmp[0] = ProxyData->NumCellsX;
		NumCellsTmp[1] = ProxyData->NumCellsY;
		SetShaderValue(RHICmdList, ComputeShaderRHI, NumCellsParam, NumCellsTmp);		
		
		// #todo(dmp): precompute these values
		float CellSizeTmp[2];
		CellSizeTmp[0] = ProxyData->WorldBBoxSize.X / ProxyData->NumCellsX;
		CellSizeTmp[1] = ProxyData->WorldBBoxSize.Y / ProxyData->NumCellsY;
		SetShaderValue(RHICmdList, ComputeShaderRHI, CellSizeParam, CellSizeTmp);		
		
		SetShaderValue(RHICmdList, ComputeShaderRHI, WorldBBoxMinParam, ProxyData->WorldBBoxMin);
		SetShaderValue(RHICmdList, ComputeShaderRHI, WorldBBoxSizeParam, ProxyData->WorldBBoxSize);



		if (GridParam.IsBound() && ProxyData->GetCurrentData() != NULL)
		{
			RHICmdList.SetShaderResourceViewParameter(Context.Shader->GetComputeShader(), GridParam.GetBaseIndex(), ProxyData->GetCurrentData()->GridBuffer.SRV);
		}

		if (Context.IsOutputStage && OutputGridParam.IsBound() && ProxyData->GetDestinationData() != NULL)
		{
			RHICmdList.SetUAVParameter(Context.Shader->GetComputeShader(), OutputGridParam.GetUAVIndex(), ProxyData->GetDestinationData()->GridBuffer.UAV);		
		}
		// Note: There is a flush in PreEditChange to make sure everything is synced up at this point 
	}

	virtual void Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const 
	{
		if (OutputGridParam.IsBound())
		{
			OutputGridParam.UnsetUAV(RHICmdList, Context.Shader->GetComputeShader());
		}
	}

private:

	FShaderParameter NumCellsParam;
	FShaderParameter CellSizeParam;
	FShaderParameter WorldBBoxMinParam;
	FShaderParameter WorldBBoxSizeParam;

	FShaderResourceParameter GridParam;
	FRWShaderParameter OutputGridParam;
	
};


UNiagaraDataInterfaceGrid2DCollection::UNiagaraDataInterfaceGrid2DCollection(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Proxy = MakeShared<FNiagaraDataInterfaceProxyGrid2DCollection, ESPMode::ThreadSafe>();
	RWProxy = (FNiagaraDataInterfaceProxyRW*) Proxy.Get();
	PushToRenderThread();
}


void UNiagaraDataInterfaceGrid2DCollection::PostInitProperties()
{
	Super::PostInitProperties();

	//Can we register data interfaces as regular types and fold them into the FNiagaraVariable framework for UI and function calls etc?
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), /*bCanBeParameter*/ true, /*bCanBePayload*/ false, /*bIsUserDefined*/ false);
	}
}

void UNiagaraDataInterfaceGrid2DCollection::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	Super::GetFunctions(OutFunctions);

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetValueFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("Value")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SetValueFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("Value")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IGNORE")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleGridFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("UnitX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("UnitY")));		
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("Value")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

}


void UNiagaraDataInterfaceGrid2DCollection::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	Super::GetVMExternalFunction(BindingInfo, InstanceData, OutFunc);

	if (BindingInfo.Name == GetValueFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == SetValueFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == SampleGridFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
}

bool UNiagaraDataInterfaceGrid2DCollection::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceGrid2DCollection* OtherTyped = CastChecked<const UNiagaraDataInterfaceGrid2DCollection>(Other);

	return OtherTyped != nullptr;		
}

void UNiagaraDataInterfaceGrid2DCollection::GetParameterDefinitionHLSL(FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	Super::GetParameterDefinitionHLSL(ParamInfo, OutHLSL);

	static const TCHAR *FormatDeclarations = TEXT(R"(				
		Texture2D<float4> {GridName};
		RWTexture2D<float4> RW{OutputGridName};

		SamplerState {GridName}Sampler
		{
			Filter = MIN_MAG_MIP_LINEAR;
			AddressU = Clamp;
			AddressV = Clamp;
		};
	)");
	TMap<FString, FStringFormatArg> ArgsDeclarations = {				
		{ TEXT("GridName"),    GridName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("OutputGridName"),    OutputGridName + ParamInfo.DataInterfaceHLSLSymbol },
	};
	OutHLSL += FString::Format(FormatDeclarations, ArgsDeclarations);
}

bool UNiagaraDataInterfaceGrid2DCollection::GetFunctionHLSL(const FName& DefinitionFunctionName, FString InstanceFunctionName, FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	bool ParentRet = Super::GetFunctionHLSL(DefinitionFunctionName, InstanceFunctionName, ParamInfo, OutHLSL);
	if (ParentRet)
	{
		return true;
	} 
	else if (DefinitionFunctionName == GetValueFunctionName)
	{
		static const TCHAR *FormatBounds = TEXT(R"(
			void {FunctionName}(int In_IndexX, int In_IndexY, out float4 Out_Val)
			{
				Out_Val = {Grid}.Load(int3(In_IndexX, In_IndexY, 0));
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), InstanceFunctionName},
			{TEXT("Grid"), GridName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	else if (DefinitionFunctionName == SetValueFunctionName)
	{
		static const TCHAR *FormatBounds = TEXT(R"(
			void {FunctionName}(int In_IndexX, int In_IndexY, float4 In_Value, out int val)
			{				
				val = 0;
				RW{OutputGrid}[int2(In_IndexX, In_IndexY)] = In_Value;
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), InstanceFunctionName},
			{TEXT("OutputGrid"), OutputGridName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	else if (DefinitionFunctionName == SampleGridFunctionName)
	{
		static const TCHAR *FormatBounds = TEXT(R"(
			void {FunctionName}(float In_UnitX, float In_UnitY, out float4 Out_Val)
			{
				Out_Val = {Grid}.SampleLevel({Grid}Sampler, float2(In_UnitX, In_UnitY), 0);
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), InstanceFunctionName},
			{TEXT("Grid"), GridName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	return false;
}

FNiagaraDataInterfaceParametersCS* UNiagaraDataInterfaceGrid2DCollection::ConstructComputeParameters()const
{
	return new FNiagaraDataInterfaceParametersCS_Grid2DCollection();
}



bool UNiagaraDataInterfaceGrid2DCollection::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceGrid2DCollection* OtherTyped = CastChecked<UNiagaraDataInterfaceGrid2DCollection>(Destination);


	return true;
}

bool UNiagaraDataInterfaceGrid2DCollection::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	check(Proxy);

	Grid2DCollectionRWInstanceData* InstanceData = new (PerInstanceData) Grid2DCollectionRWInstanceData();

	FNiagaraDataInterfaceProxyGrid2DCollection* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyGrid2DCollection>();

	int RT_NumCellsX = NumCellsX;
	int RT_NumCellsY = NumCellsY;

	FVector RT_WorldBBoxMin = WorldBBoxMin;
	FVector2D RT_WorldBBoxSize = WorldBBoxSize;

	// If we are setting the grid from the voxel size, then recompute NumVoxels and change bbox	
	if (SetGridFromCellSize)
	{
		RT_NumCellsX = WorldBBoxSize.X / CellSize;
		RT_NumCellsY = WorldBBoxSize.Y / CellSize;

		// Pad grid by 1 voxel if our computed bounding box is too small
		if (!FMath::IsNearlyEqual(CellSize * RT_NumCellsX, WorldBBoxSize.X))
		{
			RT_NumCellsX = NumCellsX + 1;
			RT_NumCellsY = NumCellsY + 1;
			RT_WorldBBoxSize = FVector2D(RT_NumCellsX, RT_NumCellsY) * CellSize;
		}
	}	

	TSet<int> RT_OutputShaderStages = OutputShaderStages;
	TSet<int> RT_IterationShaderStages = IterationShaderStages;

	// @todo-threadsafety. This would be a race but I'm taking a ref here. Not ideal in the long term.
	// Push Updates to Proxy.
	ENQUEUE_RENDER_COMMAND(FUpdateData)(
		[RT_Proxy, RT_NumCellsX, RT_NumCellsY, RT_WorldBBoxMin, RT_WorldBBoxSize, RT_OutputShaderStages, RT_IterationShaderStages, InstanceID = SystemInstance->GetId()](FRHICommandListImmediate& RHICmdList)
	{
		Grid2DCollectionRWInstanceData* TargetData = RT_Proxy->SystemInstancesToProxyData.Find(InstanceID);
		if (TargetData != nullptr)
		{
			RT_Proxy->DeferredDestroyList.Remove(InstanceID);
		}
		else
		{
			TargetData = &RT_Proxy->SystemInstancesToProxyData.Add(InstanceID);
		}

		TargetData->NumCellsX = RT_NumCellsX;
		TargetData->NumCellsY = RT_NumCellsY;

		TargetData->WorldBBoxMin = RT_WorldBBoxMin;
		TargetData->WorldBBoxSize = RT_WorldBBoxSize;

		RT_Proxy->OutputShaderStages = RT_OutputShaderStages;
		RT_Proxy->IterationShaderStages = RT_IterationShaderStages;

		RT_Proxy->SetElementCount(TargetData->NumCellsX * TargetData->NumCellsY);
	});

	return true;
}


void UNiagaraDataInterfaceGrid2DCollection::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	Grid2DCollectionRWInstanceData* InstanceData = static_cast<Grid2DCollectionRWInstanceData*>(PerInstanceData);

	InstanceData->~Grid2DCollectionRWInstanceData();

	FNiagaraDataInterfaceProxyGrid2DCollection* ThisProxy = GetProxyAs<FNiagaraDataInterfaceProxyGrid2DCollection>();

	ENQUEUE_RENDER_COMMAND(FNiagaraDIDestroyInstanceData) (
		[ThisProxy, InstanceID = SystemInstance->GetId(), Batcher = SystemInstance->GetBatcher()](FRHICommandListImmediate& CmdList)
	{
		ThisProxy->DestroyPerInstanceData(Batcher, InstanceID);
	}
	);
}

UFUNCTION(BlueprintCallable, Category = Niagara)
void UNiagaraDataInterfaceGrid2DCollection::FillTexture2D(const UNiagaraComponent *Component, UTextureRenderTarget2D *Dest)
{
	FNiagaraDataInterfaceProxyGrid2DCollection* TProxy = GetProxyAs<FNiagaraDataInterfaceProxyGrid2DCollection>();

	if (!Component)
	{
		return;
	}

	FNiagaraSystemInstance *SystemInstance = Component->GetSystemInstance();
	if (!SystemInstance)
	{
		return;
	}
	FGuid InstanceID = SystemInstance->GetId();

	ENQUEUE_RENDER_COMMAND(FUpdateDIColorCurve)(
		[Dest, TProxy, InstanceID](FRHICommandListImmediate& RHICmdList)
	{
		Grid2DCollectionRWInstanceData* Grid2DInstanceData = TProxy->SystemInstancesToProxyData.Find(InstanceID);

		if (Dest && Dest->Resource && Grid2DInstanceData && Grid2DInstanceData->CurrentData)
		{
			FTextureRenderTarget2DResource* Texture2DResource = (FTextureRenderTarget2DResource*)Dest->Resource;
			RHICopySharedMips(Texture2DResource->GetTextureRHI(), Grid2DInstanceData->CurrentData->GridBuffer.Buffer);
		}
	});
}

// #todo(dmp): move these to super class
void FNiagaraDataInterfaceProxyGrid2DCollection::DestroyPerInstanceData(NiagaraEmitterInstanceBatcher* Batcher, const FGuid& SystemInstance)
{
	check(IsInRenderingThread());

	DeferredDestroyList.Add(SystemInstance);
	Batcher->EnqueueDeferredDeletesForDI_RenderThread(this->AsShared());
}

// #todo(dmp): move these to super class
void FNiagaraDataInterfaceProxyGrid2DCollection::DeferredDestroy()
{
	for (const FGuid& Sys : DeferredDestroyList)
	{
		SystemInstancesToProxyData.Remove(Sys);
	}

	DeferredDestroyList.Empty();
}


void FNiagaraDataInterfaceProxyGrid2DCollection::PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context)
{
	// #todo(dmp): Context doesnt need to specify if a stage is output or not since we moved pre/post steage to the DI itself.  Not sure which design is better for the future
	if (Context.IsOutputStage)
	{
		Grid2DCollectionRWInstanceData* ProxyData = SystemInstancesToProxyData.Find(Context.SystemInstance);

		ProxyData->BeginSimulate();

		// If we don't have an iteration stage, then we should manually clear the buffer to make sure there is no residual data.  If we are doing something like rasterizing particles into a grid, we want it to be clear before
		// we start.  If a user wants to access data from the previous stage, then they can read from the current data.

		// #todo(dmp): we might want to expose an option where we have buffers that are write only and need a clear (ie: no buffering like the neighbor grid).  They would be considered transient perhaps?  It'd be more
		// memory efficient since it would theoretically not require any double buffering.
		if (!Context.IsIterationStage)
		{
			ClearUAV(RHICmdList, ProxyData->DestinationData->GridBuffer, FLinearColor(0, 0, 0, 0));
		}
	}
}

void FNiagaraDataInterfaceProxyGrid2DCollection::PostStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context)
{
	
	if (Context.IsOutputStage)
	{
		Grid2DCollectionRWInstanceData* ProxyData = SystemInstancesToProxyData.Find(Context.SystemInstance);
		ProxyData->EndSimulate();
	}
}

void FNiagaraDataInterfaceProxyGrid2DCollection::ResetData(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context)
{	
	Grid2DCollectionRWInstanceData* ProxyData = SystemInstancesToProxyData.Find(Context.SystemInstance);

	for (Grid2DBuffer* Buffer : ProxyData->Buffers)
	{
		ClearUAV(RHICmdList, Buffer->GridBuffer, FLinearColor(0, 0, 0, 0));
	}	
}
#undef LOCTEXT_NAMESPACE