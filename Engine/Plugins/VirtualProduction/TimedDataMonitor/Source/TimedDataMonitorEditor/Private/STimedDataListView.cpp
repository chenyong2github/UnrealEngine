// Copyright Epic Games, Inc. All Rights Reserved.

#include "STimedDataListView.h"

#include "Engine/Engine.h"
#include "ITimedDataInput.h"
#include "Misc/App.h"
#include "TimedDataMonitorSubsystem.h"

#include "EditorFontGlyphs.h"
#include "EditorStyleSet.h"

#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SExpanderArrow.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/TimedDataMonitorStyle.h"


#define LOCTEXT_NAMESPACE "STimedDataListView"


namespace TimedDataListView
{
	const FName HeaderIdName_Enable			= "Enable";
	const FName HeaderIdName_Icon			= "Edit";
	const FName HeaderIdName_Name			= "Name";
	const FName HeaderIdName_Description	= "Description";
	const FName HeaderIdName_TimeCorrection	= "TimeCorrection";
	const FName HeaderIdName_BufferSize		= "BufferSize";
	const FName HeaderIdName_BufferUnder	= "BufferUnder";
	const FName HeaderIdName_BufferOver		= "BufferOver";
	const FName HeaderIdName_FrameDrop		= "FrameDrop";
	const FName HeaderIdName_TimingDiagram	= "TimingDiagram";
}

/**
 * FTimedDataTableRowData
 */
struct FTimedDataInputTableRowData : TSharedFromThis<FTimedDataInputTableRowData>
{
	FTimedDataInputTableRowData(const FTimedDataMonitorGroupIdentifier& InGroup)
		: Group(InGroup), bIsInput(false)
	{
		UTimedDataMonitorSubsystem* TimedDataMonitorSubsystem = GEngine->GetEngineSubsystem<UTimedDataMonitorSubsystem>();
		check(TimedDataMonitorSubsystem);
		if (ITimedDataInputGroup* DataGroup = TimedDataMonitorSubsystem->GetTimedDataInputGroup(InGroup))
		{
			DisplayName = DataGroup->GetDisplayName();
			Icon = DataGroup->GetDisplayIcon();
		}
		else
		{
			// Name of the "default group"
			DisplayName = LOCTEXT("DefaultGroupName", "Other");
		}
	}
	FTimedDataInputTableRowData(const FTimedDataMonitorInputIdentifier& InInput)
		: Input(InInput), bIsInput(true)
	{
		UTimedDataMonitorSubsystem* TimedDataMonitorSubsystem = GEngine->GetEngineSubsystem<UTimedDataMonitorSubsystem>();
		check(TimedDataMonitorSubsystem);
		Group = TimedDataMonitorSubsystem->GetInputGroup(InInput);

		if (ITimedDataInput* DataInput = TimedDataMonitorSubsystem->GetTimedDataInput(InInput))
		{
			DisplayName = DataInput->GetDisplayName();
		}
	}

	void UpdateCachedValue()
	{
		UTimedDataMonitorSubsystem* TimedDataMonitorSubsystem = GEngine->GetEngineSubsystem<UTimedDataMonitorSubsystem>();
		check(TimedDataMonitorSubsystem);
		if (bIsInput)
		{
			CachedIsEnabled = TimedDataMonitorSubsystem->IsInputEnabled(Input);
			CachedBufferSize = TimedDataMonitorSubsystem->GetInputDataBufferSize(Input);
		}
		else
		{
			CachedIsEnabled = TimedDataMonitorSubsystem->IsGroupEnabled(Group);
			TimedDataMonitorSubsystem->GetGroupDataBufferSize(Group, CachedBufferSize, CachedBufferSizeMax);

			for (FTimedDataInputTableRowDataPtr& Child : Children)
			{
				Child->UpdateCachedValue();
			}
		}
	}

public:
	FTimedDataMonitorGroupIdentifier Group;
	FTimedDataMonitorInputIdentifier Input;
	bool bIsInput;

