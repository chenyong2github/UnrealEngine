// Copyright Epic Games, Inc. All Rights Reserved.
#include "NiagaraDataInterfaceGrid3DCollection.h"
#include "NiagaraShader.h"
#include "ShaderParameterUtils.h"
#include "ClearQuad.h"
#include "TextureResource.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraRenderer.h"
#include "Engine/VolumeTexture.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "NiagaraSettings.h"
#if WITH_EDITOR
#include "NiagaraGpuComputeDebug.h"
#endif

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceGrid3DCollection"

const FString UNiagaraDataInterfaceGrid3DCollection::NumTilesName(TEXT("NumTiles_"));

const FString UNiagaraDataInterfaceGrid3DCollection::GridName(TEXT("Grid_"));
const FString UNiagaraDataInterfaceGrid3DCollection::OutputGridName(TEXT("OutputGrid_"));
const FString UNiagaraDataInterfaceGrid3DCollection::SamplerName(TEXT("Sampler_"));


// Global VM function names, also used by the shaders code generation methods.
const FName UNiagaraDataInterfaceGrid3DCollection::SetValueFunctionName("SetGridValue");
const FName UNiagaraDataInterfaceGrid3DCollection::GetValueFunctionName("GetGridValue");

const FName UNiagaraDataInterfaceGrid3DCollection::SampleGridFunctionName("SampleGrid");

const FName UNiagaraDataInterfaceGrid3DCollection::SetNumCellsFunctionName("SetNumCells");

FNiagaraVariableBase UNiagaraDataInterfaceGrid3DCollection::ExposedRTVar;
const FString UNiagaraDataInterfaceGrid3DCollection::AnonymousAttributeString("Attribute At Index");
static float GNiagaraGrid3DResolutionMultiplier = 1.0f;
static FAutoConsoleVariableRef CVarNiagaraGrid3DResolutionMultiplier(
	TEXT("fx.Niagara.Grid3D.ResolutionMultiplier"),
	GNiagaraGrid3DResolutionMultiplier,
	TEXT("Optional global modifier to grid resolution\n"),
	ECVF_Default
);

static int32 GNiagaraGrid3DOverrideFormat = -1;
static FAutoConsoleVariableRef CVarNiagaraGrid3DOverrideFormat(
	TEXT("fx.Niagara.Grid3D.OverrideFormat"),
	GNiagaraGrid3DOverrideFormat,
	TEXT("Optional override for all grids to use this format.\n"),
	ECVF_Default
);

/*--------------------------------------------------------------------------------------------------------------------------*/
struct FNiagaraDataInterfaceParametersCS_Grid3DCollection : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_Grid3DCollection, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{			
		NumCellsParam.Bind(ParameterMap, *(UNiagaraDataInterfaceRWBase::NumCellsName + ParameterInfo.DataInterfaceHLSLSymbol));
		NumTilesParam.Bind(ParameterMap, *(UNiagaraDataInterfaceGrid3DCollection::NumTilesName + ParameterInfo.DataInterfaceHLSLSymbol));
		UnitToUVParam.Bind(ParameterMap, *(UNiagaraDataInterfaceRWBase::UnitToUVName + ParameterInfo.DataInterfaceHLSLSymbol));
		CellSizeParam.Bind(ParameterMap, *(UNiagaraDataInterfaceRWBase::CellSizeName + ParameterInfo.DataInterfaceHLSLSymbol));

		WorldBBoxSizeParam.Bind(ParameterMap, *(UNiagaraDataInterfaceRWBase::WorldBBoxSizeName + ParameterInfo.DataInterfaceHLSLSymbol));

		GridParam.Bind(ParameterMap, *(UNiagaraDataInterfaceGrid3DCollection::GridName + ParameterInfo.DataInterfaceHLSLSymbol));
		OutputGridParam.Bind(ParameterMap, *(UNiagaraDataInterfaceGrid3DCollection::OutputGridName + ParameterInfo.DataInterfaceHLSLSymbol));

		SamplerParam.Bind(ParameterMap, *(UNiagaraDataInterfaceGrid3DCollection::SamplerName + ParameterInfo.DataInterfaceHLSLSymbol));
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		// Get shader and DI
		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
		FNiagaraDataInterfaceProxyGrid3DCollectionProxy* VFDI = static_cast<FNiagaraDataInterfaceProxyGrid3DCollectionProxy*>(Context.DataInterface);
		
		FGrid3DCollectionRWInstanceData_RenderThread* ProxyData = VFDI->SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID);
		check(ProxyData);

		int NumCellsTmp[3];
		NumCellsTmp[0] = ProxyData->NumCells.X;
		NumCellsTmp[1] = ProxyData->NumCells.Y;
		NumCellsTmp[2] = ProxyData->NumCells.Z;
		SetShaderValue(RHICmdList, ComputeShaderRHI, NumCellsParam, NumCellsTmp);	

		int NumTilesTmp[3];
		NumTilesTmp[0] = ProxyData->NumTiles.X;
		NumTilesTmp[1] = ProxyData->NumTiles.Y;
		NumTilesTmp[2] = ProxyData->NumTiles.Z;
		SetShaderValue(RHICmdList, ComputeShaderRHI, NumTilesParam, NumTilesTmp);		

		SetShaderValue(RHICmdList, ComputeShaderRHI, UnitToUVParam, FVector(1.0f) / FVector(ProxyData->NumCells));

		SetShaderValue(RHICmdList, ComputeShaderRHI, CellSizeParam, ProxyData->CellSize);		
				
		SetShaderValue(RHICmdList, ComputeShaderRHI, WorldBBoxSizeParam, ProxyData->WorldBBoxSize);

		FRHISamplerState *SamplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		SetSamplerParameter(RHICmdList, ComputeShaderRHI, SamplerParam, SamplerState);

		if (GridParam.IsBound())
		{
			FRHIShaderResourceView* InputGridBuffer;
			if (ProxyData->CurrentData != nullptr)
			{
				InputGridBuffer = ProxyData->CurrentData->GridBuffer.SRV;
			}
			else
			{
				InputGridBuffer = FNiagaraRenderer::GetDummyTextureReadBuffer2D();
			}
			SetSRVParameter(RHICmdList, ComputeShaderRHI, GridParam, InputGridBuffer);
		}

		if ( OutputGridParam.IsUAVBound() )
		{
			FRHIUnorderedAccessView* OutputGridUAV;
			if (Context.IsOutputStage && ProxyData->DestinationData != nullptr)
			{
				OutputGridUAV = ProxyData->DestinationData->GridBuffer.UAV;
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
			FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
			OutputGridParam.UnsetUAV(RHICmdList, ComputeShaderRHI);
		}
	}

