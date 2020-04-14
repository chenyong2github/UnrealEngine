// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sensors/4MLSensor_Input.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "4MLInputHelper.h"
#include "4MLSpace.h"
#include "Debug/DebugHelpers.h"


U4MLSensor_Input::U4MLSensor_Input(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bRecordKeyRelease = false;
}

void U4MLSensor_Input::Configure(const TMap<FName, FString>& Params)
{
	const FName NAME_RecordRelease = TEXT("record_release");

	Super::Configure(Params);

	for (auto KeyValue : Params)
	{
		if (KeyValue.Key == NAME_RecordRelease)
		{
			bool bValue = bRecordKeyRelease;
			LexFromString(bValue, *KeyValue.Value);
			bRecordKeyRelease = bValue;
		}
	}

	F4MLInputHelper::CreateInputMap(InterfaceKeys, FKeyToInterfaceKeyMap);

	UpdateSpaceDef();
}

void U4MLSensor_Input::GetObservations(F4MLMemoryWriter& Ar)
{
	FScopeLock Lock(&ObservationCS);
	
	F4ML::FSpaceSerializeGuard SerializeGuard(SpaceDef, Ar);
	Ar.Serialize(InputState.GetData(), InputState.Num() * sizeof(float));

	InputState.SetNumZeroed(SpaceDef->Num());
}

TSharedPtr<F4ML::FSpace> U4MLSensor_Input::ConstructSpaceDef() const 
{
	F4ML::FSpace* Result = nullptr;

	const bool bHasButtons = (InterfaceKeys.Num() > 0);
	// mz@todo 
	const bool bHasAxis = false;// InterfaceAxis.Num();
	if (bHasButtons != bHasAxis)
	{
		if (bHasButtons)
		{
			Result = new F4ML::FSpace_MultiDiscrete(InterfaceKeys.Num());
		}
		else // bHasAxis
		{
			NOT_IMPLEMENTED();
			Result = new F4ML::FSpace_Dummy();
		}
	}
	else
	{
		Result = new F4ML::FSpace_Tuple({
			MakeShareable(new F4ML::FSpace_MultiDiscrete(InterfaceKeys.Num()))
			, MakeShareable(new F4ML::FSpace_Box(/*InterfaceAxis.Num()*/{ 1 }))
			});
	}

	return MakeShareable(Result);
}

void U4MLSensor_Input::UpdateSpaceDef()
{
	Super::UpdateSpaceDef();
	InputState.SetNumZeroed(SpaceDef->Num());
}

void U4MLSensor_Input::OnAvatarSet(AActor* Avatar)
{
	if (Avatar == nullptr)
	{
		// clean up and exit
		if (GameViewport)
		{
			GameViewport->OnInputAxis().RemoveAll(this);
			GameViewport->OnInputKey().RemoveAll(this);
			GameViewport = nullptr;
		}

		return;
	}

	APlayerController* PC = Cast<APlayerController>(Avatar);
	if (PC == nullptr)
	{
		return;
	}

	UWorld* World = Avatar->GetWorld();
	if (World)
	{
		GameViewport = World->GetGameViewport();
		if (GameViewport)
		{
			GameViewport->OnInputAxis().AddUObject(this, &U4MLSensor_Input::OnInputAxis);
			GameViewport->OnInputKey().AddUObject(this, &U4MLSensor_Input::OnInputKey);
		}
	}

	Super::OnAvatarSet(Avatar);
}

void U4MLSensor_Input::OnInputAxis(FViewport* InViewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime, int32 NumSamples, bool bGamepad)
{

}

void U4MLSensor_Input::OnInputKey(const FInputKeyEventArgs& EventArgs)
{
	const FName KeyName = EventArgs.Key.GetFName();
	int32* InterfaceKey = FKeyToInterfaceKeyMap.Find(KeyName);
	if (InterfaceKey && ((EventArgs.Event != IE_Released)|| bRecordKeyRelease))
	{
		FScopeLock Lock(&ObservationCS);
		ensure(false && "save in FSpace format");
		InputState[*InterfaceKey] = 1;
	}
}
