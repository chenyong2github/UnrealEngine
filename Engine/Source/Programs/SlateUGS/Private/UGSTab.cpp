// Copyright Epic Games, Inc. All Rights Reserved.

#include "UGSTab.h"

#include "UGSLog.h"
#include "ChangeInfo.h"

#include "UGSCore/Utility.h"
#include "UGSCore/BuildStep.h"
#include "UGSCore/DetectProjectSettingsTask.h"
#include "UGSCore/UserSettings.h"
#include "UGSCore/PerforceMonitor.h"
#include "UGSCore/EventMonitor.h"

#include "Widgets/SModalTaskWindow.h"
#include "Widgets/SLogWidget.h"

#include "Internationalization/Regex.h"

#define LOCTEXT_NAMESPACE "UGSTab"

UGSTab::UGSTab() : TabArgs(nullptr, FTabId()),
				   TabWidget(SNew(SDockTab)),
				   EmptyTabView(SNew(SEmptyTab).Tab(this)),
				   GameSyncTabView(SNew(SGameSyncTab).Tab(this)),
				   bHasQueuedMessages(false)
{
	TabWidget->SetContent(EmptyTabView);
	TabWidget->SetLabel(FText(LOCTEXT("TabName", "Select a Project")));
}

const TSharedRef<SDockTab> UGSTab::GetTabWidget()
{
	return TabWidget;
}

void UGSTab::SetTabArgs(FSpawnTabArgs InTabArgs)
{
	TabArgs = InTabArgs;
}

FSpawnTabArgs UGSTab::GetTabArgs() const
{
	return TabArgs;
}

namespace
{
	// Todo: may need this, was in GameSyncController.h
	/*
	#include "Perforce.h"
	#include "CustomConfigFile.h"
	#include "Misc/EnumClassFlags.h"

	enum class EWorkspaceUpdateOptions
	{
		Sync = 0x01,
		SyncSingleChange = 0x02,
		AutoResolveChanges = 0x04,
		GenerateProjectFiles = 0x08,
		SyncArchives = 0x10,
		Build = 0x20,
		UseIncrementalBuilds = 0x40,
		ScheduledBuild = 0x80,
		RunAfterSync = 0x100,
		OpenSolutionAfterSync = 0x200,
		ContentOnly = 0x400
	};

	ENUM_CLASS_FLAGS(EWorkspaceUpdateOptions)

	enum class EWorkspaceUpdateResult
	{
		Canceled,
		FailedToSync,
		FilesToResolve,
		FilesToClobber,
		FailedToCompile,
		FailedToCompileWithCleanWorkspace,
		Success,
	};

	FString ToString(EWorkspaceUpdateResult WorkspaceUpdateResult);
	bool TryParse(const TCHAR* Text, EWorkspaceUpdateResult& OutWorkspaceUpdateResult);

	struct FWorkspaceUpdateContext
	{
		FDateTime StartTime;
		int ChangeNumber;
		EWorkspaceUpdateOptions Options;
		TArray<FString> SyncFilter;
		TMap<FString, FString> ArchiveTypeToDepotPath;
		TMap<FString, bool> ClobberFiles;
		TMap<FGuid,FCustomConfigObject> DefaultBuildSteps;
		TArray<FCustomConfigObject> UserBuildStepObjects;
		TSet<FGuid> CustomBuildSteps;
		TMap<FString, FString> Variables;
		FPerforceSyncOptions PerforceSyncOptions;

		FWorkspaceUpdateContext(int InChangeNumber, EWorkspaceUpdateOptions InOptions, const TArray<FString>& InSyncFilter, const TMap<FGuid, FCustomConfigObject>& InDefaultBuildSteps, const TArray<FCustomConfigObject>& InUserBuildSteps, const TSet<FGuid>& InCustomBuildSteps, const TMap<FString, FString>& InVariables);
	};

	struct FWorkspaceSyncCategory
	{
		FGuid UniqueId;
		bool bEnable;
		FString Name;
		TArray<FString> Paths;

		FWorkspaceSyncCategory(const FGuid& InUniqueId);
		FWorkspaceSyncCategory(const FGuid& InUniqueId, const TCHAR* InName, const TCHAR* InPaths);
	};
	*/

	// TODO super hacky... fix up later
	#if PLATFORM_WINDOWS
		const TCHAR* HostPlatform = TEXT("Win64");
	#elif PLATFORM_MAC
		const TCHAR* HostPlatform = TEXT("Mac");
	#else
		const TCHAR* HostPlatform = TEXT("Linux");
	#endif

