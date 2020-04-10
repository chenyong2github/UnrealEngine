// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceSkeletalMesh.h"
#include "NiagaraComponent.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "SkeletalMeshTypes.h"
#include "NiagaraStats.h"
#include "Templates/AlignmentTemplates.h"
#include "NDISkeletalMeshCommon.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceSkeletalMesh_BoneSampling"

DECLARE_CYCLE_STAT(TEXT("Skel Mesh Skeleton Sampling"), STAT_NiagaraSkel_Bone_Sample, STATGROUP_Niagara);

//Final binders for all static mesh interface functions.
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetSkinnedBoneData)
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetSkinnedBoneDataFallback)
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, IsValidBone)

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetFilteredSocketBoneAt)
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetFilteredSocketTransform)

const FName FSkeletalMeshInterfaceHelper::GetSkinnedBoneDataName("GetSkinnedBoneData");
const FName FSkeletalMeshInterfaceHelper::GetSkinnedBoneDataWSName("GetSkinnedBoneDataWS");
const FName FSkeletalMeshInterfaceHelper::GetSkinnedBoneDataInterpolatedName("GetSkinnedBoneDataInterpolated");
const FName FSkeletalMeshInterfaceHelper::GetSkinnedBoneDataWSInterpolatedName("GetSkinnedBoneDataWSInterpolated");

const FName FSkeletalMeshInterfaceHelper::IsValidBoneName("IsValidBone");
const FName FSkeletalMeshInterfaceHelper::RandomBoneName("RandomBone");
const FName FSkeletalMeshInterfaceHelper::GetBoneCountName("GetBoneCount");

const FName FSkeletalMeshInterfaceHelper::RandomFilteredBoneName("RandomFilteredBone");
const FName FSkeletalMeshInterfaceHelper::GetFilteredBoneCountName("GetFilteredBoneCount");
const FName FSkeletalMeshInterfaceHelper::GetFilteredBoneAtName("GetFilteredBone");

const FName FSkeletalMeshInterfaceHelper::RandomUnfilteredBoneName("RandomUnfilteredBone");
const FName FSkeletalMeshInterfaceHelper::GetUnfilteredBoneCountName("GetUnfilteredBoneCount");
const FName FSkeletalMeshInterfaceHelper::GetUnfilteredBoneAtName("GetUnfilteredBone");

const FName FSkeletalMeshInterfaceHelper::RandomFilteredSocketName("RandomFilteredSocket");
const FName FSkeletalMeshInterfaceHelper::GetFilteredSocketCountName("GetFilteredSocketCount");
const FName FSkeletalMeshInterfaceHelper::GetFilteredSocketTransformName("GetFilteredSocketTransform");
const FName FSkeletalMeshInterfaceHelper::GetFilteredSocketBoneAtName("GetFilteredSocket");

const FName FSkeletalMeshInterfaceHelper::RandomFilteredSocketOrBoneName("RandomFilteredSocketOrBone");
const FName FSkeletalMeshInterfaceHelper::GetFilteredSocketOrBoneCountName("GetFilteredSocketOrBoneCount");
const FName FSkeletalMeshInterfaceHelper::GetFilteredSocketOrBoneAtName("GetFilteredSocketOrBone");

void UNiagaraDataInterfaceSkeletalMesh::GetSkeletonSamplingFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	//////////////////////////////////////////////////////////////////////////
	// Bone functions.
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetSkinnedBoneDataName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Bone")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Rotation")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetOptionalSkinnedBoneDataDesc", "Returns skinning dependant data for the pased bone in local space. All outputs are optional and you will incur zero to minimal cost if they are not connected.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetSkinnedBoneDataWSName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Bone")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Rotation")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetOptionalSkinnedBoneDataWSDesc", "Returns skinning dependant data for the pased bone in world space. All outputs are optional and you will incur zero to minimal cost if they are not connected.");
#endif
		OutFunctions.Add(Sig);
	}


	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetSkinnedBoneDataInterpolatedName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Bone")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Interpolation")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Rotation")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetSkinnedBoneDataDesc", "Returns skinning dependant data for the pased bone in local space. Interpolated between this frame and the previous based on passed interpolation factor. All outputs are optional and you will incur zero to minimal cost if they are not connected.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetSkinnedBoneDataWSInterpolatedName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Bone")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Interpolation")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Rotation")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetSkinnedBoneDataWSDesc", "Returns skinning dependant data for the pased bone in world space. Interpolated between this frame and the previous based on passed interpolation factor. All outputs are optional and you will incur zero to minimal cost if they are not connected.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::IsValidBoneName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Bone")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsValid")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("IsValidBoneDesc", "Determine if this bone index is valid for this mesh's skeleton.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::RandomBoneName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FNiagaraRandInfo::StaticStruct()), TEXT("RandomInfo")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Bone")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetBoneCountName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Count")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetBoneCountDesc", "Returns the number of bones in the skeletal mesh.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::RandomFilteredBoneName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FNiagaraRandInfo::StaticStruct()), TEXT("RandomInfo")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Bone")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetFilteredBoneCountName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Count")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetFilteredBoneCountDesc", "Returns the number of filtered bones in the DI list.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetFilteredBoneAtName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Bone Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Bone")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetFilteredBoneAtDesc", "Gets the bone at the passed index in the DI's filter bones list.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::RandomUnfilteredBoneName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FNiagaraRandInfo::StaticStruct()), TEXT("RandomInfo")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Bone")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetUnfilteredBoneCountName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Count")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetUnfilteredBoneCountDesc", "Returns the number of unfiltered bones (i.e. the exclusion of filtered bones) in the DI list.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetUnfilteredBoneAtName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Bone Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Bone")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetUnfilteredBoneAtDesc", "Gets the bone at the passed index from the exlusion of the DI's filter bones list.");
