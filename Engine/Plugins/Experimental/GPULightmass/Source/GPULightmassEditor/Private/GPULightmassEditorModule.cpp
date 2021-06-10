// Copyright Epic Games, Inc. All Rights Reserved.

#include "GPULightmassEditorModule.h"
#include "CoreMinimal.h"
#include "Internationalization/Internationalization.h"
#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"
#include "RenderingThread.h"
#include "Interfaces/IPluginManager.h"
#include "ShaderCore.h"
#include "SceneInterface.h"
#include "LevelEditor.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "GPULightmassSettings.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "EditorFontGlyphs.h"
#include "GPULightmassModule.h"
#include "LevelEditorViewport.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"

extern ENGINE_API void ToggleLightmapPreview_GameThread(UWorld* InWorld);

#define LOCTEXT_NAMESPACE "StaticLightingSystem"

IMPLEMENT_MODULE( FGPULightmassEditorModule, GPULightmassEditor )

FName GPULightmassSettingsTabName = TEXT("GPULightmassSettings");

void FGPULightmassEditorModule::StartupModule()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	LevelEditorModule.OnTabManagerChanged().AddRaw(this, &FGPULightmassEditorModule::RegisterTabSpawner);
	LevelEditorModule.OnMapChanged().AddRaw(this, &FGPULightmassEditorModule::OnMapChanged);
	auto BuildMenuExtender = FLevelEditorModule::FLevelEditorMenuExtender::CreateRaw(this, &FGPULightmassEditorModule::OnExtendLevelEditorBuildMenu);
	LevelEditorModule.GetAllLevelEditorToolbarBuildMenuExtenders().Add(BuildMenuExtender);
}

void FGPULightmassEditorModule::ShutdownModule()
{
}

void FGPULightmassEditorModule::RegisterTabSpawner()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();

	LevelEditorTabManager->RegisterTabSpawner(GPULightmassSettingsTabName, FOnSpawnTab::CreateRaw(this, &FGPULightmassEditorModule::SpawnSettingsTab))
		.SetDisplayName(LOCTEXT("GPULightmassSettingsTitle", "GPU Lightmass"))
		//.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "Level.LightingScenarioIcon16x"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);
}

