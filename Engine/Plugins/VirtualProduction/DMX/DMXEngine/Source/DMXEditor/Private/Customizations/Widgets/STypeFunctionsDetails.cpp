// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/Widgets/STypeFunctionsDetails.h"
#include "DMXAttribute.h"
#include "Widgets/SNameListPicker.h"
#include "Library/DMXEntityFixtureType.h"
#include "Commands/DMXEditorCommands.h"
#include "DMXFixtureTypeSharedData.h"
#include "DMXEditor.h"

#include "PropertyHandle.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Notifications/SPopUpErrorText.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Layout/SSeparator.h"
#include "ScopedTransaction.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "STypeFunctionsDetails"

/** Helper class to force a widget to fill in a space. Copied from SDetailSingleItemRow.cpp */
class SConstrainedBox
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SConstrainedBox) {}
	SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ATTRIBUTE(TOptional<float>, MinWidth)
		SLATE_ATTRIBUTE(TOptional<float>, MaxWidth)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
	{
		MinWidth = InArgs._MinWidth;
		MaxWidth = InArgs._MaxWidth;

		ChildSlot
			[
				InArgs._Content.Widget
			];
	}

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override
	{
		const float MinWidthVal = MinWidth.Get().Get(0.0f);
		const float MaxWidthVal = MaxWidth.Get().Get(0.0f);

		if (MinWidthVal == 0.0f && MaxWidthVal == 0.0f)
		{
			return SCompoundWidget::ComputeDesiredSize(LayoutScaleMultiplier);
		}
		else
		{
			FVector2D ChildSize = ChildSlot.GetWidget()->GetDesiredSize();

			float XVal = FMath::Max(MinWidthVal, ChildSize.X);
			if (MaxWidthVal >= MinWidthVal)
			{
				XVal = FMath::Min(MaxWidthVal, XVal);
			}

			return FVector2D(XVal, ChildSize.Y);
		}
	}

private:
	TAttribute< TOptional<float> > MinWidth;
	TAttribute< TOptional<float> > MaxWidth;
};

void SDMXFunctionTableRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable, TSharedPtr<FDMXFixtureFunctionItem> InItem, TSharedPtr<IPropertyHandle> InCurrentModeHandle)
{
	check(InItem.IsValid());
	Item = StaticCastSharedPtr<FDMXFixtureFunctionItem>(InItem);
	CurrentModeHandle = InCurrentModeHandle;

	ColumnSizeData = InArgs._ColumnSizeData;
	NameEmptyError = InArgs._NameEmptyError;
	NameDuplicateError = InArgs._NameDuplicateError;
	AttributeHandle = InItem->GetFunctionHandle()->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, Attribute));
	check(CastFieldChecked<FStructProperty>(AttributeHandle->GetProperty())->Struct == FDMXAttributeName::StaticStruct());

	// Get function name
	FString Name;
	Item->GetName(Name);
	STableRow< TSharedPtr<FDMXFixtureFunctionItem> >::Construct(
		STableRow< TSharedPtr<FDMXFixtureFunctionItem> >::FArguments()
		.Content()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Fill)
			.HAlign(HAlign_Fill)
			.FillWidth(1)
			[
				SNew(SSplitter)
				.Orientation(EOrientation::Orient_Horizontal)
				.ResizeMode(ESplitterResizeMode::FixedPosition)
				.PhysicalSplitterHandleSize(1.0f)
				.HitDetectionSplitterHandleSize(5.0f)
				.Style(FEditorStyle::Get(), "DetailsView.Splitter")
				+ SSplitter::Slot()
				.Value(ColumnSizeData.LeftColumnWidth)
				.OnSlotResized(SSplitter::FOnSlotResized::CreateSP(this, &SDMXFunctionTableRow::OnLeftColumnResized))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.AutoWidth()
					.Padding(0.f, 0.f, 3.f, 0.f)
					[
						SNew(SConstrainedBox)
						.MinWidth(30.f)
						[
							SNew(STextBlock)
							.Text(this, &SDMXFunctionTableRow::GetFunctionChannelText)
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
						]
					]
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.FillWidth(1)
					[
						SNew(SBox)
						.Padding(FMargin(5.f, 0.f))
						[
							SAssignNew(InlineEditableTextBlock, SInlineEditableTextBlock)
							.Text(FText::FromString(Name))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
							.IsReadOnly(false)
							.OnVerifyTextChanged(this, &SDMXFunctionTableRow::OnVerifyFunctionTextChanged)
							.OnTextCommitted(this, &SDMXFunctionTableRow::OnFunctionTextCommitted)
						]
					]
				]
				+ SSplitter::Slot()
				.Value(ColumnSizeData.RightColumnWidth)
				.OnSlotResized(ColumnSizeData.OnWidthChanged)
				[
					SNew(SBox)
					.Padding(FMargin(2.f, 2.f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Fill)
						.HAlign(HAlign_Left)
						[
							SNew(SConstrainedBox)
							.MinWidth(InArgs._AttributeMinWidth)
							.MaxWidth(InArgs._AttributeMaxWidth)
							[
								SNew(SNameListPicker)
								.HasMultipleValues(this, &SDMXFunctionTableRow::HasMultipleAttributeValues)
								.OptionsSource(MakeAttributeLambda(&FDMXAttributeName::GetPossibleValues))
								.UpdateOptionsDelegate(&FDMXAttributeName::OnValuesChanged)
								.IsValid(this, &SDMXFunctionTableRow::HideAttributeWarningIcon)
								.Value(this, &SDMXFunctionTableRow::GetAttributeValue)
								.bCanBeNone(FDMXAttributeName::bCanBeNone)
								.bDisplayWarningIcon(true)
								.OnValueChanged(this, &SDMXFunctionTableRow::SetAttributeValue)
							]
						]
						+ SHorizontalBox::Slot()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("FunctionChannel.OverlapsWithMatrixCellChannels", "Overlaps with Matrix Cell Channels"))
							.ColorAndOpacity(FSlateColor(FLinearColor::Red))
							.Visibility(this, &SDMXFunctionTableRow::CheckCellChannelsOverlap)
						]
					]
				]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(3, 0)
		[
			SAssignNew(ErrorTextWidget, SPopupErrorText)
		]
	], OwnerTable);

	ErrorTextWidget->SetError(FText::GetEmpty());
}

