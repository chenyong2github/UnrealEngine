// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraEditorSettings.h"

#include "NiagaraActions.h"
#include "NiagaraConstants.h"
#include "NiagaraEditorModule.h"

const FGuid FNiagaraEditorGuids::SystemNamespaceMetaDataGuid = FGuid::NewGuid();
const FGuid FNiagaraEditorGuids::EmitterNamespaceMetaDataGuid = FGuid::NewGuid();
const FGuid FNiagaraEditorGuids::ParticleAttributeNamespaceMetaDataGuid = FGuid::NewGuid();
const FGuid FNiagaraEditorGuids::ModuleNamespaceMetaDataGuid = FGuid::NewGuid();
const FGuid FNiagaraEditorGuids::ModuleOutputNamespaceMetaDataGuid = FGuid::NewGuid();
const FGuid FNiagaraEditorGuids::ModuleLocalNamespaceMetaDataGuid = FGuid::NewGuid();
const FGuid FNiagaraEditorGuids::TransientNamespaceMetaDataGuid = FGuid::NewGuid();
const FGuid FNiagaraEditorGuids::StackContextNamespaceMetaDataGuid = FGuid::NewGuid();
const FGuid FNiagaraEditorGuids::EngineNamespaceMetaDataGuid = FGuid::NewGuid();
const FGuid FNiagaraEditorGuids::UserNamespaceMetaDataGuid = FGuid::NewGuid();
const FGuid FNiagaraEditorGuids::ParameterCollectionNamespaceMetaDataGuid = FGuid::NewGuid();
const FGuid FNiagaraEditorGuids::DataInstanceNamespaceMetaDataGuid = FGuid::NewGuid();
const FGuid FNiagaraEditorGuids::StaticSwitchNamespaceMetaDataGuid = FGuid::NewGuid();


FNiagaraNamespaceMetadata::FNiagaraNamespaceMetadata()
	: BackgroundColor(FLinearColor::Black)
	, ForegroundStyle("NiagaraEditor.ParameterName.NamespaceText")
	, SortId(TNumericLimits<int32>::Max())
{
}

FNiagaraNamespaceMetadata::FNiagaraNamespaceMetadata(TArray<FName> InNamespaces, FName InRequiredNamespaceModifier)
	: Namespaces(InNamespaces)
	, RequiredNamespaceModifier(InRequiredNamespaceModifier)
	, BackgroundColor(FLinearColor::Black)
	, ForegroundStyle("NiagaraEditor.ParameterName.NamespaceText")
	, SortId(TNumericLimits<int32>::Max())
{
}

FNiagaraActionColors::FNiagaraActionColors()
	: NiagaraColor(EForceInit::ForceInitToZero)
	, GameColor(EForceInit::ForceInitToZero)
	, PluginColor(EForceInit::ForceInitToZero)
	, DeveloperColor(EForceInit::ForceInitToZero)
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

FLinearColor UNiagaraEditorSettings::GetSourceColor(EScriptSource  Source) const
{
	if(Source == EScriptSource::Niagara)
	{
		return ActionColors.NiagaraColor;
	}
	else if(Source == EScriptSource::Game)
	{
		return ActionColors.GameColor;
	}
	else if(Source == EScriptSource::Plugins)
	{
		return ActionColors.PluginColor;
	}
	else if(Source == EScriptSource::Developer)
	{
		return ActionColors.DeveloperColor;
	}

	return FLinearColor(1.f,1.f,1.f,0.3);
}

