// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityConfigAsset.h"
#include "Logging/MessageLog.h"
#include "MassEntityTraitBase.h"
#include "MassSpawnerTypes.h"
#include "MassSpawnerSubsystem.h"
#include "MassEntityTemplateRegistry.h"
#include "Serialization/ArchiveObjectCrc32.h"
#include "VisualLogger/VisualLogger.h"
#include "Engine/World.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif // WITH_EDITOR

#define LOCTEXT_NAMESPACE "Mass"


FMassEntityConfig::FMassEntityConfig(UObject& InOwner)
	: ConfigOwner(&InOwner)
{

}

#if WITH_EDITOR
void UMassEntityConfigAsset::ValidateEntityConfig()
{
	if (UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
	{
		if (Config.ValidateEntityTemplate(*EditorWorld))
		{
			const FText InfoText = LOCTEXT("MassEntityConfigAssetNoErrorsDetected", "There were no errors nor warnings detected during validation of the EntityConfigAsset");
			
			FMessageLog EditorInfo("MassEntity");
			EditorInfo.Info(InfoText);

			FNotificationInfo Info(InfoText);
			Info.bFireAndForget = true;
			Info.bUseThrobber = false;
			Info.FadeOutDuration = 0.5f;
			Info.ExpireDuration = 5.0f;
			if (TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info))
			{
				Notification->SetCompletionState(SNotificationItem::CS_Success);
			}
		}
	}
}
#endif // WITH_EDITOR

const FMassEntityTemplate& FMassEntityConfig::GetOrCreateEntityTemplate(const UWorld& World) const
{
	FMassEntityTemplateID TemplateID;
	TArray<UMassEntityTraitBase*> CombinedTraits;
	if (const FMassEntityTemplate* ExistingTemplate = GetEntityTemplateInternal(World, TemplateID, CombinedTraits))
	{
		return *ExistingTemplate;
	}

	UMassSpawnerSubsystem* SpawnerSystem = UWorld::GetSubsystem<UMassSpawnerSubsystem>(&World);
	check(SpawnerSystem);
	FMassEntityTemplateRegistry& TemplateRegistry = SpawnerSystem->GetMutableTemplateRegistryInstance();

	// Build new template
	// TODO: Add methods to FMassEntityTemplateBuildContext to indicate dependency vs setup.
	// Dependency should add a fragment with default values (which later can be overridden),
	// while setup would override values and should be run just once.
	FMassEntityTemplate& Template = TemplateRegistry.CreateTemplate(TemplateID);
	FMassEntityTemplateBuildContext BuildContext(Template);

	BuildContext.BuildFromTraits(CombinedTraits, World);
	Template.SetTemplateName(GetNameSafe(ConfigOwner));

	// It is ok to have an empty template, 
    // but be aware there will be an error if you try to create an entity with it
    // as there will be no archetype associated with this template...
	TemplateRegistry.InitializeEntityTemplate(Template);

	return Template;
}

void FMassEntityConfig::DestroyEntityTemplate(const UWorld& World) const
{
	FMassEntityTemplateID TemplateID;
	TArray<UMassEntityTraitBase*> CombinedTraits;
	const FMassEntityTemplate* Template = GetEntityTemplateInternal(World, TemplateID, CombinedTraits);
	if (Template == nullptr)
	{
		return;
	}

	UMassSpawnerSubsystem* SpawnerSystem = UWorld::GetSubsystem<UMassSpawnerSubsystem>(&World);
	check(SpawnerSystem);
	FMassEntityTemplateRegistry& TemplateRegistry = SpawnerSystem->GetMutableTemplateRegistryInstance();

	for (const UMassEntityTraitBase* Trait : CombinedTraits)
	{
		check(Trait);
		Trait->DestroyTemplate();
	}

	// TODO - The templates are not being torn down completely, resulting in traits that leave data in various subsystems. (Representation system)
	
	TemplateRegistry.DestroyTemplate(TemplateID);
}

const FMassEntityTemplate& FMassEntityConfig::GetEntityTemplateChecked(const UWorld& World) const
{
	FMassEntityTemplateID TemplateID;
	TArray<UMassEntityTraitBase*> CombinedTraits;
	const FMassEntityTemplate* ExistingTemplate = GetEntityTemplateInternal(World, TemplateID, CombinedTraits);
	check(ExistingTemplate);
	return *ExistingTemplate;
}

