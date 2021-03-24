// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAnalyticsProviderET.h" // NOTE: Consider changing the code to replace IAnalyticProvider.h by IAnalyticProviderET.h

class FEngineSessionManager;
class IAnalyticsProvider;
class IAnalyticsProviderET;
struct FAnalyticsEventAttribute;

/**
 * The public interface for the editor's analytics provider singleton.
 * 
 * WARNING: This is an analytics provider instance that is created whenever UE editor is launched. 
 * It is intended ONLY for use by Epic Games. This is NOT intended for games to send 
 * game-specific telemetry. Create your own provider instance for your game and configure
 * it independently.
 *
 * It is called FEngineAnalytics for legacy reasons, and is only used for editor telemetry.
 */
class FEngineAnalytics : FNoncopyable
{
public:
	/**
	 * Return the provider instance. Not valid outside of Initialize/Shutdown calls.
	 * Note: must check IsAvailable() first else this code will assert if the provider is not valid.
	 */
	static ENGINE_API IAnalyticsProviderET& GetProvider();

	/** Helper function to determine if the provider is valid. */
	static ENGINE_API bool IsAvailable() { return Analytics.IsValid(); }

	/** Called to initialize the singleton. */
	static ENGINE_API void Initialize();

	/** Called to shut down the singleton */
	static ENGINE_API void Shutdown(bool bIsEngineShutdown = false);

	static ENGINE_API void Tick(float DeltaTime);

	static ENGINE_API void LowDriveSpaceDetected();

private:
	static bool bIsInitialized;
	static ENGINE_API TSharedPtr<IAnalyticsProviderET> Analytics;
	static TSharedPtr<FEngineSessionManager> SessionManager;
};

