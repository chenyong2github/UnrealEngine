// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceSkeletalMesh.h"
#include "NiagaraComponent.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "SkeletalMeshTypes.h"
#include "NiagaraStats.h"
#include "Templates/AlignmentTemplates.h"
#include "NDISkeletalMeshCommon.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceSkeletalMesh_TriangleSampling"

DECLARE_CYCLE_STAT(TEXT("Skel Mesh Sampling"), STAT_NiagaraSkel_Sample, STATGROUP_Niagara);

//Final binders for all static mesh interface functions.
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, RandomTriCoord);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordSkinnedData);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordSkinnedDataFallback);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordColor);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordColorFallback);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordUV);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, IsValidTriCoord);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetFilteredTriangleCount);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetFilteredTriangleAt);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordVertices);

const FName FSkeletalMeshInterfaceHelper::RandomTriCoordName("RandomTriCoord");
const FName FSkeletalMeshInterfaceHelper::IsValidTriCoordName("IsValidTriCoord");
const FName FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataName("GetSkinnedTriangleData");
const FName FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataWSName("GetSkinnedTriangleDataWS");
const FName FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataInterpName("GetSkinnedTriangleDataInterpolated");
const FName FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataWSInterpName("GetSkinnedTriangleDataWSInterpolated");
const FName FSkeletalMeshInterfaceHelper::GetTriColorName("GetTriColor");
const FName FSkeletalMeshInterfaceHelper::GetTriUVName("GetTriUV");
const FName FSkeletalMeshInterfaceHelper::GetTriCoordVerticesName("GetTriCoordVertices");
const FName FSkeletalMeshInterfaceHelper::RandomTriangleName("RandomTriangle");
const FName FSkeletalMeshInterfaceHelper::GetTriangleCountName("GetTriangleCount");
const FName FSkeletalMeshInterfaceHelper::RandomFilteredTriangleName("RandomFilteredTriangle");
const FName FSkeletalMeshInterfaceHelper::GetFilteredTriangleCountName("GetFilteredTriangleCount");
const FName FSkeletalMeshInterfaceHelper::GetFilteredTriangleAtName("GetFilteredTriangle");

void UNiagaraDataInterfaceSkeletalMesh::GetTriangleSamplingFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	//-TODO: Remove / deprecate this function!
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::RandomTriCoordName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FNiagaraRandInfo::StaticStruct()), TEXT("RandomInfo")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::IsValidTriCoordName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsValid")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("IsValidDesc", "Determine if this tri coordinate's triangle index is valid for this mesh. Note that this only checks the mesh index buffer size and does not include any filtering settings.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Normal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Binormal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Tangent")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetOptionalSkinnedDataDesc", "Returns skinning dependant data for the pased MeshTriCoord in local space. All outputs are optional and you will incur zerp minimal cost if they are not connected.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataWSName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Normal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Binormal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Tangent")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetOptionalSkinnedDataWSDesc", "Returns skinning dependant data for the pased MeshTriCoord in world space. All outputs are optional and you will incur zerp minimal cost if they are not connected.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataInterpName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Interp")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Normal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Binormal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Tangent")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetSkinnedDataDesc", "Returns skinning dependant data for the pased MeshTriCoord in local space. Interpolates between previous and current frame. All outputs are optional and you will incur zerp minimal cost if they are not connected.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataWSInterpName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Interp")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Normal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Binormal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Tangent")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetSkinnedDataWSDesc", "Returns skinning dependant data for the pased MeshTriCoord in world space. Interpolates between previous and current frame. All outputs are optional and you will incur zerp minimal cost if they are not connected.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetTriColorName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Color")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetTriUVName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("UV Set")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("UV")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetTriCoordVerticesName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("TriangleIndex")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Vertex 0")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Vertex 1")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Vertex 2")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetTriCoordVetsName", "Takes the TriangleIndex from a MeshTriCoord and returns the vertices for that triangle.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::RandomTriangleName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FNiagaraRandInfo::StaticStruct()), TEXT("RandomInfo")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetTriangleCountName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Count")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::RandomFilteredTriangleName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FNiagaraRandInfo::StaticStruct()), TEXT("RandomInfo")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetFilteredTriangleCountName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Count")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetFilteredTriangleAtName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
}

