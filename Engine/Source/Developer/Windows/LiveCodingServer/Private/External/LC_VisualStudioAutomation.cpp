// Copyright 2011-2019 Molecular Matters GmbH, all rights reserved.

#include "LC_VisualStudioAutomation.h"
#include "LC_COMThread.h"
#include "LC_StringUtil.h"
#include "LC_Logging.h"
#include "Microsoft/COMPointer.h"

#if WITH_VISUALSTUDIO_DTE

namespace
{
	static COMThread* g_comThread = nullptr;


	// helper function that waits until the debugger finished executing a command and is in break mode again
	static void WaitUntilBreakMode(const EnvDTE::DebuggerPtr& debugger)
	{
		EnvDTE::dbgDebugMode mode = EnvDTE::dbgDesignMode;

		while (mode != EnvDTE::dbgBreakMode)
		{
			debugger->get_CurrentMode(&mode);
			thread::Sleep(5u);
		}
	}
}


void visualStudio::Startup(void)
{
	// start the COM thread that does the actual work
	g_comThread = new COMThread;
}


void visualStudio::Shutdown(void)
{
	// shut down the COM thread
	memory::DeleteAndNull(g_comThread);
}


static EnvDTE::DebuggerPtr FindDebuggerAttachedToProcess(unsigned int processId)
{
	// get all objects from the running object table (ROT)
	TComPtr<IRunningObjectTable> rot = nullptr;
	HRESULT result = ::GetRunningObjectTable(0u, &rot);
	if (FAILED(result) || (!rot))
	{
		LC_ERROR_DEV("Could not initialize running object table. Error: 0x%X", result);
		return nullptr;
	}

	TComPtr<IEnumMoniker> enumMoniker = nullptr;
	result = rot->EnumRunning(&enumMoniker);
	if (FAILED(result) || (!enumMoniker))
	{
		LC_ERROR_DEV("Could not enumerate running objects. Error: 0x%X", result);
		return nullptr;
	}

	enumMoniker->Reset();
	do
	{
		TComPtr<IMoniker> next = nullptr;
		result = enumMoniker->Next(1u, &next, NULL);

		if (SUCCEEDED(result) && next)
		{
			TComPtr<IBindCtx> context = nullptr;
			result = ::CreateBindCtx(0, &context);

			if (FAILED(result) || (!context))
			{
				LC_ERROR_DEV("Could not create COM binding context. Error: 0x%X", result);
				continue;
			}

			wchar_t* displayName = nullptr;
			result = next->GetDisplayName(context, NULL, &displayName);
			if (FAILED(result) || (!displayName))
			{
				LC_ERROR_DEV("Could not retrieve display name. Error: 0x%X", result);
				continue;
			}

			// only try objects which are a specific version of Visual Studio
			if (string::Contains(displayName, L"VisualStudio.DTE."))
			{
				// free display name using COM allocator
				TComPtr<IMalloc> comMalloc = nullptr;
				result = ::CoGetMalloc(1u, &comMalloc);
				if (SUCCEEDED(result) && comMalloc)
				{
					comMalloc->Free(displayName);
				}

				TComPtr<IUnknown> unknown = nullptr;
				result = rot->GetObject(next, &unknown);
				if (FAILED(result) || (!unknown))
				{
					LC_ERROR_DEV("Could not retrieve COM object from running object table. Error: 0x%X", result);
					continue;
				}

				EnvDTE::_DTEPtr dte = nullptr;
				result = unknown->QueryInterface(&dte);
				if (FAILED(result) || (!dte))
				{
					// this COM object doesn't support the DTE interface
					LC_ERROR_DEV("Could not convert IUnknown to DTE interface. Error: 0x%X", result);
					continue;
				}

				EnvDTE::DebuggerPtr debugger = nullptr;
				result = dte->get_Debugger(&debugger);
				if (FAILED(result) || (!debugger))
				{
					// cannot access debugger, which means that the process is currently not being debugged
					LC_LOG_DEV("Could not access debugger interface. Error: 0x%X", result);
					continue;
				}

				// fetch all processes to which this debugger is attached
				EnvDTE::ProcessesPtr allProcesses = nullptr;
				result = debugger->get_DebuggedProcesses(&allProcesses);
				if (FAILED(result) || (!allProcesses))
				{
					LC_ERROR_DEV("Could not retrieve processes from debugger. Error: 0x%X", result);
					continue;
				}

				long processCount = 0;
				result = allProcesses->get_Count(&processCount);
				if (FAILED(result) || (processCount <= 0))
				{
					LC_ERROR_DEV("Could not retrieve process count from debugger. Error: 0x%X", result);
					continue;
				}

				// check all processes if any of them is the one we're looking for
				for (long i = 0u; i < processCount; ++i)
				{
					EnvDTE::ProcessPtr singleProcess = nullptr;
					result = allProcesses->Item(variant_t(i + 1), &singleProcess);

					if (FAILED(result) || (!singleProcess))
					{
						LC_ERROR_DEV("Could not retrieve process from debugger. Error: 0x%X", result);
						continue;
					}

					long debuggerProcessId = 0;
					result = singleProcess->get_ProcessID(&debuggerProcessId);
					if (FAILED(result) || (debuggerProcessId <= 0))
					{
						LC_ERROR_DEV("Could not retrieve process ID from debugger. Error: 0x%X", result);
						continue;
					}

					// we got a valid processId
					if (static_cast<unsigned int>(debuggerProcessId) == processId)
					{
						// found debugger attached to our process
						return debugger;
					}
				}
			}
		}
	}
	while (result != S_FALSE);

	return nullptr;
}


