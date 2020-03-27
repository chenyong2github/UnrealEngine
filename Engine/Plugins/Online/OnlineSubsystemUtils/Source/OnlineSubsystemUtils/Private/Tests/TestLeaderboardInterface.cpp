// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/TestLeaderboardInterface.h"
#include "OnlineSubsystemUtils.h"
#include "Interfaces/OnlineIdentityInterface.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 *	Example of a leaderboard write object
 */
class TestLeaderboardWrite : public FOnlineLeaderboardWrite
{
public:
	TestLeaderboardWrite()
	{
		// Default properties
		new (LeaderboardNames) FName(TEXT("TestLeaderboard"));
		RatedStat = "TestIntStat1";
		DisplayFormat = ELeaderboardFormat::Number;
		SortMethod = ELeaderboardSort::Descending;
		UpdateMethod = ELeaderboardUpdateMethod::KeepBest;
	}
};

/**
 *	Example of a leaderboard read object
 */
class TestLeaderboardRead : public FOnlineLeaderboardRead
{
public:
	TestLeaderboardRead(const FString& InLeaderboardName, const FString& InSortedColumn, const TMap<FString, EOnlineKeyValuePairDataType::Type>& InColumns)
	{
		LeaderboardName = FName(InLeaderboardName);
		SortedColumn = FName(InSortedColumn);

		for (TPair<FString, EOnlineKeyValuePairDataType::Type> Column : InColumns)
		{
			new (ColumnMetadata) FColumnMetaData(FName(Column.Key), Column.Value);
		}
	}
};

FTestLeaderboardInterface::FTestLeaderboardInterface(const FString& InSubsystem) :
	Subsystem(InSubsystem),
	bOverallSuccess(true),
	bReadLeaderboardAttempted(false),
	Leaderboards(NULL),
	TestPhase(0),
	LastTestPhase(-1)
{
	// Define delegates
	LeaderboardFlushDelegate = FOnLeaderboardFlushCompleteDelegate::CreateRaw(this, &FTestLeaderboardInterface::OnLeaderboardFlushComplete);
	LeaderboardReadCompleteDelegate = FOnLeaderboardReadCompleteDelegate::CreateRaw(this, &FTestLeaderboardInterface::OnLeaderboardReadComplete);
	LeaderboardReadRankCompleteDelegate = FOnLeaderboardReadCompleteDelegate::CreateRaw(this, &FTestLeaderboardInterface::OnLeaderboardRankReadComplete);
	LeaderboardReadRankUserCompleteDelegate = FOnLeaderboardReadCompleteDelegate::CreateRaw(this, &FTestLeaderboardInterface::OnLeaderboardUserRankReadComplete);
}

FTestLeaderboardInterface::~FTestLeaderboardInterface()
{
	if(Leaderboards.IsValid())
	{
		Leaderboards->ClearOnLeaderboardReadCompleteDelegate_Handle(LeaderboardReadCompleteDelegateHandle);
		Leaderboards->ClearOnLeaderboardReadCompleteDelegate_Handle(LeaderboardReadRankCompleteDelegateHandle);
		Leaderboards->ClearOnLeaderboardReadCompleteDelegate_Handle(LeaderboardReadRankUserCompleteDelegateHandle);
		Leaderboards->ClearOnLeaderboardFlushCompleteDelegate_Handle(LeaderboardFlushDelegateHandle);
	}

	Leaderboards = NULL;
}

void FTestLeaderboardInterface::Test(UWorld* InWorld, const FString& InLeaderboardName, const FString& InColumnName, TMap<FString, EOnlineKeyValuePairDataType::Type>&& InColumns, const FString& InUserId)
{
	FindRankUserId = InUserId;
	OnlineSub = Online::GetSubsystem(InWorld, FName(*Subsystem));
	if (!OnlineSub)
	{
		UE_LOG_ONLINE_LEADERBOARD(Warning, TEXT("Failed to get online subsystem for %s"), *Subsystem);

		bOverallSuccess = false;
		return;
	}

	if (OnlineSub->GetIdentityInterface().IsValid())
	{
		UserId = OnlineSub->GetIdentityInterface()->GetUniquePlayerId(0);
	}

	// Cache interfaces
	Leaderboards = OnlineSub->GetLeaderboardsInterface();
	if (!Leaderboards.IsValid())
	{
		UE_LOG_ONLINE_LEADERBOARD(Warning, TEXT("Failed to get online leaderboards interface for %s"), *Subsystem);

		bOverallSuccess = false;
		return;
	}

	LeaderboardName = InLeaderboardName;
	SortedColumn = InColumnName;
	Columns = MoveTemp(InColumns);
}

