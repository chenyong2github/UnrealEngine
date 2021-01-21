// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Logging/LogMacros.h"


#include "GameplayMessageRouter.generated.h"

GAMEPLAYMESSAGES_API DECLARE_LOG_CATEGORY_EXTERN(LogGameplayMessageRouter, Log, All);

class AGameplayMessageReplicator;
class UAsyncAction_RegisterGameplayMessageReceiver;

// Match rule for message receivers
UENUM(BlueprintType)
enum class EGameplayMessageMatchType : uint8
{
	// An exact match will only receive messages with exactly the same channel
	// (e.g., registering for "A.B" will match a broadcast of A.B but not A.B.C)
	ExactMatch,

	// A partial match will receive any messages rooted in the same channel
	// (e.g., registering for "A.B" will match a broadcast of A.B as well as A.B.C)
	PartialMatch
};

/**
 * An opaque handle that can be used to remove a previously registered message receiver
 * @see UGameplayMessageRouter::RegisterReceiver and UGameplayMessageRouter::UnregisterReceiver
 */
USTRUCT(BlueprintType)
struct GAMEPLAYMESSAGES_API FGameplayMessageReceiverHandle
{
public:
	GENERATED_BODY()

		FGameplayMessageReceiverHandle() {}

	void Unregister();

private:
	UPROPERTY(Transient)
		TWeakObjectPtr<UGameplayMessageRouter> Subsystem;

	UPROPERTY(Transient)
		FGameplayTag Channel;

	UPROPERTY(Transient)
		int32 ID = 0;

	friend UGameplayMessageRouter;

	FGameplayMessageReceiverHandle(UGameplayMessageRouter* InSubsystem, FGameplayTag InChannel, int32 InID) : Subsystem(InSubsystem), Channel(InChannel), ID(InID) {}
};

/**
 * This system allows event raisers and receivers to register for messages without
 * having to know about each other directly, though they must agree on the format
 * of the message (as a USTRUCT() type).
 *
 * You can get to the message router from the game instance:
 *    UGameInstance::GetSubsystem<UGameplayMessageRouter>(GameInstance)
 * or directly from anything that has a route to a world:
 *    UGameplayMessageRouter::Get(WorldContextObject)
 *
 * Note that call order when there are multiple receivers for the same channel is
 * not guaranteed and can change over time!
 */
UCLASS()
class GAMEPLAYMESSAGES_API UGameplayMessageRouter : public UGameInstanceSubsystem
{
	GENERATED_BODY()

		friend AGameplayMessageReplicator;
	friend UAsyncAction_RegisterGameplayMessageReceiver;

public:
	/**
	 * @return the message router for the game instance associated with the world of the specified object
	 */
	static UGameplayMessageRouter& Get(UObject* WorldContextObject);

