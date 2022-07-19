// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMVVMViewBindingListView.h"

#include "Algo/Transform.h"
#include "Bindings/MVVMBindingHelper.h"
#include "Blueprint/WidgetTree.h"
#include "Dialog/SCustomDialog.h"
#include "Editor.h"
#include "Styling/AppStyle.h"
#include "Engine/Engine.h"
#include "Features/IModularFeatures.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IPropertyAccessEditor.h"
#include "MVVMBlueprintView.h"
#include "MVVMBlueprintViewBinding.h"
#include "MVVMEditorSubsystem.h"
#include "MVVMSubsystem.h"
#include "MVVMWidgetBlueprintExtension_View.h"
#include "SEnumCombo.h"
#include "SPrimaryButton.h"
#include "SSimpleButton.h" 
#include "Styling/MVVMEditorStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/SMVVMConversionPath.h"
#include "Widgets/SMVVMFieldSelector.h"
#include "Widgets/SMVVMPropertyPath.h"
#include "Widgets/SMVVMSourceSelector.h"
#include "Widgets/SMVVMViewBindingPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Views/STableViewBase.h"

#define LOCTEXT_NAMESPACE "BindingListView"

namespace UE::MVVM
{

// a wrapper around either a widget row or a binding row
struct FBindingEntry
{
	FBindingEntry(FName InWidgetName) : WidgetName(InWidgetName) {}
	FBindingEntry(int32 InIndex) : BindingIndex(InIndex) { check(InIndex != INDEX_NONE); }
	
	FMVVMBlueprintViewBinding* GetBinding(UMVVMBlueprintView* View) const
	{
		return View->GetBindingAt(BindingIndex);
	}

	const FMVVMBlueprintViewBinding* GetBinding(const UMVVMBlueprintView* View) const
	{
		return View->GetBindingAt(BindingIndex);
	}

	int32 GetBindingIndex() const
	{
		return BindingIndex;
	}

	FName GetWidgetName() const
	{
		return WidgetName;
	}

	TConstArrayView<TSharedPtr<FBindingEntry>> GetChildren() const
	{
		return Children;
	}

	void AddChild(TSharedRef<FBindingEntry> Child)
	{
		Children.Add(Child);
	}

private:
	FName WidgetName;
	int32 BindingIndex = INDEX_NONE;
	TArray<TSharedPtr<FBindingEntry>> Children;
};

namespace Private
{
	TArray<FName>* GetBindingModeNames()
	{
		static TArray<FName> BindingModeNames;

		if (BindingModeNames.IsEmpty())
		{
			UEnum* ModeEnum = StaticEnum<EMVVMBindingMode>();

			BindingModeNames.Reserve(ModeEnum->NumEnums());

			for (int32 BindingIndex = 0; BindingIndex < ModeEnum->NumEnums() - 1; ++BindingIndex)
			{
				const bool bIsHidden = ModeEnum->HasMetaData(TEXT("Hidden"), BindingIndex);
				if (!bIsHidden)
				{
					BindingModeNames.Add(ModeEnum->GetNameByIndex(BindingIndex));
				}
			}
		}

		return &BindingModeNames;
	}

	void ExpandAll(const TSharedPtr<STreeView<TSharedPtr<FBindingEntry>>>& TreeView, const TSharedPtr<FBindingEntry>& Entry)
	{
		TreeView->SetItemExpansion(Entry, true);

		for (const TSharedPtr<FBindingEntry>& Child : Entry->GetChildren())
		{
			ExpandAll(TreeView, Child);
		}
	}
}

class SWidgetRow : public STableRow<TSharedPtr<FBindingEntry>>
{
public:
	SLATE_BEGIN_ARGS(SWidgetRow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& Args, const TSharedRef<STableViewBase>& OwnerTableView, const TSharedPtr<FBindingEntry>& InEntry, UWidgetBlueprint* InWidgetBlueprint)
	{
		Entry = InEntry;
		WidgetBlueprintWeak = InWidgetBlueprint;

		STableRow<TSharedPtr<FBindingEntry>>::Construct(
			STableRow<TSharedPtr<FBindingEntry>>::FArguments()
			.Padding(1.0f)
			[
				SNew(SBox)
				.HeightOverride(30)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.Padding(2, 1)
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Fill)
					.AutoWidth()
					[
						SNew(SSourceSelector, InWidgetBlueprint)
						.ShowClear(false)
						.AutoRefresh(true)
						.ViewModels(false)
						.SelectedSource(this, &SWidgetRow::GetSelectedWidget)
					]
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					.AutoWidth()
					[
						SNew(SSimpleButton)
						.Icon(FAppStyle::Get().GetBrush("Icons.Plus"))
						.IsEnabled_Lambda([this]() { return !Entry->GetWidgetName().IsNone(); })
						.OnClicked(this, &SWidgetRow::AddBinding)

					]
				]
			],
			OwnerTableView
		);
	}

