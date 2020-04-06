// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceSkeletalMesh.h"
#include "NiagaraComponent.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "SkeletalMeshTypes.h"
#include "NiagaraStats.h"
#include "Templates/AlignmentTemplates.h"
#include "NDISkeletalMeshCommon.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceSkeletalMesh_VertexSampling"

DECLARE_CYCLE_STAT(TEXT("Skel Mesh Vertex Sampling"), STAT_NiagaraSkel_Vertex_Sample, STATGROUP_Niagara);

//Final binders for all static mesh interface functions.
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetVertexSkinnedData);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetVertexColor);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetVertexColorFallback);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetVertexUV);

DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, IsValidFilteredVertex);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, RandomFilteredVertex);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetFilteredVertexCount);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetFilteredVertexAt);

const FName FSkeletalMeshInterfaceHelper::GetSkinnedVertexDataName("GetSkinnedVertexData");
const FName FSkeletalMeshInterfaceHelper::GetSkinnedVertexDataWSName("GetSkinnedVertexDataWS");
const FName FSkeletalMeshInterfaceHelper::GetVertexColorName("GetVertexColor");
const FName FSkeletalMeshInterfaceHelper::GetVertexUVName("GetVertexUV");

const FName FSkeletalMeshInterfaceHelper::IsValidVertexName("IsValidVertex");
const FName FSkeletalMeshInterfaceHelper::RandomVertexName("RandomVertex");
const FName FSkeletalMeshInterfaceHelper::GetVertexCountName("GetVertexCount");

const FName FSkeletalMeshInterfaceHelper::IsValidFilteredVertexName("IsValidFilteredVertex");
const FName FSkeletalMeshInterfaceHelper::RandomFilteredVertexName("RandomFilteredVertex");
const FName FSkeletalMeshInterfaceHelper::GetFilteredVertexCountName("GetFilteredVertexCount");
const FName FSkeletalMeshInterfaceHelper::GetFilteredVertexAtName("GetFilteredVertex");

void UNiagaraDataInterfaceSkeletalMesh::GetVertexSamplingFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetSkinnedVertexDataName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Vertex")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Normal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Binormal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Tangent")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetSkinnedDataDesc", "Returns skinning dependant data for the pased vertex in local space. All outputs are optional and you will incur zero to minimal cost if they are not connected.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetSkinnedVertexDataWSName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Vertex")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Normal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Binormal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Tangent")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetSkinnedDataWSDesc", "Returns skinning dependant data for the pased vertex in world space. All outputs are optional and you will incur zero to minimal cost if they are not connected.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetVertexColorName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Vertex")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Color")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetVertexUVName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Vertex")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("UV Set")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("UV")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::IsValidVertexName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Vertex")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsValid")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::RandomVertexName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FNiagaraRandInfo::StaticStruct()), TEXT("RandomInfo")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Vertex")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetVertexCountName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Count")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::IsValidFilteredVertexName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Vertex")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsValid")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::RandomFilteredVertexName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FNiagaraRandInfo::StaticStruct()), TEXT("RandomInfo")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Vertex")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetFilteredVertexCountName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Count")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetFilteredVertexAtName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Filtered Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Vertex")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
}

