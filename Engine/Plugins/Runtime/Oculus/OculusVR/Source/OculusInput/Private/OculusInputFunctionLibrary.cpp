// Copyright Epic Games, Inc. All Rights Reserved.

#include "OculusInputFunctionLibrary.h"
#include "OculusHandTracking.h"
#include "Logging/MessageLog.h"

//-------------------------------------------------------------------------------------------------
// UOculusHandTrackingFunctionLibrary
//-------------------------------------------------------------------------------------------------
UOculusInputFunctionLibrary::UOculusInputFunctionLibrary(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UOculusInputFunctionLibrary::GetHandSkeletalMesh(USkeletalMesh* HandSkeletalMesh, EOculusHandType SkeletonType, EOculusHandType MeshType, float WorldToMeters)
{
	return OculusInput::FOculusHandTracking::GetHandSkeletalMesh(HandSkeletalMesh, SkeletonType, MeshType, WorldToMeters);
}

TArray<FOculusCapsuleCollider> UOculusInputFunctionLibrary::InitializeHandPhysics(EOculusHandType SkeletonType, USkinnedMeshComponent* HandComponent, const float WorldToMeters)
{
	return OculusInput::FOculusHandTracking::InitializeHandPhysics(SkeletonType, HandComponent, WorldToMeters);
}

FQuat UOculusInputFunctionLibrary::GetBoneRotation(const EOculusHandType DeviceHand, const EBone BoneId, const int32 ControllerIndex)
{
	return OculusInput::FOculusHandTracking::GetBoneRotation(ControllerIndex, DeviceHand, BoneId);
}

ETrackingConfidence UOculusInputFunctionLibrary::GetTrackingConfidence(const EOculusHandType DeviceHand, const int32 ControllerIndex)
{
	return OculusInput::FOculusHandTracking::GetTrackingConfidence(ControllerIndex, DeviceHand);
}

FTransform UOculusInputFunctionLibrary::GetPointerPose(const EOculusHandType DeviceHand, const int32 ControllerIndex)
{
	return OculusInput::FOculusHandTracking::GetPointerPose(ControllerIndex, DeviceHand);
}

bool UOculusInputFunctionLibrary::IsPointerPoseValid(const EOculusHandType DeviceHand, const int32 ControllerIndex)
{
	return OculusInput::FOculusHandTracking::IsPointerPoseValid(ControllerIndex, DeviceHand);
}

float UOculusInputFunctionLibrary::GetHandScale(const EOculusHandType DeviceHand, const int32 ControllerIndex)
{
	return OculusInput::FOculusHandTracking::GetHandScale(ControllerIndex, DeviceHand);
}

EOculusHandType UOculusInputFunctionLibrary::GetDominantHand(const int32 ControllerIndex)
{
	EOculusHandType DominantHand = EOculusHandType::None;
	if (OculusInput::FOculusHandTracking::IsHandDominant(ControllerIndex, EOculusHandType::HandLeft))
	{
		DominantHand = EOculusHandType::HandLeft;
	}
	else if (OculusInput::FOculusHandTracking::IsHandDominant(ControllerIndex, EOculusHandType::HandRight))
	{
		DominantHand = EOculusHandType::HandRight;
	}
	return DominantHand;
}

bool UOculusInputFunctionLibrary::IsHandTrackingEnabled()
{
	return OculusInput::FOculusHandTracking::IsHandTrackingEnabled();
}

FString UOculusInputFunctionLibrary::GetBoneName(EBone BoneId)
{
	uint32 ovrBoneId = OculusInput::FOculusHandTracking::ToOvrBone(BoneId);
	return OculusInput::FOculusHandTracking::GetBoneName(ovrBoneId);
}