private:

	FBindingSource GetSelectedWidget() const
	{
		return FBindingSource::CreateForWidget(WidgetBlueprintWeak.Get(), Entry->GetWidgetName());
	}

	FReply AddBinding() const
	{
		if (UWidgetBlueprint* WidgetBlueprint = WidgetBlueprintWeak.Get())
		{
			UMVVMEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();
			FMVVMBlueprintViewBinding& Binding = EditorSubsystem->AddBinding(WidgetBlueprint);
			FMVVMBlueprintPropertyPath Path;
			Path.SetWidgetName(Entry->GetWidgetName());
			EditorSubsystem->SetWidgetPropertyForBinding(WidgetBlueprint, Binding, Path);
		}

		return FReply::Handled();
	}

private:
	TSharedPtr<FBindingEntry> Entry;
	TWeakObjectPtr<UWidgetBlueprint> WidgetBlueprintWeak;
};

class SBindingRow : public STableRow<TSharedPtr<FBindingEntry>>
{
public:
	SLATE_BEGIN_ARGS(SBindingRow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& Args, const TSharedRef<STableViewBase>& OwnerTableView, const TSharedPtr<FBindingEntry>& InEntry, UMVVMBlueprintView* InBlueprintView, UWidgetBlueprint* InWidgetBlueprint)
	{
		Entry = InEntry;
		WidgetBlueprintWeak = InWidgetBlueprint;
		BlueprintView = InBlueprintView;

		OnBlueprintChangedHandle = InWidgetBlueprint->OnChanged().AddSP(this, &SBindingRow::HandleBlueprintChanged);

		FMVVMBlueprintViewBinding* ViewModelBinding = GetThisViewBinding();

		STableRow<TSharedPtr<FBindingEntry>>::Construct(
			STableRow<TSharedPtr<FBindingEntry>>::FArguments()
			.Padding(1.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.Padding(2, 0)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Left) 
				.AutoWidth()
				[
					SNew(SCheckBox)
					.IsChecked(this, &SBindingRow::IsBindingCompiled)
					.OnCheckStateChanged(this, &SBindingRow::OnIsBindingCompileChanged)
				]

				+ SHorizontalBox::Slot()
				.Padding(2, 0)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Left) 
				.AutoWidth()
				[
					SNew(SSimpleButton)
					.Icon(FAppStyle::Get().GetBrush("Icons.Error"))
					.Visibility(this, &SBindingRow::GetErrorVisibility)
					.ToolTipText(this, &SBindingRow::GetErrorToolTip)
					.OnClicked(this, &SBindingRow::OnErrorButtonClicked)
				]

				+ SHorizontalBox::Slot()
				.Padding(4, 0)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Fill)
				.AutoWidth()
				[
					SNew(SBox)
					.HeightOverride(28)
					[
						SAssignNew(WidgetMenuAnchor, SMenuAnchor)
						.OnGetMenuContent(this, &SBindingRow::OnGetWidgetMenuContent)
						[
							SNew(SButton)
							.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("ComboButton").ButtonStyle)
							.OnClicked(this, &SBindingRow::OnWidgetMenuAnchorClicked)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.HAlign(HAlign_Left)
								.VAlign(VAlign_Fill)
								.AutoWidth()
								[
									SAssignNew(SelectedWidget, SFieldEntry)
									.Field(GetSelectedWidgetProperty())
								]
								+ SHorizontalBox::Slot()
								.Padding(4, 0, 0, 0)
								.HAlign(HAlign_Right)
								.VAlign(VAlign_Center)
								.AutoWidth()
								[
									SNew(SImage)
									.Image(FAppStyle::Get().GetBrush("Icons.ChevronDown"))
									.ColorAndOpacity(FSlateColor::UseForeground())
								]
							]
						]
					]
				]

				+ SHorizontalBox::Slot()
				.Padding(2, 1)
				.VAlign(VAlign_Fill)
				.HAlign(HAlign_Left) 
				.AutoWidth()
				[
					SNew(SComboBox<FName>)
					.OptionsSource(Private::GetBindingModeNames())
					.InitiallySelectedItem(StaticEnum<EMVVMBindingMode>()->GetNameByValue((int64) ViewModelBinding->BindingType))
					.OnSelectionChanged(this, &SBindingRow::OnModeSelectionChanged)
					.OnGenerateWidget(this, &SBindingRow::GenerateModeWidget)
					.Content()
					[
						SNew(SBox)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.WidthOverride(16)
						.HeightOverride(16)
						[
							SNew(SImage)
							.Image(this, &SBindingRow::GetCurrentModeBrush)
						]
					]
				]
				
				+ SHorizontalBox::Slot()
				.Padding(2, 0)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Fill) 
				.AutoWidth()
				[
					SAssignNew(ViewModelFieldSelector, SFieldSelector, InWidgetBlueprint, true)
					.SelectedField(this, &SBindingRow::GetSelectedViewModelProperty)
					.BindingMode(this, &SBindingRow::GetCurrentBindingMode)
					.OnSelectionChanged(this, &SBindingRow::OnViewModelPropertySelectionChanged)
				]

				+ SHorizontalBox::Slot()
				.Padding(2, 1)
				.VAlign(VAlign_Fill)
				.HAlign(HAlign_Left)
				.AutoWidth()
				[
					SNew(SEnumComboBox, StaticEnum<EMVVMViewBindingUpdateMode>())
					.ContentPadding(FMargin(4, 0))
					.OnEnumSelectionChanged(this, &SBindingRow::OnUpdateModeSelectionChanged)
					.CurrentValue(this, &SBindingRow::GetUpdateModeValue)
				]
		
				+ SHorizontalBox::Slot()
				.Padding(2, 0)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Left)
				.AutoWidth()
				[
					SNew(SMVVMConversionPath, InWidgetBlueprint, false)
					.Bindings(this, &SBindingRow::GetThisViewBindingAsArray)
					.OnFunctionChanged(this, &SBindingRow::OnConversionFunctionChanged, false)
				]

				+ SHorizontalBox::Slot()
				.Padding(2, 0)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Left)
				.AutoWidth()
				[
					SNew(SMVVMConversionPath, InWidgetBlueprint, true)
					.Bindings(this, &SBindingRow::GetThisViewBindingAsArray)
					.OnFunctionChanged(this, &SBindingRow::OnConversionFunctionChanged, true)
				]

				+ SHorizontalBox::Slot() 
				.Padding(2, 0)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Right)
				.AutoWidth()
				[
					SAssignNew(ContextMenuOptionHelper, SButton)
					.ToolTipText(LOCTEXT("DropDownOptionsToolTip", "Context Menu for Binding"))
					.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
					.OnClicked(this, &SBindingRow::HandleDropDownOptionsPressed)
					[
						SNew(SBox)
						.Padding(FMargin(3, 0))
						[
							SNew(SImage)
							.Image(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SegmentedCombo.Right").DownArrowImage)
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				]
			],
			OwnerTableView
		);
	}

	~SBindingRow()
	{
		if (UWidgetBlueprint* WidgetBlueprint = WidgetBlueprintWeak.Get())
		{
			WidgetBlueprint->OnChanged().Remove(OnBlueprintChangedHandle);
		}
	}

	FMVVMBlueprintViewBinding* GetThisViewBinding() const
	{
		if (UMVVMBlueprintView* BlueprintViewPtr = BlueprintView.Get())
		{
			FMVVMBlueprintViewBinding* ViewBinding = Entry->GetBinding(BlueprintViewPtr);
			return ViewBinding;
		}

		return nullptr;
	}

	TArray<FMVVMBlueprintViewBinding*> GetThisViewBindingAsArray() const
	{
		TArray<FMVVMBlueprintViewBinding*> Result;
		if (FMVVMBlueprintViewBinding* ViewBinding = GetThisViewBinding())
		{
			Result.Add(ViewBinding);
		}
		return Result;
	}

