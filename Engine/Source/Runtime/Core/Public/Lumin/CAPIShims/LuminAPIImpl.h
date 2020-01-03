// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/CString.h"
#include "Templates/Atomic.h"
// for std::enable_if
#include <type_traits>

DEFINE_LOG_CATEGORY_STATIC(LogLuminAPI, Display, All);

namespace MLSDK_API
{

/** Utility class to load the correct MLSDK libs depedning on the path set to the MLSDK package and whether or not we want to use MLremote / Zero Iteration*/
class LibraryLoader
{
public:
	static LibraryLoader & Ref()
	{
		static LibraryLoader LibLoader;
		return LibLoader;
	}

	/** Reads the config file and environment variable for the MLSDK package path and sets up the correct environment to load the libraries from. */
	LibraryLoader()
	{
#if !PLATFORM_LUMIN
		// We search various places for the ML API DLLs to support loading alternate
		// implementations. For example to use VDZI on PC platforms.

		// Fixing SDKUNREAL-2128: When MLSDK environment variable is not set, we need to check for a possible
		// config file value for SDK location.
		FString MLSDKEnvironmentVariableName = TEXT("MLSDK");
		FString MLSDK = FPlatformMisc::GetEnvironmentVariable(*MLSDKEnvironmentVariableName);
		if (MLSDK.IsEmpty())
		{
			GConfig->GetString(TEXT("/Script/LuminPlatformEditor.MagicLeapSDKSettings"), TEXT("MLSDKPath"), MLSDK, GEngineIni);
			// Formatted as (Path="C:/Directory")
			if (!MLSDK.IsEmpty())
			{
				auto Index0 = MLSDK.Find(TEXT("\""));
				if (Index0 != -1)
				{
					++Index0;
					auto Index1 = MLSDK.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
					if (Index1 > Index0)
					{
						MLSDK = MLSDK.Mid(Index0, Index1 - Index0);
					}
				}
			}
		}

		bool bIsVDZIEnabled = false;
		GConfig->GetBool(TEXT("/Script/MagicLeap.MagicLeapSettings"), TEXT("bEnableZI"), bIsVDZIEnabled, GEngineIni);

		if (bIsVDZIEnabled)
		{
			// VDZI search paths: VDZI is only active in PC builds.
			// This allows repointing MLAPI loading to the VDZI DLLs.
			FString VDZILibraryPath = TEXT("");
			GConfig->GetString(TEXT("MLSDK"), TEXT("LibraryPath"), VDZILibraryPath, GEngineIni);
			if (!VDZILibraryPath.IsEmpty())
			{
				DllSearchPaths.Add(VDZILibraryPath);
			}

			// We also search in the MLSDK VDZI paths for libraries if we have them.
			if (!MLSDK.IsEmpty())
			{
				TArray<FString> ZIShimPath = GetZIShimPath(MLSDK);
				if (ZIShimPath.Num() > 0)
				{
					DllSearchPaths.Append(GetZIShimPath(MLSDK));
				}
				else
				{
					// Fallback to adding fixed known paths if we fail to get anything from
					// the configuration data.
					// The default VDZI dir.
					DllSearchPaths.Add(FPaths::Combine(*MLSDK, TEXT("VirtualDevice"), TEXT("lib")));
					// We also need to add the default bin dir as dependent libs are placed there instead
					// of in the lib directory.
					DllSearchPaths.Add(FPaths::Combine(*MLSDK, TEXT("VirtualDevice"), TEXT("bin")));
				}
			}
		}
#endif

		// The MLSDK DLLs are platform specific and are segregated in directories for each platform.

#if PLATFORM_LUMIN
		// Lumin uses the system path as we are in device.
		DllSearchPaths.Add(TEXT("/system/lib64"));
#else

		if (!MLSDK.IsEmpty())
		{
#if PLATFORM_WINDOWS
			DllSearchPaths.Add(FPaths::Combine(*MLSDK, TEXT("lib"), TEXT("win64")));
#elif PLATFORM_LINUX
			DllSearchPaths.Add(FPaths::Combine(*MLSDK, TEXT("lib"), TEXT("linux64")));
#elif PLATFORM_MAC
			DllSearchPaths.Add(FPaths::Combine(*MLSDK, TEXT("lib"), TEXT("osx")));
#endif // PLATFORM_WINDOWS
		}

#endif // PLATFORM_LUMIN

		// Add the search paths to where we will load the DLLs from. 
		// For all we add to the UE4 dir listing which takes care of the first-level
		// loading of DLL modules.
		for (const FString& path : DllSearchPaths)
		{
			FPlatformProcess::AddDllDirectory(*path);
		}
	}

