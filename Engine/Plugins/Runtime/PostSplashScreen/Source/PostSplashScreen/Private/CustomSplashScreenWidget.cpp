// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CustomSplashScreenWidget.h"
#include "PreLoadSettingsContainer.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Images/SImage.h"

FCriticalSection SCustomSplashScreenWidget::BackgroundImageCrit;
int SCustomSplashScreenWidget::CurrentBackgroundImage = 0;

void SCustomSplashScreenWidget::Construct(const FArguments& InArgs)
{
    ChildSlot
    [
        SNew(SDPIScaler)
        .DPIScale(this, &SCustomSplashScreenWidget::GetDPIScale)
        [
            SNew(SOverlay)

            +SOverlay::Slot()
            .HAlign(HAlign_Fill)
            .VAlign(VAlign_Fill)
            [
                SNew(SVerticalBox)
                    
                + SVerticalBox::Slot()
                [
                    SNew(SOverlay)

                    //Background Display
                    +SOverlay::Slot()
                    [
                        SNew(SScaleBox)
                        .Stretch(EStretch::Fill)
                        [
                            SNew(SImage)
                            .Image(this, &SCustomSplashScreenWidget::GetCurrentBackgroundImage)
                        ]
                    ]
                ]
            ]
        ]
    ];
}


const FSlateBrush* SCustomSplashScreenWidget::GetCurrentBackgroundImage() const
{
    FScopeLock ScopeLock(&BackgroundImageCrit);

    const FPreLoadSettingsContainerBase::FScreenGroupingBase* CurrentScreenIdentifier = FPreLoadSettingsContainerBase::Get().GetScreenAtIndex(CurrentBackgroundImage);
    const FString& BackgroundBrushIdentifier = CurrentScreenIdentifier ? CurrentScreenIdentifier->ScreenBackgroundIdentifer : TEXT("");
    return FPreLoadSettingsContainerBase::Get().GetBrush(BackgroundBrushIdentifier);
}
