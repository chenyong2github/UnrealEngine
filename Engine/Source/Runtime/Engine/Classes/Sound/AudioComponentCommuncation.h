// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioCommunicationInterface.h"
#include "UObject/Object.h"

#include "AudioComponentCommuncation.generated.h"

UCLASS()
class ENGINE_API UAudioComponentCommunication	: public UObject
												, public IAudioCommunicationInterface
{
	GENERATED_BODY()

protected:
	// IAudioCommunicationInterface
	void Trigger(FName InName) override;
	void SetBool(FName InName, bool InValue) override;
	void SetBoolArray(FName InName, const TArray<bool>& InValue) override;
	void SetInt(FName InName, int32 InValue) override;
	void SetIntArray(FName InName, const TArray<int32>& InValue) override;
	void SetFloat(FName InName, float InValue) override;
	void SetFloatArray(FName InName, const TArray<float>& InValue) override;
	void SetString(FName InName, const FString& InValue) override;
	void SetStringArray(FName InName, const TArray<FString>& InValue) override;
	void SetObject(FName InName, UObject* InValue) override;
	void SetObjectArray(FName InName, const TArray<UObject*>& InValue) override;

	template<typename T>
	void SetValue(FName InName, T&& InX);
};
