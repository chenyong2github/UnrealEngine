// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCProtocolBindingList.h"

#include "EditorFontGlyphs.h"
#include "EditorStyleSet.h"
#include "IRemoteControlProtocolModule.h"
#include "RemoteControlProtocolWidgetsSettings.h"
#include "SRCProtocolBinding.h"
#include "SRCProtocolList.h"
#include "Delegates/DelegateSignatureImpl.inl"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Types/ISlateMetaData.h"
#include "ViewModels/ProtocolEntityViewModel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STableViewBase.h"

#define LOCTEXT_NAMESPACE "RemoteControlProtocolWidgets"

void SRCProtocolBindingList::Construct(const FArguments& InArgs, TSharedRef<FProtocolEntityViewModel> InViewModel)
{
	constexpr float Padding = 2.0f;
	ViewModel = InViewModel;
	FilteredBindings = ViewModel->GetFilteredBindings(GetSettings()->HiddenProtocolTypeNames);

	PrimaryColumnWidth = 0.7f;
	PrimaryColumnSizeData = MakeShared<RemoteControlProtocolWidgetUtils::FPropertyViewColumnSizeData>();
	PrimaryColumnSizeData->LeftColumnWidth = TAttribute<float>(this, &SRCProtocolBindingList::OnGetPrimaryLeftColumnWidth);
	PrimaryColumnSizeData->RightColumnWidth = TAttribute<float>(this, &SRCProtocolBindingList::OnGetPrimaryRightColumnWidth);
	PrimaryColumnSizeData->OnWidthChanged = SSplitter::FOnSlotResized::CreateSP(this, &SRCProtocolBindingList::OnSetPrimaryColumnWidth);

	SecondaryColumnWidth = 0.7f;
	SecondaryColumnSizeData = MakeShared<RemoteControlProtocolWidgetUtils::FPropertyViewColumnSizeData>();
	SecondaryColumnSizeData->LeftColumnWidth = TAttribute<float>(this, &SRCProtocolBindingList::OnGetSecondaryLeftColumnWidth);
	SecondaryColumnSizeData->RightColumnWidth = TAttribute<float>(this, &SRCProtocolBindingList::OnGetSecondaryRightColumnWidth);
	SecondaryColumnSizeData->OnWidthChanged = SSplitter::FOnSlotResized::CreateSP(this, &SRCProtocolBindingList::OnSetSecondaryColumnWidth);
	
	ViewModel->OnBindingAdded().AddLambda([&](const TSharedPtr<FProtocolBindingViewModel> InBindingViewModel)
	{
		if(BindingList.IsValid())
		{
            BindingList->RequestListRefresh();
			BindingList->RequestNavigateToItem(InBindingViewModel);
        }
	});

	ViewModel->OnBindingRemoved().AddLambda([&](FGuid)
	{
		if(BindingList.IsValid())
		{
			BindingList->RequestListRefresh();
		}
	});

	ViewModel->OnChanged().AddLambda([&]()
	{
		if(BindingList.IsValid())
		{
			BindingList->RequestListRefresh();
		}
	});

	// The visibility toggle menu to show/hide protocol types
	FMenuBuilder ProtocolVisibilityMenu(true, nullptr);

	// @todo: refactor to allow for protocol late loading, add & remove during editor session
	ProtocolNames = IRemoteControlProtocolModule::Get().GetProtocolNames();
	for(const FName& ProtocolName : ProtocolNames)
	{
		ProtocolVisibilityMenu.AddMenuEntry(
			FText::Format(LOCTEXT("ShowProtocolFmt", "Show {0}"), FText::FromName(ProtocolName)),
			FText::Format(LOCTEXT("ShowProtocolTooltipFmt", "Show all {0} protocol entries."), FText::FromName(ProtocolName)),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([&](){ this->ToggleShowProtocol(ProtocolName); }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([&]() { return this->IsProtocolShown(ProtocolName); })
			),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}	

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(1, 1, 1, Padding)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(Padding)
			.AutoWidth()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(Padding)
				.AutoWidth()
				[
					SAssignNew(ProtocolList, SRCProtocolList)
				]

				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Left)
				[
					SNew(SButton)
	                .ButtonStyle(FEditorStyle::Get(), "NoBorder")
	                .ToolTipText(LOCTEXT("AddProtocol", "Add Protocol"))
	                .IsEnabled_Lambda([this](){ return CanAddProtocol(); })
	                .OnClicked_Lambda([this]()
	                {
	                	if(ProtocolList.IsValid() && ProtocolList->GetSelectedProtocolName().IsValid())
	                	{
	                		ViewModel->AddBinding(*ProtocolList->GetSelectedProtocolName());
	                	}
                        return FReply::Handled();
	                })
	                .ContentPadding(FMargin(2.0f, 2.0f))
	                .Content()
	                [
	                    SNew(STextBlock)
	                    .TextStyle(FEditorStyle::Get(), "NormalText.Important")
	                    .Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
	                    .Text(FEditorFontGlyphs::Plus)
	                ]
				]

				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.AutoWidth()
				.Padding(FMargin(3,0))
				[
					SNew(STextBlock)
					.ColorAndOpacity(FCoreStyle::Get().GetColor("ErrorReporting.WarningBackgroundColor"))
					.IsEnabled_Lambda([&](){ return !StatusMessage.IsEmpty(); })
					.Text_Lambda([&]() { return StatusMessage; })
				]
			]
			
			+ SHorizontalBox::Slot()
			.Padding(Padding)
			[
				SNullWidget::NullWidget
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SComboButton)
				.ContentPadding(0)
				.ForegroundColor(FSlateColor::UseForeground())
				.ButtonStyle(FEditorStyle::Get(), "ToggleButton")
				.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("ViewOptions")))
				.MenuContent()
				[
					ProtocolVisibilityMenu.MakeWidget()
				]
				.ButtonContent()
				[
				 	SNew(SImage)
				 	.Image(FEditorStyle::GetBrush("GenericViewButton"))
				]
			]
		]			 

		+ SVerticalBox::Slot()
		.Padding(1)
		[
			SNew(SBorder)
	        .BorderImage(FEditorStyle::GetBrush("ToolPanel.DarkGroupBorder"))
	        .Padding(5.0f)
	        [
        		SAssignNew(BindingList, SListView<TSharedPtr<FProtocolBindingViewModel>>)
		        .OnGenerateRow(this, &SRCProtocolBindingList::OnGenerateRow)
		        .ListItemsSource(&FilteredBindings)
	        ]
		]
	];
}

