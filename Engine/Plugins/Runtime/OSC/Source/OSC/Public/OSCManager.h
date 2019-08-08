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

	/** Adds provided message packet to bundle. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Bundle") FOSCBundle& AddMessageToBundle(const FOSCMessage& Message, UPARAM(ref) FOSCBundle& Bundle);

	/** Adds bundle packet to bundle. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "OutBundle") FOSCBundle& AddBundleToBundle(const FOSCBundle& InBundle, UPARAM(ref) FOSCBundle& OutBundle);

	/** Fills array of messages found in bundle. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetMessagesFromBundle(const FOSCBundle& Bundle, TArray<FOSCMessage>& Messages);

	/** Clears provided message of all arguments. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Message") FOSCMessage& ClearMessage(UPARAM(ref) FOSCMessage& Message);

	/** Clears provided bundle of all internal messages/bundle packets. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Bundle") FOSCBundle& ClearBundle(UPARAM(ref) FOSCBundle& Bundle);

	/** Adds float value to end of OSCMessage */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Message") FOSCMessage& AddFloat(UPARAM(ref) FOSCMessage& Message, float Value);

	/** Adds Int32 value to end of OSCMessage */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Message") FOSCMessage& AddInt32(UPARAM(ref) FOSCMessage& Message, int32 Value);

	/** Adds Int64 value to end of OSCMessage */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Message") FOSCMessage& AddInt64(UPARAM(ref) FOSCMessage& Message, int64 Value);

	/** Adds string value to end of OSCMessage */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Message") FOSCMessage& AddString(UPARAM(ref) FOSCMessage& Message, FString Value);

	/** Adds blob value to end of OSCMessage */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Message") FOSCMessage& AddBlob(UPARAM(ref) FOSCMessage& Message, TArray<uint8>& Value);

	/** Adds boolean value to end of OSCMessage */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Message") FOSCMessage& AddBool(UPARAM(ref) FOSCMessage& Message, bool Value);

	/** Set Value to float at provided Index in OSCMessage if in bounds and type matches */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetFloat(const FOSCMessage& Message, const int32 Index, float& Value);

	/** Returns all float values in order of received from OSCMessage */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetAllFloats(const FOSCMessage& Message, TArray<float>& Values);

	/** Set Value to integer at provided Index in OSCMessage if in bounds and type matches */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetInt32(const FOSCMessage& Message, const int32 Index, int32& Value);

	/** Returns all integer values in order of received from OSCMessage */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetAllInt32s(const FOSCMessage& Message, TArray<int32>& Values);

	/** Set Value to Int64 at provided Index in OSCMessage if in bounds and type matches */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetInt64(const FOSCMessage& Message, const int32 Index, int64& Value);

	/** Returns all Int64 values in order of received from OSCMessage */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetAllInt64s(const FOSCMessage& Message, TArray<int64>& Values);

	/** Set Value to string at provided Index in OSCMessage if in bounds and type matches */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetString(const FOSCMessage& Message, const int32 Index, FString& Value);

	/** Returns all string values in order of received from OSCMessage */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetAllStrings(const FOSCMessage& Message, TArray<FString>& Values);

	/** Sets Value to boolean at provided Index from OSCMessage if in bounds and type matches */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetBool(const FOSCMessage& Message, const int32 Index, bool& Value);

	/** Returns all boolean values in order of received from OSCMessage */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetAllBools(const FOSCMessage& Message, TArray<bool>& Values);

	/** Sets Value to blob at provided Index from OSCMessage if in bounds and type matches */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetBlob(const FOSCMessage& Message, const int32 Index, TArray<uint8>& Value);

	/** Returns whether OSCAddress is valid path */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static bool OSCAddressIsValidPath(const FOSCAddress& Address);

	/** Returns whether OSCAddress is valid pattern to match against */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static bool OSCAddressIsValidPattern(const FOSCAddress& Address);

	/* Converts string to OSCAddress */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Address") FOSCAddress ConvertStringToOSCAddress(const FString& String);

	/* Pushes container on end of OSCAddress' ordered container array */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Address") FOSCAddress& OSCAddressPushContainer(UPARAM(ref) FOSCAddress& Address, const FString& Container);

	/* Pops container off end of OSCAddress' ordered container array */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Address") FString OSCAddressPopContainer(UPARAM(ref) FOSCAddress& Address);

	/* Returns copy of message's OSCAddress */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Address") FOSCAddress GetOSCMessageAddress(const FOSCMessage& Message);

	/** Sets the OSCAdress Address of the provided message */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Message") FOSCMessage& SetOSCMessageAddress(UPARAM(ref) FOSCMessage& Message, const FOSCAddress& Address);

	/** Returns the OSCAddress container at the provided 'Index.' Returns empty string if index is out-of-bounds. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Container") FString GetOSCAddressContainer(const FOSCAddress& Address, const int32 Index);

	/** Returns ordered array of OSCAddress containers within OSCAddress' path. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Containers") TArray<FString> GetOSCAddressContainers(const FOSCAddress& Address);

	/** Returns OSCAddress' container path. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Path") FString GetOSCAddressContainerPath(const FOSCAddress& Address);

	/** Returns OSCAddress' full path. (Container path plus method name) */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Full Path") FString GetOSCAddressFullPath(const FOSCAddress& Address);

	/** Returns method name of OSCAddress provided */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Method") FString GetOSCAddressMethod(const FOSCAddress& Address);

	/** Sets the method name of the OSCAddress provided */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Address") FOSCAddress& SetOSCAddressMethod(FOSCAddress& Address, const FString& Method);
};

