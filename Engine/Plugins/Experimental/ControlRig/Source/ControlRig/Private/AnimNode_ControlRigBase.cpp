// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNode_ControlRigBase.h"
#include "ControlRig.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/NodeMappingContainer.h"
#include "AnimationRuntime.h"
#include "Units/Execution/RigUnit_BeginExecution.h"

FAnimNode_ControlRigBase::FAnimNode_ControlRigBase()
	: FAnimNode_CustomProperty()
	, InputSettings(FControlRigIOSettings())
	, OutputSettings(FControlRigIOSettings())
	, bExecute(true)
	, InternalBlendAlpha (1.f)
{

}

void FAnimNode_ControlRigBase::OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FAnimNode_CustomProperty::OnInitializeAnimInstance(InProxy, InAnimInstance);

	USkeletalMeshComponent* Component = InAnimInstance->GetOwningComponent();
	UControlRig* ControlRig = GetControlRig();
	if (Component && Component->SkeletalMesh && ControlRig)
	{
		UBlueprintGeneratedClass* BlueprintClass = Cast<UBlueprintGeneratedClass>(ControlRig->GetClass());
		if (BlueprintClass)
		{
			UBlueprint* Blueprint = Cast<UBlueprint>(BlueprintClass->ClassGeneratedBy);
			// node mapping container will be saved on the initialization part
			NodeMappingContainer = Component->SkeletalMesh->GetNodeMappingContainer(Blueprint);
		}

		// register skeletalmesh component for now
		ControlRig->GetDataSourceRegistry()->RegisterDataSource(UControlRig::OwnerComponent, InAnimInstance->GetOwningComponent());
	}
}

void FAnimNode_ControlRigBase::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FAnimNode_CustomProperty::Initialize_AnyThread(Context);
	Source.Initialize(Context);

	if (UControlRig* ControlRig = GetControlRig())
	{
		//Don't Inititialize the Control Rig here it may have the wrong VM on the CDO
		SetTargetInstance(ControlRig);
		ControlRig->RequestInit();
	}
}

void FAnimNode_ControlRigBase::GatherDebugData(FNodeDebugData& DebugData)
{
	Source.GatherDebugData(DebugData.BranchFlow(1.f));
}

void FAnimNode_ControlRigBase::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FAnimNode_CustomProperty::Update_AnyThread(Context);
	Source.Update(Context);

	if (bExecute)
	{
		if (UControlRig* ControlRig = GetControlRig())
		{
			// @TODO: fix this to be thread-safe
			// Pre-update doesn't work for custom anim instances
			// FAnimNode_ControlRigExternalSource needs this to be called to reset to ref pose
			ControlRig->SetDeltaTime(Context.GetDeltaTime());
		}
	}
}

