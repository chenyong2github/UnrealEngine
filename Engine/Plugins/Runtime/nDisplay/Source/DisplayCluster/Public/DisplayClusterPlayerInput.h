// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/PlayerInput.h"
#include "DisplayClusterPlayerInput.generated.h"


/**
 * Object within PlayerController that processes player input.
 */
UCLASS(Within=PlayerController, config=Input, transient)
class DISPLAYCLUSTER_API UDisplayClusterPlayerInput : public UPlayerInput
{
	GENERATED_BODY()

public:
	UDisplayClusterPlayerInput();

public:
	/** Process the frame's input events given the current input component stack. */
	virtual void ProcessInputStack(const TArray<UInputComponent*>& InputComponentStack, const float DeltaTime, const bool bGamePaused) override;

protected:
	virtual void ProcessDelegatesFilter(const TArray<UInputComponent*>& InputComponentStack, TArray<FAxisDelegateDetails>& AxisDelegates, TArray<FVectorAxisDelegateDetails>& VectorAxisDelegates, TArray<FDelegateDispatchDetails>& NonAxisDelegates) override;

private:
	void ExportNativeInputData(TMap<FString, FString>& InputData, const TArray<UInputComponent*>& InputComponentStack, TArray<FAxisDelegateDetails>& AxisDelegates, TArray<FVectorAxisDelegateDetails>& VectorAxisDelegates, TArray<FDelegateDispatchDetails>& NonAxisDelegates);
	void ImportNativeInputData(const TMap<FString, FString>& InputData, const TArray<UInputComponent*>& InputComponentStack);

	void CleanDelegateMap(bool IsMaster);
	void BuildDelegatesMap(const TArray<UInputComponent*>& InputComponentStack, bool IsMaster);

	FString SerializeAxisData  (const TArray<UInputComponent*>& InputComponentStack, const TArray<FAxisDelegateDetails>& AxisDelegates);
	FString SerializeVectorAxisData(const TArray<UInputComponent*>& InputComponentStack, const TArray<FVectorAxisDelegateDetails>& VectorAxisDelegates);
	FString SerializeActionData(const TArray<UInputComponent*>& InputComponentStack, const TArray<FDelegateDispatchDetails>& NonAxisDelegates);

	void DeserializeAndProcessAxisInput  (const FString& Data);
	void DeserializeAndProcessVectorAxisInput(const FString& Data);
	void DeserializeAndProcessActionInput(const FString& Data);

private:
	TMap<void*, TMap<void*, FName>> AxisDelegateNames;
	TMap<FName, const FInputAxisUnifiedDelegate*> AxisDelegateInstances;

	TMap<FName, TMap<EInputEvent, const FInputActionUnifiedDelegate*>> ActionDelegateInstances;
	TMap<EInputEvent, const FInputTouchUnifiedDelegate*> TouchDelegateInstances;

	TMap<void*, TMap<void*, FName>> GestureDelegateNames;
	TMap<FName, const FInputGestureUnifiedDelegate*> GestureDelegateInstances;

	TMap<void*, TMap<void*, FName>> VectorAxisDelegateNames;
	TMap<FName, const FInputVectorAxisUnifiedDelegate*> VectorAxisDelegateInstances;
};