void UNiagaraDataInterfaceSkeletalMesh::BindVertexSamplingFunction(const FVMExternalFunctionBindingInfo& BindingInfo, FNDISkeletalMesh_InstanceData* InstanceData, FVMExternalFunction &OutFunc)
{
	//////////////////////////////////////////////////////////////////////////
	if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetSkinnedVertexDataName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 15);
		TSkinningModeBinder<TNDIExplicitBinder<FNDITransformHandlerNoop, TVertexAccessorBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetVertexSkinnedData)>>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetSkinnedVertexDataWSName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 15);
		TSkinningModeBinder<TNDIExplicitBinder<FNDITransformHandler, TVertexAccessorBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetVertexSkinnedData)>>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetVertexColorName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 4);
		if (InstanceData->HasColorData())
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetVertexColor)::Bind(this, OutFunc);
		}
		else
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetVertexColorFallback)::Bind(this, OutFunc);
		}
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetVertexUVName)
	{
		check(BindingInfo.GetNumInputs() == 3 && BindingInfo.GetNumOutputs() == 2);
		TVertexAccessorBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetVertexUV)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	//////////////////////////////////////////////////////////////////////////
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::IsValidVertexName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 1);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->IsValidVertex(Context); });
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::RandomVertexName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 1);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->RandomVertex(Context); });
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetVertexCountName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->GetVertexCount(Context); });
	}
	//////////////////////////////////////////////////////////////////////////
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::IsValidFilteredVertexName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 1);
		TFilterModeBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, IsValidFilteredVertex)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::RandomFilteredVertexName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 1);
		TFilterModeBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, RandomFilteredVertex)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetFilteredVertexCountName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		TFilterModeBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetFilteredVertexCount)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetFilteredVertexAtName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 1);
		TFilterModeBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetFilteredVertexAt)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
}

//////////////////////////////////////////////////////////////////////////

void UNiagaraDataInterfaceSkeletalMesh::IsValidVertex(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Vertex_Sample);

	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	VectorVM::FExternalFuncInputHandler<int32> VertexParam(Context);
	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> OutValid(Context);

	FSkeletalMeshAccessorHelper MeshAccessor;
	MeshAccessor.Init<TNDISkelMesh_FilterModeNone, TNDISkelMesh_AreaWeightingOff>(InstData);

	const int32 MaxVertex = MeshAccessor.IsSkinAccessible() ? MeshAccessor.LODData->GetNumVertices() : 0;
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		const int32 VertexIndex = VertexParam.GetAndAdvance();

		FNiagaraBool Value;
		Value.SetValue(VertexIndex < MaxVertex);
		*OutValid.GetDestAndAdvance() = Value;
	}
}

void UNiagaraDataInterfaceSkeletalMesh::RandomVertex(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Vertex_Sample);

	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	FNDIRandomHelper RandHelper(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutVertex(Context);

	FSkeletalMeshAccessorHelper MeshAccessor;
	MeshAccessor.Init<TNDISkelMesh_FilterModeNone, TNDISkelMesh_AreaWeightingOff>(InstData);

	const int32 MaxVertex = MeshAccessor.IsSkinAccessible() ? MeshAccessor.LODData->GetNumVertices() - 1 : -1;
	if (MaxVertex >= 0)
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			RandHelper.GetAndAdvance();
			*OutVertex.GetDestAndAdvance() = RandHelper.RandRange(i, 0, MaxVertex);
		}
	}
	else
	{
		FMemory::Memset(OutVertex.GetDest(), 0xff, Context.NumInstances * sizeof(int32));
	}
}

void UNiagaraDataInterfaceSkeletalMesh::GetVertexCount(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Vertex_Sample);

	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutVertexCount(Context);

	FSkeletalMeshAccessorHelper MeshAccessor;
	MeshAccessor.Init<TNDISkelMesh_FilterModeNone, TNDISkelMesh_AreaWeightingOff>(InstData);

	const int32 MaxVertex = MeshAccessor.IsSkinAccessible() ? MeshAccessor.LODData->GetNumVertices() : 0;
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*OutVertexCount.GetDestAndAdvance() = MaxVertex;
	}
}