private:

	ECheckBoxState IsBindingEnabled() const
	{
		if (const FMVVMBlueprintViewBinding* ViewModelBinding = GetThisViewBinding())
		{
			return ViewModelBinding->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}
		return ECheckBoxState::Undetermined;
	}

	ECheckBoxState IsBindingCompiled() const
	{
		if (const FMVVMBlueprintViewBinding* ViewModelBinding = GetThisViewBinding())
		{
			return ViewModelBinding->bCompile ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}
		return ECheckBoxState::Undetermined;
	}

	EVisibility GetErrorVisibility() const
	{
		return GetThisViewBinding()->Errors.IsEmpty() ? EVisibility::Hidden : EVisibility::Visible;
	}

	FText GetErrorToolTip() const
	{
		static const FText NewLineText = FText::FromString(TEXT("\n"));
		FText HintText = LOCTEXT("ErrorButtonText", "Errors: (Click to show in a separate window)");
		FText ErrorsText = FText::Join(NewLineText, GetThisViewBinding()->Errors);

		return FText::Join(NewLineText, HintText, ErrorsText);
	}

	FReply OnErrorButtonClicked()
	{
		ErrorDialog.Reset();
		ErrorItems.Reset();

		if (const FMVVMBlueprintViewBinding* ViewModelBinding = GetThisViewBinding())
		{
			for (const FText& ErrorText : ViewModelBinding->Errors)
			{
				ErrorItems.Add(MakeShared<FText>(ErrorText));
			}

			ErrorDialog = SNew(SCustomDialog)
				.Buttons({
					SCustomDialog::FButton(LOCTEXT("OK", "OK"))
				})
				.Content()
				[
					SNew(SListView<TSharedPtr<FText>>)
					.ListItemsSource(&ErrorItems)
					.OnGenerateRow(this, &SBindingRow::OnGenerateErrorRow)
				];

			ErrorDialog->Show();
		}

		return FReply::Handled();
	}

	EMVVMBindingMode GetCurrentBindingMode() const
	{
		const FMVVMBlueprintViewBinding* ViewModelBinding = GetThisViewBinding();
		return ViewModelBinding->BindingType;
	}

	FReply OnWidgetMenuAnchorClicked()
	{
		WidgetMenuAnchor->SetIsOpen(!WidgetMenuAnchor->IsOpen());
		return FReply::Handled();
	}

	TSharedRef<SWidget> OnGetWidgetMenuContent()
	{
		FMVVMBlueprintViewBinding* ViewBinding = GetThisViewBinding();

		UWidgetBlueprint* WidgetBlueprint = WidgetBlueprintWeak.Get();

		EFieldVisibility Flags = EFieldVisibility::None;
		if (UE::MVVM::IsForwardBinding(ViewBinding->BindingType))
		{
			Flags |= EFieldVisibility::Writable;
		}

		if (UE::MVVM::IsBackwardBinding(ViewBinding->BindingType))
		{
			Flags |= EFieldVisibility::Readable;

			if (!UE::MVVM::IsOneTimeBinding(ViewBinding->BindingType))
			{
				Flags |= EFieldVisibility::Notify;
			}
		}

		FBindingSource Source = FBindingSource::CreateForWidget(WidgetBlueprint, ViewBinding->WidgetPath.GetWidgetName());

		WidgetFieldIterator = MakeShared<UE::MVVM::FFieldIterator_Bindable>(WidgetBlueprint, Flags);
		
		return SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(8, 8, 8, 4))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				[
					SAssignNew(WidgetBindingList, SSourceBindingList, WidgetBlueprint)
					.InitialSource(Source)
					.ShowSearchBox(false)
					.OnDoubleClicked(this, &SBindingRow::OnWidgetPropertySelectionChanged)
					.FieldVisibilityFlags(Flags)
				]
				+ SVerticalBox::Slot()
				.Padding(4, 4, 4, 0)
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					[
						SNew(SButton)
						.OnClicked(this, &SBindingRow::OnWidgetButtonClicked, false)
						.HAlign(HAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("Cancel", "Cancel"))
						]
					]
					+ SHorizontalBox::Slot()
					.Padding(4, 0, 0, 0)
					[
						SNew(SButton)
						.OnClicked(this, &SBindingRow::OnWidgetClearButtonClicked)
						.HAlign(HAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("Clear", "Clear"))
						]
					]
					+ SHorizontalBox::Slot()
					.Padding(4, 0, 0, 0)
					[
						SNew(SPrimaryButton)
						.OnClicked(this, &SBindingRow::OnWidgetButtonClicked, true)
						.Text(LOCTEXT("Select", "Select"))
					]
				]
			];
	}

	TSharedRef<ITableRow> OnGenerateErrorRow(TSharedPtr<FText> Text, const TSharedRef<STableViewBase>& TableView) const
	{
		return SNew(STableRow<TSharedPtr<FText>>, TableView)
			.Content()
			[
				SNew(SEditableTextBox)
				.IsReadOnly(true)
				.Text(*Text.Get())
			];
	}

	void OnConversionFunctionChanged(const UFunction* Function, bool bSourceToDest)
	{
		if (FMVVMBlueprintViewBinding* ViewBinding = GetThisViewBinding())
		{
			UMVVMEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();

			if (bSourceToDest)
			{
				EditorSubsystem->SetSourceToDestinationConversionFunction(WidgetBlueprintWeak.Get(), *ViewBinding, Function);
			} 
			else
			{
				EditorSubsystem->SetDestinationToSourceConversionFunction(WidgetBlueprintWeak.Get(), *ViewBinding, Function);
			}
		}
	}

	TArray<FBindingSource> GetAvailableViewModels() const
	{
		UMVVMEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();
		return EditorSubsystem->GetAllViewModels(WidgetBlueprintWeak.Get());
	}

	FMVVMBlueprintPropertyPath GetSelectedViewModelProperty() const
	{
		if (FMVVMBlueprintViewBinding* ViewBinding = GetThisViewBinding())
		{
			return ViewBinding->ViewModelPath;
		}

		return FMVVMBlueprintPropertyPath();
	}

	FMVVMBlueprintPropertyPath GetSelectedWidgetProperty() const
	{
		if (FMVVMBlueprintViewBinding* ViewBinding = GetThisViewBinding())
		{
			return ViewBinding->WidgetPath;
		}

		return FMVVMBlueprintPropertyPath();
	}

	void OnViewModelPropertySelectionChanged(FMVVMBlueprintPropertyPath SelectedField)
	{
		if (FMVVMBlueprintViewBinding* ViewBinding = GetThisViewBinding())
		{
			if (ViewBinding->ViewModelPath != SelectedField)
			{
				UMVVMEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();
				Subsystem->SetViewModelPropertyForBinding(WidgetBlueprintWeak.Get(), *ViewBinding, SelectedField);
			}
		}
	}

	void OnWidgetPropertySelectionChanged(const FMVVMBlueprintPropertyPath& SelectedField)
	{
		if (FMVVMBlueprintViewBinding* ViewBinding = GetThisViewBinding())
		{
			if (ViewBinding->WidgetPath != SelectedField)
			{
				UMVVMEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();
				Subsystem->SetWidgetPropertyForBinding(WidgetBlueprintWeak.Get(), *ViewBinding, SelectedField);

				if (ViewModelFieldSelector.IsValid())
				{
					ViewModelFieldSelector->Refresh();
				}

				if (SelectedWidget.IsValid())
				{
					SelectedWidget->SetField(SelectedField);
				}
			}
		}
	}

	FReply OnWidgetButtonClicked(bool bSelect)
	{
		if (bSelect)
		{
			OnWidgetPropertySelectionChanged(WidgetBindingList->GetSelectedProperty());
		}

		WidgetMenuAnchor->SetIsOpen(false);
		return FReply::Handled();
	}

	FReply OnWidgetClearButtonClicked()
	{
		FMVVMBlueprintPropertyPath Path;
		Path.SetWidgetName(Entry->GetWidgetName());
		OnWidgetPropertySelectionChanged(Path);
		return FReply::Handled();
	}

	void OnUpdateModeSelectionChanged(int32 Value, ESelectInfo::Type)
	{
		if (FMVVMBlueprintViewBinding* ViewBinding = GetThisViewBinding())
		{
			UMVVMEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();
			Subsystem->SetUpdateModeForBinding(WidgetBlueprintWeak.Get(), *ViewBinding, (EMVVMViewBindingUpdateMode) Value);
		}			
	}

	int32 GetUpdateModeValue() const
	{
		return (int32) GetThisViewBinding()->UpdateMode;
	}

	void OnIsBindingEnableChanged(ECheckBoxState NewState)
	{
		if (NewState == ECheckBoxState::Undetermined)
		{
			return;
		}

		if (FMVVMBlueprintViewBinding* ViewBinding = GetThisViewBinding())
		{
			UMVVMEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();
			Subsystem->SetEnabledForBinding(WidgetBlueprintWeak.Get(), *ViewBinding, NewState == ECheckBoxState::Checked);
		}
	}

	void OnIsBindingCompileChanged(ECheckBoxState NewState)
	{
		if (NewState == ECheckBoxState::Undetermined)
		{
			return;
		}

		if (FMVVMBlueprintViewBinding* ViewBinding = GetThisViewBinding())
		{
			UMVVMEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();
			Subsystem->SetCompileForBinding(WidgetBlueprintWeak.Get(), *ViewBinding, NewState == ECheckBoxState::Checked);
		}
	}

	const FSlateBrush* GetModeBrush(EMVVMBindingMode BindingMode) const
	{
		switch (BindingMode)
		{
		case EMVVMBindingMode::OneTimeToDestination:
			return FMVVMEditorStyle::Get().GetBrush("BindingMode.OneTime");
		case EMVVMBindingMode::OneWayToDestination:
			return FMVVMEditorStyle::Get().GetBrush("BindingMode.OneWay");
		case EMVVMBindingMode::OneWayToSource:
			return FMVVMEditorStyle::Get().GetBrush("BindingMode.OneWayToSource");
		case EMVVMBindingMode::OneTimeToSource:
			return nullptr;
		case EMVVMBindingMode::TwoWay:
			return FMVVMEditorStyle::Get().GetBrush("BindingMode.TwoWay");
		default:
			return nullptr;
		}
	}

	const FSlateBrush* GetCurrentModeBrush() const
	{
		return GetModeBrush(GetThisViewBinding()->BindingType);
	}

	const FText& GetModeLabel(EMVVMBindingMode BindingMode) const
	{
		static FText OneTimeToDestinationLabel = LOCTEXT("OneTimeToDestinationLabel", "One Time To Widget");
		static FText OneWayToDestinationLabel = LOCTEXT("OneWayToDestinationLabel", "One Way To Widget");
		static FText OneWayToSourceLabel = LOCTEXT("OneWayToSourceLabel", "One Way To View Model");
		static FText OneTimeToSourceLabel = LOCTEXT("OneTimeToSourceLabel", "One Time To View Model");
		static FText TwoWayLabel = LOCTEXT("TwoWayLabel", "Two Way");

		switch (BindingMode)
		{
		case EMVVMBindingMode::OneTimeToDestination:
			return OneTimeToDestinationLabel;
		case EMVVMBindingMode::OneWayToDestination:
			return OneWayToDestinationLabel;
		case EMVVMBindingMode::OneWayToSource:
			return OneWayToSourceLabel;
		case EMVVMBindingMode::OneTimeToSource:
			return OneTimeToSourceLabel;
		case EMVVMBindingMode::TwoWay:
			return TwoWayLabel;
		default:
			return FText::GetEmpty();
		}
	}

	TSharedRef<SWidget> GenerateModeWidget(FName ValueName) const
	{
		const UEnum* ModeEnum = StaticEnum<EMVVMBindingMode>();
		int32 Index = ModeEnum->GetIndexByName(ValueName);
		EMVVMBindingMode MVVMBindingMode = EMVVMBindingMode(Index);
		return SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.WidthOverride(16)
				.HeightOverride(16)
				[
					SNew(SImage)
					.Image(GetModeBrush(MVVMBindingMode))
				]
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.0, 0, 0, 0)
			[
				SNew(STextBlock)
				.Text(GetModeLabel(MVVMBindingMode))
				.ToolTipText(ModeEnum->GetToolTipTextByIndex(Index))
			];
	}

	void OnModeSelectionChanged(FName ValueName, ESelectInfo::Type)
	{
		if (FMVVMBlueprintViewBinding* ViewBinding = GetThisViewBinding())
		{
			const UEnum* ModeEnum = StaticEnum<EMVVMBindingMode>();
			EMVVMBindingMode NewMode = (EMVVMBindingMode) ModeEnum->GetValueByName(ValueName);

			UMVVMEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();
			Subsystem->SetBindingTypeForBinding(WidgetBlueprintWeak.Get(), *ViewBinding, NewMode);

			if (ViewModelFieldSelector.IsValid())
			{
				ViewModelFieldSelector->Refresh();
			}
		}
	}

	FReply HandleDropDownOptionsPressed() 
	{
		if (TSharedPtr<ITypedTableView<TSharedPtr<FBindingEntry>>> ListView = OwnerTablePtr.Pin())
		{
			if (TSharedPtr<SBindingsList> ParentList = StaticCastSharedPtr<SBindingsList>(ListView->AsWidget()->GetParentWidget()))
			{
				// Get the context menu content. If invalid, don't open a menu.
				ListView->Private_SetItemSelection(Entry, true);
				TSharedPtr<SWidget> MenuContent = ParentList->OnSourceConstructContextMenu();

				if (MenuContent.IsValid())
				{
					const FVector2D& SummonLocation = ContextMenuOptionHelper->GetCachedGeometry().GetRenderBoundingRect().GetBottomLeft();
					FWidgetPath WidgetPath;
					FSlateApplication::Get().PushMenu(ParentList->AsShared(), WidgetPath, MenuContent.ToSharedRef(), SummonLocation, FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
				}
			}
		}

		return FReply::Handled();
	}

	void HandleBlueprintChanged(UBlueprint* Blueprint)
	{
		ViewModelFieldSelector->Refresh();
	}

private:
	TSharedPtr<FBindingEntry> Entry;
	TWeakObjectPtr<UWidgetBlueprint> WidgetBlueprintWeak;
	TWeakObjectPtr<UMVVMBlueprintView> BlueprintView;
	TSharedPtr<SFieldSelector> ViewModelFieldSelector;
	TSharedPtr<SWidget> ContextMenuOptionHelper;
	TSharedPtr<SCustomDialog> ErrorDialog;
	TArray<TSharedPtr<FText>> ErrorItems;
	FDelegateHandle OnBlueprintChangedHandle;
	TSharedPtr<UE::MVVM::FFieldIterator_Bindable> WidgetFieldIterator;
	TSharedPtr<SMenuAnchor> WidgetMenuAnchor;
	TSharedPtr<SSourceBindingList> WidgetBindingList;
	TSharedPtr<SFieldEntry> SelectedWidget;
};

