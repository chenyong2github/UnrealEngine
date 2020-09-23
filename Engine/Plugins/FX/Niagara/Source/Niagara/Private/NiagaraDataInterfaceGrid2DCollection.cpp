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
#if WITH_EDITOR
#include "NiagaraGpuComputeDebug.h"
#endif
#include "Engine/TextureRenderTarget2D.h"
#include "NiagaraConstants.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceGrid2DCollection"

const FString UNiagaraDataInterfaceGrid2DCollection::NumTilesName(TEXT("NumTiles_"));

const FString UNiagaraDataInterfaceGrid2DCollection::GridName(TEXT("Grid_"));
const FString UNiagaraDataInterfaceGrid2DCollection::OutputGridName(TEXT("OutputGrid_"));
const FString UNiagaraDataInterfaceGrid2DCollection::SamplerName(TEXT("Sampler_"));

const FName UNiagaraDataInterfaceGrid2DCollection::SetNumCellsFunctionName("SetNumCells");

// Global VM function names, also used by the shaders code generation methods.
const FName UNiagaraDataInterfaceGrid2DCollection::SetValueFunctionName("SetGridValue");
const FName UNiagaraDataInterfaceGrid2DCollection::GetValueFunctionName("GetGridValue");
const FName UNiagaraDataInterfaceGrid2DCollection::SetVector4ValueFunctionName("SetVector4Value");
const FName UNiagaraDataInterfaceGrid2DCollection::GetVector4ValueFunctionName("GetVector4Value");
const FName UNiagaraDataInterfaceGrid2DCollection::SampleGridVector4FunctionName("SampleGridVector4Value");
const FName UNiagaraDataInterfaceGrid2DCollection::SetVector3ValueFunctionName("SetVector3Value");
const FName UNiagaraDataInterfaceGrid2DCollection::GetVector3ValueFunctionName("GetVector3Value");
const FName UNiagaraDataInterfaceGrid2DCollection::SampleGridVector3FunctionName("SampleGridVector3Value");
const FName UNiagaraDataInterfaceGrid2DCollection::SetVector2ValueFunctionName("SetVector2Value");
const FName UNiagaraDataInterfaceGrid2DCollection::GetVector2ValueFunctionName("GetVector2Value");
const FName UNiagaraDataInterfaceGrid2DCollection::SampleGridVector2FunctionName("SampleGridVector2Value");
const FName UNiagaraDataInterfaceGrid2DCollection::SetFloatValueFunctionName("SetFloatValue");
const FName UNiagaraDataInterfaceGrid2DCollection::GetFloatValueFunctionName("GetFloatValue");
const FName UNiagaraDataInterfaceGrid2DCollection::SampleGridFloatFunctionName("SampleGridFloatValue");

const FName UNiagaraDataInterfaceGrid2DCollection::GetVector4AttributeIndexFunctionName("GetVector4AttributeIndex");
const FName UNiagaraDataInterfaceGrid2DCollection::GetVector3AttributeIndexFunctionName("GetVectorAttributeIndex");
const FName UNiagaraDataInterfaceGrid2DCollection::GetVector2AttributeIndexFunctionName("GetVector2DAttributeIndex");
const FName UNiagaraDataInterfaceGrid2DCollection::GetFloatAttributeIndexFunctionName("GetFloatAttributeIndex");

const FString UNiagaraDataInterfaceGrid2DCollection::AnonymousAttributeString("Attribute At Index");

const FName UNiagaraDataInterfaceGrid2DCollection::ClearCellFunctionName("ClearCell");
const FName UNiagaraDataInterfaceGrid2DCollection::CopyPreviousToCurrentForCellFunctionName("CopyPreviousToCurrentForCell");

const FString UNiagaraDataInterfaceGrid2DCollection::AttributeIndicesBaseName(TEXT("AttributeIndices_"));
const TCHAR* UNiagaraDataInterfaceGrid2DCollection::VectorComponentNames[] = { TEXT(".x"), TEXT(".y"), TEXT(".z"), TEXT(".w") };

const FName UNiagaraDataInterfaceGrid2DCollection::SampleGridFunctionName("SampleGrid");

FNiagaraVariableBase UNiagaraDataInterfaceGrid2DCollection::ExposedRTVar;

bool UNiagaraDataInterfaceGrid2DCollection::CanCreateVarFromFuncName(const FName& FuncName)
{
	if (FuncName == UNiagaraDataInterfaceGrid2DCollection::SetVector4ValueFunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::GetVector4ValueFunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::SampleGridVector4FunctionName )
		return true;
	else if (FuncName == UNiagaraDataInterfaceGrid2DCollection::SetVector3ValueFunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::GetVector3ValueFunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::SampleGridVector3FunctionName)
		return true;
	else if (FuncName == UNiagaraDataInterfaceGrid2DCollection::SetVector2ValueFunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::GetVector2ValueFunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::SampleGridVector2FunctionName )
		return true;
	else if (FuncName == UNiagaraDataInterfaceGrid2DCollection::SetFloatValueFunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::GetFloatValueFunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::SampleGridFloatFunctionName )
		return true;
	return false;
}

FNiagaraTypeDefinition UNiagaraDataInterfaceGrid2DCollection::GetValueTypeFromFuncName(const FName& FuncName)
{

	if (FuncName == UNiagaraDataInterfaceGrid2DCollection::SetVector4ValueFunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::GetVector4ValueFunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::SampleGridVector4FunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::GetVector4AttributeIndexFunctionName)
		return FNiagaraTypeDefinition::GetVec4Def();
	else if (FuncName == UNiagaraDataInterfaceGrid2DCollection::SetVector3ValueFunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::GetVector3ValueFunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::SampleGridVector3FunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::GetVector3AttributeIndexFunctionName)
		return FNiagaraTypeDefinition::GetVec3Def();
	else if (FuncName == UNiagaraDataInterfaceGrid2DCollection::SetVector2ValueFunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::GetVector2ValueFunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::SampleGridVector2FunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::GetVector2AttributeIndexFunctionName)
		return FNiagaraTypeDefinition::GetVec2Def();
	else if (FuncName == UNiagaraDataInterfaceGrid2DCollection::SetFloatValueFunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::GetFloatValueFunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::SampleGridFloatFunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::GetFloatAttributeIndexFunctionName)
		return FNiagaraTypeDefinition::GetFloatDef();
	return FNiagaraTypeDefinition();
}

int32 UNiagaraDataInterfaceGrid2DCollection::GetComponentCountFromFuncName(const FName& FuncName)
{

	if (FuncName == UNiagaraDataInterfaceGrid2DCollection::SetVector4ValueFunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::GetVector4ValueFunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::SampleGridVector4FunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::GetVector4AttributeIndexFunctionName)
		return 4;
	else if (FuncName == UNiagaraDataInterfaceGrid2DCollection::SetVector3ValueFunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::GetVector3ValueFunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::SampleGridVector3FunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::GetVector3AttributeIndexFunctionName)
		return 3;
	else if (FuncName == UNiagaraDataInterfaceGrid2DCollection::SetVector2ValueFunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::GetVector2ValueFunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::SampleGridVector2FunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::GetVector2AttributeIndexFunctionName)
		return 2;
	else if (FuncName == UNiagaraDataInterfaceGrid2DCollection::SetFloatValueFunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::GetFloatValueFunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::SampleGridFloatFunctionName || FuncName == UNiagaraDataInterfaceGrid2DCollection::GetFloatAttributeIndexFunctionName)
		return 1;
	return INDEX_NONE;
}

static float GNiagaraGrid2DResolutionMultiplier = 1.0f;
static FAutoConsoleVariableRef CVarNiagaraGrid2DResolutionMultiplier(
	TEXT("fx.Niagara.Grid2D.ResolutionMultiplier"),
	GNiagaraGrid2DResolutionMultiplier,
	TEXT("Optional global modifier to grid resolution\n"),
	ECVF_Default
);

static int32 GNiagaraGrid2DOverrideFormat = -1;
static FAutoConsoleVariableRef CVarNiagaraGrid2DOverrideFormat(
	TEXT("fx.Niagara.Grid2D.OverrideFormat"),
	GNiagaraGrid2DOverrideFormat,
	TEXT("Optional override for all grids to use this format.\n"),
	ECVF_Default
);

/*--------------------------------------------------------------------------------------------------------------------------*/
struct FNiagaraDataInterfaceParametersCS_Grid2DCollection : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_Grid2DCollection, NonVirtual);

