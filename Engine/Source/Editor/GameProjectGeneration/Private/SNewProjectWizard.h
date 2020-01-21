// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Visibility.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Input/Reply.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "HardwareTargetingSettings.h"
#include "Widgets/Views/STileView.h"
#include "SDecoratedEnumCombo.h"
#include "TemplateCategory.h"

class SWizard;
struct FTemplateItem;

/**
 * A wizard to create a new game project
 */
class SNewProjectWizard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS( SNewProjectWizard ){}

		SLATE_EVENT( FSimpleDelegate, OnTemplateDoubleClick )

	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct( const FArguments& InArgs );

	/** Populates TemplateItemsSource with templates found on disk */
	TMap<FName, TArray<TSharedPtr<FTemplateItem>> >& FindTemplateProjects();

	/** Create the page of project settings. */
	TSharedRef<SWidget> CreateProjectSettingsPage();

	/** Handle choosing a different category tab */
	void SetCurrentCategory(FName Category);

	/** Returns true if the user is allowed to specify a project with the supplied name and path */
	bool CanCreateProject() const;

	/** Begins the creation process for the configured project */
	void CreateAndOpenProject();

	/** Should we show the project settings page? */
	bool ShouldShowProjectSettingsPage() const;

private:

	/** Accessor for the currently selected template item */
	TSharedPtr<FTemplateItem> GetSelectedTemplateItem() const;

	/** Helper function to allow direct lookup of the selected item's properties on a delegate */
	/** TRet should be defaulted but VS2012 doesn't allow default template arguments on non-class templates */
	template<typename T>
	T GetSelectedTemplateProperty(T FTemplateItem::*Prop) const
	{
		TSharedPtr<FTemplateItem> SelectedItem = GetSelectedTemplateItem();
		if ( SelectedItem.IsValid() )
		{
			return (*SelectedItem).*Prop;
		}

		return T();
	}

	/** Accessor for the project name text */
	FText GetCurrentProjectFileName() const;

	/** Accessor for the project filename string with the extension */
	FString GetCurrentProjectFileNameStringWithExtension() const;

	/** Handler for when the project name string was changed */
	void OnCurrentProjectFileNameChanged(const FText& InValue);

	/** Accessor for the project path text */
	FText GetCurrentProjectFilePath() const;

	/** Accessor for the project ParentFolder string */
	FString GetCurrentProjectFileParentFolder() const;

	/** Handler for when the project path string was changed */
	void OnCurrentProjectFilePathChanged(const FText& InValue);

	/** Accessor for the label to preview the current filename with path */
	FString GetProjectFilenameWithPathLabelText() const;

	/** Get the images for the selected template preview and category */
	const FSlateBrush* GetSelectedTemplatePreviewImage() const;

	/** Get the visibility for the selected template preview image */
	EVisibility GetSelectedTemplatePreviewVisibility() const;

	/** Get a string that details the class types referenced in the selected template */
	FText GetSelectedTemplateClassTypes() const;

	/** Get a visiblity of the class types display. If the string is empty this return Collapsed otherwise it will return Visible */
	EVisibility GetSelectedTemplateClassVisibility() const;
	
	/** Get a string that details the class types referenced in the selected template */
	FText GetSelectedTemplateAssetTypes() const;	
	
	/** Get a visiblity of the asset types display. If the string is empty this return Collapsed otherwise it will return Visible */
	EVisibility GetSelectedTemplateAssetVisibility() const;

	/** Gets the assembled project filename with path */
	FString GetProjectFilenameWithPath() const;

	/** Gets the visibility of the error label */
	EVisibility GetGlobalErrorLabelVisibility() const;

	/** Gets the visibility of the error label close button. Only visible when displaying a persistent error */
	EVisibility GetGlobalErrorLabelCloseButtonVisibility() const;

	/** 
	 * Gets the visibility of the location box on the template list page. 
	 * Only visible if the selected template doesn't want to show settings.
	 */
	EVisibility GetTemplateListLocationBoxVisibility() const;

	/** Gets the text to display in the error label */
	FText GetGlobalErrorLabelText() const;

	/** Handler for when the close button on the error label is clicked */
	FReply OnCloseGlobalErrorLabelClicked();

	/** Gets the visibility of the error label */
	EVisibility GetNameAndLocationErrorLabelVisibility() const;

	/** Gets the text to display in the error label */
	FText GetNameAndLocationErrorLabelText() const;

	/** Handler for when the "copy starter content" checkbox changes state */
	void OnSetCopyStarterContent(int32 InCopyStarterContent);

	/** Get whether we are copying starter content or not */
	int32 GetCopyStarterContentIndex() const { return bCopyStarterContent ? 1 : 0; };

	/** Returns the visibility of the starter content warning */
	EVisibility GetStarterContentWarningVisibility() const;

	/** Returns the tooltip text (actual warning) of the starter content warning */
	FText GetStarterContentWarningTooltip() const;

	int32 OnGetXREnabled() const { return bEnableXR ? 1 : 0; }
	void OnSetXREnabled(int32 InEnumIndex) { bEnableXR = InEnumIndex == 1; }

	int32 OnGetRaytracingEnabled() const { return bEnableRaytracing ? 1 : 0; }
	void OnSetRaytracingEnabled(int32 InEnumIndex) { bEnableRaytracing = InEnumIndex == 1; }

	int32 OnGetBlueprintOrCppIndex() const;
	void OnSetBlueprintOrCppIndex(int32 Index);

	/** Create the project location widget. */
	TSharedRef<SWidget> MakeProjectLocationWidget();

