// Copyright Epic Games, Inc. All Rights Reserved.
#include "NiagaraDataInterfaceGrid2DCollection.h"
#include "NiagaraShader.h"
#include "ShaderParameterUtils.h"
#include "ClearQuad.h"
#include "TextureResource.h"
#include "Engine/Texture2D.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraRenderer.h"
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
	DECLARE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_Grid2DCollection, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{			
		NumCellsParam.Bind(ParameterMap, *(NumCellsName + ParameterInfo.DataInterfaceHLSLSymbol));
		NumTilesParam.Bind(ParameterMap, *(NumTilesName + ParameterInfo.DataInterfaceHLSLSymbol));

		CellSizeParam.Bind(ParameterMap, *(CellSizeName + ParameterInfo.DataInterfaceHLSLSymbol));

		WorldBBoxSizeParam.Bind(ParameterMap, *(WorldBBoxSizeName + ParameterInfo.DataInterfaceHLSLSymbol));

		GridParam.Bind(ParameterMap, *(GridName + ParameterInfo.DataInterfaceHLSLSymbol));
		OutputGridParam.Bind(ParameterMap, *(OutputGridName + ParameterInfo.DataInterfaceHLSLSymbol));

		SamplerParam.Bind(ParameterMap, *(SamplerName + ParameterInfo.DataInterfaceHLSLSymbol));
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		// Get shader and DI
		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
		FNiagaraDataInterfaceProxyGrid2DCollectionProxy* VFDI = static_cast<FNiagaraDataInterfaceProxyGrid2DCollectionProxy*>(Context.DataInterface);
		
		FGrid2DCollectionRWInstanceData_RenderThread* ProxyData = VFDI->SystemInstancesToProxyData_RT.Find(Context.SystemInstance);
		check(ProxyData);

		int NumCellsTmp[2];
		NumCellsTmp[0] = ProxyData->NumCells.X;
		NumCellsTmp[1] = ProxyData->NumCells.Y;
		SetShaderValue(RHICmdList, ComputeShaderRHI, NumCellsParam, NumCellsTmp);	

		int NumTilesTmp[2];
		NumTilesTmp[0] = ProxyData->NumTiles.X;
		NumTilesTmp[1] = ProxyData->NumTiles.Y;
		SetShaderValue(RHICmdList, ComputeShaderRHI, NumTilesParam, NumTilesTmp);		

		SetShaderValue(RHICmdList, ComputeShaderRHI, CellSizeParam, ProxyData->CellSize);		
				
		SetShaderValue(RHICmdList, ComputeShaderRHI, WorldBBoxSizeParam, ProxyData->WorldBBoxSize);

		FRHISamplerState *SamplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		SetSamplerParameter(RHICmdList, ComputeShaderRHI, SamplerParam, SamplerState);

		if (GridParam.IsBound())
		{
			FRHIShaderResourceView* InputGridBuffer;
			if (ProxyData->CurrentData != nullptr)
			{
				RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, ProxyData->CurrentData->GridBuffer.UAV);
				InputGridBuffer = ProxyData->CurrentData->GridBuffer.SRV;
			}
			else
			{
				InputGridBuffer = FNiagaraRenderer::GetDummyTextureReadBuffer2D();
			}
			SetSRVParameter(RHICmdList, Context.Shader.GetComputeShader(), GridParam, InputGridBuffer);
		}

		if ( OutputGridParam.IsUAVBound() )
		{
			FRHIUnorderedAccessView* OutputGridUAV;
			if (Context.IsOutputStage && ProxyData->DestinationData != nullptr)
			{
				OutputGridUAV = ProxyData->DestinationData->GridBuffer.UAV;
				RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, OutputGridUAV);
			}
			else
			{
				OutputGridUAV = Context.Batcher->GetEmptyRWTextureFromPool(RHICmdList, PF_R32_FLOAT);
			}
			RHICmdList.SetUAVParameter(ComputeShaderRHI, OutputGridParam.GetUAVIndex(), OutputGridUAV);
		}
	}

	void Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const 
	{
		if (OutputGridParam.IsBound())
		{
			OutputGridParam.UnsetUAV(RHICmdList, Context.Shader.GetComputeShader());
		}
	}

