// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ModuleDescriptor.h"
#include "Misc/App.h"
#include "Misc/ScopedSlowTask.h"
#include "Dom/JsonObject.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "ModuleDescriptor"

ELoadingPhase::Type ELoadingPhase::FromString( const TCHAR *String )
{
	ELoadingPhase::Type TestType = (ELoadingPhase::Type)0;
	for(; TestType < ELoadingPhase::Max; TestType = (ELoadingPhase::Type)(TestType + 1))
	{
		const TCHAR *TestString = ToString(TestType);
		if(FCString::Stricmp(String, TestString) == 0)
		{
			break;
		}
	}
	return TestType;
}

const TCHAR* ELoadingPhase::ToString( const ELoadingPhase::Type Value )
{
	switch( Value )
	{
	case Default:
		return TEXT( "Default" );

	case PostDefault:
		return TEXT( "PostDefault" );

	case PreDefault:
		return TEXT( "PreDefault" );

	case PostConfigInit:
		return TEXT( "PostConfigInit" );

	case PostSplashScreen:
		return TEXT("PostSplashScreen");

	case PreEarlyLoadingScreen:
		return TEXT("PreEarlyLoadingScreen");

	case PreLoadingScreen:
		return TEXT( "PreLoadingScreen" );

	case PostEngineInit:
		return TEXT( "PostEngineInit" );

	case EarliestPossible:
		return TEXT("EarliestPossible");

	case None:
		return TEXT( "None" );

	default:
		ensureMsgf( false, TEXT( "Unrecognized ELoadingPhase value: %i" ), Value );
		return NULL;
	}
}

EHostType::Type EHostType::FromString( const TCHAR *String )
{
	EHostType::Type TestType = (EHostType::Type)0;
	for(; TestType < EHostType::Max; TestType = (EHostType::Type)(TestType + 1))
	{
		const TCHAR *TestString = ToString(TestType);
		if(FCString::Stricmp(String, TestString) == 0)
		{
			break;
		}
	}
	return TestType;
}

const TCHAR* EHostType::ToString( const EHostType::Type Value )
{
	switch( Value )
	{
		case Runtime:
			return TEXT( "Runtime" );

		case RuntimeNoCommandlet:
			return TEXT( "RuntimeNoCommandlet" );

		case RuntimeAndProgram:
			return TEXT( "RuntimeAndProgram" );

		case CookedOnly:
			return TEXT( "CookedOnly" );

		case UncookedOnly:
			return TEXT( "UncookedOnly" );

		case Developer:
			return TEXT( "Developer" );

		case DeveloperTool:
			return TEXT( "DeveloperTool" );

		case Editor:
			return TEXT( "Editor" );

		case EditorNoCommandlet:
			return TEXT( "EditorNoCommandlet" );

		case EditorAndProgram:
			return TEXT( "EditorAndProgram" );

		case Program:
			return TEXT("Program");

		case ServerOnly:
			return TEXT("ServerOnly");

		case ClientOnly:
			return TEXT("ClientOnly");

		case ClientOnlyNoCommandlet:
			return TEXT("ClientOnlyNoCommandlet");

		default:
			ensureMsgf( false, TEXT( "Unrecognized EModuleType value: %i" ), Value );
			return NULL;
	}
}

FModuleDescriptor::FModuleDescriptor(const FName InName, EHostType::Type InType, ELoadingPhase::Type InLoadingPhase)
	: Name(InName)
	, Type(InType)
	, LoadingPhase(InLoadingPhase)
{
}

