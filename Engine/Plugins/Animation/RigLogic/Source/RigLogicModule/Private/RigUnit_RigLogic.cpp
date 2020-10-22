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

FRigUnit_RigLogic_Data::FRigUnit_RigLogic_Data()
: SkelMeshComponent(nullptr)
, CurrentLOD(0)
, RigLogic(nullptr)
, RigInstance(nullptr)
{
}

FRigUnit_RigLogic_Data::~FRigUnit_RigLogic_Data()
{
	if (RigInstance != nullptr)
	{
		delete RigInstance;
		RigInstance = nullptr;
	}

	if (RigLogic != nullptr)
	{
		delete RigLogic;
		RigLogic = nullptr;
	}
}

FRigUnit_RigLogic_Data::FRigUnit_RigLogic_Data(const FRigUnit_RigLogic_Data& Other)
{
	*this = Other;
}

FRigUnit_RigLogic_Data& FRigUnit_RigLogic_Data::operator = (const FRigUnit_RigLogic_Data& Other)
{

	if (RigInstance != nullptr)
	{
		delete RigInstance;
		RigInstance = nullptr;
	}

	if (RigLogic != nullptr)
	{
		delete RigLogic;
		RigLogic = nullptr;
	}

	SkelMeshComponent = Other.SkelMeshComponent;
    //@helge: is this required, or the unit is always initialized afterwards?
	//RigLogic = Other.RigLogic;
	//RigInstance = Other.RigInstance;
	CurrentLOD = Other.CurrentLOD;
	InputCurveIndices = Other.InputCurveIndices;
	HierarchyBoneIndices = Other.HierarchyBoneIndices;
	MorphTargetCurveIndices = Other.MorphTargetCurveIndices;
	BlendShapeIndices = Other.BlendShapeIndices;
	CurveContainerIndicesForAnimMaps = Other.CurveContainerIndicesForAnimMaps;
	RigLogicIndicesForAnimMaps = Other.RigLogicIndicesForAnimMaps;
	UpdatedJoints = Other.UpdatedJoints;
	BlendShapeMappingCount = Other.BlendShapeMappingCount;
	NeckFemaleAverageCorCurveIndex = Other.NeckFemaleAverageCorCurveIndex;
	NeckMaleMuscularCorExpCurveIndex = Other.NeckFemaleAverageCorCurveIndex;

	return *this;
}

//maps indices of input curves from dna file to control rig curves
void FRigUnit_RigLogic_Data::MapInputCurveIndices(const IBehaviorReader* DNABehavior, const FRigCurveContainer* CurveContainer)
{
	InputCurveIndices.Reset();

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
		InputCurveIndices.Add(CurveIndex); //can be INDEX_NONE
	}
}

void FRigUnit_RigLogic_Data::MapJoints(const IBehaviorReader* DNABehavior, const FRigBoneHierarchy* Hierarchy)
{
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

void FRigUnit_RigLogic_Data::MapMorphTargets(const IBehaviorReader* DNABehavior, const FRigCurveContainer* CurveContainer)
{
	uint16 LODCount = DNABehavior->GetLODCount();

	MorphTargetCurveIndices.Reset();
	MorphTargetCurveIndices.AddZeroed(LODCount);
	BlendShapeIndices.Reset();
	BlendShapeIndices.AddZeroed(LODCount);

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
			MorphTargetCurveIndices[LodIndex].Values.Add(MorphTargetIndex);
			BlendShapeIndices[LodIndex].Values.Add(Mapping.BlendShapeChannelIndex);
		}
	}
}

void FRigUnit_RigLogic_Data::MapMaskMultipliers(const IBehaviorReader* DNABehavior, const FRigCurveContainer* CurveContainer)
{
	uint16 LODCount = DNABehavior->GetLODCount();
	CurveContainerIndicesForAnimMaps.Reset();
	CurveContainerIndicesForAnimMaps.AddZeroed(LODCount);

	RigLogicIndicesForAnimMaps.Reset();
	RigLogicIndicesForAnimMaps.AddZeroed(LODCount);

	for (uint16 LodIndex = 0; LodIndex < LODCount; ++LodIndex)
	{		
		TArrayView<const uint16> AnimMapIndicesPerLOD = DNABehavior->GetAnimatedMapIndicesForLOD(LodIndex);
		for (uint16 AnimMapIndexPerLOD: AnimMapIndicesPerLOD)
		{
			FString AnimMapNameFStr = DNABehavior->GetAnimatedMapName(AnimMapIndexPerLOD);
			FString MaskMultiplierNameStr = ConstructCurveName(AnimMapNameFStr, TEXT("<obj>_<attr>"));
			if (MaskMultiplierNameStr == "") {
				return;
			}
			FName MaskMultiplierFName(*MaskMultiplierNameStr);

			const int32 CurveIndex = CurveContainer->GetIndex(MaskMultiplierFName);
			CurveContainerIndicesForAnimMaps[LodIndex].Values.Add(CurveIndex); //can be INDEX_NONE if curve was not found
			RigLogicIndicesForAnimMaps[LodIndex].Values.Add(AnimMapIndexPerLOD);
		}
	}
}

FString FRigUnit_RigLogic_Data::ConstructCurveName(const FString& NameToSplit, const FString& FormatString)
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

void FRigUnit_RigLogic_Data::CalculateRigLogic(FControlRigExecuteContext& ExecuteContext)
{
	const int32 RawControlCount = RigInstance->GetRawControlCount();
	for (int32 ControlIndex = 0; ControlIndex < RawControlCount; ++ControlIndex)
	{
		const uint32 CurveIndex = InputCurveIndices[ControlIndex];
		float Value = ExecuteContext.GetCurves()->GetValue(CurveIndex);
	
		RigInstance->SetRawControl(ControlIndex, Value);
	}

	RigLogic->Calculate(RigInstance);
}

