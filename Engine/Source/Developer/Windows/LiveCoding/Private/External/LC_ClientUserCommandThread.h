// Copyright 2011-2019 Molecular Matters GmbH, all rights reserved.

#pragma once

#include "CoreTypes.h"
#include "LC_Thread.h"
#include "LC_CriticalSection.h"
#include "LC_Semaphore.h"
#include <string>
#include <deque>

class DuplexPipe;
class DuplexPipeClient;
class Event;


// handles incoming commands from the host (the executable that loaded the Live++ DLL)
class ClientUserCommandThread
{
public:
	class BaseCommand
	{
	public:
		explicit BaseCommand(bool expectResponse);
		virtual ~BaseCommand(void);

		virtual void Execute(DuplexPipe* pipe) = 0;

		bool ExpectsResponse(void) const;

	private:
		bool m_expectResponse;
	};

	struct ExceptionResult
	{
		const void* returnAddress;
		const void* framePointer;
		const void* stackPointer;
		bool continueExecution;
	};

	ClientUserCommandThread(DuplexPipeClient* pipeClient, DuplexPipeClient* exceptionPipeClient);
	~ClientUserCommandThread(void);

	// Starts the thread that takes care of handling incoming commands on the pipe.
	// Returns the thread ID.
	unsigned int Start(const std::wstring& processGroupName, Event* waitForStartEvent, CriticalSection* pipeAccessCS);

	// Joins this thread.
	void Join(void);

	void* EnableModule(const wchar_t* nameOfExeOrDll);
	void* EnableModules(const wchar_t* namesOfExeOrDll[], unsigned int count);
	void* EnableAllModules(const wchar_t* nameOfExeOrDll);

	void* DisableModule(const wchar_t* nameOfExeOrDll);
	void* DisableModules(const wchar_t* namesOfExeOrDll[], unsigned int count);
	void* DisableAllModules(const wchar_t* nameOfExeOrDll);

	// BEGIN EPIC MOD - Adding TryWaitForToken
	bool TryWaitForToken(void* token);
	// END EPIC MOD

	void WaitForToken(void* token);
	void TriggerRecompile(void);
	void LogMessage(const wchar_t* message);
	void BuildPatch(const wchar_t* moduleNames[], const wchar_t* objPaths[], const wchar_t* amalgamatedObjPaths[], unsigned int count);

	void TriggerRestart(void);

	// BEGIN EPIC MOD - Adding ShowConsole command
	void ShowConsole();
	// END EPIC MOD

	// BEGIN EPIC MOD - Adding SetVisible command
	void SetVisible(bool visible);
	// END EPIC MOD

	// BEGIN EPIC MOD - Adding SetActive command
	void SetActive(bool active);
	// END EPIC MOD

	// BEGIN EPIC MOD - Adding SetBuildArguments command
	void SetBuildArguments(const wchar_t* arguments);
	// END EPIC MOD

	// BEGIN EPIC MOD - Support for lazy-loading modules
	void* EnableLazyLoadedModule(const wchar_t* fileName, Windows::HMODULE moduleBase);
	// END EPIC MOD

	void ApplySettingBool(const char* settingName, int value);
	void ApplySettingInt(const char* settingName, int value);
	void ApplySettingString(const char* settingName, const wchar_t* value);

	void InstallExceptionHandler(void);
	ExceptionResult HandleException(EXCEPTION_RECORD* exception, CONTEXT* context, unsigned int threadId);
	void End(void);

private:
	// pushes a user command into the command queue
	void PushUserCommand(BaseCommand* command);

	// pops a user command from the command queue.
	// blocks until a command becomes available.
	BaseCommand* PopUserCommand(void);

	unsigned int ThreadFunction(Event* waitForStartEvent, CriticalSection* pipeAccessCS);

	thread::Handle m_thread;
	std::wstring m_processGroupName;
	DuplexPipeClient* m_pipe;
	DuplexPipeClient* m_exceptionPipe;

	// queue for working on commands received by user code
	std::deque<BaseCommand*> m_userCommandQueue;
	CriticalSection m_userCommandQueueCS;
	Semaphore m_userCommandQueueSema;
};
