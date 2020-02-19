// Copyright 2011-2019 Molecular Matters GmbH, all rights reserved.

#include "LC_Process.h"
#include "LC_Memory.h"
#include "LC_PointerUtil.h"
#include "LC_VirtualMemory.h"
#include "LC_WindowsInternals.h"
#include "LC_WindowsInternalFunctions.h"
#include "LC_Logging.h"
#include <Psapi.h>
#include "Containers/Map.h"
#include "Containers/UnrealString.h"

// BEGIN EPIC MODS
#pragma warning(push)
#pragma warning(disable:6011) // warning C6011: Dereferencing NULL pointer 'processInfo'.
#pragma warning(disable:6335) // warning C6335: Leaking process information handle 'context->pi.hProcess'.
// END EPIC MODS


namespace process
{
	static unsigned int __stdcall DrainPipe(void* data)
	{
		Context* context = static_cast<Context*>(data);

		std::vector<char> stdoutData;
		for (;;)
		{
			DWORD bytesRead = 0u;
			char buffer[256] = {};
			if (!::ReadFile(context->pipeReadEnd, buffer, sizeof(buffer) - 1u, &bytesRead, NULL))
			{
				// error while trying to read from the pipe, process has probably ended and closed its end of the pipe
				const DWORD error = ::GetLastError();
				if (error == ERROR_BROKEN_PIPE)
				{
					// this is expected
					break;
				}

				LC_ERROR_USER("Error 0x%X while reading from pipe", error);
				break;
			}

			stdoutData.insert(stdoutData.end(), buffer, buffer + bytesRead);
		}

		// convert stdout data to UTF16
		if (stdoutData.size() > 0u)
		{
			// cl.exe and link.exe write to stdout using the OEM codepage
			const int sizeNeeded = ::MultiByteToWideChar(CP_OEMCP, 0, stdoutData.data(), static_cast<int>(stdoutData.size()), NULL, 0);

			wchar_t* strTo = new wchar_t[static_cast<size_t>(sizeNeeded)];
			::MultiByteToWideChar(CP_OEMCP, 0, stdoutData.data(), static_cast<int>(stdoutData.size()), strTo, sizeNeeded);

			context->stdoutData.assign(strTo, static_cast<size_t>(sizeNeeded));
			delete[] strTo;
		}

		return 0u;
	}


	unsigned int GetId(void)
	{
		return ::GetCurrentProcessId();
	}