private:

	LAYOUT_FIELD(FShaderParameter, UnitToUVParam);
	LAYOUT_FIELD(FShaderParameter, NumCellsParam);
	LAYOUT_FIELD(FShaderParameter, NumTilesParam);
	LAYOUT_FIELD(FShaderParameter, CellSizeParam);
	LAYOUT_FIELD(FShaderParameter, WorldBBoxSizeParam);

	LAYOUT_FIELD(FShaderResourceParameter, GridParam);
	LAYOUT_FIELD(FRWShaderParameter, OutputGridParam);
	
	LAYOUT_FIELD(FShaderResourceParameter, SamplerParam);
};

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_Grid3DCollection);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceGrid3DCollection, FNiagaraDataInterfaceParametersCS_Grid3DCollection);

UNiagaraDataInterfaceGrid3DCollection::UNiagaraDataInterfaceGrid3DCollection(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, NumAttributes(1)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyGrid3DCollectionProxy());	

	FNiagaraTypeDefinition Def(UTextureRenderTarget::StaticClass());
	RenderTargetUserParameter.Parameter.SetType(Def);
}

void UNiagaraDataInterfaceGrid3DCollection::PostInitProperties()
{
	Super::PostInitProperties();

	//Can we register data interfaces as regular types and fold them into the FNiagaraVariable framework for UI and function calls etc?
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), /*bCanBeParameter*/ true, /*bCanBePayload*/ false, /*bIsUserDefined*/ false);
		ExposedRTVar = FNiagaraVariableBase(FNiagaraTypeDefinition(UTexture::StaticClass()), TEXT("RenderTarget"));
	}
}

void UNiagaraDataInterfaceGrid3DCollection::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	Super::GetFunctions(OutFunctions);

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SetNumCellsFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsY")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsZ")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Success")));

		Sig.ModuleUsageBitmask = ENiagaraScriptUsageMask::Emitter | ENiagaraScriptUsageMask::System;
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
		Sig.Name = GetValueFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexZ")));
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
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexZ")));
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
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("UnitZ")));
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
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceGrid3DCollection, GetWorldBBoxSize);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceGrid3DCollection, GetCellSize);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceGrid3DCollection, SetNumCells);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceGrid3DCollection, GetNumCells);
void UNiagaraDataInterfaceGrid3DCollection::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	Super::GetVMExternalFunction(BindingInfo, InstanceData, OutFunc);

	
	if (BindingInfo.Name == UNiagaraDataInterfaceRWBase::WorldBBoxSizeFunctionName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceGrid3DCollection, GetWorldBBoxSize)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == UNiagaraDataInterfaceRWBase::CellSizeFunctionName)
	{
		// #todo(dmp): this will override the base class definition for GetCellSize because the data interface instance data computes cell size
		// it would be nice to refactor this so it can be part of the super class
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceGrid3DCollection, GetCellSize)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SetNumCellsFunctionName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceGrid3DCollection, SetNumCells)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == NumCellsFunctionName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceGrid3DCollection, GetNumCells)::Bind(this, OutFunc);
	}
	//else if (BindingInfo.Name == GetValueFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == SetValueFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == SampleGridFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
}

void UNiagaraDataInterfaceGrid3DCollection::GetNumCells(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FGrid3DCollectionRWInstanceData_GameThread> InstData(Context);

	FNDIOutputParam<int32> NumCellsX(Context);
	FNDIOutputParam<int32> NumCellsY(Context);
	FNDIOutputParam<int32> NumCellsZ(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		NumCellsX.SetAndAdvance(InstData->NumCells.X);
		NumCellsY.SetAndAdvance(InstData->NumCells.Y);
		NumCellsZ.SetAndAdvance(InstData->NumCells.Z);
	}
}

bool UNiagaraDataInterfaceGrid3DCollection::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceGrid3DCollection* OtherTyped = CastChecked<const UNiagaraDataInterfaceGrid3DCollection>(Other);

	return OtherTyped != nullptr &&
		OtherTyped->NumAttributes == NumAttributes &&
		OtherTyped->RenderTargetUserParameter == RenderTargetUserParameter &&
		OtherTyped->OverrideBufferFormat == OverrideBufferFormat &&
#if WITH_EDITORONLY_DATA
		OtherTyped->bPreviewGrid == bPreviewGrid &&
		OtherTyped->PreviewAttribute == PreviewAttribute &&
#endif
		OtherTyped->bOverrideFormat == bOverrideFormat;
}

void UNiagaraDataInterfaceGrid3DCollection::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	Super::GetParameterDefinitionHLSL(ParamInfo, OutHLSL);

	static const TCHAR *FormatDeclarations = TEXT(R"(				
		Texture3D<float> {GridName};
		RWTexture3D<float> RW{OutputGridName};
		int3 {NumTiles};
		SamplerState {SamplerName};
	
	)");
	TMap<FString, FStringFormatArg> ArgsDeclarations = {				
		{ TEXT("GridName"),    GridName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("SamplerName"),    SamplerName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("OutputGridName"),    OutputGridName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("NumTiles"),    NumTilesName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("UnitToUVName"), UNiagaraDataInterfaceRWBase::UnitToUVName + ParamInfo.DataInterfaceHLSLSymbol},
	};
	OutHLSL += FString::Format(FormatDeclarations, ArgsDeclarations);
}

