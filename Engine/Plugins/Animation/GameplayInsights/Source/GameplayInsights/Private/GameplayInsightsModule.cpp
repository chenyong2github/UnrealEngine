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
#include "Widgets/Docking/SDockTab.h"
#include "Trace/StoreClient.h"
#include "Stats/Stats.h"
#include "ObjectPropertyTrace.h"
#include "SAnimationCurvesView.h"
#include "SBlendWeightsView.h"
#include "SMontageView.h"
#include "SObjectPropertiesView.h"
#include "SNotifiesView.h"

#if WITH_EDITOR
#include "IAnimationBlueprintEditorModule.h"
#include "Editor.h"
#include "ToolMenus.h"
#include "Engine/Selection.h"
#include "SubobjectEditorMenuContext.h"
#include "GameplayInsightsStyle.h"
#include "SSubobjectInstanceEditor.h"
#include "ProfilingDebugging/TraceAuxiliary.h"

#endif

#if WITH_ENGINE
#include "Engine/Engine.h"
#endif

#define LOCTEXT_NAMESPACE "GameplayInsightsModule"

const FName GameplayInsightsTabs::DocumentTab("DocumentTab");

void FGameplayInsightsModule::StartupModule()
{
	IModularFeatures::Get().RegisterModularFeature(TraceServices::ModuleFeatureName, &GameplayTraceModule);
	IModularFeatures::Get().RegisterModularFeature(Insights::TimingViewExtenderFeatureName, &GameplayTimingViewExtender);

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("GameplayInsights"), 0.0f, [this](float DeltaTime)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FGameplayInsightsModule_TickVisualizers);

		GameplayTimingViewExtender.TickVisualizers(DeltaTime);
		return true;
	});

	IUnrealInsightsModule& UnrealInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
	UnrealInsightsModule.OnMajorTabCreated().AddLambda([this](const FName& InMajorTabId, TSharedPtr<FTabManager> InTabManager)
	{
		if (InMajorTabId == FInsightsManagerTabs::TimingProfilerTabId)
		{
			WeakTimingProfilerTabManager = InTabManager;
		}
	});

