// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineAchievementsEOS.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemEOS.h"
#include "OnlineSubsystemEOSTypes.h"
#include "OnlineStatsEOS.h"
#include "UserManagerEOS.h"


#if WITH_EOS_SDK
#include "eos_achievements.h"

void FOnlineAchievementsEOS::WriteAchievements(const FUniqueNetId& PlayerId, FOnlineAchievementsWriteRef& WriteObject, const FOnAchievementsWrittenDelegate& Delegate)
{
	FUniqueNetIdEOSPtr NetId = MakeShared<FUniqueNetIdEOS>(PlayerId);
	TArray<FOnlineStatsUserUpdatedStats> StatsToWrite;

	FOnlineStatsUserUpdatedStats& UpdatedStats = StatsToWrite.Emplace_GetRef(NetId.ToSharedRef());
	for (const TPair<FName, FVariantData>& Stat : WriteObject->Properties)
	{
		UpdatedStats.Stats.Add(Stat.Key.ToString(), FOnlineStatUpdate(Stat.Value, FOnlineStatUpdate::EOnlineStatModificationType::Unknown));
	}

	EOSSubsystem->StatsInterfacePtr->UpdateStats(NetId.ToSharedRef(), StatsToWrite, FOnlineStatsUpdateStatsComplete());

	WriteObject->WriteState = EOnlineAsyncTaskState::Done;
	Delegate.ExecuteIfBound(PlayerId, true);
}

typedef TEOSCallback<EOS_Achievements_OnQueryPlayerAchievementsCompleteCallback, EOS_Achievements_OnQueryPlayerAchievementsCompleteCallbackInfo> FQueryProgressCallback;

