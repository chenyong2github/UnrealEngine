// Copyright Epic Games, Inc. All Rights Reserved.
#include "WorldPartitionEditorModule.h"

#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionVolume.h"

#include "WorldPartition/HLOD/HLODLayerAssetTypeActions.h"

#include "WorldPartition/SWorldPartitionEditor.h"
#include "WorldPartition/SWorldPartitionEditorGridSpatialHash.h"

#include "WorldPartition/SWorldPartitionConvertDialog.h"
#include "WorldPartition/WorldPartitionConvertOptions.h"
#include "WorldPartition/WorldPartitionEditorSettings.h"

#include "LevelEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "Engine/Level.h"
#include "Misc/MessageDialog.h"
#include "Misc/StringBuilder.h"
#include "Misc/ScopedSlowTask.h"
#include "Interfaces/IMainFrameModule.h"
#include "Widgets/SWindow.h"
#include "Commandlets/WorldPartitionConvertCommandlet.h"
#include "FileHelpers.h"

IMPLEMENT_MODULE( FWorldPartitionEditorModule, WorldPartitionEditor );

#define LOCTEXT_NAMESPACE "WorldPartition"

// World Partition
static void OnLoadSelectedWorldPartitionVolumes(TArray<TWeakObjectPtr<AActor>> Volumes)
{
	for (TWeakObjectPtr<AActor> Actor: Volumes)
	{
		AWorldPartitionVolume* WorldPartitionVolume = CastChecked<AWorldPartitionVolume>(Actor.Get());
		WorldPartitionVolume->LoadIntersectingCells();
	}
}

static void CreateLevelViewportContextMenuEntries(FMenuBuilder& MenuBuilder, TArray<TWeakObjectPtr<AActor>> Volumes)
{
	MenuBuilder.BeginSection("WorldPartition", LOCTEXT("WorldPartition", "World Partition"));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("WorldPartitionLoad", "Load selected world partition volumes"),
		LOCTEXT("WorldPartitionLoad_Tooltip", "Load selected world partition volumes"),
		FSlateIcon(),
		FExecuteAction::CreateStatic(OnLoadSelectedWorldPartitionVolumes, Volumes),
		NAME_None,
		EUserInterfaceActionType::Button);

	MenuBuilder.EndSection();
}

static TSharedRef<FExtender> OnExtendLevelEditorMenu(const TSharedRef<FUICommandList> CommandList, TArray<AActor*> SelectedActors)
{
	TSharedRef<FExtender> Extender(new FExtender());

	TArray<TWeakObjectPtr<AActor> > Volumes;
	for (AActor* Actor : SelectedActors)
	{
		if (Actor->IsA(AWorldPartitionVolume::StaticClass()))
		{
			Volumes.Add(Actor);
		}
	}

	if (Volumes.Num())
	{
		Extender->AddMenuExtension(
			"SelectActorGeneral",
			EExtensionHook::After,
			nullptr,
			FMenuExtensionDelegate::CreateStatic(&CreateLevelViewportContextMenuEntries, Volumes));
	}

	return Extender;
}

void FWorldPartitionEditorModule::StartupModule()
{
	SWorldPartitionEditorGrid::RegisterPartitionEditorGridCreateInstanceFunc(NAME_None, &SWorldPartitionEditorGrid::CreateInstance);
	SWorldPartitionEditorGrid::RegisterPartitionEditorGridCreateInstanceFunc(TEXT("SpatialHash"), &SWorldPartitionEditorGridSpatialHash::CreateInstance);
	
	FLevelEditorModule& LevelEditorModule = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	TArray<FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors>& MenuExtenderDelegates = LevelEditorModule.GetAllLevelViewportContextMenuExtenders();

	MenuExtenderDelegates.Add(FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors::CreateStatic(&OnExtendLevelEditorMenu));
	LevelEditorExtenderDelegateHandle = MenuExtenderDelegates.Last().GetHandle();
	
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	HLODLayerAssetTypeActions = MakeShareable(new FHLODLayerAssetTypeActions);
	AssetTools.RegisterAssetTypeActions(HLODLayerAssetTypeActions.ToSharedRef());
}