#endif
		OutFunctions.Add(Sig);
	}

	//////////////////////////////////////////////////////////////////////////
	//Socket functions
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::RandomFilteredSocketName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FNiagaraRandInfo::StaticStruct()), TEXT("RandomInfo")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Socket Bone")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("RandomFilteredSocketDesc", "Gets the bone for a random socket in the DI's filtered socket list.");
#endif
		OutFunctions.Add(Sig);
	}
	
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetFilteredSocketCountName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Count")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetFilteredSocketCountDesc", "Returns the number of filtered Sockets in the DI list.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetFilteredSocketBoneAtName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Socket Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Socket Bone")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetFilteredSocketBoneAtDesc", "Gets the bone for the socket at the passed index in the DI's filtered socket list.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetFilteredSocketTransformName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Socket Index")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Apply World Transform")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Socket Translation")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Socket Rotation")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Socket Scale")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetFilteredSocketTransformDesc", "Gets the transform for the socket at the passed index in the DI's filtered socket list. If the Source component is set it will respect the Relative Transform Space as well..");
#endif
		OutFunctions.Add(Sig);
	}

	//////////////////////////////////////////////////////////////////////////
	// Misc Functions
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::RandomFilteredSocketOrBoneName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FNiagaraRandInfo::StaticStruct()), TEXT("RandomInfo")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Bone")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("RandomFilteredSocketOrBoneDesc", "Gets the bone for a random filtered socket or bone from the DI's list.");
#endif
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetFilteredSocketOrBoneCountName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Count")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetFilteredSocketOrBoneCountDesc", "Gets the total filtered socket and bone count from the DI's list.");
#endif
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetFilteredSocketOrBoneAtName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Socket Or Bone Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Bone")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetFilteredSocketOrBoneAtDesc", "Gets a filtered socket or bone count from the DI's list.");
#endif
		OutFunctions.Add(Sig);
	}
}

