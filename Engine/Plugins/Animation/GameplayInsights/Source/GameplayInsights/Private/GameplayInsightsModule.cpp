// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayInsightsModule.h"
#include "Features/IModularFeatures.h"
#include "Insights/ITimingViewExtender.h"
#include "Containers/Ticker.h"
#include "Modules/ModuleManager.h"
#include "Framework/Docking/LayoutExtender.h"
#include "SAnimGraphSchematicView.h"
#include "Insights/IUnrealInsightsModule.h"
#include "HAL/PlatformApplicationMisc.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "TraceServices/ITraceServicesModule.h"
#include "TraceServices/SessionService.h"
#include "Widgets/Docking/SDockTab.h"

#if WITH_EDITOR
#include "IAnimationBlueprintEditorModule.h"
#endif

#if WITH_ENGINE
#include "Engine/Engine.h"
#endif

#define LOCTEXT_NAMESPACE "GameplayInsightsModule"

namespace GameplayInsightsTabs
{
	static const FName AnimGraphSchematicView("AnimGraphSchematicView");
};

void FGameplayInsightsModule::StartupModule()
{
	IModularFeatures::Get().RegisterModularFeature(Trace::ModuleFeatureName, &GameplayTraceModule);
	IModularFeatures::Get().RegisterModularFeature(Insights::TimingViewExtenderFeatureName, &GameplayTimingViewExtender);

	TickerHandle = FTicker::GetCoreTicker().AddTicker(TEXT("GameplayInsights"), 0.0f, [this](float DeltaTime)
	{
		GameplayTimingViewExtender.TickVisualizers(DeltaTime);
		return true;
	});

#if WITH_EDITOR
	IAnimationBlueprintEditorModule& AnimationBlueprintEditorModule = FModuleManager::LoadModuleChecked<IAnimationBlueprintEditorModule>("AnimationBlueprintEditor");
	CustomDebugObjectHandle = AnimationBlueprintEditorModule.OnGetCustomDebugObjects().AddLambda([this](const IAnimationBlueprintEditor& InAnimationBlueprintEditor, TArray<FCustomDebugObject>& OutDebugList)
	{
		GameplayTimingViewExtender.GetCustomDebugObjects(InAnimationBlueprintEditor, OutDebugList);
	});

	ITraceServicesModule& TraceServicesModule = FModuleManager::LoadModuleChecked<ITraceServicesModule>("TraceServices");

	// Connect to loopback session
	TSharedPtr<Trace::ISessionService> SessionService = TraceServicesModule.GetSessionService();
	if(SessionService.IsValid())
	{
		// Connect session
		SessionService->ConnectSession(TEXT("127.0.0.1"));

		// Wait a second then attempt a connection - it takes a bit of time for sessions to get populated
		FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float InDeltaSeconds)
		{
			IUnrealInsightsModule& UnrealInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");

			// Start analysis on the latest live session
			UnrealInsightsModule.StartAnalysisForLastLiveSession();

			return false;
		}), 1.0f);
	}

	IUnrealInsightsModule& UnrealInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");

	const float DPIScaleFactor = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(10.0f, 10.0f);

	TSharedRef<FTabManager::FLayout> MajorTabsLayout = FTabManager::NewLayout("GameplayInsightsMajorLayout_v1.0")
	->AddArea
	(
		FTabManager::NewArea(1280.f * DPIScaleFactor, 720.0f * DPIScaleFactor)
		->Split
		(
			FTabManager::NewStack()
			->AddTab(FInsightsManagerTabs::TimingProfilerTabId, ETabState::ClosedTab)
		)
	);

	FInsightsMajorTabConfig TimingProfilerConfig;
	TimingProfilerConfig.TabLabel = LOCTEXT("GameplayInsightsTabName", "Gameplay Insights");
	TimingProfilerConfig.TabTooltip = LOCTEXT("GameplayInsightsTabTooltip", "Open the Gameplay Insights tab.");
	TimingProfilerConfig.Layout = FTabManager::NewLayout("GameplayInsightsTimingLayout_v1.0")
	->AddArea
	(
		FTabManager::NewPrimaryArea()
		->SetOrientation(Orient_Horizontal)
		->Split
		(
			FTabManager::NewSplitter()
			->SetOrientation(Orient_Vertical)
			->SetSizeCoefficient(0.7f)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.1f)
				->SetHideTabWell(true)
				->AddTab(FTimingProfilerTabs::FramesTrackID, ETabState::OpenedTab)
			)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.9f)
				->SetHideTabWell(true)
				->AddTab(FTimingProfilerTabs::TimingViewID, ETabState::OpenedTab)
			)
		)
		->Split
		(
			FTabManager::NewStack()
			->SetSizeCoefficient(0.3f)
			->AddTab(GameplayInsightsTabs::AnimGraphSchematicView, ETabState::ClosedTab)
		)
	);

	UnrealInsightsModule.OnMajorTabCreated().AddLambda([this](const FName& InMajorTabId, TSharedPtr<FTabManager> InTabManager)
	{
		if(InMajorTabId == FInsightsManagerTabs::TimingProfilerTabId)
		{
			WeakTimingProfilerTabManager = InTabManager;
		}
	});

	TimingProfilerConfig.WorkspaceGroup = WorkspaceMenu::GetMenuStructure().GetDeveloperToolsProfilingCategory();

	UnrealInsightsModule.RegisterMajorTabConfig(FInsightsManagerTabs::TimingProfilerTabId, TimingProfilerConfig);
	UnrealInsightsModule.RegisterMajorTabConfig(FInsightsManagerTabs::StartPageTabId, FInsightsMajorTabConfig::Unavailable());
	UnrealInsightsModule.RegisterMajorTabConfig(FInsightsManagerTabs::SessionInfoTabId, FInsightsMajorTabConfig::Unavailable());
	UnrealInsightsModule.RegisterMajorTabConfig(FInsightsManagerTabs::LoadingProfilerTabId, FInsightsMajorTabConfig::Unavailable());
	UnrealInsightsModule.RegisterMajorTabConfig(FInsightsManagerTabs::NetworkingProfilerTabId, FInsightsMajorTabConfig::Unavailable());

	UnrealInsightsModule.CreateSessionViewer(false);
