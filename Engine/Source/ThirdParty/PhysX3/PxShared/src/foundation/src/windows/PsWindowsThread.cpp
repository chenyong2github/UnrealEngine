// This code contains NVIDIA Confidential Information and is disclosed to you
// under a form of NVIDIA software license agreement provided separately to you.
//
// Notice
// NVIDIA Corporation and its licensors retain all intellectual property and
// proprietary rights in and to this software and related documentation and
// any modifications thereto. Any use, reproduction, disclosure, or
// distribution of this software and related documentation without an express
// license agreement from NVIDIA Corporation is strictly prohibited.
//
// ALL NVIDIA DESIGN SPECIFICATIONS, CODE ARE PROVIDED "AS IS.". NVIDIA MAKES
// NO WARRANTIES, EXPRESSED, IMPLIED, STATUTORY, OR OTHERWISE WITH RESPECT TO
// THE MATERIALS, AND EXPRESSLY DISCLAIMS ALL IMPLIED WARRANTIES OF NONINFRINGEMENT,
// MERCHANTABILITY, AND FITNESS FOR A PARTICULAR PURPOSE.
//
// Information and code furnished is believed to be accurate and reliable.
// However, NVIDIA Corporation assumes no responsibility for the consequences of use of such
// information or for any infringement of patents or other rights of third parties that may
// result from its use. No license is granted by implication or otherwise under any patent
// or patent rights of NVIDIA Corporation. Details are subject to change without notice.
// This code supersedes and replaces all information previously supplied.
// NVIDIA Corporation products are not authorized for use as critical
// components in life support devices or systems without express written approval of
// NVIDIA Corporation.
//
// Copyright (c) 2008-2017 NVIDIA Corporation. All rights reserved.
// Copyright (c) 2004-2008 AGEIA Technologies, Inc. All rights reserved.
// Copyright (c) 2001-2004 NovodeX AG. All rights reserved.

#include "windows/PsWindowsInclude.h"
#include "PsFoundation.h"
#include "PsThread.h"
#include "foundation/PxErrorCallback.h"
#include "foundation/PxAssert.h"
// @ATG_CHANGE : BEGIN HoloLens support
#if PX_HOLOLENS
#include <thread>
#endif
// @ATG_CHANGE : END HoloLens support


// an exception for setting the thread name in Microsoft debuggers
#define NS_MS_VC_EXCEPTION 0x406D1388

namespace physx
{
namespace shdfnd
{

namespace
{

#if PX_VC
#pragma warning(disable : 4061) // enumerator 'identifier' in switch of enum 'enumeration' is not handled
#pragma warning(disable : 4191) //'operator/operation' : unsafe conversion from 'type of expression' to 'type required'
#endif

// struct for naming a thread in the debugger
#pragma pack(push, 8)

typedef struct tagTHREADNAME_INFO
{
	DWORD dwType;     // Must be 0x1000.
	LPCSTR szName;    // Pointer to name (in user addr space).
	DWORD dwThreadID; // Thread ID (-1=caller thread).
	DWORD dwFlags;    // Reserved for future use, must be zero.
} THREADNAME_INFO;

#pragma pack(pop)

class _ThreadImpl
{
  public:
	enum State
	{
		NotStarted,
		Started,
		Stopped
	};

	HANDLE thread;
	LONG quitNow; // Should be 32bit aligned on SMP systems.
	State state;
	DWORD threadID;

	ThreadImpl::ExecuteFn fn;
	void* arg;

