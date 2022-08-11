// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "Online/OnlineServicesOSSAdapter.h"
#include "Online/OnlineServicesRegistry.h"
#include "Online/OnlineIdOSSAdapter.h"
#include "Online/SessionsOSSAdapter.h"

#include "OnlineSubsystem.h"

namespace UE::Online
{

class FOnlineServicesOSSAdapterModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
protected:
};

struct FOSSAdapterService
{
	EOnlineServices Service = EOnlineServices::Default;
	FString ConfigName;
	FName OnlineSubsystem;
	int Priority = -1;
};

struct FOSSAdapterConfig
{
	// TArray<FOSSAdapterService> Services; // use once online config parsing supports arrays of structs
	TArray<FString> Services;
};

namespace Meta {

BEGIN_ONLINE_STRUCT_META(FOSSAdapterService)
	ONLINE_STRUCT_FIELD(FOSSAdapterService, Service),
	ONLINE_STRUCT_FIELD(FOSSAdapterService, ConfigName),
	ONLINE_STRUCT_FIELD(FOSSAdapterService, OnlineSubsystem),
	ONLINE_STRUCT_FIELD(FOSSAdapterService, Priority)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FOSSAdapterConfig)
	ONLINE_STRUCT_FIELD(FOSSAdapterConfig, Services)
END_ONLINE_STRUCT_META()

/* Meta */ }

class FOnlineServicesFactoryOSSAdapter : public IOnlineServicesFactory
{
public:
	FOnlineServicesFactoryOSSAdapter(const FOSSAdapterService& InConfig)
		: Config(InConfig)
	{
	}

	virtual ~FOnlineServicesFactoryOSSAdapter() {}
	virtual TSharedPtr<IOnlineServices> Create(FName InInstanceName) override
	{
		IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get(Config.OnlineSubsystem);
		if (Subsystem != nullptr)
		{
			return MakeShared<FOnlineServicesOSSAdapter>(Config.Service, Config.ConfigName, InInstanceName, Subsystem);
		}
		else
		{
			return nullptr;
		}
	}
protected:
	FOSSAdapterService Config;
};

namespace {

// temporary parsing until online config system has full support for parsing arrays of structs/nested structs
FString StripQuotes(const FString& Source)
{
	if (Source.StartsWith(TEXT("\"")))
	{
		return Source.Mid(1, Source.Len() - 2);
	}
	return Source;
}

void ParseServiceConfig(FOSSAdapterService& ServiceConfig, const FString& ServiceConfigString)
{
	const TCHAR* Delims[4] = { TEXT("("), TEXT(")"), TEXT("="), TEXT(",") };
	TArray<FString> Values;
	ServiceConfigString.ParseIntoArray(Values, Delims, 4, false);
	for (int32 ValueIndex = 0; ValueIndex + 1 < Values.Num(); ++ValueIndex)
	{
		if (Values[ValueIndex].IsEmpty())
		{
			continue;
		}

		if (Values[ValueIndex] == TEXT("Service"))
		{
			LexFromString(ServiceConfig.Service, *StripQuotes(Values[ValueIndex + 1]));
		}
		else if (Values[ValueIndex] == TEXT("ConfigName"))
		{
			ServiceConfig.ConfigName = StripQuotes(Values[ValueIndex + 1]);
		}
		else if (Values[ValueIndex] == TEXT("OnlineSubsystem"))
		{
			ServiceConfig.OnlineSubsystem = FName(StripQuotes(Values[ValueIndex + 1]));
		}
		else if (Values[ValueIndex] == TEXT("Priority"))
		{
			ServiceConfig.Priority = FCString::Atoi(*StripQuotes(Values[ValueIndex + 1]));
		}

		++ValueIndex;
	}
}

/* unnamed */ }

void FOnlineServicesOSSAdapterModule::StartupModule()
{
	FOnlineConfigProviderGConfig ConfigProvider(GEngineIni);
	FOSSAdapterConfig Config;
	if (LoadConfig(ConfigProvider, TEXT("OnlineServices.OSSAdapter"), Config))
	{
		for (const FString& ServiceConfigString : Config.Services)
		{
			FOSSAdapterService ServiceConfig;
			ParseServiceConfig(ServiceConfig, ServiceConfigString);
			
			if (IOnlineSubsystem::IsEnabled(ServiceConfig.OnlineSubsystem))
			{
				FOnlineServicesRegistry::Get().RegisterServicesFactory(ServiceConfig.Service, MakeUnique<FOnlineServicesFactoryOSSAdapter>(ServiceConfig), ServiceConfig.Priority);
				FOnlineIdRegistryRegistry::Get().RegisterAccountIdRegistry(ServiceConfig.Service, new FOnlineAccountIdRegistryOSSAdapter(ServiceConfig.Service), ServiceConfig.Priority);
				FOnlineIdRegistryRegistry::Get().RegisterSessionIdRegistry(ServiceConfig.Service, new FOnlineSessionIdRegistryOSSAdapter(ServiceConfig.Service), ServiceConfig.Priority);
				FOnlineIdRegistryRegistry::Get().RegisterSessionInviteIdRegistry(ServiceConfig.Service, new FOnlineSessionInviteIdRegistryOSSAdapter(ServiceConfig.Service), ServiceConfig.Priority);
			}
		}
	}
}

void FOnlineServicesOSSAdapterModule::ShutdownModule()
{
	FOnlineConfigProviderGConfig ConfigProvider(GEngineIni);
	FOSSAdapterConfig Config;
	if (LoadConfig(ConfigProvider, TEXT("OnlineServices.OSSAdapter"), Config))
	{
		for (const FString& ServiceConfigString : Config.Services)
		{
			FOSSAdapterService ServiceConfig;
			ParseServiceConfig(ServiceConfig, ServiceConfigString);

			if (IOnlineSubsystem::IsEnabled(ServiceConfig.OnlineSubsystem))
			{
				FOnlineServicesRegistry::Get().UnregisterServicesFactory(ServiceConfig.Service, ServiceConfig.Priority);
				FOnlineIdRegistryRegistry::Get().UnregisterAccountIdRegistry(ServiceConfig.Service, ServiceConfig.Priority);
				FOnlineIdRegistryRegistry::Get().UnregisterSessionIdRegistry(ServiceConfig.Service, ServiceConfig.Priority);
				FOnlineIdRegistryRegistry::Get().UnregisterSessionInviteIdRegistry(ServiceConfig.Service, ServiceConfig.Priority);
			}
		}
	}
}

/* UE::Online */ }

IMPLEMENT_MODULE(UE::Online::FOnlineServicesOSSAdapterModule, OnlineServicesOSSAdapter);
