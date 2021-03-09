// Copyright Epic Games, Inc. All Rights Reserved.

#include "CEF3Utils.h"
#include "Containers/UnrealString.h"
#include "Logging/LogMacros.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "HAL/PlatformProcess.h"
#include "CEF3UtilsLog.h"
#if WITH_CEF3
#	if PLATFORM_MAC
#		include "include/wrapper/cef_library_loader.h"
#		define CEF3_BIN_DIR TEXT("Binaries/ThirdParty/CEF3")
#		define CEF3_FRAMEWORK_DIR CEF3_BIN_DIR TEXT("/Mac/Chromium Embedded Framework.framework")
#		define CEF3_FRAMEWORK_EXE CEF3_FRAMEWORK_DIR TEXT("/Chromium Embedded Framework")
#	endif
#endif

DEFINE_LOG_CATEGORY(LogCEF3Utils);

IMPLEMENT_MODULE(FDefaultModuleImpl, CEF3Utils);

#if WITH_CEF3
namespace CEF3Utils
{
#if PLATFORM_WINDOWS
    void* CEF3DLLHandle = nullptr;
	void* ElfHandle = nullptr;
	void* D3DHandle = nullptr;
	void* GLESHandle = nullptr;
    void* EGLHandle = nullptr;
#elif PLATFORM_MAC
	// Dynamically load the CEF framework library.
	CefScopedLibraryLoader *CEFLibraryLoader = nullptr;
#endif

	void* LoadDllCEF(const FString& Path)
	{
		if (Path.IsEmpty())
		{
			return nullptr;
		}
		void* Handle = FPlatformProcess::GetDllHandle(*Path);
		if (!Handle)
		{
			int32 ErrorNum = FPlatformMisc::GetLastError();
			TCHAR ErrorMsg[1024];
			FPlatformMisc::GetSystemErrorMessage(ErrorMsg, 1024, ErrorNum);
			UE_LOG(LogCEF3Utils, Error, TEXT("Failed to get CEF3 DLL handle for %s: %s (%d)"), *Path, ErrorMsg, ErrorNum);
		}
		return Handle;
	}

	bool LoadCEF3Modules(bool bIsMainApp)
	{
#if PLATFORM_WINDOWS
	#if PLATFORM_64BITS
		FString DllPath(FPaths::Combine(*FPaths::EngineDir(), TEXT("Binaries/ThirdParty/CEF3/Win64")));
	#else
		FString DllPath(FPaths::Combine(*FPaths::EngineDir(), TEXT("Binaries/ThirdParty/CEF3/Win32")));
	#endif

		FPlatformProcess::PushDllDirectory(*DllPath);
		CEF3DLLHandle = LoadDllCEF(FPaths::Combine(*DllPath, TEXT("libcef.dll")));
		if (CEF3DLLHandle)
		{
			ElfHandle = LoadDllCEF(FPaths::Combine(*DllPath, TEXT("chrome_elf.dll")));
			D3DHandle = LoadDllCEF(FPaths::Combine(*DllPath, TEXT("d3dcompiler_47.dll")));
			GLESHandle = LoadDllCEF(FPaths::Combine(*DllPath, TEXT("libGLESv2.dll")));
			EGLHandle = LoadDllCEF(FPaths::Combine(*DllPath, TEXT("libEGL.dll")));
		}
		FPlatformProcess::PopDllDirectory(*DllPath);
		return CEF3DLLHandle != nullptr;
#elif PLATFORM_MAC
		// Dynamically load the CEF framework library.
		CEFLibraryLoader = new CefScopedLibraryLoader();
		
		FString CefFrameworkPath(FPaths::Combine(*FPaths::EngineDir(), CEF3_FRAMEWORK_EXE));
		CefFrameworkPath = FPaths::ConvertRelativePathToFull(CefFrameworkPath);
		
		bool bLoaderInitialized = false;
		if (bIsMainApp)
		{
			if (!CEFLibraryLoader->LoadInMain(TCHAR_TO_ANSI(*CefFrameworkPath)))
			{
				UE_LOG(LogCEF3Utils, Error, TEXT("Chromium loader initialization failed"));
			}
			else
			{
				bLoaderInitialized = true;
			}
		}
		else
		{
			if (!CEFLibraryLoader->LoadInHelper(TCHAR_TO_ANSI(*CefFrameworkPath)))
			{
				UE_LOG(LogCEF3Utils, Error, TEXT("Chromium helper loader initialization failed"));
			}
			else
			{
				bLoaderInitialized = true;
			}
		}
		return bLoaderInitialized;
#elif PLATFORM_LINUX
		// we runtime link the libcef.so and don't need to manually load here
		return true;
#else
		return false; // Unsupported libcef platform 
#endif
	}

	void UnloadCEF3Modules()
	{
#if PLATFORM_WINDOWS
		FPlatformProcess::FreeDllHandle(CEF3DLLHandle);
		CEF3DLLHandle = nullptr;
		FPlatformProcess::FreeDllHandle(ElfHandle);
		ElfHandle = nullptr;
		FPlatformProcess::FreeDllHandle(D3DHandle);
		D3DHandle = nullptr;
		FPlatformProcess::FreeDllHandle(GLESHandle);
		GLESHandle = nullptr;
		FPlatformProcess::FreeDllHandle(EGLHandle);
		EGLHandle = nullptr;
#elif PLATFORM_MAC
		delete CEFLibraryLoader;
		CEFLibraryLoader = nullptr;
#endif
	}
};
#endif //WITH_CEF3
