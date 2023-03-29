// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/AutomationTest.h"
#include "Commands/TestCommands.h"

class FTestCommandBuilder
{
public:
	FTestCommandBuilder() = default;

	~FTestCommandBuilder()
	{
		check(CommandQueue.IsEmpty());
	}

	FTestCommandBuilder& Do(TFunction<void()> Action)
	{
		CommandQueue.Add(MakeShared<FExecute>(Action));
		return *this;
	}

	FTestCommandBuilder& Then(TFunction<void()> Action)
	{
		return Do(Action);
	}

	FTestCommandBuilder& Until(FAutomationTestBase* TestRunner, TFunction<bool()> Query, FTimespan Timeout = FTimespan::FromSeconds(10))
	{
		CommandQueue.Add(MakeShared<FWaitUntil>(TestRunner, Query, Timeout));
		return *this;
	}

	FTestCommandBuilder& StartWhen(FAutomationTestBase* TestRunner, TFunction<bool()> Query, FTimespan Timeout = FTimespan::FromSeconds(10))
	{
		return Until(TestRunner, Query, Timeout);
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

private:
	TArray<TSharedPtr<IAutomationLatentCommand>> CommandQueue{};
};