	Context* Spawn(const wchar_t* exePath, const wchar_t* workingDirectory, const wchar_t* commandLine, const void* environmentBlock, uint32_t flags)
	{
		Context* context = new Context { flags, nullptr, PROCESS_INFORMATION {}, nullptr, {} };

		::SECURITY_ATTRIBUTES saAttr;
		saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
		saAttr.bInheritHandle = Windows::TRUE;
		saAttr.lpSecurityDescriptor = NULL;

		::STARTUPINFOW startupInfo = {};
		startupInfo.cb = sizeof(startupInfo);

		HANDLE hProcessStdOutRead = NULL;
		HANDLE hProcessStdOutWrite = NULL;
		HANDLE hProcessStdErrWrite = NULL;

		if (flags & SpawnFlags::REDIRECT_STDOUT)
		{
			// create a STD_OUT pipe for the child process
			if (!CreatePipe(&hProcessStdOutRead, &hProcessStdOutWrite, &saAttr, 0))
			{
				LC_ERROR_USER("Cannot create stdout pipe. Error: 0x%X", ::GetLastError());
				delete context;
				return nullptr;
			}

			// create a duplicate of the STD_OUT write handle for the STD_ERR write handle. this is necessary in case the child
			// application closes one of its STD output handles.
			if (!::DuplicateHandle(::GetCurrentProcess(), hProcessStdOutWrite, ::GetCurrentProcess(),
				&hProcessStdErrWrite, 0, Windows::TRUE, DUPLICATE_SAME_ACCESS))
			{
				LC_ERROR_USER("Cannot duplicate stdout pipe. Error: 0x%X", ::GetLastError());
				::CloseHandle(hProcessStdOutRead);
				::CloseHandle(hProcessStdOutWrite);
				delete context;
				return nullptr;
			}

			// the spawned process will output data into the write-end of the pipe, and our process will read from the
			// read-end. because pipes can only do some buffering, we need to ensure that pipes never get clogged, otherwise
			// the spawned process could block due to the pipe being full.
			// therefore, we also create a new thread that continuously reads data from the pipe on our end.
			context->pipeReadEnd = hProcessStdOutRead;
			context->threadId = thread::Create(64u * 1024u, &DrainPipe, context);

			startupInfo.hStdOutput = hProcessStdOutWrite;
			startupInfo.hStdError = hProcessStdErrWrite;
			startupInfo.dwFlags = STARTF_USESTDHANDLES;
		}

		wchar_t* commandLineBuffer = nullptr;
		if (commandLine)
		{
			commandLineBuffer = new wchar_t[32768];
			wcscpy_s(commandLineBuffer, 32768u, commandLine);
		}

		LC_LOG_DEV("Spawning process:");
		{
			LC_LOG_INDENT_DEV;
			LC_LOG_DEV("Executable: %S", exePath);
			LC_LOG_DEV("Command line: %S", commandLineBuffer ? commandLineBuffer : L"none");
			LC_LOG_DEV("Working directory: %S", workingDirectory ? workingDirectory : L"none");
			LC_LOG_DEV("Custom environment block: %S", environmentBlock ? L"yes" : L"no");
			LC_LOG_DEV("Flags: %u", flags);
		}

		DWORD creationFlags = CREATE_UNICODE_ENVIRONMENT;
		if (flags & SpawnFlags::NO_WINDOW)
		{
			creationFlags |= CREATE_NO_WINDOW;
		}
		if (flags & SpawnFlags::SUSPENDED)
		{
			creationFlags |= CREATE_SUSPENDED;
		}

		// the environment block is not written to by CreateProcess, so it is safe to const_cast (it's a Win32 API mistake)
		const BOOL success = ::CreateProcessW(exePath, commandLineBuffer, NULL, NULL, Windows::TRUE, creationFlags, const_cast<void*>(environmentBlock), workingDirectory, &startupInfo, &context->pi);
		if (success == 0)
		{
			LC_ERROR_USER("Could not spawn process %S. Error: %d", exePath, ::GetLastError());
		}

		delete[] commandLineBuffer;

		if (flags & SpawnFlags::REDIRECT_STDOUT)
		{
			// we don't need those ends of the pipe
			::CloseHandle(hProcessStdOutWrite);
			::CloseHandle(hProcessStdErrWrite);
		}

		return context;
	}


	void ResumeMainThread(Context* context)
	{
		::ResumeThread(context->pi.hThread);
	}


	unsigned int Wait(Context* context)
	{
		// wait until process terminates
		::WaitForSingleObject(context->pi.hProcess, INFINITE);

		if (context->flags & SpawnFlags::REDIRECT_STDOUT)
		{
			// wait until all data is drained from the pipe
			thread::Join(context->threadId);
			thread::Close(context->threadId);

			// close remaining pipe handles
			::CloseHandle(context->pipeReadEnd);
		}

		DWORD exitCode = 0xFFFFFFFFu;
		::GetExitCodeProcess(context->pi.hProcess, &exitCode);

		return exitCode;
	}


	unsigned int Wait(Handle handle)
	{
		// wait until process terminates
		::WaitForSingleObject(handle, INFINITE);

		DWORD exitCode = 0xFFFFFFFFu;
		::GetExitCodeProcess(handle, &exitCode);

		return exitCode;
	}


	void Destroy(Context*& context)
	{
		::CloseHandle(context->pi.hProcess);
		::CloseHandle(context->pi.hThread);

		memory::DeleteAndNull(context);
	}


	void Terminate(Handle processHandle)
	{
		::TerminateProcess(processHandle, 0u);

		// termination is asynchronous, wait until the process is really gone
		::WaitForSingleObject(processHandle, INFINITE);
	}