public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{			
		NumCellsParam.Bind(ParameterMap, *(NumCellsName + ParameterInfo.DataInterfaceHLSLSymbol));
		NumTilesParam.Bind(ParameterMap, *(UNiagaraDataInterfaceGrid2DCollection::NumTilesName + ParameterInfo.DataInterfaceHLSLSymbol));

		CellSizeParam.Bind(ParameterMap, *(CellSizeName + ParameterInfo.DataInterfaceHLSLSymbol));

		WorldBBoxSizeParam.Bind(ParameterMap, *(WorldBBoxSizeName + ParameterInfo.DataInterfaceHLSLSymbol));

		GridParam.Bind(ParameterMap, *(UNiagaraDataInterfaceGrid2DCollection::GridName + ParameterInfo.DataInterfaceHLSLSymbol));
		OutputGridParam.Bind(ParameterMap, *(UNiagaraDataInterfaceGrid2DCollection::OutputGridName + ParameterInfo.DataInterfaceHLSLSymbol));

		SamplerParam.Bind(ParameterMap, *(UNiagaraDataInterfaceGrid2DCollection::SamplerName + ParameterInfo.DataInterfaceHLSLSymbol));
		AttributeIndicesParam.Bind(ParameterMap, *(UNiagaraDataInterfaceGrid2DCollection::AttributeIndicesBaseName + ParameterInfo.DataInterfaceHLSLSymbol));

		// Gather up all the attribute names referenced. Note that there may be multiple in the list of the same name, 
		// but we only deal with this by the number of bound methods.
		{
			int32 NumFuncs = ParameterInfo.GeneratedFunctions.Num();

			for (int32 FuncIdx = 0; FuncIdx < NumFuncs; ++FuncIdx)
			{
				const FNiagaraDataInterfaceGeneratedFunction& Func = ParameterInfo.GeneratedFunctions[FuncIdx];
				static const FName NAME_Attribute("Attribute");
				const FName* AttributeName = Func.FindSpecifierValue(NAME_Attribute);
				if (AttributeName != nullptr)
				{
					int32 ComponentCount = UNiagaraDataInterfaceGrid2DCollection::GetComponentCountFromFuncName(Func.DefinitionName);
					AttributeNames.Add(*AttributeName);
					AttributeChannelCount.Add(ComponentCount);
				}
				else
				{
					AttributeNames.Add(FName());
					AttributeChannelCount.Add(INDEX_NONE);
				}
			}
		}
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		// Get shader and DI
		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
		FNiagaraDataInterfaceProxyGrid2DCollectionProxy* VFDI = static_cast<FNiagaraDataInterfaceProxyGrid2DCollectionProxy*>(Context.DataInterface);
		
		FGrid2DCollectionRWInstanceData_RenderThread* ProxyData = VFDI->SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID);
		check(ProxyData);

		int NumCellsTmp[2];
		NumCellsTmp[0] = ProxyData->NumCells.X;
		NumCellsTmp[1] = ProxyData->NumCells.Y;
		SetShaderValue(RHICmdList, ComputeShaderRHI, NumCellsParam, NumCellsTmp);	

		int NumTilesTmp[2];
		NumTilesTmp[0] = ProxyData->NumTiles.X;
		NumTilesTmp[1] = ProxyData->NumTiles.Y;
		SetShaderValue(RHICmdList, ComputeShaderRHI, NumTilesParam, NumTilesTmp);		

		if (ProxyData->AttributeIndices.Num() == 0 && AttributeNames.Num() > 0)
		{
			int NumAttrIndices = Align(AttributeNames.Num(), 4);
			ProxyData->AttributeIndices.SetNumZeroed(NumAttrIndices);

			// TODO handle mismatched types!
			for (int32 i = 0; i < AttributeNames.Num(); i++)
			{
				int32 FoundIdx = ProxyData->Vars.Find(AttributeNames[i]);
				check(AttributeNames.Num() == AttributeChannelCount.Num());
				check(ProxyData->Offsets.Num() == ProxyData->VarComponents.Num());
				check(ProxyData->Offsets.Num() == ProxyData->Vars.Num());
				if (ProxyData->Offsets.IsValidIndex(FoundIdx) && AttributeChannelCount[i] == ProxyData->VarComponents[FoundIdx])
				{
					ProxyData->AttributeIndices[i] = ProxyData->Offsets[FoundIdx];
				}
				else
				{
					ProxyData->AttributeIndices[i] = -1; // We may need to protect against this in the hlsl as this might underflow an array lookup if used incorrectly.
				}
			}
		}

		SetShaderValue(RHICmdList, ComputeShaderRHI, CellSizeParam, ProxyData->CellSize);		
				
		SetShaderValue(RHICmdList, ComputeShaderRHI, WorldBBoxSizeParam, ProxyData->WorldBBoxSize);

		SetShaderValueArray(RHICmdList, ComputeShaderRHI, AttributeIndicesParam, ProxyData->AttributeIndices.GetData(), ProxyData->AttributeIndices.Num());
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
			SetSRVParameter(RHICmdList, Context.Shader.GetComputeShader(), GridParam, InputGridBuffer);
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
	LAYOUT_FIELD(FShaderParameter, AttributeIndicesParam);
	
	LAYOUT_FIELD(FShaderResourceParameter, SamplerParam);
	LAYOUT_FIELD(TMemoryImageArray<FName>, AttributeNames);
	LAYOUT_FIELD(TMemoryImageArray<uint32>, AttributeChannelCount);
};

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_Grid2DCollection);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceGrid2DCollection, FNiagaraDataInterfaceParametersCS_Grid2DCollection);


UNiagaraDataInterfaceGrid2DCollection::UNiagaraDataInterfaceGrid2DCollection(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyGrid2DCollectionProxy());

	FNiagaraTypeDefinition Def(UTextureRenderTarget::StaticClass());
	RenderTargetUserParameter.Parameter.SetType(Def);
}


void UNiagaraDataInterfaceGrid2DCollection::PostInitProperties()
{
	Super::PostInitProperties();

	//Can we register data interfaces as regular types and fold them into the FNiagaraVariable framework for UI and function calls etc?
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), /*bCanBeParameter*/ true, /*bCanBePayload*/ false, /*bIsUserDefined*/ false);
		UNiagaraDataInterfaceGrid2DCollection::ExposedRTVar = FNiagaraVariableBase(FNiagaraTypeDefinition(UTexture::StaticClass()), TEXT("RenderTarget"));
	}
}

void UNiagaraDataInterfaceGrid2DCollection::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	Super::GetFunctions(OutFunctions);

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SetNumCellsFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsY")));
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
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("AttributeIndex")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Value")));

		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = NSLOCTEXT("Niagara", "NiagaraDataInterfaceGridColl2D_GetValueFunction", "Get the value at a specific index. Note that this is an older way of working with Grids. Consider using the SetFloat or other typed, named functions or parameter map variables with StageContext namespace instead.");
#endif

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
#if WITH_EDITORONLY_DATA
		Sig.Description = NSLOCTEXT("Niagara", "NiagaraDataInterfaceGridColl2D_SetValueFunction", "Set the value at a specific index. Note that this is an older way of working with Grids. Consider using the SetFloat or other typed, named functions or parameter map variables with StageContext namespace instead.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ClearCellFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));

		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bWriteFunction = true;
		Sig.bRequiresExecPin = true;
#if WITH_EDITORONLY_DATA
		Sig.Description = NSLOCTEXT("Niagara", "NiagaraDataInterfaceGridColl2D_ClearCellFunction", "Set all attributes for a given cell to be zeroes.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = CopyPreviousToCurrentForCellFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));

		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bWriteFunction = true;
		Sig.bRequiresExecPin = true;
#if WITH_EDITORONLY_DATA
		Sig.Description = NSLOCTEXT("Niagara", "NiagaraDataInterfaceGridColl2D_CopyPreviousToCurrentForCell", "Take the previous contents of the cell and copy to the output location for the cell.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SetVector4ValueFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("Value")));
		Sig.FunctionSpecifiers.Add(FName("Attribute"));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
		Sig.bRequiresExecPin = true;
		Sig.bWriteFunction = true;
#if WITH_EDITORONLY_DATA
		Sig.Description = NSLOCTEXT("Niagara", "NiagaraDataInterfaceGridColl2D_SetVector4", "Sets a Vector4 value on the Grid by Attribute name.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetVector4ValueFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("Value")));
		Sig.FunctionSpecifiers.Add(FName("Attribute"));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
#if WITH_EDITORONLY_DATA
		Sig.Description = NSLOCTEXT("Niagara", "NiagaraDataInterfaceGridColl2D_GetVector4", "Gets a Vector4 value on the Grid by Attribute name.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleGridVector4FunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Unit")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("Value")));
		Sig.FunctionSpecifiers.Add(FName("Attribute"));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
#if WITH_EDITORONLY_DATA
		Sig.Description = NSLOCTEXT("Niagara", "NiagaraDataInterfaceGridColl2D_SampleVector4", "Sample a Vector4 value on the Grid by Attribute name.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SetVector3ValueFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Value")));
		Sig.FunctionSpecifiers.Add(FName("Attribute"));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
		Sig.bRequiresExecPin = true;
		Sig.bWriteFunction = true;
#if WITH_EDITORONLY_DATA
		Sig.Description = NSLOCTEXT("Niagara", "NiagaraDataInterfaceGridColl2D_SetVector3", "Sets a Vector3 value on the Grid by Attribute name.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetVector3ValueFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Value")));
		Sig.FunctionSpecifiers.Add(FName("Attribute"));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
#if WITH_EDITORONLY_DATA
		Sig.Description = NSLOCTEXT("Niagara", "NiagaraDataInterfaceGridColl2D_GetVector3", "Gets a Vector3 value on the Grid by Attribute name.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleGridVector3FunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Unit")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Value")));
		Sig.FunctionSpecifiers.Add(FName("Attribute"));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
#if WITH_EDITORONLY_DATA
		Sig.Description = NSLOCTEXT("Niagara", "NiagaraDataInterfaceGridColl2D_SampleVector3", "Sample a Vector3 value on the Grid by Attribute name.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SetVector2ValueFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Value")));
		Sig.FunctionSpecifiers.Add(FName("Attribute"));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
		Sig.bRequiresExecPin = true;
		Sig.bWriteFunction = true;
#if WITH_EDITORONLY_DATA
		Sig.Description = NSLOCTEXT("Niagara", "NiagaraDataInterfaceGridColl2D_SetVector2", "Sets a Vector2 value on the Grid by Attribute name.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetVector2ValueFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Value")));
		Sig.FunctionSpecifiers.Add(FName("Attribute"));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
#if WITH_EDITORONLY_DATA
		Sig.Description = NSLOCTEXT("Niagara", "NiagaraDataInterfaceGridColl2D_GetVector2", "Gets a Vector2 value on the Grid by Attribute name.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleGridVector2FunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Unit")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Value")));
		Sig.FunctionSpecifiers.Add(FName("Attribute"));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
#if WITH_EDITORONLY_DATA
		Sig.Description = NSLOCTEXT("Niagara", "NiagaraDataInterfaceGridColl2D_SampleVector2", "Sample a Vector2 value on the Grid by Attribute name.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SetFloatValueFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Value")));
		Sig.FunctionSpecifiers.Add(FName("Attribute"));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
		Sig.bRequiresExecPin = true;
		Sig.bWriteFunction = true;
#if WITH_EDITORONLY_DATA
		Sig.Description = NSLOCTEXT("Niagara", "NiagaraDataInterfaceGridColl2D_SetFloat", "Sets a float value on the Grid by Attribute name.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetFloatValueFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Value")));
		Sig.FunctionSpecifiers.Add(FName("Attribute"));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
#if WITH_EDITORONLY_DATA
		Sig.Description = NSLOCTEXT("Niagara", "NiagaraDataInterfaceGridColl2D_GetFloat", "Gets a float value on the Grid by Attribute name.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleGridFloatFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Unit")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Value")));
		Sig.FunctionSpecifiers.Add(FName("Attribute"));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
#if WITH_EDITORONLY_DATA
		Sig.Description = NSLOCTEXT("Niagara", "NiagaraDataInterfaceGridColl2D_SampleFloat", "Sample a float value on the Grid by Attribute name.");
#endif
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
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetVector4AttributeIndexFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("AttributeIndex")));
		Sig.FunctionSpecifiers.Add(FName("Attribute"));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
