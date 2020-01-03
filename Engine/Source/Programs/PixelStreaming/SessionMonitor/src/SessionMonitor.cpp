// Copyright Epic Games, Inc. All Rights Reserved.

#include "SessionMonitorPCH.h"

#include "Logging.h"
#include "StringUtils.h"
#include "CmdLine.h"
#include "Console.h"
#include "FileLogOutput.h"
#include "OwnCrashDetection.h"
#include "Spawner.h"
#include "Monitor.h"
#include "Config.h"
#include "MonitorController.h"
#include "ScopeGuard.h"
#include "Utils.h"

const char* Help =
"\
Pixel Streaming SessionMonitor\n\
Copyright Epic Games, Inc. All Rights Reserved.\n\
Parameters:\n\
\n\
-help\n\
Shows this help\n\
\n\
-ConfigFile=\"File\"\n\
File to read the configuration from. If not specified, it defaults to \"SessionMonitor-Config.json\"\n\
\n\
-LocalTime\n\
If specified, it will use local time in logging, instead of UTC.\n\
\n\
-v\n\
Verbose mode (enables Verbose logs)\n\
\n\
-vv\n\
Very verbose mode (enabled VeryVerbose logs)\n\
\n\
";

std::string PARAM_ConfigFile = "SessionMonitorConfig.json";
bool PARAM_LocalTime = false; // By default we use UTC time
std::string RootDir;

std::unique_ptr<FMonitor> Monitor;

template<typename F>
void AddWork(F&& Func)
{
	Monitor->GetIOContext().post(std::forward<F>(Func));
}

bool ParseParameters(int argc, char* argv[])
{
	FCmdLine Params;
	if (!Params.Parse(argc, argv))
	{
		printf(Help);
		return false;
	}

	if (Params.Has("Help"))
	{
		printf(Help);
		return false;
	}

	if (Params.Has("v"))
	{
		LogDefault.SetVerbosity(ELogVerbosity::Verbose);
	}

	if (Params.Has("vv"))
	{
		LogDefault.SetVerbosity(ELogVerbosity::VeryVerbose);
	}

	if (Params.Has("ConfigFile"))
		PARAM_ConfigFile = Params.Get("ConfigFile");
	PARAM_LocalTime = Params.Has("LocalTime");

	return true;
}

// This is used by the Control handler (set with ConsoleCtrlHandler function)
// to wait for the main thread to finish
std::atomic<bool> bFinished = false;
bool bShuttingDown = false;
DWORD MainThreadId = 0;

void TriggerShutdown()
{
	AddWork([]()
	{
		bShuttingDown = true;
		Monitor->Stop(true);
	});
}

// Handler function will be called on separate thread!
static BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType)
{
	EG_LOG(LogDefault, Log, "Console Ctrl Handler: %lu", dwCtrlType);
	EG_LOG(LogDefault, Log, "Waiting to finish UE4WebRTCProxy...");

	if (!MainThreadId)
	{
		return FALSE;
	}

	TriggerShutdown();

	// Wait for the main thread to finish
	while (!bFinished)
	{
		Sleep(100);
	}

	// Return TRUE if handled this message, further handler functions won't be called.
	// Return FALSE to pass this message to further handlers until default handler calls ExitProcess().
	return FALSE;
}

int mainImpl(int argc, char* argv[])
{
	FConsole Console;
	Console.Init(120, 40, 400, 2000);

	RootDir = GetProcessPath();

	// Set the working directory to where our executable is
	if (!SetCurrentDirectoryW(Widen(RootDir).c_str()))
	{
		EG_LOG(LogDefault, Error, "Could not set the current working directory.");
		return EXIT_FAILURE;
	}

	// NOTE: Parsing the parameters before creating the file logger, so the log
	// filename takes into account the -LocalTime parameter (if specified)
	if (!ParseParameters(argc, argv))
	{
		return EXIT_FAILURE;
	}

	std::thread ExitCheckThread = std::thread([]
	{
		while (!bShuttingDown)
		{
			if (_kbhit())
			{
				int Ch = _getch();
				if (Ch == 'q')
				{
					TriggerShutdown();
				}
			}
		}
	});

	auto JoinExitCheckThread = ScopeGuard([&]
	{
		bShuttingDown = true;
		ExitCheckThread.join();
	});

	MainThreadId = GetCurrentThreadId();
	SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

	//
	// Create file loggers
	//
	FFileLogOutput FileLogger(nullptr); // Our own log file

	// Log the command line parameters, so we know what parameters were used for this run
	{
		std::string Str;
		for (int i = 0; i < argc; i++)
		{
			Str += std::string(argv[i]) + " ";
		}

		EG_LOG(LogDefault, Log, "CmdLine: %s", Str.c_str());
	}

	SetupOwnCrashDetection();
	// If you want to test crash detection when not running a debugger, enable the block below.
	// It will cause an access violation after 1 second.
	// NOTE: If running under the debugger, it will not trigger the crash detection
#if 0
	std::thread([]()
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		*reinterpret_cast<uint32_t*>(0) = 1;
	}).detach();
#endif

	std::string ConfigFilename;
	if (!FullPath(ConfigFilename, PARAM_ConfigFile, RootDir))
	{
		return EXIT_FAILURE;
	}

	std::vector<FAppConfig> Cfg = ReadConfig(ConfigFilename);

	boost::asio::io_context IOContext;
	Monitor = std::make_unique<FMonitor>(IOContext, std::move(Cfg));
	std::unique_ptr<FMonitorController> MonitorController;
	try
	{
		MonitorController = std::make_unique<FRestAPIMonitorController>(*Monitor, "http://127.0.0.1:40080", true);
	}
	catch (std::exception& e)
	{
		EG_LOG(LogDefault, Fatal, "Error creating monitor controller. Reason=%s", e.what());
	}

	EG_LOG(LogDefault, Log, "Ready and waiting for commands!");

	IOContext.run();

	EG_LOG(LogDefault, Log, "Exiting SessionMonitor");
	Monitor = nullptr;

    return EXIT_SUCCESS;
}

int main(int argc, char* argv[])
{
	int ExitCode;
	try
	{
		ExitCode = mainImpl(argc, argv);
	}
	catch (std::exception&e)
	{
		printf("%s\n", e.what());
		ExitCode = EXIT_FAILURE;
	}

	bFinished = true;
	return ExitCode;
}
