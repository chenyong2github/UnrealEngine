// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraEditorSettings.h"
#include "NiagaraConstants.h"

FNiagaraNamespaceMetadata::FNiagaraNamespaceMetadata()
{
}

FNiagaraNamespaceMetadata::FNiagaraNamespaceMetadata(TArray<FName> InNamespaces, FText InDisplayName, FText InDescription, FLinearColor InBackgroundColor, TArray<ENiagaraNamespaceMetadataOptions> InOptions)
	: Namespaces(InNamespaces)
	, DisplayName(InDisplayName)
	, Description(InDescription)
	, BackgroundColor(InBackgroundColor)
	, Options(InOptions)
{
}

UNiagaraEditorSettings::UNiagaraEditorSettings(const FObjectInitializer& ObjectInitlaizer)
	: Super(ObjectInitlaizer)
{
	bAutoCompile = true;
	bAutoPlay = true;
	bResetSimulationOnChange = true;
	bResimulateOnChangeWhilePaused = true;
	bResetDependentSystemsWhenEditingEmitters = false;

	NamespaceMetadata = 
	{
		FNiagaraNamespaceMetadata(
			{NAME_None},
			NSLOCTEXT("NamespaceMetadata", "DefaultDisplayName", "None"),
			NSLOCTEXT("NamespaceMetadata", "DefaultDescription", "Arbitrary sub-namespace for specifying module specific dataset attributes, or calling nested modules."),
			FLinearColor(FColor(102, 102, 102)),
			{ }),
		FNiagaraNamespaceMetadata(
			{FNiagaraConstants::SystemNamespace},
			NSLOCTEXT("NamespaceMetadata", "SystemDisplayName", "System"),
			NSLOCTEXT("NamespaceMetadata", "SystemDescription", "Persistent attribute in the system which is written in a system\n stage and can be read anywhere."),
			FLinearColor(FColor(67, 105, 124)),
			{ENiagaraNamespaceMetadataOptions::SupportsSubnamespace}),
		FNiagaraNamespaceMetadata(
			{FNiagaraConstants::EmitterNamespace},
			NSLOCTEXT("NamespaceMetadata", "EmitterDisplayName", "Emitter"),
			NSLOCTEXT("NamespaceMetadata", "EmitterDescription", "Persistent attribute which is written in a emitter\n stage and can be read in emitter and particle stages."),
			FLinearColor(FColor(126, 87, 67)),
			{ENiagaraNamespaceMetadataOptions::SupportsSubnamespace}),
		FNiagaraNamespaceMetadata(
			{FNiagaraConstants::ParticleAttributeNamespace},
			NSLOCTEXT("NamespaceMetadata", "ParticleDisplayName", "Particles"),
			NSLOCTEXT("NamespaceMetadata", "ParticleDescription", "Persistent attribute which is written in a particle\n stage and can be read in particle stages."),
			FLinearColor(FColor(87, 107, 61)),
			{ENiagaraNamespaceMetadataOptions::SupportsSubnamespace}),
		FNiagaraNamespaceMetadata(
			{FNiagaraConstants::ModuleNamespace},
			NSLOCTEXT("NamespaceMetadata", "ModuleDisplayName", "Input"),
			NSLOCTEXT("NamespaceMetadata", "ModuleDescription", "A transient value which exposes a module input to the system and emitter editor."),
			FLinearColor(FColor(118, 85, 48)),
			{ENiagaraNamespaceMetadataOptions::SupportsSubnamespace}),
		FNiagaraNamespaceMetadata(
			{FNiagaraConstants::OutputNamespace, FNiagaraConstants::ModuleNamespace},
			NSLOCTEXT("NamespaceMetadata", "ModuleOutputDisplayName", "Output"),
			NSLOCTEXT("NamespaceMetadata", "ModuleOutputDescription", "A transient value which is unique to the module that wrote it and can be read from any other module.\nTransient values do not persist from frame to frame, or between stages, e.g. emitter to particle, or spawn to update."),
			FLinearColor(FColor(118, 85, 48)),
			{ENiagaraNamespaceMetadataOptions::Advanced}),
		FNiagaraNamespaceMetadata(
			{FNiagaraConstants::LocalNamespace, FNiagaraConstants::ModuleNamespace},
			NSLOCTEXT("NamespaceMetadata", "ModuleLocalDisplayName", "Local"),
			NSLOCTEXT("NamespaceMetadata", "ModuleLocalDescription", "A transient value which can be written to and read from within a single module.\nTransient values do not persist from frame to frame, or between stages, e.g. emitter to particle, or spawn to update."),
			FLinearColor(FColor(118, 85, 48)),
			{ENiagaraNamespaceMetadataOptions::Advanced}),
		FNiagaraNamespaceMetadata(
			{FNiagaraConstants::TransientNamespace},
			NSLOCTEXT("NamespaceMetadata", "TransientDisplayName", "Transient"),
			NSLOCTEXT("NamespaceMetadata", "TransientDescription", "A transient value which can be written to and read from from any module.\nTransient values do not persist from frame to frame, or between stages, e.g. emitter to particle, or spawn to update."),
			FLinearColor(FColor(179, 122, 45)),
			{ENiagaraNamespaceMetadataOptions::Advanced}),
		FNiagaraNamespaceMetadata(
			{FNiagaraConstants::EngineNamespace},
			NSLOCTEXT("NamespaceMetadata", "EngineDisplayName", "Engine"),
			NSLOCTEXT("NamespaceMetadata", "EngineDescription", "A read only value which is provided by the engine.\nThis value's source can be the simulation itsef\ne.g. ExecutionCount, or the owner of the simulation e.g. (Owner) Scale."),
			FLinearColor(FColor(40, 40, 40)),
			{ENiagaraNamespaceMetadataOptions::PreventRenaming}),
		FNiagaraNamespaceMetadata(
			{FNiagaraConstants::UserNamespace},
			NSLOCTEXT("NamespaceMetadata", "UserDisplayName", "User"),
			NSLOCTEXT("NamespaceMetadata", "UserDescription", "A read only value which can be initialized per system and\nmodified externally in the level, by blueprint, or by c++."),
			FLinearColor(FColor(67, 105, 124)),
			{ }),
		FNiagaraNamespaceMetadata(
			{FNiagaraConstants::DataInstanceNamespace},
			NSLOCTEXT("NamespaceMetadata", "DataInstanceDisplayName", "Data Instance"),
			NSLOCTEXT("NamespaceMetadata", "DataInstanceDescription", "A special transient value which has a single bool IsAlive value, which determines if a particle is alive or not."),
			FLinearColor(FColor(179, 122, 45)),
			{ENiagaraNamespaceMetadataOptions::Advanced}),
	};
}

