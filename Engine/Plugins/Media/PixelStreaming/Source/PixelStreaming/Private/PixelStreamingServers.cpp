// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingServers.h"
#include "PixelStreamingPrivate.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "IWebSocket.h"
#include "HAL/ThreadSafeCounter.h"
#include "WebSocketsModule.h"

namespace UE::PixelStreaming
{
	namespace Servers
	{
		static const FString SamplesRelativePath = FString(TEXT("Samples")) / TEXT("PixelStreaming") / TEXT("WebServers");
		static const FString CirrusName = FString(TEXT("SignallingWebServer"));
		static const FString TransientServerPrefix = TEXT("Transient");
		static FThreadSafeCounter TransientServerId = 0;

		/*
		 * The path to the servers.
		 * If running in editor this is the Engine's installed directory Samples/
		 * If running a packaged game, this is the game's packaged directory Samples/
		 */
		FString
		GetServersAbsPath()
		{
			return FPaths::ConvertRelativePathToFull(FPaths::RootDir() / SamplesRelativePath);
		}

		TSharedPtr<FMonitoredProcess> LaunchScriptChildProcess(FString ScriptAbsPath, FString ScriptArgs, FString LogPrefix)
		{
			// Check if the binary actually exists
			IPlatformFile& FileManager = FPlatformFileManager::Get().GetPlatformFile();
			if (!FileManager.FileExists(*ScriptAbsPath))
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("Cannot start child process - the specified file did not exist. File=%s"), *ScriptAbsPath);
				return TSharedPtr<FMonitoredProcess>();
			}

			FString ScriptRunner;
			FString ScriptRunnerParams;

// Get the executable we will use to run the scripts (e.g. cmd.exe on Windows)
#if PLATFORM_WINDOWS
			ScriptRunner = TEXT("cmd.exe");
			ScriptRunnerParams = FString::Printf(TEXT("/c \"\"%s\" %s\""), *ScriptAbsPath, *ScriptArgs);
#elif PLATFORM_LINUX
			ScriptRunner = TEXT("/usr/bin/bash");
			ScriptRunnerParams = FString::Printf(TEXT(" -- \"%s\" %s"), *ScriptAbsPath, *ScriptArgs);
#else
			UE_LOG(LogPixelStreaming, Error, TEXT("Unsupported platform for Pixel Streaming scripts."));
			return TSharedPtr<FMonitoredProcess>();
#endif

			TSharedPtr<FMonitoredProcess> ChildProcess = MakeShared<FMonitoredProcess>(ScriptRunner, ScriptRunnerParams, true, true);

			// Bind to output so we can capture the output in the log
			ChildProcess->OnOutput().BindLambda([LogPrefix](FString Output) {
				UE_LOG(LogPixelStreaming, Log, TEXT("%s - %s"), *LogPrefix, *Output);
			});

