// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SPresetManager.h"
#include "SlateOptMacros.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Styling/AppStyle.h"
#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Editor/TransBuffer.h"
#include "PresetSettings.h"
#include "Misc/ScopedSlowTask.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Templates/SharedPointer.h"
#include "Widgets/SWidget.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "UObject/SavePackage.h"
#include "ObjectTools.h"
#include "PresetAsset.h"
#include "SNegativeActionButton.h"
#include "SPositiveActionButton.h"
#include "SSimpleButton.h"
#include "PresetEditorStyle.h"
#include "PresetAssetSubsystem.h"
#include "Framework/Commands/GenericCommands.h"
#include "Modules/ModuleManager.h"
#include "ISettingsModule.h"
#include "IAssetTools.h"

#define LOCTEXT_NAMESPACE "SPresetManager"

DECLARE_DELEGATE_TwoParams(FOnCollectionEnabledCheckboxChanged, TSharedPtr<SPresetManager::FPresetViewEntry>, ECheckBoxState)
DECLARE_DELEGATE_TwoParams(FOnPresetLabelChanged, TSharedPtr<SPresetManager::FPresetViewEntry>, FText)
DECLARE_DELEGATE_TwoParams(FOnPresetTooltipChanged, TSharedPtr<SPresetManager::FPresetViewEntry>, FText)
DECLARE_DELEGATE_OneParam(FOnPresetDeleted, TSharedPtr<SPresetManager::FPresetViewEntry>)
DECLARE_DELEGATE_TwoParams(FOnCollectionRenameStarted, TSharedPtr<SPresetManager::FPresetViewEntry>, TSharedPtr<SEditableTextBox> RenameWidget)
DECLARE_DELEGATE_TwoParams(FOnCollectionRenameEnded, TSharedPtr<SPresetManager::FPresetViewEntry>, const FText& NewText)

namespace PresetManagerLocals
{
	template<typename ASSETCLASS>
	void GetObjectsOfClass(TArray<FSoftObjectPath>& OutArray)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> AssetData;

		FARFilter Filter;
		Filter.ClassPaths.Add(ASSETCLASS::StaticClass()->GetClassPathName());
		Filter.PackagePaths.Add(FName("/ToolPresets"));
		Filter.bRecursiveClasses = false;	
		Filter.bRecursivePaths = true;
		Filter.bIncludeOnlyOnDiskAssets = false;
		
		AssetRegistryModule.Get().GetAssets(Filter, AssetData);

