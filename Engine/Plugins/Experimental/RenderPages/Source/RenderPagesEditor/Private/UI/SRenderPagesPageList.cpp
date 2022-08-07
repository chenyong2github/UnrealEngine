// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SRenderPagesPageList.h"

#include "UI/Components/SRenderPagesDragHandle.h"
#include "UI/Components/SRenderPagesEditableTextBlock.h"
#include "UI/Components/SRenderPagesFileSelectorTextBlock.h"
#include "RenderPage/RenderPageCollection.h"
#include "RenderPage/RenderPageMoviePipelineJob.h"
#include "RenderPage/RenderPageManager.h"
#include "IRenderPageCollectionEditor.h"
#include "IRenderPagesModule.h"

#include "DesktopPlatformModule.h"
#include "Misc/MessageDialog.h"
#include "MoviePipelineExecutor.h"
#include "MoviePipelineQueue.h"
#include "PropertyCustomizationHelpers.h"
#include "SlateOptMacros.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SScaleBox.h"

#define LOCTEXT_NAMESPACE "SRenderPagesPageList"


namespace UE::RenderPages::Private::FRenderPagesCollectionColumns
{
	const FName DragDropHandle = TEXT("DragDropHandle");
	const FName IsEnabled = TEXT("IsEnabled");
	const FName PageId = TEXT("PageId");
	const FName PageName = TEXT("PageName");
	const FName OutputDirectory = TEXT("OutputDirectory");
	const FName RenderPreset = TEXT("RenderPreset");
	const FName StartFrame = TEXT("StartFrame");
	const FName EndFrame = TEXT("EndFrame");
	const FName Tags = TEXT("Tags");
	const FName Duration = TEXT("Duration");
	const FName RenderingStatus = TEXT("Status");
}