EnvDTE::DebuggerPtr visualStudio::FindDebuggerAttachedToProcess(unsigned int processId)
{
	return g_comThread->CallInThread(&::FindDebuggerAttachedToProcess, processId);
}


static EnvDTE::DebuggerPtr FindDebuggerForProcess(unsigned int processId)
{
	// get all objects from the running object table (ROT)
	TComPtr<IRunningObjectTable> rot = nullptr;
	HRESULT result = ::GetRunningObjectTable(0u, &rot);
	if (FAILED(result) || (!rot))
	{
		LC_ERROR_DEV("Could not initialize running object table. Error: 0x%X", result);
		return nullptr;
	}

	TComPtr<IEnumMoniker> enumMoniker = nullptr;
	result = rot->EnumRunning(&enumMoniker);
	if (FAILED(result) || (!enumMoniker))
	{
		LC_ERROR_DEV("Could not enumerate running objects. Error: 0x%X", result);
		return nullptr;
	}

	enumMoniker->Reset();
	do
	{
		TComPtr<IMoniker> next = nullptr;
		result = enumMoniker->Next(1u, &next, NULL);

		if (SUCCEEDED(result) && next)
		{
			TComPtr<IBindCtx> context = nullptr;
			result = ::CreateBindCtx(0, &context);

			if (FAILED(result) || (!context))
			{
				LC_ERROR_DEV("Could not create COM binding context. Error: 0x%X", result);
				continue;
			}

			wchar_t* displayName = nullptr;
			result = next->GetDisplayName(context, NULL, &displayName);
			if (FAILED(result) || (!displayName))
			{
				LC_ERROR_DEV("Could not retrieve display name. Error: 0x%X", result);
				continue;
			}

			// only try objects which are a specific version of Visual Studio
			if (string::Contains(displayName, L"VisualStudio.DTE."))
			{
				// free display name using COM allocator
				TComPtr<IMalloc> comMalloc = nullptr;
				result = ::CoGetMalloc(1u, &comMalloc);
				if (SUCCEEDED(result) && comMalloc)
				{
					comMalloc->Free(displayName);
				}

				TComPtr<IUnknown> unknown = nullptr;
				result = rot->GetObject(next, &unknown);
				if (FAILED(result) || (!unknown))
				{
					LC_ERROR_DEV("Could not retrieve COM object from running object table. Error: 0x%X", result);
					continue;
				}

				EnvDTE::_DTEPtr dte = nullptr;
				result = unknown->QueryInterface(&dte);
				if (FAILED(result) || (!dte))
				{
					// this COM object doesn't support the DTE interface
					LC_ERROR_DEV("Could not convert IUnknown to DTE interface. Error: 0x%X", result);
					continue;
				}

				EnvDTE::DebuggerPtr debugger = nullptr;
				result = dte->get_Debugger(&debugger);
				if (FAILED(result) || (!debugger))
				{
					// cannot access debugger, which means that the process is currently not being debugged
					LC_LOG_DEV("Could not access debugger interface. Error: 0x%X", result);
					continue;
				}

				EnvDTE::ProcessPtr process = nullptr;
				result = debugger->get_CurrentProcess(&process);
				if (FAILED(result) || (!process))
				{
					// cannot access current process, reason unknown
					LC_ERROR_DEV("Could not access current process in debugger. Error: 0x%X", result);
					continue;
				}

				long debuggerProcessId = 0;
				result = process->get_ProcessID(&debuggerProcessId);
				if (FAILED(result) || (debuggerProcessId <= 0))
				{
					LC_ERROR_DEV("Could not retrieve process ID from debugger. Error: 0x%X", result);
					continue;
				}

				// we got a valid processId
				if (static_cast<unsigned int>(debuggerProcessId) == processId)
				{
					// found debugger debugging our process
					return debugger;
				}
			}
		}
	}
	while (result != S_FALSE);

	return nullptr;
}


