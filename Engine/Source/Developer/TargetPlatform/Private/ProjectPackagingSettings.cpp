// Copyright Epic Games, Inc. All Rights Reserved.

#include "Settings/ProjectPackagingSettings.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Interfaces/IProjectManager.h"
#include "DesktopPlatformModule.h"
#include "Common/TargetPlatformBase.h"

#if WITH_EDITOR
#include "Engine/Blueprint.h"
#endif

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

	// Cache the current set of Blueprint assets selected for nativization.
	CachedNativizeBlueprintAssets = NativizeBlueprintAssets;

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
	else if (Name == FName((TEXT("NativizeBlueprintAssets"))))
	{
		int32 AssetIndex;
		auto OnSelectBlueprintForExclusiveNativizationLambda = [](const FString& PackageName, bool bSelect)
		{
			if (!PackageName.IsEmpty())
			{
				// This should only apply to loaded packages. Any unloaded packages defer setting the transient flag to when they're loaded.
				if (UPackage* Package = FindPackage(nullptr, *PackageName))
				{
					// Find the Blueprint asset within the package.
					if (UBlueprint* Blueprint = FindObject<UBlueprint>(Package, *FPaths::GetBaseFilename(PackageName)))
					{
						// We're toggling the transient flag on or off.
						if ((Blueprint->NativizationFlag == EBlueprintNativizationFlag::ExplicitlyEnabled) != bSelect)
						{
							Blueprint->NativizationFlag = bSelect ? EBlueprintNativizationFlag::ExplicitlyEnabled : EBlueprintNativizationFlag::Disabled;
						}
					}
				}
			}
		};

		if (NativizeBlueprintAssets.Num() > 0)
		{
			for (AssetIndex = 0; AssetIndex < NativizeBlueprintAssets.Num(); ++AssetIndex)
			{
				const FString& PackageName = NativizeBlueprintAssets[AssetIndex].FilePath;
				if (AssetIndex >= CachedNativizeBlueprintAssets.Num())
				{
					// A new entry was added; toggle the exclusive flag on the corresponding Blueprint asset (if loaded).
					OnSelectBlueprintForExclusiveNativizationLambda(PackageName, true);

					// Add an entry to the end of the cached list.
					CachedNativizeBlueprintAssets.Add(NativizeBlueprintAssets[AssetIndex]);
				}
				else if (!PackageName.Equals(CachedNativizeBlueprintAssets[AssetIndex].FilePath))
				{
					if (NativizeBlueprintAssets.Num() < CachedNativizeBlueprintAssets.Num())
					{
						// An entry was removed; toggle the exclusive flag on the corresponding Blueprint asset (if loaded).
						OnSelectBlueprintForExclusiveNativizationLambda(CachedNativizeBlueprintAssets[AssetIndex].FilePath, false);

						// Remove this entry from the cached list.
						CachedNativizeBlueprintAssets.RemoveAt(AssetIndex);
					}
					else if (NativizeBlueprintAssets.Num() > CachedNativizeBlueprintAssets.Num())
					{
						// A new entry was inserted; toggle the exclusive flag on the corresponding Blueprint asset (if loaded).
						OnSelectBlueprintForExclusiveNativizationLambda(PackageName, true);

						// Insert the new entry into the cached list.
						CachedNativizeBlueprintAssets.Insert(NativizeBlueprintAssets[AssetIndex], AssetIndex);
					}
					else
					{
						// An entry was changed; toggle the exclusive flag on the corresponding Blueprint assets (if loaded).
						OnSelectBlueprintForExclusiveNativizationLambda(CachedNativizeBlueprintAssets[AssetIndex].FilePath, false);
						OnSelectBlueprintForExclusiveNativizationLambda(PackageName, true);

						// Update the cached entry.
						CachedNativizeBlueprintAssets[AssetIndex].FilePath = PackageName;
					}
				}
			}

			if (CachedNativizeBlueprintAssets.Num() > NativizeBlueprintAssets.Num())
			{
				// Removed entries at the end of the list; toggle the exclusive flag on the corresponding Blueprint asset(s) (if loaded).
				for (AssetIndex = NativizeBlueprintAssets.Num(); AssetIndex < CachedNativizeBlueprintAssets.Num(); ++AssetIndex)
				{
					OnSelectBlueprintForExclusiveNativizationLambda(CachedNativizeBlueprintAssets[AssetIndex].FilePath, false);
				}

				// Remove entries from the end of the cached list.
				CachedNativizeBlueprintAssets.RemoveAt(NativizeBlueprintAssets.Num(), CachedNativizeBlueprintAssets.Num() - NativizeBlueprintAssets.Num());
			}
		}
		else if (CachedNativizeBlueprintAssets.Num() > 0)
		{
			// Removed all entries; toggle the exclusive flag on the corresponding Blueprint asset(s) (if loaded).
			for (AssetIndex = 0; AssetIndex < CachedNativizeBlueprintAssets.Num(); ++AssetIndex)
			{
				OnSelectBlueprintForExclusiveNativizationLambda(CachedNativizeBlueprintAssets[AssetIndex].FilePath, false);
			}

			// Clear the cached list.
			CachedNativizeBlueprintAssets.Empty();
		}
	}
}