void UNiagaraDataInterfaceSkeletalMesh::BindTriangleSamplingFunction(const FVMExternalFunctionBindingInfo& BindingInfo, FNDISkeletalMesh_InstanceData* InstanceData, FVMExternalFunction &OutFunc)
{
	using TInterpOff = TIntegralConstant<bool, false>;
	using TInterpOn = TIntegralConstant<bool, true>;

	if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::RandomTriCoordName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 4);
		TFilterModeBinder<TAreaWeightingModeBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, RandomTriCoord)>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::IsValidTriCoordName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 1);
		TFilterModeBinder<TAreaWeightingModeBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, IsValidTriCoord)>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 15);
		if (InstanceData->bAllowCPUMeshDataAccess)
		{
			TSkinningModeBinder<TNDIExplicitBinder<FNDITransformHandlerNoop, TVertexAccessorBinder<TNDIExplicitBinder<TInterpOff, NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordSkinnedData)>>>>::BindIgnoreCPUAccess(this, BindingInfo, InstanceData, OutFunc);
		}
		else
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordSkinnedDataFallback)::Bind<FNDITransformHandlerNoop, TInterpOff>(this, BindingInfo, InstanceData, OutFunc);
		}
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataWSName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 15);
		if (InstanceData->bAllowCPUMeshDataAccess)
		{
			TSkinningModeBinder<TNDIExplicitBinder<FNDITransformHandler, TVertexAccessorBinder<TNDIExplicitBinder<TIntegralConstant<bool, false>, NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordSkinnedData)>>>>::BindIgnoreCPUAccess(this, BindingInfo, InstanceData, OutFunc);
		}
		else
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordSkinnedDataFallback)::Bind<FNDITransformHandler, TInterpOff>(this, BindingInfo, InstanceData, OutFunc);
		}
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataInterpName)
	{
		check(BindingInfo.GetNumInputs() == 6 && BindingInfo.GetNumOutputs() == 15);
		if (InstanceData->bAllowCPUMeshDataAccess)
		{
			TSkinningModeBinder<TNDIExplicitBinder<FNDITransformHandlerNoop, TVertexAccessorBinder<TNDIExplicitBinder<TInterpOn, NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordSkinnedData)>>>>::BindIgnoreCPUAccess(this, BindingInfo, InstanceData, OutFunc);
		}
		else
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordSkinnedDataFallback)::Bind<FNDITransformHandlerNoop, TInterpOn>(this, BindingInfo, InstanceData, OutFunc);
		}
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataWSInterpName)
	{
		check(BindingInfo.GetNumInputs() == 6 && BindingInfo.GetNumOutputs() == 15);
		if (InstanceData->bAllowCPUMeshDataAccess)
		{
			TSkinningModeBinder<TNDIExplicitBinder< FNDITransformHandler, TVertexAccessorBinder<TNDIExplicitBinder<TIntegralConstant<bool, true>, NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordSkinnedData)>>>>::BindIgnoreCPUAccess(this, BindingInfo, InstanceData, OutFunc);
		}
		else
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordSkinnedDataFallback)::Bind<FNDITransformHandler, TInterpOn>(this, BindingInfo, InstanceData, OutFunc);
		}
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetTriColorName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 4);
		if (InstanceData->HasColorData())
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordColor)::Bind(this, OutFunc);
		}
		else
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordColorFallback)::Bind(this, OutFunc);
		}
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetTriUVName)
	{
		check(BindingInfo.GetNumInputs() == 6 && BindingInfo.GetNumOutputs() == 2);
		TVertexAccessorBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordUV)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetTriCoordVerticesName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 3);
		TSkinningModeBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetTriCoordVertices)>::BindCheckCPUAccess(this, BindingInfo, InstanceData, OutFunc);		
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::RandomTriangleName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 4);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->RandomTriangle(Context); });
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetTriangleCountName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->GetTriangleCount(Context); });
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::RandomFilteredTriangleName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 4);
		TFilterModeBinder<TAreaWeightingModeBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, RandomTriCoord)>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetFilteredTriangleCountName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		TFilterModeBinder<TAreaWeightingModeBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetFilteredTriangleCount)>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetFilteredTriangleAtName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 4);
		TFilterModeBinder<TAreaWeightingModeBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetFilteredTriangleAt)>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
}

