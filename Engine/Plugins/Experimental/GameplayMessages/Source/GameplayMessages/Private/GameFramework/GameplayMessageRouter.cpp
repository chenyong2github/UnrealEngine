// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFramework/GameplayMessageRouter.h"
#include "GameFramework/GameplayMessageReplicator.h"
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

bool UGameplayMessageRouter::HasInstance(UObject* WorldContextObject)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::Assert);
	UGameplayMessageRouter* Router = World != nullptr ? UGameInstance::GetSubsystem<UGameplayMessageRouter>(World->GetGameInstance()) : nullptr;
	return Router != nullptr;
}

void UGameplayMessageRouter::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FWorldDelegates::OnWorldInitializedActors.AddUObject(this, &UGameplayMessageRouter::OnWorldActorsInitialized);
}

void UGameplayMessageRouter::Deinitialize()
{
	Super::Deinitialize();

	FWorldDelegates::OnWorldInitializedActors.RemoveAll(this);
}

void UGameplayMessageRouter::BroadcastMessageInternal(FGameplayTag Channel, const UScriptStruct* StructType, const void* MessageBytes, bool bReplicate, bool bSaveToChannel)
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
			// Copy in case there are removals while handling callbacks
			TArray<FReceiverData> ReceiverArray(pList->Receivers);

			for (const FReceiverData& Receiver : ReceiverArray)
			{
				if (bOnInitialTag || (Receiver.MatchType == EGameplayMessageMatchType::PartialMatch))
				{
					// The receiving type must be either a parent of the sending type or completely ambiguous (for internal use)
					if (!Receiver.ReceiverStructType || StructType->IsChildOf(Receiver.ReceiverStructType))
					{
						Receiver.Callback(Channel, StructType, MessageBytes);
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

	if (bSaveToChannel)
	{
		FStructOnScope& SavedMessage = SavedMessageMap.FindOrAdd(Channel);

		if (StructType != SavedMessage.GetStruct())
		{
			// If the existing message is empty or if it currently holds a struct of a different type, we need to Initialize
			SavedMessage.Initialize(StructType);
		}

		StructType->CopyScriptStruct(SavedMessage.GetStructMemory(), MessageBytes);
	}

	if (bReplicate && MessageReplicator)
	{
		MessageReplicator->ReplicateMessage(Channel, StructType, MessageBytes);
	}
}

void UGameplayMessageRouter::K2_BroadcastMessage(FGameplayTag Channel, const int32& Message, bool bReplicate, bool bSaveToChannel, AActor* ActorContext)
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

	P_GET_PROPERTY(FBoolProperty, bReplicate);
	P_GET_PROPERTY(FBoolProperty, bSaveToChannel);
	P_GET_OBJECT(AActor, ActorContext);

	P_FINISH;

	if (ensure((StructProp != nullptr) && (StructProp->Struct != nullptr) && (MessagePtr != nullptr)))
	{
		P_THIS->BroadcastMessageInternal(Channel, StructProp->Struct, MessagePtr, bReplicate, bSaveToChannel);
	}
}

FGameplayMessageReceiverHandle UGameplayMessageRouter::RegisterReceiverInternal(FGameplayTag Channel, TFunction<void(FGameplayTag, const UScriptStruct*, const void*)>&& Callback, const UScriptStruct* StructType, EGameplayMessageMatchType MatchType)
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

void UGameplayMessageRouter::ClearSavedMessage(FGameplayTag Channel)
{
	SavedMessageMap.Remove(Channel);
}

bool UGameplayMessageRouter::GetSavedMessageInternal(FGameplayTag Channel, const UScriptStruct*& OutStructType, const void*& OutMessageBytes)
{
	if (Channel.IsValid())
	{
		if (FStructOnScope* SavedMessage = SavedMessageMap.Find(Channel))
		{
			if (SavedMessage->IsValid())
			{
				OutStructType = Cast<UScriptStruct>(SavedMessage->GetStruct());
				OutMessageBytes = SavedMessage->GetStructMemory();
				return true;
			}
			else
			{
				ClearSavedMessage(Channel);
			}
		}
	}

	OutStructType = nullptr;
	OutMessageBytes = nullptr;
	return false;
}

void UGameplayMessageRouter::OnWorldActorsInitialized(const UWorld::FActorsInitializedParams& Params)
{
	UGameInstance* GameInstance = GetGameInstance();
	check(GameInstance);

	if (Params.World && (Params.World->GetGameInstance() == GameInstance))
	{
		HandleWorldChanged(Params.World);
	}
}

void UGameplayMessageRouter::HandleWorldChanged(UWorld* NewWorld)
{
	if (NewWorld && NewWorld->IsGameWorld() && NewWorld->GetNetMode() < NM_Client)
	{
		MessageReplicator = NewWorld->SpawnActor<AGameplayMessageReplicator>();
	}
	else
	{
		MessageReplicator = nullptr;
	}

	// Saved messages should be able to persist through world transitions but until we have a better mechanism for handling the lifetime of saved messages this is probably safest.
	SavedMessageMap.Empty();
}
