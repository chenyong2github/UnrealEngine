// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithRuntimeModule.h"

#include "DatasmithRuntime.h"
#include "DirectLinkUtils.h"
#include "LogCategory.h"
#include "MaterialImportUtils.h"
#include "MaterialSelectors/DatasmithRuntimeMaterialSelector.h"

#include "DatasmithTranslatorModule.h"
#include "MasterMaterials/DatasmithMasterMaterialManager.h"

#if WITH_EDITOR
#include "Settings/ProjectPackagingSettings.h"
#include "Misc/Paths.h"
#endif // WITH_EDITOR

#ifdef USE_CAD_RUNTIME_DLL
#include "CoreTechTypes.h"
#include "DatasmithCADTranslatorModule.h"
#include "DatasmithOpenNurbsTranslatorModule.h"
#include "DatasmithWireTranslatorModule.h"
#include "DatasmithDispatcherModule.h"
#include "CADInterfacesModule.h"

#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#endif

const TCHAR* MaterialsPath = TEXT("/DatasmithRuntime/Materials");

class FDatasmithRuntimeModule : public IDatasmithRuntimeModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Verify DatasmithTranslatorModule has been loaded
		check(IDatasmithTranslatorModule::IsAvailable());

#if WITH_EDITOR
		// If don't have any active references to our materials they won't be packaged into monolithic builds, and we wouldn't
		// be able to create dynamic material instances at runtime.
		UProjectPackagingSettings* PackagingSettings = Cast<UProjectPackagingSettings>( UProjectPackagingSettings::StaticClass()->GetDefaultObject() );
		if ( PackagingSettings )
		{
			bool bAlreadyInPath = false;

			TArray<FDirectoryPath>& DirectoriesToCook = PackagingSettings->DirectoriesToAlwaysCook;
			for ( int32 Index = DirectoriesToCook.Num() - 1; Index >= 0; --Index )
			{
				if ( FPaths::IsSamePath( DirectoriesToCook[ Index ].Path, MaterialsPath ) )
				{
					bAlreadyInPath = true;
					break;
				}
			}

			if ( !bAlreadyInPath )
			{
				FDirectoryPath MaterialsDirectory;
				MaterialsDirectory.Path = MaterialsPath;

				PackagingSettings->DirectoriesToAlwaysCook.Add( MaterialsDirectory );

				UE_LOG(LogDatasmithRuntime, Log, TEXT("Adding %s to the list of directories to always package otherwise we cannot create dynamic material instances at runtime"), MaterialsPath);
			}
		}
#endif // WITH_EDITOR

		bool bCADRuntimeSupported = false;
#ifdef USE_CAD_RUNTIME_DLL
		FString DatasmithCADRuntimeBinDir = FPaths::Combine( FPaths::EngineDir(), TEXT("Plugins/Enterprise/DatasmithCADImporter/Binaries"), FPlatformProcess::GetBinariesSubdirectory() );
		FString DatasmithCADRuntimeLibPath = FPaths::Combine( DatasmithCADRuntimeBinDir, TEXT("DatasmithCADRuntime.dll"));
		FPlatformProcess::PushDllDirectory( *DatasmithCADRuntimeBinDir );
		void* DatasmithCADRuntimeDllHandle = FPlatformProcess::GetDllHandle( *DatasmithCADRuntimeLibPath );
		FPlatformProcess::PopDllDirectory( *DatasmithCADRuntimeBinDir );

		if (DatasmithCADRuntimeDllHandle)
		{
			// Load CADInterface module to load kernel_io dll
			FModuleManager::Get().LoadModuleChecked(CADINTERFACES_MODULE_NAME);

			if (void* DatasmithCADRuntimeInit = FPlatformProcess::GetDllExport(DatasmithCADRuntimeDllHandle, TEXT("DatasmithCADRuntimeInitialize")))
			{
				if (((int32 (*)(void (*)(TSharedPtr<CADLibrary::ICoreTechInterface>)))DatasmithCADRuntimeInit)(&CADLibrary::SetCoreTechInterface) == 0)
				{
					FModuleManager::Get().LoadModuleChecked(DATASMITHDISPATCHER_MODULE_NAME);
					FModuleManager::Get().LoadModuleChecked(DATASMITHWIRETRANSLATOR_MODULE_NAME);
					FModuleManager::Get().LoadModuleChecked(DATASMITHOPENNURBSTRANSLATOR_MODULE_NAME);
					FModuleManager::Get().LoadModuleChecked(DATASMITHCADTRANSLATOR_MODULE_NAME);

					if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CADTranslator.EnableThreadedImport")))
					{
						CVar->Set(0);
					}

					if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CADTranslator.EnableCADCache")))
					{
						CVar->Set(0);
					}

					bCADRuntimeSupported = true;
				}
			}
		}
#elif defined(USE_KERNEL_IO_SDK)
		bCADRuntimeSupported = true;
#endif

		FModuleManager::Get().LoadModuleChecked(TEXT("UdpMessaging"));

		DatasmithRuntime::FDestinationProxy::InitializeEndpointProxy();

		FDatasmithMasterMaterialManager::Get().RegisterSelector(DatasmithRuntime::MATERIAL_HOST, MakeShared< FDatasmithRuntimeMaterialSelector >());

		ADatasmithRuntimeActor::OnStartupModule(bCADRuntimeSupported);
	}

	virtual void ShutdownModule() override
	{
		ADatasmithRuntimeActor::OnShutdownModule();
		
		FDatasmithMasterMaterialManager::Get().UnregisterSelector(DatasmithRuntime::MATERIAL_HOST);

		DatasmithRuntime::FDestinationProxy::ShutdownEndpointProxy();
	}
};

//////////////////////////////////////////////////////////////////////////

IMPLEMENT_MODULE(FDatasmithRuntimeModule, DatasmithRuntime);

