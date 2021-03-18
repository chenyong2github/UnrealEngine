// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_RigLogic.h"

#include "ControlRig.h"
#include "DNAReader.h"
#include "RigInstance.h"
#include "RigLogic.h"
#include "Components/SkeletalMeshComponent.h"
#include "Math/TransformNonVectorized.h"
#include "Units/RigUnitContext.h"

DEFINE_LOG_CATEGORY(LogRigLogicUnit);

const uint8 FRigUnit_RigLogic_Data::MAX_ATTRS_PER_JOINT = 9;

/** Constructs curve name from nameToSplit using formatString of form x<obj>y<attr>z **/
static FString ConstructCurveName(const FString& NameToSplit, const FString& FormatString)
{
	// constructs curve name from NameToSplit (always in form <obj>.<attr>)
	// using FormatString of form x<obj>y<attr>z
	// where x, y and z are arbitrary strings
	// example:
	// FormatString="mesh_<obj>_<attr>"
	// 'head.blink_L' becomes 'mesh_head_blink_L'

	FString ObjectName, AttributeName;
	if (!NameToSplit.Split(".", &ObjectName, &AttributeName))
	{

		UE_LOG(LogRigLogicUnit, Error, TEXT("RigUnit_R: Missing '.' in '%s'"),
			*NameToSplit);
		return TEXT("");
	}

	FString CurveName = FormatString;
	CurveName = CurveName.Replace(TEXT("<obj>"), *ObjectName);
	CurveName = CurveName.Replace(TEXT("<attr>"), *AttributeName);
	return CurveName;
}

FRigUnit_RigLogic_Data::FRigUnit_RigLogic_Data()
: SkelMeshComponent(nullptr)
, SharedRigRuntimeContext(nullptr)
, RigInstance(nullptr)
, CurrentLOD(0)
{
}

FRigUnit_RigLogic_Data::~FRigUnit_RigLogic_Data()
{
	SharedRigRuntimeContext = nullptr;
}

FRigUnit_RigLogic_Data::FRigUnit_RigLogic_Data(const FRigUnit_RigLogic_Data& Other)
{
	*this = Other;
}

FRigUnit_RigLogic_Data& FRigUnit_RigLogic_Data::operator=(const FRigUnit_RigLogic_Data& Other)
{
	SkelMeshComponent = Other.SkelMeshComponent;
	SharedRigRuntimeContext = nullptr;
	RigInstance = nullptr;
	InputCurveIndices = Other.InputCurveIndices;
	HierarchyBoneIndices = Other.HierarchyBoneIndices;
	MorphTargetCurveIndices = Other.MorphTargetCurveIndices;
	BlendShapeIndices = Other.BlendShapeIndices;
	CurveContainerIndicesForAnimMaps = Other.CurveContainerIndicesForAnimMaps;
	RigLogicIndicesForAnimMaps = Other.RigLogicIndicesForAnimMaps;
	CurrentLOD = Other.CurrentLOD;
	return *this;
}

bool FRigUnit_RigLogic_Data::IsRigLogicInitialized()
{
	return (SharedRigRuntimeContext != nullptr) && SharedRigRuntimeContext->RigLogic.IsValid() && RigInstance.IsValid();
}

void FRigUnit_RigLogic_Data::InitializeRigLogic(const FRigBoneHierarchy* BoneHierarchy, const FRigCurveContainer* CurveContainer)
{
	if ((SharedRigRuntimeContext == nullptr) || !SharedRigRuntimeContext->BehaviorReader.IsValid() || SharedRigRuntimeContext->BehaviorReader->GetJointCount() == 0u)
	{
		UE_LOG(LogRigLogicUnit, Warning, TEXT("No valid DNA file found, abort initialization."));
		return;
	}

	if (!SharedRigRuntimeContext->RigLogic.IsValid())
	{
		SharedRigRuntimeContext->RigLogic = MakeShared<FRigLogic>(SharedRigRuntimeContext->BehaviorReader.Get());
		CacheVariableJointIndices();
		RigInstance = nullptr;
	}

	if (!RigInstance.IsValid())
	{
		RigInstance = MakeUnique<FRigInstance>(SharedRigRuntimeContext->RigLogic.Get());
		RigInstance->SetLOD(CurrentLOD);

		MapJoints(BoneHierarchy);
		MapInputCurveIndices(CurveContainer);
		MapMorphTargets(CurveContainer);
		MapMaskMultipliers(CurveContainer);
	}
}

