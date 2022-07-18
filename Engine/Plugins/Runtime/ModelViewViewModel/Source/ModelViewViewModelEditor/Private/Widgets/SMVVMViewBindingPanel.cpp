// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMVVMViewBindingPanel.h"

#include "Widgets/SMVVMManageViewModelsWidget.h"
#include "Widgets/SMVVMViewModelContextListWidget.h"
#include "Widgets/SMVVMViewBindingListView.h"

#include "WidgetBlueprintEditor.h"
#include "WidgetBlueprintToolMenuContext.h"
#include "MVVMBlueprintView.h"
#include "MVVMWidgetBlueprintExtension_View.h"
#include "Customizations/MVVMConversionPathCustomization.h"
#include "Customizations/MVVMPropertyPathCustomization.h"

#include "Styling/AppStyle.h"
#include "IStructureDetailsView.h"
#include "PropertyEditorModule.h"
#include "ToolMenuContext.h"
#include "ToolMenus.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/MVVMEditorStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSplitter.h"

#include "StatusBarSubsystem.h"
#include "Tabs/MVVMBindingSummoner.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "BindingPanel"

namespace UE::MVVM
{

/** */
void SBindingsPanel::Construct(const FArguments& InArgs, TSharedPtr<FWidgetBlueprintEditor> WidgetBlueprintEditor, bool bInIsDrawerTab)
{
	WeakBlueprintEditor = WidgetBlueprintEditor;
	bIsDrawerTab = bInIsDrawerTab;

	UWidgetBlueprint* WidgetBlueprint = WidgetBlueprintEditor->GetWidgetBlueprintObj();
	check(WidgetBlueprint);

	UMVVMWidgetBlueprintExtension_View* MVVMExtensionPtr = UMVVMWidgetBlueprintExtension_View::GetExtension<UMVVMWidgetBlueprintExtension_View>(WidgetBlueprint);
	MVVMExtension = MVVMExtensionPtr;
	if (MVVMExtensionPtr)
	{
		BlueprintViewChangedDelegateHandle = MVVMExtensionPtr->OnBlueprintViewChangedDelegate().AddSP(this, &SBindingsPanel::HandleBlueprintViewChangedDelegate);
	}
	else
	{
		WidgetBlueprint->OnExtensionAdded.AddRaw(this, &SBindingsPanel::HandleExtensionAdded);
	}

	{
		// Connection Settings
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

		FDetailsViewArgs DetailsViewArgs;
		DetailsViewArgs.bUpdatesFromSelection = false;
		DetailsViewArgs.bLockable = false;
		DetailsViewArgs.bShowPropertyMatrixButton = false;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		DetailsViewArgs.ViewIdentifier = NAME_None;
		DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
		
		FStructureDetailsViewArgs StructureDetailsViewArgs;
		StructDetailsView = PropertyEditorModule.CreateStructureDetailView(DetailsViewArgs, StructureDetailsViewArgs, TSharedPtr<FStructOnScope>());
		StructDetailsView->GetDetailsView()->RegisterInstancedCustomPropertyTypeLayout(FMVVMBlueprintPropertyPath::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&UE::MVVM::FPropertyPathCustomization::MakeInstance, WidgetBlueprint));
		StructDetailsView->GetDetailsView()->RegisterInstancedCustomPropertyTypeLayout(FMVVMBlueprintViewConversionPath::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&UE::MVVM::FConversionPathCustomization::MakeInstance, WidgetBlueprint));
	}

	HandleBlueprintViewChangedDelegate();
}


SBindingsPanel::~SBindingsPanel()
{
	if (UMVVMWidgetBlueprintExtension_View* Extension = MVVMExtension.Get())
	{
		Extension->OnBlueprintViewChangedDelegate().Remove(BlueprintViewChangedDelegateHandle);
	}
	if (TSharedPtr<FWidgetBlueprintEditor> WidgetEditor = WeakBlueprintEditor.Pin())
	{
		UWidgetBlueprint* WidgetBlueprint = WidgetEditor->GetWidgetBlueprintObj();
		if (WidgetBlueprint)
		{
			WidgetBlueprint->OnExtensionAdded.RemoveAll(this);
		}
	}
}

