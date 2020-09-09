// Copyright Epic Games, Inc. All Rights Reserved.

#include "IMagicLeapARPinFeature.h"
#include "Kismet/GameplayStatics.h"

const FString IMagicLeapARPinFeature::ContentBindingSaveGameSlotName(TEXT("MagicLeapARPinGlobalSave"));

IMagicLeapARPinFeature::IMagicLeapARPinFeature()
: ContentBindingSave(nullptr)
, ContentBindingSaveGameUserIndex(0)
, bSaveGameDataIsDirty(false)
, bSaveGameInProgress(false)
, bLoadGameInProgress(false)
{
	OnMagicLeapARPinUpdatedEvent.AddRaw(this, &IMagicLeapARPinFeature::OnARPinsUpdated);

	SaveGameDelegate.BindRaw(this, &IMagicLeapARPinFeature::OnSaveGameToSlot);
	LoadGameDelegate.BindRaw(this, &IMagicLeapARPinFeature::OnLoadGameFromSlot);
}

IMagicLeapARPinFeature::~IMagicLeapARPinFeature()
{
	if (ContentBindingSave != nullptr)
	{
		ContentBindingSave->RemoveFromRoot();
	}
}

void IMagicLeapARPinFeature::OnARPinsUpdated(const TArray<FGuid>& Added, const TArray<FGuid>& Updated, const TArray<FGuid>& Deleted)
{
	LoadBindingsFromDiskAsync();

	if (ContentBindingSave != nullptr)
	{
		for (const FGuid& PinId : Added)
		{
			const FMagicLeapARPinObjectIdList* ObjectIdList = ContentBindingSave->AllContentBindings.Find(PinId);
			if (ObjectIdList != nullptr)
			{
				OnMagicLeapContentBindingFoundMulti.Broadcast(PinId, ObjectIdList->ObjectIdList);
				BroadcastOnMagicLeapContentBindingFoundEvent(PinId, ObjectIdList->ObjectIdList);
			}
		}
	}
	else
	{
		// If we're waiting on the save game object to be loaded from disk, queue task to determime found pcfs when load is complete.
		if (PendingTasks.Enqueue(FQueueData()))
		{
			FQueueData* QueueData = PendingTasks.Peek();
			QueueData->Type = EQueueTaskType::Search;
			QueueData->Data.SetSubtype<FARPinDataCache>(FARPinDataCache());
			QueueData->Data.GetSubtype<FARPinDataCache>().Added = Added;
		}
	}
}

void IMagicLeapARPinFeature::AddContentBindingAsync(const FGuid& PinId, const FString& ObjectId)
{
	// Try load save game object, create a new object if one doesnt exist
	LoadBindingsFromDiskAsync(true);

	if (ContentBindingSave != nullptr)
	{
		FMagicLeapARPinObjectIdList& ObjectIdListContainer = ContentBindingSave->AllContentBindings.FindOrAdd(PinId);
		ObjectIdListContainer.ObjectIdList.Add(ObjectId);

		SaveBindingsToDiskAsync();
	}
	else
	{
		// If we're waiting on the save game object to be loaded from disk, queue task to add new binding
		QueueContentBindingOperation(EQueueTaskType::Add, PinId, ObjectId);
	}
}

void IMagicLeapARPinFeature::RemoveContentBindingAsync(const FGuid& PinId, const FString& ObjectId)
{
	LoadBindingsFromDiskAsync();

	if (ContentBindingSave != nullptr)
	{
		FMagicLeapARPinObjectIdList* ObjectIdListContainer = ContentBindingSave->AllContentBindings.Find(PinId);
		if (ObjectIdListContainer != nullptr)
		{
			ObjectIdListContainer->ObjectIdList.Remove(ObjectId);
			if (ObjectIdListContainer->ObjectIdList.Num() == 0)
			{
				ContentBindingSave->AllContentBindings.Remove(PinId);
			}

			SaveBindingsToDiskAsync();
		}
	}
	else
	{
		// If we're waiting on the save game object to be loaded from disk, queue task to remove a binding
		QueueContentBindingOperation(EQueueTaskType::Remove, PinId, ObjectId);
	}
}

void IMagicLeapARPinFeature::OnSaveGameToSlot(const FString& InSlotName, const int32 InUserIndex, bool bDataSaved)
{
	UE_CLOG(!bDataSaved, LogMagicLeapARPin, Error, TEXT("Error saving content bindings to disk."));

	bSaveGameInProgress = false;

	// If Add/RemoveContentBinding calls was made while an async save operation was going on,
	// we trigger another call to save the updated object.
	if (bSaveGameDataIsDirty)
	{
		bSaveGameDataIsDirty = false;
		bSaveGameInProgress = true;
		UGameplayStatics::AsyncSaveGameToSlot(ContentBindingSave, ContentBindingSaveGameSlotName, ContentBindingSaveGameUserIndex, SaveGameDelegate);
	}
}

