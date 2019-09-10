// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Common/TargetPlatformBase.h"
#include "HAL/IConsoleManager.h"
#include "DeviceBrowserDefaultPlatformWidgetCreator.h"
#include "Interfaces/IProjectBuildMutatorFeature.h"
#include "Features/IModularFeatures.h"
#include "Interfaces/IProjectManager.h"
#include "Interfaces/IPluginManager.h"
#include "ProjectDescriptor.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"

#define LOCTEXT_NAMESPACE "TargetPlatform"

bool FTargetPlatformBase::UsesForwardShading() const
{
	static IConsoleVariable* CVarForwardShading = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ForwardShading"));
	return CVarForwardShading ? (CVarForwardShading->GetInt() != 0) : false;
}

bool FTargetPlatformBase::UsesDBuffer() const
{
	static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DBuffer"));
	return CVar ? (CVar->GetInt() != 0) : false;
}

bool FTargetPlatformBase::UsesBasePassVelocity() const
{
	static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.BasePassOutputsVelocity"));
	return CVar ? (CVar->GetInt() != 0) : false;
}

bool FTargetPlatformBase::UsesSelectiveBasePassOutputs() const
{
	static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SelectiveBasePassOutputs"));
	return CVar ? (CVar->GetInt() != 0) : false;
}

bool FTargetPlatformBase::UsesDistanceFields() const
{
	return true;
}

float FTargetPlatformBase::GetDownSampleMeshDistanceFieldDivider() const
{
	return 1.0f;
}

static bool IsPluginEnabledForTarget(const IPlugin& Plugin, const FProjectDescriptor* Project, const FString& Platform, EBuildConfiguration Configuration, EBuildTargetType TargetType)
{
	if (!Plugin.GetDescriptor().SupportsTargetPlatform(Platform))
	{
		return false;
	}

	bool bEnabledForProject = Plugin.IsEnabledByDefault();
	if (Project != nullptr)
	{
		for(const FPluginReferenceDescriptor& PluginReference : Project->Plugins)
		{
			if (PluginReference.Name == Plugin.GetName() && !PluginReference.bOptional)
			{
				bEnabledForProject = PluginReference.IsEnabledForPlatform(Platform) && PluginReference.IsEnabledForTargetConfiguration(Configuration) && PluginReference.IsEnabledForTarget(TargetType);
			}
		}
	}
	return bEnabledForProject;
}

static bool IsPluginCompiledForTarget(const IPlugin& Plugin, const FProjectDescriptor* Project, const FString& Platform, EBuildConfiguration Configuration, EBuildTargetType TargetType, bool bRequiresCookedData)
{
	bool bCompiledForTarget = false;
	if (IsPluginEnabledForTarget(Plugin, Project, Platform, Configuration, TargetType))
	{
		bool bBuildDeveloperTools = (TargetType == EBuildTargetType::Editor || TargetType == EBuildTargetType::Program || (Configuration != EBuildConfiguration::Test && Configuration != EBuildConfiguration::Shipping));
		for (const FModuleDescriptor& Module : Plugin.GetDescriptor().Modules)
		{
			if (Module.IsCompiledInConfiguration(Platform, Configuration, TEXT(""), TargetType, bBuildDeveloperTools, bRequiresCookedData))
			{
				bCompiledForTarget = true;
				break;
			}
		}
	}
	return bCompiledForTarget;
}

bool FTargetPlatformBase::RequiresTempTarget(bool bProjectHasCode, EBuildConfiguration Configuration, bool bRequiresAssetNativization, FText& OutReason) const
{
	// check to see if we already have a Target.cs file
	if (bProjectHasCode)
	{
		return false;
	}

	// check if asset nativization is enabled
	if (bRequiresAssetNativization)
    {
		OutReason = LOCTEXT("TempTarget_Nativization", "asset nativization is enabled");
        return true;
    }

	// check to see if any projectmutator modular features are available
	for (IProjectBuildMutatorFeature* Feature : IModularFeatures::Get().GetModularFeatureImplementations<IProjectBuildMutatorFeature>(PROJECT_BUILD_MUTATOR_FEATURE))
	{
		if (Feature->RequiresProjectBuild(PlatformInfo->PlatformInfoName, OutReason))
		{
			return true;
		}
	}

	// check the target platforms for any differences in build settings or additional plugins
	const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject();
	if (!FApp::IsEngineInstalled() && !HasDefaultBuildSettings())
	{
		OutReason = LOCTEXT("TempTarget_NonDefaultBuildConfig", "project has non-default build configuration");
		return true;
	}

	// find if there are any plugins enabled or disabled which differ from the default
	for(const TSharedRef<IPlugin>& Plugin: IPluginManager::Get().GetDiscoveredPlugins())
	{
		const FString Platform = PlatformInfo->UBTTargetId.ToString();

		bool bCompiledForTarget = IsPluginCompiledForTarget(*Plugin, Project, Platform, Configuration, PlatformInfo->PlatformType, RequiresCookedData());

		bool bCompiledForBaseTarget = false;
		if (!Plugin->GetDescriptor().bInstalled)
		{
			bCompiledForBaseTarget |= IsPluginCompiledForTarget(*Plugin, nullptr, Platform, Configuration, PlatformInfo->PlatformType, RequiresCookedData());
		}

		if (bCompiledForTarget != bCompiledForBaseTarget)
		{
			if (bCompiledForTarget)
			{
				OutReason = FText::Format(LOCTEXT("TempTarget_PluginEnabled", "{0} plugin is enabled"), FText::FromString(Plugin->GetName()));
				return true;
			}
			else
			{
				OutReason = FText::Format(LOCTEXT("TempTarget_PluginDisabled", "{0} plugin is disabled"), FText::FromString(Plugin->GetName()));
				return true;
			}
		}
	}
	return false;
}