void FTestLeaderboardInterface::WriteLeaderboards()
{
	TestLeaderboardWrite WriteObject;
	
	// Set some data
	WriteObject.SetIntStat("TestIntStat1", 50);
	WriteObject.SetFloatStat("TestFloatStat1", 99.5f);

	// Write it to the buffers
	Leaderboards->WriteLeaderboards(TEXT("TEST"), *UserId, WriteObject);
	TestPhase++;
}

void FTestLeaderboardInterface::OnLeaderboardFlushComplete(FName SessionName, bool bWasSuccessful)
{
	UE_LOG_ONLINE_LEADERBOARD(Verbose, TEXT("OnLeaderboardFlushComplete Session: %s bWasSuccessful: %d"), *SessionName.ToString(), bWasSuccessful);
	bOverallSuccess = bOverallSuccess && bWasSuccessful;

	Leaderboards->ClearOnLeaderboardFlushCompleteDelegate_Handle(LeaderboardFlushDelegateHandle);
	TestPhase++;
}

void FTestLeaderboardInterface::FlushLeaderboards()
{
	LeaderboardFlushDelegateHandle = Leaderboards->AddOnLeaderboardFlushCompleteDelegate_Handle(LeaderboardFlushDelegate);
	Leaderboards->FlushLeaderboards(TEXT("TEST"));
}

void FTestLeaderboardInterface::PrintLeaderboards()
{
	for (int32 RowIdx = 0; RowIdx < ReadObject->Rows.Num(); ++RowIdx)
	{
		const FOnlineStatsRow& StatsRow = ReadObject->Rows[RowIdx];
		UE_LOG_ONLINE_LEADERBOARD(Log, TEXT("Leaderboard stats for: Nickname = %s, Rank = %d"), *StatsRow.NickName, StatsRow.Rank);

		for (FStatsColumnArray::TConstIterator It(StatsRow.Columns); It; ++It)
		{
			UE_LOG_ONLINE_LEADERBOARD(Log, TEXT("  %s = %s"), *It.Key().ToString(), *It.Value().ToString());
		}
	}
}

void FTestLeaderboardInterface::OnLeaderboardReadComplete(bool bWasSuccessful)
{
	UE_LOG_ONLINE_LEADERBOARD(Verbose, TEXT("OnLeaderboardReadComplete bWasSuccessful: %d"), bWasSuccessful);
	bOverallSuccess = bOverallSuccess && (bReadLeaderboardAttempted == bWasSuccessful);

	PrintLeaderboards();

	Leaderboards->ClearOnLeaderboardReadCompleteDelegate_Handle(LeaderboardReadCompleteDelegateHandle);
	bReadLeaderboardAttempted = false;
	TestPhase++;
}

void FTestLeaderboardInterface::OnLeaderboardRankReadComplete(bool bWasSuccessful)
{
	UE_LOG_ONLINE_LEADERBOARD(Verbose, TEXT("OnLeaderboardRankReadComplete bWasSuccessful: %d"), bWasSuccessful);
	bOverallSuccess = bOverallSuccess && (bReadLeaderboardAttempted == bWasSuccessful);

	PrintLeaderboards();

	Leaderboards->ClearOnLeaderboardReadCompleteDelegate_Handle(LeaderboardReadRankCompleteDelegateHandle);
	bReadLeaderboardAttempted = false;
	TestPhase++;
}

void FTestLeaderboardInterface::OnLeaderboardUserRankReadComplete(bool bWasSuccessful)
{
	UE_LOG_ONLINE_LEADERBOARD(Verbose, TEXT("OnLeaderboardUserRankReadComplete bWasSuccessful: %d"), bWasSuccessful);
	bOverallSuccess = bOverallSuccess && (bReadLeaderboardAttempted == bWasSuccessful);

	PrintLeaderboards();

	Leaderboards->ClearOnLeaderboardReadCompleteDelegate_Handle(LeaderboardReadRankUserCompleteDelegateHandle);
	bReadLeaderboardAttempted = false;
	TestPhase++;
}