TSharedRef<SDockTab> FGPULightmassEditorModule::SpawnSettingsTab(const FSpawnTabArgs& Args)
{
	FPropertyEditorModule& PropPlugin = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs(false, false, true, FDetailsViewArgs::HideNameArea, false, GUnrealEd);
	DetailsViewArgs.bShowActorLabel = false;

	SettingsView = PropPlugin.CreateDetailView(DetailsViewArgs);

	if (UWorld* World = GEditor->GetEditorWorldContext().World())
	{
		if (World->GetSubsystem<UGPULightmassSubsystem>())
		{
			SettingsView->SetObject(World->GetSubsystem<UGPULightmassSubsystem>()->GetSettings());
		}
	}

	return SNew(SDockTab)
		.Icon(FEditorStyle::GetBrush("Level.LightingScenarioIcon16x"))
		.Label(NSLOCTEXT("GPULightmass", "GPULightmassSettingsTabTitle", "GPU Lightmass"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2)
			[

				// Start Build
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 8.f, 0.0f)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
					.IsEnabled(IsRayTracingEnabled())
					.Visibility_Lambda([](){ return IsRunning() ? EVisibility::Collapsed : EVisibility::Visible; })
					.OnClicked_Raw(this, &FGPULightmassEditorModule::OnStartClicked)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.AutoWidth()
						[
							SNew(STextBlock)
							.TextStyle(FEditorStyle::Get(), "ContentBrowser.TopBar.Font")
							.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.11"))
							.Text(FEditorFontGlyphs::Lightbulb_O)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(4, 0, 0, 0)
						[
							SNew(STextBlock)
							.TextStyle(FEditorStyle::Get(), "ContentBrowser.TopBar.Font")
							.Text_Lambda( []()
							{ 
								return FGPULightmassEditorModule::IsBakeWhatYouSeeMode() ? 
										LOCTEXT("GPULightmassSettingsStartInteractive", "Start Building Lighting") : 
										LOCTEXT("GPULightmassSettingsStartFull", "Build Lighting");
							})

						]
					]
				]

				// Save and Stop Building
				+SHorizontalBox::Slot()
				.Padding(0.0f, 0.0f, 8.f, 0.0f)
				.AutoWidth()
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
					.Visibility_Lambda([](){ return IsRunning() && IsBakeWhatYouSeeMode() ? EVisibility::Visible : EVisibility::Collapsed; })
					.OnClicked_Raw(this, &FGPULightmassEditorModule::OnSaveAndStopClicked)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.AutoWidth()
						[
							SNew(STextBlock)
							.TextStyle(FEditorStyle::Get(), "ContentBrowser.TopBar.Font")
							.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.11"))
							.Text(FEditorFontGlyphs::Lightbulb_O)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(4, 0, 0, 0)
						[
							SNew(STextBlock)
							.TextStyle(FEditorStyle::Get(), "ContentBrowser.TopBar.Font")
							.Text(LOCTEXT("GPULightmassSettingsSaveAndStop", "Save And Stop Building"))
						]
					]
				]

				// Cancel Build
				+SHorizontalBox::Slot()
				.Padding(0.0f, 0.0f, 8.f, 0.0f)
				.AutoWidth()
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.ButtonStyle(FEditorStyle::Get(), "FlatButton.Danger")
					.Visibility_Lambda([](){ return IsRunning() ? EVisibility::Visible: EVisibility::Collapsed; })
					.OnClicked_Raw(this, &FGPULightmassEditorModule::OnCancelClicked)
					.Text(LOCTEXT("GPULightmassSettingsCancel", "Cancel Build"))
					.TextStyle(FEditorStyle::Get(), "ContentBrowser.TopBar.Font")
				]
				

				+SHorizontalBox::Slot()
				.FillWidth(1.0)
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda( [] () { return FGPULightmassEditorModule::IsRealtimeOn() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda( [] (ECheckBoxState NewState) 
					{
						if (GCurrentLevelEditingViewportClient)
						{
							GCurrentLevelEditingViewportClient->SetRealtime( NewState == ECheckBoxState::Checked );
						}	
					})
				]
         
				+SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(140)
					[	
						SNew(STextBlock)
						.Text_Lambda( [](){ return FGPULightmassEditorModule::IsRealtimeOn() ? LOCTEXT("GPULightmassRealtimeEnabled", "Viewport Realtime is ON ") : LOCTEXT("GPULightmassRealtimeDisabled", "Viewport Realtime is OFF");})
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.f, 4.f)
			[
				SAssignNew(Messages, STextBlock)
				.AutoWrapText(true)
				.Text_Lambda( []() -> FText
				{

					bool bIsRayTracingEnabled = IsRayTracingEnabled();
					FText RTDisabledMsg = LOCTEXT("GPULightmassRayTracingDisabled", "GPU Lightmass requires ray tracing support which is disabled.");
					if (!bIsRayTracingEnabled)
					{
						return LOCTEXT("GPULightmassRayTracingDisabled", "GPU Lightmass requires ray tracing support which is disabled.");
					}

					// Ready
					static FText ReadyMsg = FText(LOCTEXT("GPULightmassReady", "GPU Lightmass is ready."));

					// Ready, BWYS
					static FText BWYSReadyMsg = FText(LOCTEXT("GPULightmassReadyBWYS", "GPU Lightmass is ready. Lighting will rebuild continuously in Bake What You See mode until saved or canceled."));

					// Ready, BWYS+RT OFF Warning 
					static FText RtOffBWYSWarningMsg = LOCTEXT("GPULightmassSpeedReadyRTWarning", "Building Lighting when using Bake What You See Mode will automatically enable Viewport Realtime to start building. Lighting will rebuild continuously in Bake What You See mode until saved or canceled.");

					// Building FULL + RT Off Warning
					UWorld* World = GEditor->GetEditorWorldContext().World();
					FText BuildingMsg = FText::Format(LOCTEXT("GPULightmassBuildingLighting", "GPU Lightmass is building lighting for {0}."), FText::FromString(World->GetActiveLightingScenario() ? World->GetActiveLightingScenario()->GetOuter()->GetName() : World->GetName()));

					// Building FULL + RT ON Warning 
					static FText BuildingFullRTOnMsg = LOCTEXT("GPULightmassBuildingFullRTOn", "GPU Lightmass runs in slow mode when the viewport is realtime to avoid freezing. Uncheck Viewport Realtime to get full speed.");

					// Building BWYS + RT ON Warning 
					static FText BuildingRTOnMsg = LOCTEXT("GPULightmassBuildingInteractiveRTOn", "Disable Viewport Realtime to speed up building.");

					// Building BWYS + RT OFF Warning 
					static FText BuidlingRTOffMsg = LOCTEXT("GPULightmassBuildingInteractiveRTOff", "Re-enable Viewport Realtime to preview lighting.  Enabling Viewport Realtime will slow down building, to avoid freezing.");

					bool bIsRunning = IsRunning();
					bool bIsInteractive = IsBakeWhatYouSeeMode();
					bool bIsRealtime = IsRealtimeOn();
					if (bIsRunning)
					{
						if (bIsInteractive)
						{
							return bIsRealtime ? BuildingRTOnMsg : BuidlingRTOffMsg;
						}

						return bIsRealtime ? BuildingFullRTOnMsg : BuildingMsg;
					}
					else if (bIsInteractive)
					{
						return bIsRealtime ?  BWYSReadyMsg : RtOffBWYSWarningMsg;
					}

					return bIsRealtime ? BuildingFullRTOnMsg : ReadyMsg;
				})	
			]

			+ SVerticalBox::Slot()
			[
				SettingsView.ToSharedRef()
			]
		];
}

void FGPULightmassEditorModule::UpdateSettingsTab()
{
	if (SettingsView.IsValid())
	{
		SettingsView->ForceRefresh();
	}
}