		for (int i = 0; i < AssetData.Num(); i++)
		{
			ASSETCLASS* Object = Cast<ASSETCLASS>(AssetData[i].GetAsset());		
			if (Object)
			{				
				OutArray.Add(Object->GetPathName());
			}
		}
	}

	template<typename ItemType> class SCollectionTableRow;

	template<typename ItemType>
	class SCollectionTableRow : public STableRow< ItemType>
	{
		typedef SCollectionTableRow< ItemType > FSuperRowType;
		typedef typename STableRow<ItemType>::FArguments FTableRowArgs;
		typedef SPresetManager::FPresetViewEntry::EEntryType EEntryType;

	public:

		SLATE_BEGIN_ARGS(SCollectionTableRow) { }
		    SLATE_ARGUMENT(TSharedPtr<SPresetManager::FPresetViewEntry>, ViewEntry)
			SLATE_EVENT(FOnCollectionEnabledCheckboxChanged, OnCollectionEnabledCheckboxChanged)
			SLATE_EVENT(FOnCollectionRenameStarted, OnCollectionRenameStarted)
			SLATE_EVENT(FOnCollectionRenameEnded, OnCollectionRenameEnded)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
		{
			FTableRowArgs Args = FTableRowArgs()
				.Style(&FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row"))
				.ExpanderStyleSet(& FCoreStyle::Get());

			ViewEntry = InArgs._ViewEntry;
			OnCollectionEnabledCheckboxChanged = InArgs._OnCollectionEnabledCheckboxChanged;
			OnCollectionRenameStarted = InArgs._OnCollectionRenameStarted;
			OnCollectionRenameEnded = InArgs._OnCollectionRenameEnded;

			STableRow<ItemType>::Construct(Args, InOwnerTableView);
		}

		BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
		virtual void ConstructChildren(ETableViewMode::Type InOwnerTableMode, const TAttribute<FMargin>& InPadding, const TSharedRef<SWidget>& InContent) override
		{
			STableRow<ItemType>::Content = InContent;

			TSharedPtr<class ITableRow> ThisTableRow = this->SharedThis(this);

			if(InOwnerTableMode == ETableViewMode::Tree)
			{

				// Rows in a TreeView need an expander button and some indentation
				this->ChildSlot
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Fill)
					[
						SAssignNew(EnabledWidget, SCheckBox)
						.Visibility_Lambda([this]()
						{
							return ViewEntry->EntryType == EEntryType::Collection ? EVisibility::Visible : EVisibility::Collapsed;
						})
						.IsChecked_Lambda([this]()
						{
							return ViewEntry->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						})
						.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
						{
							if (OnCollectionEnabledCheckboxChanged.IsBound())
							{
								OnCollectionEnabledCheckboxChanged.Execute(ViewEntry, State);
							}
						})
				
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Fill)
					[
						SAssignNew(STableRow< ItemType>::ExpanderArrowWidget, SExpanderArrow, ThisTableRow)
						.StyleSet(STableRow< ItemType>::ExpanderStyleSet)
						.ShouldDrawWires(false)
					]

					+ SHorizontalBox::Slot()
						.AutoWidth()						
						.Padding(5.0f)
						[
							SNew(SImage)
							.Visibility_Lambda([this]()
							{
								return ViewEntry->EntryType == EEntryType::Tool ? EVisibility::Visible : EVisibility::Collapsed;
							})
							.Image(&ViewEntry->EntryIcon)
						]

					+ SHorizontalBox::Slot()
						.FillWidth(1)
						.Padding(5.0f)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							[
								SNew(STextBlock)
								.Text(ViewEntry->EntryLabel)
								.Visibility_Lambda([this]()
								{
									return ViewEntry->bIsRenaming ? EVisibility::Collapsed : EVisibility::Visible;
								})							
								.Font_Lambda([this]()
								{
									return FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText").Font;							
								})
							]

							+ SHorizontalBox::Slot()
							[
								SAssignNew(CollectionRenameBox, SEditableTextBox)
								.Text(ViewEntry->EntryLabel)
								
								.Visibility_Lambda([this]()
								{
									if (!bIsEntryBeingRenamed && ViewEntry->bIsRenaming)
									{
										OnCollectionRenameStarted.ExecuteIfBound(ViewEntry, CollectionRenameBox);
									}
									bIsEntryBeingRenamed = ViewEntry->bIsRenaming;
									return ViewEntry->bIsRenaming ? EVisibility::Visible : EVisibility::Collapsed;
								})
								.OnTextCommitted_Lambda([this](const FText & NewText, ETextCommit::Type CommitStatus)
								{
									OnCollectionRenameEnded.ExecuteIfBound(ViewEntry, NewText);
								})
								.Font_Lambda([this]()
								{
									return FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText").Font;							
								})
							]
						]


					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SSpacer)
					
					]

					+ SHorizontalBox::Slot()
						.AutoWidth()
						.HAlign(EHorizontalAlignment::HAlign_Right)
						.Padding(5.0f)
						[
							SNew(STextBlock)
							.Text(TAttribute<FText>::CreateLambda([this]()
							{
								return FText::AsNumber(ViewEntry->Count);
							}))
						]
				];
			}
		}
		END_SLATE_FUNCTION_BUILD_OPTIMIZATION

	private:
		TSharedPtr<SPresetManager::FPresetViewEntry> ViewEntry;
		TSharedPtr<SCheckBox> EnabledWidget;
		TSharedPtr<SEditableTextBox> CollectionRenameBox;

		bool bIsEntryBeingRenamed = false;

		FOnCollectionEnabledCheckboxChanged OnCollectionEnabledCheckboxChanged;
		FOnCollectionRenameStarted OnCollectionRenameStarted;
		FOnCollectionRenameEnded OnCollectionRenameEnded;
	};



	template<typename ItemType>
	class SPresetTableRow : public SMultiColumnTableRow< ItemType>
	{
		typedef SPresetManager::FPresetViewEntry::EEntryType EEntryType;
		using typename SMultiColumnTableRow< ItemType>::FSuperRowType;

	public:

		SLATE_BEGIN_ARGS(SPresetTableRow) { }
		SLATE_ARGUMENT(TSharedPtr<SPresetManager::FPresetViewEntry>, ViewEntry)
		SLATE_EVENT(FOnPresetDeleted, OnPresetDeleted)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
		{
			typename FSuperRowType::FArguments Args = typename FSuperRowType::FArguments()
				.ExpanderStyleSet(&FCoreStyle::Get());

			OnPresetDeleted = InArgs._OnPresetDeleted;
			ViewEntry = InArgs._ViewEntry;

			SMultiColumnTableRow<ItemType>::Construct(Args, InOwnerTableView);
		}

		BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
		virtual void ConstructChildren(ETableViewMode::Type InOwnerTableMode, const TAttribute<FMargin>& InPadding, const TSharedRef<SWidget>& InContent) override
		{
			STableRow<ItemType>::Content = InContent;

			TSharedPtr<class ITableRow> ThisTableRow = this->SharedThis(this);

			if (ViewEntry->EntryType == EEntryType::Tool)
			{
				this->ChildSlot				
				[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(5.0f)
					[
						SNew(SImage)
						.Visibility_Lambda([this]()
						{
							return ViewEntry->EntryType == EEntryType::Tool ? EVisibility::Visible : EVisibility::Collapsed;
						})
						.Image(&ViewEntry->EntryIcon)
					]

				+ SHorizontalBox::Slot()
					.FillWidth(1)
					.Padding(5.0f)
					[
						SNew(STextBlock)
						.Text(ViewEntry->EntryLabel)
					]

				+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SSpacer)

					]

				+ SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(EHorizontalAlignment::HAlign_Right)
					.Padding(5.0f)
					[
						SNew(STextBlock)
						.Text(TAttribute<FText>::CreateLambda([this]()
						{
							return FText::AsNumber(ViewEntry->Count);
						}))
					]
				];
			}
			else
			{
				ensure(ViewEntry->EntryType == EEntryType::Preset);

				this->ChildSlot
				.Padding(InPadding)
				[
					InContent
				];
			}
			
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName)
		{
			if (InColumnName == "Label")
			{
				return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.Padding(FMargin(4.0f, 0.0f))
					[
						SNew(STextBlock)
						.Text(TAttribute<FText>::CreateLambda([this]()
						{
							return FText::FromString(ViewEntry->PresetLabel);
						}))
					];
			}
			else if (InColumnName == "Tooltip")
			{
				return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.Padding(FMargin(4.0f, 0.0f))
					[
						SNew(STextBlock)
						.Text(TAttribute<FText>::CreateLambda([this]()
						{
							return FText::FromString(ViewEntry->PresetTooltip);
						}))
					];
			}
			else if (InColumnName == "Tool")
			{
				return SNew(SBox)
					.VAlign(VAlign_Center)
					.Padding(FMargin(5.0f, 5.0f))
					[
						SNew(SImage)
						.Image(&ViewEntry->EntryIcon)
						.DesiredSizeOverride(FVector2D(16, 16))
					];
			}
			else if (InColumnName == "Delete")
			{
				return SNew(SNegativeActionButton)
					.Icon(FAppStyle::GetBrush("Icons.Delete"))						
					.OnClicked_Lambda([this]()
					{
						OnPresetDeleted.ExecuteIfBound(ViewEntry); return FReply::Handled();
					})
					.Visibility_Lambda([this]()
					{
						return this->IsHovered() ? EVisibility::Visible : EVisibility::Hidden;
					});
			}

			return SNullWidget::NullWidget;

		}
		END_SLATE_FUNCTION_BUILD_OPTIMIZATION

	private:
		TSharedPtr<SPresetManager::FPresetViewEntry> ViewEntry;
		FOnPresetDeleted                     OnPresetDeleted;
	};

}