FText SDMXFunctionTableRow::GetFunctionChannelText() const
{
	return FText::FromString(FString::FromInt(GetItem()->GetFunctionChannel()));
}

EVisibility SDMXFunctionTableRow::CheckCellChannelsOverlap() const
{
	if (CurrentModeHandle.IsValid())
	{
		TSharedPtr<IPropertyHandle> FixtureMatrixConfigHandle = CurrentModeHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, FixtureMatrixConfig));
		check(FixtureMatrixConfigHandle.IsValid());

		TArray<void*> RawData;
		FixtureMatrixConfigHandle->AccessRawData(RawData);
		// reinterpret_cast here as AccessRawData fills an array a void*
		FDMXFixtureMatrix* FixtureMatrixConfig = reinterpret_cast<FDMXFixtureMatrix*>(RawData[0]);

		if (!FixtureMatrixConfig || FixtureMatrixConfig->CellAttributes.Num() == 0)
		{
			return EVisibility::Collapsed;
		}
				
		int32 FirstCellChannel = FixtureMatrixConfig->FirstCellChannel;
		int32 LastCellChannel = FixtureMatrixConfig->GetFixtureMatrixLastChannel();
		int32 NumCells = FixtureMatrixConfig->XCells * FixtureMatrixConfig->YCells;

		int32 FunctionChannel = GetItem()->GetFunctionChannel();

		if (FunctionChannel >= FirstCellChannel && 
			FunctionChannel <= LastCellChannel &&
			NumCells > 0)
		{
			return EVisibility::Visible;
		}
	}

	return EVisibility::Collapsed;
}

void SDMXFunctionTableRow::OnLeftColumnResized(float InNewWidth)
{
	// This has to be bound or the splitter will take it upon itself to determine the size
	// We do nothing here because it is handled by the column size data
}

void SDMXFunctionTableRow::EnterEditingMode()
{
	FSlateApplication::Get().SetKeyboardFocus(InlineEditableTextBlock);
	InlineEditableTextBlock->EnterEditingMode();
}

void SDMXFunctionTableRow::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (!InlineEditableTextBlock->IsInEditMode())
	{
		FString Name;
		Item->GetName(Name);
		InlineEditableTextBlock->SetText(FText::FromString(Name));
	}
}

