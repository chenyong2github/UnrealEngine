// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestListeners/ConsoleListener.h"
#include "Containers/UnrealString.h"

using namespace Catch;

class ConsoleListener : public EventListenerBase, public FOutputDevice
{
public:
	ConsoleListener(IConfig const* config): EventListenerBase(config)
	{
		m_preferences.shouldRedirectStdOut = true;
	}
private:
	void testRunStarting(TestRunInfo const& testRunInfo) override {
		// Register this event listener as an output device to enable reporting of UE_LOG, ensure etc
		// Note: For UE_LOG(...) reporting the user must pass the "--log" command line argument, but this is not required for ensure reporting
		GLog->AddOutputDevice(this);
	}

	void testRunEnded(TestRunStats const& testRunStats) override {
		GLog->RemoveOutputDevice(this);
	}

	void platformPrint(const TCHAR* Message)
	{
#if defined(PLATFORM_WINDOWS)
		FGenericPlatformMisc::LocalPrint(Message);
#else
		FPlatformMisc::LocalPrint(Message);
#endif
	}

	// FOutputDevice interface
	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type LogVerbosity, const class FName& Category) override
	{
		// By default only log warnings/errors. If the user passes the command line argument "--debug" be more verbose
		ELogVerbosity::Type DesiredLogVerbosity = ELogVerbosity::Warning;
		if (bGDebug)
		{
			DesiredLogVerbosity = ELogVerbosity::VeryVerbose;
		}

		// TODO It might be nicer to increase the desired logging verbosity using the "-v high"/"--verbosity high"
		// Catch option and changing condition above to `getCurrentContext().getConfig()->verbosity() == Verbosity::High`
		// I tried this but unfortunately it didn't work, passing that option makes catch complain with the error message:
		// "Verbosity level not supported by this reporter"

		if (LogVerbosity <= DesiredLogVerbosity)
		{
			FString OutputMessage = FString::Printf(TEXT("%s(%s): %s\n"), *FText::FromName(Category).ToString(), ToString(LogVerbosity), V);
			platformPrint(OutputMessage.GetCharArray().GetData());
		}
	}

	virtual bool CanBeUsedOnMultipleThreads() const
	{
		return true;
	}

	virtual bool CanBeUsedOnPanicThread() const
	{
		return true;
	}
	// End of FOutputDevice interface

	void testCaseStarting(TestCaseInfo  const& TestInfo) override {
		if (bGDebug)
		{
			FString InfoTestCaseStarting = FString::Printf(TEXT("%s:%d with tags %s\n"), *FString(TestInfo.lineInfo.file), TestInfo.lineInfo.line, *FString(TestInfo.tagsAsString().c_str()));			
			platformPrint(InfoTestCaseStarting.GetCharArray().GetData());
		}
	}

	// TODO: Use UE structured logging for build machines
	void testCaseEnded(TestCaseStats const& testCaseStats) override {
		if (testCaseStats.totals.testCases.failed > 0)
		{
			FString ErrorTestCase = FString::Printf(TEXT("* Error: Test case \"%s\" failed \n"), *FString(testCaseStats.testInfo->name.c_str()));
			platformPrint(ErrorTestCase.GetCharArray().GetData());
		}
	}

	// TODO: Use UE structured logging for build machines
	void assertionEnded(AssertionStats const& assertionStats) override {
		if (!assertionStats.assertionResult.succeeded()) {
			FString ErrorAssertion = FString::Printf(TEXT("* Error: Assertion \"%s\" failed at %s:%d\n"), *FString(assertionStats.assertionResult.getExpression().c_str()), *FString(assertionStats.assertionResult.getSourceInfo().file), assertionStats.assertionResult.getSourceInfo().line);
			platformPrint(ErrorAssertion.GetCharArray().GetData());
		}
	}
};
CATCH_REGISTER_LISTENER(ConsoleListener);