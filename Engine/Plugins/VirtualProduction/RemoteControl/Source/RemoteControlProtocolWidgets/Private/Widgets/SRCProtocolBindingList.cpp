// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCProtocolBindingList.h"

#include "Editor.h"
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
#include "ViewModels/RCViewModelCommon.h"
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
	Refresh();

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
		if(Refresh())
		{
			Refresh(true);
			BindingList->RequestNavigateToItem(InBindingViewModel);
        }
	});

	ViewModel->OnBindingRemoved().AddLambda([&](FGuid)
	{
		Refresh();
	});

	ViewModel->OnChanged().AddLambda([&]()
	{
		Refresh();
	});

	// The visibility toggle menu to show/hide protocol types
	FMenuBuilder ProtocolVisibilityMenu(true, nullptr);

	TSharedRef<SScrollBar> ExternalScrollBar = SNew(SScrollBar);
	ExternalScrollBar->SetVisibility(TAttribute<EVisibility>(this, &SRCProtocolBindingList::GetScrollBarVisibility));

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

	SAssignNew(BindingList, SListView<TSharedPtr<IRCTreeNodeViewModel>>)
	.ListItemsSource(&FilteredBindings)
	.OnGenerateRow(this, &SRCProtocolBindingList::ConstructBindingWidget)
	.SelectionMode(ESelectionMode::None)
	.ExternalScrollbar(ExternalScrollBar);

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
		.FillHeight(1.0f)
		.Padding(0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.DarkGroupBorder"))
				.Padding(5.0f)
				[
					BindingList.ToSharedRef()				
				]				
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(16.0f)
				[
					ExternalScrollBar
				]
			]
		]
	];
}

SRCProtocolBindingList::~SRCProtocolBindingList()
{
	for (const TSharedPtr<TStructOnScope<FRemoteControlProtocolEntity>>& RemoteControlProtocolEntity : AwaitingProtocolEntities)
	{
		if (FRemoteControlProtocolEntity* RemoteControlProtocolEntityPtr = RemoteControlProtocolEntity->Get())
		{
			if (RemoteControlProtocolEntityPtr->GetBindingStatus() == ERCBindingStatus::Awaiting)
			{
				RemoteControlProtocolEntityPtr->ResetDefaultBindingState();
			}
		}
	}

	AwaitingProtocolEntities.Empty();
}

TSharedRef<ITableRow> SRCProtocolBindingList::ConstructBindingWidget(TSharedPtr<IRCTreeNodeViewModel> InViewModel, const TSharedRef<STableViewBase>& InOwnerTable)
{
	check(InViewModel.IsValid());
	
	return SNew(SRCProtocolBinding, InOwnerTable, StaticCastSharedPtr<FProtocolBindingViewModel>(InViewModel).ToSharedRef())
		.PrimaryColumnSizeData(PrimaryColumnSizeData)
		.SecondaryColumnSizeData(SecondaryColumnSizeData)
		.OnStartRecording(this, &SRCProtocolBindingList::OnStartRecording)
		.OnStopRecording(this, &SRCProtocolBindingList::OnStopRecording);
}

bool SRCProtocolBindingList::CanAddProtocol()
{
	const TSharedPtr<FName> SelectedProtocolName = ProtocolList.IsValid() ? ProtocolList->GetSelectedProtocolName() : nullptr;
	if(!ViewModel->IsBound())
	{
		StatusMessage = FText::GetEmpty();
		return false;
	}
	
	if(ViewModel->CanAddBinding(SelectedProtocolName.IsValid() ? *SelectedProtocolName : NAME_None, StatusMessage))
	{
		StatusMessage = FText::GetEmpty();
		return true;
	}
	
	return false;	
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
	GetSettings()->SaveConfig();

	Refresh(false);
}

bool SRCProtocolBindingList::IsProtocolShown(const FName& InProtocolName)
{
	return !GetSettings()->HiddenProtocolTypeNames.Contains(InProtocolName);
}

EVisibility SRCProtocolBindingList::GetScrollBarVisibility() const
{
	const bool bHasAnythingToShow = FilteredBindings.Num() > 0;
	return bHasAnythingToShow ? EVisibility::Visible : EVisibility::Collapsed;
}

void SRCProtocolBindingList::OnStartRecording(TSharedPtr<TStructOnScope<FRemoteControlProtocolEntity>> InEntity)
{
	AwaitingProtocolEntities.Add(InEntity);
}

void SRCProtocolBindingList::OnStopRecording(TSharedPtr<TStructOnScope<FRemoteControlProtocolEntity>> InEntity)
{
	AwaitingProtocolEntities.Remove(InEntity);
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

bool SRCProtocolBindingList::Refresh(bool bNavigateToEnd)
{
	FilteredBindings.Empty(FilteredBindings.Num());
	for(const TSharedPtr<FProtocolBindingViewModel>& ProtocolBinding :  ViewModel->GetFilteredBindings(GetSettings()->HiddenProtocolTypeNames))
	{
		FilteredBindings.Add(ProtocolBinding);
	}
	
	if(BindingList.IsValid())
	{
		// Don't build list if not supported
		if(!CanAddProtocol())
		{
			return false;
		}

		
		GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateLambda([bNavigateToEnd, WeakListPtr = TWeakPtr<SRCProtocolBindingList>(StaticCastSharedRef<SRCProtocolBindingList>(AsShared()))]()
		{
			if (TSharedPtr<SRCProtocolBindingList> ListPtr = WeakListPtr.Pin())
			{
				ListPtr->BindingList->RequestListRefresh();
				if(bNavigateToEnd)
				{
					ListPtr->BindingList->ScrollToBottom();	
				}
			}
		}));
		
		return true;
	}
	
	return false;
}

#undef LOCTEXT_NAMESPACE
