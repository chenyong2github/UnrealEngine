// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNode_ControlRigBase.h"
#include "ControlRig.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/NodeMappingContainer.h"
#include "AnimationRuntime.h"
#include "Units/Execution/RigUnit_BeginExecution.h"

#if ENABLE_ANIM_DEBUG
TAutoConsoleVariable<int32> CVarAnimNodeControlRigDebug(TEXT("a.AnimNode.ControlRig.Debug"), 0, TEXT("Set to 1 to turn on debug drawing for AnimNode_ControlRigBase"));
#endif

// CVar to disable control rig execution within an anim node
static TAutoConsoleVariable<int32> CVarControlRigDisableExecutionAnimNode(TEXT("ControlRig.DisableExecutionInAnimNode"), 0, TEXT("if nonzero we disable the execution of Control Rigs inside an anim node."));

FAnimNode_ControlRigBase::FAnimNode_ControlRigBase()
	: FAnimNode_CustomProperty()
	, bTransferInputPose(true)
	, bTransferInputCurves(true)
	, bTransferPoseInGlobalSpace(true)
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

bool FAnimNode_ControlRigBase::CanExecute()
{
	if(CVarControlRigDisableExecutionAnimNode->GetInt() != 0)
	{
		return false;
	}

	if (UControlRig* ControlRig = GetControlRig())
	{
		return ControlRig->CanExecute(); 
	}

	return false;
}

