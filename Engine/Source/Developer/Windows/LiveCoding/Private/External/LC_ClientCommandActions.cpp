// Copyright 2011-2019 Molecular Matters GmbH, all rights reserved.

#include "LC_ClientCommandActions.h"
#include "LC_ClientUserCommandThread.h"
#include "LC_DuplexPipe.h"
#include "LC_SyncPoint.h"
#include "LC_Executable.h"
#include "LC_Event.h"
#include "LC_Process.h"
#include "LC_Logging.h"


bool actions::RegisterProcessFinished::Execute(const CommandType* command, const DuplexPipe* pipe, void* context, const void*, size_t)
{
	pipe->SendAck();

	bool* successfullyRegisteredProcess = static_cast<bool*>(context);
	*successfullyRegisteredProcess = command->success;

	// don't continue execution
	return false;
}


bool actions::EnableModulesFinished::Execute(const CommandType* command, const DuplexPipe* pipe, void*, const void*, size_t)
{
	Event* event = static_cast<Event*>(command->token);
	event->Signal();
	pipe->SendAck();

	return false;
}


bool actions::DisableModulesFinished::Execute(const CommandType* command, const DuplexPipe* pipe, void*, const void*, size_t)
{
	Event* event = static_cast<Event*>(command->token);
	event->Signal();
	pipe->SendAck();

	return false;
}


bool actions::EnterSyncPoint::Execute(const CommandType*, const DuplexPipe* pipe, void*, const void*, size_t)
{
	syncPoint::Enter();
	pipe->SendAck();

	return true;
}


bool actions::LeaveSyncPoint::Execute(const CommandType*, const DuplexPipe* pipe, void*, const void*, size_t)
{
	syncPoint::Leave();
	pipe->SendAck();

	return true;
}


bool actions::CallHooks::Execute(const CommandType* command, const DuplexPipe* pipe, void*, const void*, size_t)
{
	for (const hook::Function* hook = command->first; hook < command->last; ++hook)
	{
		// note that sections are often padded with zeroes, so skip everything that's zero
		hook::Function function = *hook;
		if (function)
		{
			function();
		}
	}

	pipe->SendAck();

	return true;
}


// BEGIN EPIC MOD - Support for UE4 debug visualizers
struct FNameEntry;
extern FNameEntry*** GFNameTableForDebuggerVisualizers_MT;

class FChunkedFixedUObjectArray;
extern FChunkedFixedUObjectArray*& GObjectArrayForDebugVisualizers;
// END EPIC MOD


bool actions::LoadPatch::Execute(const CommandType* command, const DuplexPipe* pipe, void*, const void*, size_t)
{
	// load library into this process
	HMODULE module = ::LoadLibraryW(command->path);

	// BEGIN EPIC MOD - Support for UE4 debug visualizers
	if (module != nullptr)
	{
		typedef void InitNatvisHelpersFunc(FNameEntry*** NameTable, FChunkedFixedUObjectArray* ObjectArray);

		InitNatvisHelpersFunc* InitNatvisHelpers = (InitNatvisHelpersFunc*)(void*)GetProcAddress(module, "InitNatvisHelpers");
		if (InitNatvisHelpers != nullptr)
		{
			(*InitNatvisHelpers)(GFNameTableForDebuggerVisualizers_MT, GObjectArrayForDebugVisualizers);
		}
	}
	// END EPIC MOD

	pipe->SendAck();

	// send back command with module info
	pipe->SendCommandAndWaitForAck(commands::LoadPatchInfo { module }, nullptr, 0u);

	return true;
}


bool actions::UnloadPatch::Execute(const CommandType* command, const DuplexPipe* pipe, void*, const void*, size_t)
{
	// unload library from this process
	::FreeLibrary(command->module);
	pipe->SendAck();

	return true;
}


bool actions::CallEntryPoint::Execute(const CommandType* command, const DuplexPipe* pipe, void*, const void*, size_t)
{
	executable::CallDllEntryPoint(command->moduleBase, command->entryPointRva);
	pipe->SendAck();

	return true;
}


bool actions::LogOutput::Execute(const CommandType*, const DuplexPipe* pipe, void*, const void* payload, size_t)
{
	logging::LogNoFormat<logging::Channel::USER>(static_cast<const wchar_t*>(payload));
	pipe->SendAck();

	return true;
}

// BEGIN EPIC MOD - Notification that compilation has finished
extern bool GIsCompileActive;
// END EPIC MOD

bool actions::CompilationFinished::Execute(const CommandType*, const DuplexPipe* pipe, void*, const void*, size_t)
{
	pipe->SendAck();

	// BEGIN EPIC MOD - Notification that compilation has finished
	GIsCompileActive = false;
	// END EPIC MOD

	// don't continue execution
	return false;
}


bool actions::HandleExceptionFinished::Execute(const CommandType* command, const DuplexPipe* pipe, void* context, const void*, size_t)
{
	ClientUserCommandThread::ExceptionResult* resultContext = static_cast<ClientUserCommandThread::ExceptionResult*>(context);
	resultContext->returnAddress = command->returnAddress;
	resultContext->framePointer = command->framePointer;
	resultContext->stackPointer = command->stackPointer;
	resultContext->continueExecution = command->continueExecution;

	pipe->SendAck();

	// don't continue execution
	return false;
}
