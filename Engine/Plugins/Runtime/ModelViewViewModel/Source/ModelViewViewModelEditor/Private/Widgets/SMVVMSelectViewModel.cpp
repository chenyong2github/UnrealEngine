// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMVVMSelectViewModel.h"

#include "Bindings/MVVMBindingHelper.h"
#include "Components/Widget.h"
#include "Editor.h"
#include "FieldNotification/IFieldValueChanged.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"
#include "MVVMBlueprintViewModelContext.h"
#include "MVVMEditorSubsystem.h"
#include "MVVMViewModelBase.h"
#include "SPrimaryButton.h"
#include "Styling/MVVMEditorStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SMVVMViewModelBindingListWidget.h"

#include "ClassViewerModule.h"
#include "ClassViewerFilter.h"
#include "SClassViewer.h"

#define LOCTEXT_NAMESPACE "SMVVMSelectViewModel"

namespace UE::MVVM
{

namespace Private
{
	class FSelectViewModelClassFilter : public IClassViewerFilter
	{
	public:
		FSelectViewModelClassFilter() = default;
		static const EClassFlags ClassFlag = CLASS_HideDropDown | CLASS_Hidden | CLASS_Deprecated;
		virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
		{
			if (InClass->IsChildOf(UWidget::StaticClass()))
			{
				return false;
			}
			return InClass->ImplementsInterface(UNotifyFieldValueChanged::StaticClass()) && !InClass->HasAnyClassFlags(ClassFlag);
		}

		virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef<const IUnloadedBlueprintData> InUnloadedClassData, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
		{
			if (InUnloadedClassData->IsChildOf(UWidget::StaticClass()))
			{
				return false;
			}
			return InUnloadedClassData->ImplementsInterface(UNotifyFieldValueChanged::StaticClass()) && !InUnloadedClassData->HasAnyClassFlags(ClassFlag);
		}
	};
} //namespace

void SMVVMSelectViewModel::Construct(const FArguments& InArgs, const UWidgetBlueprint* WidgetBlueprint)
{
	OnCancel = InArgs._OnCancel;
	OnViewModelCommitted = InArgs._OnViewModelCommitted;

	TSharedRef<SWidget> ButtonsPanelContent = SNew(SUniformGridPanel)
		.SlotPadding(FAppStyle::Get().GetMargin("StandardDialog.SlotPadding"))
		+ SUniformGridPanel::Slot(0, 0)
		[
			SNew(SPrimaryButton)
			.Text(LOCTEXT("ViewModelAddButtonText", "Ok"))
			.OnClicked(this, &SMVVMSelectViewModel::HandleAccepted)
			.IsEnabled(this, &SMVVMSelectViewModel::HandelIsSelectionEnabled)
		]
		+ SUniformGridPanel::Slot(1, 0)
		[
			SNew(SButton)
			.Text(LOCTEXT("ViewModelCancelButtonText", "Cancel"))
			.HAlign(HAlign_Center)
			.OnClicked(this, &SMVVMSelectViewModel::HandleCancel)
		];

	FClassViewerInitializationOptions ClassViewerOptions;
	ClassViewerOptions.DisplayMode = EClassViewerDisplayMode::TreeView;
	ClassViewerOptions.Mode = EClassViewerMode::ClassPicker;
	ClassViewerOptions.ClassFilters.Add(MakeShared<Private::FSelectViewModelClassFilter>());

	ClassViewer = StaticCastSharedRef<SClassViewer>(FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer").CreateClassViewer(ClassViewerOptions, FOnClassPicked::CreateSP(this, &SMVVMSelectViewModel::HandleClassPicked)));

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SSplitter)
				.PhysicalSplitterHandleSize(2.0f)
				+ SSplitter::Slot()
				.Value(0.6f)
				[
					SNew(SBorder)
					.BorderImage(FStyleDefaults::GetNoBrush())
					[
						ClassViewer.ToSharedRef()
					]
				]
				+ SSplitter::Slot()
				.Value(0.4f)
				[
					SNew(SBorder)
					.BorderImage(FStyleDefaults::GetNoBrush())
					[
						SAssignNew(ViewModelBindingListWidget, SViewModelBindingListWidget, WidgetBlueprint)
					]
				]
			]
			+ SVerticalBox::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Bottom)
			.AutoHeight()
			.Padding(8)
			[
				ButtonsPanelContent
			]
		]
	];
}

void SMVVMSelectViewModel::HandleClassPicked(UClass* ClassPicked)
{
	SelectedClass = ClassPicked;
	ViewModelBindingListWidget->SetViewModel(ClassPicked, FName(), FGuid());
}


FReply SMVVMSelectViewModel::HandleAccepted()
{
	OnViewModelCommitted.ExecuteIfBound(SelectedClass.Get());
	return FReply::Handled();
}


FReply SMVVMSelectViewModel::HandleCancel()
{
	OnCancel.ExecuteIfBound();
	return FReply::Handled();
}


bool SMVVMSelectViewModel::HandelIsSelectionEnabled() const
{
	return SelectedClass.Get() != nullptr;
}

} //namespace

#undef LOCTEXT_NAMESPACE