bool UNiagaraDataInterfaceGrid3DCollection::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	bool ParentRet = Super::GetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, OutHLSL);
	if (ParentRet)
	{
		return true;
	} 
	else if (FunctionInfo.DefinitionName == GetValueFunctionName)
	{
		static const TCHAR *FormatBounds = TEXT(R"(
			void {FunctionName}(int In_IndexX, int In_IndexY, int In_IndexZ, int In_AttributeIndex, out float Out_Val)
			{
				int TileIndexX = In_AttributeIndex % {NumTiles}.x;
				int TileIndexY = (In_AttributeIndex / {NumTiles}.x) % {NumTiles}.y;
				int TileIndexZ = In_AttributeIndex / ({NumTiles}.x * {NumTiles}.y);

				Out_Val = {Grid}.Load(int4(In_IndexX + TileIndexX * {NumCellsName}.x, In_IndexY + TileIndexY * {NumCellsName}.y, In_IndexZ + TileIndexZ * {NumCellsName}.z, 0));
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("Grid"), GridName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("NumCellsName"), UNiagaraDataInterfaceRWBase::NumCellsName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("UnitToUVName"), UNiagaraDataInterfaceRWBase::UnitToUVName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("NumTiles"),    NumTilesName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SetValueFunctionName)
	{
		static const TCHAR *FormatBounds = TEXT(R"(
			void {FunctionName}(int In_IndexX, int In_IndexY, int In_IndexZ, int In_AttributeIndex, float In_Value, out int val)
			{			
				int TileIndexX = In_AttributeIndex % {NumTiles}.x;
				int TileIndexY = (In_AttributeIndex / {NumTiles}.x) % {NumTiles}.y;
				int TileIndexZ = In_AttributeIndex / ({NumTiles}.x * {NumTiles}.y);

				val = 0;
				RW{OutputGrid}[int3(In_IndexX + TileIndexX * {NumCellsName}.x, In_IndexY + TileIndexY * {NumCellsName}.y, In_IndexZ + TileIndexZ * {NumCellsName}.z)] = In_Value;
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("OutputGrid"), OutputGridName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("NumCellsName"), UNiagaraDataInterfaceRWBase::NumCellsName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("UnitToUVName"), UNiagaraDataInterfaceRWBase::UnitToUVName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("NumTiles"), NumTilesName + ParamInfo.DataInterfaceHLSLSymbol},

		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleGridFunctionName)
	{
		static const TCHAR *FormatBounds = TEXT(R"(
			void {FunctionName}(float In_UnitX, float In_UnitY, float In_UnitZ, int In_AttributeIndex, out float Out_Val)
			{
				int TileIndexX = In_AttributeIndex % {NumTiles}.x;
				int TileIndexY = (In_AttributeIndex / {NumTiles}.x) % {NumTiles}.y;
				int TileIndexZ = In_AttributeIndex / ({NumTiles}.x * {NumTiles}.y);		

				float3 UVW =
				{
					In_UnitX / {NumTiles}.x + 1.0*TileIndexX/{NumTiles}.x,
					In_UnitY / {NumTiles}.y + 1.0*TileIndexY/{NumTiles}.y,
					In_UnitZ / {NumTiles}.z + 1.0*TileIndexZ/{NumTiles}.y
				};
				float3 TileMin =
				{
					(TileIndexX * {NumCellsName}.x + 0.5) / ({NumTiles}.x * {NumCellsName}.x),
					(TileIndexY * {NumCellsName}.y + 0.5) / ({NumTiles}.y * {NumCellsName}.y),
					(TileIndexZ * {NumCellsName}.z + 0.5) / ({NumTiles}.z * {NumCellsName}.z)
				};
				float3 TileMax =
				{
					((TileIndexX + 1) * {NumCellsName}.x - 0.5) / ({NumTiles}.x * {NumCellsName}.x),
					((TileIndexY + 1) * {NumCellsName}.y - 0.5) / ({NumTiles}.y * {NumCellsName}.y),
					((TileIndexZ + 1) * {NumCellsName}.z - 0.5) / ({NumTiles}.z * {NumCellsName}.z)
				};
				UVW = clamp(UVW, TileMin, TileMax);

				Out_Val = {Grid}.SampleLevel({SamplerName}, UVW, 0);
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("Grid"), GridName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("SamplerName"), SamplerName + ParamInfo.DataInterfaceHLSLSymbol },
			{TEXT("NumTiles"), NumTilesName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("UnitToUVName"), UNiagaraDataInterfaceRWBase::UnitToUVName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("NumCellsName"), UNiagaraDataInterfaceRWBase::NumCellsName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	return false;
}

bool UNiagaraDataInterfaceGrid3DCollection::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceGrid3DCollection* OtherTyped = CastChecked<UNiagaraDataInterfaceGrid3DCollection>(Destination);
	OtherTyped->NumAttributes = NumAttributes;
	OtherTyped->RenderTargetUserParameter = RenderTargetUserParameter;
	OtherTyped->OverrideBufferFormat = OverrideBufferFormat;
	OtherTyped->bOverrideFormat = bOverrideFormat;
#if WITH_EDITORONLY_DATA
	OtherTyped->bPreviewGrid = bPreviewGrid;
	OtherTyped->PreviewAttribute = PreviewAttribute;
#endif

	return true;
}

bool UNiagaraDataInterfaceGrid3DCollection::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	check(Proxy);

	FGrid3DCollectionRWInstanceData_GameThread* InstanceData = new (PerInstanceData) FGrid3DCollectionRWInstanceData_GameThread();
	SystemInstancesToProxyData_GT.Emplace(SystemInstance->GetId(), InstanceData);
		
	if (SetResolutionMethod == ESetResolutionMethod::Independent)
	{
		InstanceData->NumCells.X = NumCells.X;
		InstanceData->NumCells.Y = NumCells.Y;
		InstanceData->NumCells.Z = NumCells.Z;

		InstanceData->WorldBBoxSize = WorldBBoxSize;
		InstanceData->CellSize = InstanceData->WorldBBoxSize / FVector(InstanceData->NumCells);
	}
	else if (SetResolutionMethod == ESetResolutionMethod::MaxAxis)
	{
		InstanceData->CellSize = FVector(FMath::Max<float>(FMath::Max(WorldBBoxSize.X, WorldBBoxSize.Y), WorldBBoxSize.Z) / ((float) NumCellsMaxAxis));
	}	
	else if (SetResolutionMethod == ESetResolutionMethod::CellSize)
	{
		InstanceData->CellSize = FVector(CellSize);
	}

	ENiagaraGpuBufferFormat BufferFormat = bOverrideFormat ? OverrideBufferFormat : GetDefault<UNiagaraSettings>()->DefaultGridFormat;
	if (GNiagaraGrid3DOverrideFormat >= int32(ENiagaraGpuBufferFormat::Float) && (GNiagaraGrid3DOverrideFormat < int32(ENiagaraGpuBufferFormat::Max)))
	{
		BufferFormat = ENiagaraGpuBufferFormat(GNiagaraGrid3DOverrideFormat);
	}
	InstanceData->PixelFormat = FNiagaraUtilities::BufferFormatToPixelFormat(BufferFormat);

	// compute world bounds and padding based on cell size
	if (SetResolutionMethod == ESetResolutionMethod::MaxAxis || SetResolutionMethod == ESetResolutionMethod::CellSize)
	{
		InstanceData->NumCells.X = WorldBBoxSize.X / InstanceData->CellSize[0];
		InstanceData->NumCells.Y = WorldBBoxSize.Y / InstanceData->CellSize[0];
		InstanceData->NumCells.Z = WorldBBoxSize.Z / InstanceData->CellSize[0];

		// Pad grid by 1 cell if our computed bounding box is too small
		if (WorldBBoxSize.X > WorldBBoxSize.Y && WorldBBoxSize.X > WorldBBoxSize.Z)
		{
			if (!FMath::IsNearlyEqual(InstanceData->CellSize[0] * InstanceData->NumCells.Y, WorldBBoxSize.Y))
			{
				InstanceData->NumCells.Y++;
			}

			if (!FMath::IsNearlyEqual(InstanceData->CellSize[0] * InstanceData->NumCells.Z, WorldBBoxSize.Z))
			{
				InstanceData->NumCells.Z++;
			}
		} 
		else if (WorldBBoxSize.Y > WorldBBoxSize.X && WorldBBoxSize.Y > WorldBBoxSize.Z)
		{
			if (!FMath::IsNearlyEqual(InstanceData->CellSize[0] * InstanceData->NumCells.X, WorldBBoxSize.X))
			{
				InstanceData->NumCells.X++;
			}

			if (!FMath::IsNearlyEqual(InstanceData->CellSize[0] * InstanceData->NumCells.Z, WorldBBoxSize.Z))
			{
				InstanceData->NumCells.Z++;
			}
		} 
		else if (WorldBBoxSize.Z > WorldBBoxSize.X && WorldBBoxSize.Z > WorldBBoxSize.Y)
		{
			if (!FMath::IsNearlyEqual(InstanceData->CellSize[0] * InstanceData->NumCells.X, WorldBBoxSize.X))
			{
				InstanceData->NumCells.X++;
			}

			if (!FMath::IsNearlyEqual(InstanceData->CellSize[0] * InstanceData->NumCells.Y, WorldBBoxSize.Y))
			{
				InstanceData->NumCells.Y++;
			}
		}

		InstanceData->WorldBBoxSize = FVector(InstanceData->NumCells.X, InstanceData->NumCells.Y, InstanceData->NumCells.Z) * InstanceData->CellSize[0];
	}	

	if (InstanceData->NumCells.X <= 0 ||
		InstanceData->NumCells.Y <= 0 ||
		InstanceData->NumCells.Z <= 0)
	{
		UE_LOG(LogNiagara, Error, TEXT("Zero grid resolution defined on %s"), *FNiagaraUtilities::SystemInstanceIDToString(SystemInstance->GetId()));
		return false;
	}

	if (!FMath::IsNearlyEqual(GNiagaraGrid3DResolutionMultiplier, 1.0f))
	{
		InstanceData->NumCells.X = FMath::Max(1, int32(float(InstanceData->NumCells.X) * GNiagaraGrid3DResolutionMultiplier));
		InstanceData->NumCells.Y = FMath::Max(1, int32(float(InstanceData->NumCells.Y) * GNiagaraGrid3DResolutionMultiplier));
		InstanceData->NumCells.Z = FMath::Max(1, int32(float(InstanceData->NumCells.Z) * GNiagaraGrid3DResolutionMultiplier));
	}

	// Compute number of tiles based on resolution of individual attributes
	// #todo(dmp): refactor
	int32 MaxDim = 16384;
	int32 MaxTilesX = floor(MaxDim / InstanceData->NumCells.X);
	int32 MaxTilesY = floor(MaxDim / InstanceData->NumCells.Y);
	int32 MaxTilesZ = floor(MaxDim / InstanceData->NumCells.Z);
	int32 MaxAttributes = MaxTilesX * MaxTilesY * MaxTilesZ;
	if ((NumAttributes > MaxAttributes && MaxAttributes > 0) || NumAttributes == 0)
	{
		UE_LOG(LogNiagara, Error, TEXT("Invalid number of attributes defined on %s... max is %i, num defined is %i"), *FNiagaraUtilities::SystemInstanceIDToString(SystemInstance->GetId()), MaxAttributes, NumAttributes);
		return false;
	}

	// need to determine number of tiles in x and y based on number of attributes and max dimension size
	int32 NumTilesX = FMath::Min<int32>(MaxTilesX, NumAttributes);
	int32 NumTilesY = FMath::Min<int32>(MaxTilesY, ceil(1.0 * NumAttributes / NumTilesX));
	int32 NumTilesZ = FMath::Min<int32>(MaxTilesZ, ceil(1.0 * NumAttributes / (NumTilesX * NumTilesY)));

	InstanceData->NumTiles.X = NumTilesX;
	InstanceData->NumTiles.Y = NumTilesY;
	InstanceData->NumTiles.Z = NumTilesZ;

	check(InstanceData->NumTiles.X > 0);
	check(InstanceData->NumTiles.Y > 0);
	check(InstanceData->NumTiles.Z > 0);

	// Initialize target texture
	InstanceData->TargetTexture = nullptr;
	InstanceData->RTUserParamBinding.Init(SystemInstance->GetInstanceParameters(), RenderTargetUserParameter.Parameter);
	InstanceData->UpdateTargetTexture(BufferFormat);

#if WITH_EDITOR
	InstanceData->bPreviewGrid = bPreviewGrid;
	InstanceData->PreviewAttribute = FIntVector4(INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE);
	if (bPreviewGrid && !PreviewAttribute.IsNone())
	{
	//-TODO: Hook up when we have named attribubtes
	//	const int32 VariableIndex = InstanceData->Vars.IndexOfByPredicate([&](const FNiagaraVariableBase& Variable) { return Variable.GetName() == PreviewAttribute; });
	//	if (VariableIndex != INDEX_NONE)
	//	{
	//		const int32 NumComponents = InstanceData->Vars[VariableIndex].GetType().GetSize() / sizeof(float);
	//		if (ensure(NumComponents > 0 && NumComponents <= 4))
	//		{
	//			const int32 ComponentOffset = InstanceData->Offsets[VariableIndex];
	//			for (int32 i = 0; i < NumComponents; ++i)
	//			{
	//				InstanceData->PreviewAttribute[i] = ComponentOffset + i;
	//			}
	//		}
	//	}
	//	// Look for anonymous attributes
	//	else if (NumAttributes > 0)
	//	{
			const FString PreviewAttributeString = PreviewAttribute.ToString();
			if (PreviewAttributeString.StartsWith(AnonymousAttributeString))
			{
				InstanceData->PreviewAttribute[0] = FCString::Atoi(&PreviewAttributeString.GetCharArray()[AnonymousAttributeString.Len() + 1]);
			}
	//	}

		if (InstanceData->PreviewAttribute == FIntVector4(INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE))
		{
			UE_LOG(LogNiagara, Warning, TEXT("Failed to map PreviewAttribute %s to a grid index"), *PreviewAttribute.ToString());
		}
	}
#endif

	// Push Updates to Proxy.
	FNiagaraDataInterfaceProxyGrid3DCollectionProxy* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyGrid3DCollectionProxy>();
	ENQUEUE_RENDER_COMMAND(FUpdateData)(
		[RT_Proxy, RT_Resource=InstanceData->TargetTexture ? InstanceData->TargetTexture->Resource : nullptr, InstanceID = SystemInstance->GetId(), RT_InstanceData=*InstanceData, RT_OutputShaderStages=OutputShaderStages, RT_IterationShaderStages= IterationShaderStages](FRHICommandListImmediate& RHICmdList)
	{
		check(!RT_Proxy->SystemInstancesToProxyData_RT.Contains(InstanceID));
		FGrid3DCollectionRWInstanceData_RenderThread* TargetData = &RT_Proxy->SystemInstancesToProxyData_RT.Add(InstanceID);

		TargetData->NumCells = RT_InstanceData.NumCells;
		TargetData->NumTiles = RT_InstanceData.NumTiles;
		TargetData->CellSize = RT_InstanceData.CellSize;
		TargetData->WorldBBoxSize = RT_InstanceData.WorldBBoxSize;
		TargetData->PixelFormat = RT_InstanceData.PixelFormat;
#if WITH_EDITOR
		TargetData->bPreviewGrid = RT_InstanceData.bPreviewGrid;
		TargetData->PreviewAttribute = RT_InstanceData.PreviewAttribute;
#endif

		RT_Proxy->OutputSimulationStages_DEPRECATED = RT_OutputShaderStages;
		RT_Proxy->IterationSimulationStages_DEPRECATED = RT_IterationShaderStages;

		if (RT_Resource && RT_Resource->TextureRHI.IsValid())
		{
			TargetData->RenderTargetToCopyTo = RT_Resource->TextureRHI;
		}
		else
		{
			TargetData->RenderTargetToCopyTo = nullptr;
		}
	});

	return true;
}


void UNiagaraDataInterfaceGrid3DCollection::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	SystemInstancesToProxyData_GT.Remove(SystemInstance->GetId());

	FGrid3DCollectionRWInstanceData_GameThread* InstanceData = static_cast<FGrid3DCollectionRWInstanceData_GameThread*>(PerInstanceData);

	InstanceData->~FGrid3DCollectionRWInstanceData_GameThread();

	FNiagaraDataInterfaceProxyGrid3DCollectionProxy* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyGrid3DCollectionProxy>();
	ENQUEUE_RENDER_COMMAND(FNiagaraDIDestroyInstanceData) (
		[RT_Proxy, InstanceID=SystemInstance->GetId(), Batcher=SystemInstance->GetBatcher()](FRHICommandListImmediate& CmdList)
		{
			//check(ThisProxy->SystemInstancesToProxyData.Contains(InstanceID));
			RT_Proxy->SystemInstancesToProxyData_RT.Remove(InstanceID);
		}
	);
}


bool UNiagaraDataInterfaceGrid3DCollection::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	FGrid3DCollectionRWInstanceData_GameThread* InstanceData = SystemInstancesToProxyData_GT.FindRef(SystemInstance->GetId());

	ENiagaraGpuBufferFormat BufferFormat = bOverrideFormat ? OverrideBufferFormat : GetDefault<UNiagaraSettings>()->DefaultGridFormat;
	if (GNiagaraGrid3DOverrideFormat >= int32(ENiagaraGpuBufferFormat::Float) && (GNiagaraGrid3DOverrideFormat < int32(ENiagaraGpuBufferFormat::Max)))
	{
		BufferFormat = ENiagaraGpuBufferFormat(GNiagaraGrid3DOverrideFormat);
	}
	bool NeedsReset = InstanceData->UpdateTargetTexture(BufferFormat);

	FNiagaraDataInterfaceProxyGrid3DCollectionProxy* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyGrid3DCollectionProxy>();
	ENQUEUE_RENDER_COMMAND(FUpdateData)(
		[RT_Resource= InstanceData->TargetTexture ? InstanceData->TargetTexture->Resource : nullptr, RT_Proxy, InstanceID = SystemInstance->GetId()](FRHICommandListImmediate& RHICmdList)
	{
		FGrid3DCollectionRWInstanceData_RenderThread* TargetData = RT_Proxy->SystemInstancesToProxyData_RT.Find(InstanceID);

		if (RT_Resource && RT_Resource->TextureRHI.IsValid())
		{
			TargetData->RenderTargetToCopyTo = RT_Resource->TextureRHI;
		}
		else
		{
			TargetData->RenderTargetToCopyTo = nullptr;
		}

	});

	return NeedsReset;
}

void UNiagaraDataInterfaceGrid3DCollection::GetExposedVariables(TArray<FNiagaraVariableBase>& OutVariables) const
{
	OutVariables.Emplace(ExposedRTVar);
}

bool UNiagaraDataInterfaceGrid3DCollection::GetExposedVariableValue(const FNiagaraVariableBase& InVariable, void* InPerInstanceData, FNiagaraSystemInstance* InSystemInstance, void* OutData) const
{
	FGrid3DCollectionRWInstanceData_GameThread* InstanceData = static_cast<FGrid3DCollectionRWInstanceData_GameThread*>(InPerInstanceData);
	if (InVariable.IsValid() && InVariable == ExposedRTVar && InstanceData && InstanceData->TargetTexture)
	{
		UTextureRenderTarget** Var = (UTextureRenderTarget**)OutData;
		*Var = InstanceData->TargetTexture;
		return true;
	}
	return false;
}

UFUNCTION(BlueprintCallable, Category = Niagara)
bool UNiagaraDataInterfaceGrid3DCollection::FillVolumeTexture(const UNiagaraComponent *Component, UVolumeTexture*Dest, int AttributeIndex)
{
	/*
	#todo(dmp): we need to get a UVolumeTextureRenderTarget for any of this to work
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
	if (!Dest || Dest->GetSizeX() != NumCells.X || Dest->GetSizeY() != NumCells.Y || Dest->GetSizeZ() != NumCells.Z || Dest->GetPixelFormat() != RequiredTye)
	{
		return false;
	}

	FNiagaraDataInterfaceProxyGrid3DCollectionProxy* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyGrid3DCollectionProxy>();
	ENQUEUE_RENDER_COMMAND(FUpdateDIColorCurve)(
		[RT_Proxy, InstanceID=SystemInstance->GetId(), RT_TextureResource=Dest->Resource, AttributeIndex](FRHICommandListImmediate& RHICmdList)
	{
		FGrid3DCollectionRWInstanceData_RenderThread* Grid3DInstanceData = RT_Proxy->SystemInstancesToProxyData_RT.Find(InstanceID);

		if (RT_TextureResource && RT_TextureResource->TextureRHI.IsValid() && Grid3DInstanceData && Grid3DInstanceData->CurrentData)
		{
			FRHICopyTextureInfo CopyInfo;
			CopyInfo.Size = FIntVector(Grid3DInstanceData->NumCells.X, Grid3DInstanceData->NumCells.Y, 1);

			int TileIndexX = AttributeIndex % Grid3DInstanceData->NumTiles.X;
			int TileIndexY = AttributeIndex / Grid3DInstanceData->NumTiles.X;
			int StartX = TileIndexX * Grid3DInstanceData->NumCells.X;
			int StartY = TileIndexY * Grid3DInstanceData->NumCells.Y;
			CopyInfo.SourcePosition = FIntVector(StartX, StartY, 0);

			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, Grid3DInstanceData->CurrentData->GridBuffer.Buffer);
			RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, RT_TextureResource->TextureRHI);

			RHICmdList.CopyTexture(Grid3DInstanceData->CurrentData->GridBuffer.Buffer, RT_TextureResource->TextureRHI, CopyInfo);

			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, RT_TextureResource->TextureRHI);			
		}
	});
	
	return true;
	*/
	return false;
}

UFUNCTION(BlueprintCallable, Category = Niagara)
bool UNiagaraDataInterfaceGrid3DCollection::FillRawVolumeTexture(const UNiagaraComponent *Component, UVolumeTexture *Dest, int &TilesX, int &TilesY, int&TilesZ)
{
	/*
	#todo(dmp): we need to get a UVolumeTextureRenderTarget for any of this to work

	if (!Component)
	{
		TilesX = -1;
		TilesY = -1;
		TilesZ = -1;
		return false;
	}

	FNiagaraSystemInstance* SystemInstance = Component->GetSystemInstance();
	if (!SystemInstance)
	{
		TilesX = -1;
		TilesY = -1;
		TilesZ = -1;
		return false;
	}

	FGrid3DCollectionRWInstanceData_GameThread* Grid3DInstanceData = SystemInstancesToProxyData_GT.FindRef(SystemInstance->GetId());
	if (!Grid3DInstanceData)
	{
		TilesX = -1;
		TilesY = -1;
		TilesZ = -1;
		return false;
	}
	
	TilesX = Grid3DInstanceData->NumTiles.X;
	TilesY = Grid3DInstanceData->NumTiles.Y;
	TilesZ = Grid3DInstanceData->NumTiles.Z;

	// check dest size and type needs to be float
	// #todo(dmp): don't hardcode float since we might do other stuff in the future
	EPixelFormat RequiredTye = PF_R32_FLOAT;
	if (!Dest || Dest->GetSizeX() != NumCells.X * TilesX || Dest->GetSizeY() != NumCells.Y * TilesY || Dest->GetSizeZ() != NumCells.Z * TilesZ || Dest->GetPixelFormat() != RequiredTye)
	{
		return false;
	}

	FNiagaraDataInterfaceProxyGrid3DCollectionProxy* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyGrid3DCollectionProxy>();
	ENQUEUE_RENDER_COMMAND(FUpdateDIColorCurve)(
		[RT_Proxy, RT_InstanceID=SystemInstance->GetId(), RT_TextureResource=Dest->Resource](FRHICommandListImmediate& RHICmdList)
	{
		FGrid3DCollectionRWInstanceData_RenderThread* RT_Grid3DInstanceData = RT_Proxy->SystemInstancesToProxyData_RT.Find(RT_InstanceID);
	
		if (RT_TextureResource && RT_TextureResource->TextureRHI.IsValid() && RT_Grid3DInstanceData && RT_Grid3DInstanceData->CurrentData)
		{
			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, RT_Grid3DInstanceData->CurrentData->GridBuffer.Buffer);
			RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, RT_TextureResource->TextureRHI);

			FRHICopyTextureInfo CopyInfo;
			RHICmdList.CopyTexture(RT_Grid3DInstanceData->CurrentData->GridBuffer.Buffer, RT_TextureResource->TextureRHI, CopyInfo);

			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, RT_TextureResource->TextureRHI);
		}
		
	});
	
	return true;
	*/

	return false;
}

UFUNCTION(BlueprintCallable, Category = Niagara)
void UNiagaraDataInterfaceGrid3DCollection::GetRawTextureSize(const UNiagaraComponent *Component, int &SizeX, int &SizeY, int &SizeZ)
{
	if (!Component)
	{
		SizeX = -1;
		SizeY = -1;
		SizeZ = -1;
		return;
	}

	FNiagaraSystemInstance *SystemInstance = Component->GetSystemInstance();
	if (!SystemInstance)
	{
		SizeX = -1;
		SizeY = -1;
		SizeZ = -1;
		return;
	}
	FNiagaraSystemInstanceID InstanceID = SystemInstance->GetId();

	FGrid3DCollectionRWInstanceData_GameThread* Grid3DInstanceData = SystemInstancesToProxyData_GT.FindRef(InstanceID);
	if (!Grid3DInstanceData)
	{
		SizeX = -1;
		SizeY = -1;
		SizeZ = -1;
		return;
	}

	SizeX = Grid3DInstanceData->NumCells.X * Grid3DInstanceData->NumTiles.X;
	SizeY = Grid3DInstanceData->NumCells.Y * Grid3DInstanceData->NumTiles.Y;
	SizeZ = Grid3DInstanceData->NumCells.Z * Grid3DInstanceData->NumTiles.Z;
}

UFUNCTION(BlueprintCallable, Category = Niagara)
void UNiagaraDataInterfaceGrid3DCollection::GetTextureSize(const UNiagaraComponent *Component, int &SizeX, int &SizeY, int &SizeZ)
{
	if (!Component)
	{
		SizeX = -1;
		SizeY = -1;
		SizeZ = -1;
		return;
	}

	FNiagaraSystemInstance *SystemInstance = Component->GetSystemInstance();
	if (!SystemInstance)
	{
		SizeX = -1;
		SizeY = -1;
		SizeZ = -1;
		return;
	}
	FNiagaraSystemInstanceID InstanceID = SystemInstance->GetId();

	FGrid3DCollectionRWInstanceData_GameThread* Grid3DInstanceData = SystemInstancesToProxyData_GT.FindRef(InstanceID);
	if (!Grid3DInstanceData)
	{
		SizeX = -1;
		SizeY = -1;
		SizeZ = -1;
		return;
	}

	SizeX = Grid3DInstanceData->NumCells.X;
	SizeY = Grid3DInstanceData->NumCells.Y;
	SizeZ = Grid3DInstanceData->NumCells.Z;
}

void UNiagaraDataInterfaceGrid3DCollection::GetWorldBBoxSize(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FGrid3DCollectionRWInstanceData_GameThread> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutWorldBoundsX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutWorldBoundsY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutWorldBoundsZ(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutWorldBoundsX.GetDestAndAdvance() = InstData->WorldBBoxSize.X;
		*OutWorldBoundsY.GetDestAndAdvance() = InstData->WorldBBoxSize.Y;
		*OutWorldBoundsZ.GetDestAndAdvance() = InstData->WorldBBoxSize.Z;
	}
}

void UNiagaraDataInterfaceGrid3DCollection::SetNumCells(FVectorVMContext& Context)
{
	// This should only be called from a system or emitter script due to a need for only setting up initially.
	VectorVM::FUserPtrHandler<FGrid3DCollectionRWInstanceData_GameThread> InstData(Context);
	VectorVM::FExternalFuncInputHandler<int> InNumCellsX(Context);
	VectorVM::FExternalFuncInputHandler<int> InNumCellsY(Context);
	VectorVM::FExternalFuncInputHandler<int> InNumCellsZ(Context);
	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> OutSuccess(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		int NewNumCellsX = InNumCellsX.GetAndAdvance();
		int NewNumCellsY = InNumCellsY.GetAndAdvance();
		int NewNumCellsZ = InNumCellsZ.GetAndAdvance();

		bool bSuccess = (InstData.Get() != nullptr && Context.NumInstances == 1 && NumCells.X >= 0 && NumCells.Y >= 0 && NumCells.Z >= 0);
		*OutSuccess.GetDestAndAdvance() = bSuccess;
		if (bSuccess)
		{
			FIntVector OldNumCells = InstData->NumCells;


			InstData->NumCells.X = NewNumCellsX;
			InstData->NumCells.Y = NewNumCellsY;
			InstData->NumCells.Z = NewNumCellsZ;


			InstData->NeedsRealloc = OldNumCells != InstData->NumCells;
		}
	}
}

bool UNiagaraDataInterfaceGrid3DCollection::PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	FGrid3DCollectionRWInstanceData_GameThread* InstanceData = static_cast<FGrid3DCollectionRWInstanceData_GameThread*>(PerInstanceData);
	bool bNeedsReset = false;

	if (InstanceData->NeedsRealloc && InstanceData->NumCells.X > 0 && InstanceData->NumCells.Y > 0 && InstanceData->NumCells.Z > 0)
	{
		InstanceData->NeedsRealloc = false;

		InstanceData->CellSize = InstanceData->WorldBBoxSize / FVector(InstanceData->NumCells.X, InstanceData->NumCells.Y, InstanceData->NumCells.Z);

		FNiagaraDataInterfaceProxyGrid3DCollectionProxy* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyGrid3DCollectionProxy>();

		// #todo(dmp): we should align this method with the implementation in Grid2DCollection.  For now, we are relying on the next call to tick  to reset the User Texture
		
		// Push Updates to Proxy.		
		ENQUEUE_RENDER_COMMAND(FUpdateData)(
			[RT_Proxy, InstanceID = SystemInstance->GetId(), RT_InstanceData = *InstanceData](FRHICommandListImmediate& RHICmdList)
		{
			check(RT_Proxy->SystemInstancesToProxyData_RT.Contains(InstanceID));
			FGrid3DCollectionRWInstanceData_RenderThread* TargetData = RT_Proxy->SystemInstancesToProxyData_RT.Find(InstanceID);

			TargetData->NumCells = RT_InstanceData.NumCells;
			
			TargetData->CellSize = RT_InstanceData.CellSize;

			TargetData->Buffers.Empty();
			TargetData->CurrentData = nullptr;
			TargetData->DestinationData = nullptr;
		});

	}

	return false;
}