//maps indices of input curves from dna file to control rig curves
void FRigUnit_RigLogic_Data::MapInputCurveIndices(const FRigCurveContainer* CurveContainer)
{
	const IBehaviorReader* DNABehavior = SharedRigRuntimeContext->BehaviorReader.Get();
	const uint32 ControlCount = DNABehavior->GetRawControlCount();
	InputCurveIndices.Reset(ControlCount);
	for (uint32_t ControlIndex = 0; ControlIndex < ControlCount; ++ControlIndex)
	{
		const FString DNAControlName = DNABehavior->GetRawControlName(ControlIndex);
		const FString AnimatedControlName = ConstructCurveName(DNAControlName, TEXT("<obj>_<attr>"));
		if (AnimatedControlName == TEXT(""))
		{
			return;
		}
		const FName ControlFName(*AnimatedControlName);
		const int32 CurveIndex = CurveContainer ? CurveContainer->GetIndex(ControlFName) : INDEX_NONE;
		InputCurveIndices.Add(CurveIndex); //can be INDEX_NONE
	}
}

void FRigUnit_RigLogic_Data::MapJoints(const FRigBoneHierarchy* Hierarchy)
{
	const IBehaviorReader* DNABehavior = SharedRigRuntimeContext->BehaviorReader.Get();
	const uint16 JointCount = DNABehavior->GetJointCount();
	HierarchyBoneIndices.Reset(JointCount);
	for (uint16 JointIndex = 0; JointIndex < JointCount ; ++JointIndex)
	{
		const FString RLJointName = DNABehavior->GetJointName(JointIndex);
		const FName JointFName = FName(*RLJointName);
		const int32 BoneIndex = Hierarchy->GetIndex(JointFName);
		HierarchyBoneIndices.Add(BoneIndex);
	}
}

void FRigUnit_RigLogic_Data::CacheVariableJointIndices()
{
	const IBehaviorReader* DNABehavior = SharedRigRuntimeContext->BehaviorReader.Get();
	const uint16 LODCount = DNABehavior->GetLODCount();
	SharedRigRuntimeContext->VariableJointIndices.Reset();
	SharedRigRuntimeContext->VariableJointIndices.AddDefaulted(LODCount);
	TSet<uint16> DistinctVariableJointIndices;
	for (uint16 LodIndex = 0; LodIndex < LODCount; ++LodIndex)
	{
		TArrayView<const uint16> VariableAttributeIndices = DNABehavior->GetJointVariableAttributeIndices(LodIndex);
		DistinctVariableJointIndices.Reset();
		DistinctVariableJointIndices.Reserve(VariableAttributeIndices.Num());
		for (const uint16 AttrIndex : VariableAttributeIndices)
		{
			const uint16 JointIndex = AttrIndex / MAX_ATTRS_PER_JOINT;
			DistinctVariableJointIndices.Add(JointIndex);
		}
		SharedRigRuntimeContext->VariableJointIndices[LodIndex].Values = DistinctVariableJointIndices.Array();
	}
}

