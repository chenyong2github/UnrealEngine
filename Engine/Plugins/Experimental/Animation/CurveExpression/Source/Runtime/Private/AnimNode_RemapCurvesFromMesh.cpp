// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNode_RemapCurvesFromMesh.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNodeFunctionRef.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/ExposedValueHandler.h"
#include "CurveExpressionModule.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimCurveTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNode_RemapCurvesFromMesh)

void FAnimNode_RemapCurvesFromMesh::Initialize_AnyThread(
	const FAnimationInitializeContext& Context
	)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread)
	Super::Initialize_AnyThread(Context);
	SourcePose.Initialize(Context);
}


void FAnimNode_RemapCurvesFromMesh::CacheBones_AnyThread(
	const FAnimationCacheBonesContext& Context
	)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(CacheBones_AnyThread)
	Super::CacheBones_AnyThread(Context);
	SourcePose.CacheBones(Context);
}
	

void FAnimNode_RemapCurvesFromMesh::VerifyExpressions(
	TFunction<void(FString)> InReportingFunc
	) const
{
	using namespace CurveExpression::Evaluator;

	auto ReportAndLog = [InReportingFunc](FString InMessage)
	{
		if (InReportingFunc)
		{
			InReportingFunc(InMessage);
		}
		UE_LOG(LogCurveExpression, Warning, TEXT("%s"), *InMessage);
	};
	
	if (!ExpressionEngine.IsSet())
	{
		return;
	}
	
	if (CurveExpressions.IsEmpty())
	{
		ReportAndLog(TEXT("No curve expressions set."));
		return;
	}

	bool bFoundError = false;
	const FEngine VerificationEngine(
		ExpressionEngine->GetConstantValues(),
		EParseFlags::ValidateConstants);

	for (const TPair<FName, FString>& ExpressionPair: CurveExpressions)
	{
		TOptional<FParseError> Error = VerificationEngine.Verify(ExpressionPair.Value);
		if (Error.IsSet())
		{
			ReportAndLog(FString::Printf(TEXT("Expression error in '%s': %s"), *ExpressionPair.Value, *Error->Message));
			bFoundError = true;
		}
	}

	if (!bFoundError)
	{
		UE_LOG(LogCurveExpression, Display, TEXT("Curve expressions verified ok."))
	}
}



bool FAnimNode_RemapCurvesFromMesh::CanVerifyExpressions() const
{
	return ExpressionEngine.IsSet();
}


void FAnimNode_RemapCurvesFromMesh::Update_AnyThread(
	const FAnimationUpdateContext& Context
	)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Update_AnyThread)

	// Run update on input pose nodes
	SourcePose.Update(Context);

	// Evaluate any BP logic plugged into this node
	GetEvaluateGraphExposedInputs().Execute(Context);
	
	if (bExpressionsImmutable && ExpressionEngine.IsSet() && !CurveExpressions.IsEmpty() && CachedExpressions.IsEmpty())
	{
		CachedExpressions.Reset();
		for (const TPair<FName, FString>& ExpressionPair: CurveExpressions)
		{
			using namespace CurveExpression::Evaluator;

			TVariant<FExpressionObject, FParseError> Result = ExpressionEngine->Parse(ExpressionPair.Value);
			if (const FExpressionObject* Expression = Result.TryGet<FExpressionObject>())
			{
				CachedExpressions.Add(ExpressionPair.Key, *Expression);
			}
		}
	}			
}


void FAnimNode_RemapCurvesFromMesh::Evaluate_AnyThread(
	FPoseContext& Output
	)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Evaluate_AnyThread)

	FPoseContext SourceData(Output);
	SourcePose.Evaluate(SourceData);
	
	Output = SourceData;
	
	// If we have an expression engine, evaluate the expressions that have a matching target curve.
	// If the expressions are not immutable between compiles, then we need to reparse them each time. 
	if (ExpressionEngine.IsSet())
	{
		if (bExpressionsImmutable)
		{
			FBlendedCurve Curve;
			Curve.Reserve(CachedExpressions.Num());
			for (const TTuple<FName, CurveExpression::Evaluator::FExpressionObject>& ExpressionItem : CachedExpressions)
			{
				Curve.Add(ExpressionItem.Key, ExpressionEngine->Execute(ExpressionItem.Value)); 
			}

			Output.Curve.Combine(Curve);
		}
		else
		{
			FBlendedCurve Curve;
			Curve.Reserve(CurveExpressions.Num());

			for (const TTuple<FName, FString>& ExpressionItem : CurveExpressions)
			{
				TOptional<float> Result = ExpressionEngine->Evaluate(ExpressionItem.Value);
				if (Result.IsSet())
				{
					Curve.Add(ExpressionItem.Key, *Result);
				}
			}

			Output.Curve.Combine(Curve);
		}
	}
}


void FAnimNode_RemapCurvesFromMesh::GatherDebugData(
	FNodeDebugData& DebugData
	)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(GatherDebugData)
	FString DebugLine = DebugData.GetNodeName(this);

	DebugLine += FString::Printf(TEXT("('%s')"), *GetNameSafe(CurrentlyUsedSourceMeshComponent.IsValid() ? CurrentlyUsedSourceMeshComponent.Get()->GetSkeletalMeshAsset() : nullptr));
	DebugData.AddDebugItem(DebugLine, true);
}


