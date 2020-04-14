// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sensors/4MLSensor.h"
#include "InputCoreTypes.h"
#include "KeyState.h"
#include <vector>
#include "4MLSensor_Input.generated.h"


struct FInputKeyEventArgs; 
class FViewport;
class UGameViewportClient;


/** Note that this sensor doesn't buffer input state between GetObservations call
 *	@todo a child class could easily do that by overriding OnInputKey/OnInputAxis and 
 *		GetObservations
 */
UCLASS(Blueprintable)
class UE4ML_API U4MLSensor_Input : public U4MLSensor
{
	GENERATED_BODY()
public:
	U4MLSensor_Input(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	
protected:
	virtual void OnInputAxis(FViewport* InViewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime, int32 NumSamples, bool bGamepad);
	virtual void OnInputKey(const FInputKeyEventArgs& EventArgs);

	virtual void Configure(const TMap<FName, FString>& Params) override;
	virtual void GetObservations(F4MLMemoryWriter& Ar) override;
	virtual void UpdateSpaceDef() override; 
	virtual TSharedPtr<F4ML::FSpace> ConstructSpaceDef() const override;
	virtual void OnAvatarSet(AActor* Avatar);

	UPROPERTY(EditDefaultsOnly, Category = UE4ML)
	UGameViewportClient* GameViewport;

	UPROPERTY(EditDefaultsOnly, Category = UE4ML)
	uint32 bRecordKeyRelease : 1;
	
	TArray<float> InputState;

	// stores (FKey, ActionName pairs). The order is important since FKeyToInterfaceKeyMap refers to it. 
	TArray<TTuple<FKey, FName>> InterfaceKeys;

	TMap<FKey, int32> FKeyToInterfaceKeyMap;
};