#if WITH_EDITOR
	// register rewind debugger view creators
	static FObjectPropertiesViewCreator ObjectPropertiesViewCreator;
	IModularFeatures::Get().RegisterModularFeature(IRewindDebuggerViewCreator::ModularFeatureName, &ObjectPropertiesViewCreator);
	static FAnimGraphSchematicViewCreator AnimGraphSchematicViewCreator;
	IModularFeatures::Get().RegisterModularFeature(IRewindDebuggerViewCreator::ModularFeatureName, &AnimGraphSchematicViewCreator);
	static FBlendWeightsViewCreator BlendWeightsViewCreator;
	IModularFeatures::Get().RegisterModularFeature(IRewindDebuggerViewCreator::ModularFeatureName, &BlendWeightsViewCreator);
	static FMontageViewCreator MontageViewCreator;
	IModularFeatures::Get().RegisterModularFeature(IRewindDebuggerViewCreator::ModularFeatureName, &MontageViewCreator);
	static FNotifiesViewCreator NotifiesViewCreator;
	IModularFeatures::Get().RegisterModularFeature(IRewindDebuggerViewCreator::ModularFeatureName, &NotifiesViewCreator);
	static FAnimationCurvesViewCreator AnimationCurvesViewCreator;
	IModularFeatures::Get().RegisterModularFeature(IRewindDebuggerViewCreator::ModularFeatureName, &AnimationCurvesViewCreator);


	if (!IsRunningCommandlet())
	{
		IAnimationBlueprintEditorModule& AnimationBlueprintEditorModule = FModuleManager::LoadModuleChecked<IAnimationBlueprintEditorModule>("AnimationBlueprintEditor");
		CustomDebugObjectHandle = AnimationBlueprintEditorModule.OnGetCustomDebugObjects().AddLambda([this](const IAnimationBlueprintEditor& InAnimationBlueprintEditor, TArray<FCustomDebugObject>& OutDebugList)
		{
			GameplayTimingViewExtender.GetCustomDebugObjects(InAnimationBlueprintEditor, OutDebugList);
		});

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
		TimingProfilerConfig.TabLabel = LOCTEXT("AnimationInsightsTabName", "Animation Insights");
		TimingProfilerConfig.TabIcon = FSlateIcon(FGameplayInsightsStyle::Get().GetStyleSetName(), "AnimationInsights.TabIcon");

		TimingProfilerConfig.TabTooltip = LOCTEXT("AnimationInsightsTabTooltip", "Open the Animation Insights tab.");
		TimingProfilerConfig.Layout = FTabManager::NewLayout("GameplayInsightsTimingLayout_v1.2")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->AddTab(FTimingProfilerTabs::ToolbarID, ETabState::ClosedTab)
				->SetHideTabWell(true)
			)
			->Split
			(
				FTabManager::NewSplitter()
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
					->AddTab(GameplayInsightsTabs::DocumentTab, ETabState::ClosedTab)
					->AddTab(FTimingProfilerTabs::TimersID, ETabState::ClosedTab)
					->AddTab(FTimingProfilerTabs::StatsCountersID, ETabState::ClosedTab)
					->AddTab(FTimingProfilerTabs::CallersID, ETabState::ClosedTab)
					->AddTab(FTimingProfilerTabs::CalleesID, ETabState::ClosedTab)
				)
			)
			->Split
			(
				FTabManager::NewStack()
				->AddTab(FTimingProfilerTabs::LogViewID, ETabState::ClosedTab)
			)
		);

		TimingProfilerConfig.WorkspaceGroup = WorkspaceMenu::GetMenuStructure().GetDeveloperToolsProfilingCategory();

		UnrealInsightsModule.RegisterMajorTabConfig(FInsightsManagerTabs::TimingProfilerTabId, TimingProfilerConfig);
		UnrealInsightsModule.RegisterMajorTabConfig(FInsightsManagerTabs::StartPageTabId, FInsightsMajorTabConfig::Unavailable());
		UnrealInsightsModule.RegisterMajorTabConfig(FInsightsManagerTabs::SessionInfoTabId, FInsightsMajorTabConfig::Unavailable());
		UnrealInsightsModule.RegisterMajorTabConfig(FInsightsManagerTabs::LoadingProfilerTabId, FInsightsMajorTabConfig::Unavailable());
		UnrealInsightsModule.RegisterMajorTabConfig(FInsightsManagerTabs::NetworkingProfilerTabId, FInsightsMajorTabConfig::Unavailable());

		UnrealInsightsModule.SetUnrealInsightsLayoutIni(GEditorLayoutIni);

		// Create store and start analysis session - should only be done after engine has init and all plugins are loaded
		FCoreDelegates::OnFEngineLoopInitComplete.AddLambda([this]
		{
			IUnrealInsightsModule& UnrealInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
			if (!UnrealInsightsModule.GetStoreClient())
			{
#if WITH_TRACE_STORE
				UE_LOG(LogCore, Display, TEXT("GameplayInsights module auto-connecting to internal trace server..."));
				// Create the Store Service.
				FString StoreDir = FPaths::ProjectSavedDir() / TEXT("TraceSessions");
				UE::Trace::FStoreService::FDesc StoreServiceDesc;
				StoreServiceDesc.StoreDir = *StoreDir;
				StoreServiceDesc.RecorderPort = 0; // Let system decide port
				StoreServiceDesc.ThreadCount = 2;
				StoreService = TSharedPtr<UE::Trace::FStoreService>(UE::Trace::FStoreService::Create(StoreServiceDesc));

				FCoreDelegates::OnPreExit.AddLambda([this]() {
					StoreService.Reset();
				});

				// Connect to our newly created store and setup the insights module
				UnrealInsightsModule.ConnectToStore(TEXT("localhost"), StoreService->GetPort());
				UE::Trace::SendTo(TEXT("localhost"), StoreService->GetRecorderPort());
#else
				UE_LOG(LogCore, Display, TEXT("GameplayInsights module auto-connecting to local trace server..."));
				UnrealInsightsModule.ConnectToStore(TEXT("127.0.0.1"));
				const bool bConnected = FTraceAuxiliary::Start(
					FTraceAuxiliary::EConnectionType::Network,
					TEXT("127.0.0.1"),
					nullptr);
#endif // WITH_TRACE_STORE

				UnrealInsightsModule.CreateSessionViewer(false);
				UnrealInsightsModule.StartAnalysisForLastLiveSession();
			}
		});

	}

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FGameplayInsightsModule::RegisterMenus));

