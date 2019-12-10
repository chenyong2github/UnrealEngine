// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimNode_LinkedAnimGraph.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_Inertialization.h"
#include "Animation/AnimNode_LinkedInputPose.h"
#include "Animation/AnimNode_Root.h"

static float GetBlendDuration(const IAnimClassInterface* PriorAnimBPClass, const IAnimClassInterface* NewAnimBPClass, FName Layer)
{
	const FAnimGraphBlendOptions* PriorBlendOptions = PriorAnimBPClass ? PriorAnimBPClass->GetGraphBlendOptions().Find(Layer) : nullptr;
	const FAnimGraphBlendOptions* NewBlendOptions = NewAnimBPClass ? NewAnimBPClass->GetGraphBlendOptions().Find(Layer) : nullptr;

	float BlendOutTime = PriorBlendOptions ? PriorBlendOptions->BlendOutTime : -1.0f;
	float BlendInTime = NewBlendOptions ? NewBlendOptions->BlendInTime : -1.0f;

	if (BlendInTime < 0.0f)
	{
		return BlendOutTime;
	}

	if (BlendOutTime < 0.0f)
	{
		return BlendInTime;
	}

	return FMath::Min(BlendInTime, BlendOutTime);
}

FAnimNode_LinkedAnimGraph::FAnimNode_LinkedAnimGraph()
	: InstanceClass(nullptr)
	, Tag(NAME_None)
	, LinkedRoot(nullptr)
	, PendingBlendDuration(-1.0f)
{
}

void FAnimNode_LinkedAnimGraph::InitializeSubGraph_AnyThread(const FAnimationInitializeContext& Context)
{
	UAnimInstance* InstanceToRun = GetTargetInstance<UAnimInstance>();
	if(InstanceToRun && LinkedRoot)
	{
		FAnimInstanceProxy& Proxy = InstanceToRun->GetProxyOnAnyThread<FAnimInstanceProxy>();

		// Make sure we have valid objects in place for the sub-graph init
		Proxy.InitializeObjects(InstanceToRun);

		Proxy.InitializationCounter.SynchronizeWith(Context.AnimInstanceProxy->InitializationCounter);
		Proxy.InitializeRootNode_WithRoot(LinkedRoot);
	}
}

void FAnimNode_LinkedAnimGraph::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_Base::Initialize_AnyThread(Context);

	InitializeSubGraph_AnyThread(Context);

	// Make sure we propagate down all input poses, as they may not all be linked in the linked graph
	for(FPoseLink& InputPose : InputPoses)
	{
		InputPose.Initialize(Context);
	}
}

void FAnimNode_LinkedAnimGraph::CacheBonesSubGraph_AnyThread(const FAnimationCacheBonesContext& Context)
{
	UAnimInstance* InstanceToRun = GetTargetInstance<UAnimInstance>();
	if(InstanceToRun && LinkedRoot)
	{
		FAnimInstanceProxy& Proxy = InstanceToRun->GetProxyOnAnyThread<FAnimInstanceProxy>();
		Proxy.CachedBonesCounter.SynchronizeWith(Context.AnimInstanceProxy->CachedBonesCounter);

		// Note not calling Proxy.CacheBones_WithRoot here as it is guarded by
		// bBoneCachesInvalidated, which is handled at a higher level
		FAnimationCacheBonesContext LinkedContext(&Proxy);
		LinkedRoot->CacheBones_AnyThread(LinkedContext);
	}
}

void FAnimNode_LinkedAnimGraph::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	CacheBonesSubGraph_AnyThread(Context);

	// Make sure we propagate down all input poses, as they may not all be linked in the linked graph
	for(FPoseLink& InputPose : InputPoses)
	{
		InputPose.CacheBones(Context);
	}
}