FReply SBindingsPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	FReply Reply = FReply::Unhandled();
	if (TSharedPtr<FWidgetBlueprintEditor> WidgetEditor = WeakBlueprintEditor.Pin())
	{
		if (WidgetEditor->GetToolkitCommands()->ProcessCommandBindings(InKeyEvent))
		{
			Reply = FReply::Handled();
		}
	}

	return Reply;
}

bool SBindingsPanel::SupportsKeyboardFocus() const
{
	return true;
}

void SBindingsPanel::HandleBlueprintViewChangedDelegate()
{
	UMVVMWidgetBlueprintExtension_View* MVVMExtensionPtr = MVVMExtension.Get();

	bool bHasExtension = MVVMExtensionPtr && MVVMExtensionPtr->GetBlueprintView() != nullptr;
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			bHasExtension ? GenerateEditViewWidget() : GenerateCreateViewWidget()
		]
		+SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		.Padding(FMargin(bHasExtension ? 160.0 : 24.0, 10.0))
		[
			CreateDrawerDockButton()
		]
	];
}


void SBindingsPanel::OnBindingListSelectionChanged(TConstArrayView<FMVVMBlueprintViewBinding*> Selection)
{
	if (FMVVMBlueprintViewBinding* Binding = Selection.Num() > 0 ? Selection.Last() : nullptr)
	{
		TSharedRef<FStructOnScope> StructScope = MakeShared<FStructOnScope>(FMVVMBlueprintViewBinding::StaticStruct(), reinterpret_cast<uint8*>(Binding));
		StructDetailsView->SetStructureData(StructScope);
		DetailContainer->SetContent(StructDetailsView->GetWidget().ToSharedRef());
	}
	else
	{
		// show empty details view
		DetailsView->SetObject(nullptr);
		DetailContainer->SetContent(DetailsView.ToSharedRef());
	}
}

void SBindingsPanel::AddDefaultBinding()
{
	if (UMVVMWidgetBlueprintExtension_View* MVVMExtensionPtr = MVVMExtension.Get())
	{
		if (UMVVMBlueprintView* EditorData = MVVMExtensionPtr->GetBlueprintView())
		{
			EditorData->AddDefaultBinding();
			BindingsList->Refresh();
		}
	}
}

bool SBindingsPanel::CanAddBinding() const
{
	if (UMVVMWidgetBlueprintExtension_View* MVVMExtensionPtr = MVVMExtension.Get())
	{
		if (UMVVMBlueprintView* BlueprintView = MVVMExtensionPtr->GetBlueprintView())
		{
			return BlueprintView->GetViewModels().Num() > 0;
		}
	}

	return false;
}

FText SBindingsPanel::GetAddBindingToolTip() const
{
	if (CanAddBinding())
	{
		return LOCTEXT("AddBindingTooltip", "Add an empty binding.");
	}
	else
	{
		return LOCTEXT("CannotAddBindingToolTip", "A viewmodel is required before adding bindings.");
	}
}

TSharedRef<SWidget> SBindingsPanel::CreateDrawerDockButton()
{
	if (bIsDrawerTab)
	{
		return 
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(LOCTEXT("DockInLayout_Tooltip", "Docks MVVM drawer in tab."))
			.ContentPadding(FMargin(1, 0))
			.OnClicked(this, &SBindingsPanel::CreateDrawerDockButtonClicked)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.0, 0.0f)
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(FAppStyle::Get().GetBrush("EditorViewport.SubMenu.Layouts"))
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(4.0, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("DockInLayout", "Dock in Layout"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			];
	}

	return SNullWidget::NullWidget;
}

FReply SBindingsPanel::CreateDrawerDockButtonClicked()
{
	if (TSharedPtr<FWidgetBlueprintEditor> WidgetEditor = WeakBlueprintEditor.Pin())
	{
		GEditor->GetEditorSubsystem<UStatusBarSubsystem>()->ForceDismissDrawer();

		if (TSharedPtr<SDockTab> ExistingTab = WidgetEditor->GetToolkitHost()->GetTabManager()->TryInvokeTab(FMVVMBindingSummoner::TabID))
		{
			ExistingTab->ActivateInParent(ETabActivationCause::SetDirectly); 
		}
	}

	return FReply::Handled();
}

