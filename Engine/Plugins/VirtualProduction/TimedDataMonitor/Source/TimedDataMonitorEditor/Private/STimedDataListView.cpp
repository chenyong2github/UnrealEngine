// Copyright Epic Games, Inc. All Rights Reserved.

#include "STimedDataListView.h"

#include "Engine/Engine.h"
#include "ITimedDataInput.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/Timecode.h"
#include "Misc/Timespan.h"
#include "TimedDataMonitorEditorSettings.h"
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

	FTimespan FromPlatformSeconds(double InPlatformSeconds)
	{
		const FDateTime NowDateTime = FDateTime::Now();
		const double HighPerformanceClock = FPlatformTime::Seconds();
		const double DateTimeSeconds = (InPlatformSeconds - HighPerformanceClock) + NowDateTime.GetTimeOfDay().GetTotalSeconds();
		return FTimespan::FromSeconds(DateTimeSeconds);
	}
}

/**
 * FTimedDataTableRowData
 */
struct FTimedDataInputTableRowData : TSharedFromThis<FTimedDataInputTableRowData>
{
	FTimedDataInputTableRowData(const FTimedDataMonitorInputIdentifier& InInputId)
		: InputIdentifer(InInputId), bIsInput(true)
	{
		UTimedDataMonitorSubsystem* TimedDataMonitorSubsystem = GEngine->GetEngineSubsystem<UTimedDataMonitorSubsystem>();
		check(TimedDataMonitorSubsystem);

		DisplayName = TimedDataMonitorSubsystem->GetInputDisplayName(InputIdentifer);

		if (ITimedDataInput* InpuTimedData = TimedDataMonitorSubsystem->GetTimedDataInput(InputIdentifer))
		{
			InputIcon = InpuTimedData->GetDisplayIcon();
		}
	}

	FTimedDataInputTableRowData(const FTimedDataMonitorChannelIdentifier& InChannelId)
		: ChannelIdentifier(InChannelId), bIsInput(false)
	{
		UTimedDataMonitorSubsystem* TimedDataMonitorSubsystem = GEngine->GetEngineSubsystem<UTimedDataMonitorSubsystem>();
		check(TimedDataMonitorSubsystem);

		InputIdentifer = TimedDataMonitorSubsystem->GetChannelInput(ChannelIdentifier);
		DisplayName = TimedDataMonitorSubsystem->GetChannelDisplayName(ChannelIdentifier);
	}