void FAnimNode_LinkedAnimGraph::Update_AnyThread(const FAnimationUpdateContext& InContext)
{
	GetEvaluateGraphExposedInputs().Execute(InContext);

	UAnimInstance* InstanceToRun = GetTargetInstance<UAnimInstance>();
	if(InstanceToRun && LinkedRoot)
	{
		FAnimInstanceProxy& Proxy = InstanceToRun->GetProxyOnAnyThread<FAnimInstanceProxy>();
		Proxy.UpdateCounter.SynchronizeWith(InContext.AnimInstanceProxy->UpdateCounter);

		PropagateInputProperties(InContext.AnimInstanceProxy->GetAnimInstanceObject());

		// We can call this unconditionally here now because linked anim instances are forced to have a parallel update
		// in USkeletalMeshComponent::TickAnimation. It used to be the case that we could do non-parallel work in 
		// USkeletalMeshComponent::TickAnimation, which would mean we would have to skip doing that work here.
		FAnimationUpdateContext NewContext = InContext.WithOtherProxy(&Proxy);
		Proxy.UpdateAnimation_WithRoot(NewContext, LinkedRoot, GetDynamicLinkFunctionName());
	}
	else if(InputPoses.Num() > 0)
	{
		// If we have no valid instance (self or otherwise), we need to propagate down the graph to make sure
		// subsequent nodes get properly updated
		InputPoses[0].Update(InContext);
	}

	// Consume pending inertial blend request
	if(PendingBlendDuration >= 0.0f)
	{
		if(InputPoses.Num() > 0)
		{
			FAnimNode_Inertialization* InertializationNode = InContext.GetAncestor<FAnimNode_Inertialization>();
			if(InertializationNode)
			{
				InertializationNode->RequestInertialization(PendingBlendDuration);
			}
			else if (PendingBlendDuration != 0.0f)
			{
				FAnimNode_Inertialization::LogRequestError(InContext, InputPoses[0]);
			}
		}

		PendingBlendDuration = -1.0f;
	}
}

void FAnimNode_LinkedAnimGraph::Evaluate_AnyThread(FPoseContext& Output)
{
	UAnimInstance* InstanceToRun = GetTargetInstance<UAnimInstance>();
	if(InstanceToRun && LinkedRoot)
	{
		FAnimInstanceProxy& Proxy = InstanceToRun->GetProxyOnAnyThread<FAnimInstanceProxy>();
		Proxy.EvaluationCounter.SynchronizeWith(Output.AnimInstanceProxy->EvaluationCounter);
		Output.Pose.SetBoneContainer(&Proxy.GetRequiredBones());

		// Create an evaluation context
		FPoseContext EvaluationContext(&Proxy, Output.ExpectsAdditivePose());
		EvaluationContext.ResetToRefPose();
			
		// Run the anim blueprint
		Proxy.EvaluateAnimation_WithRoot(EvaluationContext, LinkedRoot);

		// Move the curves
		Output.Curve.MoveFrom(EvaluationContext.Curve);
		Output.Pose.MoveBonesFrom(EvaluationContext.Pose);
	}
	else if(InputPoses.Num() > 0)
	{
		// If we have no valid instance (self or otherwise), we need to propagate down the graph to make sure
		// subsequent nodes get properly evaluated
		InputPoses[0].Evaluate(Output);
	}
	else
	{
		Output.ResetToRefPose();
	}
}

void FAnimNode_LinkedAnimGraph::GatherDebugData(FNodeDebugData& DebugData)
{
	// Add our entry
	FString DebugLine = DebugData.GetNodeName(this);
	DebugLine += FString::Printf(TEXT("Target: %s"), (*InstanceClass) ? *InstanceClass->GetName() : TEXT("None"));

	DebugData.AddDebugItem(DebugLine);

	UAnimInstance* InstanceToRun = GetTargetInstance<UAnimInstance>();
	// Gather data from the linked instance
	if(InstanceToRun && LinkedRoot)
	{
		FAnimInstanceProxy& Proxy = InstanceToRun->GetProxyOnAnyThread<FAnimInstanceProxy>();
		Proxy.GatherDebugData_WithRoot(DebugData.BranchFlow(1.0f), LinkedRoot, GetDynamicLinkFunctionName());
	}
	else if(InputPoses.Num() > 0)
	{
		// If we have no valid instance (self or otherwise), we need to propagate down the graph to make sure
		// subsequent nodes get their debug data properly collected to reflect relevancy
		InputPoses[0].GatherDebugData(DebugData);
	}
}

