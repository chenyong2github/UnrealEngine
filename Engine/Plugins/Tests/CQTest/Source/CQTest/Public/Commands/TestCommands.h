// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Misc/AutomationTest.h"

class FWaitUntil : public IAutomationLatentCommand
{
public:
	explicit FWaitUntil(FAutomationTestBase* TestRunner, TFunction<bool()> Query, FTimespan Timeout = FTimespan::FromSeconds(10))
		: TestBase(TestRunner)
		, Query(MoveTemp(Query)) 
		, Timeout(Timeout)
		, StartTime(FDateTime::UtcNow())
	{}

	bool Update() override
	{
		if (Query())
		{
			return true;
		}
		else if (FDateTime::UtcNow() >= StartTime + Timeout)
		{
			TestBase->AddError(TEXT("Latent command timed out."));
			return true;
		}
		return false;
	}

	FAutomationTestBase* TestBase;
	TFunction<bool()> Query;
	FTimespan Timeout;
	FDateTime StartTime;
};

class FExecute : public IAutomationLatentCommand
{
public:
	explicit FExecute(TFunction<void()> Func)
		: Func(MoveTemp(Func)) {}

	bool Update() override
	{
		Func();
		return true;
	}

	TFunction<void()> Func;
};

class FRunSequence : public IAutomationLatentCommand
{
public:
	FRunSequence(const TArray<TSharedPtr<IAutomationLatentCommand>>& ToAdd)
		: Commands(ToAdd)
	{
	}

	template <class... Cmds>
	FRunSequence(Cmds... Commands)
		: FRunSequence(TArray<TSharedPtr<IAutomationLatentCommand>>{ Commands... })
	{
	}

	bool Update() override
	{
		// Taking notes for the next time someone needs to understand the odd behavior here
		// From the test framework's point of view, latent commands can all be run after the test has completed
		// So if a BeforeTest function adds latent actions which need to happen before RunTest,
		// then RunTest, AfterTest, and TearDown must be latent actions, run in that sequence
		// But all of the latent actions from BeforeTest must run before RunTest is executed
		// Which means that RunTest can't be added to the latent actions queue until
		// there are no more latent commands in the queue to run
		// So we need to run the first command in this series, followed by asking the test framework
		// to execute all of its latent commands.
		// Given that the latent actions may be waiting for tickable game objects
		// Control must be given back to the game, so we can't use a tight loop
		// to call ExecuteLatentCommands
		// The next challenge is that THIS command is in that list of commands
		// So we track if this call was recursive with the bRentrantFlag
		if (Commands.Num() == 0)
		{
			return true;
		}
		if (bRentrantFlag)
		{
			return true;
		}

		if (bCommandDone)
		{
			bRentrantFlag = true;
			if (FAutomationTestFramework::Get().ExecuteLatentCommands())
			{
				FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FRunSequence>(Commands));
				return true;
			}
			else
			{
				bRentrantFlag = false;
			}
		}
		else
		{
			if (Commands[0]->Update())
			{
				Commands.RemoveAt(0);
				bCommandDone = true;
			}
		}

		return false;
	}

	TArray<TSharedPtr<IAutomationLatentCommand>> Commands;
	bool bRentrantFlag = false;
	bool bCommandDone = false;
};