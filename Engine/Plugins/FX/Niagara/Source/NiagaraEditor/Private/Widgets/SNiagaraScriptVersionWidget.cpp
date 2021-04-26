// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraScriptVersionWidget.h"

#include "DetailLayoutBuilder.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "SGraphActionMenu.h"
#include "SlateOptMacros.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"
#include "NiagaraEditor/Private/NiagaraVersionMetaData.h"
#include "NiagaraEditor/Public/NiagaraActions.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STableViewBase.h"

#define LOCTEXT_NAMESPACE "NiagaraScriptVersionWidget"

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

FText SNiagaraScriptVersionWidget::FormatVersionLabel(const FNiagaraAssetVersion& Version) const
{
	FText Format = Version == Script->GetExposedVersion() ? FText::FromString("{0}.{1} (exposed)") : FText::FromString("{0}.{1}");
	return FText::Format(Format, Version.MajorVersion, Version.MinorVersion);
}

FText SNiagaraScriptVersionWidget::GetInfoHeaderText() const
{
	if (Script->IsVersioningEnabled())
	{
		return LOCTEXT("NiagaraManageVersionInfoHeader", "Here you can see and manage available script versions. Adding versions allows you to make changes without breaking existing assets.\nThe exposed version is the one that users will see when adding the module, function or dynamic input to the stack. If the exposed version has a higher major version than the one used in an asset, the user will see a prompt to upgrade to the new version.");	
	}
	return LOCTEXT("NiagaraManageVersionInfoHeaderDisabled", "Versioning is not yet enabled for this script. Enabling versioning allows you to make changes to the module without breaking exising usages. But it also forces users to manually upgrade to new versions, as changes are no longer pushed out automatically.\n\nDo you want to enable versioning for this script? This will convert the current script to version 1.0 and automatically expose it.");
}

FNiagaraVersionMenuAction::FNiagaraVersionMenuAction(FText InNodeCategory, FText InMenuDesc, FText InToolTip, const int32 InGrouping, FText InKeywords, FOnExecuteStackAction InAction, int32 InSectionID, FNiagaraAssetVersion InVersion)
	: FNiagaraMenuAction(MoveTemp(InNodeCategory), MoveTemp(InMenuDesc), MoveTemp(InToolTip), InGrouping, MoveTemp(InKeywords), MoveTemp(InAction), InSectionID)
	, AssetVersion(InVersion)
{}