/* SPresetManager interface
 *****************************************************************************/

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SPresetManager::Construct( const FArguments& InArgs )
{
	UPresetUserSettings::Initialize();
	BindCommands();

	UserSettings = UPresetUserSettings::Get();
	if (UserSettings.IsValid())
	{
		UserSettings->LoadEditorConfig();
	}

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.FillHeight(1.0)
		//.AutoHeight()
		[
				SAssignNew(Splitter, SSplitter)
					.Orientation(EOrientation::Orient_Horizontal)
					
					+ SSplitter::Slot()
					.Value(0.4f)
					.Resizable(false)
						[
							SNew(SVerticalBox)		

								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									SNew(SBorder)
									.BorderImage(&FAppStyle::Get().GetWidgetStyle<FTableColumnHeaderStyle>("TableView.Header.Column").NormalBrush)
									[
										SNew(SHorizontalBox)
									
										+ SHorizontalBox::Slot()
										.AutoWidth()
										.HAlign(HAlign_Right)
										.VAlign(VAlign_Fill)
										[
											SAssignNew(UserCollectionsExpander, SButton)
											.ButtonStyle(FCoreStyle::Get(), "NoBorder" )
											.VAlign(VAlign_Center)
											.HAlign(HAlign_Center)
											.ClickMethod( EButtonClickMethod::MouseDown )
											.OnClicked_Lambda([this]()
											{
												bAreUserCollectionsExpanded = !bAreUserCollectionsExpanded; return FReply::Handled();
											})
											.ContentPadding(0.f)
											.ForegroundColor( FSlateColor::UseForeground() )
											.IsFocusable( false )
											[
												SNew(SImage)
												.Image( this, &SPresetManager::GetUserCollectionsExpanderImage )
												.ColorAndOpacity( FSlateColor::UseSubduedForeground() )
											]
										]
									

										+ SHorizontalBox::Slot()
										.FillWidth(1.0)
										.Padding(5.0f, 5.0f, 5.0f, 5.0f)
										.HAlign(HAlign_Left)
										.VAlign(VAlign_Center)
										[
											SNew(STextBlock)										
											.Text(LOCTEXT("UserPresetLabels", "User Preset Collections"))
											.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 13, "Bold"))
										]

										+ SHorizontalBox::Slot()
										.AutoWidth()
										.HAlign(HAlign_Right)
										.VAlign(VAlign_Center)
										.Padding(16.0f, 8.0f, 8.0f, 8.0f)
										[
											SAssignNew(AddUserPresetButton, SPositiveActionButton)
											.ToolTipText(LOCTEXT("AddUserPresetCollection", "Add User Preset Collection"))
											.Icon(FAppStyle::GetBrush("Icons.Plus"))
											.OnClicked_Lambda([this]()
											{
												AddNewUserPresetCollection(); return FReply::Handled();
											})
										]
									]
								]

								+ SVerticalBox::Slot()
								.AutoHeight()
								[
			
											SAssignNew(EditorPresetCollectionTreeView, STreeView<TSharedPtr<FPresetViewEntry> >)
												.TreeItemsSource(&EditorCollectionsDataList)
												.SelectionMode(ESelectionMode::Single)
												.OnGenerateRow(this, &SPresetManager::HandleTreeGenerateRow)
												.OnGetChildren(this, &SPresetManager::HandleTreeGetChildren)
												.OnSelectionChanged(this, &SPresetManager::HandleEditorTreeSelectionChanged)
												.Visibility_Lambda([this]()
												{
													return bAreUserCollectionsExpanded ? EVisibility::Visible : EVisibility::Collapsed;
												})
												.HeaderRow
												(
													SNew(SHeaderRow)
													.Visibility(EVisibility::Collapsed)

													+ SHeaderRow::Column("Collection")
													.FixedWidth(150.0f)
													.HeaderContent()
													[
														SNew(STextBlock)
														.Text(LOCTEXT("PresetManagerCollectionTitleHeader", "Collection"))
													]


												)											
								]

								+ SVerticalBox::Slot()
									.FillHeight(1.0f)
								[
			
											SAssignNew(UserPresetCollectionTreeView, STreeView<TSharedPtr<FPresetViewEntry> >)
												.TreeItemsSource(&UserCollectionsDataList)
												.SelectionMode(ESelectionMode::Single)
												.OnGenerateRow(this, &SPresetManager::HandleTreeGenerateRow)
												.OnGetChildren(this, &SPresetManager::HandleTreeGetChildren)
												.OnSelectionChanged(this, &SPresetManager::HandleUserTreeSelectionChanged)
												.OnContextMenuOpening(this, &SPresetManager::OnGetCollectionContextMenuContent)
												.Visibility_Lambda([this]()
												{
													return bAreUserCollectionsExpanded ? EVisibility::Visible : EVisibility::Collapsed;
												})
												.HeaderRow
												(
													SNew(SHeaderRow)
													.Visibility(EVisibility::Collapsed)

													+ SHeaderRow::Column("Collection")
													.FixedWidth(150.0f)
													.HeaderContent()
													[
														SNew(STextBlock)
														.Text(LOCTEXT("PresetManagerCollectionTitleHeader", "Collection"))
													]


												)											
								]

								+ SVerticalBox::Slot()
									.AutoHeight()									
								[
									SNew(SBorder)
									.BorderImage(&FAppStyle::Get().GetWidgetStyle<FTableColumnHeaderStyle>("TableView.Header.Column").NormalBrush)
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot()
										.AutoWidth()
										.HAlign(HAlign_Right)
										.VAlign(VAlign_Fill)
										[
											SAssignNew(ProjectCollectionsExpander, SButton)
											.ButtonStyle(FCoreStyle::Get(), "NoBorder" )
											.VAlign(VAlign_Center)
											.HAlign(HAlign_Center)
											.ClickMethod( EButtonClickMethod::MouseDown )
											.OnClicked_Lambda([this]()
											{
												bAreProjectCollectionsExpanded = !bAreProjectCollectionsExpanded; return FReply::Handled();
											})
											.ContentPadding(0.f)
											.ForegroundColor( FSlateColor::UseForeground() )
											.IsFocusable( false )
											[
												SNew(SImage)
												.Image( this, &SPresetManager::GetProjectCollectionsExpanderImage )
												.ColorAndOpacity( FSlateColor::UseSubduedForeground() )
											]
										]

										+ SHorizontalBox::Slot()
										.FillWidth(1.0)
										.Padding(5.0f, 5.0f, 5.0f, 5.0f)
										.HAlign(HAlign_Left)
										.VAlign(VAlign_Center)
										[
											SNew(STextBlock)
											.Text(LOCTEXT("ProjectPresetLabels", "Project Preset Collections"))
											.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 13, "Bold"))
										]

										+ SHorizontalBox::Slot()
										.AutoWidth()
										.HAlign(HAlign_Right)
										.VAlign(VAlign_Center)
										.Padding(16.0f, 8.0f, 8.0f, 8.0f)
										[
											SNew(SSimpleButton)
											.ToolTipText(LOCTEXT("OpenProjectSettingsPresets", "Open Project Settings for Presets"))
											.Icon(FAppStyle::GetBrush("Icons.Settings"))
											.OnClicked_Lambda([this]() 
											{ 
												if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
												{
													SettingsModule->ShowViewer("Project", "Plugins", "Interactive Tool Presets");
												}
												return FReply::Handled();
											})
										]
									]
								]

								+ SVerticalBox::Slot()
									.FillHeight(1.0f)
								[
										
											SNew(SVerticalBox)
											.Visibility_Lambda([this]()
											{
												return bAreProjectCollectionsExpanded ? EVisibility::Visible : EVisibility::Collapsed;
											})

											+ SVerticalBox::Slot()
											.FillHeight(1.0f)
											[

											SAssignNew(ProjectPresetCollectionTreeView, STreeView<TSharedPtr<FPresetViewEntry> >)
											.Visibility(this, &SPresetManager::ProjectPresetCollectionsVisibility)
												.ItemHeight(32.0f)
												.TreeItemsSource(&ProjectCollectionsDataList)
												.SelectionMode(ESelectionMode::Single)
												.OnGenerateRow(this, &SPresetManager::HandleTreeGenerateRow)
												.OnGetChildren(this, &SPresetManager::HandleTreeGetChildren)
												.OnSelectionChanged(this, &SPresetManager::HandleTreeSelectionChanged)
												.OnContextMenuOpening(this, &SPresetManager::OnGetCollectionContextMenuContent)
												.HeaderRow
												(
													SNew(SHeaderRow)
													.Visibility(EVisibility::Collapsed)

													+ SHeaderRow::Column("Collection")
													.FixedWidth(150.0f)
													.HeaderContent()
													[
														SNew(STextBlock)
														.Text(LOCTEXT("PresetManagerCollectionTitleHeader", "Collection"))
													]


												)											
											]

											+ SVerticalBox::Slot()
											.AutoHeight()
											.HAlign(HAlign_Center)
											.Padding(5.0f)
											[
												SNew(STextBlock)
												.WrapTextAt(150.0f)												
												.Visibility_Lambda([this]()
												{
													return ProjectPresetCollectionsVisibility() == EVisibility::Visible ? EVisibility::Collapsed : EVisibility::Visible;
												})
												.Text(LOCTEXT("ProjectPresetsNotLoadedLabel", "Manage Project Preset Collections in Project Settings"))
												.Justification(ETextJustify::Center)
												.Font(FAppStyle::GetFontStyle("NormalFontItalic"))
											]


								]

						]

						+ SSplitter::Slot()
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.FillHeight(1.0)
							[
								SNew(SOverlay)
								+ SOverlay::Slot()
								.ZOrder(1)
							[


								SAssignNew(PresetListView, SListView<TSharedPtr<FPresetViewEntry>>)
								.ListItemsSource(&PresetDataList)
								.ItemHeight(32.0f)
								.SelectionMode(ESelectionMode::SingleToggle)
								.OnGenerateRow(this, &SPresetManager::HandleListGenerateRow)		
								.OnSelectionChanged(this, &SPresetManager::HandleListSelectionChanged)
								.OnContextMenuOpening(this, &SPresetManager::OnGetPresetContextMenuContent)
								.HeaderRow
								(
									SNew(SHeaderRow)
									.Visibility(EVisibility::Visible)

									+ SHeaderRow::Column("Tool")
									.FixedWidth(30.0f)
									.HeaderContentPadding(FMargin(5.0f, 5.0f))
									.HAlignHeader(EHorizontalAlignment::HAlign_Center)
									.VAlignHeader(EVerticalAlignment::VAlign_Center)
									.HAlignCell(EHorizontalAlignment::HAlign_Center)
									.HeaderContent()									
									[
										SNew(SImage)
										.Image(FPresetEditorStyle::Get()->GetBrush("ManagerIcons.Tools"))
										.DesiredSizeOverride(FVector2D(20, 20))
									]
									
									+ SHeaderRow::Column("Label")
									.FillWidth(80.0f)
									.HeaderContentPadding(FMargin(5.0f, 5.0f))
									.HAlignHeader(EHorizontalAlignment::HAlign_Left)
									.VAlignHeader(EVerticalAlignment::VAlign_Center)
									.HeaderContent()
									[
										SNew(STextBlock)
										.Text(LOCTEXT("PresetManagerPresetLabelHeader", "Label"))
									]

									+ SHeaderRow::Column("Tooltip")
									.FillWidth(80.0f)
									.HAlignHeader(EHorizontalAlignment::HAlign_Left)
									.VAlignHeader(EVerticalAlignment::VAlign_Center)
									.HeaderContent()
									[
										SNew(STextBlock)
										.Text(LOCTEXT("PresetManagerPresetTooltipHeader", "Tooltip"))
									]
								)
							]
							
							+ SOverlay::Slot()
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							.ZOrder(2)
								[
									SNew(STextBlock)
									.WrapTextAt(150.0f)
									.Visibility_Lambda([this]()
									{
										return bHasPresetsInCollection ? EVisibility::Collapsed : EVisibility::Visible;
									})
									.Text(LOCTEXT("NoPresetsAvailableLabel", "Add New Presets from any Modeling Tool"))
									.Justification(ETextJustify::Center)
									.Font(FAppStyle::GetFontStyle("NormalFontItalic"))
								]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(5.0f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(5.0f)
							[
								SNew(SHorizontalBox)								
								+ SHorizontalBox::Slot()
							    .FillWidth(1.f)
								.Padding(5.0f)
								.HAlign(EHorizontalAlignment::HAlign_Left)	
								[
									SNew(STextBlock)
									.Text(LOCTEXT("PresetLabelEditLabel", "Label"))
								]
								+ SHorizontalBox::Slot()
								.FillWidth(1.f)								
								.Padding(5.0f)
								[
									SNew(SEditableTextBox)
									.IsEnabled(this, &SPresetManager::EditAreaEnabled)
									.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
									.Text_Lambda([this]() 
									{
										if (ActivePresetToEdit)
										{
											return FText::FromString(ActivePresetToEdit->PresetLabel);
										}
										return FText::GetEmpty();
									})
									.OnTextChanged_Lambda([this](const FText& NewText)
									{
										if (ActivePresetToEdit)
										{
											// Cap the number of characters sent out of the text box, so we don't overflow menus and tooltips
											ActivePresetToEdit->PresetLabel = NewText.ToString().Left(255);
										}
									})
									.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type CommitStatus)
									{
										if (ActivePresetToEdit)
										{
											// Cap the number of characters sent out of the text box, so we don't overflow menus and tooltips
											SetPresetLabel(ActivePresetToEdit, FText::FromString(NewText.ToString().Left(255)));
										}
									})
								]
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(5.0f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.FillWidth(1.f)
								.Padding(5.0f)
								.HAlign(EHorizontalAlignment::HAlign_Left)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("PresetTooltipEditLabel", "Tooltip"))
								]
								+ SHorizontalBox::Slot()
								.FillWidth(1.f)
								.Padding(5.0f)																
								[
									SNew(SBox)
									.MinDesiredHeight(44.f)
									.MaxDesiredHeight(44.0f)
									[
										SNew(SMultiLineEditableTextBox)		
										.IsEnabled(this, &SPresetManager::EditAreaEnabled)
										.AllowMultiLine(false)
										.AutoWrapText(true)
										.WrappingPolicy(ETextWrappingPolicy::DefaultWrapping)
										.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
										.Text_Lambda([this]()
										{
											if (ActivePresetToEdit)
											{
												return FText::FromString(ActivePresetToEdit->PresetTooltip);
											}
											return FText::GetEmpty();
										})
										.OnTextChanged_Lambda([this](const FText& NewText) 
										{
											if (ActivePresetToEdit)
											{
												// Cap the number of characters sent out of the text box, so we don't overflow menus and tooltips
												ActivePresetToEdit->PresetTooltip = NewText.ToString().Left(2048);
											}
										})
										.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type CommitStatus)
										{
											if (ActivePresetToEdit)
											{
												// Cap the number of characters sent out of the text box, so we don't overflow menus and tooltips
												SetPresetTooltip(ActivePresetToEdit, FText::FromString(NewText.ToString().Left(2048)));
											}
										})
									]
								]
							]
						]						
					]
				]
		];

		RegeneratePresetTrees();
		if (UserCollectionsDataList.Num() == 0)
		{
			bAreUserCollectionsExpanded = false;
		}
		if (ProjectCollectionsDataList.Num() == 0)
		{
			bAreProjectCollectionsExpanded = false;
		}

}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

