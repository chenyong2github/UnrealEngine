// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNodes/AnimNode_RetargetPoseFromMesh.h"
#include "Animation/AnimInstanceProxy.h"


void FAnimNode_RetargetPoseFromMesh::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread)
    FAnimNode_Base::Initialize_AnyThread(Context);

	// Initial update of the node, so we dont have a frame-delay on setup
	GetEvaluateGraphExposedInputs().Execute(Context);
}

void FAnimNode_RetargetPoseFromMesh::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(CacheBones_AnyThread)

	const FBoneContainer& RequiredBones = Context.AnimInstanceProxy->GetRequiredBones();
	if (!RequiredBones.IsValid())
	{
		return;
	}

	const bool bIsRetargeterReady = IKRetargeterAsset && Processor && Processor->IsInitialized();
	if (!bIsRetargeterReady)
	{
		return;
	}

	// rebuild mapping
	RequiredToTargetBoneMapping.Reset();

	const FReferenceSkeleton& RefSkeleton = RequiredBones.GetReferenceSkeleton();
	const FTargetSkeleton& TargetSkeleton = Processor->GetTargetSkeleton();
	
	const TArray<FBoneIndexType>& RequiredBonesArray = RequiredBones.GetBoneIndicesArray();
	for (int32 Index = 0; Index < RequiredBonesArray.Num(); ++Index)
	{
		const FBoneIndexType ReqBoneIndex = RequiredBonesArray[Index]; 
		if (ReqBoneIndex != INDEX_NONE)
		{
			const FName Name = RefSkeleton.GetBoneName(ReqBoneIndex);
			const int32 TargetBoneIndex = TargetSkeleton.FindBoneIndexByName(Name);
			if (TargetBoneIndex != INDEX_NONE)
			{
				// store require bone to target bone indices
				RequiredToTargetBoneMapping.Emplace(Index, TargetBoneIndex);
			}
		}
	}
}

void FAnimNode_RetargetPoseFromMesh::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Update_AnyThread)
	FAnimNode_Base::Update_AnyThread(Context);
    // this introduces a frame of latency in setting the pin-driven source component,
    // but we cannot do the work to extract transforms on a worker thread as it is not thread safe.
    GetEvaluateGraphExposedInputs().Execute(Context);
}
void FAnimNode_RetargetPoseFromMesh::Evaluate_AnyThread(FPoseContext& Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Evaluate_AnyThread)

	if (!(IKRetargeterAsset && Processor))
	{
		return;
	}

	const bool bIsInitialized = Processor->IsInitialized();
	const bool bInitializedWithSameMesh = Processor->GetTargetSkeleton().SkeletalMesh == Output.AnimInstanceProxy->GetSkelMeshComponent()->SkeletalMesh;
	// it's possible in editor to have anim instances initialized before PreUpdate() is called
	// which results in trying to run the retargeter without an source pose to copy from
	const bool bSourceMeshBonesCopied = !SourceMeshComponentSpaceBoneTransforms.IsEmpty();
	if (!(bIsInitialized && bInitializedWithSameMesh && bSourceMeshBonesCopied))
	{
		Output.ResetToRefPose();
		return;
	}

#if WITH_EDITOR
	// live preview IK Rig solver settings in the retarget, editor only
	// NOTE: this copies goal targets as well, but these are overwritten by IK chain goals
	if (bDriveTargetIKRigWithAsset)
	{
		Processor->CopyAllSettingsFromAsset();
	}
#endif

	// run the retargeter
	const TArray<FTransform>& RetargetedPose = Processor->RunRetargeter(SourceMeshComponentSpaceBoneTransforms);

	// copy pose back
	FCSPose<FCompactPose> ComponentPose;
	ComponentPose.InitPose(Output.Pose);
	const FCompactPose& CompactPose = ComponentPose.GetPose();
	for (const TPair<int32, int32>& Pair : RequiredToTargetBoneMapping)
	{
		const FCompactPoseBoneIndex CompactBoneIndex(Pair.Key);
		if (CompactPose.IsValidIndex(CompactBoneIndex))
		{
			const int32 TargetBoneIndex = Pair.Value;
			ComponentPose.SetComponentSpaceTransform(CompactBoneIndex, RetargetedPose[TargetBoneIndex]);
		}
	}

	// convert to local space
	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(ComponentPose, Output.Pose);
}

void FAnimNode_RetargetPoseFromMesh::PreUpdate(const UAnimInstance* InAnimInstance)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(PreUpdate)
	
	if (!IsValid(IKRetargeterAsset))
	{
		return;
	}
	
	if (!IsValid(Processor))
	{
		Processor = NewObject<UIKRetargetProcessor>(InAnimInstance->GetOwningComponent());	
	}
	
	EnsureInitialized(InAnimInstance);
	if (Processor->IsInitialized())
	{
		CopyBoneTransformsFromSource(InAnimInstance->GetSkelMeshComponent());
	}
}

#if WITH_EDITOR
void FAnimNode_RetargetPoseFromMesh::SetProcessorNeedsInitialized()
{
	if (Processor)
	{
		Processor->SetNeedsInitialized();
	}
}
#endif

const UIKRetargetProcessor* FAnimNode_RetargetPoseFromMesh::GetRetargetProcessor() const
{
	return Processor;
}

void FAnimNode_RetargetPoseFromMesh::EnsureInitialized(const UAnimInstance* InAnimInstance)
{
	// has user supplied a retargeter asset?
	if (!IKRetargeterAsset)
	{
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
		return; // can't do anything if we don't have a source mesh
	}

	// store all the components that were used to initialize
	// if in future updates, any of this are mismatched, we have to re-initialize
	CurrentlyUsedSourceMesh = SourceMeshComponent->SkeletalMesh;
	CurrentlyUsedTargetMesh = InAnimInstance->GetSkelMeshComponent()->SkeletalMesh;
	const bool bMeshesAreValid = CurrentlyUsedSourceMesh.IsValid() && CurrentlyUsedTargetMesh.IsValid();
	if (!bMeshesAreValid)
	{
		return; // cannot initialize if components are missing skeletal mesh references
	}

	// try initializing the processor
	if (!Processor->IsInitialized())
	{
		// initialize retarget processor with source and target skeletal meshes
		// (anim instance is passed in as outer UObject for new UIKRigProcessor) 
		Processor->Initialize(
			CurrentlyUsedSourceMesh.Get(),
			CurrentlyUsedTargetMesh.Get(),
			IKRetargeterAsset);
	}
}

void FAnimNode_RetargetPoseFromMesh::CopyBoneTransformsFromSource(USkeletalMeshComponent* TargetMeshComponent)
{
	if (!SourceMeshComponent.IsValid())
	{
		return; 
	}

	USkeletalMeshComponent* SourceMeshComp =  SourceMeshComponent.Get();
	
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
	
	const bool bUROInSync =
		SourceMeshComp->ShouldUseUpdateRateOptimizations() &&
		SourceMeshComp->AnimUpdateRateParams != nullptr &&
		SourceMeshComponent->AnimUpdateRateParams == TargetMeshComponent->AnimUpdateRateParams;
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
}