void FRigUnit_RigLogic_Data::MapMorphTargets(const FRigCurveContainer* CurveContainer)
{
	const IBehaviorReader* DNABehavior = SharedRigRuntimeContext->BehaviorReader.Get();
	const uint16 LODCount = DNABehavior->GetLODCount();

	MorphTargetCurveIndices.Reset();
	MorphTargetCurveIndices.AddDefaulted(LODCount);
	BlendShapeIndices.Reset();
	BlendShapeIndices.AddDefaulted(LODCount);

	for (uint16 LodIndex = 0; LodIndex < LODCount; ++LodIndex)
	{
		TArrayView<const uint16> BlendShapeChannelIndicesForLOD =
			DNABehavior->GetMeshBlendShapeChannelMappingIndicesForLOD(LodIndex);
		MorphTargetCurveIndices[LodIndex].Values.Reserve(BlendShapeChannelIndicesForLOD.Num());
		BlendShapeIndices[LodIndex].Values.Reserve(BlendShapeChannelIndicesForLOD.Num());
		for (uint16 MappingIndex: BlendShapeChannelIndicesForLOD)
		{
			const FMeshBlendShapeChannelMapping Mapping = DNABehavior->GetMeshBlendShapeChannelMapping(MappingIndex);
			const uint16 BlendShapeIndex = Mapping.BlendShapeChannelIndex;
			const uint16 MeshIndex = Mapping.MeshIndex;
			const FString BlendShapeStr = DNABehavior->GetBlendShapeChannelName(BlendShapeIndex);
			const FString MeshStr = DNABehavior->GetMeshName(MeshIndex);
			const FString MorphTargetStr = MeshStr + TEXT("__") + BlendShapeStr;
			const FName MorphTargetName(*MorphTargetStr);
			const int32 MorphTargetIndex = CurveContainer->GetIndex(MorphTargetName);
			MorphTargetCurveIndices[LodIndex].Values.Add(MorphTargetIndex);
			BlendShapeIndices[LodIndex].Values.Add(Mapping.BlendShapeChannelIndex);
		}
	}
}

void FRigUnit_RigLogic_Data::MapMaskMultipliers(const FRigCurveContainer* CurveContainer)
{
	const IBehaviorReader* DNABehavior = SharedRigRuntimeContext->BehaviorReader.Get();
	const uint16 LODCount = DNABehavior->GetLODCount();
	CurveContainerIndicesForAnimMaps.Reset();
	CurveContainerIndicesForAnimMaps.AddDefaulted(LODCount);

	RigLogicIndicesForAnimMaps.Reset();
	RigLogicIndicesForAnimMaps.AddDefaulted(LODCount);

	for (uint16 LodIndex = 0; LodIndex < LODCount; ++LodIndex)
	{		
		TArrayView<const uint16> AnimMapIndicesPerLOD = DNABehavior->GetAnimatedMapIndicesForLOD(LodIndex);
		CurveContainerIndicesForAnimMaps[LodIndex].Values.Reserve(AnimMapIndicesPerLOD.Num());
		RigLogicIndicesForAnimMaps[LodIndex].Values.Reserve(AnimMapIndicesPerLOD.Num());
		for (uint16 AnimMapIndexPerLOD: AnimMapIndicesPerLOD)
		{
			const FString AnimMapNameFStr = DNABehavior->GetAnimatedMapName(AnimMapIndexPerLOD);
			const FString MaskMultiplierNameStr = ConstructCurveName(AnimMapNameFStr, TEXT("<obj>_<attr>"));
			if (MaskMultiplierNameStr == "") {
				return;
			}
			const FName MaskMultiplierFName(*MaskMultiplierNameStr);
			const int32 CurveIndex = CurveContainer->GetIndex(MaskMultiplierFName);
			CurveContainerIndicesForAnimMaps[LodIndex].Values.Add(CurveIndex); //can be INDEX_NONE if curve was not found
			RigLogicIndicesForAnimMaps[LodIndex].Values.Add(AnimMapIndexPerLOD);
		}
	}
}