template<typename FilterMode, typename AreaWeightingMode>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::RandomTriIndex(FNDIRandomHelper& RandHelper, FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 InstanceIndex)
{
	checkf(false, TEXT("Invalid template call for RandomTriIndex. Bug in Filter binding or Area Weighting binding. Contact code team."));
	return 0;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::RandomTriIndex<TNDISkelMesh_FilterModeNone, TNDISkelMesh_AreaWeightingOff>
	(FNDIRandomHelper& RandHelper, FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 InstanceIndex)
{
	int32 SecIdx = RandHelper.RandRange(InstanceIndex, 0, Accessor.LODData->RenderSections.Num() - 1);
	//int32 SecIdx = RandStream.RandRange(0, Accessor.LODData->RenderSections.Num() - 1);
	const FSkelMeshRenderSection& Sec = Accessor.LODData->RenderSections[SecIdx];
	int32 Tri = RandHelper.RandRange(InstanceIndex, 0, Sec.NumTriangles - 1);
	return (Sec.BaseIndex / 3) + Tri;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::RandomTriIndex<TNDISkelMesh_FilterModeNone, TNDISkelMesh_AreaWeightingOn>
	(FNDIRandomHelper& RandHelper, FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 InstanceIndex)
{
	check(Accessor.Mesh);
	const FSkeletalMeshSamplingInfo& SamplingInfo = Accessor.Mesh->GetSamplingInfo();
	const FSkeletalMeshSamplingLODBuiltData& WholeMeshBuiltData = SamplingInfo.GetWholeMeshLODBuiltData(InstData->GetLODIndex());
	int32 TriIdx = WholeMeshBuiltData.AreaWeightedTriangleSampler.GetEntryIndex(RandHelper.Rand(InstanceIndex), RandHelper.Rand(InstanceIndex));
	return TriIdx;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::RandomTriIndex<TNDISkelMesh_FilterModeSingle, TNDISkelMesh_AreaWeightingOff>
	(FNDIRandomHelper& RandHelper, FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 InstanceIndex)
{
	int32 Idx = RandHelper.RandRange(InstanceIndex, 0, Accessor.SamplingRegionBuiltData->TriangleIndices.Num() - 1);
	return Accessor.SamplingRegionBuiltData->TriangleIndices[Idx] / 3;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::RandomTriIndex<TNDISkelMesh_FilterModeSingle, TNDISkelMesh_AreaWeightingOn>
	(FNDIRandomHelper& RandHelper, FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 InstanceIndex)
{
	int32 Idx = Accessor.SamplingRegionBuiltData->AreaWeightedSampler.GetEntryIndex(RandHelper.Rand(InstanceIndex), RandHelper.Rand(InstanceIndex));
	return Accessor.SamplingRegionBuiltData->TriangleIndices[Idx] / 3;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::RandomTriIndex<TNDISkelMesh_FilterModeMulti, TNDISkelMesh_AreaWeightingOff>
	(FNDIRandomHelper& RandHelper, FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 InstanceIndex)
{
	check(Accessor.Mesh);
	int32 RegionIdx = RandHelper.RandRange(InstanceIndex, 0, InstData->SamplingRegionIndices.Num() - 1);
	const FSkeletalMeshSamplingInfo& SamplingInfo = Accessor.Mesh->GetSamplingInfo();
	const FSkeletalMeshSamplingRegion& Region = SamplingInfo.GetRegion(InstData->SamplingRegionIndices[RegionIdx]);
	const FSkeletalMeshSamplingRegionBuiltData& RegionBuiltData = SamplingInfo.GetRegionBuiltData(InstData->SamplingRegionIndices[RegionIdx]);
	int32 Idx = RandHelper.RandRange(InstanceIndex, 0, RegionBuiltData.TriangleIndices.Num() - 1);
	return RegionBuiltData.TriangleIndices[Idx] / 3;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::RandomTriIndex<TNDISkelMesh_FilterModeMulti, TNDISkelMesh_AreaWeightingOn>
	(FNDIRandomHelper& RandHelper, FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 InstanceIndex)
{
	check(Accessor.Mesh);
	int32 RegionIdx = InstData->SamplingRegionAreaWeightedSampler.GetEntryIndex(RandHelper.Rand(InstanceIndex), RandHelper.Rand(InstanceIndex));
	const FSkeletalMeshSamplingInfo& SamplingInfo = Accessor.Mesh->GetSamplingInfo();
	const FSkeletalMeshSamplingRegion& Region = SamplingInfo.GetRegion(InstData->SamplingRegionIndices[RegionIdx]);
	const FSkeletalMeshSamplingRegionBuiltData& RegionBuiltData = SamplingInfo.GetRegionBuiltData(InstData->SamplingRegionIndices[RegionIdx]);
	int32 Idx = RegionBuiltData.AreaWeightedSampler.GetEntryIndex(RandHelper.Rand(InstanceIndex), RandHelper.Rand(InstanceIndex));
	return RegionBuiltData.TriangleIndices[Idx] / 3;
}

template<typename FilterMode, typename AreaWeightingMode>
void UNiagaraDataInterfaceSkeletalMesh::RandomTriCoord(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Sample);

	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	//FNDIParameter<FNiagaraRandInfo> RandInfoParam(Context);
	FNDIRandomHelper RandHelper(Context);

	checkfSlow(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());

	FNDIOutputParam<int32> OutTri(Context);
	FNDIOutputParam<FVector> OutBary(Context);

	FSkeletalMeshAccessorHelper MeshAccessor;
	MeshAccessor.Init<FilterMode, AreaWeightingMode>(InstData);

	if (MeshAccessor.IsSkinAccessible())
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			RandHelper.GetAndAdvance();//We grab the rand info to a local value first so it can be used for multiple rand calls from the helper.
			OutTri.SetAndAdvance(RandomTriIndex<FilterMode, AreaWeightingMode>(RandHelper, MeshAccessor, InstData, i));
			OutBary.SetAndAdvance(RandomBarycentricCoord(Context.RandStream));
		}
	}
	else
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			OutTri.SetAndAdvance(-1);
			OutBary.SetAndAdvance(FVector::ZeroVector);
		}
	}
}

template<typename FilterMode, typename AreaWeightingMode>
void UNiagaraDataInterfaceSkeletalMesh::IsValidTriCoord(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Sample);

	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);

	VectorVM::FExternalFuncInputHandler<int32> TriParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryXParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryYParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryZParam(Context);

	checkfSlow(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());

	FNDIOutputParam<FNiagaraBool> OutValid(Context);

	FSkeletalMeshAccessorHelper MeshAccessor;
	MeshAccessor.Init<FilterMode, AreaWeightingMode>(InstData);

	if (MeshAccessor.IsSkinAccessible())
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			const int32 RequestedIndex = (TriParam.GetAndAdvance() * 3) + 2; // Get the last triangle index of the set
			OutValid.SetAndAdvance(MeshAccessor.IndexBuffer != nullptr && MeshAccessor.IndexBuffer->Num() > RequestedIndex);
		}
	}
	else
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			OutValid.SetAndAdvance(false);
		}
	}
}

//////////////////////////////////////////////////////////////////////////

void UNiagaraDataInterfaceSkeletalMesh::RandomTriangle(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Sample);

	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	FNDIRandomHelper RandHelper(Context);
	FNDIOutputParam<int32> OutTri(Context);
	FNDIOutputParam<FVector> OutBary(Context);

	FSkeletalMeshAccessorHelper MeshAccessor;
	MeshAccessor.Init<TIntegralConstant<int32, 0>, TIntegralConstant<int32, 0>>(InstData);

	if (!MeshAccessor.IsSkinAccessible())
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			OutTri.SetAndAdvance(-1);
			OutBary.SetAndAdvance(FVector::ZeroVector);
		}
		return;
	}

	//-TODO: AREA WEIGHTED
	USkeletalMesh* SkelMesh = MeshAccessor.Mesh;
	check(SkelMesh); // IsSkinAccessible should have ensured this
	const int32 LODIndex = InstData->GetLODIndex();
	const bool bAreaWeighted = SkelMesh->GetLODInfo(LODIndex)->bSupportUniformlyDistributedSampling;

	if (bAreaWeighted)
	{
		const FSkeletalMeshSamplingInfo& SamplingInfo = SkelMesh->GetSamplingInfo();
		const FSkeletalMeshSamplingLODBuiltData& WholeMeshBuiltData = SamplingInfo.GetWholeMeshLODBuiltData(InstData->GetLODIndex());
		if (WholeMeshBuiltData.AreaWeightedTriangleSampler.GetNumEntries() > 0)
		{
			for (int32 i = 0; i < Context.NumInstances; ++i)
			{
				RandHelper.GetAndAdvance();
				OutTri.SetAndAdvance(WholeMeshBuiltData.AreaWeightedTriangleSampler.GetEntryIndex(RandHelper.Rand(i), RandHelper.Rand(i)));
				OutBary.SetAndAdvance(RandHelper.RandomBarycentricCoord(i));
			}
			return;
		}
	}

	const int32 MaxTriangle = (MeshAccessor.IndexBuffer->Num() / 3) - 1;
	if (MaxTriangle >= 0)
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			RandHelper.GetAndAdvance();
			OutTri.SetAndAdvance(RandHelper.RandRange(i, 0, MaxTriangle));
			OutBary.SetAndAdvance(RandHelper.RandomBarycentricCoord(i));
		}
	}
	else
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			OutTri.SetAndAdvance(-1);
			OutBary.SetAndAdvance(FVector::ZeroVector);
		}
	}
}

