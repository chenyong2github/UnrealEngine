// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimNode_SubInstance.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_SubInput.h"
#include "Animation/AnimNode_Root.h"

FAnimNode_SubInstance::FAnimNode_SubInstance()
	: InstanceClass(nullptr)
	, Tag(NAME_None)
{
}

void FAnimNode_SubInstance::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_Base::Initialize_AnyThread(Context);

	UAnimInstance* InstanceToRun = GetTargetInstance<UAnimInstance>();
	if(InstanceToRun && LinkedRoot)
	{
		FAnimInstanceProxy& Proxy = InstanceToRun->GetProxyOnAnyThread<FAnimInstanceProxy>();
		Proxy.InitializeRootNode_WithRoot(LinkedRoot);
	}
}

void FAnimNode_SubInstance::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	UAnimInstance* InstanceToRun = GetTargetInstance<UAnimInstance>();
	if(InstanceToRun && LinkedRoot)
	{
		FAnimInstanceProxy& Proxy = InstanceToRun->GetProxyOnAnyThread<FAnimInstanceProxy>();
		Proxy.CacheBones_WithRoot(LinkedRoot);
	}
}

void FAnimNode_SubInstance::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	GetEvaluateGraphExposedInputs().Execute(Context);

	UAnimInstance* InstanceToRun = GetTargetInstance<UAnimInstance>();
	if(InstanceToRun && LinkedRoot)
	{
		FAnimInstanceProxy& Proxy = InstanceToRun->GetProxyOnAnyThread<FAnimInstanceProxy>();

		PropagateInputProperties(Context.AnimInstanceProxy->GetAnimInstanceObject());

		// Only update if we've not had a single-threaded update already
		if(InstanceToRun->bNeedsUpdate)
		{
			Proxy.UpdateAnimation_WithRoot(LinkedRoot, GetDynamicLinkFunctionName());
		}
	}
}

void FAnimNode_SubInstance::Evaluate_AnyThread(FPoseContext& Output)
{
	UAnimInstance* InstanceToRun = GetTargetInstance<UAnimInstance>();
	if(InstanceToRun && LinkedRoot)
	{
		FAnimInstanceProxy& Proxy = InstanceToRun->GetProxyOnAnyThread<FAnimInstanceProxy>();
		Output.Pose.SetBoneContainer(&Proxy.GetRequiredBones());

		// Create an evaluation context
		FPoseContext EvaluationContext(&Proxy);
		EvaluationContext.ResetToRefPose();
			
		// Run the anim blueprint
		Proxy.EvaluateAnimation_WithRoot(EvaluationContext, LinkedRoot);

		// Move the curves
		Output.Curve.MoveFrom(EvaluationContext.Curve);
		Output.Pose.MoveBonesFrom(EvaluationContext.Pose);
	}
	else
	{
		Output.ResetToRefPose();
	}
}

void FAnimNode_SubInstance::GatherDebugData(FNodeDebugData& DebugData)
{
	// Add our entry
	FString DebugLine = DebugData.GetNodeName(this);
	DebugLine += FString::Printf(TEXT("Target: %s"), (*InstanceClass) ? *InstanceClass->GetName() : TEXT("None"));

	DebugData.AddDebugItem(DebugLine);

	UAnimInstance* InstanceToRun = GetTargetInstance<UAnimInstance>();
	// Gather data from the sub instance
	if(InstanceToRun && LinkedRoot)
	{
		FAnimInstanceProxy& Proxy = InstanceToRun->GetProxyOnAnyThread<FAnimInstanceProxy>();
		Proxy.GatherDebugData_WithRoot(DebugData.BranchFlow(1.0f), LinkedRoot, GetDynamicLinkFunctionName());
	}
}

void FAnimNode_SubInstance::OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance)
{
	UAnimInstance* InstanceToRun = GetTargetInstance<UAnimInstance>();

	if(*InstanceClass)
	{
		ReinitializeSubAnimInstance(InAnimInstance);
	}
	else if(InstanceToRun)
	{
		// We have an instance but no instance class
		TeardownInstance();
	}
}

void FAnimNode_SubInstance::TeardownInstance()
{
	UAnimInstance* InstanceToRun = GetTargetInstance<UAnimInstance>();
	if (InstanceToRun)
	{
		InstanceToRun->UninitializeAnimation();
		InstanceToRun = nullptr;
	}
}

