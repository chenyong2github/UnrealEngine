// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/LatentActionManager.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AutomationScreenshotOptions.h"
#include "HAL/IConsoleManager.h"
#include "Templates/UniquePtr.h"

#include "AutomationBlueprintFunctionLibrary.generated.h"

class ACameraActor;

/**
 * FAutomationTaskStatusBase - abstract class for task status
 */
class FAutomationTaskStatusBase
{
public:
	virtual ~FAutomationTaskStatusBase() = default;

	bool IsDone() const { return Done; };
	virtual void SetDone() { Done = true; };

protected:
	bool Done;
};

/**
 * UAutomationEditorTask
 */
UCLASS(BlueprintType, Transient)
class FUNCTIONALTESTING_API UAutomationEditorTask : public UObject
{
	GENERATED_BODY()

public:
	virtual ~UAutomationEditorTask() = default;

	/** Query if the Editor task is done  */
	UFUNCTION(BlueprintCallable, Category = "Automation")
	bool IsTaskDone() const;

	/** Query if a task was setup */
	UFUNCTION(BlueprintCallable, Category = "Automation")
	bool IsValidTask() const;

	void BindTask(TUniquePtr<FAutomationTaskStatusBase> inTask);

private:
	TUniquePtr<FAutomationTaskStatusBase> Task;
};

/**
 * 
 */
UCLASS(meta=(ScriptName="AutomationLibrary"))
class FUNCTIONALTESTING_API UAutomationBlueprintFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()
	