void FAnimNode_LinkedAnimGraph::OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance)
{
	UAnimInstance* InstanceToRun = GetTargetInstance<UAnimInstance>();

	if(*InstanceClass)
	{
		ReinitializeLinkedAnimInstance(InAnimInstance);
	}
	else if(InstanceToRun)
	{
		// We have an instance but no instance class
		TeardownInstance();
	}
}

void FAnimNode_LinkedAnimGraph::TeardownInstance()
{
	UAnimInstance* InstanceToRun = GetTargetInstance<UAnimInstance>();
	if (InstanceToRun)
	{
		InstanceToRun->UninitializeAnimation();
		InstanceToRun = nullptr;
	}
}

void FAnimNode_LinkedAnimGraph::ReinitializeLinkedAnimInstance(const UAnimInstance* InOwningAnimInstance, UAnimInstance* InNewAnimInstance)
{
	UAnimInstance* InstanceToRun = GetTargetInstance<UAnimInstance>();

	IAnimClassInterface* PriorAnimBPClass = InstanceToRun ? IAnimClassInterface::GetFromClass(InstanceToRun->GetClass()) : nullptr;

	if(*InstanceClass || InNewAnimInstance)
	{
		USkeletalMeshComponent* MeshComp = InOwningAnimInstance->GetSkelMeshComponent();
		check(MeshComp);
		// Full reinit, kill old instances
		if(InstanceToRun)
		{
			DynamicUnlink(const_cast<UAnimInstance*>(InOwningAnimInstance));

			MeshComp->GetLinkedAnimInstances().Remove(InstanceToRun);
			// Never delete the owning animation instance
			if (InstanceToRun != InOwningAnimInstance)
			{
				InstanceToRun->MarkPendingKill();
			}
			InstanceToRun = nullptr;
		}

		// Need an instance to run, so create it now
		InstanceToRun = InNewAnimInstance ? InNewAnimInstance : NewObject<UAnimInstance>(MeshComp, InstanceClass);
		if (InNewAnimInstance == nullptr)
		{
			// if incoming AnimInstance was null, it was created by this function
			// we mark them as created by linked anim graph
			// this is to know who owns memory instance
			InstanceToRun->bCreatedByLinkedAnimGraph = true;
		}

		SetTargetInstance(InstanceToRun);

		// Link before we call InitializeAnimation() so we propgate the call to linked input poses
		DynamicLink(const_cast<UAnimInstance*>(InOwningAnimInstance));

		if(InNewAnimInstance == nullptr)
		{
			// Initialize the new instance
			InstanceToRun->InitializeAnimation();

			MeshComp->GetLinkedAnimInstances().Add(InstanceToRun);
		}

		InitializeProperties(InOwningAnimInstance, InstanceToRun->GetClass());

		IAnimClassInterface* NewAnimBPClass = IAnimClassInterface::GetFromClass(InstanceToRun->GetClass());

		RequestBlend(PriorAnimBPClass, NewAnimBPClass);
	}
	else if(InstanceToRun)
	{
		// We have an instance but no instance class
		TeardownInstance();
	}
}

void FAnimNode_LinkedAnimGraph::SetAnimClass(TSubclassOf<UAnimInstance> InClass, const UAnimInstance* InOwningAnimInstance)
{
	UClass* NewClass = InClass.Get();
	if(NewClass)
	{
		// Verify target skeleton match at runtime
		IAnimClassInterface* LinkedAnimBlueprintClass = IAnimClassInterface::GetFromClass(NewClass);
		IAnimClassInterface* OuterAnimBlueprintClass = IAnimClassInterface::GetFromClass(InOwningAnimInstance->GetClass());
		USkeleton* LinkedSkeleton = LinkedAnimBlueprintClass->GetTargetSkeleton();
		USkeleton* OuterSkeleton = OuterAnimBlueprintClass->GetTargetSkeleton();
		if(LinkedSkeleton != OuterSkeleton)
		{
			UE_LOG(LogAnimation, Warning, TEXT("Setting linked anim instance class: Class has a mismatched target skeleton. Expected %s, found %s."), OuterSkeleton ? *OuterSkeleton->GetName() : TEXT("null"), LinkedSkeleton ? *LinkedSkeleton->GetName() : TEXT("null"));
			return;
		}
	}

	// Verified OK, so set it now
	TSubclassOf<UAnimInstance> OldClass = InstanceClass;
	InstanceClass = InClass;

	if(InstanceClass != OldClass)
	{
		ReinitializeLinkedAnimInstance(InOwningAnimInstance);
	}
}