#else
	FOnRegisterMajorTabExtensions& TimingProfilerExtension = UnrealInsightsModule.OnRegisterMajorTabExtension(FInsightsManagerTabs::TimingProfilerTabId);
	TimingProfilerExtension.AddRaw(this, &FGameplayInsightsModule::RegisterTimingProfilerLayoutExtensions);
#endif

#if OBJECT_PROPERTY_TRACE_ENABLED
	FObjectPropertyTrace::Init();
#endif
}

void FGameplayInsightsModule::ShutdownModule()
{
#if OBJECT_PROPERTY_TRACE_ENABLED
	FObjectPropertyTrace::Destroy();
#endif

#if WITH_EDITOR
	IAnimationBlueprintEditorModule* AnimationBlueprintEditorModule = FModuleManager::GetModulePtr<IAnimationBlueprintEditorModule>("AnimationBlueprintEditor");
	if(AnimationBlueprintEditorModule)
	{
		AnimationBlueprintEditorModule->OnGetCustomDebugObjects().Remove(CustomDebugObjectHandle);
	}
#else
	IUnrealInsightsModule& UnrealInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
	FOnRegisterMajorTabExtensions& TimingProfilerLayoutExtension = UnrealInsightsModule.OnRegisterMajorTabExtension(FInsightsManagerTabs::TimingProfilerTabId);
	TimingProfilerLayoutExtension.RemoveAll(this);
#endif

	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);

	IModularFeatures::Get().UnregisterModularFeature(TraceServices::ModuleFeatureName, &GameplayTraceModule);
	IModularFeatures::Get().UnregisterModularFeature(Insights::TimingViewExtenderFeatureName, &GameplayTimingViewExtender);
}

TSharedRef<SDockTab> FGameplayInsightsModule::SpawnTimingProfilerDocumentTab(const FTabManager::FSearchPreference& InSearchPreference)
{
	TSharedRef<SDockTab> NewTab = SNew(SDockTab);
	TSharedPtr<FTabManager> TimingProfilerTabManager = WeakTimingProfilerTabManager.Pin();
	if(TimingProfilerTabManager.IsValid())
	{
		TimingProfilerTabManager->InsertNewDocumentTab(GameplayInsightsTabs::DocumentTab, InSearchPreference, NewTab);
	}
	return NewTab;
}

void FGameplayInsightsModule::RegisterTimingProfilerLayoutExtensions(FInsightsMajorTabExtender& InOutExtender)
{
	InOutExtender.GetLayoutExtender().ExtendLayout(FTimingProfilerTabs::TimersID, ELayoutExtensionPosition::Before, FTabManager::FTab(GameplayInsightsTabs::DocumentTab, ETabState::ClosedTab));
}

#if WITH_EDITOR
void FGameplayInsightsModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