void FAnimNode_RemapCurvesFromMesh::PreUpdate(
	const UAnimInstance* InAnimInstance
	)
{
	QUICK_SCOPE_CYCLE_COUNTER(FAnimNode_RemapCurvesFromMesh_PreUpdate);

	// Make sure we're using the correct source and target skeleton components, since they
	// may have changed from underneath us.
	RefreshMeshComponent(InAnimInstance->GetSkelMeshComponent());

	const USkeletalMeshComponent* CurrentMeshComponent = CurrentlyUsedSourceMeshComponent.IsValid() ? CurrentlyUsedSourceMeshComponent.Get() : nullptr;

	if (CurrentMeshComponent && CurrentMeshComponent->GetSkeletalMeshAsset() && CurrentMeshComponent->IsRegistered())
	{
		// If our source is running under leader-pose, then get bone data from there
		if(USkeletalMeshComponent* LeaderPoseComponent = Cast<USkeletalMeshComponent>(CurrentMeshComponent->LeaderPoseComponent.Get()))
		{
			CurrentMeshComponent = LeaderPoseComponent;
		}

		// re-check mesh component validity as it may have changed to leader
		if(CurrentMeshComponent->GetSkeletalMeshAsset() && CurrentMeshComponent->IsRegistered())
		{
			if (ExpressionEngine.IsSet())
			{
				if (const UAnimInstance* SourceAnimInstance = CurrentMeshComponent->GetAnimInstance())
				{
					ExpressionEngine->UpdateConstantValues(SourceAnimInstance->GetAnimationCurveList(EAnimCurveType::AttributeCurve));
				}
			}
		}
		else
		{
			ExpressionEngine.Reset();
		}
	}
}


void FAnimNode_RemapCurvesFromMesh::ReinitializeMeshComponent(
	USkeletalMeshComponent* InNewSkeletalMeshComponent, 
	USkeletalMeshComponent* InTargetMeshComponent
	)
{
	CurrentlyUsedSourceMeshComponent.Reset();
	CurrentlyUsedSourceMesh.Reset();
	CurrentlyUsedTargetMesh.Reset();
	
	ExpressionEngine.Reset();
	CachedExpressions.Reset();

	if (InTargetMeshComponent && IsValid(InNewSkeletalMeshComponent) && InNewSkeletalMeshComponent->GetSkeletalMeshAsset())
	{
		USkeletalMesh* SourceSkelMesh = InNewSkeletalMeshComponent->GetSkeletalMeshAsset();
		USkeletalMesh* TargetSkelMesh = InTargetMeshComponent->GetSkeletalMeshAsset();
		
		if (IsValid(SourceSkelMesh) && !SourceSkelMesh->HasAnyFlags(RF_NeedPostLoad) &&
			IsValid(TargetSkelMesh) && !TargetSkelMesh->HasAnyFlags(RF_NeedPostLoad))
		{
			CurrentlyUsedSourceMeshComponent = InNewSkeletalMeshComponent;
			CurrentlyUsedSourceMesh = SourceSkelMesh;
			CurrentlyUsedTargetMesh = TargetSkelMesh;

			// Grab the source curves and use their names to seed the expression evaluation engine.
			TMap<FName, float> SourceCurves;
			const USkeleton* SourceSkeleton = SourceSkelMesh->GetSkeleton();
			if (ensureMsgf(SourceSkeleton, TEXT("Invalid null source skeleton : %s"), *GetNameSafe(TargetSkelMesh)))
			{
				// TODO: is this good enough for future use? ExpressionEngine appears to have been built on the assumption that curves are always pre-defined.
				SourceSkeleton->ForEachCurveMetaData([&SourceCurves](FName InCurveName, const FCurveMetaData& InCurveMetaData)
				{
					SourceCurves.Add(InCurveName, 0.0f);
				});

				ExpressionEngine.Emplace(MoveTemp(SourceCurves));
			}
		}
	}
}
	

void FAnimNode_RemapCurvesFromMesh::RefreshMeshComponent(
	USkeletalMeshComponent* InTargetMeshComponent
	)
{
	auto ResetMeshComponent = [this](USkeletalMeshComponent* InMeshComponent, USkeletalMeshComponent* InTargetMeshComponent)
	{
		// if current mesh exists, but not same as input mesh
		if (const USkeletalMeshComponent* CurrentMeshComponent = CurrentlyUsedSourceMeshComponent.Get())
		{
			// if component has been changed, reinitialize
			if (CurrentMeshComponent != InMeshComponent)
			{
				ReinitializeMeshComponent(InMeshComponent, InTargetMeshComponent);
			}
			// if component is still same but mesh has been changed, we have to reinitialize
			else if (CurrentMeshComponent->GetSkeletalMeshAsset() != CurrentlyUsedSourceMesh.Get())
			{
				ReinitializeMeshComponent(InMeshComponent, InTargetMeshComponent);
			}
			else if (InTargetMeshComponent)
			{
				// see if target mesh has changed
				if (InTargetMeshComponent->GetSkeletalMeshAsset() != CurrentlyUsedTargetMesh.Get())
				{
					ReinitializeMeshComponent(InMeshComponent, InTargetMeshComponent);
				}
			}
		}
		// if not valid, but input mesh is
		else if (!CurrentMeshComponent && InMeshComponent)
		{
			ReinitializeMeshComponent(InMeshComponent, InTargetMeshComponent);
		}
	};

	if(SourceMeshComponent.IsValid())
	{
		ResetMeshComponent(SourceMeshComponent.Get(), InTargetMeshComponent);
	}
	else if (bUseAttachedParent)
	{
		if (InTargetMeshComponent)
		{
			if (USkeletalMeshComponent* ParentComponent = Cast<USkeletalMeshComponent>(InTargetMeshComponent->GetAttachParent()))
			{
				ResetMeshComponent(ParentComponent, InTargetMeshComponent);
			}
			else
			{
				CurrentlyUsedSourceMeshComponent.Reset();
			}
		}
		else
		{
			CurrentlyUsedSourceMeshComponent.Reset();
		}
	}
	else
	{
		CurrentlyUsedSourceMeshComponent.Reset();
	}
}