void SBindingsList::Construct(const FArguments& InArgs, TSharedPtr<SBindingsPanel> Owner, UMVVMWidgetBlueprintExtension_View* InMVVMExtension)
{
	BindingPanel = Owner;
	MVVMExtension = InMVVMExtension;
	check(InMVVMExtension);

	MVVMExtension->OnBlueprintViewChangedDelegate().AddSP(this, &SBindingsList::Refresh);
	MVVMExtension->GetBlueprintView()->OnBindingsUpdated.AddSP(this, &SBindingsList::Refresh);
	MVVMExtension->GetBlueprintView()->OnViewModelsUpdated.AddSP(this, &SBindingsList::Refresh);

	ChildSlot
	[
		SAssignNew(TreeView, STreeView<TSharedPtr<FBindingEntry>>)
		.TreeItemsSource(&RootWidgets)
		.SelectionMode(ESelectionMode::Single)
		.OnGenerateRow(this, &SBindingsList::GenerateEntryRow)
		.OnGetChildren(this, &SBindingsList::GetChildrenOfEntry)
		.OnContextMenuOpening(this, &SBindingsList::OnSourceConstructContextMenu)
		.OnSelectionChanged(this, &SBindingsList::OnSourceListSelectionChanged)
		.ItemHeight(32)
	];

	Refresh();
}

