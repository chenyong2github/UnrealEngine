// Copyright Epic Games, Inc. All Rights Reserved.

#include "StageDataCollection.h"

#include "Misc/App.h"
#include "StageMessages.h"
#include "StageMonitorModule.h"


void FStageDataCollection::AddProvider(const FGuid& Identifier, const FStageInstanceDescriptor& Descriptor, const FMessageAddress& Address)
{
	//Adds this provider if it's not being monitored
	if (!Providers.ContainsByPredicate([Identifier](const FCollectionProviderEntry& Other) { return Other.Identifier == Identifier; }))
	{
		FCollectionProviderEntry NewProvider;
		NewProvider.Identifier = Identifier;
		NewProvider.Descriptor = Descriptor;
		NewProvider.Address = Address;
		Providers.Emplace(MoveTemp(NewProvider));

		OnStageDataProviderListChanged().Broadcast();
	}
}

void FStageDataCollection::UpdateProviderDescription(const FGuid& Identifier, const FStageInstanceDescriptor& NewDescriptor, const FMessageAddress& NewAddress)
{
	//Update this provider's information if we had it in our list
	FCollectionProviderEntry* Provider = Providers.FindByPredicate([Identifier](const FCollectionProviderEntry& Other){ return Other.Identifier == Identifier; });
	if (Provider)
	{
		Provider->Descriptor = NewDescriptor;
		Provider->Address = NewAddress;
	}
}

void FStageDataCollection::AddProviderMessage(UScriptStruct* Type, const FStageProviderMessage* MessageData)
{
	if (MessageData == nullptr)
	{
		return;
	}

	//Only process messages coming from registered machines
	FCollectionProviderEntry* Provider = Providers.FindByPredicate([MessageData](const FCollectionProviderEntry& Other) { return MessageData->Identifier == Other.Identifier; });
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

	//Add this message to message collection => Full entry list and per provider / data type list
	TSharedPtr<FStageDataEntry> NewEntry = MakeShared<FStageDataEntry>();
	NewEntry->MessageTime = FApp::GetCurrentTime();
	NewEntry->Data = MakeShared<FStructOnScope>(Type);
	Type->CopyScriptStruct(NewEntry->Data->GetStructMemory(), MessageData);

	UpdateProviderLatestEntry(MessageData->Identifier, Type, NewEntry);
	InsertNewEntry(NewEntry);
	
	//Let know listeners that new data was received
	OnStageDataCollectionNewDataReceived().Broadcast(NewEntry);
}

TArray<FMessageAddress> FStageDataCollection::GetProvidersAddress() const
{
	TArray<FMessageAddress> Addresses;
	for (const FCollectionProviderEntry& Entry : Providers)
	{
		if (Entry.State != EStageDataProviderState::Closed)
		{
			Addresses.Add(Entry.Address);
		}
	}

	return Addresses;
}

void FStageDataCollection::SetProviderState(const FGuid& Identifier, EStageDataProviderState State)
{
	if (FCollectionProviderEntry* Provider = Providers.FindByPredicate([Identifier](const FCollectionProviderEntry& Other) { return Other.Identifier == Identifier; }))
	{
		Provider->State = State;
	}
}

bool FStageDataCollection::GetProvider(const FGuid& Identifier, FCollectionProviderEntry& OutProviderEntry) const
{
	if (const FCollectionProviderEntry* Provider = Providers.FindByPredicate([Identifier](const FCollectionProviderEntry& Other) { return Other.Identifier == Identifier; }))
	{
		OutProviderEntry = *Provider;
		return true;
	}

	return false;
}

void FStageDataCollection::ClearAll()
{
	//Clear all entry list and per provider latest data
	Entries.Empty();
	ProviderLatestData.Empty();
	
	//Let know our listeners that we have just been cleared
	OnStageDataClearedDelegate.Broadcast();
}

TSharedPtr<FStageDataEntry> FStageDataCollection::GetLatest(const FGuid& Identifier, UScriptStruct* Type)
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

EStageDataProviderState FStageDataCollection::GetProviderState(const FGuid& Identifier)
{
	const FCollectionProviderEntry* Provider = Providers.FindByPredicate([Identifier](const FCollectionProviderEntry& Other) { return Other.Identifier == Identifier; });
	if (Provider)
	{
		return Provider->State;
	}
	
	//Unknown provider state defaults to closed
	return EStageDataProviderState::Closed;
}

void FStageDataCollection::UpdateProviderLatestEntry(const FGuid& Identifier, UScriptStruct* Type, TSharedPtr<FStageDataEntry> NewEntry)
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

void FStageDataCollection::InsertNewEntry(TSharedPtr<FStageDataEntry> NewEntry)
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

void FStageDataCollection::GetAllEntries(TArray<TSharedPtr<FStageDataEntry>>& OutEntries)
{
	OutEntries = Entries; 
}

