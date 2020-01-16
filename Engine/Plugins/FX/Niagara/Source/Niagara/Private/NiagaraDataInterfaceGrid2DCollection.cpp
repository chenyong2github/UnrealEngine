// Copyright Epic Games, Inc. All Rights Reserved.
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

static const FString NumTilesName(TEXT("NumTiles_"));

static const FString GridName(TEXT("Grid_"));
static const FString OutputGridName(TEXT("OutputGrid_"));
static const FString SamplerName(TEXT("Sampler_"));


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
		NumTilesParam.Bind(ParameterMap, *(NumTilesName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));

		CellSizeParam.Bind(ParameterMap, *(CellSizeName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		
		WorldBBoxSizeParam.Bind(ParameterMap, *(WorldBBoxSizeName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));

		GridParam.Bind(ParameterMap, *(GridName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		OutputGridParam.Bind(ParameterMap, *(OutputGridName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));

		SamplerParam.Bind(ParameterMap, *(SamplerName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
	}

	virtual void Serialize(FArchive& Ar)override
	{		
		Ar << NumCellsParam;		
		Ar << NumTilesParam;	
		Ar << CellSizeParam;			
		Ar << WorldBBoxSizeParam;

		Ar << GridParam;
		Ar << OutputGridParam;		

		Ar << SamplerParam;
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

		int NumTilesTmp[2];
		NumTilesTmp[0] = ProxyData->NumTilesX;
		NumTilesTmp[1] = ProxyData->NumTilesY;
		SetShaderValue(RHICmdList, ComputeShaderRHI, NumTilesParam, NumTilesTmp);		

		SetShaderValue(RHICmdList, ComputeShaderRHI, CellSizeParam, ProxyData->CellSize);		
				
		SetShaderValue(RHICmdList, ComputeShaderRHI, WorldBBoxSizeParam, ProxyData->WorldBBoxSize);

		FRHISamplerState *SamplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		SetSamplerParameter(RHICmdList, ComputeShaderRHI, SamplerParam, SamplerState);

		if (GridParam.IsBound() && ProxyData->GetCurrentData() != NULL)
		{
			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, ProxyData->GetCurrentData()->GridBuffer.UAV);
			SetSRVParameter(RHICmdList, Context.Shader->GetComputeShader(), GridParam, ProxyData->GetCurrentData()->GridBuffer.SRV);
		}

		if (Context.IsOutputStage && OutputGridParam.IsBound() && ProxyData->GetDestinationData() != NULL)
		{
			const FTextureRWBuffer2D& TextureBuffer = ProxyData->GetDestinationData()->GridBuffer;
			RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, ProxyData->GetDestinationData()->GridBuffer.UAV);
			OutputGridParam.SetTexture(RHICmdList, Context.Shader->GetComputeShader(), TextureBuffer.Buffer, TextureBuffer.UAV);
		}
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
	FShaderParameter NumTilesParam;
	FShaderParameter CellSizeParam;	
	FShaderParameter WorldBBoxSizeParam;

	FShaderResourceParameter GridParam;
	FRWShaderParameter OutputGridParam;

	FShaderResourceParameter SamplerParam;
	
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
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("AttributeIndex")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Value")));

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
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("AttributeIndex")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Value")));
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
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("AttributeIndex")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Value")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

}

// #todo(dmp): expose more CPU functionality
// #todo(dmp): ideally these would be exposed on the parent class, but we can't bind functions of parent classes but need to work on the interface
// for sharing an instance data object with the super class
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceGrid2DCollection, GetWorldBBoxSize);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceGrid2DCollection, GetCellSize);
void UNiagaraDataInterfaceGrid2DCollection::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	Super::GetVMExternalFunction(BindingInfo, InstanceData, OutFunc);

	
	if (BindingInfo.Name == WorldBBoxSizeFunctionName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 2);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceGrid2DCollection, GetWorldBBoxSize)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == CellSizeFunctionName)
	{
		// #todo(dmp): this will override the base class definition for GetCellSize because the data interface instance data computes cell size
		// it would be nice to refactor this so it can be part of the super class
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 2);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceGrid2DCollection, GetCellSize)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetValueFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
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