void FWorldPartitionEditorModule::ShutdownModule()
{
	UWorldPartition::WorldPartitionChangedEvent.RemoveAll(this);
	
	if (FLevelEditorModule* LevelEditorModule = FModuleManager::Get().GetModulePtr<FLevelEditorModule>("LevelEditor"))
	{
		LevelEditorModule->GetAllLevelViewportContextMenuExtenders().RemoveAll([=](const FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors& In) { return In.GetHandle() == LevelEditorExtenderDelegateHandle; });
	}

	IAssetTools* AssetToolsModule = nullptr;
	if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		AssetToolsModule = &FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
	}

	// Unregister the HLODLayer asset type actions
	if (HLODLayerAssetTypeActions.IsValid())
	{
		if (AssetToolsModule)
		{
			AssetToolsModule->UnregisterAssetTypeActions(HLODLayerAssetTypeActions.ToSharedRef());
		}
		HLODLayerAssetTypeActions.Reset();
	}
}

TSharedRef<SWidget> FWorldPartitionEditorModule::CreateWorldPartitionEditor()
{
	UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
	return SNew(SWorldPartitionEditor).InWorld(EditorWorld);
}

bool FWorldPartitionEditorModule::IsWorldPartitionEnabled() const
{
	return GetDefault<UWorldPartitionEditorSettings>()->bEnableWorldPartition;
}

bool FWorldPartitionEditorModule::IsConversionPromptEnabled() const
{
	return IsWorldPartitionEnabled() && GetDefault<UWorldPartitionEditorSettings>()->bEnableConversionPrompt;
}

void FWorldPartitionEditorModule::SetConversionPromptEnabled(bool bEnabled)
{
	GetMutableDefault<UWorldPartitionEditorSettings>()->bEnableConversionPrompt = bEnabled;
}

bool FWorldPartitionEditorModule::GetEnableLoadingOfLastLoadedCells() const
{
	return GetDefault<UWorldPartitionEditorSettings>()->bEnableLoadingOfLastLoadedCells;
}
	
float FWorldPartitionEditorModule::GetAutoCellLoadingMaxWorldSize() const
{
	return GetDefault<UWorldPartitionEditorSettings>()->AutoCellLoadingMaxWorldSize;
}

