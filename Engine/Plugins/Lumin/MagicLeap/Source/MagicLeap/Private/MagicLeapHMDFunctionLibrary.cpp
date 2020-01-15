// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapHMDFunctionLibrary.h"
#include "MagicLeapHMD.h"
#include "Engine/Engine.h"
#include "Lumin/CAPIShims/LuminAPILifecycle.h"

static const FName MLDeviceName(TEXT("MagicLeap"));

// Internal helper.
static FMagicLeapHMD* GetMagicLeapHMD()
{
	IXRTrackingSystem* const XR = GEngine->XRSystem.Get();
	if (XR && (XR->GetSystemName() == MLDeviceName))
	{
		IHeadMountedDisplay* const HMD = XR->GetHMDDevice();
		if (HMD)
		{
			// we know it's a FMagicLeapHMD by the name match above
			return static_cast<FMagicLeapHMD*>(HMD);
		}
	}

	return nullptr;
}

void UMagicLeapHMDFunctionLibrary::SetBasePosition(const FVector& InBasePosition)
{ /* deprecated */ }

void UMagicLeapHMDFunctionLibrary::SetBaseOrientation(const FQuat& InBaseOrientation)
{ /* deprecated */ }

void UMagicLeapHMDFunctionLibrary::SetBaseRotation(const FRotator& InBaseRotation)
{ /* deprecated */ }

void UMagicLeapHMDFunctionLibrary::SetFocusActor(const AActor* InFocusActor, bool bSetStabilizationActor)
{
	FMagicLeapHMD* const HMD = GetMagicLeapHMD();
	if (HMD)
	{
		HMD->SetFocusActor(InFocusActor, bSetStabilizationActor);
	}
}

void UMagicLeapHMDFunctionLibrary::SetStabilizationDepthActor(const AActor* InStabilizationDepthActor, bool bSetFocusActor)
{
	FMagicLeapHMD* const HMD = GetMagicLeapHMD();
	if (HMD)
	{
		HMD->SetStabilizationDepthActor(InStabilizationDepthActor, bSetFocusActor);
	}
}

int32 UMagicLeapHMDFunctionLibrary::GetMLSDKVersionMajor()
{
#if WITH_MLSDK
	return MLSDK_VERSION_MAJOR;
#else
	return 0;
#endif //WITH_MLSDK
}

int32 UMagicLeapHMDFunctionLibrary::GetMLSDKVersionMinor()
{
#if WITH_MLSDK
	return MLSDK_VERSION_MINOR;
#else
	return 0;
#endif //WITH_MLSDK
}

int32 UMagicLeapHMDFunctionLibrary::GetMLSDKVersionRevision()
{
#if WITH_MLSDK
	return MLSDK_VERSION_REVISION;
#else
	return 0;
#endif //WITH_MLSDK
}

FString UMagicLeapHMDFunctionLibrary::GetMLSDKVersion()
{
#if WITH_MLSDK
	return TEXT(MLSDK_VERSION_NAME);
#else
	return FString();
#endif //WITH_MLSDK
}

bool UMagicLeapHMDFunctionLibrary::IsRunningOnMagicLeapHMD()
{
#if PLATFORM_LUMIN
	return true;
#else
	FMagicLeapHMD* const HMD = GetMagicLeapHMD();
	if (HMD)
	{
		//`IsStereoEnabled` is a strict form of `IsHMDEnabled`. 
		//	Non-target platforms do not request stereo. 
		return HMD->IsStereoEnabled();
	}

	return false;
#endif
}

bool UMagicLeapHMDFunctionLibrary::GetHeadTrackingState(FMagicLeapHeadTrackingState& State)
{
	FMagicLeapHMD* const HMD = GetMagicLeapHMD();
	if (HMD)
	{
		return HMD->GetHeadTrackingState(State);
	}

	return false;
}

bool UMagicLeapHMDFunctionLibrary::GetHeadTrackingMapEvents(TSet<EMagicLeapHeadTrackingMapEvent>& MapEvents)
{
	FMagicLeapHMD* const HMD = GetMagicLeapHMD();
	if (HMD)
	{
		return HMD->GetHeadTrackingMapEvents(MapEvents);
	}

	return false;
}

bool UMagicLeapHMDFunctionLibrary::SetAppReady()
{
#if WITH_MLSDK
	return MLLifecycleSetReadyIndication() == MLResult_Ok;
#else
	return true;
#endif // WITH_MLSDK
}