bool FModuleDescriptor::Read(const FJsonObject& Object, FText& OutFailReason)
{
	// Read the module name
	TSharedPtr<FJsonValue> NameValue = Object.TryGetField(TEXT("Name"));
	if(!NameValue.IsValid() || NameValue->Type != EJson::String)
	{
		OutFailReason = LOCTEXT("ModuleWithoutAName", "Found a 'Module' entry with a missing 'Name' field");
		return false;
	}
	Name = FName(*NameValue->AsString());

	// Read the module type
	TSharedPtr<FJsonValue> TypeValue = Object.TryGetField(TEXT("Type"));
	if(!TypeValue.IsValid() || TypeValue->Type != EJson::String)
	{
		OutFailReason = FText::Format( LOCTEXT( "ModuleWithoutAType", "Found Module entry '{0}' with a missing 'Type' field" ), FText::FromName(Name) );
		return false;
	}
	Type = EHostType::FromString(*TypeValue->AsString());
	if(Type == EHostType::Max)
	{
		OutFailReason = FText::Format( LOCTEXT( "ModuleWithInvalidType", "Module entry '{0}' specified an unrecognized module Type '{1}'" ), FText::FromName(Name), FText::FromString(TypeValue->AsString()) );
		return false;
	}

	// Read the loading phase
	TSharedPtr<FJsonValue> LoadingPhaseValue = Object.TryGetField(TEXT("LoadingPhase"));
	if(LoadingPhaseValue.IsValid() && LoadingPhaseValue->Type == EJson::String)
	{
		LoadingPhase = ELoadingPhase::FromString(*LoadingPhaseValue->AsString());
		if(LoadingPhase == ELoadingPhase::Max)
		{
			OutFailReason = FText::Format( LOCTEXT( "ModuleWithInvalidLoadingPhase", "Module entry '{0}' specified an unrecognized module LoadingPhase '{1}'" ), FText::FromName(Name), FText::FromString(LoadingPhaseValue->AsString()) );
			return false;
		}
	}

	// Read the whitelisted and blacklisted platforms
	Object.TryGetStringArrayField(TEXT("WhitelistPlatforms"), WhitelistPlatforms);
	Object.TryGetStringArrayField(TEXT("BlacklistPlatforms"), BlacklistPlatforms);

	// Read the whitelisted and blacklisted targets
	Object.TryGetEnumArrayField(TEXT("WhitelistTargets"), WhitelistTargets);
	Object.TryGetEnumArrayField(TEXT("BlacklistTargets"), BlacklistTargets);

	// Read the whitelisted and blacklisted target configurations
	Object.TryGetEnumArrayField(TEXT("WhitelistTargetConfigurations"), WhitelistTargetConfigurations);
	Object.TryGetEnumArrayField(TEXT("BlacklistTargetConfigurations"), BlacklistTargetConfigurations);

	// Read the whitelisted and blacklisted programs
	Object.TryGetStringArrayField(TEXT("WhitelistPrograms"), WhitelistPrograms);
	Object.TryGetStringArrayField(TEXT("BlacklistPrograms"), BlacklistPrograms);

	// Read the additional dependencies
	Object.TryGetStringArrayField(TEXT("AdditionalDependencies"), AdditionalDependencies);

	return true;
}

bool FModuleDescriptor::ReadArray(const FJsonObject& Object, const TCHAR* Name, TArray<FModuleDescriptor>& OutModules, FText& OutFailReason)
{
	bool bResult = true;

	TSharedPtr<FJsonValue> ModulesArrayValue = Object.TryGetField(Name);
	if(ModulesArrayValue.IsValid() && ModulesArrayValue->Type == EJson::Array)
	{
		const TArray< TSharedPtr< FJsonValue > >& ModulesArray = ModulesArrayValue->AsArray();
		for(int Idx = 0; Idx < ModulesArray.Num(); Idx++)
		{
			const TSharedPtr<FJsonValue>& ModuleValue = ModulesArray[Idx];
			if(ModuleValue.IsValid() && ModuleValue->Type == EJson::Object)
			{
				FModuleDescriptor Descriptor;
				if(Descriptor.Read(*ModuleValue->AsObject().Get(), OutFailReason))
				{
					OutModules.Add(Descriptor);
				}
				else
				{
					bResult = false;
				}
			}
			else
			{
				OutFailReason = LOCTEXT( "ModuleWithInvalidModulesArray", "The 'Modules' array has invalid contents and was not able to be loaded." );
				bResult = false;
			}
		}
	}
	
	return bResult;
}

