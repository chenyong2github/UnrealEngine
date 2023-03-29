// Copyright Epic Games, Inc. All Rights Reserved.

#include "CQTest.h"

struct FCommandLog
{
	TArray<FString> Commands;
};

class FNamedCommand : public IAutomationLatentCommand
{
public:
	FNamedCommand(TArray<FString>& CommandLog, FString Name)
		: Log(CommandLog), CommandName(Name) {}

	bool Update() override
	{
		Log.Add(CommandName);
		return true;
	}

	TArray<FString>& Log;
	FString CommandName;
};

class FTickingNamedCommand : public FNamedCommand
{
public:
	FTickingNamedCommand(TArray<FString>& CommandLog, FString Name, int32 Ticks)
		: FNamedCommand(CommandLog, Name), ExpectedCount(Ticks) {}
		
	bool Update() override
	{
		if (CurrentCount == ExpectedCount)
		{
			return true;
		}

		Log.Add(CommandName);
		CurrentCount++;
		return false;
	}

	int32 ExpectedCount{ 0 };
	int32 CurrentCount{ 0 };
};

TEST_CLASS(RunSequenceTests, "TestFramework.CQTest")
{
	const TArray<FString> Names = 
	{ 
		"Zero",
		"One",
		"Two",
		"Three",
		"Four",
	};

	TFunction<bool(RunSequenceTests*)> Assertion;
	TArray<FString> CommandLog;

	AFTER_EACH()
	{
		ASSERT_THAT(IsTrue(Assertion(this)));
	}

	TEST_METHOD(RunSequence_WithZeroCommands_DoesNotFail)
	{
		AddCommand(new FRunSequence());
		Assertion = [](RunSequenceTests* test) {
			return test->CommandLog.IsEmpty();
		};
	}

	TEST_METHOD(RunSequence_WithOneCommand_RunsCommand)
	{
		AddCommand(new FRunSequence(MakeShared<FNamedCommand>(CommandLog, Names[0])));
		Assertion = [](RunSequenceTests* test) {
			return test->CommandLog.Num() == 1 && test->CommandLog[0] == test->Names[0];
		};
	}

	TEST_METHOD(RunSequence_WithNamedCommands_RunsCommandsInOrder)
	{
		TArray<TSharedPtr<FNamedCommand>> Commands;
		Commands.Add(MakeShared<FNamedCommand>(CommandLog, Names[0]));
		Commands.Add(MakeShared<FNamedCommand>(CommandLog, Names[1]));
		Commands.Add(MakeShared<FNamedCommand>(CommandLog, Names[2]));
		Commands.Add(MakeShared<FNamedCommand>(CommandLog, Names[3]));
		Commands.Add(MakeShared<FNamedCommand>(CommandLog, Names[4]));
		
		AddCommand(new FRunSequence(Commands));

		Assertion = [](RunSequenceTests* test) {
			return test->CommandLog == test->Names;
		};
	}

	TEST_METHOD(RunSequence_WithTickingCommands_RunsCommandsInOrder)
	{
		TArray<TSharedPtr<FTickingNamedCommand>> Commands;
		Commands.Add(MakeShared<FTickingNamedCommand>(CommandLog, Names[0], 3));
		Commands.Add(MakeShared<FTickingNamedCommand>(CommandLog, Names[1], 3));
		Commands.Add(MakeShared<FTickingNamedCommand>(CommandLog, Names[2], 3));
		Commands.Add(MakeShared<FTickingNamedCommand>(CommandLog, Names[3], 3));
		Commands.Add(MakeShared<FTickingNamedCommand>(CommandLog, Names[4], 3));

		AddCommand(new FRunSequence(Commands));

		Assertion = [](RunSequenceTests* test) {
			if (test->CommandLog.Num() != 15)
			{
				return false;
			}

			int32 NameIndex = -1;
			for (int32 CommandIndex = 0; CommandIndex < test->CommandLog.Num(); CommandIndex++)
			{
				if (CommandIndex % 3 == 0)
				{
					NameIndex++;
				}

				if (test->CommandLog[CommandIndex] != test->Names[NameIndex])
				{
					return false;
				}
			}

			return true;
		};
	}
};