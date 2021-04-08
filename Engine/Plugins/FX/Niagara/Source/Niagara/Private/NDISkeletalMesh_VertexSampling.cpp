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
		TSkinningModeBinder<TNDIExplicitBinder<FNDITransformHandlerNoop, TVertexAccessorBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetVertexSkinnedData)>>>::BindCheckCPUAccess(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetSkinnedVertexDataWSName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 15);
		TSkinningModeBinder<TNDIExplicitBinder<FNDITransformHandler, TVertexAccessorBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetVertexSkinnedData)>>>::BindCheckCPUAccess(this, BindingInfo, InstanceData, OutFunc);
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
	FNDIInputParam<int32> VertexParam(Context);
	FNDIOutputParam<FNiagaraBool> OutValid(Context);

	FSkeletalMeshAccessorHelper MeshAccessor;
	MeshAccessor.Init<TNDISkelMesh_FilterModeNone, TNDISkelMesh_AreaWeightingOff>(InstData);

	const int32 MaxVertex = MeshAccessor.IsLODAccessible() ? MeshAccessor.LODData->GetNumVertices() : 0;
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		const int32 VertexIndex = VertexParam.GetAndAdvance();
		OutValid.SetAndAdvance(VertexIndex < MaxVertex);
	}
}

void UNiagaraDataInterfaceSkeletalMesh::RandomVertex(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Vertex_Sample);

	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	FNDIRandomHelper RandHelper(Context);
	FNDIOutputParam<int32> OutVertex(Context);

	FSkeletalMeshAccessorHelper MeshAccessor;
	MeshAccessor.Init<TNDISkelMesh_FilterModeNone, TNDISkelMesh_AreaWeightingOff>(InstData);

	const int32 MaxVertex = MeshAccessor.IsLODAccessible() ? MeshAccessor.LODData->GetNumVertices() - 1 : -1;
	if (MaxVertex >= 0)
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			RandHelper.GetAndAdvance();
			OutVertex.SetAndAdvance(RandHelper.RandRange(i, 0, MaxVertex));
		}
	}
	else
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			OutVertex.SetAndAdvance(-1);
		}
	}
}

void UNiagaraDataInterfaceSkeletalMesh::GetVertexCount(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Vertex_Sample);

	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	FNDIOutputParam<int32> OutVertexCount(Context);

	FSkeletalMeshAccessorHelper MeshAccessor;
	MeshAccessor.Init<TNDISkelMesh_FilterModeNone, TNDISkelMesh_AreaWeightingOff>(InstData);

	const int32 MaxVertex = MeshAccessor.IsLODAccessible() ? MeshAccessor.LODData->GetNumVertices() : 0;
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutVertexCount.SetAndAdvance(MaxVertex);
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
	USkeletalMesh* SkelMesh = Accessor.Mesh;
	check(SkelMesh);

	int32 RegionIdx = RandHelper.RandRange(Instance, 0, InstData->SamplingRegionIndices.Num() - 1);
	const FSkeletalMeshSamplingInfo& SamplingInfo = SkelMesh->GetSamplingInfo();
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

	FNDIOutputParam<int32> OutVert(Context);

	FSkeletalMeshAccessorHelper MeshAccessor;
	MeshAccessor.Init<FilterMode, TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::None>>(InstData);

	if (MeshAccessor.IsLODAccessible())
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			RandHelper.GetAndAdvance();
			OutVert.SetAndAdvance(RandomFilteredVertIndex<FilterMode>(RandHelper, i, MeshAccessor, InstData));
		}
	}
	else
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			OutVert.SetAndAdvance(-1);
		}
	}
}

template<typename FilterMode>
void UNiagaraDataInterfaceSkeletalMesh::IsValidFilteredVertex(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Vertex_Sample);

	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	FNDIInputParam<int32> VertexParam(Context);

	checkfSlow(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());

	FNDIOutputParam<FNiagaraBool> OutValid(Context);

	FSkeletalMeshAccessorHelper MeshAccessor;
	MeshAccessor.Init<FilterMode, TNDISkelMesh_AreaWeightingOff>(InstData);

	const int32 MaxVertex = MeshAccessor.IsLODAccessible() ? MeshAccessor.LODData->GetNumVertices() : 0;

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		const int32 RequestedIndex = VertexParam.GetAndAdvance();
		OutValid.SetAndAdvance(RequestedIndex < MaxVertex);
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
	USkeletalMesh* SkelMesh = Accessor.Mesh;
	check(SkelMesh);

	int32 NumVerts = 0;
	for (int32 RegionIdx = 0; RegionIdx < InstData->SamplingRegionIndices.Num(); RegionIdx++)
	{
		const FSkeletalMeshSamplingInfo& SamplingInfo = SkelMesh->GetSamplingInfo();
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

	FNDIOutputParam<int32> OutVert(Context);

	FSkeletalMeshAccessorHelper MeshAccessor;
	MeshAccessor.Init<FilterMode, TNDISkelMesh_AreaWeightingOff>(InstData);

	const int32 Count = MeshAccessor.IsLODAccessible() ? GetFilteredVertexCount<FilterMode>(MeshAccessor, InstData) : 0;
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutVert.SetAndAdvance(Count);
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
	USkeletalMesh* SkelMesh = Accessor.Mesh;
	check(SkelMesh);

	for (int32 RegionIdx = 0; RegionIdx < InstData->SamplingRegionIndices.Num(); RegionIdx++)
	{
		const FSkeletalMeshSamplingInfo& SamplingInfo = SkelMesh->GetSamplingInfo();
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
	FNDIInputParam<int32> FilteredVertexParam(Context);
	checkfSlow(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());

	FNDIOutputParam<int32> OutVert(Context);

	FSkeletalMeshAccessorHelper Accessor;
	Accessor.Init<FilterMode, TNDISkelMesh_AreaWeightingOff>(InstData);

	if (Accessor.IsLODAccessible())
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			const int32 FilteredVert = FilteredVertexParam.GetAndAdvance();
			const int32 RealIdx = GetFilteredVertexAt<FilterMode>(Accessor, InstData, FilteredVert);
			OutVert.SetAndAdvance(RealIdx);
		}
	}
	else
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			OutVert.SetAndAdvance(-1);
		}
	}
}