void FTestLeaderboardInterface::ReadLeaderboards()
{
	ReadObject = MakeShareable(new TestLeaderboardRead(LeaderboardName, SortedColumn, Columns));
	FOnlineLeaderboardReadRef ReadObjectRef = ReadObject.ToSharedRef();

	LeaderboardReadCompleteDelegateHandle = Leaderboards->AddOnLeaderboardReadCompleteDelegate_Handle(LeaderboardReadCompleteDelegate);
	bReadLeaderboardAttempted = Leaderboards->ReadLeaderboardsForFriends(0, ReadObjectRef);
}

void FTestLeaderboardInterface::ReadLeaderboardsRank(int32 Rank, int32 Range)
{
	ReadObject = MakeShareable(new TestLeaderboardRead(LeaderboardName, SortedColumn, Columns));
	FOnlineLeaderboardReadRef ReadObjectRef = ReadObject.ToSharedRef();

	LeaderboardReadRankCompleteDelegateHandle = Leaderboards->AddOnLeaderboardReadCompleteDelegate_Handle(LeaderboardReadRankCompleteDelegate);
	bReadLeaderboardAttempted = Leaderboards->ReadLeaderboardsAroundRank(Rank, Range, ReadObjectRef);
}

void FTestLeaderboardInterface::ReadLeaderboardsUser(const FUniqueNetId& InUserId, int32 Range)
{
	if (!OnlineSub || !OnlineSub->GetIdentityInterface().IsValid())
	{
		bOverallSuccess = false;
		++TestPhase;
		return;
	}

	ReadObject = MakeShareable(new TestLeaderboardRead(LeaderboardName, SortedColumn, Columns));
	FOnlineLeaderboardReadRef ReadObjectRef = ReadObject.ToSharedRef();

	// Need to get a shared reference for ReadLeaderboardsAroundUser
	TSharedPtr<const FUniqueNetId> ArbitraryId = OnlineSub->GetIdentityInterface()->CreateUniquePlayerId(InUserId.ToString());

	if (ArbitraryId.IsValid())
	{
		LeaderboardReadRankUserCompleteDelegateHandle = Leaderboards->AddOnLeaderboardReadCompleteDelegate_Handle(LeaderboardReadRankUserCompleteDelegate);
		bReadLeaderboardAttempted = Leaderboards->ReadLeaderboardsAroundUser(ArbitraryId.ToSharedRef(), Range, ReadObjectRef);
	}
	else
	{
		UE_LOG_ONLINE_LEADERBOARD(Warning, TEXT("Cannot run the leaderboards around user test as it failed to start. UserId not valid"));
		bOverallSuccess = false;
		Leaderboards->ClearOnLeaderboardReadCompleteDelegate_Handle(LeaderboardReadRankUserCompleteDelegateHandle);
		++TestPhase;
	}
}

void FTestLeaderboardInterface::ReadLeaderboardsUser(int32 Range)
{
	FUniqueNetIdString FindUser(FindRankUserId);
	ReadLeaderboardsUser(FindUser, Range);
}

bool FTestLeaderboardInterface::Tick( float DeltaTime )
{
    QUICK_SCOPE_CYCLE_COUNTER(STAT_FTestLeaderboardInterface_Tick);

	if (TestPhase != LastTestPhase)
	{
		LastTestPhase = TestPhase;
		if (!bOverallSuccess)
		{
			UE_LOG_ONLINE_LEADERBOARD(Log, TEXT("Testing failed in phase %d"), LastTestPhase);
			TestPhase = 6;
		}

		switch(TestPhase)
		{
		case 0:
			WriteLeaderboards();
			break;
		case 1:
			FlushLeaderboards();
			break;
		case 2:
			ReadLeaderboards();
			break;
		case 3:
			ReadLeaderboardsRank(3, 5);
			break;
		case 4:
			ReadLeaderboardsUser(*UserId, 5);
			break;
		case 5:
		{
			if (FindRankUserId.IsEmpty())
			{
				++TestPhase;
				UE_LOG_ONLINE_LEADERBOARD(Log, TEXT("Test will be skipping arbitrary lookup as an id was not provided."));
				return true;
			}
			else
			{
				ReadLeaderboardsUser(1);
			}
		} break;
		case 6:
			UE_LOG_ONLINE_LEADERBOARD(Log, TEXT("TESTING COMPLETE Success:%s!"), bOverallSuccess ? TEXT("true") : TEXT("false"));
			delete this;
			return false;
		}
	}
	return true;
}

#endif //WITH_DEV_AUTOMATION_TESTS
