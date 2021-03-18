// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/TestStatsInterface.h"
#include "OnlineSubsystemUtils.h"
#include "Interfaces/OnlineIdentityInterface.h"

#if WITH_DEV_AUTOMATION_TESTS
//
///**
// *	Example of a Stats write object
// */
//class TestStatsWrite : public FOnlineStatsWrite
//{
//public:
//	TestStatsWrite()
//	{
//		// Default properties
//		new (StatsNames) FName(TEXT("TestStats"));
//		RatedStat = "TestIntStat1";
//		DisplayFormat = EStatsFormat::Number;
//		SortMethod = EStatsSort::Descending;
//		UpdateMethod = EStatsUpdateMethod::KeepBest;
//	}
//};
//
///**
// *	Example of a Stats read object
// */
//class TestStatsRead : public FOnlineStatsRead
//{
//public:
//	TestStatsRead(const FString& InStatsName, const FString& InSortedColumn, const TMap<FString, EOnlineKeyValuePairDataType::Type>& InColumns)
//	{
//		StatsName = FName(InStatsName);
//		SortedColumn = FName(InSortedColumn);
//
//		for (TPair<FString, EOnlineKeyValuePairDataType::Type> Column : InColumns)
//		{
//			new (ColumnMetadata) FColumnMetaData(FName(Column.Key), Column.Value);
//		}
//	}
//};

FTestStatsInterface::FTestStatsInterface(const FString& InSubsystem) :
	Subsystem(InSubsystem),
	bOverallSuccess(true),
	Stats(NULL),
	TestPhase(0),
	LastTestPhase(-1)
{
	//// Define delegates
	//StatsFlushDelegate = FOnStatsFlushCompleteDelegate::CreateRaw(this, &FTestStatsInterface::OnStatsFlushComplete);
	//StatsReadCompleteDelegate = FOnStatsReadCompleteDelegate::CreateRaw(this, &FTestStatsInterface::OnStatsReadComplete);
	//StatsReadRankCompleteDelegate = FOnStatsReadCompleteDelegate::CreateRaw(this, &FTestStatsInterface::OnStatsRankReadComplete);
	//StatsReadRankUserCompleteDelegate = FOnStatsReadCompleteDelegate::CreateRaw(this, &FTestStatsInterface::OnStatsUserRankReadComplete);
}

FTestStatsInterface::~FTestStatsInterface()
{
	if (Stats.IsValid())
	{
		//Statss->ClearOnStatsReadCompleteDelegate_Handle(StatsReadCompleteDelegateHandle);
		//Statss->ClearOnStatsReadCompleteDelegate_Handle(StatsReadRankCompleteDelegateHandle);
		//Statss->ClearOnStatsReadCompleteDelegate_Handle(StatsReadRankUserCompleteDelegateHandle);
		//Statss->ClearOnStatsFlushCompleteDelegate_Handle(StatsFlushDelegateHandle);
	}

	Stats = NULL;
}

void FTestStatsInterface::Test(UWorld* InWorld) //todo: add more params to thi
{
	OnlineSub = Online::GetSubsystem(InWorld, FName(*Subsystem));
	if (!OnlineSub)
	{
		UE_LOG_ONLINE_STATS(Warning, TEXT("Failed to get online subsystem for %s"), *Subsystem);

		bOverallSuccess = false;
		return;
	}

	if (OnlineSub->GetIdentityInterface().IsValid())
	{
		UserId = OnlineSub->GetIdentityInterface()->GetUniquePlayerId(0);
	}

	// Cache interfaces
	Stats = OnlineSub->GetStatsInterface();
	if (!Stats.IsValid())
	{
		UE_LOG_ONLINE_STATS(Warning, TEXT("Failed to get online Stats interface for %s"), *Subsystem);

		bOverallSuccess = false;
		return;
	}
}

void FTestStatsInterface::TestFromConfig(UWorld* InWorld)
{
	// todo: implement
	delete this;
}