#if WITH_EDITORONLY_DATA
		Sig.Description = NSLOCTEXT("Niagara", "NiagaraDataInterfaceGridColl2D_GetVector4AttributeIndex", "Gets a attribute starting index value for Vector4 on the Grid by Attribute name. Returns -1 if not found.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetVector3AttributeIndexFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("AttributeIndex")));
		Sig.FunctionSpecifiers.Add(FName("Attribute"));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
#if WITH_EDITORONLY_DATA
		Sig.Description = NSLOCTEXT("Niagara", "NiagaraDataInterfaceGridColl2D_GetVector3AttributeIndex", "Gets a attribute starting index value for Vector3 on the Grid by Attribute name. Returns -1 if not found.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetVector2AttributeIndexFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("AttributeIndex")));
		Sig.FunctionSpecifiers.Add(FName("Attribute"));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
#if WITH_EDITORONLY_DATA
		Sig.Description = NSLOCTEXT("Niagara", "NiagaraDataInterfaceGridColl2D_GetVector2AttributeIndex", "Gets a attribute starting index value for Vector2 on the Grid by Attribute name. Returns -1 if not found.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetFloatAttributeIndexFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("AttributeIndex")));
		Sig.FunctionSpecifiers.Add(FName("Attribute"));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
#if WITH_EDITORONLY_DATA
		Sig.Description = NSLOCTEXT("Niagara", "NiagaraDataInterfaceGridColl2D_GetFloatAttributeIndex", "Gets a attribute starting index value for float on the Grid by Attribute name. Returns -1 if not found.");
#endif
		OutFunctions.Add(Sig);
	}
}

// #todo(dmp): expose more CPU functionality
// #todo(dmp): ideally these would be exposed on the parent class, but we can't bind functions of parent classes but need to work on the interface
// for sharing an instance data object with the super class
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceGrid2DCollection, GetWorldBBoxSize);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceGrid2DCollection, GetCellSize);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceGrid2DCollection, GetNumCells);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceGrid2DCollection, SetNumCells);
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceGrid2DCollection, GetAttributeIndex);
void UNiagaraDataInterfaceGrid2DCollection::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	Super::GetVMExternalFunction(BindingInfo, InstanceData, OutFunc);

	static const FName NAME_Attribute("Attribute");

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
	else if (BindingInfo.Name == NumCellsFunctionName)
	{
		// #todo(dmp): this will override the base class definition for GetCellSize because the data interface instance data computes cell size
		// it would be nice to refactor this so it can be part of the super class
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 2);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceGrid2DCollection, GetNumCells)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SetNumCellsFunctionName)
	{
		check(BindingInfo.GetNumInputs() == 3 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceGrid2DCollection, SetNumCells)::Bind(this, OutFunc);
	}
	//else if (BindingInfo.Name == GetValueFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == SetValueFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == CopyPreviousToCurrentForCellFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == ClearCellFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == SetVector4ValueFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == GetVector4ValueFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == SampleGridVector4FunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == SetVector3ValueFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == GetVector3ValueFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == SampleGridVector3FunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == SetVector2ValueFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == GetVector2ValueFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == SampleGridVector2FunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == SetFloatValueFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == GetFloatValueFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == SampleGridFloatFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == SampleGridFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == GetVector4AttributeIndexFunctionName)
	{
		FName AttributeName = BindingInfo.FindSpecifier(NAME_Attribute)->Value;
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceGrid2DCollection, GetAttributeIndex)::Bind(this, OutFunc, AttributeName, 4);
	}
	else if (BindingInfo.Name == GetVector3AttributeIndexFunctionName)
	{
		FName AttributeName = BindingInfo.FindSpecifier(NAME_Attribute)->Value;
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceGrid2DCollection, GetAttributeIndex)::Bind(this, OutFunc, AttributeName, 3);
	}
	else if (BindingInfo.Name == GetVector2AttributeIndexFunctionName)
	{
		FName AttributeName = BindingInfo.FindSpecifier(NAME_Attribute)->Value;
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceGrid2DCollection, GetAttributeIndex)::Bind(this, OutFunc, AttributeName, 2);
	}
	else if (BindingInfo.Name == GetFloatAttributeIndexFunctionName)
	{
		FName AttributeName = BindingInfo.FindSpecifier(NAME_Attribute)->Value;
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceGrid2DCollection, GetAttributeIndex)::Bind(this, OutFunc, AttributeName, 1);
	}
}

bool UNiagaraDataInterfaceGrid2DCollection::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceGrid2DCollection* OtherTyped = CastChecked<const UNiagaraDataInterfaceGrid2DCollection>(Other);

	return OtherTyped != nullptr &&
#if WITH_EDITOR
		OtherTyped->bPreviewGrid == bPreviewGrid &&
		OtherTyped->PreviewAttribute == PreviewAttribute &&
#endif
		OtherTyped->RenderTargetUserParameter == RenderTargetUserParameter &&
		OtherTyped->bCreateRenderTarget == bCreateRenderTarget &&
		OtherTyped->BufferFormat == BufferFormat;
}

void UNiagaraDataInterfaceGrid2DCollection::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	Super::GetParameterDefinitionHLSL(ParamInfo, OutHLSL);

	static const TCHAR *FormatDeclarations = TEXT(R"(				
		Texture2D<float> {GridName};
		RWTexture2D<float> RW{OutputGridName};
		int2 {NumTiles};
		SamplerState {SamplerName};
		int4 {AttributeIndicesName}[{AttributeInt4Count}];
	
	)");

	// If we use an int array for the attribute indices, the shader compiler will actually use int4 due to the packing rules,
	// and leave 3 elements unused. Besides being wasteful, this means that the array we send to the CS would need to be padded,
	// which is a hassle. Instead, use int4 explicitly, and access individual components in the generated code.
	// Note that we have to have at least one here because hlsl doesn't support arrays of size 0.
	const int AttributeInt4Count = FMath::Max(1, FMath::DivideAndRoundUp(ParamInfo.GeneratedFunctions.Num(), 4));

	TMap<FString, FStringFormatArg> ArgsDeclarations = {				
		{ TEXT("GridName"),    GridName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("SamplerName"),    SamplerName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("OutputGridName"),    OutputGridName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("NumTiles"),    NumTilesName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("AttributeIndicesName"), AttributeIndicesBaseName + ParamInfo.DataInterfaceHLSLSymbol},
		{ TEXT("AttributeInt4Count"), AttributeInt4Count},
	};
	OutHLSL += FString::Format(FormatDeclarations, ArgsDeclarations);
}

void UNiagaraDataInterfaceGrid2DCollection::WriteSetHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, int32 InNumChannels, FString& OutHLSL)
{
	
	FString FormatBounds = TEXT(R"(
			void {FunctionName}(int In_IndexX, int In_IndexY, float{NumChannelsVariableSuffix} In_Value)
			{			
				int In_AttributeIndex = {AttributeIndicesName}[{AttributeIndexGroup}]{AttributeIndexComponent};

			    for (int i = 0; i < {NumChannels}; i++)
				{
					int TileIndexX = (In_AttributeIndex + i) % {NumTiles}.x;
					int TileIndexY = (In_AttributeIndex + i) / {NumTiles}.x;
				
					float Val;
				)");
	if (InNumChannels == 1)
	{
		FormatBounds += TEXT("					Val = In_Value;\n");
	}
	else if (InNumChannels > 1)
	{
		FormatBounds += TEXT(R"(
					switch(i)
					{
						case 0:
							Val = In_Value.x;
							break; 
						case 1:
							Val = In_Value.y;
							break; )");
	}

	if (InNumChannels > 2)
	{
		FormatBounds += TEXT(R"(
						case 2:
							Val = In_Value.z;
							break; )");
	}
	if (InNumChannels > 3)
	{
		FormatBounds += TEXT(R"(
						case 3:
							Val = In_Value.w;
							break; )");
	}
	if (InNumChannels > 1)
	{
		FormatBounds += TEXT(R"(	
					})");
	}
	FormatBounds += TEXT(R"(	
					RW{OutputGrid}[int2(In_IndexX + TileIndexX * {NumCellsName}.x, In_IndexY + TileIndexY * {NumCellsName}.y)] = Val;
				}
			}
		)");
	TMap<FString, FStringFormatArg> ArgsBounds = {
		{TEXT("FunctionName"), FunctionInfo.InstanceName},
		{TEXT("OutputGrid"), OutputGridName + ParamInfo.DataInterfaceHLSLSymbol},
		{TEXT("NumCellsName"), NumCellsName + ParamInfo.DataInterfaceHLSLSymbol},
		{TEXT("NumTiles"),    NumTilesName + ParamInfo.DataInterfaceHLSLSymbol},
		{TEXT("AttributeIndicesName"), AttributeIndicesBaseName + ParamInfo.DataInterfaceHLSLSymbol},
		{TEXT("AttributeIndexGroup"), FunctionInstanceIndex / 4},
		{TEXT("AttributeIndexComponent"), VectorComponentNames[FunctionInstanceIndex % 4]},
		{TEXT("NumChannels"), FString::FromInt(InNumChannels)},
		{TEXT("NumChannelsVariableSuffix"), InNumChannels > 1 ? FString::FromInt(InNumChannels) : TEXT("")},

	};
	OutHLSL += FString::Format(*FormatBounds, ArgsBounds);
}

void UNiagaraDataInterfaceGrid2DCollection::WriteGetHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, int32 InNumChannels, FString& OutHLSL)
{
	
	FString FormatBounds = TEXT(R"(
			void {FunctionName}(int In_IndexX, int In_IndexY, out float{NumChannelsVariableSuffix} Out_Val)
			{
				int In_AttributeIndex = {AttributeIndicesName}[{AttributeIndexGroup}]{AttributeIndexComponent};

			    for (int i = 0; i < {NumChannels}; i++)
				{
					int TileIndexX = (In_AttributeIndex + i) % {NumTiles}.x;
					int TileIndexY = (In_AttributeIndex + i) / {NumTiles}.x;

					float Val = {Grid}.Load(int3(In_IndexX + TileIndexX * {NumCellsName}.x, In_IndexY + TileIndexY * {NumCellsName}.y, 0));
					)");
	if (InNumChannels == 1)
	{
		FormatBounds += TEXT("					Out_Val = Val;\n");
	}
	else if (InNumChannels > 1)
	{
		FormatBounds += TEXT(R"(
					switch(i)
					{
						case 0:
							Out_Val.x = Val;
							break; 
						case 1:
							Out_Val.y = Val;
							break; )");
	}

	if (InNumChannels > 2)
	{
		FormatBounds += TEXT(R"(
						case 2:
							Out_Val.z = Val;
							break; )");
	}
	if (InNumChannels > 3)
	{
		FormatBounds += TEXT(R"(
						case 3:
							Out_Val.w = Val;
							break; )");
	}
	if (InNumChannels > 1)
	{
		FormatBounds += TEXT(R"(	
					})");
	}
	FormatBounds += TEXT(R"(	
				}
			}
		)");
	TMap<FString, FStringFormatArg> ArgsBounds = {
		{TEXT("FunctionName"), FunctionInfo.InstanceName},
		{TEXT("OutputGrid"), OutputGridName + ParamInfo.DataInterfaceHLSLSymbol},
		{TEXT("Grid"), GridName + ParamInfo.DataInterfaceHLSLSymbol},
		{TEXT("NumCellsName"), NumCellsName + ParamInfo.DataInterfaceHLSLSymbol},
		{TEXT("NumTiles"),    NumTilesName + ParamInfo.DataInterfaceHLSLSymbol},
		{TEXT("AttributeIndicesName"), AttributeIndicesBaseName + ParamInfo.DataInterfaceHLSLSymbol},
		{TEXT("AttributeIndexGroup"), FunctionInstanceIndex / 4},
		{TEXT("AttributeIndexComponent"), VectorComponentNames[FunctionInstanceIndex % 4]},
		{TEXT("NumChannels"), FString::FromInt(InNumChannels)},
		{TEXT("NumChannelsVariableSuffix"), InNumChannels > 1 ? FString::FromInt(InNumChannels) : TEXT("")},

	};
	OutHLSL += FString::Format(*FormatBounds, ArgsBounds);
}

