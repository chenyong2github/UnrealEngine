// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestListeners/ConsoleListener.h"

using namespace Catch;

class ConsoleListener : public EventListenerBase, public FOutputDevice
{
	using EventListenerBase::EventListenerBase; // inherit constructor
private:
	void testRunStarting(TestRunInfo const& testRunInfo) override {
		// Register this event listener as an output device to enable reporting of UE_LOG, ensure etc
		// Note: For UE_LOG(...) reporting the user must pass the "--log" command line argument, but this is not required for ensure reporting
		GLog->AddOutputDevice(this);
	}

	void testRunEnded(TestRunStats const& testRunStats) override {
		GLog->RemoveOutputDevice(this);
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

		if (LogVerbosity <= DesiredLogVerbosity && !GLog->GetSuppressEventTag())
		{
			std::cout << TCHAR_TO_UTF8(*FText::FromName(Category).ToString())
				<< "("
				<< TCHAR_TO_UTF8(ToString(LogVerbosity))
				<< "): "
				<< TCHAR_TO_UTF8(V)
				<< "\n";
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
			std::cout << TestInfo.lineInfo.file << ":" << TestInfo.lineInfo.line << " with tags " << TestInfo.tagsAsString() << " \n";
		}
	}

	void testCaseEnded(TestCaseStats const& testCaseStats) override {
		if (testCaseStats.totals.testCases.failed > 0)
		{
			std::cout << "* Error: Test case \"" << testCaseStats.testInfo->name << "\" failed \n";
		}
	}

	void assertionEnded(AssertionStats const& assertionStats) override {
		if (!assertionStats.assertionResult.succeeded()) {
			std::cout << "* Error: Assertion \"" << assertionStats.assertionResult.getExpression() << "\" failed at " << assertionStats.assertionResult.getSourceInfo().file << ": " << assertionStats.assertionResult.getSourceInfo().line << "\n";
		}
	}
};
CATCH_REGISTER_LISTENER(ConsoleListener);