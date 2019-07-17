// Copyright 2011-2019 Molecular Matters GmbH, all rights reserved.

#pragma once

#include "LC_Types.h"
#include "VisualStudioDTE.h"

#if WITH_VISUALSTUDIO_DTE

namespace visualStudio
{
	void Startup(void);
	void Shutdown(void);


	// Finds a Visual Studio debugger instance currently attached to the process with the given ID
	EnvDTE::DebuggerPtr FindDebuggerAttachedToProcess(unsigned int processId);

	// Finds a Visual Studio debugger instance currently debugging the process with the given ID
	EnvDTE::DebuggerPtr FindDebuggerForProcess(unsigned int processId);

	// Attaches a Visual Studio debugger instance to the process with the given ID
	bool AttachToProcess(const EnvDTE::DebuggerPtr& debugger, unsigned int processId);

	// Enumerates all threads of a debugger instance
	types::vector<EnvDTE::ThreadPtr> EnumerateThreads(const EnvDTE::DebuggerPtr& debugger);

	// Freezes all given threads
	bool FreezeThreads(const EnvDTE::DebuggerPtr& debugger, const types::vector<EnvDTE::ThreadPtr>& threads);

	// Freezes a single thread with the given thread ID
	bool FreezeThread(const EnvDTE::DebuggerPtr& debugger, const types::vector<EnvDTE::ThreadPtr>& threads, unsigned int threadId);

	// Thaws all given threads
	bool ThawThreads(const EnvDTE::DebuggerPtr& debugger, const types::vector<EnvDTE::ThreadPtr>& threads);

	// Thaws a single thread with the given thread ID
	bool ThawThread(const EnvDTE::DebuggerPtr& debugger, const types::vector<EnvDTE::ThreadPtr>& threads, unsigned int threadId);

	// Resumes the process in the debugger
	bool Resume(const EnvDTE::DebuggerPtr& debugger);

	// Breaks the process in the debugger
	bool Break(const EnvDTE::DebuggerPtr& debugger);
}

#endif
