// Copyright Epic Games, Inc. All Rights Reserved.

#include "PostSplashScreenPrivatePCH.h"

#include "PreLoadScreenManager.h"

#include "PostSplashScreen.h"
#include "CustomSplashScreen.h"

#include "RenderingThread.h"

#define LOCTEXT_NAMESPACE "CustomSplashScreen"

class FPostSplashScreenModule : public IPostSplashScreenModule
{
public:
    FPostSplashScreenModule()
	{
	}

	virtual void StartupModule() override
	{
		if (!GIsEditor && FApp::CanEverRender() && FPreLoadScreenManager::Get())
		{
			CustomSplashScreen = MakeShared<FCustomSplashScreen>();
			CustomSplashScreen->Init();
			FPreLoadScreenManager::Get()->RegisterPreLoadScreen(CustomSplashScreen);

			FPreLoadScreenManager::Get()->OnPreLoadScreenManagerCleanUp.AddRaw(this, &FPostSplashScreenModule::CleanUpModule);
		}
    }

    //Once the PreLoadScreenManager is cleaning up, we can get rid of all our resources too
    virtual void CleanUpModule()
    {
        CustomSplashScreen.Reset();
        ShutdownModule();
    }

	
	virtual bool IsGameModule() const override
	{
		return true;
	}

    TSharedPtr<FCustomSplashScreen> CustomSplashScreen;
};

IMPLEMENT_GAME_MODULE(FPostSplashScreenModule, PostSplashScreen);

#undef LOCTEXT_NAMESPACE