void UNiagaraDataInterfaceSkeletalMesh::BindSkeletonSamplingFunction(const FVMExternalFunctionBindingInfo& BindingInfo, FNDISkeletalMesh_InstanceData* InstanceData, FVMExternalFunction &OutFunc)
{
	using TInterpOff = TIntegralConstant<bool, false>;
	using TInterpOn = TIntegralConstant<bool, true>;

	//Bone Functions
	if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetSkinnedBoneDataName)
	{
		ensure(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 10);
		if (InstanceData->Mesh)
		{
			TSkinningModeBinder<TNDIExplicitBinder<FNDITransformHandlerNoop, TNDIExplicitBinder<TInterpOff, NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetSkinnedBoneData)>>>::Bind(this, BindingInfo, InstanceData, OutFunc);
		}
		else
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetSkinnedBoneDataFallback)::Bind<FNDITransformHandlerNoop, TInterpOff>(this, BindingInfo, InstanceData, OutFunc);
		}
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetSkinnedBoneDataWSName)
	{
		ensure(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 10);
		if (InstanceData->Mesh)
		{
			TSkinningModeBinder<TNDIExplicitBinder<FNDITransformHandler, TNDIExplicitBinder<TInterpOff, NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetSkinnedBoneData)>>>::Bind(this, BindingInfo, InstanceData, OutFunc);
		}
		else
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetSkinnedBoneDataFallback)::Bind<FNDITransformHandler, TInterpOff>(this, BindingInfo, InstanceData, OutFunc);
		}
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetSkinnedBoneDataInterpolatedName)
	{
		ensure(BindingInfo.GetNumInputs() == 3 && BindingInfo.GetNumOutputs() == 10);
		if (InstanceData->Mesh)
		{
			TSkinningModeBinder<TNDIExplicitBinder<FNDITransformHandlerNoop, TNDIExplicitBinder<TInterpOn, NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetSkinnedBoneData)>>>::Bind(this, BindingInfo, InstanceData, OutFunc);
		}
		else
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetSkinnedBoneDataFallback)::Bind<FNDITransformHandlerNoop, TInterpOn>(this, BindingInfo, InstanceData, OutFunc);
		}
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetSkinnedBoneDataWSInterpolatedName)
	{
		ensure(BindingInfo.GetNumInputs() == 3 && BindingInfo.GetNumOutputs() == 10);
		if (InstanceData->Mesh)
		{
			TSkinningModeBinder<TNDIExplicitBinder<FNDITransformHandler, TNDIExplicitBinder<TInterpOn, NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetSkinnedBoneData)>>>::Bind(this, BindingInfo, InstanceData, OutFunc);
		}
		else
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetSkinnedBoneDataFallback)::Bind<FNDITransformHandler, TInterpOn>(this, BindingInfo, InstanceData, OutFunc);
		}
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::IsValidBoneName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, IsValidBone)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::RandomBoneName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 1);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->RandomBone(Context); });
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetBoneCountName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->GetBoneCount(Context); });
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::RandomFilteredBoneName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 1);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->RandomFilteredBone(Context); });
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetFilteredBoneCountName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->GetFilteredBoneCount(Context); });
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetFilteredBoneAtName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 1);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->GetFilteredBoneAt(Context); });
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::RandomUnfilteredBoneName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 1);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->RandomUnfilteredBone(Context); });
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetUnfilteredBoneCountName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->GetUnfilteredBoneCount(Context); });
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetUnfilteredBoneAtName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 1);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->GetUnfilteredBoneAt(Context); });
	}
	//Socket Functions
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::RandomFilteredSocketName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 1);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->RandomFilteredSocket(Context); });
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetFilteredSocketCountName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->GetFilteredSocketCount(Context); });
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetFilteredSocketBoneAtName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetFilteredSocketBoneAt)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetFilteredSocketTransformName)
	{
		check(BindingInfo.GetNumInputs() == 3 && BindingInfo.GetNumOutputs() == 10);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetFilteredSocketTransform)::Bind(this, OutFunc);
	}
	// Misc functions
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::RandomFilteredSocketOrBoneName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 1);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->RandomFilteredSocketOrBone(Context); });
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetFilteredSocketOrBoneCountName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->GetFilteredSocketOrBoneCount(Context); });
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetFilteredSocketOrBoneAtName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 1);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->GetFilteredSocketOrBoneBoneAt(Context); });
	}
}

//////////////////////////////////////////////////////////////////////////
// Direct sampling from listed sockets and bones.

void UNiagaraDataInterfaceSkeletalMesh::GetFilteredBoneCount(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Bone_Sample);

	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<int32> OutCount(Context);

	const int32 Num = InstData->NumFilteredBones;
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*OutCount.GetDestAndAdvance() = Num;
	}
}

void UNiagaraDataInterfaceSkeletalMesh::GetFilteredBoneAt(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	VectorVM::FExternalFuncInputHandler<int32> BoneParam(Context);

	VectorVM::FExternalFuncRegisterHandler<int32> OutBone(Context);

	const int32 Max = InstData->NumFilteredBones - 1;
	if (Max >= 0)
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			const int32 BoneIndex = FMath::Clamp(BoneParam.GetAndAdvance(), 0, Max);
			*OutBone.GetDestAndAdvance() = InstData->FilteredAndUnfilteredBones[BoneIndex];
		}
	}
	else
	{
		FMemory::Memset(OutBone.GetDest(), 0xFF, Context.NumInstances * sizeof(int32));
	}
}

void UNiagaraDataInterfaceSkeletalMesh::RandomFilteredBone(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	FNDIRandomHelper RandHelper(Context);

	VectorVM::FExternalFuncRegisterHandler<int32> OutBone(Context);

	const int32 Max = InstData->NumFilteredBones - 1;
	if (Max >= 0)
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			RandHelper.GetAndAdvance();
			const int32 BoneIndex = RandHelper.RandRange(i, 0, Max);
			*OutBone.GetDestAndAdvance() = InstData->FilteredAndUnfilteredBones[BoneIndex];
		}
	}
	else
	{
		FMemory::Memset(OutBone.GetDest(), 0xFF, Context.NumInstances * sizeof(int32));
	}
}

void UNiagaraDataInterfaceSkeletalMesh::GetUnfilteredBoneCount(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Bone_Sample);
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<int32> OutCount(Context);

	const int32 Num = InstData->NumUnfilteredBones;
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*OutCount.GetDestAndAdvance() = Num;
	}
}