private:

	EHardwareClass::Type SelectedHardwareClassTarget;
	void SetHardwareClassTarget(EHardwareClass::Type InHardwareClass);
	EHardwareClass::Type GetHardwareClassTarget() const { return SelectedHardwareClassTarget; }

	EGraphicsPreset::Type SelectedGraphicsPreset;
	void SetGraphicsPreset(EGraphicsPreset::Type InGraphicsPreset);
	EGraphicsPreset::Type GetGraphicsPreset() const { return SelectedGraphicsPreset; }

	/** Sets the default project name and path */
	void SetDefaultProjectLocation();

	/** Checks the current project path an name for validity and updates cached values accordingly */
	void UpdateProjectFileValidity();

	/** Returns true if we have a code template selected */
	bool IsCompilerRequired() const;

	/** Opens the specified project file */
	bool OpenProject(const FString& ProjectFile);

	/** Opens the solution for the specified project */
	bool OpenCodeIDE(const FString& ProjectFile);

	/** Creates a project with the supplied project filename */
	bool CreateProject(const FString& ProjectFile);

	/** Closes the containing window, but only if summoned via the editor so the non-game version doesn't just close to desktop. */
	void CloseWindowIfAppropriate( bool ForceClose = false );

	/** Displays an error to the user */
	void DisplayError(const FText& ErrorText);

	/** Handler for when the Browse button is clicked */
	FReply HandleBrowseButtonClicked( );

	/** Called when a user double-clicks a template item in the template project list. */
	void HandleTemplateListViewDoubleClick( TSharedPtr<FTemplateItem> TemplateItem );

	/** Handler for when the selection changes in the template list */
	void HandleTemplateListViewSelectionChanged( TSharedPtr<FTemplateItem> TemplateItem, ESelectInfo::Type SelectInfo );

	TSharedRef<SWidget> MakeProjectSettingsOptionsBox();

	/** Create a project information struct from the currently selected settings. */
	struct FProjectInformation CreateProjectInfo() const;

private:

	FString LastBrowsePath;
	FString CurrentProjectFileName;
	FString CurrentProjectFilePath;
	FText PersistentGlobalErrorLabelText;

	FSimpleDelegate OnTemplateDoubleClick;

	/** The global error text from the last validity check */
	FText LastGlobalValidityErrorText;

	/** The global error text from the last validity check */
	FText LastNameAndLocationValidityErrorText;

	/** True if the last global validity check returned that the project path is valid for creation */
	bool bLastGlobalValidityCheckSuccessful;

	/** True if the last NameAndLocation validity check returned that the project path is valid for creation */
	bool bLastNameAndLocationValidityCheckSuccessful;

	/** True if user has selected to copy starter content. */
	bool bCopyStarterContent;

	/** Whether or not to enable XR in the created project. */
	bool bEnableXR;

	/** Whether or not to enable Raytracing in the created project. */
	bool bEnableRaytracing;

	/** Whether or not we should use the blueprint or C++ version of this template. */
	bool bShouldGenerateCode;

	/** Name of the currently selected category */
	FName ActiveCategory;

	/** A map of category name to array of templates available for that category */
	TMap<FName, TArray<TSharedPtr<FTemplateItem>> > Templates;

	/** The filtered array of templates we are currently showing */
	TArray<TSharedPtr<FTemplateItem> > FilteredTemplateList;

	/** The slate widget representing the list of templates */
	TSharedPtr<STileView<TSharedPtr<FTemplateItem>>> TemplateListView;

	/** Names for pages */
	static FName TemplatePageName;
	static FName NameAndLocationPageName;
};