SPresetManager::~SPresetManager()
{
}

void SPresetManager::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	RegeneratePresetTrees();
}

void SPresetManager::RegeneratePresetTrees()
{
	if (!ensure(UserSettings.IsValid()))
	{
		return;
	}

	const UPresetProjectSettings* ProjectSettings = GetDefault<UPresetProjectSettings>();
	TArray<FSoftObjectPath> AvailablePresetCollections = ProjectSettings->LoadedPresetCollections.Array();
	TArray<FSoftObjectPath> AvailableUserPresetCollections;
	PresetManagerLocals::GetObjectsOfClass<UInteractiveToolsPresetCollectionAsset>(AvailableUserPresetCollections);

	TotalPresetCount = 0;

	auto GenerateSubTree = [this](UInteractiveToolsPresetCollectionAsset* PresetCollection, TSharedPtr<FPresetViewEntry> RootEntry)
	{
		TMap<FString, FInteractiveToolPresetStore >::TIterator ToolNameIter = PresetCollection->PerToolPresets.CreateIterator();
		for (; (bool)ToolNameIter; ++ToolNameIter)
		{
			int32 ToolCount = 0;
			for (int32 PresetIndex = 0; PresetIndex < ToolNameIter.Value().NamedPresets.Num(); ++PresetIndex)
			{
				ToolCount += ToolNameIter.Value().NamedPresets[PresetIndex].IsValid() ? 1 : 0;
			}
			if (ToolCount)
			{
				RootEntry->Children.Add(MakeShared<FPresetViewEntry>(
					ToolNameIter.Value().ToolLabel,
					ToolNameIter.Value().ToolIcon,
					RootEntry->CollectionPath,
					ToolNameIter.Key(),
					ToolCount));
				RootEntry->Children.Last()->Parent = RootEntry;
				RootEntry->Count += ToolCount;
				TotalPresetCount += ToolCount;
			}
		}

	};

	auto GenerateTreeEntries = [this, &GenerateSubTree](TObjectPtr<UInteractiveToolsPresetCollectionAsset> DefaultCollection,
		TArray<FSoftObjectPath>* AssetList,
		TArray< TSharedPtr< FPresetViewEntry > >& TreeList,
		TSharedPtr<STreeView<TSharedPtr<FPresetViewEntry> > >& TreeView)
	{
		bool bTreeNeedsRefresh = false;
		TArray< TSharedPtr< FPresetViewEntry > > TempTreeDataList;

		if (DefaultCollection)
		{
			TSharedPtr<FPresetViewEntry> CollectionEntry = MakeShared<FPresetViewEntry>(
				UserSettings->bDefaultCollectionEnabled,
				FSoftObjectPath(),
				DefaultCollection->CollectionLabel,
				0);
			CollectionEntry->bIsDefaultCollection = true;
			GenerateSubTree(DefaultCollection, CollectionEntry);
			TempTreeDataList.Add(CollectionEntry);
		}

		if (AssetList)
		{
			AssetList->RemoveAll([](const FSoftObjectPath& Path)
			{
				return !Path.IsAsset();
			});

			for (const FSoftObjectPath& Path : *AssetList)
			{
				UInteractiveToolsPresetCollectionAsset* PresetCollection = nullptr;

				if (Path.IsAsset())
				{
					PresetCollection = Cast<UInteractiveToolsPresetCollectionAsset>(Path.TryLoad());
				}
				if (PresetCollection)
				{
					TSharedPtr<FPresetViewEntry> CollectionEntry = MakeShared<FPresetViewEntry>(
						UserSettings->EnabledPresetCollections.Contains(Path),
						Path,
						PresetCollection->CollectionLabel,
						0);
					GenerateSubTree(PresetCollection, CollectionEntry);
					TempTreeDataList.Add(CollectionEntry);
				}
			}
		}

		if (TempTreeDataList.Num() != TreeList.Num())
		{
			bTreeNeedsRefresh = true;
		}
		else
		{
			for (int32 CollectionIndex = 0; CollectionIndex < TreeList.Num(); ++CollectionIndex)
			{
				if (!(TreeList[CollectionIndex]->HasSameMetadata(*TempTreeDataList[CollectionIndex])))
				{
					bTreeNeedsRefresh = true;
				}
			}
		}

		if (bTreeNeedsRefresh)
		{
			TreeList = TempTreeDataList;
			TreeView->RequestTreeRefresh();
			bHasActiveCollection = false;
		}

		for (TSharedPtr<FPresetViewEntry>& Entry : TreeList)
		{
			Entry->bEnabled = UserSettings->EnabledPresetCollections.Contains(Entry->CollectionPath);
			if (Entry->bIsDefaultCollection)
			{
				Entry->bEnabled = UserSettings->bDefaultCollectionEnabled;
			}
			else
			{
				Entry->bEnabled = UserSettings->EnabledPresetCollections.Contains(Entry->CollectionPath);
			}
		}
	};

	// Handle the default collection
	UPresetAssetSubsystem* PresetAssetSubsystem = GEditor->GetEditorSubsystem<UPresetAssetSubsystem>();
	TObjectPtr<UInteractiveToolsPresetCollectionAsset> DefaultCollection = nullptr;
	if (ensure(PresetAssetSubsystem))
	{
		DefaultCollection = PresetAssetSubsystem->GetDefaultCollection();
	}

	GenerateTreeEntries(nullptr, &AvailablePresetCollections, ProjectCollectionsDataList, ProjectPresetCollectionTreeView);
	GenerateTreeEntries(nullptr, &AvailableUserPresetCollections, UserCollectionsDataList, UserPresetCollectionTreeView);
	GenerateTreeEntries(DefaultCollection, nullptr, EditorCollectionsDataList, EditorPresetCollectionTreeView);

}