	Handle Open(unsigned int processId)
	{
		return ::OpenProcess(PROCESS_ALL_ACCESS, Windows::FALSE, processId);
	}


	void Close(Handle& handle)
	{
		::CloseHandle(handle);
		handle = INVALID_HANDLE_VALUE;
	}


	std::wstring GetImagePath(Handle handle)
	{
		DWORD charCount = MAX_PATH + 1u;
		wchar_t processName[MAX_PATH + 1u] = {};
		::QueryFullProcessImageName(handle, 0u, processName, &charCount);

		return std::wstring(processName);
	}


	std::wstring GetWorkingDirectory(void)
	{
		wchar_t workingDirectory[MAX_PATH + 1] = {};
		::GetCurrentDirectoryW(MAX_PATH + 1u, workingDirectory);

		return std::wstring(workingDirectory);
	}


	std::wstring GetCommandLine(void)
	{
		return std::wstring(::GetCommandLineW());
	}


	void* GetBase(void)
	{
		return ::GetModuleHandle(NULL);
	}


	std::wstring GetImagePath(void)
	{
		wchar_t filename[MAX_PATH + 1u] = {};
		::GetModuleFileNameW(NULL, filename, MAX_PATH + 1u);

		return std::wstring(filename);
	}


	uint32_t GetImageSize(Handle handle, void* moduleBase)
	{
		MODULEINFO info = {};
		::GetModuleInformation(handle, static_cast<HMODULE>(moduleBase), &info, sizeof(MODULEINFO));
		return info.SizeOfImage;
	}


	bool IsActive(Handle handle)
	{
		DWORD exitCode = 0u;
		const BOOL success = ::GetExitCodeProcess(handle, &exitCode);
		if ((success != 0) && (exitCode == STILL_ACTIVE))
		{
			return true;
		}

		// either the function has failed (because the process terminated unexpectedly) or the exit code
		// signals that the process exited already.
		return false;
	}

	
	void ReadProcessMemory(Handle handle, const void* srcAddress, void* destBuffer, size_t size)
	{
		const BOOL success = ::ReadProcessMemory(handle, srcAddress, destBuffer, size, NULL);
		if (success == 0)
		{
			LC_ERROR_USER("Cannot read %zu bytes from remote process at address 0x%p. Error: 0x%X", size, srcAddress, ::GetLastError());
		}
	}


	void WriteProcessMemory(Handle handle, void* destAddress, const void* srcBuffer, size_t size)
	{
		DWORD oldProtect = 0u;
		::VirtualProtectEx(handle, destAddress, size, PAGE_READWRITE, &oldProtect);
		{
			// instead of the regular WriteProcessMemory function, we use an undocumented function directly.
			// this is because Windows 10 introduced a performance regression that causes WriteProcessMemory to be 100 times slower (!)
			// than in previous versions of Windows.
			// this bug was reported here:
			// https://developercommunity.visualstudio.com/content/problem/228061/writeprocessmemory-slowdown-on-windows-10.html
			windowsInternal::NtWriteVirtualMemory(handle, destAddress, const_cast<PVOID>(srcBuffer), static_cast<ULONG>(size), NULL);
		}
		::VirtualProtectEx(handle, destAddress, size, oldProtect, &oldProtect);
	}


	void* ScanMemoryRange(Handle handle, const void* lowerBound, const void* upperBound, size_t size, size_t alignment)
	{
		for (const void* scan = lowerBound; /* nothing */; /* nothing */)
		{
			// align address to be scanned
			scan = pointer::AlignTop<const void*>(scan, alignment);
			if (pointer::Offset<const void*>(scan, size) >= upperBound)
			{
				// outside of range to scan
				LC_ERROR_DEV("Could not find memory range that fits 0x%X bytes with alignment 0x%X in range from 0x%p to 0x%p (scan: 0x%p)", size, alignment, lowerBound, upperBound, scan);
				return nullptr;
			}
			else if (scan < lowerBound)
			{
				// outside of range (possible wrap-around)
				LC_ERROR_DEV("Could not find memory range that fits 0x%X bytes with alignment 0x%X in range from 0x%p to 0x%p (scan: 0x%p)", size, alignment, lowerBound, upperBound, scan);
				return nullptr;
			}

			::MEMORY_BASIC_INFORMATION memoryInfo = {};
			::VirtualQueryEx(handle, scan, &memoryInfo, sizeof(::MEMORY_BASIC_INFORMATION));

			if ((memoryInfo.RegionSize >= size) && (memoryInfo.State == MEM_FREE))
			{
				return memoryInfo.BaseAddress;
			}

			// keep on searching
			scan = pointer::Offset<const void*>(memoryInfo.BaseAddress, memoryInfo.RegionSize);
		}
	}