void SNiagaraScriptVersionWidget::Construct(const FArguments& InArgs, UNiagaraScript* InScript, UNiagaraVersionMetaData* InMetadata)
{
	OnVersionDataChanged = InArgs._OnVersionDataChanged;
	OnChangeToVersion = InArgs._OnChangeToVersion;
	Script = InScript;
	VersionMetadata = InMetadata; 
	
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.bAllowSearch = false;
	DetailsArgs.NotifyHook = this;

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	VersionSettingsDetails = PropertyModule.CreateDetailView(DetailsArgs);
	VersionSettingsDetails->SetObject(VersionMetadata);

	// the list of available version
	SAssignNew(VersionListWidget, SGraphActionMenu)
	    .OnActionSelected(this, &SNiagaraScriptVersionWidget::OnActionSelected)
		.OnCollectAllActions(this, &SNiagaraScriptVersionWidget::CollectAllVersionActions)
	    .OnCollectStaticSections_Lambda([](TArray<int32>& StaticSectionIDs) {StaticSectionIDs.Add(1);})
	    .OnGetSectionTitle_Lambda([](int32) {return LOCTEXT("NiagaraVersionManagementTitle", "Versions");})
	    .AutoExpandActionMenu(true)
	    .ShowFilterTextBox(false)
	    .OnCreateCustomRowExpander_Static(&SNiagaraScriptVersionWidget::CreateCustomActionExpander)
	    .UseSectionStyling(true)
		.AlphaSortItems(false)
	    .OnGetSectionWidget(this, &SNiagaraScriptVersionWidget::GetVersionSelectionHeaderWidget)
		.OnContextMenuOpening(this, &SNiagaraScriptVersionWidget::OnVersionContextMenuOpening)
	    .OnCreateWidgetForAction_Lambda([](const FCreateWidgetForActionData* InData)
	    {
	        TSharedPtr<FNiagaraMenuAction> NiagaraAction = StaticCastSharedPtr<FNiagaraMenuAction>(InData->Action);
	        return SNew(STextBlock).Text(NiagaraAction->GetMenuDescription());
	    });
	FText ItemName = FormatVersionLabel(Script->GetExposedVersion());
	VersionListWidget->SelectItemByName(FName(ItemName.ToString()));

	ChildSlot
    [
	 SNew(SBox)
        .MinDesiredWidth(400)
		.Padding(10)
        [
            SNew(SVerticalBox)

            // the top description text
            + SVerticalBox::Slot()
            .AutoHeight()
            [
            	SNew(STextBlock)
            	.AutoWrapText(true)
				.Text(this, &SNiagaraScriptVersionWidget::GetInfoHeaderText)
            ]

            // the main part of the widget
            + SVerticalBox::Slot()
            .FillHeight(1)
            .Padding(FMargin(5, 15))
            .VAlign(VAlign_Fill)
            [
            	// display button to enable versioning or versioning details
	            SNew(SWidgetSwitcher)
		        .WidgetIndex(this, &SNiagaraScriptVersionWidget::GetDetailWidgetIndex)
		        + SWidgetSwitcher::Slot()
		        [
					SNew(SOverlay)

					+ SOverlay::Slot()
					.VAlign(VAlign_Top)
					.HAlign(HAlign_Center)
					[
						// enable versioning button
				        SNew(SButton)
		                .ForegroundColor(FEditorStyle::GetSlateColor("DefaultForeground"))
		                .ButtonStyle(FEditorStyle::Get(), "FlatButton.Dark")
		                .ContentPadding(FMargin(6, 2))
		                .TextStyle(FEditorStyle::Get(), "ContentBrowser.TopBar.Font")
		                .Text(LOCTEXT("NiagaraAEnableVersioning", "Enable versioning"))
		                .OnClicked(this, &SNiagaraScriptVersionWidget::EnableVersioning)
	                ]
		        ]
		        + SWidgetSwitcher::Slot()
		        [
			        SNew(SHorizontalBox)

		            // version selector
		            + SHorizontalBox::Slot()
		            .AutoWidth()
		            [
		                SNew(SVerticalBox)

		                // version list
		                + SVerticalBox::Slot()
		                .FillHeight(1)
		                [
		                    SNew(SBox)
		                    .MaxDesiredWidth(200)
		                    .MinDesiredWidth(200)
		                    [
		                        VersionListWidget.ToSharedRef()
		                    ]
		                ]
		            ]

		            // separator
		            + SHorizontalBox::Slot()
		            .AutoWidth()
		            .Padding(5)
		            [
		                SNew(SSeparator)
		                .Orientation(Orient_Vertical)
		            ]

		            // version details
		            + SHorizontalBox::Slot()
		            [
		                VersionSettingsDetails.ToSharedRef()
		            ]
		        ]
            ]
        ]
    ];
}

void SNiagaraScriptVersionWidget::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	// update script data from details view changes
	FVersionedNiagaraScriptData* ScriptData = Script->GetScriptData(SelectedVersion);
	check(ScriptData);
	
	ScriptData->VersionChangeDescription = VersionMetadata->ChangeDescription;
	ScriptData->UpdateScriptExecution = VersionMetadata->UpdateScriptExecution;
	ScriptData->ScriptAsset = VersionMetadata->ScriptAsset;
	ScriptData->PythonUpdateScript = VersionMetadata->PythonUpdateScript;

	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraVersionMetaData, bIsExposedVersion) && VersionMetadata->bIsExposedVersion)
	{
		ExecuteExposeAction(ScriptData->Version);
	}

	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraVersionMetaData, bIsVisibleInVersionSelector))
	{
		ScriptData->Version.bIsVisibleInVersionSelector = VersionMetadata->bIsVisibleInVersionSelector;
		VersionListWidget->RefreshAllActions(true);
		VersionInListSelected(ScriptData->Version);
	}

	OnVersionDataChanged.ExecuteIfBound();
}

void SNiagaraScriptVersionWidget::SetOnVersionDataChanged(FSimpleDelegate InOnVersionDataChanged)
{
	OnVersionDataChanged = InOnVersionDataChanged;
}

TSharedRef<SExpanderArrow> SNiagaraScriptVersionWidget::CreateCustomActionExpander(const FCustomExpanderData& ActionMenuData)
{
	return SNew(SExpanderArrow, ActionMenuData.TableRow)
        .Visibility(EVisibility::Hidden);
}

