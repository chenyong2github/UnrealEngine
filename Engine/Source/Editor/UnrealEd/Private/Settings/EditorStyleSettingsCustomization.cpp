// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorStyleSettingsCustomization.h"
#include "DetailCategoryBuilder.h"
#include "Styling/StyleColors.h"
#include "DetailLayoutBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "Widgets/Input/STextComboBox.h"
#include "DetailWidgetRow.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/SEditableTextBox.h"

#if ALLOW_THEMES
#include "IDesktopPlatform.h"
#include "DesktopPlatformModule.h"
#include "SPrimaryButton.h"
#include "HAL/FileManager.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "ThemeEditor"

TWeakPtr<SWindow> ThemeEditorWindow;
FString CurrentActiveThemeDisplayName;


class SThemeEditor : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SThemeEditor)
	{}
		SLATE_EVENT(FOnThemeEditorClosed, OnThemeEditorClosed);
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, TSharedRef<SWindow> InParentWindow)
	{
		OnThemeEditorClosed = InArgs._OnThemeEditorClosed;

		ParentWindow = InParentWindow;
		InParentWindow->SetOnWindowClosed(FOnWindowClosed::CreateSP(this, &SThemeEditor::OnParentWindowClosed));

		FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

		FDetailsViewArgs DetailsViewArgs;
		DetailsViewArgs.bAllowSearch = false;
		DetailsViewArgs.bShowOptions = false;
		DetailsViewArgs.bHideSelectionTip = true;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;

		TSharedRef<IDetailsView> DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);

		DetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([](const FPropertyAndParent& PropertyAndParent)
			{
				static const FName CurrentThemeIdName("CurrentThemeId");

				return PropertyAndParent.Property.GetFName() != CurrentThemeIdName;
			})
		);

		DetailsView->SetObject(&USlateThemeManager::Get());
		ChildSlot
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.Padding(6.0f, 3.0f)
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(0.6f)
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					.Padding(5.0f, 2.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ThemeName", "Name"))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(2.0f)
					.VAlign(VAlign_Center)
					.Padding(5.0f, 2.0f)
					[
						SAssignNew(EditableThemeName, SEditableTextBox)
						.Text(this, &SThemeEditor::GetThemeName)
						.OnTextChanged(this, &SThemeEditor::OnThemeNameChanged)
						.OnTextCommitted(this, &SThemeEditor::OnThemeNameCommitted)
						.SelectAllTextWhenFocused(true)
						//.IsReadOnly(true)
					]
				]
				/*+ SVerticalBox::Slot()
				.Padding(6.0f, 3.0f)
				.AutoHeight()
				[

					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(0.6f)
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					.Padding(5.0f, 2.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ThemeDescription", "Description"))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(2.0f)
					.VAlign(VAlign_Center)
					.Padding(5.0f, 2.0f)
					[
						SNew(SEditableTextBox)
						.Text(FText::FromString("Test Theme Description"))
						.IsReadOnly(true)
					]
				]*/
				+ SVerticalBox::Slot()
				.Padding(6.0f, 3.0f)
				[
					DetailsView
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Bottom)
				.Padding(6.0f, 3.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Bottom)
					.Padding(4, 3)
					[
						SNew(SPrimaryButton)
						.Text(LOCTEXT("SaveThemeButton", "Save"))
						.OnClicked(this, &SThemeEditor::OnSaveClicked)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Bottom)
					.Padding(4, 3)
					[
						SNew(SButton)
						.Text(LOCTEXT("CancelThemeEditingButton", "Cancel"))
						.OnClicked(this, &SThemeEditor::OnCancelClicked)
					]
				]
			]
		];

	}

