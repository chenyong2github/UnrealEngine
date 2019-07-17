// Copyright 2011-2019 Molecular Matters GmbH, all rights reserved.

#pragma once

#include "CoreTypes.h"
#include "LC_Thread.h"
#include "LC_Commands.h"
#include "LC_Telemetry.h"
#include "LC_DuplexPipeServer.h"
#include "LC_CriticalSection.h"
#include "LC_Event.h"
#include "LC_Scheduler.h"
#include "LC_Executable.h"
#include "LC_RunMode.h"
#include "LC_Types.h"
#include "LC_LiveModule.h"
#include "VisualStudioDTE.h"


class MainFrame;
class DirectoryCache;
class LiveModule;
class LiveProcess;

class ServerCommandThread
{
public:
	ServerCommandThread(MainFrame* mainFrame, const wchar_t* const processGroupName, RunMode::Enum runMode);
	~ServerCommandThread(void);

	void RestartTargets(void);

	std::wstring GetProcessImagePath(void) const;

private:
	scheduler::Task<LiveModule*>* LoadModule(unsigned int processId, void* moduleBase, const wchar_t* modulePath, scheduler::TaskBase* taskRoot);
	bool UnloadModule(unsigned int processId, const wchar_t* modulePath);

	void PrewarmCompilerEnvironmentCache(void);

	unsigned int ServerThread(void);
	unsigned int CompileThread(void);

	struct CommandThreadContext
	{
		DuplexPipeServer pipe;
		Event* readyEvent;
		thread::Handle commandThread;

		DuplexPipeServer exceptionPipe;
		thread::Handle exceptionCommandThread;
	};

	unsigned int CommandThread(DuplexPipeServer* pipe, Event* readyEvent);
	unsigned int ExceptionCommandThread(DuplexPipeServer* exceptionPipe);

	void RemoveCommandThread(const DuplexPipe* pipe);

	LiveProcess* FindProcessById(unsigned int processId);

	void CompileChanges(bool didAllProcessesMakeProgress);


	// actions
	struct actions
	{
		#define DECLARE_ACTION(_name)																													\
			struct _name																																\
			{																																			\
				typedef ::commands::_name CommandType;																									\
				static bool Execute(const CommandType* command, const DuplexPipe* pipe, void* context, const void* payload, size_t payloadSize);		\
			}

		DECLARE_ACTION(TriggerRecompile);
		DECLARE_ACTION(LogMessage);
		DECLARE_ACTION(BuildPatch);
		DECLARE_ACTION(HandleException);
		DECLARE_ACTION(ReadyForCompilation);
		DECLARE_ACTION(DisconnectClient);
		DECLARE_ACTION(RegisterProcess);

		DECLARE_ACTION(EnableModules);
		DECLARE_ACTION(DisableModules);

		DECLARE_ACTION(ApplySettingBool);
		DECLARE_ACTION(ApplySettingInt);
		DECLARE_ACTION(ApplySettingString);

		// BEGIN EPIC MOD - Adding ShowConsole command
		DECLARE_ACTION(ShowConsole);
		// END EPIC MOD

		// BEGIN EPIC MOD - Adding ShowConsole command
		DECLARE_ACTION(SetVisible);
		// END EPIC MOD

		// BEGIN EPIC MOD - Adding SetActive command
		DECLARE_ACTION(SetActive);
		// END EPIC MOD

		// BEGIN EPIC MOD - Adding SetBuildArguments command
		DECLARE_ACTION(SetBuildArguments);
		// END EPIC MOD

		// BEGIN EPIC MOD - Adding support for lazy-loading modules
		DECLARE_ACTION(EnableLazyLoadedModule);
		DECLARE_ACTION(FinishedLazyLoadingModules);
		// END EPIC MOD

		#undef DECLARE_ACTION
	};


	std::wstring m_processGroupName;
	RunMode::Enum m_runMode;

	MainFrame* m_mainFrame;
	thread::Handle m_serverThread;
	thread::Handle m_compileThread;

	types::vector<LiveModule*> m_liveModules;
	types::vector<LiveProcess*> m_liveProcesses;
	types::unordered_map<executable::Header, LiveModule*> m_imageHeaderToLiveModule;

	CriticalSection m_actionCS;
	CriticalSection m_exceptionCS;
	Event m_inExceptionHandlerEvent;
	Event m_handleCommandsEvent;

	// directory cache for all modules combined
	DirectoryCache* m_directoryCache;

	// keeping track of the client connections
	CriticalSection m_connectionCS;
	types::vector<CommandThreadContext*> m_commandThreads;

	// BEGIN EPIC MOD - Adding SetActive command
	bool m_active = true;
	// END EPIC MOD

	// BEGIN EPIC MOD - Lazy loading modules
	bool EnableRequiredModules(const TArray<FString>& RequiredModules);
	// END EPIC MOD

	// for triggering recompiles using the API
	bool m_manualRecompileTriggered;
	types::unordered_map<std::wstring, types::vector<LiveModule::ModifiedObjFile>> m_liveModuleToModifiedOrNewObjFiles;

	// restart mechanism
	CriticalSection m_restartCS;
	void* m_restartJob;
	unsigned int m_restartedProcessCount;
#if WITH_VISUALSTUDIO_DTE
	types::unordered_map<unsigned int, EnvDTE::DebuggerPtr> m_restartedProcessIdToDebugger;
#endif
};
