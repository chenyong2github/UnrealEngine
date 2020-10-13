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

	FGPULightmassModule& GPULightmassModule = FModuleManager::LoadModuleChecked<FGPULightmassModule>(TEXT("GPULightmass"));
	GPULightmassModule.OnStaticLightingSystemsChanged.AddLambda([&SettingsView = SettingsView, &StartStopButtonText = StartStopButtonText]()
	{ 
		if (SettingsView.IsValid())
		{
			SettingsView->ForceRefresh();
		}

		if (StartStopButtonText.IsValid())
		{
			UWorld* World = GEditor->GetEditorWorldContext().World();
			if (World)
			{
				StartStopButtonText->SetText(World->GetSubsystem<UGPULightmassSubsystem>()->IsRunning() ? LOCTEXT("GPULightmassSettingsStop", "Stop") : LOCTEXT("GPULightmassSettingsStart", "Build Lighting"));
			}
		}
	});
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

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World)
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
				SNew(SButton)
				.HAlign(HAlign_Center)
				.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
				.IsEnabled(IsRayTracingEnabled())
				.OnClicked(FOnClicked::CreateRaw(this, &FGPULightmassEditorModule::OnStartStopClicked))
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
						SAssignNew(StartStopButtonText, STextBlock)
						.TextStyle(FEditorStyle::Get(), "ContentBrowser.TopBar.Font")
						.Text(LOCTEXT("GPULightmassSettingsStart", "Build Lighting"))
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2)
			[
				SAssignNew(Messages, STextBlock)
				.AutoWrapText(true)
				.Text(IsRayTracingEnabled() ? LOCTEXT("GPULightmassReady", "GPU Lightmass is ready.") : LOCTEXT("GPULightmassRayTracingDisabled", "GPU Lightmass requires ray tracing support which is disabled."))
			]
			+ SVerticalBox::Slot()
			[
				SettingsView.ToSharedRef()
			]
		];
}

FReply FGPULightmassEditorModule::OnStartStopClicked()
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	
	if (!World->GetSubsystem<UGPULightmassSubsystem>()->IsRunning())
	{
		World->GetSubsystem<UGPULightmassSubsystem>()->Launch();
	}
	else
	{
		World->GetSubsystem<UGPULightmassSubsystem>()->Stop();
	}

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

void FGPULightmassEditorModule::Tick(float DeltaTime)
{
	if (Messages.IsValid())
	{
		if (!IsRayTracingEnabled())
		{
			Messages->SetText(LOCTEXT("GPULightmassRayTracingDsiabled", "GPU Lightmass requires ray tracing support which is disabled."));
			return;
		}

		bool bIsViewportNonRealtime = GCurrentLevelEditingViewportClient && !GCurrentLevelEditingViewportClient->IsRealtime();

		if (bIsViewportNonRealtime)
		{
			UWorld* World = GEditor->GetEditorWorldContext().World();
			if (World->GetSubsystem<UGPULightmassSubsystem>()->IsRunning())
			{
				FText Text = FText::Format(LOCTEXT("GPULightmassBuildingLighting", "GPU Lightmass is building lighting for {0}."), FText::FromString(World->GetActiveLightingScenario() ? World->GetActiveLightingScenario()->GetOuter()->GetName() : World->GetName()));
				Messages->SetText(Text);
			}
			else
			{
				Messages->SetText(FText(LOCTEXT("GPULightmassReady", "GPU Lightmass is ready.")));
			}
		}
		else
		{
			Messages->SetText(LOCTEXT("GPULightmassSpeedModes", "GPU Lightmass runs in slow mode when the viewport is realtime to avoid freezing. Uncheck realtime on the viewport (or press Ctrl+R) to get full speed."));
		}
	}
}

bool FGPULightmassEditorModule::IsTickable() const
{
	return true;
}

TStatId FGPULightmassEditorModule::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FGPULightmassEditorModule, STATGROUP_Tickables);
}

#undef LOCTEXT_NAMESPACE