//////////////////////////////////////////////////////////////////////////
template<typename FilterMode>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::RandomFilteredVertIndex(FNDIRandomHelper& RandHelper, int32 Instance, FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	checkf(false, TEXT("Invalid template call for RandomVertIndex. Bug in Filter binding or Area Weighting binding. Contact code team."));
	return 0;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::RandomFilteredVertIndex<TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::None>>
	(FNDIRandomHelper& RandHelper, int32 Instance, FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	return RandHelper.RandRange(Instance, 0, Accessor.LODData->GetNumVertices() - 1);
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::RandomFilteredVertIndex<TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::SingleRegion>>
	(FNDIRandomHelper& RandHelper, int32 Instance, FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	int32 Idx = RandHelper.RandRange(Instance, 0, Accessor.SamplingRegionBuiltData->Vertices.Num() - 1);
	return Accessor.SamplingRegionBuiltData->Vertices[Idx];
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::RandomFilteredVertIndex<TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::MultiRegion>>
	(FNDIRandomHelper& RandHelper, int32 Instance, FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	int32 RegionIdx = RandHelper.RandRange(Instance, 0, InstData->SamplingRegionIndices.Num() - 1);
	const FSkeletalMeshSamplingInfo& SamplingInfo = InstData->Mesh->GetSamplingInfo();
	const FSkeletalMeshSamplingRegion& Region = SamplingInfo.GetRegion(InstData->SamplingRegionIndices[RegionIdx]);
	const FSkeletalMeshSamplingRegionBuiltData& RegionBuiltData = SamplingInfo.GetRegionBuiltData(InstData->SamplingRegionIndices[RegionIdx]);
	int32 Idx = RandHelper.RandRange(Instance, 0, RegionBuiltData.Vertices.Num() - 1);
	return RegionBuiltData.Vertices[Idx];
}

template<typename FilterMode>
void UNiagaraDataInterfaceSkeletalMesh::RandomFilteredVertex(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Vertex_Sample);

	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	FNDIRandomHelper RandHelper(Context);
	checkfSlow(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());	

	VectorVM::FExternalFuncRegisterHandler<int32> OutVert(Context);

	FSkeletalMeshAccessorHelper MeshAccessor;
	MeshAccessor.Init<FilterMode, TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::None>>(InstData);

	if (MeshAccessor.IsSkinAccessible())
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			RandHelper.GetAndAdvance();
			*OutVert.GetDestAndAdvance() = RandomFilteredVertIndex<FilterMode>(RandHelper, i, MeshAccessor, InstData);
		}
	}
	else
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			*OutVert.GetDestAndAdvance() = -1;
		}
	}
}

template<typename FilterMode>
void UNiagaraDataInterfaceSkeletalMesh::IsValidFilteredVertex(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Vertex_Sample);

	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	VectorVM::FExternalFuncInputHandler<int32> VertexParam(Context);

	checkfSlow(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());

	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> OutValid(Context);

	FSkeletalMeshAccessorHelper MeshAccessor;
	MeshAccessor.Init<FilterMode, TNDISkelMesh_AreaWeightingOff>(InstData);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		int32 RequestedIndex = VertexParam.Get();

		FNiagaraBool Value;
		Value.SetValue(MeshAccessor.IsSkinAccessible() && (int32)MeshAccessor.LODData->GetNumVertices() > RequestedIndex);
		*OutValid.GetDest() = Value;

		OutValid.Advance();
		VertexParam.Advance();
	}
}

//////////////////////////////////////////////////////////////////////////

template<typename FilterMode>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetFilteredVertexCount(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	checkf(false, TEXT("Invalid template call for GetFilteredVertexCount. Bug in Filter binding or Area Weighting binding. Contact code team."));
	return 0;
}

template<>
FORCEINLINE_DEBUGGABLE int32 UNiagaraDataInterfaceSkeletalMesh::GetFilteredVertexCount<TNDISkelMesh_FilterModeNone>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	return  Accessor.LODData->GetNumVertices();;
}

template<>
FORCEINLINE_DEBUGGABLE int32 UNiagaraDataInterfaceSkeletalMesh::GetFilteredVertexCount<TNDISkelMesh_FilterModeSingle>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	return Accessor.SamplingRegionBuiltData->Vertices.Num();
}