void UE::RenderPages::Private::SRenderPagesPageList::Tick(const FGeometry&, const double, const float)
{
	if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		if (RenderPagesCollectionWeakPtr != BlueprintEditor->GetInstance())
		{
			Refresh();
		}
	}
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void UE::RenderPages::Private::SRenderPagesPageList::Construct(const FArguments& InArgs, TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor)
{
	BlueprintEditorWeakPtr = InBlueprintEditor;

	Refresh();
	InBlueprintEditor->OnRenderPageCreated().AddSP(this, &SRenderPagesPageList::OnRenderPageCreated);
	InBlueprintEditor->OnRenderPagesChanged().AddSP(this, &SRenderPagesPageList::Refresh);
	InBlueprintEditor->OnRenderPagesBatchRenderingStarted().AddSP(this, &SRenderPagesPageList::OnBatchRenderingStarted);
	InBlueprintEditor->OnRenderPagesBatchRenderingFinished().AddSP(this, &SRenderPagesPageList::OnBatchRenderingFinished);

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.f)
		[
			SNew(SHorizontalBox)
			// Search Box
			+ SHorizontalBox::Slot()
			.Padding(4.f, 2.f)
			[
				SAssignNew(RenderPagesSearchBox, SSearchBox)
				.HintText(LOCTEXT("Search_HintText", "Search Tags | Text"))
				.OnTextChanged(this, &SRenderPagesPageList::OnSearchBarTextChanged)
			]
			// Filters
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.f, 2.f, 2.f, 2.f)
			[
				SNew(SComboButton)
				.ComboButtonStyle(FAppStyle::Get(), "GenericFilters.ComboButtonStyle")
				.ForegroundColor(FLinearColor::White)
				.ToolTipText(LOCTEXT("Filters_Tooltip", "Filter options for the Pages Collection."))
				.HasDownArrow(true)
				.ContentPadding(0.f)
				.ButtonContent()
				[
					SNew(STextBlock)
					.TextStyle(FAppStyle::Get(), "GenericFilters.TextStyle")
					.Font(FAppStyle::Get().GetFontStyle("FontAwesome.12"))
					.Text(FText::FromString(FString(TEXT("\xf0b0"))) /*fa-filter*/)
				]
			]
		]
		// Pages Collection
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(0.f)
			[
				SAssignNew(RenderPageListWidget, SListView<URenderPage*>)
				.ItemHeight(20.0f)
				.OnGenerateRow(this, &SRenderPagesPageList::HandlePagesCollectionGenerateRow)
				.OnSelectionChanged(this, &SRenderPagesPageList::HandlePagesCollectionSelectionChanged)
				.SelectionMode(ESelectionMode::Multi)
				.ClearSelectionOnClick(false)
				.ListItemsSource(&RenderPages)
				.HeaderRow(
					SNew(SHeaderRow)

					+ SHeaderRow::Column(FRenderPagesCollectionColumns::DragDropHandle)
					.DefaultLabel(LOCTEXT("PagesCollectionDragDropHandleColumnHeader", ""))
					.FixedWidth(36.0f)

					+ SHeaderRow::Column(FRenderPagesCollectionColumns::IsEnabled)
					.DefaultLabel(LOCTEXT("PagesCollectionIsEnabledColumnHeader", "Enabled"))
					.FixedWidth(30.0f) //55.0f for text : "Enabled"
					[
						SAssignNew(RenderPageEnabledHeaderCheckbox, SCheckBox)
						.IsChecked(true)
						.OnCheckStateChanged(this, &SRenderPagesPageList::OnHeaderCheckboxToggled)
					]

					+ SHeaderRow::Column(FRenderPagesCollectionColumns::PageId)
					.DefaultLabel(LOCTEXT("PagesCollectionIDColumnHeader", "Page ID"))
					.FillWidth(0.3f)

					+ SHeaderRow::Column(FRenderPagesCollectionColumns::PageName)
					.DefaultLabel(LOCTEXT("PagesCollectionNameColumnHeader", "Page Name"))
					.FillWidth(0.3f)

					+ SHeaderRow::Column(FRenderPagesCollectionColumns::OutputDirectory)
					.DefaultLabel(LOCTEXT("PagesCollectionOutDirColumnHeader", "Output Directory"))
					.FillWidth(0.7f)

					+ SHeaderRow::Column(FRenderPagesCollectionColumns::RenderPreset)
					.DefaultLabel(LOCTEXT("PagesCollectionRenderPresetColumnHeader", "Render Preset"))
					.FillWidth(0.5f)

					+ SHeaderRow::Column(FRenderPagesCollectionColumns::StartFrame)
					.DefaultLabel(LOCTEXT("PagesCollectionStartFrameColumnHeader", "Start Frame"))
					.FixedWidth(80.0f)

					+ SHeaderRow::Column(FRenderPagesCollectionColumns::EndFrame)
					.DefaultLabel(LOCTEXT("PagesCollectionEndFrameColumnHeader", "End Frame"))
					.FixedWidth(80.0f)

					+ SHeaderRow::Column(FRenderPagesCollectionColumns::Tags)
					.DefaultLabel(LOCTEXT("PagesCollectionTagsColumnHeader", "Tags"))
					.FillWidth(0.7f)

					+ SHeaderRow::Column(FRenderPagesCollectionColumns::Duration)
					.DefaultLabel(LOCTEXT("PagesCollectionEstDurColumnHeader", "Est Duration"))
					.FixedWidth(120.0f)
				)
			]
		]
	];

	Refresh();
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void UE::RenderPages::Private::SRenderPagesPageList::OnRenderPageCreated(URenderPage* Page)
{
	if (!RenderPageEnabledHeaderCheckbox.IsValid())
	{
		return;
	}
	Page->SetIsEnabled(RenderPageEnabledHeaderCheckbox->GetCheckedState() != ECheckBoxState::Unchecked);
}

void UE::RenderPages::Private::SRenderPagesPageList::OnHeaderCheckboxToggled(ECheckBoxState State)
{
	if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		if (URenderPageCollection* Collection = BlueprintEditor->GetInstance(); IsValid(Collection))
		{
			bool bRefresh = false;

			for (URenderPage* Page : Collection->GetRenderPages())
			{
				Page->SetIsEnabled(State == ECheckBoxState::Checked);
				bRefresh = true;
			}

			if (bRefresh)
			{
				Refresh();
			}
		}
	}
}

ECheckBoxState UE::RenderPages::Private::SRenderPagesPageList::GetDesiredHeaderEnabledCheckboxState()
{
	ECheckBoxState State = ECheckBoxState::Checked;
	if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		if (URenderPageCollection* Collection = BlueprintEditor->GetInstance(); IsValid(Collection))
		{
			bool bFirstPage = true;
			for (URenderPage* Page : Collection->GetRenderPages())
			{
				ECheckBoxState PageState = (Page->GetIsEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
				if (bFirstPage)
				{
					State = PageState;
					bFirstPage = false;
				}
				else if (State != PageState)
				{
					return ECheckBoxState::Undetermined;
				}
			}
		}
	}
	return State;
}