bool SDMXFunctionTableRow::OnVerifyFunctionTextChanged(const FText& InNewText, FText& OutErrorMessage)
{
	if (FText::TrimPrecedingAndTrailing(InNewText).IsEmpty())
	{
		ErrorTextWidget->SetError(NameEmptyError);
		return true;
	}

	FString OldName;
	Item->GetName(OldName);

	const FString& NewName = InNewText.ToString();

	if (OldName == NewName)
	{
		ErrorTextWidget->SetError(FText::GetEmpty());
		return true;
	}

	if (Item->IsNameUnique(NewName))
	{
		ErrorTextWidget->SetError(FText::GetEmpty());
	}
	else
	{
		ErrorTextWidget->SetError(NameDuplicateError);
	}

	return true;
}

void SDMXFunctionTableRow::OnFunctionTextCommitted(const FText& InNewText, ETextCommit::Type InTextCommit)
{
	FString OldName;
	Item->GetName(OldName);

	const FString& NewName = InNewText.ToString();

	if (OldName == NewName)
	{
		return;
	}

	FString UniqueNewName;
	Item->SetName(NewName, UniqueNewName);

	InlineEditableTextBlock->SetText(FText::FromString(UniqueNewName));

	ErrorTextWidget->SetError(FText::GetEmpty());

	// Release keyboard focus in case it was programmatically
	// set. Otherwise the row goes into edit mode when clicked anew
	FSlateApplication::Get().ClearKeyboardFocus();
}

void SDMXFunctionTableRow::SetAttributeValue(FName NewValue)
{
	FStructProperty* StructProperty = CastFieldChecked<FStructProperty>(AttributeHandle->GetProperty());

	TArray<void*> RawData;
	AttributeHandle->AccessRawData(RawData);
	FDMXAttributeName* PreviousValue = reinterpret_cast<FDMXAttributeName*>(RawData[0]);
	FDMXAttributeName NewProtocolName;
	NewProtocolName.SetFromName(NewValue);

	// Export new value to text format that can be imported later
	FString TextValue;
	StructProperty->Struct->ExportText(TextValue, &NewProtocolName, PreviousValue, nullptr, EPropertyPortFlags::PPF_None, nullptr);

	// Set values on edited property handle from exported text
	ensure(AttributeHandle->SetValueFromFormattedString(TextValue, EPropertyValueSetFlags::DefaultFlags) == FPropertyAccess::Result::Success);
}

FName SDMXFunctionTableRow::GetAttributeValue() const
{
	TArray<const void*> RawData;
	AttributeHandle->AccessRawData(RawData);

	for (const void* RawPtr : RawData)
	{
		if (RawPtr != nullptr)
		{
			// The types we use with this customization must have a cast constructor to FName
			return reinterpret_cast<const FDMXAttributeName*>(RawPtr)->GetName();
		}
	}

	return FName();
}

bool SDMXFunctionTableRow::HasMultipleAttributeValues() const
{
	TArray<const void*> RawData;
	AttributeHandle->AccessRawData(RawData);

	TOptional<FDMXAttributeName> CompareAgainst;
	for (const void* RawPtr : RawData)
	{
		if (RawPtr == nullptr)
		{
			if (CompareAgainst.IsSet())
			{
				return false;
			}
		}
		else
		{
			const FDMXAttributeName* ThisValue = reinterpret_cast<const FDMXAttributeName*>(RawPtr);

			if (!CompareAgainst.IsSet())
			{
				CompareAgainst = *ThisValue;
			}
			else if (!(*ThisValue == CompareAgainst.GetValue()))
			{
				return true;
			}
		}
	}

	return false;
}

bool SDMXFunctionTableRow::HideAttributeWarningIcon() const
{
	if (HasMultipleAttributeValues())
	{
		return true;
	}

	const FName CurrentValue = GetAttributeValue();
	if (CurrentValue.IsEqual(FDMXNameListItem::None))
	{
		return true;
	}

	return FDMXAttributeName::IsValid(GetAttributeValue());
}

FReply SDMXFunctionTableRow::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	InlineEditableTextBlock->EnterEditingMode();
	return FReply::Handled();
}