SBindingsList::~SBindingsList()
{
	if (UMVVMWidgetBlueprintExtension_View* MVVMExtensionPtr = MVVMExtension.Get())
	{
		MVVMExtensionPtr->OnBlueprintViewChangedDelegate().RemoveAll(this);
		MVVMExtensionPtr->GetBlueprintView()->OnBindingsUpdated.RemoveAll(this);
		MVVMExtensionPtr->GetBlueprintView()->OnViewModelsUpdated.RemoveAll(this);
	}
}

void SBindingsList::GetChildrenOfEntry(TSharedPtr<FBindingEntry> Entry, TArray<TSharedPtr<FBindingEntry>>& OutChildren) const
{
	TConstArrayView<TSharedPtr<FBindingEntry>> Children = Entry->GetChildren();
	OutChildren.Append(Children.GetData(), Children.Num());
}

void SBindingsList::Refresh()
{
	UMVVMWidgetBlueprintExtension_View* MVVMExtensionPtr = MVVMExtension.Get();
	UMVVMBlueprintView* BlueprintView = MVVMExtensionPtr ? MVVMExtensionPtr->GetBlueprintView() : nullptr;

	// store the current binding index
	TArray<FMVVMBlueprintViewBinding*, TInlineAllocator<8>> SelectedBindings;

	if (TreeView.IsValid() && BlueprintView)
	{
		TArray<TSharedPtr<FBindingEntry>> SelectedItems = TreeView->GetSelectedItems();
		for (const TSharedPtr<FBindingEntry>& Entry : SelectedItems)
		{
			FMVVMBlueprintViewBinding* Binding = Entry->GetBinding(BlueprintView);
			if (Binding != nullptr)
			{
				SelectedBindings.Add(Binding);
			}
		}
	}

	RootWidgets.Reset();

	TArray<TSharedPtr<FBindingEntry>> SelectedEntries;

	// generate our entries
	// for each widget with bindings, create an entry at the root level
	// then add all bindings that reference that widget as its children
	if (BlueprintView)
	{
		TArrayView<FMVVMBlueprintViewBinding> Bindings = BlueprintView->GetBindings();
		for (int32 BindingIndex = 0; BindingIndex < Bindings.Num(); ++BindingIndex)
		{
			const FMVVMBlueprintViewBinding& Binding = Bindings[BindingIndex];
			
			FName WidgetName = Binding.WidgetPath.GetWidgetName();

			TSharedPtr<FBindingEntry> ExistingWidget;
			for (TSharedPtr<FBindingEntry> Widget : RootWidgets)
			{
				if (WidgetName == Widget->GetWidgetName())
				{
					ExistingWidget = Widget;
					break;
				}
			}

			if (!ExistingWidget.IsValid())
			{
				ExistingWidget = RootWidgets.Add_GetRef(MakeShared<FBindingEntry>(WidgetName));
			}

			TSharedRef<FBindingEntry> NewBindingEntry = MakeShared<FBindingEntry>(BindingIndex);
			ExistingWidget->AddChild(NewBindingEntry);

			if (SelectedBindings.Contains(&Binding))
			{
				SelectedEntries.Add(NewBindingEntry);
			}
		}
	}

	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();

		TreeView->SetItemSelection(SelectedEntries, true);

		for (const TSharedPtr<FBindingEntry>& Entry : RootWidgets)
		{
			Private::ExpandAll(TreeView, Entry);
		}
	}
}


