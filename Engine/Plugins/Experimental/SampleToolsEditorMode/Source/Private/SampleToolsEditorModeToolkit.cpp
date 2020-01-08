// Copyright Epic Games, Inc. All Rights Reserved.

#include "SampleToolsEditorModeToolkit.h"
#include "SampleToolsEditorMode.h"
#include "Engine/Selection.h"

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"



#include "EditorModeManager.h"

#define LOCTEXT_NAMESPACE "FSampleToolsEditorModeToolkit"

FSampleToolsEditorModeToolkit::FSampleToolsEditorModeToolkit()
{
}

void FSampleToolsEditorModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost)
{

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs(
		/*bUpdateFromSelection=*/ false,
		/*bLockable=*/ false,
		/*bAllowSearch=*/ false,
		FDetailsViewArgs::HideNameArea,
		/*bHideSelectionTip=*/ true,
		/*InNotifyHook=*/ nullptr,
		/*InSearchInitialKeyFocus=*/ false,
		/*InViewIdentifier=*/ NAME_None);
	DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;
	DetailsViewArgs.bShowOptions = false;
	DetailsViewArgs.bAllowMultipleTopLevelObjects = true;

	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);


	//////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////// 
	// AddYourTool Step 3 - add a button to initialize your Tool
	//////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////// 


	SAssignNew(ToolkitWidget, SBorder)
		.HAlign(HAlign_Center)
		.Padding(25)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(50)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text(LOCTEXT("HeaderLabel", "Sample Tools"))
				]

			+ SVerticalBox::Slot()
				.HAlign(HAlign_Center)
				.AutoHeight()
				[
					SNew(SButton).Text(LOCTEXT("CreateActorSampleToolLabel", "Create Actor on Click"))
					.OnClicked_Lambda([this]() { return this->StartTool(TEXT("CreateActorSampleTool")); })
					.IsEnabled_Lambda([this]() { return this->CanStartTool(TEXT("CreateActorSampleTool")); })
				]

			+ SVerticalBox::Slot()
				.HAlign(HAlign_Center)
				.AutoHeight()
				[
					SNew(SButton).Text(LOCTEXT("MeasureDistanceSampleToolLabel", "Measure Distance"))
					.OnClicked_Lambda([this]() { return this->StartTool(TEXT("MeasureDistanceSampleTool")); })
					.IsEnabled_Lambda([this]() { return this->CanStartTool(TEXT("MeasureDistanceSampleTool")); })
				]

			+ SVerticalBox::Slot()
				.HAlign(HAlign_Center)
				.AutoHeight()
				[
					SNew(SButton).Text(LOCTEXT("DrawCurveOnMeshSampleToolLabel", "Draw Curve On Mesh"))
					.OnClicked_Lambda([this]() { return this->StartTool(TEXT("DrawCurveOnMeshSampleTool")); })
					.IsEnabled_Lambda([this]() { return this->CanStartTool(TEXT("DrawCurveOnMeshSampleTool")); })
				]

			+ SVerticalBox::Slot()
				.HAlign(HAlign_Center)
				.AutoHeight()
				[
					SNew(SButton).Text(LOCTEXT("SurfacePointToolLabel", "Surface Point Tool"))
					.OnClicked_Lambda([this]() { return this->StartTool(TEXT("SurfacePointTool")); })
					.IsEnabled_Lambda([this]() { return this->CanStartTool(TEXT("SurfacePointTool")); })
				]


			+ SVerticalBox::Slot()
				.HAlign(HAlign_Center)
				.AutoHeight()
				[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton).Text(LOCTEXT("AcceptToolButtonLabel", "Accept"))
						.OnClicked_Lambda([this]() { return this->EndTool(EToolShutdownType::Accept); })
						.IsEnabled_Lambda([this]() { return this->CanAcceptActiveTool(); })
					]
				+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton).Text(LOCTEXT("CancelToolButtonLabel", "Cancel"))
						.OnClicked_Lambda([this]() { return this->EndTool(EToolShutdownType::Cancel); })
						.IsEnabled_Lambda([this]() { return this->CanCancelActiveTool(); })
					]
				+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton).Text(LOCTEXT("CompletedToolButtonLabel", "Complete"))
						.OnClicked_Lambda([this]() { return this->EndTool(EToolShutdownType::Completed); })
						.IsEnabled_Lambda([this]() { return this->CanCompleteActiveTool(); })
					]
				]

			+ SVerticalBox::Slot()
				.HAlign(HAlign_Center)
				.Padding(2)
				.AutoHeight()
				.MaxHeight(500.0f)
				[
					DetailsView->AsShared()
				]

		];
		
	FModeToolkit::Init(InitToolkitHost);
}


