// Copyright Epic Games, Inc. All Rights Reserved. 
#include "InterchangePipelineConfigurationGeneric.h"

#include "CoreMinimal.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IMainFrameModule.h"
#include "Misc/Paths.h"
#include "SInterchangePipelineConfigurationDialog.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "Widgets/SWindow.h"

EInterchangePipelineConfigurationDialogResult UInterchangePipelineConfigurationGeneric::ShowPipelineConfigurationDialog()
{
	//Create and show the graph inspector UI dialog
	TSharedPtr<SWindow> ParentWindow;
	if(IMainFrameModule* MainFrame = FModuleManager::LoadModulePtr<IMainFrameModule>("MainFrame"))
	{
		ParentWindow = MainFrame->GetParentWindow();
	}
	TSharedRef<SWindow> Window = SNew(SWindow)
		.ClientSize(FVector2D(1000.f, 650.f))
		.Title(NSLOCTEXT("Interchange", "PipelineConfigurationGenericTitle", "Interchange Pipeline Configuration"));
	TSharedPtr<SInterchangePipelineConfigurationDialog> InterchangePipelineConfigurationDialog;

	Window->SetContent
	(
		SAssignNew(InterchangePipelineConfigurationDialog, SInterchangePipelineConfigurationDialog)
		.OwnerWindow(Window)
	);

	FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);
	
	if (InterchangePipelineConfigurationDialog->IsCanceled())
	{
		return EInterchangePipelineConfigurationDialogResult::Cancel;
	}
	
	if (InterchangePipelineConfigurationDialog->IsImportAll())
	{
		return EInterchangePipelineConfigurationDialogResult::ImportAll;
	}

	return EInterchangePipelineConfigurationDialogResult::Import;
}