	/**
	  Loads the given library from the correct path.
	  @param Name Name of library to load, without any prefix or extension. e.g."ml_perception_client".
	  @return True if the library was succesfully loaded. A false value generally indicates that the MLSDK path is not set correctly.
	*/
	void * LoadDLL(FString Name)
	{
		Name = FString(FPlatformProcess::GetModulePrefix()) + Name + TEXT(".") + FPlatformProcess::GetModuleExtension();
#if PLATFORM_MAC
		// FPlatformProcess::GetModulePrefix() for Mac is an empty string in Unreal
		// whereas MLSDK uses 'lib' as the prefix for its OSX libs.
		if (FString(FPlatformProcess::GetModulePrefix()).Len() == 0)
		{
			Name = FString(TEXT("lib")) + Name;
		}
#endif
		for (const FString& path : DllSearchPaths)
		{
			void* dll = FPlatformProcess::GetDllHandle(*FPaths::Combine(*path, *Name));
			if (dll != nullptr)
			{
				UE_LOG(LogLuminAPI, Display, TEXT("Dll loaded: %s"), *FPaths::Combine(*path, *Name));
				return dll;
			}
		}

		return nullptr;
	}

private:
	TArray<FString> DllSearchPaths;

	/** Fills the Variables with the evaluated contents of the SDK Shim discovery data. */
	bool GetZIShimVariables(const FString & MLSDK, TMap<FString, FString> & ResultVariables) const
	{
		// The known path to the paths file.
		FString SDKShimDiscoveryFile = FPaths::Combine(MLSDK, TEXT(".metadata"), TEXT("sdk_shim_discovery.txt"));
		if (!FPaths::FileExists(SDKShimDiscoveryFile))
		{
			return false;
		}
		// Map of variable to value for evaluating the content of the file.
		// On successful parsing and evaluation we copy the data to the result.
		TMap<FString, FString> Variables;
		Variables.Add(TEXT("$(MLSDK)"), MLSDK);
		// TODO: Determine MLSDK version and set MLSDK_VERSION variable.
#if PLATFORM_WINDOWS
		Variables.Add(TEXT("$(HOST)"), TEXT("win64"));
#elif PLATFORM_LINUX
		Variables.Add(TEXT("$(HOST)"), TEXT("linux64"));
#elif PLATFORM_MAC
		Variables.Add(TEXT("$(HOST)"), TEXT("osx"));
#endif // PLATFORM_WINDOWS
		// Single pass algo for evaluating the file:
		// 1. for each line:
		// a. for each occurance of $(NAME):
		// i. replace with Variables[NAME], if no NAME var found ignore replacement.
		// b. split into var=value, and add to Variables.
		IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();
		TUniquePtr<IFileHandle> File(PlatformFile.OpenRead(*SDKShimDiscoveryFile));
		if (File)
		{
			TArray<uint8> Data;
			Data.SetNumZeroed(File->Size() + 1);
			File->Read(Data.GetData(), Data.Num() - 1);
			FUTF8ToTCHAR TextConvert(reinterpret_cast<ANSICHAR*>(Data.GetData()));
			const TCHAR * Text = TextConvert.Get();
			while (Text && *Text)
			{
				Text += FCString::Strspn(Text, TEXT("\t "));
				if (Text[0] == '#' || Text[0] == '\r' || Text[0] == '\n' || !*Text)
				{
					// Skip comment or empty lines.
					Text += FCString::Strcspn(Text, TEXT("\r\n"));
				}
				else
				{
					// Parse variable=value line.
					FString Variable(FCString::Strcspn(Text, TEXT("\t= ")), Text);
					Text += Variable.Len();
					Text += FCString::Strspn(Text, TEXT("\t= "));
					FString Value(FCString::Strcspn(Text, TEXT("\r\n")), Text);
					Text += Value.Len();
					Value.TrimEndInline();
					// Eval any var references in both variable and value.
					int32 EvalCount = 0;
					do
					{
						EvalCount = 0;
						for (auto VarEntry : Variables)
						{
							EvalCount += Variable.ReplaceInline(*VarEntry.Key, *VarEntry.Value);
							EvalCount += Value.ReplaceInline(*VarEntry.Key, *VarEntry.Value);
						}
					} while (EvalCount != 0 && (Variable.Contains(TEXT("$(")) || Value.Contains(TEXT("$("))));
					// Intern the new variable.
					Variable = TEXT("$(") + Variable + TEXT(")");
					Variables.Add(Variable, Value);
				}
				// Skip over EOL to get to the next line.
				if (Text[0] == '\r' && Text[1] == '\n')
				{
					Text += 2;
				}
				else
				{
					Text += 1;
				}
			}
		}
		// We now need to copy the evaled variables to the result and un-munge them for
		// plain access. We use them munged for the eval for easier eval replacement above.
		ResultVariables.Empty(Variables.Num());
		for (auto VarEntry : Variables)
		{
			ResultVariables.Add(VarEntry.Key.Mid(2, VarEntry.Key.Len() - 3), VarEntry.Value);
		}
		return true;
	}

