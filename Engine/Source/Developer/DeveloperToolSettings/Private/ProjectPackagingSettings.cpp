// Copyright Epic Games, Inc. All Rights Reserved.

#include "Settings/ProjectPackagingSettings.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Interfaces/IProjectManager.h"
#include "DesktopPlatformModule.h"
#include "DeveloperToolSettingsDelegates.h"

#define LOCTEXT_NAMESPACE "SettingsClasses"


/* UProjectPackagingSettings interface
 *****************************************************************************/

const UProjectPackagingSettings::FConfigurationInfo UProjectPackagingSettings::ConfigurationInfo[(int)EProjectPackagingBuildConfigurations::PPBC_MAX] =
{
	/* PPBC_Debug */         { EBuildConfiguration::Debug, LOCTEXT("DebugConfiguration", "Debug"), LOCTEXT("DebugConfigurationTooltip", "Package the game in Debug configuration") },
	/* PPBC_DebugGame */     { EBuildConfiguration::DebugGame, LOCTEXT("DebugGameConfiguration", "DebugGame"), LOCTEXT("DebugGameConfigurationTooltip", "Package the game in DebugGame configuration") },
	/* PPBC_Development */   { EBuildConfiguration::Development, LOCTEXT("DevelopmentConfiguration", "Development"), LOCTEXT("DevelopmentConfigurationTooltip", "Package the game in Development configuration") },
	/* PPBC_Test */          { EBuildConfiguration::Test, LOCTEXT("TestConfiguration", "Test"), LOCTEXT("TestConfigurationTooltip", "Package the game in Test configuration") },
	/* PPBC_Shipping */      { EBuildConfiguration::Shipping, LOCTEXT("ShippingConfiguration", "Shipping"), LOCTEXT("ShippingConfigurationTooltip", "Package the game in Shipping configuration") },
};

UProjectPackagingSettings::UProjectPackagingSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}


void UProjectPackagingSettings::PostInitProperties()
{
	// Build code projects by default
	Build = EProjectPackagingBuild::IfProjectHasCode;

	FixCookingPaths();

	Super::PostInitProperties();
}

void UProjectPackagingSettings::FixCookingPaths()
{
	// Fix AlwaysCook/NeverCook paths to use content root
	for (FDirectoryPath& PathToFix : DirectoriesToAlwaysCook)
	{
		if (!PathToFix.Path.IsEmpty() && !PathToFix.Path.StartsWith(TEXT("/"), ESearchCase::CaseSensitive))
		{
			PathToFix.Path = FString::Printf(TEXT("/Game/%s"), *PathToFix.Path);
		}
	}

	for (FDirectoryPath& PathToFix : DirectoriesToNeverCook)
	{
		if (!PathToFix.Path.IsEmpty() && !PathToFix.Path.StartsWith(TEXT("/"), ESearchCase::CaseSensitive))
		{
			PathToFix.Path = FString::Printf(TEXT("/Game/%s"), *PathToFix.Path);
		}
	}

	for (FDirectoryPath& PathToFix : TestDirectoriesToNotSearch)
	{
		if (!PathToFix.Path.IsEmpty() && !PathToFix.Path.StartsWith(TEXT("/"), ESearchCase::CaseSensitive))
		{
			PathToFix.Path = FString::Printf(TEXT("/Game/%s"), *PathToFix.Path);
		}
	}
}

#if WITH_EDITOR
void UProjectPackagingSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName Name = (PropertyChangedEvent.MemberProperty != nullptr)
		? PropertyChangedEvent.MemberProperty->GetFName()
		: NAME_None;

	if (Name == FName(TEXT("DirectoriesToAlwaysCook")) || Name == FName(TEXT("DirectoriesToNeverCook")) || Name == FName(TEXT("TestDirectoriesToNotSearch")) || Name == NAME_None)
	{
		// We need to fix paths for no name updates to catch the reloadconfig call
		FixCookingPaths();
	}
	else if (Name == FName((TEXT("StagingDirectory"))))
	{
		// fix up path
		FString Path = StagingDirectory.Path;
		FPaths::MakePathRelativeTo(Path, FPlatformProcess::BaseDir());
		StagingDirectory.Path = Path;
	}
	else if (Name == FName(TEXT("ForDistribution")))
	{
		if (ForDistribution && BuildConfiguration != EProjectPackagingBuildConfigurations::PPBC_Shipping)
		{
			BuildConfiguration = EProjectPackagingBuildConfigurations::PPBC_Shipping;
			// force serialization for "Build COnfiguration"
			UpdateSinglePropertyInConfigFile(GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UProjectPackagingSettings, BuildConfiguration)), GetDefaultConfigFilename());
		}
	}
	else if (Name == FName(TEXT("bGenerateChunks")))
	{
		if (bGenerateChunks)
		{
			UsePakFile = true;
		}
	}
	else if (Name == FName(TEXT("UsePakFile")))
	{
		if (!UsePakFile)
		{
			bGenerateChunks = false;
			bBuildHttpChunkInstallData = false;
		}
	}
	else if (Name == FName(TEXT("bBuildHTTPChunkInstallData")))
	{
		if (bBuildHttpChunkInstallData)
		{
			UsePakFile = true;
			bGenerateChunks = true;
			//Ensure data is something valid
			if (HttpChunkInstallDataDirectory.Path.IsEmpty())
			{
				auto CloudInstallDir = FPaths::ConvertRelativePathToFull(FPaths::GetPath(FPaths::GetProjectFilePath())) / TEXT("ChunkInstall");
				HttpChunkInstallDataDirectory.Path = CloudInstallDir;
			}
			if (HttpChunkInstallDataVersion.IsEmpty())
			{
				HttpChunkInstallDataVersion = TEXT("release1");
			}
		}
	}
	else if (Name == FName((TEXT("ApplocalPrerequisitesDirectory"))))
	{
		// If a variable is already in use, assume the user knows what they are doing and don't modify the path
		if (!ApplocalPrerequisitesDirectory.Path.Contains("$("))
		{
			// Try making the path local to either project or engine directories.
			FString EngineRootedPath = ApplocalPrerequisitesDirectory.Path;
			FString EnginePath = FPaths::ConvertRelativePathToFull(FPaths::GetPath(FPaths::EngineDir())) + "/";
			FPaths::MakePathRelativeTo(EngineRootedPath, *EnginePath);
			if (FPaths::IsRelative(EngineRootedPath))
			{
				ApplocalPrerequisitesDirectory.Path = "$(EngineDir)/" + EngineRootedPath;
				return;
			}

			FString ProjectRootedPath = ApplocalPrerequisitesDirectory.Path;
			FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetPath(FPaths::GetProjectFilePath())) + "/";
			FPaths::MakePathRelativeTo(ProjectRootedPath, *ProjectPath);
			if (FPaths::IsRelative(EngineRootedPath))
			{
				ApplocalPrerequisitesDirectory.Path = "$(ProjectDir)/" + ProjectRootedPath;
				return;
			}
		}
	}
}