void SDMXFunctionItemListViewBox::Construct(const FArguments& InArgs, const TSharedPtr<FDMXEditor>& InDMXEditor, const TSharedPtr<IPropertyHandleArray>& InModesHandleArray)
{
	bRefreshRequested = false;
	ModesHandleArray = InModesHandleArray;
	SharedData = InDMXEditor->GetFixtureTypeSharedData();

	check(SharedData.IsValid());

	SharedData->OnModesSelected.AddSP(this, &SDMXFunctionItemListViewBox::OnModesSelected);

	// Register column size
	ColumnWidth = InArgs._ColumnWidth;
	ColumnSizeData.LeftColumnWidth = TAttribute<float>(this, &SDMXFunctionItemListViewBox::OnGetLeftColumnWidth);
	ColumnSizeData.RightColumnWidth = TAttribute<float>(this, &SDMXFunctionItemListViewBox::OnGetRightColumnWidth);
	ColumnSizeData.OnWidthChanged = SSplitter::FOnSlotResized::CreateSP(this, &SDMXFunctionItemListViewBox::OnSetColumnWidth);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SBox)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SAssignNew(ListView, SDMXFunctionItemListView)
				.ItemHeight(40.0f)
				.ListItemsSource(&ListSource)
				.OnGenerateRow(this, &SDMXFunctionItemListViewBox::GenerateFunctionNameRow)
				.OnSelectionChanged(this, &SDMXFunctionItemListViewBox::OnListSelectionChanged)
				.OnContextMenuOpening(this, &SDMXFunctionItemListViewBox::OnContextMenuOpening)
				.SelectionMode(ESelectionMode::Multi)
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
			.Orientation(Orient_Horizontal)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)			
			.Padding(0.f, 0.f, 3.f, 0.f)
			[
				SNew(STextBlock)
				.Text(this, &SDMXFunctionItemListViewBox::GetCellChannelsStartChannel)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				.Visibility(this, &SDMXFunctionItemListViewBox::GetFixtureMatrixVisibility)
				.Margin(FMargin(2.f, 15.f, 2.f, 5.f))
			]				
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Top)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Top)
				[
					SNew(STextBlock)
					.Text(this, &SDMXFunctionItemListViewBox::GetCellAttributesHeader)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
					.Visibility(this, &SDMXFunctionItemListViewBox::GetFixtureMatrixVisibility)
					.Margin(FMargin(2.f, 15.f, 2.f, 5.f))
				]

				+ SVerticalBox::Slot()
				.HAlign(HAlign_Fill)
				.AutoHeight()
				[
					SNew(SBox)
					.Visibility(this, &SDMXFunctionItemListViewBox::GetFixtureMatrixVisibility)
					.HAlign(HAlign_Fill)					
					[
						SAssignNew(CellAttributeListView, SDMXCellAttributeItemListView)
						.ItemHeight(40.0f)
						.ListItemsSource(&CellAttributeListSource)
						.OnGenerateRow(this, &SDMXFunctionItemListViewBox::GenerateCellAttributeNameRow)
						.SelectionMode(ESelectionMode::None)
					]
				]

				+ SVerticalBox::Slot()
				.HAlign(HAlign_Fill)
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(this, &SDMXFunctionItemListViewBox::GetFixtureMatrixWarning)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
					.ColorAndOpacity(FSlateColor(FLinearColor::Red))
					.Visibility(this, &SDMXFunctionItemListViewBox::GetFixtureMatrixVisibility)
					.Margin(FMargin(2.f, 15.f, 2.f, 5.f))
				]
			]
		]
	];

	RegisterCommands();
	RebuildList();
}

EVisibility SDMXFunctionItemListViewBox::GetFixtureMatrixVisibility() const
{
	if (CurrentModeHandle.IsValid())
	{
		TArray<UObject*> OuterObjects;
		CurrentModeHandle->GetOuterObjects(OuterObjects);
		
		// Multi function editing is not supported across Fixture Type Functions Details
		if (OuterObjects.Num() == 1)
		{
			UDMXEntityFixtureType* FixtureType = CastChecked<UDMXEntityFixtureType>(OuterObjects[0]);
			if (FixtureType->bFixtureMatrixEnabled)
			{
				return EVisibility::Visible;
			}
		}
	}

	return EVisibility::Collapsed;
}