//////////////////////////////////////////////////////////////////////////

void UNiagaraDataInterfaceSkeletalMesh::GetVertexColor(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Vertex_Sample);
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	FNDIInputParam<int32> VertParam(Context);

	FNDIOutputParam<FLinearColor> OutColor(Context);

	USkeletalMeshComponent* Comp = Cast<USkeletalMeshComponent>(InstData->SceneComponent.Get());
	if ( const FSkeletalMeshLODRenderData* LODData = InstData->CachedLODData )
	{
		const FColorVertexBuffer& Colors = LODData->StaticVertexBuffers.ColorVertexBuffer;
		checkfSlow(Colors.GetNumVertices() != 0, TEXT("Trying to access vertex colors from mesh without any."));

		const FMultiSizeIndexContainer& Indices = LODData->MultiSizeIndexContainer;
		const FRawStaticIndexBuffer16or32Interface* IndexBuffer = Indices.GetIndexBuffer();
		const int32 VertMax = LODData->GetNumVertices() - 1;
		if ( VertMax >= 0 )
		{
			for (int32 i = 0; i < Context.NumInstances; ++i)
			{
				int32 Vertex = VertParam.GetAndAdvance();
				Vertex = FMath::Clamp(VertMax, 0, Vertex);

				OutColor.SetAndAdvance(Colors.VertexColor(Vertex).ReinterpretAsLinear());
			}
			// We are done
			return;
		}
	}

	// Fall though for bad data
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutColor.SetAndAdvance(FLinearColor::White);
	}
}

void UNiagaraDataInterfaceSkeletalMesh::GetVertexColorFallback(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	FNDIInputParam<int32> VertParam(Context);

	FNDIOutputParam<FLinearColor> OutColor(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutColor.SetAndAdvance(FLinearColor::White);
	}
}

template<typename VertexAccessorType>
void UNiagaraDataInterfaceSkeletalMesh::GetVertexUV(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Vertex_Sample);
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	VertexAccessorType VertAccessor;
	FNDIInputParam<int32> VertParam(Context);
	FNDIInputParam<int32> UVSetParam(Context);

	checkf(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());
	checkf(InstData->bMeshValid, TEXT("Skeletal Mesh Interface has invalid mesh. %s"), *GetPathName());

	FNDIOutputParam<FVector2D> OutUV(Context);

	USkeletalMeshComponent* Comp = Cast<USkeletalMeshComponent>(InstData->SceneComponent.Get());
	if ( const FSkeletalMeshLODRenderData* LODData = InstData->CachedLODData )
	{
		const FMultiSizeIndexContainer& Indices = LODData->MultiSizeIndexContainer;
		const FRawStaticIndexBuffer16or32Interface* IndexBuffer = Indices.GetIndexBuffer();
		const int32 VertMax = LODData->GetNumVertices() - 1;
		if (VertMax >= 0)
		{
			for (int32 i = 0; i < Context.NumInstances; ++i)
			{
				const int32 Vert = FMath::Clamp(VertParam.GetAndAdvance(), 0, VertMax);
				const int32 UVSet = UVSetParam.GetAndAdvance();
				const FVector2D UV = VertAccessor.GetVertexUV(LODData, Vert, UVSet);
				OutUV.SetAndAdvance(UV);
			}
			// We are done
			return;
		}
	}

	// Fall though for bad data
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutUV.SetAndAdvance(FVector2D::ZeroVector);
	}
}