FName FAnimNode_LinkedAnimGraph::GetDynamicLinkFunctionName() const
{
	return NAME_AnimGraph;
}

UAnimInstance* FAnimNode_LinkedAnimGraph::GetDynamicLinkTarget(UAnimInstance* InOwningAnimInstance) const
{
	return GetTargetInstance<UAnimInstance>();
}

void FAnimNode_LinkedAnimGraph::DynamicLink(UAnimInstance* InOwningAnimInstance)
{
	UAnimInstance* LinkTargetInstance = GetDynamicLinkTarget(InOwningAnimInstance);
	if(LinkTargetInstance)
	{
		IAnimClassInterface* SubAnimBlueprintClass = IAnimClassInterface::GetFromClass(LinkTargetInstance->GetClass());
		if(SubAnimBlueprintClass)
		{
			FAnimInstanceProxy* NonConstProxy = &InOwningAnimInstance->GetProxyOnAnyThread<FAnimInstanceProxy>();
			const FName FunctionToLink = GetDynamicLinkFunctionName();

			// Link input poses
			for(const FAnimBlueprintFunction& AnimBlueprintFunction : SubAnimBlueprintClass->GetAnimBlueprintFunctions())
			{
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
							FAnimNode_LinkedInputPose* LinkedInputPoseNode = AnimBlueprintFunction.InputPoseNodeProperties[InputPropertyIndex]->ContainerPtrToValuePtr<FAnimNode_LinkedInputPose>(LinkTargetInstance);
							check(LinkedInputPoseNode->Name == AnimBlueprintFunction.InputPoseNames[InputPoseIndex]);
							LinkedInputPoseNode->DynamicLink(NonConstProxy, &InputPoses[InputPoseIndex]);
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

void FAnimNode_LinkedAnimGraph::DynamicUnlink(UAnimInstance* InOwningAnimInstance)
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
			const FName FunctionToLink = GetDynamicLinkFunctionName();

			// Link input poses
			for(const FAnimBlueprintFunction& AnimBlueprintFunction : SubAnimBlueprintClass->GetAnimBlueprintFunctions())
			{
				if(AnimBlueprintFunction.Name == FunctionToLink)
				{
					for(int32 InputPoseIndex = 0; InputPoseIndex < AnimBlueprintFunction.InputPoseNames.Num() && InputPoseIndex < InputPoses.Num(); ++InputPoseIndex)
					{
						int32 InputPropertyIndex = FindFunctionInputIndex(AnimBlueprintFunction, AnimBlueprintFunction.InputPoseNames[InputPoseIndex]);
						if(InputPropertyIndex != INDEX_NONE && AnimBlueprintFunction.InputPoseNodeProperties[InputPropertyIndex])
						{
							FAnimNode_LinkedInputPose* LinkedInputPoseNode = AnimBlueprintFunction.InputPoseNodeProperties[InputPropertyIndex]->ContainerPtrToValuePtr<FAnimNode_LinkedInputPose>(LinkTargetInstance);
							check(LinkedInputPoseNode->Name == AnimBlueprintFunction.InputPoseNames[InputPoseIndex]);
							LinkedInputPoseNode->DynamicUnlink();
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

int32 FAnimNode_LinkedAnimGraph::FindFunctionInputIndex(const FAnimBlueprintFunction& InAnimBlueprintFunction, const FName& InInputName)
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

void FAnimNode_LinkedAnimGraph::RequestBlend(const IAnimClassInterface* PriorAnimBPClass, const IAnimClassInterface* NewAnimBPClass)
{
	PendingBlendDuration = GetBlendDuration(PriorAnimBPClass, NewAnimBPClass, GetDynamicLinkFunctionName());
}