TSharedPtr<IDeviceManagerCustomPlatformWidgetCreator> FTargetPlatformBase::GetCustomWidgetCreator() const
{
	static TSharedPtr<FDeviceBrowserDefaultPlatformWidgetCreator> DefaultWidgetCreator = MakeShared<FDeviceBrowserDefaultPlatformWidgetCreator>();
	return DefaultWidgetCreator;
}

bool FTargetPlatformBase::HasDefaultBuildSettings() const
{
	// first check default build settings for all platforms
	TArray<FString> BoolKeys, IntKeys, StringKeys, BuildKeys;
	BuildKeys.Add(TEXT("bCompileApex")); 
	BuildKeys.Add(TEXT("bCompileICU"));
	BuildKeys.Add(TEXT("bCompileSimplygon")); 
	BuildKeys.Add(TEXT("bCompileSimplygonSSF"));
	BuildKeys.Add(TEXT("bCompileRecast")); 
	BuildKeys.Add(TEXT("bCompileSpeedTree"));
	BuildKeys.Add(TEXT("bCompileWithPluginSupport")); 
	BuildKeys.Add(TEXT("bCompilePhysXVehicle")); 
	BuildKeys.Add(TEXT("bCompileFreeType"));
	BuildKeys.Add(TEXT("bCompileForSize"));	
	BuildKeys.Add(TEXT("bCompileCEF3")); 
	BuildKeys.Add(TEXT("bCompileCustomSQLitePlatform"));

	const PlatformInfo::FPlatformInfo& PlatInfo = GetPlatformInfo();
	if (!DoProjectSettingsMatchDefault(PlatInfo.TargetPlatformName.ToString(), TEXT("/Script/BuildSettings.BuildSettings"), &BuildKeys, nullptr, nullptr))
	{
		return false;
	}

	FString PlatformSection;
	GetBuildProjectSettingKeys(PlatformSection, BoolKeys, IntKeys, StringKeys);

	if(!DoProjectSettingsMatchDefault(PlatInfo.TargetPlatformName.ToString(), PlatformSection, &BoolKeys, &IntKeys, &StringKeys))
	{
		return false;
	}

	return true;
}

bool FTargetPlatformBase::DoProjectSettingsMatchDefault(const FString& InPlatformName, const FString& InSection, const TArray<FString>* InBoolKeys, const TArray<FString>* InIntKeys, const TArray<FString>* InStringKeys)
{
	FConfigFile ProjIni;
	FConfigFile DefaultIni;
	FConfigCacheIni::LoadLocalIniFile(ProjIni, TEXT("Engine"), true, *InPlatformName, true);
	FConfigCacheIni::LoadExternalIniFile(DefaultIni, TEXT("Engine"), *FPaths::EngineConfigDir(), *FPaths::EngineConfigDir(), true, NULL, true);

	if (InBoolKeys != NULL)
	{
		for (int Index = 0; Index < InBoolKeys->Num(); ++Index)
		{
			FString Default(TEXT("False")), Project(TEXT("False"));
			DefaultIni.GetString(*InSection, *((*InBoolKeys)[Index]), Default);
			ProjIni.GetString(*InSection, *((*InBoolKeys)[Index]), Project);
			if (Default.Compare(Project, ESearchCase::IgnoreCase))
			{
				return false;
			}
		}
	}

	if (InIntKeys != NULL)
	{
		for (int Index = 0; Index < InIntKeys->Num(); ++Index)
		{
			int64 Default(0), Project(0);
			DefaultIni.GetInt64(*InSection, *((*InIntKeys)[Index]), Default);
			ProjIni.GetInt64(*InSection, *((*InIntKeys)[Index]), Project);
			if (Default != Project)
			{
				return false;
			}
		}
	}

	if (InStringKeys != NULL)
	{
		for (int Index = 0; Index < InStringKeys->Num(); ++Index)
		{
			FString Default(TEXT("False")), Project(TEXT("False"));
			DefaultIni.GetString(*InSection, *((*InStringKeys)[Index]), Default);
			ProjIni.GetString(*InSection, *((*InStringKeys)[Index]), Project);
			if (Default.Compare(Project, ESearchCase::IgnoreCase))
			{
				return false;
			}
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