	class FLineWriter : public FLineBasedTextWriter
	{
	public:
		virtual void FlushLine(const FString& Line) override
		{
			UE_LOG(LogSlateUGS, Log, TEXT("%s"), *Line);
		}
	};

	// TODO move most of these to a class with access to the settings
	//bool ShouldSyncPrecompiledEditor(TSharedPtr<FUserSettings> Settings)
	bool ShouldSyncPrecompiledEditor()
	{
		return false;
		//return Settings->bSyncPrecompiledEditor;// && PerforceMonitor->HasZippedBinaries();
	}

	// Honestly ... seems ... super hacky/hardcoded. With out all these you assert when trying to merge build targets sooo a bit odd
	// TODO Need to do each of these ... per ... platform??
	TMap<FGuid, FCustomConfigObject> GetDefaultBuildStepObjects(const FString& EditorTargetName, TSharedPtr<FUserSettings> Settings)
	{
		TArray<FBuildStep> DefaultBuildSteps;
		DefaultBuildSteps.Add(FBuildStep(FGuid(0x01F66060, 0x73FA4CC8, 0x9CB3E217, 0xFBBA954E), 0, TEXT("Compile UnrealHeaderTool"), TEXT("Compiling UnrealHeaderTool..."), 1, TEXT("UnrealHeaderTool"), HostPlatform, TEXT("Development"), TEXT(""), !ShouldSyncPrecompiledEditor()));
		FString ActualEditorTargetName = (EditorTargetName.Len() > 0) ? EditorTargetName : "UnrealEditor";
		DefaultBuildSteps.Add(FBuildStep(FGuid(0xF097FF61, 0xC9164058, 0x839135B4, 0x6C3173D5), 1, FString::Printf(TEXT("Compile %s"), *ActualEditorTargetName), FString::Printf(TEXT("Compiling %s..."), *ActualEditorTargetName), 10, ActualEditorTargetName, HostPlatform, ::ToString(Settings->CompiledEditorBuildConfig), TEXT(""), !ShouldSyncPrecompiledEditor()));
		DefaultBuildSteps.Add(FBuildStep(FGuid(0xC6E633A1, 0x956F4AD3, 0xBC956D06, 0xD131E7B4), 2, TEXT("Compile ShaderCompileWorker"), TEXT("Compiling ShaderCompileWorker..."), 1, TEXT("ShaderCompileWorker"), HostPlatform, TEXT("Development"), TEXT(""), !ShouldSyncPrecompiledEditor()));
		DefaultBuildSteps.Add(FBuildStep(FGuid(0x24FFD88C, 0x79014899, 0x9696AE10, 0x66B4B6E8), 3, TEXT("Compile UnrealLightmass"), TEXT("Compiling UnrealLightmass..."), 1, TEXT("UnrealLightmass"), HostPlatform, TEXT("Development"), TEXT(""), !ShouldSyncPrecompiledEditor()));
		DefaultBuildSteps.Add(FBuildStep(FGuid(0xFFF20379, 0x06BF4205, 0x8A3EC534, 0x27736688), 4, TEXT("Compile CrashReportClient"), TEXT("Compiling CrashReportClient..."), 1, TEXT("CrashReportClient"), HostPlatform, TEXT("Development"), TEXT(""), !ShouldSyncPrecompiledEditor()));
		DefaultBuildSteps.Add(FBuildStep(FGuid(0x89FE8A79, 0xD2594C7B, 0xBFB468F7, 0x218B91C2), 5, TEXT("Compile UnrealInsights"), TEXT("Compiling UnrealInsights..."), 1, TEXT("UnrealInsights"), HostPlatform, TEXT("Development"), TEXT(""), !ShouldSyncPrecompiledEditor()));
		DefaultBuildSteps.Add(FBuildStep(FGuid(0x46312669, 0x5069428D, 0x8D72C241, 0x6C5A322E), 6, TEXT("Launch UnrealInsights"), TEXT("Running UnrealInsights..."), 1, TEXT("UnrealInsights"), HostPlatform, TEXT("Shipping"), TEXT(""), !ShouldSyncPrecompiledEditor()));
		DefaultBuildSteps.Add(FBuildStep(FGuid(0xBB48CA5B, 0x56824432, 0x824DC451, 0x336A6523), 7, TEXT("Compile Zen Dashboard"), TEXT("Compile ZenDashboard Step..."), 1, TEXT("ZenDashboard"), HostPlatform, TEXT("Development"), TEXT(""), !ShouldSyncPrecompiledEditor()));
		DefaultBuildSteps.Add(FBuildStep(FGuid(0x586CC0D3, 0x39144DF9, 0xACB62C02, 0xCD9D4FC6), 8, TEXT("Launch Zen Dashboard"), TEXT("Running Zen Dashboard..."), 1, TEXT("ZenDashboard"), HostPlatform, TEXT("Development"), TEXT(""), !ShouldSyncPrecompiledEditor()));
		DefaultBuildSteps.Add(FBuildStep(FGuid(0x91C2A429, 0xC39149B4, 0x92A54E6B, 0xE71E0F00), 9, TEXT("Compile SwitchboardListener"), TEXT("Compiling SwitchboardListener..."), 1, TEXT("SwitchboardListener"), HostPlatform, TEXT("Development"), TEXT(""), !ShouldSyncPrecompiledEditor()));
		DefaultBuildSteps.Add(FBuildStep(FGuid(0x5036C75B, 0x8DF04329, 0x82A1869D, 0xD2D48605), 10, TEXT("Compile UnrealMultiUserServer"), TEXT("Compiling UnrealMultiUserServer..."), 1, TEXT("UnrealMultiUserServer"), HostPlatform, TEXT("Development"), TEXT(""), !ShouldSyncPrecompiledEditor()));
		DefaultBuildSteps.Add(FBuildStep(FGuid(0x274B89C3, 0x9DC64465, 0xA50840AB, 0xC4593CC2), 11, TEXT("Compile UnrealMultiUserSlateServer"), TEXT("Compiling UnrealMultiUserSlateServer..."), 1, TEXT("UnrealMultiUserSlateServer"), HostPlatform, TEXT("Development"), TEXT(""), !ShouldSyncPrecompiledEditor()));

		TMap<FGuid,FCustomConfigObject> DefaultBuildStepObjects;
		for (const FBuildStep& DefaultBuildStep : DefaultBuildSteps)
		{
			DefaultBuildStepObjects.Add(DefaultBuildStep.UniqueId, DefaultBuildStep.ToConfigObject());
		}

		return DefaultBuildStepObjects;
	}

