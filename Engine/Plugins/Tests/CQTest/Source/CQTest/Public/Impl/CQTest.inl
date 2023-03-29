// Copyright Epic Games, Inc. All Rights Reserved.

namespace
{
	template <typename AsserterType>
	struct TBeforeTestCommand : public IAutomationLatentCommand
	{
		explicit TBeforeTestCommand(TBaseTest<AsserterType>& CurrentTest)
			: CurrentTest(CurrentTest) {}

		bool Update() override
		{
			CurrentTest.Setup();
			if (auto LatentActions = CurrentTest.TestCommandBuilder.Build())
			{
				CurrentTest.AddCommand(LatentActions);
			}
			return true;
		}

		TBaseTest<AsserterType>& CurrentTest;
	};

	template <typename AsserterType>
	struct TRunTestCommand : public IAutomationLatentCommand
	{
		TRunTestCommand(TBaseTest<AsserterType>& CurrentTest, const FString& RequestedTest, const TTestRunner<AsserterType>& TestRunner)
			: CurrentTest(CurrentTest)
			, RequestedTest(RequestedTest)
			, TestRunner(TestRunner)
		{
		}

		bool Update() override
		{
			if (TestRunner.HasAnyErrors())
			{
				return true; // skip run if errors in BeforeTest
			}

			CurrentTest.RunTest(RequestedTest);
			if (auto LatentActions = CurrentTest.TestCommandBuilder.Build())
			{
				CurrentTest.AddCommand(LatentActions);
			}
			return true;
		}

		TBaseTest<AsserterType>& CurrentTest;
		const FString& RequestedTest;
		const TTestRunner<AsserterType>& TestRunner;
	};

	template <typename AsserterType>
	struct TAfterTestCommand : public IAutomationLatentCommand
	{
		explicit TAfterTestCommand(TBaseTest<AsserterType>& CurrentTest)
			: CurrentTest(CurrentTest) {}

		bool Update() override
		{
			CurrentTest.TearDown();
			if (auto LatentActions = CurrentTest.TestCommandBuilder.Build())
			{
				CurrentTest.AddCommand(LatentActions);
			}
			return true;
		}

		TBaseTest<AsserterType>& CurrentTest;
	};

	template <typename AsserterType>
	struct TTearDownRunner : public IAutomationLatentCommand
	{
		explicit TTearDownRunner(TTestRunner<AsserterType>& TestRunner)
			: TestRunner(TestRunner) {}

		bool Update() override
		{
			TestRunner.CurrentTestPtr = nullptr;
			return true;
		}

		TTestRunner<AsserterType>& TestRunner;
	};
} // namespace


template <typename AsserterType>
inline TTestRunner<AsserterType>::TTestRunner(FString InName, int32 InLineNumber, const char* InFileName, FString InTestDir, uint32 InTestFlags, TTestInstanceGenerator<AsserterType> InFactory)
	: FAutomationTestBase(InName, true)
	, LineNumber(InLineNumber)
	, FileName(InFileName)
	, TestDir(InTestDir)
	, TestFlags(InTestFlags)
	, TestInstanceFactory(InFactory)
{
	bInitializing = true;
	if (TestDir.Equals(GenerateTestDirectory))
	{
		TestDir = TestDirectoryGenerator::Generate(FileName);
	}
	else if (TestDir.Contains(TEXT("[GenerateTestDirectory]"), ESearchCase::IgnoreCase))
	{
		TestDir = TestDir.Replace(TEXT("[GenerateTestDirectory]"), *TestDirectoryGenerator::Generate(FileName), ESearchCase::IgnoreCase);
	}

	CurrentTestPtr = TestInstanceFactory(*this);

	bInitializing = false;
}

template <typename AsserterType>
inline bool TTestRunner<AsserterType>::RunTest(const FString& RequestedTest)
{
	if (RequestedTest.Len() == 0)
	{
		return false;
	}

	CurrentTestPtr = TestInstanceFactory(*this);
	check(CurrentTestPtr != nullptr);
	auto& CurrentTest = *CurrentTestPtr;

	auto Before = MakeShared<TBeforeTestCommand<AsserterType>>(CurrentTest);
	auto Run = MakeShared<TRunTestCommand<AsserterType>>(CurrentTest, RequestedTest, *this);
	auto After = MakeShared<TAfterTestCommand<AsserterType>>(CurrentTest);
	auto TearDown = MakeShared<TTearDownRunner<AsserterType>>(*this);

	auto RemainingSteps = TArray<TSharedPtr<IAutomationLatentCommand>>{ Before, Run, After, TearDown };

	while (RemainingSteps.Num() > 0)
	{
		RemainingSteps[0]->Update();
		RemainingSteps.RemoveAt(0);

		if (CurrentTestPtr != nullptr && CurrentTest.bHasLatentActions && RemainingSteps.Num() > 1)
		{
			// Ensure that all latent commands are run before adding the next step
			RemainingSteps.Insert(MakeShared<FExecute>([]() { return true; }), 0);
			CurrentTest.AddCommand(new FRunSequence(RemainingSteps));
			return true;
		}
	}

	return true;
}

template <typename AsserterType>
inline FString TTestRunner<AsserterType>::GetBeautifiedTestName() const
{
	return FString::Printf(TEXT("%s.%s"), *TestDir, *TestName);
}

template <typename AsserterType>
inline uint32 TTestRunner<AsserterType>::GetRequiredDeviceNum() const
{
	return 1;
}

template <typename AsserterType>
inline uint32 TTestRunner<AsserterType>::GetTestFlags() const
{
	return TestFlags;
}

template <typename AsserterType>
inline FString TTestRunner<AsserterType>::GetTestSourceFileName() const
{
	return FileName;
}

template <typename AsserterType>
inline int32 TTestRunner<AsserterType>::GetTestSourceFileLine() const
{
	return LineNumber;
}

template <typename AsserterType>
inline int32 TTestRunner<AsserterType>::GetTestSourceFileLine(const FString& Name) const
{
	FString TestParam(Name);
	int32 Pos = Name.Find(TEXT(" "));
	if (Pos != INDEX_NONE)
	{
		TestParam = Name.RightChop(Pos + 1);
	}
	if (TestNames.Contains(TestParam))
	{
		return TestLineNumbers[TestParam];
	}
	return GetTestSourceFileLine();
}

template <typename AsserterType>
inline void TTestRunner<AsserterType>::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	for (const auto& testName : TestNames)
	{
		OutBeautifiedNames.Add(testName);
		OutTestCommands.Add(testName);
	}
}

/////////////

template <typename Derived, typename AsserterType>
inline void TTest<Derived, AsserterType>::RunTest(const FString& TestName)
{
	auto TestMethod = Methods[TestName];
	Derived& Self = static_cast<Derived&>(*this);

	(Self.*(TestMethod))();
}

template <typename AsserterType>
inline TBaseTest<AsserterType>::TBaseTest(FAutomationTestBase& TestRunner, bool bInitializing)
	: TestCommandBuilder(FTestCommandBuilder{})
	, TestRunner(TestRunner)
	, bInitializing(bInitializing)
	, Assert(AsserterType{ TestRunner })
{
}

template <typename AsserterType>
inline void TBaseTest<AsserterType>::AddCommand(IAutomationLatentCommand* Cmd)
{
	TestRunner.AddCommand(Cmd);
	bHasLatentActions = true;
}

template <typename AsserterType>
inline void TBaseTest<AsserterType>::AddCommand(TSharedPtr<IAutomationLatentCommand> Cmd)
{
	FAutomationTestFramework::Get().EnqueueLatentCommand(Cmd);
	bHasLatentActions = true;
}