	TArray<FString> GetZIShimPath(const FString & MLSDK) const
	{
#if PLATFORM_WINDOWS
		const FString ZIShimPathVar = "ZI_SHIM_PATH_win64";
#elif PLATFORM_LINUX
		const FString ZIShimPathVar = "ZI_SHIM_PATH_linux64";
#elif PLATFORM_MAC
		const FString ZIShimPathVar = "ZI_SHIM_PATH_osx";
#else
		const FString ZIShimPathVar = "";
#endif // PLATFORM_WINDOWS

		TMap<FString, FString> Variables;
		if (GetZIShimVariables(MLSDK, Variables) && Variables.Contains(ZIShimPathVar))
		{
			// The shim path var we are looking for.
			FString Value = Variables[ZIShimPathVar];
			// Since it's a path variable it can have multiple components. Hence
			// split those out into our result;
			TArray<FString> Result;
			for (FString Path; Value.Split(TEXT(";"), &Path, &Value); )
			{
				Result.Add(Path);
			}
			Result.Add(Value);
			return Result;
		}
		return TArray<FString>();
	}
};

/** Manages a single API library to load it on demand when retrieving an entry in that library.
	The library is designated with a type key to statically bind the loaded instance to
	only one of these. */
template <typename LibKey>
class Library
{
public:
	~Library()
	{
		if (DllHandle)
		{
			FPlatformProcess::FreeDllHandle(DllHandle);
			DllHandle = nullptr;
		}
	}

	// The singleton for the library.
	static Library & Ref()
	{
		static Library LibraryInstance;
		return LibraryInstance;
	}

	// Set the name of the DLLs (or SO, or DYLIB) to load when fetching symbols.
	void SetName(const char * Name)
	{
		if (!LibName) LibName = Name;
	}