void FRigUnit_RigLogic_Data::CalculateRigLogic(const FRigCurveContainer* CurveContainer)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RigUnit_RigLogic_Calculate);

	// LOD change is inexpensive
	RigInstance->SetLOD(CurrentLOD);

	const int32 RawControlCount = RigInstance->GetRawControlCount();
	for (int32 ControlIndex = 0; ControlIndex < RawControlCount; ++ControlIndex)
	{
		const uint32 CurveIndex = InputCurveIndices[ControlIndex];
		const float Value = CurveContainer->GetValue(CurveIndex);
		RigInstance->SetRawControl(ControlIndex, Value);
	}
	SharedRigRuntimeContext->RigLogic->Calculate(RigInstance.Get());
}

void FRigUnit_RigLogic_Data::UpdateJoints(FRigHierarchyContainer* Hierarchy, const FRigUnit_RigLogic_JointUpdateParams& JointUpdateParams)
{
	for (const uint16 JointIndex : SharedRigRuntimeContext->VariableJointIndices[CurrentLOD].Values)
	{
		const int32 BoneIndex = HierarchyBoneIndices[JointIndex];
		if (BoneIndex != INDEX_NONE)
		{
			const FTransform& Neutral = JointUpdateParams.NeutralJointTransforms[JointIndex];
			const FTransform& Delta = JointUpdateParams.DeltaTransforms[JointIndex];
			const FTransform Transform
			{
				Neutral.GetRotation() * Delta.GetRotation(),
				Neutral.GetTranslation() + Delta.GetTranslation(),
				Neutral.GetScale3D() + Delta.GetScale3D()  // Neutral scale is always 1.0
			};
			Hierarchy->BoneHierarchy[BoneIndex].LocalTransform = Transform;
		}
	}
	Hierarchy->BoneHierarchy.RecomputeGlobalTransforms();
}

void FRigUnit_RigLogic_Data::UpdateBlendShapeCurves(FRigCurveContainer* CurveContainer, TArrayView<const float> BlendShapeValues)
{
	// set output blend shapes
	if (BlendShapeIndices.IsValidIndex(CurrentLOD) && MorphTargetCurveIndices.IsValidIndex(CurrentLOD))
	{
		const uint32 BlendShapePerLODCount = static_cast<uint32>(BlendShapeIndices[CurrentLOD].Values.Num());

		if (ensure(BlendShapePerLODCount == MorphTargetCurveIndices[CurrentLOD].Values.Num()))
		{
			for (uint32 MeshBlendIndex = 0; MeshBlendIndex < BlendShapePerLODCount; MeshBlendIndex++)
			{
				const int32 BlendShapeIndex = BlendShapeIndices[CurrentLOD].Values[MeshBlendIndex];
				const int32 MorphTargetCurveIndex = MorphTargetCurveIndices[CurrentLOD].Values[MeshBlendIndex];
				if (MorphTargetCurveIndex != INDEX_NONE)
				{
					const float Value = BlendShapeValues[BlendShapeIndex];
					CurveContainer->SetValue(MorphTargetCurveIndex, Value);
				}
			}
		}
	}
	else
	{
		UE_LOG(LogRigLogicUnit, Warning, TEXT("Invalid LOD Index for the BlendShapes. Ensure your curve is set up correctly!"));
	}
}

void FRigUnit_RigLogic_Data::UpdateAnimMapCurves(FRigCurveContainer* CurveContainer, TArrayView<const float> AnimMapOutputs)
{
	// set output mask multipliers
	// In case curves are not imported yet into CL, AnimatedMapsCurveIndices will be empty, so we need to check
	// array bounds before trying to access it:
	if (RigLogicIndicesForAnimMaps.IsValidIndex(CurrentLOD) && CurveContainerIndicesForAnimMaps.IsValidIndex(CurrentLOD))
	{
		const uint32 AnimMapPerLODCount = RigLogicIndicesForAnimMaps[CurrentLOD].Values.Num();
		for (uint32 AnimMapIndexForLOD = 0; AnimMapIndexForLOD < AnimMapPerLODCount; ++AnimMapIndexForLOD)
		{
			const int32 RigLogicAnimMapIndex = RigLogicIndicesForAnimMaps[CurrentLOD].Values[AnimMapIndexForLOD];
			const int32 CurveContainerAnimMapIndex = CurveContainerIndicesForAnimMaps[CurrentLOD].Values[AnimMapIndexForLOD];
			if (CurveContainerAnimMapIndex != INDEX_NONE)
			{
				const float Value = AnimMapOutputs[RigLogicAnimMapIndex];
				CurveContainer->SetValue(CurveContainerAnimMapIndex, Value);
			}
		}
	}
	else
	{
		UE_LOG(LogRigLogicUnit, Warning, TEXT("Invalid LOD Index for the AnimationMaps. Ensure your curve is set up correctly!"));
	}	
}

