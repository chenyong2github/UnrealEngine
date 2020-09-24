// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/CString.h"
#include "Templates/Atomic.h"
#include "Modules/ModuleManager.h"
// for std::enable_if
#include <type_traits>

#include "Lumin/CAPIShims/LuminAPI.h"
#include "Lumin/CAPIShims/IMagicLeapLibraryLoader.h"

DEFINE_LOG_CATEGORY_STATIC(LogLuminAPIImpl, Display, All);

/** Utility class to load the correct MLSDK libs depending on the path set to the MLSDK package and whether or not we want to use MLremote / Zero Iteration*/
/** Reads the config file and environment variable for the MLSDK package path and sets up the correct environment to load the libraries from. */
class MLSDK_API FMagicLeapLibraryLoader : public IMagicLeapLibraryLoader
{
public:
	/**
	 * Called right after the module DLL has been loaded and the module object has been created
	 * Load dependent modules here, and they will be guaranteed to be available during ShutdownModule. ie:
	 *
	 * FModuleManager::Get().LoadModuleChecked(TEXT("HTTP"));
	 */
	void StartupModule() override
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
					DllSearchPaths.Append(ZIShimPath);
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
		else
		{
			UE_LOG(LogLuminAPIImpl, Warning, TEXT("MLSDK not found.  This likely means the MLSDK environment variable is not set."));
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
	void * LoadDLL(const FString& Name) const override
	{
		FString DLLName = FString(FPlatformProcess::GetModulePrefix()) + Name + TEXT(".") + FPlatformProcess::GetModuleExtension();
#if PLATFORM_MAC
		// FPlatformProcess::GetModulePrefix() for Mac is an empty string in Unreal
		// whereas MLSDK uses 'lib' as the prefix for its OSX libs.
		if (FString(FPlatformProcess::GetModulePrefix()).Len() == 0)
		{
			DLLName = FString(TEXT("lib")) + DLLName;
		}
#endif
		for (const FString& path : DllSearchPaths)
		{
			void* dll = FPlatformProcess::GetDllHandle(*FPaths::Combine(*path, *DLLName));
			if (dll != nullptr)
			{
				UE_LOG(LogLuminAPIImpl, Display, TEXT("Dll loaded: %s"), *FPaths::Combine(*path, *DLLName));
				return dll;
			}
		}
		
		return nullptr;
	}
	
private:
	TArray<FString> DllSearchPaths;
	
	bool GetZIShimVariablesLabDriver(const FString & MLSDK, TMap<FString, FString> & ResultVariables) const
	{
		bool bRetVal = false;
		void* PipeRead = nullptr;
		void* PipeWrite = nullptr;
		
		if (!FPlatformProcess::CreatePipe(PipeRead, PipeWrite))
		{
			return false;
		}
		
		FString Args(TEXT(""));
		FString ExecutablePath(TEXT(""));
#if PLATFORM_WINDOWS
		Args = FString::Printf(TEXT("/C %s/labdriver.cmd -pretty com.magicleap.zi:get-shim-search-paths -release -sdk=%s"), *MLSDK, *MLSDK);
		ExecutablePath = TEXT("cmd.exe");
#elif PLATFORM_LINUX
		Args = FString::Printf(TEXT("-pretty com.magicleap.zi:get-shim-search-paths -release -sdk=%s"), *MLSDK);
		ExecutablePath = FString::Printf(TEXT("%s/labdriver"), *MLSDK);
#elif PLATFORM_MAC
		Args = FString::Printf(TEXT("-pretty com.magicleap.zi:get-shim-search-paths -release -sdk=%s"), *MLSDK);
		ExecutablePath = FString::Printf(TEXT("%s/labdriver"), *MLSDK);
#endif // PLATFORM_WINDOWS
		FProcHandle ProcHandle = FPlatformProcess::CreateProc(*ExecutablePath, *Args, false, true, true, nullptr, 0, nullptr, PipeWrite);
		FString StringOutput;
		if (ProcHandle.IsValid())
		{
			while (FPlatformProcess::IsProcRunning(ProcHandle))
			{
				FString ThisRead = FPlatformProcess::ReadPipe(PipeRead);
				StringOutput += ThisRead;
			}
			
			StringOutput += FPlatformProcess::ReadPipe(PipeRead);
			int32 ReturnCode = -1;
			FPlatformProcess::GetProcReturnCode(ProcHandle, &ReturnCode);
			bRetVal = StringOutput.Contains(TEXT("\"success\": true"));
		}
		
		FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
		
		if (bRetVal)
		{
			// Reset bRetVal.  We've only succeeded if we add at least one path to ResultVariables
			bRetVal = false;
			TArray<FString> Output;
			StringOutput.ParseIntoArrayLines(Output);
			
			int32 FirstOutputLineIndex = INDEX_NONE;
			FString ShimPathResult;
			for (int32 i = 0; i < Output.Num(); i++)
			{
				// Find the start of the output section
				if (Output[i].Contains(TEXT("\"output\": [")))
				{
					FirstOutputLineIndex = i + 1;
					continue;
				}
				
				// Parse the output section to get the paths
				if (FirstOutputLineIndex >= 0 && i >= FirstOutputLineIndex)
				{
					FString TempString = Output[i].RightChop(Output[i].Find(TEXT("\"")) + 1);
					TempString = TempString.Mid(0, TempString.Find(TEXT("\"")));
					ShimPathResult += TempString;
					bRetVal = true;
					
					// If the next line is the end of the output, we're done.  Otherwise, add a semi-colon to prepare to add the next path
					if ((i + 1) < Output.Num() && !Output[i+1].Contains(TEXT("],")))
					{
						ShimPathResult += TEXT(";");
					}
					else
					{
						break;
					}
				}
			}
#if PLATFORM_WINDOWS
			ResultVariables.Add(TEXT("ZI_SHIM_PATH_win64"), ShimPathResult);
#elif PLATFORM_LINUX
			ResultVariables.Add(TEXT("ZI_SHIM_PATH_linux64"), ShimPathResult);
#elif PLATFORM_MAC
			ResultVariables.Add(TEXT("ZI_SHIM_PATH_osx"), ShimPathResult);
#endif // PLATFORM_WINDOWS
		}
		
		return bRetVal;
	}
	
	/** Fills the Variables with the evaluated contents of the SDK Shim discovery data. */
	bool GetZIShimVariables(const FString & MLSDK, TMap<FString, FString> & ResultVariables) const
	{
		// First try to get the shim variables from lab driver
		if (GetZIShimVariablesLabDriver(MLSDK, ResultVariables))
		{
			return true;
		}
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

IMPLEMENT_MODULE(FMagicLeapLibraryLoader, MLSDK);
