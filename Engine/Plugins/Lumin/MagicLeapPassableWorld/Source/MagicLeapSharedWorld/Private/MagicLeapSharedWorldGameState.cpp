// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapSharedWorldGameState.h"
#include "MagicLeapARPinFunctionLibrary.h"
#include "Net/UnrealNetwork.h"

AMagicLeapSharedWorldGameState::AMagicLeapSharedWorldGameState(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{}

FTransform AMagicLeapSharedWorldGameState::CalculateXRCameraRootTransform_Implementation() const
{
	FTransform Result;
	int32 NumPinsUsed = 0;
	for (int32 i = 0; i < SharedWorldData.PinIDs.Num(); ++i)
	{
		FVector ClientPinPosition_TrackingSpace;
		FRotator ClientPinRotation_TrackingSpace;
		bool bPinFoundInEnvironment = false;

		const bool bPinTransformResult = UMagicLeapARPinFunctionLibrary::GetARPinPositionAndOrientation_TrackingSpace(SharedWorldData.PinIDs[i], ClientPinPosition_TrackingSpace, ClientPinRotation_TrackingSpace, bPinFoundInEnvironment);
		if (bPinTransformResult)
		{
			const FTransform ClientPinTransform_TrackingSpace(ClientPinRotation_TrackingSpace, ClientPinPosition_TrackingSpace);

			// inv(inv(AlignmentTransform) * ClientPinTransform)
			Result += (ClientPinTransform_TrackingSpace.Inverse() * AlignmentTransforms.AlignmentTransforms[i]);
			++NumPinsUsed;
		}
	}

	if (NumPinsUsed > 0)
	{
		Result.SetLocation(Result.GetLocation() / NumPinsUsed);

		const FRotator ResultRotator = Result.GetRotation().Rotator();
		// Use only Yaw to ensure up vector is aligned with gravity.
		Result.SetRotation(FRotator(0.0f, ResultRotator.Yaw / NumPinsUsed, 0.0f).Quaternion().GetNormalized());
	}

	return Result;
}

void AMagicLeapSharedWorldGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMagicLeapSharedWorldGameState, SharedWorldData);
	DOREPLIFETIME(AMagicLeapSharedWorldGameState, AlignmentTransforms);
}

void AMagicLeapSharedWorldGameState::OnReplicate_SharedWorldData()
{
	UE_LOG(LogMagicLeapSharedWorld, Display, TEXT("AMagicLeapSharedWorldGameState received new SharedWorldData"));
	for (const FGuid& Pin : SharedWorldData.PinIDs)
	{
		UE_LOG(LogMagicLeapSharedWorld, Display, TEXT("%s"), *Pin.ToString());
	}

	if (OnSharedWorldDataUpdated.IsBound())
	{
		OnSharedWorldDataUpdated.Broadcast();
	}
}

void AMagicLeapSharedWorldGameState::OnReplicate_AlignmentTransforms()
{
	UE_LOG(LogMagicLeapSharedWorld, Display, TEXT("AMagicLeapSharedWorldGameState received new AlignmentTransforms"));
	for (const FTransform& Transform : AlignmentTransforms.AlignmentTransforms)
	{
		UE_LOG(LogMagicLeapSharedWorld, Display, TEXT("%s"), *Transform.ToString());
	}

	if (OnAlignmentTransformsUpdated.IsBound())
	{
		OnAlignmentTransformsUpdated.Broadcast();
	}
}
