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
	// Creates an OSC Server.  If left empty (or '0'),
	// attempts to use LocalHost IP address. If StartListening set,
	// immediately begins listening on creation.
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static class UOSCServer* CreateOSCServer(FString ReceiveIPAddress, int32 Port, bool bMulticastLoopback, bool bStartListening);

	// Creates an OSC Client.  If left empty (or '0'), attempts to use
	// attempts to use LocalHost IP address.
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static class UOSCClient* CreateOSCClient(FString SendIPAddress, int32 Port);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Bundle") FOSCBundle& AddMessageToBundle(const FOSCMessage& Message, UPARAM(ref) FOSCBundle& Bundle);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "OutBundle") FOSCBundle& AddBundleToBundle(const FOSCBundle& InBundle, UPARAM(ref) FOSCBundle& OutBundle);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetMessagesFromBundle(const FOSCBundle& Bundle, TArray<FOSCMessage>& Messages);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void ClearMessage(UPARAM(ref) FOSCMessage& Message);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void ClearBundle(UPARAM(ref) FOSCBundle& Bundle);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Message") FOSCMessage& AddFloat(UPARAM(ref) FOSCMessage& Message, float Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Message") FOSCMessage& AddInt32(UPARAM(ref) FOSCMessage& Message, int32 Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Message") FOSCMessage& AddInt64(UPARAM(ref) FOSCMessage& Message, int64 Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Message") FOSCMessage& AddString(UPARAM(ref) FOSCMessage& Message, FString Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Message") FOSCMessage& AddBlob(UPARAM(ref) FOSCMessage& Message, TArray<uint8>& Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Message") FOSCMessage& AddBool(UPARAM(ref) FOSCMessage& Message, bool Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetFloat(const FOSCMessage& Message, const int32 Index, float& Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetAllFloats(const FOSCMessage& Message, TArray<float>& Values);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetInt32(const FOSCMessage& Message, const int32 Index, int32& Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetAllInt32s(const FOSCMessage& Message, TArray<int32>& Values);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetInt64(const FOSCMessage& Message, const int32 Index, int64& Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetAllInt64s(const FOSCMessage& Message, TArray<int64>& Values);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetString(const FOSCMessage& Message, const int32 Index, FString& Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetAllStrings(const FOSCMessage& Message, TArray<FString>& Values);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetBool(const FOSCMessage& Message, const int32 Index, bool& Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetAllBools(const FOSCMessage& Message, TArray<bool>& Values);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetBlob(const FOSCMessage& Message, const int32 Index, TArray<uint8>& Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static bool OSCAddressIsValidPath(const FOSCAddress& Address);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static bool OSCAddressIsValidPattern(const FOSCAddress& Address);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static FOSCAddress StringToOSCAddress(const FString& String);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Address") FOSCAddress& OSCAddressPushContainer(UPARAM(ref) FOSCAddress& Address, const FString& Container);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Address") FString OSCAddressPopContainer(UPARAM(ref) FOSCAddress& Address);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static FOSCAddress GetOSCMessageAddress(const FOSCMessage& Message);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Message") FOSCMessage& SetOSCMessageAddress(UPARAM(ref) FOSCMessage& Message, const FOSCAddress& Address);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static FString GetOSCAddressContainer(const FOSCAddress& Address, const int32 Index);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static TArray<FString> GetOSCAddressContainers(const FOSCAddress& Address);

	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static FString GetOSCAddressMethodName(const FOSCAddress& Address);
};