	EBuildConfig GetEditorBuildConfig()
	{
		return EBuildConfig::Development;
		//return ShouldSyncPrecompiledEditor()? EBuildConfig::Development : Settings->CompiledEditorBuildConfig;
	}

	FString GetEditorExePath(EBuildConfig Config, TSharedPtr<FDetectProjectSettingsTask> DetectSettings)
	{
		FString ExeFileName = TEXT("UnrealEditor");

		if (Config != EBuildConfig::DebugGame && Config != EBuildConfig::Development)
		{
	#if PLATFORM_WINDOWS
			ExeFileName = FString::Printf(TEXT("UnrealEditor-%s-%s.exe"), HostPlatform, *::ToString(Config));
	#else
			ExeFileName = FString::Printf(TEXT("UnrealEditor-%s-%s"), HostPlatform, *::ToString(Config));
	#endif
		}
		return DetectSettings->BranchDirectoryName / "Engine" / "Binaries" / HostPlatform / ExeFileName;
	}

	TMap<FString, FString> GetWorkspaceVariables(TSharedPtr<FDetectProjectSettingsTask> DetectSettings)
	{
		EBuildConfig EditorBuildConfig = GetEditorBuildConfig();

		TMap<FString, FString> Variables;
		Variables.Add("BranchDir", DetectSettings->BranchDirectoryName);
		Variables.Add("ProjectDir", FPaths::GetPath(DetectSettings->NewSelectedFileName));
		Variables.Add("ProjectFile", DetectSettings->NewSelectedFileName);

		// Todo: These might not be called "UE4*" anymore
		Variables.Add("UE4EditorExe", GetEditorExePath(EditorBuildConfig, DetectSettings));
		Variables.Add("UE4EditorCmdExe", GetEditorExePath(EditorBuildConfig, DetectSettings).Replace(TEXT(".exe"), TEXT("-Cmd.exe")));
		Variables.Add("UE4EditorConfig", ::ToString(EditorBuildConfig));
		Variables.Add("UE4EditorDebugArg", (EditorBuildConfig == EBuildConfig::Debug || EditorBuildConfig == EBuildConfig::DebugGame)? " -debug" : "");
		return Variables;
	}