bool UNiagaraEditorSettings::GetAutoCompile() const
{
	return bAutoCompile;
}

void UNiagaraEditorSettings::SetAutoCompile(bool bInAutoCompile)
{
	if (bAutoCompile != bInAutoCompile)
	{
		bAutoCompile = bInAutoCompile;
		SaveConfig();
	}
}

bool UNiagaraEditorSettings::GetAutoPlay() const
{
	return bAutoPlay;
}

void UNiagaraEditorSettings::SetAutoPlay(bool bInAutoPlay)
{
	if (bAutoPlay != bInAutoPlay)
	{
		bAutoPlay = bInAutoPlay;
		SaveConfig();
	}
}

bool UNiagaraEditorSettings::GetResetSimulationOnChange() const
{
	return bResetSimulationOnChange;
}

void UNiagaraEditorSettings::SetResetSimulationOnChange(bool bInResetSimulationOnChange)
{
	if (bResetSimulationOnChange != bInResetSimulationOnChange)
	{
		bResetSimulationOnChange = bInResetSimulationOnChange;
		SaveConfig();
	}
}

bool UNiagaraEditorSettings::GetResimulateOnChangeWhilePaused() const
{
	return bResimulateOnChangeWhilePaused;
}

void UNiagaraEditorSettings::SetResimulateOnChangeWhilePaused(bool bInResimulateOnChangeWhilePaused)
{
	if (bResimulateOnChangeWhilePaused != bInResimulateOnChangeWhilePaused)
	{
		bResimulateOnChangeWhilePaused = bInResimulateOnChangeWhilePaused;
		SaveConfig();
	}
}

