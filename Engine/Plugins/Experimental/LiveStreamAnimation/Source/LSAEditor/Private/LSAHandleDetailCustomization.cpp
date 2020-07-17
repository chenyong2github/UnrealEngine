// Copyright Epic Games, Inc. All Rights Reserved.

#include "LSAHandleDetailCustomization.h"
#include "LiveStreamAnimationHandle.h"
#include "LiveStreamAnimationSettings.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"

#include "SlateFwd.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Views/SListView.h"

DECLARE_DELEGATE_OneParam(FOnHandleSelectionChanged, FName);
DECLARE_DELEGATE_RetVal_OneParam(FName, FGetSelectedHandle, bool&);

//~ This is based largely on SBoneSelectionWidget and SBoneTreeView from BoneSelectionWidget.h
class SLSAHandleSelectionWidget : public SCompoundWidget
{
public:

	using ThisClass = SLSAHandleSelectionWidget;

	SLATE_BEGIN_ARGS(ThisClass)
		: _OnHandleSelectionChanged()
		, _OnGetSelectedHandle()
	{}


	/** set selected handle */
	SLATE_EVENT(FOnHandleSelectionChanged, OnHandleSelectionChanged);

	/** get selected handle **/
	SLATE_EVENT(FGetSelectedHandle, OnGetSelectedHandle);

	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs)
	{
		OnHandleSelectionChanged = InArgs._OnHandleSelectionChanged;
		OnGetSelectedHandle = InArgs._OnGetSelectedHandle;

		ChildSlot
		[
			SAssignNew(HandlePickerButton, SComboButton)
			.OnGetMenuContent(FOnGetContent::CreateSP(this, &ThisClass::CreateHandleSelectionMenu))
			.ContentPadding(FMargin(4.0f, 2.0f, 4.0f, 2.0f))
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text(this, &ThisClass::GetSelectedHandleNameText)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ToolTipText(this, &ThisClass::GetFinalToolTip)
			]
		];
	}

private:

	using SHandleListView = SListView<TSharedPtr<FName>>;

	TOptional<FName> GetSelectedHandleName(bool& bMultiple) const
	{
		if (OnGetSelectedHandle.IsBound())
		{
			return OnGetSelectedHandle.Execute(bMultiple);
		}

		return TOptional<FName>();
	}

	FText GetSelectedHandleNameText() const
	{
		bool bMultiple = false;
		TOptional<FName> SelectedHandleName = GetSelectedHandleName(bMultiple);
		return SelectedHandleName ? FText::FromName(SelectedHandleName.GetValue()) : FText::GetEmpty();
	}

	FText GetFinalToolTip() const
	{
		return FText::Format(
			NSLOCTEXT("LiveStreamAnimation", "HandleSelector_Tooltip", "Handle:{0}\n\nClick to choose a different handle"),
			GetSelectedHandleNameText());
	}

	TSharedRef<SWidget> CreateHandleSelectionMenu()
	{
		TSharedRef<SHandleListView> HandleListView = SNew(SHandleListView)
			.ListItemsSource(&HandleNameSourceList)
			.OnGenerateRow(this, &ThisClass::MakeHandleListViewRowWidget)
			.OnSelectionChanged(this, &ThisClass::OnHandleListViewSelectionChanged)
			.SelectionMode(ESelectionMode::Single);

		bool bMultiple = false;
		TOptional<FName> SelectedBone = GetSelectedHandleName(bMultiple);
		if (bMultiple)
		{
			SelectedBone.Reset();
		}

		RebuildHandleListViewEntries(SelectedBone, HandleListView);

		TSharedRef<SBox> HandleListViewMenu = SNew(SBox)
		.Content()
		[
			SNew(SBorder)
			.Padding(6)
			.BorderImage(FEditorStyle::GetBrush("NoBorder"))
			.Content()
			[
				SNew(SBox)
				.WidthOverride(300)
				.HeightOverride(512)
				.Content()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Font(FEditorStyle::GetFontStyle("BoldFont"))
						.Text(NSLOCTEXT("LiveStreamAnimation", "HandleSelector_Title", "Select..."))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SSeparator)
						.SeparatorImage(FEditorStyle::GetBrush("Menu.Separator"))
						.Orientation(Orient_Horizontal)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SAssignNew(FilterTextWidget, SSearchBox)
						.SelectAllTextWhenFocused(true)
						.OnTextChanged(this, &ThisClass::OnHandleListViewFilterTextChanged, HandleListView)
						.HintText(NSLOCTEXT("LiveStreamAnimation", "HandleSelector_Search", "Search..."))
					]
					+ SVerticalBox::Slot()
					[
						HandleListView
					]
				]
			]
		];

		return HandleListViewMenu;
	}

	void OnHandleListViewFilterTextChanged(const FText& InFilterText, TSharedRef<SHandleListView> HandleListView)
	{
		FilterText = InFilterText;
		RebuildHandleListViewEntries(TOptional<FName>(), HandleListView);
	}

	void RebuildHandleListViewEntries(TOptional<FName> SelectedHandle, TSharedRef<SHandleListView> HandleListView)
	{
		HandleNameSourceList.Empty();

		const TArrayView<const FName> HandleNames = ULiveStreamAnimationSettings::GetHandleNames();
		for (const FName HandleName : HandleNames)
		{
			if (!FilterText.IsEmpty() && !HandleName.ToString().Contains(FilterText.ToString()))
			{
				continue;
			}

			TSharedRef<FName> HandleListEntry = MakeShared<FName>(HandleName);
			HandleNameSourceList.Add(HandleListEntry);

			if (SelectedHandle && SelectedHandle.GetValue() == HandleName)
			{
				HandleListView->SetItemSelection(HandleListEntry, true);
				HandleListView->RequestScrollIntoView(HandleListEntry);
			}
		}

		HandleListView->RequestListRefresh();
	}

	TSharedRef<ITableRow> MakeHandleListViewRowWidget(TSharedPtr<FName> InHandle, const TSharedRef<STableViewBase>& OwnerTable)
	{
		return SNew(STableRow<TSharedPtr<FName>>, OwnerTable)
			.Content()
			[
				SNew(STextBlock)
				.HighlightText(FilterText)
				.Text(FText::FromName(*InHandle))
			];
	}

	void OnHandleListViewSelectionChanged(TSharedPtr<FName> HandleName, ESelectInfo::Type SelectInfo)
	{
		if (HandleName.IsValid() && SelectInfo != ESelectInfo::Direct)
		{
			OnHandleSelectionChanged.ExecuteIfBound(*HandleName);
		}

		HandlePickerButton->SetIsOpen(false);
	}

	TSharedPtr<SComboButton> HandlePickerButton;
	TSharedPtr<SSearchBox> FilterTextWidget;

	TArray<TSharedPtr<FName>> HandleNameSourceList;
	FText FilterText;

	FOnHandleSelectionChanged OnHandleSelectionChanged;
	FGetSelectedHandle OnGetSelectedHandle;
};

void FLSAHandleDetailCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	auto FindStructMemberProperty = [PropertyHandle](FName PropertyName)
	{
		uint32 NumChildren = 0;
		PropertyHandle->GetNumChildren(NumChildren);
		for (uint32 ChildIdx = 0; ChildIdx < NumChildren; ++ChildIdx)
		{
			TSharedPtr<IPropertyHandle> ChildHandle = PropertyHandle->GetChildHandle(ChildIdx);
			if (ChildHandle->GetProperty()->GetFName() == PropertyName)
			{
				return ChildHandle;
			}
		}

		return TSharedPtr<IPropertyHandle>();
	};

	HandleProperty = FindStructMemberProperty(GET_MEMBER_NAME_CHECKED(FLiveStreamAnimationHandleWrapper, Handle));
	check(HandleProperty);

	if (HandleProperty->IsValidHandle())
	{
		HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			SNew(SLSAHandleSelectionWidget)
			.ToolTipText(PropertyHandle->GetToolTipText())
			.OnGetSelectedHandle(this, &ThisClass::GetSelectedHandle)
			.OnHandleSelectionChanged(this, &ThisClass::OnHandleSelectionChanged)
		];
	}
	else
	{
		ensureAlways(false);
		UE_LOG(LogTemp, Warning, TEXT(""))
	}
}

void FLSAHandleDetailCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{

}

FName FLSAHandleDetailCustomization::GetSelectedHandle(bool& bMultipleValues) const
{
	FString OutText;

	FPropertyAccess::Result Result = HandleProperty->GetValueAsFormattedString(OutText);
	bMultipleValues = (Result == FPropertyAccess::MultipleValues);

	return FName(*OutText);
}

void FLSAHandleDetailCustomization::OnHandleSelectionChanged(FName NewHandle)
{
	HandleProperty->SetValue(NewHandle);
}

TSharedRef<IPropertyTypeCustomization> FLSAHandleDetailCustomization::MakeInstance()
{
	return MakeShared<FLSAHandleDetailCustomization>();
}