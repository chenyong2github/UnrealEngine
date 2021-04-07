// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "UObject/CoreOnline.h"
#include "Interfaces/OnlineStatsInterface.h"
#include "OnlineSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Class used to test the friends interface
 */
 class FTestStatsInterface : public FTickerObjectBase
 {
	/** The subsystem that was requested to be tested or the default if empty */
	const FString Subsystem;

	/** Cached Online Subsystem Pointer */
	class IOnlineSubsystem* OnlineSub;

	/** Keep track of success across all functions and callbacks */
	bool bOverallSuccess;

	/** Logged in UserId */
	TSharedPtr<const FUniqueNetId> UserId;

	/** Convenient access to the Stats interfaces */
	IOnlineStatsPtr Stats;

	/** Current phase of testing */
	int32 TestPhase;
	/** Last phase of testing triggered */
	int32 LastTestPhase;

	/** Hidden on purpose */
	FTestStatsInterface()
		: Subsystem()
	{
	}

	/**
	 *	Write out some test data to a Stats
	 */
	void WriteStats();

	/**
	 *	Delegate called when a Stats has been successfully read
	 */
	void OnStatsReadComplete(bool bWasSuccessful);

	/**
	 *	Read in some predefined data from a Stats
	 */
	void ReadStats();

	/** Utilities */
	void PrintStats();

 public:
	/**
	 * Sets the subsystem name to test
	 *
	 * @param InSubsystem the subsystem to test
	 */
	FTestStatsInterface(const FString& InSubsystem);

	virtual ~FTestStatsInterface();

	// FTickerObjectBase

	bool Tick( float DeltaTime ) override;

	// FTestStatsInterface

	/**
	 * Kicks off all of the testing process
	 */
	void Test(UWorld* InWorld);
 };

#endif //WITH_DEV_AUTOMATION_TESTS
