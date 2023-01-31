// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ICVFXTestControllerBase.h"

#include "HAL/IConsoleManager.h"
#include "ICVFXTestControllerAutoTest.generated.h"

class ULocalPlayer;

UENUM()
enum class EICVFXAutoTestState : uint8
{
	InitialLoad,
	Soak,
	Idle ,
	Finished,
	Shutdown,

	MAX
};

class FICVFXAutoTestState
{
public:
	using Super = FICVFXAutoTestState;

	FICVFXAutoTestState() = delete;
	FICVFXAutoTestState(class UICVFXTestControllerAutoTest* const TestController) : Controller(TestController) {}
	virtual ~FICVFXAutoTestState() {}

	virtual void Start(const EICVFXAutoTestState PrevState);
	virtual void End(const EICVFXAutoTestState NewState) {}
	virtual void Tick(const float TimeDelta);

	double GetTestStateTime() const 
	{
		return TimeSinceStart;
	}

protected:
	class UICVFXTestControllerAutoTest* Controller;

private:
	double TimeSinceStart = 0.0;
};

UCLASS()
class UICVFXTestControllerAutoTest : public UICVFXTestControllerBase
{
	GENERATED_BODY()

public:
	FString GetStateName(const EICVFXAutoTestState State) const;
	FICVFXAutoTestState& GetTestState() const;
	FICVFXAutoTestState& SetTestState(const EICVFXAutoTestState NewState);

	virtual void EndICVFXTest(const int32 ExitCode=0) override;

protected:
	virtual void OnInit() override;
	virtual void OnPreMapChange() override;
	virtual void OnTick(float TimeDelta) override;
	virtual void BeginDestroy() override;

private:
	virtual void UnbindAllDelegates() override;

	FICVFXAutoTestState* States[(uint8)EICVFXAutoTestState::MAX];
	float StateTimeouts[(uint8)EICVFXAutoTestState::MAX];
	EICVFXAutoTestState CurrentState;

	virtual void OnPreWorldInitialize(UWorld* World) override;

	UFUNCTION()
	void OnWorldBeginPlay();

	UFUNCTION()
	void OnGameStateSet(AGameStateBase* const GameStateBase);
	
	FConsoleVariableSinkHandle SoakTimeSink;

	UFUNCTION()
	void OnSoakTimeChanged();
};