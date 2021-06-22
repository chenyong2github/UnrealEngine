// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithC4DDynamicImporterModule.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Logging/LogMacros.h"
#include "Misc/Paths.h"

#include "Internationalization/Text.h"
#include "Misc/MessageDialog.h"

#include "Misc/App.h"
#include "Internationalization/Internationalization.h"
#include "Widgets/Notifications/SNotificationList.h" 
#include "Widgets/Notifications/GlobalNotification.h"

#include "DatasmithC4DImporter.h"
#ifdef _CINEWARE_SDK_
DATASMITH_C4D_PUSH_WARNINGS
#include "cineware_api.h"
DATASMITH_C4D_POP_WARNINGS
#endif

#include <vector>

DEFINE_LOG_CATEGORY(LogDatasmithC4DImport)

#define LOCTEXT_NAMESPACE "DatasmithC4DImportPlugin"

#define TOAST_MESSAGE "Improved Cineware Import available: maxon.net/unreal"

class IDatasmithC4DImporter;

class FDatasmithC4DDynamicImporterModule : public IDatasmithC4DDynamicImporterModule
{

public:
	FDatasmithC4DDynamicImporterModule() : CinewareAvailable(-1)
	{
	}

	bool TryLoadingCineware() override
	{
		CinewareAvailable = 0;

#ifdef _CINEWARE_SDK_
		// Verify Cineware Dll exists before starting its initialization
		FString CinewareDllPath = CINEWARE_LOCATION;
		FString CinewareDllDir = FPaths::GetPath(CinewareDllPath);

		FPlatformProcess::PushDllDirectory( *CinewareDllDir );
		void* CinewareDllHandle = FPlatformProcess::GetDllHandle( *CinewareDllPath );
		FPlatformProcess::PopDllDirectory( *CinewareDllDir );

		if (CinewareDllHandle)
		{
			// loading the Cineware modules happens async
			if (!cineware::LoadCineware())
			{
				ShowNotification(TOAST_MESSAGE);
				return false;
			}

			// cineware::WaitForCinewareInit has to return true before calling Cineware methods
			if (!cineware::WaitForCinewareInit())
			{
				ShowNotification(TOAST_MESSAGE);
				return false;
			}
		}
		else
		{
			ShowNotification(TOAST_MESSAGE);
			return false;
		}
		
		CinewareAvailable = 1;
		return true;
#else
		return false;
#endif
	}

	TSharedPtr<class IDatasmithC4DImporter> GetDynamicImporter(TSharedRef<IDatasmithScene>& OutScene, FDatasmithC4DImportOptions& InputOptions) override
	{
#ifdef _CINEWARE_SDK_
		return MakeShared<FDatasmithC4DDynamicImporter>(OutScene, InputOptions);
#else
		return nullptr;
#endif
	}

	/** IModuleInterface implementation */
	virtual void StartupModule() override
	{
		FString EnvVariable = FPlatformMisc::GetEnvironmentVariable(TEXT("DATASMITHC4D_DEBUG"));
		bDebugMode = !EnvVariable.IsEmpty();
	}

#ifdef _CINEWARE_SDK_
	FReply RedirectToEndpoint(const FGeometry& Geometry, const FPointerEvent& Event)
	{
		FPlatformProcess::LaunchURL(TEXT("https://www.maxon.net/en/unreal"), NULL, NULL);
		return FReply::Handled();
	}
#endif

	void ShowNotification(const FString& Msg) override
	{
#ifdef _CINEWARE_SDK_
		if (!IsRunningCommandlet())
		{
			FText InfoMsg = FText::Format(LOCTEXT("DatasmithC4DImporterLoaded", "{0}"), FText::FromString(Msg));
			UE_LOG(LogDatasmithC4DImport, Warning, TEXT("%s"), *InfoMsg.ToString());

			FNotificationInfo NotificationInfo(InfoMsg);
			NotificationInfo.ExpireDuration = 8.0f;
			NotificationInfo.bUseLargeFont = false;
			auto NotificationIem = FSlateNotificationManager::Get().AddNotification(NotificationInfo);

			FPointerEvent PointerEvent;
			FPointerEventHandler LinkEventHandler;
			LinkEventHandler.BindRaw(this, &FDatasmithC4DDynamicImporterModule::RedirectToEndpoint);
			NotificationIem->SetOnMouseButtonUp(LinkEventHandler);
		}
#endif
	}


private:

	/** Indicates if DATASMITHC4D_DEBUG environment variable is set */
	bool bDebugMode;

	/** Check if can load cineware once */
	int32 CinewareAvailable;
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDatasmithC4DDynamicImporterModule, DatasmithC4DDynamicImporter);