	uint32_t affinityMask;
};

_ThreadImpl* getThread(ThreadImpl* impl)
{
	return reinterpret_cast<_ThreadImpl*>(impl);
}

DWORD WINAPI PxThreadStart(LPVOID arg)
{
	_ThreadImpl* impl = getThread((ThreadImpl*)arg);

	// run either the passed in function or execute from the derived class (Runnable).
	if(impl->fn)
		(*impl->fn)(impl->arg);
	else if(impl->arg)
		((Runnable*)impl->arg)->execute();
	return 0;
}

// cache physical thread count
uint32_t gPhysicalCoreCount = 0;
}

static const uint32_t gSize = sizeof(_ThreadImpl);
const uint32_t& ThreadImpl::getSize()
{
	return gSize;
}

ThreadImpl::Id ThreadImpl::getId()
{
	return static_cast<Id>(GetCurrentThreadId());
}

// fwd GetLogicalProcessorInformation()
typedef BOOL(WINAPI* LPFN_GLPI)(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION, PDWORD);

uint32_t ThreadImpl::getNbPhysicalCores()
{
	if(!gPhysicalCoreCount)
	{
		// @ATG_CHANGE : BEGIN HoloLens support
#if PX_HOLOLENS
		DWORD processorCoreCount = std::thread::hardware_concurrency();
#else
		// @ATG_CHANGE : END
		// modified example code from: http://msdn.microsoft.com/en-us/library/ms683194
		LPFN_GLPI glpi;
		PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = NULL;
		PSYSTEM_LOGICAL_PROCESSOR_INFORMATION ptr = NULL;
		DWORD returnLength = 0;
		DWORD processorCoreCount = 0;
		DWORD byteOffset = 0;

		glpi = (LPFN_GLPI)GetProcAddress(GetModuleHandle(TEXT("kernel32")), "GetLogicalProcessorInformation");

		if(NULL == glpi)
		{
			// GetLogicalProcessorInformation not supported on OS < XP Service Pack 3
			return 0;
		}

		DWORD rc = (DWORD)glpi(NULL, &returnLength);
		PX_ASSERT(rc == FALSE);
		PX_UNUSED(rc);

		// first query reports required buffer space
		if(GetLastError() == ERROR_INSUFFICIENT_BUFFER)
		{
			buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)PxAlloca(returnLength);
		}
		else
		{
			physx::shdfnd::getFoundation().error(PxErrorCode::eINTERNAL_ERROR, __FILE__, __LINE__,
			                                     "Error querying buffer size for number of physical processors");
			return 0;
		}

		// retrieve data
		rc = (DWORD)glpi(buffer, &returnLength);
		if(rc != TRUE)
		{
			physx::shdfnd::getFoundation().error(PxErrorCode::eINTERNAL_ERROR, __FILE__, __LINE__,
			                                     "Error querying number of physical processors");
			return 0;
		}

		ptr = buffer;

		while(byteOffset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <= returnLength)
		{
			switch(ptr->Relationship)
			{
			case RelationProcessorCore:
				processorCoreCount++;
				break;
			default:
				break;
			}

			byteOffset += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
			ptr++;
		}

	// @ATG_CHANGE : BEGIN HoloLens support
#endif
	// @ATG_CHANGE : END
		gPhysicalCoreCount = processorCoreCount;
	}

