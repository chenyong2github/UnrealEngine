// Copyright Epic Games, Inc. All Rights Reserved.

#include "RivermaxManager.h"

#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/CoreDelegates.h"
#include "RivermaxHeader.h"
#include "RivermaxLog.h"


namespace UE::RivermaxCore::Private
{
	FRivermaxManager::FRivermaxManager()
	{
		const auto InitializeRivermaxLibFunc = [this]()
		{
			rmax_init_config Config;
			memset(&Config, 0, sizeof(Config));
			//Config.flags |= RIVERMAX_HANDLE_SIGNAL; //Verify if desired

			rmax_status_t Status = rmax_init(&Config);
			if (Status == RMAX_OK)
			{
				uint32 Major = 0;
				uint32 Minor = 0;
				uint32 Release = 0;
				uint32 Build = 0;
				Status = rmax_get_version(&Major, &Minor, &Release, &Build);
				if (Status == RMAX_OK)
				{
					bIsInitialized = true;
					UE_LOG(LogRivermax, Log, TEXT("Rivermax library version %d.%d.%d.%d succesfully initialized"), Major, Minor, Release, Build);
				}
				else
				{
					UE_LOG(LogRivermax, Log, TEXT("Failed to retrieve Rivermax library version. Status: %d"), Status);
				}
			}
			else if (Status == RMAX_ERR_LICENSE_ISSUE)
			{
				UE_LOG(LogRivermax, Log, TEXT("Rivermax License could not be found. Have you configured RIVERMAX_LICENSE_PATH environment variable?"));
			}
			
			if(bIsInitialized == false)
			{
				UE_LOG(LogRivermax, Log, TEXT("Rivermax library failed to initialize. Status: %d"), Status);
			}
		};

		const bool bIsLibraryLoaded = LoadRivermaxLibrary();
		if (bIsLibraryLoaded && FApp::CanEverRender())
		{
			//Postpone initialization after all modules have been loaded
			FCoreDelegates::OnAllModuleLoadingPhasesComplete.AddLambda(InitializeRivermaxLibFunc);
		}
		else
		{
			UE_LOG(LogRivermax, Log, TEXT("Skipping Rivermax initialization. Library won't be usable."));
		}
	}

	FRivermaxManager::~FRivermaxManager()
	{
		if (IsInitialized())
		{
			rmax_status_t Status = rmax_cleanup();
			if (Status != RMAX_OK)
			{
				UE_LOG(LogRivermax, Log, TEXT("Failed to cleanup Rivermax Library. Status: %d"), Status);
			}
			UE_LOG(LogRivermax, Log, TEXT("Rivermax Library has shutdown."));
		}

		if (LibraryHandle != nullptr)
		{
			FPlatformProcess::FreeDllHandle(LibraryHandle);
			LibraryHandle = nullptr;
		}
	}

	bool FRivermaxManager::IsInitialized() const
	{
		return bIsInitialized;
	}

	bool FRivermaxManager::LoadRivermaxLibrary()
	{
#if defined(RIVERMAX_LIBRARY_PLATFORM_PATH) && defined(RIVERMAX_LIBRARY_NAME)
		const FString LibraryPath = FPaths::Combine(FPaths::EngineSourceDir(), TEXT("ThirdParty/NVIDIA/Rivermax/lib"), TEXT(PREPROCESSOR_TO_STRING(RIVERMAX_LIBRARY_PLATFORM_PATH)));
		const FString LibraryName = TEXT(PREPROCESSOR_TO_STRING(RIVERMAX_LIBRARY_NAME));

		FPlatformProcess::PushDllDirectory(*LibraryPath);
		LibraryHandle = FPlatformProcess::GetDllHandle(*LibraryName);
		FPlatformProcess::PopDllDirectory(*LibraryPath);
#endif

		const bool bIsLibraryValid = LibraryHandle != nullptr;
		if(!bIsLibraryValid)
		{
			UE_LOG(LogRivermax, Log, TEXT("Failed to load required library %s. Rivermax library will not be functional."), *LibraryName);
		}

		return bIsLibraryValid;
	}

}