TSharedRef<SWidget> SNiagaraScriptVersionWidget::OnGetAddVersionMenu()
{
	FNiagaraAssetVersion LatestVersion = Script->GetAllAvailableVersions().Last();
	FMenuBuilder MenuBuilder(true, nullptr);

	FText Label = FText::Format(LOCTEXT("NiagaraAddMajorVersion", "New major version ({0}.0)"), LatestVersion.MajorVersion + 1);
	FText Tooltip = LOCTEXT("NiagaraAddMajorVersion_Tooltip", "Adds a new major version. This should be used for breaking changes (e.g. adding a new parameter).");
	FUIAction UIAction(FExecuteAction::CreateSP(this, &SNiagaraScriptVersionWidget::AddNewMajorVersion), FCanExecuteAction());
	MenuBuilder.AddMenuEntry(Label, Tooltip, FSlateIcon(), UIAction);

	Label = FText::Format(LOCTEXT("NiagaraAddMinorVersion", "New minor version ({0}.{1})"), LatestVersion.MajorVersion, LatestVersion.MinorVersion + 1);
	Tooltip = LOCTEXT("NiagaraAddMinorVersion_Tooltip", "Adds a new minor version. This should be used for non-breaking changes (e.g. adding a comment to the graph).");
	UIAction = FUIAction(FExecuteAction::CreateSP(this, &SNiagaraScriptVersionWidget::AddNewMinorVersion), FCanExecuteAction());
	MenuBuilder.AddMenuEntry(Label, Tooltip, FSlateIcon(), UIAction);

	return MenuBuilder.MakeWidget();
}

