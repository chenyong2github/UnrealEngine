// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertTakeRecorderManager.h"

#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "Logging/LogMacros.h"

#include "IConcertClient.h"
#include "IConcertSyncClient.h"
#include "IConcertSession.h"
#include "IConcertSessionHandler.h"
#include "IConcertSyncClientModule.h"
#include "ConcertSyncArchives.h"
#include "ConcertSyncSettings.h"
#include "ConcertTakeRecorderStyle.h"

#include "ITakeRecorderModule.h"
#include "Recorder/TakeRecorder.h"
#include "TakeRecorderSources.h"
#include "TakeRecorderSettings.h"
#include "TakeRecorderSources.h"
#include "TakeRecorderSource.h"
#include "Recorder/TakeRecorderPanel.h"
#include "Recorder/TakeRecorderBlueprintLibrary.h"
#include "TakesCoreBlueprintLibrary.h"
#include "TakeMetaData.h"
#include "LevelSequence.h"

#include "UObject/UObjectGlobals.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SOverlay.h"

#include "EditorFontGlyphs.h"
#include "EditorStyleSet.h"

#include "IdentifierTable/ConcertIdentifierTable.h"
#include "UObject/ObjectMacros.h"
#include "Misc/MessageDialog.h"


DEFINE_LOG_CATEGORY_STATIC(LogConcertTakeRecorder, Warning, Log)

// Enable Take Syncing
static TAutoConsoleVariable<int32> CVarEnableTakeSync(TEXT("Concert.EnableTakeRecorderSync"), 0, TEXT("Enable Concert Take Recorder Syncing."));

#define LOCTEXT_NAMESPACE "ConcertTakeRecorder"

FConcertTakeRecorderManager::FConcertTakeRecorderManager()
{
	if (TSharedPtr<IConcertSyncClient> ConcertSyncClient = IConcertSyncClientModule::Get().GetClient(TEXT("MultiUser")))
	{
		IConcertClientRef ConcertClient = ConcertSyncClient->GetConcertClient();
		ConcertClient->OnSessionStartup().AddRaw(this, &FConcertTakeRecorderManager::Register);
		ConcertClient->OnSessionShutdown().AddRaw(this, &FConcertTakeRecorderManager::Unregister);
	
		if (TSharedPtr<IConcertClientSession> ConcertClientSession = ConcertClient->GetCurrentSession())
		{
			Register(ConcertClientSession.ToSharedRef());
		}

		CacheTakeSyncFolderFiltered();
		RegisterToolbarExtension();

		FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FConcertTakeRecorderManager::OnObjectModified);
		UTakeRecorder::OnRecordingInitialized().AddRaw(this, &FConcertTakeRecorderManager::OnTakeRecorderInitialized);
	}
	else
	{
		ensureAlwaysMsgf(false, TEXT("Could not find MultiUser client when creating ConcertTakeRecorderManager."));
	}
}

FConcertTakeRecorderManager::~FConcertTakeRecorderManager()
{
	UTakeRecorder::OnRecordingInitialized().RemoveAll(this);
	if (TSharedPtr<IConcertSyncClient> ConcertSyncClient = IConcertSyncClientModule::Get().GetClient(TEXT("MultiUser")))
	{
		IConcertClientRef ConcertClient = ConcertSyncClient->GetConcertClient();
		ConcertClient->OnSessionStartup().RemoveAll(this);
		ConcertClient->OnSessionShutdown().RemoveAll(this);
	}

	UnregisterToolbarExtension();
}

void FConcertTakeRecorderManager::Register(TSharedRef<IConcertClientSession> InSession)
{
	// Hold onto the session so we can trigger events
	WeakSession = InSession;

	// Register our events
	InSession->RegisterCustomEventHandler<FConcertTakeInitializedEvent>(this, &FConcertTakeRecorderManager::OnTakeInitializedEvent);
	InSession->RegisterCustomEventHandler<FConcertRecordingFinishedEvent>(this, &FConcertTakeRecorderManager::OnRecordingFinishedEvent);
	InSession->RegisterCustomEventHandler<FConcertRecordingCancelledEvent>(this, &FConcertTakeRecorderManager::OnRecordingCancelledEvent);
}