	void * GetEntry(const char * Name)
	{
		if (!DllHandle)
		{
			// The library name need to be set for us to load it. I.e. someone needs to
			// call SetName before calling GetEntry. Normally this is done by the
			// DelayCall class below.
			check(LibName != nullptr);
			DllHandle = LibraryLoader::Ref().LoadDLL(ANSI_TO_TCHAR(LibName));
		}
		if (DllHandle)
		{
			check(Name != nullptr);
			return FPlatformProcess::GetDllExport(DllHandle, ANSI_TO_TCHAR(Name));
		}
		return nullptr;
	}

private:
	const char * LibName = nullptr;
	void * DllHandle = nullptr;
};

#if MLSDK_API_USE_STUBS
// Special case for void return type
template<typename ReturnType> inline typename std::enable_if<std::is_void<ReturnType>::value, ReturnType>::type DefaultReturn()
{
}
// Special case for all pointer return types
template<typename ReturnType> inline typename std::enable_if<std::is_pointer<ReturnType>::value, ReturnType>::type DefaultReturn()
{
	return nullptr;
}
// Value type cases
template<typename ReturnType> inline ReturnType DefaultValue()
{
	ReturnType returnVal;
	FMemory::Memzero(&returnVal, sizeof(ReturnType));
	return returnVal;
}
template<> inline MLResult DefaultValue()
{
	return MLResult_NotImplemented;
}
template<typename ReturnType> inline typename std::enable_if<!std::is_void<ReturnType>::value && !std::is_pointer<ReturnType>::value, ReturnType>::type DefaultReturn()
{
	return DefaultValue<ReturnType>();
}
#endif // MLSDK_API_USE_STUBS

/** This is a single delay loaded entry value. The class in keyed on both the library and function,
	as types. When first created it will try and load the pointer to the named global value. */
template <typename LibKey, typename Key, typename T>
class DelayValue
{
public:
	DelayValue(const char* LibName, const char* EntryName)
	{
		if (!Value) 
		{
			Library<LibKey>::Ref().SetName(LibName);
			Value = static_cast<T*>(Library<LibKey>::Ref().GetEntry(EntryName));
		}
	}

	T Get() 
	{
		return Value ? *Value : DefaultReturn<T>();
	}
	
private:
	static T* Value;

};

template <typename LibKey, typename Key, typename T>
T* DelayValue<LibKey, Key, T>::Value = nullptr;

/** This is a single delay loaded entry call. The class in keyed on both the library and function,
	as types. When first used as a function it will attempt to retrieve the foreign entry and
	call it. Onward the retrieved entry is called directly. */
template <typename LibKey, typename Key, typename Result, typename... Args>
class DelayCall
{
public:
	// On construction we use the given LibName to set the library name of that singleton.
	DelayCall(const char *LibName, const char *EntryName)
		: EntryName(EntryName)
	{
		Library<LibKey>::Ref().SetName(LibName);
		Self() = this;
	}

	Result operator()(Args... args)
	{
#if MLSDK_API_USE_STUBS
		if (*Call == nullptr)
		{
			return DefaultReturn<Result>();
		}
#endif // MLSDK_API_USE_STUBS
		return (*Call)(args...);
	}

private:
	typedef Result (*CallPointer)(Args...);

	const char *EntryName = nullptr;
	CallPointer Call = &LoadAndCall;

	static DelayCall * & Self()
	{
		static DelayCall * DelayCallInstance = nullptr;
		return DelayCallInstance;
	}

	// This is the default for a call. After it's called the call destination becomes the
	// call entry in the loaded library. Which bypasses this call to avoid as much overhead
	// as possible.
	static Result LoadAndCall(Args... args)
	{
		Self()->Call = (CallPointer)(Library<LibKey>::Ref().GetEntry(Self()->EntryName));
#if MLSDK_API_USE_STUBS
		if (Self()->Call == nullptr)
		{
			return DefaultReturn<Result>();
		}
#endif // MLSDK_API_USE_STUBS
		return (*Self()->Call)(args...);
	}
};

} // namespace MLSDK_API

#if defined(MLSDK_API_NO_DEPRECATION_WARNING)
#define MLSDK_API_DEPRECATED_MSG(msg)
#define MLSDK_API_DEPRECATED
#else
#if defined(_MSC_VER) && _MSC_VER
#define MLSDK_API_DEPRECATED_MSG(msg) __declspec(deprecated(msg))
#define MLSDK_API_DEPRECATED __declspec(deprecated)
#else
#define MLSDK_API_DEPRECATED_MSG(msg) __attribute__((deprecated(msg)))
#define MLSDK_API_DEPRECATED __attribute__((deprecated))
#endif
#endif