private:

	LAYOUT_FIELD(FShaderParameter, NumCellsParam);
	LAYOUT_FIELD(FShaderParameter, NumTilesParam);
	LAYOUT_FIELD(FShaderParameter, CellSizeParam);
	LAYOUT_FIELD(FShaderParameter, WorldBBoxSizeParam);

	LAYOUT_FIELD(FShaderResourceParameter, GridParam);
	LAYOUT_FIELD(FRWShaderParameter, OutputGridParam);
	
	LAYOUT_FIELD(FShaderResourceParameter, SamplerParam);
};

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_Grid2DCollection);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceGrid2DCollection, FNiagaraDataInterfaceParametersCS_Grid2DCollection);


UNiagaraDataInterfaceGrid2DCollection::UNiagaraDataInterfaceGrid2DCollection(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyGrid2DCollectionProxy());
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

		Sig.bExperimental = true;
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

		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bWriteFunction = true;
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

		Sig.bExperimental = true;
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

	FGrid2DCollectionRWInstanceData_GameThread* InstanceData = new (PerInstanceData) FGrid2DCollectionRWInstanceData_GameThread();
	SystemInstancesToProxyData_GT.Emplace(SystemInstance->GetId(), InstanceData);

	InstanceData->NumCells.X = NumCellsX;
	InstanceData->NumCells.Y = NumCellsY;

	// #todo(dmp): refactor
	int MaxDim = 16384;
	int MaxTilesX = floor(MaxDim / NumCellsX);
	int MaxTilesY = floor(MaxDim / NumCellsY);

	// need to determine number of tiles in x and y based on number of attributes and max dimension size
	int NumTilesX = NumAttributes <= MaxTilesX ? NumAttributes : MaxTilesX;
	int NumTilesY = ceil(1.0*NumAttributes / NumTilesX);

	InstanceData->NumTiles.X = NumTilesX;
	InstanceData->NumTiles.Y = NumTilesY;
	
	InstanceData->WorldBBoxSize = WorldBBoxSize;

	// If we are setting the grid from the voxel size, then recompute NumVoxels and change bbox	
	if (SetGridFromMaxAxis)
	{
		float CellSize = FMath::Max(WorldBBoxSize.X, WorldBBoxSize.Y) / NumCellsMaxAxis;

		InstanceData->NumCells.X = WorldBBoxSize.X / CellSize;
		InstanceData->NumCells.Y = WorldBBoxSize.Y / CellSize;

		// Pad grid by 1 voxel if our computed bounding box is too small
		if (WorldBBoxSize.X > WorldBBoxSize.Y && !FMath::IsNearlyEqual(CellSize * InstanceData->NumCells.Y, WorldBBoxSize.Y))
		{
			InstanceData->NumCells.Y++;
		}
		else if (WorldBBoxSize.X < WorldBBoxSize.Y && !FMath::IsNearlyEqual(CellSize * InstanceData->NumCells.X, WorldBBoxSize.X))
		{
			InstanceData->NumCells.X++;
		}		
		
		InstanceData->WorldBBoxSize = FVector2D(InstanceData->NumCells.X, InstanceData->NumCells.Y) * CellSize;
		NumCellsX = InstanceData->NumCells.X;
		NumCellsY = InstanceData->NumCells.Y;
	}	

	InstanceData->CellSize = InstanceData->WorldBBoxSize / FVector2D(InstanceData->NumCells.X, InstanceData->NumCells.Y);

	// Push Updates to Proxy.
	FNiagaraDataInterfaceProxyGrid2DCollectionProxy* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyGrid2DCollectionProxy>();
	ENQUEUE_RENDER_COMMAND(FUpdateData)(
		[RT_Proxy, InstanceID = SystemInstance->GetId(), RT_InstanceData=*InstanceData, RT_OutputShaderStages=OutputShaderStages, RT_IterationShaderStages= IterationShaderStages](FRHICommandListImmediate& RHICmdList)
	{
		check(!RT_Proxy->SystemInstancesToProxyData_RT.Contains(InstanceID));
		FGrid2DCollectionRWInstanceData_RenderThread* TargetData = &RT_Proxy->SystemInstancesToProxyData_RT.Add(InstanceID);

		TargetData->NumCells = RT_InstanceData.NumCells;
		TargetData->NumTiles = RT_InstanceData.NumTiles;
		TargetData->CellSize = RT_InstanceData.CellSize;
		TargetData->WorldBBoxSize = RT_InstanceData.WorldBBoxSize;

		RT_Proxy->OutputSimulationStages_DEPRECATED = RT_OutputShaderStages;
		RT_Proxy->IterationSimulationStages_DEPRECATED = RT_IterationShaderStages;

		RT_Proxy->SetElementCount(TargetData->NumCells.X * TargetData->NumCells.Y);
	});

	return true;
}


