// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OSCServer.h"
#include "OSCClient.h"
#include "OSCManager.generated.h"

UCLASS()
class OSC_API UOSCManager : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	//////////////////////////////////////////////////////////////////////////
	// Audio|OSC functions
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static class UOSCServer* CreateOSCServer(FString ReceiveAddress, int32 Port);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static class UOSCClient* CreateOSCClient(FString SendAddress, int32 Port);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void CreateOSCMessage(UPARAM(ref) FOSCMessage& Message);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void CreateOSCBundle(UPARAM(ref) FOSCBundle& Bundle);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void AddMessageToBundle(const FOSCMessage& Message, UPARAM(ref) FOSCBundle& Bundle);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void AddBundleToBundle(const FOSCBundle& inBundle, UPARAM(ref) FOSCBundle& outBundle);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetMessagesFromBundle(const FOSCBundle& Bundle, TArray<FOSCMessage>& Messages);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void ClearMessage(UPARAM(ref) FOSCMessage& Message);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void ClearBundle(UPARAM(ref) FOSCBundle& Bundle);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void SetAddress(UPARAM(ref) FOSCMessage& Message, const FString& Address);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void AddFloat(UPARAM(ref) FOSCMessage& Message, float Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void AddInt32(UPARAM(ref) FOSCMessage& Message, int32 Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void AddInt64(UPARAM(ref) FOSCMessage& Message, int64 Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void AddString(UPARAM(ref) FOSCMessage& Message, FString Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void AddBlob(UPARAM(ref) FOSCMessage& Message, TArray<uint8>& Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void AddBool(UPARAM(ref) FOSCMessage& Message, bool Value);

	//////////////////////////////////////////////////////////////////////////
	// Audio|OSC functions
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetFloat(const FOSCMessage& Message, const int index, float& Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetAllFloat(const FOSCMessage& Message, TArray<float>& Values);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetInt32(const FOSCMessage& Message, const int index, int32& Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetAllInt32(const FOSCMessage& Message, TArray<int32>& Values);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetInt64(const FOSCMessage& Message, const int index, int64& Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetAllInt64(const FOSCMessage& Message, TArray<int64>& Values);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetString(const FOSCMessage& Message, const int index, FString& Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetAllString(const FOSCMessage& Message, TArray<FString>& Values);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetBool(const FOSCMessage& Message, const int index, bool& Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetAllBool(const FOSCMessage& Message, TArray<bool>& Values);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetBlob(const FOSCMessage& Message, const int index, TArray<uint8>& Value);
};
