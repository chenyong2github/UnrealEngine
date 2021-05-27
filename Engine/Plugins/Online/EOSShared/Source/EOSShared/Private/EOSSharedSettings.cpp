// Copyright Epic Games, Inc. All Rights Reserved.

#include "EOSSharedSettings.h"

#include "Algo/Transform.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "EOSShared.h"
#include "IEOSSDKManager.h"

#include "eos_logging.h"

#define LOCTEXT_NAMESPACE "EOS"
#define INI_SECTION TEXT("/Script/EOSShared.EOSSharedSettings")

FEOSSharedSettings UEOSSharedSettings::GetSettings()
{
	if (UObjectInitialized())
	{
		return UEOSSharedSettings::AutoGetSettings();
	}
	return UEOSSharedSettings::ManualGetSettings();
}

FEOSSharedSettings UEOSSharedSettings::AutoGetSettings()
{
	return GetDefault<UEOSSharedSettings>()->ToNative();
}

FEOSSharedSettings UEOSSharedSettings::ManualGetSettings()
{
	FEOSSharedSettings Native;

	FString LogLevelString;
	if (GConfig->GetString(INI_SECTION, TEXT("LogLevel"), LogLevelString, GEngineIni))
	{
		LexFromString(Native.LogLevel, *LogLevelString);
	}

	return Native;
}

FEOSSharedSettings UEOSSharedSettings::ToNative() const
{
	FEOSSharedSettings Native;

	Native.LogLevel = LogLevel;

	return Native;
}

#if WITH_EDITOR
void UEOSSharedSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property)
	{
		if (PropertyChangedEvent.Property->GetFName() == FName(TEXT("LogLevel")))
		{
			IEOSSDKManager* SDKManager = IEOSSDKManager::Get();
			if (SDKManager && SDKManager->IsInitialized())
			{
				const EOS_ELogLevel EosLogLevel = ConvertLogLevel(LogLevel);
				const EOS_EResult EosResult = EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES, EosLogLevel);
				if (EosResult != EOS_EResult::EOS_Success)
				{
					UE_LOG(LogEOSSDK, Warning, TEXT("EOS_Logging_SetLogLevel failed error:%s"), *LexToString(EosResult));
				}
			}
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void LexFromString(EEosLogLevel& Value, const TCHAR* String)
{
	if (FCString::Stricmp(String, TEXT("Off")) == 0)
	{
		Value = EEosLogLevel::Off;
	}
	else if (FCString::Stricmp(String, TEXT("Fatal")) == 0)
	{
		Value = EEosLogLevel::Fatal;
	}
	else if (FCString::Stricmp(String, TEXT("Error")) == 0)
	{
		Value = EEosLogLevel::Error;
	}
	else if (FCString::Stricmp(String, TEXT("Warning")) == 0)
	{
		Value = EEosLogLevel::Warning;
	}
	else if (FCString::Stricmp(String, TEXT("Verbose")) == 0)
	{
		Value = EEosLogLevel::Verbose;
	}
	else if (FCString::Stricmp(String, TEXT("VeryVerbose")) == 0)
	{
		Value = EEosLogLevel::VeryVerbose;
	}
	else
	{
		Value = EEosLogLevel::Info;
	}
}

EOS_ELogLevel ConvertLogLevel(const EEosLogLevel LogLevel)
{
	EOS_ELogLevel Result = EOS_ELogLevel::EOS_LOG_Info;
	switch (LogLevel)
	{
	case EEosLogLevel::Off:			Result = EOS_ELogLevel::EOS_LOG_Off; break;
	case EEosLogLevel::Fatal:		Result = EOS_ELogLevel::EOS_LOG_Fatal; break;
	case EEosLogLevel::Error:		Result = EOS_ELogLevel::EOS_LOG_Error; break;
	case EEosLogLevel::Warning:		Result = EOS_ELogLevel::EOS_LOG_Warning; break;
	case EEosLogLevel::Info:		Result = EOS_ELogLevel::EOS_LOG_Info; break;
	case EEosLogLevel::Verbose:		Result = EOS_ELogLevel::EOS_LOG_Verbose; break;
	case EEosLogLevel::VeryVerbose:	Result = EOS_ELogLevel::EOS_LOG_VeryVerbose; break;
	}
	return Result;
}

#undef INI_SECTION
#undef LOCTEXT_NAMESPACE