void UNiagaraDataInterfaceGrid3DCollection::GetCellSize(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FGrid3DCollectionRWInstanceData_GameThread> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutCellSizeX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutCellSizeY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutCellSizeZ(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{	
		*OutCellSizeX.GetDestAndAdvance() = InstData->CellSize.X;
		*OutCellSizeY.GetDestAndAdvance() = InstData->CellSize.Y;
		*OutCellSizeZ.GetDestAndAdvance() = InstData->CellSize.Z;
	}
}

bool FGrid3DCollectionRWInstanceData_GameThread::UpdateTargetTexture(ENiagaraGpuBufferFormat BufferFormat)
{
	// Pull value from user parameter
	if (UObject* UserParamObject = RTUserParamBinding.GetValue())
	{
		TargetTexture = Cast<UTextureRenderTargetVolume>(UserParamObject);
	
		if (TargetTexture == nullptr)
		{
			UE_LOG(LogNiagara, Error, TEXT("RenderTarget UserParam is a '%s' but is expected to be a UTextureRenderTargetVolume"), *GetNameSafe(UserParamObject->GetClass()));
		}
	}

	// Could be from user parameter of created internally
	if (TargetTexture != nullptr)
	{
		const FIntVector RTSize(NumCells.X * NumTiles.X, NumCells.Y * NumTiles.Y, NumCells.Z * NumTiles.Z);
		const EPixelFormat RenderTargetFormat = FNiagaraUtilities::BufferFormatToPixelFormat(BufferFormat);
		if (TargetTexture->SizeX != RTSize.X || TargetTexture->SizeY != RTSize.Y || TargetTexture->SizeZ != RTSize.Z || TargetTexture->OverrideFormat != RenderTargetFormat)
		{
			TargetTexture->OverrideFormat = RenderTargetFormat;
			TargetTexture->ClearColor = FLinearColor(0, 0, 0, 0);
			TargetTexture->InitAutoFormat(RTSize.X, RTSize.Y, RTSize.Z);
			TargetTexture->UpdateResourceImmediate(true);

			return true;
		}
	}

	return false;
}

void FGrid3DCollectionRWInstanceData_RenderThread::BeginSimulate(FRHICommandList& RHICmdList)
{
	for (TUniquePtr<FGrid3DBuffer>& Buffer : Buffers)
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
		DestinationData = new FGrid3DBuffer(NumCells.X * NumTiles.X, NumCells.Y * NumTiles.Y, NumCells.Z * NumTiles.Z, PixelFormat);
		RHICmdList.Transition(FRHITransitionInfo(DestinationData->GridBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVMask));
		Buffers.Emplace(DestinationData);
	}
}