void UNiagaraDataInterfaceGrid2DCollection::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	SystemInstancesToProxyData_GT.Remove(SystemInstance->GetId());

	FGrid2DCollectionRWInstanceData_GameThread* InstanceData = static_cast<FGrid2DCollectionRWInstanceData_GameThread*>(PerInstanceData);

	InstanceData->~FGrid2DCollectionRWInstanceData_GameThread();

	FNiagaraDataInterfaceProxyGrid2DCollectionProxy* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyGrid2DCollectionProxy>();
	ENQUEUE_RENDER_COMMAND(FNiagaraDIDestroyInstanceData) (
		[RT_Proxy, InstanceID=SystemInstance->GetId(), Batcher=SystemInstance->GetBatcher()](FRHICommandListImmediate& CmdList)
		{
			//check(ThisProxy->SystemInstancesToProxyData.Contains(InstanceID));
			RT_Proxy->SystemInstancesToProxyData_RT.Remove(InstanceID);
		}
	);
}

UFUNCTION(BlueprintCallable, Category = Niagara)
bool UNiagaraDataInterfaceGrid2DCollection::FillTexture2D(const UNiagaraComponent *Component, UTextureRenderTarget2D *Dest, int AttributeIndex)
{
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

	FNiagaraDataInterfaceProxyGrid2DCollectionProxy* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyGrid2DCollectionProxy>();
	ENQUEUE_RENDER_COMMAND(FUpdateDIColorCurve)(
		[RT_Proxy, InstanceID=SystemInstance->GetId(), RT_TextureResource=Dest->Resource, AttributeIndex](FRHICommandListImmediate& RHICmdList)
	{
		FGrid2DCollectionRWInstanceData_RenderThread* Grid2DInstanceData = RT_Proxy->SystemInstancesToProxyData_RT.Find(InstanceID);

		if (RT_TextureResource && RT_TextureResource->TextureRHI.IsValid() && Grid2DInstanceData && Grid2DInstanceData->CurrentData)
		{
			FRHICopyTextureInfo CopyInfo;
			CopyInfo.Size = FIntVector(Grid2DInstanceData->NumCells.X, Grid2DInstanceData->NumCells.Y, 1);

			int TileIndexX = AttributeIndex % Grid2DInstanceData->NumTiles.X;
			int TileIndexY = AttributeIndex / Grid2DInstanceData->NumTiles.X;
			int StartX = TileIndexX * Grid2DInstanceData->NumCells.X;
			int StartY = TileIndexY * Grid2DInstanceData->NumCells.Y;
			CopyInfo.SourcePosition = FIntVector(StartX, StartY, 0);

			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, Grid2DInstanceData->CurrentData->GridBuffer.Buffer);
			RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, RT_TextureResource->TextureRHI);

			RHICmdList.CopyTexture(Grid2DInstanceData->CurrentData->GridBuffer.Buffer, RT_TextureResource->TextureRHI, CopyInfo);

			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, RT_TextureResource->TextureRHI);
		}
	});

	return true;
}

