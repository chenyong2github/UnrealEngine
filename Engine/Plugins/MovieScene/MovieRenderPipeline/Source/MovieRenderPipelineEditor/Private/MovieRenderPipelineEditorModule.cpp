// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieRenderPipelineEditorModule.h"
#include "MovieRenderPipelineCoreModule.h"
#include "MoviePipelineExecutor.h"
#include "WorkspaceMenuStructureModule.h"
#include "WorkspaceMenuStructure.h"
#include "Widgets/SMoviePipelineConfigTabContent.h"
#include "Widgets/SMoviePipelineQueueTabContent.h"
#include "ISettingsModule.h"
#include "MovieRenderPipelineSettings.h"
#include "HAL/IConsoleManager.h"
#include "MoviePipelineExecutor.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Docking/TabManager.h"
#include "EditorStyleSet.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "Templates/SubclassOf.h"
#include "MovieRenderPipelineStyle.h"

#define LOCTEXT_NAMESPACE "FMovieRenderPipelineEditorModule"

FName IMovieRenderPipelineEditorModule::MoviePipelineQueueTabName = "MoviePipelineQueue";
FText IMovieRenderPipelineEditorModule::MoviePipelineQueueTabLabel = LOCTEXT("MovieRenderQueueTab_Label", "Movie Render Queue");
FName IMovieRenderPipelineEditorModule::MoviePipelineConfigEditorTabName = "MovieRenderPipeline";
FText IMovieRenderPipelineEditorModule::MoviePipelineConfigEditorTabLabel = LOCTEXT("MovieRenderPipelineTab_Label", "Movie Render Pipeline");
										 
namespace
{
	static TSharedRef<SDockTab> SpawnMovieRenderPipelineTab(const FSpawnTabArgs& SpawnTabArgs)
	{
		TSharedRef<SDockTab> NewTab = SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SMoviePipelineConfigTabContent)
			];
		
		return NewTab;
	}

	static TSharedRef<SDockTab> SpawnMoviePipelineQueueTab(const FSpawnTabArgs& InSpawnTabArgs)
	{
		TSharedRef<SDockTab> NewTab = SNew(SDockTab)
			.TabRole(ETabRole::MajorTab)
			[
				SNew(SMoviePipelineQueueTabContent)
			];

		return NewTab;
	}

	static void RegisterTabImpl()
	{
		// FTabSpawnerEntry& MRPConfigTabSpawner = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(IMovieRenderPipelineEditorModule::MoviePipelineConfigEditorTabName, FOnSpawnTab::CreateStatic(SpawnMovieRenderPipelineTab));
		// 
		// MRPConfigTabSpawner
		// 	.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCinematicsCategory())
		// 	.SetDisplayName(IMovieRenderPipelineEditorModule::MoviePipelineConfigEditorTabLabel)
		// 	.SetTooltipText(LOCTEXT("MovieRenderPipelineConfigTab_Tooltip", "Open the Movie Render Config UI for creating and editing presets."))
		// 	.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.TabIcon"));

		FTabSpawnerEntry& MRPQueueTabSpawner = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(IMovieRenderPipelineEditorModule::MoviePipelineQueueTabName, FOnSpawnTab::CreateStatic(SpawnMoviePipelineQueueTab));

		MRPQueueTabSpawner
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCinematicsCategory())
			.SetDisplayName(IMovieRenderPipelineEditorModule::MoviePipelineQueueTabLabel)
			.SetTooltipText(LOCTEXT("MovieRenderPipelineQueueTab_Tooltip", "Open the Movie Render Queue to render Sequences to disk at a higher quality than realtime allows."))
			.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.TabIcon"));
	}
}

void FMovieRenderPipelineEditorModule::PerformTestPipelineRender(const TArray<FString>& Args)
{
	if (Args.Num() == 0)
	{
		UE_LOG(LogMovieRenderPipeline, Warning, TEXT("No arguments specified. Specify the path to a sequence asset to render."));
		return;
	}

	FSoftObjectPath SequencePath = FSoftObjectPath(FPackageName::ExportTextPathToObjectPath(Args[0]));

	const UMovieRenderPipelineProjectSettings* ProjectSettings = GetDefault<UMovieRenderPipelineProjectSettings>();
	TSubclassOf<UMoviePipelineExecutorBase> ExecutorClass = ProjectSettings->DefaultLocalExecutor;

	check(false);
	// Create an instance of the Executor and add it to the root so it never dies.
	// Executor = NewObject<UMoviePipelineExecutorBase>(GetTransientPackage(), ExecutorClass);
	// Executor->AddToRoot();
	// Executor->OnExecutorFinished().AddRaw(this, &FMovieRenderPipelineEditorModule::OnTestPipelineExecutorFinished);
	// 
	// TArray<UMoviePipelineMasterConfig*> Pipelines = GenerateTestPipelineConfigs();
	// 
	// TArray<FMoviePipelineExecutorJobPrev> Jobs;
	// for(UMoviePipelineMasterConfig* Pipeline : Pipelines)
	// {
	// 	Jobs.Add(FMoviePipelineExecutorJobPrev(SequencePath, Pipeline));
	// }
	// 
	// Executor->Execute(Jobs);
}

void FMovieRenderPipelineEditorModule::OnTestPipelineExecutorFinished(UMoviePipelineExecutorBase* InExecutor, bool bSuccess)
{
	if (Executor)
	{
		Executor->RemoveFromRoot();
		Executor = nullptr;
	}
}

void FMovieRenderPipelineEditorModule::RegisterSettings()
{
	ISettingsModule& SettingsModule = FModuleManager::LoadModuleChecked<ISettingsModule>("Settings");

	SettingsModule.RegisterSettings("Project", "Plugins", "Movie Render Pipeline",
		LOCTEXT("ProjectSettings_Label", "Movie Render Pipeline"),
		LOCTEXT("ProjectSettings_Description", "Configure project-wide defaults for the movie render pipeline."),
		GetMutableDefault<UMovieRenderPipelineProjectSettings>()
	);

	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("MovieRenderPipeline.TestRenderSequence"),
		TEXT("Renders the specified sequence asset using the default executor and a pre-made configuration."),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FMovieRenderPipelineEditorModule::PerformTestPipelineRender),
		ECVF_Default
	);
}

void FMovieRenderPipelineEditorModule::UnregisterSettings()
{
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule)
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "Movie Render Pipeline");
	}

	IConsoleManager::Get().UnregisterConsoleObject(TEXT("MovieRenderPipeline.TestRenderSequence"));
}

void FMovieRenderPipelineEditorModule::StartupModule()
{
	// Initialize our custom style
	FMovieRenderPipelineStyle::Get();

	RegisterTabImpl();
	RegisterSettings();
}

void FMovieRenderPipelineEditorModule::ShutdownModule()
{
	UnregisterSettings();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMovieRenderPipelineEditorModule, MovieRenderPipelineEditor)