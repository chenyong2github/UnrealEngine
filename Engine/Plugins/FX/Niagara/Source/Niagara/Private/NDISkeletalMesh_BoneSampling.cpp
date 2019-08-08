// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, IsValidBone)
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetSpecificBoneAt)

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetSpecificSocketBoneAt)

const FName FSkeletalMeshInterfaceHelper::GetSkinnedBoneDataName("GetSkinnedBoneData");
const FName FSkeletalMeshInterfaceHelper::GetSkinnedBoneDataWSName("GetSkinnedBoneDataWS");
const FName FSkeletalMeshInterfaceHelper::GetSkinnedBoneDataInterpolatedName("GetSkinnedBoneDataInterpolated");
const FName FSkeletalMeshInterfaceHelper::GetSkinnedBoneDataWSInterpolatedName("GetSkinnedBoneDataWSInterpolated");
const FName FSkeletalMeshInterfaceHelper::RandomSpecificBoneName("RandomSpecificBone");
const FName FSkeletalMeshInterfaceHelper::IsValidBoneName("IsValidBoneName");
const FName FSkeletalMeshInterfaceHelper::GetSpecificBoneCountName("GetSpecificBoneCount");
const FName FSkeletalMeshInterfaceHelper::GetSpecificBoneAtName("GetSpecificBone");
const FName FSkeletalMeshInterfaceHelper::RandomSpecificSocketBoneName("RandomSpecificSocketBone");
const FName FSkeletalMeshInterfaceHelper::GetSpecificSocketCountName("GetSpecificSocketCount");
const FName FSkeletalMeshInterfaceHelper::GetSpecificSocketBoneAtName("GetSpecificSocketBone");

void UNiagaraDataInterfaceSkeletalMesh::GetSkeletonSamplingFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	//////////////////////////////////////////////////////////////////////////
	// Bone functions.

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::RandomSpecificBoneName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef() , TEXT("Bone")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
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
		Sig.Name = FSkeletalMeshInterfaceHelper::GetSpecificBoneCountName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Count")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetSpecificBoneCountDesc", "Returns the number of specific bones in the DI list.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetSpecificBoneAtName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Bone Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Bone")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetSpecificBoneAtDesc", "Gets the bone at the passed index in the DI's specfic bones list.");
#endif
		OutFunctions.Add(Sig);
	}

	//////////////////////////////////////////////////////////////////////////
	//Socket functions

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::RandomSpecificSocketBoneName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Socket Bone")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("RandomSpecificSocketBoneDesc", "Gets the bone for a random socket in the DI's specific socket list.");
#endif
		OutFunctions.Add(Sig);
	}
	
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetSpecificSocketCountName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Count")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetSpecificSocketCountDesc", "Returns the number of specific Sockets in the DI list.");
#endif
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FSkeletalMeshInterfaceHelper::GetSpecificSocketBoneAtName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Socket Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Socket Bone")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetSpecificSocketBoneAtDesc", "Gets the bone for the socket at the passed index in the DI's specfic socket list.");
#endif
		OutFunctions.Add(Sig);
	}
}