bool UNiagaraEditorSettings::GetResetDependentSystemsWhenEditingEmitters() const
{
	return bResetDependentSystemsWhenEditingEmitters;
}

void UNiagaraEditorSettings::SetResetDependentSystemsWhenEditingEmitters(bool bInResetDependentSystemsWhenEditingEmitters)
{
	if (bResetDependentSystemsWhenEditingEmitters != bInResetDependentSystemsWhenEditingEmitters)
	{
		bResetDependentSystemsWhenEditingEmitters = bInResetDependentSystemsWhenEditingEmitters;
		SaveConfig();
	}
}

FNiagaraNewAssetDialogConfig UNiagaraEditorSettings::GetNewAssetDailogConfig(FName InDialogConfigKey) const
{
	const FNiagaraNewAssetDialogConfig* Config = NewAssetDialogConfigMap.Find(InDialogConfigKey);
	if (Config != nullptr)
	{
		return *Config;
	}
	return FNiagaraNewAssetDialogConfig();
}

void UNiagaraEditorSettings::SetNewAssetDialogConfig(FName InDialogConfigKey, const FNiagaraNewAssetDialogConfig& InNewAssetDialogConfig)
{
	NewAssetDialogConfigMap.Add(InDialogConfigKey, InNewAssetDialogConfig);
	SaveConfig();
}

FNiagaraNamespaceMetadata UNiagaraEditorSettings::GetMetaDataForNamespaces(TArray<FName> InNamespaces) const
{
	TArray<FNiagaraNamespaceMetadata> MatchingMetadata;
	for (const FNiagaraNamespaceMetadata& NamespaceMetadataItem : NamespaceMetadata)
	{
		if (NamespaceMetadataItem.Namespaces.Num() <= InNamespaces.Num())
		{	
			bool bNamespacesMatch = true;
			for (int32 i = 0; i < NamespaceMetadataItem.Namespaces.Num() && bNamespacesMatch; i++)
			{
				if(NamespaceMetadataItem.Namespaces[i] != InNamespaces[i])
				{
					bNamespacesMatch = false;
				}
			}
			if (bNamespacesMatch)
			{
				MatchingMetadata.Add(NamespaceMetadataItem);
			}
		}
	}
	if (MatchingMetadata.Num() == 0)
	{
		return FNiagaraNamespaceMetadata();
	}
	else if (MatchingMetadata.Num() == 1)
	{
		return MatchingMetadata[0];
	}
	else
	{
		int32 IndexOfLargestMatch = 0;
		for (int32 i = 1; i < MatchingMetadata.Num(); i++)
		{
			if (MatchingMetadata[i].Namespaces.Num() > MatchingMetadata[IndexOfLargestMatch].Namespaces.Num())
			{
				IndexOfLargestMatch = i;
			}
		}
		return MatchingMetadata[IndexOfLargestMatch];
	}
}

FName UNiagaraEditorSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FText UNiagaraEditorSettings::GetSectionText() const
{
	return NSLOCTEXT("NiagaraEditorPlugin", "NiagaraEditorSettingsSection", "Niagara Editor");
}

void UNiagaraEditorSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property != nullptr)
	{
		SettingsChangedDelegate.Broadcast(PropertyChangedEvent.Property->GetName(), this);
	}
}

UNiagaraEditorSettings::FOnNiagaraEditorSettingsChanged& UNiagaraEditorSettings::OnSettingsChanged()
{
	return SettingsChangedDelegate;
}

UNiagaraEditorSettings::FOnNiagaraEditorSettingsChanged UNiagaraEditorSettings::SettingsChangedDelegate;