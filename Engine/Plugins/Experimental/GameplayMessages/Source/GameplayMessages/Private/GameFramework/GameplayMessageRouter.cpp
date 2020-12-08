// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFramework/GameplayMessageRouter.h"
#include "Engine/Engine.h"

DEFINE_LOG_CATEGORY(LogGameplayMessageRouter);

namespace UE
{
	namespace GameplayMessageRouter
	{
		static int32 ShouldLogMessages = 0;
		static FAutoConsoleVariableRef CVarShouldLogMessages(TEXT("GameplayMessageRouter.LogMessages"),
			ShouldLogMessages,
			TEXT("Should messages broadcast through the gameplay message router be logged?"));
	}
}

//////////////////////////////////////////////////////////////////////
// FGameplayMessageReceiverHandle

void FGameplayMessageReceiverHandle::Unregister()
{
	if (UGameplayMessageRouter* StrongSubsystem = Subsystem.Get())
	{
		StrongSubsystem->UnregisterReceiver(*this);
		Subsystem.Reset();
		Channel = FGameplayTag();
		ID = 0;
	}
}

//////////////////////////////////////////////////////////////////////
// UGameplayMessageRouter

UGameplayMessageRouter& UGameplayMessageRouter::Get(UObject* WorldContextObject)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::Assert);
	check(World);
	UGameplayMessageRouter* Router = UGameInstance::GetSubsystem<UGameplayMessageRouter>(World->GetGameInstance());
	check(Router);
	return *Router;
}

void UGameplayMessageRouter::BroadcastMessageInternal(FGameplayTag Channel, const UScriptStruct* StructType, const void* MessageBytes) const
{
	// Log the message if enabled
	if (UE::GameplayMessageRouter::ShouldLogMessages != 0)
	{
		FString* pContextString = nullptr;
#if WITH_EDITOR
		if (GIsEditor)
		{
			extern ENGINE_API FString GPlayInEditorContextString;
			pContextString = &GPlayInEditorContextString;
		}
#endif

		FString HumanReadableMessage;
		StructType->ExportText(/*out*/ HumanReadableMessage, MessageBytes, /*Defaults=*/ nullptr, /*OwnerObject=*/ nullptr, PPF_None, /*ExportRootScope=*/ nullptr);
		UE_LOG(LogGameplayMessageRouter, Log, TEXT("BroadcastMessage(%s, %s, %s)"), pContextString ? **pContextString : *GetPathNameSafe(this), *Channel.ToString(), *HumanReadableMessage);
	}

	// Broadcast the message
	bool bOnInitialTag = true;
	for (FGameplayTag Tag = Channel; Tag.IsValid(); Tag = Tag.RequestDirectParent())
	{
		if (const FChannelReceiverList* pList = ReceiverMap.Find(Tag))
		{
			for (const FReceiverData& Receiver : pList->Receivers)
			{
				if (bOnInitialTag || (Receiver.MatchType == EGameplayMessageMatchType::PartialMatch))
				{
					if (StructType->IsChildOf(Receiver.ReceiverStructType))
					{
						Receiver.Callback(Channel, MessageBytes);
					}
					else
					{
						UE_LOG(LogGameplayMessageRouter, Error, TEXT("Struct type mismatch on channel %s (broadcast type %s, receiver at %s was expecting type %s)"),
							*Channel.ToString(),
							*GetPathName(StructType),
							*Tag.ToString(),
							*GetPathName(Receiver.ReceiverStructType));
					}
				}
			}
		}
		bOnInitialTag = false;
	}	
}

void UGameplayMessageRouter::K2_BroadcastMessage(FGameplayTag Channel, const int32& Message)
{
	// This will never be called, the exec version below will be hit instead
	check(0);
}

DEFINE_FUNCTION(UGameplayMessageRouter::execK2_BroadcastMessage)
{
	P_GET_STRUCT(FGameplayTag, Channel);

	Stack.MostRecentPropertyAddress = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	void* MessagePtr = Stack.MostRecentPropertyAddress;
	FStructProperty* StructProp = CastField<FStructProperty>(Stack.MostRecentProperty);
	P_FINISH;

	if (ensure((StructProp != nullptr) && (StructProp->Struct != nullptr) && (MessagePtr != nullptr)))
	{
		P_THIS->BroadcastMessageInternal(Channel, StructProp->Struct, MessagePtr);
	}
}

FGameplayMessageReceiverHandle UGameplayMessageRouter::RegisterReceiverInternal(FGameplayTag Channel, TFunction<void(FGameplayTag, const void*)>&& Callback, const UScriptStruct* StructType, EGameplayMessageMatchType MatchType)
{
	FChannelReceiverList& List = ReceiverMap.FindOrAdd(Channel);

	FReceiverData& Entry = List.Receivers.AddDefaulted_GetRef();
	Entry.Callback = MoveTemp(Callback);
	Entry.ReceiverStructType = StructType;
	Entry.HandleID = ++List.HandleID;
	Entry.MatchType = MatchType;

	return FGameplayMessageReceiverHandle(this, Channel, Entry.HandleID);
}

void UGameplayMessageRouter::UnregisterReceiver(FGameplayMessageReceiverHandle Handle)
{
	check(Handle.Subsystem == this);

	if (FChannelReceiverList* pList = ReceiverMap.Find(Handle.Channel))
	{
		int32 MatchIndex = pList->Receivers.IndexOfByPredicate([ID = Handle.ID](const FReceiverData& Other) { return Other.HandleID == ID; });
		if (MatchIndex != INDEX_NONE)
		{
			pList->Receivers.RemoveAtSwap(MatchIndex);
		}

		if (pList->Receivers.Num() == 0)
		{
			ReceiverMap.Remove(Handle.Channel);
		}
	}
}