bool UProjectPackagingSettings::CanEditChange(const FProperty* InProperty) const
{
	return Super::CanEditChange(InProperty);
}
#endif

TArray<EProjectPackagingBuildConfigurations> UProjectPackagingSettings::GetValidPackageConfigurations()
{
	// Check if the project has code
	FProjectStatus ProjectStatus;
	bool bHasCode = IProjectManager::Get().QueryStatusForCurrentProject(ProjectStatus) && ProjectStatus.bCodeBasedProject;

	// If if does, find all the targets
	const TArray<FTargetInfo>* Targets = nullptr;
	if (bHasCode)
	{
		Targets = &(FDesktopPlatformModule::Get()->GetTargetsForCurrentProject());
	}

	// Set up all the configurations
	TArray<EProjectPackagingBuildConfigurations> Configurations;
	for (int32 Idx = 0; Idx < (int)EProjectPackagingBuildConfigurations::PPBC_MAX; Idx++)
	{
		EProjectPackagingBuildConfigurations PackagingConfiguration = (EProjectPackagingBuildConfigurations)Idx;

		// Check the target type is valid
		const UProjectPackagingSettings::FConfigurationInfo& Info = UProjectPackagingSettings::ConfigurationInfo[Idx];
		if (!bHasCode && Info.Configuration == EBuildConfiguration::DebugGame)
		{
			continue;
		}

		Configurations.Add(PackagingConfiguration);
	}
	return Configurations;
}

const FTargetInfo* UProjectPackagingSettings::GetBuildTargetInfo() const
{
	const FTargetInfo* DefaultGameTarget = nullptr;
	const FTargetInfo* DefaultClientTarget = nullptr;
	for (const FTargetInfo& Target : FDesktopPlatformModule::Get()->GetTargetsForCurrentProject())
	{
		if (Target.Name == BuildTarget)
		{
			return &Target;
		}
		else if (Target.Type == EBuildTargetType::Game && (DefaultGameTarget == nullptr || Target.Name < DefaultGameTarget->Name))
		{
			DefaultGameTarget = &Target;
		}
		else if (Target.Type == EBuildTargetType::Client && (DefaultClientTarget == nullptr || Target.Name < DefaultClientTarget->Name))
		{
			DefaultClientTarget = &Target;
		}
	}
	return (DefaultGameTarget != nullptr) ? DefaultGameTarget : DefaultClientTarget;
}

EProjectPackagingBuildConfigurations UProjectPackagingSettings::GetBuildConfigurationForPlatform(FName PlatformName) const
{
	const EProjectPackagingBuildConfigurations* Value = PerPlatformBuildConfig.Find(PlatformName);

	// PPBC_MAX defines the default project setting case and should be handled accordingly.
	return Value == nullptr ? EProjectPackagingBuildConfigurations::PPBC_MAX : *Value;
}

void UProjectPackagingSettings::SetBuildConfigurationForPlatform(FName PlatformName, EProjectPackagingBuildConfigurations Configuration)
{
	PerPlatformBuildConfig.Add(PlatformName, Configuration);
}

void UProjectPackagingSettings::SetBuildConfigurationForAllPlatforms(EProjectPackagingBuildConfigurations Configuration)
{
	for (auto& Config : PerPlatformBuildConfig)
	{
		Config.Value = Configuration;
	}
}

FName UProjectPackagingSettings::GetTargetFlavorForPlatform(FName FlavorName) const
{
	const FName* Value = PerPlatformTargetFlavorName.Find(FlavorName);

	// the flavor name is also the name of the vanilla info
	return Value == nullptr ? FlavorName : *Value;
}

void UProjectPackagingSettings::SetTargetFlavorForPlatform(FName PlatformName, FName TargetFlavorName)
{
	PerPlatformTargetFlavorName.Add(PlatformName, TargetFlavorName);
}

FString UProjectPackagingSettings::GetBuildTargetForPlatform(FName PlatformName) const
{
	const FString* Value = PerPlatformBuildTarget.Find(PlatformName);

	// empty string defines the default project setting case and should be handled accordingly.
	return Value == nullptr ? "" : *Value;
}

void UProjectPackagingSettings::SetBuildTargetForPlatform(FName PlatformName, FString BuildTargetName)
{
	PerPlatformBuildTarget.Add(PlatformName, BuildTargetName);
}

#undef LOCTEXT_NAMESPACE