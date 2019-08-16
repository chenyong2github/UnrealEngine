// Copyright 2011-2019 Molecular Matters GmbH, all rights reserved.

#pragma once

#include "LC_WindowsInternals.h"
#include "LC_Logging.h"


namespace windowsInternal
{
	// helper class that allows us to call any function in any Windows DLL, as long as it is exported and we know its signature.
	// base template.
	template <typename T>
	class Function {};

	// partial specialization for matching any function signature.
	template <typename R, typename... Args>
	class Function<R (Args...)>
	{
		typedef R (NTAPI *PtrToFunction)(Args...);

	public:
		inline Function(const char* moduleName, const char* functionName);

		inline R operator()(Args... args) const;

	private:
		// helper for letting us check the result for arbitrary return types.
		// base template.
		template <typename T>
		inline void CheckResult(T) const {}

		// explicit specialization for NTSTATUS return values
		template <>
		inline void CheckResult(NTSTATUS result) const
		{
			if (!NT_SUCCESS(result))
			{
				LC_ERROR_USER("Call to function %s in module %s failed. Error: 0x%X", m_functionName, m_moduleName, result);
			}
		}

		const char* m_moduleName;
		const char* m_functionName;
		PtrToFunction m_function;
	};
}


template <typename R, typename... Args>
inline windowsInternal::Function<R (Args...)>::Function(const char* moduleName, const char* functionName)
	: m_moduleName(moduleName)
	, m_functionName(functionName)
	, m_function(nullptr)
{
	HMODULE module = ::GetModuleHandleA(moduleName);
	if (!module)
	{
		LC_ERROR_USER("Cannot get handle for module %s", moduleName);
		return;
	}

	m_function = reinterpret_cast<PtrToFunction>(reinterpret_cast<uintptr_t>(::GetProcAddress(module, functionName)));
	if (!m_function)
	{
		LC_ERROR_USER("Cannot get address of function %s in module %s", functionName, moduleName);
	}
}


template <typename R, typename... Args>
inline R windowsInternal::Function<R (Args...)>::operator()(Args... args) const
{
	const R result = m_function(args...);
	CheckResult(result);

	return result;
}


// these are undocumented functions found in ntdll.dll.
// we don't call them directly, but use them for "extracting" their signature.
extern "C" windowsInternal::NTSTATUS NtSuspendProcess(HANDLE ProcessHandle);
extern "C" windowsInternal::NTSTATUS NtResumeProcess(HANDLE ProcessHandle);
extern "C" windowsInternal::NTSTATUS NtWriteVirtualMemory(HANDLE ProcessHandle, PVOID BaseAddress, PVOID Buffer, ULONG NumberOfBytesToWrite, PULONG NumberOfBytesWritten);
extern "C" windowsInternal::NTSTATUS NtQuerySystemInformation(windowsInternal::NT_SYSTEM_INFORMATION_CLASS SystemInformationClass, PVOID SystemInformation, ULONG SystemInformationLength, PULONG ReturnLength);
extern "C" windowsInternal::NTSTATUS NtQueryInformationProcess(HANDLE ProcessHandle, windowsInternal::NT_PROCESS_INFORMATION_CLASS ProcessInformationClass, PVOID ProcessInformation, ULONG ProcessInformationLength, PULONG ReturnLength);
extern "C" windowsInternal::NTSTATUS NtContinue(CONTEXT* ThreadContext, BOOLEAN RaiseAlert);


// cache important undocumented functions
namespace windowsInternal
{
	extern Function<decltype(NtSuspendProcess)> NtSuspendProcess;
	extern Function<decltype(NtResumeProcess)> NtResumeProcess;
	extern Function<decltype(NtWriteVirtualMemory)> NtWriteVirtualMemory;
	extern Function<decltype(NtQuerySystemInformation)> NtQuerySystemInformation;
	extern Function<decltype(NtQueryInformationProcess)> NtQueryInformationProcess;
	extern Function<decltype(NtContinue)> NtContinue;
}
