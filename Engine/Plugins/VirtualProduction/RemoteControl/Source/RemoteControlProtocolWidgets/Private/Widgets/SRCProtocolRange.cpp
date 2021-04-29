// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCProtocolRange.h"

#include "IDetailPropertyRow.h"
#include "IDetailTreeNode.h"
#include "IStructureDetailsView.h"
#include "RCPropertyContainer.h"
#include "Components/Widget.h"
#include "ViewModels/ProtocolRangeViewModel.h"
#include "Widgets/SPropertyView.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SSpacer.h"

#define LOCTEXT_NAMESPACE "RemoteControlProtocolWidgets"

void SRCProtocolRange::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const TSharedRef<FProtocolRangeViewModel>& InViewModel)
{
	constexpr float Padding = 2.0f;
	ViewModel = InViewModel;
	PrimaryColumnSizeData = InArgs._PrimaryColumnSizeData;
	SecondaryColumnSizeData = InArgs._SecondaryColumnSizeData;

	const TSharedPtr<SWidget> LeftWidget =
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
		.HAlign(HAlign_Right)
        .AutoWidth()
        [
        	MakeInput()
        ];

	const TSharedPtr<SWidget> RightWidget =
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
		.HAlign(HAlign_Left)
        .AutoWidth()
        [
            MakeOutput()
        ]

      + SHorizontalBox::Slot()
      .FillWidth(1.0f)
      [
          SNew(SSpacer)
      ]

      + SHorizontalBox::Slot()
      .HAlign(HAlign_Right)
      .VAlign(VAlign_Center)
      .AutoWidth()
      .Padding(0)
      [
          SNew(SButton)
          .ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
          .ForegroundColor(FSlateColor::UseForeground())
          .IsFocusable(false)
          .OnClicked(this, &SRCProtocolRange::OnDelete)
          .Content()
          [
              SNew(STextBlock)
              .Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
              .Text(FText::FromString(FString(TEXT("\xf00d"))))
          ]
      ];
	
	STableRow::Construct(
        STableRow::FArguments()
        .Style(FEditorStyle::Get(), "DetailsView.TreeView.TableRow")
        .ShowSelection(false)
        .Content()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .Padding(0.0f)
            .HAlign(HAlign_Fill)
            .VAlign(VAlign_Fill)
            .FillWidth(1.f)
            [
                SNew(RemoteControlProtocolWidgetUtils::SCustomSplitter)
                .LeftWidget(LeftWidget.ToSharedRef())
                .RightWidget(RightWidget.ToSharedRef())
                .ColumnSizeData(PrimaryColumnSizeData)
            ]
        ],
        InOwnerTableView);
}

TSharedRef<SWidget> SRCProtocolRange::MakeInput()
{
	TSharedPtr<SWidget> Widget;
	if(const FProperty* Property = ViewModel->GetInputProperty())
	{
		URCPropertyContainerBase* PropertyContainer = PropertyContainers::CreateContainerForProperty(GetTransientPackage(), Property);
		if(PropertyContainer)
		{
			InputProxyPropertyContainer.Reset(PropertyContainer);

			const TSharedRef<SPropertyView> PropertyViewWidget = SNew(SPropertyView)
            .Object(InputProxyPropertyContainer.Get())
            .RootPropertyName("Value")
			.NameVisibility(EPropertyNameVisibility::Show)
			.DisplayName(LOCTEXT("ProtocolRangeInput", "Input"))
			.ResizableColumn(false)
            .Spacing(10.0f)
            .ColumnPadding(true);

			InputProxyPropertyHandle = PropertyViewWidget->GetPropertyHandle();
			ViewModel->CopyInputValue(InputProxyPropertyHandle = PropertyViewWidget->GetPropertyHandle());
			OnInputProxyPropertyChangedHandle = PropertyViewWidget->OnFinishedChangingProperties().AddSP(this, &SRCProtocolRange::OnInputProxyChanged);

			Widget = PropertyViewWidget;
		}
	
		if(!Widget.IsValid())
		{
			Widget = SNew(SBox)
            [
                SNew(STextBlock)
                .Text(FText::FromString(FString::Printf(TEXT("Unsupported Type: %s"), *Property->GetName())))
            ];
		}
	}

	return Widget.ToSharedRef();
}

TSharedRef<SWidget> SRCProtocolRange::MakeOutput()
{
	TSharedPtr<SWidget> Widget;
	if(const FProperty* Property = ViewModel->GetProperty().Get())
	{
		URCPropertyContainerBase* PropertyContainer = PropertyContainers::CreateContainerForProperty(GetTransientPackage(), Property);
		if(PropertyContainer)
		{
			OutputProxyPropertyContainer.Reset(PropertyContainer);

			const TSharedRef<SPropertyView> PropertyViewWidget = SNew(SPropertyView)
            .Object(OutputProxyPropertyContainer.Get())
            .RootPropertyName("Value")
            .ColumnSizeData(SecondaryColumnSizeData)
            .Spacing(10.0f)
            .ColumnPadding(true);

			OutputProxyPropertyHandle = PropertyViewWidget->GetPropertyHandle();
			ViewModel->CopyOutputValue(OutputProxyPropertyHandle);
			OnOutputProxyPropertyChangedHandle = PropertyViewWidget->OnFinishedChangingProperties().AddSP(this, &SRCProtocolRange::OnOutputProxyChanged);
			
			Widget = PropertyViewWidget;
		}

		if(!Widget.IsValid())
		{
			Widget = SNew(SBox)
            [
                SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("Unsupported Type: %s"), *Property->GetName())))
            ];
		}
	}

	return Widget.ToSharedRef();
}

FReply SRCProtocolRange::OnDelete() const
{
	ViewModel->Remove();
	return FReply::Handled();
}

static void OnProxyPropertyChanged(const FPropertyChangedEvent& InEvent, const TSharedPtr<IPropertyHandle>& InPropertyHandle, const TFunction<void(const TSharedPtr<IPropertyHandle>&)>& InSetter)
{
	// It's possible this call is delayed and the property has been changed or invalidated
	if(InPropertyHandle.IsValid() && InPropertyHandle->IsValidHandle() && InEvent.ChangeType == EPropertyChangeType::ValueSet)
	{
		InSetter(InPropertyHandle);
	}
}

void SRCProtocolRange::OnInputProxyChanged(const FPropertyChangedEvent& InEvent)
{
	OnProxyPropertyChanged(InEvent, InputProxyPropertyHandle, [&](const TSharedPtr<IPropertyHandle>& InPropertyHandle){ ViewModel->SetInputData(InPropertyHandle); });
}

void SRCProtocolRange::OnOutputProxyChanged(const FPropertyChangedEvent& InEvent)
{
	OnProxyPropertyChanged(InEvent, OutputProxyPropertyHandle, [&](const TSharedPtr<IPropertyHandle>& InPropertyHandle) { ViewModel->SetOutputData(InPropertyHandle); });
}

#undef LOCTEXT_NAMESPACE
