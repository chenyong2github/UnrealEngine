// Copyright Epic Games, Inc. All Rights Reserved.
#include "IMegascansLiveLinkModule.h"
#include "UI/QMSUIManager.h"
#include "UI/MaterialBlendingDetails.h"
#include "UI/MSSettings.h"




#define LOCTEXT_NAMESPACE "MegascansPlugin"

class FQMSLiveLinkModule : public IMegascansLiveLinkModule
{
public:
	virtual void StartupModule() override
	{
		if (GIsEditor && !IsRunningCommandlet())
		{
			 FQMSUIManager::Initialize();
		}

		auto& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.RegisterCustomClassLayout(
			"MaterialBlendSettings",
			FOnGetDetailCustomizationInstance::CreateStatic(&BlendSettingsCustomization::MakeInstance)
		);
		PropertyModule.NotifyCustomizationModuleChanged();
	}

	virtual void ShutdownModule() override
	{
		if ((GIsEditor && !IsRunningCommandlet()))
		{
			
		}

		if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
		{
			auto& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

			PropertyModule.UnregisterCustomClassLayout("MaterialBlendSettings");
		}
	}


};

IMPLEMENT_MODULE(FQMSLiveLinkModule, MegascansPlugin);

#undef LOCTEXT_NAMESPACE