bool UProjectPackagingSettings::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty->GetFName() == FName(TEXT("NativizeBlueprintAssets")))
	{
		return BlueprintNativizationMethod == EProjectPackagingBlueprintNativizationMethod::Exclusive;
	}

	return Super::CanEditChange(InProperty);
}


bool UProjectPackagingSettings::AddBlueprintAssetToNativizationList(const class UBlueprint* InBlueprint)
{
	if (InBlueprint)
	{
		const FString PackageName = InBlueprint->GetOutermost()->GetName();

		// Make sure it's not already in the exclusive list. This can happen if the user previously added this asset in the Project Settings editor.
		const bool bFound = IsBlueprintAssetInNativizationList(InBlueprint);
		if (!bFound)
		{
			// Add this Blueprint asset to the exclusive list.
			FFilePath FileInfo;
			FileInfo.FilePath = PackageName;
			NativizeBlueprintAssets.Add(FileInfo);

			// Also add it to the mirrored list for tracking edits.
			CachedNativizeBlueprintAssets.Add(FileInfo);

			return true;
		}
	}

	return false;
}

bool UProjectPackagingSettings::RemoveBlueprintAssetFromNativizationList(const class UBlueprint* InBlueprint)
{
	if (InBlueprint)
	{
		const FString PackageName = InBlueprint->GetOutermost()->GetName();

		int32 AssetIndex = FindBlueprintInNativizationList(InBlueprint);
		if (AssetIndex >= 0)
		{
			// Note: Intentionally not using RemoveAtSwap() here, so that the order is preserved.
			NativizeBlueprintAssets.RemoveAt(AssetIndex);

			// Also remove it from the mirrored list (for tracking edits).
			CachedNativizeBlueprintAssets.RemoveAt(AssetIndex);

			return true;
		}
	}

	return false;
}

int32 UProjectPackagingSettings::FindBlueprintInNativizationList(const UBlueprint* InBlueprint) const
{
	int32 ListIndex = INDEX_NONE;
	if (InBlueprint)
	{
		const FString PackageName = InBlueprint->GetOutermost()->GetName();

		for (int32 AssetIndex = 0; AssetIndex < NativizeBlueprintAssets.Num(); ++AssetIndex)
		{
			if (NativizeBlueprintAssets[AssetIndex].FilePath.Equals(PackageName, ESearchCase::IgnoreCase))
			{
				ListIndex = AssetIndex;
				break;
			}
		}
	}
	return ListIndex;
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

	return Value == nullptr ? EProjectPackagingBuildConfigurations::PPBC_Development : *Value;
}

void UProjectPackagingSettings::SetBuildConfigurationForPlatform(FName PlatformName, EProjectPackagingBuildConfigurations Configuration)
{
	PerPlatformBuildConfig.Add(PlatformName, Configuration);
}

FName UProjectPackagingSettings::GetTargetPlatformForPlatform(FName PlatformName) const
{
	const FName* Value = PerPlatformTargetPlatformName.Find(PlatformName);

	// the platform name is also the name of the vanilla info
	return Value == nullptr ? PlatformName : *Value;
}

void UProjectPackagingSettings::SetTargetPlatformForPlatform(FName PlatformName, FName TargetPlatformName)
{
	PerPlatformTargetPlatformName.Add(PlatformName, TargetPlatformName);
}

#undef LOCTEXT_NAMESPACE