void UNiagaraDataInterfaceSkeletalMesh::GetTriangleCount(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Sample);

	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	FNDIOutputParam<int32> OutCount(Context);

	FSkeletalMeshAccessorHelper MeshAccessor;
	MeshAccessor.Init<TIntegralConstant<int32, 0>, TIntegralConstant<int32, 0>>(InstData);
	
	const int32 NumTriangles = MeshAccessor.IsSkinAccessible() ? MeshAccessor.IndexBuffer->Num() / 3 : 0;
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutCount.SetAndAdvance(NumTriangles);
	}
}

template<typename FilterMode, typename AreaWeightingMode>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetFilteredTriangleCount(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	checkf(false, TEXT("Invalid template call for GetFilteredTriangleCount. Bug in Filter binding or Area Weighting binding. Contact code team."));
	return 0;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetFilteredTriangleCount<TNDISkelMesh_FilterModeNone, TNDISkelMesh_AreaWeightingOff>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	int32 NumTris = 0;
	for (int32 i = 0; i < Accessor.LODData->RenderSections.Num(); i++)
	{
		NumTris += Accessor.LODData->RenderSections[i].NumTriangles;
	}
	return NumTris;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetFilteredTriangleCount<TNDISkelMesh_FilterModeNone, TNDISkelMesh_AreaWeightingOn>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	check(Accessor.Mesh);
	const FSkeletalMeshSamplingInfo& SamplingInfo = Accessor.Mesh->GetSamplingInfo();
	const FSkeletalMeshSamplingLODBuiltData& WholeMeshBuiltData = SamplingInfo.GetWholeMeshLODBuiltData(InstData->GetLODIndex());
	return WholeMeshBuiltData.AreaWeightedTriangleSampler.GetNumEntries();
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetFilteredTriangleCount<TNDISkelMesh_FilterModeSingle, TNDISkelMesh_AreaWeightingOff>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	return Accessor.SamplingRegionBuiltData->TriangleIndices.Num();
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetFilteredTriangleCount<TNDISkelMesh_FilterModeSingle, TNDISkelMesh_AreaWeightingOn>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	return Accessor.SamplingRegionBuiltData->AreaWeightedSampler.GetNumEntries();
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetFilteredTriangleCount<TNDISkelMesh_FilterModeMulti, TNDISkelMesh_AreaWeightingOff>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	USkeletalMesh* SkelMesh = Accessor.Mesh;
	check(SkelMesh);
	int32 NumTris = 0;

	for (int32 RegionIdx = 0; RegionIdx < InstData->SamplingRegionIndices.Num(); RegionIdx++)
	{
		const FSkeletalMeshSamplingInfo& SamplingInfo = SkelMesh->GetSamplingInfo();
		const FSkeletalMeshSamplingRegion& Region = SamplingInfo.GetRegion(InstData->SamplingRegionIndices[RegionIdx]);
		const FSkeletalMeshSamplingRegionBuiltData& RegionBuiltData = SamplingInfo.GetRegionBuiltData(InstData->SamplingRegionIndices[RegionIdx]);
		NumTris += RegionBuiltData.TriangleIndices.Num();
	}
	return NumTris;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetFilteredTriangleCount<TNDISkelMesh_FilterModeMulti, TNDISkelMesh_AreaWeightingOn>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData)
{
	USkeletalMesh* SkelMesh = Accessor.Mesh;
	check(SkelMesh);
	int32 NumTris = 0;

	for (int32 RegionIdx = 0; RegionIdx < InstData->SamplingRegionIndices.Num(); RegionIdx++)
	{
		const FSkeletalMeshSamplingInfo& SamplingInfo = SkelMesh->GetSamplingInfo();
		const FSkeletalMeshSamplingRegion& Region = SamplingInfo.GetRegion(InstData->SamplingRegionIndices[RegionIdx]);
		const FSkeletalMeshSamplingRegionBuiltData& RegionBuiltData = SamplingInfo.GetRegionBuiltData(InstData->SamplingRegionIndices[RegionIdx]);
		NumTris += RegionBuiltData.TriangleIndices.Num();
	}
	return NumTris;
}

template<typename FilterMode, typename AreaWeightingMode>
void UNiagaraDataInterfaceSkeletalMesh::GetFilteredTriangleCount(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Sample);
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	checkfSlow(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());

	FNDIOutputParam<int32> OutTri(Context);

	FSkeletalMeshAccessorHelper MeshAccessor;
	MeshAccessor.Init<FilterMode, AreaWeightingMode>(InstData);

	int32 Count = MeshAccessor.IsSkinAccessible() ? GetFilteredTriangleCount<FilterMode, AreaWeightingMode>(MeshAccessor, InstData) : 0;
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutTri.SetAndAdvance(Count);
	}
}

//////////////////////////////////////////////////////////////////////////

