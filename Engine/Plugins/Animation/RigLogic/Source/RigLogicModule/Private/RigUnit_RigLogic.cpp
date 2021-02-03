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

static TSharedPtr<FSharedRigRuntimeContext> GetSharedRigRuntimeContext(TWeakObjectPtr<USkeletalMeshComponent> SkelMeshComponent)
{
	if (SkelMeshComponent.IsValid())
	{
		USkeletalMesh* SkelMesh = SkelMeshComponent->SkeletalMesh;
		UAssetUserData* UserData = SkelMesh->GetAssetUserDataOfClass(UDNAAsset::StaticClass());
		if (UserData == nullptr)
		{
			return nullptr;
		}
		UDNAAsset* DNAAsset = Cast<UDNAAsset>(UserData);
		return DNAAsset->GetSharedRigRuntimeContext();
	}

	return nullptr;
}

FRigUnit_RigLogic_Data::FRigUnit_RigLogic_Data()
: SkelMeshComponent(nullptr)
, CurrentLOD(0)
, SharedRigRuntimeContext(nullptr)
, RigInstance(nullptr)
{
}

FRigUnit_RigLogic_Data::~FRigUnit_RigLogic_Data()
{
}

FRigUnit_RigLogic_Data::FRigUnit_RigLogic_Data(const FRigUnit_RigLogic_Data& Other)
{
	*this = Other;
}

FRigUnit_RigLogic_Data& FRigUnit_RigLogic_Data::operator=(const FRigUnit_RigLogic_Data& Other)
{
	RigInstance = nullptr;
	SkelMeshComponent = Other.SkelMeshComponent;
	CurrentLOD = Other.CurrentLOD;
	SharedRigRuntimeContext = Other.SharedRigRuntimeContext;
	UpdatedJoints = Other.UpdatedJoints;
	return *this;
}

bool FRigUnit_RigLogic_Data::IsRigLogicInitialized(FSharedRigRuntimeContext* Context)
{
	return (Context != nullptr) && Context->RigLogic.IsValid() && RigInstance.IsValid();
}

void FRigUnit_RigLogic_Data::InitializeRigLogic(FSharedRigRuntimeContext* Context, const FRigBoneHierarchy* BoneHierarchy, const FRigCurveContainer* CurveContainer)
{
	const IBehaviorReader* DNABehavior = Context->BehaviorReader.Get();
	if (DNABehavior == nullptr || DNABehavior->GetJointCount() == 0u)
	{
		UE_LOG(LogRigLogicUnit, Warning, TEXT("Empty DNA file detected, abort initialization."));
		return;
	}

	if (!Context->RigLogic.IsValid())
	{
		Context->RigLogic = MakeShared<FRigLogic>(DNABehavior);

		MapJoints(Context, BoneHierarchy);
		MapInputCurveIndices(Context, CurveContainer);
		MapMorphTargets(Context, CurveContainer);
		MapMaskMultipliers(Context, CurveContainer);

		RigInstance = nullptr;
	}

	if (!RigInstance.IsValid())
	{
		RigInstance = MakeUnique<FRigInstance>(Context->RigLogic.Get());
		RigInstance->SetLOD(CurrentLOD);

		UpdatedJoints.SetNumZeroed(DNABehavior->GetJointCount());
	}
}

void FRigUnit_RigLogic_Data::ChangeRigLogicLODIfNeeded()
{
	if (!SkelMeshComponent.IsValid())
	{
		return;
	}

	int32 SkelMeshPredictedLOD = SkelMeshComponent->PredictedLODLevel;
	// Set current LOD to RigLogic only if it changed.
	if (CurrentLOD != SkelMeshPredictedLOD)
	{
		CurrentLOD = SkelMeshPredictedLOD;
		check(RigInstance.IsValid());
		RigInstance->SetLOD(CurrentLOD);
	}
}

