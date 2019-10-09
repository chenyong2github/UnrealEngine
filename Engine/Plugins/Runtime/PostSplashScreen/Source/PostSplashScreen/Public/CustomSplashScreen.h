// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PostSplashScreenPrivatePCH.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "PreLoadScreenBase.h"

#include "CustomSplashScreenWidget.h"


class FCustomSplashScreen : public FPreLoadScreenBase
{
public:
	
    /*** IPreLoadScreen Implementation ***/
	virtual void Tick(float DeltaTime) override;
    virtual void Init() override;

    //Override to make sure this is always an EarlyStartupScreen
    virtual EPreLoadScreenTypes GetPreLoadScreenType() const override { return EPreLoadScreenTypes::CustomSplashScreen; }
    
	virtual FName GetPreLoadScreenTag() const override { return NAME_None; }
	
	virtual bool IsDone() const override;

	virtual float GetAddedTickDelay() override;

    virtual TSharedPtr<SWidget> GetWidget() override { return SplashScreenWidget; }
	
private:
	/** SharedPtr to our actual widget being rendered during ESP **/
	TSharedPtr<SCustomSplashScreenWidget> SplashScreenWidget;

	float TimeElapsed;
	float MaxTimeToDisplay;
};
