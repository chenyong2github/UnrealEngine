// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"

#include "RenderingThread.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SDPIScaler.h"

//Widget that displays a very simple version of a FPreLoadScreen UI that just includes a background and localized text together.
//Rotates through the PreLoadScreens in the same order they are in the FPreLoadSettingsContainerBase. Uses the TimeToDisplayEachBackground variable to determine how long
//to display each screen before rotating. Loops back when finished.
class SCustomSplashScreenWidget : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SCustomSplashScreenWidget) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    //Not used in the default simple implementation
    virtual float GetDPIScale() const { return 1.0f; };

    const FSlateBrush* GetCurrentBackgroundImage() const;
	
	static FCriticalSection BackgroundImageCrit;
	static int CurrentBackgroundImage;

};