	return gPhysicalCoreCount;
}

ThreadImpl::ThreadImpl()
{
	getThread(this)->thread = NULL;
	getThread(this)->state = _ThreadImpl::NotStarted;
	getThread(this)->quitNow = 0;
	getThread(this)->fn = NULL;
	getThread(this)->arg = NULL;
	getThread(this)->affinityMask = 0;
}

ThreadImpl::ThreadImpl(ExecuteFn fn, void* arg)
{
	getThread(this)->thread = NULL;
	getThread(this)->state = _ThreadImpl::NotStarted;
	getThread(this)->quitNow = 0;
	getThread(this)->fn = fn;
	getThread(this)->arg = arg;
	getThread(this)->affinityMask = 0;

	start(0, NULL);
}

ThreadImpl::~ThreadImpl()
{
	if(getThread(this)->state == _ThreadImpl::Started)
		kill();
	CloseHandle(getThread(this)->thread);
}

void ThreadImpl::start(uint32_t stackSize, Runnable* runnable)
{
	if(getThread(this)->state != _ThreadImpl::NotStarted)
		return;
	getThread(this)->state = _ThreadImpl::Started;

	if(runnable && !getThread(this)->arg && !getThread(this)->fn)
		getThread(this)->arg = runnable;

	getThread(this)->thread =
	    CreateThread(NULL, stackSize, PxThreadStart, (LPVOID) this, CREATE_SUSPENDED, &getThread(this)->threadID);
	if(!getThread(this)->thread)
	{
		physx::shdfnd::getFoundation().error(PxErrorCode::eINTERNAL_ERROR, __FILE__, __LINE__,
			                                    "PsWindowsThread::start: Failed to create thread.");
		getThread(this)->state = _ThreadImpl::NotStarted;
		return;
	}

	// set affinity and resume
	if(getThread(this)->affinityMask)
		setAffinityMask(getThread(this)->affinityMask);

	DWORD rc = ResumeThread(getThread(this)->thread);
	if(rc == DWORD(-1))
	{
		physx::shdfnd::getFoundation().error(PxErrorCode::eINTERNAL_ERROR, __FILE__, __LINE__,
			                                    "PsWindowsThread::start: Failed to resume thread.");
		getThread(this)->state = _ThreadImpl::NotStarted;
		return;
	}	
}

void ThreadImpl::signalQuit()
{
	InterlockedIncrement(&(getThread(this)->quitNow));
}

bool ThreadImpl::waitForQuit()
{
	if(getThread(this)->state == _ThreadImpl::NotStarted)
		return false;

	WaitForSingleObject(getThread(this)->thread, INFINITE);
	return true;
}

bool ThreadImpl::quitIsSignalled()
{
	return InterlockedCompareExchange(&(getThread(this)->quitNow), 0, 0) != 0;
}

void ThreadImpl::quit()
{
	getThread(this)->state = _ThreadImpl::Stopped;
	ExitThread(0);
}

void ThreadImpl::kill()
{
	// @ATG_CHANGE : BEGIN HoloLens support - no equivalent is available
#if !PX_HOLOLENS
	if(getThread(this)->state == _ThreadImpl::Started)
		TerminateThread(getThread(this)->thread, 0);
#endif
	// @ATG_CHANGE : END
	getThread(this)->state = _ThreadImpl::Stopped;
}

void ThreadImpl::sleep(uint32_t ms)
{
	Sleep(ms);
}

void ThreadImpl::yield()
{
	SwitchToThread();
}

// @ATG_CHANGE : BEGIN thread affinity addition
// This is a drop in equivelent to the thread affinity APIs that most legacy code is based on.
// As with those older APIs, it's functionality may be unexpected on machines with more than 64 cores.
#if PX_HOLOLENS && !defined(SetThreadAffinityMask)
DWORD_PTR WINAPI SetThreadAffinityMask(
	_In_ HANDLE    hThread,
	_In_ DWORD_PTR dwThreadAffinityMask
)
{
	static struct CPU_INFO {
		int Count;
		ULONG CpuInfoBytes;
		uint8_t* CpuInfoBuffer;
		PSYSTEM_CPU_SET_INFORMATION* CpuInfoPtrs;

		CPU_INFO() {
			Count = 0;
			CpuInfoBytes = 0;
			GetSystemCpuSetInformation(nullptr, 0, &CpuInfoBytes, GetCurrentProcess(), 0);

			CpuInfoBuffer = reinterpret_cast<uint8_t*>(malloc(CpuInfoBytes));
			if (CpuInfoBuffer)
			{
				GetSystemCpuSetInformation(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(CpuInfoBuffer),
					CpuInfoBytes, &CpuInfoBytes, GetCurrentProcess(), 0);

				BYTE* firstCpu = CpuInfoBuffer;
				for (BYTE* currentCpu = firstCpu; currentCpu < firstCpu + CpuInfoBytes; currentCpu += reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(currentCpu)->Size)
				{
					Count++;
				}

				CpuInfoPtrs = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION*>(calloc(Count, sizeof(PSYSTEM_CPU_SET_INFORMATION)));

				if (CpuInfoPtrs)
				{
					firstCpu = CpuInfoBuffer;
					int currentCpuNum = 0;
					for (BYTE* currentCpu = firstCpu; currentCpu < firstCpu + CpuInfoBytes; currentCpu += reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(currentCpu)->Size)
					{
						CpuInfoPtrs[currentCpuNum] = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(currentCpu);
						currentCpuNum++;
					}
				}
			}
		}
	} Cpu_Info;

	// if static initialization got Cpu info....
	if (Cpu_Info.Count > 0 && Cpu_Info.CpuInfoBytes && Cpu_Info.CpuInfoBuffer && Cpu_Info.CpuInfoPtrs)
	{
#ifdef _WIN64
		DWORD_PTR priorMask = 0xffffffffffffffffull;
		ULONG coreIds[64];
#else
		DWORD_PTR priorMask = 0xfffffffful;
		ULONG coreIds[32];
#endif
		ULONG priorCoreCount = 0;

		// this is simplified, assuming that not other setter will be setting affinity
		if (GetThreadSelectedCpuSets(hThread, coreIds, ARRAYSIZE(coreIds), &priorCoreCount))
		{
			if (priorCoreCount > 0 && priorCoreCount <= ARRAYSIZE(coreIds))
			{
				priorMask = 0;
				for (unsigned int currentCoreEntry = 0; currentCoreEntry < priorCoreCount; currentCoreEntry++)
				{
					for (int i = 0; i < Cpu_Info.Count; i++)
					{
						if (coreIds[currentCoreEntry] == Cpu_Info.CpuInfoPtrs[i]->CpuSet.Id)
						{
							priorMask |= (1ull << i);
							break;
						}
					}
				}
			}
		}

		int markedCount = 0;
		for (int coreNum = 0; coreNum < ARRAYSIZE(coreIds) && coreNum < Cpu_Info.Count; coreNum++)
		{
			if (dwThreadAffinityMask & (1ull << coreNum))
			{
				coreIds[markedCount] = Cpu_Info.CpuInfoPtrs[coreNum]->CpuSet.Id;
				markedCount++;
			}
		}

		if (SetThreadSelectedCpuSets(hThread, coreIds, markedCount))
		{
			return priorMask;
		}
		else
		{
			SetLastError(ERROR_BAD_ARGUMENTS);
			return 0;
		}
	}
	else
	{
		SetLastError(ERROR_DEVICE_ENUMERATION_ERROR);
		return 0;
	}
}
#endif
// @ATG_CHANGE : END thread affinity addition

uint32_t ThreadImpl::setAffinityMask(uint32_t mask)
{
	if(mask)
	{
		// store affinity
		getThread(this)->affinityMask = mask;

		// if thread already started apply immediately
		if(getThread(this)->state == _ThreadImpl::Started)
		{
			uint32_t err = uint32_t(SetThreadAffinityMask(getThread(this)->thread, mask));
			return err;
		}
	}

	return 0;
}

void ThreadImpl::setName(const char* name)
{
	THREADNAME_INFO info;
	info.dwType = 0x1000;
	info.szName = name;
	info.dwThreadID = getThread(this)->threadID;
	info.dwFlags = 0;

	// C++ Exceptions are disabled for this project, but SEH is not (and cannot be)
	// http://stackoverflow.com/questions/943087/what-exactly-will-happen-if-i-disable-c-exceptions-in-a-project
	__try
	{
		RaiseException(NS_MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		// this runs if not attached to a debugger (thus not really naming the thread)
	}
}

void ThreadImpl::setPriority(ThreadPriority::Enum prio)
{
	BOOL rc = false;
	switch(prio)
	{
	case ThreadPriority::eHIGH:
		rc = SetThreadPriority(getThread(this)->thread, THREAD_PRIORITY_HIGHEST);
		break;
	case ThreadPriority::eABOVE_NORMAL:
		rc = SetThreadPriority(getThread(this)->thread, THREAD_PRIORITY_ABOVE_NORMAL);
		break;
	case ThreadPriority::eNORMAL:
		rc = SetThreadPriority(getThread(this)->thread, THREAD_PRIORITY_NORMAL);
		break;
	case ThreadPriority::eBELOW_NORMAL:
		rc = SetThreadPriority(getThread(this)->thread, THREAD_PRIORITY_BELOW_NORMAL);
		break;
	case ThreadPriority::eLOW:
		rc = SetThreadPriority(getThread(this)->thread, THREAD_PRIORITY_LOWEST);
		break;
	default:
		break;
	}
	if(!rc)
	{
		physx::shdfnd::getFoundation().error(PxErrorCode::eINTERNAL_ERROR, __FILE__, __LINE__,
			"PsWindowsThread::setPriority: Failed to set thread priority.");
	}
}

ThreadPriority::Enum ThreadImpl::getPriority(Id threadId)
{
	ThreadPriority::Enum retval = ThreadPriority::eLOW;
	int priority = GetThreadPriority((HANDLE)threadId);
	PX_COMPILE_TIME_ASSERT(THREAD_PRIORITY_HIGHEST > THREAD_PRIORITY_ABOVE_NORMAL);
	if(priority >= THREAD_PRIORITY_HIGHEST)
		retval = ThreadPriority::eHIGH;
	else if(priority >= THREAD_PRIORITY_ABOVE_NORMAL)
		retval = ThreadPriority::eABOVE_NORMAL;
	else if(priority >= THREAD_PRIORITY_NORMAL)
		retval = ThreadPriority::eNORMAL;
	else if(priority >= THREAD_PRIORITY_BELOW_NORMAL)
		retval = ThreadPriority::eBELOW_NORMAL;
	return retval;
}

uint32_t TlsAlloc()
{
	DWORD rv = ::TlsAlloc();
	PX_ASSERT(rv != TLS_OUT_OF_INDEXES);
	return (uint32_t)rv;
}

void TlsFree(uint32_t index)
{
	::TlsFree(index);
}

void* TlsGet(uint32_t index)
{
	return ::TlsGetValue(index);
}

size_t TlsGetValue(uint32_t index)
{
	return reinterpret_cast<size_t>(::TlsGetValue(index));
}

uint32_t TlsSet(uint32_t index, void* value)
{
	return (uint32_t)::TlsSetValue(index, value);
}

uint32_t TlsSetValue(uint32_t index, size_t value)
{
	return (uint32_t)::TlsSetValue(index, reinterpret_cast<void*>(value));
}

uint32_t ThreadImpl::getDefaultStackSize()
{
	return 1048576;
};

} // namespace shdfnd
} // namespace physx