	void UpdateCachedValue()
	{
		UTimedDataMonitorSubsystem* TimedDataMonitorSubsystem = GEngine->GetEngineSubsystem<UTimedDataMonitorSubsystem>();
		check(TimedDataMonitorSubsystem);

		FTimedDataChannelSampleTime NewestDataTime;
		if (bIsInput)
		{
			switch (TimedDataMonitorSubsystem->GetInputEnabled(InputIdentifer))
			{
			case ETimedDataMonitorInputEnabled::Enabled:
				CachedEnabled = ECheckBoxState::Checked;
				break;
			case ETimedDataMonitorInputEnabled::Disabled:
				CachedEnabled = ECheckBoxState::Unchecked;
				break;
			case ETimedDataMonitorInputEnabled::MultipleValues:
			default:
				CachedEnabled = ECheckBoxState::Undetermined;
				break;
			};

			CachedInputEvaluationType = TimedDataMonitorSubsystem->GetInputEvaluationType(InputIdentifer);
			CachedInputEvaluationOffset = TimedDataMonitorSubsystem->GetInputEvaluationOffsetInSeconds(InputIdentifer);
			CachedState = TimedDataMonitorSubsystem->GetInputState(InputIdentifer);
			CachedBufferSize = TimedDataMonitorSubsystem->GetInputDataBufferSize(InputIdentifer);
			bCachedCanEditBufferSize = (CachedEnabled == ECheckBoxState::Checked || CachedEnabled == ECheckBoxState::Undetermined) && TimedDataMonitorSubsystem->IsDataBufferSizeControlledByInput(InputIdentifer);

			NewestDataTime = TimedDataMonitorSubsystem->GetInputNewestDataTime(InputIdentifer);

			CachedStatsBufferUnderflow = 0;
			CachedStatsBufferOverflow = 0;
			CachedStatsFrameDropped = 0;

			for (FTimedDataInputTableRowDataPtr& Child : InputChildren)
			{
				Child->UpdateCachedValue();

				//Update the group stats here to simplify the queries
				CachedStatsBufferUnderflow = FMath::Max(CachedStatsBufferUnderflow, Child->CachedStatsBufferUnderflow);
				CachedStatsBufferOverflow = FMath::Max(CachedStatsBufferOverflow, Child->CachedStatsBufferOverflow);
				CachedStatsFrameDropped += Child->CachedStatsFrameDropped;
			}
		}
		else
		{
			CachedEnabled = TimedDataMonitorSubsystem->IsChannelEnabled(ChannelIdentifier) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			CachedInputEvaluationType = TimedDataMonitorSubsystem->GetInputEvaluationType(InputIdentifer);
			CachedInputEvaluationOffset = 0.f;
			CachedState = TimedDataMonitorSubsystem->GetChannelState(ChannelIdentifier);
			CachedBufferSize = TimedDataMonitorSubsystem->GetChannelNumberOfSamples(ChannelIdentifier);
			CachedStatsBufferUnderflow = TimedDataMonitorSubsystem->GetChannelBufferUnderflowStat(ChannelIdentifier);
			CachedStatsBufferOverflow = TimedDataMonitorSubsystem->GetChannelBufferOverflowStat(ChannelIdentifier);
			CachedStatsFrameDropped = TimedDataMonitorSubsystem->GetChannelFrameDroppedStat(ChannelIdentifier);
			bCachedCanEditBufferSize = (CachedEnabled == ECheckBoxState::Checked || CachedEnabled == ECheckBoxState::Undetermined) && !TimedDataMonitorSubsystem->IsDataBufferSizeControlledByInput(InputIdentifer);

			NewestDataTime = TimedDataMonitorSubsystem->GetChannelNewestDataTime(ChannelIdentifier);
		}

		if (CachedEnabled == ECheckBoxState::Checked)
		{
			switch (CachedInputEvaluationType)
			{
			case ETimedDataInputEvaluationType::Timecode:
			{
				FTimecode Timecode = FTimecode::FromFrameNumber(NewestDataTime.Timecode.Time.GetFrame(), NewestDataTime.Timecode.Rate);
				CachedDescription = FText::Format(LOCTEXT("TimecodeDescription", "{0}@{1}"), FText::FromString(Timecode.ToString()), NewestDataTime.Timecode.Rate.ToPrettyText());
			}
			break;
			case ETimedDataInputEvaluationType::PlatformTime:
			{
				FTimespan PlatformSecond = TimedDataListView::FromPlatformSeconds(NewestDataTime.PlatformSecond);
				CachedDescription = FText::FromString(PlatformSecond.ToString());
			}
			break;
			case ETimedDataInputEvaluationType::None:
			default:
				CachedDescription = FText::GetEmpty();
				break;
			}
		}
	}

public:
	FTimedDataMonitorInputIdentifier InputIdentifer;
	FTimedDataMonitorChannelIdentifier ChannelIdentifier;
	bool bIsInput;

	FText DisplayName;
	const FSlateBrush* InputIcon = nullptr;
	TArray<FTimedDataInputTableRowDataPtr> InputChildren;

	ECheckBoxState CachedEnabled = ECheckBoxState::Undetermined;
	ETimedDataInputEvaluationType CachedInputEvaluationType = ETimedDataInputEvaluationType::None;
	float CachedInputEvaluationOffset = 0.f;
	ETimedDataInputState CachedState = ETimedDataInputState::Disconnected;
	FText CachedDescription;
	int32 CachedBufferSize = 0;
	int32 CachedStatsBufferUnderflow = 0;
	int32 CachedStatsBufferOverflow = 0;
	int32 CachedStatsFrameDropped = 0;
	bool bCachedCanEditBufferSize = false;
};


/**
 * STimedDataInputTableRow
 */
void STimedDataInputTableRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwerTableView, const TSharedRef<STimedDataInputListView>& InOwnerTreeView)
{
	Item = InArgs._Item;
	OwnerTreeView = InOwnerTreeView;
	check(Item.IsValid());

	Super::FArguments Arg;

	if (Item->bIsInput)
	{
		Arg.Style(&FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row"));
	}
	else
	{
		Arg.Style(FTimedDataMonitorStyle::Get(), "TableView.Child");
	}
	Super::Construct(Arg, InOwerTableView);
}


TSharedRef<SWidget> STimedDataInputTableRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	const FTextBlockStyle* ItemTextBlockStyle = Item->bIsInput
		? &FTimedDataMonitorStyle::Get().GetWidgetStyle<FTextBlockStyle>("TextBlock.Large")
		: &FTimedDataMonitorStyle::Get().GetWidgetStyle<FTextBlockStyle>("TextBlock.Regular");

	if (TimedDataListView::HeaderIdName_Enable == ColumnName)
	{
		const FText Tooltip = Item->bIsInput
			? LOCTEXT("EnabledChannelToolTip", "Toggles all channels from this input.")
			: LOCTEXT("EnabledChannelToolTip", "Toggles whether this channel will collect stats and be used when calibrating.");
		return SNew(SCheckBox)
			.Style(FTimedDataMonitorStyle::Get(), "CheckBox.Enable")
			.ToolTipText(Tooltip)
			.IsChecked(this, &STimedDataInputTableRow::GetEnabledCheckState)
			.OnCheckStateChanged(this, &STimedDataInputTableRow::OnEnabledCheckStateChanged);
	}
	if (TimedDataListView::HeaderIdName_Icon == ColumnName)
	{
		if (Item->bIsInput)
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
					.Image(Item->InputIcon)
				];
		}
		return SNullWidget::NullWidget;
	}
	if (TimedDataListView::HeaderIdName_Name == ColumnName)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(10, 0, 10, 0)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.11"))
				.Text(this, &STimedDataInputTableRow::GetStateGlyphs)
				.ColorAndOpacity(this, &STimedDataInputTableRow::GetStateColorAndOpacity)
			]
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Text(Item->DisplayName)
				.TextStyle(ItemTextBlockStyle)
			];
	}
	if (TimedDataListView::HeaderIdName_Description == ColumnName)
	{
		return SNew(STextBlock)
			.Text(this, &STimedDataInputTableRow::GetDescription)
			.TextStyle(ItemTextBlockStyle);
	}
	if (TimedDataListView::HeaderIdName_TimeCorrection == ColumnName)
	{
		if (Item->bIsInput)
		{
			return SNew(STextBlock)
				.TextStyle(ItemTextBlockStyle)
				.Text(this, &STimedDataInputTableRow::GetEvaluationOffsetText);
		}
		return SNullWidget::NullWidget;
	}
	if (TimedDataListView::HeaderIdName_BufferSize == ColumnName)
	{
		//@todo put proper editing widget
		if (Item->bIsInput) // bCachedCanEditBufferSize
		{
			return SNew(SNumericEntryBox<int32>)
				.ToolTipText(LOCTEXT("BufferSize_ToolTip", "Buffer Size."))
				.MinValue(1)
				.MinDesiredValueWidth(50)
				.Value(this, &STimedDataInputTableRow::GetBufferSize)
				.OnValueCommitted(this, &STimedDataInputTableRow::SetBufferSize)
				.IsEnabled(this, &STimedDataInputTableRow::CanEditBufferSize);
		}
		else
		{
			return SNew(STextBlock)
				.TextStyle(ItemTextBlockStyle)
				.Text(this, &STimedDataInputTableRow::GetBufferSizeText);
		}
	}
	if (TimedDataListView::HeaderIdName_BufferUnder == ColumnName)
	{
		return SNew(STextBlock)
			.Text(this, &STimedDataInputTableRow::GetBufferUnderflowCount)
			.TextStyle(ItemTextBlockStyle);
	}
	if (TimedDataListView::HeaderIdName_BufferOver == ColumnName)
	{
		return SNew(STextBlock)
			.Text(this, &STimedDataInputTableRow::GetBufferOverflowCount)
			.TextStyle(ItemTextBlockStyle);
	}
	if (TimedDataListView::HeaderIdName_FrameDrop == ColumnName)
	{
		return SNew(STextBlock)
			.Text(this, &STimedDataInputTableRow::GetFrameDroppedCount)
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
	return Item->CachedEnabled;
}


