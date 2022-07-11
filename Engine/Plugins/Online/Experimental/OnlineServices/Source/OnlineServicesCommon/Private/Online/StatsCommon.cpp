// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/StatsCommon.h"

#include "Online/Auth.h"
#include "Online/OnlineServicesCommon.h"

namespace UE::Online {

const TCHAR* LexToString(EStatModifyMethod Value)
{
	switch (Value)
	{
	case EStatModifyMethod::Sum:
		return TEXT("Sum");
	case EStatModifyMethod::Largest:
		return TEXT("Largest");
	case EStatModifyMethod::Smallest:
		return TEXT("Smallest");
	default: checkNoEntry(); // Intentional fallthrough
	case EStatModifyMethod::Set:
		return TEXT("Set");
	}
}

void LexFromString(EStatModifyMethod& OutValue, const TCHAR* InStr)
{
	if (FCString::Stricmp(InStr, TEXT("Sum")) == 0)
	{
		OutValue = EStatModifyMethod::Sum;
	}
	else if (FCString::Stricmp(InStr, TEXT("Set")) == 0)
	{
		OutValue = EStatModifyMethod::Set;
	}
	else if (FCString::Stricmp(InStr, TEXT("Largest")) == 0)
	{
		OutValue = EStatModifyMethod::Largest;
	}
	else if (FCString::Stricmp(InStr, TEXT("Smallest")) == 0)
	{
		OutValue = EStatModifyMethod::Smallest;
	}
	else
	{
		ensureMsgf(false, TEXT("Can't convert %s to EStatModifyMethod"), InStr);
		OutValue = EStatModifyMethod::Set;
	}
}

const TCHAR* LexToString(EStatLeaderboardUpdateMethod Value)
{
	switch (Value)
	{
	case EStatLeaderboardUpdateMethod::Force:
		return TEXT("Force");
	default: checkNoEntry(); // Intentional fallthrough
	case EStatLeaderboardUpdateMethod::KeepBest:
		return TEXT("KeepBest");
	}
}

void LexFromString(EStatLeaderboardUpdateMethod& OutValue, const TCHAR* InStr)
{
	if (FCString::Stricmp(InStr, TEXT("KeepBest")) == 0)
	{
		OutValue = EStatLeaderboardUpdateMethod::KeepBest;
	}
	else if (FCString::Stricmp(InStr, TEXT("Force")) == 0)
	{
		OutValue = EStatLeaderboardUpdateMethod::Force;
	}
	else
	{
		ensureMsgf(false, TEXT("Can't convert %s to EStatLeaderboardUpdateMethod"), InStr);
		OutValue = EStatLeaderboardUpdateMethod::KeepBest;
	}
}

const TCHAR* LexToString(EStatLeaderboardOrderMethod Value)
{
	switch (Value)
	{
	case EStatLeaderboardOrderMethod::Ascending:
		return TEXT("Ascending");
	default: checkNoEntry(); // Intentional fallthrough
	case EStatLeaderboardOrderMethod::Descending:
		return TEXT("Descending");
	}
}

void LexFromString(EStatLeaderboardOrderMethod& OutValue, const TCHAR* InStr)
{
	if (FCString::Stricmp(InStr, TEXT("Ascending")) == 0)
	{
		OutValue = EStatLeaderboardOrderMethod::Ascending;
	}
	else if (FCString::Stricmp(InStr, TEXT("Descending")) == 0)
	{
		OutValue = EStatLeaderboardOrderMethod::Descending;
	}
	else
	{
		ensureMsgf(false, TEXT("Can't convert %s to EStatLeaderboardOrderMethod"), InStr);
		OutValue = EStatLeaderboardOrderMethod::Descending;
	}
}

const TCHAR* LexToString(EStatUsageFlags Value)
{
	switch (Value)
	{
	case EStatUsageFlags::Achievement:
		return TEXT("Achievement");
	case EStatUsageFlags::Leaderboard:
		return TEXT("Leaderboard");
	default: checkNoEntry(); // Intentional fallthrough
	case EStatUsageFlags::None:
		return TEXT("None");
	}
}

void LexFromString(EStatUsageFlags& OutValue, const TCHAR* InStr)
{
	if (FCString::Stricmp(InStr, TEXT("None")) == 0)
	{
		OutValue = EStatUsageFlags::None;
	}
	else if (FCString::Stricmp(InStr, TEXT("Achievement")) == 0)
	{
		OutValue = EStatUsageFlags::Achievement;
	}
	else if (FCString::Stricmp(InStr, TEXT("Leaderboard")) == 0)
	{
		OutValue = EStatUsageFlags::Leaderboard;
	}
	else
	{
		ensureMsgf(false, TEXT("Can't convert %s to EStatusageFlags"), InStr);
		OutValue = EStatUsageFlags::None;
	}
}

FStatsCommon::FStatsCommon(FOnlineServicesCommon& InServices)
	: TOnlineComponent(TEXT("Stats"), InServices)
{
}

void FStatsCommon::Initialize()
{
	ReadStatDefinitionsFromConfig();

	TOnlineComponent<IStats>::Initialize();
}

void FStatsCommon::ReadStatDefinitionsFromConfig()
{
	const TCHAR* ConfigSection = TEXT("OnlineServices.Stats");

	for (int StatIdx = 0;; StatIdx++)
	{
		FString StatName;
		GConfig->GetString(ConfigSection, *FString::Printf(TEXT("StatDef_%d_Name"), StatIdx), StatName, GEngineIni);
		if (StatName.IsEmpty())
		{
			break;
		}

		FStatDefinition& StatDefinition = StatDefinitions.Emplace(StatName);
		StatDefinition.Name = MoveTemp(StatName);

		GConfig->GetInt(ConfigSection, *FString::Printf(TEXT("StatDef_%d_Id"), StatIdx), StatDefinition.Id, GEngineIni);

		FText StatUsageFlagsStr;
		GConfig->GetText(ConfigSection, *FString::Printf(TEXT("StatDef_%d_UsageFlags"), StatIdx), StatUsageFlagsStr, GEngineIni);
		TArray<FString> StatUsageFlagStrArray;
		bool CullEmpty = true;
		StatUsageFlagsStr.ToString().ParseIntoArray(StatUsageFlagStrArray, TEXT(","), CullEmpty);
		for (const FString& StatUsageFlagStr : StatUsageFlagStrArray)
		{
			EStatUsageFlags StatUsageFlag = EStatUsageFlags::None;
			LexFromString(StatUsageFlag, *StatUsageFlagStr);
			StatDefinition.UsageFlags |= (uint32)StatUsageFlag;
		}

		FText StatModifyMethod;
		GConfig->GetText(ConfigSection, *FString::Printf(TEXT("StatDef_%d_ModifyMethod"), StatIdx), StatModifyMethod, GEngineIni);
		if (!StatModifyMethod.IsEmpty())
		{
			LexFromString(StatDefinition.ModifyMethod, *StatModifyMethod.ToString());
		}

		if (StatDefinition.UsageFlags & (uint32)EStatUsageFlags::Leaderboard)
		{
			FText StatLeaderboardUpdateMethod;
			GConfig->GetText(ConfigSection, *FString::Printf(TEXT("StatDef_%d_LeaderboardUpdateMethod"), StatIdx), StatLeaderboardUpdateMethod, GEngineIni);
			if (!StatLeaderboardUpdateMethod.IsEmpty())
			{
				LexFromString(StatDefinition.LeaderboardUpdateMethod, *StatLeaderboardUpdateMethod.ToString());
			}

			FText StatLeaderboardOrderMethod;
			GConfig->GetText(ConfigSection, *FString::Printf(TEXT("StatDef_%d_LeaderboardOrderMethod"), StatIdx), StatLeaderboardOrderMethod, GEngineIni);
			if (!StatLeaderboardOrderMethod.IsEmpty())
			{
				LexFromString(StatDefinition.LeaderboardOrderMethod, *StatLeaderboardOrderMethod.ToString());
			}
		}
	}
}

void FStatsCommon::RegisterCommands()
{
	TOnlineComponent<IStats>::RegisterCommands();

	RegisterCommand(&FStatsCommon::UpdateStats);
	RegisterCommand(&FStatsCommon::QueryStats);
	RegisterCommand(&FStatsCommon::BatchQueryStats);
#if !UE_BUILD_SHIPPING
	RegisterCommand(&FStatsCommon::ResetStats);
#endif // !UE_BUILD_SHIPPING
}

TOnlineAsyncOpHandle<FUpdateStats> FStatsCommon::UpdateStats(FUpdateStats::Params&& Params)
{
	TOnlineAsyncOpRef<FUpdateStats> Operation = GetOp<FUpdateStats>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineAsyncOpHandle<FQueryStats> FStatsCommon::QueryStats(FQueryStats::Params&& Params)
{
	TOnlineAsyncOpRef<FQueryStats> Operation = GetOp<FQueryStats>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineAsyncOpHandle<FBatchQueryStats> FStatsCommon::BatchQueryStats(FBatchQueryStats::Params&& Params)
{
	TOnlineAsyncOpRef<FBatchQueryStats> Operation = GetOp<FBatchQueryStats>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

#if !UE_BUILD_SHIPPING
TOnlineAsyncOpHandle<FResetStats> FStatsCommon::ResetStats(FResetStats::Params&& Params)
{
	TOnlineAsyncOpRef<FResetStats> Operation = GetOp<FResetStats>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}
#endif // !UE_BUILD_SHIPPING

/* UE::Online */ }