	static FString FormatUserName(FString UserName)
	{
		TStringBuilder<256> NormalUserName;
		for(int Index = 0; Index < UserName.Len(); Index++)
		{
			if(Index == 0 || UserName[Index - 1] == '.')
			{
				NormalUserName.AppendChar(FChar::ToUpper(UserName[Index]));
			}
			else if(UserName[Index] == '.')
			{
				NormalUserName.AppendChar(' ');
			}
			else
			{
				NormalUserName.AppendChar(FChar::ToLower(UserName[Index]));
			}
		}
		return NormalUserName.ToString();
	}
}

bool UGSTab::OnWorkspaceChosen(const FString& Project)
{
	bool bIsDataValid = FPaths::FileExists(Project); // Todo: Check that the project file is also associated with a workspace
	if (bIsDataValid)
	{
		ProjectFileName = Project;

		SetupWorkspace();
		GameSyncTabView->SetStreamPathText(FText::FromString(DetectSettings->StreamName));
		GameSyncTabView->SetChangelistText(FText::FromString(FString::FromInt(WorkspaceSettings->CurrentChangeNumber)));
		GameSyncTabView->SetProjectPathText(FText::FromString(ProjectFileName));
		TabWidget->SetContent(GameSyncTabView);
		TabWidget->SetLabel(FText::FromString(DetectSettings->StreamName));

		return true;
	}

	return false;
}

void UGSTab::OnSyncChangelist(int Changelist)
{
	TSharedRef<FWorkspaceUpdateContext, ESPMode::ThreadSafe> Context = MakeShared<FWorkspaceUpdateContext, ESPMode::ThreadSafe>(
		Changelist,
		Options,
		CombinedSyncFilter,
		GetDefaultBuildStepObjects(DetectSettings->NewProjectEditorTarget, UserSettings),
		ProjectSettings->BuildSteps,
		TSet<FGuid>(),
		GetWorkspaceVariables(DetectSettings));

	// Update the workspace with the Context!
	Workspace->Update(Context);
}

void UGSTab::OnSyncLatest()
{
	int ChangeNumber = -1;
	FEvent* AbortEvent = FPlatformProcess::GetSynchEventFromPool(true);

	if (PerforceClient->LatestChangeList(ChangeNumber, AbortEvent, MakeShared<FLogWidgetTextWriter>(GameSyncTabView->GetSyncLog().ToSharedRef()).Get()))
	{
		OnSyncChangelist(ChangeNumber);
	}

	FPlatformProcess::ReturnSynchEventToPool(AbortEvent);
}

void UGSTab::OnSyncFilterWindowSaved(
	const TArray<FString>& SyncViewCurrent,
	const TArray<FGuid>& SyncExcludedCategoriesCurrent,
	const TArray<FString>& SyncViewAll,
	const TArray<FGuid>& SyncExcludedCategoriesAll)
{
	WorkspaceSettings->SyncView = SyncViewCurrent;
	WorkspaceSettings->SyncExcludedCategories = SyncExcludedCategoriesCurrent;

	UserSettings->SyncView = SyncViewAll;
	UserSettings->SyncExcludedCategories = SyncExcludedCategoriesAll;

	UserSettings->Save();
}

void UGSTab::QueueMessageForMainThread(TFunction<void()> Function)
{
	FScopeLock Lock(&CriticalSection);

	MessageQueue.Add(Function);
	bHasQueuedMessages = true;
}

void UGSTab::Tick()
{
	if (bHasQueuedMessages)
	{
		bHasQueuedMessages = false;

		FScopeLock Lock(&CriticalSection);
		for (const TFunction<void()>& MessageFunction : MessageQueue)
		{
			MessageFunction();
		}

		MessageQueue.Empty();
	}
}

namespace
{
	bool ShouldShowChange(const FPerforceChangeSummary& Change, const TArray<FString>& ExcludeChanges)
	{
		for (const FString& ExcludeChange : ExcludeChanges)
		{
			FRegexPattern RegexPattern(*ExcludeChange, ERegexPatternFlags::CaseInsensitive);
			FRegexMatcher RegexMatcher(RegexPattern, Change.Description);

			if (RegexMatcher.FindNext())
			{
				return false;
			}
		}

		if (Change.User == TEXT("buildmachine") && Change.Description.Contains(TEXT("lightmaps")))
		{
			return false;
		}

		return true;
	}
}