template<typename FilterMode, typename AreaWeightingMode>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetFilteredTriangleAt(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 FilteredIndex)
{
	checkf(false, TEXT("Invalid template call for GetFilteredTriangleAt. Bug in Filter binding or Area Weighting binding. Contact code team."));
	return 0;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetFilteredTriangleAt<TNDISkelMesh_FilterModeNone, TNDISkelMesh_AreaWeightingOff>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 FilteredIndex)
{
	for (int32 i = 0; i < Accessor.LODData->RenderSections.Num(); i++)
	{
		if (Accessor.LODData->RenderSections[i].NumTriangles > (uint32)FilteredIndex)
		{
			const FSkelMeshRenderSection& Sec = Accessor.LODData->RenderSections[i];
			return Sec.BaseIndex + FilteredIndex;
		}
		FilteredIndex -= Accessor.LODData->RenderSections[i].NumTriangles;
	}
	return 0;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetFilteredTriangleAt<TNDISkelMesh_FilterModeNone, TNDISkelMesh_AreaWeightingOn>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 FilteredIndex)
{
	int32 TriIdx = FilteredIndex;
	return TriIdx;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetFilteredTriangleAt<TNDISkelMesh_FilterModeSingle, TNDISkelMesh_AreaWeightingOff>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 FilteredIndex)
{
	int32 MaxIdx = Accessor.SamplingRegionBuiltData->TriangleIndices.Num() - 1;
	FilteredIndex = FMath::Min(FilteredIndex, MaxIdx);
	return Accessor.SamplingRegionBuiltData->TriangleIndices[FilteredIndex] / 3;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetFilteredTriangleAt<TNDISkelMesh_FilterModeSingle, TNDISkelMesh_AreaWeightingOn>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 FilteredIndex)
{
	int32 Idx = FilteredIndex;
	int32 MaxIdx = Accessor.SamplingRegionBuiltData->TriangleIndices.Num() - 1;
	Idx = FMath::Min(Idx, MaxIdx);

	return Accessor.SamplingRegionBuiltData->TriangleIndices[Idx] / 3;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetFilteredTriangleAt<TNDISkelMesh_FilterModeMulti, TNDISkelMesh_AreaWeightingOff>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 FilteredIndex)
{
	USkeletalMesh* SkelMesh = Accessor.Mesh;
	check(SkelMesh);

	for (int32 RegionIdx = 0; RegionIdx < InstData->SamplingRegionIndices.Num(); RegionIdx++)
	{
		const FSkeletalMeshSamplingInfo& SamplingInfo = SkelMesh->GetSamplingInfo();
		const FSkeletalMeshSamplingRegion& Region = SamplingInfo.GetRegion(InstData->SamplingRegionIndices[RegionIdx]);
		const FSkeletalMeshSamplingRegionBuiltData& RegionBuiltData = SamplingInfo.GetRegionBuiltData(InstData->SamplingRegionIndices[RegionIdx]);
		if (FilteredIndex < RegionBuiltData.TriangleIndices.Num())
		{
			return RegionBuiltData.TriangleIndices[FilteredIndex] / 3;
		}

		FilteredIndex -= RegionBuiltData.TriangleIndices.Num();
	}
	return 0;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceSkeletalMesh::GetFilteredTriangleAt<TNDISkelMesh_FilterModeMulti, TNDISkelMesh_AreaWeightingOn>
	(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 FilteredIndex)
{
	USkeletalMesh* SkelMesh = Accessor.Mesh;
	check(SkelMesh);

	for (int32 RegionIdx = 0; RegionIdx < InstData->SamplingRegionIndices.Num(); RegionIdx++)
	{
		const FSkeletalMeshSamplingInfo& SamplingInfo = SkelMesh->GetSamplingInfo();
		const FSkeletalMeshSamplingRegion& Region = SamplingInfo.GetRegion(InstData->SamplingRegionIndices[RegionIdx]);
		const FSkeletalMeshSamplingRegionBuiltData& RegionBuiltData = SamplingInfo.GetRegionBuiltData(InstData->SamplingRegionIndices[RegionIdx]);
		if (FilteredIndex < RegionBuiltData.TriangleIndices.Num())
		{
			return RegionBuiltData.TriangleIndices[FilteredIndex] / 3;
		}
		FilteredIndex -= RegionBuiltData.TriangleIndices.Num();
	}
	return 0;
}

template<typename FilterMode, typename AreaWeightingMode>
void UNiagaraDataInterfaceSkeletalMesh::GetFilteredTriangleAt(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Sample);

	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	VectorVM::FExternalFuncInputHandler<int32> TriParam(Context);
	checkfSlow(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());

	FNDIOutputParam<int32> OutTri(Context);
	FNDIOutputParam<FVector> OutBary(Context);

	FSkeletalMeshAccessorHelper Accessor;
	Accessor.Init<FilterMode, AreaWeightingMode>(InstData);

	if (Accessor.IsSkinAccessible())
	{
		const FVector BaryCoord(1.0f / 3.0f);
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			int32 Tri = TriParam.GetAndAdvance();
			int32 RealIdx = 0;
			RealIdx = GetFilteredTriangleAt<FilterMode, AreaWeightingMode>(Accessor, InstData, Tri);

			const int32 TriMax = (Accessor.IndexBuffer->Num() / 3) - 1;
			RealIdx = FMath::Clamp(RealIdx, 0, TriMax);

			OutTri.SetAndAdvance(RealIdx);
			OutBary.SetAndAdvance(BaryCoord);
		}
	}
	else
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			OutTri.SetAndAdvance(-1);
			OutBary.SetAndAdvance(FVector::ZeroVector);
		}
	}
}

void UNiagaraDataInterfaceSkeletalMesh::GetTriCoordColor(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Sample);
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	FNDIInputParam<int32> TriParam(Context);
	FNDIInputParam<FVector> BaryParam(Context);

	FNDIOutputParam<FLinearColor> OutColor(Context);

	USkeletalMeshComponent* Comp = Cast<USkeletalMeshComponent>(InstData->SceneComponent.Get());
	const FSkeletalMeshLODRenderData* LODData = InstData->CachedLODData;
	check(LODData);
	const FColorVertexBuffer& Colors = LODData->StaticVertexBuffers.ColorVertexBuffer;
	checkfSlow(Colors.GetNumVertices() != 0, TEXT("Trying to access vertex colors from mesh without any."));

	const FMultiSizeIndexContainer& Indices = LODData->MultiSizeIndexContainer;
	const FRawStaticIndexBuffer16or32Interface* IndexBuffer = Indices.GetIndexBuffer();
	const int32 TriMax = (IndexBuffer->Num() / 3) - 1;
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		const int32 Tri = FMath::Clamp(TriParam.GetAndAdvance(), 0, TriMax) * 3;
		const int32 Idx0 = IndexBuffer->Get(Tri);
		const int32 Idx1 = IndexBuffer->Get(Tri + 1);
		const int32 Idx2 = IndexBuffer->Get(Tri + 2);

		FLinearColor Color = BarycentricInterpolate(BaryParam.GetAndAdvance(), Colors.VertexColor(Idx0).ReinterpretAsLinear(), Colors.VertexColor(Idx1).ReinterpretAsLinear(), Colors.VertexColor(Idx2).ReinterpretAsLinear());
		OutColor.SetAndAdvance(Color);
	}
}

