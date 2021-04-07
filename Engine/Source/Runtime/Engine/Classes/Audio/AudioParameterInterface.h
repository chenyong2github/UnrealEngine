// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "AudioParameterInterface.generated.h"

UINTERFACE(MinimalApi, BlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class UAudioParameterInterface : public UInterface
{
	GENERATED_UINTERFACE_BODY()
};

class IAudioParameterInterface : public IInterface
{
	GENERATED_IINTERFACE_BODY()

public:
	virtual void Shutdown() = 0;

	// Triggers a named trigger 
	UFUNCTION(BlueprintCallable, Category = "Audio|Parameter|Triggers")
	virtual void Trigger(FName Name) = 0;

	// Sets a named Boolean 
	UFUNCTION(BlueprintCallable, Category = "Audio|Parameter|Primitive")
	virtual void SetBool(FName Name, bool Value) = 0;
	
	// Sets a named Boolean Array 
	UFUNCTION(BlueprintCallable, Category = "Audio|Parameter|Array")
	virtual void SetBoolArray(FName Name,const TArray<bool>& Value) = 0;

	// Sets a named Int32 
	UFUNCTION(BlueprintCallable, Category = "Audio|Parameter|Primitive")
	virtual void SetInt(FName Name, int32 Value) = 0;

	// Sets a named Int32 Array
	UFUNCTION(BlueprintCallable, Category = "Audio|Parameter|Array")
	virtual void SetIntArray(FName Name, const TArray<int32>& Value) = 0;

	// Sets a named Float
	UFUNCTION(BlueprintCallable, Category = "Audio|Parameter|Primitive")
	virtual void SetFloat(FName Name, float Value) = 0;
	
	// Sets a named Float Array
	UFUNCTION(BlueprintCallable, Category = "Audio|Parameter|Array")
	virtual void SetFloatArray(FName Name, const TArray<float>& Value) = 0;
	
	// Sets a named String
	UFUNCTION(BlueprintCallable, Category = "Audio|Parameter|Primitive")
	virtual void SetString(FName Name, const FString& Value) = 0;

	// Sets a named String Array
	UFUNCTION(BlueprintCallable, Category = "Audio|Parameter|Array")
	virtual void SetStringArray(FName Name, const TArray<FString>& Value) = 0;
	
	// Sets a named UObject 
	UFUNCTION(BlueprintCallable, Category = "Audio|Parameter|Primitive")
	virtual void SetObject(FName Name, UObject* Value) = 0;

	// Sets a named UObject Array
	UFUNCTION(BlueprintCallable, Category = "Audio|Parameter|Array")
	virtual void SetObjectArray(FName Name, const TArray<UObject*>& Value) = 0;
};