FSharedRigRuntimeContext* FRigUnit_RigLogic::GetSharedRigRuntimeContext(USkeletalMesh* SkelMesh)
{
	UAssetUserData* UserData = SkelMesh->GetAssetUserDataOfClass(UDNAAsset::StaticClass());
	if (UserData == nullptr)
	{
		return nullptr;
	}
	UDNAAsset* DNAAsset = Cast<UDNAAsset>(UserData);
	return &(DNAAsset->Context);
}

FRigUnit_RigLogic_Execute()
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RigUnit_RigLogic_Execute);

 	FRigHierarchyContainer* Hierarchy = ExecuteContext.Hierarchy;
	if (Hierarchy)
	{
		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				if (!Data.SkelMeshComponent.IsValid())
				{
					Data.SkelMeshComponent = Context.DataSourceRegistry->RequestSource<USkeletalMeshComponent>(UControlRig::OwnerComponent);
					// In normal execution, Data.SkelMeshComponent will be nullptr at the beginning
					// however, during unit testing we cannot fetch it from DataSourceRegistry 
					// in that case, a mock version will be inserted into Data by unit test beforehand
				}
				if (!Data.SkelMeshComponent.IsValid() || Data.SkelMeshComponent->SkeletalMesh == nullptr)
				{
					return;
				}
				Data.CurrentLOD = Data.SkelMeshComponent->GetPredictedLODLevel();

				// Fetch shared runtime context of rig from DNAAsset
				Data.SharedRigRuntimeContext = GetSharedRigRuntimeContext(Data.SkelMeshComponent->SkeletalMesh);
				// Context is initialized with a BehaviorReader, which can be imported into SkeletalMesh from DNA file
				// or overwritten by GeneSplicer when making a new character
				FRigCurveContainer* CurveContainer = ExecuteContext.GetCurves();
				Data.InitializeRigLogic(&Hierarchy->BoneHierarchy, CurveContainer);
				break; 
			}
			case EControlRigState::Update:
			{
				// Fetch shared runtime context of rig from DNAAsset
				FRigCurveContainer* CurveContainer = ExecuteContext.GetCurves();
				if (!Data.SkelMeshComponent.IsValid() || !Data.IsRigLogicInitialized() || CurveContainer == nullptr)
				{
					return;
				}
				Data.CurrentLOD = Data.SkelMeshComponent->GetPredictedLODLevel();
				Data.CalculateRigLogic(CurveContainer);

				//Filing a struct so we can call the same method for updating joints from tests
				FRigUnit_RigLogic_JointUpdateParams JointUpdateParamsTemp
				(
					Data.SharedRigRuntimeContext->RigLogic->GetNeutralJointValues(),
					Data.RigInstance->GetJointOutputs()
				);

				Data.UpdateJoints(Hierarchy, JointUpdateParamsTemp);

				TArrayView<const float> BlendShapeValues = Data.RigInstance->GetBlendShapeOutputs();
				Data.UpdateBlendShapeCurves(CurveContainer, BlendShapeValues);

				TArrayView<const float> AnimMapOutputs = Data.RigInstance->GetAnimatedMapOutputs();
				Data.UpdateAnimMapCurves(CurveContainer, AnimMapOutputs);
			}
			default:
			{
				break;
			}
		}
	}
}
