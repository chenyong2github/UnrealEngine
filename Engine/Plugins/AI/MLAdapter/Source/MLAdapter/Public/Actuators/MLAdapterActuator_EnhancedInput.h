// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "InputCoreTypes.h"
#include "Actuators/MLAdapterActuator.h"
#include "InputAction.h"
#include "MLAdapterActuator_EnhancedInput.generated.h"


UCLASS()
class MLADAPTER_API UMLAdapterActuator_EnhancedInput : public UMLAdapterActuator
{
	GENERATED_BODY()

public:
	virtual void Configure(const TMap<FName, FString>& Params) override;
	virtual TSharedPtr<FMLAdapter::FSpace> ConstructSpaceDef() const override;

	virtual void Act(const float DeltaTime) override;

	virtual void DigestInputData(FMLAdapterMemoryReader& ValueStream) override;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = MLAdapter)
	TArray<TObjectPtr<UInputAction>> TrackedActions;

protected:
	TArray<float> InputData;
};