void FAnimNode_ControlRigBase::UpdateInput(UControlRig* ControlRig, const FPoseContext& InOutput)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (InputSettings.bUpdatePose)
	{
		const FBoneContainer& RequiredBones = InOutput.Pose.GetBoneContainer();

		// get component pose from control rig
		FCSPose<FCompactPose> MeshPoses;
		// first I need to convert to local pose
		MeshPoses.InitPose(InOutput.Pose);

		// reset transforms here to prevent additive transforms from accumulating to INF
		// we only update transforms from the mesh pose for bones in the current LOD, 
		// so the reset here ensures excluded bones are also reset
		ControlRig->GetBoneHierarchy().ResetTransforms();

		// @re-think - now control rig contains init pose from their default hierarchy and current pose from this instance.
		// we may need this init pose somewhere (instance refpose)
		for (auto Iter = ControlRigBoneMapping.CreateConstIterator(); Iter; ++Iter)
		{
			const FName& Name = Iter.Key();
			const uint16 Index = Iter.Value();

			FTransform ComponentTransform = MeshPoses.GetComponentSpaceTransform(FCompactPoseBoneIndex(Index));
			if (NodeMappingContainer.IsValid())
			{
				ComponentTransform = NodeMappingContainer->GetSourceToTargetTransform(Name).GetRelativeTransformReverse(ComponentTransform);
			}

			// we don't want to do it recursively here because the global transform of each imported bone is set individually
			ControlRig->SetGlobalTransform(Name, ComponentTransform, false);

			// user created bones can be children of imported bones, 
			// so we need to propagate transform to user created bones, and only to them.
			int32 BoneIndex = ControlRig->GetBoneHierarchy().GetIndex(Name);

			if (BoneIndex != INDEX_NONE)
			{
				const FRigBone& Bone = ControlRig->GetBoneHierarchy()[BoneIndex];

				// "dependents" array is a cache of the direct children of the bone
				for (const int32 Dependent : Bone.Dependents)
				{
					ensure(Dependent < ControlRig->GetBoneHierarchy().Num() && Dependent >= 0);
					
					if (ControlRig->GetBoneHierarchy()[Dependent].Type == ERigBoneType::User)
					{
						ControlRig->GetBoneHierarchy().RecalculateGlobalTransform(Dependent);

						// children of user created bones are also user created bones, which need to be updated as well.
						ControlRig->GetBoneHierarchy().PropagateTransform(Dependent);
					}
				}
			}
		}
	}

	if (InputSettings.bUpdateCurves)
	{
		// we just do name mapping 
		for (auto Iter = ControlRigCurveMapping.CreateConstIterator(); Iter; ++Iter)
		{
			const FName& Name = Iter.Key();
			const uint16 Index = Iter.Value();

			ControlRig->SetCurveValue(Name, InOutput.Curve.Get(Index));
		}
	}
}

void FAnimNode_ControlRigBase::UpdateOutput(UControlRig* ControlRig, FPoseContext& InOutput)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (OutputSettings.bUpdatePose)
	{
		// copy output of the rig
		const FBoneContainer& RequiredBones = InOutput.Pose.GetBoneContainer();

		// get component pose from control rig
		FCSPose<FCompactPose> MeshPoses;
		MeshPoses.InitPose(InOutput.Pose);

		for (auto Iter = ControlRigBoneMapping.CreateConstIterator(); Iter; ++Iter)
		{
			const FName& Name = Iter.Key();
			const uint16 Index = Iter.Value();

			FCompactPoseBoneIndex CompactPoseIndex(Index);
			FTransform ComponentTransform = ControlRig->GetGlobalTransform(Name);
			if (NodeMappingContainer.IsValid())
			{
				ComponentTransform = NodeMappingContainer->GetSourceToTargetTransform(Name) * ComponentTransform;
			}

			MeshPoses.SetComponentSpaceTransform(CompactPoseIndex, ComponentTransform);
		}

		FCSPose<FCompactPose>::ConvertComponentPosesToLocalPosesSafe(MeshPoses, InOutput.Pose);
		InOutput.Pose.NormalizeRotations();
	}

	if (OutputSettings.bUpdateCurves)
	{
		// update curve
		for (auto Iter = ControlRigCurveMapping.CreateConstIterator(); Iter; ++Iter)
		{
			const FName& Name = Iter.Key();
			const uint16 Index = Iter.Value();

			const float PreviousValue = InOutput.Curve.Get(Index);
			const float Value = ControlRig->GetCurveValue(Name);

			if(!FMath::IsNearlyEqual(PreviousValue, Value))
			{
				// this causes a side effect of marking the curve as "valid"
				// so only apply it for curves that have really changed
				InOutput.Curve.Set(Index, Value);
			}
		}
	}
}