bool FSampleToolsEditorModeToolkit::CanStartTool(const FString& ToolTypeIdentifier)
{
	UInteractiveToolManager* Manager = GetToolsEditorMode()->GetToolManager();

	return (Manager->HasActiveTool(EToolSide::Left) == false) &&
		(Manager->CanActivateTool(EToolSide::Left, ToolTypeIdentifier) == true);
}

bool FSampleToolsEditorModeToolkit::CanAcceptActiveTool()
{
	return GetToolsEditorMode()->GetToolManager()->CanAcceptActiveTool(EToolSide::Left);
}

bool FSampleToolsEditorModeToolkit::CanCancelActiveTool()
{
	return GetToolsEditorMode()->GetToolManager()->CanCancelActiveTool(EToolSide::Left);
}

bool FSampleToolsEditorModeToolkit::CanCompleteActiveTool()
{
	return GetToolsEditorMode()->GetToolManager()->HasActiveTool(EToolSide::Left) && CanCancelActiveTool() == false;
}


FReply FSampleToolsEditorModeToolkit::StartTool(const FString& ToolTypeIdentifier)
{
	if (GetToolsEditorMode()->GetToolManager()->SelectActiveToolType(EToolSide::Left, ToolTypeIdentifier) == false)
	{
		UE_LOG(LogTemp, Warning, TEXT("ToolManager: Unknown Tool Type %s"), *ToolTypeIdentifier);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ToolManager: Starting Tool Type %s"), *ToolTypeIdentifier);
		GetToolsEditorMode()->GetToolManager()->ActivateTool(EToolSide::Left);

		// Update properties panel
		UInteractiveTool* CurTool = GetToolsEditorMode()->GetToolManager()->GetActiveTool(EToolSide::Left);
		DetailsView->SetObjects(CurTool->GetToolProperties());
	}
	return FReply::Handled();
}

FReply FSampleToolsEditorModeToolkit::EndTool(EToolShutdownType ShutdownType)
{
	UE_LOG(LogTemp, Warning, TEXT("ENDING TOOL"));

	GetToolsEditorMode()->GetToolManager()->DeactivateTool(EToolSide::Left, ShutdownType);

	DetailsView->SetObject(nullptr);

	return FReply::Handled();
}


FName FSampleToolsEditorModeToolkit::GetToolkitFName() const
{
	return FName("SampleToolsEditorMode");
}

FText FSampleToolsEditorModeToolkit::GetBaseToolkitName() const
{
	return NSLOCTEXT("SampleToolsEditorModeToolkit", "DisplayName", "SampleToolsEditorMode Tool");
}

class FEdMode* FSampleToolsEditorModeToolkit::GetEditorMode() const
{
	return GLevelEditorModeTools().GetActiveMode(FSampleToolsEditorMode::EM_SampleToolsEditorModeId);
}

FSampleToolsEditorMode* FSampleToolsEditorModeToolkit::GetToolsEditorMode() const
{
	return (FSampleToolsEditorMode*)GetEditorMode();
}

#undef LOCTEXT_NAMESPACE