void FOnlineAchievementsEOS::QueryAchievements(const FUniqueNetId& PlayerId, const FOnQueryAchievementsCompleteDelegate& Delegate)
{
	int32 LocalUserId = EOSSubsystem->UserManager->GetLocalUserNumFromUniqueNetId(PlayerId);
	if (LocalUserId < 0)
	{
		UE_LOG_ONLINE_ACHIEVEMENTS(Error, TEXT("Can't query achievement progress for non-local user (%)"), *PlayerId.ToString());
		Delegate.ExecuteIfBound(PlayerId, false);
		return;
	}

	EOS_Achievements_QueryPlayerAchievementsOptions Options = { };
	Options.ApiVersion = EOS_ACHIEVEMENTS_QUERYPLAYERACHIEVEMENTS_API_LATEST;
	Options.UserId = EOSSubsystem->UserManager->GetLocalProductUserId(LocalUserId);

	FQueryProgressCallback* CallbackObj = new FQueryProgressCallback();
	CallbackObj->CallbackLambda = [this, LambaPlayerId = FUniqueNetIdEOS(PlayerId), OnComplete = FOnQueryAchievementsCompleteDelegate(Delegate)](const EOS_Achievements_OnQueryPlayerAchievementsCompleteCallbackInfo* Data)
	{
		bool bWasSuccessful = Data->ResultCode == EOS_EResult::EOS_Success;
		if (bWasSuccessful)
		{
			TSharedPtr<TArray<FOnlineAchievement>> Cheevos = MakeShareable(new TArray<FOnlineAchievement>());
			CachedAchievementsMap.Add(LambaPlayerId.UniqueNetIdStr, Cheevos);

			int32 LocalUserNum = EOSSubsystem->UserManager->GetLocalUserNumFromUniqueNetId(LambaPlayerId);
			EOS_ProductUserId UserId = EOSSubsystem->UserManager->GetLocalProductUserId(LocalUserNum);

			EOS_Achievements_GetPlayerAchievementCountOptions CountOptions = { };
			CountOptions.ApiVersion = EOS_ACHIEVEMENTS_GETPLAYERACHIEVEMENTCOUNT_API_LATEST;
			CountOptions.UserId = UserId;
			uint32 Count = EOS_Achievements_GetPlayerAchievementCount(EOSSubsystem->AchievementsHandle, &CountOptions);

			EOS_Achievements_CopyPlayerAchievementByIndexOptions CopyOptions = { };
			CopyOptions.ApiVersion = EOS_ACHIEVEMENTS_COPYPLAYERACHIEVEMENTBYINDEX_API_LATEST;
			CopyOptions.UserId = UserId;

			for (uint32 Index = 0; Index < Count; Index++)
			{
				CopyOptions.AchievementIndex = Index;

				EOS_Achievements_PlayerAchievement* AchievementEOS = nullptr;
				EOS_EResult Result = EOS_Achievements_CopyPlayerAchievementByIndex(EOSSubsystem->AchievementsHandle, &CopyOptions, &AchievementEOS);
				if (Result == EOS_EResult::EOS_Success)
				{
					FOnlineAchievement* Achievement = new(*Cheevos) FOnlineAchievement();

					Achievement->Id = AchievementEOS->AchievementId;
					Achievement->Progress = AchievementEOS->Progress;

					EOS_Achievements_PlayerAchievement_Release(AchievementEOS);

					if (UE_BUILD_DEBUG)
					{
						UE_LOG_ONLINE_ACHIEVEMENTS(Log, TEXT("Achievement progress (%s)"), *Achievement->ToDebugString());
					}
				}
				else
				{
					UE_LOG_ONLINE_ACHIEVEMENTS(Error, TEXT("EOS_Achievements_CopyPlayerAchievementByIndex() failed with error code (%s)"), ANSI_TO_TCHAR(EOS_EResult_ToString(Result)));
				}
			}
		}
		else
		{
			UE_LOG_ONLINE_ACHIEVEMENTS(Error, TEXT("EOS_Achievements_QueryPlayerAchievements() failed with error code (%s)"), ANSI_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
		}
		OnComplete.ExecuteIfBound(LambaPlayerId, bWasSuccessful);
	};
	EOS_Achievements_QueryPlayerAchievements(EOSSubsystem->AchievementsHandle, &Options, CallbackObj, CallbackObj->GetCallbackPtr());
}

typedef TEOSCallback<EOS_Achievements_OnQueryDefinitionsCompleteCallback, EOS_Achievements_OnQueryDefinitionsCompleteCallbackInfo> FQueryDefinitionsCallback;

void FOnlineAchievementsEOS::QueryAchievementDescriptions(const FUniqueNetId& PlayerId, const FOnQueryAchievementsCompleteDelegate& Delegate)
{
	if (CachedAchievementDefinitions.Num())
	{
		UE_LOG_ONLINE_ACHIEVEMENTS(Warning, TEXT("Using cached achievement definitions"));
		Delegate.ExecuteIfBound(PlayerId, true);
		return;
	}

	int32 LocalUserId = EOSSubsystem->UserManager->GetLocalUserNumFromUniqueNetId(PlayerId);
	if (LocalUserId < 0)
	{
		UE_LOG_ONLINE_ACHIEVEMENTS(Error, TEXT("Can't query achievement definitions for non-local user (%)"), *PlayerId.ToString());
		Delegate.ExecuteIfBound(PlayerId, false);
		return;
	}

	EOS_Achievements_QueryDefinitionsOptions Options = { };
	Options.ApiVersion = EOS_ACHIEVEMENTS_QUERYDEFINITIONS_API_LATEST;
	Options.LocalUserId = EOSSubsystem->UserManager->GetLocalProductUserId(LocalUserId);

	FQueryDefinitionsCallback* CallbackObj = new FQueryDefinitionsCallback();
	CallbackObj->CallbackLambda = [this, LambaPlayerId = FUniqueNetIdEOS(PlayerId), OnComplete = FOnQueryAchievementsCompleteDelegate(Delegate)](const EOS_Achievements_OnQueryDefinitionsCompleteCallbackInfo* Data)
	{
		bool bWasSuccessful = Data->ResultCode == EOS_EResult::EOS_Success;
		if (bWasSuccessful)
		{
			EOS_Achievements_GetAchievementDefinitionCountOptions CountOptions = { };
			CountOptions.ApiVersion = EOS_ACHIEVEMENTS_GETACHIEVEMENTDEFINITIONCOUNT_API_LATEST;
			uint32 Count = EOS_Achievements_GetAchievementDefinitionCount(EOSSubsystem->AchievementsHandle, &CountOptions);

			EOS_Achievements_CopyAchievementDefinitionByIndexOptions CopyOptions = { };
			CopyOptions.ApiVersion = EOS_ACHIEVEMENTS_COPYDEFINITIONBYINDEX_API_LATEST;
			CachedAchievementDefinitions.Empty(Count);
			CachedAchievementDefinitionsMap.Empty();

			for (uint32 Index = 0; Index < Count; Index++)
			{
				CopyOptions.AchievementIndex = Index;
				EOS_Achievements_Definition* Definition = nullptr;

				EOS_EResult Result = EOS_Achievements_CopyAchievementDefinitionByIndex(EOSSubsystem->AchievementsHandle, &CopyOptions, &Definition);
				if (Result == EOS_EResult::EOS_Success)
				{
					FOnlineAchievementDesc* Desc = new(CachedAchievementDefinitions) FOnlineAchievementDesc();
					// Work around for the ID not being part of the description
					CachedAchievementDefinitionsMap.Add(Definition->AchievementId, Desc);

					Desc->Title = FText::FromString(Definition->DisplayName);
					Desc->LockedDesc = FText::FromString(Definition->LockedDescription);
					Desc->UnlockedDesc = FText::FromString(Definition->CompletionDescription);
					Desc->bIsHidden = Definition->bIsHidden == EOS_TRUE;

					EOS_Achievements_Definition_Release(Definition);

					if (UE_BUILD_DEBUG)
					{
						UE_LOG_ONLINE_ACHIEVEMENTS(Log, TEXT("Achievement desc is (%s)"), *Desc->ToDebugString());
					}
				}
				else
				{
					UE_LOG_ONLINE_ACHIEVEMENTS(Error, TEXT("EOS_Achievements_CopyAchievementDefinitionByIndex() failed with error code (%s)"), ANSI_TO_TCHAR(EOS_EResult_ToString(Result)));
				}
			}
		}
		else
		{
			UE_LOG_ONLINE_ACHIEVEMENTS(Error, TEXT("EOS_Achievements_QueryDefinitions() failed with error code (%s)"), ANSI_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
		}
		OnComplete.ExecuteIfBound(LambaPlayerId, bWasSuccessful);
	};
	EOS_Achievements_QueryDefinitions(EOSSubsystem->AchievementsHandle, &Options, CallbackObj, CallbackObj->GetCallbackPtr());
}

EOnlineCachedResult::Type FOnlineAchievementsEOS::GetCachedAchievement(const FUniqueNetId& PlayerId, const FString& AchievementId, FOnlineAchievement& OutAchievement)
{
	FUniqueNetIdEOS EOSID(PlayerId);
	if (CachedAchievementsMap.Contains(EOSID.UniqueNetIdStr))
	{
		const TArray<FOnlineAchievement>& Achievements = *CachedAchievementsMap[EOSID.UniqueNetIdStr];
		for (const FOnlineAchievement& Achievement : Achievements)
		{
			if (Achievement.Id == AchievementId)
			{
				OutAchievement = Achievement;
				return EOnlineCachedResult::Success;
			}
		}
	}
	return EOnlineCachedResult::NotFound;
}

EOnlineCachedResult::Type FOnlineAchievementsEOS::GetCachedAchievements(const FUniqueNetId& PlayerId, TArray<FOnlineAchievement>& OutAchievements)
{
	FUniqueNetIdEOS EOSID(PlayerId);
	if (CachedAchievementsMap.Contains(EOSID.UniqueNetIdStr))
	{
		OutAchievements = *CachedAchievementsMap[EOSID.UniqueNetIdStr];
		return EOnlineCachedResult::Success;
	}
	return EOnlineCachedResult::NotFound;
}

EOnlineCachedResult::Type FOnlineAchievementsEOS::GetCachedAchievementDescription(const FString& AchievementId, FOnlineAchievementDesc& OutAchievementDesc)
{
	if (CachedAchievementDefinitionsMap.Contains(AchievementId))
	{
		OutAchievementDesc = *CachedAchievementDefinitionsMap[AchievementId];
		return EOnlineCachedResult::Success;
	}
	return EOnlineCachedResult::NotFound;
}

#if !UE_BUILD_SHIPPING
bool FOnlineAchievementsEOS::ResetAchievements(const FUniqueNetId&)
{
	UE_LOG_ONLINE_ACHIEVEMENTS(Warning, TEXT("ResetAchievements() is not supported"));
	return false;
}
#endif

#endif
