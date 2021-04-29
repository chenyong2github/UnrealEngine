// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/TestStatsInterface.h"
#include "OnlineSubsystemUtils.h"
#include "Interfaces/OnlineIdentityInterface.h"

#if WITH_DEV_AUTOMATION_TESTS

FTestStatsInterface::FTestStatsInterface(const FString& InSubsystem) :
	Subsystem(InSubsystem),
	bOverallSuccess(true),
	Stats(NULL),
	TestPhase(0),
	LastTestPhase(-1)
{

}

FTestStatsInterface::~FTestStatsInterface()
{
	Stats = NULL;
}

void FTestStatsInterface::Test(UWorld* InWorld)
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

void FTestStatsInterface::WriteStats()
{
	TArray<FOnlineStatsUserUpdatedStats> Writes;

	int64 NewScore = 999;
	FVariantData NewScoreData;
	NewScoreData.SetValue(NewScore);

	int64 NewFrags = 53;
	FVariantData NewFragsData;
	NewFragsData.SetValue(NewFrags);

	int64 NewDeaths = 24;
	FVariantData NewDeathsData;
	NewDeathsData.SetValue(NewDeaths);

	int64 NewMatchesPlayed = 3;
	FVariantData NewMatchesPlayedData;
	NewMatchesPlayedData.SetValue(NewMatchesPlayed);

	UE_LOG_ONLINE_STATS(Log, TEXT("FTestStatsInterface::WriteStats()"));
	UE_LOG_ONLINE_STATS(Log, TEXT("	- ShooterAllTimeMatchResultsScore is being set to %ll"), NewScore);
	UE_LOG_ONLINE_STATS(Log, TEXT("	- ShooterAllTimeMatchResultsFrags is being incremented by %ll"), NewFrags);
	UE_LOG_ONLINE_STATS(Log, TEXT("	- ShooterAllTimeMatchResultsDeaths is being set to the largest of its existing value and %ll"), NewDeaths);
	UE_LOG_ONLINE_STATS(Log, TEXT("	- ShooterAllTimeMatchResultsMatchesPlayed is being set to the smallest of its existng value and %ll"), NewMatchesPlayed);

	FOnlineStatsUserUpdatedStats& Write1 = Writes.Emplace_GetRef(UserId.ToSharedRef());
	Write1.Stats.Add(TEXT("ShooterAllTimeMatchResultsScore"), FOnlineStatUpdate(NewScoreData, FOnlineStatUpdate::EOnlineStatModificationType::Set));
	Write1.Stats.Add(TEXT("ShooterAllTimeMatchResultsFrags"), FOnlineStatUpdate(NewFragsData, FOnlineStatUpdate::EOnlineStatModificationType::Sum));
	Write1.Stats.Add(TEXT("ShooterAllTimeMatchResultsDeaths"), FOnlineStatUpdate(NewDeathsData, FOnlineStatUpdate::EOnlineStatModificationType::Largest));
	Write1.Stats.Add(TEXT("ShooterAllTimeMatchResultsMatchesPlayed"), FOnlineStatUpdate(NewMatchesPlayedData, FOnlineStatUpdate::EOnlineStatModificationType::Smallest));

	// Write it to the buffers
	Stats->UpdateStats(UserId.ToSharedRef(), Writes, FOnlineStatsUpdateStatsComplete::CreateLambda([this](const FOnlineError& Error) {
		UE_LOG_ONLINE_STATS(Log, TEXT("Write test finish"));
		TestPhase++;

		if(!Error.bSucceeded)
		{
			UE_LOG_ONLINE_STATS(Error, TEXT("WriteStats test failed: %s"), *Error.GetErrorMessage().ToString());
			bOverallSuccess = false;
		}
	}));
}

void FTestStatsInterface::ReadStats()
{
	Stats->QueryStats(UserId.ToSharedRef(), UserId.ToSharedRef(), FOnlineStatsQueryUserStatsComplete::CreateLambda([this](const FOnlineError& Error,  const TSharedPtr<const FOnlineStatsUserStats>& QueriedStats)
	{
		UE_LOG_ONLINE_STATS(Log, TEXT("Read test finish with %d queried stats"), QueriedStats->Stats.Num());


		if (!Error.bSucceeded)
		{
			UE_LOG_ONLINE_STATS(Error, TEXT("ReadStats test failed: %s"), *Error.GetErrorMessage().ToString());
			bOverallSuccess = false;
		}
		else
		{
			for (const TPair<FString, FOnlineStatValue>& StatReads : QueriedStats->Stats)
			{
				UE_LOG_ONLINE_STATS(Log, TEXT("Stat name %s has value %s"), *StatReads.Key, *StatReads.Value.ToString());
			}

			TSharedPtr<const FOnlineStatsUserStats> ReadUserStats = Stats->GetStats(UserId.ToSharedRef());
			for (const TPair<FString, FOnlineStatValue>& StatReads : ReadUserStats->Stats)
			{
				UE_LOG_ONLINE_STATS(Log, TEXT("Read Stat name %s has value %s"), *StatReads.Key, *StatReads.Value.ToString());
			}
			TestPhase++;
		}
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
			TestPhase = 3;
		}

		LastTestPhase = TestPhase;

		switch (TestPhase)
		{
		case 0:
			UE_LOG_ONLINE_STATS(Log, TEXT("// Beginning ReadStats (reading self, pre-write)"));
			ReadStats();
			break;
		case 1:
			UE_LOG_ONLINE_STATS(Log, TEXT("// Beginning Write (writing matches to 999)"));
			WriteStats();
			break;
		case 2:
			UE_LOG_ONLINE_STATS(Log, TEXT("// Beginning ReadStats (reading self, post-write)"));
			ReadStats();
			break;
		case 3:
			UE_LOG_ONLINE_STATS(Log, TEXT("TESTING COMPLETE Success:%s!"), bOverallSuccess ? TEXT("true") : TEXT("false"));
			delete this;
			return false;
		}
	}
	return true;
}

#endif //WITH_DEV_AUTOMATION_TESTS