#endif
}

void FGameplayInsightsModule::ShutdownModule()
{
#if WITH_EDITOR
	IAnimationBlueprintEditorModule* AnimationBlueprintEditorModule = FModuleManager::GetModulePtr<IAnimationBlueprintEditorModule>("AnimationBlueprintEditor");
	if(AnimationBlueprintEditorModule)
	{
		AnimationBlueprintEditorModule->OnGetCustomDebugObjects().Remove(CustomDebugObjectHandle);
	}
#endif

	FTicker::GetCoreTicker().RemoveTicker(TickerHandle);

	IModularFeatures::Get().UnregisterModularFeature(Trace::ModuleFeatureName, &GameplayTraceModule);
	IModularFeatures::Get().UnregisterModularFeature(Insights::TimingViewExtenderFeatureName, &GameplayTimingViewExtender);
}

TSharedRef<SDockTab> FGameplayInsightsModule::SpawnTimingProfilerDocumentTab(const FTabManager::FSearchPreference& InSearchPreference)
{
	TSharedRef<SDockTab> NewTab = SNew(SDockTab);
	TSharedPtr<FTabManager> TimingProfilerTabManager = WeakTimingProfilerTabManager.Pin();
	if(TimingProfilerTabManager.IsValid())
	{
		TimingProfilerTabManager->InsertNewDocumentTab(GameplayInsightsTabs::AnimGraphSchematicView, InSearchPreference, NewTab);
	}
	return NewTab;
}

IMPLEMENT_MODULE(FGameplayInsightsModule, GameplayInsights);

#undef LOCTEXT_NAMESPACE