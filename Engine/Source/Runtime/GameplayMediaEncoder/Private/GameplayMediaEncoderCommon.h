// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "Logging/LogMacros.h"
#include "RHIStaticStates.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/ThreadSafeCounter.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/Event.h"
#include "Misc/ScopeExit.h"
#include "ShaderCore.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "RHI.h"
#include "RHIResources.h"

//
// Macros to control some things during development
// #RVF : Remove this when cleaning up
//
#define WRITE_TO_FILE 0
#define LIVESTREAMING 0

#if UE_BUILD_SHIPPING
	#define GAMEPLAYMEDIAENCODER_DEBUG 0
#else
	#define GAMEPLAYMEDIAENCODER_DEBUG 0
#endif

//
// Includes common to Windows and XboxOne
//
#if PLATFORM_WINDOWS || PLATFORM_XBOXONE
	#include "Templates/RefCounting.h"
#endif

//
// Windows only include
//
#if PLATFORM_WINDOWS

	#include "Windows/AllowWindowsPlatformTypes.h"
	#include "Windows/PreWindowsApi.h"
		#include <d3d11.h>
		#include <mftransform.h>
		#include <mfapi.h>
		#include <mferror.h>
		#include <mfidl.h>
		#include <codecapi.h>
		#include <shlwapi.h>
		#include <mfreadwrite.h>
	#include "Windows/PostWindowsApi.h"
	#include "Windows/HideWindowsPlatformTypes.h"

	#include "D3D11State.h"
	#include "D3D11Resources.h"
#endif

//
// XboxOne only includes
//
#if PLATFORM_XBOXONE

#include "XboxOne/XboxOneAllowPlatformTypes.h"
#include "XboxOne/XboxOnePreApi.h"
		#include <d3d11_x.h>
		#include <d3d12_x.h>
		#include <d3dcompiler_x.h>
		#include <d3dx12_x.h>
		#include <mftransform.h>
		#include <mfapi.h>
		#include <mferror.h>
		#include <mfidl.h>
		#include <codecapi.h>
		#include <mfreadwrite.h>
#include "XboxOne/XboxOnePostApi.h"
#include "XboxOne/XboxOneHidePlatformTypes.h"

#endif

#if GAMEPLAYMEDIAENCODER_DEBUG
	#define GAMEPLAYMEDIAENCODER_START PRAGMA_DISABLE_OPTIMIZATION
	#define GAMEPLAYMEDIAENCODER_END PRAGMA_ENABLE_OPTIMIZATION
#else
	#define GAMEPLAYMEDIAENCODER_START 
	#define GAMEPLAYMEDIAENCODER_END 
#endif

#define WMFMEDIA_SUPPORTED_PLATFORM (PLATFORM_WINDOWS && (WINVER >= 0x0600 /*Vista*/) && !UE_SERVER)

DECLARE_LOG_CATEGORY_EXTERN(GameplayMediaEncoder, Log, VeryVerbose);

//
// Windows and XboxOne common code/macros
//
#if PLATFORM_WINDOWS || PLATFORM_XBOXONE

inline const FString GetComErrorDescription(HRESULT Res)
{
	const uint32 BufSize = 4096;
	WIDECHAR buffer[4096];
	if (::FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM,
		nullptr,
		Res,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
		buffer,
		sizeof(buffer) / sizeof(*buffer),
		nullptr))
	{
		return buffer;
	}
	else
	{
		return TEXT("[cannot find error description]");
	}
}

#if PLATFORM_WINDOWS
	#include "Windows/AllowWindowsPlatformTypes.h"
#elif PLATFORM_XBOXONE
	#include "XboxOne/XboxOneAllowPlatformTypes.h"
#endif