void SBindingsPanel::HandleExtensionAdded(UBlueprintExtension* NewExtension)
{
	if (UMVVMWidgetBlueprintExtension_View* MVVMExtensionPtr = Cast<UMVVMWidgetBlueprintExtension_View>(NewExtension))
	{
		UWidgetBlueprint* WidgetBlueprint = MVVMExtensionPtr->GetWidgetBlueprint();

		if (WidgetBlueprint)
		{
			WidgetBlueprint->OnExtensionAdded.RemoveAll(this);

			MVVMExtension = MVVMExtensionPtr;

			if (!BlueprintViewChangedDelegateHandle.IsValid())
			{
				BlueprintViewChangedDelegateHandle = MVVMExtensionPtr->OnBlueprintViewChangedDelegate().AddSP(this, &SBindingsPanel::HandleBlueprintViewChangedDelegate);
			}

			if (MVVMExtensionPtr->GetBlueprintView() == nullptr)
			{
				MVVMExtensionPtr->CreateBlueprintViewInstance();
			}

			HandleBlueprintViewChangedDelegate();
		}
	}
}

TSharedRef<SWidget> SBindingsPanel::GenerateSettingsMenu()
{
	UMVVMWidgetBlueprintExtension_View* MVVMExtensionPtr = MVVMExtension.Get();
	DetailsView->SetObject(MVVMExtensionPtr && MVVMExtensionPtr->GetBlueprintView() ? MVVMExtensionPtr->GetBlueprintView() : nullptr);
	DetailContainer->SetContent(DetailsView.ToSharedRef());

	UWidgetBlueprintToolMenuContext* WidgetBlueprintMenuContext = NewObject<UWidgetBlueprintToolMenuContext>();
	WidgetBlueprintMenuContext->WidgetBlueprintEditor = WeakBlueprintEditor;

	FToolMenuContext MenuContext(WidgetBlueprintMenuContext);
	return UToolMenus::Get()->GenerateWidget("MVVMEditor.Panel.Toolbar.Settings", MenuContext);
}


void SBindingsPanel::RegisterSettingsMenu()
{
	UToolMenu* Menu = UToolMenus::Get()->RegisterMenu("MVVMEditor.Panel.Toolbar.Settings");
}

TSharedRef<SWidget> SBindingsPanel::GenerateCreateViewWidget()
{
	return SNew(SBorder)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.HAlign(EHorizontalAlignment::HAlign_Center)
			.VAlign(EVerticalAlignment::VAlign_Center)
			[
				SNew(SButton)
				.OnClicked(this, &SBindingsPanel::HandleCreateViewClicked)
				.Text(LOCTEXT("AddViewModelButtonText", "Add View Model"))
			]
		];
}

TSharedRef<SWidget> SBindingsPanel::GenerateEditViewWidget()
{
	FSlimHorizontalToolBarBuilder ToolbarBuilderGlobal(TSharedPtr<const FUICommandList>(), FMultiBoxCustomization::None);
	{
		ToolbarBuilderGlobal.BeginSection("Binding");
		ToolbarBuilderGlobal.AddToolBarButton(
			FUIAction(
				FExecuteAction::CreateSP(this, &SBindingsPanel::AddDefaultBinding),
				FCanExecuteAction::CreateSP(this, &SBindingsPanel::CanAddBinding),
				FGetActionCheckState()
			),
			NAME_None,
			LOCTEXT("AddBinding", "Add Binding"),
			TAttribute<FText>(this, &SBindingsPanel::GetAddBindingToolTip),
			FSlateIcon(FMVVMEditorStyle::Get().GetStyleSetName(), "BindingView.AddBinding"),
			EUserInterfaceActionType::Button
		);
		ToolbarBuilderGlobal.EndSection();
	}

	{
		ToolbarBuilderGlobal.AddWidget(SNew(SSpacer), NAME_None, true, HAlign_Right);
	}

	{
		ToolbarBuilderGlobal.BeginSection("Options");
		ToolbarBuilderGlobal.AddComboButton(
			FUIAction(
				FExecuteAction(),
				FCanExecuteAction(),
				FGetActionCheckState()
			),
			FOnGetContent::CreateSP(this, &SBindingsPanel::GenerateSettingsMenu),
			LOCTEXT("Settings", "Settings"),
			LOCTEXT("SettingsTooltip", "ModelView Settings"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.GameSettings"),
			false
		);
		ToolbarBuilderGlobal.EndSection();
	}

	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 2.0f))
		[
			ToolbarBuilderGlobal.MakeWidget()
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(4.0f, 4.0f))
			[
				SNew(SSplitter)
				.Orientation(EOrientation::Orient_Horizontal)

				+ SSplitter::Slot()
				.Value(0.75f)
				[
					SAssignNew(BindingsList, SBindingsList, StaticCastSharedRef<SBindingsPanel>(AsShared()), MVVMExtension.Get())
				]

				+ SSplitter::Slot()
				.Value(0.25f)
				[
					SAssignNew(DetailContainer, SBorder)
					[
						DetailsView.ToSharedRef()
					]
				]
			]
		];
}