			// Run the child process
			UE_LOG(LogPixelStreaming, Log, TEXT("Launch child process - %s %s"), *ScriptRunner, *ScriptRunnerParams);
			ChildProcess->Launch();
			return ChildProcess;
		}

		FString AddPlatformScripts(FString ServerPath)
		{
			ServerPath = ServerPath / TEXT("platform_scripts");
#if PLATFORM_WINDOWS
			return ServerPath / TEXT("cmd") / TEXT("run_local.bat");
#elif PLATFORM_LINUX
			return ServerPath / TEXT("bash") / TEXT("run_local.sh");
#else
			UE_LOG(LogPixelStreaming, Error, TEXT("Unsupported platform for Pixel Streaming scripts."));
			return ServerPath;
#endif
		}

		void SelectivelyCopyServerTo(FString SourceAbsPath, FString DestinationAbsPath)
		{
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

			if (!FPaths::DirectoryExists(DestinationAbsPath))
			{
				verifyf(PlatformFile.CreateDirectory(*DestinationAbsPath), TEXT("Creating the destination directory for the servers failed"));
			}

			PlatformFile.IterateDirectory(*SourceAbsPath, [&PlatformFile, &DestinationAbsPath](const TCHAR* Path, bool bIsDir) -> bool {
				FString ExistingAbsPath = FString(Path);

				if (bIsDir)
				{
					FString DirName = FPaths::GetPathLeaf(ExistingAbsPath);
					// we don't want to copy node_modules, logs, or platform_script
					if (DirName == TEXT("node_modules") || DirName == TEXT("logs"))
					{
						return true;
					}
					// special handling for platform_scripts
					else if (DirName == TEXT("platform_scripts"))
					{
						// make "platform_scripts" dir
						FString TargetPlatformScriptsDir = DestinationAbsPath / DirName;
						verifyf(PlatformFile.CreateDirectory(*TargetPlatformScriptsDir), TEXT("Creating the platform scripts directory failed."));

						// get the cmd/bash dirs into an array so we can do a file only copy on both dirs
						const int NSubDirs = 2;
						FString PlatformScriptSubDirs[NSubDirs];
						PlatformScriptSubDirs[0] = TEXT("cmd");
						PlatformScriptSubDirs[1] = TEXT("bash");
						for (int i = 0; i < NSubDirs; i++)
						{
							FString SubDir = PlatformScriptSubDirs[i];
							FString PlatformScriptNewSubDir = TargetPlatformScriptsDir / SubDir;
							verifyf(PlatformFile.CreateDirectory(*PlatformScriptNewSubDir), TEXT("Creating the platform scripts sub directory (e.g. /cmd or /bash) failed."));
							// Get all script files in the platform_scripts/subdir
							TArray<FString> ScriptFiles;
							FString ExistingPlatformScriptsSubDir = ExistingAbsPath / SubDir;
							PlatformFile.FindFiles(ScriptFiles, *ExistingPlatformScriptsSubDir, nullptr);
							for (FString& ScriptFileAbsPath : ScriptFiles)
							{
								FString ScriptFilename = FPaths::GetPathLeaf(ScriptFileAbsPath);
								FString ToPath = PlatformScriptNewSubDir / ScriptFilename;
								verifyf(PlatformFile.CopyFile(*ToPath, *ScriptFileAbsPath), TEXT("Could not copy file to: %s from: %s"), *ToPath, *ScriptFileAbsPath);
							}
						}
					}
					// copy everything else
					else
					{
						FString CopyDestDir = DestinationAbsPath / DirName;
						verifyf(PlatformFile.CreateDirectory(*CopyDestDir), TEXT("Creating directory inside server failed - specifically %s"), *CopyDestDir);
						verifyf(PlatformFile.CopyDirectoryTree(*CopyDestDir, *ExistingAbsPath, true), TEXT("Could not copy everything to: %s from: %s"), *CopyDestDir, *ExistingAbsPath);
					}
				}
				// Copy any loose files
				else
				{
					FString Filename = FPaths::GetPathLeaf(ExistingAbsPath);
					FString ToPath = DestinationAbsPath / Filename;
					verifyf(PlatformFile.CopyFile(*ToPath, *ExistingAbsPath), TEXT("Could not copy file to: %s from: %s"), *ToPath, *ExistingAbsPath);
				}
				return true;
			});
		}

		bool CopyServerTo(FString SourceServerAbsPath, FString DestinationAbsPath)
		{

			if (!FPaths::DirectoryExists(SourceServerAbsPath))
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("The path passed to copy the servers from does not exist - this path does not exist %s."), *SourceServerAbsPath);
				return false;
			}

			if (!FPaths::DirectoryExists(DestinationAbsPath))
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("The path passed to copy the servers to does not exist - this path does not exist %s."), *DestinationAbsPath);
				return false;
			}

			// We selectively copy because some of these server folders can have a lot of content in logs, node_modules and can take a while if copied.
			SelectivelyCopyServerTo(SourceServerAbsPath, DestinationAbsPath);
			return true;
		}

		FString CreateTransientServerDirectory(FString ServerName)
		{
			// Transient servers are created mostly for test so we store them in the automation directory.
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

			FString SavedPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
			verifyf(PlatformFile.CreateDirectory(*SavedPath), TEXT("Creating /Saved directory failed."));

			// Put the transient server under Saved/${Prefix}${ServerName}/
			FString TransientDir = FString::Printf(TEXT("%s%s%d"), *TransientServerPrefix, *ServerName, TransientServerId.Increment());
			FString TransientServerPathAbs = SavedPath / TransientDir;
			UE_LOG(LogPixelStreaming, Log, TEXT("Attempting to creating directory: %s"), *TransientServerPathAbs);
			verifyf(PlatformFile.CreateDirectory(*TransientServerPathAbs), TEXT("Creating %s failed."), *TransientServerPathAbs);

			return TransientServerPathAbs;
		}

		/*
		 * Server is ephemeral because we clone an existing server directory, then when server process is
		 * complete/cancelled we destroy the cloned server directory.
		 */
		TSharedPtr<FMonitoredProcess> LaunchEphemeralServer(FString ExistingServerAbsPath, FString Args, FString& OutLaunchAbsPath)
		{
			FString ServerName = FPaths::GetPathLeaf(ExistingServerAbsPath);
			FString NewServerPath = CreateTransientServerDirectory(ServerName);
			OutLaunchAbsPath = NewServerPath;

			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

			UE_LOG(LogPixelStreaming, Log, TEXT("Copying %s to %s"), *ExistingServerAbsPath, *NewServerPath);
			CopyServerTo(ExistingServerAbsPath, NewServerPath);

			FString ScriptAbsPath = AddPlatformScripts(NewServerPath);
			TSharedPtr<FMonitoredProcess> ServerProcess = LaunchScriptChildProcess(ScriptAbsPath, Args, ServerName);

			// Bind to the cancel and delete the server folder
			ServerProcess->OnCanceled().BindLambda([NewServerPath]() {
				UE_LOG(LogPixelStreaming, Log, TEXT("Server was cancelled, recursively deleting %s"), *NewServerPath);
				IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
				bool bDeletedServerDir = PlatformFile.DeleteDirectoryRecursively(*NewServerPath);
				if (!bDeletedServerDir)
				{
					UE_LOG(LogPixelStreaming, Error, TEXT("Could not delete server dir %s"), *NewServerPath);
				}
			});

			// Bind to the complete and delete the server folder
			ServerProcess->OnCompleted().BindLambda([NewServerPath](int ExitCode) {
				UE_LOG(LogPixelStreaming, Log, TEXT("Server process was finished, recursively deleting %s"), *NewServerPath);
				IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
				bool bDeletedServerDir = PlatformFile.DeleteDirectoryRecursively(*NewServerPath);
				if (!bDeletedServerDir)
				{
					UE_LOG(LogPixelStreaming, Error, TEXT("Could not delete server dir %s"), *NewServerPath);
				}
			});

			return ServerProcess;
		}

		TSharedPtr<FMonitoredProcess> LaunchServer(FString ServerAbsPath, FLaunchArgs LaunchArgs, FString& OutLaunchAbsPath)
		{
			/* Launch the server process, either in-place, or by copying to a temp dir */

			if (LaunchArgs.bEphemeral)
			{
				return LaunchEphemeralServer(ServerAbsPath, LaunchArgs.ProcessArgs, OutLaunchAbsPath);
			}
			else
			{
				OutLaunchAbsPath = ServerAbsPath;
				return LaunchScriptChildProcess(AddPlatformScripts(ServerAbsPath), LaunchArgs.ProcessArgs, FPaths::GetPathLeaf(ServerAbsPath));
			}

			/* Note this has not handled waiting for server to become ready, that is server specific and is therefore handled in say `LaunchCirrus` */
		}

		bool ExtractValueFromArgs(FString ArgsString, FString ArgKey, FString FallbackValue, FString& OutValue)
		{
			// Tokenize string in single whitespace " ".
			TArray<FString> ArgTokens;
			ArgsString.ParseIntoArray(ArgTokens, TEXT(" "), true);

			for (FString& Token : ArgTokens)
			{
				Token.TrimStartAndEndInline();

				if (!Token.StartsWith(ArgKey, ESearchCase::Type::CaseSensitive))
				{
					continue;
				}

				// We have a matching token for our search "key" - split on it.
				FString RightStr;
				if (!Token.Split(TEXT("="), nullptr, &RightStr))
				{
					continue;
				}

				OutValue = RightStr;
				return true;
			}
			OutValue = FallbackValue;
			return false;
		}

		int NextPort()
		{
			// Todo: Checking for in-use ports.
			return (4000 + TransientServerId.Increment()) % 65535;
		}

		FString QueryOrSetServerArg(FLaunchArgs& LaunchArgs, FString ArgKey, FString FallbackArgValue)
		{
			FString OutValue;
			bool bExtractedValue = ExtractValueFromArgs(LaunchArgs.ProcessArgs, ArgKey, FallbackArgValue, OutValue);

			// No key was present so we will inject our own.
			if (!bExtractedValue)
			{
				LaunchArgs.ProcessArgs += FString::Printf(TEXT(" %s%s"), *ArgKey, *FallbackArgValue);
			}

			return OutValue;
		}

		/* ------------------------------ FWebSocketProbe -------------------------- */

		/* A utility class that tries to establish a websocket connection. */
		class FWebSocketProbe
		{
		private:
			TSharedRef<IWebSocket> WebSocket;
			FThreadSafeBool bShouldAttemptReconnect;

		public:
			FWebSocketProbe(FString Url)
				: WebSocket(FWebSocketsModule::Get().CreateWebSocket(Url, TEXT("")))
				, bShouldAttemptReconnect(true)
			{
				WebSocket->OnConnectionError().AddLambda([&bShouldAttemptReconnect = bShouldAttemptReconnect](const FString& Error) {
					UE_LOG(LogPixelStreaming, Log, TEXT("Waiting to connect to the signalling server.... %s"), *Error);
					bShouldAttemptReconnect = true;
				});
			}

			bool Probe()
			{
				bool bIsConnected = WebSocket->IsConnected();

				if (!bIsConnected && bShouldAttemptReconnect)
				{
					WebSocket->Connect();
					bShouldAttemptReconnect = false;
				}

				return bIsConnected;
			}
		};

		/* ------------------------------ FServerBase ------------------------------ */

		void FServerBase::Launch(FLaunchArgs& InLaunchArgs)
		{
			LaunchArgs = InLaunchArgs;
			bPollUntilReady = false;
			ServerProcess = LaunchImpl(InLaunchArgs, ServerRootAbsPath, Endpoints);
			bHasLaunched = true;
			PollingStartedSeconds = FPlatformTime::Seconds();
		}

		FString FServerBase::GetPathOnDisk()
		{
			return ServerRootAbsPath;
		}

		bool FServerBase::IsReady()
		{
			return bIsReady;
		}

		bool FServerBase::IsTickableWhenPaused() const
		{
			return true;
		}

		bool FServerBase::IsTickableInEditor() const
		{
			return true;
		}

		bool FServerBase::IsAllowedToTick() const
		{
			return bAllowedToTick;
		}

		void FServerBase::Tick(float DeltaTime)
		{
			// No need to do polling if polling is turned off
			if (!bPollUntilReady)
			{
				return;
			}

			// No need to start polling if we have not launched or we have already concluded the server is ready.
			if (!bHasLaunched || bIsReady)
			{
				return;
			}

			float SecondsElapsedPolling = FPlatformTime::Seconds() - PollingStartedSeconds;
			float SecondsSinceReconnect = FPlatformTime::Seconds() - LastReconnectionTimeSeconds;

			if (SecondsElapsedPolling < LaunchArgs.ReconnectionTimeoutSeconds)
			{
				if (SecondsSinceReconnect < LaunchArgs.ReconnectionIntervalSeconds)
				{
					return;
				}

				if (TestConnection())
				{
					// No need to poll anymore we are connected
					bAllowedToTick = false;
					bIsReady = true;
					UE_LOG(LogPixelStreaming, Log, TEXT("Connected to the server. Server is now ready."));
					OnReady.Broadcast(Endpoints);
					return;
				}
				else
				{
					UE_LOG(LogPixelStreaming, Log, TEXT("Polling again in another %.f seconds for server to become ready..."), LaunchArgs.ReconnectionIntervalSeconds);
					LastReconnectionTimeSeconds = FPlatformTime::Seconds();
				}
			}
			else
			{
				// No need to poll anymore we timed out
				UE_LOG(LogPixelStreaming, Error, TEXT("Server was not ready after %.f seconds, polling timed out."), LaunchArgs.ReconnectionTimeoutSeconds);
				bAllowedToTick = false;
				bTimedOut = true;
				OnFailedToReady.Broadcast();
			}
		}

		FServerBase::~FServerBase()
		{
			Stop();
		}

		void FServerBase::Stop()
		{
			if (ServerProcess)
			{
				ServerProcess->Cancel();
				ServerProcess.Reset();
			}
		}

		bool FServerBase::HasLaunched()
		{
			return bHasLaunched;
		}

		bool FServerBase::IsTimedOut()
		{
			return bTimedOut;
		}

		/* ------------------------------ FCirrus ------------------------------ */

		/* The NodeJS Cirrus signalling server wrapped in a child process */
		class FCirrus : public FServerBase
		{
		public:
			FCirrus() = default;
			virtual ~FCirrus() = default;

		private:
			TUniquePtr<FWebSocketProbe> Probe;

		protected:
			/* Begin FServerBase interface */
			void Stop() override
			{
				UE_LOG(LogPixelStreaming, Log, TEXT("Stopping Cirrus signalling server."));
				FServerBase::Stop();
			}

			TSharedPtr<FMonitoredProcess> LaunchImpl(FLaunchArgs& InLaunchArgs, FString& OutServerPath, TMap<EEndpoint, FString>& OutEndPoints) override
			{
				FString CirrusDir = GetServersAbsPath() / CirrusName;

				// Query for ports, or set them if they don't exist
				FString StreamerPort = QueryOrSetServerArg(InLaunchArgs, TEXT("--StreamerPort="), FString::FromInt(NextPort()));
				FString SFUPort = QueryOrSetServerArg(InLaunchArgs, TEXT("--SFUPort="), FString::FromInt(NextPort()));
				FString MatchmakerPort = QueryOrSetServerArg(InLaunchArgs, TEXT("--MatchmakerPort="), FString::FromInt(NextPort()));
				FString HttpPort = QueryOrSetServerArg(InLaunchArgs, TEXT("--HttpPort="), FString::FromInt(NextPort()));

				// Construct endpoint urls
				const FString SignallingStreamerUrl = FString::Printf(TEXT("ws://localhost:%s"), *StreamerPort);
				const FString WebserverUrl = FString::Printf(TEXT("http://localhost:%s"), *HttpPort);
				OutEndPoints.Add(EEndpoint::Signalling_Streamer, SignallingStreamerUrl);
				OutEndPoints.Add(EEndpoint::Signalling_Players, FString::Printf(TEXT("ws://localhost:%s"), *HttpPort));
				OutEndPoints.Add(EEndpoint::Signalling_SFU, FString::Printf(TEXT("ws://localhost:%s"), *SFUPort));
				OutEndPoints.Add(EEndpoint::Signalling_Matchmaker, FString::Printf(TEXT("ws://localhost:%s"), *MatchmakerPort));
				OutEndPoints.Add(EEndpoint::Signalling_Webserver, WebserverUrl);

				TSharedPtr<FMonitoredProcess> CirrusProcess = LaunchServer(CirrusDir, InLaunchArgs, OutServerPath);
				UE_LOG(LogPixelStreaming, Log, TEXT("Cirrus signalling server running at: %s"), *WebserverUrl);

				if (bPollUntilReady)
				{
					Probe = MakeUnique<FWebSocketProbe>(SignallingStreamerUrl);
				}

				return CirrusProcess;
			}

			bool TestConnection() override
			{
				if (bIsReady)
				{
					return true;
				}
				else
				{
					bool bConnected = Probe->Probe();
					if (bConnected)
					{
						// Close the websocket connection so others can use it
						Probe.Reset();
						return true;
					}
					else
					{
						return false;
					}
				}
			}

			/* End FServerBase interface */
		};

		/* -------------------- Public static utility methods -------------------- */
		TSharedPtr<FServerBase> MakeSignallingServer()
		{
			return MakeShared<FCirrus>();
		}

	} // namespace Servers
} // namespace UE::PixelStreaming