void UNiagaraDataInterfaceGrid2DCollection::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	Super::GetParameterDefinitionHLSL(ParamInfo, OutHLSL);

	static const TCHAR *FormatDeclarations = TEXT(R"(				
		Texture2D<float> {GridName};
		RWTexture2D<float> RW{OutputGridName};
		int2 {NumTiles};
		SamplerState {SamplerName};
	
	)");
	TMap<FString, FStringFormatArg> ArgsDeclarations = {				
		{ TEXT("GridName"),    GridName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("SamplerName"),    SamplerName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("OutputGridName"),    OutputGridName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("NumTiles"),    NumTilesName + ParamInfo.DataInterfaceHLSLSymbol },
	};
	OutHLSL += FString::Format(FormatDeclarations, ArgsDeclarations);
}

bool UNiagaraDataInterfaceGrid2DCollection::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	bool ParentRet = Super::GetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, OutHLSL);
	if (ParentRet)
	{
		return true;
	} 
	else if (FunctionInfo.DefinitionName == GetValueFunctionName)
	{
		static const TCHAR *FormatBounds = TEXT(R"(
			void {FunctionName}(int In_IndexX, int In_IndexY, int In_AttributeIndex, out float Out_Val)
			{
				int TileIndexX = In_AttributeIndex % {NumTiles}.x;
				int TileIndexY = In_AttributeIndex / {NumTiles}.x;

				Out_Val = {Grid}.Load(int3(In_IndexX + TileIndexX * {NumCellsName}.x, In_IndexY + TileIndexY * {NumCellsName}.y, 0));
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("Grid"), GridName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("NumCellsName"), NumCellsName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("NumTiles"),    NumTilesName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SetValueFunctionName)
	{
		static const TCHAR *FormatBounds = TEXT(R"(
			void {FunctionName}(int In_IndexX, int In_IndexY, int In_AttributeIndex, float In_Value, out int val)
			{			
				int TileIndexX = In_AttributeIndex % {NumTiles}.x;
				int TileIndexY = In_AttributeIndex / {NumTiles}.x;
	
				val = 0;
				RW{OutputGrid}[int2(In_IndexX + TileIndexX * {NumCellsName}.x, In_IndexY + TileIndexY * {NumCellsName}.y)] = In_Value;
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("OutputGrid"), OutputGridName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("NumCellsName"), NumCellsName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("NumTiles"),    NumTilesName + ParamInfo.DataInterfaceHLSLSymbol},

		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleGridFunctionName)
	{
		static const TCHAR *FormatBounds = TEXT(R"(
			void {FunctionName}(float In_UnitX, float In_UnitY, int In_AttributeIndex, out float Out_Val)
			{
				int TileIndexX = In_AttributeIndex % {NumTiles}.x;
				int TileIndexY = In_AttributeIndex / {NumTiles}.x;
				
				Out_Val = {Grid}.SampleLevel({SamplerName}, float2(In_UnitX / {NumTiles}.x + 1.0*TileIndexX/{NumTiles}.x, In_UnitY / {NumTiles}.y + 1.0*TileIndexY/{NumTiles}.y), 0);
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("Grid"), GridName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("SamplerName"),    SamplerName + ParamInfo.DataInterfaceHLSLSymbol },
			{TEXT("NumTiles"),    NumTilesName + ParamInfo.DataInterfaceHLSLSymbol},
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

	// #todo(dmp): refactor
	int MaxDim = 16384;
	int MaxTilesX = floor(MaxDim / NumCellsX);
	int MaxTilesY = floor(MaxDim / NumCellsY);

	// need to determine number of tiles in x and y based on number of attributes and max dimension size
	int NumTilesX = NumAttributes <= MaxTilesX ? NumAttributes : MaxTilesX;
	int NumTilesY = ceil(1.0*NumAttributes / NumTilesX);

	int RT_NumTilesX = NumTilesX;
	int RT_NumTilesY = NumTilesY;
	
	FVector2D RT_WorldBBoxSize = WorldBBoxSize;

	// If we are setting the grid from the voxel size, then recompute NumVoxels and change bbox	
	if (SetGridFromMaxAxis)
	{
		float CellSize = FMath::Max(WorldBBoxSize.X, WorldBBoxSize.Y) / NumCellsMaxAxis;

		RT_NumCellsX = WorldBBoxSize.X / CellSize;
		RT_NumCellsY = WorldBBoxSize.Y / CellSize;

		// Pad grid by 1 voxel if our computed bounding box is too small
		if (WorldBBoxSize.X > WorldBBoxSize.Y && !FMath::IsNearlyEqual(CellSize * RT_NumCellsY, WorldBBoxSize.Y))
		{
			RT_NumCellsY++;
		}
		else if (WorldBBoxSize.X < WorldBBoxSize.Y && !FMath::IsNearlyEqual(CellSize * RT_NumCellsX, WorldBBoxSize.X))
		{
			RT_NumCellsX++;
		}		
		
		RT_WorldBBoxSize = FVector2D(RT_NumCellsX, RT_NumCellsY) * CellSize;
		NumCellsX = RT_NumCellsX;
		NumCellsY = RT_NumCellsY;
	}	

	TSet<int> RT_OutputShaderStages = OutputShaderStages;
	TSet<int> RT_IterationShaderStages = IterationShaderStages;
	
	FVector2D RT_CellSize = RT_WorldBBoxSize / FVector2D(RT_NumCellsX, RT_NumCellsY);
	InstanceData->CellSize = RT_CellSize;
	InstanceData->WorldBBoxSize = RT_WorldBBoxSize;
	
	// Push Updates to Proxy.
	ENQUEUE_RENDER_COMMAND(FUpdateData)(
		[RT_Proxy, RT_NumCellsX, RT_NumCellsY, RT_NumTilesX, RT_NumTilesY, RT_CellSize, RT_WorldBBoxSize, RT_OutputShaderStages, RT_IterationShaderStages, InstanceID = SystemInstance->GetId()](FRHICommandListImmediate& RHICmdList)
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
		
		TargetData->NumTilesX = RT_NumTilesX;
		TargetData->NumTilesY = RT_NumTilesY;

		TargetData->CellSize = RT_CellSize;
		
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
bool UNiagaraDataInterfaceGrid2DCollection::FillTexture2D(const UNiagaraComponent *Component, UTextureRenderTarget2D *Dest, int AttributeIndex)
{
	FNiagaraDataInterfaceProxyGrid2DCollection* TProxy = GetProxyAs<FNiagaraDataInterfaceProxyGrid2DCollection>();

	if (!Component || !Dest)
	{
		return false;
	}

	FNiagaraSystemInstance *SystemInstance = Component->GetSystemInstance();
	if (!SystemInstance)
	{
		return false;
	}

	// check valid attribute index
	if (AttributeIndex < 0 || AttributeIndex >=NumAttributes)
	{
		return false;
	}

	// check dest size and type needs to be float
	// #todo(dmp): don't hardcode float since we might do other stuff in the future
	EPixelFormat RequiredTye = PF_R32_FLOAT;
	if (Dest->SizeX != NumCellsX || Dest->SizeY != NumCellsY || Dest->GetFormat() != RequiredTye)
	{
		return false;
	}

	FNiagaraSystemInstanceID InstanceID = SystemInstance->GetId();

	ENQUEUE_RENDER_COMMAND(FUpdateDIColorCurve)(
		[Dest, TProxy, InstanceID, AttributeIndex](FRHICommandListImmediate& RHICmdList)
	{
		Grid2DCollectionRWInstanceData* Grid2DInstanceData = TProxy->SystemInstancesToProxyData.Find(InstanceID);

		if (Dest && Dest->Resource && Grid2DInstanceData && Grid2DInstanceData->CurrentData)
		{
			FRHICopyTextureInfo CopyInfo;
			CopyInfo.Size = FIntVector(Grid2DInstanceData->NumCellsX, Grid2DInstanceData->NumCellsY, 1);

			int TileIndexX = AttributeIndex % Grid2DInstanceData->NumTilesX;
			int TileIndexY = AttributeIndex / Grid2DInstanceData->NumTilesX;
			int StartX = TileIndexX * Grid2DInstanceData->NumCellsX;
			int StartY = TileIndexY * Grid2DInstanceData->NumCellsY;
			CopyInfo.SourcePosition = FIntVector(StartX, StartY, 0);

			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, Grid2DInstanceData->CurrentData->GridBuffer.Buffer);

			RHICmdList.CopyTexture(Grid2DInstanceData->CurrentData->GridBuffer.Buffer, Dest->Resource->TextureRHI, CopyInfo);
		}
	});

	return true;
}

UFUNCTION(BlueprintCallable, Category = Niagara)
bool UNiagaraDataInterfaceGrid2DCollection::FillRawTexture2D(const UNiagaraComponent *Component, UTextureRenderTarget2D *Dest, int &TilesX, int &TilesY)
{
	FNiagaraDataInterfaceProxyGrid2DCollection* TProxy = GetProxyAs<FNiagaraDataInterfaceProxyGrid2DCollection>();

	if (!Component)
	{
		TilesX = -1;
		TilesY = -1;
		return false;
	}

	FNiagaraSystemInstance *SystemInstance = Component->GetSystemInstance();
	if (!SystemInstance)
	{
		TilesX = -1;
		TilesY = -1;
		return false;
	}
	FNiagaraSystemInstanceID InstanceID = SystemInstance->GetId();
	
	Grid2DCollectionRWInstanceData* Grid2DInstanceData = TProxy->SystemInstancesToProxyData.Find(InstanceID);
	if (!Grid2DInstanceData)
	{
		TilesX = -1;
		TilesY = -1;
		return false;
	}

	TilesX = Grid2DInstanceData->NumTilesX;
	TilesY = Grid2DInstanceData->NumTilesY;	

	// check dest size and type needs to be float
	// #todo(dmp): don't hardcode float since we might do other stuff in the future
	EPixelFormat RequiredTye = PF_R32_FLOAT;
	if (!Dest || Dest->SizeX != NumCellsX * TilesX || Dest->SizeY != NumCellsY * TilesY || Dest->GetFormat() != RequiredTye)
	{
		return false;
	}

	// #todo(dmp): pass grid2dinstancedata through?
	ENQUEUE_RENDER_COMMAND(FUpdateDIColorCurve)(
		[Dest, TProxy, InstanceID](FRHICommandListImmediate& RHICmdList)
	{
		Grid2DCollectionRWInstanceData* Grid2DInstanceDataTmp = TProxy->SystemInstancesToProxyData.Find(InstanceID);

		if (Dest && Dest->Resource && Grid2DInstanceDataTmp && Grid2DInstanceDataTmp->CurrentData)
		{
			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, Grid2DInstanceDataTmp->CurrentData->GridBuffer.Buffer);

			FRHICopyTextureInfo CopyInfo;
			RHICmdList.CopyTexture(Grid2DInstanceDataTmp->CurrentData->GridBuffer.Buffer, Dest->Resource->TextureRHI, CopyInfo);
		}
	});

	return true;
}

UFUNCTION(BlueprintCallable, Category = Niagara)
void UNiagaraDataInterfaceGrid2DCollection::GetRawTextureSize(const UNiagaraComponent *Component, int &SizeX, int &SizeY)
{
	FNiagaraDataInterfaceProxyGrid2DCollection* TProxy = GetProxyAs<FNiagaraDataInterfaceProxyGrid2DCollection>();

	if (!Component)
	{
		SizeX = -1;
		SizeY = -1;
		return;
	}

	FNiagaraSystemInstance *SystemInstance = Component->GetSystemInstance();
	if (!SystemInstance)
	{
		SizeX = -1;
		SizeY = -1;
		return;
	}
	FNiagaraSystemInstanceID InstanceID = SystemInstance->GetId();

	Grid2DCollectionRWInstanceData* Grid2DInstanceData = TProxy->SystemInstancesToProxyData.Find(InstanceID);
	if (!Grid2DInstanceData)
	{
		SizeX = -1;
		SizeY = -1;
		return;
	}
	
	if (Grid2DInstanceData->Buffers.Num() <= 0)
	{
		SizeX = -1;
		SizeY = -1;
		return;
	}

	SizeX = Grid2DInstanceData->Buffers[0]->GridBuffer.Buffer->GetSizeX();
	SizeY = Grid2DInstanceData->Buffers[0]->GridBuffer.Buffer->GetSizeY();
}

UFUNCTION(BlueprintCallable, Category = Niagara)
void UNiagaraDataInterfaceGrid2DCollection::GetTextureSize(const UNiagaraComponent *Component, int &SizeX, int &SizeY)
{
	FNiagaraDataInterfaceProxyGrid2DCollection* TProxy = GetProxyAs<FNiagaraDataInterfaceProxyGrid2DCollection>();

	if (!Component)
	{
		SizeX = -1;
		SizeY = -1;
		return;
	}

	FNiagaraSystemInstance *SystemInstance = Component->GetSystemInstance();
	if (!SystemInstance)
	{
		SizeX = -1;
		SizeY = -1;
		return;
	}
	FNiagaraSystemInstanceID InstanceID = SystemInstance->GetId();

	Grid2DCollectionRWInstanceData* Grid2DInstanceData = TProxy->SystemInstancesToProxyData.Find(InstanceID);
	if (!Grid2DInstanceData)
	{
		SizeX = -1;
		SizeY = -1;
		return;
	}

	SizeX = Grid2DInstanceData->NumCellsX;
	SizeY = Grid2DInstanceData->NumCellsY;
}


void UNiagaraDataInterfaceGrid2DCollection::GetWorldBBoxSize(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<Grid2DCollectionRWInstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutWorldBoundsX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutWorldBoundsY(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutWorldBoundsX.GetDestAndAdvance() = InstData->WorldBBoxSize.X;
		*OutWorldBoundsY.GetDestAndAdvance() = InstData->WorldBBoxSize.Y;
	}
}


void UNiagaraDataInterfaceGrid2DCollection::GetCellSize(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<Grid2DCollectionRWInstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutCellSizeX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutCellSizeY(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{	
		*OutCellSizeX.GetDestAndAdvance() = InstData->CellSize.X;
		*OutCellSizeY.GetDestAndAdvance() = InstData->CellSize.Y;
	}
}

// #todo(dmp): move these to super class
void FNiagaraDataInterfaceProxyGrid2DCollection::DestroyPerInstanceData(NiagaraEmitterInstanceBatcher* Batcher, const FNiagaraSystemInstanceID& SystemInstance)
{
	check(IsInRenderingThread());

	DeferredDestroyList.Add(SystemInstance);
	Batcher->EnqueueDeferredDeletesForDI_RenderThread(this->AsShared());
}

// #todo(dmp): move these to super class
void FNiagaraDataInterfaceProxyGrid2DCollection::DeferredDestroy()
{
	for (const FNiagaraSystemInstanceID& Sys : DeferredDestroyList)
	{
		SystemInstancesToProxyData.Remove(Sys);
	}

	DeferredDestroyList.Empty();
}


void FNiagaraDataInterfaceProxyGrid2DCollection::PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context)
{
	// #todo(dmp): Context doesnt need to specify if a stage is output or not since we moved pre/post stage to the DI itself.  Not sure which design is better for the future
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

	if (!ProxyData)
	{
		return;
	}

	for (Grid2DBuffer* Buffer : ProxyData->Buffers)
	{
		if (Buffer)
		{
			ClearUAV(RHICmdList, Buffer->GridBuffer, FLinearColor(0, 0, 0, 0));
		}		
	}	
}
#undef LOCTEXT_NAMESPACE