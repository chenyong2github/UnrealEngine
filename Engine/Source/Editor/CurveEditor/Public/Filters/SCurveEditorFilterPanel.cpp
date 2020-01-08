// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCurveEditorFilterPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Layout/SBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "CurveEditorFilterBase.h"
#include "PropertyEditorModule.h"
#include "ClassViewerFilter.h"
#include "ClassViewerModule.h"
#include "CurveEditor.h"

TWeakPtr<SWindow> SCurveEditorFilterPanel::ExistingFilterWindow;

/** ClassViewerFilter for Animation Modifier classes */
class FCurveFilterClassFilter : public IClassViewerFilter
{
public:
	bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
	{
		return InClass->IsChildOf(UCurveEditorFilterBase::StaticClass()) && InClass != UCurveEditorFilterBase::StaticClass();
	}

	virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef<const IUnloadedBlueprintData> InClass, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
	{
		return InClass->IsChildOf(UCurveEditorFilterBase::StaticClass());
	}
};

void SCurveEditorFilterPanel::Construct(const FArguments& InArgs, TSharedRef<FCurveEditor> InCurveEditor)
{
	WeakCurveEditor = InCurveEditor;

	FClassViewerInitializationOptions Options;
	Options.bShowUnloadedBlueprints = true;
	Options.bShowNoneOption = false;
	// Options.Mode = EClassViewerMode::ClassBrowsing;
	Options.bAllowViewOptions = false;
	Options.NameTypeToDisplay = EClassViewerNameTypeToDisplay::DisplayName;

	TSharedPtr<FCurveFilterClassFilter> ClassFilter = MakeShared<FCurveFilterClassFilter>();
	Options.ClassFilter = ClassFilter;

	FOnClassPicked OnPicked(FOnClassPicked::CreateSP(this, &SCurveEditorFilterPanel::SetFilterClass));


	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");
	
	// Configure the Details View
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bUpdatesFromSelection = false;
	DetailsViewArgs.bLockable = false;
	DetailsViewArgs.ViewIdentifier = "CurveEditorFilterPanel";
	DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.bShowPropertyMatrixButton = false;
	DetailsViewArgs.bShowOptions = false;

	// Generate it and store a reference so we can update it with the right object later.
	DetailView = PropertyEditor.CreateDetailView(DetailsViewArgs);
	TSharedRef<SWidget> ClassFilterWidget = ClassViewerModule.CreateClassViewer(Options, OnPicked);

	ChildSlot
	[
		SNew(SVerticalBox)

		// Class Picker to choose which class shows up in the Details Panel
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			ClassFilterWidget
		]
		// Details Panel
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			DetailView.ToSharedRef()
		]
		
		// Footer Row
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		[			
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(4, 0, 0, 0)
			.FillWidth(1.0f)
			[
				// Unfortunately the Class Filter can't have a class selected by default so 
				// when the panel is opened you don't know what filter has been pre-selected.
				// We're solving this for now by putting a textbox that shows your current filter
				// by name.
				SNew(STextBlock)
				.Text(this, &SCurveEditorFilterPanel::GetCurrentFilterText)
				.TextStyle(FEditorStyle::Get(), "LargeText")
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				// Apply Button
				SNew(SButton)
				.Text(NSLOCTEXT("CurveEditorFilterPanel", "ApplyFilter", "Apply"))
				.OnClicked(this, &SCurveEditorFilterPanel::OnApplyClicked)
				.IsEnabled(this, &SCurveEditorFilterPanel::CanApplyFilter)
			]
		]
	];
}

void SCurveEditorFilterPanel::SetFilterClass(UClass* InClass)
{
	UObject* ClassCDO = InClass->GetDefaultObject<UObject>();
	DetailView->SetObject(ClassCDO);
}