void FGrid3DCollectionRWInstanceData_RenderThread::EndSimulate(FRHICommandList& RHICmdList)
{
	CurrentData = DestinationData;
	DestinationData = nullptr;
}

void FNiagaraDataInterfaceProxyGrid3DCollectionProxy::PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context)
{
	// #todo(dmp): Context doesnt need to specify if a stage is output or not since we moved pre/post stage to the DI itself.  Not sure which design is better for the future
	if (Context.IsOutputStage)
	{
		FGrid3DCollectionRWInstanceData_RenderThread* ProxyData = SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID);

		ProxyData->BeginSimulate(RHICmdList);

		// If we don't have an iteration stage, then we should manually clear the buffer to make sure there is no residual data.  If we are doing something like rasterizing particles into a grid, we want it to be clear before
		// we start.  If a user wants to access data from the previous stage, then they can read from the current data.

		// #todo(dmp): we might want to expose an option where we have buffers that are write only and need a clear (ie: no buffering like the neighbor grid).  They would be considered transient perhaps?  It'd be more
		// memory efficient since it would theoretically not require any double buffering.
		if (!Context.IsIterationStage)
		{
			check(ProxyData->DestinationData);
			RHICmdList.Transition(FRHITransitionInfo(ProxyData->DestinationData->GridBuffer.UAV, ERHIAccess::SRVMask, ERHIAccess::UAVCompute));
			RHICmdList.ClearUAVFloat(ProxyData->DestinationData->GridBuffer.UAV, FVector4(ForceInitToZero));
			RHICmdList.Transition(FRHITransitionInfo(ProxyData->DestinationData->GridBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));
		}
		else if (ProxyData->CurrentData != NULL && ProxyData->DestinationData != NULL)
		{
			// in iteration stages we copy the source to destination
			// FIXME: is this really needed? The Grid2D DI doesn't do it.
			FRHICopyTextureInfo CopyInfo;
			RHICmdList.CopyTexture(ProxyData->CurrentData->GridBuffer.Buffer, ProxyData->DestinationData->GridBuffer.Buffer, CopyInfo);
		}
	}
}