void UNiagaraDataInterfaceGrid2DCollection::WriteSampleHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, int32 InNumChannels, FString& OutHLSL)
{
	FString FormatBounds = TEXT(R"(
			void {FunctionName}(float2 In_Unit, out float{NumChannelsVariableSuffix} Out_Val)
			{
				int In_AttributeIndex = {AttributeIndicesName}[{AttributeIndexGroup}]{AttributeIndexComponent};

			    for (int i = 0; i < {NumChannels}; i++)
				{
					int TileIndexX = (In_AttributeIndex + i) % {NumTiles}.x;
					int TileIndexY = (In_AttributeIndex + i) / {NumTiles}.x;
					float2 UV =
					{
						In_Unit.x / {NumTiles}.x + 1.0*TileIndexX/{NumTiles}.x,
						In_Unit.y / {NumTiles}.y + 1.0*TileIndexY/{NumTiles}.y
					};
					float2 TileMin =
					{
						(TileIndexX * {NumCellsName}.x + 0.5) / ({NumTiles}.x * {NumCellsName}.x),
						(TileIndexY * {NumCellsName}.y + 0.5) / ({NumTiles}.y * {NumCellsName}.y),
					};
					float2 TileMax =
					{
						((TileIndexX + 1) * {NumCellsName}.x - 0.5) / ({NumTiles}.x * {NumCellsName}.x),
						((TileIndexY + 1) * {NumCellsName}.y - 0.5) / ({NumTiles}.y * {NumCellsName}.y),
					};
					UV = clamp(UV, TileMin, TileMax);
				
					float Val = {Grid}.SampleLevel({SamplerName}, UV, 0);
					)");
	if (InNumChannels == 1)
	{
		FormatBounds += TEXT("					Out_Val = Val;\n");
	}
	else if (InNumChannels > 1)
	{
		FormatBounds += TEXT(R"(
					switch(i)
					{
						case 0:
							Out_Val.x = Val;
							break; 
						case 1:
							Out_Val.y = Val;
							break; )");
}

	if (InNumChannels > 2)
	{
		FormatBounds += TEXT(R"(
						case 2:
							Out_Val.z = Val;
							break; )");
	}
	if (InNumChannels > 3)
	{
		FormatBounds += TEXT(R"(
						case 3:
							Out_Val.w = Val;
							break; )");
	}
	if (InNumChannels > 1)
	{
		FormatBounds += TEXT(R"(	
					})");
	}
	FormatBounds += TEXT(R"(	
				}
			}
		)");

	TMap<FString, FStringFormatArg> ArgsBounds = {
		{TEXT("FunctionName"), FunctionInfo.InstanceName},
		{TEXT("Grid"), GridName + ParamInfo.DataInterfaceHLSLSymbol},
		{TEXT("SamplerName"),    SamplerName + ParamInfo.DataInterfaceHLSLSymbol },
		{TEXT("NumTiles"),    NumTilesName + ParamInfo.DataInterfaceHLSLSymbol},
		{TEXT("NumCellsName"), NumCellsName + ParamInfo.DataInterfaceHLSLSymbol},
		{TEXT("NumChannels"), FString::FromInt(InNumChannels)},
		{TEXT("NumChannelsVariableSuffix"), InNumChannels > 1 ? FString::FromInt(InNumChannels) : TEXT("")},
		{TEXT("AttributeIndicesName"), AttributeIndicesBaseName + ParamInfo.DataInterfaceHLSLSymbol},
		{TEXT("AttributeIndexGroup"), FunctionInstanceIndex / 4},
		{TEXT("AttributeIndexComponent"), VectorComponentNames[FunctionInstanceIndex % 4]},
	};
	OutHLSL += FString::Format(*FormatBounds, ArgsBounds);
}

void UNiagaraDataInterfaceGrid2DCollection::WriteAttributeGetIndexHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, int32 InNumChannels, FString& OutHLSL)
{
	FString FormatBounds = TEXT(R"(
			void {FunctionName}(out int Out_Val)
			{
				int In_AttributeIndex = {AttributeIndicesName}[{AttributeIndexGroup}]{AttributeIndexComponent};
				Out_Val = In_AttributeIndex;
			}
	)");

	
	TMap<FString, FStringFormatArg> ArgsBounds = {
		{TEXT("FunctionName"), FunctionInfo.InstanceName},
		{TEXT("AttributeIndicesName"), AttributeIndicesBaseName + ParamInfo.DataInterfaceHLSLSymbol},
		{TEXT("AttributeIndexGroup"), FunctionInstanceIndex / 4},
		{TEXT("AttributeIndexComponent"), VectorComponentNames[FunctionInstanceIndex % 4]},
	};
	OutHLSL += FString::Format(*FormatBounds, ArgsBounds);
}

