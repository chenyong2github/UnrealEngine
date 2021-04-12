// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "InputCoreTypes.h"
#include "Actuators/MLAdapterActuator.h"
#include "MLAdapterActuator_Camera.generated.h"


UCLASS()
class UMLAdapterActuator_Camera : public UMLAdapterActuator
{
	GENERATED_BODY()
public:
	UMLAdapterActuator_Camera(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void Configure(const TMap<FName, FString>& Params) override;
	virtual TSharedPtr<FMLAdapter::FSpace> ConstructSpaceDef() const override;

	/** Presses the keys stored in "KeysToPress" */
	virtual void Act(const float DeltaTime) override;

	virtual void DigestInputData(FMLAdapterMemoryReader& ValueStream) override;

protected:

#if WITH_GAMEPLAY_DEBUGGER
	virtual void DescribeSelfToGameplayDebugger(class FGameplayDebuggerCategory& DebuggerCategory) const override;
#endif // WITH_GAMEPLAY_DEBUGGER

	FRotator HeadingRotator;
	FVector HeadingVector;

	uint32 bConsumeData : 1;
	uint32 bVectorMode : 1;
	uint32 bDeltaMode : 1;
};