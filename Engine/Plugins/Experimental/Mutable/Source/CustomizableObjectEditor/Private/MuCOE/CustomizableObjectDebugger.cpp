// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/CustomizableObjectDebugger.h"
#include "MuCOE/CustomizableObjectDebuggerActions.h"
#include "MuCOE/CustomizableObjectEditorModule.h"
#include "MuCOE/CustomizableObjectEditorStyle.h"
#include "MuCOE/CustomizableObjectEditor.h"

#include "MuCOE/SCustomizableObjectProperties.h"
#include "SMutableGraphViewer.h"
#include "SMutableCodeViewer.h"
#include "SMutableObjectViewer.h"

#include "Editor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "DesktopPlatformModule.h"
#include "ContentBrowserModule.h"
#include "PropertyEditorModule.h"

#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Types/SlateEnums.h"

#include "PreviewScene.h"
#include "ScopedTransaction.h"
#include "FileHelpers.h"
#include "BusyCursor.h"
#include "EditorDirectories.h"

#include "WorkspaceMenuStructureModule.h"
#include "WorkspaceMenuStructure.h"
#include "Engine/Selection.h"
#include "Components/SkeletalMeshComponent.h"

#include "IDetailsView.h"
#include "WorkspaceMenuStructure.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Notifications/NotificationManager.h"

#include "GenericPlatform/GenericPlatformApplicationMisc.h"

#include "HAL/PlatformApplicationMisc.h"


#define LOCTEXT_NAMESPACE "CustomizableObjectDebugger"

DEFINE_LOG_CATEGORY_STATIC(LogCustomizableObjectDebugger, Log, All);
const FName FCustomizableObjectDebugger::MutableNewTabId(TEXT("CustomizableObjectDebugger_NewTab"));


void FCustomizableObjectDebugger::InitCustomizableObjectDebugger( 
	const EToolkitMode::Type Mode, 
	const TSharedPtr< class IToolkitHost >& InitToolkitHost, 
	UCustomizableObject* ObjectToEdit )
{
	CustomizableObject = ObjectToEdit;

	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout( "Standalone_CustomizableObjectDebugger_Layout_v3" )
	->AddArea
	(
		FTabManager::NewPrimaryArea()
		->SetOrientation(Orient_Horizontal)
		->Split
		(
			FTabManager::NewStack()
			->SetSizeCoefficient(0.6f)
			->AddTab(MutableNewTabId, ETabState::ClosedTab)
		)
	);

	const bool bCreateDefaultStandaloneMenu = false;
	const bool bCreateDefaultToolbar = false;
	FAssetEditorToolkit::InitAssetEditor( Mode, InitToolkitHost, CustomizableObjectDebuggerAppIdentifier, StandaloneDefaultLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, ObjectToEdit );

	// Open a tab for the object being debugged.
	TSharedPtr<SDockTab> NewMutableObjectTab = SNew(SDockTab)
		.Label(FText::FromString(FString::Printf(TEXT("Object [%s]"),*CustomizableObject->GetName())))
		[
			SNew(SMutableObjectViewer, CustomizableObject, TabManager, MutableNewTabId)
		];

	TabManager->InsertNewDocumentTab(MutableNewTabId, FTabManager::ESearchPreference::PreferLiveTab, NewMutableObjectTab.ToSharedRef());

}


const FSlateBrush* FCustomizableObjectDebugger::GetDefaultTabIcon() const
{
	return FCustomizableObjectEditorStyle::Get().GetBrush("CustomizableObjectEditor.Debug");
}


FName FCustomizableObjectDebugger::GetToolkitFName() const
{
	return FName("CustomizableObjectDebugger");
}


FText FCustomizableObjectDebugger::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "Customizable Object Editor");
}


void FCustomizableObjectDebugger::AddReferencedObjects( FReferenceCollector& Collector )
{
	Collector.AddReferencedObject( CustomizableObject );
}


FText FCustomizableObjectDebugger::GetToolkitName() const
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("ObjectName"), FText::FromString(GetEditingObject()->GetName()));
	Args.Add(TEXT("ToolkitName"), GetBaseToolkitName());
	return FText::Format(LOCTEXT("AppLabelWithAssetName", "Debug {ObjectName} - {ToolkitName}"), Args);
}


FString FCustomizableObjectDebugger::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("DebuggerWorldCentricTabPrefix", "CustomizableObjectDebugger ").ToString();
}


FLinearColor FCustomizableObjectDebugger::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.3f, 0.2f, 0.5f, 0.5f);
}


#undef LOCTEXT_NAMESPACE
