#pragma once


#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "Modules/ModuleManager.h"
#include "IDatasmithC4DImporter.h"
#ifdef _CINEWARE_SDK_
#if PLATFORM_WINDOWS
__pragma(warning(push))
__pragma(warning(disable: 4668))
__pragma(warning(disable: 4191))
__pragma(warning(disable: 4946))
__pragma(warning(disable: 4244))
#include "cineware.h"
#include "cineware_api.h"
__pragma(warning(pop))
#else
#include "cineware.h"
#include "cineware_api.h"
#endif
#endif

class IDatasmithC4DImpoter;
class IDatasmithScene;
struct FDatasmithC4DImportOptions;

#define C4DDYNAMIC_IMPORT_MODULE_NAME TEXT("DatasmithC4DDynamicImporter")


class IDatasmithC4DDynamicImporterModule : public IModuleInterface
{

public:

	virtual ~IDatasmithC4DDynamicImporterModule() = default;

	static inline IDatasmithC4DDynamicImporterModule& Get()
	{
		return FModuleManager::LoadModuleChecked< IDatasmithC4DDynamicImporterModule >(C4DDYNAMIC_IMPORT_MODULE_NAME);
	}

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(C4DDYNAMIC_IMPORT_MODULE_NAME);
	}
	
	virtual bool TryLoadingCineware() = 0;

	virtual TSharedPtr<class IDatasmithC4DImporter> GetDynamicImporter(TSharedRef<IDatasmithScene>& OutScene, FDatasmithC4DImportOptions& InputOptions) = 0;

	virtual void ShowNotification(const FString& message) = 0;
};