void UNiagaraDataInterfaceSkeletalMesh::GetUnfilteredBoneAt(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	VectorVM::FExternalFuncInputHandler<int32> BoneParam(Context);

	VectorVM::FExternalFuncRegisterHandler<int32> OutBone(Context);

	const int32 Max = InstData->NumUnfilteredBones - 1;
	if (Max >= 0)
	{
		if (InstData->NumFilteredBones == 0)
		{
			for (int32 i = 0; i < Context.NumInstances; ++i)
			{
				const int32 BoneIndex = FMath::Clamp(BoneParam.GetAndAdvance(), 0, Max);
				*OutBone.GetDestAndAdvance() = BoneIndex;
			}
		}
		else
		{
			for (int32 i = 0; i < Context.NumInstances; ++i)
			{
				const int32 BoneIndex = FMath::Clamp(BoneParam.GetAndAdvance(), 0, Max);
				*OutBone.GetDestAndAdvance() = InstData->FilteredAndUnfilteredBones[BoneIndex + InstData->NumFilteredBones];
			}
		}
	}
	else
	{
		FMemory::Memset(OutBone.GetDest(), 0xFF, Context.NumInstances * sizeof(int32));
	}
}
void UNiagaraDataInterfaceSkeletalMesh::RandomUnfilteredBone(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	FNDIRandomHelper RandHelper(Context);

	VectorVM::FExternalFuncRegisterHandler<int32> OutBone(Context);

	const int32 UnfilteredMax = InstData->NumUnfilteredBones - 1;
	if (UnfilteredMax >= 0)
	{
		if (InstData->NumFilteredBones == 0)
		{
			const int32 ExcludedBoneIndex = InstData->ExcludedBoneIndex;
			const int32 NumBones = InstData->NumUnfilteredBones - (ExcludedBoneIndex >= 0 ? 2 : 1);
			if (NumBones >= 0)
			{
				for (int32 i = 0; i < Context.NumInstances; ++i)
				{
					RandHelper.GetAndAdvance();
					const int32 BoneIndex = RandHelper.RandRange(i, 0, NumBones);
					*OutBone.GetDestAndAdvance() = BoneIndex != ExcludedBoneIndex ? BoneIndex : BoneIndex + 1;
				}
			}
			else
			{
				FMemory::Memset(OutBone.GetDest(), 0xFF, Context.NumInstances * sizeof(int32));
			}
		}
		else
		{
			for (int32 i = 0; i < Context.NumInstances; ++i)
			{
				RandHelper.GetAndAdvance();
				const int32 BoneIndex = RandHelper.RandRange(i, 0, UnfilteredMax);
				*OutBone.GetDestAndAdvance() = InstData->FilteredAndUnfilteredBones[BoneIndex + InstData->NumFilteredBones];
			}
		}
	}
	else
	{
		FMemory::Memset(OutBone.GetDest(), 0xFF, Context.NumInstances * sizeof(int32));
	}
}

void UNiagaraDataInterfaceSkeletalMesh::IsValidBone(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	VectorVM::FExternalFuncInputHandler<int32> BoneParam(Context);

	checkfSlow(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());

	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> OutValid(Context);

	FSkeletalMeshAccessorHelper MeshAccessor;
	MeshAccessor.Init<TNDISkelMesh_FilterModeNone, TNDISkelMesh_AreaWeightingOff>(InstData);	

	if (MeshAccessor.AreBonesAccessible())
	{
		const FReferenceSkeleton& RefSkeleton = MeshAccessor.Mesh->RefSkeleton;
		int32 NumBones = RefSkeleton.GetNum();
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			int32 RequestedIndex = BoneParam.GetAndAdvance();

			FNiagaraBool Value;
			Value.SetValue(RequestedIndex >= 0 && RequestedIndex < NumBones);
			*OutValid.GetDestAndAdvance() = Value;
		}
	}
	else
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			FNiagaraBool Value;
			Value.SetValue(false);
			*OutValid.GetDestAndAdvance() = Value;
		}
	}
}

void UNiagaraDataInterfaceSkeletalMesh::RandomBone(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	FNDIRandomHelper RandHelper(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutBone(Context);

	FSkeletalMeshAccessorHelper MeshAccessor;
	MeshAccessor.Init<TIntegralConstant<int32, 0>, TIntegralConstant<int32, 0>>(InstData);

	const int32 ExcludedBoneIndex = InstData->ExcludedBoneIndex;
	int32 NumBones = 0;
	if (MeshAccessor.AreBonesAccessible())
	{
		const FReferenceSkeleton& RefSkeleton = MeshAccessor.Mesh->RefSkeleton;
		NumBones = RefSkeleton.GetNum() - (ExcludedBoneIndex >= 0 ? 2 : 1);
	}

	if (NumBones >= 0)
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			RandHelper.GetAndAdvance();
			const int32 BoneIndex = RandHelper.RandRange(i, 0, NumBones);
			*OutBone.GetDestAndAdvance() = BoneIndex != ExcludedBoneIndex ? BoneIndex : BoneIndex + 1;
		}
	}
	else
	{
		FMemory::Memset(OutBone.GetDest(), 0xFF, Context.NumInstances * sizeof(int32));
	}
}