#if OBJECT_PROPERTY_TRACE_ENABLED
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("Kismet.SubobjectEditorContextMenu");

		FToolMenuSection& Section = Menu->AddSection("GameplayInsights", LOCTEXT("GameplayInsights", "Gameplay Insights"));

		auto GetCheckState = [](const TSharedPtr<SSubobjectEditor>& InSubobjectEditor)
		{
			if (InSubobjectEditor->GetNumSelectedNodes() > 0 && FObjectPropertyTrace::IsEnabled())
			{
				TArray<FSubobjectEditorTreeNodePtrType> SelectedNodes = InSubobjectEditor->GetSelectedNodes();
				int32 TotalObjectCount = SelectedNodes.Num();
				int32 RegisteredObjectCount = 0;

				for (FSubobjectEditorTreeNodePtrType SubobjectNode : SelectedNodes)
				{
					const UObject* SelectedComponent = SubobjectNode->GetObject();
					if (FObjectPropertyTrace::IsObjectRegistered(SelectedComponent))
					{
						RegisteredObjectCount++;
					}
					else
					{
						break;
					}
				}

				if (RegisteredObjectCount == TotalObjectCount)
				{
					return ECheckBoxState::Checked;
				}
				else
				{
					return ECheckBoxState::Unchecked;
				}
			}

			return ECheckBoxState::Unchecked;
		};

		FToolUIAction Action;
		Action.ExecuteAction = FToolMenuExecuteAction::CreateLambda([&GetCheckState](const FToolMenuContext& InContext)
		{
			if (FObjectPropertyTrace::IsEnabled())
			{
				USubobjectEditorMenuContext* ContextObject = InContext.FindContext<USubobjectEditorMenuContext>();
				TSharedPtr<SSubobjectEditor> SubobjectEditor = ContextObject ? ContextObject->SubobjectEditor.Pin() : nullptr;

				if (SubobjectEditor.IsValid() && StaticCastSharedPtr<SSubobjectInstanceEditor>(SubobjectEditor))
				{					
					ECheckBoxState CheckState = GetCheckState(SubobjectEditor);

					for(FSubobjectEditorTreeNodePtrType Node : SubobjectEditor->GetSelectedNodes())
					{
						const UObject* SelectedComponent = Node->GetObject();
						if(CheckState == ECheckBoxState::Unchecked)
						{
							FObjectPropertyTrace::RegisterObject(SelectedComponent);
						}
						else
						{
							FObjectPropertyTrace::UnregisterObject(SelectedComponent);
						}
					}
				}
			}
		});
		Action.CanExecuteAction = FToolMenuCanExecuteAction::CreateLambda([](const FToolMenuContext& InContext)
		{
			if (FObjectPropertyTrace::IsEnabled())
			{
				USubobjectEditorMenuContext* ContextObject = InContext.FindContext<USubobjectEditorMenuContext>();
				if (ContextObject && ContextObject->SubobjectEditor.IsValid() && StaticCastSharedPtr<SSubobjectInstanceEditor>(ContextObject->SubobjectEditor.Pin()))
				{
					TSharedPtr<SSubobjectEditor> SubobjectEditor = ContextObject->SubobjectEditor.Pin();
					if (SubobjectEditor.IsValid())
					{
						return SubobjectEditor->GetNumSelectedNodes() > 0;
					}
				}
			}

			return false;
		});
		Action.GetActionCheckState = FToolMenuGetActionCheckState::CreateLambda([&GetCheckState](const FToolMenuContext& InContext)
		{
			USubobjectEditorMenuContext* ContextObject = InContext.FindContext<USubobjectEditorMenuContext>();
			if (ContextObject && StaticCastSharedPtr<SSubobjectInstanceEditor>(ContextObject->SubobjectEditor.Pin()))
			{
				TSharedPtr<SSubobjectEditor> SubobjectEditor = ContextObject->SubobjectEditor.Pin();

				if (SubobjectEditor.IsValid())
				{
					return GetCheckState(SubobjectEditor);
				}
			}

			return ECheckBoxState::Unchecked;
		});
		Action.IsActionVisibleDelegate = FToolMenuIsActionButtonVisible::CreateLambda([](const FToolMenuContext& InContext)
		{
			if (FObjectPropertyTrace::IsEnabled())
			{
				USubobjectEditorMenuContext* ContextObject = InContext.FindContext<USubobjectEditorMenuContext>();
				if (ContextObject && ContextObject->SubobjectEditor.IsValid() && StaticCastSharedPtr<SSubobjectInstanceEditor>(ContextObject->SubobjectEditor.Pin()))
				{
					return true;
				}
			}

			return false;
		});

		FToolMenuEntry& Entry = Section.AddMenuEntry(
			"TraceComponentProperties",
			LOCTEXT("TraceComponentProperties", "Trace Component Properties"),
			LOCTEXT("TraceComponentPropertiesTooltip", "Trace the properties of this component to be viewed in Insights"),
			FSlateIcon(),
			Action,
			EUserInterfaceActionType::ToggleButton);
	}
#endif
}

void FGameplayInsightsModule::EnableObjectPropertyTrace(UObject* Object, bool bEnable)
{
#if OBJECT_PROPERTY_TRACE_ENABLED
	if (bEnable)
	{
		FObjectPropertyTrace::RegisterObject(Object);
	}
	else
	{
		FObjectPropertyTrace::UnregisterObject(Object);
	}
#endif
}

bool FGameplayInsightsModule::IsObjectPropertyTraceEnabled(UObject* Object)
{
#if OBJECT_PROPERTY_TRACE_ENABLED
	return FObjectPropertyTrace::IsObjectRegistered(Object);
#else
	return false;
#endif
}

#endif

IMPLEMENT_MODULE(FGameplayInsightsModule, GameplayInsights);

#undef LOCTEXT_NAMESPACE