FText SDMXFunctionItemListViewBox::GetCellAttributesHeader() const
{	
	if (CurrentModeHandle.IsValid())
	{
		TSharedPtr<IPropertyHandle> FixtureMatrixConfigHandle = CurrentModeHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, FixtureMatrixConfig));
		TSharedPtr<IPropertyHandle> XCellsHandle = FixtureMatrixConfigHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureMatrix, XCells));
		TSharedPtr<IPropertyHandle> YCellsHandle = FixtureMatrixConfigHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureMatrix, YCells));

		int32 XCells = 0;
		int32 YCells = 0;

		// Try to access the properties. May fail when removing modes
		if (XCellsHandle->GetValue(XCells) == FPropertyAccess::Success &&
			YCellsHandle->GetValue(YCells) == FPropertyAccess::Success)
		{
			TSharedPtr<IPropertyHandle> CellAttributesHandle = FixtureMatrixConfigHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureMatrix, CellAttributes));
			check(CellAttributesHandle.IsValid());

			TSharedPtr<IPropertyHandleArray> CellAttributesHandleArray = CellAttributesHandle->AsArray();
			check(CellAttributesHandleArray.IsValid());

			uint32 NumCellFunctions = 0;
			if (CellAttributesHandleArray->GetNumElements(NumCellFunctions) != FPropertyAccess::Success)
			{
				return LOCTEXT("DMXFixtureMatrix.ErrorAccessFunctions", "Unable to retrieve FixtureMode Functions value.");
			}

			int32 NumElements = XCells * YCells * NumCellFunctions;
			if (NumElements < 0)
			{
				return LOCTEXT("DMXFixtureMatrix.InvalidCellMatrixValues", "Invalid Cell Matrix values.");
			}

			return FText::FromString(FString::Printf(TEXT("Cell Functions %d elements"), NumElements));
		}
	}
	
	return FText();
}

FText SDMXFunctionItemListViewBox::GetFixtureMatrixWarning() const
{
	if (CurrentModeHandle.IsValid())
	{
		TSharedPtr<IPropertyHandle> FixtureMatrixConfigHandle = CurrentModeHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, FixtureMatrixConfig));
		
		TSharedPtr<IPropertyHandle> XCellsHandle = FixtureMatrixConfigHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureMatrix, XCells));
		TSharedPtr<IPropertyHandle> YCellsHandle = FixtureMatrixConfigHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureMatrix, YCells));

		int32 XCells = 0;
		int32 YCells = 0;

		// Try to get the values, may fail when removing modes
		if (XCellsHandle->GetValue(XCells) == FPropertyAccess::Success &&
			YCellsHandle->GetValue(YCells) == FPropertyAccess::Success)
		{
			if (XCells == 0 || YCells == 0)
			{
				return LOCTEXT("DMXFixtureMatrix.NoFixtureMatrixCellsPresentWarning", "Invalid Fixture Matrix: 0 Cells");
			}

			TSharedPtr<IPropertyHandle> CellAttributesHandle = FixtureMatrixConfigHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureMatrix, CellAttributes));
			TSharedPtr<IPropertyHandleArray> CellAttributesHandleArray = CellAttributesHandle->AsArray();
			check(CellAttributesHandleArray.IsValid());

			uint32 NumAttributes = 0;
			CellAttributesHandleArray->GetNumElements(NumAttributes);
			if (NumAttributes == 0)
			{
				return LOCTEXT("DMXFixtureMatrix.NoFixtureMatrixAttributesPresentWarning", "Invalid Fixture Matrix: No Attributes added");
			}
		}
	}

	return FText::GetEmpty();
}

FText SDMXFunctionItemListViewBox::GetCellChannelsStartChannel() const
{

	if (CurrentModeHandle.IsValid())
	{
		TSharedPtr<IPropertyHandle> CellMatrixConfigHandle = CurrentModeHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, FixtureMatrixConfig));
		check(CellMatrixConfigHandle.IsValid());
		TSharedPtr<IPropertyHandle> FirstCellChannelHandle = CellMatrixConfigHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureMatrix, FirstCellChannel));
		check(FirstCellChannelHandle.IsValid());
		
		int32 StartChannel = 0;
		if (FirstCellChannelHandle->GetValue(StartChannel) != FPropertyAccess::Success)
		{
			return LOCTEXT("DMXFixtureMatrix.ErrorAccessFirstChannel", "Unable to retrieve FixtureMatrix first Cell Channel Value.");
		}
		
		return FText::FromString(FString::Printf(TEXT("Cell Function Starting Channel: %d"), StartChannel));
	}

	return FText();
}

void SDMXFunctionItemListViewBox::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bRefreshRequested)
	{
		RebuildList();

		bRefreshRequested = false;
	}
}