const TCHAR* UNiagaraDataInterfaceGrid2DCollection::TypeDefinitionToHLSLTypeString(const FNiagaraTypeDefinition& InDef) const
{
	if (InDef == FNiagaraTypeDefinition::GetFloatDef())
		return TEXT("float");
	if (InDef == FNiagaraTypeDefinition::GetVec2Def())
		return TEXT("float2");
	if (InDef == FNiagaraTypeDefinition::GetVec3Def())
		return TEXT("float3");
	if (InDef == FNiagaraTypeDefinition::GetVec4Def() || InDef == FNiagaraTypeDefinition::GetColorDef())
		return TEXT("float4");
	return nullptr;
}
FName UNiagaraDataInterfaceGrid2DCollection::TypeDefinitionToGetFunctionName(const FNiagaraTypeDefinition& InDef) const
{
	if (InDef == FNiagaraTypeDefinition::GetFloatDef())
		return GetFloatValueFunctionName;
	if (InDef == FNiagaraTypeDefinition::GetVec2Def())
		return GetVector2ValueFunctionName;
	if (InDef == FNiagaraTypeDefinition::GetVec3Def())
		return GetVector3ValueFunctionName;
	if (InDef == FNiagaraTypeDefinition::GetVec4Def() || InDef == FNiagaraTypeDefinition::GetColorDef())
		return GetVector4ValueFunctionName;
	return NAME_None;;
}
FName UNiagaraDataInterfaceGrid2DCollection::TypeDefinitionToSetFunctionName(const FNiagaraTypeDefinition& InDef) const
{
	if (InDef == FNiagaraTypeDefinition::GetFloatDef())
		return SetFloatValueFunctionName;
	if (InDef == FNiagaraTypeDefinition::GetVec2Def())
		return SetVector2ValueFunctionName;
	if (InDef == FNiagaraTypeDefinition::GetVec3Def())
		return SetVector3ValueFunctionName;
	if (InDef == FNiagaraTypeDefinition::GetVec4Def() || InDef == FNiagaraTypeDefinition::GetColorDef())
		return SetVector4ValueFunctionName;
	return NAME_None;;
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
	else if (FunctionInfo.DefinitionName == CopyPreviousToCurrentForCellFunctionName)
	{
		static const TCHAR* FormatBounds = TEXT(R"(
			void {FunctionName}(int In_IndexX, int In_IndexY)
			{
				for (int AttributeIndex = 0; AttributeIndex < {NumTiles}.x; AttributeIndex++)
				{			
					int TileIndexX = AttributeIndex % {NumTiles}.x;
					int TileIndexY = AttributeIndex / {NumTiles}.x;
					float Val = { Grid }.Load(int3(In_IndexX + TileIndexX* { NumCellsName }.x, In_IndexY + TileIndexY* { NumCellsName }.y, 0));
					RW{OutputGrid}[int2(In_IndexX + TileIndexX * {NumCellsName}.x, In_IndexY + TileIndexY * {NumCellsName}.y)] = Val;
				}
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("Grid"), GridName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("OutputGrid"), OutputGridName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("NumCellsName"), NumCellsName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("NumTiles"),    NumTilesName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	else if (FunctionInfo.DefinitionName == ClearCellFunctionName)
	{
		static const TCHAR* FormatBounds = TEXT(R"(
			void {FunctionName}(int In_IndexX, int In_IndexY)
			{
				for (int AttributeIndex = 0; AttributeIndex < {NumTiles}.x; AttributeIndex++)
				{			
					int TileIndexX = AttributeIndex % {NumTiles}.x;
					int TileIndexY = AttributeIndex / {NumTiles}.x;
					float Val = 0.0f;
					RW{OutputGrid}[int2(In_IndexX + TileIndexX * {NumCellsName}.x, In_IndexY + TileIndexY * {NumCellsName}.y)] = Val;
				}
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("Grid"), GridName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("OutputGrid"), OutputGridName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("NumCellsName"), NumCellsName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("NumTiles"),    NumTilesName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SetVector4ValueFunctionName)
	{
		WriteSetHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, 4, OutHLSL);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetVector4ValueFunctionName)
	{
		WriteGetHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, 4, OutHLSL);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleGridVector4FunctionName)
	{
		WriteSampleHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, 4, OutHLSL);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SetVector3ValueFunctionName)
	{
		WriteSetHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, 3, OutHLSL);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetVector3ValueFunctionName)
	{
		WriteGetHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, 3, OutHLSL);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleGridVector3FunctionName)
	{
		WriteSampleHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, 3, OutHLSL);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SetVector2ValueFunctionName)
	{
		WriteSetHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, 2, OutHLSL);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetVector2ValueFunctionName)
	{
		WriteGetHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, 2, OutHLSL);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleGridVector2FunctionName)
	{
		WriteSampleHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, 2, OutHLSL);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SetFloatValueFunctionName)
	{
		WriteSetHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, 1, OutHLSL);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetFloatValueFunctionName)
	{
		WriteGetHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, 1, OutHLSL);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleGridFloatFunctionName)
	{
		WriteSampleHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, 1, OutHLSL);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetVector4AttributeIndexFunctionName)
	{
		WriteAttributeGetIndexHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, 4, OutHLSL);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetVector3AttributeIndexFunctionName)
	{
		WriteAttributeGetIndexHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, 3, OutHLSL);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetVector2AttributeIndexFunctionName)
	{
		WriteAttributeGetIndexHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, 2, OutHLSL);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetFloatAttributeIndexFunctionName)
	{
		WriteAttributeGetIndexHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, 1, OutHLSL);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleGridFunctionName)
	{
		static const TCHAR* FormatBounds = TEXT(R"(
				void {FunctionName}(float In_UnitX, float In_UnitY, int In_AttributeIndex, out float Out_Val)
				{
					int TileIndexX = In_AttributeIndex % {NumTiles}.x;
					int TileIndexY = In_AttributeIndex / {NumTiles}.x;
					float2 UV =
					{
						In_UnitX / {NumTiles}.x + 1.0*TileIndexX/{NumTiles}.x,
						In_UnitY / {NumTiles}.y + 1.0*TileIndexY/{NumTiles}.y
					};
					float2 TileMin =
					{
						(TileIndexX * {NumCellsName}.x + 0.5) / ({NumTiles}.x * {NumCellsName}.x),
						(TileIndexY * {NumCellsName}.y + 0.5) / ({NumTiles}.y * {NumCellsName}.y),
					};
					float2 TileMax =
					{
						((TileIndexX + 1) * {NumCellsName}.x - 0.5) / ({NumTiles}.x * {NumCellsName}.x),
						((TileIndexY + 1) * {NumCellsName}.y - 0.5) / ({NumTiles}.y * {NumCellsName}.y),
					};
					UV = clamp(UV, TileMin, TileMax);
				
					Out_Val = {Grid}.SampleLevel({SamplerName}, UV, 0);
				}
			)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("Grid"), GridName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("SamplerName"),    SamplerName + ParamInfo.DataInterfaceHLSLSymbol },
			{TEXT("NumTiles"),    NumTilesName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("NumCellsName"), NumCellsName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	return false;
}
#if WITH_EDITOR
bool UNiagaraDataInterfaceGrid2DCollection::GenerateIterationSourceNamespaceReadAttributesHLSL(FNiagaraDataInterfaceGPUParamInfo& DIInstanceInfo, const FNiagaraVariable& IterationSourceVar, TConstArrayView<FNiagaraVariable> InArguments, TConstArrayView<FNiagaraVariable> InAttributes, TConstArrayView<FString> InAttributeHLSLNames, bool bInSetToDefaults, bool bPartialWrites, TArray<FText>& OutErrors, FString& OutHLSL) const
{

	FString DIVarName;
	OutHLSL += TEXT("\t//Generated by UNiagaraDataInterfaceGrid2DCollection::GenerateIterationSourceNamespaceReadAttributesHLSL\n");
	for (int32 i = 0; i < InArguments.Num(); i++)
	{
		OutHLSL += FString::Printf(TEXT("\t// Argument Name \"%s\" Type \"%s\"\n"), *InArguments[i].GetName().ToString(), *InArguments[i].GetType().GetName());
		if (InArguments[i].GetType().GetClass() == GetClass())
		{
			DIVarName = InArguments[i].GetName().ToString();
		}
	}
	

	if (InAttributes.Num() != InAttributeHLSLNames.Num())
		return false;

	if (InAttributes.Num() > 0)
	{
		OutHLSL += FString::Printf(TEXT("\tint X, Y;\n\t%s.ExecutionIndexToGridIndex(X, Y);\n"), *DIVarName);
	}

	TArray<FString> RootArray;
	IterationSourceVar.GetName().ToString().ParseIntoArray(RootArray, TEXT("."));

	for (int32 i = 0; i < InAttributes.Num(); i++)
	{
		OutHLSL += FString::Printf(TEXT("\t// Variable Name \"%s\" Type \"%s\" Var \"%s\"\n" ), *InAttributes[i].GetName().ToString(), *InAttributes[i].GetType().GetName(), *InAttributeHLSLNames[i]);

		TArray<FString> OutArray;
		if (InAttributes[i].GetName().ToString().ParseIntoArray(OutArray, TEXT(".")) > 0)
		{
			if (TypeDefinitionToSetFunctionName(InAttributes[i].GetType()) == NAME_None)
			{
				FText Error = FText::Format(LOCTEXT("UnknownType", "Unsupported Type {0} , Attribute {1}"), InAttributes[i].GetType().GetNameText(), FText::FromName(InAttributes[i].GetName()));
				OutErrors.Add(Error);
				continue;
			}

			// Clear out the shared namespace with the root variable...
			FString AttributeName;
			for (int32 NamespaceIdx = 0;  NamespaceIdx < OutArray.Num(); NamespaceIdx++)
			{
				if (NamespaceIdx < RootArray.Num() && RootArray[NamespaceIdx] == OutArray[NamespaceIdx])
					continue;
				if (OutArray[NamespaceIdx] == (FNiagaraConstants::PreviousNamespace.ToString()) || OutArray[NamespaceIdx] == (FNiagaraConstants::InitialNamespace.ToString()))
				{
					FText Error = FText::Format(LOCTEXT("UnknownSubNamespace", "Unsupported NamespaceModifier Attribute {0}"), FText::FromName(InAttributes[i].GetName()));
					OutErrors.Add(Error);
				}
				if (AttributeName.Len() != 0)
					AttributeName += TEXT(".");
				AttributeName += OutArray[NamespaceIdx];
			}
			OutHLSL += FString::Printf(TEXT("\t%s.%s<Attribute=\"%s\">(X, Y, %s);\n"), *DIVarName, *TypeDefinitionToGetFunctionName(InAttributes[i].GetType()).ToString(), *AttributeName, *InAttributeHLSLNames[i]);
		}

	}
	return true;
}
bool UNiagaraDataInterfaceGrid2DCollection::GenerateIterationSourceNamespaceWriteAttributesHLSL(FNiagaraDataInterfaceGPUParamInfo& DIInstanceInfo, const FNiagaraVariable& IterationSourceVar, TConstArrayView<FNiagaraVariable> InArguments, TConstArrayView<FNiagaraVariable> InAttributes, TConstArrayView<FString> InAttributeHLSLNames, bool bPartialWrites, TArray<FText>& OutErrors, FString& OutHLSL) const
{
	FString DIVarName;
	OutHLSL += TEXT("\t//Generated by UNiagaraDataInterfaceGrid2DCollection::GenerateIterationSourceNamespaceWriteAttributesHLSL\n");
	for (int32 i = 0; i < InArguments.Num(); i++)
	{
		OutHLSL += FString::Printf(TEXT("\t// Argument Name \"%s\" Type \"%s\"\n"), *InArguments[i].GetName().ToString(), *InArguments[i].GetType().GetName());		
		if (InArguments[i].GetType().GetClass() == GetClass())
		{
			DIVarName = InArguments[i].GetName().ToString();
		}
	}
	if (InAttributes.Num() != InAttributeHLSLNames.Num())
		return false;

	// First we need to copy all the data over from the input buffer, because we can't assume that this function will know all the attributes held within the grid. Instead, we copy all of them
	// over AND THEN overlay the local changes. Hopefully the optimizer will know enough to fix this up.
	if (InAttributes.Num() > 0)
	{
		OutHLSL += FString::Printf(TEXT("\tint X, Y;\n\t%s.ExecutionIndexToGridIndex(X, Y);\n"), *DIVarName);
	}
	
	TArray<FString> RootArray;
	IterationSourceVar.GetName().ToString().ParseIntoArray(RootArray, TEXT("."));

	for (int32 i = 0; i < InAttributes.Num(); i++)
	{
		OutHLSL += FString::Printf(TEXT("\t// Name \"%s\" Type \"%s\" Var \"%s\"\n"), *InAttributes[i].GetName().ToString(), *InAttributes[i].GetType().GetName(), *InAttributeHLSLNames[i]);
		
		TArray<FString> OutArray;
		if (InAttributes[i].GetName().ToString().ParseIntoArray(OutArray, TEXT(".")) > 0)
		{
			if (TypeDefinitionToSetFunctionName(InAttributes[i].GetType()) == NAME_None)
			{
				FText Error = FText::Format(LOCTEXT("UnknownType", "Unsupported Type {0} , Attribute {1}"), InAttributes[i].GetType().GetNameText(), FText::FromName(InAttributes[i].GetName()));
				OutErrors.Add(Error);
				continue;
			}
			
			// Clear out the shared namespace with the root variable...
			FString AttributeName;
			for (int32 NamespaceIdx = 0; NamespaceIdx < OutArray.Num(); NamespaceIdx++)
			{
				if (NamespaceIdx < RootArray.Num() && RootArray[NamespaceIdx] == OutArray[NamespaceIdx])
					continue;

				if (OutArray[NamespaceIdx] == (FNiagaraConstants::PreviousNamespace.ToString()) || OutArray[NamespaceIdx] == (FNiagaraConstants::InitialNamespace.ToString()))
				{
					FText Error = FText::Format(LOCTEXT("UnknownSubNamespace", "Unsupported NamespaceModifier Attribute {0}"), FText::FromName(InAttributes[i].GetName()));
					OutErrors.Add(Error);
				}
				if (AttributeName.Len() != 0)
					AttributeName += TEXT(".");
				AttributeName += OutArray[NamespaceIdx];
			}

			OutHLSL += FString::Printf(TEXT("\t%s.%s<Attribute=\"%s\">(X, Y, %s);\n"), *DIVarName, *TypeDefinitionToSetFunctionName(InAttributes[i].GetType()).ToString(), *AttributeName, *InAttributeHLSLNames[i]);
		}
	}
	return true;
}


bool UNiagaraDataInterfaceGrid2DCollection::GenerateSetupHLSL(FNiagaraDataInterfaceGPUParamInfo& DIInstanceInfo, TConstArrayView<FNiagaraVariable> InArguments, bool bSpawnOnly, bool bPartialWrites, TArray<FText>& OutErrors, FString& OutHLSL) const
{
	FString DIVarName;
	OutHLSL += TEXT("\t//Generated by UNiagaraDataInterfaceGrid2DCollection::GenerateSetupHLSL\n");
	for (int32 i = 0; i < InArguments.Num(); i++)
	{
		OutHLSL += FString::Printf(TEXT("\t// Argument Name \"%s\" Type \"%s\"\n"), *InArguments[i].GetName().ToString(), *InArguments[i].GetType().GetName());

		if (InArguments[i].GetType().GetClass() == GetClass())
		{
			DIVarName = InArguments[i].GetName().ToString();
		}
	}

	if (!bSpawnOnly && !bPartialWrites)
	{
		static const TCHAR* FormatBounds = TEXT(R"(
			// We need to copy from previous to current first thing, because other functions afterwards may just set values on the local grid.
			int X, Y;
			{Grid}.ExecutionIndexToGridIndex(X, Y);
			{Grid}.CopyPreviousToCurrentForCell(X,Y);
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("Grid"), DIVarName},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
	}

	return true;
}