EnvDTE::DebuggerPtr visualStudio::FindDebuggerForProcess(unsigned int processId)
{
	return g_comThread->CallInThread(&::FindDebuggerForProcess, processId);
}


static bool AttachToProcess(const EnvDTE::DebuggerPtr& debugger, unsigned int processId)
{
	// fetch all local processes running on this machine
	EnvDTE::ProcessesPtr allProcesses = nullptr;
	HRESULT result = debugger->get_LocalProcesses(&allProcesses);
	if (FAILED(result) || (!allProcesses))
	{
		LC_ERROR_DEV("Could not retrieve local processes from debugger. Error: 0x%X", result);
		return false;
	}

	long processCount = 0;
	result = allProcesses->get_Count(&processCount);
	if (FAILED(result) || (processCount <= 0))
	{
		LC_ERROR_DEV("Could not retrieve local process count from debugger. Error: 0x%X", result);
		return false;
	}

	// check all processes if any of them is the one we're looking for
	for (long i = 0u; i < processCount; ++i)
	{
		EnvDTE::ProcessPtr singleProcess = nullptr;
		result = allProcesses->Item(variant_t(i + 1), &singleProcess);

		if (FAILED(result) || (!singleProcess))
		{
			LC_ERROR_DEV("Could not retrieve local process from debugger. Error: 0x%X", result);
			continue;
		}

		long localProcessId = 0;
		result = singleProcess->get_ProcessID(&localProcessId);
		if (FAILED(result) || (localProcessId <= 0))
		{
			LC_ERROR_DEV("Could not retrieve local process ID from debugger. Error: 0x%X", result);
			continue;
		}

		// we got a valid processId
		if (static_cast<unsigned int>(localProcessId) == processId)
		{
			// this is the process we want to attach to
			result = singleProcess->Attach();
			if (FAILED(result))
			{
				LC_ERROR_USER("Could not attach debugger to process. Error: 0x%X", result);
				return false;
			}

			return true;
		}
	}

	return false;
}


bool visualStudio::AttachToProcess(const EnvDTE::DebuggerPtr& debugger, unsigned int processId)
{
	return g_comThread->CallInThread(&::AttachToProcess, debugger, processId);
}


static types::vector<EnvDTE::ThreadPtr> EnumerateThreads(const EnvDTE::DebuggerPtr& debugger)
{
	types::vector<EnvDTE::ThreadPtr> threads;

	EnvDTE::ProgramPtr program = nullptr;
	HRESULT result = debugger->get_CurrentProgram(&program);
	if (FAILED(result) || (!program))
	{
		LC_ERROR_DEV("Could not retrieve current program from debugger. Error: 0x%X", result);
		return threads;
	}

	EnvDTE::ThreadsPtr allThreads = nullptr;
	result = program->get_Threads(&allThreads);
	if (FAILED(result) || (!allThreads))
	{
		LC_ERROR_DEV("Could not retrieve running threads from debugger. Error: 0x%X", result);
		return threads;
	}

	long threadCount = 0;
	result = allThreads->get_Count(&threadCount);
	if (FAILED(result) || (threadCount <= 0))
	{
		LC_ERROR_DEV("Could not retrieve thread count from debugger. Error: 0x%X", result);
		return threads;
	}

	threads.reserve(static_cast<size_t>(threadCount));

	for (long i = 0u; i < threadCount; ++i)
	{
		EnvDTE::ThreadPtr singleThread = nullptr;
		result = allThreads->Item(variant_t(i + 1), &singleThread);
		if (FAILED(result) || (!singleThread))
		{
			LC_ERROR_DEV("Could not retrieve thread from debugger. Error: 0x%X", result);
			continue;
		}

		threads.push_back(singleThread);
	}

	return threads;
}


