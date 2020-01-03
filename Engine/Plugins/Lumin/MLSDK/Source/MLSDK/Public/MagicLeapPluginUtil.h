// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/CString.h"
#include "Templates/Atomic.h"
#include "Lumin/CAPIShims/LuminAPIPlatform.h"

/** Utility class to deal with some API features. */
class FMagicLeapAPISetup
{
public:
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

	/** Returns the, cached, API level for the platform. (thread safe) */
	static uint32 GetPlatformLevel()
	{
#if WITH_MLSDK
		// Default to the compiled platform level if we can't get the runtime value.
		static TAtomic<uint32> Level(ML_PLATFORM_API_LEVEL);
		static TAtomic<bool> HaveLevel(false);
		if (!HaveLevel.Load(EMemoryOrder::Relaxed))
		{
			uint32 TempLevel = 0;
			MLResult Result = MLPlatformGetAPILevel(&TempLevel);
			if (Result == MLResult_Ok)
			{
				Level = TempLevel;
			}
			HaveLevel = true;
		}
		return Level;
#else
		return 0;
#endif
	}
};