//maps indices of input curves from dna file to control rig curves
void FRigUnit_RigLogic_Data::MapInputCurveIndices(FSharedRigRuntimeContext* Context, const FRigCurveContainer* CurveContainer)
{
	Context->InputCurveIndices.Reset();

	const IBehaviorReader* DNABehavior = Context->BehaviorReader.Get();
	const uint32 ControlCount = DNABehavior->GetRawControlCount();
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
		Context->InputCurveIndices.Add(CurveIndex); //can be INDEX_NONE
	}
}

void FRigUnit_RigLogic_Data::MapJoints(FSharedRigRuntimeContext* Context, const FRigBoneHierarchy* Hierarchy)
{
	const IBehaviorReader* DNABehavior = Context->BehaviorReader.Get();
	const uint16 JointCount = DNABehavior->GetJointCount();
	Context->HierarchyBoneIndices.Reset(JointCount);
	for (uint16 JointIndex = 0; JointIndex < JointCount ; ++JointIndex)
	{
		const FString RLJointName = DNABehavior->GetJointName(JointIndex);
		const FName JointFName = FName(*RLJointName);
		const int32 BoneIndex = Hierarchy->GetIndex(JointFName);
		Context->HierarchyBoneIndices.Add(BoneIndex);
	}
}

void FRigUnit_RigLogic_Data::MapMorphTargets(FSharedRigRuntimeContext* Context, const FRigCurveContainer* CurveContainer)
{
	const IBehaviorReader* DNABehavior = Context->BehaviorReader.Get();
	const uint16 LODCount = DNABehavior->GetLODCount();

	Context->MorphTargetCurveIndices.Reset();
	Context->MorphTargetCurveIndices.AddZeroed(LODCount);
	Context->BlendShapeIndices.Reset();
	Context->BlendShapeIndices.AddZeroed(LODCount);

	for (uint16 LodIndex = 0; LodIndex < LODCount; ++LodIndex)
	{
		TArrayView<const uint16> BlendShapeChannelIndicesForLOD =
			DNABehavior->GetMeshBlendShapeChannelMappingIndicesForLOD(LodIndex);
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
			Context->MorphTargetCurveIndices[LodIndex].Values.Add(MorphTargetIndex);
			Context->BlendShapeIndices[LodIndex].Values.Add(Mapping.BlendShapeChannelIndex);
		}
	}
}

void FRigUnit_RigLogic_Data::MapMaskMultipliers(FSharedRigRuntimeContext* Context, const FRigCurveContainer* CurveContainer)
{
	const IBehaviorReader* DNABehavior = Context->BehaviorReader.Get();
	const uint16 LODCount = DNABehavior->GetLODCount();
	Context->CurveContainerIndicesForAnimMaps.Reset();
	Context->CurveContainerIndicesForAnimMaps.AddZeroed(LODCount);

	Context->RigLogicIndicesForAnimMaps.Reset();
	Context->RigLogicIndicesForAnimMaps.AddZeroed(LODCount);

	for (uint16 LodIndex = 0; LodIndex < LODCount; ++LodIndex)
	{		
		TArrayView<const uint16> AnimMapIndicesPerLOD = DNABehavior->GetAnimatedMapIndicesForLOD(LodIndex);
		for (uint16 AnimMapIndexPerLOD: AnimMapIndicesPerLOD)
		{
			const FString AnimMapNameFStr = DNABehavior->GetAnimatedMapName(AnimMapIndexPerLOD);
			const FString MaskMultiplierNameStr = ConstructCurveName(AnimMapNameFStr, TEXT("<obj>_<attr>"));
			if (MaskMultiplierNameStr == "") {
				return;
			}
			const FName MaskMultiplierFName(*MaskMultiplierNameStr);

			const int32 CurveIndex = CurveContainer->GetIndex(MaskMultiplierFName);
			Context->CurveContainerIndicesForAnimMaps[LodIndex].Values.Add(CurveIndex); //can be INDEX_NONE if curve was not found
			Context->RigLogicIndicesForAnimMaps[LodIndex].Values.Add(AnimMapIndexPerLOD);
		}
	}
}