FReply SCurveEditorFilterPanel::OnApplyClicked()
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (CurveEditor && DetailView->GetSelectedObjects().Num() > 0)
	{
		const TMap<FCurveModelID, FKeyHandleSet>& SelectedKeys = CurveEditor->GetSelection().GetAll();
		
		FText TransactionText = FText::Format(NSLOCTEXT("CurveEditorFilterApply", "Filter Curves", "Filtered {0}|plural(one=Curve, other=Curves)"), CurveEditor->GetSelection().Count());
		FScopedTransaction Transaction(TransactionText);

		UCurveEditorFilterBase* Filter = Cast<UCurveEditorFilterBase>(DetailView->GetSelectedObjects()[0]);
		TMap<FCurveModelID, FKeyHandleSet> OutKeysToSelect;
		Filter->ApplyFilter(CurveEditor.ToSharedRef(), SelectedKeys, OutKeysToSelect);

		// Clear their selection and then set it to the keys the filter thinks you should have selected.
		CurveEditor->GetSelection().Clear();

		for (const TTuple<FCurveModelID, FKeyHandleSet>& OutSet : OutKeysToSelect)
		{
			CurveEditor->GetSelection().Add(OutSet.Key, ECurvePointType::Key, OutSet.Value.AsArray());
		}
	}

	return FReply::Handled();
}

bool SCurveEditorFilterPanel::CanApplyFilter() const
{
	bool bHasSelection = false;
	bool bHasFilter = false;

	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (CurveEditor.IsValid())
	{
		bHasSelection = CurveEditor->GetSelection().Count() > 0;
	}

	bHasFilter = DetailView->GetSelectedObjects().Num() > 0;

	return bHasSelection && bHasFilter;
}


void SCurveEditorFilterPanel::OpenDialog(const TSharedRef<FTabManager>& TabManager, TSharedRef<FCurveEditor> InHostCurveEditor, TSubclassOf<UCurveEditorFilterBase> DefaultFilterClass)
{
	TSharedPtr<SWindow> ExistingWindow = ExistingFilterWindow.Pin();
	if (ExistingWindow.IsValid())
	{
		ExistingWindow->BringToFront();
	}
	else
	{
		ExistingWindow = SNew(SWindow)
			.Title(NSLOCTEXT("CurveEditorFilterPanel", "WindowTitle", "Curve Editor Filters"))
			.HasCloseButton(true)
			.SupportsMaximize(false)
			.SupportsMinimize(false)
			.ClientSize(FVector2D(480, 360));

		TSharedPtr<SDockTab> OwnerTab = TabManager->GetOwnerTab();
		TSharedPtr<SWindow> RootWindow = OwnerTab.IsValid() ? OwnerTab->GetParentWindow() : TSharedPtr<SWindow>();
		if (RootWindow.IsValid())
		{
			FSlateApplication::Get().AddWindowAsNativeChild(ExistingWindow.ToSharedRef(), RootWindow.ToSharedRef());
		}
		else
		{
			FSlateApplication::Get().AddWindow(ExistingWindow.ToSharedRef());
		}
	}

	TSharedPtr<SCurveEditorFilterPanel> FilterPanel;
	ExistingWindow->SetContent(
		SAssignNew(FilterPanel, SCurveEditorFilterPanel, InHostCurveEditor)
	);

	FilterPanel->SetFilterClass(DefaultFilterClass);
	ExistingFilterWindow = ExistingWindow;
}

void SCurveEditorFilterPanel::CloseDialog()
{
	TSharedPtr<SWindow> ExistingWindow = ExistingFilterWindow.Pin();
	if (ExistingWindow.IsValid())
	{
		ExistingWindow->RequestDestroyWindow();
		ExistingWindow = nullptr;
	}
}

FText SCurveEditorFilterPanel::GetCurrentFilterText() const
{
	FText CurrentFilterName = NSLOCTEXT("SCurveEditorFilterPanel", "NoFilterSelectedName", "None");
	if (DetailView->GetSelectedObjects().Num() > 0)
	{
		UCurveEditorFilterBase* CurrentFilter = Cast<UCurveEditorFilterBase>(DetailView->GetSelectedObjects()[0].Get());
		if (CurrentFilter)
		{
			CurrentFilterName = CurrentFilter->GetClass()->GetDisplayNameText();
		}
	}

	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (CurveEditor.IsValid())
	{
		int32 SelectedKeysCount = CurveEditor->GetSelection().Count();
		if (SelectedKeysCount > 0)
		{
			const FText FormatString = NSLOCTEXT("SCurveEditorFilterPanel", "CurrentFilterWithKeysFormat", "Current Filter: {0} ({1} Keys)");
			return FText::Format(FormatString, CurrentFilterName, SelectedKeysCount);
		}
		else
		{
			const FText FormatString = NSLOCTEXT("SCurveEditorFilterPanel", "CurrentFilterNoKeysFormat", "Current Filter: {0} (No Keys Selected)");
			return FText::Format(FormatString, CurrentFilterName);
		}
	}

	return FText();
}