private:
	FText GetThemeName() const
	{
		return USlateThemeManager::Get().GetCurrentTheme().DisplayName;
	}

	bool ValidateThemeName(const FText& ThemeName)
	{
		FText OutErrorMessage;
		FString	Filename = USlateThemeManager::Get().GetEngineThemeDir() / ThemeName.ToString() + TEXT(".json");

		// in cases the name user entered already exists when creating a new theme: 
		if (FPaths::FileExists(Filename))
		{
			// show error message whenever there's duplicate 
			OutErrorMessage = FText::Format(LOCTEXT("RenameThemeAlreadyExists", "A theme already exists with the name '{0}'."), ThemeName);
			EditableThemeName->SetError(OutErrorMessage);
			return false; 
		}
		EditableThemeName->SetError(FText::GetEmpty());
		return true; 
	}

	void OnThemeNameChanged(const FText& NewName)
	{
		// verify duplicates before setting the display name. 
		ValidateThemeName(NewName);
	}

	void OnThemeNameCommitted(const FText& NewName, ETextCommit::Type = ETextCommit::Default)
	{
		// if the new name is a valid name, write to display. Else, go back to the previous name. 
		if (ValidateThemeName(NewName))
		{
			USlateThemeManager::Get().SetCurrentThemeDisplayName(NewName);
		}
	}

	FReply OnSaveClicked()
	{
		FString Filename;
		bool bSuccess = true; 

		const FStyleTheme& Theme = USlateThemeManager::Get().GetCurrentTheme();

		// New theme being created: 
		if (Theme.Filename.IsEmpty())
		{
			TSharedPtr<SWindow> MyWindow = FSlateApplication::Get().FindWidgetWindow(SharedThis(this));

			TArray<FString> Filenames;
			IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
			Filename = USlateThemeManager::Get().GetEngineThemeDir() / Theme.DisplayName.ToString() + TEXT(".json");

			// if not a valid name, DO NOT SAVE. 
			if (!ValidateThemeName(Theme.DisplayName))
			{
				bSuccess = false;
			}
		}
		// Modifying an existing theme: 
		else
		{
			// updated name is not taken: SAVE. 
			if (ValidateThemeName(Theme.DisplayName))
			{
				Filename = Theme.Filename;
			}
			// updated name either already exists, or is the same as before: 
			else
			{		
				// updated name is the same as the current theme: SAVE anyway. 
				if (Theme.DisplayName.ToString().Equals(CurrentActiveThemeDisplayName))
				{
					Filename = Theme.Filename;
				}
				// updated name already exists: DON'T SAVE. 
				else
				{
					bSuccess = false;
				}
			}
		}

		if(!Filename.IsEmpty() && bSuccess)
		{
			USlateThemeManager::Get().SaveCurrentThemeAs(Filename);

			EditableThemeName->SetError(FText::GetEmpty()); 

			ParentWindow.Pin()->SetOnWindowClosed(FOnWindowClosed());
			ParentWindow.Pin()->RequestDestroyWindow();

			OnThemeEditorClosed.ExecuteIfBound(true);
		}
		return FReply::Handled();
	}

	FReply OnCancelClicked()
	{
		ParentWindow.Pin()->SetOnWindowClosed(FOnWindowClosed());
		ParentWindow.Pin()->RequestDestroyWindow();

		OnThemeEditorClosed.ExecuteIfBound(false);
		return FReply::Handled();
	}

	void OnParentWindowClosed(const TSharedRef<SWindow>&)
	{
		OnCancelClicked();
	}

private:
	FOnThemeEditorClosed OnThemeEditorClosed;
	TSharedPtr<SEditableTextBox> EditableThemeName; 
	TWeakPtr<SWindow> ParentWindow;
};

#undef LOCTEXT_NAMESPACE

#define LOCTEXT_NAMESPACE "EditorStyleSettingsCustomization"

TSharedRef<IPropertyTypeCustomization> FStyleColorListCustomization::MakeInstance()
{
	return MakeShared<FStyleColorListCustomization>();
}

void FStyleColorListCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{

}

void FStyleColorListCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	uint32 NumChildren = 0;
	TSharedPtr<IPropertyHandle> ColorArrayProperty = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FStyleColorList, StyleColors));

	ColorArrayProperty->GetNumChildren(NumChildren);
	for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
	{
		FResetToDefaultOverride ResetToDefaultOverride =
			FResetToDefaultOverride::Create(
				FIsResetToDefaultVisible::CreateSP(this, &FStyleColorListCustomization::IsResetToDefaultVisible, (EStyleColor)ChildIndex),
				FResetToDefaultHandler::CreateSP(this, &FStyleColorListCustomization::OnResetColorToDefault, (EStyleColor)ChildIndex));

		if (ChildIndex < (uint32)EStyleColor::User1)
		{
			IDetailPropertyRow& Row = ChildBuilder.AddProperty(ColorArrayProperty->GetChildHandle(ChildIndex).ToSharedRef());
			Row.OverrideResetToDefault(ResetToDefaultOverride);
		}
		else
		{
			// user colors are added if they have been customized with a display name
			FText DisplayName = USlateThemeManager::Get().GetColorDisplayName((EStyleColor)ChildIndex);
			if (!DisplayName.IsEmpty())
			{
				IDetailPropertyRow& Row = ChildBuilder.AddProperty(ColorArrayProperty->GetChildHandle(ChildIndex).ToSharedRef());
				Row.DisplayName(DisplayName);
				Row.OverrideResetToDefault(ResetToDefaultOverride);
			}
		}
	}
}


void FStyleColorListCustomization::OnResetColorToDefault(TSharedPtr<IPropertyHandle> Handle, EStyleColor Color)
{
	FLinearColor CurrentColor = USlateThemeManager::Get().GetColor(Color);
	const FStyleTheme& Theme = USlateThemeManager::Get().GetCurrentTheme();
	if (Theme.LoadedDefaultColors.Num())
	{
		USlateThemeManager::Get().ResetActiveColorToDefault(Color);
	}
}

bool FStyleColorListCustomization::IsResetToDefaultVisible(TSharedPtr<IPropertyHandle> Handle, EStyleColor Color)
{
	FLinearColor CurrentColor = USlateThemeManager::Get().GetColor(Color);
	const FStyleTheme& Theme = USlateThemeManager::Get().GetCurrentTheme();
	if (Theme.LoadedDefaultColors.Num())
	{
		return Theme.LoadedDefaultColors[(int32)Color] != CurrentColor;
	}

	return false;
}

TSharedRef<IDetailCustomization> FEditorStyleSettingsCustomization::MakeInstance()
{
	return MakeShared<FEditorStyleSettingsCustomization>();
}


void FEditorStyleSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	IDetailCategoryBuilder& ColorCategory = DetailLayout.EditCategory("Colors");

	TArray<UObject*> Objects = { &USlateThemeManager::Get() };

	if (IDetailPropertyRow* ThemeRow = ColorCategory.AddExternalObjectProperty(Objects, "CurrentThemeId"))
	{
		MakeThemePickerRow(*ThemeRow);
	}
}

void FEditorStyleSettingsCustomization::RefreshComboBox()
{
	TSharedPtr<FString> SelectedTheme;
	GenerateThemeOptions(SelectedTheme);
	ComboBox->RefreshOptions();
	ComboBox->SetSelectedItem(SelectedTheme);
}

void FEditorStyleSettingsCustomization::GenerateThemeOptions(TSharedPtr<FString>& OutSelectedTheme)
{
	const TArray<FStyleTheme>& Themes = USlateThemeManager::Get().GetThemes();

	ThemeOptions.Empty(Themes.Num());
	int32 Index = 0;
	for (const FStyleTheme& Theme : Themes)
	{
		TSharedRef<FString> ThemeString = MakeShared<FString>(FString::FromInt(Index));

		if (USlateThemeManager::Get().GetCurrentTheme() == Theme)
		{
			OutSelectedTheme = ThemeString;
		}

		ThemeOptions.Add(ThemeString);
		++Index;
	}

}

