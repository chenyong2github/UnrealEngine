// Copyright Epic Games, Inc. All Rights Reserved.

#include "PythonAutomationTest.h"
#include "IPythonScriptPlugin.h"
#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "PyAutomationTest"

DEFINE_LOG_CATEGORY_STATIC(PyAutomationTest, Log, Log)

bool UPyAutomationTestLibrary::IsRunningPyLatentCommand = false;
float UPyAutomationTestLibrary::PyLatentCommandTimeout = 120.0f;

void UPyAutomationTestLibrary::SetIsRunningPyLatentCommand(bool isRunning)
{
	IsRunningPyLatentCommand = isRunning;
}

bool UPyAutomationTestLibrary::GetIsRunningPyLatentCommand()
{
	return IsRunningPyLatentCommand;
}

void UPyAutomationTestLibrary::SetPyLatentCommandTimeout(float Seconds)
{
	PyLatentCommandTimeout = Seconds;
}

float UPyAutomationTestLibrary::GetPyLatentCommandTimeout()
{
	return PyLatentCommandTimeout;
}

void UPyAutomationTestLibrary::ResetPyLatentCommand()
{
	IsRunningPyLatentCommand = false;
	PyLatentCommandTimeout = 120.0f;
}


void CleanUpPythonScheduler()
{
	// Clean up python side
	IPythonScriptPlugin::Get()->ExecPythonCommand(TEXT("import unreal;unreal.AutomationScheduler.cleanup()"));
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FIsRunningPyLatentCommand, float, Timeout);

bool FIsRunningPyLatentCommand::Update()
{
	float NewTime = FPlatformTime::Seconds();
	if (NewTime - StartTime < Timeout)
	{
		return !UPyAutomationTestLibrary::GetIsRunningPyLatentCommand();
	}

	UPyAutomationTestLibrary::SetIsRunningPyLatentCommand(false);
	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	CurrentTest->AddError(TEXT("Timeout reached waiting for Python Latent Command."));

	CleanUpPythonScheduler();

	return true;
}

class FPythonAutomationTestBase : public FAutomationTestBase
{
public:
	FPythonAutomationTestBase(const FString& InName, const bool bInComplexTask)
		: FAutomationTestBase(InName, bInComplexTask)
	{
	}

	virtual FString GetTestSourceFileName() const override { return __FILE__; }
	virtual FString GetTestSourceFileName(const FString& InTestName) const override
	{
		// Because FPythonAutomationTest is a Complex Automation Test, InTestName contains the name of the cpp class and a test parameter.
		// We isolate the test parameter and return it as it is the path of the python script.
		int Position = InTestName.Find(TEXT(" "));
		return InTestName.RightChop(Position+1);
	}

	virtual int32 GetTestSourceFileLine() const override { return __LINE__; }
	virtual int32 GetTestSourceFileLine(const FString& InTestName) const override
	{
		// FPythonAutomationTest generates one test per script file. File Line is therefore the begining of the file.
		return 0;
	}

protected:
	FString BeautifyPath(FString Path) const
	{
		int Position = Path.Find(TEXT("/Python/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (Position > 0) Position += 8;

		return Path.LeftChop(3).RightChop(Position).Replace(TEXT("/"), TEXT("."), ESearchCase::CaseSensitive);
	}

	virtual void SetTestContext(FString Context) override
	{
		TestParameterContext = BeautifyPath(Context);
	}

private:
	FString PyTestName;

};

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(
	FPythonAutomationTest,
	FPythonAutomationTestBase,
	"Editor.Python",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FPythonAutomationTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{

	FString PythonTestsDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
	PythonTestsDir += TEXT("Python");
	// TO DO - Scan also User folder Documents/UnrealEngine/Python or something define in Engine.ini (see FBX test builder)

	// Find all files in the /Content/Python directory
	TArray<FString> FilesInDirectory;
	IFileManager::Get().FindFilesRecursive(FilesInDirectory, *PythonTestsDir, TEXT("*.*"), true, false);

	// Scan all the found files, use only test_*.py file
	for (const FString& Filename:FilesInDirectory)
	{
		FString Ext = FPaths::GetExtension(Filename, true);
		if (Ext.Compare(TEXT(".py"), ESearchCase::IgnoreCase) == 0)
		{
			FString FileBaseName = FPaths::GetBaseFilename(Filename);
			if (FileBaseName.Len() < 9 || !FileBaseName.StartsWith(TEXT("test_")))
			{
				// test script files must start with 'test_'
				continue;
			}

			OutBeautifiedNames.Add(BeautifyPath(Filename));
			OutTestCommands.Add(Filename);

		}
	}

}

bool FPythonAutomationTest::RunTest(const FString& Parameters)
{
	bool Result = false;

	if (IPythonScriptPlugin::Get()->IsPythonAvailable())
	{
		UPyAutomationTestLibrary::ResetPyLatentCommand();
		CleanUpPythonScheduler();

		FPythonCommandEx PythonCommand;
		PythonCommand.Command = *FString::Printf(TEXT("\"%s\""), *Parameters); // Account for space in path
		PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
		Result = IPythonScriptPlugin::Get()->ExecPythonCommandEx(PythonCommand);

		float Timout = UPyAutomationTestLibrary::GetPyLatentCommandTimeout();
		ADD_LATENT_AUTOMATION_COMMAND(FIsRunningPyLatentCommand(Timout));
	}
	else
	{
		AddError(TEXT("Python plugin is not available."));
	}

	return Result;
}


#undef LOCTEXT_NAMESPACE