bool UNiagaraDataInterfaceGrid2DCollection::GenerateTeardownHLSL(FNiagaraDataInterfaceGPUParamInfo& DIInstanceInfo, TConstArrayView<FNiagaraVariable> InArguments, bool bSpawnOnly, bool bPartialWrites, TArray<FText>& OutErrors, FString& OutHLSL) const
{
	OutHLSL += TEXT("\t//Generated by UNiagaraDataInterfaceGrid2DCollection::GenerateTeardownHLSL\n");
	

	return true;
}
#endif
bool UNiagaraDataInterfaceGrid2DCollection::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceGrid2DCollection* OtherTyped = CastChecked<UNiagaraDataInterfaceGrid2DCollection>(Destination);
	OtherTyped->RenderTargetUserParameter = RenderTargetUserParameter;
	OtherTyped->bCreateRenderTarget = bCreateRenderTarget;
	OtherTyped->BufferFormat = BufferFormat;
#if WITH_EDITOR
	OtherTyped->bPreviewGrid = bPreviewGrid;
	OtherTyped->PreviewAttribute = PreviewAttribute;
#endif

	return true;
}

bool UNiagaraDataInterfaceGrid2DCollection::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	check(Proxy);

	FGrid2DCollectionRWInstanceData_GameThread* InstanceData = new (PerInstanceData) FGrid2DCollectionRWInstanceData_GameThread();
	SystemInstancesToProxyData_GT.Emplace(SystemInstance->GetId(), InstanceData);

	InstanceData->NumCells.X = NumCellsX;
	InstanceData->NumCells.Y = NumCellsY;

	/* Go through all references to this data interface and build up the attribute list from the function metadata of those referenced.*/
	int32 NumAttribChannelsFound = 0;
	FindAttributes(InstanceData->Vars, InstanceData->Offsets, NumAttribChannelsFound);

	NumAttribChannelsFound = NumAttributes + NumAttribChannelsFound;
	InstanceData->NumAttributes = NumAttribChannelsFound;

	// #todo(dmp): refactor
	int MaxDim = 16384;
	int MaxTilesX = floor(MaxDim / NumCellsX);
	int MaxTilesY = floor(MaxDim / NumCellsY);
	int32 MaxAttributes = MaxTilesX * MaxTilesY;
	if ((NumAttribChannelsFound > MaxAttributes && MaxAttributes > 0) || NumAttribChannelsFound == 0)
	{
		UE_LOG(LogNiagara, Error, TEXT("Not enough or too many attributes defined on %s... max is %i, num defined is %i"), *FNiagaraUtilities::SystemInstanceIDToString(SystemInstance->GetId()), MaxAttributes, NumAttribChannelsFound);
		return false;
	}

	// need to determine number of tiles in x and y based on number of attributes and max dimension size
	int NumTilesX = NumAttribChannelsFound <= MaxTilesX ? NumAttribChannelsFound : MaxTilesX;
	int NumTilesY = ceil(1.0* NumAttribChannelsFound / NumTilesX);

	InstanceData->NumTiles.X = NumTilesX;
	InstanceData->NumTiles.Y = NumTilesY;
	
	InstanceData->WorldBBoxSize = WorldBBoxSize;

	InstanceData->PixelFormat = FNiagaraUtilities::BufferFormatToPixelFormat(BufferFormat);

	if (GNiagaraGrid2DOverrideFormat >= int32(ENiagaraGpuBufferFormat::Float) && (GNiagaraGrid2DOverrideFormat < int32(ENiagaraGpuBufferFormat::Max)) )
	{
		InstanceData->PixelFormat = FNiagaraUtilities::BufferFormatToPixelFormat(ENiagaraGpuBufferFormat(GNiagaraGrid2DOverrideFormat));
	}

	if (!FMath::IsNearlyEqual(GNiagaraGrid2DResolutionMultiplier, 1.0f))
	{
		InstanceData->NumCells.X = FMath::Max(1, int32(float(InstanceData->NumCells.X) * GNiagaraGrid2DResolutionMultiplier));
		InstanceData->NumCells.Y = FMath::Max(1, int32(float(InstanceData->NumCells.Y) * GNiagaraGrid2DResolutionMultiplier));
	}


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

	FTextureResource* RT_Resource = NULL;

	InstanceData->TargetTexture = nullptr;

	if (UTextureRenderTarget* UserParamObject = Cast<UTextureRenderTarget>(InstanceData->RTUserParamBinding.Init(SystemInstance->GetInstanceParameters(), RenderTargetUserParameter.Parameter)))
	{
		InstanceData->TargetTexture = Cast<UTextureRenderTarget2D>(UserParamObject);
		if (!InstanceData->TargetTexture)
		{
			UE_LOG(LogNiagara, Error, TEXT("Only UTextureRenderTarget2D are valid on %s"), *FNiagaraUtilities::SystemInstanceIDToString(SystemInstance->GetId()));
		}
	}

	if (!InstanceData->TargetTexture && bCreateRenderTarget != 0)
	{
		InstanceData->TargetTexture =  NewObject<UTextureRenderTarget2D>(this);
		FNiagaraSystemInstanceID SysID = SystemInstance->GetId();
		ManagedRenderTargets.Add(SysID) = InstanceData->TargetTexture;
	}

	if (InstanceData->TargetTexture)
	{
		// resize RT to match what we need for the output
		InstanceData->TargetTexture->RenderTargetFormat = FNiagaraUtilities::BufferFormatToRenderTargetFormat(BufferFormat);
		InstanceData->TargetTexture->ClearColor = FLinearColor(.5, 0, 0, 0);
		InstanceData->TargetTexture->bAutoGenerateMips = false;
		InstanceData->TargetTexture->InitAutoFormat(NumCellsX * NumTilesX, NumCellsY * NumTilesY);
		InstanceData->TargetTexture->UpdateResourceImmediate(true);

		if (InstanceData->TargetTexture->Resource)
		{
			RT_Resource = InstanceData->TargetTexture->Resource;
		}
	}

#if WITH_EDITOR
	InstanceData->bPreviewGrid = bPreviewGrid;
	InstanceData->PreviewAttribute = FIntVector4(INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE);
	if (bPreviewGrid && !PreviewAttribute.IsNone())
	{
		const int32 VariableIndex = InstanceData->Vars.IndexOfByPredicate([&](const FNiagaraVariableBase& Variable) { return Variable.GetName() == PreviewAttribute; });
		if (VariableIndex != INDEX_NONE)
		{
			const int32 NumComponents = InstanceData->Vars[VariableIndex].GetType().GetSize() / sizeof(float);
			if (ensure(NumComponents > 0 && NumComponents <= 4))
			{
				const int32 ComponentOffset = InstanceData->Offsets[VariableIndex];
				for (int32 i = 0; i < NumComponents; ++i)
				{
					InstanceData->PreviewAttribute[i] = ComponentOffset + i;
				}
			}
		}
		// Look for anonymous attributes
		else if ( NumAttributes > 0 )
		{
			const FString PreviewAttributeString = PreviewAttribute.ToString();
			if (PreviewAttributeString.StartsWith(AnonymousAttributeString))
			{
				InstanceData->PreviewAttribute[0] = FCString::Atoi(&PreviewAttributeString.GetCharArray()[AnonymousAttributeString.Len() + 1]);
			}
		}

		if (InstanceData->PreviewAttribute == FIntVector4(INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE))
		{
			UE_LOG(LogNiagara, Warning, TEXT("Failed to map PreviewAttribute %s to a grid index"), *PreviewAttribute.ToString());
		}
	}