void UNiagaraDataInterfaceSkeletalMesh::GetBoneCount(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Bone_Sample);

	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutCount(Context);

	FSkeletalMeshAccessorHelper MeshAccessor;
	MeshAccessor.Init<TIntegralConstant<int32, 0>, TIntegralConstant<int32, 0>>(InstData);
	
	int32 NumBones = 0;
	if (MeshAccessor.AreBonesAccessible())
	{
		const FReferenceSkeleton& RefSkeleton = MeshAccessor.Mesh->RefSkeleton;
		NumBones = RefSkeleton.GetNum();
	}

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*OutCount.GetDestAndAdvance() = NumBones;
	}
}

struct FBoneSocketSkinnedDataOutputHandler
{
	FBoneSocketSkinnedDataOutputHandler(FVectorVMContext& Context)
		: PosX(Context), PosY(Context), PosZ(Context)
		, RotX(Context), RotY(Context), RotZ(Context), RotW(Context)
		, VelX(Context), VelY(Context), VelZ(Context)
		, bNeedsPosition(PosX.IsValid() || PosY.IsValid() || PosZ.IsValid())
		, bNeedsRotation(RotX.IsValid() || RotY.IsValid() || RotZ.IsValid() || RotW.IsValid())
		, bNeedsVelocity(VelX.IsValid() || VelY.IsValid() || VelZ.IsValid())
	{
	}

	VectorVM::FExternalFuncRegisterHandler<float> PosX; VectorVM::FExternalFuncRegisterHandler<float> PosY; VectorVM::FExternalFuncRegisterHandler<float> PosZ;
	VectorVM::FExternalFuncRegisterHandler<float> RotX; VectorVM::FExternalFuncRegisterHandler<float> RotY; VectorVM::FExternalFuncRegisterHandler<float> RotZ; VectorVM::FExternalFuncRegisterHandler<float> RotW;
	VectorVM::FExternalFuncRegisterHandler<float> VelX; VectorVM::FExternalFuncRegisterHandler<float> VelY; VectorVM::FExternalFuncRegisterHandler<float> VelZ;

	//TODO: Rotation + Scale too? Use quats so we can get proper interpolation between bone and parent.

	const bool bNeedsPosition;
	const bool bNeedsRotation;
	const bool bNeedsVelocity;

	FORCEINLINE void SetPosition(FVector Position)
	{
		*PosX.GetDestAndAdvance() = Position.X;
		*PosY.GetDestAndAdvance() = Position.Y;
		*PosZ.GetDestAndAdvance() = Position.Z;
	}

	FORCEINLINE void SetRotation(FQuat Rotation)
	{
		*RotX.GetDestAndAdvance() = Rotation.X;
		*RotY.GetDestAndAdvance() = Rotation.Y;
		*RotZ.GetDestAndAdvance() = Rotation.Z;
		*RotW.GetDestAndAdvance() = Rotation.W;
	}

	FORCEINLINE void SetVelocity(FVector Velocity)
	{
		*VelX.GetDestAndAdvance() = Velocity.X;
		*VelY.GetDestAndAdvance() = Velocity.Y;
		*VelZ.GetDestAndAdvance() = Velocity.Z;
	}
};