FReply SDMXFunctionItemListViewBox::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (CommandList->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SDMXFunctionItemListViewBox::RebuildList(bool bUpdateSelection)
{
	check(SharedData.IsValid());
	check(ModesHandleArray.IsValid());

	TArray<TSharedPtr<FDMXFixtureFunctionItem>> FunctionsBeingEdited;
	TArray<TSharedPtr<FDMXCellAttributeItem>> CellAttributesBeingEdited;
	uint32 NumModes = 0;
	if (ModesHandleArray->GetNumElements(NumModes) == FPropertyAccess::Success)
	{
		for (uint32 IndexOfMode = 0; IndexOfMode < NumModes; IndexOfMode++)
		{
			TSharedRef<IPropertyHandle> ModeHandle = ModesHandleArray->GetElement(IndexOfMode);

			TSharedPtr<IPropertyHandle> ModeNameHandle = ModeHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, ModeName));
			check(ModeNameHandle.IsValid() && ModeNameHandle->IsValidHandle());

			TSharedPtr<FDMXFixtureModeItem> ModeItem = MakeShared<FDMXFixtureModeItem>(SharedData, ModeNameHandle);
			if (!ModeItem->IsModeSelected())
			{
				// Ignore modes that aren't selected
				continue;
			}

			CurrentModeHandle = ModeHandle;

			TSharedPtr<IPropertyHandle> FunctionsHandle = ModeHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, Functions));
			check(FunctionsHandle.IsValid() && FunctionsHandle->IsValidHandle());
			TSharedPtr<IPropertyHandleArray> FunctionsHandleArray = FunctionsHandle->AsArray();
			check(FunctionsHandleArray.IsValid());

			FSimpleDelegate OnNumFunctionsChangedDelegate = FSimpleDelegate::CreateSP(this, &SDMXFunctionItemListViewBox::OnNumFunctionsChanged);
			FunctionsHandleArray->SetOnNumElementsChanged(OnNumFunctionsChangedDelegate);

			uint32 NumFunctions = 0;
			if (FunctionsHandleArray->GetNumElements(NumFunctions) == FPropertyAccess::Success)
			{
				for (uint32 IndexOfFunction = 0; IndexOfFunction < NumFunctions; IndexOfFunction++)
				{
					FunctionsBeingEdited.Add(MakeShared<FDMXFixtureFunctionItem>(SharedData, ModeNameHandle, FunctionsHandleArray->GetElement(IndexOfFunction)));
				}
			}

			TSharedPtr<IPropertyHandle> FixtureMatrixConfigHandle = ModeHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, FixtureMatrixConfig));
			check(FixtureMatrixConfigHandle.IsValid() && FixtureMatrixConfigHandle->IsValidHandle());

			TSharedPtr<IPropertyHandle> CellAttributesHandle = FixtureMatrixConfigHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureMatrix, CellAttributes));
			check(CellAttributesHandle.IsValid() && CellAttributesHandle->IsValidHandle());
			TSharedPtr<IPropertyHandleArray> CellAttributesHandleArray = CellAttributesHandle->AsArray();
			check(CellAttributesHandleArray.IsValid());

			CellAttributesHandleArray->SetOnNumElementsChanged(OnNumFunctionsChangedDelegate);

			uint32 NumCellFunctions = 0;
			if (CellAttributesHandleArray->GetNumElements(NumCellFunctions) == FPropertyAccess::Success)
			{
				for (uint32 IndexOfFunction = 0; IndexOfFunction < NumCellFunctions; IndexOfFunction++)
				{
					CellAttributesBeingEdited.Add(MakeShared<FDMXCellAttributeItem>(CellAttributesHandleArray->GetElement(IndexOfFunction)));
				}
			}
		}
	}

	SharedData->SetFunctionsBeingEdited(FunctionsBeingEdited);

	ListSource = FunctionsBeingEdited;
	CellAttributeListSource = CellAttributesBeingEdited;

	TableRows.Reset();
	ListView->RebuildList();

	CellAttributeListView->RebuildList();

	if (bUpdateSelection)
	{
		// Adopt selection from function items
		TArray<TSharedPtr<FDMXFixtureFunctionItem>> SelectedFunctions;
		for (const TSharedPtr<FDMXFixtureFunctionItem>& FunctionItem : ListSource)
		{
			if (FunctionItem->IsModeSelected() && FunctionItem->IsFunctionSelected())
			{
				SelectedFunctions.Add(FunctionItem);
			}
		}

		if (SelectedFunctions.Num() == 0 && ListSource.Num() > 0)
		{
			// If nothing is selected, select the first function
			ListView->SetSelection(ListSource[0]);
		}
		else
		{
			ListView->SetItemSelection(SelectedFunctions, true);
		}
	}
}

FDMXCellAttributeItem::FDMXCellAttributeItem(const TSharedPtr<IPropertyHandle> InCellAttributeHandle)
{
	CellFunctionHandle = InCellAttributeHandle;
}

