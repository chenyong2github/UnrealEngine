// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

#ifndef UE_WITH_ZEN
#	if PLATFORM_WINDOWS
#		define UE_WITH_ZEN 1
#	else
#		define UE_WITH_ZEN 0
#	endif
#endif

#if UE_WITH_ZEN

#include "Containers/StringView.h"
#include "Containers/UnrealString.h"
#include "Templates/UniquePtr.h"
#include "ZenStatistics.h"

#define UE_API DERIVEDDATACACHE_API

namespace UE::Zen {

class FZenServiceInstance;

enum EServiceMode {Default, DefaultNoLaunch};

/**
 * Type used to declare usage of a Zen server instance whether the shared default instance or a unique non-default instance.
 * Used to help manage launch, and optionally in the future, shutdown a shared default instance.  Use the default constructor
 * to reference the default instance (which may be launched on demand), or use the non-default constructors with a specific
 * URL or HostName/Port pair which is required to pre-exist (will not be auto-launched).
 */
class FScopeZenService
{
public:
	UE_API FScopeZenService()
	: FScopeZenService(FStringView())
	{
	}
	UE_API FScopeZenService(FStringView InstanceURL);
	UE_API FScopeZenService(FStringView InstanceHostName, uint16 InstancePort);
	UE_API FScopeZenService(EServiceMode Mode);

	UE_API const FZenServiceInstance& GetInstance() const { return *ServiceInstance; }
	UE_API FZenServiceInstance& GetInstance() { return *ServiceInstance; }

private:
	FZenServiceInstance* ServiceInstance;
	TUniquePtr<UE::Zen::FZenServiceInstance> UniqueNonDefaultInstance;
};

/**
 * Gets the default Zen service instance.  The default instance is configured through ZenServiceInstance INI section.
 * The default instance can (depending on configuration):
 *  - Auto-launched on demand (optionally elevated on Windows)
 *  - Be copied out of the workspace tree before execution
 *  - Shared between multiple tools running concurrently (implemented by launching multiple instances and expecting them to communicate and shutdown as needed)
 *  - Instigate self-shutdown when all processes that requested it have terminated
 *  - Use a subdirectory of the existing local DDC cache path as Zen's data path
 *  - Use an upstream Zen service
 *  - Be overridden by commandline arguments to reference an existing running instance instead of auto-launching a new instance.
 * Note that no assumptions should be made about the hostname/port of the default instance.  Calling code should instead expect
 * that the instance is authoritative over the hostname/port that it will end up using and should query that information from the
 * instance as needed.
 */
UE_API FZenServiceInstance& GetDefaultServiceInstance();

/**
 * A representation of a Zen service instance.  Generally not accessed directly, but via FScopeZenService.
 */
class FZenServiceInstance
{
public:

	 UE_API FZenServiceInstance()
	 : FZenServiceInstance(Default, FStringView())
	 {
	 }
	 UE_API FZenServiceInstance(EServiceMode Mode, FStringView InstanceURL);
	 UE_API ~FZenServiceInstance();

	 inline const TCHAR* GetURL() const { return *URL; }
	 inline const TCHAR* GetHostName() const { return *HostName; }
	 inline uint16 GetPort() const { return Port; }
	 UE_API bool GetStats(FZenStats& stats) const;
	 UE_API bool IsServiceRunning();
	 UE_API bool IsServiceReady();

private:
	struct FZenConnectSettings
	{
		FString HostName;
		uint16 Port = 1337;
	};

	struct FSettings
	{
		bool bAutoLaunch = true;

		struct FAutoLaunchSettings
		{
			FString WorkspaceDataPath;
			FString DataPath;
			FString LogPath;
			FString ExtraArgs;
			uint16 DesiredPort = 1337;
			bool bHidden = true;
		} AutoLaunchSettings;

		FZenConnectSettings ConnectExistingSettings;
	};

	void PopulateSettings(FStringView InstanceURL);
	void PromptUserToStopRunningServerInstance(const FString& ServerFilePath);
	FString ConditionalUpdateLocalInstall();
	bool AutoLaunch();

	FSettings Settings;
	FString URL;
	FString HostName;
	uint16 Port;
	static inline uint16 AutoLaunchedPort = 0;
	bool bHasLaunchedLocal = false;
};

} // namespace UE::Zen

#undef UE_API

#else

namespace UE::Zen {

enum EServiceMode {Default, DefaultNoLaunch};

} // namespace UE::Zen

#endif // UE_WITH_ZEN