void FAnimNode_SubInstance::ReinitializeSubAnimInstance(const UAnimInstance* InOwningAnimInstance, UAnimInstance* InNewAnimInstance)
{
	UAnimInstance* InstanceToRun = GetTargetInstance<UAnimInstance>();

	if(*InstanceClass || InNewAnimInstance)
	{
		USkeletalMeshComponent* MeshComp = InOwningAnimInstance->GetSkelMeshComponent();
		check(MeshComp);
		// Full reinit, kill old instances
		if(InstanceToRun)
		{
			DynamicUnlink(const_cast<UAnimInstance*>(InOwningAnimInstance));

			MeshComp->SubInstances.Remove(InstanceToRun);
			// Never delete the owning animation instance
			if (InstanceToRun != InOwningAnimInstance)
			{
				InstanceToRun->MarkPendingKill();
			}
			InstanceToRun = nullptr;
		}

		// Need an instance to run, so create it now
		InstanceToRun = InNewAnimInstance ? InNewAnimInstance : NewObject<UAnimInstance>(MeshComp, InstanceClass);
		SetTargetInstance(InstanceToRun);

		// Link before we call InitializeAnimation() so we propgate the call to sub-inputs
		DynamicLink(const_cast<UAnimInstance*>(InOwningAnimInstance));

		if(InNewAnimInstance == nullptr)
		{
			// Initialize the new instance
			InstanceToRun->InitializeAnimation();

			MeshComp->SubInstances.Add(InstanceToRun);
		}

		InitializeProperties(InOwningAnimInstance, InstanceToRun->GetClass());
	}
	else if(InstanceToRun)
	{
		// We have an instance but no instance class
		TeardownInstance();
	}
}

void FAnimNode_SubInstance::SetAnimClass(TSubclassOf<UAnimInstance> InClass, const UAnimInstance* InOwningAnimInstance)
{
	UClass* NewClass = InClass.Get();
	if(NewClass)
	{
		// Verify target skeleton match at runtime
		IAnimClassInterface* SubAnimBlueprintClass = IAnimClassInterface::GetFromClass(NewClass);
		IAnimClassInterface* OuterAnimBlueprintClass = IAnimClassInterface::GetFromClass(InOwningAnimInstance->GetClass());
		USkeleton* SubSkeleton = SubAnimBlueprintClass->GetTargetSkeleton();
		USkeleton* OuterSkeleton = OuterAnimBlueprintClass->GetTargetSkeleton();
		if(SubSkeleton != OuterSkeleton)
		{
			UE_LOG(LogAnimation, Warning, TEXT("Setting sub instance class: Sub instance class has a mismatched target skeleton. Expected %s, found %s."), OuterSkeleton ? *OuterSkeleton->GetName() : TEXT("null"), SubSkeleton ? *SubSkeleton->GetName() : TEXT("null"));
			return;
		}
	}

	// Verified OK, so set it now
	TSubclassOf<UAnimInstance> OldClass = InstanceClass;
	InstanceClass = InClass;

	if(InstanceClass != OldClass)
	{
		ReinitializeSubAnimInstance(InOwningAnimInstance);
	}
}

FName FAnimNode_SubInstance::GetDynamicLinkFunctionName() const
{
	return NAME_AnimGraph;
}

UAnimInstance* FAnimNode_SubInstance::GetDynamicLinkTarget(UAnimInstance* InOwningAnimInstance) const
{
	return GetTargetInstance<UAnimInstance>();
}