void FNiagaraDataInterfaceProxyGrid3DCollectionProxy::PostStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context)
{
	if (Context.IsOutputStage)
	{
		FGrid3DCollectionRWInstanceData_RenderThread* ProxyData = SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID);
		RHICmdList.Transition(FRHITransitionInfo(ProxyData->DestinationData->GridBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
		ProxyData->EndSimulate(RHICmdList);
	}
}

void FNiagaraDataInterfaceProxyGrid3DCollectionProxy::PostSimulate(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceArgs& Context)
{
	FGrid3DCollectionRWInstanceData_RenderThread* ProxyData = SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID);

	if (ProxyData->RenderTargetToCopyTo != nullptr && ProxyData->CurrentData != nullptr && ProxyData->CurrentData->GridBuffer.Buffer != nullptr)
	{
		FRHITexture* Source = ProxyData->CurrentData->GridBuffer.Buffer;
		FRHITexture* Destination = ProxyData->RenderTargetToCopyTo;
		FRHITransitionInfo TransitionsBefore[] = {
			FRHITransitionInfo(Source, ERHIAccess::SRVMask, ERHIAccess::CopySrc),
			FRHITransitionInfo(Destination, ERHIAccess::SRVMask, ERHIAccess::CopyDest)
		};

		FRHICopyTextureInfo CopyInfo;
		RHICmdList.CopyTexture(ProxyData->CurrentData->GridBuffer.Buffer, ProxyData->RenderTargetToCopyTo, CopyInfo);
		FRHITransitionInfo TransitionsAfter[] = {
			FRHITransitionInfo(Source, ERHIAccess::CopySrc, ERHIAccess::SRVMask),
			FRHITransitionInfo(Destination, ERHIAccess::CopyDest, ERHIAccess::SRVMask)
		};

	}

#if WITH_EDITOR
	if (ProxyData->bPreviewGrid && ProxyData->CurrentData)
	{
		if (FNiagaraGpuComputeDebug* GpuComputeDebug = Context.Batcher->GetGpuComputeDebug())
		{
			if (ProxyData->PreviewAttribute[0] != INDEX_NONE)
			{
				GpuComputeDebug->AddAttributeTexture(RHICmdList, Context.SystemInstanceID, SourceDIName, ProxyData->CurrentData->GridBuffer.Buffer, FIntPoint::ZeroValue, ProxyData->PreviewAttribute);
			}
			else
			{
				GpuComputeDebug->AddTexture(RHICmdList, Context.SystemInstanceID, SourceDIName, ProxyData->CurrentData->GridBuffer.Buffer);
			}
		}
	}
#endif
}