void FEditorStyleSettingsCustomization::MakeThemePickerRow(IDetailPropertyRow& PropertyRow)
{

	TSharedPtr<FString> SelectedItem;
	GenerateThemeOptions(SelectedItem);

	// Make combo choices
	ComboBox =
		SNew(STextComboBox)
		.OptionsSource(&ThemeOptions)
		.InitiallySelectedItem(SelectedItem)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.OnGetTextLabelForItem(this, &FEditorStyleSettingsCustomization::GetTextLabelForThemeEntry)
		.OnSelectionChanged(this, &FEditorStyleSettingsCustomization::OnThemePicked);


	FDetailWidgetRow& CustomWidgetRow = PropertyRow.CustomWidget(false);

	CustomWidgetRow
	.NameContent()
	[
		PropertyRow.GetPropertyHandle()->CreatePropertyNameWidget(LOCTEXT("ActiveThemeDisplayName", "Active Theme"))
	]
	.ValueContent()
	.MaxDesiredWidth(350.f)
	[
		SNew(SHorizontalBox)
		.IsEnabled(this, &FEditorStyleSettingsCustomization::IsThemeEditingEnabled)
		+SHorizontalBox::Slot()
		[
			SNew(SBox)
			.WidthOverride(125.f)
			[
				ComboBox.ToSharedRef()
			]
		]
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		.AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(LOCTEXT("EditThemeToolTip", "Edit this theme"))
			.OnClicked(this, &FEditorStyleSettingsCustomization::OnEditThemeClicked)
			[
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Image(FAppStyle::Get().GetBrush("Icons.Edit"))
			]
		]
		+SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		.AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(LOCTEXT("DuplicateThemeToolTip", "Duplicate this theme and edit it"))
			.OnClicked(this, &FEditorStyleSettingsCustomization::OnDuplicateAndEditThemeClicked)
			[
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Image(FAppStyle::Get().GetBrush("Icons.Duplicate"))
			]
		]
		+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			.AutoWidth()
			[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.Visibility_Lambda(
				[]()
				{
					// if current active theme is the default dark theme: hide the delete button. 
					return USlateThemeManager::Get().IsDefaultThemeActive() ? EVisibility::Hidden : EVisibility::Visible;
				})
			.ToolTipText(LOCTEXT("DeleteThemeToolTip", "Delete this theme"))
			.OnClicked(this, &FEditorStyleSettingsCustomization::OnDeleteThemeClicked)
			[
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Image(FAppStyle::Get().GetBrush("Icons.Delete"))
			]
		]
	];
}

static void OnThemeEditorClosed(bool bSaved, TWeakPtr<FEditorStyleSettingsCustomization> ActiveCustomization, FGuid CreatedThemeId, FGuid PreviousThemeId)
{
	if (!bSaved)
	{
		if (PreviousThemeId.IsValid())
		{
	
			USlateThemeManager::Get().ApplyTheme(PreviousThemeId);

			if (CreatedThemeId.IsValid())
			{
				USlateThemeManager::Get().RemoveTheme(CreatedThemeId);

			}
			if (ActiveCustomization.IsValid())
			{

				ActiveCustomization.Pin()->RefreshComboBox();
			}
		}
		else
		{
			for (int32 ColorIndex = 0; ColorIndex < (int32)EStyleColor::MAX; ++ColorIndex)
			{
				USlateThemeManager::Get().ResetActiveColorToDefault((EStyleColor)ColorIndex);
			}
		}
	}
}

FReply FEditorStyleSettingsCustomization::OnDeleteThemeClicked()
{
	const FStyleTheme PreviouslyActiveTheme = USlateThemeManager::Get().GetCurrentTheme();

	// Are you sure you want to do this?
	const FText FileNameToRemove = FText::FromString(PreviouslyActiveTheme.DisplayName.ToString());
	const FText TextBody = FText::Format(LOCTEXT("ActionRemoveMsg", "Are you sure you want to permanently delete the theme \"{0}\"? This action cannot be undone."), FileNameToRemove);
	const FText TextTitle = FText::Format(LOCTEXT("RemoveTheme_Title", "Remove Theme \"{0}\"?"), FileNameToRemove);

	// If user select "OK"...
	if (EAppReturnType::Ok == FMessageDialog::Open(EAppMsgType::OkCancel, TextBody, &TextTitle))
	{
		// apply default theme
		USlateThemeManager::Get().ApplyDefaultTheme();

		// remove previously active theme
		const FString Filename = USlateThemeManager::Get().GetEngineThemeDir() / PreviouslyActiveTheme.DisplayName.ToString() + TEXT(".json");
		IFileManager::Get().Delete(*Filename);
		USlateThemeManager::Get().RemoveTheme(PreviouslyActiveTheme.Id);
		RefreshComboBox();
	}
	// Else, do nothing. 
	return FReply::Handled();
}