bool FGPULightmassEditorModule::IsBakeWhatYouSeeMode()
{
	if (UWorld* World = GEditor->GetEditorWorldContext().World())
	{
		if (UGPULightmassSubsystem* LMSubsystem = World->GetSubsystem<UGPULightmassSubsystem>())
		{
			return LMSubsystem->GetSettings()->Mode == EGPULightmassMode::BakeWhatYouSee;
		}
	}

	return false;
}

bool FGPULightmassEditorModule::IsRealtimeOn() 
{
	return GCurrentLevelEditingViewportClient && GCurrentLevelEditingViewportClient->IsRealtime();
}

bool FGPULightmassEditorModule::IsRunning() 
{
	if (UWorld* World = GEditor->GetEditorWorldContext().World())
	{
		return World->GetSubsystem<UGPULightmassSubsystem>()->IsRunning();
	}

	return false;
}

FReply FGPULightmassEditorModule::OnStartClicked()
{
	if (UWorld* World = GEditor->GetEditorWorldContext().World())
	{
		if (!World->GetSubsystem<UGPULightmassSubsystem>()->IsRunning())
		{
			if (IsBakeWhatYouSeeMode() && !IsRealtimeOn() && GCurrentLevelEditingViewportClient != nullptr)
			{
				GCurrentLevelEditingViewportClient->SetRealtime(true);
			}

			World->GetSubsystem<UGPULightmassSubsystem>()->Launch();
			World->GetSubsystem<UGPULightmassSubsystem>()->OnLightBuildEnded().AddRaw(this, &FGPULightmassEditorModule::UpdateSettingsTab);
		}
	}

	UpdateSettingsTab();

	return FReply::Handled();
}


FReply FGPULightmassEditorModule::OnSaveAndStopClicked()
{
	if (UWorld* World = GEditor->GetEditorWorldContext().World())
	{
		if (World->GetSubsystem<UGPULightmassSubsystem>()->IsRunning())
		{
			World->GetSubsystem<UGPULightmassSubsystem>()->Save();
			World->GetSubsystem<UGPULightmassSubsystem>()->Stop();
			World->GetSubsystem<UGPULightmassSubsystem>()->OnLightBuildEnded().RemoveAll(this);
		}
	}

	UpdateSettingsTab();

	return FReply::Handled();
}

FReply FGPULightmassEditorModule::OnCancelClicked()
{

	if (UWorld* World = GEditor->GetEditorWorldContext().World())
	{
		if (World->GetSubsystem<UGPULightmassSubsystem>()->IsRunning())
		{
			World->GetSubsystem<UGPULightmassSubsystem>()->Stop();
			World->GetSubsystem<UGPULightmassSubsystem>()->OnLightBuildEnded().RemoveAll(this);
		}
	}
	
	UpdateSettingsTab();

	return FReply::Handled();
}

void FGPULightmassEditorModule::OnMapChanged(UWorld* InWorld, EMapChangeType MapChangeType)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (SettingsView.IsValid())
	{
		if (World->GetSubsystem<UGPULightmassSubsystem>())
		{
			SettingsView->SetObject(World->GetSubsystem<UGPULightmassSubsystem>()->GetSettings(), true);

			if (MapChangeType == EMapChangeType::LoadMap || MapChangeType == EMapChangeType::NewMap)
			{
				World->GetSubsystem<UGPULightmassSubsystem>()->OnLightBuildEnded().AddRaw(this, &FGPULightmassEditorModule::UpdateSettingsTab);
			}
			else if (MapChangeType == EMapChangeType::TearDownWorld)
			{
				World->GetSubsystem<UGPULightmassSubsystem>()->OnLightBuildEnded().RemoveAll(this);
			}
		}
	}
}

TSharedRef<FExtender> FGPULightmassEditorModule::OnExtendLevelEditorBuildMenu(const TSharedRef<FUICommandList> CommandList)
{
	TSharedRef<FExtender> Extender(new FExtender());

	Extender->AddMenuExtension("LevelEditorLighting", EExtensionHook::First, nullptr,
		FMenuExtensionDelegate::CreateRaw(this, &FGPULightmassEditorModule::CreateBuildMenu));

	return Extender;
}

void FGPULightmassEditorModule::CreateBuildMenu(FMenuBuilder& Builder)
{
	FUIAction ActionOpenGPULightmassSettingsTab(FExecuteAction::CreateLambda([]() {
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();
		LevelEditorTabManager->TryInvokeTab(GPULightmassSettingsTabName);
	}), FCanExecuteAction());

	Builder.AddMenuEntry(LOCTEXT("GPULightmassSettingsTitle", "GPU Lightmass"),
		LOCTEXT("OpensGPULightmassSettings", "Opens GPU Lightmass settings tab."), FSlateIcon(FEditorStyle::GetStyleSetName(), "Level.LightingScenarioIcon16x"), ActionOpenGPULightmassSettingsTab,
		NAME_None, EUserInterfaceActionType::Button);
}

#undef LOCTEXT_NAMESPACE