/* SPresetManager implementation
 *****************************************************************************/

int32 SPresetManager::GetTotalPresetCount() const
{
	return TotalPresetCount;
}

TSharedRef<ITableRow> SPresetManager::HandleTreeGenerateRow(TSharedPtr<FPresetViewEntry> TreeEntry, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(PresetManagerLocals::SCollectionTableRow< TSharedPtr<FPresetViewEntry> >, OwnerTable)
		.ViewEntry(TreeEntry)
		.OnCollectionEnabledCheckboxChanged(this, &SPresetManager::SetCollectionEnabled)
		.OnCollectionRenameStarted(this, &SPresetManager::CollectionRenameStarted)
		.OnCollectionRenameEnded(this, &SPresetManager::CollectionRenameEnded);
}

void SPresetManager::HandleTreeGetChildren(TSharedPtr<FPresetViewEntry> TreeEntry, TArray< TSharedPtr<FPresetViewEntry> >& ChildrenOut)
{
	ChildrenOut = TreeEntry->Children;
}

void SPresetManager::GeneratePresetList(TSharedPtr<FPresetViewEntry> TreeEntry)
{
	PresetDataList.Empty();
	PresetListView->RequestListRefresh();
	bHasActiveCollection = false;
	ActivePresetToEdit = nullptr;
	bHasPresetsInCollection = false;

	if (!TreeEntry)
	{
		return;
	}

	if (TreeEntry->EntryType == FPresetViewEntry::EEntryType::Collection ||
		TreeEntry->EntryType == FPresetViewEntry::EEntryType::Tool)
	{
		UInteractiveToolsPresetCollectionAsset* PresetCollection = GetCollectionFromEntry(TreeEntry);

		if (PresetCollection)
		{
			if (TreeEntry->EntryType == FPresetViewEntry::EEntryType::Collection)
			{
				bHasActiveCollection = true;
				bIsActiveCollectionEnabled = TreeEntry->bEnabled;
				ActiveCollectionLabel = TreeEntry->EntryLabel;

				TMap<FString, FInteractiveToolPresetStore >::TIterator ToolNameIter = PresetCollection->PerToolPresets.CreateIterator();
				for (; (bool)ToolNameIter; ++ToolNameIter)
				{
					int32 ToolCount = ToolNameIter.Value().NamedPresets.Num();
					for (int32 PresetIndex = 0; PresetIndex < ToolNameIter.Value().NamedPresets.Num(); ++PresetIndex)
					{
						if (ToolNameIter.Value().NamedPresets[PresetIndex].IsValid())
						{
							bHasPresetsInCollection = true;
							PresetDataList.Add(MakeShared<FPresetViewEntry>(
								ToolNameIter.Key(),
								PresetIndex,
								ToolNameIter.Value().NamedPresets[PresetIndex].Label,
								ToolNameIter.Value().NamedPresets[PresetIndex].Tooltip,
								FText::FromString(ToolNameIter.Value().NamedPresets[PresetIndex].Label)
								));
							PresetDataList.Last()->Parent = TreeEntry;
							PresetDataList.Last()->CollectionPath = TreeEntry->CollectionPath;
							PresetDataList.Last()->EntryIcon = ToolNameIter.Value().ToolIcon;
						}
					}
				}
			}
			else
			{
				bHasActiveCollection = true;
				bIsActiveCollectionEnabled = TreeEntry->Parent->bEnabled;
				ActiveCollectionLabel = TreeEntry->Parent->EntryLabel;

				const FInteractiveToolPresetStore* ToolData = PresetCollection->PerToolPresets.Find(TreeEntry->ToolName);
				if (!ToolData)
				{
					return;
				}
				int32 ToolCount = ToolData->NamedPresets.Num();
				for (int32 PresetIndex = 0; PresetIndex < ToolData->NamedPresets.Num(); ++PresetIndex)
				{
					if (ToolData->NamedPresets[PresetIndex].IsValid())
					{
						bHasPresetsInCollection = true;
						PresetDataList.Add(MakeShared<FPresetViewEntry>(
							TreeEntry->ToolName,
							PresetIndex,
							ToolData->NamedPresets[PresetIndex].Label,
							ToolData->NamedPresets[PresetIndex].Tooltip,
							FText::FromString(ToolData->NamedPresets[PresetIndex].Label)
							));
						PresetDataList.Last()->Parent = TreeEntry;
						PresetDataList.Last()->CollectionPath = TreeEntry->CollectionPath;
						PresetDataList.Last()->EntryIcon = TreeEntry->EntryIcon;
					}
				}
			}
		}
	}

}