template<typename SkinningHandlerType, typename TransformHandlerType, typename bInterpolated>
void UNiagaraDataInterfaceSkeletalMesh::GetSkinnedBoneData(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);

	SkinningHandlerType SkinningHandler;
	TransformHandlerType TransformHandler;
	VectorVM::FExternalFuncInputHandler<int32> BoneParam(Context);
	VectorVM::FExternalFuncInputHandler<float> InterpParam;

	if (bInterpolated::Value)
	{
		InterpParam.Init(Context);
	}

	checkfSlow(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());
	checkfSlow(InstData->Mesh, TEXT("Skeletal Mesh Interface has invalid mesh. %s"), *GetPathName());

	FBoneSocketSkinnedDataOutputHandler Output(Context);

	//TODO: Replace this by storing off FTransforms and doing a proper lerp to get a final transform.
	//Also need to pull in a per particle interpolation factor.
	const FMatrix& InstanceTransform = InstData->Transform;
	const FMatrix& PrevInstanceTransform = InstData->PrevTransform;
	const FQuat InstanceRotation = Output.bNeedsRotation ? InstanceTransform.ToQuat() : FQuat::Identity;
	const FQuat PrevInstanceRotation = Output.bNeedsRotation ? PrevInstanceTransform.ToQuat() : FQuat::Identity;

	FSkinWeightVertexBuffer* SkinWeightBuffer = InstData->GetSkinWeights();

	FSkeletalMeshAccessorHelper Accessor;
	Accessor.Init<TNDISkelMesh_FilterModeNone, TNDISkelMesh_AreaWeightingOff>(InstData);
	check(Accessor.AreBonesAccessible());

	const int32 BoneCount = SkinningHandler.GetBoneCount(Accessor, bInterpolated::Value);
	const int32 BoneAndSocketCount = BoneCount + InstData->FilteredSocketInfo.Num();
	float InvDt = 1.0f / InstData->DeltaSeconds;

	const TArray<FTransform>& FilteredSocketCurrTransforms = InstData->GetFilteredSocketsCurrBuffer();
	const TArray<FTransform>& FilteredSocketPrevTransforms = InstData->GetFilteredSocketsPrevBuffer();

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		const float Interp = bInterpolated::Value ? InterpParam.GetAndAdvance() : 1.0f;

		// Determine bone or socket
		const int32 Bone = BoneParam.GetAndAdvance();
		const bool bIsSocket = Bone >= BoneCount;
		const int32 Socket = Bone - BoneCount;

		FVector Pos;
		FVector Prev;

		// Handle invalid bone indices first
		if (Bone < 0 || Bone >= BoneAndSocketCount)
		{
			Pos = FVector::ZeroVector;
			TransformHandler.TransformPosition(Pos, InstanceTransform);

			if (Output.bNeedsVelocity || bInterpolated::Value)
			{
				Prev = FVector::ZeroVector;
				TransformHandler.TransformPosition(Prev, PrevInstanceTransform);
			}
			if (Output.bNeedsRotation)
			{
				Output.SetRotation(FQuat::Identity);
			}
		}
		else if (bIsSocket)
		{
			FTransform CurrSocketTransform = FilteredSocketCurrTransforms[Socket];
			FTransform PrevSocketTransform = FilteredSocketPrevTransforms[Socket];

			Pos = CurrSocketTransform.GetLocation();
			TransformHandler.TransformPosition(Pos, InstanceTransform);

			if (Output.bNeedsVelocity || bInterpolated::Value)
			{
				Prev = PrevSocketTransform.GetLocation();
				TransformHandler.TransformPosition(Prev, PrevInstanceTransform);
			}

			if (Output.bNeedsRotation)
			{
				FQuat Rotation = CurrSocketTransform.GetRotation();
				TransformHandler.TransformRotation(Rotation, InstanceRotation);
				if (bInterpolated::Value)
				{
					FQuat PrevRotation = PrevSocketTransform.GetRotation();
					TransformHandler.TransformRotation(PrevRotation, PrevInstanceRotation);
					Rotation = FQuat::Slerp(PrevRotation, Rotation, Interp);
				}

				Output.SetRotation(Rotation);
			}
		}
		// Bone
		else
		{
			Pos = SkinningHandler.GetSkinnedBonePosition(Accessor, Bone);
			TransformHandler.TransformPosition(Pos, InstanceTransform);

			if (Output.bNeedsVelocity || bInterpolated::Value)
			{
				Prev = SkinningHandler.GetSkinnedBonePreviousPosition(Accessor, Bone);
				TransformHandler.TransformPosition(Prev, PrevInstanceTransform);
			}

			if (Output.bNeedsRotation)
			{
				FQuat Rotation = SkinningHandler.GetSkinnedBoneRotation(Accessor, Bone);
				TransformHandler.TransformRotation(Rotation, InstanceRotation);
				if (bInterpolated::Value)
				{
					FQuat PrevRotation = SkinningHandler.GetSkinnedBonePreviousRotation(Accessor, Bone);
					TransformHandler.TransformRotation(PrevRotation, PrevInstanceRotation);
					Rotation = FQuat::Slerp(PrevRotation, Rotation, Interp);
				}

				Output.SetRotation(Rotation);
			}
		}

		if (Output.bNeedsVelocity || bInterpolated::Value)
		{
			Pos = FMath::Lerp(Prev, Pos, Interp);
		}

		if (Output.bNeedsPosition)
		{
			Output.SetPosition(Pos);
		}

		if(Output.bNeedsVelocity)
		{
			//Don't have enough information to get a better interpolated velocity.
			FVector Velocity = (Pos - Prev) * InvDt;
			Output.SetVelocity(Velocity);
		}
	}
}