	void MakePagesExecutable(Handle handle, void* address, size_t size)
	{
		const uint32_t pageSize = virtualMemory::GetPageSize();
		const void* endOfRegion = pointer::Offset<const void*>(address, size);

		for (const void* scan = address; /* nothing */; /* nothing */)
		{
			::MEMORY_BASIC_INFORMATION memoryInfo = {};
			const SIZE_T bytesInBuffer = ::VirtualQueryEx(handle, scan, &memoryInfo, sizeof(::MEMORY_BASIC_INFORMATION));
			if (bytesInBuffer == 0u)
			{
				// could not query the protection, bail out
				break;
			}

			const uint32_t executableProtection = ConvertPageProtectionToExecutableProtection(memoryInfo.Protect);
			if (executableProtection != memoryInfo.Protect)
			{
				// change this page into an executable one
				DWORD oldProtection = 0u;
				::VirtualProtectEx(handle, memoryInfo.BaseAddress, pageSize, executableProtection, &oldProtection);
			}

			const void* endOfThisRegion = pointer::Offset<const void*>(memoryInfo.BaseAddress, pageSize);
			if (endOfThisRegion >= endOfRegion)
			{
				// we are done
				break;
			}

			// keep on walking pages
			scan = endOfThisRegion;
		}
	}


	void FlushInstructionCache(Handle handle, void* address, size_t size)
	{
		::FlushInstructionCache(handle, address, size);
	}


	void Suspend(Handle handle)
	{
		windowsInternal::NtSuspendProcess(handle);
	}


	void Resume(Handle handle)
	{
		windowsInternal::NtResumeProcess(handle);
	}


	void Continue(CONTEXT* threadContext)
	{
		windowsInternal::NtContinue(threadContext, Windows::FALSE);
	}


	std::vector<unsigned int> EnumerateThreads(unsigned int processId)
	{
		std::vector<unsigned int> threadIds;
		threadIds.reserve(256u);

		// 2MB should be enough for getting the process information, even on systems with high load
		ULONG bufferSize = 2048u * 1024u;
		void* processSnapshot = nullptr;
		windowsInternal::NTSTATUS status = 0;

		do
		{
			processSnapshot = ::malloc(bufferSize);

			// try getting a process snapshot into the provided buffer
			status = windowsInternal::NtQuerySystemInformation(windowsInternal::SystemProcessInformation, processSnapshot, bufferSize, NULL);

			if (status == STATUS_INFO_LENGTH_MISMATCH)
			{
				// buffer is too small, try again
				::free(processSnapshot);
				processSnapshot = nullptr;

				bufferSize *= 2u;
			}
			else if (status < 0)
			{
				// something went wrong
				LC_ERROR_USER("Cannot enumerate threads in process (PID: %d)", processId);
				::free(processSnapshot);

				return threadIds;
			}
		}
		while (status == STATUS_INFO_LENGTH_MISMATCH);

		// find the process information for the given process ID
		{
			windowsInternal::NT_SYSTEM_PROCESS_INFORMATION* processInfo = static_cast<windowsInternal::NT_SYSTEM_PROCESS_INFORMATION*>(processSnapshot);

			while (processInfo != nullptr)
			{
				if (processInfo->UniqueProcessId == reinterpret_cast<HANDLE>(static_cast<DWORD_PTR>(processId)))
				{
					// we found the process we're looking for
					break;
				}

				if (processInfo->NextEntryOffset == 0u)
				{
					// we couldn't find our process
					LC_ERROR_USER("Cannot enumerate threads, process not found (PID: %d)", processId);
					::free(processSnapshot);

					return threadIds;
				}
				else
				{
					// walk to the next process info
					processInfo = pointer::Offset<windowsInternal::NT_SYSTEM_PROCESS_INFORMATION*>(processInfo, processInfo->NextEntryOffset);
				}
			}

			// record all threads belonging to the given process
			if (processInfo)
			{
				for (ULONG i = 0u; i < processInfo->NumberOfThreads; ++i)
				{
					const DWORD threadId = static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(processInfo->Threads[i].ClientId.UniqueThread));
					threadIds.push_back(threadId);
				}
			}
		}

