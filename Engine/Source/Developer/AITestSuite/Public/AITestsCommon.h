// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/AutomationTest.h"
#include "TestLogger.h"
#include "Engine/EngineBaseTypes.h"


DECLARE_LOG_CATEGORY_EXTERN(LogAITestSuite, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogBehaviorTreeTest, Log, All);

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FAITestCommand_WaitSeconds, float, Duration);

class FAITestCommand_WaitOneTick : public IAutomationLatentCommand
{
public: 
	FAITestCommand_WaitOneTick()
		: bAlreadyRun(false)
	{} 
	virtual bool Update() override;
private: 
	bool bAlreadyRun;
};


namespace FAITestHelpers
{
	UWorld* GetWorld();
	static const float TickInterval = 1.f / 30;

	void UpdateFrameCounter();
	uint64 FramesCounter();
}

struct AITESTSUITE_API FAITestBase
{
private:
	// internals
	TArray<UObject*> SpawnedObjects;
	uint32 bTearedDown : 1;
protected:
	FAutomationTestBase* TestRunner;

	FAITestBase() : bTearedDown(false), TestRunner(nullptr)
	{}

	template<typename ClassToSpawn>
	ClassToSpawn* NewAutoDestroyObject()
	{
		ClassToSpawn* ObjectInstance = NewObject<ClassToSpawn>();
		ObjectInstance->AddToRoot();
		SpawnedObjects.Add(ObjectInstance);
		return ObjectInstance;
	}

	void AddAutoDestroyObject(UObject& ObjectRef);
	virtual UWorld& GetWorld() const;

	FAutomationTestBase& GetTestRunner() const { check(TestRunner); return *TestRunner; }

public:

	virtual void SetTestRunner(FAutomationTestBase& AutomationTestInstance) { TestRunner = &AutomationTestInstance; }

	// interface
	virtual ~FAITestBase();
	/** @return true if setup was completed successfully, false otherwise (which will result in failing the test instance). */
	virtual bool SetUp() { return true; }
	/** @return true to indicate that the test is done. */
	virtual bool Update() { return false; } 
	/** @return false to indicate an issue with test execution. Will signal to automation framework this test instance failed. */
	virtual bool InstantTest() { return false;}
	// it's essential that overriding functions call the super-implementation. Otherwise the check in ~FAITestBase will fail.
	virtual void TearDown();
};

DEFINE_EXPORTED_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(AITESTSUITE_API, FAITestCommand_SetUpTest, FAITestBase*, AITest);
DEFINE_EXPORTED_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(AITESTSUITE_API, FAITestCommand_PerformTest, FAITestBase*, AITest);
DEFINE_EXPORTED_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(AITESTSUITE_API, FAITestCommand_TearDownTest, FAITestBase*, AITest);

// @note that TestClass needs to derive from FAITestBase
#define IMPLEMENT_AI_LATENT_TEST(TestClass, PrettyName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass##_Runner, PrettyName, (EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)) \
	bool TestClass##_Runner::RunTest(const FString& Parameters) \
	{ \
		/* spawn test instance. Setup should be done in test's constructor */ \
		TestClass* TestInstance = new TestClass(); \
		TestInstance->SetTestRunner(*this); \
		/* set up */ \
		ADD_LATENT_AUTOMATION_COMMAND(FAITestCommand_SetUpTest(TestInstance)); \
		/* run latent command to update */ \
		ADD_LATENT_AUTOMATION_COMMAND(FAITestCommand_PerformTest(TestInstance)); \
		/* run latent command to tear down */ \
		ADD_LATENT_AUTOMATION_COMMAND(FAITestCommand_TearDownTest(TestInstance)); \
		return true; \
	} 

#define IMPLEMENT_AI_INSTANT_TEST(TestClass, PrettyName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass##Runner, PrettyName, (EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)) \
	bool TestClass##Runner::RunTest(const FString& Parameters) \
	{ \
		bool bSuccess = false; \
		/* spawn test instance. */ \
		TestClass* TestInstance = new TestClass(); \
		TestInstance->SetTestRunner(*this); \
		/* set up */ \
		if (TestInstance->SetUp()) \
		{ \
			/* call the instant-test code */ \
			bSuccess = TestInstance->InstantTest(); \
			/* tear down */ \
			TestInstance->TearDown(); \
		}\
		delete TestInstance; \
		return bSuccess; \
	} 