void FAnimNode_ControlRigBase::Evaluate_AnyThread(FPoseContext& Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FPoseContext SourcePose(Output);

	if (Source.GetLinkNode())
	{
		Source.Evaluate(SourcePose);
	}
	else
	{
		// apply refpose
		SourcePose.ResetToRefPose();
	}

	if (FAnimWeight::IsRelevant(InternalBlendAlpha))
	{
		if (FAnimWeight::IsFullWeight(InternalBlendAlpha))
		{
			ExecuteControlRig(SourcePose);
			Output = SourcePose;
		}
		else 
		{
			// this blends additively - by weight
			FPoseContext ControlRigPose(SourcePose);
			ControlRigPose = SourcePose;
			ExecuteControlRig(ControlRigPose);

			FPoseContext AdditivePose(ControlRigPose);
			AdditivePose = ControlRigPose;
			FAnimationRuntime::ConvertPoseToAdditive(AdditivePose.Pose, SourcePose.Pose);
			AdditivePose.Curve.ConvertToAdditive(SourcePose.Curve);
			Output = SourcePose;

			FAnimationPoseData BaseAnimationPoseData(Output);
			const FAnimationPoseData AdditiveAnimationPoseData(AdditivePose);
			FAnimationRuntime::AccumulateAdditivePose(BaseAnimationPoseData, AdditiveAnimationPoseData, InternalBlendAlpha, AAT_LocalSpaceBase);
		}
	}
	else // if not relevant, skip to run control rig
		// this may cause issue if we have simulation node in the control rig that accumulates time
	{
		Output = SourcePose;
	}
}

void FAnimNode_ControlRigBase::ExecuteControlRig(FPoseContext& InOutput)
{
	if (UControlRig* ControlRig = GetControlRig())
	{
		// first update input to the system
		UpdateInput(ControlRig, InOutput);

		if (bExecute)
		{
			// first evaluate control rig
			ControlRig->Evaluate_AnyThread();
		}

		// now update output
		UpdateOutput(ControlRig, InOutput);
	}
}

struct FControlRigControlScope
{
	FControlRigControlScope(UControlRig* InControlRig)
		: ControlRig(InControlRig)
	{
		if (ControlRig.IsValid())
		{
			CopyOfControls = ControlRig->AvailableControls();
			
		}
	}

	~FControlRigControlScope()
	{
		if (ControlRig.IsValid())
		{
			for (const FRigControl& CopyRigControl: CopyOfControls)
			{
				FRigControl* RigControl = ControlRig->FindControl(CopyRigControl.Name);
				if (RigControl)
				{
					if (CopyRigControl.ControlType == RigControl->ControlType)
					{
						switch (RigControl->ControlType)
						{
						case ERigControlType::Transform:
						{
							FTransform Val = CopyRigControl.GetValue(ERigControlValueType::Current).Get<FTransform>();
							RigControl->GetValue(ERigControlValueType::Current).Set<FTransform>(Val);
							break;
						}
						case ERigControlType::TransformNoScale:
						{
							FTransformNoScale Val = CopyRigControl.GetValue(ERigControlValueType::Current).Get<FTransformNoScale>();
							RigControl->GetValue(ERigControlValueType::Current).Set<FTransformNoScale>(Val);
							break;
						}
						case ERigControlType::EulerTransform:
						{
							FEulerTransform Val = CopyRigControl.GetValue(ERigControlValueType::Current).Get<FEulerTransform>();
							RigControl->GetValue(ERigControlValueType::Current).Set<FEulerTransform>(Val);
							break;
						}
						case ERigControlType::Float:
						{
							float Val = CopyRigControl.GetValue(ERigControlValueType::Current).Get<float>();
							RigControl->GetValue(ERigControlValueType::Current).Set<float>(Val);
							break;
						}
						case ERigControlType::Bool:
						{
							bool Val = CopyRigControl.GetValue(ERigControlValueType::Current).Get<bool>();
							RigControl->GetValue(ERigControlValueType::Current).Set<bool>(Val);
							break;
						}
						case ERigControlType::Integer:
						{
							int32 Val = CopyRigControl.GetValue(ERigControlValueType::Current).Get<int32>();
							RigControl->GetValue(ERigControlValueType::Current).Set<int32>(Val);
							break;
						}
						case ERigControlType::Vector2D:
						{
							FVector2D Val = CopyRigControl.GetValue(ERigControlValueType::Current).Get<FVector2D>();
							RigControl->GetValue(ERigControlValueType::Current).Set<FVector2D>(Val);
							break;
						}
						case ERigControlType::Position:
						case ERigControlType::Scale:
						case ERigControlType::Rotator:
						{
							FVector Val = CopyRigControl.GetValue(ERigControlValueType::Current).Get<FVector>();
							RigControl->GetValue(ERigControlValueType::Current).Set<FVector>(Val);
							break;
						}
						default:
							break;

						};
					}
				}
			}
		}
	}