UFUNCTION(BlueprintCallable, Category = Niagara)
bool UNiagaraDataInterfaceGrid2DCollection::FillRawTexture2D(const UNiagaraComponent *Component, UTextureRenderTarget2D *Dest, int &TilesX, int &TilesY)
{
	if (!Component)
	{
		TilesX = -1;
		TilesY = -1;
		return false;
	}

	FNiagaraSystemInstance* SystemInstance = Component->GetSystemInstance();
	if (!SystemInstance)
	{
		TilesX = -1;
		TilesY = -1;
		return false;
	}

	FGrid2DCollectionRWInstanceData_GameThread* Grid2DInstanceData = SystemInstancesToProxyData_GT.FindRef(SystemInstance->GetId());
	if (!Grid2DInstanceData)
	{
		TilesX = -1;
		TilesY = -1;
		return false;
	}
	
	TilesX = Grid2DInstanceData->NumTiles.X;
	TilesY = Grid2DInstanceData->NumTiles.Y;

	// check dest size and type needs to be float
	// #todo(dmp): don't hardcode float since we might do other stuff in the future
	EPixelFormat RequiredTye = PF_R32_FLOAT;
	if (!Dest || Dest->SizeX != NumCellsX * TilesX || Dest->SizeY != NumCellsY * TilesY || Dest->GetFormat() != RequiredTye)
	{
		return false;
	}

	FNiagaraDataInterfaceProxyGrid2DCollectionProxy* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyGrid2DCollectionProxy>();
	ENQUEUE_RENDER_COMMAND(FUpdateDIColorCurve)(
		[RT_Proxy, RT_InstanceID=SystemInstance->GetId(), RT_TextureResource=Dest->Resource](FRHICommandListImmediate& RHICmdList)
	{
		FGrid2DCollectionRWInstanceData_RenderThread* RT_Grid2DInstanceData = RT_Proxy->SystemInstancesToProxyData_RT.Find(RT_InstanceID);
		if (RT_TextureResource && RT_TextureResource->TextureRHI.IsValid() && RT_Grid2DInstanceData && RT_Grid2DInstanceData->CurrentData)
		{
			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, RT_Grid2DInstanceData->CurrentData->GridBuffer.Buffer);
			RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, RT_TextureResource->TextureRHI);

			FRHICopyTextureInfo CopyInfo;
			RHICmdList.CopyTexture(RT_Grid2DInstanceData->CurrentData->GridBuffer.Buffer, RT_TextureResource->TextureRHI, CopyInfo);

			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, RT_TextureResource->TextureRHI);
		}
	});

	return true;
}

UFUNCTION(BlueprintCallable, Category = Niagara)
void UNiagaraDataInterfaceGrid2DCollection::GetRawTextureSize(const UNiagaraComponent *Component, int &SizeX, int &SizeY)
{
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

	FGrid2DCollectionRWInstanceData_GameThread* Grid2DInstanceData = SystemInstancesToProxyData_GT.FindRef(InstanceID);
	if (!Grid2DInstanceData)
	{
		SizeX = -1;
		SizeY = -1;
		return;
	}

	SizeX = Grid2DInstanceData->NumCells.X * Grid2DInstanceData->NumTiles.X;
	SizeY = Grid2DInstanceData->NumCells.Y * Grid2DInstanceData->NumTiles.Y;
}

UFUNCTION(BlueprintCallable, Category = Niagara)
void UNiagaraDataInterfaceGrid2DCollection::GetTextureSize(const UNiagaraComponent *Component, int &SizeX, int &SizeY)
{
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

	FGrid2DCollectionRWInstanceData_GameThread* Grid2DInstanceData = SystemInstancesToProxyData_GT.FindRef(InstanceID);
	if (!Grid2DInstanceData)
	{
		SizeX = -1;
		SizeY = -1;
		return;
	}

	SizeX = Grid2DInstanceData->NumCells.X;
	SizeY = Grid2DInstanceData->NumCells.Y;
}

