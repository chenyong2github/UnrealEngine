// Copyright Epic Games, Inc. All Rights Reserved.

#include "StageMonitorSession.h"

#include "Misc/App.h"
#include "StageMessages.h"
#include "StageCriticalEventHandler.h"
#include "StageMonitorModule.h"


FStageMonitorSession::FStageMonitorSession(const FString& InSessionName)
	: CriticalEventHandler(MakeUnique<FStageCriticalEventHandler>())
	, SessionName(InSessionName)
{

}

void FStageMonitorSession::AddProvider(const FGuid& Identifier, const FStageInstanceDescriptor& Descriptor, const FMessageAddress& Address)
{
	//Adds this provider if it's not being monitored
	if (!Providers.ContainsByPredicate([Identifier](const FStageSessionProviderEntry& Other) { return Other.Identifier == Identifier; }))
	{
		FStageSessionProviderEntry NewProvider;
		NewProvider.Identifier = Identifier;
		NewProvider.Descriptor = Descriptor;
		NewProvider.Address = Address;
		NewProvider.State = EStageDataProviderState::Active;
		Providers.Emplace(MoveTemp(NewProvider));

		OnStageDataProviderListChanged().Broadcast();
	}
}

void FStageMonitorSession::UpdateProviderDescription(const FGuid& Identifier, const FStageInstanceDescriptor& NewDescriptor, const FMessageAddress& NewAddress)
{
	//Update this provider's information if we had it in our list
	FStageSessionProviderEntry* Provider = Providers.FindByPredicate([Identifier](const FStageSessionProviderEntry& Other){ return Other.Identifier == Identifier; });
	if (Provider)
	{
		Provider->Descriptor = NewDescriptor;
		Provider->Address = NewAddress;
	}
}

void FStageMonitorSession::AddProviderMessage(UScriptStruct* Type, const FStageProviderMessage* MessageData)
{
	if (MessageData == nullptr)
	{
		return;
	}

	//Only process messages coming from registered machines
	FStageSessionProviderEntry* Provider = Providers.FindByPredicate([MessageData](const FStageSessionProviderEntry& Other) { return MessageData->Identifier == Other.Identifier; });
	if (Provider == nullptr)
	{
		return;
	}

	//Stamp last time this provider received a message
	Provider->LastReceivedMessageTime = FApp::GetCurrentTime();
	
	//If we're dealing with discovery response, just stamp received time and exit
	if (Type == FStageProviderDiscoveryResponseMessage::StaticStruct())
	{
		return;
	}

	// Special handling for critical event to track stage state
	if (Type->IsChildOf(FCriticalStateProviderMessage::StaticStruct()))
	{
		CriticalEventHandler->HandleCriticalEventMessage(static_cast<const FCriticalStateProviderMessage*>(MessageData));
	}

	//Add this message to session data => Full entry list and per provider / data type list
	TSharedPtr<FStageDataEntry> NewEntry = MakeShared<FStageDataEntry>();
	NewEntry->MessageTime = FApp::GetCurrentTime();
	NewEntry->Data = MakeShared<FStructOnScope>(Type);
	Type->CopyScriptStruct(NewEntry->Data->GetStructMemory(), MessageData);

	UpdateProviderLatestEntry(MessageData->Identifier, Type, NewEntry);
	InsertNewEntry(NewEntry);
	
	//Let know listeners that new data was received
	OnStageSessionNewDataReceived().Broadcast(NewEntry);
}

TArray<FMessageAddress> FStageMonitorSession::GetProvidersAddress() const
{
	TArray<FMessageAddress> Addresses;
	for (const FStageSessionProviderEntry& Entry : Providers)
	{
		if (Entry.State != EStageDataProviderState::Closed)
		{
			Addresses.Add(Entry.Address);
		}
	}

	return Addresses;
}

void FStageMonitorSession::SetProviderState(const FGuid& Identifier, EStageDataProviderState State)
{
	if (FStageSessionProviderEntry* Provider = Providers.FindByPredicate([Identifier](const FStageSessionProviderEntry& Other) { return Other.Identifier == Identifier; }))
	{
		Provider->State = State;
	}
}

bool FStageMonitorSession::GetProvider(const FGuid& Identifier, FStageSessionProviderEntry& OutProviderEntry) const
{
	if (const FStageSessionProviderEntry* Provider = Providers.FindByPredicate([Identifier](const FStageSessionProviderEntry& Other) { return Other.Identifier == Identifier; }))
	{
		OutProviderEntry = *Provider;
		return true;
	}

	return false;
}