void STimedDataInputTableRow::OnEnabledCheckStateChanged(ECheckBoxState NewState)
{
	UTimedDataMonitorSubsystem* TimedDataMonitorSubsystem = GEngine->GetEngineSubsystem<UTimedDataMonitorSubsystem>();
	check(TimedDataMonitorSubsystem);

	if (Item->bIsInput)
	{
		TimedDataMonitorSubsystem->SetInputEnabled(Item->InputIdentifer, NewState == ECheckBoxState::Checked);
	}
	else
	{
		TimedDataMonitorSubsystem->SetChannelEnabled(Item->ChannelIdentifier, NewState == ECheckBoxState::Checked);
	}
	OwnerTreeView->RequestRefresh();
}


FText STimedDataInputTableRow::GetStateGlyphs() const
{
	return (Item->CachedEnabled == ECheckBoxState::Checked) ?  FEditorFontGlyphs::Circle :  FEditorFontGlyphs::Circle_O;
}


FSlateColor STimedDataInputTableRow::GetStateColorAndOpacity() const
{
	if (Item->CachedEnabled != ECheckBoxState::Unchecked)
	{
		switch (Item->CachedState)
		{
		case ETimedDataInputState::Connected:
			return FLinearColor::Green;
		case ETimedDataInputState::Disconnected:
			return FLinearColor::Red;
		case ETimedDataInputState::Unresponsive:
			return FLinearColor::Yellow;
		}
	}

	return FSlateColor::UseForeground();
}


FText STimedDataInputTableRow::GetDescription() const
{
	return Item->CachedDescription;
}


FText STimedDataInputTableRow::GetEvaluationOffsetText() const
{
	if (Item->bIsInput)
	{
		return FText::AsNumber(Item->CachedInputEvaluationOffset);
	}
	return FText::GetEmpty();
}


TOptional<int32> STimedDataInputTableRow::GetBufferSize() const
{
	return Item->CachedBufferSize;
}


FText STimedDataInputTableRow::GetBufferSizeText() const
{
	return FText::AsNumber(Item->CachedBufferSize);
}


void STimedDataInputTableRow::SetBufferSize(int32 InValue, ETextCommit::Type InType)
{
	if (InType == ETextCommit::OnEnter || InType == ETextCommit::OnUserMovedFocus)
	{
		UTimedDataMonitorSubsystem* TimedDataMonitorSubsystem = GEngine->GetEngineSubsystem<UTimedDataMonitorSubsystem>();
		check(TimedDataMonitorSubsystem);

		if (Item->bIsInput)
		{
			TimedDataMonitorSubsystem->SetInputDataBufferSize(Item->InputIdentifer, InValue);
		}
		else
		{
			TimedDataMonitorSubsystem->SetChannelDataBufferSize(Item->ChannelIdentifier, InValue);
		}
		OwnerTreeView->RequestRefresh();
	}
}


bool STimedDataInputTableRow::CanEditBufferSize() const
{
	return Item->bCachedCanEditBufferSize;
}


FText STimedDataInputTableRow::GetBufferUnderflowCount() const
{
	return FText::AsNumber(Item->CachedStatsBufferUnderflow);
}

FText STimedDataInputTableRow::GetBufferOverflowCount() const
{
	return FText::AsNumber(Item->CachedStatsBufferOverflow);
}