void FConcertTakeRecorderManager::Unregister(TSharedRef<IConcertClientSession> InSession)
{
	if (TSharedPtr<IConcertClientSession> Session = WeakSession.Pin())
	{
		check(Session == InSession);
	}
	WeakSession.Reset();
}

void FConcertTakeRecorderManager::RegisterToolbarExtension()
{
	static const FName ModuleName = "TakeRecorder";
	ITakeRecorderModule& Module = FModuleManager::LoadModuleChecked<ITakeRecorderModule>(ModuleName);
	Module.GetToolbarExtensionGenerators().AddRaw(this, &FConcertTakeRecorderManager::CreateExtensionWidget);
}

void FConcertTakeRecorderManager::UnregisterToolbarExtension()
{
	static const FName ModuleName = "TakeRecorder";
	ITakeRecorderModule& Module = FModuleManager::LoadModuleChecked<ITakeRecorderModule>(ModuleName);
	Module.GetToolbarExtensionGenerators().RemoveAll(this);
}

void FConcertTakeRecorderManager::CreateExtensionWidget(TArray<TSharedRef<SWidget>>& OutExtensions)
{
	const int ButtonBoxSize = 28;

	OutExtensions.Add(
		SNew(SBox)
		.Visibility_Raw(this, &FConcertTakeRecorderManager::HandleTakeSyncButtonVisibility)
		.WidthOverride(ButtonBoxSize)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
 				SNew(SCheckBox)
 				.Padding(4.f)
 				.ToolTipText(LOCTEXT("ToggleTakeRecorderSyncTooltip", "Toggle Multi-User Take Recorder Sync. If the option is enabled, starting/stopping/canceling a take will be synchronized with other Multi-User clients."))
 				.Style(FEditorStyle::Get(), "ToggleButtonCheckbox")
 				.ForegroundColor(FSlateColor::UseForeground())
				.IsChecked_Raw(this, &FConcertTakeRecorderManager::IsTakeSyncChecked)
				.OnCheckStateChanged_Raw(this, &FConcertTakeRecorderManager::HandleTakeSyncCheckBox)
 				[
					SNew(SImage)
					.Image(FConcertTakeRecorderStyle::Get()->GetBrush("Concert.TakeRecorder.SyncTakes.Small"))
 				]
			]
			+ SOverlay::Slot()
			[
				SNew(STextBlock)
				.Visibility_Raw(this, &FConcertTakeRecorderManager::HandleTakeSyncWarningVisibility)
				.ToolTipText_Raw(this, &FConcertTakeRecorderManager::GetWarningText)
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.8"))
				.Text(FEditorFontGlyphs::Exclamation_Triangle)
			]
		]
	);
}

void FConcertTakeRecorderManager::OnTakeRecorderInitialized(UTakeRecorder* TakeRecorder)
{
	TakeRecorder->OnRecordingFinished().AddRaw(this, &FConcertTakeRecorderManager::OnRecordingFinished);
	TakeRecorder->OnRecordingCancelled().AddRaw(this, &FConcertTakeRecorderManager::OnRecordingCancelled);

	if (IsTakeSyncEnabled() && TakeRecorderState.LastStartedTake != TakeRecorder->GetName())
	{
		if (TSharedPtr<IConcertClientSession> Session = WeakSession.Pin())
		{
			UTakeRecorderPanel* Panel = UTakeRecorderBlueprintLibrary::GetTakeRecorderPanel();
			check(Panel);

			ITakeRecorderModule& TakeRecorderModule = FModuleManager::LoadModuleChecked<ITakeRecorderModule>("TakeRecorder");
			UTakeMetaData* TakeMetaData = Panel->GetTakeMetaData();
			UTakePreset* TakePreset = TakeRecorderModule.GetPendingTake();

			if (TakeMetaData && TakePreset)
			{
				if (TakePreset->GetOutermost()->IsDirty())
				{
					FText WarningMessage(LOCTEXT("Warning_RevertChanges", "Cannot start a synchronized take since there are changes to the take preset. Either revert your changes or save the preset to start a synchronized take."));
					FMessageDialog::Open(EAppMsgType::Ok, WarningMessage);
					TakeRecorder->Stop();
					return;
				}

				FConcertTakeInitializedEvent TakeInitializedEvent;
				TakeInitializedEvent.TakeName = TakeRecorder->GetName();
				TakeInitializedEvent.TakePresetPath = TakeMetaData->GetPresetOrigin()->GetPathName();

				FConcertLocalIdentifierTable InLocalIdentifierTable;
				FConcertSyncObjectWriter Writer(&InLocalIdentifierTable, TakeMetaData, TakeInitializedEvent.TakeData, true, false);
				Writer.SerializeObject(TakeMetaData);

				InLocalIdentifierTable.GetState(TakeInitializedEvent.IdentifierState);
				Session->SendCustomEvent(TakeInitializedEvent, Session->GetSessionClientEndpointIds(), EConcertMessageFlags::ReliableOrdered);
			}
		}
	}
}