TSharedRef<ITableRow> SBindingsList::GenerateEntryRow(TSharedPtr<FBindingEntry> Entry, const TSharedRef<STableViewBase>& OwnerTable) const
{
	TSharedPtr<ITableRow> Row;

	if (UMVVMWidgetBlueprintExtension_View* MVVMExtensionPtr = MVVMExtension.Get())
	{
		if (Entry->GetBinding(MVVMExtensionPtr->GetBlueprintView()) != nullptr)
		{
			Row = SNew(SBindingRow, OwnerTable, Entry, MVVMExtensionPtr->GetBlueprintView(), MVVMExtensionPtr->GetWidgetBlueprint());
		}
		else
		{
			Row = SNew(SWidgetRow, OwnerTable, Entry, MVVMExtensionPtr->GetWidgetBlueprint());
		}

		return Row.ToSharedRef();
	}

	ensureMsgf(false, TEXT("Failed to create binding or widget row."));
	return SNew(STableRow<TSharedPtr<FBindingEntry>>, OwnerTable);
}

TSharedPtr<SWidget> SBindingsList::OnSourceConstructContextMenu()
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, nullptr);

	TArray<TSharedPtr<FBindingEntry>> Selection = TreeView->GetSelectedItems();
	if (Selection.Num() > 0)
	{
		FUIAction RemoveAction;
		RemoveAction.ExecuteAction = FExecuteAction::CreateLambda([this, Selection]()
			{
				if (UMVVMWidgetBlueprintExtension_View* MVVMExtensionPtr = MVVMExtension.Get())
				{
					if (TSharedPtr<SBindingsPanel> BindingPanelPtr = BindingPanel.Pin())
					{
						BindingPanelPtr->OnBindingListSelectionChanged(TConstArrayView<FMVVMBlueprintViewBinding*>());
					}

					if (UMVVMBlueprintView* BlueprintView = MVVMExtensionPtr->GetBlueprintView())
					{
						for (const TSharedPtr<FBindingEntry>& Entry : Selection)
						{
							BlueprintView->RemoveBinding(Entry->GetBinding(BlueprintView));
						}
					}
				}
			});
		MenuBuilder.AddMenuEntry(LOCTEXT("RemoveBinding", "Remove Binding"), LOCTEXT("RemoveBindingTooltip", "Remove this binding."), FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"), RemoveAction);
	}

	return MenuBuilder.MakeWidget();
}