#endif

	// Push Updates to Proxy.
	FNiagaraDataInterfaceProxyGrid2DCollectionProxy* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyGrid2DCollectionProxy>();
	ENQUEUE_RENDER_COMMAND(FUpdateData)(
		[RT_Resource, RT_Proxy, InstanceID=SystemInstance->GetId(), RT_InstanceData=*InstanceData, RT_OutputShaderStages=OutputShaderStages, RT_IterationShaderStages= IterationShaderStages](FRHICommandListImmediate& RHICmdList)
	{
		check(!RT_Proxy->SystemInstancesToProxyData_RT.Contains(InstanceID));
		FGrid2DCollectionRWInstanceData_RenderThread* TargetData = &RT_Proxy->SystemInstancesToProxyData_RT.Add(InstanceID);

		TargetData->DebugTargetTexture = RT_InstanceData.TargetTexture;
		TargetData->NumCells = RT_InstanceData.NumCells;
		TargetData->NumTiles = RT_InstanceData.NumTiles;
		TargetData->CellSize = RT_InstanceData.CellSize;
		TargetData->WorldBBoxSize = RT_InstanceData.WorldBBoxSize;
		TargetData->PixelFormat = RT_InstanceData.PixelFormat;
		TargetData->Offsets = RT_InstanceData.Offsets;
		TargetData->Vars.Reserve(RT_InstanceData.Vars.Num());
		for (int32 i = 0; i < RT_InstanceData.Vars.Num(); i++)
		{
			TargetData->Vars.Emplace(RT_InstanceData.Vars[i].GetName());
			TargetData->VarComponents.Emplace(RT_InstanceData.Vars[i].GetType().GetSize() / sizeof(float));
		}
#if WITH_EDITOR
		TargetData->bPreviewGrid = RT_InstanceData.bPreviewGrid;
		TargetData->PreviewAttribute = RT_InstanceData.PreviewAttribute;
#endif

		RT_Proxy->OutputSimulationStages_DEPRECATED = RT_OutputShaderStages;
		RT_Proxy->IterationSimulationStages_DEPRECATED = RT_IterationShaderStages;

		RT_Proxy->SetElementCount(TargetData->NumCells.X * TargetData->NumCells.Y);

		
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

	// Make sure to clear out the reference to the render target if we created one.
	FNiagaraSystemInstanceID SysId = SystemInstance->GetId();
	ManagedRenderTargets.Remove(SysId);
}

bool UNiagaraDataInterfaceGrid2DCollection::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{	
	FGrid2DCollectionRWInstanceData_GameThread* InstanceData = SystemInstancesToProxyData_GT.FindRef(SystemInstance->GetId());

	FTextureResource* RT_Resource = nullptr;

	bool NeedsReset = false;
	if (UTextureRenderTarget* UserParamObject = Cast<UTextureRenderTarget>(InstanceData->RTUserParamBinding.Init(SystemInstance->GetInstanceParameters(), RenderTargetUserParameter.Parameter)))
	{
		if (UTextureRenderTarget2D* LocalTargetTexture = Cast<UTextureRenderTarget2D>(UserParamObject))
		{
			InstanceData->TargetTexture = LocalTargetTexture;	
		}
		else
		{
			UE_LOG(LogNiagara, Error, TEXT("Only UTextureRenderTarget2D are valid on %s"), *FNiagaraUtilities::SystemInstanceIDToString(SystemInstance->GetId()));
		}
	}

	if (InstanceData->TargetTexture)
	{
		int32 RTSizeX = InstanceData->NumCells.X * InstanceData->NumTiles.X;
		int32 RTSizeY = InstanceData->NumCells.Y * InstanceData->NumTiles.Y;

		const ETextureRenderTargetFormat RenderTargetFormat = FNiagaraUtilities::BufferFormatToRenderTargetFormat(BufferFormat);
		if (InstanceData->TargetTexture->SizeX != RTSizeX || InstanceData->TargetTexture->SizeY != RTSizeY || InstanceData->TargetTexture->RenderTargetFormat != RenderTargetFormat)
		{
			// resize RT to match what we need for the output
			InstanceData->TargetTexture->RenderTargetFormat = RenderTargetFormat;
			InstanceData->TargetTexture->ClearColor = FLinearColor(0.5,0,0,0);
			InstanceData->TargetTexture->bAutoGenerateMips = false;
			InstanceData->TargetTexture->InitAutoFormat(RTSizeX, RTSizeY);
			InstanceData->TargetTexture->UpdateResourceImmediate(true);
			//TargetTexture->InitCustomFormat(InstanceData->NumCells.X * InstanceData->NumTiles.X, InstanceData->NumCells.Y * InstanceData->NumTiles.Y, PF_R32_FLOAT, false);

			if (InstanceData->TargetTexture->Resource)
			{
				NeedsReset = true;					
			}				
		}
		RT_Resource = InstanceData->TargetTexture->Resource;
	}

	FNiagaraDataInterfaceProxyGrid2DCollectionProxy* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyGrid2DCollectionProxy>();
	ENQUEUE_RENDER_COMMAND(FUpdateData)(
		[GridColl = this,TexPtr = InstanceData->TargetTexture, RT_Resource, RT_Proxy, InstanceID = SystemInstance->GetId()](FRHICommandListImmediate& RHICmdList)
	{
		FGrid2DCollectionRWInstanceData_RenderThread* TargetData = RT_Proxy->SystemInstancesToProxyData_RT.Find(InstanceID);
		TargetData->DebugTargetTexture = TexPtr;
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

void UNiagaraDataInterfaceGrid2DCollection::GetExposedVariables(TArray<FNiagaraVariableBase>& OutVariables) const
{
	OutVariables.Emplace(ExposedRTVar);
}

bool UNiagaraDataInterfaceGrid2DCollection::GetExposedVariableValue(const FNiagaraVariableBase& InVariable, void* InPerInstanceData, FNiagaraSystemInstance* InSystemInstance, void* OutData) const
{
	FGrid2DCollectionRWInstanceData_GameThread* InstanceData = static_cast<FGrid2DCollectionRWInstanceData_GameThread*>(InPerInstanceData);
	if (InVariable.IsValid() && InVariable == ExposedRTVar && InstanceData && InstanceData->TargetTexture)
	{
		UTextureRenderTarget** Var = (UTextureRenderTarget**)OutData;
		*Var = InstanceData->TargetTexture;
		return true;
	}
	return false;
}

void UNiagaraDataInterfaceGrid2DCollection::CollectAttributesForScript(UNiagaraScript* Script, FName VariableName, TArray<FNiagaraVariableBase>& OutVariables, TArray<uint32>& OutVariableOffsets, int32& TotalAttributes, TArray<FText>* OutWarnings)
{
	if (const FNiagaraScriptExecutionParameterStore* ParameterStore = Script->GetExecutionReadyParameterStore(ENiagaraSimTarget::GPUComputeSim))
	{
		const FNiagaraVariableBase DataInterfaceVariable(FNiagaraTypeDefinition(UNiagaraDataInterfaceGrid2DCollection::StaticClass()), VariableName);

		const int32 * IndexOfDataInterface = ParameterStore->FindParameterOffset(DataInterfaceVariable);
		if (IndexOfDataInterface != nullptr)
		{
			const TArray<FNiagaraDataInterfaceGPUParamInfo>& ParamInfoArray = Script->GetVMExecutableData().DIParamInfo;
			for (const FNiagaraDataInterfaceGeneratedFunction& Func : ParamInfoArray[*IndexOfDataInterface].GeneratedFunctions)
			{
				static const FName NAME_Attribute("Attribute");

				if (const FName* AttributeName = Func.FindSpecifierValue(NAME_Attribute))
				{
					FNiagaraVariableBase NewVar(UNiagaraDataInterfaceGrid2DCollection::GetValueTypeFromFuncName(Func.DefinitionName), *AttributeName);
					if (UNiagaraDataInterfaceGrid2DCollection::CanCreateVarFromFuncName(Func.DefinitionName))
					{
						if (!OutVariables.Contains(NewVar))
						{
							const int32 FoundNameMatch = OutVariables.IndexOfByPredicate([&](const FNiagaraVariableBase& Var) { return Var.GetName() == *AttributeName; });
							if (FoundNameMatch == INDEX_NONE)
							{
								OutVariables.Add(NewVar);
								const int32 NumComponents = NewVar.GetSizeInBytes() / sizeof(float);
								OutVariableOffsets.Add(TotalAttributes);
								TotalAttributes += NumComponents;
							}
							else
							{
								if (OutWarnings)
								{
									FText Warning = FText::Format(LOCTEXT("BadType", "Same name, different types! {0} vs {1}, Attribute {2}"), NewVar.GetType().GetNameText(), OutVariables[FoundNameMatch].GetType().GetNameText(), FText::FromName(NewVar.GetName()));
									OutWarnings->Add(Warning);
								}
							}
						}
					}
				}
			}
		}
	}
}

void UNiagaraDataInterfaceGrid2DCollection::FindAttributesByName(FName VariableName, TArray<FNiagaraVariableBase>& OutVariables, TArray<uint32>& OutVariableOffsets, int32& OutNumAttribChannelsFound, TArray<FText>* OutWarnings) const
{
	OutNumAttribChannelsFound = 0;

	UNiagaraSystem* OwnerSystem = GetTypedOuter<UNiagaraSystem>();
	if (OwnerSystem == nullptr)
	{
		return;
	}

	int32 TotalAttributes = NumAttributes;
	for (const FNiagaraEmitterHandle& EmitterHandle : OwnerSystem->GetEmitterHandles())
	{
		UNiagaraEmitter* Emitter = EmitterHandle.GetInstance();
		if (Emitter && EmitterHandle.GetIsEnabled() && Emitter->IsValid() && (Emitter->SimTarget == ENiagaraSimTarget::GPUComputeSim))
		{
			CollectAttributesForScript(Emitter->GetGPUComputeScript(), VariableName, OutVariables, OutVariableOffsets, TotalAttributes, OutWarnings);
		}
	}
	OutNumAttribChannelsFound = TotalAttributes - NumAttributes;
}

void UNiagaraDataInterfaceGrid2DCollection::FindAttributes(TArray<FNiagaraVariableBase>& OutVariables, TArray<uint32>& OutVariableOffsets, int32& OutNumAttribChannelsFound, TArray<FText>* OutWarnings) const
{
	OutNumAttribChannelsFound = 0;

	UNiagaraSystem* OwnerSystem = GetTypedOuter<UNiagaraSystem>();
	if (OwnerSystem == nullptr)
	{
		return;
	}

	int32 TotalAttributes = NumAttributes;
	for (const FNiagaraEmitterHandle& EmitterHandle : OwnerSystem->GetEmitterHandles())
	{
		UNiagaraEmitter* Emitter = EmitterHandle.GetInstance();
		if (Emitter && EmitterHandle.GetIsEnabled() && Emitter->IsValid() && (Emitter->SimTarget == ENiagaraSimTarget::GPUComputeSim))
		{
			// Search scripts for this data interface so we get the variable name
			auto FindDataInterfaceVariable = 
				[&OwnerSystem, &Emitter](const UNiagaraDataInterface* DataInterface) -> FName
				{
					UNiagaraScript* Scripts[] =
					{
						OwnerSystem->GetSystemSpawnScript(),
						OwnerSystem->GetSystemUpdateScript(),
						Emitter->GetGPUComputeScript(),
					};

					for (UNiagaraScript* Script : Scripts)
					{
						for (FNiagaraScriptDataInterfaceInfo& DataInterfaceInfo : Script->GetCachedDefaultDataInterfaces())
						{
							if (DataInterfaceInfo.DataInterface == DataInterface)
							{
								return DataInterfaceInfo.RegisteredParameterMapRead.IsNone() ? DataInterfaceInfo.RegisteredParameterMapWrite : DataInterfaceInfo.RegisteredParameterMapRead;
							}
						}
					}
					return NAME_None;
				};

			const FName VariableName = FindDataInterfaceVariable(this);
			if (!VariableName.IsNone() )
			{
				CollectAttributesForScript(Emitter->GetGPUComputeScript(), VariableName, OutVariables, OutVariableOffsets, TotalAttributes, OutWarnings);
			}
		}
	}
	OutNumAttribChannelsFound = TotalAttributes - NumAttributes;
}

static void TransitionAndCopyTexture(FRHICommandList& RHICmdList, FRHITexture* Source, FRHITexture* Destination, const FRHICopyTextureInfo& CopyInfo)
{
	FRHITransitionInfo TransitionsBefore[] = {
		FRHITransitionInfo(Source, ERHIAccess::SRVMask, ERHIAccess::CopySrc),
		FRHITransitionInfo(Destination, ERHIAccess::SRVMask, ERHIAccess::CopyDest)
	};

	RHICmdList.Transition(MakeArrayView(TransitionsBefore, UE_ARRAY_COUNT(TransitionsBefore)));

	RHICmdList.CopyTexture(Source, Destination, CopyInfo);

	FRHITransitionInfo TransitionsAfter[] = {
		FRHITransitionInfo(Source, ERHIAccess::CopySrc, ERHIAccess::SRVMask),
		FRHITransitionInfo(Destination, ERHIAccess::CopyDest, ERHIAccess::SRVMask)
	};

	RHICmdList.Transition(MakeArrayView(TransitionsAfter, UE_ARRAY_COUNT(TransitionsAfter)));
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
			TransitionAndCopyTexture(RHICmdList, Grid2DInstanceData->CurrentData->GridBuffer.Buffer, RT_TextureResource->TextureRHI, CopyInfo);
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
			FRHICopyTextureInfo CopyInfo;
			TransitionAndCopyTexture(RHICmdList, RT_Grid2DInstanceData->CurrentData->GridBuffer.Buffer, RT_TextureResource->TextureRHI, CopyInfo);
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

void UNiagaraDataInterfaceGrid2DCollection::GetNumCells(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FGrid2DCollectionRWInstanceData_GameThread> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int> OutNumCellsX(Context);
	VectorVM::FExternalFuncRegisterHandler<int> OutNumCellsY(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutNumCellsX.GetDestAndAdvance() = InstData->NumCells.X;
		*OutNumCellsY.GetDestAndAdvance() = InstData->NumCells.Y;
	}
}

void UNiagaraDataInterfaceGrid2DCollection::SetNumCells(FVectorVMContext& Context)
{
	// This should only be called from a system or emitter script due to a need for only setting up initially.
	VectorVM::FUserPtrHandler<FGrid2DCollectionRWInstanceData_GameThread> InstData(Context);
	VectorVM::FExternalFuncInputHandler<int> InNumCellsX(Context);
	VectorVM::FExternalFuncInputHandler<int> InNumCellsY(Context);
	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> OutSuccess(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		int NewNumCellsX = InNumCellsX.GetAndAdvance();
		int NewNumCellsY = InNumCellsY.GetAndAdvance();
		bool bSuccess = (InstData.Get() != nullptr && Context.NumInstances == 1 && NumCellsX >= 0 && NumCellsY >= 0);
		*OutSuccess.GetDestAndAdvance() = bSuccess;
		if (bSuccess)
		{
			FIntPoint OldNumCells = InstData->NumCells;
	
			InstData->NumCells.X = NewNumCellsX;
			InstData->NumCells.Y = NewNumCellsY;

			InstData->NeedsRealloc = OldNumCells != InstData->NumCells;
		}
	}
}

bool UNiagaraDataInterfaceGrid2DCollection::PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	FGrid2DCollectionRWInstanceData_GameThread* InstanceData = static_cast<FGrid2DCollectionRWInstanceData_GameThread*>(PerInstanceData);
	bool bNeedsReset = false;

	if (InstanceData->NeedsRealloc)
	{
		InstanceData->NeedsRealloc = false;
		
		InstanceData->CellSize = InstanceData->WorldBBoxSize / FVector2D(InstanceData->NumCells.X, InstanceData->NumCells.Y);

		int MaxDim = 16384;
		int MaxTilesX = floor(MaxDim / InstanceData->NumCells.X);
		int MaxTilesY = floor(MaxDim / InstanceData->NumCells.Y);
		int32 MaxAttributes = MaxTilesX * MaxTilesY;

		// need to determine number of tiles in x and y based on number of attributes and max dimension size
		int NumTilesX = InstanceData->NumAttributes <= MaxTilesX ? InstanceData->NumAttributes : MaxTilesX;
		int NumTilesY = ceil(1.0 * InstanceData->NumAttributes / NumTilesX);

		InstanceData->NumTiles.X = NumTilesX;
		InstanceData->NumTiles.Y = NumTilesY;

		FTextureResource* RT_Resource = NULL;
		if (InstanceData->TargetTexture)
		{
			// resize RT to match what we need for the output			
			InstanceData->TargetTexture->InitAutoFormat(InstanceData->NumCells.X * InstanceData->NumTiles.X, InstanceData->NumCells.Y * InstanceData->NumTiles.Y);
			InstanceData->TargetTexture->UpdateResourceImmediate(true);

			if (InstanceData->TargetTexture->Resource)
			{
				RT_Resource = InstanceData->TargetTexture->Resource;
			}
		}

		// Push Updates to Proxy.
		FNiagaraDataInterfaceProxyGrid2DCollectionProxy* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyGrid2DCollectionProxy>();
		ENQUEUE_RENDER_COMMAND(FUpdateData)(
			[GridColl = this, TexPtr = InstanceData->TargetTexture, RT_Resource, RT_Proxy, InstanceID = SystemInstance->GetId(), RT_InstanceData = *InstanceData, RT_OutputShaderStages = OutputShaderStages, RT_IterationShaderStages = IterationShaderStages](FRHICommandListImmediate& RHICmdList)
		{
			check(RT_Proxy->SystemInstancesToProxyData_RT.Contains(InstanceID));
			FGrid2DCollectionRWInstanceData_RenderThread* TargetData = RT_Proxy->SystemInstancesToProxyData_RT.Find(InstanceID);
			
			TargetData->NumCells = RT_InstanceData.NumCells;
			TargetData->NumTiles = RT_InstanceData.NumTiles;
			TargetData->CellSize = RT_InstanceData.CellSize;
			
			TargetData->Buffers.Empty();
			TargetData->CurrentData = nullptr;
			TargetData->DestinationData = nullptr;
			
			RT_Proxy->SetElementCount(TargetData->NumCells.X* TargetData->NumCells.Y);

			if (RT_Resource && RT_Resource->TextureRHI.IsValid())
			{
				TargetData->RenderTargetToCopyTo = RT_Resource->TextureRHI;
			}
			else
			{
				TargetData->RenderTargetToCopyTo = nullptr;
			}
		});

	}

	return false;
}


void UNiagaraDataInterfaceGrid2DCollection::GetAttributeIndex(FVectorVMContext& Context, const FName& InName, int32 NumChannels)
{
	VectorVM::FUserPtrHandler<FGrid2DCollectionRWInstanceData_GameThread> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int> OutIndex(Context);
	int32 Index = INDEX_NONE;
	if (InstData.Get())
		Index = InstData.Get()->FindAttributeIndexByName(InName, NumChannels);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutIndex.GetDestAndAdvance() = Index;
	}
}

int32 FGrid2DCollectionRWInstanceData_GameThread::FindAttributeIndexByName(const FName& InName, int32 NumChannels)
{
	for (int32 i = 0; i < Vars.Num(); i++)
	{
		const FNiagaraVariableBase& Var = Vars[i];
		if (Var.GetName() == InName)
		{
			if (NumChannels == 1 && Var.GetType() == FNiagaraTypeDefinition::GetFloatDef())
				return Offsets[i];
			else if (NumChannels == 2 && Var.GetType() == FNiagaraTypeDefinition::GetVec2Def())
				return Offsets[i];
			else if (NumChannels == 3 && Var.GetType() == FNiagaraTypeDefinition::GetVec3Def())
				return Offsets[i];
			else if (NumChannels == 4 && Var.GetType() == FNiagaraTypeDefinition::GetVec4Def())
				return Offsets[i];
			else if (NumChannels == 4 && Var.GetType() == FNiagaraTypeDefinition::GetColorDef())
				return Offsets[i];
		}
	}

	return INDEX_NONE;
}

void FGrid2DCollectionRWInstanceData_RenderThread::BeginSimulate(FRHICommandList& RHICmdList)
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
		DestinationData = new FGrid2DBuffer(NumCells.X * NumTiles.X, NumCells.Y * NumTiles.Y, PixelFormat);
		// The rest of the code expects to find the buffers readable, and will transition from there to UAVCompute as necessary.
		RHICmdList.Transition(FRHITransitionInfo(DestinationData->GridBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVMask));
		Buffers.Emplace(DestinationData);
	}
}

void FGrid2DCollectionRWInstanceData_RenderThread::EndSimulate(FRHICommandList& RHICmdList)
{
	CurrentData = DestinationData;
	DestinationData = nullptr;
}

void FNiagaraDataInterfaceProxyGrid2DCollectionProxy::PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context)
{
	// #todo(dmp): Context doesnt need to specify if a stage is output or not since we moved pre/post stage to the DI itself.  Not sure which design is better for the future
	if (Context.IsOutputStage)
	{
		FGrid2DCollectionRWInstanceData_RenderThread* ProxyData = SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID);

		ProxyData->BeginSimulate(RHICmdList);

		// If we don't have an iteration stage, then we should manually clear the buffer to make sure there is no residual data.  If we are doing something like rasterizing particles into a grid, we want it to be clear before
		// we start.  If a user wants to access data from the previous stage, then they can read from the current data.

		// #todo(dmp): we might want to expose an option where we have buffers that are write only and need a clear (ie: no buffering like the neighbor grid).  They would be considered transient perhaps?  It'd be more
		// memory efficient since it would theoretically not require any double buffering.		
		RHICmdList.Transition(FRHITransitionInfo(ProxyData->DestinationData->GridBuffer.UAV, ERHIAccess::SRVMask, ERHIAccess::UAVCompute));
		if (!Context.IsIterationStage)
		{
			RHICmdList.ClearUAVFloat(ProxyData->DestinationData->GridBuffer.UAV, FVector4(ForceInitToZero));
			RHICmdList.Transition(FRHITransitionInfo(ProxyData->DestinationData->GridBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));
		}		
	}
}

