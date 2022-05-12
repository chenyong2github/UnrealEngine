// Copyright Epic Games, Inc. All Rights Reserved.

#include "SaveGameSystem.h"
#include "HAL/PlatformMisc.h"
#include "Containers/Ticker.h"


UE::Tasks::FPipe ISaveGameSystem::AsyncTaskPipe{ TEXT("SaveGamePipe") };



void ISaveGameSystem::DoesSaveGameExistAsync(const TCHAR* Name, FPlatformUserId PlatformUserId, FSaveGameAsyncExistsCallback Callback)
{
	FString SlotName(Name);

	// start the save operation on a background thread.
	AsyncTaskPipe.Launch(UE_SOURCE_LOCATION,
		[this, SlotName, PlatformUserId, Callback]()
		{
			// check if the savegame exists
			int32 UserIndex = FPlatformMisc::GetUserIndexForPlatformUser(PlatformUserId);
			const ESaveExistsResult Result = DoesSaveGameExistWithResult(*SlotName, UserIndex);

			// trigger the callback on the game thread.
			if (Callback)
			{
				OnAsyncComplete(
					[SlotName, PlatformUserId, Result, Callback]()
					{
						Callback(SlotName, PlatformUserId, Result);
					}
				);
			}
		}
	);
}


void ISaveGameSystem::SaveGameAsync(bool bAttemptToUseUI, const TCHAR* Name, FPlatformUserId PlatformUserId, TSharedRef<const TArray<uint8>> Data, FSaveGameAsyncOpCompleteCallback Callback)
{
	FString SlotName(Name);

	// start the save operation on a background thread.
	AsyncTaskPipe.Launch(UE_SOURCE_LOCATION,
		[this, bAttemptToUseUI, SlotName, PlatformUserId, Data, Callback]()
		{
			// save the savegame
			int32 UserIndex = FPlatformMisc::GetUserIndexForPlatformUser(PlatformUserId);
			const bool bResult = SaveGame(bAttemptToUseUI, *SlotName, UserIndex, *Data);

			// trigger the callback on the game thread
			if (Callback)
			{
				OnAsyncComplete(
					[SlotName, PlatformUserId, bResult, Callback]()
					{
						Callback(SlotName, PlatformUserId, bResult);
					}
				);
			}
		}
	);
}


void ISaveGameSystem::LoadGameAsync(bool bAttemptToUseUI, const TCHAR* Name, FPlatformUserId PlatformUserId, FSaveGameAsyncLoadCompleteCallback Callback)
{
	FString SlotName(Name);

	// start the load operation on a background thread.
	AsyncTaskPipe.Launch(UE_SOURCE_LOCATION,
		[this, bAttemptToUseUI, SlotName, PlatformUserId, Callback]()
		{
			// load the savegame
			TSharedRef<TArray<uint8>> Data = MakeShared<TArray<uint8>>();
			int32 UserIndex = FPlatformMisc::GetUserIndexForPlatformUser(PlatformUserId);
			const bool bResult = LoadGame(bAttemptToUseUI, *SlotName, UserIndex, Data.Get());

			// trigger the callback on the game thread
			if (Callback)
			{
				OnAsyncComplete(
					[SlotName, PlatformUserId, bResult, Callback, Data]()
					{
						Callback(SlotName, PlatformUserId, bResult, Data.Get());
					}
				);
			}
		}
	);
}


void ISaveGameSystem::DeleteGameAsync(bool bAttemptToUseUI, const TCHAR* Name, FPlatformUserId PlatformUserId, FSaveGameAsyncOpCompleteCallback Callback)
{
	FString SlotName(Name);

	// start the delete operation on a background thread.
	AsyncTaskPipe.Launch(UE_SOURCE_LOCATION,
		[this, bAttemptToUseUI, SlotName, PlatformUserId, Callback]()
		{
			// delete the savegame
			int32 UserIndex = FPlatformMisc::GetUserIndexForPlatformUser(PlatformUserId);
			const bool bResult = DeleteGame(bAttemptToUseUI, *SlotName, UserIndex);

			// trigger the callback on the game thread
			if (Callback)
			{
				OnAsyncComplete(
					[SlotName, PlatformUserId, bResult, Callback]()
					{
						Callback(SlotName, PlatformUserId, bResult);
					}
				);
			}
		}
	);
}

void ISaveGameSystem::GetSaveGameNamesAsync(FPlatformUserId PlatformUserId, FSaveGameAsyncGetNamesCallback Callback)
{
	// start the delete operation on a background thread.
	AsyncTaskPipe.Launch(UE_SOURCE_LOCATION,
		[this, PlatformUserId, Callback]()
		{
			// get the list of savegames
			TArray<FString> FoundSaves;
			int32 UserIndex = FPlatformMisc::GetUserIndexForPlatformUser(PlatformUserId);
			const bool bResult = GetSaveGameNames(FoundSaves, UserIndex);

			// trigger the callback on the game thread
			if (Callback)
			{
				OnAsyncComplete(
					[PlatformUserId, bResult, FoundSaves = MoveTemp(FoundSaves), Callback]()
					{
						Callback(PlatformUserId, bResult, FoundSaves);
					}
				);
			}
		}
	);

}


void ISaveGameSystem::InitAsync(bool bAttemptToUseUI, FPlatformUserId PlatformUserId, FSaveGameAsyncInitCompleteCallback Callback)
{
	// default implementation does nothing, so just trigger the completion callback on the game thread immediately
	if (Callback)
	{
		OnAsyncComplete(
			[PlatformUserId, Callback]()
			{
				Callback(PlatformUserId, true);
			}
		);
	}
}


void ISaveGameSystem::OnAsyncComplete(TFunction<void()> Callback)
{
	// NB. Using Ticker because AsyncTask may run during async package loading which may not be suitable for save data
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[Callback = MoveTemp(Callback)](float) -> bool
		{
			Callback();
			return false;
		}
	));
}
