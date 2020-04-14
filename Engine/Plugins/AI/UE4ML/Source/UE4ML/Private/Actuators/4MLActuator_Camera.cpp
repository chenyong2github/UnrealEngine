// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actuators/4MLActuator_Camera.h"
#include "4MLTypes.h"
#include "4MLSpace.h"
#include "4MLInputHelper.h"
#include "Agents/4MLAgent.h"
#include "GameFramework/PlayerController.h"
#include "Debug/DebugHelpers.h"


namespace F4MLActuatorCameraTweakables
{
	int32 bSkipActing = 0;
}

namespace
{
	FAutoConsoleVariableRef CVar_SkipActing(TEXT("ue4ml.actuator.camera.skip_acting"), F4MLActuatorCameraTweakables::bSkipActing, TEXT("Whether the actuator should stop affecting the camera"), ECVF_Default);
}

U4MLActuator_Camera::U4MLActuator_Camera(const FObjectInitializer& ObjectInitializer)
{
	bVectorMode = true;
	bConsumeData = true;
	bDeltaMode = true;
}

void U4MLActuator_Camera::Configure(const TMap<FName, FString>& Params)
{
	const FName NAME_Mode = TEXT("mode");

	Super::Configure(Params);

	for (auto KeyValue : Params)
	{
		if (KeyValue.Key == NAME_Mode)
		{
			bVectorMode = (KeyValue.Value.Find(TEXT("vector")) != INDEX_NONE);
		}
	}

	UpdateSpaceDef();
}

TSharedPtr<F4ML::FSpace> U4MLActuator_Camera::ConstructSpaceDef() const
{
	static const float MaxFPS = 24.f;
	F4ML::FSpace* Result = bVectorMode
		? new F4ML::FSpace_Box({ 3 })
		: new F4ML::FSpace_Box({ 2 }, -360.f * MaxFPS, 360.f * MaxFPS);
	return MakeShareable(Result);
}

void U4MLActuator_Camera::Act(const float DeltaTime)
{
	APlayerController* PC = Cast<APlayerController>(GetAgent().GetAvatar());
	ensure(PC != nullptr || GetAgent().GetAvatar() == nullptr);
	if (PC == nullptr)
	{
		return;
	}

	FRotator Rotation = FRotator::ZeroRotator;
	{
		FScopeLock Lock(&ActionCS);
		Rotation = HeadingRotator;
		if (bVectorMode)
		{
			Rotation = HeadingVector.Rotation();
		}
		if (bConsumeData)
		{
			HeadingRotator = FRotator::ZeroRotator;
			HeadingVector = FVector::ForwardVector;
		}
	}
	
	if (!F4MLActuatorCameraTweakables::bSkipActing)
	{
		PC->AddPitchInput(Rotation.Pitch * DeltaTime);
		PC->AddYawInput(Rotation.Yaw * DeltaTime);
	}
}

void U4MLActuator_Camera::DigestInputData(F4MLMemoryReader& ValueStream)
{
	FScopeLock Lock(&ActionCS);
	if (bVectorMode)
	{
		ValueStream << HeadingVector;
	}
	else
	{
		ValueStream << HeadingRotator.Pitch << HeadingRotator.Yaw;
	}
}

#if WITH_GAMEPLAY_DEBUGGER
#include "GameplayDebuggerCategory.h"
void U4MLActuator_Camera::DescribeSelfToGameplayDebugger(FGameplayDebuggerCategory& DebuggerCategory) const
{
	FRotator Rotation = HeadingRotator;
	if (bVectorMode)
	{
		Rotation = HeadingVector.Rotation();
	}

	DebugRuntimeString = FString::Printf(TEXT("[%.2f, %2.f]"), Rotation.Pitch, Rotation.Yaw);
	Super::DescribeSelfToGameplayDebugger(DebuggerCategory);
}
#endif // WITH_GAMEPLAY_DEBUGGER