void UNiagaraDataInterfaceGrid2DCollection::GetWorldBBoxSize(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FGrid2DCollectionRWInstanceData_GameThread> InstData(Context);
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
	VectorVM::FUserPtrHandler<FGrid2DCollectionRWInstanceData_GameThread> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutCellSizeX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutCellSizeY(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{	
		*OutCellSizeX.GetDestAndAdvance() = InstData->CellSize.X;
		*OutCellSizeY.GetDestAndAdvance() = InstData->CellSize.Y;
	}
}

void FGrid2DCollectionRWInstanceData_RenderThread::BeginSimulate()
{
	for (TUniquePtr<FGrid2DBuffer>& Buffer : Buffers)
	{
		check(Buffer.IsValid());
		if (Buffer.Get() != CurrentData)
		{
			DestinationData = Buffer.Get();
			break;
		}
	}

	if (DestinationData == nullptr)
	{
		DestinationData = new FGrid2DBuffer(NumCells.X * NumTiles.X, NumCells.Y * NumTiles.Y);
		Buffers.Emplace(DestinationData);
	}
}

void FGrid2DCollectionRWInstanceData_RenderThread::EndSimulate()
{
	CurrentData = DestinationData;
	DestinationData = nullptr;
}

void FNiagaraDataInterfaceProxyGrid2DCollectionProxy::PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context)
{
	// #todo(dmp): Context doesnt need to specify if a stage is output or not since we moved pre/post stage to the DI itself.  Not sure which design is better for the future
	if (Context.IsOutputStage)
	{
		FGrid2DCollectionRWInstanceData_RenderThread* ProxyData = SystemInstancesToProxyData_RT.Find(Context.SystemInstance);

		ProxyData->BeginSimulate();

		// If we don't have an iteration stage, then we should manually clear the buffer to make sure there is no residual data.  If we are doing something like rasterizing particles into a grid, we want it to be clear before
		// we start.  If a user wants to access data from the previous stage, then they can read from the current data.

		// #todo(dmp): we might want to expose an option where we have buffers that are write only and need a clear (ie: no buffering like the neighbor grid).  They would be considered transient perhaps?  It'd be more
		// memory efficient since it would theoretically not require any double buffering.		
		if (!Context.IsIterationStage)
		{
			RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, ProxyData->DestinationData->GridBuffer.UAV);
			RHICmdList.ClearUAVFloat(ProxyData->DestinationData->GridBuffer.UAV, FVector4(ForceInitToZero));
			RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, ProxyData->DestinationData->GridBuffer.UAV);
		}
	}
}

void FNiagaraDataInterfaceProxyGrid2DCollectionProxy::PostStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context)
{
	
	if (Context.IsOutputStage)
	{
		FGrid2DCollectionRWInstanceData_RenderThread* ProxyData = SystemInstancesToProxyData_RT.Find(Context.SystemInstance);
		ProxyData->EndSimulate();
	}
}

void FNiagaraDataInterfaceProxyGrid2DCollectionProxy::ResetData(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context)
{	
	FGrid2DCollectionRWInstanceData_RenderThread* ProxyData = SystemInstancesToProxyData_RT.Find(Context.SystemInstance);
	if (!ProxyData)
	{
		return;
	}

	for (TUniquePtr<FGrid2DBuffer>& Buffer : ProxyData->Buffers)
	{
		if (Buffer.IsValid())
		{
			RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, Buffer->GridBuffer.UAV);
			RHICmdList.ClearUAVFloat(Buffer->GridBuffer.UAV, FVector4(ForceInitToZero));
			RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, Buffer->GridBuffer.UAV);
		}		
	}	
}
#undef LOCTEXT_NAMESPACE