FText FDMXCellAttributeItem::GetAttributeName() const
{
	TSharedPtr<IPropertyHandle> CellAttributeHandle = CellFunctionHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureCellAttribute, Attribute));
	check(CellAttributeHandle.IsValid() && CellAttributeHandle->IsValidHandle());

	TArray<const void*> RawData;
	CellAttributeHandle->AccessRawData(RawData);

	for (const void* RawPtr : RawData)
	{
		if (RawPtr != nullptr)
		{
			// reinterpret_cast here as AccessRawData returns a TArray of void*
			FString AttributeName = reinterpret_cast<const FDMXAttributeName*>(RawPtr)->GetName().ToString();
			if (AttributeName.IsEmpty() || *AttributeName == FDMXNameListItem::None)
			{
				AttributeName = TEXT("<No Attribute selected>");
			}
			return FText::FromString(AttributeName);
		}
	}

	return FText::GetEmpty();
}

void SDMXFunctionItemListViewBox::OnNumFunctionsChanged()
{
	RebuildList(false);

	// Select last
	if (ListSource.Num() > 0)
	{
		int32 LastIndex = ListSource.Num() - 1;
		ListView->SetSelection(ListSource[LastIndex]);
	}
}

void SDMXFunctionItemListViewBox::OnModesSelected()
{
	RebuildList();
}

TSharedRef<ITableRow> SDMXFunctionItemListViewBox::GenerateFunctionNameRow(TSharedPtr<FDMXFixtureFunctionItem> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedRef<SDMXFunctionTableRow> Row =
		SNew(SDMXFunctionTableRow, OwnerTable, InItem, CurrentModeHandle)
		.ColumnSizeData(ColumnSizeData)
		.NameEmptyError(LOCTEXT("FixtureFunctionName.ErrorEmpty", "The Function Name can't be blank."))
		.NameDuplicateError(LOCTEXT("FixtureFunctionName.ErrorDuplicateFunctionName", "Function Name is already in use."));

	// Remember the row
	TableRows.Add(Row);

	return Row;
}

TSharedRef<ITableRow> SDMXFunctionItemListViewBox::GenerateCellAttributeNameRow(TSharedPtr<FDMXCellAttributeItem> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedRef<STableRow<TSharedPtr<FDMXCellAttributeItem>>> Row = SNew(STableRow<TSharedPtr<FDMXCellAttributeItem>>, OwnerTable)
		.Content()
		[
			SNew(STextBlock)
			.Text(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(InItem.ToSharedRef(), &FDMXCellAttributeItem::GetAttributeName)))
		];

	return Row;
}

void SDMXFunctionItemListViewBox::OnListSelectionChanged(TSharedPtr<FDMXFixtureFunctionItem> NewlySelectedItem, ESelectInfo::Type SelectInfo)
{
	check(SharedData.IsValid());

	TArray<TSharedPtr<FDMXFixtureFunctionItem>> SelectedItems;
	ListView->GetSelectedItems(SelectedItems);

	SharedData->SelectFunctions(SelectedItems);
}

TSharedPtr<SWidget> SDMXFunctionItemListViewBox::OnContextMenuOpening()
{
	const bool bCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bCloseWindowAfterMenuSelection, CommandList);

	MenuBuilder.BeginSection("BasicOperations");
	{
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Cut);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Copy);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Paste);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Duplicate);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SDMXFunctionItemListViewBox::RegisterCommands()
{
	// Listen to common editor shortcuts for copy/paste etc
	CommandList = MakeShared<FUICommandList>();

	CommandList->MapAction(FGenericCommands::Get().Cut,
		FUIAction(FExecuteAction::CreateSP(this, &SDMXFunctionItemListViewBox::OnCutSelectedItems),
			FCanExecuteAction::CreateSP(this, &SDMXFunctionItemListViewBox::CanCutItems))
	);

	CommandList->MapAction(FGenericCommands::Get().Copy,
		FUIAction(FExecuteAction::CreateSP(this, &SDMXFunctionItemListViewBox::OnCopySelectedItems),
			FCanExecuteAction::CreateSP(this, &SDMXFunctionItemListViewBox::CanCopyItems))
	);

	CommandList->MapAction(FGenericCommands::Get().Paste,
		FUIAction(FExecuteAction::CreateSP(this, &SDMXFunctionItemListViewBox::OnPasteItems),
			FCanExecuteAction::CreateSP(this, &SDMXFunctionItemListViewBox::CanPasteItems))
	);

	CommandList->MapAction(FGenericCommands::Get().Duplicate,
		FUIAction(FExecuteAction::CreateSP(this, &SDMXFunctionItemListViewBox::OnDuplicateItems),
			FCanExecuteAction::CreateSP(this, &SDMXFunctionItemListViewBox::CanDuplicateItems))
	);

	CommandList->MapAction(FGenericCommands::Get().Delete,
		FUIAction(FExecuteAction::CreateSP(this, &SDMXFunctionItemListViewBox::OnDeleteItems),
			FCanExecuteAction::CreateSP(this, &SDMXFunctionItemListViewBox::CanDeleteItems))
	);

	CommandList->MapAction(FGenericCommands::Get().Rename,
		FUIAction(FExecuteAction::CreateSP(this, &SDMXFunctionItemListViewBox::OnRenameItem),
			FCanExecuteAction::CreateSP(this, &SDMXFunctionItemListViewBox::CanRenameItem))
	);
}