void FNiagaraDataInterfaceProxyGrid2DCollectionProxy::PostStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context)
{	
	if (Context.IsOutputStage)
	{
		FGrid2DCollectionRWInstanceData_RenderThread* ProxyData = SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID);
		RHICmdList.Transition(FRHITransitionInfo(ProxyData->DestinationData->GridBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
		ProxyData->EndSimulate(RHICmdList);
	}
}

void FNiagaraDataInterfaceProxyGrid2DCollectionProxy::PostSimulate(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceArgs& Context)
{
	FGrid2DCollectionRWInstanceData_RenderThread* ProxyData = SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID);

	if (ProxyData->RenderTargetToCopyTo != nullptr && ProxyData->CurrentData != nullptr && ProxyData->CurrentData->GridBuffer.Buffer != nullptr)
	{
		FRHICopyTextureInfo CopyInfo;
		TransitionAndCopyTexture(RHICmdList, ProxyData->CurrentData->GridBuffer.Buffer, ProxyData->RenderTargetToCopyTo, CopyInfo);
	}

#if WITH_EDITOR
	if (ProxyData->bPreviewGrid && ProxyData->CurrentData)
	{
		if (FNiagaraGpuComputeDebug* GpuComputeDebug = Context.Batcher->GetGpuComputeDebug())
		{
			if (ProxyData->PreviewAttribute[0] != INDEX_NONE)
			{
				GpuComputeDebug->AddAttributeTexture(RHICmdList, Context.SystemInstanceID, SourceDIName, ProxyData->CurrentData->GridBuffer.Buffer, ProxyData->NumTiles, ProxyData->PreviewAttribute);
			}
			else
			{
				GpuComputeDebug->AddTexture(RHICmdList, Context.SystemInstanceID, SourceDIName, ProxyData->CurrentData->GridBuffer.Buffer);
			}
		}
	}
#endif
}

void FNiagaraDataInterfaceProxyGrid2DCollectionProxy::ResetData(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceArgs& Context)
{	
	FGrid2DCollectionRWInstanceData_RenderThread* ProxyData = SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID);
	if (!ProxyData)
	{
		return;
	}

	for (TUniquePtr<FGrid2DBuffer>& Buffer : ProxyData->Buffers)
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
#undef LOCTEXT_NAMESPACE