void IMagicLeapARPinFeature::OnLoadGameFromSlot(const FString& InSlotName, const int32 InUserIndex, USaveGame* InSaveGameObj)
{
	if (InSaveGameObj != nullptr)
	{
		ContentBindingSave = Cast<UMagicLeapARPinContentBindings>(InSaveGameObj);
		if (ContentBindingSave != nullptr)
		{
			ContentBindingSave->AddToRoot();
		}
	}

	bLoadGameInProgress = false;

	if (ContentBindingSave != nullptr)
	{
		bool bNeedsSaving = false;

		// Go through the queue of all Add/RemoveContentBinding and OnARPinUpdated calls made while we were waiting for the save game object to load.
		FQueueData CurrentData;
		while (PendingTasks.Dequeue(CurrentData))
		{
			switch (CurrentData.Type)
			{
				case EQueueTaskType::Add:
				{
					FMagicLeapARPinObjectIdList& ObjectIdListContainer = ContentBindingSave->AllContentBindings.FindOrAdd(CurrentData.Data.GetSubtype<FSaveGameDataCache>().PinId);
					ObjectIdListContainer.ObjectIdList.Add(CurrentData.Data.GetSubtype<FSaveGameDataCache>().ObjectId);
					bNeedsSaving = true;
					break;
				}
				case EQueueTaskType::Remove:
				{
					FMagicLeapARPinObjectIdList* ObjectIdListContainer = ContentBindingSave->AllContentBindings.Find(CurrentData.Data.GetSubtype<FSaveGameDataCache>().PinId);
					if (ObjectIdListContainer != nullptr)
					{
						ObjectIdListContainer->ObjectIdList.Remove(CurrentData.Data.GetSubtype<FSaveGameDataCache>().ObjectId);
						if (ObjectIdListContainer->ObjectIdList.Num() == 0)
						{
							ContentBindingSave->AllContentBindings.Remove(CurrentData.Data.GetSubtype<FSaveGameDataCache>().PinId);
						}

						bNeedsSaving = true;
					}
					break;
				}
				case EQueueTaskType::Search:
				{
					// TODO : take care to remove the PCFs from the Added list that were removed while still waiting for the save game object to load.
					for (const FGuid& PinId : CurrentData.Data.GetSubtype<FARPinDataCache>().Added)
					{
						const FMagicLeapARPinObjectIdList* ObjectIdList = ContentBindingSave->AllContentBindings.Find(PinId);
						if (ObjectIdList != nullptr)
						{
							OnMagicLeapContentBindingFoundMulti.Broadcast(PinId, ObjectIdList->ObjectIdList);
							BroadcastOnMagicLeapContentBindingFoundEvent(PinId, ObjectIdList->ObjectIdList);
						}
					}

					break;
				}
			}
		}

		if (bNeedsSaving)
		{
			SaveBindingsToDiskAsync();
		}
	}
	else
	{
		PendingTasks.Empty();
	}
}

void IMagicLeapARPinFeature::LoadBindingsFromDiskAsync(bool bCreateIfNeeded)
{
	if (ContentBindingSave == nullptr)
	{
		if (UGameplayStatics::DoesSaveGameExist(ContentBindingSaveGameSlotName, ContentBindingSaveGameUserIndex))
		{
			if (!bLoadGameInProgress)
			{
				bLoadGameInProgress = true;
				UGameplayStatics::AsyncLoadGameFromSlot(ContentBindingSaveGameSlotName, ContentBindingSaveGameUserIndex, LoadGameDelegate);
			}
		}
		else if (bCreateIfNeeded)
		{
			ContentBindingSave = Cast<UMagicLeapARPinContentBindings>(UGameplayStatics::CreateSaveGameObject(UMagicLeapARPinContentBindings::StaticClass()));
			if (ContentBindingSave != nullptr)
			{
				ContentBindingSave->AddToRoot();
			}
		}
	}
}

void IMagicLeapARPinFeature::SaveBindingsToDiskAsync()
{
	// If there is no async save operation going on right now, trigger a new one.
	if (!bSaveGameInProgress)
	{
		bSaveGameDataIsDirty = false;
		bSaveGameInProgress = true;
		UGameplayStatics::AsyncSaveGameToSlot(ContentBindingSave, ContentBindingSaveGameSlotName, ContentBindingSaveGameUserIndex, SaveGameDelegate);
	}
	else
	{
		// If there is an async save operation going on, mark the state as dirty so when that operation completes, 
		// this state causes a new save operation to be triggered.
		// Since the save game object is serialized on the game thread and then copied by value to the worker thread,
		// we don't need to actually lock the ContentBindingSave object, nor do we need to queue every single call to Add/RemoveContentBinding
		// because in the end we just need to save the latest state of that object.
		// All functions in this class are called on the game thread, which also frees us from needing to use any mutex locks or atomic operations.
		bSaveGameDataIsDirty = true;
	}
}

void IMagicLeapARPinFeature::QueueContentBindingOperation(enum EQueueTaskType TaskType, const FGuid& PinId, const FString& ObjectId)
{
	if (PendingTasks.Enqueue(FQueueData()))
	{
		FQueueData* QueueData = PendingTasks.Peek();
		QueueData->Type = TaskType;
		QueueData->Data.SetSubtype<FSaveGameDataCache>(FSaveGameDataCache());
		QueueData->Data.GetSubtype<FSaveGameDataCache>().PinId = PinId;
		QueueData->Data.GetSubtype<FSaveGameDataCache>().ObjectId = ObjectId;
	}
}