void FModuleDescriptor::Write(TJsonWriter<>& Writer) const
{
	Writer.WriteObjectStart();
	Writer.WriteValue(TEXT("Name"), Name.ToString());
	Writer.WriteValue(TEXT("Type"), FString(EHostType::ToString(Type)));
	Writer.WriteValue(TEXT("LoadingPhase"), FString(ELoadingPhase::ToString(LoadingPhase)));
	if (WhitelistPlatforms.Num() > 0)
	{
		Writer.WriteArrayStart(TEXT("WhitelistPlatforms"));
		for(int Idx = 0; Idx < WhitelistPlatforms.Num(); Idx++)
		{
			Writer.WriteValue(WhitelistPlatforms[Idx]);
		}
		Writer.WriteArrayEnd();
	}
	if (BlacklistPlatforms.Num() > 0)
	{
		Writer.WriteArrayStart(TEXT("BlacklistPlatforms"));
		for(int Idx = 0; Idx < BlacklistPlatforms.Num(); Idx++)
		{
			Writer.WriteValue(BlacklistPlatforms[Idx]);
		}
		Writer.WriteArrayEnd();
	}
	if (WhitelistTargets.Num() > 0)
	{
		Writer.WriteArrayStart(TEXT("WhitelistTargets"));
		for (int Idx = 0; Idx < WhitelistTargets.Num(); Idx++)
		{
			Writer.WriteValue(LexToString(WhitelistTargets[Idx]));
		}
		Writer.WriteArrayEnd();
	}
	if (BlacklistTargets.Num() > 0)
	{
		Writer.WriteArrayStart(TEXT("BlacklistTargets"));
		for (int Idx = 0; Idx < BlacklistTargets.Num(); Idx++)
		{
			Writer.WriteValue(LexToString(BlacklistTargets[Idx]));
		}
		Writer.WriteArrayEnd();
	}
	if (WhitelistTargetConfigurations.Num() > 0)
	{
		Writer.WriteArrayStart(TEXT("WhitelistTargetConfigurations"));
		for (int Idx = 0; Idx < WhitelistTargetConfigurations.Num(); Idx++)
		{
			Writer.WriteValue(LexToString(WhitelistTargetConfigurations[Idx]));
		}
		Writer.WriteArrayEnd();
	}
	if (BlacklistTargetConfigurations.Num() > 0)
	{
		Writer.WriteArrayStart(TEXT("BlacklistTargetConfigurations"));
		for (int Idx = 0; Idx < BlacklistTargetConfigurations.Num(); Idx++)
		{
			Writer.WriteValue(LexToString(BlacklistTargetConfigurations[Idx]));
		}
		Writer.WriteArrayEnd();
	}
	if (WhitelistPrograms.Num() > 0)
	{
		Writer.WriteArrayStart(TEXT("WhitelistPrograms"));
		for (int Idx = 0; Idx < WhitelistPrograms.Num(); Idx++)
		{
			Writer.WriteValue(WhitelistPrograms[Idx]);
		}
		Writer.WriteArrayEnd();
	}
	if (BlacklistPrograms.Num() > 0)
	{
		Writer.WriteArrayStart(TEXT("BlacklistPrograms"));
		for (int Idx = 0; Idx < BlacklistPrograms.Num(); Idx++)
		{
			Writer.WriteValue(BlacklistPrograms[Idx]);
		}
		Writer.WriteArrayEnd();
	}
	if (AdditionalDependencies.Num() > 0)
	{
		Writer.WriteArrayStart(TEXT("AdditionalDependencies"));
		for (int Idx = 0; Idx < AdditionalDependencies.Num(); Idx++)
		{
			Writer.WriteValue(AdditionalDependencies[Idx]);
		}
		Writer.WriteArrayEnd();
	}
	Writer.WriteObjectEnd();
}

void FModuleDescriptor::WriteArray(TJsonWriter<>& Writer, const TCHAR* Name, const TArray<FModuleDescriptor>& Modules)
{
	if(Modules.Num() > 0)
	{
		Writer.WriteArrayStart(Name);
		for(int Idx = 0; Idx < Modules.Num(); Idx++)
		{
			Modules[Idx].Write(Writer);
		}
		Writer.WriteArrayEnd();
	}
}