bool SDMXFunctionItemListViewBox::CanCutItems() const
{
	int32 NumSelectedItems = ListView->GetNumItemsSelected();
	return NumSelectedItems == 1;
}

void SDMXFunctionItemListViewBox::OnCutSelectedItems()
{
	int32 NumSelectedItems = ListView->GetNumItemsSelected();

	const FScopedTransaction Transaction(NumSelectedItems > 1 ? LOCTEXT("CutFunctions", "Cut Functions") : LOCTEXT("CutFunction", "Cut Function"));
	OnCopySelectedItems();
	OnDeleteItems();
}

bool SDMXFunctionItemListViewBox::CanCopyItems() const
{
	int32 NumSelectedItems = ListView->GetNumItemsSelected();
	return NumSelectedItems > 0;
}

void SDMXFunctionItemListViewBox::OnCopySelectedItems()
{
	check(SharedData.IsValid());

	TArray<TSharedPtr<FDMXFixtureFunctionItem>> SelectedFunctions;
	ListView->GetSelectedItems(SelectedFunctions);

	SharedData->CopyFunctionsToClipboard(SelectedFunctions);
}

bool SDMXFunctionItemListViewBox::CanPasteItems() const
{
	int32 NumSelectedItems = ListView->GetNumItemsSelected();
	return NumSelectedItems > 0;
}

void SDMXFunctionItemListViewBox::OnPasteItems()
{
	check(SharedData.IsValid());

	TArray<TSharedPtr<FDMXFixtureFunctionItem>> SelectedFunctions;
	ListView->GetSelectedItems(SelectedFunctions);

	SharedData->PasteClipboardToFunctions(SelectedFunctions);
}

bool SDMXFunctionItemListViewBox::CanDuplicateItems() const
{
	int32 NumSelectedItems = ListView->GetNumItemsSelected();
	return NumSelectedItems > 0;
}

void SDMXFunctionItemListViewBox::OnDuplicateItems()
{
	check(SharedData.IsValid());

	TArray<TSharedPtr<FDMXFixtureFunctionItem>> SelectedFunctions;
	ListView->GetSelectedItems(SelectedFunctions);

	SharedData->DuplicateFunctions(SelectedFunctions);
}

bool SDMXFunctionItemListViewBox::CanDeleteItems() const
{
	int32 NumSelectedItems = ListView->GetNumItemsSelected();
	return NumSelectedItems > 0;
}

void SDMXFunctionItemListViewBox::OnDeleteItems()
{
	check(SharedData.IsValid());

	TArray<TSharedPtr<FDMXFixtureFunctionItem>> SelectedFunctions;
	ListView->GetSelectedItems(SelectedFunctions);

	SharedData->DeleteFunctions(SelectedFunctions);
}

bool SDMXFunctionItemListViewBox::CanRenameItem() const
{
	int32 NumSelectedItems = ListView->GetNumItemsSelected();
	return NumSelectedItems == 1;
}

void SDMXFunctionItemListViewBox::OnRenameItem()
{
	TArray<TSharedPtr<FDMXFixtureFunctionItem>> SelectedFunctions;
	ListView->GetSelectedItems(SelectedFunctions);
	check(SelectedFunctions.Num() == 1);

	const TSharedPtr<SDMXFunctionTableRow>* RowPtr = TableRows.FindByPredicate([&](const TSharedPtr<SDMXFunctionTableRow>& Row) {
		return Row->GetItem() == SelectedFunctions[0];
	});

	if (RowPtr)
	{
		(*RowPtr)->EnterEditingMode();
	}
}

#undef LOCTEXT_NAMESPACE