void FAnimNode_ControlRigBase::UpdateInput(UControlRig* ControlRig, const FPoseContext& InOutput)
{
	if(!CanExecute())
	{
		return;
	}

#if WITH_EDITOR
	// if we are recording any change - let's clear the undo stack
	if(URigHierarchy* Hierarchy = ControlRig->GetHierarchy())
	{
		if(Hierarchy->IsTracingChanges())
		{
			Hierarchy->ResetTransformStack();
		}
	}
#endif

	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (InputSettings.bUpdatePose && bTransferInputPose)
	{
		const FBoneContainer& RequiredBones = InOutput.Pose.GetBoneContainer();

		// reset transforms here to prevent additive transforms from accumulating to INF
		// we only update transforms from the mesh pose for bones in the current LOD, 
		// so the reset here ensures excluded bones are also reset
		if(!ControlRigBoneInputMappingIncomplete.IsEmpty())
		{
			ControlRig->GetHierarchy()->ResetPoseToInitial(ERigElementType::Bone);
		}

		if(bTransferPoseInGlobalSpace || NodeMappingContainer.IsValid())
		{
			// get component pose from control rig
			FCSPose<FCompactPose> MeshPoses;
			// first I need to convert to local pose
			MeshPoses.InitPose(InOutput.Pose);

			if(!ControlRigBoneInputMappingComplete.IsEmpty())
			{
				for (const TPair<uint16, uint16>& Pair : ControlRigBoneInputMappingComplete)
				{
					const uint16 SkeletonIndex = Pair.Key;
					const uint16 ControlRigIndex = Pair.Value;
					
					FCompactPoseBoneIndex CompactPoseIndex(SkeletonIndex);
					FTransform ComponentTransform = MeshPoses.GetComponentSpaceTransform(CompactPoseIndex);
					ControlRig->GetHierarchy()->SetGlobalTransformByIndex(ControlRigIndex, ComponentTransform, false);
				}
			}
			else
			{
				for (auto Iter = ControlRigBoneInputMappingIncomplete.CreateConstIterator(); Iter; ++Iter)
				{
					const FName& Name = Iter.Key();
					const uint16 Index = Iter.Value();
					const FRigElementKey Key(Name, ERigElementType::Bone);

					FCompactPoseBoneIndex CompactPoseIndex(Index);
					FTransform ComponentTransform = MeshPoses.GetComponentSpaceTransform(CompactPoseIndex);
					if (NodeMappingContainer.IsValid())
					{
						ComponentTransform = NodeMappingContainer->GetSourceToTargetTransform(Name).GetRelativeTransformReverse(ComponentTransform);
					}
					ControlRig->GetHierarchy()->SetGlobalTransform(Key, ComponentTransform, false);
				}
			}
		}
		else
		{
			if(!ControlRigBoneInputMappingComplete.IsEmpty())
			{
				for (const TPair<uint16, uint16>& Pair : ControlRigBoneInputMappingComplete)
				{
					const uint16 SkeletonIndex = Pair.Key;
					const uint16 ControlRigIndex = Pair.Value;
					
					FCompactPoseBoneIndex CompactPoseIndex(SkeletonIndex);
					FTransform LocalTransform = InOutput.Pose[CompactPoseIndex];
					ControlRig->GetHierarchy()->SetLocalTransformByIndex(ControlRigIndex, LocalTransform, false);
				}
			}
			else
			{
				for (auto Iter = ControlRigBoneInputMappingIncomplete.CreateConstIterator(); Iter; ++Iter)
				{
					const FName& Name = Iter.Key();
					const uint16 Index = Iter.Value();
					const FRigElementKey Key(Name, ERigElementType::Bone);

					FCompactPoseBoneIndex CompactPoseIndex(Index);
					FTransform LocalTransform = InOutput.Pose[CompactPoseIndex];
					ControlRig->GetHierarchy()->SetLocalTransform(Key, LocalTransform, false);
				}
			}
		}
		
		if(ControlRigBoneInputMappingIncomplete.IsEmpty())
		{
			if(bTransferPoseInGlobalSpace)
			{
				for(uint16 ElementIndexToReset : ControlRigTransformElementsToReset)
				{
					FTransform InitialTransform = ControlRig->GetHierarchy()->GetGlobalTransformByIndex(ElementIndexToReset, true);
					ControlRig->GetHierarchy()->SetGlobalTransformByIndex(ElementIndexToReset, InitialTransform, false);
				}
			}
			else
			{
				for(uint16 ElementIndexToReset : ControlRigTransformElementsToReset)
				{
					FTransform InitialTransform = ControlRig->GetHierarchy()->GetLocalTransformByIndex(ElementIndexToReset, true);
					ControlRig->GetHierarchy()->SetLocalTransformByIndex(ElementIndexToReset, InitialTransform, false);
				}
			}
		}
					
#if WITH_EDITOR
		ControlRig->ApplyTransformOverrideForUserCreatedBones();
#endif
	}

	if (InputSettings.bUpdateCurves && bTransferInputCurves)
	{
		if(!ControlRigCurveMappingComplete.IsEmpty())
		{
			for (const TPair<uint16, uint16>& Pair : ControlRigCurveMappingComplete)
			{
				const uint16 SkeletonIndex = Pair.Key;
				const uint16 ControlRigIndex = Pair.Value;

				const float Value = InOutput.Curve.Get(SkeletonIndex);
				ControlRig->GetHierarchy()->SetCurveValueByIndex(ControlRigIndex, Value);
			}
		}
		else
		{
			for (auto Iter = ControlRigCurveMappingIncomplete.CreateConstIterator(); Iter; ++Iter)
			{
				const FName& Name = Iter.Key();
				const uint16 Index = Iter.Value();
				const FRigElementKey Key(Name, ERigElementType::Curve);

				ControlRig->GetHierarchy()->SetCurveValue(Key, InOutput.Curve.Get(Index));
			}
		}
	}

#if WITH_EDITOR
	if(URigHierarchy* Hierarchy = ControlRig->GetHierarchy())
	{
		if(Hierarchy->IsTracingChanges())
		{
			Hierarchy->StorePoseForTrace(TEXT("FAnimNode_ControlRigBase::UpdateInput"));
		}
	}
#endif
}