bool UGSTab::ShouldIncludeInReviewedList(const TSet<int>& PromotedChangeNumbers, int ChangeNumber) const
{
	if (PromotedChangeNumbers.Contains(ChangeNumber))
	{
		return true;
	}

	TSharedPtr<FEventSummary> Review;
	if (EventMonitor->TryGetSummaryForChange(ChangeNumber, Review))
	{
		if (Review->LastStarReview.IsValid() && Review->LastStarReview->Type == EEventType::Starred)
		{
			return true;
		}

		if (Review->Verdict == EReviewVerdict::Good || Review->Verdict == EReviewVerdict::Mixed)
		{
			return true;
		}
	}

	return false;
}

void UGSTab::UpdateGameTabBuildList()
{
	TArray<TSharedRef<FPerforceChangeSummary, ESPMode::ThreadSafe>> Changes = PerforceMonitor->GetChanges();
	TArray<TSharedPtr<FChangeInfo>> ChangeInfos;

	// do we need to store these for something?
	TSet<int> PromotedChangeNumbers = PerforceMonitor->GetPromotedChangeNumbers();

	TArray<FString> ExcludeChanges;
	TSharedPtr<FCustomConfigFile, ESPMode::ThreadSafe> ProjectConfigFile = Workspace->GetProjectConfigFile();
	if (ProjectConfigFile.IsValid())
	{
		ProjectConfigFile->TryGetValues(TEXT("Options.ExcludeChanges"), ExcludeChanges);
	}

	bool bFirstChange = true;
	bool bOnlyShowReviewed = false;
	//  bool bOnlyShowReviewed = OnlyShowReviewedCheckBox.Checked;

	for (int ChangeIdx = 0; ChangeIdx < Changes.Num(); ChangeIdx++)
	{
		const FPerforceChangeSummary& Change = Changes[ChangeIdx].Get();
		if (ShouldShowChange(Change, ExcludeChanges) || PromotedChangeNumbers.Contains(Change.Number))
		{
			if (!bOnlyShowReviewed || (!EventMonitor->IsUnderInvestigation(Change.Number) && (ShouldIncludeInReviewedList(PromotedChangeNumbers, Change.Number) || bFirstChange)))
			{
				bFirstChange = false;

				FDateTime DisplayTime = Change.Date;
				if (UserSettings->bShowLocalTimes)
				{
					DisplayTime = (DisplayTime - DetectSettings->ServerTimeZone).GetDate();
				}

				TSharedPtr<FEventSummary> Review;
				EReviewVerdict Status = EReviewVerdict::Unknown;
				if (EventMonitor->TryGetSummaryForChange(Change.Number, Review) && Review->LastStarReview.IsValid())
				{
					if (Review->LastStarReview->Type == EEventType::Starred)
					{
						Status = EReviewVerdict::Good;
					}
					else
					{
						Status = Review->Verdict;
					}
				}

				TArray<FString> DescLines;

				// split on \n so we can just take the first line
				Change.Description.ParseIntoArray(DescLines, TEXT("\n"));

				ChangeInfos.Add(MakeShareable(new FChangeInfo{
					Status,
					Change.Number,
					FText::FromString(DisplayTime.ToString()),
					FText::FromString(FormatUserName(Change.User)),
					FText::FromString(DescLines[0].TrimStartAndEnd())}));
			}
		}
	}

	GameSyncTabView->AddHordeBuilds(ChangeInfos);
}

bool UGSTab::IsSyncing() const
{
	return Workspace->IsBusy();
}

FString UGSTab::GetSyncProgress() const
{
	return Workspace->GetCurrentProgress().Key;
}

TArray<FWorkspaceSyncCategory> UGSTab::GetSyncCategories(SyncCategoryType CategoryType) const
{
	TArray<FWorkspaceSyncCategory> SyncCategories;
	TMap<FGuid, FWorkspaceSyncCategory> Categories = Workspace->GetSyncCategories();
	TArray<FGuid> SyncExcludedCategories;

	if (CategoryType == SyncCategoryType::CurrentWorkspace)
	{
		SyncExcludedCategories = WorkspaceSettings->SyncExcludedCategories;
	}
	else
	{
		SyncExcludedCategories = UserSettings->SyncExcludedCategories;
	}

	for (const FGuid& Guid : SyncExcludedCategories)
	{
		Categories[Guid].bEnable = false;
	}
	Categories.GenerateValueArray(SyncCategories);
	return SyncCategories;
}

