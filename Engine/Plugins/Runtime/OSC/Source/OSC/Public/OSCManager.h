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
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Add OSC MEssage to Bundle", Keywords = "osc message bundle"))
	static UPARAM(DisplayName = "Bundle") FOSCBundle& AddMessageToBundle(const FOSCMessage& Message, UPARAM(ref) FOSCBundle& Bundle);

	/** Adds bundle packet to bundle. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Add OSC Bundle to Bundle", Keywords = "osc message"))
	static UPARAM(DisplayName = "OutBundle") FOSCBundle& AddBundleToBundle(const FOSCBundle& InBundle, UPARAM(ref) FOSCBundle& OutBundle);

	/** Fills array with child bundles found in bundle. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Get OSC Bundles From Bundle", Keywords = "osc bundle"))
	static UPARAM(DisplayName = "Bundles") TArray<FOSCBundle>& GetBundlesFromBundle(const FOSCBundle& Bundle, TArray<FOSCBundle>& Bundles);

	/** Fills array with messages found in bundle. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Get OSC Messages From Bundle", Keywords = "osc bundle message"))
	static UPARAM(DisplayName = "Messages") TArray<FOSCMessage>& GetMessagesFromBundle(const FOSCBundle& Bundle, TArray<FOSCMessage>& Messages);

	/** Clears provided message of all arguments. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Clear OSC Message", Keywords = "osc message"))
	static UPARAM(DisplayName = "Message") FOSCMessage& ClearMessage(UPARAM(ref) FOSCMessage& Message);

	/** Clears provided bundle of all internal messages/bundle packets. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Clear OSC Bundle", Keywords = "osc message"))
	static UPARAM(DisplayName = "Bundle") FOSCBundle& ClearBundle(UPARAM(ref) FOSCBundle& Bundle);

	/** Adds float value to end of OSCMessage */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Add Float to OSC Message", Keywords = "osc message"))
	static UPARAM(DisplayName = "Message") FOSCMessage& AddFloat(UPARAM(ref) FOSCMessage& Message, float Value);

	/** Adds Int32 value to end of OSCMessage */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Add Integer to OSC Message", Keywords = "osc message"))
	static UPARAM(DisplayName = "Message") FOSCMessage& AddInt32(UPARAM(ref) FOSCMessage& Message, int32 Value);

	/** Adds Int64 value to end of OSCMessage */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Add Integer (64-bit) to OSC Message", Keywords = "osc message"))
	static UPARAM(DisplayName = "Message") FOSCMessage& AddInt64(UPARAM(ref) FOSCMessage& Message, int64 Value);

	/** Adds string value to end of OSCMessage */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Add String to OSC Message", Keywords = "osc message"))
	static UPARAM(DisplayName = "Message") FOSCMessage& AddString(UPARAM(ref) FOSCMessage& Message, FString Value);

	/** Adds blob value to end of OSCMessage */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Add Blob to OSC Message", Keywords = "osc message"))
	static UPARAM(DisplayName = "Message") FOSCMessage& AddBlob(UPARAM(ref) FOSCMessage& Message, TArray<uint8>& Value);

	/** Adds boolean value to end of OSCMessage */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Add Bool to OSC Message", Keywords = "osc message"))
	static UPARAM(DisplayName = "Message") FOSCMessage& AddBool(UPARAM(ref) FOSCMessage& Message, bool Value);

	/** Set Value to float at provided Index in OSCMessage if in bounds and type matches */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Get OSC Message Float At Index", Keywords = "osc message"))
	static void GetFloat(const FOSCMessage& Message, const int32 Index, float& Value);

	/** Returns all float values in order of received from OSCMessage */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Get OSC Message Floats", Keywords = "osc message"))
	static void GetAllFloats(const FOSCMessage& Message, TArray<float>& Values);

	/** Set Value to integer at provided Index in OSCMessage if in bounds and type matches */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Get OSC Message Integer at Index", Keywords = "osc message"))
	static void GetInt32(const FOSCMessage& Message, const int32 Index, int32& Value);

	/** Returns all integer values in order of received from OSCMessage */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Get OSC Message Integers", Keywords = "osc message"))
	static void GetAllInt32s(const FOSCMessage& Message, TArray<int32>& Values);

	/** Set Value to Int64 at provided Index in OSCMessage if in bounds and type matches */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Get OSC Message Integer (64-bit) at Index", Keywords = "osc message"))
	static void GetInt64(const FOSCMessage& Message, const int32 Index, int64& Value);

	/** Returns all Int64 values in order of received from OSCMessage */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Get OSC Message Integers (64-bit)", Keywords = "osc message"))
	static void GetAllInt64s(const FOSCMessage& Message, TArray<int64>& Values);

	/** Set Value to string at provided Index in OSCMessage if in bounds and type matches */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Get OSC Message String at Index", Keywords = "osc message"))
	static void GetString(const FOSCMessage& Message, const int32 Index, FString& Value);

	/** Returns all string values in order of received from OSCMessage */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Get OSC Message Strings", Keywords = "osc message"))
	static void GetAllStrings(const FOSCMessage& Message, TArray<FString>& Values);

	/** Sets Value to boolean at provided Index from OSCMessage if in bounds and type matches */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Get OSC Message Bool At Index", Keywords = "osc message"))
	static void GetBool(const FOSCMessage& Message, const int32 Index, bool& Value);

	/** Returns all boolean values in order of received from OSCMessage */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Get OSC Message Bools", Keywords = "osc message"))
	static void GetAllBools(const FOSCMessage& Message, TArray<bool>& Values);

	/** Sets Value to blob at provided Index from OSCMessage if in bounds and type matches */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static void GetBlob(const FOSCMessage& Message, const int32 Index, TArray<uint8>& Value);

	/** Returns whether OSCAddress is valid path */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Is OSC Address Valid Path", Keywords = "valid osc address path address"))
	static bool OSCAddressIsValidPath(const FOSCAddress& Address);

	/** Returns whether OSCAddress is valid pattern to match against */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Is OSC Address Valid Pattern", Keywords = "valid osc address pattern address"))
	static bool OSCAddressIsValidPattern(const FOSCAddress& Address);

	/* Converts string to OSCAddress */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Address") FOSCAddress ConvertStringToOSCAddress(const FString& String);

	/** Returns if address pattern matches the provided address path.
	  * If passed address is not a valid path, returns false.
	  */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "OSC Address Path Matches Pattern", Keywords = "matches osc address path address"))
	static UPARAM(DisplayName = "Is Match") bool OSCAddressPathMatchesPattern(const FOSCAddress& Pattern, const FOSCAddress& Path);

	/** Pushes container onto address' ordered array of containers */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Push Container to OSC Address", Keywords = "push osc address container"))
	static UPARAM(DisplayName = "Address") FOSCAddress& OSCAddressPushContainer(UPARAM(ref) FOSCAddress& Address, const FString& Container);

	/** Pushes container onto address' ordered array of containers */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Push Container Array to OSC Address", Keywords = "push osc address container"))
	static UPARAM(DisplayName = "Address") FOSCAddress& OSCAddressPushContainers(UPARAM(ref) FOSCAddress& Address, const TArray<FString>& Containers);

	/** Pops container from ordered array of containers. If no containers, returns empty string */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Pop Container from OSC Address", Keywords = "pop osc address container"))
	static UPARAM(DisplayName = "Container") FString OSCAddressPopContainer(UPARAM(ref) FOSCAddress& Address);

	/** Pops container from ordered array of containers. If NumContainers is greater than or equal to the number of containers in address, returns all containers. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Pop Containers from OSC Address", Keywords = "pop osc address container"))
	static UPARAM(DisplayName = "Containers") TArray<FString> OSCAddressPopContainers(UPARAM(ref) FOSCAddress& Address, int32 NumContainers);

	/* Returns copy of message's OSCAddress */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Address") FOSCAddress GetOSCMessageAddress(const FOSCMessage& Message);

	/** Sets the OSCAddress of the provided message */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Message") FOSCMessage& SetOSCMessageAddress(UPARAM(ref) FOSCMessage& Message, const FOSCAddress& Address);

	/** Returns the OSCAddress container at the provided 'Index.' Returns empty string if index is out-of-bounds. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Get OSC Address Container At Index", Keywords = "osc address container path"))
	static UPARAM(DisplayName = "Container") FString GetOSCAddressContainer(const FOSCAddress& Address, const int32 Index);

	/** Builds referenced array of address of containers in order */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Get OSC Address Containers", Keywords = "osc address container path"))
	static UPARAM(DisplayName = "Containers") TArray<FString> GetOSCAddressContainers(const FOSCAddress& Address);

	/** Returns full path of OSC address in the form '/Container1/Container2/Method' */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Path") FString GetOSCAddressContainerPath(const FOSCAddress& Address);

	/** Returns full path of OSC address in the form '/Container1/Container2' */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Convert OSC Address To String", Keywords = "osc address path"))
	static UPARAM(DisplayName = "Full Path") FString GetOSCAddressFullPath(const FOSCAddress& Address);

	/** Returns method name of OSCAddress provided */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	static UPARAM(DisplayName = "Method") FString GetOSCAddressMethod(const FOSCAddress& Address);

	/** Clears containers of OSCAddress provided */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Clear OSC Address Containers", Keywords = "osc address clear"))
	static UPARAM(DisplayName = "Address") FOSCAddress& ClearOSCAddressContainers(FOSCAddress& Address);

	/** Sets the method name of the OSCAddress provided */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC", meta = (DisplayName = "Set OSC Address Method", Keywords = "osc method"))
	static UPARAM(DisplayName = "Address") FOSCAddress& SetOSCAddressMethod(FOSCAddress& Address, const FString& Method);
};