void FNiagaraDataInterfaceProxyGrid3DCollectionProxy::ResetData(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceArgs& Context)
{	
	FGrid3DCollectionRWInstanceData_RenderThread* ProxyData = SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID);
	if (!ProxyData)
	{
		return;
	}

	for (TUniquePtr<FGrid3DBuffer>& Buffer : ProxyData->Buffers)
	{
		if (Buffer.IsValid())
		{
			ERHIAccess AccessAfter;
			const bool bIsDestination = (ProxyData->DestinationData == Buffer.Get());
			if (bIsDestination)
			{
				// The destination buffer is already in UAVCompute because PreStage() runs first. It must stay in UAVCompute after the clear
				// because the shader is going to use it.
				AccessAfter = ERHIAccess::UAVCompute;
			}
			else
			{
				// The other buffers are in SRVMask and must be returned to that state after the clear.
				RHICmdList.Transition(FRHITransitionInfo(Buffer->GridBuffer.UAV, ERHIAccess::SRVMask, ERHIAccess::UAVCompute));
				AccessAfter = ERHIAccess::SRVMask;
			}

			RHICmdList.ClearUAVFloat(Buffer->GridBuffer.UAV, FVector4(ForceInitToZero));
			RHICmdList.Transition(FRHITransitionInfo(Buffer->GridBuffer.UAV, ERHIAccess::UAVCompute, AccessAfter));
		}		
	}	
}

FIntVector FNiagaraDataInterfaceProxyGrid3DCollectionProxy::GetElementCount(FNiagaraSystemInstanceID SystemInstanceID) const
{
	if ( const FGrid3DCollectionRWInstanceData_RenderThread* ProxyData = SystemInstancesToProxyData_RT.Find(SystemInstanceID) )
	{
		return ProxyData->NumCells;
	}
	return FIntVector::ZeroValue;
}

#undef LOCTEXT_NAMESPACE