void UNiagaraDataInterfaceSkeletalMesh::BindSkeletonSamplingFunction(const FVMExternalFunctionBindingInfo& BindingInfo, FNDISkeletalMesh_InstanceData* InstanceData, FVMExternalFunction &OutFunc)
{
	//Bone Functions
	if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::RandomSpecificBoneName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		auto Lambda = [this](FVectorVMContext& Context) { this->RandomSpecificBone(Context); }; 
		OutFunc = FVMExternalFunction::CreateLambda(Lambda); 
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::IsValidBoneName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, IsValidBone)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetSkinnedBoneDataName)
	{
		ensure(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 10);
		TSkinningModeBinder<TNDIExplicitBinder<FNDITransformHandlerNoop, TNDIExplicitBinder<TIntegralConstant<bool, false>, NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetSkinnedBoneData)>>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetSkinnedBoneDataWSName)
	{
		ensure(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 10);
		TSkinningModeBinder<TNDIExplicitBinder<FNDITransformHandler, TNDIExplicitBinder<TIntegralConstant<bool, false>, NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetSkinnedBoneData)>>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetSkinnedBoneDataInterpolatedName)
	{
		ensure(BindingInfo.GetNumInputs() == 3 && BindingInfo.GetNumOutputs() == 10);
		TSkinningModeBinder<TNDIExplicitBinder<FNDITransformHandlerNoop, TNDIExplicitBinder<TIntegralConstant<bool, true>, NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetSkinnedBoneData)>>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetSkinnedBoneDataWSInterpolatedName)
	{
		ensure(BindingInfo.GetNumInputs() == 3 && BindingInfo.GetNumOutputs() == 10);
		TSkinningModeBinder<TNDIExplicitBinder<FNDITransformHandler, TNDIExplicitBinder<TIntegralConstant<bool, true>, NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetSkinnedBoneData)>>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetSpecificBoneCountName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		auto Lambda = [this](FVectorVMContext& Context) { this->GetSpecificBoneCount(Context); };
		OutFunc = FVMExternalFunction::CreateLambda(Lambda);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetSpecificBoneAtName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetSpecificBoneAt)::Bind(this, OutFunc);
	}
	//Socket Functions
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::RandomSpecificSocketBoneName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		auto Lambda = [this](FVectorVMContext& Context) { this->RandomSpecificSocketBone(Context); };
		OutFunc = FVMExternalFunction::CreateLambda(Lambda);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetSpecificSocketCountName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		auto Lambda = [this](FVectorVMContext& Context) { this->GetSpecificSocketCount(Context); };
		OutFunc = FVMExternalFunction::CreateLambda(Lambda);
	}
	else if (BindingInfo.Name == FSkeletalMeshInterfaceHelper::GetSpecificSocketBoneAtName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceSkeletalMesh, GetSpecificSocketBoneAt)::Bind(this, OutFunc);
	}
}


//////////////////////////////////////////////////////////////////////////
// Direct sampling from listed sockets and bones.

void UNiagaraDataInterfaceSkeletalMesh::GetSpecificBoneCount(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Bone_Sample);
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<int32> OutCount(Context);

	int32 Num = SpecificBones.Num();
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*OutCount.GetDestAndAdvance() = Num;
	}
}

void UNiagaraDataInterfaceSkeletalMesh::GetSpecificBoneAt(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Bone_Sample);

	VectorVM::FExternalFuncInputHandler<int32> BoneParam(Context);
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<int32> OutBone(Context);
	const TArray<int32>& SpecificBonesArray = InstData->SpecificBones;

	int32 Max = SpecificBones.Num() - 1;
	if (Max != INDEX_NONE)
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			int32 BoneIndex = FMath::Clamp(BoneParam.GetAndAdvance(), 0, Max);
			*OutBone.GetDestAndAdvance() = SpecificBonesArray[BoneIndex];
		}
	}
	else
	{
		FMemory::Memset(OutBone.GetDest(), 0xFF, Context.NumInstances * sizeof(int32));
	}
}

void UNiagaraDataInterfaceSkeletalMesh::RandomSpecificBone(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Bone_Sample);

	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<int32> OutBone(Context);
	const TArray<int32>& SpecificBonesArray = InstData->SpecificBones;

	int32 Max = SpecificBones.Num() - 1;
	if (Max != INDEX_NONE)
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			int32 BoneIndex = Context.RandStream.RandRange(0, Max);
			*OutBone.GetDestAndAdvance() = SpecificBonesArray[BoneIndex];
		}
	}
	else
	{
		FMemory::Memset(OutBone.GetDest(), 0xFF, Context.NumInstances * sizeof(int32));
	}
}