template<>
FORCEINLINE_DEBUGGABLE int32 UNiagaraDataInterfaceSkeletalMesh::GetFilteredVertexCount<TNDISkelMesh_FilterModeMulti>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	int32 NumVerts = 0;
	for (int32 RegionIdx = 0; RegionIdx < InstData->SamplingRegionIndices.Num(); RegionIdx++)
	{
		const FSkeletalMeshSamplingInfo& SamplingInfo = InstData->Mesh->GetSamplingInfo();
		const FSkeletalMeshSamplingRegion& Region = SamplingInfo.GetRegion(InstData->SamplingRegionIndices[RegionIdx]);
		const FSkeletalMeshSamplingRegionBuiltData& RegionBuiltData = SamplingInfo.GetRegionBuiltData(InstData->SamplingRegionIndices[RegionIdx]);
		NumVerts += RegionBuiltData.Vertices.Num();
	}
	return NumVerts;
}

template<typename FilterMode>
void UNiagaraDataInterfaceSkeletalMesh::GetFilteredVertexCount(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Vertex_Sample);
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	checkfSlow(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());

	VectorVM::FExternalFuncRegisterHandler<int32> OutVert(Context);

	FSkeletalMeshAccessorHelper MeshAccessor;
	MeshAccessor.Init<FilterMode, TNDISkelMesh_AreaWeightingOff>(InstData);

	if (MeshAccessor.IsSkinAccessible())
	{
		int32 Count = GetFilteredVertexCount<FilterMode>(MeshAccessor, InstData);
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			*OutVert.GetDestAndAdvance() = Count;
		}
	}
	else
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			*OutVert.GetDestAndAdvance() = 0;
		}
	}
}

//////////////////////////////////////////////////////////////////////////

template<typename FilterMode>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetFilteredVertexAt(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 FilteredIndex)
{
	checkf(false, TEXT("Invalid template call for GetFilteredVertexAt. Bug in Filter binding. Contact code team."));
	return 0;
}

template<>
FORCEINLINE_DEBUGGABLE int32 UNiagaraDataInterfaceSkeletalMesh::GetFilteredVertexAt<TNDISkelMesh_FilterModeNone>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 FilteredIndex)
{
	return FilteredIndex;
}

template<>
FORCEINLINE_DEBUGGABLE int32 UNiagaraDataInterfaceSkeletalMesh::GetFilteredVertexAt<TNDISkelMesh_FilterModeSingle>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 FilteredIndex)
{
	int32 MaxIdx = Accessor.SamplingRegionBuiltData->Vertices.Num() - 1;
	FilteredIndex = FMath::Min(FilteredIndex, MaxIdx);
	return Accessor.SamplingRegionBuiltData->Vertices[FilteredIndex];
}

template<>
FORCEINLINE_DEBUGGABLE int32 UNiagaraDataInterfaceSkeletalMesh::GetFilteredVertexAt<TNDISkelMesh_FilterModeMulti>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 FilteredIndex)
{
	for (int32 RegionIdx = 0; RegionIdx < InstData->SamplingRegionIndices.Num(); RegionIdx++)
	{
		const FSkeletalMeshSamplingInfo& SamplingInfo = InstData->Mesh->GetSamplingInfo();
		const FSkeletalMeshSamplingRegion& Region = SamplingInfo.GetRegion(InstData->SamplingRegionIndices[RegionIdx]);
		const FSkeletalMeshSamplingRegionBuiltData& RegionBuiltData = SamplingInfo.GetRegionBuiltData(InstData->SamplingRegionIndices[RegionIdx]);
		if (FilteredIndex < RegionBuiltData.Vertices.Num())
		{
			return RegionBuiltData.Vertices[FilteredIndex];
		}

		FilteredIndex -= RegionBuiltData.Vertices.Num();
	}
	return 0;
}