void FConcertTakeRecorderManager::OnRecordingFinished(UTakeRecorder* TakeRecorder)
{
	if (IsTakeSyncEnabled() && TakeRecorderState.LastStoppedTake != TakeRecorder->GetName())
	{
		if (TSharedPtr<IConcertClientSession> Session = WeakSession.Pin())
		{
			FConcertRecordingFinishedEvent RecordingFinishedEvent;
			RecordingFinishedEvent.TakeName = TakeRecorder->GetName();
			Session->SendCustomEvent(RecordingFinishedEvent, Session->GetSessionClientEndpointIds(), EConcertMessageFlags::ReliableOrdered);
		}
	}

	TakeRecorder->OnRecordingFinished().RemoveAll(this);
	TakeRecorder->OnRecordingCancelled().RemoveAll(this);
}

void FConcertTakeRecorderManager::OnRecordingCancelled(UTakeRecorder* TakeRecorder)
{
	if (IsTakeSyncEnabled() && TakeRecorderState.LastStoppedTake != TakeRecorder->GetName())
	{
		if (TSharedPtr<IConcertClientSession> Session = WeakSession.Pin())
		{
			FConcertRecordingCancelledEvent RecordingCancelledEvent;
			RecordingCancelledEvent.TakeName = TakeRecorder->GetName();
			Session->SendCustomEvent(RecordingCancelledEvent, Session->GetSessionClientEndpointIds(), EConcertMessageFlags::ReliableOrdered);
		}
	}

	TakeRecorder->OnRecordingFinished().RemoveAll(this);
	TakeRecorder->OnRecordingCancelled().RemoveAll(this);
}

void FConcertTakeRecorderManager::OnTakeInitializedEvent(const FConcertSessionContext&, const FConcertTakeInitializedEvent& InEvent)
{
	if (IsTakeSyncEnabled())
	{
		TakeRecorderState.LastStartedTake = InEvent.TakeName;

		UTakePreset* TakePreset = Cast<UTakePreset>(StaticFindObject(UTakePreset::StaticClass(), nullptr, *InEvent.TakePresetPath));
		if (!TakePreset)
		{
			TakePreset = Cast<UTakePreset>(StaticLoadObject(UObject::StaticClass(), nullptr, *InEvent.TakePresetPath));
		}

		if (TakePreset && TakePreset->GetLevelSequence())
		{
			// Stop the active recorder if it's running.
			UTakeRecorder* ActiveTakeRecorder = UTakeRecorder::GetActiveRecorder();
			if (ActiveTakeRecorder && ActiveTakeRecorder->GetState() == ETakeRecorderState::Started)
			{
				ActiveTakeRecorder->Stop();
			}

			UTakeMetaData* TakeMetadata = NewObject<UTakeMetaData>(GetTransientPackage(), NAME_None, EObjectFlags::RF_Transient);

			FConcertLocalIdentifierTable Table(InEvent.IdentifierState);
			FConcertSyncObjectReader Reader(&Table, FConcertSyncWorldRemapper(), nullptr, TakeMetadata, InEvent.TakeData);
			Reader.SerializeObject(TakeMetadata);

			ULevelSequence* LevelSequence = TakePreset->GetLevelSequence();
			UTakeRecorderSources* Sources = LevelSequence->FindMetaData<UTakeRecorderSources>();

			FTakeRecorderParameters DefaultParams;
			DefaultParams.User = GetDefault<UTakeRecorderUserSettings>()->Settings;
			DefaultParams.Project = GetDefault<UTakeRecorderProjectSettings>()->Settings;

			UTakeRecorder* NewRecorder = NewObject<UTakeRecorder>(GetTransientPackage(), NAME_None, RF_Transient);
			NewRecorder->Initialize(
				LevelSequence,
				Sources,
				TakeMetadata,
				DefaultParams
			);
		}
	}
}