const FMassEntityTemplate* FMassEntityConfig::GetEntityTemplateInternal(const UWorld& World, FMassEntityTemplateID& TemplateIDOut, TArray<UMassEntityTraitBase*>& CombinedTraitsOut) const
{
	UMassSpawnerSubsystem* SpawnerSystem = UWorld::GetSubsystem<UMassSpawnerSubsystem>(&World);
	check(SpawnerSystem);
	const FMassEntityTemplateRegistry& TemplateRegistry = SpawnerSystem->GetTemplateRegistryInstance();

	// Combine all the features into one array
	// @todo this is an inefficient way assuming given template is expected to have already been created. Figure out a way to cache it.
	TArray<const UObject*> Visited;
	CombinedTraitsOut.Reset();
	Visited.Add(ConfigOwner);
	GetCombinedTraits(CombinedTraitsOut, Visited);

	// Return existing template if found.
	// TODO: cache the hash.
	const uint32 HashOut = UE::MassSpawner::HashTraits(CombinedTraitsOut);
	TemplateIDOut = FMassEntityTemplateID(HashOut);
	return TemplateRegistry.FindTemplateFromTemplateID(TemplateIDOut);
}

void FMassEntityConfig::GetCombinedTraits(TArray<UMassEntityTraitBase*>& OutTraits, TArray<const UObject*>& Visited) const
{
	if (Parent)
	{
		if (Visited.IndexOfByKey(Parent) != INDEX_NONE)
		{
			// Infinite loop detected.
			FString Path;
			for (const UObject* Object : Visited)
			{
				Path += Object->GetName();
				Path += TEXT("/");
			}
			UE_VLOG(ConfigOwner, LogMassSpawner, Error, TEXT("%s: Encountered %s as parent second time (Infinite loop). %s"), *GetNameSafe(ConfigOwner), *GetNameSafe(Parent), *Path);
		}
		else
		{
			Visited.Add(Parent);
			Parent->GetConfig().GetCombinedTraits(OutTraits, Visited);
		}
	}

	for (UMassEntityTraitBase* Trait : Traits)
	{
		if (!Trait)
		{
			continue;
		}
		// Allow only one feature per type. This is also used to allow child configs override parent features.
		const int32 Index = OutTraits.IndexOfByPredicate([Trait](const UMassEntityTraitBase* ExistingFeature) -> bool { return Trait->GetClass() == ExistingFeature->GetClass(); });
		if (Index != INDEX_NONE)
		{
			OutTraits[Index] = Trait;
		}
		else
		{
			OutTraits.Add(Trait);
		}
	}
}

void FMassEntityConfig::AddTrait(UMassEntityTraitBase& Trait)
{
	Traits.Add(&Trait);
}

bool FMassEntityConfig::ValidateEntityTemplate(const UWorld& World)
{
	TArray<const UObject*> Visited;
	TArray<UMassEntityTraitBase*> CombinedTraits;
	Visited.Add(ConfigOwner);
	GetCombinedTraits(CombinedTraits, Visited);

	FMassEntityTemplate Template;
	FMassEntityTemplateBuildContext BuildContext(Template);

	return BuildContext.BuildFromTraits(CombinedTraits, World);
}

//-----------------------------------------------------------------------------
// DEPRECATED
//-----------------------------------------------------------------------------
const FMassEntityTemplate& FMassEntityConfig::GetOrCreateEntityTemplate(const UWorld& World, const UObject& InConfigOwner) const
{
	return GetOrCreateEntityTemplate(World);
}

void FMassEntityConfig::DestroyEntityTemplate(const UWorld& World, const UObject& InConfigOwner) const
{
	DestroyEntityTemplate(World);
}

const FMassEntityTemplate& FMassEntityConfig::GetEntityTemplateChecked(const UWorld& World, const UObject& InConfigOwner) const
{
	return GetEntityTemplateChecked(World);
}

bool FMassEntityConfig::ValidateEntityTemplate(const UWorld& World, const UObject& InConfigOwner)
{
	return ValidateEntityTemplate(World);
}

void FMassEntityConfig::GetCombinedTraits(TArray<UMassEntityTraitBase*>& OutTraits, TArray<const UObject*>& Visited, const UObject& InConfigOwner) const
{
	return GetCombinedTraits(OutTraits, Visited);
}


#undef LOCTEXT_NAMESPACE 