bool FModuleDescriptor::IsCompiledInConfiguration(const FString& Platform, EBuildConfiguration Configuration, const FString& TargetName, EBuildTargetType TargetType, bool bBuildDeveloperTools, bool bBuildRequiresCookedData) const
{
	// Check the platform is whitelisted
	if (WhitelistPlatforms.Num() > 0 && !WhitelistPlatforms.Contains(Platform))
	{
		return false;
	}

	// Check the platform is not blacklisted
	if (BlacklistPlatforms.Contains(Platform))
	{
		return false;
	}

	// Check the target is whitelisted
	if (WhitelistTargets.Num() > 0 && !WhitelistTargets.Contains(TargetType))
	{
		return false;
	}

	// Check the target is not blacklisted
	if (BlacklistTargets.Contains(TargetType))
	{
		return false;
	}

	// Check the target configuration is whitelisted
	if (WhitelistTargetConfigurations.Num() > 0 && !WhitelistTargetConfigurations.Contains(Configuration))
	{
		return false;
	}

	// Check the target configuration is not blacklisted
	if (BlacklistTargetConfigurations.Contains(Configuration))
	{
		return false;
	}

	// Special checks just for programs
	if(TargetType == EBuildTargetType::Program)
	{
		// Check the program name is whitelisted. Note that this behavior is slightly different to other whitelist/blacklist checks; we will whitelist a module of any type if it's explicitly allowed for this program.
		if(WhitelistPrograms.Num() > 0)
		{
			return WhitelistPrograms.Contains(TargetName);
		}
				
		// Check the program name is not blacklisted
		if(BlacklistPrograms.Contains(TargetName))
		{
			return false;
		}
	}

	// Check the module is compatible with this target.
	switch (Type)
	{
	case EHostType::Runtime:
	case EHostType::RuntimeNoCommandlet:
        return TargetType != EBuildTargetType::Program;
	case EHostType::RuntimeAndProgram:
		return true;
	case EHostType::CookedOnly:
        return bBuildRequiresCookedData;
	case EHostType::UncookedOnly:
		return !bBuildRequiresCookedData;
	case EHostType::Developer:
		return TargetType == EBuildTargetType::Editor || TargetType == EBuildTargetType::Program;
	case EHostType::DeveloperTool:
		return bBuildDeveloperTools;
	case EHostType::Editor:
	case EHostType::EditorNoCommandlet:
		return TargetType == EBuildTargetType::Editor;
	case EHostType::EditorAndProgram:
		return TargetType == EBuildTargetType::Editor || TargetType == EBuildTargetType::Program;
	case EHostType::Program:
		return TargetType == EBuildTargetType::Program;
    case EHostType::ServerOnly:
        return TargetType != EBuildTargetType::Program && TargetType != EBuildTargetType::Client;
    case EHostType::ClientOnly:
	case EHostType::ClientOnlyNoCommandlet:
        return TargetType != EBuildTargetType::Program && TargetType != EBuildTargetType::Server;
    }

	return false;
}

bool FModuleDescriptor::IsCompiledInCurrentConfiguration() const
{
	return IsCompiledInConfiguration(FPlatformMisc::GetUBTPlatform(), FApp::GetBuildConfiguration(), UE_APP_NAME, FApp::GetBuildTargetType(), !!WITH_UNREAL_DEVELOPER_TOOLS, FPlatformProperties::RequiresCookedData());
}

