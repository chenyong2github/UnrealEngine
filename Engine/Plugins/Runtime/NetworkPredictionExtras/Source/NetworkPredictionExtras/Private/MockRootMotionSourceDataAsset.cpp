// Copyright Epic Games, Inc. All Rights Reserved


#include "MockRootMotionSourceDataAsset.h"
#include "Curves/CurveVector.h"
#include "Animation/AnimMontage.h"
#include "NetworkPredictionCheck.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogMockRootMotionSourceDataAsset, Log, All);

int32 UMockRootMotionSourceDataAsset::FindRootMotionSourceID(UAnimMontage* Montage)
{
	return Montages.Montages.Find(Montage);
}

int32 UMockRootMotionSourceDataAsset::FindRootMotionSourceID(UCurveVector* Curve)
{
	int32 CurveIdx = Curves.Curves.Find(Curve);
	if (CurveIdx != INDEX_NONE)
	{
		// Crude way of mapping ID into curves/montages. This wont scale well with lots of different
		// types of RootMotion Sources
		CurveIdx += Montages.Montages.Num();
	}
	return CurveIdx;
}

bool UMockRootMotionSourceDataAsset::IsValidSourceID(int32 RootMotionSourceID) const
{
	return Montages.Montages.IsValidIndex(RootMotionSourceID);
}

FTransform UMockRootMotionSourceDataAsset::StepRootMotion(const FNetSimTimeStep& TimeStep, const FMockRootMotionSyncState* In, FMockRootMotionSyncState* Out, const FMockRootMotionAuxState* Aux)
{
	npCheckSlow(In);
	npCheckSlow(In->RootMotionSourceID != INDEX_NONE);

	// Map ID to montage or curves and call the _Imply function.
	if (Montages.Montages.IsValidIndex(In->RootMotionSourceID))
	{
		return StepRootMotion_Montage(Montages.Montages[In->RootMotionSourceID], TimeStep, In, Out, Aux);
	}
	else
	{
		const int32 CurveIdx = In->RootMotionSourceID - Montages.Montages.Num();
		if (Curves.Curves.IsValidIndex(CurveIdx))
		{
			return StepRootMotion_Curve(Curves.Curves[CurveIdx], TimeStep, In, Out, Aux);
		}
		else
		{
			npEnsureMsgf(false, TEXT("Invalid RootMotionSourceID: %d. Not mapped in %s. Skipping Update"), In->RootMotionSourceID, *GetName());
		}
	}

	return FTransform::Identity;
}

FTransform UMockRootMotionSourceDataAsset::StepRootMotion_Curve(UCurveVector* Curve, const FNetSimTimeStep& TimeStep, const FMockRootMotionSyncState* In, FMockRootMotionSyncState* Out, const FMockRootMotionAuxState* Aux)
{
	npCheckSlow(Curve);
	npCheckSlow(In);
	npCheckSlow(Out);
	npCheckSlow(Aux);

	float MinPosition = 0.f;
	float MaxPosition = 0.f;
	Curve->GetTimeRange(MinPosition, MaxPosition);

	const float DeltaSeconds = (float)TimeStep.StepMS / 1000.f;
	const float EndPosition = FMath::Clamp(In->PlayPosition + (DeltaSeconds * In->PlayRate), MinPosition, MaxPosition);
	
	if (EndPosition < MaxPosition)
	{
		Out->PlayPosition = EndPosition;
	}
	else
	{
		// The RootMotion is finished
		// We will want to support looping/clamping options at some level. For now the RootMotion just finishes
		Out->RootMotionSourceID = INDEX_NONE;
	}

	const FVector Start = Curve->GetVectorValue(In->PlayPosition);
	const FVector End = Curve->GetVectorValue(EndPosition);

	FVector DeltaV = (End - Start);
	
	// Allow Aux parameters to scale the translation from the curve
	// Note that just shoving an FVector in here feels too fragile. We want a more formal binding of root motion source -> parameters
	if (const FVector* Scale = Aux->Parameters.GetByType<FVector>())
	{
		DeltaV *= *Scale;
	}
	else
	{
		UE_LOG(LogMockRootMotionSourceDataAsset, Warning, TEXT("Invalid Aux parameters when evaluating curve motion. Size: %d"), Aux->Parameters.Data.Num());
	}
	
	FTransform DeltaTrans = FTransform::Identity;
	DeltaTrans.SetTranslation(DeltaV);

	return DeltaTrans;
}

FTransform UMockRootMotionSourceDataAsset::StepRootMotion_Montage(UAnimMontage* Montage, const FNetSimTimeStep& TimeStep, const FMockRootMotionSyncState* In, FMockRootMotionSyncState* Out, const FMockRootMotionAuxState* Aux)
{
	npCheckSlow(Montage);
	npCheckSlow(In);
	npCheckSlow(Out);
	npCheckSlow(Aux);

	const float MinPosition = 0.f;
	const float MaxPosition = Montage->GetPlayLength();

	const float DeltaSeconds = (float)TimeStep.StepMS / 1000.f;
	const float EndPosition = FMath::Clamp(In->PlayPosition + (DeltaSeconds * In->PlayRate), MinPosition, MaxPosition);

	if (EndPosition < MaxPosition)
	{
		Out->PlayPosition = EndPosition;
	}
	else
	{
		// The RootMotion is finished
		// We will want to support looping/clamping options at some level. For now the RootMotion just finishes
		Out->RootMotionSourceID = INDEX_NONE;
	}

	
	FTransform NewTransform;
	
	// Extract root motion from animation sequence 
	NewTransform = Montage->ExtractRootMotionFromTrackRange(In->PlayPosition, EndPosition);

	//UE_LOG(LogTemp, Warning, TEXT("%.4f: Delta: %s"), EndPosition, *NewTransform.GetTranslation().ToString());
	return NewTransform;
}

void UMockRootMotionSourceDataAsset::FinalizePose(const FMockRootMotionSyncState* Sync, UAnimInstance* AnimInstance)
{
	npCheckSlow(Sync);
	npCheckSlow(AnimInstance);

	// Push the authoratative sync state to the anim instance
	// This is an important part of the system - the sync state can and will change out from underneath you
	// so its important we have some way to tell the animinstance 'this is what you should look like right now'
	if (Montages.Montages.IsValidIndex(Sync->RootMotionSourceID))
	{
		UAnimMontage* Montage = Montages.Montages[Sync->RootMotionSourceID];
		if (!AnimInstance->Montage_IsPlaying(Montage))
		{
			AnimInstance->Montage_Play(Montage);
		}
		
		AnimInstance->Montage_SetPosition(Montage, Sync->PlayPosition);
	}
}