template<typename TransformHandlerType, typename bInterpolated>
void UNiagaraDataInterfaceSkeletalMesh::GetSkinnedBoneDataFallback(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Bone_Sample);
	TransformHandlerType TransformHandler;
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);

	VectorVM::FExternalFuncInputHandler<int32> BoneParam(Context);
	VectorVM::FExternalFuncInputHandler<float> InterpParam;

	if (bInterpolated::Value)
	{
		InterpParam.Init(Context);
	}

	checkfSlow(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());	

	FBoneSocketSkinnedDataOutputHandler Output(Context);

	//TODO: Replace this by storing off FTransforms and doing a proper lerp to get a final transform.
	//Also need to pull in a per particle interpolation factor.
	const FMatrix& InstanceTransform = InstData->Transform;
	const FMatrix& PrevInstanceTransform = InstData->PrevTransform;
	const FQuat InstanceRotation = Output.bNeedsRotation ? InstData->Transform.GetMatrixWithoutScale().ToQuat() : FQuat::Identity;
	const FQuat PrevInstanceRotation = Output.bNeedsRotation ? InstData->Transform.GetMatrixWithoutScale().ToQuat() : FQuat::Identity;
	const float InvDt = 1.0f / InstData->DeltaSeconds;

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		const float Interp = bInterpolated::Value ? InterpParam.GetAndAdvance() : 1.0f;

		FVector Prev = FVector::ZeroVector;
		FVector Pos = FVector::ZeroVector;		
		TransformHandler.TransformPosition(Pos, InstanceTransform);

		if (Output.bNeedsVelocity || bInterpolated::Value)
		{
			TransformHandler.TransformPosition(Prev, PrevInstanceTransform);
		}

		if (Output.bNeedsRotation)
		{
			Output.SetRotation(FQuat::Identity);
		}
		
		if (Output.bNeedsVelocity || bInterpolated::Value)
		{
			Pos = FMath::Lerp(Prev, Pos, Interp);
		}

		if (Output.bNeedsPosition)
		{
			Output.SetPosition(Pos);
		}

		if (Output.bNeedsVelocity)
		{
			FVector Velocity = (Pos - Prev) * InvDt;
			Output.SetVelocity(Velocity);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// Sockets

void UNiagaraDataInterfaceSkeletalMesh::GetFilteredSocketCount(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<int32> OutCount(Context);

	const int32 Num = InstData->FilteredSocketInfo.Num();
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*OutCount.GetDestAndAdvance() = Num;
	}
}

void UNiagaraDataInterfaceSkeletalMesh::GetFilteredSocketBoneAt(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	VectorVM::FExternalFuncInputHandler<int32> SocketParam(Context);

	VectorVM::FExternalFuncRegisterHandler<int32> OutSocketBone(Context);
	const int32 FilteredSocketBoneOffset = InstData->FilteredSocketBoneOffset;
	const int32 Max = FilteredSockets.Num() - 1;

	if (Max != INDEX_NONE)
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			const int32 SocketIndex = FMath::Clamp(SocketParam.GetAndAdvance(), 0, Max);
			*OutSocketBone.GetDestAndAdvance() = FilteredSocketBoneOffset + SocketIndex;
		}
	}
	else
	{
		FMemory::Memset(OutSocketBone.GetDest(), 0xFF, Context.NumInstances * sizeof(int32));
	}
}

void UNiagaraDataInterfaceSkeletalMesh::GetFilteredSocketTransform(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	VectorVM::FExternalFuncInputHandler<int32> SocketParam(Context);
	VectorVM::FExternalFuncInputHandler<FNiagaraBool> ApplyWorldTransform(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutSocketTranslateX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSocketTranslateY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSocketTranslateZ(Context);
	
	VectorVM::FExternalFuncRegisterHandler<float> OutSocketRotationX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSocketRotationY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSocketRotationZ(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSocketRotationW(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutSocketScaleX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSocketScaleY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSocketScaleZ(Context);

	const TArray<FTransform>& CurrentFilteredSockets = InstData->GetFilteredSocketsCurrBuffer();
	const int32 SocketMax = CurrentFilteredSockets.Num() - 1;
	if (SocketMax >= 0)
	{
		const bool bNeedsRotation = OutSocketRotationX.IsValid() || OutSocketRotationY.IsValid() || OutSocketRotationZ.IsValid() || OutSocketRotationW.IsValid();
		const FMatrix InstanceTransform = InstData->Transform;
		const FQuat InstanceRotation = bNeedsRotation ? InstanceTransform.ToQuat() : FQuat::Identity;

		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			const int32 SocketIndex = FMath::Clamp(SocketParam.GetAndAdvance(), 0, SocketMax);
			FVector SocketTranslation = CurrentFilteredSockets[SocketIndex].GetTranslation();
			FQuat SocketRotation = CurrentFilteredSockets[SocketIndex].GetRotation();
			FVector SocketScale = CurrentFilteredSockets[SocketIndex].GetScale3D();

			const bool bApplyTransform = ApplyWorldTransform.GetAndAdvance().GetValue();
			if (bApplyTransform)
			{
				SocketTranslation = InstanceTransform.TransformPosition(SocketTranslation);
				SocketRotation = InstanceRotation * SocketRotation;
				SocketScale = InstanceTransform.TransformVector(SocketScale);
			}

			*OutSocketTranslateX.GetDestAndAdvance() = SocketTranslation.X;
			*OutSocketTranslateY.GetDestAndAdvance() = SocketTranslation.Y;
			*OutSocketTranslateZ.GetDestAndAdvance() = SocketTranslation.Z;

			*OutSocketRotationX.GetDestAndAdvance() = SocketRotation.X;
			*OutSocketRotationY.GetDestAndAdvance() = SocketRotation.Y;
			*OutSocketRotationZ.GetDestAndAdvance() = SocketRotation.Z;
			*OutSocketRotationW.GetDestAndAdvance() = SocketRotation.W;

			*OutSocketScaleX.GetDestAndAdvance() = SocketScale.X;
			*OutSocketScaleY.GetDestAndAdvance() = SocketScale.Y;
			*OutSocketScaleZ.GetDestAndAdvance() = SocketScale.Z;
		}
	}
	else
	{
		for (int32 i=0; i < Context.NumInstances; ++i)
		{
			*OutSocketTranslateX.GetDestAndAdvance() = 0.0f;
			*OutSocketTranslateY.GetDestAndAdvance() = 0.0f;
			*OutSocketTranslateZ.GetDestAndAdvance() = 0.0f;

			*OutSocketRotationX.GetDestAndAdvance() = FQuat::Identity.X;
			*OutSocketRotationY.GetDestAndAdvance() = FQuat::Identity.Y;
			*OutSocketRotationZ.GetDestAndAdvance() = FQuat::Identity.Z;
			*OutSocketRotationW.GetDestAndAdvance() = FQuat::Identity.W;

			*OutSocketScaleX.GetDestAndAdvance() = 1.0f;
			*OutSocketScaleY.GetDestAndAdvance() = 1.0f;
			*OutSocketScaleZ.GetDestAndAdvance() = 1.0f;
		}
	}
}

void UNiagaraDataInterfaceSkeletalMesh::RandomFilteredSocket(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	FNDIRandomHelper RandHelper(Context);

	VectorVM::FExternalFuncRegisterHandler<int32> OutSocketBone(Context);
	const int32 FilteredSocketBoneOffset = InstData->FilteredSocketBoneOffset;

	int32 Max = FilteredSockets.Num() - 1;
	if (Max != INDEX_NONE)
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			RandHelper.GetAndAdvance();
			const int32 SocketIndex = RandHelper.RandRange(i, 0, Max);
			*OutSocketBone.GetDestAndAdvance() = FilteredSocketBoneOffset + SocketIndex;
		}
	}
	else
	{
		FMemory::Memset(OutSocketBone.GetDest(), 0xFF, Context.NumInstances * sizeof(int32));
	}
}