// Stub specialization for no valid mesh data on the data interface
template<>
void UNiagaraDataInterfaceSkeletalMesh::GetVertexUV<FSkelMeshVertexAccessorNoop>(FVectorVMContext& Context)
{
	FNDIInputParam<int32> VertParam(Context);
	FNDIInputParam<int32> UVSetParam(Context);
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);

	FNDIOutputParam<FVector2D> OutUV(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutUV.SetAndAdvance(FVector2D::ZeroVector);
	}
}

struct FGetVertexSkinnedDataOutputHandler
{
	FGetVertexSkinnedDataOutputHandler(FVectorVMContext& Context)
		: Position(Context)
		, Velocity(Context)
		, TangentZ(Context)
		, TangentY(Context)
		, TangentX(Context)
		, bNeedsPosition(Position.IsValid())
		, bNeedsVelocity(Velocity.IsValid())
		, bNeedsTangentX(TangentX.IsValid())
		, bNeedsTangentY(TangentY.IsValid())
		, bNeedsTangentZ(TangentZ.IsValid())
	{
	}
	FNDIOutputParam<FVector> Position;
	FNDIOutputParam<FVector> Velocity;
	FNDIOutputParam<FVector> TangentZ;
	FNDIOutputParam<FVector> TangentY;
	FNDIOutputParam<FVector> TangentX;

	const bool bNeedsPosition;
	const bool bNeedsVelocity;
	const bool bNeedsTangentX;
	const bool bNeedsTangentY;
	const bool bNeedsTangentZ;
};

template<typename SkinningHandlerType, typename TransformHandlerType, typename VertexAccessorType>
void UNiagaraDataInterfaceSkeletalMesh::GetVertexSkinnedData(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Vertex_Sample);
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);

	SkinningHandlerType SkinningHandler;
	TransformHandlerType TransformHandler;
	FNDIInputParam<int32> VertParam(Context);

	checkfSlow(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());

	const FMatrix& Transform = InstData->Transform;
	const FMatrix& PrevTransform = InstData->PrevTransform;

	FGetVertexSkinnedDataOutputHandler Output(Context);

	FSkeletalMeshAccessorHelper Accessor;
	Accessor.Init<TNDISkelMesh_FilterModeNone, TNDISkelMesh_AreaWeightingOff>(InstData);
	if ( Accessor.IsLODAccessible() )
	{
		const int32 VertMax = Accessor.LODData->GetNumVertices() - 1;
		if ( VertMax >= 0 )
		{
			const float InvDt = 1.0f / InstData->DeltaSeconds;
			const bool bNeedsTangentBasis = Output.bNeedsTangentX || Output.bNeedsTangentY || Output.bNeedsTangentZ;

			for (int32 i = 0; i < Context.NumInstances; ++i)
			{
				const int32 Vertex = FMath::Clamp(VertParam.GetAndAdvance(), 0, VertMax);

				FVector Pos = FVector::ZeroVector;
				if (Output.bNeedsPosition || Output.bNeedsVelocity)
				{
					Pos = SkinningHandler.GetSkinnedVertexPosition(Accessor, Vertex);
					TransformHandler.TransformPosition(Pos, Transform);
					Output.Position.SetAndAdvance(Pos);
				}

				if (Output.bNeedsVelocity)
				{
					FVector Prev = SkinningHandler.GetSkinnedVertexPreviousPosition(Accessor, Vertex);
					TransformHandler.TransformPosition(Prev, PrevTransform);
					const FVector Velocity = (Pos - Prev) * InvDt;
					Output.Velocity.SetAndAdvance(Velocity);
				}

				if (bNeedsTangentBasis)
				{
					FVector TangentX = FVector::ZeroVector;
					FVector TangentY = FVector::ZeroVector;
					FVector TangentZ = FVector::ZeroVector;
					SkinningHandler.GetSkinnedTangentBasis(Accessor, Vertex, TangentX, TangentY, TangentZ);

					if (Output.bNeedsTangentX)
					{
						TransformHandler.TransformVector(TangentX, Transform);
						Output.TangentX.SetAndAdvance(TangentX);
					}

					if (Output.bNeedsTangentY)
					{
						TransformHandler.TransformVector(TangentY, Transform);
						Output.TangentY.SetAndAdvance(TangentY);
					}

					if (Output.bNeedsTangentZ)
					{
						TransformHandler.TransformVector(TangentZ, Transform);
						Output.TangentZ.SetAndAdvance(TangentZ);
					}
				}
			}
			// We are done
			return;
		}
	}

	// Fall though for bad data
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		FVector Position = FVector::ZeroVector;
		if (Output.bNeedsPosition || Output.bNeedsVelocity)
		{
			TransformHandler.TransformPosition(Position, Transform);
		}
		Output.Position.SetAndAdvance(Position);
		Output.Velocity.SetAndAdvance(FVector::ZeroVector);
		Output.TangentX.SetAndAdvance(FVector::XAxisVector);
		Output.TangentY.SetAndAdvance(FVector::YAxisVector);
		Output.TangentZ.SetAndAdvance(FVector::ZAxisVector);
	}
}

#undef LOCTEXT_NAMESPACE