		::free(processSnapshot);

		return threadIds;
	}


	std::vector<Module> EnumerateModules(Handle handle)
	{
		// 1024 modules should be enough for most processes
		std::vector<Module> modules;
		modules.reserve(1024u);

		windowsInternal::NT_PROCESS_BASIC_INFORMATION pbi = {};
		windowsInternal::NtQueryInformationProcess(handle, windowsInternal::ProcessBasicInformation, &pbi, sizeof(pbi), NULL);

		const windowsInternal::NT_PEB processPEB = ReadProcessMemory<windowsInternal::NT_PEB>(handle, pbi.PebBaseAddress);
		const windowsInternal::NT_PEB_LDR_DATA loaderData = ReadProcessMemory<windowsInternal::NT_PEB_LDR_DATA>(handle, processPEB.Ldr);

		LIST_ENTRY* listHeader = loaderData.InLoadOrderModuleList.Flink;
		LIST_ENTRY* currentNode = listHeader;
		do
		{
			const windowsInternal::NT_LDR_DATA_TABLE_ENTRY entry = ReadProcessMemory<windowsInternal::NT_LDR_DATA_TABLE_ENTRY>(handle, currentNode);

			wchar_t fullDllName[MAX_PATH] = {};

			// certain modules don't have a name and DLL base, skip those
			if ((entry.DllBase != nullptr) && (entry.FullDllName.Length > 0) && (entry.FullDllName.Buffer != nullptr))
			{
				ReadProcessMemory(handle, entry.FullDllName.Buffer, fullDllName, entry.FullDllName.Length);
				modules.emplace_back(Module { fullDllName, entry.DllBase, entry.SizeOfImage });
			}

			currentNode = entry.InLoadOrderLinks.Flink;
			if (currentNode == nullptr)
			{
				break;
			}
		}
		while (listHeader != currentNode);

		return modules;
	}


	uint32_t ConvertPageProtectionToExecutableProtection(uint32_t protection)
	{
		// cut off PAGE_GUARD, PAGE_NOCACHE, PAGE_WRITECOMBINE, and PAGE_REVERT_TO_FILE_MAP
		const uint32_t extraBits = protection & 0xFFFFFF00u;
		const uint32_t pageProtection = protection & 0x000000FFu;

		switch (pageProtection)
		{
			case PAGE_NOACCESS:
			case PAGE_READONLY:
			case PAGE_READWRITE:
			case PAGE_WRITECOPY:
				return (pageProtection << 4u) | extraBits;

			case PAGE_EXECUTE:
			case PAGE_EXECUTE_READ:
			case PAGE_EXECUTE_READWRITE:
			case PAGE_EXECUTE_WRITECOPY:
			default:
				return protection;
		}
	}


	bool IsWoW64(Handle handle)
	{
		// a WoW64 process has a PEB32 instead of a real PEB.
		// if we get a meaningful pointer to this PEB32, the process is running under WoW64.
		ULONG_PTR peb32 = 0u;
		windowsInternal::NtQueryInformationProcess(handle, windowsInternal::ProcessWow64Information, &peb32, sizeof(ULONG_PTR), NULL);

		return (peb32 != 0u);
	}


	Environment* CreateEnvironment(Handle handle)
	{
		const void* processEnvironment = nullptr;

		const bool isWow64 = IsWoW64(handle);
		if (!isWow64)
		{
			// this is either a 32-bit process running on 32-bit Windows, or a 64-bit process running on 64-bit Windows.
			// the environment can be retrieved directly from the process' PEB and process parameters.
			windowsInternal::NT_PROCESS_BASIC_INFORMATION pbi = {};
			windowsInternal::NtQueryInformationProcess(handle, windowsInternal::ProcessBasicInformation, &pbi, sizeof(pbi), NULL);

			const windowsInternal::NT_PEB peb = ReadProcessMemory<windowsInternal::NT_PEB>(handle, pbi.PebBaseAddress);
			const windowsInternal::RTL_USER_PROCESS_PARAMETERS parameters = ReadProcessMemory<windowsInternal::RTL_USER_PROCESS_PARAMETERS>(handle, peb.ProcessParameters);

			processEnvironment = parameters.Environment;
		}
		else
		{
			// this is a process running under WoW64.
			// we must get the environment from the PEB32 of the process, rather than the "real" PEB.
			ULONG_PTR peb32Wow64 = 0u;
			windowsInternal::NtQueryInformationProcess(handle, windowsInternal::ProcessWow64Information, &peb32Wow64, sizeof(ULONG_PTR), NULL);

			const windowsInternal::NT_PEB32 peb32 = ReadProcessMemory<windowsInternal::NT_PEB32>(handle, pointer::FromInteger<const void*>(peb32Wow64));
			const windowsInternal::RTL_USER_PROCESS_PARAMETERS32 parameters32 = ReadProcessMemory<windowsInternal::RTL_USER_PROCESS_PARAMETERS32>(handle, pointer::FromInteger<const void*>(peb32.ProcessParameters32));

			processEnvironment = pointer::FromInteger<const void*>(parameters32.Environment);		
		}

		if (!processEnvironment)
		{
			return nullptr;
		}

		// query the size of the page(s) the environment is stored in
		::MEMORY_BASIC_INFORMATION memoryInfo = {};
		const SIZE_T bytesInBuffer = ::VirtualQueryEx(handle, processEnvironment, &memoryInfo, sizeof(::MEMORY_BASIC_INFORMATION));
		if (bytesInBuffer == 0u)
		{
			// operation failed, bail out
			return nullptr;
		}

		Environment* environment = new Environment;
		environment->size = memoryInfo.RegionSize - (reinterpret_cast<uintptr_t>(processEnvironment) - reinterpret_cast<uintptr_t>(memoryInfo.BaseAddress));
		environment->data = ::malloc(environment->size);

		ReadProcessMemory(handle, processEnvironment, environment->data, environment->size);

		return environment;
	}


	// BEGIN EPIC MOD - Allow passing environment block for linker
	Environment* CreateEnvironmentFromMap(const TMap<FString, FString>& Pairs)
	{
		std::vector<wchar_t> environmentData;
		for (const TPair<FString, FString>& Pair : Pairs)
		{
			FString Variable = FString::Printf(TEXT("%s=%s"), *Pair.Key, *Pair.Value);
			environmentData.insert(environmentData.end(), *Variable, *Variable + (Variable.Len() + 1));
		}
		environmentData.push_back('\0');

		Environment* environment = new Environment;
		environment->size = environmentData.size();
		environment->data = ::malloc(environmentData.size() * sizeof(wchar_t));
		if (environment->data != nullptr)
		{
			memcpy(environment->data, environmentData.data(), environmentData.size() * sizeof(wchar_t));
		}
		return environment;
	}
	// END EPIC MOD


	void DestroyEnvironment(Environment*& environment)
	{
		if (environment)
		{
			::free(environment->data);
		}

		memory::DeleteAndNull(environment);
	}


	void DumpMemory(Handle handle, const void* address, size_t size)
	{
		uint8_t* memory = new uint8_t[size];
		ReadProcessMemory(handle, address, memory, size);

		LC_LOG_DEV("Raw data:");
		LC_LOG_INDENT_DEV;
		for (size_t i = 0u; i < size; ++i)
		{
			LC_LOG_DEV("0x%02X", memory[i]);
		}

		delete[] memory;
	}
}

// BEGIN EPIC MODS
#pragma warning(pop)
// END EPIC MODS