TArray<FString> UGSTab::GetSyncViews(SyncCategoryType CategoryType) const
{
	if (CategoryType == SyncCategoryType::CurrentWorkspace)
	{
		return WorkspaceSettings->SyncView;
	}
	return UserSettings->SyncView;
}

const TArray<FString>& UGSTab::GetCombinedSyncFilter() const
{
	return CombinedSyncFilter;
}

// This is getting called on a thread, lock our stuff up
void UGSTab::OnWorkspaceSyncComplete(TSharedRef<FWorkspaceUpdateContext, ESPMode::ThreadSafe> WorkspaceContext, EWorkspaceUpdateResult SyncResult, const FString& StatusMessage)
{
	FScopeLock Lock(&CriticalSection);

	WorkspaceSettings->CurrentChangeNumber     = Workspace->GetCurrentChangeNumber();
	WorkspaceSettings->LastBuiltChangeNumber   = Workspace->GetLastBuiltChangeNumber();
	WorkspaceSettings->LastSyncResult          = SyncResult;
	WorkspaceSettings->LastSyncResultMessage   = StatusMessage;
	WorkspaceSettings->LastSyncTime            = WorkspaceContext->StartTime;
	// TODO check this is valid, may be off
	WorkspaceSettings->LastSyncDurationSeconds = (FDateTime::UtcNow() - WorkspaceContext->StartTime).GetSeconds();

	// Queue up setting the changelist text on the main thread
	QueueMessageForMainThread([this] {
		GameSyncTabView->SetChangelistText(FText::FromString(FString::FromInt(WorkspaceSettings->CurrentChangeNumber)));
	});

	UserSettings->Save();
}