// Where we determine we are sampling a skeletal mesh without tri color we bind to this fallback method 
void UNiagaraDataInterfaceSkeletalMesh::GetTriCoordColorFallback(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	FNDIInputParam<int32> TriParam(Context);
	FNDIInputParam<FVector> BaryParam(Context);

	FNDIOutputParam<FLinearColor> OutColor(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutColor.SetAndAdvance(FLinearColor::White);
	}
}

template<typename VertexAccessorType>
void UNiagaraDataInterfaceSkeletalMesh::GetTriCoordUV(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Sample);
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	VertexAccessorType VertAccessor;
	FNDIInputParam<int32> TriParam(Context);
	FNDIInputParam<FVector> BaryParam(Context);
	FNDIInputParam<int32> UVSetParam(Context);

	checkf(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());
	checkf(InstData->bMeshValid, TEXT("Skeletal Mesh Interface has invalid mesh. %s"), *GetPathName());

	FNDIOutputParam<FVector2D> OutUV(Context);

	USkeletalMeshComponent* Comp = Cast<USkeletalMeshComponent>(InstData->SceneComponent.Get());
	const FSkeletalMeshLODRenderData* LODData = InstData->CachedLODData;
	check(LODData);

	const FMultiSizeIndexContainer& Indices = LODData->MultiSizeIndexContainer;
	const FRawStaticIndexBuffer16or32Interface* IndexBuffer = Indices.GetIndexBuffer();
	const int32 TriMax = (IndexBuffer->Num() / 3) - 1;
	const int32 UVSetMax = LODData->StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords() - 1;
	const float InvDt = 1.0f / InstData->DeltaSeconds;
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		const int32 Tri = FMath::Clamp(TriParam.GetAndAdvance(), 0, TriMax) * 3;
		const int32 Idx0 = IndexBuffer->Get(Tri);
		const int32 Idx1 = IndexBuffer->Get(Tri + 1);
		const int32 Idx2 = IndexBuffer->Get(Tri + 2);
		const int32 UVSet = FMath::Clamp(UVSetParam.GetAndAdvance(), 0, UVSetMax);
		const FVector2D UV0 = VertAccessor.GetVertexUV(LODData, Idx0, UVSet);
		const FVector2D UV1 = VertAccessor.GetVertexUV(LODData, Idx1, UVSet);
		const FVector2D UV2 = VertAccessor.GetVertexUV(LODData, Idx2, UVSet);

		FVector2D UV = BarycentricInterpolate(BaryParam.GetAndAdvance(), UV0, UV1, UV2);
		OutUV.SetAndAdvance(UV);
	}
}

// Stub specialization for no valid mesh data on the data interface
template<>
void UNiagaraDataInterfaceSkeletalMesh::GetTriCoordUV<FSkelMeshVertexAccessorNoop>(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	FNDIInputParam<int32> TriParam(Context);
	FNDIInputParam<FVector> BaryParam(Context);
	FNDIInputParam<int32> UVSetParam(Context);

	FNDIOutputParam<FVector2D> OutUV(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutUV.SetAndAdvance(FVector2D::ZeroVector);
	}
}

struct FGetTriCoordSkinnedDataOutputHandler
{
	FGetTriCoordSkinnedDataOutputHandler(FVectorVMContext& Context)
		: Position(Context)
		, Velocity(Context)
		, Normal(Context)
		, Binormal(Context)
		, Tangent(Context)
		, bNeedsPosition(Position.IsValid())
		, bNeedsVelocity(Velocity.IsValid())
		, bNeedsNorm(Normal.IsValid())
		, bNeedsBinorm(Binormal.IsValid())
		, bNeedsTangent(Tangent.IsValid())
	{
	}

	FNDIOutputParam<FVector> Position;
	FNDIOutputParam<FVector> Velocity;
	FNDIOutputParam<FVector> Normal;
	FNDIOutputParam<FVector> Binormal;
	FNDIOutputParam<FVector> Tangent;

	const bool bNeedsPosition;
	const bool bNeedsVelocity;
	const bool bNeedsNorm;
	const bool bNeedsBinorm;
	const bool bNeedsTangent;
};