public:
	static void FinishLoadingBeforeScreenshot();

	static bool TakeAutomationScreenshotInternal(UObject* WorldContextObject, const FString& Name, const FString& Notes, FAutomationScreenshotOptions Options);

	static FIntPoint GetAutomationScreenshotSize(const FAutomationScreenshotOptions& Options);

	/**
	 * Takes a screenshot of the game's viewport.  Does not capture any UI.
	 */
	UFUNCTION(BlueprintCallable, Category = "Automation", meta = (Latent, HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject", LatentInfo = "LatentInfo", Name = "" ))
	static void TakeAutomationScreenshot(UObject* WorldContextObject, FLatentActionInfo LatentInfo, const FString& Name, const FString& Notes, const FAutomationScreenshotOptions& Options);

	/**
	 * Takes a screenshot of the game's viewport, from a particular camera actors POV.  Does not capture any UI.
	 */
	UFUNCTION(BlueprintCallable, Category = "Automation", meta = (Latent, HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject", LatentInfo = "LatentInfo", NameOverride = "" ))
	static void TakeAutomationScreenshotAtCamera(UObject* WorldContextObject, FLatentActionInfo LatentInfo, ACameraActor* Camera, const FString& NameOverride, const FString& Notes, const FAutomationScreenshotOptions& Options);

	/**
	 * 
	 */
	static bool TakeAutomationScreenshotOfUI_Immediate(UObject* WorldContextObject, const FString& Name, const FAutomationScreenshotOptions& Options);

	UFUNCTION(BlueprintCallable, Category = "Automation", meta = ( Latent, HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject", LatentInfo = "LatentInfo", NameOverride = "" ))
	static void TakeAutomationScreenshotOfUI(UObject* WorldContextObject, FLatentActionInfo LatentInfo, const FString& Name, const FAutomationScreenshotOptions& Options);

	UFUNCTION(BlueprintCallable, Category = "Automation", meta = (HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
	static void EnableStatGroup(UObject* WorldContextObject, FName GroupName);

	UFUNCTION(BlueprintCallable, Category = "Automation", meta = (HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
	static void DisableStatGroup(UObject* WorldContextObject, FName GroupName);

	UFUNCTION(BlueprintCallable, Category = "Automation")
	static float GetStatIncAverage(FName StatName);

	UFUNCTION(BlueprintCallable, Category = "Automation")
	static float GetStatIncMax(FName StatName);

	UFUNCTION(BlueprintCallable, Category = "Automation")
	static float GetStatExcAverage(FName StatName);

	UFUNCTION(BlueprintCallable, Category = "Automation")
	static float GetStatExcMax(FName StatName);

	UFUNCTION(BlueprintCallable, Category = "Automation")
	static float GetStatCallCount(FName StatName);

	/**
	 * Lets you know if any automated tests are running, or are about to run and the automation system is spinning up tests.
	 */
	UFUNCTION(BlueprintPure, Category="Automation")
	static bool AreAutomatedTestsRunning();

	UFUNCTION(BlueprintCallable, Category = "Automation", meta = (Latent, HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject", LatentInfo = "LatentInfo"))
	static void AutomationWaitForLoading(UObject* WorldContextObject, FLatentActionInfo LatentInfo);

	/**
	* take high res screenshot in editor.
	*/
	UFUNCTION(BlueprintCallable, Category = "Automation", meta = (AdvancedDisplay="Camera, bMaskEnabled, bCaptureHDR, ComparisonTolerance, ComparisonNotes"))
	static UAutomationEditorTask* TakeHighResScreenshot(int32 ResX, int32 ResY, FString Filename, ACameraActor* Camera = nullptr, bool bMaskEnabled = false, bool bCaptureHDR = false, EComparisonTolerance ComparisonTolerance = EComparisonTolerance::Low, FString ComparisonNotes = TEXT(""));

	/**
	 * 
	 */
	UFUNCTION(BlueprintPure, Category="Automation")
	static FAutomationScreenshotOptions GetDefaultScreenshotOptionsForGameplay(EComparisonTolerance Tolerance = EComparisonTolerance::Low, float Delay = 0.2);

	/**
	 * 
	 */
	UFUNCTION(BlueprintPure, Category="Automation")
	static FAutomationScreenshotOptions GetDefaultScreenshotOptionsForRendering(EComparisonTolerance Tolerance = EComparisonTolerance::Low, float Delay = 0.2);

	/**
	 * Mute the report of log error and warning matching a pattern during an automated test
	 */
	UFUNCTION(BlueprintCallable, Category = "Automation", meta = (AdvancedDisplay = "Occurrences, ExactMatch"))
	static void AddExpectedLogError(FString ExpectedPatternString, int32 Occurrences = 1, bool ExactMatch = false);

	/**
	 * Sets all other settings based on an overall value
	 * @param Value 0:Cinematic, 1:Epic...etc.
	 */
	UFUNCTION(BlueprintCallable, Category = "Automation", meta = (HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
	static void SetScalabilityQualityLevelRelativeToMax(UObject* WorldContextObject, int32 Value = 1);

	UFUNCTION(BlueprintCallable, Category = "Automation", meta = (HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
	static void SetScalabilityQualityToEpic(UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "Automation", meta = (HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
	static void SetScalabilityQualityToLow(UObject* WorldContextObject);
};

#if (WITH_DEV_AUTOMATION_TESTS || WITH_PERF_AUTOMATION_TESTS)

template<typename T>
class FConsoleVariableSwapperTempl
{
public:
	FConsoleVariableSwapperTempl(FString InConsoleVariableName);

	void Set(T Value);

	void Restore();

private:
	bool bModified;
	FString ConsoleVariableName;

	T OriginalValue;
};

class FAutomationTestScreenshotEnvSetup
{
public:
	FAutomationTestScreenshotEnvSetup();
	~FAutomationTestScreenshotEnvSetup();

	// Disable AA, auto-exposure, motion blur, contact shadow if InOutOptions.bDisableNoisyRenderingFeatures.
	// Update screenshot comparison tolerance stored in InOutOptions.
	// Set visualization buffer name if required.
	void Setup(UWorld* InWorld, FAutomationScreenshotOptions& InOutOptions);

	/** Restore the old settings. */
	void Restore();

private:
	FConsoleVariableSwapperTempl<int32> DefaultFeature_AntiAliasing;
	FConsoleVariableSwapperTempl<int32> DefaultFeature_AutoExposure;
	FConsoleVariableSwapperTempl<int32> DefaultFeature_MotionBlur;
	FConsoleVariableSwapperTempl<int32> PostProcessAAQuality;
	FConsoleVariableSwapperTempl<int32> MotionBlurQuality;
	FConsoleVariableSwapperTempl<int32> ScreenSpaceReflectionQuality;
	FConsoleVariableSwapperTempl<int32> EyeAdaptationQuality;
	FConsoleVariableSwapperTempl<int32> ContactShadows;
	FConsoleVariableSwapperTempl<float> TonemapperGamma;
	FConsoleVariableSwapperTempl<float> TonemapperSharpen;
	FConsoleVariableSwapperTempl<float> SecondaryScreenPercentage;

	TWeakObjectPtr<UWorld> WorldPtr;
	TSharedPtr< class FAutomationViewExtension, ESPMode::ThreadSafe > AutomationViewExtension;
};

#endif