void UE::RenderPages::Private::SRenderPagesPageList::AddRenderStatusColumn()
{
	if (!RenderPageListWidget.IsValid())
	{
		return;
	}
	RenderPageListWidget->GetHeaderRow()->AddColumn(SHeaderRow::Column(FRenderPagesCollectionColumns::RenderingStatus)
		.DefaultLabel(LOCTEXT("PagesCollectionRenderStatusColumnHeader", "Render Status"))
		.FillWidth(0.5f));
}

void UE::RenderPages::Private::SRenderPagesPageList::RemoveRenderStatusColumn()
{
	if (!RenderPageListWidget.IsValid())
	{
		return;
	}
	RenderPageListWidget->GetHeaderRow()->RemoveColumn(FRenderPagesCollectionColumns::RenderingStatus);
}


void UE::RenderPages::Private::SRenderPagesPageList::Refresh()
{
	if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		const bool bIsBatchRendering = BlueprintEditor->IsBatchRendering();// show all pages during a batch render, ignore the search bar
		const FString SearchBarContent = (!RenderPagesSearchBox.IsValid() ? TEXT("") : RenderPagesSearchBox->GetText().ToString());

		RenderPages.Empty();
		RenderPagesCollectionWeakPtr = BlueprintEditor->GetInstance();
		if (URenderPageCollection* Collection = RenderPagesCollectionWeakPtr.Get(); IsValid(Collection))
		{
			for (URenderPage* Page : Collection->GetRenderPages())
			{
				if (bIsBatchRendering || Page->MatchesSearchTerm(SearchBarContent))
				{
					RenderPages.Add(Page);
				}
			}
		}

		RefreshHeaderEnabledCheckbox();

		RemoveRenderStatusColumn();
		if (bIsBatchRendering)
		{
			AddRenderStatusColumn();
		}

		if (RenderPageListWidget.IsValid())
		{
			RenderPageListWidget->RebuildList(); // rebuild is needed (instead of using RequestListRefresh()), because otherwise it won't show the changes made to the URenderPage variables

			TArray<URenderPage*> SelectedItems = BlueprintEditor->GetSelectedRenderPages().FilterByPredicate([this](URenderPage* Item)
			{
				return IsValid(Item) && RenderPages.Contains(Item);
			});
			RenderPageListWidget->ClearSelection();
			RenderPageListWidget->SetItemSelection(SelectedItems, true);
			BlueprintEditor->SetSelectedRenderPages(SelectedItems);
		}
	}
}

void UE::RenderPages::Private::SRenderPagesPageList::RefreshHeaderEnabledCheckbox()
{
	if (!RenderPageEnabledHeaderCheckbox.IsValid())
	{
		return;
	}
	RenderPageEnabledHeaderCheckbox->SetIsChecked(GetDesiredHeaderEnabledCheckboxState());
}

TSharedRef<ITableRow> UE::RenderPages::Private::SRenderPagesPageList::HandlePagesCollectionGenerateRow(URenderPage* Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedPtr<SWidget> BaseThis = AsShared();
	TSharedPtr<SRenderPagesPageList> This = StaticCastSharedPtr<SRenderPagesPageList>(BaseThis);

	return SNew(SRenderPagesPageListTableRow, OwnerTable, BlueprintEditorWeakPtr, Item, This);
}

void UE::RenderPages::Private::SRenderPagesPageList::HandlePagesCollectionSelectionChanged(URenderPage* Item, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo == ESelectInfo::Type::Direct)
	{
		return;
	}
	if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		BlueprintEditor->SetSelectedRenderPages(RenderPageListWidget->GetSelectedItems());
	}
}


void UE::RenderPages::Private::SRenderPagesPageListTableRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, TWeakPtr<IRenderPageCollectionEditor> InBlueprintEditor, URenderPage* InRenderPage, const TSharedPtr<SRenderPagesPageList>& InPageListWidget)
{
	BlueprintEditorWeakPtr = InBlueprintEditor;
	RenderPage = InRenderPage;
	PageListWidget = InPageListWidget;

	SMultiColumnTableRow<URenderPage*>::Construct(FSuperRowType::FArguments()
		.OnCanAcceptDrop(this, &SRenderPagesPageListTableRow::OnCanAcceptDrop)
		.OnAcceptDrop(this, &SRenderPagesPageListTableRow::OnAcceptDrop),
		InOwnerTableView);
}

TOptional<EItemDropZone> UE::RenderPages::Private::SRenderPagesPageListTableRow::OnCanAcceptDrop(const FDragDropEvent& Event, EItemDropZone InItemDropZone, URenderPage* Page)
{
	if (BlueprintEditorWeakPtr.IsValid() && Event.GetOperationAs<FRenderPagesPageListTableRowDragDropOp>())
	{
		if (InItemDropZone == EItemDropZone::OntoItem)
		{
			return EItemDropZone::BelowItem;
		}
		return InItemDropZone;
	}
	return TOptional<EItemDropZone>();
}