void UNiagaraDataInterfaceSkeletalMesh::IsValidBone(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Bone_Sample);

	VectorVM::FExternalFuncInputHandler<int32> BoneParam(Context);

	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);
	checkfSlow(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());
	checkfSlow(InstData->Mesh, TEXT("Skeletal Mesh Interface has invalid mesh. %s"), *GetPathName());

	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> OutValid(Context);

	FSkeletalMeshAccessorHelper MeshAccessor;
	MeshAccessor.Init<TIntegralConstant<int32, 0>, TIntegralConstant<int32, 0>>(InstData);
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
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Bone_Sample);
	SkinningHandlerType SkinningHandler;
	TransformHandlerType TransformHandler;
	VectorVM::FExternalFuncInputHandler<int32> BoneParam(Context);
	VectorVM::FExternalFuncInputHandler<float> InterpParam;

	if (bInterpolated::Value)
	{
		InterpParam.Init(Context);
	}

	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);

	checkfSlow(InstData.Get(), TEXT("Skeletal Mesh Interface has invalid instance data. %s"), *GetPathName());
	checkfSlow(InstData->Mesh, TEXT("Skeletal Mesh Interface has invalid mesh. %s"), *GetPathName());

	//TODO: Replace this by storing off FTransforms and doing a proper lerp to get a final transform.
	//Also need to pull in a per particle interpolation factor.
	const FMatrix& Transform = InstData->Transform;
	const FMatrix& PrevTransform = InstData->PrevTransform;

	FBoneSocketSkinnedDataOutputHandler Output(Context);

	FSkinWeightVertexBuffer* SkinWeightBuffer;
	FSkeletalMeshLODRenderData& LODData = InstData->GetLODRenderDataAndSkinWeights(SkinWeightBuffer);

	FSkeletalMeshAccessorHelper Accessor;
	Accessor.Init<TIntegralConstant<int32, 0>, TIntegralConstant<int32, 0>>(InstData);

	const FReferenceSkeleton& RefSkel = Accessor.Mesh->RefSkeleton;

	const int32 BoneMax = RefSkel.GetNum() - 1;
	const int32 BoneAndSocketMax = BoneMax + InstData->SpecificSockets.Num();
	float InvDt = 1.0f / InstData->DeltaSeconds;

	const int32 SpecificSocketBoneOffset = InstData->SpecificSocketBoneOffset;
	const TArray<FTransform>& SpecificSocketCurrTransforms = InstData->GetSpecificSocketsCurrBuffer();
	const TArray<FTransform>& SpecificSocketPrevTransforms = InstData->GetSpecificSocketsPrevBuffer();

	FVector BonePos;
	FVector BonePrev;

	FVector Pos;
	FVector Prev;
	FVector Velocity;

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		const float Interp = bInterpolated::Value ? InterpParam.GetAndAdvance() : 1.0f;

		// Determine bone or socket
		int32 RawBone = BoneParam.GetAndAdvance();
		int32 Bone = FMath::Clamp(RawBone, 0, BoneAndSocketMax);
		const bool bIsSocket = Bone > BoneMax;
		const int32 Socket = Bone - SpecificSocketBoneOffset;

		// Handle edge cases first...
		if ((!bIsSocket && Bone >= BoneMax) ||
			(bIsSocket && (Socket >= SpecificSocketCurrTransforms.Num() || Socket < 0)))
		{
			Pos = FVector::ZeroVector;
			TransformHandler.TransformPosition(Pos, Transform);

			if (Output.bNeedsVelocity || bInterpolated::Value)
			{
				Prev = FVector::ZeroVector;
				TransformHandler.TransformPosition(Prev, PrevTransform);
			}
			if (Output.bNeedsRotation)
			{
				FQuat Rotation = FQuat::Identity;
				if (bInterpolated::Value)
				{
					FQuat PrevRotation = FQuat::Identity;
					Rotation = FQuat::Identity;
				}

				Output.SetRotation(Rotation);
			}
		}
		else if ( bIsSocket )
		{
			FTransform CurrSocketTransform = SpecificSocketCurrTransforms[Socket];
			FTransform PrevSocketTransform = SpecificSocketPrevTransforms[Socket];

			Pos = CurrSocketTransform.GetLocation();
			TransformHandler.TransformPosition(Pos, Transform);

			if (Output.bNeedsVelocity || bInterpolated::Value)
			{
				Prev = PrevSocketTransform.GetLocation();
				TransformHandler.TransformPosition(Prev, PrevTransform);
			}

			if (Output.bNeedsRotation)
			{
				FQuat Rotation = CurrSocketTransform.GetRotation();
				if (bInterpolated::Value)
				{
					FQuat PrevRotation = PrevSocketTransform.GetRotation();
					Rotation = FMath::Lerp(PrevRotation, Rotation, Interp);
				}

				Output.SetRotation(Rotation);
			}
		}
		// Bone
		else
		{
			Pos = SkinningHandler.GetSkinnedBonePosition(Accessor, Bone);
			TransformHandler.TransformPosition(Pos, Transform);

			if (Output.bNeedsVelocity || bInterpolated::Value)
			{
				Prev = SkinningHandler.GetSkinnedBonePreviousPosition(Accessor, Bone);
				TransformHandler.TransformPosition(Prev, PrevTransform);
			}

			if (Output.bNeedsRotation)
			{
				FQuat Rotation = SkinningHandler.GetSkinnedBoneRotation(Accessor, Bone);
				if (bInterpolated::Value)
				{
					FQuat PrevRotation = SkinningHandler.GetSkinnedBonePreviousRotation(Accessor, Bone);
					Rotation = FMath::Lerp(PrevRotation, Rotation, Interp);
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
			Velocity = (Pos - Prev) * InvDt;
			Output.SetVelocity(Velocity);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// Sockets

void UNiagaraDataInterfaceSkeletalMesh::GetSpecificSocketCount(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Bone_Sample);
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<int32> OutCount(Context);

	const int32 Num = InstData->SpecificSockets.Num();
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*OutCount.GetDestAndAdvance() = Num;
	}
}

void UNiagaraDataInterfaceSkeletalMesh::GetSpecificSocketBoneAt(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Bone_Sample);

	VectorVM::FExternalFuncInputHandler<int32> SocketParam(Context);
	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<int32> OutSocketBone(Context);
	const TArray<FName>& SpecificSocketsArray = InstData->SpecificSockets;
	const int32 SpecificSocketBoneOffset = InstData->SpecificSocketBoneOffset;

	int32 Max = SpecificSockets.Num() - 1;
	if (Max != INDEX_NONE)
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			const int32 SocketIndex = FMath::Clamp(SocketParam.GetAndAdvance(), 0, Max);
			*OutSocketBone.GetDestAndAdvance() = SpecificSocketBoneOffset + SocketIndex;
		}
	}
	else
	{
		FMemory::Memset(OutSocketBone.GetDest(), 0xFF, Context.NumInstances * sizeof(int32));
	}
}

void UNiagaraDataInterfaceSkeletalMesh::RandomSpecificSocketBone(FVectorVMContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_Bone_Sample);

	VectorVM::FUserPtrHandler<FNDISkeletalMesh_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<int32> OutSocketBone(Context);
	const TArray<FName>& SpecificSocketsArray = InstData->SpecificSockets;
	const int32 SpecificSocketBoneOffset = InstData->SpecificSocketBoneOffset;

	int32 Max = SpecificSockets.Num() - 1;
	if (Max != INDEX_NONE)
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			const int32 SocketIndex = Context.RandStream.RandRange(0, Max);
			*OutSocketBone.GetDestAndAdvance() = SpecificSocketBoneOffset + SocketIndex;
		}
	}
	else
	{
		FMemory::Memset(OutSocketBone.GetDest(), 0xFF, Context.NumInstances * sizeof(int32));
	}
}

#undef LOCTEXT_NAMESPACE