FReply SBindingsPanel::HandleCreateViewClicked()
{
	if (TSharedPtr<FWidgetBlueprintEditor> WidgetBlueprintEditor = WeakBlueprintEditor.Pin())
	{
		UWidgetBlueprint* WidgetBlueprint = WidgetBlueprintEditor->GetWidgetBlueprintObj();
		check(WidgetBlueprint);

		WidgetBlueprint->OnExtensionAdded.RemoveAll(this);
		UMVVMWidgetBlueprintExtension_View* MVVMExtensionPtr = UMVVMWidgetBlueprintExtension_View::RequestExtension<UMVVMWidgetBlueprintExtension_View>(WidgetBlueprint);
		check(MVVMExtensionPtr);
		MVVMExtension = MVVMExtensionPtr;

		if (!BlueprintViewChangedDelegateHandle.IsValid())
		{
			BlueprintViewChangedDelegateHandle = MVVMExtensionPtr->OnBlueprintViewChangedDelegate().AddSP(this, &SBindingsPanel::HandleBlueprintViewChangedDelegate);
		}

		if (MVVMExtensionPtr->GetBlueprintView() == nullptr)
		{
			MVVMExtensionPtr->CreateBlueprintViewInstance();
		}

		HandleBlueprintViewChangedDelegate();

		ShowManageViewModelsWindow();
	}

	return FReply::Handled();
}

void SBindingsPanel::ShowManageViewModelsWindow()
{
	if (UMVVMWidgetBlueprintExtension_View* MVVMExtensionPtr = MVVMExtension.Get())
	{
		if (UMVVMBlueprintView* BlueprintView = MVVMExtensionPtr->GetBlueprintView())
		{
			TWeakObjectPtr<UMVVMWidgetBlueprintExtension_View> LocalMVVMExtension = MVVMExtension;
			FOnViewModelContextsPicked ViewModelContextsPickedDelegate = FOnViewModelContextsPicked::CreateLambda
			(
				[LocalMVVMExtension](TArray<FMVVMBlueprintViewModelContext> UpdatedContexts)
				{
					UMVVMWidgetBlueprintExtension_View* MVVMExtensionPtr = LocalMVVMExtension.Get();
					if (ensure(MVVMExtensionPtr))
					{
						if (UMVVMBlueprintView* BlueprintView = MVVMExtensionPtr->GetBlueprintView())
						{
							BlueprintView->SetViewModels(UpdatedContexts);
						}
					}
				}
			);

			TSharedRef<SWindow> ManageViewModelsWindow = SNew(SWindow)
				.Title(LOCTEXT("MVVMManageViewMoedlsWindowHeader", "Manage ViewModels"))
				.SupportsMaximize(false)
				.ClientSize(FVector2D(800.0f, 600.0f));

			TSharedRef<SMVVMManageViewModelsWidget> ManageViewModelsWidget = SNew(SMVVMManageViewModelsWidget)
				.ParentWindow(ManageViewModelsWindow)
				.OnViewModelContextsPickedDelegate(ViewModelContextsPickedDelegate)
				.WidgetBlueprint(WeakBlueprintEditor.Pin()->GetWidgetBlueprintObj());

			ManageViewModelsWindow->SetContent(ManageViewModelsWidget);
			GEditor->EditorAddModalWindow(ManageViewModelsWindow);
		}
	}
}

} // namespace UE::MVVM

#undef LOCTEXT_NAMESPACE
