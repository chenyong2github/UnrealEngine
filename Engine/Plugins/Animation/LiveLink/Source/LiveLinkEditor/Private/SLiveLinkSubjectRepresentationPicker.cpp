// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLiveLinkSubjectRepresentationPicker.h"

#include "AssetData.h"
#include "EditorStyleSet.h"
#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"
#include "LiveLinkEditorPrivate.h"
#include "LiveLinkPreset.h"
#include "Misc/FeedbackContext.h"
#include "PropertyCustomizationHelpers.h"
#include "Styling/SlateIconFinder.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"


#define LOCTEXT_NAMESPACE "SLiveLinkSubjectRepresentationPicker"

namespace SubjectUI
{
	static const FName EnabledColumnName(TEXT("Enabled"));
	static const FName NameColumnName(TEXT("Name"));
	static const FName RoleColumnName(TEXT("Role"));
};

struct FLiveLinkSubjectRepEntry
{
	FLiveLinkSubjectRepEntry(const FLiveLinkSubjectRepresentation& InSubRep, bool bInEnabled)
		: SubjectRepresentation(InSubRep)
		, bEnabled(bInEnabled)
	{}
	FLiveLinkSubjectRepresentation SubjectRepresentation;
	bool bEnabled;
};

class SLiveLinkSubjectEntryRow : public SMultiColumnTableRow<FLiveLinkSubjectRepEntryPtr>
{
public:
	SLATE_BEGIN_ARGS(SLiveLinkSubjectEntryRow) {}
		SLATE_ARGUMENT(FLiveLinkSubjectRepEntryPtr, Entry)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args, const TSharedRef<STableViewBase>& OwnerTableView)
	{
		EntryPtr = Args._Entry;

		SMultiColumnTableRow<FLiveLinkSubjectRepEntryPtr>::Construct(
			FSuperRowType::FArguments()
			.Padding(0.f),
			OwnerTableView
		);
	}

	/** Overridden from SMultiColumnTableRow.  Generates a widget for this column of the list view. */
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (ColumnName == SubjectUI::EnabledColumnName)
		{
			return SNew(SCheckBox)
				.IsChecked(EntryPtr->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
				.IsEnabled(false);
		}
		else if (ColumnName == SubjectUI::NameColumnName)
		{
			return	SNew(STextBlock)
				.Text(FText::FromName(EntryPtr->SubjectRepresentation.Subject));
		}
		else if (ColumnName == SubjectUI::RoleColumnName)
		{
			return SNew(STextBlock)
				.Text(EntryPtr->SubjectRepresentation.Role->GetDefaultObject<ULiveLinkRole>()->GetDisplayName());
		}

		return SNullWidget::NullWidget;
	}

	FLiveLinkSubjectRepEntryPtr EntryPtr;
};

void SLiveLinkSubjectRepresentationPicker::Construct(const FArguments& InArgs)
{
	ValueAttribute = InArgs._Value;
	OnValueChangedDelegate = InArgs._OnValueChanged;
	HasMultipleValuesAttribute = InArgs._HasMultipleValues;
	bShowRole = InArgs._ShowRole;

	SubjectRepData.Reset();
	SelectedLiveLinkPreset.Reset();

	TSharedPtr<SWidget> ComboButtonContent;
	if (bShowRole)
	{
		ComboButtonContent = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1)
			[	
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				[
					SNew(STextBlock)
					.Font(InArgs._Font)
					.Text(this, &SLiveLinkSubjectRepresentationPicker::GetSubjectNameValueText)
				]
				+ SVerticalBox::Slot()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					[
						SNew(SImage)
						.Image(this, &SLiveLinkSubjectRepresentationPicker::GetRoleIcon)
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(InArgs._Font)
						.Text(this, &SLiveLinkSubjectRepresentationPicker::GetRoleText)
					]
				]
			];
	}
	else
	{
		ComboButtonContent = SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("NoBorder"))
			.Padding(FMargin(0, 0, 5, 0))
			[
				SNew(SEditableTextBox)
				.Text(this, &SLiveLinkSubjectRepresentationPicker::GetSubjectNameValueText)
				.OnTextCommitted(this, &SLiveLinkSubjectRepresentationPicker::OnComboTextCommitted)
				.SelectAllTextWhenFocused(true)
				.SelectAllTextOnCommit(true)
				.ClearKeyboardFocusOnCommit(false)
				.Font(InArgs._Font)
			];
	}

	ChildSlot
	[
		SAssignNew(PickerComboButton, SComboButton)
		.ComboButtonStyle(InArgs._ComboButtonStyle)
		.ButtonStyle(InArgs._ButtonStyle)
		.ForegroundColor(InArgs._ForegroundColor)
		.ContentPadding(InArgs._ContentPadding)
		.VAlign(VAlign_Fill)
		.OnGetMenuContent(this, &SLiveLinkSubjectRepresentationPicker::BuildMenu)
		.ButtonContent()
		[
			ComboButtonContent.ToSharedRef()
		]
	];
}

