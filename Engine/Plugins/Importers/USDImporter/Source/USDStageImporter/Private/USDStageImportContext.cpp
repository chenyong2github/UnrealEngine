// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDStageImportContext.h"

#include "USDAssetCache.h"
#include "USDLog.h"
#include "USDMemory.h"
#include "USDOptionsWindow.h"
#include "USDStageImportOptions.h"

#include "Dialogs/DlgPickPath.h"
#include "Editor.h"
#include "IMessageLogListing.h"
#include "MessageLogModule.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

FUsdStageImportContext::FUsdStageImportContext()
{
	World = nullptr;
	ImportOptions = NewObject< UUsdStageImportOptions >();
	bReadFromStageCache = false;
	bStageWasOriginallyOpen = false;
	SceneActor = nullptr;
	ImportedAsset = nullptr;
	AssetCache = nullptr;
	OriginalMetersPerUnit = 0.01f;
}

bool FUsdStageImportContext::Init(const FString& InName, const FString& InFilePath, const FString& InInitialPackagePath, EObjectFlags InFlags, bool bInIsAutomated, bool bIsReimport, bool bAllowActorImport)
{
	ObjectName = InName;
	FilePath = InFilePath;
	bIsAutomated = bInIsAutomated;
	ImportObjectFlags = InFlags | RF_Transactional;
	World = GEditor->GetEditorWorldContext().World();
	PackagePath = InInitialPackagePath;

	if ( !PackagePath.EndsWith( TEXT("/") ) )
	{
		PackagePath.Append( TEXT("/") );
	}

	FPaths::NormalizeFilename(FilePath);

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
			// e.g. "/Game/MyFolder/layername/"
			// We inject the package path here because this is what the automated import task upstream code will do. This way the importer
			// can always expect to receive /ContentPath/layername/
			PackagePath = FString::Printf( TEXT( "%s/%s/" ), *PickContentPathDlg->GetPath().ToString(), *InName );
		}

		ImportOptions->EnableActorImport( bAllowActorImport );

		// Show dialog for import options
		bool bProceedWithImport = SUsdOptionsWindow::ShowOptions( *ImportOptions );
		if (!bProceedWithImport)
		{
			return false;
		}
	}

	return true;
}