void FAnimNode_ControlRigBase::UpdateOutput(UControlRig* ControlRig, FPoseContext& InOutput)
{
	if(!CanExecute())
	{
		return;
	}

	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (OutputSettings.bUpdatePose)
	{
		// copy output of the rig
		const FBoneContainer& RequiredBones = InOutput.Pose.GetBoneContainer();

		TMap<FName, uint16>& IncompleteMapping = ControlRigBoneOutputMappingIncomplete;
		TArray<TPair<uint16, uint16>>& CompleteMapping = ControlRigBoneOutputMappingComplete;

		// if we don't have a different mapping for outputs, use the input mapping
		if(IncompleteMapping.IsEmpty() && CompleteMapping.IsEmpty())
		{
			IncompleteMapping = ControlRigBoneInputMappingIncomplete;
			CompleteMapping = ControlRigBoneInputMappingComplete;
		}

		if(bTransferPoseInGlobalSpace || NodeMappingContainer.IsValid())
		{
			// get component pose from control rig
			FCSPose<FCompactPose> MeshPoses;
			MeshPoses.InitPose(InOutput.Pose);

			if(!CompleteMapping.IsEmpty())
			{
				for (const TPair<uint16, uint16>& Pair : CompleteMapping)
				{
					const uint16 SkeletonIndex = Pair.Key;
					const uint16 ControlRigIndex = Pair.Value;

					FCompactPoseBoneIndex CompactPoseIndex(SkeletonIndex);
					FTransform ComponentTransform = ControlRig->GetHierarchy()->GetGlobalTransformByIndex(ControlRigIndex);
					MeshPoses.SetComponentSpaceTransform(CompactPoseIndex, ComponentTransform);
				}
			}
			else
			{
				for (auto Iter = IncompleteMapping.CreateConstIterator(); Iter; ++Iter)
				{
					const FName& Name = Iter.Key();
					const uint16 Index = Iter.Value();
					const FRigElementKey Key(Name, ERigElementType::Bone);

					FCompactPoseBoneIndex CompactPoseIndex(Index);
					FTransform ComponentTransform = ControlRig->GetHierarchy()->GetGlobalTransform(Key);
					if (NodeMappingContainer.IsValid())
					{
						ComponentTransform = NodeMappingContainer->GetSourceToTargetTransform(Name) * ComponentTransform;
					}

					MeshPoses.SetComponentSpaceTransform(CompactPoseIndex, ComponentTransform);
				}
			}

			FCSPose<FCompactPose>::ConvertComponentPosesToLocalPosesSafe(MeshPoses, InOutput.Pose);
			InOutput.Pose.NormalizeRotations();
		}
		else
		{
			if(!CompleteMapping.IsEmpty())
			{
				for (const TPair<uint16, uint16>& Pair : CompleteMapping)
				{
					const uint16 SkeletonIndex = Pair.Key;
					const uint16 ControlRigIndex = Pair.Value;

					FCompactPoseBoneIndex CompactPoseIndex(SkeletonIndex);
					FTransform LocalTransform = ControlRig->GetHierarchy()->GetLocalTransformByIndex(ControlRigIndex);
					InOutput.Pose[CompactPoseIndex] = LocalTransform;
				}
			}
			else
			{
				for (auto Iter = IncompleteMapping.CreateConstIterator(); Iter; ++Iter)
				{
					const FName& Name = Iter.Key();
					const uint16 Index = Iter.Value();
					const FRigElementKey Key(Name, ERigElementType::Bone);

					FCompactPoseBoneIndex CompactPoseIndex(Index);
					FTransform LocalTransform = ControlRig->GetHierarchy()->GetLocalTransform(Key);
					InOutput.Pose[CompactPoseIndex] = LocalTransform;
				}
			}
		}
	}

	if (OutputSettings.bUpdateCurves)
	{
		if(!ControlRigCurveMappingComplete.IsEmpty())
		{
			for (const TPair<uint16, uint16>& Pair : ControlRigCurveMappingComplete)
			{
				const uint16 SkeletonIndex = Pair.Key;
				const uint16 ControlRigIndex = Pair.Value;

				const float PreviousValue = InOutput.Curve.Get(SkeletonIndex);
				const float Value = ControlRig->GetHierarchy()->GetCurveValueByIndex(ControlRigIndex);

				if(!FMath::IsNearlyEqual(PreviousValue, Value))
				{
					// this causes a side effect of marking the curve as "valid"
					// so only apply it for curves that have really changed
					InOutput.Curve.Set(SkeletonIndex, Value);
				}
			}
		}
		else
		{
			for (auto Iter = ControlRigCurveMappingIncomplete.CreateConstIterator(); Iter; ++Iter)
			{
				const FName& Name = Iter.Key();
				const uint16 Index = Iter.Value();
				const FRigElementKey Key(Name, ERigElementType::Curve);

				const float PreviousValue = InOutput.Curve.Get(Index);
				const float Value = ControlRig->GetHierarchy()->GetCurveValue(Key);

				if(!FMath::IsNearlyEqual(PreviousValue, Value))
				{
					// this causes a side effect of marking the curve as "valid"
					// so only apply it for curves that have really changed
					InOutput.Curve.Set(Index, Value);
				}
			}
		}
	}

#if WITH_EDITOR
	if(URigHierarchy* Hierarchy = ControlRig->GetHierarchy())
	{
		if(Hierarchy->IsTracingChanges())
		{
			Hierarchy->StorePoseForTrace(TEXT("FAnimNode_ControlRigBase::UpdateOutput"));
			Hierarchy->DumpTransformStackToFile();
		}
	}
#endif
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

	if (CanExecute() && FAnimWeight::IsRelevant(InternalBlendAlpha) && GetControlRig())
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
#if WITH_EDITOR
			if(URigHierarchy* Hierarchy = ControlRig->GetHierarchy())
			{
				if(Hierarchy->IsTracingChanges())
				{
					Hierarchy->StorePoseForTrace(TEXT("FAnimNode_ControlRigBase::BeforeEvaluate"));
				}
			}
#endif

			// first evaluate control rig
			ControlRig->Evaluate_AnyThread();

#if ENABLE_ANIM_DEBUG 
			// When Control Rig is at editing time (in CR editor), draw instructions are consumed by ControlRigEditMode, so we need to skip drawing here.
			bool bShowDebug = (CVarAnimNodeControlRigDebug.GetValueOnAnyThread() == 1 && ControlRig->ExecutionType != ERigExecutionType::Editing);

			if (bShowDebug)
			{ 
				QueueControlRigDrawInstructions(ControlRig, InOutput.AnimInstanceProxy);
			}
#endif

#if WITH_EDITOR
			if(URigHierarchy* Hierarchy = ControlRig->GetHierarchy())
			{
				if(Hierarchy->IsTracingChanges())
				{
					Hierarchy->StorePoseForTrace(TEXT("FAnimNode_ControlRigBase::AfterEvaluate"));
				}
			}
#endif
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
			URigHierarchy* Hierarchy = ControlRig->GetHierarchy();
			Hierarchy->ForEach<FRigControlElement>([this, Hierarchy](FRigControlElement* ControlElement) -> bool
			{
				ControlValues.Add(ControlElement->GetKey(), Hierarchy->GetControlValueByIndex(ControlElement->GetIndex()));
				return true; // continue
			});
		}
	}

	~FControlRigControlScope()
	{
		if (ControlRig.IsValid())
		{
			URigHierarchy* Hierarchy = ControlRig->GetHierarchy();
			for (const TPair<FRigElementKey, FRigControlValue>& Pair: ControlValues)
			{
				Hierarchy->SetControlValue(Pair.Key, Pair.Value);
			}
		}
	}

	TMap<FRigElementKey, FRigControlValue> ControlValues;
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

		ControlRigBoneInputMappingComplete.Reset();
		ControlRigBoneOutputMappingComplete.Reset();
		ControlRigCurveMappingComplete.Reset();
		ControlRigTransformElementsToReset.Reset();
		ControlRigBoneInputMappingIncomplete.Reset();
		ControlRigBoneOutputMappingIncomplete.Reset();
		ControlRigCurveMappingIncomplete.Reset();

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
						ControlRigBoneInputMappingIncomplete.Add(*SourceName, Index);
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
						ControlRigBoneInputMappingIncomplete.Add(BoneName, Index);
					}
				}
			}

			if(!InputBonesToTransfer.IsEmpty())
			{
				ControlRigBoneOutputMappingIncomplete = ControlRigBoneInputMappingIncomplete;
				ControlRigBoneInputMappingIncomplete.Reset();
				
				if (NodeMappingContainer.IsValid())
				{
					// get target to source mapping table - this is reversed mapping table
					TMap<FName, FName> TargetToSourceMappingTable;
					NodeMappingContainer->GetTargetToSourceMappingTable(TargetToSourceMappingTable);

					for(FBoneReference& InputBoneToTransfer : InputBonesToTransfer)
					{
						if(!InputBoneToTransfer.Initialize(RequiredBones))
						{
							continue;
						}
						FName TargetNodeName = RefSkeleton.GetBoneName(InputBoneToTransfer.BoneIndex);
						FName* SourceName = TargetToSourceMappingTable.Find(TargetNodeName);
						if (SourceName)
						{
							ControlRigBoneInputMappingIncomplete.Add(*SourceName, InputBoneToTransfer.BoneIndex);
						}
					}
				}
				else
				{
					TArray<FName> NodeNames;
					TArray<FNodeItem> NodeItems;
					ControlRig->GetMappableNodeData(NodeNames, NodeItems);

					for(FBoneReference& InputBoneToTransfer : InputBonesToTransfer)
					{
						if(!InputBoneToTransfer.Initialize(RequiredBones))
						{
							continue;
						}
						const FName& BoneName = RefSkeleton.GetBoneName(RequiredBonesArray[InputBoneToTransfer.BoneIndex]);
						if (NodeNames.Contains(BoneName))
						{
							ControlRigBoneInputMappingIncomplete.Add(BoneName, InputBoneToTransfer.BoneIndex);
						}
					}
				}
			}
			
			// we just support curves by name only
			TArray<FName> const& CurveNames = RequiredBones.GetUIDToNameLookupTable();
			URigHierarchy* Hierarchy = ControlRig->GetHierarchy();
			for (uint16 Index = 0; Index < CurveNames.Num(); ++Index)
			{
				// see if the curve name exists in the control rig
				if (Hierarchy->GetIndex(FRigElementKey(CurveNames[Index], ERigElementType::Curve)) != INDEX_NONE)
				{
					ControlRigCurveMappingIncomplete.Add(CurveNames[Index], Index);
				}
			}

			// check if we can switch the bones to a complete mapping.
			// we can only do that if there is no node mapping container set.
			if(!NodeMappingContainer.IsValid())
			{
				for(int32 InputOutput = 0; InputOutput < 2; InputOutput++)
				{
					bool bIsCompleteBoneMapping = true;
					TMap<FName, uint16>& IncompleteMapping = InputOutput == 0 ? ControlRigBoneInputMappingIncomplete : ControlRigBoneOutputMappingIncomplete;
					if(IncompleteMapping.IsEmpty())
					{
						continue;
					}
					
					TArray<TPair<uint16, uint16>>& CompleteMapping = InputOutput == 0 ? ControlRigBoneInputMappingComplete : ControlRigBoneOutputMappingComplete;
					
					for (auto Iter = IncompleteMapping.CreateConstIterator(); Iter; ++Iter)
					{
						const uint16 SkeletonIndex = Iter.Value();
						const int32 ControlRigIndex = ControlRig->GetHierarchy()->GetIndex(FRigElementKey(Iter.Key(), ERigElementType::Bone));
						if(ControlRigIndex != INDEX_NONE)
						{
							CompleteMapping.Add(TPair<uint16, uint16>(SkeletonIndex, (uint16)ControlRigIndex));
						}
						else
						{
							bIsCompleteBoneMapping = false;
						}
					}

					if(bIsCompleteBoneMapping)
					{
						IncompleteMapping.Reset();

						if(InputOutput == 0)
						{
							ControlRigTransformElementsToReset.Reset();

							for(int32 ElementIndex = 0; ElementIndex < ControlRig->GetHierarchy()->Num(); ElementIndex++)
							{
								const FRigElementKey Key = ControlRig->GetHierarchy()->GetKey(ElementIndex);
								if(Key.Type == ERigElementType::Bone || Key.Type == ERigElementType::Curve)
								{
									continue;
								}
								ControlRigTransformElementsToReset.Add((uint16)ElementIndex);
							}
						}
					}
					else
					{
						CompleteMapping.Reset();
					}
				}
			}

			bool bIsCompleteCurveMapping = true;
			
			// check if we can switch the curves to a complete mapping as well
			for (auto Iter = ControlRigCurveMappingIncomplete.CreateConstIterator(); Iter; ++Iter)
			{
				const uint16 SkeletonIndex = Iter.Value();
				const int32 ControlRigIndex = ControlRig->GetHierarchy()->GetIndex(FRigElementKey(Iter.Key(), ERigElementType::Curve));
				if(ControlRigIndex != INDEX_NONE)
				{
					ControlRigCurveMappingComplete.Add(TPair<uint16, uint16>(SkeletonIndex, (uint16)ControlRigIndex));
				}
				else
				{
					bIsCompleteCurveMapping = false;
				}
			}

			if(bIsCompleteCurveMapping)
			{
				ControlRigCurveMappingIncomplete.Reset();
			}
			else
			{
				ControlRigCurveMappingComplete.Reset();
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

void FAnimNode_ControlRigBase::QueueControlRigDrawInstructions(UControlRig* ControlRig, FAnimInstanceProxy* Proxy) const
{
	ensure(ControlRig);
	ensure(Proxy);

	if (ControlRig && Proxy)
	{
		for (const FControlRigDrawInstruction& Instruction : ControlRig->GetDrawInterface())
		{
			if (!Instruction.IsValid())
			{
				continue;
			}

			FTransform InstructionTransform = Instruction.Transform * Proxy->GetComponentTransform();
			switch (Instruction.PrimitiveType)
			{
				case EControlRigDrawSettings::Points:
				{
					for (const FVector& Point : Instruction.Positions)
					{
						Proxy->AnimDrawDebugPoint(InstructionTransform.TransformPosition(Point), Instruction.Thickness, Instruction.Color.ToFColor(true), false, -1.f, SDPG_Foreground);
					}
					break;
				}
				case EControlRigDrawSettings::Lines:
				{
					const TArray<FVector>& Points = Instruction.Positions;

					for (int32 PointIndex = 0; PointIndex < Points.Num() - 1; PointIndex += 2)
					{
						Proxy->AnimDrawDebugLine(InstructionTransform.TransformPosition(Points[PointIndex]), InstructionTransform.TransformPosition(Points[PointIndex + 1]), Instruction.Color.ToFColor(true), false, -1.f, Instruction.Thickness, SDPG_Foreground);
					}
					break;
				}
				case EControlRigDrawSettings::LineStrip:
				{
					const TArray<FVector>& Points = Instruction.Positions;

					for (int32 PointIndex = 0; PointIndex < Points.Num() - 1; PointIndex++)
					{
						Proxy->AnimDrawDebugLine(InstructionTransform.TransformPosition(Points[PointIndex]), InstructionTransform.TransformPosition(Points[PointIndex + 1]), Instruction.Color.ToFColor(true), false, -1.f, Instruction.Thickness, SDPG_Foreground);
					}
					break;
				}

				case EControlRigDrawSettings::DynamicMesh:
				{
					// TODO: Add support for this if anyone is actually using it. Currently it is only defined and referenced in an unused API, DrawCone in Control Rig.
					break;
				}
			}
		}
	}
}

