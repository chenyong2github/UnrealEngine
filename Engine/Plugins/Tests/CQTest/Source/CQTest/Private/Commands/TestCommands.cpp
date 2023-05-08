// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commands/TestCommands.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogCqTest, Log, All)

bool CQTEST_API FWaitUntil::Update()
{
	if (Description != nullptr && !bHasLoggedStart)
	{
		UE_LOG(LogCqTest, Log, TEXT("Starting %s"), Description);
		bHasLoggedStart = true;
	}
	if (TestRunner.HasAnyErrors())
	{
		return true;
	}

	if (Query())
	{
		if (Description)
		{
			UE_LOG(LogCqTest, Log, TEXT("Finished %s"), Description);
		}
		return true;
	}
	else if (FDateTime::UtcNow() >= StartTime + Timeout)
	{
		if (Description)
		{
			TestRunner.AddError(*FString::Printf(TEXT("Timed out waiting for %s"), Description));
		}
		else
		{
			TestRunner.AddError(TEXT("Latent command timed out."));
		}
		return true;
	}
	return false;
}

bool CQTEST_API FExecute::Update()
{
	if (Description)
	{
		UE_LOG(LogCqTest, Log, TEXT("Running %s"), Description);
	}
	if (!TestRunner.HasAnyErrors())
	{
		Func();
	}
	return true;
}

void CQTEST_API FRunSequence::Append(TSharedPtr<IAutomationLatentCommand> ToAdd)
{
	Commands.Add(ToAdd);
}

void CQTEST_API FRunSequence::AppendAll(TArray<TSharedPtr<IAutomationLatentCommand>> ToAdd)
{
	for (auto& Cmd : ToAdd)
	{
		Commands.Add(Cmd);
	}
}

void CQTEST_API FRunSequence::Prepend(TSharedPtr<IAutomationLatentCommand> ToAdd)
{
	Commands.Insert(ToAdd, 0);
}

bool CQTEST_API FRunSequence::Update()
{
	if (Commands.Num() == 0)
	{
		return true;
	}
	else
	{
		auto Command = Commands[0];
		Commands.RemoveAt(0); //Remove the command now, in case the command prepends other commands
		if (Command != nullptr && Command->Update() == false)
		{
			Commands.Insert(Command, 0);
			return false;
		}
		return Commands.IsEmpty();
	}
}