FLiveLinkSubjectRepresentation SLiveLinkSubjectRepresentationPicker::GetCurrentValue() const
{
	return ValueAttribute.Get();
}

FText SLiveLinkSubjectRepresentationPicker::GetSubjectNameValueText() const
{
	const bool bHasMultipleValues = HasMultipleValuesAttribute.Get();
	if (bHasMultipleValues)
	{
		return LOCTEXT("MultipleValuesText", "<multiple values>");
	}

	FLiveLinkSubjectRepresentation CurrentSubjectRepresentation = ValueAttribute.Get();
	return FText::FromName(CurrentSubjectRepresentation.Subject);
}

const FSlateBrush* SLiveLinkSubjectRepresentationPicker::GetRoleIcon() const
{
	const bool bHasMultipleValues = HasMultipleValuesAttribute.Get();
	FLiveLinkSubjectRepresentation CurrentSubjectRepresentation = ValueAttribute.Get();
	if (!bHasMultipleValues && CurrentSubjectRepresentation.Role != nullptr)
	{
		return FSlateIconFinder::FindIconBrushForClass(CurrentSubjectRepresentation.Role);
	}
	return FSlateIconFinder::FindIconBrushForClass(ULiveLinkRole::StaticClass());
}

FText SLiveLinkSubjectRepresentationPicker::GetRoleText() const
{
	const bool bHasMultipleValues = HasMultipleValuesAttribute.Get();
	if (bHasMultipleValues)
	{
		return LOCTEXT("MultipleValuesText", "<multiple values>");
	}

	FLiveLinkSubjectRepresentation CurrentSubjectRepresentation = ValueAttribute.Get();
	if (CurrentSubjectRepresentation.Role == nullptr)
	{
		return LOCTEXT("NoValueText", "<none>");
	}
	return CurrentSubjectRepresentation.Role->GetDisplayNameText();
}

TSharedRef<SWidget> SLiveLinkSubjectRepresentationPicker::BuildMenu()
{
	SubjectRepData.Reset();
	SelectedLiveLinkPreset.Reset();
	BuildSubjectRepDataList();

	return SNew(SBox)
		.Padding(0)
		.WidthOverride(300.f)
		.HeightOverride(300.f)
		[
			SNew(SBorder)
			.ForegroundColor(FCoreStyle::Get().GetSlateColor("DefaultForeground"))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					// Current Preset
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					.Padding(8, 0)
					[
						SAssignNew(SelectPresetComboButton, SComboButton)
						.ContentPadding(0)
						.ForegroundColor(this, &SLiveLinkSubjectRepresentationPicker::GetSelectPresetForegroundColor)
						.ButtonStyle(FEditorStyle::Get(), "ToggleButton") // Use the tool bar item style for this button
						.OnGetMenuContent(this, &SLiveLinkSubjectRepresentationPicker::BuildPresetSubMenu)
						.ButtonContent()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SImage)
								.Image(FLiveLinkEditorPrivate::GetStyleSet()->GetBrush("LiveLinkClient.Common.Icon.Small"))
							]
							+ SHorizontalBox::Slot()
							.FillWidth(1.f)
							.Padding(2, 0, 0, 0)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(this, &SLiveLinkSubjectRepresentationPicker::GetPresetSelectedText)
							]
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ContentPadding(0)
						.ButtonStyle(FEditorStyle::Get(), "ToggleButton") // Use the tool bar item style for this button
						.OnClicked(this, &SLiveLinkSubjectRepresentationPicker::ClearCurrentPreset)
						.IsEnabled(this, &SLiveLinkSubjectRepresentationPicker::HasCurrentPreset)
						[
							SNew(SImage)
							.Image(FEditorStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
						]
					]
				]
				+ SVerticalBox::Slot()
				[
					SNew(SBorder)
					.Padding(FMargin(4.0f, 4.0f))
					[
						SAssignNew(SubjectListView, SListView<FLiveLinkSubjectRepEntryPtr>)
						.ListItemsSource(&SubjectRepData)
						.SelectionMode(ESelectionMode::Single)
						.OnGenerateRow(this, &SLiveLinkSubjectRepresentationPicker::MakeSubjectRepListViewWidget)
						.OnSelectionChanged(this, &SLiveLinkSubjectRepresentationPicker::OnSubjectRepListSelectionChanged)
						.HeaderRow
						(
							SNew(SHeaderRow)
							+ SHeaderRow::Column(SubjectUI::EnabledColumnName)
							.ManualWidth(20.f)
							.DefaultLabel(LOCTEXT("EnabledColumnHeaderName", ""))
							+ SHeaderRow::Column(SubjectUI::NameColumnName)
							.FillWidth(60.f)
							.DefaultLabel(LOCTEXT("SubjectColumnHeaderName", "Subject"))
							+ SHeaderRow::Column(SubjectUI::RoleColumnName)
							.FillWidth(40.f)
							.DefaultLabel(LOCTEXT("RoleColumnHeaderName", "Role"))
						)
					]
				]
			]
		];
}