void FRigUnit_RigLogic_Data::UpdateJoints(FRigHierarchyContainer* Hierarchy, const FRigUnit_RigLogic_JointUpdateParams& JointUpdateParams)
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
			const int32 BoneIndex = HierarchyBoneIndices[JointIndex];
			if (BoneIndex != INDEX_NONE)
			{
				const FTransform& Neutral = JointUpdateParams.NeutralJointTransforms[JointIndex];
				const FTransform& Delta = JointUpdateParams.DeltaTransforms[JointIndex];
				FTransform Transform
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

void FRigUnit_RigLogic_Data::UpdateBlendShapeCurves(FRigCurveContainer* CurveContainer, TArrayView<const float>& BlendShapeValues )
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

void FRigUnit_RigLogic_Data::UpdateAnimMapCurves(FRigCurveContainer* CurveContainer, TArrayView<const float>& AnimMapOutputs)
{
	// set output mask multipliers
	// In case curves are not imported yet into CL, AnimatedMapsCurveIndices will be empty, so we need to check
	// array bounds before trying to access it:
	if (RigLogicIndicesForAnimMaps.IsValidIndex(CurrentLOD) && CurveContainerIndicesForAnimMaps.IsValidIndex(CurrentLOD))
	{
		uint32 AnimMapPerLODCount = RigLogicIndicesForAnimMaps[CurrentLOD].Values.Num();

		for (uint32 AnimMapIndexForLOD = 0; AnimMapIndexForLOD < AnimMapPerLODCount; ++AnimMapIndexForLOD)
		{
			const int32 RigLogicAnimMapIndex = RigLogicIndicesForAnimMaps[CurrentLOD].Values[AnimMapIndexForLOD];

			const int32 CurveContainerAnimMapIndex = CurveContainerIndicesForAnimMaps[CurrentLOD].Values[AnimMapIndexForLOD];
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

IBehaviorReader* FRigUnit_RigLogic_Data::FetchBehaviorReaderFromOwner() 
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
		//inside DNAAsset there can be (Behavior)StreamReader or GeneSplicerDNAReader
		//both implement behavior reader interface, so implicitly casting is safe:
		return DNAAsset->GetBehaviorReader().Get();
	}

	return nullptr;
}

bool FRigUnit_RigLogic_Data::IsRigLogicInitialized()
{
	return RigLogic != nullptr && RigInstance != nullptr;
}

void FRigUnit_RigLogic_Data::InitializeRigLogic(IBehaviorReader* DNABehavior )
{
	if (DNABehavior == nullptr || DNABehavior->GetJointCount() == 0u)
	{
		UE_LOG(LogRigLogicUnit, Warning, TEXT("Empty DNA file detected, abort initialization."));
		return;
	}

	RigLogic = new FRigLogic(DNABehavior); //needs only BehaviorReader
	RigInstance = new FRigInstance(RigLogic);
	RigInstance->SetLOD(0);
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
		RigInstance->SetLOD(CurrentLOD);
	}
}

FRigUnit_RigLogic_Execute()
{
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
				if (Data.SkelMeshComponent== nullptr || Data.SkelMeshComponent->SkeletalMesh == nullptr)
				{
					return;
				}
				Data.CurrentLOD = Data.SkelMeshComponent->PredictedLODLevel;

				//Fetch BehaviorStreamReader from DNAAsset
				//it can be imported into SkeletalMesh from DNA file, or overwritten by GeneSplicer when making
				//a new character
				IBehaviorReader* DNABehavior = Data.FetchBehaviorReaderFromOwner();				
				Data.InitializeRigLogic(DNABehavior);
				if (!Data.IsRigLogicInitialized())
				{
					return;
				}

				FRigCurveContainer* CurveContainer = ExecuteContext.GetCurves();

				Data.MapJoints(DNABehavior, &Hierarchy->BoneHierarchy);
				Data.MapInputCurveIndices(DNABehavior, CurveContainer);
				Data.MapMorphTargets(DNABehavior, CurveContainer);
				Data.MapMaskMultipliers(DNABehavior, CurveContainer);
				Data.UpdatedJoints.SetNumZeroed(DNABehavior->GetJointCount());

				break; 
			}
			case EControlRigState::Update:
			{

				FRigCurveContainer* CurveContainer = ExecuteContext.GetCurves();

				if (!Data.IsRigLogicInitialized() || CurveContainer == nullptr)
				{
					return;
				}
				Data.ChangeRigLogicLODIfNeeded();
				Data.CalculateRigLogic(ExecuteContext);

				//Filing a struct so we can call the same method for updating joints from tests
				FRigUnit_RigLogic_JointUpdateParams JointUpdateParamsTemp
				(
					Data.RigLogic->GetJointVariableAttributeIndices(Data.CurrentLOD),
					Data.RigLogic->GetNeutralJointValues(),
					Data.RigInstance->GetJointOutputs()
				);

				Data.UpdateJoints(Hierarchy, JointUpdateParamsTemp);

				TArrayView<const float> BlendShapeValues = Data.RigInstance->GetBlendShapeOutputs();
				Data.UpdateBlendShapeCurves( CurveContainer, BlendShapeValues );

				TArrayView<const float> AnimMapOutputs = Data.RigInstance->GetAnimatedMapOutputs();
				Data.UpdateAnimMapCurves( CurveContainer, AnimMapOutputs ); 
			}
			default:
			{
				break;
			}
		}
	}
}
