// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNodes/AnimNode_IKRetargeter.h"
#include "Animation/AnimInstanceProxy.h"


void FAnimNode_IKRetargeter::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread)
    FAnimNode_Base::Initialize_AnyThread(Context);

	// Initial update of the node, so we dont have a frame-delay on setup
	GetEvaluateGraphExposedInputs().Execute(Context);
}

void FAnimNode_IKRetargeter::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(CacheBones_AnyThread)
}

void FAnimNode_IKRetargeter::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Update_AnyThread)
	FAnimNode_Base::Update_AnyThread(Context);
    // this introduces a frame of latency in setting the pin-driven source component,
    // but we cannot do the work to extract transforms on a worker thread as it is not thread safe.
    GetEvaluateGraphExposedInputs().Execute(Context);
}
void FAnimNode_IKRetargeter::Evaluate_AnyThread(FPoseContext& Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Evaluate_AnyThread)

	if (!bIsInitialized)
	{
		Output.ResetToRefPose();
		return;
	}

	// run the retargeter
	CurrentlyUsedRetargeter->RunRetargeter(SourceMeshComponentSpaceBoneTransforms, bEnableIK);

	// copy pose back
	FCSPose<FCompactPose> ComponentPose;
	ComponentPose.InitPose(Output.Pose);
	for (FCompactPoseBoneIndex CompactBoneIndex : Output.Pose.ForEachBoneIndex())
	{
		ComponentPose.SetComponentSpaceTransform(CompactBoneIndex, CurrentlyUsedRetargeter->TargetSkeleton.OutputGlobalPose[CompactBoneIndex.GetInt()]);
	}

	// convert to local space
	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(ComponentPose, Output.Pose);
}

void FAnimNode_IKRetargeter::PreUpdate(const UAnimInstance* InAnimInstance)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(PreUpdate)
	EnsureInitialized(InAnimInstance);
	if (bIsInitialized)
	{
		CopyBoneTransformsFromSource(InAnimInstance->GetSkelMeshComponent());
	}
}

void FAnimNode_IKRetargeter::EnsureInitialized(const UAnimInstance* InAnimInstance)
{
	// has user supplied a retargeter asset?
	if (!IKRetargeterAsset)
	{
		bIsInitialized = false;
		return;
	}

	// if user hasn't explicitly connected a source mesh, optionally use the parent mesh component (if there is one) 
	if (!SourceMeshComponent.IsValid() && bUseAttachedParent)
	{
		USkeletalMeshComponent* TargetMesh = InAnimInstance->GetSkelMeshComponent();
		USkeletalMeshComponent* ParentComponent = Cast<USkeletalMeshComponent>(TargetMesh->GetAttachParent());
		if (ParentComponent)
		{
			SourceMeshComponent = ParentComponent;
		}
	}
	
	// has a source mesh been plugged in or found?
	if (!SourceMeshComponent.IsValid())
	{
		bIsInitialized = false;
		return; // can't do anything if we don't have a source mesh
	}
	
	// are all the parts already loaded?
	const bool bPartsLoaded =
	   CurrentlyUsedSourceMeshComponent.IsValid()
    && CurrentlyUsedSourceMesh.IsValid()
    && CurrentlyUsedTargetMesh.IsValid()
	&& CurrentlyUsedRetargeter
	&& CurrentlyUsedSourceIKRig.IsValid()
	&& CurrentlyUsedTargetIKRig.IsValid();
	if (!bPartsLoaded)
	{
		InitializeRetargetData(InAnimInstance); // nothing loaded yet, initialize
		return;
	}

	// so parts have been loaded, but have any of the parts changed since we last initialized?
	USkeletalMeshComponent* SourceMeshComp = SourceMeshComponent.Get();
	USkeletalMesh* TargetMesh = InAnimInstance->GetSkelMeshComponent()->SkeletalMesh;
	USkeletalMesh* SourceMesh = SourceMeshComp->SkeletalMesh;
	const bool bSameParts =
	   CurrentlyUsedSourceMeshComponent == SourceMeshComp 
    && CurrentlyUsedTargetMesh == TargetMesh 
    && CurrentlyUsedSourceMesh == SourceMesh 
	&& CurrentlyUsedSourceIKRig == IKRetargeterAsset->SourceIKRigAsset
	&& CurrentlyUsedTargetIKRig == IKRetargeterAsset->TargetIKRigAsset;
	if (!bSameParts)
	{
		InitializeRetargetData(InAnimInstance); // parts have changed, re-initialize
		return;
	}
}