	/**
	 * @return true if a valid GameplayMessageRouter subsystem if active in the provided world
	 */
	static bool HasInstance(UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Broadcast a message on the specified channel
	 *
	 * @param Channel			The message channel to broadcast on
	 * @param Message			The message to send (must be the same type of UScriptStruct expected by the receivers for this channel, otherwise an error will be logged)
	 * @param bReplicate		Should this message be replicated from the server to the client
	 * @param bSaveToChannel	Should this message be saved to the channel so it can be queried in the future
	 * @param ActorContext		***Not functional yet*** Rather than broadcasting every message globally, you can optionally specify an actor context. Only message receivers that have registered with this actor's context will receive this message
	 */
	template <typename FMessageStructType>
	void BroadcastMessage(FGameplayTag Channel, const FMessageStructType& Message, bool bReplicate = false, bool bSaveToChannel = false, AActor* ActorContext = nullptr)
	{
		const UScriptStruct* StructType = TBaseStructure<FMessageStructType>::Get();
		BroadcastMessageInternal(Channel, StructType, &Message, bReplicate, bSaveToChannel);
	}

	/**
	 * Register to receive messages on a specified channel
	 *
	 * @param Channel			The message channel to listen to
	 * @param Callback			Function to call with the message when someone broadcasts it (must be the same type of UScriptStruct provided by broadcasters for this channel, otherwise an error will be logged)
	 * @param MatchType			Whether Callback should be called for broadcasts of more derived channels or if it will only be called for exact matches
	 * @param bTriggerForSaved	If a message has previously been saved to this channel, immediately trigger the receiver callback event with this message
	 * @param ActorContext		***Not functional yet*** Rather than every message being sent globally, it is possible to broadcast a message with an optional actor context. Only message receivers that have registered with the same actor's context will receive that message
	 *
	 * @return a handle that can be used to unregister this receiver (either by calling Unregister() on the handle or calling UnregisterReceiver on the router)
	 */
	template <typename FMessageStructType>
	FGameplayMessageReceiverHandle RegisterReceiver(FGameplayTag Channel, TFunction<void(FGameplayTag, const FMessageStructType&)>&& Callback, EGameplayMessageMatchType MatchType = EGameplayMessageMatchType::ExactMatch, bool bTriggerForSaved = false, AActor* ActorContext = nullptr)
	{
		auto ThunkCallback = [InnerCallback = MoveTemp(Callback)](FGameplayTag ActualTag, const UScriptStruct* SenderStructType, const void* SenderPayload)
		{
			InnerCallback(ActualTag, *reinterpret_cast<const FMessageStructType*>(SenderPayload));
		};

		const UScriptStruct* StructType = TBaseStructure<FMessageStructType>::Get();
		FGameplayMessageReceiverHandle Handle = RegisterReceiverInternal(Channel, MoveTemp(ThunkCallback), StructType, MatchType);

		if (bTriggerForSaved)
		{
			const UScriptStruct* SavedStruct = nullptr;
			const void* SavedPtr = nullptr;

			if (GetSavedMessageInternal(Channel, SavedStruct, SavedPtr))
			{
				ThunkCallback(Channel, SavedStruct, SavedPtr);
			}
		}

		return Handle;
	}

	/**
	 * Remove a message receiver previously registered by RegisterReceiver
	 *
	 * @param Handle	The handle returned by RegisterReceiver
	 */
	void UnregisterReceiver(FGameplayMessageReceiverHandle Handle);

	/**
	 * Clear the message saved to a channel if there is one
	 *
	 * @param Channel	The message channel to clear of its saved message
	 */
	UFUNCTION(BlueprintCallable, Category = Messaging)
		void ClearSavedMessage(FGameplayTag Channel);

protected:
	/**
	 * Broadcast a message on the specified channel
	 *
	 * @param Channel			The message channel to broadcast on
	 * @param Message			The message to send (must be the same type of UScriptStruct expected by the receivers for this channel, otherwise an error will be logged)
	 * @param bReplicate		Should this message be replicated from the server to the client
	 * @param bSaveToChannel	Should this message be saved to the channel so it can be queried in the future
	 * @param ActorContext		***Not functional yet*** Rather than broadcasting every message globally, you can optionally specify an actor context. Only message receivers that have registered with this actor's context will receive this message.
	 */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = Messaging, meta = (CustomStructureParam = "Message", AllowAbstract = "false", DisplayName = "Broadcast Message"))
		void K2_BroadcastMessage(FGameplayTag Channel, const int32& Message, bool bReplicate = false, bool bSaveToChannel = false, AActor* ActorContext = nullptr);

	DECLARE_FUNCTION(execK2_BroadcastMessage);

private:
	// Internal helper for broadcasting a message
	void BroadcastMessageInternal(FGameplayTag Channel, const UScriptStruct* StructType, const void* MessageBytes, bool bReplicate = false, bool bSaveToChannel = false);

	// Internal helper for registering a message receiver
	FGameplayMessageReceiverHandle RegisterReceiverInternal(FGameplayTag Channel, TFunction<void(FGameplayTag, const UScriptStruct*, const void*)>&& Callback, const UScriptStruct* StructType, EGameplayMessageMatchType MatchType);

	// Internal helper for recovering a message that has been saved to a channel, if one exists
	bool GetSavedMessageInternal(FGameplayTag Channel, const UScriptStruct*& OutStructType, const void*& OutMessageBytes);

	void OnWorldActorsInitialized(const UWorld::FActorsInitializedParams& Params);
	void HandleWorldChanged(UWorld* NewWorld);

private:
	// Entry for a single receiver
	struct FReceiverData
	{
		TFunction<void(FGameplayTag, const UScriptStruct*, const void*)> Callback;
		const UScriptStruct* ReceiverStructType;
		int32 HandleID;
		EGameplayMessageMatchType MatchType;
	};

	// List of all entries for a given channel
	struct FChannelReceiverList
	{
		TArray<FReceiverData> Receivers;
		int32 HandleID = 0;
	};

private:
	TMap<FGameplayTag, FChannelReceiverList> ReceiverMap;

	TMap<FGameplayTag, FStructOnScope> SavedMessageMap;

	UPROPERTY(Transient)
	AGameplayMessageReplicator* MessageReplicator;
};