void SPresetManager::HandleEditorTreeSelectionChanged(TSharedPtr<FPresetViewEntry> TreeEntry, ESelectInfo::Type SelectInfo)
{
	for (TSharedPtr<FPresetViewEntry> Entry : UserPresetCollectionTreeView->GetRootItems())
	{
		Entry->bIsRenaming = false;
	}

	if (SelectInfo != ESelectInfo::Direct)
	{
		UserPresetCollectionTreeView->ClearSelection();
		ProjectPresetCollectionTreeView->ClearSelection();
		GeneratePresetList(TreeEntry);

		LastFocusedList = EditorPresetCollectionTreeView;
	}
}

void SPresetManager::HandleTreeSelectionChanged(TSharedPtr<FPresetViewEntry> TreeEntry, ESelectInfo::Type SelectInfo)
{
	for (TSharedPtr<FPresetViewEntry> Entry : UserPresetCollectionTreeView->GetRootItems())
	{
		Entry->bIsRenaming = false;
	}

	if (SelectInfo != ESelectInfo::Direct)
	{
		UserPresetCollectionTreeView->ClearSelection();
		EditorPresetCollectionTreeView->ClearSelection();
		GeneratePresetList(TreeEntry);

		LastFocusedList = ProjectPresetCollectionTreeView;

	}
}

void SPresetManager::HandleUserTreeSelectionChanged(TSharedPtr<FPresetViewEntry> TreeEntry, ESelectInfo::Type SelectInfo)
{
	for (TSharedPtr<FPresetViewEntry> Entry : UserPresetCollectionTreeView->GetRootItems())
	{
		Entry->bIsRenaming = false;
	}

	if (SelectInfo != ESelectInfo::Direct)
	{
		ProjectPresetCollectionTreeView->ClearSelection();
		EditorPresetCollectionTreeView->ClearSelection();
		GeneratePresetList(TreeEntry);

		LastFocusedList = UserPresetCollectionTreeView;

	}
}

TSharedRef<ITableRow> SPresetManager::HandleListGenerateRow(TSharedPtr<FPresetViewEntry> TreeEntry, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(PresetManagerLocals::SPresetTableRow< TSharedPtr<FPresetViewEntry> >, OwnerTable)
		.ViewEntry(TreeEntry)
		.OnPresetDeleted(this, &SPresetManager::DeletePresetFromCollection);
}

void SPresetManager::HandleListSelectionChanged(TSharedPtr<FPresetViewEntry> TreeEntry, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		LastFocusedList = PresetListView;
	}


	if (TreeEntry)
	{
		ActivePresetToEdit = TreeEntry;
	}
	else
	{
		if (ActivePresetToEdit)
		{
			SetPresetLabel(ActivePresetToEdit, FText::FromString(ActivePresetToEdit->PresetLabel));
			SetPresetTooltip(ActivePresetToEdit, FText::FromString(ActivePresetToEdit->PresetTooltip));
		}
		ActivePresetToEdit.Reset();
	}
}