void SBindingsList::OnSourceListSelectionChanged(TSharedPtr<FBindingEntry> Entry, ESelectInfo::Type SelectionType) const
{
	if (bSelectionChangedGuard)
	{
		return;
	}
	TGuardValue<bool> ReentrantGuard(bSelectionChangedGuard, true);

	if (TSharedPtr<SBindingsPanel> BindingPanelPtr = BindingPanel.Pin())
	{
		if (UMVVMWidgetBlueprintExtension_View* MVVMExtensionPtr = MVVMExtension.Get())
		{
			if (UMVVMBlueprintView* View = MVVMExtensionPtr->GetBlueprintView())
			{
				TArray<TSharedPtr<FBindingEntry>> SelectedEntries = TreeView->GetSelectedItems();
				TArray<FMVVMBlueprintViewBinding*> SelectedBindings;

				for (const TSharedPtr<FBindingEntry>& SelectedEntry : SelectedEntries)
				{
					if (FMVVMBlueprintViewBinding* SelectedBinding = Entry->GetBinding(View))
					{
						SelectedBindings.Add(SelectedBinding);
					}
				}

				BindingPanelPtr->OnBindingListSelectionChanged(SelectedBindings);
			}
		}
	}
}

} // namespace UE::MVVM

#undef LOCTEXT_NAMESPACE
