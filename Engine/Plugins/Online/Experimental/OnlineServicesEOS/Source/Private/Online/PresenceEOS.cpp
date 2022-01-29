// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/PresenceEOS.h"
#include "Online/OnlineAsyncOp.h"
#include "Online/OnlineIdEOS.h"
#include "Online/OnlineServicesEOS.h"
#include "Online/OnlineServicesEOSTypes.h"
#include "Online/AuthEOS.h"

#include "eos_presence.h"

namespace UE::Online {

static inline EPresenceState ToEPresenceState(EOS_Presence_EStatus InStatus)
{
	switch (InStatus)
	{
		case EOS_Presence_EStatus::EOS_PS_Online:
		{
			return EPresenceState::Online;
		}
		case EOS_Presence_EStatus::EOS_PS_Away:
		{
			return EPresenceState::Away;
		}
		case EOS_Presence_EStatus::EOS_PS_ExtendedAway:
		{
			return EPresenceState::ExtendedAway;
		}
		case EOS_Presence_EStatus::EOS_PS_DoNotDisturb:
		{
			return EPresenceState::DoNotDisturb;
		}
	}
	return EPresenceState::Offline;
}

static inline EOS_Presence_EStatus ToEOS_Presence_EStatus(EPresenceState InState)
{
	switch (InState)
	{
	case EPresenceState::Online:
	{
		return EOS_Presence_EStatus::EOS_PS_Online;
	}
	case EPresenceState::Away:
	{
		return EOS_Presence_EStatus::EOS_PS_Away;
	}
	case EPresenceState::ExtendedAway:
	{
		return EOS_Presence_EStatus::EOS_PS_ExtendedAway;
	}
	case EPresenceState::DoNotDisturb:
	{
		return EOS_Presence_EStatus::EOS_PS_DoNotDisturb;
	}
	}
	return EOS_Presence_EStatus::EOS_PS_Offline;
}

FPresenceEOS::FPresenceEOS(FOnlineServicesEOS& InServices)
	: FPresenceCommon(InServices)
{
}

void FPresenceEOS::Initialize()
{
	FPresenceCommon::Initialize();

	PresenceHandle = EOS_Platform_GetPresenceInterface(static_cast<FOnlineServicesEOS&>(GetServices()).GetEOSPlatformHandle());
	check(PresenceHandle != nullptr);

	// Register for friend updates
	EOS_Presence_AddNotifyOnPresenceChangedOptions Options = { };
	Options.ApiVersion = EOS_PRESENCE_ADDNOTIFYONPRESENCECHANGED_API_LATEST;
	NotifyPresenceChangedNotificationId = EOS_Presence_AddNotifyOnPresenceChanged(PresenceHandle, &Options, this, [](const EOS_Presence_PresenceChangedCallbackInfo* Data)
	{
		FPresenceEOS* This = reinterpret_cast<FPresenceEOS*>(Data->ClientData);
		const FOnlineAccountIdHandle LocalUserId = FindAccountIdChecked(Data->LocalUserId);

		This->Services.Get<FAuthEOS>()->ResolveAccountId(LocalUserId, Data->PresenceUserId)
		.Next([This, LocalUserId](const FOnlineAccountIdHandle& PresenceUserId)
		{
			UE_LOG(LogTemp, Warning, TEXT("OnEOSPresenceUpdate: LocalUserId=[%s] PresenceUserId=[%s]"), *ToLogString(LocalUserId), *ToLogString(PresenceUserId));
			This->UpdateUserPresence(LocalUserId, PresenceUserId);
		});
	});
}

void FPresenceEOS::PreShutdown()
{
	EOS_Presence_RemoveNotifyOnPresenceChanged(PresenceHandle, NotifyPresenceChangedNotificationId);
}

TOnlineAsyncOpHandle<FQueryPresence> FPresenceEOS::QueryPresence(FQueryPresence::Params&& InParams)
{
	TOnlineAsyncOpRef<FQueryPresence> Op = GetJoinableOp<FQueryPresence>(MoveTemp(InParams));
	if (!Op->IsReady())
	{
		// Initialize
		const FQueryPresence::Params& Params = Op->GetParams();
		if (!Services.Get<FAuthEOS>()->IsLoggedIn(Params.LocalUserId))
		{
			// TODO: Error codes
			Op->SetError(Errors::Unknown());
			return Op->GetHandle();
		}
		EOS_EpicAccountId TargetUserEasId = GetEpicAccountId(Params.TargetUserId);
		if (!EOS_EpicAccountId_IsValid(TargetUserEasId))
		{
			// TODO: Error codes
			Op->SetError(Errors::Unknown());
			return Op->GetHandle();
		}

		// TODO:  If we try to query a local user's presence, is that an error, should we return the cached state, should we still ask EOS?
		const bool bIsLocalUser = Services.Get<FAuthEOS>()->IsLoggedIn(Params.TargetUserId);
		if (bIsLocalUser)
		{
			Op->SetError(Errors::Unknown()); // TODO: Error codes
			return Op->GetHandle();
		}

		Op->Then([this](TOnlineAsyncOp<FQueryPresence>& InAsyncOp) mutable
			{
				const FQueryPresence::Params& Params = InAsyncOp.GetParams();
				EOS_Presence_QueryPresenceOptions QueryPresenceOptions = { };
				QueryPresenceOptions.ApiVersion = EOS_PRESENCE_QUERYPRESENCE_API_LATEST;
				QueryPresenceOptions.LocalUserId = GetEpicAccountIdChecked(Params.LocalUserId);
				QueryPresenceOptions.TargetUserId = GetEpicAccountIdChecked(Params.TargetUserId);

				return EOS_Async<EOS_Presence_QueryPresenceCallbackInfo>(EOS_Presence_QueryPresence, PresenceHandle, QueryPresenceOptions);
			})
			.Then([this](TOnlineAsyncOp<FQueryPresence>& InAsyncOp, const EOS_Presence_QueryPresenceCallbackInfo* Data) mutable
			{
				UE_LOG(LogTemp, Warning, TEXT("QueryPresenceResult: [%s]"), *LexToString(Data->ResultCode));

				if (Data->ResultCode == EOS_EResult::EOS_Success)
				{
					const FQueryPresence::Params& Params = InAsyncOp.GetParams();
					UpdateUserPresence(Params.LocalUserId, Params.TargetUserId);
					FQueryPresence::Result Result = { FindOrCreatePresence(Params.LocalUserId, Params.TargetUserId) };
					InAsyncOp.SetResult(MoveTemp(Result));
				}
				else
				{
					InAsyncOp.SetError(Errors::Unknown()); // TODO: Error codes
				}
			})
			.Enqueue(GetSerialQueue());
	}
	return Op->GetHandle();
}

TOnlineResult<FGetPresence> FPresenceEOS::GetPresence(FGetPresence::Params&& Params)
{
	if (TMap<FOnlineAccountIdHandle, TSharedRef<FUserPresence>>* PresenceList = PresenceLists.Find(Params.LocalUserId))
	{
		TSharedRef<FUserPresence>* PresencePtr = PresenceList->Find(Params.TargetUserId);
		if (PresencePtr)
		{
			FGetPresence::Result Result = { *PresencePtr };
			return TOnlineResult<FGetPresence>(MoveTemp(Result));
		}
	}
	return TOnlineResult<FGetPresence>(Errors::Unknown()); // TODO: error codes
}

TOnlineAsyncOpHandle<FUpdatePresence> FPresenceEOS::UpdatePresence(FUpdatePresence::Params&& InParams)
{
	// TODO: Validate params
	// EOS_PRESENCE_DATA_MAX_KEYS - number of total keys.  Compare proposed with existing with pending ops?  Include removed keys!
	// EOS_PRESENCE_DATA_MAX_KEY_LENGTH - length of each key.  Compare updated with this.
	// EOS_PRESENCE_DATA_MAX_VALUE_LENGTH - length of each value.  Compare updated with this.
	// EOS_PRESENCE_RICH_TEXT_MAX_VALUE_LENGTH - length of status. Compare updated with this.

	TOnlineAsyncOpRef<FUpdatePresence> Op = GetMergeableOp<FUpdatePresence>(MoveTemp(InParams));
	if (!Op->IsReady())
	{
		// Initialize
		const FUpdatePresence::Params& Params = Op->GetParams();
		if (!Services.Get<FAuthEOS>()->IsLoggedIn(Params.LocalUserId))
		{
			// TODO: Error codes
			Op->SetError(Errors::Unknown());
			return Op->GetHandle();
		}

		// Don't cache anything from Params as they could be modified by another merge in the meanwhile.
		Op->Then([this](TOnlineAsyncOp<FUpdatePresence>& InAsyncOp) mutable
			{
				const FUpdatePresence::Params& Params = InAsyncOp.GetParams();
				EOS_HPresenceModification ChangeHandle = nullptr;
				EOS_Presence_CreatePresenceModificationOptions Options = { };
				Options.ApiVersion = EOS_PRESENCE_CREATEPRESENCEMODIFICATION_API_LATEST;
				Options.LocalUserId = GetEpicAccountIdChecked(Params.LocalUserId);
				EOS_EResult CreatePresenceModificationResult = EOS_Presence_CreatePresenceModification(PresenceHandle, &Options, &ChangeHandle);
				if (CreatePresenceModificationResult == EOS_EResult::EOS_Success)
				{
					check(ChangeHandle != nullptr);

					// State
					if (Params.Mutations.State.IsSet())
					{
						EOS_PresenceModification_SetStatusOptions StatusOptions = { };
						StatusOptions.ApiVersion = EOS_PRESENCEMODIFICATION_SETSTATUS_API_LATEST;
						StatusOptions.Status = ToEOS_Presence_EStatus(Params.Mutations.State.GetValue());
						EOS_EResult SetStatusResult = EOS_PresenceModification_SetStatus(ChangeHandle, &StatusOptions);
						if (SetStatusResult != EOS_EResult::EOS_Success)
						{
							UE_LOG(LogTemp, Warning, TEXT("UpdatePresence: EOS_PresenceModification_SetStatus failed with result %s"), *LexToString(SetStatusResult));
							InAsyncOp.SetError(Errors::Unknown());
							return MakeFulfilledPromise<const EOS_Presence_SetPresenceCallbackInfo*>(nullptr).GetFuture();
						}
						else
						{
							UE_LOG(LogTemp, Warning, TEXT("UpdatePresence: Status set to %u"), (uint8)Params.Mutations.State.GetValue()); // Temp logging
						}
					}

					// Raw rich text
					if (Params.Mutations.StatusString.IsSet())
					{
						// Convert the status string as the rich text string
						EOS_PresenceModification_SetRawRichTextOptions RawRichTextOptions = { };
						RawRichTextOptions.ApiVersion = EOS_PRESENCEMODIFICATION_SETRAWRICHTEXT_API_LATEST;

						FTCHARToUTF8 Utf8RawRichText(*Params.Mutations.StatusString.GetValue());
						RawRichTextOptions.RichText = Utf8RawRichText.Get();

						EOS_EResult SetRawRichTextResult = EOS_PresenceModification_SetRawRichText(ChangeHandle, &RawRichTextOptions);
						if (SetRawRichTextResult != EOS_EResult::EOS_Success)
						{
							UE_LOG(LogTemp, Warning, TEXT("UpdatePresence: EOS_PresenceModification_SetRawRichText failed with result %s"), *LexToString(SetRawRichTextResult));
							InAsyncOp.SetError(Errors::Unknown());
							return MakeFulfilledPromise<const EOS_Presence_SetPresenceCallbackInfo*>(nullptr).GetFuture();
						}
						else
						{
							UE_LOG(LogTemp, Warning, TEXT("UpdatePresence: RichText set to %s"), *Params.Mutations.StatusString.GetValue()); // Temp logging
						}
					}

					// Removed fields
					if (Params.Mutations.RemovedProperties.Num() > 0)
					{
						// EOS_PresenceModification_DeleteData
						TArray<FTCHARToUTF8, TInlineAllocator<EOS_PRESENCE_DATA_MAX_KEYS>> Utf8Strings;
						TArray<EOS_PresenceModification_DataRecordId, TInlineAllocator<EOS_PRESENCE_DATA_MAX_KEYS>> RecordIds;
						int32 CurrentIndex = 0;
						for (const FString& RemovedProperty : Params.Mutations.RemovedProperties)
						{
							const FTCHARToUTF8& Utf8Key = Utf8Strings.Emplace_GetRef(RemovedProperty);

							EOS_PresenceModification_DataRecordId& RecordId = RecordIds.Emplace_GetRef();
							RecordId.ApiVersion = EOS_PRESENCEMODIFICATION_DATARECORDID_API_LATEST;
							RecordId.Key = Utf8Key.Get();

							UE_LOG(LogTemp, Warning, TEXT("UpdatePresence: Removing field %s"), *RemovedProperty); // Temp logging
						}

						EOS_PresenceModification_DeleteDataOptions DataOptions = { };
						DataOptions.ApiVersion = EOS_PRESENCEMODIFICATION_DELETEDATA_API_LATEST;
						DataOptions.RecordsCount = RecordIds.Num();
						DataOptions.Records = RecordIds.GetData();
						EOS_EResult DeleteDataResult = EOS_PresenceModification_DeleteData(ChangeHandle, &DataOptions);
						if (DeleteDataResult != EOS_EResult::EOS_Success)
						{
							UE_LOG(LogTemp, Warning, TEXT("UpdatePresence: EOS_PresenceModification_DeleteDataOptions failed with result %s"), *LexToString(DeleteDataResult));
							InAsyncOp.SetError(Errors::Unknown());
							return MakeFulfilledPromise<const EOS_Presence_SetPresenceCallbackInfo*>(nullptr).GetFuture();
						}
					}

					// Added/Updated fields
					if (Params.Mutations.UpdatedProperties.Num() > 0)
					{
						if (Params.Mutations.UpdatedProperties.Num() > EOS_PRESENCE_DATA_MAX_KEYS)
						{
							// TODO: Move this check higher.  Needs to take into account number of present fields (not just ones updated) and removed fields.
							UE_LOG(LogTemp, Warning, TEXT("UpdatePresence: Too many presence keys.  %u/%u"), Params.Mutations.UpdatedProperties.Num(), EOS_PRESENCE_DATA_MAX_KEYS);
							InAsyncOp.SetError(Errors::Unknown());
							return MakeFulfilledPromise<const EOS_Presence_SetPresenceCallbackInfo*>(nullptr).GetFuture();
						}
						TArray<FTCHARToUTF8, TInlineAllocator<EOS_PRESENCE_DATA_MAX_KEYS * 2>> Utf8Strings;
						TArray<EOS_Presence_DataRecord, TInlineAllocator<EOS_PRESENCE_DATA_MAX_KEYS>> Records;
						int32 CurrentIndex = 0;
						for (const TPair<FString, FPresenceVariant>& UpdatedProperty : Params.Mutations.UpdatedProperties)
						{
							const FTCHARToUTF8& Utf8Key = Utf8Strings.Emplace_GetRef(UpdatedProperty.Key);
							const FTCHARToUTF8& Utf8Value = Utf8Strings.Emplace_GetRef(UpdatedProperty.Value); // TODO: Better serialization

							EOS_Presence_DataRecord& Record = Records.Emplace_GetRef();
							Record.ApiVersion = EOS_PRESENCE_DATARECORD_API_LATEST;
							Record.Key = Utf8Key.Get();
							Record.Value = Utf8Value.Get();
							UE_LOG(LogTemp, Warning, TEXT("UpdatePresence: Set field [%s] to [%s]"), *UpdatedProperty.Key, *UpdatedProperty.Value); // Temp logging
						}

						EOS_PresenceModification_SetDataOptions DataOptions = { };
						DataOptions.ApiVersion = EOS_PRESENCEMODIFICATION_SETDATA_API_LATEST;
						DataOptions.RecordsCount = Records.Num();
						DataOptions.Records = Records.GetData();
						EOS_EResult SetDataResult = EOS_PresenceModification_SetData(ChangeHandle, &DataOptions);
						if (SetDataResult != EOS_EResult::EOS_Success)
						{
							UE_LOG(LogTemp, Warning, TEXT("UpdatePresence: EOS_PresenceModification_SetData failed with result %s"), *LexToString(SetDataResult));
							InAsyncOp.SetError(Errors::Unknown());
							return MakeFulfilledPromise<const EOS_Presence_SetPresenceCallbackInfo*>(nullptr).GetFuture();
						}
					}

					EOS_Presence_SetPresenceOptions SetPresenceOptions = { };
					SetPresenceOptions.ApiVersion = EOS_PRESENCE_SETPRESENCE_API_LATEST;
					SetPresenceOptions.LocalUserId = GetEpicAccountIdChecked(Params.LocalUserId);
					SetPresenceOptions.PresenceModificationHandle = ChangeHandle;
					TFuture<const EOS_Presence_SetPresenceCallbackInfo*> EOSAsyncFuture = EOS_Async<EOS_Presence_SetPresenceCallbackInfo>(EOS_Presence_SetPresence, PresenceHandle, SetPresenceOptions);
					EOS_PresenceModification_Release(ChangeHandle);
					return EOSAsyncFuture;
				}
				else
				{
					// TODO:  Error code
					InAsyncOp.SetError(Errors::Unknown());
					return MakeFulfilledPromise<const EOS_Presence_SetPresenceCallbackInfo*>(nullptr).GetFuture();
				}
			})
			.Then([this](TOnlineAsyncOp<FUpdatePresence>& InAsyncOp, const EOS_Presence_SetPresenceCallbackInfo* Data) mutable
			{
				UE_LOG(LogTemp, Warning, TEXT("SetPresenceResult: [%s]"), *LexToString(Data->ResultCode));

				if (Data->ResultCode == EOS_EResult::EOS_Success)
				{
					// Update local presence
					const FUpdatePresence::Params& Params = InAsyncOp.GetParams();
					TSharedRef<FUserPresence> LocalUserPresence = FindOrCreatePresence(Params.LocalUserId, Params.LocalUserId);
					if (Params.Mutations.State.IsSet())
					{
						LocalUserPresence->State = Params.Mutations.State.GetValue();
					}
					if (Params.Mutations.StatusString.IsSet())
					{
						LocalUserPresence->StatusString = Params.Mutations.StatusString.GetValue();
					}
					for (const FString& RemovedKey : Params.Mutations.RemovedProperties)
					{
						LocalUserPresence->Properties.Remove(RemovedKey);
					}
					for (const TPair<FString, FPresenceVariant>& UpdatedProperty : Params.Mutations.UpdatedProperties)
					{
						LocalUserPresence->Properties.Emplace(UpdatedProperty.Key, UpdatedProperty.Value);
					}

					InAsyncOp.SetResult(FUpdatePresence::Result());
				}
				else
				{
					InAsyncOp.SetError(Errors::Unknown()); // TODO: Error codes
				}
			})
			.Enqueue(GetSerialQueue());
	}
	return Op->GetHandle();
}

/** Get a user's presence, creating entries if missing */
TSharedRef<FUserPresence> FPresenceEOS::FindOrCreatePresence(FOnlineAccountIdHandle LocalUserId, FOnlineAccountIdHandle PresenceUserId)
{
	TMap<FOnlineAccountIdHandle, TSharedRef<FUserPresence>>& LocalUserPresenceList = PresenceLists.FindOrAdd(LocalUserId);
	if (const TSharedRef<FUserPresence>* const ExistingPresence = LocalUserPresenceList.Find(PresenceUserId))
	{
		return *ExistingPresence;
	}

	TSharedRef<FUserPresence> UserPresence = MakeShared<FUserPresence>();
	UserPresence->UserId = PresenceUserId;
	LocalUserPresenceList.Emplace(PresenceUserId, UserPresence);
	return UserPresence;
}

void FPresenceEOS::UpdateUserPresence(FOnlineAccountIdHandle LocalUserId, FOnlineAccountIdHandle PresenceUserId)
{
	bool bPresenceHasChanged = false;
	TSharedRef<FUserPresence> UserPresence = FindOrCreatePresence(LocalUserId, PresenceUserId);
	// TODO:  Handle updates for local users.  Don't want to conflict with UpdatePresence calls

	// Get presence from EOS
	EOS_Presence_Info* PresenceInfo = nullptr;
	EOS_Presence_CopyPresenceOptions Options = { };
	Options.ApiVersion = EOS_PRESENCE_COPYPRESENCE_API_LATEST;
	Options.LocalUserId = GetEpicAccountIdChecked(LocalUserId);
	Options.TargetUserId = GetEpicAccountIdChecked(PresenceUserId);
	EOS_EResult CopyPresenceResult = EOS_Presence_CopyPresence(PresenceHandle, &Options, &PresenceInfo);
	if (CopyPresenceResult == EOS_EResult::EOS_Success)
	{
		// Convert the presence data to our format
		EPresenceState NewPresenceState = ToEPresenceState(PresenceInfo->Status);
		if (UserPresence->State != NewPresenceState)
		{
			bPresenceHasChanged = true;
			UserPresence->State = NewPresenceState;
		}

		FString NewStatusString = UTF8_TO_TCHAR(PresenceInfo->RichText);
		if (UserPresence->StatusString != NewStatusString)
		{
			bPresenceHasChanged = true;
			UserPresence->StatusString = MoveTemp(NewStatusString);
		}

		if (PresenceInfo->Records)
		{
			// TODO:  Handle Properties that aren't replicated through presence (eg "ProductId")
			TArrayView<const EOS_Presence_DataRecord> Records(PresenceInfo->Records, PresenceInfo->RecordsCount);
			// Detect removals
			TArray<FString, TInlineAllocator<EOS_PRESENCE_DATA_MAX_KEYS>> RemovedKeys;
			UserPresence->Properties.GenerateKeyArray(RemovedKeys);
			for (const EOS_Presence_DataRecord& Record : Records)
			{
				FString RecordKey = UTF8_TO_TCHAR(Record.Key);
				FString RecordValue = UTF8_TO_TCHAR(Record.Value);
				RemovedKeys.Remove(RecordKey);
				if (FString* ExistingValue = UserPresence->Properties.Find(RecordKey))
				{
					if (*ExistingValue == RecordValue)
					{
						continue; // No change
					}
				}
				bPresenceHasChanged = true;
				UserPresence->Properties.Add(MoveTemp(RecordKey), MoveTemp(RecordValue));
			}
			// Any fields that have been removed
			if (RemovedKeys.Num() > 0)
			{
				bPresenceHasChanged = true;
				for (const FString& RemovedKey : RemovedKeys)
				{
					UserPresence->Properties.Remove(RemovedKey);
				}
			}
		}
		else if (UserPresence->Properties.Num() > 0)
		{
			bPresenceHasChanged = true;
			UserPresence->Properties.Reset();
		}
		EOS_Presence_Info_Release(PresenceInfo);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("UpdateUserPresence: CopyPresence Failed %s"), *LexToString(CopyPresenceResult));
	}

	if (bPresenceHasChanged)
	{
		FPresenceUpdated PresenceUpdatedParams = { LocalUserId, UserPresence };
		OnPresenceUpdatedEvent.Broadcast(PresenceUpdatedParams);
	}
}

/* UE::Online */ }