bool SPresetManager::EditAreaEnabled() const
{
	return ActivePresetToEdit.IsValid();
}

EVisibility SPresetManager::ProjectPresetCollectionsVisibility() const
{
	return ProjectCollectionsDataList.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

void SPresetManager::SetCollectionEnabled(TSharedPtr<FPresetViewEntry> TreeEntry, ECheckBoxState State)
{	
	if (!ensure(UserSettings.IsValid()))
	{
		return;
	}
	if (TreeEntry->bIsDefaultCollection)
	{
		UserSettings->bDefaultCollectionEnabled = (State == ECheckBoxState::Checked);
		UserSettings->SaveEditorConfig();
	}
	else
	{
		if (State == ECheckBoxState::Checked && !UserSettings->EnabledPresetCollections.Contains(TreeEntry->CollectionPath))
		{
			UserSettings->EnabledPresetCollections.Add(TreeEntry->CollectionPath);
			UserSettings->SaveEditorConfig();
		}
		else if (State != ECheckBoxState::Checked && UserSettings->EnabledPresetCollections.Contains(TreeEntry->CollectionPath))
		{
			UserSettings->EnabledPresetCollections.Remove(TreeEntry->CollectionPath);
			UserSettings->SaveEditorConfig();
		}
	}
}

void SPresetManager::CollectionRenameStarted(TSharedPtr<FPresetViewEntry> TreeEntry, TSharedPtr<SEditableTextBox> RenameWidget)
{
	// TODO: Figure out why this crashes
	//FSlateApplication::Get().SetKeyboardFocus(RenameWidget, EFocusCause::SetDirectly);
}

void SPresetManager::CollectionRenameEnded(TSharedPtr<FPresetViewEntry> TreeEntry, const FText& NewText)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	TreeEntry->bIsRenaming = false;

	FAssetData CollectionAsset;
	if (AssetRegistryModule.Get().TryGetAssetByObjectPath(TreeEntry->CollectionPath, CollectionAsset) == UE::AssetRegistry::EExists::Exists)
	{
		UInteractiveToolsPresetCollectionAsset* CollectionObject = Cast<UInteractiveToolsPresetCollectionAsset>(CollectionAsset.GetAsset());
		if (!CollectionObject)
		{
			return;
		}

		TArray< FAssetRenameData> RenameData;

		FString NewPackageName, NewAssetName;
		IAssetTools::Get().CreateUniqueAssetName(NewText.ToString(), "", NewPackageName, NewAssetName);

		RenameData.SetNum(1);		
		RenameData[0].Asset = CollectionObject;
		RenameData[0].NewName = NewAssetName;
		RenameData[0].NewPackagePath = CollectionAsset.PackagePath.ToString();
		if(IAssetTools::Get().RenameAssets(RenameData))
		{
			if (UserSettings->EnabledPresetCollections.Contains(TreeEntry->CollectionPath))
			{
				UserSettings->EnabledPresetCollections.Remove(TreeEntry->CollectionPath);
				UserSettings->EnabledPresetCollections.Add(CollectionObject->GetPathName());
				UserSettings->SaveEditorConfig();
			}

			CollectionObject->CollectionLabel = NewText;
			CollectionObject->MarkPackageDirty();	
		}
	}
}


void SPresetManager::DeletePresetFromCollection(TSharedPtr< FPresetViewEntry > Entry)
{
	UInteractiveToolsPresetCollectionAsset* PresetCollection = GetCollectionFromEntry(Entry);
	if (PresetCollection)
	{
		PresetCollection->PerToolPresets[Entry->ToolName].NamedPresets.RemoveAt(Entry->PresetIndex);
		PresetCollection->MarkPackageDirty();

		GeneratePresetList(Entry->Parent);
	}

	SaveIfDefaultCollection(Entry);
}

void SPresetManager::SetPresetLabel(TSharedPtr< FPresetViewEntry > Entry, FText InLabel)
{
	UInteractiveToolsPresetCollectionAsset* PresetCollection = GetCollectionFromEntry(Entry);
	if (PresetCollection)
	{
		PresetCollection->PerToolPresets[Entry->ToolName].NamedPresets[Entry->PresetIndex].Label = InLabel.ToString();
		PresetCollection->MarkPackageDirty();
	}

	SaveIfDefaultCollection(Entry);
}

void SPresetManager::SetPresetTooltip(TSharedPtr< FPresetViewEntry > Entry, FText InTooltip)
{
	UInteractiveToolsPresetCollectionAsset* PresetCollection = GetCollectionFromEntry(Entry);
	if (PresetCollection)
	{
		PresetCollection->PerToolPresets[Entry->ToolName].NamedPresets[Entry->PresetIndex].Tooltip = InTooltip.ToString();
		PresetCollection->MarkPackageDirty();
	}

	SaveIfDefaultCollection(Entry);
}

void SPresetManager::DeleteSelectedUserPresetCollection()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	TArray<TSharedPtr<FPresetViewEntry>> SelectedUserCollections = UserPresetCollectionTreeView->GetSelectedItems();

	if (SelectedUserCollections.Num() == 1)
	{
		TSharedPtr<FPresetViewEntry> Entry = SelectedUserCollections[0];
		if (Entry->bIsDefaultCollection)
		{
			return;
		}

		FAssetData CollectionAsset;
		if (AssetRegistryModule.Get().TryGetAssetByObjectPath(Entry->CollectionPath, CollectionAsset) == UE::AssetRegistry::EExists::Exists)
		{
			TArray<FAssetData> AssetData;
			AssetData.Add(CollectionAsset);
			ObjectTools::DeleteAssets(AssetData, true);
		}

		GeneratePresetList(nullptr);
	}
}

void SPresetManager::AddNewUserPresetCollection()
{
	// Load necessary modules
	FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// Generate a unique asset name
	FString Name, PackageName;
	AssetToolsModule.Get().CreateUniqueAssetName(TEXT("/ToolPresets/Presets/"), TEXT("UserPresetCollection"), PackageName, Name);
	const FString PackagePath = FPackageName::GetLongPackagePath(PackageName);

	// Create object and package
	UPackage* package = CreatePackage(*PackageName);
	UInteractiveToolsPresetCollectionAssetFactory* MyFactory = NewObject<UInteractiveToolsPresetCollectionAssetFactory>(UInteractiveToolsPresetCollectionAssetFactory::StaticClass()); // Can omit, and a default factory will be used
	UObject* NewObject = AssetToolsModule.Get().CreateAsset(Name, PackagePath, UInteractiveToolsPresetCollectionAsset::StaticClass(), MyFactory);
	UInteractiveToolsPresetCollectionAsset* NewCollection = ExactCast<UInteractiveToolsPresetCollectionAsset>(NewObject);
	NewCollection->CollectionLabel = FText::FromString(Name);
	FSavePackageArgs SavePackageArgs;
	SavePackageArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::Save(package, NewObject, *FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()), SavePackageArgs);

	// Inform asset registry
	AssetRegistry.AssetCreated(NewObject);

	// Since we're adding a new entry, open this tree view up again.
	bAreUserCollectionsExpanded = true;
}

