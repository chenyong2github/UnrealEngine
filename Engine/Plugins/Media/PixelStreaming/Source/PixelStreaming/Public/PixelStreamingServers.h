// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/MonitoredProcess.h"
#include "Templates/SharedPointer.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "Containers/Map.h"
#include "Tickable.h"
#include "HAL/ThreadSafeBool.h"

namespace UE::PixelStreaming
{
	/*
	 * Utility for launching Pixel Streaming servers such as Cirrus, SFU, and Matchmaker.
	 * This utility automatically searchs in known locations for these servers and if found, launchs them as child processes.
	 */
	namespace Servers
	{

		/**
		 * Configuration to control behaviour when launching any of the Pixel Streaming servers.
		 **/
		struct PIXELSTREAMING_API FLaunchArgs
		{
			// If true, will copy to a temp directory and when finished delete itself. If false, will launch from Samples/PixelStreaming/WebServers/*.
			bool bEphemeral = true;

			// Arguments passed to the actual server when its process is started.
			FString ProcessArgs = TEXT("");

			// Reconnection timeout in seconds
			float ReconnectionTimeoutSeconds = 30.0f;

			// Reconnect interval in seconds.
			float ReconnectionIntervalSeconds = 2.0f;
		};

		/**
		 * Endpoints for the various Pixel Streaming servers.
		 **/
		enum class PIXELSTREAMING_API EEndpoint
		{
			// The websocket signalling url between the server and the UE streamer - e.g. ws://localhost:8888
			Signalling_Streamer,

			// The websocket signalling url between the server and the players (aka. web browsers) - e.g. ws://localhost:80
			Signalling_Players,

			// The websocket signalling url between the server and the matchmaker server - e.g. ws://localhost:9999
			Signalling_Matchmaker,

			// The websocket signalling url between the server and the SFU server - e.g. ws://localhost:8889
			Signalling_SFU,

			// The http url for the webserver hosted within the signalling server - e.g. http://localhost
			Signalling_Webserver
		};

		// ---------------------------------------------------------------------------------------------
		typedef TMap<EEndpoint, FString> FEndpoints;
		DECLARE_MULTICAST_DELEGATE_OneParam(FOnReady, const FEndpoints& /* Endpoint urls */);
		// ---------------------------------------------------------------------------------------------

		/**
		 *  Base class for all Pixel Streaming servers.
		 **/
		class PIXELSTREAMING_API FServerBase : public FTickableGameObject
		{
		public:
			virtual ~FServerBase();

			/* Immediately stops the child process that was running the server. */
			virtual void Stop();

			/**
			 * @return	The absolute path to the root directory that the server was launched from.
			 **/
			virtual FString GetPathOnDisk();

			/**
			 * @return	True if the server has been launched. Note: Launched does not necessarily mean it is connectible yet. Bind to OnReady for that.
			 **/
			virtual bool HasLaunched();

			/**
			 * Launch the server in a child process using the supplied launch arguments.
			 * @param LaunchArgs	The launch arguments to control how the server is launched, including what args to pass to the child process.
			 **/
			virtual void Launch(FLaunchArgs& InLaunchArgs);

			/**
			 * @return	True if the server has been connected to and is ready for new connections.
			 **/
			virtual bool IsReady();

			/**
			 * @return	True if the server has timed out while trying to establish a connection.
			 **/
			virtual bool IsTimedOut();

			/* Begin FTickableGameObject */
			virtual bool IsTickableWhenPaused() const override;
			virtual bool IsTickableInEditor() const override;
			virtual void Tick(float DeltaTime) override;
			virtual bool IsAllowedToTick() const override;
			TStatId GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(PixelStreamingServers, STATGROUP_Tickables); }
			/* End FTickableGameObject */

		protected:
			/**
			 * Implemented by derived types. The actual implementation of how this specified server is launched.
			 * @param LaunchArgs	The launch arguments to use.
			 * @param OutServerPath	The output absolute path to the root directory containing the server we wish to launch - can change if server is ephemeral.
			 * @param OutEndpoints	The output endpoints the user can expect to use with this server.
			 **/
			virtual TSharedPtr<FMonitoredProcess> LaunchImpl(FLaunchArgs& InLaunchArgs, FString& OutServerPath, TMap<EEndpoint, FString>& OutEndpoints) = 0;

			/**
			 * Implemented by derived types. Implementation specific but somehow the server has been tested to see if it ready for connections.
			 * @return	True if the server is able to be connected to.
			 **/
			virtual bool TestConnection() = 0;

		public:
			// Delegate fired when the server is ready for connections, first parameter is a map of all supported endpoints and their urls.
			FOnReady OnReady;

			DECLARE_MULTICAST_DELEGATE(FOnFailedToReady);
			/* Can fire when the server is unable to be contacted or connecting to it timed out. */
			FOnFailedToReady OnFailedToReady;

		protected:
			FLaunchArgs LaunchArgs;
			TSharedPtr<FMonitoredProcess> ServerProcess;
			bool bHasLaunched = false;
			FString ServerRootAbsPath;
			TMap<EEndpoint, FString> Endpoints;
			FThreadSafeBool bIsReady = false;
			float PollingStartedSeconds = 0.0f;
			float LastReconnectionTimeSeconds = 0.0f;
			bool bAllowedToTick = true;
			bool bTimedOut = false;
		};

		/* -------------- Static utility methods for working with Pixel Streaming servers. ----------------- */

		/**
		 * Creates a NodeJS Cirrus signalling server by launching it as a child process.
		 * Note: Calling this method does not launch the server. You should call Launch() yourself
		 * once you have bound to appropriate delegates such as OnReady.
		 * @return	The signalling server wrapped in a server object for simple management.
		 **/
		PIXELSTREAMING_API TSharedPtr<FServerBase> MakeSignallingServer();

	} // namespace Servers

} // namespace UE::PixelStreaming
