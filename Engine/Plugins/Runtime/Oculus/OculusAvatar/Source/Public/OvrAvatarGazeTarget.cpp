// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "OvrAvatarGazeTarget.h"
#include "HAL/IConsoleManager.h"
#include "OvrAvatarHelpers.h"
#include "OvrAvatarManager.h"
#include "GameFramework/Actor.h"

static int32 GOvrAvatarGazeTargetDebugDraw = 0;
static FAutoConsoleVariableRef CVarOvrAvatarGazeTargetDebugDraw(
	TEXT("OculusAvatar.gazeTarget.draw"),
	GOvrAvatarGazeTargetDebugDraw,
	TEXT("Debug Draw Targets"),
	ECVF_Default
);

static float GOvrAvatarGazeTargetDebugScale = 100.f;
static FAutoConsoleVariableRef CVarOvrAvatarGazeTargetDebugScale(
	TEXT("OculusAvatar.gazeTarget.scale"),
	GOvrAvatarGazeTargetDebugScale,
	TEXT("Debug Line Scale"),
	ECVF_Default
);

static const FString sGazeTargetTypeStrings[ovrAvatarGazeTargetType_Count] =
{
	FString("AvatarHead"),
	FString("AvatarHand"),
	FString("Object"),
	FString("ObjectStatic"),
};


static const FString GazeTargetToString(ovrAvatarGazeTargetType targetType)
{
	static const FString sEmptyString = "";
	return targetType < ovrAvatarGazeTargetType_Count ? sGazeTargetTypeStrings[targetType] : sEmptyString;
}

UOvrAvatarGazeTarget::UOvrAvatarGazeTarget()
: GazeTransform(nullptr)
, bShuttingDown(false)
{
	PrimaryComponentTick.bCanEverTick = true;
	UOvrAvatarManager::Get().OnShutdown().AddUObject(this, UOvrAvatarGazeTarget::HandleShutdownEvent);

	bIsEnabled = true;

	NativeTarget.targetCount = 1;
	NativeTarget.targets[0].type = ovrAvatarGazeTargetType_Object;
	NativeTarget.targets[0].id = GetUniqueID();
	OvrAvatarHelpers::OvrAvatarVec3Zero(NativeTarget.targets[0].worldPosition);
}

void UOvrAvatarGazeTarget::HandleShutdownEvent()
{
	bShuttingDown = true;
}

void UOvrAvatarGazeTarget::BeginPlay() 
{ 
	Super::BeginPlay(); 
	if (bShuttingDown)
	{
		return;
	}

	NativeTarget.targets[0].type = ConvertEditorTypeToNativeType();
	ovrAvatar_UpdateGazeTargets(&NativeTarget);

	UE_LOG(LogAvatars, Display, TEXT("UOvrAvatarGazeTarget::BeginPlay - GUID %u, Type: %s "),
		NativeTarget.targets[0].id, *GazeTargetToString(NativeTarget.targets[0].type));
}

void UOvrAvatarGazeTarget::BeginDestroy()
{
	Super::BeginDestroy();	

	EnableGazeTarget(false);
}

void UOvrAvatarGazeTarget::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) 
{ 
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction); 

	if (!bIsEnabled || bShuttingDown)
	{
		return;
	}

	const FTransform& GazeTrans = GetGazeTransform();
	const FVector worldPosition = GazeTrans.GetLocation();
	OvrAvatarHelpers::FVectorToOvrAvatarVector3f(worldPosition, NativeTarget.targets[0].worldPosition);
	ovrAvatar_UpdateGazeTargets(&NativeTarget);

	if (GOvrAvatarGazeTargetDebugDraw)
	{
		DrawDebugCoordinateSystem(GetWorld(), worldPosition, FRotator(GazeTrans.GetRotation()), GOvrAvatarGazeTargetDebugScale);
	}
}

const FTransform& UOvrAvatarGazeTarget::GetGazeTransform() const
{
	if (TargetType == OculusAvatarGazeTargetType::AvatarHead)
	{
		return AvatarHeadTransform;
	}
	else if (GazeTransform.IsValid())
	{
		return  GazeTransform.Get()->GetComponentTransform();
	}
	else if (GetOwner()->GetRootComponent())
	{
		return GetOwner()->GetRootComponent()->GetComponentTransform();
	}

	return FTransform::Identity;
}

void UOvrAvatarGazeTarget::EnableGazeTarget(bool DoEnable)
{
	if (!DoEnable && bIsEnabled && !bShuttingDown)
	{
		ovrAvatar_RemoveGazeTargets(1, &NativeTarget.targets[0].id);
	}

	bIsEnabled = DoEnable;
}

ovrAvatarGazeTargetType UOvrAvatarGazeTarget::ConvertEditorTypeToNativeType() const
{
	switch (TargetType)
	{
	case OculusAvatarGazeTargetType::AvatarHand:
		return ovrAvatarGazeTargetType_AvatarHand;
	case OculusAvatarGazeTargetType::AvatarHead:
		return ovrAvatarGazeTargetType_AvatarHead;
	case OculusAvatarGazeTargetType::Object:
		return ovrAvatarGazeTargetType_Object;
	case OculusAvatarGazeTargetType::ObjectStatic:
		return ovrAvatarGazeTargetType_ObjectStatic;
	}

	return ovrAvatarGazeTargetType_Count;
}