void FRigUnit_RigLogic_Data::CalculateRigLogic(FSharedRigRuntimeContext* Context, const FRigCurveContainer* CurveContainer)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RigUnit_RigLogic_Calculate);
	const int32 RawControlCount = RigInstance->GetRawControlCount();
	for (int32 ControlIndex = 0; ControlIndex < RawControlCount; ++ControlIndex)
	{
		const uint32 CurveIndex = Context->InputCurveIndices[ControlIndex];
		const float Value = CurveContainer->GetValue(CurveIndex);
		RigInstance->SetRawControl(ControlIndex, Value);
	}

	Context->RigLogic->Calculate(RigInstance.Get());
}

void FRigUnit_RigLogic_Data::UpdateJoints(FSharedRigRuntimeContext* Context, FRigHierarchyContainer* Hierarchy, const FRigUnit_RigLogic_JointUpdateParams& JointUpdateParams)
{
	const uint16 JointCount = UpdatedJoints.Num();
	for (uint16 JointIndex = 0; JointIndex < JointCount; ++JointIndex)
	{
		UpdatedJoints[JointIndex] = false;
	}

	for (const uint16 AttrIndex : JointUpdateParams.VariableAttributes)
	{
		const uint16 JointIndex = AttrIndex / MAX_ATTRS_PER_JOINT;
		if (!UpdatedJoints[JointIndex]) // Check if joint is already updated. It can happen max 9 times because there is 9 attributes for each joint.
		{
			UpdatedJoints[JointIndex] = true;
			const int32 BoneIndex = Context->HierarchyBoneIndices[JointIndex];
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
	}

	Hierarchy->BoneHierarchy.RecomputeGlobalTransforms();
}

void FRigUnit_RigLogic_Data::UpdateBlendShapeCurves(FSharedRigRuntimeContext* Context, FRigCurveContainer* CurveContainer, TArrayView<const float>& BlendShapeValues)
{
	// set output blend shapes
	if (Context->BlendShapeIndices.IsValidIndex(CurrentLOD) && Context->MorphTargetCurveIndices.IsValidIndex(CurrentLOD))
	{
		const uint32 BlendShapePerLODCount = static_cast<uint32>(Context->BlendShapeIndices[CurrentLOD].Values.Num());

		if (ensure(BlendShapePerLODCount == Context->MorphTargetCurveIndices[CurrentLOD].Values.Num()))
		{
			for (uint32 MeshBlendIndex = 0; MeshBlendIndex < BlendShapePerLODCount; MeshBlendIndex++)
			{
				const int32 BlendShapeIndex = Context->BlendShapeIndices[CurrentLOD].Values[MeshBlendIndex];

				const int32 MorphTargetCurveIndex = Context->MorphTargetCurveIndices[CurrentLOD].Values[MeshBlendIndex];
				if (MorphTargetCurveIndex != INDEX_NONE)
				{
					float Value = BlendShapeValues[BlendShapeIndex];
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

void FRigUnit_RigLogic_Data::UpdateAnimMapCurves(FSharedRigRuntimeContext* Context, FRigCurveContainer* CurveContainer, TArrayView<const float>& AnimMapOutputs)
{
	// set output mask multipliers
	// In case curves are not imported yet into CL, AnimatedMapsCurveIndices will be empty, so we need to check
	// array bounds before trying to access it:
	if (Context->RigLogicIndicesForAnimMaps.IsValidIndex(CurrentLOD) && Context->CurveContainerIndicesForAnimMaps.IsValidIndex(CurrentLOD))
	{
		uint32 AnimMapPerLODCount = Context->RigLogicIndicesForAnimMaps[CurrentLOD].Values.Num();

		for (uint32 AnimMapIndexForLOD = 0; AnimMapIndexForLOD < AnimMapPerLODCount; ++AnimMapIndexForLOD)
		{
			const int32 RigLogicAnimMapIndex = Context->RigLogicIndicesForAnimMaps[CurrentLOD].Values[AnimMapIndexForLOD];
			const int32 CurveContainerAnimMapIndex = Context->CurveContainerIndicesForAnimMaps[CurrentLOD].Values[AnimMapIndexForLOD];
			if (CurveContainerAnimMapIndex != INDEX_NONE)
			{
				float Value = AnimMapOutputs[RigLogicAnimMapIndex];
				CurveContainer->SetValue(CurveContainerAnimMapIndex, Value);
			}
		}
	}
	else
	{
		UE_LOG(LogRigLogicUnit, Warning, TEXT("Invalid LOD Index for the AnimationMaps. Ensure your curve is set up correctly!"));
	}	
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
				if (Data.SkelMeshComponent == nullptr)
				{
					Data.SkelMeshComponent = Context.DataSourceRegistry->RequestSource<USkeletalMeshComponent>(UControlRig::OwnerComponent);
					// In normal execution, Data.SkelMeshComponent will be nullptr at the beginning
					// however, during unit testing we cannot fetch it from DataSourceRegistry 
					// in that case, a mock version will be inserted into Data by unit test beforehand
				}
				if (Data.SkelMeshComponent == nullptr || Data.SkelMeshComponent->SkeletalMesh == nullptr)
				{
					return;
				}
				Data.CurrentLOD = Data.SkelMeshComponent->PredictedLODLevel;

				// Fetch shared runtime context of rig from DNAAsset
				TSharedPtr<FSharedRigRuntimeContext> SharedRigRuntimeContext = GetSharedRigRuntimeContext(Data.SkelMeshComponent);
				Data.SharedRigRuntimeContext = SharedRigRuntimeContext;
				// Context is initialized with a BehaviorReader, which can be imported into SkeletalMesh from DNA file
				// or overwritten by GeneSplicer when making a new character
				FRigCurveContainer* CurveContainer = ExecuteContext.GetCurves();
				Data.InitializeRigLogic(SharedRigRuntimeContext.Get(), &Hierarchy->BoneHierarchy, CurveContainer);
				break; 
			}
			case EControlRigState::Update:
			{
				TSharedPtr<FSharedRigRuntimeContext> SharedRigRuntimeContext = Data.SharedRigRuntimeContext.Pin();
				FRigCurveContainer* CurveContainer = ExecuteContext.GetCurves();

				if (!Data.IsRigLogicInitialized(SharedRigRuntimeContext.Get()) || CurveContainer == nullptr)
				{
					return;
				}
				Data.ChangeRigLogicLODIfNeeded();
				Data.CalculateRigLogic(SharedRigRuntimeContext.Get(), CurveContainer);

				//Filing a struct so we can call the same method for updating joints from tests
				FRigUnit_RigLogic_JointUpdateParams JointUpdateParamsTemp
				(
					SharedRigRuntimeContext->RigLogic->GetJointVariableAttributeIndices(Data.CurrentLOD),
					SharedRigRuntimeContext->RigLogic->GetNeutralJointValues(),
					Data.RigInstance->GetJointOutputs()
				);

				Data.UpdateJoints(SharedRigRuntimeContext.Get(), Hierarchy, JointUpdateParamsTemp);

				TArrayView<const float> BlendShapeValues = Data.RigInstance->GetBlendShapeOutputs();
				Data.UpdateBlendShapeCurves(SharedRigRuntimeContext.Get(), CurveContainer, BlendShapeValues);

				TArrayView<const float> AnimMapOutputs = Data.RigInstance->GetAnimatedMapOutputs();
				Data.UpdateAnimMapCurves(SharedRigRuntimeContext.Get(), CurveContainer, AnimMapOutputs);
			}
			default:
			{
				break;
			}
		}
	}
}