TSharedPtr<SWidget> SNiagaraScriptVersionWidget::OnVersionContextMenuOpening()
{
	TArray<TSharedPtr<FEdGraphSchemaAction>> SelectedActions;
	VersionListWidget->GetSelectedActions(SelectedActions);
	if (SelectedActions.Num() != 1)
	{
		return SNullWidget::NullWidget;
	}
	FNiagaraVersionMenuAction* SelectedAction = (FNiagaraVersionMenuAction*) SelectedActions[0].Get();
	
	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.BeginSection("Edit", LOCTEXT("EditMenuHeader", "Edit"));
	{
		MenuBuilder.AddMenuEntry(LOCTEXT("ExposeVersion", "Expose version"), LOCTEXT("ExposeVersion_Tooltip", "Exposing this version will make it the default version for any new assets."), FSlateIcon(),
            FUIAction(
                FExecuteAction::CreateSP(this, &SNiagaraScriptVersionWidget::ExecuteExposeAction, SelectedAction->AssetVersion),
                FCanExecuteAction::CreateSP(this, &SNiagaraScriptVersionWidget::CanExecuteExposeAction, SelectedAction->AssetVersion)
            ));

		MenuBuilder.AddSeparator();
		
		MenuBuilder.AddMenuEntry(LOCTEXT("DeleteVersion", "Delete version"), LOCTEXT("DeleteVersion_Tooltip", "Deletes this version and all associated data. This will break existing usages of that version!"), FSlateIcon(),
            FUIAction(
                FExecuteAction::CreateSP(this, &SNiagaraScriptVersionWidget::ExecuteDeleteAction, SelectedAction->AssetVersion),
                FCanExecuteAction::CreateSP(this, &SNiagaraScriptVersionWidget::CanExecuteDeleteAction, SelectedAction->AssetVersion)
            ));
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

int32 SNiagaraScriptVersionWidget::GetDetailWidgetIndex() const
{
	return Script->IsVersioningEnabled() ? 1 : 0;
}

FReply SNiagaraScriptVersionWidget::EnableVersioning()
{
	Script->EnableVersioning();
	OnVersionDataChanged.ExecuteIfBound();
	
	return FReply::Handled();
}

bool SNiagaraScriptVersionWidget::CanExecuteDeleteAction(FNiagaraAssetVersion AssetVersion)
{
	return AssetVersion != Script->GetExposedVersion() && (AssetVersion.MajorVersion != 1 || AssetVersion.MinorVersion != 0);
}

bool SNiagaraScriptVersionWidget::CanExecuteExposeAction(FNiagaraAssetVersion AssetVersion)
{
	return AssetVersion != Script->GetExposedVersion();
}

void SNiagaraScriptVersionWidget::ExecuteDeleteAction(FNiagaraAssetVersion AssetVersion)
{
	OnChangeToVersion.ExecuteIfBound(Script->GetExposedVersion().VersionGuid);
	Script->DeleteVersion(AssetVersion.VersionGuid);

	VersionListWidget->RefreshAllActions(true);
	VersionInListSelected(Script->GetExposedVersion());
	OnVersionDataChanged.ExecuteIfBound();
}

void SNiagaraScriptVersionWidget::ExecuteExposeAction(FNiagaraAssetVersion AssetVersion)
{
	Script->ExposeVersion(AssetVersion.VersionGuid);

	VersionListWidget->RefreshAllActions(true);
	VersionInListSelected(AssetVersion);
	OnVersionDataChanged.ExecuteIfBound();
}

TSharedRef<ITableRow> SNiagaraScriptVersionWidget::HandleVersionViewGenerateRow(TSharedRef<FNiagaraAssetVersion> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return
        SNew(STableRow<TSharedRef<FText>>, OwnerTable)
        [
            SNew(STextBlock)
            .Text(FormatVersionLabel(Item.Get()))
        ];
}

void SNiagaraScriptVersionWidget::OnActionSelected(const TArray<TSharedPtr<FEdGraphSchemaAction>>& SelectedActions,	ESelectInfo::Type)
{
	for (TSharedPtr<FEdGraphSchemaAction> Action : SelectedActions)
	{
		FNiagaraMenuAction* SelectedAction = (FNiagaraMenuAction*) Action.Get();
		if (SelectedAction)
		{
			SelectedAction->ExecuteAction();
		}
	}
}

TSharedRef<SWidget> SNiagaraScriptVersionWidget::GetVersionSelectionHeaderWidget(TSharedRef<SWidget>, int32)
{
	// creates the add version button
	return SNew(SComboButton)
        .ButtonStyle(FEditorStyle::Get(), "RoundButton")
        .ForegroundColor(FEditorStyle::GetSlateColor("DefaultForeground"))
        .ContentPadding(FMargin(2, 0))
        .OnGetMenuContent(this, &SNiagaraScriptVersionWidget::OnGetAddVersionMenu)
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Center)
        .HasDownArrow(false)
        .ButtonContent()
        [
            SNew(SHorizontalBox)

            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(FMargin(0, 1))
            [
                SNew(SImage)
                .Image(FEditorStyle::GetBrush("Plus"))
            ]

            + SHorizontalBox::Slot()
            .VAlign(VAlign_Center)
            .AutoWidth()
            .Padding(FMargin(2,0,0,0))
            [
                SNew(STextBlock)
                .Font(IDetailLayoutBuilder::GetDetailFontBold())
                .Text(LOCTEXT("NiagaraVersionManagementAdd", "Add version"))
                .ShadowOffset(FVector2D(1,1))
            ]
        ];
}

void SNiagaraScriptVersionWidget::CollectAllVersionActions(FGraphActionListBuilderBase& OutAllActions)
{
	TArray<FNiagaraAssetVersion> AssetVersions = Script->GetAllAvailableVersions();
	for (FNiagaraAssetVersion& Version : AssetVersions)
	{
		FText MenuDesc = FormatVersionLabel(Version);
		FText Tooltip;
		auto ExecAction = FNiagaraMenuAction::FOnExecuteStackAction::CreateSP(this, &SNiagaraScriptVersionWidget::VersionInListSelected, Version);
		TSharedPtr<FNiagaraMenuAction> Action = MakeShared<FNiagaraVersionMenuAction>(FText(), MenuDesc, Tooltip, 0, FText(), ExecAction, 1, Version);
		OutAllActions.AddAction(Action);
	}
}

void SNiagaraScriptVersionWidget::VersionInListSelected(FNiagaraAssetVersion InSelectedVersion)
{
	SelectedVersion = InSelectedVersion.VersionGuid;
	FVersionedNiagaraScriptData* ScriptData = Script->GetScriptData(SelectedVersion);
	check(ScriptData);
	
	VersionMetadata->VersionGuid = InSelectedVersion.VersionGuid;
	VersionMetadata->ChangeDescription = ScriptData->VersionChangeDescription;
	VersionMetadata->bIsExposedVersion = InSelectedVersion == Script->GetExposedVersion();
	VersionMetadata->bIsVisibleInVersionSelector = InSelectedVersion.bIsVisibleInVersionSelector;

	VersionMetadata->UpdateScriptExecution = ScriptData->UpdateScriptExecution;
	VersionMetadata->ScriptAsset = ScriptData->ScriptAsset;
	VersionMetadata->PythonUpdateScript = ScriptData->PythonUpdateScript;

	VersionSettingsDetails->SetObject(VersionMetadata, true);
}

void SNiagaraScriptVersionWidget::AddNewMajorVersion()
{
	FNiagaraAssetVersion LatestVersion = Script->GetAllAvailableVersions().Last();
	FGuid NewVersion = Script->AddNewVersion(LatestVersion.MajorVersion + 1, 0);

	VersionListWidget->RefreshAllActions(true);
	OnVersionDataChanged.ExecuteIfBound();
	OnChangeToVersion.ExecuteIfBound(NewVersion);
}

void SNiagaraScriptVersionWidget::AddNewMinorVersion()
{
	FNiagaraAssetVersion LatestVersion = Script->GetAllAvailableVersions().Last();
	FGuid NewVersion = Script->AddNewVersion(LatestVersion.MajorVersion, LatestVersion.MinorVersion + 1);

	VersionListWidget->RefreshAllActions(true);
	OnVersionDataChanged.ExecuteIfBound();
	OnChangeToVersion.ExecuteIfBound(NewVersion);
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

#undef LOCTEXT_NAMESPACE