void UGSTab::SetupWorkspace()
{
	ProjectFileName = FUtility::GetPathWithCorrectCase(ProjectFileName);

	// TODO likely should also log this on an Empty tab... so we can show logging info when we are loading things
	DetectSettings = MakeShared<FDetectProjectSettingsTask>(MakeShared<FPerforceConnection>(TEXT(""), TEXT(""), TEXT("")), ProjectFileName, MakeShared<FLineWriter>());

	TSharedRef<FModalTaskResult> Result = ExecuteModalTask(
		FSlateApplication::Get().GetActiveModalWindow(),
		DetectSettings.ToSharedRef(),
		LOCTEXT("OpeningProjectTitle", "Opening Project"),
		LOCTEXT("OpeningProjectCaption", "Opening project, please wait..."));
	if (Result->Failed())
	{
		FMessageDialog::Open(EAppMsgType::Ok, Result->GetMessage());
		return;
	}

	FString DataFolder = FString(FPlatformProcess::UserSettingsDir()) / TEXT("UnrealGameSync");
	IFileManager::Get().MakeDirectory(*DataFolder);

	UserSettings = MakeShared<FUserSettings>(*(DataFolder / TEXT("UnrealGameSync_Slate.ini")));

	PerforceClient = DetectSettings->PerforceClient;
	WorkspaceSettings = UserSettings->FindOrAddWorkspace(*DetectSettings->BranchClientPath);
	ProjectSettings = UserSettings->FindOrAddProject(*DetectSettings->NewSelectedClientFileName);

	// Check if the project we've got open in this workspace is the one we're actually synced to
	int CurrentChangeNumber = -1;
	if (WorkspaceSettings->CurrentProjectIdentifier == DetectSettings->NewSelectedProjectIdentifier)
	{
		CurrentChangeNumber = WorkspaceSettings->CurrentChangeNumber;
	}

	FString ClientKey = DetectSettings->BranchClientPath.Replace(*FString::Printf(TEXT("//%s/"), *PerforceClient->ClientName), TEXT(""));
	if (ClientKey.EndsWith(TEXT("/")))
	{
		ClientKey = ClientKey.Left(ClientKey.Len() - 1);
	}

	FString ProjectLogBaseName = DataFolder / FString::Printf(TEXT("%s@%s"), *PerforceClient->ClientName, *ClientKey.Replace(TEXT("/"), TEXT("$")));
	FString TelemetryProjectIdentifier = FPerforceUtils::GetClientOrDepotDirectoryName(*DetectSettings->NewSelectedProjectIdentifier);

	FString LogFileName = DataFolder / FPaths::GetPath(ProjectFileName) + TEXT(".sync.log");
	GameSyncTabView->SetSyncLogLocation(LogFileName); // Todo: if SetSyncLogLocation fails, then it failed to create a log file and may need to handle that
	GameSyncTabView->GetSyncLog()->AppendLine(TEXT("Creating log at: ") + LogFileName);

	Workspace = MakeShared<FWorkspace>(
		PerforceClient.ToSharedRef(),
		DetectSettings->BranchDirectoryName,
		ProjectFileName,
		DetectSettings->BranchClientPath,
		DetectSettings->NewSelectedClientFileName,
		CurrentChangeNumber,
		WorkspaceSettings->LastBuiltChangeNumber,
		TelemetryProjectIdentifier,
		MakeShared<FLogWidgetTextWriter>(GameSyncTabView->GetSyncLog().ToSharedRef()));

	Workspace->OnUpdateComplete = [this] (TSharedRef<FWorkspaceUpdateContext, ESPMode::ThreadSafe> WorkspaceContext, EWorkspaceUpdateResult SyncResult, const FString& StatusMessage) {
		OnWorkspaceSyncComplete(WorkspaceContext, SyncResult, StatusMessage);
	};

	CombinedSyncFilter = FUserSettings::GetCombinedSyncFilter(
		Workspace->GetSyncCategories(),
		UserSettings->SyncView,
		UserSettings->SyncExcludedCategories,
		WorkspaceSettings->SyncView,
		WorkspaceSettings->SyncExcludedCategories);

	// Options on what to do with workspace when updating it
	Options = EWorkspaceUpdateOptions::Sync | EWorkspaceUpdateOptions::SyncArchives | EWorkspaceUpdateOptions::GenerateProjectFiles;
	if (UserSettings->bAutoResolveConflicts)
	{
		Options |= EWorkspaceUpdateOptions::AutoResolveChanges;
	}
	if (UserSettings->bUseIncrementalBuilds)
	{
		Options |= EWorkspaceUpdateOptions::UseIncrementalBuilds;
	}
	if (UserSettings->bBuildAfterSync)
	{
		Options |= EWorkspaceUpdateOptions::Build;
	}
	if (UserSettings->bBuildAfterSync && UserSettings->bRunAfterSync)
	{
		Options |= EWorkspaceUpdateOptions::RunAfterSync;
	}
	if (UserSettings->bOpenSolutionAfterSync)
	{
		Options |= EWorkspaceUpdateOptions::OpenSolutionAfterSync;
	}

	// Setup our Perforce and Event monitoring threads
	FString BranchClientPath = DetectSettings->BranchDirectoryName;
	FString SelectedClientFileName = DetectSettings->NewSelectedClientFileName;
	FString SelectedProjectIdentifier = DetectSettings->NewSelectedProjectIdentifier;

	// TODO create callback functions that will be queued for the main thread to generate and update the main table view
	PerforceMonitor = MakeShared<FPerforceMonitor>(PerforceClient.ToSharedRef(), BranchClientPath, SelectedClientFileName, SelectedProjectIdentifier, ProjectLogBaseName + ".p4.log");
	PerforceMonitor->OnUpdate = [this]{ QueueMessageForMainThread([this] { UpdateGameTabBuildList(); }); };

	//PerforceMonitor->OnUpdateMetadata = [this]{ printf("PerforceMonitor->OnUpdateMetadata\n"); }; //MessageQueue.Add([this]{ UpdateBuildMetadata(); }); };
	PerforceMonitor->OnStreamChange = [this]{ printf("PerforceMonitor->OnStreamChange\n"); }; // MessageQueue.Add([this]{ Owner->StreamChanged(this); }); };

	/* TODO figure out if this is even working, and if so how to correctly use this
	 */
	FString SqlConnectionString;
	EventMonitor = MakeShared<FEventMonitor>(SqlConnectionString, FPerforceUtils::GetClientOrDepotDirectoryName(*SelectedProjectIdentifier), PerforceClient->UserName, ProjectLogBaseName + ".review.log");
	EventMonitor->OnUpdatesReady = [this]{ printf("EventMonitor->OnUpdatesReady\n"); }; //MessageQueue.Add([this]{ UpdateReviews(); }); };

	// Start the threads
	PerforceMonitor->Start();
	EventMonitor->Start();
}

#undef LOCTEXT_NAMESPACE