FText STimedDataInputTableRow::GetFrameDroppedCount() const
{
	return FText::AsNumber(Item->CachedStatsFrameDropped);
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
			.FillWidth(0.33)
			.HAlignCell(EHorizontalAlignment::HAlign_Left)
			.DefaultLabel(LOCTEXT("HeaderName_Name", "Name"))

			+ SHeaderRow::Column(TimedDataListView::HeaderIdName_Description)
			.FillWidth(0.33)
			.HAlignCell(EHorizontalAlignment::HAlign_Left)
			.DefaultLabel(LOCTEXT("HeaderName_Description", "Description"))

			+ SHeaderRow::Column(TimedDataListView::HeaderIdName_TimeCorrection)
			.FixedWidth(100)
			.HAlignCell(EHorizontalAlignment::HAlign_Left)
			.DefaultLabel(LOCTEXT("HeaderName_TimeCorrection", "Time Correction"))

			+ SHeaderRow::Column(TimedDataListView::HeaderIdName_BufferSize)
			.FixedWidth(100)
			.HAlignCell(EHorizontalAlignment::HAlign_Left)
			.DefaultLabel(LOCTEXT("HeaderName_BufferSize", "Buffer Size"))

			+ SHeaderRow::Column(TimedDataListView::HeaderIdName_BufferUnder)
			.FixedWidth(50)
			.HAlignCell(EHorizontalAlignment::HAlign_Left)
			.DefaultLabel(LOCTEXT("HeaderName_BufferUnder", "B.U."))

			+ SHeaderRow::Column(TimedDataListView::HeaderIdName_BufferOver)
			.FixedWidth(50)
			.HAlignCell(EHorizontalAlignment::HAlign_Left)
			.DefaultLabel(LOCTEXT("HeaderName_BufferOver", "B.O."))

			+ SHeaderRow::Column(TimedDataListView::HeaderIdName_FrameDrop)
			.FixedWidth(50)
			.HAlignCell(EHorizontalAlignment::HAlign_Left)
			.DefaultLabel(LOCTEXT("HeaderName_FrameDrop", "F.D."))

			+ SHeaderRow::Column(TimedDataListView::HeaderIdName_TimingDiagram)
			.FillWidth(0.33)
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

	double RefreshTimer = GetDefault<UTimedDataMonitorEditorSettings>()->RefreshRate;
	if (bRefreshRequested || (FApp::GetCurrentTime() - LastCachedValueUpdateTime > RefreshTimer))
	{
		bRefreshRequested = false;
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

	TArray<FTimedDataMonitorInputIdentifier> Inputs = TimedDataMonitorSubsystem->GetAllInputs();
	for (const FTimedDataMonitorInputIdentifier& InputIdentifier : Inputs)
	{
		TSharedRef<FTimedDataInputTableRowData> ParentRowData = MakeShared<FTimedDataInputTableRowData>(InputIdentifier);
		ListItemsSource.Add(ParentRowData);

		TArray<FTimedDataMonitorChannelIdentifier> Channels = TimedDataMonitorSubsystem->GetInputChannels(InputIdentifier);
		for (const FTimedDataMonitorChannelIdentifier& ChannelIdentifier : Channels)
		{
			TSharedRef<FTimedDataInputTableRowData> ChildRowData = MakeShared<FTimedDataInputTableRowData>(ChannelIdentifier);
			ParentRowData->InputChildren.Add(ChildRowData);
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
	UTimedDataMonitorSubsystem* TimedDataMonitorSubsystem = GEngine->GetEngineSubsystem<UTimedDataMonitorSubsystem>();
	check(TimedDataMonitorSubsystem);

	bool bIsEnabled = CheckBoxState == ECheckBoxState::Checked;
	for (const FTimedDataInputTableRowDataPtr& RowDataPtr : ListItemsSource)
	{
		TimedDataMonitorSubsystem->SetInputEnabled(RowDataPtr->InputIdentifer, bIsEnabled);
	}
}


TSharedRef<ITableRow> STimedDataInputListView::OnGenerateRow(FTimedDataInputTableRowDataPtr InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STimedDataInputTableRow, OwnerTable, SharedThis<STimedDataInputListView>(this))
		.Item(InItem);
}


void STimedDataInputListView::GetChildrenForInfo(FTimedDataInputTableRowDataPtr InItem, TArray<FTimedDataInputTableRowDataPtr>& OutChildren)
{
	OutChildren = InItem->InputChildren;
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