void FStageMonitorSession::ClearAll()
{
	//Clear all entry list and per provider latest data
	Entries.Empty();
	ProviderLatestData.Empty();
	
	//Let know our listeners that we have just been cleared
	OnStageSessionDataClearedDelegate.Broadcast();
}

TSharedPtr<FStageDataEntry> FStageMonitorSession::GetLatest(const FGuid& Identifier, UScriptStruct* Type)
{
	if (ProviderLatestData.Contains(Identifier))
	{
		//Find the provider and see if it has an entry for this data type.
		TArray<TSharedPtr<FStageDataEntry>>& ProviderEntries = ProviderLatestData[Identifier];
		TSharedPtr<FStageDataEntry>* Latest = ProviderEntries.FindByPredicate([Type](const TSharedPtr<FStageDataEntry>& Other)
			{
				if (Other.IsValid())
				{
					if (Other->Data.IsValid())
					{
						return Other->Data->GetStruct() == Type;
					}
				}

				return false;
			});

		if (Latest != nullptr)
		{
			return *Latest;
		}
	}

	return TSharedPtr<FStageDataEntry>();
}

EStageDataProviderState FStageMonitorSession::GetProviderState(const FGuid& Identifier)
{
	const FStageSessionProviderEntry* Provider = Providers.FindByPredicate([Identifier](const FStageSessionProviderEntry& Other) { return Other.Identifier == Identifier; });
	if (Provider)
	{
		return Provider->State;
	}
	
	//Unknown provider state defaults to closed
	return EStageDataProviderState::Closed;
}

void FStageMonitorSession::UpdateProviderLatestEntry(const FGuid& Identifier, UScriptStruct* Type, TSharedPtr<FStageDataEntry> NewEntry)
{
	//Update latest entry of that type
	TArray<TSharedPtr<FStageDataEntry>>& ProviderEntries = ProviderLatestData.FindOrAdd(Identifier);
	TSharedPtr<FStageDataEntry>* Latest = ProviderEntries.FindByPredicate([Type](const TSharedPtr<FStageDataEntry>& Other)
		{
			if (Other.IsValid())
			{
				if (Other->Data.IsValid())
				{
					return Other->Data->GetStruct() == Type;
				}
			}

			return false;
		});

	if (Latest == nullptr)
	{
		ProviderEntries.Add(NewEntry);
	}
	else
	{
		*Latest = NewEntry;
	}
}

void FStageMonitorSession::InsertNewEntry(TSharedPtr<FStageDataEntry> NewEntry)
{
	const FStageProviderMessage* Message = reinterpret_cast<const FStageProviderMessage*>(NewEntry->Data->GetStructMemory());
	check(Message);

	//We are always inserting in order so we shouldn't have to navigate in our list much
	const double NewFrameSeconds = Message->FrameTime.AsSeconds();
	int32 EntryIndex = Entries.Num() - 1;
	for (; EntryIndex >= 0; --EntryIndex)
	{
		const FStageProviderMessage* ThisEntry = reinterpret_cast<const FStageProviderMessage*>(Entries[EntryIndex]->Data->GetStructMemory());
		const double ThisFrameSeconds = ThisEntry->FrameTime.AsSeconds();
		if (ThisFrameSeconds <= NewFrameSeconds)
		{
			break;
		}
	}

	const int32 InsertIndex = EntryIndex + 1;
	Entries.Insert(NewEntry, InsertIndex);
}

void FStageMonitorSession::GetAllEntries(TArray<TSharedPtr<FStageDataEntry>>& OutEntries)
{
	OutEntries = Entries; 
}

bool FStageMonitorSession::IsStageInCriticalState() const
{
	return CriticalEventHandler->IsCriticalStateActive();
}

bool FStageMonitorSession::IsTimePartOfCriticalState(double TimeInSeconds) const
{
	return CriticalEventHandler->IsTimingPartOfCriticalRange(TimeInSeconds);
}

FName FStageMonitorSession::GetCurrentCriticalStateSource() const
{
	return CriticalEventHandler->GetCurrentCriticalStateSource();
}

TArray<FName> FStageMonitorSession::GetCriticalStateHistorySources() const
{
	return CriticalEventHandler->GetCriticalStateHistorySources();
}

TArray<FName> FStageMonitorSession::GetCriticalStateSources(double TimeInSeconds) const
{
	return CriticalEventHandler->GetCriticalStateSources(TimeInSeconds);
}

FString FStageMonitorSession::GetSessionName() const
{
	return SessionName;
}