/** 
 *	This macro allows one to implement a whole set of simple tests that share common setups. To use it first implement
 *	a struct that builds the said common setup. Like so:
 *
 *		struct FMyCommonSetup : public FAITestBase
 *		{
 *			virtual bool SetUp() override
 *			{
 *				// your test common setup build code here
 *
 *				// return false if setup fails and the test needs to be aborted
 *				return true; 
 *			}
 *		};
 *	
 *	Once that's done you can implement a specific test using this setup class like so:
 *
 *	IMPLEMENT_INSTANT_TEST_WITH_FIXTURE(FMyCommonSetup, "System.Engine.AI.MyTestGroup", ThisSpecificTestName)
 *	{
 *		// your test code here
 *
 *		// return false to indicate the whole test instance failed for some reason
 *		return true;
 *	}
 */
#define IMPLEMENT_INSTANT_TEST_WITH_FIXTURE(Fixture, PrettyGroupNameString, TestExperiment) \
	struct Fixture##_##TestExperiment : public Fixture \
	{ \
		virtual bool InstantTest() override; \
	}; \
	IMPLEMENT_AI_INSTANT_TEST(Fixture##_##TestExperiment, PrettyGroupNameString "." # TestExperiment) \
	bool Fixture##_##TestExperiment::InstantTest()
//----------------------------------------------------------------------//
// Specific test types
//----------------------------------------------------------------------//
template<class TComponent>
struct FAITest_SimpleComponentBasedTest : public FAITestBase
{
	FTestLogger<int32> Logger;
	TComponent* Component;

	FAITest_SimpleComponentBasedTest()
	{
		Component = NewAutoDestroyObject<TComponent>();
	}

	virtual void SetTestRunner(FAutomationTestBase& AutomationTestInstance) override
	{ 
		FAITestBase::SetTestRunner(AutomationTestInstance);
		Logger.TestRunner = TestRunner;
	}

	virtual ~FAITest_SimpleComponentBasedTest()
	{
		GetTestRunner().TestTrue(TEXT("Not all expected values has been logged"), Logger.ExpectedValues.Num() == 0 || Logger.ExpectedValues.Num() == Logger.LoggedValues.Num());
	}

	virtual bool SetUp() override
	{
		UWorld* World = FAITestHelpers::GetWorld();
		Component->RegisterComponentWithWorld(World);
		return World != nullptr;
	}

	void TickComponent()
	{
		Component->TickComponent(FAITestHelpers::TickInterval, ELevelTick::LEVELTICK_All, nullptr);
	}
};

//----------------------------------------------------------------------//
// state testing macros, valid in FTestAIBase (and subclasses') methods 
// Using these macros makes sure the test function fails if the assertion
// fails making sure the rest of the test relying on given condition being 
// true doesn't crash
//----------------------------------------------------------------------//
#define AITEST_TRUE(What, Value)\
	if (!GetTestRunner().TestTrue(What, Value))\
	{\
		return false;\
	}

#define AITEST_FALSE(What, Value)\
	if (!GetTestRunner().TestFalse(What, Value))\
	{\
		return false;\
	}

#define AITEST_NULL(What, Pointer)\
	if (!GetTestRunner().TestNull(What, Pointer))\
	{\
		return false;\
	}

#define AITEST_NOT_NULL(What, Pointer)\
	if (!GetTestRunner().TestNotNull(What, Pointer))\
	{\
		return false;\
	}

namespace FTestHelpers
{
	template<typename T1, typename T2>
	inline bool TestEqual(const FString& Description, T1 Expression, T2 Expected, FAutomationTestBase& This)
	{
		This.TestEqual(*Description, Expression, Expected);
		return Expression == Expected;
	}

	template<typename T1, typename T2>
	inline bool TestEqual(const FString& Description, T1* Expression, T2* Expected, FAutomationTestBase& This)
	{
		This.TestEqual(*Description, reinterpret_cast<uint64>(Expression), reinterpret_cast<uint64>(Expected));
		return Expression == Expected;
	}

	template<typename T1, typename T2>
	inline bool TestNotEqual(const FString& Description, T1 Expression, T2 Expected, FAutomationTestBase& This)
	{
		This.TestNotEqual(*Description, Expression, Expected);
		return Expression != Expected;
	}

	template<typename T1, typename T2>
	inline bool TestNotEqual(const FString& Description, T1* Expression, T2* Expected, FAutomationTestBase& This)
	{
		This.TestNotEqual(*Description, reinterpret_cast<uint64>(Expression), reinterpret_cast<uint64>(Expected));
		return Expression != Expected;
	}
}

#define AITEST_EQUAL(What, Actual, Expected)\
	if (!FTestHelpers::TestEqual(What, Actual, Expected, GetTestRunner()))\
	{\
		return false;\
	}

#define AITEST_NOT_EQUAL(What, Actual, Expected)\
	if (!FTestHelpers::TestNotEqual(What, Actual, Expected, GetTestRunner()))\
	{\
		return false;\
	}