template<typename FilterMode>
void UNiagaraDataInterfaceSkeletalMesh::GetFilteredVertexAt(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Vertex_Sample);

	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	VectorVM::FExternalFuncInputHandler<int32> FilteredVertexParam(Context);
	checkfSlow(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());

	VectorVM::FExternalFuncRegisterHandler<int32> OutVert(Context);

	FSkeletalMeshAccessorHelper Accessor;
	Accessor.Init<FilterMode, TNDISkelMesh_AreaWeightingOff>(InstData);

	if (Accessor.IsSkinAccessible())
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			int32 FilteredVert = FilteredVertexParam.GetAndAdvance();
			int32 RealIdx = 0;
			RealIdx = GetFilteredVertexAt<FilterMode>(Accessor, InstData, FilteredVert);
			*OutVert.GetDestAndAdvance() = RealIdx;
		}
	}
	else
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			*OutVert.GetDestAndAdvance() = -1;
		}
	}
}

//////////////////////////////////////////////////////////////////////////

void UNiagaraDataInterfaceSkeletalMesh::GetVertexColor(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Vertex_Sample);
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	VectorVM::FExternalFuncInputHandler<int32> VertParam(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutColorR(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutColorG(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutColorB(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutColorA(Context);

	USkeletalMeshComponent* Comp = Cast<USkeletalMeshComponent>(InstData->Component.Get());
	FSkinWeightVertexBuffer* SkinWeightBuffer;
	FSkeletalMeshLODRenderData* LODData = InstData->GetLODRenderDataAndSkinWeights(SkinWeightBuffer);
	check(LODData);
	const FColorVertexBuffer& Colors = LODData->StaticVertexBuffers.ColorVertexBuffer;
	checkfSlow(Colors.GetNumVertices() != 0, TEXT("Trying to access vertex colors from mesh without any."));

	FMultiSizeIndexContainer& Indices = LODData->MultiSizeIndexContainer;
	const FRawStaticIndexBuffer16or32Interface* IndexBuffer = Indices.GetIndexBuffer();
	int32 VertMax = LODData->GetNumVertices();
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		int32 Vertex = VertParam.GetAndAdvance();
		Vertex = FMath::Min(VertMax, Vertex);

		FLinearColor Color = Colors.VertexColor(Vertex).ReinterpretAsLinear();

		*OutColorR.GetDestAndAdvance() = Color.R;
		*OutColorG.GetDestAndAdvance() = Color.G;
		*OutColorB.GetDestAndAdvance() = Color.B;
		*OutColorA.GetDestAndAdvance() = Color.A;
	}
}

void UNiagaraDataInterfaceSkeletalMesh::GetVertexColorFallback(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	VectorVM::FExternalFuncInputHandler<int32> VertParam(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutColorR(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutColorG(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutColorB(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutColorA(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*OutColorR.GetDestAndAdvance() = 1.0f;
		*OutColorG.GetDestAndAdvance() = 1.0f;
		*OutColorB.GetDestAndAdvance() = 1.0f;
		*OutColorA.GetDestAndAdvance() = 1.0f;
	}
}

template<typename VertexAccessorType>
void UNiagaraDataInterfaceSkeletalMesh::GetVertexUV(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Vertex_Sample);
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	VertexAccessorType VertAccessor;
	VectorVM::FExternalFuncInputHandler<int32> VertParam(Context);
	VectorVM::FExternalFuncInputHandler<int32> UVSetParam(Context);

	checkfSlow(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());
	checkfSlow(InstData->Mesh, TEXT("Skeletal Mesh Interface has invalid mesh. %s"), *GetPathName());

	VectorVM::FExternalFuncRegisterHandler<float> OutUVX(Context);	VectorVM::FExternalFuncRegisterHandler<float> OutUVY(Context);

	USkeletalMeshComponent* Comp = Cast<USkeletalMeshComponent>(InstData->Component.Get());
	FSkinWeightVertexBuffer* SkinWeightBuffer;
	FSkeletalMeshLODRenderData* LODData = InstData->GetLODRenderDataAndSkinWeights(SkinWeightBuffer);
	check(LODData);

	FMultiSizeIndexContainer& Indices = LODData->MultiSizeIndexContainer;
	FRawStaticIndexBuffer16or32Interface* IndexBuffer = Indices.GetIndexBuffer();
	const int32 VertMax = LODData->GetNumVertices() - 1;
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		int32 Vert = VertParam.GetAndAdvance();
		Vert = FMath::Min(Vert, VertMax);

		int32 UVSet = UVSetParam.GetAndAdvance();
		FVector2D UV = VertAccessor.GetVertexUV(LODData, Vert, UVSet);
		
		*OutUVX.GetDestAndAdvance() = UV.X;
		*OutUVY.GetDestAndAdvance() = UV.Y;
	}
}

// Stub specialization for no valid mesh data on the data interface
template<>
void UNiagaraDataInterfaceSkeletalMesh::GetVertexUV<FSkelMeshVertexAccessorNoop>(FVectorVMContext& Context)
{
	VectorVM::FExternalFuncInputHandler<int32> VertParam(Context);
	VectorVM::FExternalFuncInputHandler<int32> UVSetParam(Context);
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutUVX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutUVY(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*OutUVX.GetDestAndAdvance() = 0.0f;
		*OutUVY.GetDestAndAdvance() = 0.0f;
	}
}

struct FGetVertexSkinnedDataOutputHandler
{
	FGetVertexSkinnedDataOutputHandler(FVectorVMContext& Context)
		: PosX(Context), PosY(Context), PosZ(Context)
		, VelX(Context), VelY(Context), VelZ(Context)
		, TangentZX(Context), TangentZY(Context), TangentZZ(Context)
		, TangentYX(Context), TangentYY(Context), TangentYZ(Context)
		, TangentXX(Context), TangentXY(Context), TangentXZ(Context)
		, bNeedsPosition(PosX.IsValid() || PosY.IsValid() || PosZ.IsValid())
		, bNeedsVelocity(VelX.IsValid() || VelY.IsValid() || VelZ.IsValid())
		, bNeedsTangentX(TangentXX.IsValid() || TangentXY.IsValid() || TangentXZ.IsValid())
		, bNeedsTangentY(TangentYX.IsValid() || TangentYY.IsValid() || TangentYZ.IsValid())
		, bNeedsTangentZ(TangentZX.IsValid() || TangentZY.IsValid() || TangentZZ.IsValid())
	{
	}

	VectorVM::FExternalFuncRegisterHandler<float> PosX; VectorVM::FExternalFuncRegisterHandler<float> PosY; VectorVM::FExternalFuncRegisterHandler<float> PosZ;
	VectorVM::FExternalFuncRegisterHandler<float> VelX; VectorVM::FExternalFuncRegisterHandler<float> VelY; VectorVM::FExternalFuncRegisterHandler<float> VelZ;
	VectorVM::FExternalFuncRegisterHandler<float> TangentZX; VectorVM::FExternalFuncRegisterHandler<float> TangentZY; VectorVM::FExternalFuncRegisterHandler<float> TangentZZ;
	VectorVM::FExternalFuncRegisterHandler<float> TangentYX; VectorVM::FExternalFuncRegisterHandler<float> TangentYY; VectorVM::FExternalFuncRegisterHandler<float> TangentYZ;
	VectorVM::FExternalFuncRegisterHandler<float> TangentXX; VectorVM::FExternalFuncRegisterHandler<float> TangentXY; VectorVM::FExternalFuncRegisterHandler<float> TangentXZ;

	const bool bNeedsPosition;
	const bool bNeedsVelocity;
	const bool bNeedsTangentX;
	const bool bNeedsTangentY;
	const bool bNeedsTangentZ;

	FORCEINLINE void SetPosition(FVector Position)
	{
		*PosX.GetDestAndAdvance() = Position.X;
		*PosY.GetDestAndAdvance() = Position.Y;
		*PosZ.GetDestAndAdvance() = Position.Z;
	}

	FORCEINLINE void SetVelocity(FVector Velocity)
	{
		*VelX.GetDestAndAdvance() = Velocity.X;
		*VelY.GetDestAndAdvance() = Velocity.Y;
		*VelZ.GetDestAndAdvance() = Velocity.Z;
	}

	FORCEINLINE void SetTangentX(FVector TangentX)
	{
		*TangentXX.GetDestAndAdvance() = TangentX.X;
		*TangentXY.GetDestAndAdvance() = TangentX.Y;
		*TangentXZ.GetDestAndAdvance() = TangentX.Z;
	}

	FORCEINLINE void SetTangentY(FVector TangentY)
	{
		*TangentYX.GetDestAndAdvance() = TangentY.X;
		*TangentYY.GetDestAndAdvance() = TangentY.Y;
		*TangentYZ.GetDestAndAdvance() = TangentY.Z;
	}

	FORCEINLINE void SetTangentZ(FVector TangentZ)
	{
		*TangentZX.GetDestAndAdvance() = TangentZ.X;
		*TangentZY.GetDestAndAdvance() = TangentZ.Y;
		*TangentZZ.GetDestAndAdvance() = TangentZ.Z;
	}
};

template<typename SkinningHandlerType, typename TransformHandlerType, typename VertexAccessorType>
void UNiagaraDataInterfaceSkeletalMesh::GetVertexSkinnedData(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Vertex_Sample);
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);

	SkinningHandlerType SkinningHandler;
	TransformHandlerType TransformHandler;
	VectorVM::FExternalFuncInputHandler<int32> VertParam(Context);

	checkfSlow(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());

	const FMatrix& Transform = InstData->Transform;
	const FMatrix& PrevTransform = InstData->PrevTransform;

	FGetVertexSkinnedDataOutputHandler Output(Context);

	FSkeletalMeshAccessorHelper Accessor;
	Accessor.Init<TNDISkelMesh_FilterModeNone, TNDISkelMesh_AreaWeightingOff>(InstData);
	const int32 VertMax = Accessor.IsSkinAccessible() ? Accessor.LODData->GetNumVertices() : 0;
	const float InvDt = 1.0f / InstData->DeltaSeconds;
	const bool bNeedsTangentBasis = Output.bNeedsTangentX || Output.bNeedsTangentY || Output.bNeedsTangentZ;

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		const int32 Vertex = FMath::Min(VertParam.GetAndAdvance(), VertMax);
		
		FVector Pos = FVector::ZeroVector;
		if (Output.bNeedsPosition || Output.bNeedsVelocity)
		{
			Pos = SkinningHandler.GetSkinnedVertexPosition(Accessor, Vertex);
			TransformHandler.TransformPosition(Pos, Transform);
			Output.SetPosition(Pos);
		}

		if (Output.bNeedsVelocity)
		{
			FVector Prev = SkinningHandler.GetSkinnedVertexPreviousPosition(Accessor, Vertex);
			TransformHandler.TransformPosition(Prev, PrevTransform);
			const FVector Velocity = (Pos - Prev) * InvDt;
			Output.SetVelocity(Velocity);
		}

		if (bNeedsTangentBasis)
		{
			FVector TangentX = FVector::ZeroVector;
			FVector TangentZ = FVector::ZeroVector;
			SkinningHandler.GetSkinnedTangentBasis(Accessor, Vertex, TangentX, TangentZ);

			if (Output.bNeedsTangentX)
			{
				TransformHandler.TransformVector(TangentX, Transform);
				Output.SetTangentX(TangentX);
			}

			if (Output.bNeedsTangentY)
			{
				Output.SetTangentY((TangentX ^ TangentZ).GetSafeNormal());
			}

			if (Output.bNeedsTangentZ)
			{
				TransformHandler.TransformVector(TangentZ, Transform);
				Output.SetTangentZ(TangentZ);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