	FText DisplayName;
	const FSlateBrush* Icon = nullptr;
	TArray<FTimedDataInputTableRowDataPtr> Children;

	bool CachedIsEnabled = true;
	int32 CachedBufferSize = 0;
	int32 CachedBufferSizeMax = 0;
};


/**
 * STimedDataInputTableRow
 */
void STimedDataInputTableRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	Item = InArgs._Item;
	check(Item.IsValid());
	Super::FArguments Arg;

	if (Item->bIsInput)
	{
		Arg.Style(FTimedDataMonitorStyle::Get(), "TableView.Child");
	}
	else
	{
		Arg.Style(&FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row"));
	}
	Super::Construct(Arg, InOwnerTableView);
}


TSharedRef<SWidget> STimedDataInputTableRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	const FTextBlockStyle* ItemTextBlockStyle = !Item->bIsInput
		? &FTimedDataMonitorStyle::Get().GetWidgetStyle<FTextBlockStyle>("TextBlock.Large")
		: &FTimedDataMonitorStyle::Get().GetWidgetStyle<FTextBlockStyle>("TextBlock.Regular");

	if (TimedDataListView::HeaderIdName_Enable == ColumnName)
	{
		const FText Tooltip = Item->bIsInput
			? LOCTEXT("EnabledInputToolTip", "Toggles whether this input will collect stats and used when calibrating.")
			: LOCTEXT("EnabledGroupToolTip", "Toggles all inputs from this group.");
		return SNew(SCheckBox)
			.Style(FTimedDataMonitorStyle::Get(), "CheckBox.Enable")
			.ToolTipText(Tooltip)
			.IsChecked(this, &STimedDataInputTableRow::GetEnabledCheckState)
			.OnCheckStateChanged(this, &STimedDataInputTableRow::OnEnabledCheckStateChanged);
	}
	if (TimedDataListView::HeaderIdName_Icon == ColumnName)
	{
		if (!Item->bIsInput)
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(6, 0, 0, 0)
				[
					SNew(SExpanderArrow, SharedThis(this))
					.ShouldDrawWires(false)
					.IndentAmount(12)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SImage)
					.Image(Item->Icon)
				];
		}
		return SNullWidget::NullWidget;
	}
	if (TimedDataListView::HeaderIdName_Name == ColumnName)
	{
		return SNew(STextBlock)
			.Text(Item->DisplayName)
			.TextStyle(ItemTextBlockStyle);
	}
	if (TimedDataListView::HeaderIdName_Description == ColumnName)
	{
		//@todo put description
		return SNew(STextBlock)
			.Text(LOCTEXT("Tmp1", "[PH]10:11:12:13@24"))
			.TextStyle(ItemTextBlockStyle);
	}
	if (TimedDataListView::HeaderIdName_TimeCorrection == ColumnName)
	{
		//@todo put offset widget
		if (Item->bIsInput)
		{
			return SNew(STextBlock)
				.Text(LOCTEXT("Tmp3", "[PH]Offset widget"))
				.TextStyle(ItemTextBlockStyle);
		}
		return SNullWidget::NullWidget;
	}
	if (TimedDataListView::HeaderIdName_BufferSize == ColumnName)
	{
		//@todo put proper editing widget
		return SNew(SNumericEntryBox<int32>)
				.ToolTipText(LOCTEXT("BufferSize_ToolTip", "Buffer Size."))
				.MinValue(1)
				.MinDesiredValueWidth(50)
				.Value(this, &STimedDataInputTableRow::GetBufferSize)
				.OnValueCommitted(this, &STimedDataInputTableRow::SetBufferSize)
				.IsEnabled(this, &STimedDataInputTableRow::CanEditBufferSize);
	}
	if (TimedDataListView::HeaderIdName_BufferUnder == ColumnName)
	{
		//@todo put proper stat
		return SNew(STextBlock)
			.Text(LOCTEXT("Tmp2", "112"))
			.TextStyle(ItemTextBlockStyle);
	}
	if (TimedDataListView::HeaderIdName_BufferOver == ColumnName)
	{
		//@todo put proper stat
		return SNew(STextBlock)
			.Text(LOCTEXT("Tmp2", "112"))
			.TextStyle(ItemTextBlockStyle);
	}
	if (TimedDataListView::HeaderIdName_FrameDrop == ColumnName)
	{
		//@todo put proper stat
		return SNew(STextBlock)
			.Text(LOCTEXT("Tmp2", "112"))
			.TextStyle(ItemTextBlockStyle);
	}
	if (TimedDataListView::HeaderIdName_TimingDiagram == ColumnName)
	{
		//@todo put proper timing diagram
		return SNew(SBorder)
			.BorderImage(FTimedDataMonitorStyle::Get().GetBrush("Brush.White"))
			.BorderBackgroundColor(FLinearColor::Green);
	}

	return SNullWidget::NullWidget;
}


