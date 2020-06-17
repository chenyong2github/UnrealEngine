// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "InputCoreTypes.h"
#include "Actuators/4MLActuator.h"
#include "4MLActuator_InputKey.generated.h"


UCLASS()
class U4MLActuator_InputKey : public U4MLActuator
{
	GENERATED_BODY()
public:
	U4MLActuator_InputKey(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void Configure(const TMap<FName, FString>& Params) override;
	virtual TSharedPtr<F4ML::FSpace> ConstructSpaceDef() const override;

	/** Presses the keys stored in "KeysToPress" */
	virtual void Act(const float DeltaTime) override;

	virtual void DigestInputData(F4MLMemoryReader& ValueStream) override;

protected:
	TArray<TTuple<FKey, FName>> RegisteredKeys;
	TArray<int32> KeysToPress;

	TBitArray<> PressedKeys;

	TArray<float> InputData;

	/** temporary solution. If true then the incoming actions are expected to be 
	 *	MultiBinary, if false (default) the actions will be treated as Discrete */
	bool bIsMultiBinary;
};