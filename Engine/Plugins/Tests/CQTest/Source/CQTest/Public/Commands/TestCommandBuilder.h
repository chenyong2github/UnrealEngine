// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/AutomationTest.h"
#include "Commands/TestCommands.h"

class FTestCommandBuilder
{
public:
	FTestCommandBuilder(FAutomationTestBase& InTestRunner)
		: TestRunner(InTestRunner) {}

	~FTestCommandBuilder()
	{
		checkf(CommandQueue.IsEmpty(), TEXT("Adding latent actions from within latent actions is currently unsupported."));
	}

	FTestCommandBuilder& Do(const TCHAR* Description, TFunction<void()> Action)
	{
		if (!TestRunner.HasAnyErrors())
		{
			CommandQueue.Add(MakeShared<FExecute>(TestRunner, Action, Description));
		}
		return *this;
	}

	FTestCommandBuilder& Do(TFunction<void()> Action)
	{
		return Do(nullptr, Action);
	}

	FTestCommandBuilder& Then(TFunction<void()> Action)
	{
		return Do(Action);
	}

	FTestCommandBuilder& Then(const TCHAR* Description, TFunction<void()> Action)
	{
		return Do(Description, Action);
	}

	FTestCommandBuilder& Until(const TCHAR* Description, TFunction<bool()> Query, FTimespan Timeout = FTimespan::FromSeconds(10))
	{
		if (!TestRunner.HasAnyErrors())
		{
			CommandQueue.Add(MakeShared<FWaitUntil>(TestRunner, Query, Timeout, Description));
		}
		return *this;
	}

	FTestCommandBuilder& Until(TFunction<bool()> Query, FTimespan Timeout = FTimespan::FromSeconds(10))
	{
		return Until(nullptr, Query, Timeout);
	}

	FTestCommandBuilder& StartWhen(TFunction<bool()> Query, FTimespan Timeout = FTimespan::FromSeconds(10))
	{
		return Until(Query, Timeout);
	}

	FTestCommandBuilder& StartWhen(const TCHAR* Description, TFunction<bool()> Query, FTimespan Timeout = FTimespan::FromSeconds(10))
	{
		return Until(Description, Query, Timeout);
	}

	TSharedPtr<IAutomationLatentCommand> Build()
	{
		TSharedPtr<IAutomationLatentCommand> Result = nullptr;
		if (CommandQueue.Num() == 0)
		{
			return Result;
		}
		else if (CommandQueue.Num() == 1)
		{
			Result = CommandQueue[0];
		}
		else
		{
			Result = MakeShared<FRunSequence>(CommandQueue);
		}

		CommandQueue.Empty();
		return Result;
	}

protected:
	TArray<TSharedPtr<IAutomationLatentCommand>> CommandQueue{};
	FAutomationTestBase& TestRunner;

	template<typename Asserter>
	friend struct TBaseTest;
};