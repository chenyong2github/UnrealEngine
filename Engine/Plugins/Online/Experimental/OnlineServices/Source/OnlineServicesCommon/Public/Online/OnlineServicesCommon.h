// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/OnlineServices.h"

#include "Online/OnlineComponentRegistry.h"
#include "Online/OnlineAsyncOpCache.h"
#include "Online/OnlineConfig.h"

#include "Containers/Ticker.h"
#include "Templates/SharedPointer.h"

#include "Async/Async.h"

namespace UE::Online {

class ONLINESERVICESCOMMON_API FOnlineServicesCommon
	: public IOnlineServices
	, public TSharedFromThis<FOnlineServicesCommon>
	, public FTSTickerObjectBase
{
public:
	using Super = IOnlineServices;

	FOnlineServicesCommon();
	FOnlineServicesCommon(const FOnlineServicesCommon&) = delete;
	FOnlineServicesCommon(FOnlineServicesCommon&&) = delete;
	virtual ~FOnlineServicesCommon() {}

	// IOnlineServices
	virtual void Init() override;
	virtual void Destroy() override;
	virtual IAuthPtr GetAuthInterface() override;
	virtual IFriendsPtr GetFriendsInterface() override;

	// FOnlineServicesCommon

	/**
	 * Retrieve any of the Interface IOnlineComponents
	 */
	template <typename ComponentType>
	ComponentType* Get()
	{
		return Components.Get<ComponentType>();
	}

	/**
	 * Called to register all the IOnlineComponents with the IOnlineService, called after this is constructed
	 */
	virtual void RegisterComponents();

	/**
	 * Calls Initialize on all the components, called after RegisterComponents
	 */
	virtual void Initialize();

	/**
	 * Calls PostInitialize on all the components, called after Initialize
	 */
	virtual void PostInitialize();

	/**
	 * Calls LoadConfig on all the components
	 */
	virtual void LoadConfig();

	/**
	 * Calls Tick on all the components
	 */
	virtual bool Tick(float DeltaSeconds) override;

	/**
	 * Calls PreShutdown on all the components, called prior to Shutdown
	 */
	virtual void PreShutdown();

	/**
	 * Calls Shutdown on all the components, called before this is destructed
	 */
	virtual void Shutdown();

	/**
	 * Call a callable according to a specified execution policy
	 */
	template <typename CallableType>
	void Execute(FOnlineAsyncExecutionPolicy ExecutionPolicy, CallableType&& Callable)
	{
		if (ExecutionPolicy.GetExecutionPolicy() == EOnlineAsyncExecutionPolicy::RunOnGameThread)
		{
			ExecuteOnGameThread(MoveTempIfPossible(Callable));
		}
	}

	/**
	 * Call a callable on the game thread
	 */
	template <typename CallableType>
	void ExecuteOnGameThread(CallableType&& Callable)
	{
		if (IsInGameThread())
		{
			Callable();
		}
		else
		{
			Async(EAsyncExecution::TaskGraphMainThread, MoveTemp(Callable));
		}
	}

	/**
	 * Override the default config provider (FOnlineConfigProviderGConfig(GEngineini))
	 */
	void SetConfigProvider(TUniquePtr<IOnlineConfigProvider>&& InConfigProvider)
	{
		ConfigProvider = MoveTemp(InConfigProvider);
	}

	/**
	 * Clear the list of config overrides
	 */
	void ResetConfigSectionOverrides()
	{
		ConfigSectionOverrides.Reset();
	}

	/**
	 * Add a config section override. These will be used in the order they are added
	 */
	void AddConfigSectionOverride(const FString& Override)
	{
		ConfigSectionOverrides.Add(Override);
	}

	/**
	 * Get the config name for the Subsystem
	 */
	const FString& GetConfigName() const { return ConfigName; }

	/**
	 * Load a config struct for an interface + operation
	 * Will load values from the following sections:
	 *   OnlineServices
	 *   OnlineServices.<ServiceProvider>
	 *   OnlineServices.<ServiceProvider>.<InterfaceName> (if InterfaceName is set)
	 *   OnlineServices.<ServiceProvider>.<InterfaceName>.<OperationName> (if OperationName is set)
	 * 
	 * @param Struct Struct to populate with values from config
	 * @param InterfaceName Optional interface name to append to the config section name
	 * @param OperationName Optional operation name to append to the config section name
	 * 
	 * @return true if a value was loaded
	 */
	template <typename StructType>
	bool LoadConfig(StructType& Struct, const FString& InterfaceName = FString(), const FString& OperationName = FString())
	{
		TArray<FString> SectionHeiarchy;
		FString SectionName = TEXT("OnlineServices");
		SectionHeiarchy.Add(SectionName);
		SectionName += TEXT(".") + GetConfigName();
		SectionHeiarchy.Add(SectionName);
		if (!InterfaceName.IsEmpty())
		{
			SectionName += TEXT(".") + InterfaceName;
			SectionHeiarchy.Add(SectionName);
			if (!InterfaceName.IsEmpty())
			{
				SectionName += TEXT(".") + OperationName;
				SectionHeiarchy.Add(SectionName);
			}
		}
		return LoadConfig(Struct, SectionHeiarchy);
	}

	/**
	 * Load a config struct for a section heiarchy, also using the ConfigSectionOverrides
	 *
	 * @param Struct Struct to populate with values from config
	 * @param SectionHeiarchy Array of config sections to load values from
	 *
	 * @return true if a value was loaded
	 */
	template <typename StructType>
	bool LoadConfig(StructType& Struct, const TArray<FString>& SectionHeiarchy)
	{
		bool bLoadedConfig = false;
		for (const FString& Section : SectionHeiarchy)
		{
			bLoadedConfig |= LoadConfig(*ConfigProvider, Section, Struct);
			for (const FString& Override : ConfigSectionOverrides)
			{
				FString OverrideSection = Section + TEXT(" ") + Override;
				bLoadedConfig |= LoadConfig(*ConfigProvider, OverrideSection, Struct);
			}
		}
		return bLoadedConfig;
	}

	FOnlineAsyncOpCache OpCache;

protected:
	FOnlineComponentRegistry Components;
	TUniquePtr<IOnlineConfigProvider> ConfigProvider;

	/* Config section overrides */
	TArray<FString> ConfigSectionOverrides;
	FString ConfigName;
};

/* UE::Online */ }