SRCProtocolBindingList::~SRCProtocolBindingList()
{
}

TSharedRef<SRCProtocolBinding> SRCProtocolBindingList::ConstructBindingWidget(const TSharedRef<STableViewBase>& InOwnerTable, TSharedPtr<FProtocolBindingViewModel> InViewModel) const
{
	return SNew(SRCProtocolBinding, InOwnerTable, InViewModel.ToSharedRef())
		.PrimaryColumnSizeData(PrimaryColumnSizeData)
		.SecondaryColumnSizeData(SecondaryColumnSizeData);
}

TSharedRef<ITableRow> SRCProtocolBindingList::OnGenerateRow(TSharedPtr<FProtocolBindingViewModel> InViewModel, const TSharedRef<STableViewBase>& OwnerTable) const
{
	check(InViewModel.IsValid());
	TSharedRef<SRCProtocolBinding> BindingWidget = ConstructBindingWidget(OwnerTable, InViewModel);
	return BindingWidget;
}

bool SRCProtocolBindingList::CanAddProtocol()
{
	const TSharedPtr<FName> SelectedProtocolName = ProtocolList->GetSelectedProtocolName();
	const bool bResult = ViewModel->CanAddBinding(SelectedProtocolName.IsValid() ? *SelectedProtocolName : NAME_None, StatusMessage);
	if(bResult)
	{
		StatusMessage = FText::GetEmpty();
	}
	return bResult;	
}

void SRCProtocolBindingList::ToggleShowProtocol(const FName& InProtocolName)
{
	if(IsProtocolShown(InProtocolName))
	{
		GetSettings()->HiddenProtocolTypeNames.Add(InProtocolName);
	}
	else
	{
		GetSettings()->HiddenProtocolTypeNames.Remove(InProtocolName);
	}

	FilteredBindings = ViewModel->GetFilteredBindings(GetSettings()->HiddenProtocolTypeNames);
	BindingList->RequestListRefresh();
}

bool SRCProtocolBindingList::IsProtocolShown(const FName& InProtocolName)
{
	return !GetSettings()->HiddenProtocolTypeNames.Contains(InProtocolName);
}

URemoteControlProtocolWidgetsSettings* SRCProtocolBindingList::GetSettings()
{
	if(Settings.IsValid())
	{
		return Settings.Get();
	}

	Settings = GetMutableDefault<URemoteControlProtocolWidgetsSettings>();
	ensure(Settings.IsValid());
	
	return Settings.Get();
}

#undef LOCTEXT_NAMESPACE