bool FModuleDescriptor::IsLoadedInCurrentConfiguration() const
{
	// Check that the module is built for this configuration
	if(!IsCompiledInCurrentConfiguration())
	{
		return false;
	}

	// Always respect the whitelist/blacklist for program targets
	EBuildTargetType TargetType = FApp::GetBuildTargetType();
	if(TargetType == EBuildTargetType::Program)
	{
		const FString TargetName = UE_APP_NAME;

		// Check the program name is whitelisted. Note that this behavior is slightly different to other whitelist/blacklist checks; we will whitelist a module of any type if it's explicitly allowed for this program.
		if(WhitelistPrograms.Num() > 0)
		{
			return WhitelistPrograms.Contains(TargetName);
		}
				
		// Check the program name is not blacklisted
		if(BlacklistPrograms.Contains(TargetName))
		{
			return false;
		}
	}

	// Check that the runtime environment allows it to be loaded
	switch (Type)
	{
	case EHostType::RuntimeAndProgram:
		#if (WITH_ENGINE || WITH_PLUGIN_SUPPORT)
			return true;
		#endif
		break;

	case EHostType::Runtime:
		#if (WITH_ENGINE || WITH_PLUGIN_SUPPORT) && !IS_PROGRAM
			return true;
		#endif
		break;
	
	case EHostType::RuntimeNoCommandlet:
		#if (WITH_ENGINE || WITH_PLUGIN_SUPPORT)  && !IS_PROGRAM
			if(!IsRunningCommandlet()) return true;
		#endif
		break;

	case EHostType::CookedOnly:
		return FPlatformProperties::RequiresCookedData();

	case EHostType::UncookedOnly:
		return !FPlatformProperties::RequiresCookedData();

	case EHostType::Developer:
		#if WITH_EDITOR || IS_PROGRAM
			return true;
		#else
			return false;
		#endif

	case EHostType::DeveloperTool:
		#if WITH_UNREAL_DEVELOPER_TOOLS
			return true;
		#else
			return false;
		#endif

	case EHostType::Editor:
		#if WITH_EDITOR
			if(GIsEditor) return true;
		#endif
		break;

	case EHostType::EditorNoCommandlet:
		#if WITH_EDITOR
			if(GIsEditor && !IsRunningCommandlet()) return true;
		#endif
		break;

	case EHostType::EditorAndProgram:
		#if WITH_EDITOR
			return GIsEditor;
		#elif IS_PROGRAM
			return true;
		#else
			return false;
		#endif

	case EHostType::Program:
		#if WITH_PLUGIN_SUPPORT && IS_PROGRAM
			return true;
		#endif
		break;

	case EHostType::ServerOnly:
		return !FPlatformProperties::IsClientOnly();

	case EHostType::ClientOnlyNoCommandlet:
#if (WITH_ENGINE || WITH_PLUGIN_SUPPORT)  && !IS_PROGRAM
		return (!IsRunningDedicatedServer()) && (!IsRunningCommandlet());
#endif
		// the fall in the case of not having defines listed above is intentional
	case EHostType::ClientOnly:
		return !IsRunningDedicatedServer();
	
	}
	return false;
}

void FModuleDescriptor::LoadModulesForPhase(ELoadingPhase::Type LoadingPhase, const TArray<FModuleDescriptor>& Modules, TMap<FName, EModuleLoadResult>& ModuleLoadErrors)
{
	FScopedSlowTask SlowTask(Modules.Num());
	for (int Idx = 0; Idx < Modules.Num(); Idx++)
	{
		SlowTask.EnterProgressFrame(1);
		const FModuleDescriptor& Descriptor = Modules[Idx];

		// Don't need to do anything if this module is already loaded
		if (!FModuleManager::Get().IsModuleLoaded(Descriptor.Name))
		{
			if (LoadingPhase == Descriptor.LoadingPhase && Descriptor.IsLoadedInCurrentConfiguration())
			{
				// @todo plugin: DLL search problems.  Plugins that statically depend on other modules within this plugin may not be found?  Need to test this.

				// NOTE: Loading this module may cause other modules to become loaded, both in the engine or game, or other modules 
				//       that are part of this project or plugin.  That's totally fine.
				EModuleLoadResult FailureReason;
				IModuleInterface* ModuleInterface = FModuleManager::Get().LoadModuleWithFailureReason(Descriptor.Name, FailureReason);
				if (ModuleInterface == nullptr)
				{
					// The module failed to load. Note this in the ModuleLoadErrors list.
					ModuleLoadErrors.Add(Descriptor.Name, FailureReason);
				}
			}
		}
	}
}

#if !IS_MONOLITHIC
bool FModuleDescriptor::CheckModuleCompatibility(const TArray<FModuleDescriptor>& Modules, TArray<FString>& OutIncompatibleFiles)
{
	FModuleManager& ModuleManager = FModuleManager::Get();

	bool bResult = true;
	for (const FModuleDescriptor& Module : Modules)
	{
		if (Module.IsCompiledInCurrentConfiguration() && !ModuleManager.IsModuleUpToDate(Module.Name))
		{
			OutIncompatibleFiles.Add(Module.Name.ToString());
			bResult = false;
		}
	}
	return bResult;
}
#endif

#undef LOCTEXT_NAMESPACE
