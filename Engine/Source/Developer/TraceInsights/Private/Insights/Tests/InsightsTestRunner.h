// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

#if !UE_BUILD_SHIPPING && !WITH_EDITOR

#include "Insights/IUnrealInsightsModule.h"

DECLARE_LOG_CATEGORY_EXTERN(InsightsTestRunner, Log, All);

class TRACEINSIGHTS_API FInsightsTestRunner : public TSharedFromThis<FInsightsTestRunner>, public IInsightsComponent
{
public:
	void ScheduleCommand(const FString& InCmd);
	virtual ~FInsightsTestRunner();
	virtual void Initialize(IUnrealInsightsModule& InsightsModule) override;
	virtual void Shutdown() override;
	virtual void RegisterMajorTabs(IUnrealInsightsModule& InsightsModule) override;
	virtual void UnregisterMajorTabs() override;

	static TSharedPtr<FInsightsTestRunner> CreateInstance();

	static TSharedPtr<FInsightsTestRunner> Get();
	/** The delegate to be invoked when this manager ticks. */
	FTickerDelegate OnTick;

	/** Handle to the registered OnTick. */
	FDelegateHandle OnTickHandle;

	bool Tick(float DeltaTime);

	void SetAutoQuit(bool InAutoQuit) { bAutoQuit = InAutoQuit; }
	bool GetAutoQuit() const { return bAutoQuit; }

	void SetInitAutomationModules(bool InInitAutomationModules) { bInitAutomationModules = InInitAutomationModules; }
	bool GetInitAutomationModules() const { return bInitAutomationModules; }

private:
	void RunTests();

	void OnSessionAnalysisCompleted();
	TSharedRef<SDockTab> SpawnAutomationWindowTab(const FSpawnTabArgs& Args);

	FDelegateHandle SessionAnalysisCompletedHandle;

	FString CommandToExecute;

	bool bAutoQuit = false;
	bool bInitAutomationModules = false;
	bool bIsRunningTests = false;
	bool bIsAnalysisComplete = false;

	static const TCHAR* AutoQuitMsgOnComplete;

	static TSharedPtr<FInsightsTestRunner> Instance;
};

#endif //UE_BUILD_SHIPPING && !WITH_EDITOR
