// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprints/DisplayClusterBlueprint.h"
#include "Blueprints/DisplayClusterBlueprintGeneratedClass.h"
#include "DisplayClusterRootActor.h"
#include "IDisplayClusterConfiguration.h"
#include "Misc/DisplayClusterLog.h"


UDisplayClusterBlueprint::UDisplayClusterBlueprint()
	: ConfigData(nullptr), AssetVersion(0)
{
	BlueprintType = BPTYPE_Normal;
}

#if WITH_EDITOR

UClass* UDisplayClusterBlueprint::GetBlueprintClass() const
{
	return UDisplayClusterBlueprintGeneratedClass::StaticClass();
}

void UDisplayClusterBlueprint::GetReparentingRules(TSet<const UClass*>& AllowedChildrenOfClasses,
	TSet<const UClass*>& DisallowedChildrenOfClasses) const
{
	AllowedChildrenOfClasses.Add(ADisplayClusterRootActor::StaticClass());
}

#endif

UDisplayClusterBlueprintGeneratedClass* UDisplayClusterBlueprint::GetGeneratedClass() const
{
	return Cast<UDisplayClusterBlueprintGeneratedClass>(*GeneratedClass);
}

UDisplayClusterConfigurationData* UDisplayClusterBlueprint::GetOrLoadConfig()
{
	if (ConfigData)
	{
		return ConfigData;
	}
	
	if (!ensure(PathToConfig.IsEmpty()))
	{
		UE_LOG(LogDisplayClusterBlueprint, Error, TEXT("GetOrLoadConfig - ConfigData object not exported and no PathToConfig set."));
		return nullptr;
	}
	
	ConfigData = IDisplayClusterConfiguration::Get().LoadConfig(PathToConfig);
	return ConfigData;
}

void UDisplayClusterBlueprint::SetConfigData(UDisplayClusterConfigurationData* InConfigData)
{
#if WITH_EDITOR
	Modify();
#endif
	
	if (InConfigData)
	{
		InConfigData->Rename(nullptr, this, REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
		InConfigData->SetFlags(RF_Public);
	}

	if (ConfigData && ConfigData != InConfigData)
	{
		// Makes sure the old data won't be exported and the rename will call modify. Probably not necessary.
		ConfigData->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
	}
	
	ConfigData = InConfigData;

#if WITH_EDITORONLY_DATA
	PathToConfig = InConfigData ? InConfigData->PathToConfig : "";
#endif
}

void UDisplayClusterBlueprint::SetConfigPath(const FString& InPath)
{
	PathToConfig = InPath;
}
