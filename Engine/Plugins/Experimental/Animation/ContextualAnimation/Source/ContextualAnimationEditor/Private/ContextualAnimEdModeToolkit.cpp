// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimEdModeToolkit.h"
#include "ContextualAnimEdMode.h"
#include "Engine/Selection.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "EditorModeManager.h"
#include "Modules/ModuleManager.h"
#include "ContextualAnimEdModeSettings.h"
#include "ContextualAnimPreviewManager.h"

#define LOCTEXT_NAMESPACE "ContextualAnimEdModeToolkit"

FContextualAnimEdModeToolkit::FContextualAnimEdModeToolkit()
	: Settings(nullptr)
{
	Settings = NewObject<UContextualAnimEdModeSettings>(UContextualAnimEdModeSettings::StaticClass());
}

void FContextualAnimEdModeToolkit::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Settings);
}

void FContextualAnimEdModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs Args;
	Args.bAllowSearch = false;
	Args.bHideSelectionTip = true;

	EdModeSettingsWidget = PropertyModule.CreateDetailView(Args);
	EdModeSettingsWidget->SetObject(Settings);

	PreviewManagerWidget = PropertyModule.CreateDetailView(Args);
	PreviewManagerWidget->SetObject(GetContextualAnimEdMode()->PreviewManager);

	SAssignNew(ToolkitWidget, SBorder)
		.HAlign(HAlign_Center)
		.Padding(5)
		[
			SNew(SVerticalBox)
			
			+ SVerticalBox::Slot()
			.HAlign(HAlign_Left)
			.AutoHeight()
			.Padding(5)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(FText::FromString("\
					- Select the class for your test actor\n\
					- Start Simulating Mode\n\
					- Alt + Click to spawn a test actor\n\
					- Alt + Click or WASD to move the test actor around\n\
					- [Enter] to start / stop an interaction"))
			]

			+ SVerticalBox::Slot()
			.HAlign(HAlign_Left)
			.AutoHeight()
			[
				EdModeSettingsWidget.ToSharedRef()
			]

			+ SVerticalBox::Slot()
			.HAlign(HAlign_Left)
			.AutoHeight()
			[
				PreviewManagerWidget.ToSharedRef()
			]
		];

	FModeToolkit::Init(InitToolkitHost);
}

FName FContextualAnimEdModeToolkit::GetToolkitFName() const
{
	return FName("ContextualAnimEdMode");
}

FText FContextualAnimEdModeToolkit::GetBaseToolkitName() const
{
	return NSLOCTEXT("ContextualAnimEdModeToolkit", "DisplayName", "Contextual Anim Tool");
}

FEdMode* FContextualAnimEdModeToolkit::GetEditorMode() const
{
	return GLevelEditorModeTools().GetActiveMode(FContextualAnimEdMode::EM_ContextualAnimEdModeId);
}

FContextualAnimEdMode* FContextualAnimEdModeToolkit::GetContextualAnimEdMode() const
{
	return static_cast<FContextualAnimEdMode*>(GetEditorMode());
}

#undef LOCTEXT_NAMESPACE