void FConcertTakeRecorderManager::OnRecordingFinishedEvent(const FConcertSessionContext&, const FConcertRecordingFinishedEvent& InEvent)
{
	if (IsTakeSyncEnabled())
	{
		TakeRecorderState.LastStoppedTake = InEvent.TakeName;
		if (UTakeRecorder* ActiveTakeRecorder = UTakeRecorder::GetActiveRecorder())
		{
			ActiveTakeRecorder->Stop();
		}
	}
}

void FConcertTakeRecorderManager::OnRecordingCancelledEvent(const FConcertSessionContext&, const FConcertRecordingCancelledEvent& InEvent)
{
	if (IsTakeSyncEnabled())
	{
		TakeRecorderState.LastStoppedTake = InEvent.TakeName;
		if (UTakeRecorder* ActiveTakeRecorder = UTakeRecorder::GetActiveRecorder())
		{
			ActiveTakeRecorder->Stop();
		}
	}
}

bool FConcertTakeRecorderManager::IsTakeSyncEnabled() const
{
	return CVarEnableTakeSync.GetValueOnAnyThread() > 0 && bTakeSyncFolderFiltered;
}

ECheckBoxState FConcertTakeRecorderManager::IsTakeSyncChecked() const
{
	return IsTakeSyncEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FConcertTakeRecorderManager::HandleTakeSyncCheckBox(ECheckBoxState State) const
{
	if (!bTakeSyncFolderFiltered)
	{
		return;
	}

	CVarEnableTakeSync->AsVariable()->Set(State == ECheckBoxState::Checked ? 1 : 0);
}

EVisibility FConcertTakeRecorderManager::HandleTakeSyncButtonVisibility() const
{
	return WeakSession.IsValid() ? EVisibility::Visible : EVisibility::Collapsed;
}

FText FConcertTakeRecorderManager::GetWarningText() const
{
	return LOCTEXT("WarningLabel", "The take recorder root save directory must be added to the Multi-User transaction exclude filter in order to synchronize transactions.");
}

EVisibility FConcertTakeRecorderManager::HandleTakeSyncWarningVisibility() const
{
	return WeakSession.IsValid() && !bTakeSyncFolderFiltered ? EVisibility::Visible : EVisibility::Collapsed;
}

void FConcertTakeRecorderManager::OnObjectModified(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent)
{
	if (ObjectBeingModified == GetDefault<UConcertSyncConfig>() || ObjectBeingModified == GetDefault<UTakeRecorderProjectSettings>())
	{
		if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FPackageClassFilter, ContentPaths)
			|| PropertyChangedEvent.GetPropertyName() == TEXT("Path"))
		{
			CacheTakeSyncFolderFiltered();
		}
	}
}

void FConcertTakeRecorderManager::CacheTakeSyncFolderFiltered()
{
	bTakeSyncFolderFiltered = false;

	const FString& RootTakeSaveDir = GetDefault<UTakeRecorderProjectSettings>()->Settings.RootTakeSaveDir.Path;

	for (const FPackageClassFilter& Filter : GetDefault<UConcertSyncConfig>()->ExcludePackageClassFilters)
	{
		for (const FString& ContentPath : Filter.ContentPaths)
		{
			if (RootTakeSaveDir.MatchesWildcard(ContentPath))
			{
				bTakeSyncFolderFiltered = true;
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE /*ConcertTakeRecorder*/