void FTestStatsInterface::WriteStats()
{
	TArray<FOnlineStatsUserUpdatedStats> Writes;

	int64 NewScore = 535;
	FVariantData NewScoreData;
	NewScoreData.SetValue(NewScore);

	FOnlineStatsUserUpdatedStats& Write1 = Writes.Emplace_GetRef(UserId.ToSharedRef());
	Write1.Stats.Add(TEXT("ShooterAllTimeMatchResultsScore"), FOnlineStatUpdate(NewScoreData, FOnlineStatUpdate::EOnlineStatModificationType::Set)); 

	// Write it to the buffers
	Stats->UpdateStats(UserId.ToSharedRef(), Writes, FOnlineStatsUpdateStatsComplete::CreateLambda([this](const FOnlineError& Error) {
		UE_LOG_ONLINE_STATS(Log, TEXT("Write test finish"));
		TestPhase++;				
	}));
}

void FTestStatsInterface::PrintStats()
{
	//TODO
}

void FTestStatsInterface::ReadStats()
{
	Stats->QueryStats(UserId.ToSharedRef(), UserId.ToSharedRef(), FOnlineStatsQueryUserStatsComplete::CreateLambda([this](const FOnlineError& Error,  const TSharedPtr<const FOnlineStatsUserStats>& QueriedStats)
	{
		UE_LOG_ONLINE_STATS(Log, TEXT("Read test finish with %d queried stats"), QueriedStats->Stats.Num());
		for(const TPair<FString, FOnlineStatValue>& StatReads : QueriedStats->Stats)
		{
			UE_LOG_ONLINE_STATS(Log, TEXT("Stat name %s has value %s"), *StatReads.Key, *StatReads.Value.ToString());
		}

		TSharedPtr<const FOnlineStatsUserStats> ReadUserStats = Stats->GetStats(UserId.ToSharedRef());
		for (const TPair<FString, FOnlineStatValue>& StatReads : ReadUserStats->Stats)
		{
			UE_LOG_ONLINE_STATS(Log, TEXT("Read Stat name %s has value %s"), *StatReads.Key, *StatReads.Value.ToString());
		}
		TestPhase++;
	}));
}

bool FTestStatsInterface::Tick(float DeltaTime)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FTestStatsInterface_Tick);

	if (TestPhase != LastTestPhase)
	{
		if (!bOverallSuccess)
		{
			UE_LOG_ONLINE_STATS(Log, TEXT("Testing failed in phase %d"), LastTestPhase);
			TestPhase = 2;
		}

		LastTestPhase = TestPhase;

		switch (TestPhase)
		{
		case 0:
			UE_LOG_ONLINE_STATS(Log, TEXT("// Beginning Write (writing matches to 535)"));
			WriteStats();
			break;
		case 1:
			UE_LOG_ONLINE_STATS(Log, TEXT("// Beginning ReadStats (reading self)"));
			ReadStats();
			break;
		/*case 3:
			UE_LOG_ONLINE_Stats(Log, TEXT("// Beginning ReadStatssFriends"));
			ReadStatssFriends();
			break;
		case 4:
			UE_LOG_ONLINE_Stats(Log, TEXT("// Beginning ReadStatssRank polling users from 1 to 8"));
			ReadStatssRank(3, 5);
			break;
		case 5:
			UE_LOG_ONLINE_Stats(Log, TEXT("// Beginning ReadStatssUser polling all users +- 5 spaces from the local user"));
			ReadStatssUser(*UserId, 5);
			break;
		case 6:
		{
			if (FindRankUserId.IsEmpty())
			{
				++TestPhase;
				UE_LOG_ONLINE_Stats(Log, TEXT("Test will be skipping arbitrary lookup as an id was not provided."));
				return true;
			}
			else
			{
				UE_LOG_ONLINE_Stats(Log, TEXT("// Beginning ReadStatssUser polling all users +- 1 from the designated user (%s)"), *FindRankUserId);
				ReadStatssUser(1);
			}
		} break;*/
		case 2:
			UE_LOG_ONLINE_STATS(Log, TEXT("TESTING COMPLETE Success:%s!"), bOverallSuccess ? TEXT("true") : TEXT("false"));
			delete this;
			return false;
		}
	}
	return true;
}

#endif //WITH_DEV_AUTOMATION_TESTS
