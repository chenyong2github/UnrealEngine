// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintAsyncActionBase.h"
#include "GameplayMessageRouter.h"
#include "GameplayTags/Classes/GameplayTagContainer.h"

#include "AsyncAction_RegisterGameplayMessageReceiver.generated.h"

class UAsyncAction_RegisterGameplayMessageReceiver;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAsyncGameplayMessageDelegate, const UAsyncAction_RegisterGameplayMessageReceiver*, MessageResult);


UCLASS(BlueprintType)
class GAMEPLAYMESSAGES_API UAsyncAction_RegisterGameplayMessageReceiver : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	/**
	* Asynchronously waits for a gameplay message to be broadcasted on the specified channel.
	* 
	* @param Channel			The message channel to listen for
	* @param MatchType			The rule used for matching the receiver's channel with the broadcasted channel
	* @param bTriggerForSaved	If a message has previously been saved to this channel, immediately trigger a MessageReceived event with this message
	* @param ActorContext		***Not functional yet*** Rather than every message being sent globally, it is possible to broadcast a message with an optional actor context. Only message receivers that have registered with the same actor's context will receive that message.
	*/
	UFUNCTION(BlueprintCallable, Category = Messaging, meta=(WorldContext="WorldContextObject", BlueprintInternalUseOnly="true"))
	static UAsyncAction_RegisterGameplayMessageReceiver* RegisterGameplayMessageReceiver(UObject* WorldContextObject, FGameplayTag Channel, EGameplayMessageMatchType MatchType = EGameplayMessageMatchType::ExactMatch, bool bTriggerForSaved = false, AActor* ActorContext = nullptr);

	/**
	* Attempt to copy the payload received from the broadcasted gameplay message into the specified wildcard.
	* The wildcard's type must match the type from the received message.
	* 
	* @param OutPayload		The wildcard reference the payload should be copied into
	* @return				If the copy was a success
	*/
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Messaging", meta = (CustomStructureParam = "OutPayload"))
	bool GetPayload(UPARAM(ref) int32& OutPayload);

	DECLARE_FUNCTION(execGetPayload);

	/**
	 * Unregister this handler as a message receiver and mark it ready for destruction.
	 */
	UFUNCTION(BlueprintCallable, Category = "Messaging")
	void Unregister();

	virtual void Activate() override;
	virtual void SetReadyToDestroy() override;

public:
	/** Called when a message is broadcast on the specified channel. Use GetPayload() to request the message payload. */
	UPROPERTY(BlueprintAssignable)
	FAsyncGameplayMessageDelegate OnMessageReceived;

private:
	void HandleMessageReceived(FGameplayTag Channel, const UScriptStruct* MessageStructType, const void* MessagePayload);

private:

	const UScriptStruct* ReceivedMessageStructType = nullptr;
	const void* ReceivedMessagePayloadPtr = nullptr;

	TWeakObjectPtr<UWorld> WorldPtr;
	FGameplayTag ChannelToRegister;
	EGameplayMessageMatchType MessageMatchType = EGameplayMessageMatchType::ExactMatch;
	bool bTriggerForSaved = false;

	FGameplayMessageReceiverHandle ReceiverHandle;
};