types::vector<EnvDTE::ThreadPtr> visualStudio::EnumerateThreads(const EnvDTE::DebuggerPtr& debugger)
{
	return g_comThread->CallInThread(&::EnumerateThreads, debugger);
}


static bool FreezeThreads(const EnvDTE::DebuggerPtr& debugger, const types::vector<EnvDTE::ThreadPtr>& threads)
{
	bool success = true;

	const size_t count = threads.size();
	for (size_t i = 0u; i < count; ++i)
	{
		EnvDTE::ThreadPtr singleThread = threads[i];
		const HRESULT result = singleThread->Freeze();
		WaitUntilBreakMode(debugger);

		success &= SUCCEEDED(result);
	}

	return success;
}


bool visualStudio::FreezeThreads(const EnvDTE::DebuggerPtr& debugger, const types::vector<EnvDTE::ThreadPtr>& threads)
{
	return g_comThread->CallInThread(&::FreezeThreads, debugger, threads);
}


static bool FreezeThread(const EnvDTE::DebuggerPtr& debugger, const types::vector<EnvDTE::ThreadPtr>& threads, unsigned int threadId)
{
	const size_t count = threads.size();
	for (size_t i = 0u; i < count; ++i)
	{
		EnvDTE::ThreadPtr singleThread = threads[i];

		long id = 0;
		HRESULT result = singleThread->get_ID(&id);
		if (FAILED(result) || (id <= 0))
		{
			continue;
		}

		if (static_cast<unsigned int>(id) == threadId)
		{
			// found the thread we're looking for
			result = singleThread->Freeze();
			WaitUntilBreakMode(debugger);

			return SUCCEEDED(result);
		}
	}

	return false;
}


bool visualStudio::FreezeThread(const EnvDTE::DebuggerPtr& debugger, const types::vector<EnvDTE::ThreadPtr>& threads, unsigned int threadId)
{
	return g_comThread->CallInThread(&::FreezeThread, debugger, threads, threadId);
}


static bool ThawThreads(const EnvDTE::DebuggerPtr& debugger, const types::vector<EnvDTE::ThreadPtr>& threads)
{
	bool success = true;

	const size_t count = threads.size();
	for (size_t i = 0u; i < count; ++i)
	{
		EnvDTE::ThreadPtr singleThread = threads[i];
		const HRESULT result = singleThread->Thaw();
		WaitUntilBreakMode(debugger);

		success &= SUCCEEDED(result);
	}

	return success;
}


bool visualStudio::ThawThreads(const EnvDTE::DebuggerPtr& debugger, const types::vector<EnvDTE::ThreadPtr>& threads)
{
	return g_comThread->CallInThread(&::ThawThreads, debugger, threads);
}


static bool ThawThread(const EnvDTE::DebuggerPtr& debugger, const types::vector<EnvDTE::ThreadPtr>& threads, unsigned int threadId)
{
	const size_t count = threads.size();
	for (size_t i = 0u; i < count; ++i)
	{
		EnvDTE::ThreadPtr singleThread = threads[i];

		long id = 0;
		HRESULT result = singleThread->get_ID(&id);
		if (FAILED(result) || (id <= 0))
		{
			continue;
		}

		if (static_cast<unsigned int>(id) == threadId)
		{
			// found the thread we're looking for
			result = singleThread->Thaw();
			WaitUntilBreakMode(debugger);

			return SUCCEEDED(result);
		}
	}

	return false;
}


bool visualStudio::ThawThread(const EnvDTE::DebuggerPtr& debugger, const types::vector<EnvDTE::ThreadPtr>& threads, unsigned int threadId)
{
	return g_comThread->CallInThread(&::ThawThread, debugger, threads, threadId);
}


static bool Resume(const EnvDTE::DebuggerPtr& debugger)
{
	const HRESULT result = debugger->Go(variant_t(Windows::FALSE));
	return SUCCEEDED(result);
}


bool visualStudio::Resume(const EnvDTE::DebuggerPtr& debugger)
{
	return g_comThread->CallInThread(&::Resume, debugger);
}


static bool Break(const EnvDTE::DebuggerPtr& debugger)
{
	// wait until the debugger really enters break-mode
	const HRESULT result = debugger->Break(variant_t(Windows::TRUE));
	return SUCCEEDED(result);
}


bool visualStudio::Break(const EnvDTE::DebuggerPtr& debugger)
{
	return g_comThread->CallInThread(&::Break, debugger);
}

#endif