bool FWorldPartitionEditorModule::ConvertMap(const FString& InLongPackageName)
{
	if (!IsConversionPromptEnabled() || ULevel::GetIsLevelPartitionedFromPackage(FName(*InLongPackageName)))
	{
		return true;
	}

	FText Title = LOCTEXT("ConvertMapMsgTitle", "Opening non World Partition map");
	EAppReturnType::Type Ret = FMessageDialog::Open(EAppMsgType::YesNoCancel, LOCTEXT("ConvertMapMsg", "Do you want to convert this map? (Process will restart editor)"), &Title);
	if (Ret == EAppReturnType::No)
	{
		return true;
	}
	else if (Ret == EAppReturnType::Cancel)
	{
		return false;
	}

	UWorldPartitionConvertOptions* DefaultConvertOptions = GetMutableDefault<UWorldPartitionConvertOptions>();
	DefaultConvertOptions->CommandletClass = GetDefault<UWorldPartitionEditorSettings>()->CommandletClass;
	DefaultConvertOptions->bInPlace = false;
	DefaultConvertOptions->bSkipStableGUIDValidation = true;
	DefaultConvertOptions->LongPackageName = InLongPackageName;

	TSharedPtr<SWindow> DlgWindow =
		SNew(SWindow)
		.Title(LOCTEXT("ConvertWindowTitle", "Convert Settings"))
		.ClientSize(SWorldPartitionConvertDialog::DEFAULT_WINDOW_SIZE)
		.SizingRule(ESizingRule::UserSized)
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		.SizingRule(ESizingRule::FixedSize);

	TSharedRef<SWorldPartitionConvertDialog> ConvertDialog =
		SNew(SWorldPartitionConvertDialog)
		.ParentWindow(DlgWindow)
		.ConvertOptions(DefaultConvertOptions);

	DlgWindow->SetContent(ConvertDialog);

	IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
	FSlateApplication::Get().AddModalWindow(DlgWindow.ToSharedRef(), MainFrameModule.GetParentWindow());

	if (ConvertDialog->ClickedOk())
	{
		// User will already have been prompted to save on file open
		if (!UEditorLoadingAndSavingUtils::NewBlankMap(/*bSaveExistingMap*/false))
		{
			return false;
		}

		FProcHandle ProcessHandle;
		bool bCancelled = false;

		// Task scope
		{
			FScopedSlowTask SlowTask(0, LOCTEXT("ConvertProgress", "Converting map to world partition..."));
			SlowTask.MakeDialog(true);

			FString CurrentExecutableName = FPlatformProcess::ExecutablePath();

			// Try to provide complete Path, if we can't try with project name
			FString ProjectPath = FPaths::IsProjectFilePathSet() ? FPaths::GetProjectFilePath() : FApp::GetProjectName();

			uint32 ProcessID;
			FString Arguments = FString::Printf(TEXT("\"%s\" %s"), *ProjectPath, *DefaultConvertOptions->ToCommandletArgs());
			ProcessHandle = FPlatformProcess::CreateProc(*CurrentExecutableName, *Arguments, true, false, false, &ProcessID, 0, nullptr, nullptr);
			
			while (FPlatformProcess::IsProcRunning(ProcessHandle))
			{
				if (SlowTask.ShouldCancel())
				{
					bCancelled = true;
					FPlatformProcess::TerminateProc(ProcessHandle);
					break;
				}

				SlowTask.EnterProgressFrame(0);
				FPlatformProcess::Sleep(0.1);
			}
		}

		int32 Result = 0;
		if (!bCancelled && FPlatformProcess::GetProcReturnCode(ProcessHandle, &Result))
		{	
			if (Result == 0)
			{
				FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("ConvertMapCompleted", "Conversion succeeded. Editor will now restart."));
				FUnrealEdMisc::Get().RestartEditor(false);
			}
		}
		else if (bCancelled)
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("ConvertMapCancelled", "Conversion cancelled!"));
		}
		
		if(Result != 0)
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("ConvertMapFailed", "Conversion failed!"));
		}
	}

	return false;
}

UWorldPartitionEditorSettings::UWorldPartitionEditorSettings()
{
	bEnableConversionPrompt = false;
	CommandletClass = UWorldPartitionConvertCommandlet::StaticClass();
}

FString UWorldPartitionConvertOptions::ToCommandletArgs() const
{
	TStringBuilder<1024> CommandletArgsBuilder;
	CommandletArgsBuilder.Appendf(TEXT("-run=%s %s -AllowCommandletRendering"), *CommandletClass->GetName(), *LongPackageName);
	
	if (!bInPlace)
	{
		CommandletArgsBuilder.Append(TEXT(" -ConversionSuffix"));
	}

	if (bSkipStableGUIDValidation)
	{
		CommandletArgsBuilder.Append(TEXT(" -SkipStableGUIDValidation"));
	}

	if (bSkipMiniMapGeneration)
	{
		CommandletArgsBuilder.Append(TEXT(" -SkipMiniMapGeneration"));
	}

	if (bDeleteSourceLevels)
	{
		CommandletArgsBuilder.Append(TEXT(" -DeleteSourceLevels"));
	}
	
	if (bGenerateIni)
	{
		CommandletArgsBuilder.Append(TEXT(" -GenerateIni"));
	}
	
	if (bReportOnly)
	{
		CommandletArgsBuilder.Append(TEXT(" -ReportOnly"));
	}
	
	if (bVerbose)
	{
		CommandletArgsBuilder.Append(TEXT(" -Verbose"));
	}
	
	return CommandletArgsBuilder.ToString();
}

#undef LOCTEXT_NAMESPACE