FReply UE::RenderPages::Private::SRenderPagesPageListTableRow::OnAcceptDrop(const FDragDropEvent& Event, EItemDropZone InItemDropZone, URenderPage* Page)
{
	if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		if (const TSharedPtr<FRenderPagesPageListTableRowDragDropOp> DragDropOp = Event.GetOperationAs<FRenderPagesPageListTableRowDragDropOp>())
		{
			if (URenderPageCollection* Instance = BlueprintEditor->GetInstance(); IsValid(Instance))
			{
				if (Instance->ReorderRenderPage(DragDropOp->GetPage(), Page, (InItemDropZone != EItemDropZone::AboveItem)))
				{
					BlueprintEditor->MarkAsModified();
					BlueprintEditor->OnRenderPagesChanged().Broadcast();
					return FReply::Handled();
				}
			}
		}
	}
	return FReply::Unhandled();
}

TSharedRef<SWidget> UE::RenderPages::Private::SRenderPagesPageListTableRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	if (!IsValid(RenderPage))
	{
		return SNullWidget::NullWidget;
	}

	if (ColumnName == FRenderPagesCollectionColumns::DragDropHandle)
	{
		return SNew(SBox)
			.Padding(FMargin(0.0f, 2.0f, 2.0f, 2.0f))
			[
				SNew(SScaleBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Stretch(EStretch::ScaleToFit)
				.StretchDirection(EStretchDirection::Both)
				[
					SNew(SRenderPagesDragHandle<FRenderPagesPageListTableRowDragDropOp>, RenderPage)
					.Widget(SharedThis(this))
				]
			];
	}
	else if (ColumnName == FRenderPagesCollectionColumns::IsEnabled)
	{
		return SNew(SBox)
			.HAlign(HAlign_Center)
			[
				SNew(SCheckBox)
				.IsChecked(RenderPage->GetIsEnabled())
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
				{
					RenderPage->SetIsEnabled(State == ECheckBoxState::Checked);
					if (PageListWidget.IsValid())
					{
						PageListWidget->RefreshHeaderEnabledCheckbox();
					}
					if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
					{
						BlueprintEditor->MarkAsModified();
					}
				})
			];
	}
	else if (ColumnName == FRenderPagesCollectionColumns::PageId)
	{
		return SNew(SRenderPagesEditableTextBlock)
			.Text(FText::FromString(RenderPage->GetPageId()))
			.OnTextCommitted_Lambda([this](const FText& InLabel, ETextCommit::Type InCommitInfo) -> FText
			{
				const FString OldPageId = RenderPage->GetPageId();
				const FString NewPageId = URenderPage::PurgePageIdOrReturnEmptyString(InLabel.ToString());
				if (NewPageId.IsEmpty() || (RenderPage->GetPageId() == NewPageId))
				{
					return FText::FromString(OldPageId);
				}

				if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
				{
					if (URenderPageCollection* PageCollection = BlueprintEditor->GetInstance(); IsValid(PageCollection))
					{
						if (PageCollection->DoesPageIdExist(NewPageId))
						{
							const FText TitleText = LOCTEXT("PageIdNotUniqueTitle", "Duplicate Page IDs");
							FMessageDialog::Open(
								EAppMsgType::Ok,
								FText::Format(LOCTEXT("PageIdNotUniqueMessage", "Page ID \"{0}\" is not unique."), FText::FromString(NewPageId)),
								&TitleText);
							return FText::FromString(OldPageId);
						}

						RenderPage->SetPageId(NewPageId);
						BlueprintEditor->MarkAsModified();
						return FText::FromString(RenderPage->GetPageId());
					}
				}
				return FText::FromString(OldPageId);
			});
	}
	else if (ColumnName == FRenderPagesCollectionColumns::PageName)
	{
		return SNew(SRenderPagesEditableTextBlock)
			.Text(FText::FromString(RenderPage->GetPageName()))
			.OnTextCommitted_Lambda([this](const FText& InLabel, ETextCommit::Type InCommitInfo) -> FText
			{
				RenderPage->SetPageName(InLabel.ToString());
				if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
				{
					BlueprintEditor->MarkAsModified();
				}
				return FText::FromString(RenderPage->GetPageName());
			});
	}
	else if (ColumnName == FRenderPagesCollectionColumns::OutputDirectory)
	{
		return SNew(SRenderPagesFileSelectorTextBlock)
			.Text(FText::FromString(RenderPage->GetOutputDirectoryForDisplay()))
			.FolderPath_Lambda([this]() -> FString
			{
				return RenderPage->GetOutputDirectory();
			})
			.OnTextCommitted_Lambda([this](const FText& InLabel, ETextCommit::Type InCommitInfo) -> FText
			{
				RenderPage->SetOutputDirectory(InLabel.ToString());
				if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
				{
					BlueprintEditor->MarkAsModified();
				}
				return FText::FromString(RenderPage->GetOutputDirectoryForDisplay());
			});
	}
	else if (ColumnName == FRenderPagesCollectionColumns::RenderPreset)
	{
		return SNew(SObjectPropertyEntryBox)
			.AllowedClass(UMoviePipelineMasterConfig::StaticClass())
			.ObjectPath_Lambda([this]()-> FString
			{
				if (UMoviePipelineMasterConfig* Preset = RenderPage->GetRenderPreset(); IsValid(Preset))
				{
					return Preset->GetPathName();
				}
				return FString();
			})
			.OnObjectChanged_Lambda([this](const FAssetData& AssetData) -> void
			{
				RenderPage->SetRenderPreset(nullptr);
				if (UObject* AssetDataAsset = AssetData.GetAsset(); IsValid(AssetDataAsset))
				{
					if (UMoviePipelineMasterConfig* Preset = Cast<UMoviePipelineMasterConfig>(AssetDataAsset))
					{
						RenderPage->SetRenderPreset(Preset);
					}
				}
				if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
				{
					BlueprintEditor->MarkAsModified();
					BlueprintEditor->OnRenderPagesChanged().Broadcast();
				}
			})
			.AllowClear(true)
			.DisplayUseSelected(true)
			.DisplayBrowse(true)
			.DisplayThumbnail(false);
	}
	else if ((ColumnName == FRenderPagesCollectionColumns::StartFrame) || (ColumnName == FRenderPagesCollectionColumns::EndFrame))
	{
		FText Text;
		if (TOptional<int32> Frame = ((ColumnName == FRenderPagesCollectionColumns::StartFrame) ? RenderPage->GetStartFrame() : RenderPage->GetEndFrame()))
		{
			Text = FText::AsNumber(*Frame);
		}
		return SNew(SBox)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			[
				SNew(STextBlock).Text(Text)
			];
	}
	else if (ColumnName == FRenderPagesCollectionColumns::Tags)
	{
		//TODO: add support for tags
	}
	else if (ColumnName == FRenderPagesCollectionColumns::Duration)
	{
		FText Text;
		if (TOptional<double> Duration = RenderPage->GetDurationInSeconds())
		{
			FTimespan Timespan = FTimespan::FromSeconds(*Duration);
			int32 Hours = static_cast<int32>(Timespan.GetTotalHours());
			int32 Minutes = Timespan.GetMinutes();
			int32 Seconds = Timespan.GetSeconds();

			FNumberFormattingOptions NumberFormattingOptions;
			NumberFormattingOptions.MinimumIntegralDigits = 2;
			NumberFormattingOptions.MaximumIntegralDigits = 2;

			FText TimespanFormatPattern = NSLOCTEXT("Timespan", "Format_HoursMinutesSeconds", "{Hours}:{Minutes}:{Seconds}");
			FFormatNamedArguments TimeArguments;
			TimeArguments.Add(TEXT("Hours"), Hours);
			TimeArguments.Add(TEXT("Minutes"), FText::AsNumber(Minutes, &NumberFormattingOptions));
			TimeArguments.Add(TEXT("Seconds"), FText::AsNumber(Seconds, &NumberFormattingOptions));

			Text = FText::Format(TimespanFormatPattern, TimeArguments);
		}
		return SNew(SBox)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Text)
			];
	}
	else if (ColumnName == FRenderPagesCollectionColumns::RenderingStatus)
	{
		return SNew(SBox)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() -> FText
				{
					return GetRenderStatusText();
				})
			];
	}
	return SNullWidget::NullWidget;
}

FText UE::RenderPages::Private::SRenderPagesPageListTableRow::GetRenderStatusText() const
{
	if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		if (URenderPagesMoviePipelineRenderJob* RenderJob = BlueprintEditor->GetBatchRenderJob(); IsValid(RenderJob))
		{
			return FText::FromString(RenderJob->GetPageStatus(RenderPage));
		}
	}
	return FText();
}


#undef LOCTEXT_NAMESPACE