FText SLiveLinkSubjectRepresentationPicker::GetPresetSelectedText() const
{
	ULiveLinkPreset* LiveLinkPresetPtr = SelectedLiveLinkPreset.Get();
	if (LiveLinkPresetPtr)
	{
		return FText::FromName(LiveLinkPresetPtr->GetFName());
	}
	return LOCTEXT("SelectAPresetLabel", "<No Preset Selected>");
}

FSlateColor SLiveLinkSubjectRepresentationPicker::GetSelectPresetForegroundColor() const
{
	static const FName InvertedForegroundName("InvertedForeground");
	static const FName DefaultForegroundName("DefaultForeground");
	TSharedPtr<SComboButton> SelectPresetComboButtonPin = SelectPresetComboButton.Pin();
	return (SelectPresetComboButtonPin.IsValid() && SelectPresetComboButtonPin->IsHovered()) ? FEditorStyle::GetSlateColor(InvertedForegroundName) : FEditorStyle::GetSlateColor(DefaultForegroundName);
}

FReply SLiveLinkSubjectRepresentationPicker::ClearCurrentPreset()
{
	SelectedLiveLinkPreset.Reset();
	BuildSubjectRepDataList();

	return FReply::Handled();
}

bool SLiveLinkSubjectRepresentationPicker::HasCurrentPreset() const
{
	return SelectedLiveLinkPreset.IsValid();
}

TSharedRef<ITableRow> SLiveLinkSubjectRepresentationPicker::MakeSubjectRepListViewWidget(FLiveLinkSubjectRepEntryPtr Entry, const TSharedRef<STableViewBase>& OwnerTable) const
{
	return SNew(SLiveLinkSubjectEntryRow, OwnerTable)
		.Entry(Entry);
}

void SLiveLinkSubjectRepresentationPicker::OnSubjectRepListSelectionChanged(FLiveLinkSubjectRepEntryPtr Entry, ESelectInfo::Type SelectionType)
{
	if (Entry.IsValid())
	{
		SetValue(Entry->SubjectRepresentation);
	}
	else
	{
		SetValue(FLiveLinkSubjectRepresentation());
	}
}

TSharedRef<SWidget> SLiveLinkSubjectRepresentationPicker::BuildPresetSubMenu()
{
	ULiveLinkPreset* LiveLinkPresetPtr = SelectedLiveLinkPreset.Get();
	FAssetData CurrentAssetData = LiveLinkPresetPtr ? FAssetData(LiveLinkPresetPtr) : FAssetData();

	TArray<const UClass*> ClassFilters;
	ClassFilters.Add(ULiveLinkPreset::StaticClass());

	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.AddWidget(
		PropertyCustomizationHelpers::MakeAssetPickerWithMenu(
			FAssetData(),
			false,
			false,
			ClassFilters,
			TArray<UFactory*>(),
			FOnShouldFilterAsset::CreateLambda([CurrentAssetData](const FAssetData& InAssetData) { return InAssetData == CurrentAssetData; }),
			FOnAssetSelected::CreateRaw(this, &SLiveLinkSubjectRepresentationPicker::NewPresetSelected),
			FSimpleDelegate()
		),
		FText::GetEmpty(),
		true,
		false
	);
	return MenuBuilder.MakeWidget();
}