	TArray<FRigControl> CopyOfControls;
	TWeakObjectPtr<UControlRig> ControlRig;
};

void FAnimNode_ControlRigBase::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FAnimNode_CustomProperty::CacheBones_AnyThread(Context);
	Source.CacheBones(Context);

	if (UControlRig* ControlRig = GetControlRig())
	{
		// fill up node names
		FBoneContainer& RequiredBones = Context.AnimInstanceProxy->GetRequiredBones();

		ControlRigBoneMapping.Reset();
		ControlRigCurveMapping.Reset();

		if(RequiredBones.IsValid())
		{
			const TArray<FBoneIndexType>& RequiredBonesArray = RequiredBones.GetBoneIndicesArray();
			const int32 NumBones = RequiredBonesArray.Num();

			const FReferenceSkeleton& RefSkeleton = RequiredBones.GetReferenceSkeleton();

			// @todo: thread-safe? probably not in editor, but it may not be a big issue in editor
			if (NodeMappingContainer.IsValid())
			{
				// get target to source mapping table - this is reversed mapping table
				TMap<FName, FName> TargetToSourceMappingTable;
				NodeMappingContainer->GetTargetToSourceMappingTable(TargetToSourceMappingTable);

				// now fill up node name
				for (uint16 Index = 0; Index < NumBones; ++Index)
				{
					// get bone name, and find reverse mapping
					FName TargetNodeName = RefSkeleton.GetBoneName(RequiredBonesArray[Index]);
					FName* SourceName = TargetToSourceMappingTable.Find(TargetNodeName);
					if (SourceName)
					{
						ControlRigBoneMapping.Add(*SourceName, Index);
					}
				}
			}
			else
			{
				TArray<FName> NodeNames;
				TArray<FNodeItem> NodeItems;
				ControlRig->GetMappableNodeData(NodeNames, NodeItems);

				// even if not mapped, we map only node that exists in the controlrig
				for (uint16 Index = 0; Index < NumBones; ++Index)
				{
					const FName& BoneName = RefSkeleton.GetBoneName(RequiredBonesArray[Index]);
					if (NodeNames.Contains(BoneName))
					{
						ControlRigBoneMapping.Add(BoneName, Index);
					}
				}
			}
			
			// we just support curves by name only
			TArray<FName> const& CurveNames = RequiredBones.GetUIDToNameLookupTable();
			const FRigCurveContainer& RigCurveContainer = ControlRig->GetCurveContainer();
			for (uint16 Index = 0; Index < CurveNames.Num(); ++Index)
			{
				// see if the curve name exists in the control rig
				if (RigCurveContainer.GetIndex(CurveNames[Index]) != INDEX_NONE)
				{
					ControlRigCurveMapping.Add(CurveNames[Index], Index);
				}
			}
		}

		// re-init when LOD changes
		// and restore control values
		FControlRigControlScope Scope(ControlRig);
		ControlRig->Execute(EControlRigState::Init, FRigUnit_BeginExecution::EventName);
	}
}

UClass* FAnimNode_ControlRigBase::GetTargetClass() const
{
	if (UControlRig* ControlRig = GetControlRig())
	{
		return ControlRig->GetClass();
	}

	return nullptr;
}

