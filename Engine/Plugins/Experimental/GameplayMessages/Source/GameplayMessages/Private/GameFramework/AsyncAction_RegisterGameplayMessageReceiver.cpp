// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFramework/AsyncAction_RegisterGameplayMessageReceiver.h"
#include "Engine/Engine.h"
#include "GameFramework/GameplayMessageRouter.h"

UAsyncAction_RegisterGameplayMessageReceiver* UAsyncAction_RegisterGameplayMessageReceiver::RegisterGameplayMessageReceiver(UObject* WorldContextObject, FGameplayTag Channel, EGameplayMessageMatchType MatchType, bool bTriggerForSaved, AActor* ActorContext)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (!World)
	{
		return nullptr;
	}

	UAsyncAction_RegisterGameplayMessageReceiver* Action = NewObject<UAsyncAction_RegisterGameplayMessageReceiver>();
	Action->WorldPtr = World;
	Action->ChannelToRegister = Channel;
	Action->MessageMatchType = MatchType;
	Action->bTriggerForSaved = bTriggerForSaved;
	Action->RegisterWithGameInstance(World);

	return Action;
}

void UAsyncAction_RegisterGameplayMessageReceiver::Activate()
{
	if (UWorld* World = WorldPtr.Get())
	{
		if (UGameplayMessageRouter::HasInstance(World))
		{
			UGameplayMessageRouter& Router = UGameplayMessageRouter::Get(World);

			TWeakObjectPtr<UAsyncAction_RegisterGameplayMessageReceiver> WeakThis(this);
			ReceiverHandle = Router.RegisterReceiverInternal(ChannelToRegister,
				[WeakThis](FGameplayTag Channel, const UScriptStruct* MessageStructType, const void* MessagePayload)
				{
					if (UAsyncAction_RegisterGameplayMessageReceiver* StrongThis = WeakThis.Get())
					{
						StrongThis->HandleMessageReceived(Channel, MessageStructType, MessagePayload);
					}
				}
				, nullptr, MessageMatchType);

			if (bTriggerForSaved)
			{
				const UScriptStruct* SavedStruct = nullptr;
				const void* SavedPtr = nullptr;

				if (Router.GetSavedMessageInternal(ChannelToRegister, SavedStruct, SavedPtr))
				{
					HandleMessageReceived(ChannelToRegister, SavedStruct, SavedPtr);
				}
			}

			return;
		}
	}

	SetReadyToDestroy();
}

void UAsyncAction_RegisterGameplayMessageReceiver::SetReadyToDestroy()
{
	ReceiverHandle.Unregister();

	Super::SetReadyToDestroy();
}

bool UAsyncAction_RegisterGameplayMessageReceiver::GetPayload(int32& OutPayload)
{
	check(0);
	return false;
}

DEFINE_FUNCTION(UAsyncAction_RegisterGameplayMessageReceiver::execGetPayload)
{
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	void* MessagePtr = Stack.MostRecentPropertyAddress;
	FStructProperty* StructProp = CastField<FStructProperty>(Stack.MostRecentProperty);
	P_FINISH;

	bool bSuccess = false;

	// Make sure the type we are trying to get through the blueprint node matches the type of the message payload received.
	if (ensure((StructProp != nullptr) && (StructProp->Struct != nullptr) && (MessagePtr != nullptr) && (StructProp->Struct == P_THIS->ReceivedMessageStructType) && (P_THIS->ReceivedMessagePayloadPtr != nullptr)))
	{
		StructProp->Struct->CopyScriptStruct(MessagePtr, P_THIS->ReceivedMessagePayloadPtr);
		bSuccess = true;
	}

	*(bool*)RESULT_PARAM = bSuccess;
}

void UAsyncAction_RegisterGameplayMessageReceiver::Unregister()
{
	SetReadyToDestroy();
}

void UAsyncAction_RegisterGameplayMessageReceiver::HandleMessageReceived(FGameplayTag Channel, const UScriptStruct* MessageStructType, const void* MessagePayload)
{
	ReceivedMessageStructType = MessageStructType;
	ReceivedMessagePayloadPtr = MessagePayload;

	OnMessageReceived.Broadcast(this);

	ReceivedMessageStructType = nullptr;
	ReceivedMessagePayloadPtr = nullptr;

	if (!OnMessageReceived.IsBound())
	{
		// If the BP object that created the async node is destroyed, OnMessageReceived will be unbound after calling the broadcast.
		// In this case we can safely mark this receiver as ready for destruction.
		// Need to support a more proactive mechanism for cleanup FORT-340994
		SetReadyToDestroy();
	}
}