// macro to deal with COM calls inside a function that returns `false` on error
#define CHECK_HR(COM_call)\
	{\
		HRESULT Res = COM_call;\
		if (FAILED(Res))\
		{\
			UE_LOG(GameplayMediaEncoder, Error, TEXT("`" #COM_call "` failed: 0x%X - %s"), Res, *GetComErrorDescription(Res));\
			return false;\
		}\
	}

// macro to deal with COM calls inside COM method (that returns HRESULT)
#define CHECK_HR_COM(COM_call)\
	{\
		HRESULT Res = COM_call;\
		if (FAILED(Res))\
		{\
			UE_LOG(GameplayMediaEncoder, Error, TEXT("`" #COM_call "` failed: 0x%X - %s"), Res, *GetComErrorDescription(Res));\
			return Res;\
		}\
	}

// macro to deal with COM calls inside COM method (that simply returns)
#define CHECK_HR_VOID(COM_call)\
	{\
		HRESULT Res = COM_call;\
		if (FAILED(Res))\
		{\
			UE_LOG(GameplayMediaEncoder, Error, TEXT("`" #COM_call "` failed: 0x%X - %s"), Res, *GetComErrorDescription(Res));\
			return;\
		}\
	}

#if PLATFORM_WINDOWS
	#include "Windows/HideWindowsPlatformTypes.h"
#elif PLATFORM_XBOXONE
	#include "XboxOne/XboxOneHidePlatformTypes.h"
#endif

// following commented include causes name clash between UE4 and Windows `IMediaEventSink`,
// we just need a couple of GUIDs from there so the solution is to duplicate them below
//#include "wmcodecdsp.h"

const GUID CLSID_AACMFTEncoder = { 0x93AF0C51, 0x2275, 0x45d2, { 0xA3, 0x5B, 0xF2, 0xBA, 0x21, 0xCA, 0xED, 0x00 } };
const GUID CLSID_CMSH264EncoderMFT = { 0x6ca50344, 0x051a, 0x4ded, { 0x97, 0x79, 0xa4, 0x33, 0x05, 0x16, 0x5e, 0x35 } };
const GUID CLSID_VideoProcessorMFT = { 0x88753b26, 0x5b24, 0x49bd, { 0xb2, 0xe7, 0xc, 0x44, 0x5c, 0x78, 0xc9, 0x82 } };

// `MF_LOW_LATENCY` is defined in "mfapi.h" for >= WIN8
// UE4 supports lower Windows versions at the moment and so `WINVER` is < `_WIN32_WINNT_WIN8`
// to be able to use `MF_LOW_LATENCY` with default UE4 build we define it ourselves and check actual
// Windows version in runtime
#if (WINVER < _WIN32_WINNT_WIN8)
	const GUID MF_LOW_LATENCY = { 0x9c27891a, 0xed7a, 0x40e1,{ 0x88, 0xe8, 0xb2, 0x27, 0x27, 0xa0, 0x24, 0xee } };
#endif


#endif

//
// Windows only utility code/macros
//
#if PLATFORM_WINDOWS

ID3D11Device* GetUE4DxDevice();

// scope-disable particular DX11 Debug Layer errors
class FScopeDisabledDxDebugErrors final
{
private:

public:
	FScopeDisabledDxDebugErrors(TArray<D3D11_MESSAGE_ID>&& ErrorsToDisable)
	{
		TRefCountPtr<ID3D11Debug> Debug;
		HRESULT HRes = GetUE4DxDevice()->QueryInterface(__uuidof(ID3D11Debug), reinterpret_cast<void**>(Debug.GetInitReference()));

		if (HRes == E_NOINTERFACE)
		{
			// Debug Layer is not enabled, so no need to disable its errors
			return;
		}

		if (!SUCCEEDED(HRes) ||
			!SUCCEEDED(HRes = InfoQueue->QueryInterface(__uuidof(ID3D11InfoQueue), reinterpret_cast<void**>(Debug.GetInitReference()))))
		{
			UE_LOG(GameplayMediaEncoder, VeryVerbose, TEXT("Failed to get ID3D11InfoQueue: 0x%X - %s"), HRes, *GetComErrorDescription(HRes));
			return;
		}

		D3D11_INFO_QUEUE_FILTER filter = {};
		filter.DenyList.NumIDs = ErrorsToDisable.Num();
		filter.DenyList.pIDList = ErrorsToDisable.GetData();
		bSucceeded = SUCCEEDED(InfoQueue->PushStorageFilter(&filter));
	}

	~FScopeDisabledDxDebugErrors()
	{
		if (bSucceeded)
		{
			InfoQueue->PopStorageFilter();
		}
	}

private:
	TRefCountPtr<ID3D11InfoQueue> InfoQueue;
	bool bSucceeded = false;
};


#endif

//
// XboxOne only utility code/macros
//
#if PLATFORM_XBOXONE

ID3D12Device* GetUE4DxDevice();

#endif

class FThread final : public FRunnable
{
public:
	using FCallback = TFunction<void()>;

	explicit FThread(TCHAR const* ThreadName, const FCallback& Callback) :
		Callback(Callback)
	{
		Thread.Reset(FRunnableThread::Create(this, ThreadName, TPri_BelowNormal));
	}

	void Join()
	{
		Thread->WaitForCompletion();
	}

	virtual uint32 Run() override
	{
		Callback();
		return 0;
	}

private:
	FCallback Callback;
	TUniquePtr<FRunnableThread> Thread;

private:
	FThread(const FThread&) = delete;
	FThread& operator=(const FThread&) = delete;
};


template<typename F>
inline void ExecuteRHICommand(F&& Functor)
{
	FRHICommandList& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	if (RHICmdList.Bypass())
	{
		Functor();
	}
	else
	{
		FRHICOMMAND_MACRO(FLocalRHICommand)
		{
			F Functor;

			FLocalRHICommand(F InFunctor)
				: Functor(MoveTemp(InFunctor))
			{}

			void Execute(FRHICommandListBase& CmdList)
			{
				Functor();
			}
		};

		new (RHICmdList.AllocCommand<FLocalRHICommand>()) FLocalRHICommand(Functor);
	}
}

// #RVF : Remove this once the code is production ready.
struct FMemoryCheckpoint
{
	FString Name;
	float UsedPhysicalMB;
	float DeltaMB;
	float AccumulatedMB;
};
extern TArray<FMemoryCheckpoint> gMemoryCheckpoints;
uint64 MemoryCheckpoint(const FString& Name);
void LogMemoryCheckpoints(const FString& Name);

