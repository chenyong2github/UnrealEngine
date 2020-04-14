// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sensors/4MLSensor.h"
#include "4MLTypes.h"
#include "4MLSensor_Attribute.generated.h"


class UAttributeSet;
struct FGameplayAttributeData;


UCLASS(Blueprintable)
class UE4ML_API U4MLSensor_Attribute : public U4MLSensor
{
	GENERATED_BODY()
public:
	U4MLSensor_Attribute(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual bool ConfigureForAgent(U4MLAgent& Agent) override;
	virtual void Configure(const TMap<FName, FString>& Params) override;

protected:
	virtual TSharedPtr<F4ML::FSpace> ConstructSpaceDef() const override;
	virtual void UpdateSpaceDef() override;
	virtual void OnAvatarSet(AActor* Avatar) override;
	virtual void SenseImpl(const float DeltaTime) override;
	virtual void GetObservations(F4MLMemoryWriter& Ar) override;

	void SetAttributes(TArray<FString>& InAttributeNames);
	void BindAttributes(AActor& Actor);
protected:
	UPROPERTY()
	UAttributeSet* AttributeSet;

	TArray<FName> AttributeNames;
	// valid only as long as AttributeSet != nullptr
	TArray<FGameplayAttributeData*> Attributes;

	TArray<float> Values;
};