FReply FEditorStyleSettingsCustomization::OnDuplicateAndEditThemeClicked()
{
	FGuid PreviouslyActiveTheme = USlateThemeManager::Get().GetCurrentTheme().Id;

	CurrentActiveThemeDisplayName = USlateThemeManager::Get().GetCurrentTheme().DisplayName.ToString(); 

	FGuid NewThemeId = USlateThemeManager::Get().DuplicateActiveTheme();
	USlateThemeManager::Get().ApplyTheme(NewThemeId);

	RefreshComboBox();

	OpenThemeEditorWindow(FOnThemeEditorClosed::CreateStatic(&OnThemeEditorClosed, TWeakPtr<FEditorStyleSettingsCustomization>(SharedThis(this)), NewThemeId, PreviouslyActiveTheme));

	return FReply::Handled();
}

FReply FEditorStyleSettingsCustomization::OnEditThemeClicked()
{

	CurrentActiveThemeDisplayName = USlateThemeManager::Get().GetCurrentTheme().DisplayName.ToString();

	OpenThemeEditorWindow(FOnThemeEditorClosed::CreateStatic(&OnThemeEditorClosed, TWeakPtr<FEditorStyleSettingsCustomization>(SharedThis(this)), FGuid(), FGuid()));

	return FReply::Handled();
}

FString FEditorStyleSettingsCustomization::GetTextLabelForThemeEntry(TSharedPtr<FString> Entry)
{
	const TArray<FStyleTheme>& Themes = USlateThemeManager::Get().GetThemes();
	return Themes[TCString<TCHAR>::Atoi(**Entry)].DisplayName.ToString();
}

void FEditorStyleSettingsCustomization::OnThemePicked(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	// If set directly in code, the theme was already applied
	if(SelectInfo != ESelectInfo::Direct)
	{
		const TArray<FStyleTheme>& Themes = USlateThemeManager::Get().GetThemes();

		USlateThemeManager::Get().ApplyTheme(Themes[TCString<TCHAR>::Atoi(**NewSelection)].Id);
	}
}

void FEditorStyleSettingsCustomization::OpenThemeEditorWindow(FOnThemeEditorClosed OnThemeEditorClosed)
{
	if(!ThemeEditorWindow.IsValid())
	{
		TSharedRef<SWindow> NewWindow = SNew(SWindow)
			.Title(LOCTEXT("ThemeEditorWindowTitle", "Theme Editor"))
			.SizingRule(ESizingRule::UserSized)
			.ClientSize(FVector2D(600, 600))
			.SupportsMaximize(false)
			.SupportsMinimize(false);

		TSharedRef<SThemeEditor> ThemeEditor =
			SNew(SThemeEditor, NewWindow)
			.OnThemeEditorClosed(OnThemeEditorClosed);
			
		NewWindow->SetContent(
			ThemeEditor
		);

		if (TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(ComboBox.ToSharedRef()))
		{
			FSlateApplication::Get().AddWindowAsNativeChild(NewWindow, ParentWindow.ToSharedRef());
		}
		else
		{
			FSlateApplication::Get().AddWindow(NewWindow);
		}

		ThemeEditorWindow = NewWindow;
	}


}

bool FEditorStyleSettingsCustomization::IsThemeEditingEnabled() const
{
	// Don't allow changing themes while editing them
	return !ThemeEditorWindow.IsValid();
}

#undef LOCTEXT_NAMESPACE

#endif // ALLOW_THEMES
