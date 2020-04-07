// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraEditorSettings.h"
#include "NiagaraConstants.h"

FNiagaraNamespaceMetadata::FNiagaraNamespaceMetadata()
	: BackgroundColor(FLinearColor::Black)
	, ForegroundStyle("NiagaraEditor.ParameterName.NamespaceText")
{
}

FNiagaraNamespaceMetadata::FNiagaraNamespaceMetadata(TArray<FName> InNamespaces)
	: Namespaces(InNamespaces)
	, BackgroundColor(FLinearColor::Black)
	, ForegroundStyle("NiagaraEditor.ParameterName.NamespaceText")
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
	SetupNamespaceMetadata();
}

#define LOCTEXT_NAMESPACE "NamespaceMetadata"

void UNiagaraEditorSettings::SetupNamespaceMetadata()
{
	NamespaceMetadata = 
	{
		FNiagaraNamespaceMetadata({NAME_None})
			.SetDisplayName(LOCTEXT("DefaultDisplayName", "None"))
			.SetDescription(LOCTEXT("DefaultDescription", "Arbitrary sub-namespace for specifying module specific dataset attributes, or calling nested modules."))
			.SetBackgroundColor(FLinearColor(FColor(102, 102, 102))),
		FNiagaraNamespaceMetadata({FNiagaraConstants::SystemNamespace})
			.SetDisplayName(LOCTEXT("SystemDisplayName", "System"))
			.SetDescription(LOCTEXT("SystemDescription", "Persistent attribute in the system which is written in a system\n stage and can be read anywhere."))
			.SetBackgroundColor(FLinearColor(FColor(49, 113, 142)))
			.AddOption(ENiagaraNamespaceMetadataOptions::CanChangeNamespaceModifier),
		FNiagaraNamespaceMetadata({FNiagaraConstants::EmitterNamespace})
			.SetDisplayName(LOCTEXT("EmitterDisplayName", "Emitter"))
			.SetDescription(LOCTEXT("EmitterDescription", "Persistent attribute which is written in a emitter\n stage and can be read in emitter and particle stages."))
			.SetBackgroundColor(FLinearColor(FColor(145, 99, 56)))
			.AddOption(ENiagaraNamespaceMetadataOptions::CanChangeNamespaceModifier),
		FNiagaraNamespaceMetadata({FNiagaraConstants::ParticleAttributeNamespace})
			.SetDisplayName(LOCTEXT("ParticleDisplayName", "Particles"))
			.SetDescription(LOCTEXT("ParticleDescription", "Persistent attribute which is written in a particle\n stage and can be read in particle stages."))
			.SetBackgroundColor(FLinearColor(FColor(72, 130, 71)))
			.AddOption(ENiagaraNamespaceMetadataOptions::CanChangeNamespaceModifier),
		FNiagaraNamespaceMetadata({FNiagaraConstants::ModuleNamespace})
			.SetDisplayName(LOCTEXT("ModuleDisplayName", "Input"))
			.SetDescription(LOCTEXT("ModuleDescription", "A transient value which exposes a module input to the system and emitter editor."))
			.SetBackgroundColor(FLinearColor(FColor(136, 66, 65)))
			.AddOption(ENiagaraNamespaceMetadataOptions::CanChangeNamespaceModifier),
		FNiagaraNamespaceMetadata({FNiagaraConstants::OutputNamespace, FNiagaraConstants::ModuleNamespace})
			.SetDisplayName(LOCTEXT("ModuleOutputDisplayName", "Output"))
			.SetDescription(LOCTEXT("ModuleOutputDescription", "A transient value which is unique to the module that wrote it and can be read from any other module.\nTransient values do not persist from frame to frame, or between stages, e.g. emitter to particle, or spawn to update."))
			.SetBackgroundColor(FLinearColor(FColor(109, 95, 124)))
			.AddOption(ENiagaraNamespaceMetadataOptions::Advanced)
			.AddOption(ENiagaraNamespaceMetadataOptions::PreventCreatingInSystemEditor)
			.AddOption(ENiagaraNamespaceMetadataOptions::CanChangeNamespaceModifier),
		FNiagaraNamespaceMetadata({FNiagaraConstants::OutputNamespace})
			.SetDisplayName(LOCTEXT("OutputDisplayName", "Output"))
			.SetDescription(LOCTEXT("OutputDescription", "A transient value which is unique to the module that wrote it and can be read from any other module.\nTransient values do not persist from frame to frame, or between stages, e.g. emitter to particle, or spawn to update."))
			.SetBackgroundColor(FLinearColor(FColor(109, 95, 124)))
			.AddOption(ENiagaraNamespaceMetadataOptions::Advanced),
		FNiagaraNamespaceMetadata({FNiagaraConstants::LocalNamespace, FNiagaraConstants::ModuleNamespace})
			.SetDisplayName(LOCTEXT("ModuleLocalDisplayName", "Local"))
			.SetDescription(LOCTEXT("ModuleLocalDescription", "A transient value which can be written to and read from within a single module.\nTransient values do not persist from frame to frame, or between stages, e.g. emitter to particle, or spawn to update."))
			.SetBackgroundColor(FLinearColor(FColor(174, 157, 40)))
			.AddOption(ENiagaraNamespaceMetadataOptions::Advanced),
		FNiagaraNamespaceMetadata({FNiagaraConstants::TransientNamespace})
			.SetDisplayName(LOCTEXT("TransientDisplayName", "Transient"))
			.SetDescription(LOCTEXT("TransientDescription", "A transient value which can be written to and read from from any module.\nTransient values do not persist from frame to frame, or between stages, e.g. emitter to particle, or spawn to update."))
			.SetBackgroundColor(FLinearColor(FColor(109, 95, 124)))
			.AddOption(ENiagaraNamespaceMetadataOptions::Advanced),
		FNiagaraNamespaceMetadata({FNiagaraConstants::EngineNamespace})
			.SetDisplayName(LOCTEXT("EngineDisplayName", "Engine"))
			.SetDescription(LOCTEXT("EngineDescription", "A read only value which is provided by the engine.\nThis value's source can be the simulation itsef\ne.g. ExecutionCount, or the owner of the simulation e.g. (Owner) Scale."))
			.SetBackgroundColor(FLinearColor(FColor(170, 170, 170)))
			.SetForegroundStyle("NiagaraEditor.ParameterName.NamespaceTextDark")
			.AddOption(ENiagaraNamespaceMetadataOptions::PreventRenaming),
		FNiagaraNamespaceMetadata({FNiagaraConstants::UserNamespace})
			.SetDisplayName(LOCTEXT("UserDisplayName", "User"))
			.SetDescription(LOCTEXT("UserDescription", "A read only value which can be initialized per system and\nmodified externally in the level, by blueprint, or by c++."))
			.SetBackgroundColor(FLinearColor(FColor(91, 161, 194))),
		FNiagaraNamespaceMetadata({FNiagaraConstants::ParameterCollectionNamespace})
			.SetDisplayName(LOCTEXT("NiagaraParameterCollectionDisplayName", "NPC"))
			.SetDisplayNameLong(LOCTEXT("NiagaraParameterCollectionDisplayNameLong", "Niagara Parameter Collection"))
			.SetDescription(LOCTEXT("NiagaraParameterCollectionDescription", "Values read from a niagara parameter collection asset.\nRead only in a niagara system."))
			.SetBackgroundColor(FLinearColor(FColor(170, 170, 170)))
			.SetForegroundStyle("NiagaraEditor.ParameterName.NamespaceTextDark")
			.AddOption(ENiagaraNamespaceMetadataOptions::Advanced)
			.AddOption(ENiagaraNamespaceMetadataOptions::PreventRenaming),
		FNiagaraNamespaceMetadata({FNiagaraConstants::DataInstanceNamespace})
			.SetDisplayName(LOCTEXT("DataInstanceDisplayName", "Data Instance"))
			.SetDescription(LOCTEXT("DataInstanceDescription", "A special transient value which has a single bool IsAlive value, which determines if a particle is alive or not."))
			.SetBackgroundColor(FLinearColor(FColor(170, 170, 170)))
			.SetForegroundStyle("NiagaraEditor.ParameterName.NamespaceTextDark")
			.AddOption(ENiagaraNamespaceMetadataOptions::Advanced)
			.AddOption(ENiagaraNamespaceMetadataOptions::PreventRenaming),
	};

	NamespaceModifierMetadata =
	{
		FNiagaraNamespaceMetadata({FNiagaraConstants::InitialNamespace})
			.SetDisplayName(LOCTEXT("InitialModifierDisplayName", "Initial"))
			.SetDescription(LOCTEXT("InitialModifierDescription", "A namespace modifier for dataset attributes which when used in\na linked input in an update script will get the initial value from the spawn script."))
			.SetBackgroundColor(FLinearColor(FColor(102, 102, 102))),
		FNiagaraNamespaceMetadata({FNiagaraConstants::ModuleNamespace})
			.SetDisplayName(LOCTEXT("ModuleModifierDisplayName", "Module"))
			.SetDescription(LOCTEXT("ModuleModifierDescription", "A namespace modifier which is used when writing dataset attributes\nin module which will make that attribute unique to the module instance when used."))
			.SetBackgroundColor(FLinearColor(FColor(102, 102, 102))),
		FNiagaraNamespaceMetadata({FNiagaraConstants::SystemNamespace})
			.SetDisplayName(LOCTEXT("SystemModifierDisplayName", "System"))
			.SetDescription(LOCTEXT("SystemModifierDescription", "A namespace modifier which specifies that an engine provided parameter comes from the system."))
			.SetBackgroundColor(FLinearColor(FColor(49, 113, 142))),
		FNiagaraNamespaceMetadata({FNiagaraConstants::EmitterNamespace})
			.SetDisplayName(LOCTEXT("EmitterModifierDisplayName", "Emitter"))
			.SetDescription(LOCTEXT("EmitterModifierDescription", "A namespace modifier which specifies that an engine provided parameter comes from the emitter."))
			.SetBackgroundColor(FLinearColor(FColor(145, 99, 56))),
		FNiagaraNamespaceMetadata({FNiagaraConstants::OwnerNamespace})
			.SetDisplayName(LOCTEXT("OwnerDisplayName", "Owner"))
			.SetDescription(LOCTEXT("OwnerDescription", "A namespace modifier which specifies that an engine provided parameter comes from the owner, or component."))
			.SetBackgroundColor(FLinearColor(FColor(170, 170, 170)))
			.SetForegroundStyle("NiagaraEditor.ParameterName.NamespaceTextDark"),
	};
}

#undef LOCTEXT_NAMESPACE

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

bool UNiagaraEditorSettings::GetDisplayAdvancedParameterPanelCategories() const
{
	return bDisplayAdvancedParameterPanelCategories;
}

void UNiagaraEditorSettings::SetDisplayAdvancedParameterPanelCategories(bool bInDisplayAdvancedParameterPanelCategories)
{
	if (bDisplayAdvancedParameterPanelCategories != bInDisplayAdvancedParameterPanelCategories)
	{
		bDisplayAdvancedParameterPanelCategories = bInDisplayAdvancedParameterPanelCategories;
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

FNiagaraNamespaceMetadata UNiagaraEditorSettings::GetMetaDataForNamespaceModifier(FName NamespaceModifier) const
{
	for (const FNiagaraNamespaceMetadata& NamespaceModifierMetadataItem : NamespaceModifierMetadata)
	{
		if (NamespaceModifierMetadataItem.Namespaces.Num() == 1 && NamespaceModifierMetadataItem.Namespaces[0] == NamespaceModifier)
		{
			return NamespaceModifierMetadataItem;
		}
	}
	return FNiagaraNamespaceMetadata();
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