void UNiagaraDataInterfaceSkeletalMesh::RandomFilteredSocketOrBone(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	FNDIRandomHelper RandHelper(Context);

	VectorVM::FExternalFuncRegisterHandler<int32> OutBoneIndex(Context);

	const int32 Max = FilteredSockets.Num() + InstData->NumFilteredBones - 1;
	if (Max >= 0)
	{
		const int32 NumFilteredBones = InstData->NumFilteredBones;
		const int32 FilteredSocketBoneOffset = InstData->FilteredSocketBoneOffset;
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			RandHelper.GetAndAdvance();
			const int32 FilteredIndex = RandHelper.RandRange(i, 0, Max);
			if (FilteredIndex < NumFilteredBones)
			{
				*OutBoneIndex.GetDestAndAdvance() = InstData->FilteredAndUnfilteredBones[FilteredIndex];
			}
			else
			{
				*OutBoneIndex.GetDestAndAdvance() = FilteredSocketBoneOffset + FilteredIndex - NumFilteredBones;
			}
		}
	}
	else
	{
		FMemory::Memset(OutBoneIndex.GetDest(), 0xFF, Context.NumInstances * sizeof(int32));
	}
}

void UNiagaraDataInterfaceSkeletalMesh::GetFilteredSocketOrBoneCount(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<int32> OutCount(Context);
	const int32 Count = FilteredSockets.Num() + InstData->NumFilteredBones;
	for (int32 i=0; i < Context.NumInstances; ++i)
	{
		*OutCount.GetDestAndAdvance() = Count;
	}
}

void UNiagaraDataInterfaceSkeletalMesh::GetFilteredSocketOrBoneBoneAt(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	VectorVM::FExternalFuncInputHandler<int32> IndexParam(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutBoneIndex(Context);

	const int32 Max = FilteredSockets.Num() + InstData->NumFilteredBones - 1;
	if (Max >= 0)
	{
		const int32 NumFilteredBones = InstData->NumFilteredBones;
		const int32 FilteredSocketBoneOffset = InstData->FilteredSocketBoneOffset;
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			const int32 FilteredIndex = IndexParam.GetAndAdvance();
			if (FilteredIndex < NumFilteredBones)
			{
				*OutBoneIndex.GetDestAndAdvance() = InstData->FilteredAndUnfilteredBones[FilteredIndex];
			}
			else
			{
				*OutBoneIndex.GetDestAndAdvance() = FilteredSocketBoneOffset + FilteredIndex - NumFilteredBones;
			}
		}
	}
	else
	{
		FMemory::Memset(OutBoneIndex.GetDest(), 0xFF, Context.NumInstances * sizeof(int32));
	}
}

#undef LOCTEXT_NAMESPACE