void FAnimNode_SubInstance::DynamicLink(UAnimInstance* InOwningAnimInstance)
{
	UAnimInstance* LinkTargetInstance = GetDynamicLinkTarget(InOwningAnimInstance);
	if(LinkTargetInstance)
	{
		IAnimClassInterface* SubAnimBlueprintClass = IAnimClassInterface::GetFromClass(LinkTargetInstance->GetClass());
		if(SubAnimBlueprintClass)
		{
			FAnimInstanceProxy* NonConstProxy = &InOwningAnimInstance->GetProxyOnAnyThread<FAnimInstanceProxy>();

			// Link input poses
			for(const FAnimBlueprintFunction& AnimBlueprintFunction : SubAnimBlueprintClass->GetAnimBlueprintFunctions())
			{
				const FName FunctionToLink = GetDynamicLinkFunctionName();
				if(AnimBlueprintFunction.Name == FunctionToLink)
				{
					for(int32 InputPoseIndex = 0; InputPoseIndex < AnimBlueprintFunction.InputPoseNames.Num() && InputPoseIndex < InputPoses.Num(); ++InputPoseIndex)
					{
						// Make sure we attempt a re-link first, as only this pose link knows its target
						FAnimationInitializeContext Context(NonConstProxy);
						InputPoses[InputPoseIndex].AttemptRelink(Context);

						int32 InputPropertyIndex = FindFunctionInputIndex(AnimBlueprintFunction, AnimBlueprintFunction.InputPoseNames[InputPoseIndex]);
						if(InputPropertyIndex != INDEX_NONE && AnimBlueprintFunction.InputPoseNodeProperties[InputPropertyIndex])
						{
							FAnimNode_SubInput* SubInputNode = AnimBlueprintFunction.InputPoseNodeProperties[InputPropertyIndex]->ContainerPtrToValuePtr<FAnimNode_SubInput>(LinkTargetInstance);
							check(SubInputNode->Name == AnimBlueprintFunction.InputPoseNames[InputPoseIndex]);
							SubInputNode->DynamicLink(NonConstProxy, &InputPoses[InputPoseIndex]);
						}
						else
						{
							UE_LOG(LogAnimation, Warning, TEXT("Unable to dynamically link input pose %s."), *AnimBlueprintFunction.InputPoseNames[InputPoseIndex].ToString());
						}
					}

					if(AnimBlueprintFunction.OutputPoseNodeProperty)
					{
						LinkedRoot = AnimBlueprintFunction.OutputPoseNodeProperty->ContainerPtrToValuePtr<FAnimNode_Root>(LinkTargetInstance);
					}
					else
					{
						UE_LOG(LogAnimation, Warning, TEXT("Unable to dynamically link root %s."), *FunctionToLink.ToString());
					}

					break;
				}
			}
		}
	}
}

void FAnimNode_SubInstance::DynamicUnlink(UAnimInstance* InOwningAnimInstance)
{
	// unlink root
	LinkedRoot = nullptr;

	// unlink input poses
	UAnimInstance* LinkTargetInstance = GetDynamicLinkTarget(InOwningAnimInstance);
	if(LinkTargetInstance)
	{
		IAnimClassInterface* SubAnimBlueprintClass = IAnimClassInterface::GetFromClass(LinkTargetInstance->GetClass());
		if(SubAnimBlueprintClass)
		{
			// Link input poses
			for(const FAnimBlueprintFunction& AnimBlueprintFunction : SubAnimBlueprintClass->GetAnimBlueprintFunctions())
			{
				if(AnimBlueprintFunction.Name == GetDynamicLinkFunctionName())
				{
					for(int32 InputPoseIndex = 0; InputPoseIndex < AnimBlueprintFunction.InputPoseNames.Num() && InputPoseIndex < InputPoses.Num(); ++InputPoseIndex)
					{
						int32 InputPropertyIndex = FindFunctionInputIndex(AnimBlueprintFunction, AnimBlueprintFunction.InputPoseNames[InputPoseIndex]);
						if(InputPropertyIndex != INDEX_NONE && AnimBlueprintFunction.InputPoseNodeProperties[InputPropertyIndex])
						{
							FAnimNode_SubInput* SubInputNode = AnimBlueprintFunction.InputPoseNodeProperties[InputPropertyIndex]->ContainerPtrToValuePtr<FAnimNode_SubInput>(LinkTargetInstance);
							check(SubInputNode->Name == AnimBlueprintFunction.InputPoseNames[InputPoseIndex]);
							SubInputNode->DynamicUnlink();
						}
						else
						{
							UE_LOG(LogAnimation, Warning, TEXT("Unable to dynamically unlink input pose %s."), *AnimBlueprintFunction.InputPoseNames[InputPoseIndex].ToString());
						}
					}

					break;
				}
			}
		}
	}
}

int32 FAnimNode_SubInstance::FindFunctionInputIndex(const FAnimBlueprintFunction& InAnimBlueprintFunction, const FName& InInputName)
{
	for(int32 InputPropertyIndex = 0; InputPropertyIndex < InAnimBlueprintFunction.InputPoseNames.Num(); ++InputPropertyIndex)
	{
		if(InInputName == InAnimBlueprintFunction.InputPoseNames[InputPropertyIndex])
		{
			return InputPropertyIndex;
		}
	}

	return INDEX_NONE;
}