const FSlateBrush* SPresetManager::GetProjectCollectionsExpanderImage() const
{
	return GetExpanderImage(ProjectCollectionsExpander, false);
}

const FSlateBrush* SPresetManager::GetUserCollectionsExpanderImage() const
{
	return GetExpanderImage(UserCollectionsExpander, true);
}

const FSlateBrush* SPresetManager::GetExpanderImage(TSharedPtr<SWidget> ExpanderWidget, bool bIsUserCollections) const
{
	const bool bIsItemExpanded = bIsUserCollections ? bAreUserCollectionsExpanded : bAreProjectCollectionsExpanded;

	FName ResourceName;
	if (bIsItemExpanded)
	{
		if (ExpanderWidget->IsHovered())
		{
			static FName ExpandedHoveredName = "TreeArrow_Expanded_Hovered";
			ResourceName = ExpandedHoveredName;
		}
		else
		{
			static FName ExpandedName = "TreeArrow_Expanded";
			ResourceName = ExpandedName;
		}
	}
	else
	{
		if (ExpanderWidget->IsHovered())
		{
			static FName CollapsedHoveredName = "TreeArrow_Collapsed_Hovered";
			ResourceName = CollapsedHoveredName;
		}
		else
		{
			static FName CollapsedName = "TreeArrow_Collapsed";
			ResourceName = CollapsedName;
		}
	}

	return FCoreStyle::Get().GetBrush(ResourceName);
}

UInteractiveToolsPresetCollectionAsset* SPresetManager::GetCollectionFromEntry(TSharedPtr<FPresetViewEntry> Entry)
{
	UInteractiveToolsPresetCollectionAsset* PresetCollection = nullptr;
	UPresetAssetSubsystem* PresetAssetSubsystem = GEditor->GetEditorSubsystem<UPresetAssetSubsystem>();
	
	if (Entry->Root().bIsDefaultCollection && ensure(PresetAssetSubsystem))
	{
		PresetCollection = PresetAssetSubsystem->GetDefaultCollection();
	}
	else
	{
		if (Entry->CollectionPath.IsAsset())
		{
			PresetCollection = Cast<UInteractiveToolsPresetCollectionAsset>(Entry->CollectionPath.TryLoad());
		}
	}

	return PresetCollection;
}

void SPresetManager::SaveIfDefaultCollection(TSharedPtr<FPresetViewEntry> Entry)
{
	UPresetAssetSubsystem* PresetAssetSubsystem = GEditor->GetEditorSubsystem<UPresetAssetSubsystem>();

	if (Entry->Root().bIsDefaultCollection && ensure(PresetAssetSubsystem))
	{
		ensure(PresetAssetSubsystem->SaveDefaultCollection());
	}
}

TSharedPtr<SWidget> SPresetManager::OnGetPresetContextMenuContent() const
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, UICommandList);

	MenuBuilder.BeginSection("PresetManagerPresetAction", LOCTEXT("PresetAction", "Preset Actions"));

	MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete, NAME_None, LOCTEXT("DeletePresetLabel", "Delete Preset"), LOCTEXT("DeletePresetToolTip", "Delete the selected preset"));

	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedPtr<SWidget> SPresetManager::OnGetCollectionContextMenuContent() const
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, UICommandList);

	MenuBuilder.BeginSection("PresetManagerCollectionAction", LOCTEXT("CollectionAction", "Preset Collection Actions"));

	MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete, NAME_None, LOCTEXT("DeleteCollectionLabel", "Delete Collection"), LOCTEXT("DeleteCollectionToolTip", "Delete the selected collection"));
	MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename, NAME_None, LOCTEXT("RenameCollectionLabel", "Rename Collection"), LOCTEXT("RenameCollectionToolTip", "Rename the selected collection"));

	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SPresetManager::BindCommands()
{
	// This should not be called twice on the same instance
	check(!UICommandList.IsValid());

	UICommandList = MakeShareable(new FUICommandList);

	FUICommandList& CommandList = *UICommandList;

	// ...and bind them all

	CommandList.MapAction(
		FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &SPresetManager::OnDeleteClicked),
		FCanExecuteAction::CreateSP(this, &SPresetManager::CanDelete));

	CommandList.MapAction(
		FGenericCommands::Get().Rename,
		FExecuteAction::CreateSP(this, &SPresetManager::OnRenameClicked),
		FCanExecuteAction::CreateSP(this, &SPresetManager::CanRename));	
}


void SPresetManager::OnDeleteClicked()
{
	if (UserPresetCollectionTreeView == LastFocusedList)
	{
		DeleteSelectedUserPresetCollection();
	}

	if (PresetListView == LastFocusedList)
	{
		for (TSharedPtr<FPresetViewEntry> Entry : PresetListView->GetSelectedItems())
		{
			DeletePresetFromCollection(Entry);
		}
	}
}

bool SPresetManager::CanDelete()
{
	bool bIsListValid = LastFocusedList.IsValid() && (LastFocusedList == UserPresetCollectionTreeView || LastFocusedList == PresetListView);
	bool bIsSelectionValid = false;
	if (bIsListValid)
	{
		bIsSelectionValid = LastFocusedList.Pin()->GetNumItemsSelected() == 1 &&
			                (LastFocusedList.Pin()->GetSelectedItems()[0]->EntryType == FPresetViewEntry::EEntryType::Collection ||
							 LastFocusedList.Pin()->GetSelectedItems()[0]->EntryType == FPresetViewEntry::EEntryType::Preset);
	}
	return bIsSelectionValid;
}

void SPresetManager::OnRenameClicked()
{
	for (TSharedPtr<FPresetViewEntry> Entry : UserPresetCollectionTreeView->GetRootItems())
	{
		Entry->bIsRenaming = false;
	}

	for (TSharedPtr<FPresetViewEntry> Entry : UserPresetCollectionTreeView->GetSelectedItems())
	{
		Entry->bIsRenaming = true;
	}
}

bool SPresetManager::CanRename()
{
	bool bIsListValid = LastFocusedList.IsValid() && LastFocusedList == UserPresetCollectionTreeView;
	bool bIsSelectionValid = false;
	if (bIsListValid)
	{
		bIsSelectionValid = LastFocusedList.Pin()->GetNumItemsSelected() == 1 &&
			                LastFocusedList.Pin()->GetSelectedItems()[0]->EntryType == FPresetViewEntry::EEntryType::Collection;
	}
	return bIsSelectionValid;
}

#undef LOCTEXT_NAMESPACE