template<typename SkinningHandlerType, typename TransformHandlerType, typename VertexAccessorType, typename bInterpolated>
void UNiagaraDataInterfaceSkeletalMesh::GetTriCoordSkinnedData(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Sample);
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);

	SkinningHandlerType SkinningHandler;
	TransformHandlerType TransformHandler;
	VertexAccessorType VertAccessor;
	FNDIInputParam<int32> TriParam(Context);
	FNDIInputParam<FVector> BaryParam(Context);
	VectorVM::FExternalFuncInputHandler<float> InterpParam;

 	if(bInterpolated::Value)
 	{
 		InterpParam.Init(Context);
 	}	

	checkf(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());
	checkf(InstData->bMeshValid, TEXT("Skeletal Mesh Interface has invalid mesh. %s"), *GetPathName());

	//TODO: Replace this by storing off FTransforms and doing a proper lerp to get a final transform.
	//Also need to pull in a per particle interpolation factor.
	const FMatrix& Transform = InstData->Transform;
	const FMatrix& PrevTransform = InstData->PrevTransform;

	FGetTriCoordSkinnedDataOutputHandler Output(Context);

	FSkeletalMeshAccessorHelper Accessor;
	Accessor.Init<TNDISkelMesh_FilterModeNone, TNDISkelMesh_AreaWeightingOff>(InstData);

	check(Accessor.IsSkinAccessible()); // supposed to use the fallback for invalid mesh

	const FSkeletalMeshLODRenderData* LODData = Accessor.LODData;

	const int32 TriMax = (Accessor.IndexBuffer->Num() / 3) - 1;
	const float InvDt = 1.0f / InstData->DeltaSeconds;

	FVector Pos0;		FVector Pos1;		FVector Pos2;
	FVector Prev0;		FVector Prev1;		FVector Prev2;
	FVector Normal;
	FVector Binormal;
	FVector Tangent;
	int32 Idx0; int32 Idx1; int32 Idx2;
	FVector Pos;
	FVector Prev;
	FVector Velocity;

	bool bNeedsCurr = bInterpolated::Value || Output.bNeedsPosition || Output.bNeedsVelocity || Output.bNeedsNorm || Output.bNeedsBinorm || Output.bNeedsTangent;
	bool bNeedsPrev = bInterpolated::Value || Output.bNeedsVelocity;

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		FMeshTriCoordinate MeshTriCoord(TriParam.GetAndAdvance(), BaryParam.GetAndAdvance());

		float Interp = 1.0f;
		if (bInterpolated::Value)
		{
			Interp = InterpParam.GetAndAdvance();
		}

		if(MeshTriCoord.Tri < 0 || MeshTriCoord.Tri > TriMax)
		{
			MeshTriCoord = FMeshTriCoordinate(0, FVector(1.0f, 0.0f, 0.0f));
		}

		SkinningHandler.GetTriangleIndices(Accessor, MeshTriCoord.Tri, Idx0, Idx1, Idx2);

		if (bNeedsCurr)
		{
			SkinningHandler.GetSkinnedTrianglePositions(Accessor, Idx0, Idx1, Idx2, Pos0, Pos1, Pos2);
		}

		if (bNeedsPrev)
		{
			SkinningHandler.GetSkinnedTrianglePreviousPositions(Accessor, Idx0, Idx1, Idx2, Prev0, Prev1, Prev2);
			Prev = BarycentricInterpolate(MeshTriCoord.BaryCoord, Prev0, Prev1, Prev2);
			TransformHandler.TransformPosition(Prev, PrevTransform);
		}

		if (Output.bNeedsPosition || Output.bNeedsVelocity)
		{
			Pos = BarycentricInterpolate(MeshTriCoord.BaryCoord, Pos0, Pos1, Pos2);
			TransformHandler.TransformPosition(Pos, Transform);
			
			if (bInterpolated::Value)
			{
				Pos = FMath::Lerp(Prev, Pos, Interp);
			}

			Output.Position.SetAndAdvance(Pos);
		}

		if (Output.bNeedsVelocity)
		{
			Velocity = (Pos - Prev) * InvDt;

			//No need to handle velocity wrt interpolation as it's based on the prev position anyway

			Output.Velocity.SetAndAdvance(Velocity);
		}

		//TODO: For preskin we should be able to calculate this stuff on the mesh for a perf win in most cases.
		if (Output.bNeedsNorm)
		{
			Normal = ((Pos1 - Pos2) ^ (Pos0 - Pos2)).GetSafeNormal();
			TransformHandler.TransformVector(Normal, Transform);

			if (bInterpolated::Value)
			{
				FVector PrevNormal = ((Prev1 - Prev2) ^ (Prev0 - Prev2)).GetSafeNormal();
				TransformHandler.TransformVector(PrevNormal, PrevTransform);

				Normal = FMath::VInterpNormalRotationTo(PrevNormal, Normal, Interp, 1.0f);
			}

			Output.Normal.SetAndAdvance(Normal);
		}

		if (Output.bNeedsBinorm || Output.bNeedsTangent)
		{
			FVector2D UV0 = VertAccessor.GetVertexUV(LODData, Idx0, 0);
			FVector2D UV1 = VertAccessor.GetVertexUV(LODData, Idx1, 0);
			FVector2D UV2 = VertAccessor.GetVertexUV(LODData, Idx2, 0);

			// Normal binormal tangent calculation code based on tools code found at:
			// \Engine\Source\Developer\MeshUtilities\Private\MeshUtilities.cpp
			// Skeletal_ComputeTriangleTangents
			FMatrix	ParameterToLocal(
				FPlane(Pos1.X - Pos0.X, Pos1.Y - Pos0.Y, Pos1.Z - Pos0.Z, 0),
				FPlane(Pos2.X - Pos0.X, Pos2.Y - Pos0.Y, Pos2.Z - Pos0.Z, 0),
				FPlane(Pos0.X, Pos0.Y, Pos0.Z, 0),
				FPlane(0, 0, 0, 1)
			);

			FMatrix ParameterToTexture(
				FPlane(UV1.X - UV0.X, UV1.Y - UV0.Y, 0, 0),
				FPlane(UV2.X - UV0.X, UV2.Y - UV0.Y, 0, 0),
				FPlane(UV0.X, UV0.Y, 1, 0),
				FPlane(0, 0, 0, 1)
			);

			// Use InverseSlow to catch singular matrices.  Inverse can miss this sometimes.
			const FMatrix TextureToLocal = ParameterToTexture.Inverse() * ParameterToLocal;

			if (bInterpolated::Value)
			{
				FMatrix	PrevParameterToLocal(
					FPlane(Prev1.X - Prev0.X, Prev1.Y - Prev0.Y, Prev1.Z - Prev0.Z, 0),
					FPlane(Prev2.X - Prev0.X, Prev2.Y - Prev0.Y, Prev2.Z - Prev0.Z, 0),
					FPlane(Prev0.X, Prev0.Y, Prev0.Z, 0),
					FPlane(0, 0, 0, 1)
				);

				// Use InverseSlow to catch singular matrices.  Inverse can miss this sometimes.
				const FMatrix PrevTextureToLocal = ParameterToTexture.Inverse() * PrevParameterToLocal;

				//TODO: For preskin we should be able to calculate this stuff on the mesh for a perf win in most cases.
				if (Output.bNeedsBinorm)
				{
					Binormal = (TextureToLocal.TransformVector(FVector(1, 0, 0)).GetSafeNormal());
					TransformHandler.TransformVector(Binormal, Transform);

					FVector PrevBinormal = (PrevTextureToLocal.TransformVector(FVector(1, 0, 0)).GetSafeNormal());
					TransformHandler.TransformVector(PrevBinormal, PrevTransform);

					Binormal = FMath::VInterpNormalRotationTo(Binormal, Binormal, Interp, 1.0f);

					Output.Binormal.SetAndAdvance(Binormal);
				}

				//TODO: For preskin we should be able to calculate this stuff on the mesh for a perf win in most cases.
				if (Output.bNeedsTangent)
				{
					Tangent = (TextureToLocal.TransformVector(FVector(0, 1, 0)).GetSafeNormal());
					TransformHandler.TransformVector(Tangent, Transform);

					FVector PrevTangent = (TextureToLocal.TransformVector(FVector(0, 1, 0)).GetSafeNormal());
					TransformHandler.TransformVector(PrevTangent, PrevTransform);

					Tangent = FMath::VInterpNormalRotationTo(PrevTangent, Tangent, Interp, 1.0f);

					Output.Tangent.SetAndAdvance(Tangent);
				}
			}
			else
			{
				if (Output.bNeedsBinorm)
				{
					Binormal = (TextureToLocal.TransformVector(FVector(1, 0, 0)).GetSafeNormal());
					TransformHandler.TransformVector(Binormal, Transform);

					Output.Binormal.SetAndAdvance(Binormal);
				}

				if (Output.bNeedsTangent)
				{
					Tangent = (TextureToLocal.TransformVector(FVector(0, 1, 0)).GetSafeNormal());
					TransformHandler.TransformVector(Tangent, Transform);
					Output.Tangent.SetAndAdvance(Tangent);
				}
			}
		}
	}
}