void UNiagaraEditorSettings::SetupNamespaceMetadata()
{
	DefaultNamespaceMetadata = FNiagaraNamespaceMetadata({ NAME_None })
		.SetDisplayName(LOCTEXT("DefaultDisplayName", "None"))
		.SetDescription(LOCTEXT("DefaultDescription", "Non-standard unknown namespace."))
		.SetBackgroundColor(FLinearColor(FColor(102, 102, 102)))
		.AddOption(ENiagaraNamespaceMetadataOptions::PreventEditingNamespace)
		.AddOption(ENiagaraNamespaceMetadataOptions::PreventEditingNamespaceModifier)
		.AddOption(ENiagaraNamespaceMetadataOptions::PreventEditingName);

	NamespaceMetadata =
	{
		FNiagaraNamespaceMetadata({FNiagaraConstants::SystemNamespace})
			.SetDisplayName(LOCTEXT("SystemDisplayName", "System"))
			.SetDisplayNameLong(LOCTEXT("SystemDisplayNameLong", "System Attributes"))
			.SetDescription(LOCTEXT("SystemDescription", "Persistent attribute in the system which is written in a system\n stage and can be read anywhere."))
			.SetBackgroundColor(FLinearColor(FColor(49, 113, 142)))
			.SetSortId(10)
			.AddOptionalNamespaceModifier(FNiagaraConstants::ModuleNamespace)
			.AddOptionalNamespaceModifier(FNiagaraConstants::InitialNamespace)
			.AddOptionalNamespaceModifier(FNiagaraConstants::PreviousNamespace)
			.SetGuid(FNiagaraEditorGuids::SystemNamespaceMetaDataGuid),
		FNiagaraNamespaceMetadata({FNiagaraConstants::EmitterNamespace})
			.SetDisplayName(LOCTEXT("EmitterDisplayName", "Emitter"))
			.SetDisplayNameLong(LOCTEXT("EmitterDisplayNameLong", "Emitter Attributes"))
			.SetDescription(LOCTEXT("EmitterDescription", "Persistent attribute which is written in a emitter\nstage and can be read in emitter and particle stages."))
			.SetBackgroundColor(FLinearColor(FColor(145, 99, 56)))
			.SetSortId(20)
			.AddOptionalNamespaceModifier(FNiagaraConstants::ModuleNamespace)
			.AddOptionalNamespaceModifier(FNiagaraConstants::InitialNamespace)
			.AddOptionalNamespaceModifier(FNiagaraConstants::PreviousNamespace)
			.SetGuid(FNiagaraEditorGuids::EmitterNamespaceMetaDataGuid),
		FNiagaraNamespaceMetadata({FNiagaraConstants::ParticleAttributeNamespace})
			.SetDisplayName(LOCTEXT("ParticleDisplayName", "Particles"))
			.SetDisplayNameLong(LOCTEXT("ParticleDisplayNameLong", "Particle Attributes"))
			.SetDescription(LOCTEXT("ParticleDescription", "Persistent attribute which is written in a particle\nstage and can be read in particle stages."))
			.SetBackgroundColor(FLinearColor(FColor(72, 130, 71)))
			.SetSortId(30)
			.AddOptionalNamespaceModifier(FNiagaraConstants::ModuleNamespace)
			.AddOptionalNamespaceModifier(FNiagaraConstants::InitialNamespace)
			.AddOptionalNamespaceModifier(FNiagaraConstants::PreviousNamespace)
			.SetGuid(FNiagaraEditorGuids::ParticleAttributeNamespaceMetaDataGuid),
		FNiagaraNamespaceMetadata({FNiagaraConstants::ModuleNamespace})
			.SetDisplayName(LOCTEXT("ModuleDisplayName", "Input"))
			.SetDisplayNameLong(LOCTEXT("ModuleDisplayNameLong", "Module Inputs"))
			.SetDescription(LOCTEXT("ModuleDescription", "A value which exposes a module input to the system and emitter editor."))
			.SetBackgroundColor(FLinearColor(FColor(136, 66, 65)))
			.SetSortId(40)
			.AddOption(ENiagaraNamespaceMetadataOptions::HideInSystem)
			.SetGuid(FNiagaraEditorGuids::ModuleNamespaceMetaDataGuid),
		FNiagaraNamespaceMetadata({FNiagaraConstants::OutputNamespace}, FNiagaraConstants::ModuleNamespace)
			.SetDisplayName(LOCTEXT("ModuleOutputDisplayName", "Output"))
			.SetDisplayNameLong(LOCTEXT("ModuleOutputDisplayNameLong", "Module Outputs"))
			.SetDescription(LOCTEXT("ModuleOutputDescription", "A transient value which the module author has decided might be useful to other modules further down in the stage.\nTransient values do not persist from frame to frame, or between stages, e.g. emitter to particle, or spawn to update."))
			.SetBackgroundColor(FLinearColor(FColor(108, 87, 131)))
			.SetSortId(60)
			.AddOption(ENiagaraNamespaceMetadataOptions::AdvancedInScript)
			.AddOption(ENiagaraNamespaceMetadataOptions::AdvancedInSystem)
			.AddOption(ENiagaraNamespaceMetadataOptions::PreventCreatingInSystemEditor)
			.SetGuid(FNiagaraEditorGuids::ModuleOutputNamespaceMetaDataGuid),
		FNiagaraNamespaceMetadata({FNiagaraConstants::LocalNamespace, FNiagaraConstants::ModuleNamespace})
			.SetDisplayName(LOCTEXT("ModuleLocalDisplayName", "Local"))
			.SetDisplayNameLong(LOCTEXT("ModuleLocalDisplayNameLong", "Module Locals"))
			.SetDescription(LOCTEXT("ModuleLocalDescription", "A transient value which can be written to and read from within a single module.\nTransient values do not persist from frame to frame, or between stages, e.g. emitter to particle, or spawn to update."))
			.SetBackgroundColor(FLinearColor(FColor(191, 176, 84)))
			.SetForegroundStyle("NiagaraEditor.ParameterName.NamespaceTextDark")
			.SetSortId(50)
			.AddOption(ENiagaraNamespaceMetadataOptions::HideInSystem)
			.AddOption(ENiagaraNamespaceMetadataOptions::PreventEditingNamespaceModifier)
			.SetGuid(FNiagaraEditorGuids::ModuleLocalNamespaceMetaDataGuid),
		FNiagaraNamespaceMetadata({FNiagaraConstants::TransientNamespace})
			.SetDisplayName(LOCTEXT("TransientDisplayName", "Transient"))
			.SetDisplayNameLong(LOCTEXT("TransientDisplayNameLong", "Stage Transients"))
			.SetDescription(LOCTEXT("TransientDescription", "A transient value which can be written to and read from from any module.\nTransient values do not persist from frame to frame, or between stages, e.g. emitter to particle, or spawn to update."))
			.SetBackgroundColor(FLinearColor(FColor(108, 87, 131)))
			.SetSortId(80)
			.AddOption(ENiagaraNamespaceMetadataOptions::AdvancedInScript)
			.AddOption(ENiagaraNamespaceMetadataOptions::AdvancedInSystem)
			.AddOption(ENiagaraNamespaceMetadataOptions::PreventEditingNamespaceModifier)
			.SetGuid(FNiagaraEditorGuids::TransientNamespaceMetaDataGuid),
		FNiagaraNamespaceMetadata({FNiagaraConstants::StackContextNamespace})
			.SetDisplayName(LOCTEXT("StackContextDisplayName", "StackContext"))
			.SetDisplayNameLong(LOCTEXT("StackContextDisplayNameLong", "Stack Context Sensitive"))
			.SetDescription(LOCTEXT("StackContextDescription", "A value which can be written to and read from from any module.\nStackContext values do  persist from frame to frame, or between stages, e.g. emitter to particle, or spawn to update.\nThey take on the namespace most relevant to where they are used.\nSystem Spawn/Update: \"System\"\nEmitter Spawn/Update: \"Emitter\".\nParticle Spawn/Update/Events: \"Particles\".\nParticle Simulation Stage iterating Particles: \"Particles\".\nParticle Simulation Stage with Iteration Source: Writes to the iteration source directly."))
			.SetBackgroundColor(FLinearColor(FColor(87, 131, 121)))
			.SetSortId(80)
			.AddOption(ENiagaraNamespaceMetadataOptions::AdvancedInScript)
			.AddOption(ENiagaraNamespaceMetadataOptions::AdvancedInSystem)
		//.AddOption(ENiagaraNamespaceMetadataOptions::PreventEditingNamespaceModifier),
			.AddOptionalNamespaceModifier(FNiagaraConstants::ModuleNamespace)
			.AddOptionalNamespaceModifier(FNiagaraConstants::InitialNamespace)
			.AddOptionalNamespaceModifier(FNiagaraConstants::PreviousNamespace)
			.SetGuid(FNiagaraEditorGuids::StackContextNamespaceMetaDataGuid),
		FNiagaraNamespaceMetadata({FNiagaraConstants::EngineNamespace})
			.SetDisplayName(LOCTEXT("EngineDisplayName", "Engine"))
			.SetDisplayNameLong(LOCTEXT("EngineDisplayNameLong", "Engine Provided"))
			.SetDescription(LOCTEXT("EngineDescription", "A read only value which is provided by the engine.\nThis value's source can be the simulation itsef\ne.g. ExecutionCount, or the owner of the simulation (The component), e.g. (Owner) Scale."))
			.SetBackgroundColor(FLinearColor(FColor(170, 170, 170)))
			.SetForegroundStyle("NiagaraEditor.ParameterName.NamespaceTextDark")
			.SetSortId(70)
			.AddOption(ENiagaraNamespaceMetadataOptions::PreventEditingNamespace)
			.AddOption(ENiagaraNamespaceMetadataOptions::PreventEditingNamespaceModifier)
			.AddOption(ENiagaraNamespaceMetadataOptions::PreventEditingName)
			.SetGuid(FNiagaraEditorGuids::EngineNamespaceMetaDataGuid),
		FNiagaraNamespaceMetadata({FNiagaraConstants::UserNamespace})
			.SetDisplayName(LOCTEXT("UserDisplayName", "User"))
			.SetDisplayNameLong(LOCTEXT("UserDisplayNameLong", "User Exposed"))
			.SetDescription(LOCTEXT("UserDescription", "A read only value which can be initialized per system and\nmodified externally in the level, by blueprint, or by c++."))
			.SetBackgroundColor(FLinearColor(FColor(91, 161, 194)))
			.SetSortId(0)
			.AddOption(ENiagaraNamespaceMetadataOptions::HideInScript)
			.AddOption(ENiagaraNamespaceMetadataOptions::PreventEditingNamespaceModifier)
			.AddOption(ENiagaraNamespaceMetadataOptions::PreventEditingNamespace)
			.SetGuid(FNiagaraEditorGuids::UserNamespaceMetaDataGuid),
		FNiagaraNamespaceMetadata({FNiagaraConstants::ParameterCollectionNamespace})
			.SetDisplayName(LOCTEXT("NiagaraParameterCollectionDisplayName", "NPC"))
			.SetDisplayNameLong(LOCTEXT("NiagaraParameterCollectionDisplayNameLong", "Niagara Parameter Collection"))
			.SetDescription(LOCTEXT("NiagaraParameterCollectionDescription", "Values read from a niagara parameter collection asset.\nRead only in a niagara system."))
			.SetBackgroundColor(FLinearColor(FColor(170, 170, 170)))
			.SetForegroundStyle("NiagaraEditor.ParameterName.NamespaceTextDark")
			.SetSortId(90)
			.AddOption(ENiagaraNamespaceMetadataOptions::AdvancedInScript)
			.AddOption(ENiagaraNamespaceMetadataOptions::AdvancedInSystem)
			.AddOption(ENiagaraNamespaceMetadataOptions::PreventEditingNamespace)
			.AddOption(ENiagaraNamespaceMetadataOptions::PreventEditingNamespaceModifier)
			.AddOption(ENiagaraNamespaceMetadataOptions::PreventEditingName)
			.SetGuid(FNiagaraEditorGuids::ParameterCollectionNamespaceMetaDataGuid),
		FNiagaraNamespaceMetadata({FNiagaraConstants::DataInstanceNamespace})
			.SetDisplayName(LOCTEXT("DataInstanceDisplayName", "Data Instance"))
			.SetDescription(LOCTEXT("DataInstanceDescription", "A special value which has a single bool IsAlive value, which determines if a particle is alive or not."))
			.SetBackgroundColor(FLinearColor(FColor(170, 170, 170)))
			.SetForegroundStyle("NiagaraEditor.ParameterName.NamespaceTextDark")
			.SetSortId(100)
			.AddOption(ENiagaraNamespaceMetadataOptions::HideInSystem)
			.AddOption(ENiagaraNamespaceMetadataOptions::AdvancedInScript)
			.AddOption(ENiagaraNamespaceMetadataOptions::AdvancedInSystem)
			.AddOption(ENiagaraNamespaceMetadataOptions::PreventEditingNamespace)
			.AddOption(ENiagaraNamespaceMetadataOptions::PreventEditingNamespaceModifier)
			.AddOption(ENiagaraNamespaceMetadataOptions::PreventEditingName)
			.SetGuid(FNiagaraEditorGuids::DataInstanceNamespaceMetaDataGuid),
		FNiagaraNamespaceMetadata({FNiagaraConstants::StaticSwitchNamespace})
			.SetDisplayName(LOCTEXT("StatisSwitchDisplayName", "Static Switch Inputs"))
			.SetDescription(LOCTEXT("StaticSwitchDescription", "Values which can only be set at edit time."))
			.SetSortId(45)
			.AddOption(ENiagaraNamespaceMetadataOptions::HideInSystem)
			.AddOption(ENiagaraNamespaceMetadataOptions::PreventEditingNamespace)
			.AddOption(ENiagaraNamespaceMetadataOptions::PreventEditingNamespaceModifier)
			.AddOption(ENiagaraNamespaceMetadataOptions::PreventEditingName)
			.SetGuid(FNiagaraEditorGuids::StaticSwitchNamespaceMetaDataGuid),
	};

	DefaultNamespaceModifierMetadata = FNiagaraNamespaceMetadata({ NAME_None })
		.SetDisplayName(LOCTEXT("DefaultModifierDisplayName", "None"))
		.SetDescription(LOCTEXT("DefaultModifierDescription", "Arbitrary sub-namespace for specifying module specific dataset attributes, or calling nested modules."))
		.SetBackgroundColor(FLinearColor(FColor(102, 102, 102)))
		.AddOption(ENiagaraNamespaceMetadataOptions::PreventEditingNamespace)
		.AddOption(ENiagaraNamespaceMetadataOptions::PreventEditingNamespaceModifier)
		.AddOption(ENiagaraNamespaceMetadataOptions::PreventEditingName);

	NamespaceModifierMetadata =
	{
		FNiagaraNamespaceMetadata({FNiagaraConstants::InitialNamespace})
			.SetDisplayName(LOCTEXT("InitialModifierDisplayName", "Initial"))
			.SetDescription(LOCTEXT("InitialModifierDescription", "A namespace modifier for dataset attributes which when used in\na linked input in an update script will get the initial value from the spawn script."))
			.SetBackgroundColor(FLinearColor(FColor(170, 170, 170)))
			.SetForegroundStyle("NiagaraEditor.ParameterName.NamespaceTextDark"),
		FNiagaraNamespaceMetadata({FNiagaraConstants::PreviousNamespace})
			.SetDisplayName(LOCTEXT("PreviousModifierDisplayName", "Previous"))
			.SetDescription(LOCTEXT("PreviousModifierDescription", "A namespace modifier for dataset attributes which when used in\na linked input in an update script will get the value from the start of the update script."))
			.SetBackgroundColor(FLinearColor(FColor(152, 152, 102)))
			.SetForegroundStyle("NiagaraEditor.ParameterName.NamespaceTextDark"),
		FNiagaraNamespaceMetadata({FNiagaraConstants::ModuleNamespace})
			.SetDisplayName(LOCTEXT("ModuleModifierDisplayName", "Module"))
			.SetDescription(LOCTEXT("ModuleModifierDescription", "A namespace modifier which makes that attribute unique to the module\ninstance by appending the unique module name."))
			.SetBackgroundColor(FLinearColor(FColor(102, 102, 152)))
			.AddOption(ENiagaraNamespaceMetadataOptions::HideInSystem),
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

void UNiagaraEditorSettings::BuildCachedPlaybackSpeeds() const
{
	CachedPlaybackSpeeds = PlaybackSpeeds;
	if (!CachedPlaybackSpeeds.GetValue().Contains(1.f))
	{
		CachedPlaybackSpeeds.GetValue().Add(1.f);
	}
	
	CachedPlaybackSpeeds.GetValue().Sort();
}

TArray<float> UNiagaraEditorSettings::GetPlaybackSpeeds() const
{
	if(!CachedPlaybackSpeeds.IsSet())
	{
		BuildCachedPlaybackSpeeds();
	}

	return CachedPlaybackSpeeds.GetValue();	
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
		SettingsChangedDelegate.Broadcast(GET_MEMBER_NAME_CHECKED(UNiagaraEditorSettings, bDisplayAdvancedParameterPanelCategories).ToString(), this);
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

FNiagaraNamespaceMetadata UNiagaraEditorSettings::GetDefaultNamespaceMetadata() const
{
	return DefaultNamespaceMetadata;
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

FNiagaraNamespaceMetadata UNiagaraEditorSettings::GetMetaDataForId(const FGuid& NamespaceId) const
{
	for (const FNiagaraNamespaceMetadata& NamespaceMetaDatum : NamespaceMetadata)
	{
		if (NamespaceMetaDatum.GetGuid() == NamespaceId)
		{
			return NamespaceMetaDatum;
		}
	}
	ensureMsgf(false, TEXT("Failed to find namespace metadata by ID!"));
	return DefaultNamespaceMetadata;
}

const FGuid& UNiagaraEditorSettings::GetIdForUsage(ENiagaraScriptUsage Usage) const
{
	switch (Usage) {
	case ENiagaraScriptUsage::SystemSpawnScript:
	case ENiagaraScriptUsage::SystemUpdateScript:
		return FNiagaraEditorGuids::SystemNamespaceMetaDataGuid;
	case ENiagaraScriptUsage::EmitterSpawnScript:
	case ENiagaraScriptUsage::EmitterUpdateScript:
		return FNiagaraEditorGuids::EmitterNamespaceMetaDataGuid;
	case ENiagaraScriptUsage::ParticleSpawnScript:
	case ENiagaraScriptUsage::ParticleSpawnScriptInterpolated:
	case ENiagaraScriptUsage::ParticleUpdateScript:
	case ENiagaraScriptUsage::ParticleEventScript:
	case ENiagaraScriptUsage::ParticleGPUComputeScript:
	case ENiagaraScriptUsage::ParticleSimulationStageScript:
		return FNiagaraEditorGuids::ParticleAttributeNamespaceMetaDataGuid;
	default:
		ensureMsgf(false, TEXT("Encounted unexpected usage when finding namespace metadata!"));
		return DefaultNamespaceMetadata.GetGuid();
	}
}

const TArray<FNiagaraNamespaceMetadata>& UNiagaraEditorSettings::GetAllNamespaceMetadata() const
{
	return NamespaceMetadata;
}

FNiagaraNamespaceMetadata UNiagaraEditorSettings::GetDefaultNamespaceModifierMetadata() const
{
	return DefaultNamespaceModifierMetadata;
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

const TArray<FNiagaraNamespaceMetadata>& UNiagaraEditorSettings::GetAllNamespaceModifierMetadata() const
{
	return NamespaceModifierMetadata;
}

const TArray<FNiagaraCurveTemplate>& UNiagaraEditorSettings::GetCurveTemplates() const
{
	return CurveTemplates;
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
	CachedPlaybackSpeeds.Reset();
	
	if (PropertyChangedEvent.Property != nullptr)
	{
		SettingsChangedDelegate.Broadcast(PropertyChangedEvent.Property->GetName(), this);
	}
}

UNiagaraEditorSettings::FOnNiagaraEditorSettingsChanged& UNiagaraEditorSettings::OnSettingsChanged()
{
	return GetMutableDefault<UNiagaraEditorSettings>()->SettingsChangedDelegate;
}

#undef LOCTEXT_NAMESPACE