void FAnimNode_IKRetargeter::InitializeRetargetData(const UAnimInstance* InAnimInstance)
{
	// assume we fail until we don't
	bIsInitialized = false;

	// store all the components that were used to initialize
	// if in future updates, any of this are mismatched, we have to re-initialize
	CurrentlyUsedSourceMeshComponent = SourceMeshComponent;
	CurrentlyUsedSourceMesh = SourceMeshComponent->SkeletalMesh;
	CurrentlyUsedTargetMesh = InAnimInstance->GetSkelMeshComponent()->SkeletalMesh;
	CurrentlyUsedRetargeter = DuplicateObject(IKRetargeterAsset, const_cast<UAnimInstance*>(InAnimInstance));
	CurrentlyUsedSourceIKRig = CurrentlyUsedRetargeter->SourceIKRigAsset;
	CurrentlyUsedTargetIKRig = CurrentlyUsedRetargeter->TargetIKRigAsset;

	const bool bMeshesAreValid = CurrentlyUsedSourceMesh.IsValid() && CurrentlyUsedTargetMesh.IsValid();
	if (!bMeshesAreValid)
	{
		return; // cannot initialize if components are missing skeletal mesh references
	}

	const bool bRetargeterIsValid =
	   CurrentlyUsedRetargeter
	&& CurrentlyUsedSourceIKRig.IsValid()
	&& CurrentlyUsedTargetIKRig.IsValid();
	if (!bRetargeterIsValid)
	{
		return; // cannot initialize unless we have a retargeter with BOTH source AND target IK Rigs
	}

	// initialize retargeter with source and target skeletal meshes
	// (anim instance is passed in as outer UObject for new UIKRigProcessor) 
	CurrentlyUsedRetargeter->Initialize(
		CurrentlyUsedSourceMesh.Get(),
		CurrentlyUsedTargetMesh.Get(),
		const_cast<UAnimInstance*>(InAnimInstance));
	
	// made it!
	bIsInitialized = CurrentlyUsedRetargeter->bIsLoadedAndValid;
}

void FAnimNode_IKRetargeter::CopyBoneTransformsFromSource(USkeletalMeshComponent* TargetMeshComponent)
{
	if (!CurrentlyUsedSourceMeshComponent.IsValid())
	{
		return; 
	}

	USkeletalMeshComponent* SourceMeshComp =  CurrentlyUsedSourceMeshComponent.Get();
	
	// is the source mesh ticking?
	if (!SourceMeshComp->IsRegistered())
		
	{
		CurrentlyUsedSourceMesh.Reset(); // forces reinitialization when re-registered
		return; // skip copying pose when component is no longer ticking
	}
	
	// if our source is running under master-pose, then get bone data from there
	if(USkeletalMeshComponent* MasterPoseComponent = Cast<USkeletalMeshComponent>(SourceMeshComponent->MasterPoseComponent.Get()))
	{
		SourceMeshComp = MasterPoseComponent;
	}

	// re-check mesh component validity as it may have changed to master
	if (!(SourceMeshComp->SkeletalMesh && SourceMeshComp->IsRegistered()))
	{
		return; // master pose either missing skeletal mesh reference or not ticking, either way, we aren't copying from it
	}
	
	const bool bUROInSync = SourceMeshComp->ShouldUseUpdateRateOptimizations() && SourceMeshComp->AnimUpdateRateParams != nullptr && SourceMeshComponent->AnimUpdateRateParams == TargetMeshComponent->AnimUpdateRateParams;
	const bool bUsingExternalInterpolation = SourceMeshComp->IsUsingExternalInterpolation();
	const TArray<FTransform>& CachedComponentSpaceTransforms = SourceMeshComp->GetCachedComponentSpaceTransforms();
	const bool bArraySizesMatch = CachedComponentSpaceTransforms.Num() == SourceMeshComp->GetComponentSpaceTransforms().Num();

	// copy source array from the appropriate location
	SourceMeshComponentSpaceBoneTransforms.Reset();
	if ((bUROInSync || bUsingExternalInterpolation) && bArraySizesMatch)
	{
		SourceMeshComponentSpaceBoneTransforms.Append(CachedComponentSpaceTransforms); // copy from source's cache
	}
	else
	{
		SourceMeshComponentSpaceBoneTransforms.Append(SourceMeshComp->GetComponentSpaceTransforms()); // copy directly
	}

	// ref skeleton is need for parent index lookups later, so store it now
	CurrentlyUsedSourceMesh = SourceMeshComp->SkeletalMesh;
}

USkeletalMeshComponent* FAnimNode_IKRetargeter::GetSourceMesh() const
{
	return nullptr;
}