// Fallback sampling function for no valid mesh on the interface
template<typename TransformHandlerType, typename bInterpolated>
void UNiagaraDataInterfaceSkeletalMesh::GetTriCoordSkinnedDataFallback(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Sample);
	TransformHandlerType TransformHandler;

	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);	
	FNDIInputParam<int32> TriParam(Context);
	FNDIInputParam<FVector> BaryParam(Context);
	VectorVM::FExternalFuncInputHandler<float> InterpParam;

	if (bInterpolated::Value)
	{
		InterpParam.Init(Context);
	}

	checkfSlow(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());

	//TODO: Replace this by storing off FTransforms and doing a proper lerp to get a final transform.
	//Also need to pull in a per particle interpolation factor.
	const FMatrix& Transform = InstData->Transform;
	const FMatrix& PrevTransform = InstData->PrevTransform;

	FGetTriCoordSkinnedDataOutputHandler Output(Context);
	bool bNeedsPrev = bInterpolated::Value || Output.bNeedsVelocity;

	const float InvDt = 1.0f / InstData->DeltaSeconds;

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		float Interp = 1.0f;
		if (bInterpolated::Value)
		{
			Interp = InterpParam.GetAndAdvance();
		}

		FVector Prev(0.0f);
		FVector Pos(0.0f);
		if (bNeedsPrev)
		{
			TransformHandler.TransformPosition(Prev, PrevTransform);
		}

		if (Output.bNeedsPosition || Output.bNeedsVelocity)
		{
			TransformHandler.TransformPosition(Pos, Transform);

			if (bInterpolated::Value)
			{
				Pos = FMath::Lerp(Prev, Pos, Interp);
			}

			Output.Position.SetAndAdvance(Pos);
		}

		if (Output.bNeedsVelocity)
		{			
			FVector Velocity = (Pos - Prev) * InvDt;
			Output.Velocity.SetAndAdvance(Velocity);
		}

		if (Output.bNeedsNorm)
		{
			Output.Normal.SetAndAdvance(FVector(0.0f, 0.0f, 1.0f));
		}

		if (Output.bNeedsBinorm)
		{
			Output.Binormal.SetAndAdvance(FVector(0.0f, 1.0f, 0.0f));
		}
		
		if (Output.bNeedsTangent)
		{
			Output.Tangent.SetAndAdvance(FVector(1.0f, 0.0f, 0.0f));
		}		
	}
}

template<typename SkinningHandlerType>
void UNiagaraDataInterfaceSkeletalMesh::GetTriCoordVertices(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Sample);
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);

	SkinningHandlerType SkinningHandler;
	FNDIInputParam<int32> TriParam(Context);

	checkf(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());
	checkf(InstData->bMeshValid, TEXT("Skeletal Mesh Interface has invalid mesh. %s"), *GetPathName());

	FNDIOutputParam<int32> OutV0(Context);
	FNDIOutputParam<int32> OutV1(Context);
	FNDIOutputParam<int32> OutV2(Context);

	int32 Idx0; int32 Idx1; int32 Idx2;
	FSkeletalMeshAccessorHelper Accessor;
	Accessor.Init<TNDISkelMesh_FilterModeNone, TNDISkelMesh_AreaWeightingOff>(InstData);

	const int32 TriMax = Accessor.IsSkinAccessible() ? (Accessor.IndexBuffer->Num() / 3) - 1 : 0;
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		const int32 Tri = FMath::Clamp(TriParam.GetAndAdvance(), 0, TriMax);
		SkinningHandler.GetTriangleIndices(Accessor, Tri, Idx0, Idx1, Idx2);
		OutV0.SetAndAdvance(Idx0);
		OutV1.SetAndAdvance(Idx1);
		OutV2.SetAndAdvance(Idx2);
	}
}

#undef LOCTEXT_NAMESPACE