ECheckBoxState STimedDataInputTableRow::GetEnabledCheckState() const
{
	UTimedDataMonitorSubsystem* TimedDataMonitorSubsystem = GEngine->GetEngineSubsystem<UTimedDataMonitorSubsystem>();
	check(TimedDataMonitorSubsystem);

	if (Item->bIsInput)
	{
		return TimedDataMonitorSubsystem->IsInputEnabled(Item->Input) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	else
	{
		return TimedDataMonitorSubsystem->IsGroupEnabled(Item->Group) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
}


void STimedDataInputTableRow::OnEnabledCheckStateChanged(ECheckBoxState NewState)
{
	UTimedDataMonitorSubsystem* TimedDataMonitorSubsystem = GEngine->GetEngineSubsystem<UTimedDataMonitorSubsystem>();
	check(TimedDataMonitorSubsystem);

	if (Item->bIsInput)
	{
		TimedDataMonitorSubsystem->SetInputEnabled(Item->Input, NewState == ECheckBoxState::Checked);
	}
	else
	{
		TimedDataMonitorSubsystem->SetGroupEnabled(Item->Group, NewState == ECheckBoxState::Checked);
	}
	Item->UpdateCachedValue();
}


TOptional<int32> STimedDataInputTableRow::GetBufferSize() const
{
	if (Item->bIsInput)
	{
		return Item->CachedBufferSize;
	}
	else
	{
		if (Item->CachedBufferSize == Item->CachedBufferSizeMax)
		{
			return Item->CachedBufferSize;
		}
	}
	return TOptional<int32>();
}


void STimedDataInputTableRow::SetBufferSize(int32 InValue, ETextCommit::Type InType)
{
	if (InType == ETextCommit::OnEnter || InType == ETextCommit::OnUserMovedFocus)
	{
		UTimedDataMonitorSubsystem* TimedDataMonitorSubsystem = GEngine->GetEngineSubsystem<UTimedDataMonitorSubsystem>();
		check(TimedDataMonitorSubsystem);

		if (Item->bIsInput)
		{
			TimedDataMonitorSubsystem->SetInputDataBufferSize(Item->Input, InValue);
		}
		else
		{
			TimedDataMonitorSubsystem->SetGroupDataBufferSize(Item->Group, InValue);
		}
		Item->UpdateCachedValue();
	}
}


bool STimedDataInputTableRow::CanEditBufferSize() const
{
	return Item->CachedIsEnabled;
}


/**
 * STimedDataListView
 */
void STimedDataInputListView::Construct(const FArguments& InArgs)
{
	UTimedDataMonitorSubsystem* TimedDataMonitorSubsystem = GEngine->GetEngineSubsystem<UTimedDataMonitorSubsystem>();
	check(TimedDataMonitorSubsystem);
	TimedDataMonitorSubsystem->OnIdentifierListChanged().AddSP(this, &STimedDataInputListView::RequestRebuildSources);

	Super::Construct
	(
		Super::FArguments()
		.TreeItemsSource(&ListItemsSource)
		.SelectionMode(ESelectionMode::SingleToggle)
		.OnGenerateRow(this, &STimedDataInputListView::OnGenerateRow)
		.OnGetChildren(this, &STimedDataInputListView::GetChildrenForInfo)
		.OnSelectionChanged(this, &STimedDataInputListView::OnSelectionChanged)
		.OnIsSelectableOrNavigable(this, &STimedDataInputListView::OnIsSelectableOrNavigable)
		.HighlightParentNodesForSelection(true)
		.HeaderRow
		(
			SNew(SHeaderRow)

			+ SHeaderRow::Column(TimedDataListView::HeaderIdName_Enable)
			.FixedWidth(32)
			.DefaultLabel(FText::GetEmpty())
			[
				SNew(SCheckBox)
				.HAlign(HAlign_Center)
				.IsChecked(this, &STimedDataInputListView::GetAllEnabledCheckState)
				.OnCheckStateChanged(this, &STimedDataInputListView::OnToggleAllEnabledCheckState)
			]

			+ SHeaderRow::Column(TimedDataListView::HeaderIdName_Icon)
			.FixedWidth(32)
			.HAlignCell(EHorizontalAlignment::HAlign_Center)
			.VAlignCell(EVerticalAlignment::VAlign_Center)
			.DefaultLabel(FText::GetEmpty())

			+ SHeaderRow::Column(TimedDataListView::HeaderIdName_Name)
			.FillWidth(75)
			.HAlignCell(EHorizontalAlignment::HAlign_Left)
			.DefaultLabel(LOCTEXT("HeaderName_Name", "Name"))

			+ SHeaderRow::Column(TimedDataListView::HeaderIdName_Description)
			.FillWidth(100)
			.HAlignCell(EHorizontalAlignment::HAlign_Left)
			.DefaultLabel(LOCTEXT("HeaderName_Description", "Description"))

			+ SHeaderRow::Column(TimedDataListView::HeaderIdName_TimeCorrection)
			.FillWidth(100)
			.HAlignCell(EHorizontalAlignment::HAlign_Left)
			.DefaultLabel(LOCTEXT("HeaderName_TimeCorrection", "Time Correction"))

			+ SHeaderRow::Column(TimedDataListView::HeaderIdName_BufferSize)
			.FillWidth(15)
			.HAlignCell(EHorizontalAlignment::HAlign_Left)
			.DefaultLabel(LOCTEXT("HeaderName_BufferSize", "B. Size"))

			+ SHeaderRow::Column(TimedDataListView::HeaderIdName_BufferUnder)
			.FillWidth(10)
			.HAlignCell(EHorizontalAlignment::HAlign_Left)
			.DefaultLabel(LOCTEXT("HeaderName_BufferUnder", "B.U."))

			+ SHeaderRow::Column(TimedDataListView::HeaderIdName_BufferOver)
			.FillWidth(10)
			.HAlignCell(EHorizontalAlignment::HAlign_Left)
			.DefaultLabel(LOCTEXT("HeaderName_BufferOver", "B.O."))

			+ SHeaderRow::Column(TimedDataListView::HeaderIdName_FrameDrop)
			.FillWidth(10)
			.HAlignCell(EHorizontalAlignment::HAlign_Left)
			.DefaultLabel(LOCTEXT("HeaderName_FrameDrop", "F.D."))

			+ SHeaderRow::Column(TimedDataListView::HeaderIdName_TimingDiagram)
			.FillWidth(75)
			.HAlignCell(EHorizontalAlignment::HAlign_Left)
			.DefaultLabel(LOCTEXT("HeaderName_TimingDiagram", "Timing Diagram"))
		)
	);
}


STimedDataInputListView::~STimedDataInputListView()
{
	UTimedDataMonitorSubsystem* TimedDataMonitorSubsystem = GEngine->GetEngineSubsystem<UTimedDataMonitorSubsystem>();
	if (TimedDataMonitorSubsystem)
	{
		TimedDataMonitorSubsystem->OnIdentifierListChanged().RemoveAll(this);
	}
}


void STimedDataInputListView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	Super::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (bRebuildListRequested)
	{
		RebuildSources();
		RebuildList();
		bRebuildListRequested = false;
	}

	double RefreshTimer = 0.5; //todo: move to setting
	if (FApp::GetCurrentTime() - LastCachedValueUpdateTime > RefreshTimer)
	{
		LastCachedValueUpdateTime = FApp::GetCurrentTime();
		for (FTimedDataInputTableRowDataPtr& RowDataPtr : ListItemsSource)
		{
			RowDataPtr->UpdateCachedValue();
		}
	}
}


void STimedDataInputListView::RequestRebuildSources()
{
	bRebuildListRequested = true;
}


void STimedDataInputListView::RebuildSources()
{
	ListItemsSource.Reset();

	UTimedDataMonitorSubsystem* TimedDataMonitorSubsystem = GEngine->GetEngineSubsystem<UTimedDataMonitorSubsystem>();
	check(TimedDataMonitorSubsystem);

	TMap<FTimedDataMonitorGroupIdentifier, TSharedRef<FTimedDataInputTableRowData>> GroupMap;
	{
		TArray<FTimedDataMonitorGroupIdentifier> Groups = TimedDataMonitorSubsystem->GetAllGroups();
		for (const FTimedDataMonitorGroupIdentifier& Identifier : Groups)
		{
			TSharedRef<FTimedDataInputTableRowData> ParentRowData = MakeShared<FTimedDataInputTableRowData>(Identifier);
			ListItemsSource.Add(ParentRowData);
			GroupMap.Add(Identifier, ParentRowData);
		}
	}

	{
		TArray<FTimedDataMonitorInputIdentifier> Inputs = TimedDataMonitorSubsystem->GetAllInputs();
		for (const FTimedDataMonitorInputIdentifier& Identifier : Inputs)
		{
			TSharedRef<FTimedDataInputTableRowData> ChildRowData = MakeShared<FTimedDataInputTableRowData>(Identifier);

			FTimedDataMonitorGroupIdentifier InputGroupIdentifier = TimedDataMonitorSubsystem->GetInputGroup(Identifier);
			if (TSharedRef<FTimedDataInputTableRowData>* FoundParentRowData = GroupMap.Find(InputGroupIdentifier))
			{
				(*FoundParentRowData)->Children.Add(ChildRowData);
			}
			else
			{
				// there is no group, create an empty one
				check(!TimedDataMonitorSubsystem->DoesGroupExist(InputGroupIdentifier));
					
				TSharedRef<FTimedDataInputTableRowData> ParentRowData = MakeShared<FTimedDataInputTableRowData>(InputGroupIdentifier);
				ParentRowData->Children.Add(ChildRowData);
				ListItemsSource.Add(ParentRowData);
				GroupMap.Add(InputGroupIdentifier, ParentRowData);
			}
		}
	}


	for (FTimedDataInputTableRowDataPtr& TableRowData : ListItemsSource)
	{
		TableRowData->UpdateCachedValue();
	}

	RequestTreeRefresh();
}


ECheckBoxState STimedDataInputListView::GetAllEnabledCheckState() const
{
	return ECheckBoxState::Checked;
}


void STimedDataInputListView::OnToggleAllEnabledCheckState(ECheckBoxState CheckBoxState)
{

}


TSharedRef<ITableRow> STimedDataInputListView::OnGenerateRow(FTimedDataInputTableRowDataPtr InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STimedDataInputTableRow, OwnerTable)
		.Item(InItem);
}


void STimedDataInputListView::GetChildrenForInfo(FTimedDataInputTableRowDataPtr InItem, TArray<FTimedDataInputTableRowDataPtr>& OutChildren)
{
	OutChildren = InItem->Children;
}


void STimedDataInputListView::OnSelectionChanged(FTimedDataInputTableRowDataPtr InItem, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		if (InItem && InItem->bIsInput)
		{
			ClearSelection();
		}
	}
}


bool STimedDataInputListView::OnIsSelectableOrNavigable(FTimedDataInputTableRowDataPtr InItem) const
{
	return InItem && !InItem->bIsInput;
}

#undef LOCTEXT_NAMESPACE