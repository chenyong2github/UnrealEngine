// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDStageImportContext.h"

#include "USDLog.h"
#include "USDMemory.h"
#include "USDStageImportOptions.h"
#include "USDStageImportOptionsWindow.h"

#include "Dialogs/DlgPickPath.h"
#include "Editor.h"
#include "IMessageLogListing.h"
#include "MessageLogModule.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

FUsdStageImportContext::FUsdStageImportContext()
{
	ImportOptions = NewObject< UUsdStageImportOptions >();
	bReadFromStageCache = false;
	bStageWasOriginallyOpen = false;
	SceneActor = nullptr;
	ImportedPackage = nullptr;
	OriginalMetersPerUnit = 0.01f;
}

bool FUsdStageImportContext::Init(const FString& InName, const FString& InFilePath, const FString& InInitialPackagePath, EObjectFlags InFlags, bool bInIsAutomated, bool bIsReimport, bool bAllowActorImport)
{
	ObjectName = InName;
	FilePath = InFilePath;
	bIsAutomated = bInIsAutomated;
	ImportObjectFlags = InFlags | RF_Public | RF_Standalone | RF_Transactional;
	World = GEditor->GetEditorWorldContext().World();
	PackagePath = TEXT("/Game/"); // Trailing '/' is needed to set the default path

	FPaths::NormalizeFilename(FilePath);

	AssetsCache.Empty();
	PrimPathsToAssets.Empty();

	if(!bIsAutomated)
	{
		// Show dialog for content folder
		if (!bIsReimport)
		{
			TSharedRef<SDlgPickPath> PickContentPathDlg =
				SNew(SDlgPickPath)
				.Title(NSLOCTEXT("USDStageImportContext", "ChooseImportRootContentPath", "Choose where to place the imported USD assets"))
				.DefaultPath(FText::FromString( InInitialPackagePath ));

			if (PickContentPathDlg->ShowModal() == EAppReturnType::Cancel)
			{
				return false;
			}
			PackagePath = PickContentPathDlg->GetPath().ToString() + "/";
		}

		ImportOptions->EnableActorImport( bAllowActorImport );

		// Show dialog for import options
		bool bProceedWithImport = SUsdOptionsWindow::ShowImportOptions(*ImportOptions);
		if (!bProceedWithImport)
		{
			return false;
		}
	}

	return true;
}

void FUsdStageImportContext::AddErrorMessage(EMessageSeverity::Type MessageSeverity, FText ErrorMessage)
{
	TokenizedErrorMessages.Add(FTokenizedMessage::Create(MessageSeverity, ErrorMessage));
	UE_LOG(LogUsd, Error, TEXT("%s"), *ErrorMessage.ToString());
}

void FUsdStageImportContext::DisplayErrorMessages(bool bAutomated)
{
	if(!bAutomated)
	{
		//Always clear the old message after an import or re-import
		const TCHAR* LogTitle = TEXT("USDStageImport");
		FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
		TSharedPtr<class IMessageLogListing> LogListing = MessageLogModule.GetLogListing(LogTitle);
		LogListing->SetLabel(FText::FromString("USD Stage Import"));
		LogListing->ClearMessages();

		if (TokenizedErrorMessages.Num() > 0)
		{
			LogListing->AddMessages(TokenizedErrorMessages);
			MessageLogModule.OpenMessageLog(LogTitle);
		}
	}
	else
	{
		for (const TSharedRef<FTokenizedMessage>& Message : TokenizedErrorMessages)
		{
			UE_LOG(LogUsd, Error, TEXT("%s"), *Message->ToText().ToString());
		}
	}
}

void FUsdStageImportContext::ClearErrorMessages()
{
	TokenizedErrorMessages.Empty();
}

