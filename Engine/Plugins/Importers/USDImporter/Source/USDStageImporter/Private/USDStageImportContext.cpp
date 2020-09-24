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