void SLiveLinkSubjectRepresentationPicker::NewPresetSelected(const FAssetData& AssetData)
{
	GWarn->BeginSlowTask(LOCTEXT("MediaProfileLoadPackage", "Loading Media Profile"), true, false);
	ULiveLinkPreset* LiveLinkPresetPtr = Cast<ULiveLinkPreset>(AssetData.GetAsset());
	SelectedLiveLinkPreset = LiveLinkPresetPtr;

	BuildSubjectRepDataList();

	TSharedPtr<SComboButton> SelectPresetComboButtonPin = SelectPresetComboButton.Pin();
	if (SelectPresetComboButtonPin)
	{
		SelectPresetComboButtonPin->SetIsOpen(false);
	}

	GWarn->EndSlowTask();
}

void SLiveLinkSubjectRepresentationPicker::OnComboTextCommitted(const FText& NewText, ETextCommit::Type InTextCommit)
{
	FLiveLinkSubjectRepresentation Representation;
	Representation.Subject.Name = *NewText.ToString();
	SetValue(Representation);
}

void SLiveLinkSubjectRepresentationPicker::SetValue(const FLiveLinkSubjectRepresentation& InValue)
{
	if (OnValueChangedDelegate.IsBound())
	{
		OnValueChangedDelegate.ExecuteIfBound(InValue);
	}
	else if (ValueAttribute.IsBound())
	{
		ValueAttribute = InValue;
	}

	TSharedPtr<SComboButton> PickerComboButtonPin = PickerComboButton.Pin();
	if (PickerComboButtonPin.IsValid())
	{
		PickerComboButtonPin->SetIsOpen(false);
	}
}

void SLiveLinkSubjectRepresentationPicker::BuildSubjectRepDataList()
{
	SubjectRepData.Reset();

	ULiveLinkPreset* LiveLinkPresetPtr = SelectedLiveLinkPreset.Get();
	if (LiveLinkPresetPtr)
	{
		for (const FLiveLinkSubjectPreset& SubjectPreset : LiveLinkPresetPtr->GetSubjectPresets())
		{
			FLiveLinkSubjectRepresentation Representation;
			Representation.Role = SubjectPreset.Role;
			Representation.Subject = SubjectPreset.Key.SubjectName;

			if (Representation.Role != nullptr && !Representation.Subject.IsNone())
			{
				SubjectRepData.Add(MakeShared<FLiveLinkSubjectRepEntry>(Representation, SubjectPreset.bEnabled));
			}
		}
	}
	else if (IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
		TArray<FLiveLinkSubjectKey> SubjectKeys = LiveLinkClient.GetSubjects(true, true);

		TArray<FLiveLinkSubjectName> UniqueSubjectName;
		UniqueSubjectName.Reset(SubjectKeys.Num());
		for(const FLiveLinkSubjectKey& Key : SubjectKeys)
		{
			UniqueSubjectName.AddUnique(Key.SubjectName);
		}
		UniqueSubjectName.Sort(FNameLexicalLess());

		for (const FLiveLinkSubjectName& SubjectName : UniqueSubjectName)
		{
			bool bEnabled = LiveLinkClient.IsSubjectEnabled(SubjectName);
			FLiveLinkSubjectRepresentation Representation;
			Representation.Subject = SubjectName;
			Representation.Role = LiveLinkClient.GetSubjectRole(SubjectName);

			if (Representation.Role != nullptr && !Representation.Subject.IsNone())
			{
				SubjectRepData.Add(MakeShared<FLiveLinkSubjectRepEntry>(Representation, bEnabled));
			}
		}
	}

	TSharedPtr<SListView<FLiveLinkSubjectRepEntryPtr>> SubjectListViewPin = SubjectListView.Pin();
	if (SubjectListViewPin.IsValid())
	{
		SubjectListViewPin->RebuildList();
	}
}

#undef LOCTEXT_NAMESPACE