// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/Engine.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"

#define LOCTEXT_NAMESPACE "NiagaraTests"

class NiagaraTestSuite
{
public:
	NiagaraTestSuite(UWorld* WorldIn, FAutomationTestBase* TestIn) : World(WorldIn), Test(TestIn)
	{}

	/*
	 * These are all the tests.
	 * New tests can be added by creating a new function and then registering it in the FNiagaraTest constructor.
	 */

	void NiagaraTest_ManualTick()
	{
		UNiagaraSystem* TestSystem = LoadObject<UNiagaraSystem>( nullptr, TEXT("/Game/Effects/ParticleSystems/Niagara/Versioning/NS_VersioningTest.NS_VersioningTest"));
		UNiagaraComponent* NiagaraComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, TestSystem, FVector(0,0,0));
		FNiagaraSystemInstanceControllerPtr InstanceController = NiagaraComponent->GetSystemInstanceController();
		FNiagaraSystemInstance* SystemInstance = InstanceController->GetSystemInstance_Unsafe();

		float Age = SystemInstance->GetAge();
		Test->TestEqual(TEXT("Starting Age"), Age, 0.0f);
		SystemInstance->AdvanceSimulation(2, 0.0625);
		
		Age = SystemInstance->GetAge();
		Test->TestEqual(TEXT("Ticked Age"), Age, 0.125f);
	}

	void NiagaraTest_WorldTick()
	{
		UNiagaraSystem* TestSystem = LoadObject<UNiagaraSystem>( nullptr, TEXT("/Game/Effects/ParticleSystems/Niagara/Versioning/NS_VersioningTest.NS_VersioningTest"));
		UNiagaraComponent* NiagaraComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, TestSystem, FVector(0,0,0));
		FNiagaraSystemInstance* SystemInstance = NiagaraComponent->GetSystemInstanceController()->GetSystemInstance_Unsafe();

		float Age = SystemInstance->GetAge();
		Test->TestEqual(TEXT("Starting Age"), Age, 0.0f);
		
		TickWorld(0.0625);
		
		Age = SystemInstance->GetAge();
		Test->TestEqual(TEXT("Ticked Age"), Age, 0.0625f);
	}

private: // test helpers
	void TickWorld(float Time)
	{
		const float step = 0.1f;
		while (Time > 0.f)
		{
			World->Tick(LEVELTICK_All, FMath::Min(Time, step));
			Time -= step;

			// This is terrible but required for subticking like this.
			// we could always cache the real GFrameCounter at the start of our tests and restore it when finished.
			GFrameCounter++;
		}
	}

	UWorld* World;
	FAutomationTestBase* Test;
};

#define ADD_TEST(Name) \
	TestFunctions.Add(&NiagaraTestSuite::Name); \
	TestFunctionNames.Add(TEXT(#Name))

class FNiagaraTest : public FAutomationTestBase
{
public:
	typedef void (NiagaraTestSuite::*TestFunc)();

	FNiagaraTest(const FString& InName) : FAutomationTestBase(InName, false)
	{
		// list all test functions here
		ADD_TEST(NiagaraTest_ManualTick);
		ADD_TEST(NiagaraTest_WorldTick);
	}

	virtual uint32 GetTestFlags() const override { return EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter; }
	virtual bool IsStressTest() const { return false; }
	virtual uint32 GetRequiredDeviceNum() const override { return 1; }

protected:
	virtual FString GetBeautifiedTestName() const override { return "Project.Functional Tests.Niagara"; }
	virtual void GetTests(TArray<FString>& OutBeautifiedNames, TArray <FString>& OutTestCommands) const override
	{
		for (const FString& TestFuncName : TestFunctionNames)
		{
			OutBeautifiedNames.Add(TestFuncName);
			OutTestCommands.Add(TestFuncName);
		}
	}

	bool RunTest(const FString& Parameters) override
	{
		// find the matching test
		TestFunc TestFunction = nullptr;
		for (int32 i = 0; i < TestFunctionNames.Num(); ++i)
		{
			if (TestFunctionNames[i] == Parameters)
			{
				TestFunction = TestFunctions[i];
				break;
			}
		}
		if (TestFunction == nullptr)
		{
			return false;
		}

		UWorld *World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext &WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);

		FURL URL;
		World->InitializeActorsForPlay(URL);
		World->BeginPlay();

		// run the matching test
		uint64 InitialFrameCounter = GFrameCounter;
		{
			NiagaraTestSuite Tester(World, this);
			(Tester.*TestFunction)();
		}
		GFrameCounter = InitialFrameCounter;

		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return true;
	}

	TArray<TestFunc> TestFunctions;
	TArray<FString> TestFunctionNames;
};

namespace
{
	// this registers the tests with the automation framework
	FNiagaraTest FNiagaraTestAutomationTestInstance(TEXT("FNiagaraTest"));
}

#undef LOCTEXT_NAMESPACE