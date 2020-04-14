// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actuators/4MLActuator_InputKey.h"
#include "4MLTypes.h"
#include "4MLSpace.h"
#include "4MLInputHelper.h"
#include "Agents/4MLAgent.h"
#include "GameFramework/PlayerController.h"
#include "Debug/DebugHelpers.h"


U4MLActuator_InputKey::U4MLActuator_InputKey(const FObjectInitializer& ObjectInitializer)
{
	bIsMultiBinary = false;
}

void U4MLActuator_InputKey::Configure(const TMap<FName, FString>& Params) 
{
	Super::Configure(Params);

	TMap<FKey, int32> TmpKeyMap;
	F4MLInputHelper::CreateInputMap(RegisteredKeys, TmpKeyMap);

	PressedKeys.Init(false, RegisteredKeys.Num());

	UpdateSpaceDef();
}

TSharedPtr<F4ML::FSpace> U4MLActuator_InputKey::ConstructSpaceDef() const
{
	F4ML::FSpace* Result = nullptr;

	if (bIsMultiBinary)
	{
		NOT_IMPLEMENTED();
		Result = new F4ML::FSpace_Dummy();
	}
	else
	{
		Result = new F4ML::FSpace_Discrete(RegisteredKeys.Num());
	}
	
	return MakeShareable(Result);
}

void U4MLActuator_InputKey::Act(const float DeltaTime)
{
	APlayerController* PC = Cast<APlayerController>(GetAgent().GetAvatar());
	if (!ensure(PC))
	{
		return;
	}

	FScopeLock Lock(&ActionCS);

	TBitArray<> OldPressedKeys = PressedKeys;
	PressedKeys.Init(false, RegisteredKeys.Num());

	for (int Index = 0; Index < InputData.Num(); ++Index)
	{
		int KeyID = Index % RegisteredKeys.Num();
		if (InputData[Index] != 0.f)
		{
			PressedKeys[KeyID] = true;
			if (OldPressedKeys[KeyID] == false)
			{
				// press only if not pressed previously
				// @todo this should probably be optional
				PC->InputKey(RegisteredKeys[KeyID].Get<0>(), IE_Pressed, 1.0f, false);
			}
		}
	}
	InputData.Empty(InputData.Num());

	for (int Index = 0; Index < RegisteredKeys.Num(); ++Index)
	{
		if (OldPressedKeys[Index] && !PressedKeys[Index])
		{
			PC->InputKey(RegisteredKeys[Index].Get<0>(), IE_Released, 1.0f, false);
		}
	}
}

void U4MLActuator_InputKey::DigestInputData(F4MLMemoryReader& ValueStream)
{
	FScopeLock Lock(&ActionCS);

	const int32 OldSize = InputData.Num();
	InputData.AddUninitialized(RegisteredKeys.Num());
	// offsetting the serialization since there might be unprocessed data in InputData
	ValueStream.Serialize(InputData.GetData() + OldSize, RegisteredKeys.Num() * sizeof(float));
}