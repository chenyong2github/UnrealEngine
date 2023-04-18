// Copyright Epic Games, Inc. All Rights Reserved.

#include "Algo/Find.h"
#include "Engine/Engine.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "MuCO/CustomizableObject.h"
#include "MuCO/CustomizableObjectDGGUI.h"
#include "MuCO/CustomizableObjectExtension.h"
#include "MuCO/CustomizableSkeletalComponent.h"
#include "MuCO/ICustomizableObjectModule.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Customizable Object module implementation (private)
 */
class FCustomizableObjectModule : public ICustomizableObjectModule
{
public:

	// IModuleInterface 
	void StartupModule() override;
	void ShutdownModule() override;

	// ICustomizableObjectModule interface
	FString GetPluginVersion() const override;
	bool AreExtraBoneInfluencesEnabled() const override;
	void RegisterExtension(TObjectPtr<const UCustomizableObjectExtension> Extension) override;
	void UnregisterExtension(TObjectPtr<const UCustomizableObjectExtension> Extension) override;
	TArrayView<const TObjectPtr<const UCustomizableObjectExtension>> GetRegisteredExtensions() const override;
	TArrayView<const FRegisteredCustomizableObjectPinType> GetExtendedPinTypes() const override;
	TArrayView<const FRegisteredObjectNodeInputPin> GetAdditionalObjectNodePins() const override;

private:
	void RefreshExtensionData();

	// Command to look for Customizable Object Instance in the player pawn of the current world and open a DGGUI to edit its parameters
	IConsoleCommand* LaunchDGGUICommand;
	static void ToggleDGGUI(const TArray<FString>& Arguments);

	// Ensure extensions aren't garbage collected
	TArray<TStrongObjectPtr<const UCustomizableObjectExtension>> StrongExtensions;
	// For returning from GetRegisteredExtensions
	TArray<TObjectPtr<const UCustomizableObjectExtension>> Extensions;

	TArray<FRegisteredCustomizableObjectPinType> ExtendedPinTypes;
	TArray<FRegisteredObjectNodeInputPin> AdditionalObjectNodePins;
};


IMPLEMENT_MODULE( FCustomizableObjectModule, CustomizableObject );

void FCustomizableObjectModule::StartupModule()
{
	LaunchDGGUICommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("mutable.ToggleDGGUI"),
		TEXT("Looks for a Customizable Object Instance within the player pawn and opens a UI to modify its parameters, or closes it if it's open. Specify slot ID to control which component is modified."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&FCustomizableObjectModule::ToggleDGGUI));
}


void FCustomizableObjectModule::ShutdownModule()
{
}


FString FCustomizableObjectModule::GetPluginVersion() const
{
	FString Version = "x.x";
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin("Mutable");
	if (Plugin.IsValid() && Plugin->IsEnabled())
	{
		Version = Plugin->GetDescriptor().VersionName;
	}
	return Version;
}

bool FCustomizableObjectModule::AreExtraBoneInfluencesEnabled() const
{
	bool bAreExtraBoneInfluencesEnabled = false;

	FConfigFile* PluginConfig = GConfig->FindConfigFileWithBaseName("Mutable");
	if (PluginConfig)
	{
		bool bValue = false;
		if (PluginConfig->GetBool(TEXT("Features"), TEXT("bExtraBoneInfluencesEnabled"), bValue))
		{
			bAreExtraBoneInfluencesEnabled = bValue;
		}
	}

	return bAreExtraBoneInfluencesEnabled;
}

void FCustomizableObjectModule::RegisterExtension(TObjectPtr<const UCustomizableObjectExtension> Extension)
{
	check(IsInGameThread());
	
	StrongExtensions.Add(TStrongObjectPtr<const UCustomizableObjectExtension>(Extension));
	Extensions.Add(Extension);
	
	RefreshExtensionData();
}

void FCustomizableObjectModule::UnregisterExtension(TObjectPtr<const UCustomizableObjectExtension> Extension)
{
	check(IsInGameThread());
	
	StrongExtensions.Remove(TStrongObjectPtr<const UCustomizableObjectExtension>(Extension));
	Extensions.Remove(Extension);
	
	RefreshExtensionData();
}

TArrayView<const TObjectPtr<const UCustomizableObjectExtension>> FCustomizableObjectModule::GetRegisteredExtensions() const
{
	check(IsInGameThread());
	return MakeArrayView(Extensions);
}

TArrayView<const FRegisteredCustomizableObjectPinType> FCustomizableObjectModule::GetExtendedPinTypes() const
{
	check(IsInGameThread());
	return MakeArrayView(ExtendedPinTypes);
}

TArrayView<const FRegisteredObjectNodeInputPin> FCustomizableObjectModule::GetAdditionalObjectNodePins() const
{
	check(IsInGameThread());
	return MakeArrayView(AdditionalObjectNodePins);
}

void FCustomizableObjectModule::RefreshExtensionData()
{
	ExtendedPinTypes.Reset();
	AdditionalObjectNodePins.Reset();

	for (const TObjectPtr<const UCustomizableObjectExtension>& Extension : Extensions)
	{
		for (const FCustomizableObjectPinType& PinType : Extension->GetPinTypes())
		{
			FRegisteredCustomizableObjectPinType& RegisteredPinType = ExtendedPinTypes.AddDefaulted_GetRef();

			RegisteredPinType.Extension = TWeakObjectPtr<const UCustomizableObjectExtension>(Extension);
			RegisteredPinType.PinType = PinType;
		}

		for (const FObjectNodeInputPin& Pin : Extension->GetAdditionalObjectNodePins())
		{
			FRegisteredObjectNodeInputPin RegisteredPin;
			RegisteredPin.Extension = TWeakObjectPtr<const UCustomizableObjectExtension>(Extension);
			// Generate a name that will be unique across extensions, to prevent extensions from
			// unintentionally interfering with each other.
			RegisteredPin.GlobalPinName = FName(Extension->GetPathName() + TEXT("__") + Pin.PinName.ToString());
			RegisteredPin.InputPin = Pin;

			const FRegisteredObjectNodeInputPin* MatchingPin =
				Algo::FindBy(AdditionalObjectNodePins, RegisteredPin.GlobalPinName, &FRegisteredObjectNodeInputPin::GlobalPinName);

			if (MatchingPin)
			{
				// The pin should only be in the list if its extension is valid
				check(MatchingPin->Extension.Get());

				UE_LOG(LogMutable, Error,
					TEXT("Object node pin %s from extension %s has the same name as pin %s from extension %s. Please rename one of the two."),
					*Pin.PinName.ToString(),
					*Extension->GetPathName(),
					*MatchingPin->InputPin.PinName.ToString(),
					*MatchingPin->Extension.Get()->GetPathName());

				// Don't register the clashing pin
				continue;
			}

			AdditionalObjectNodePins.Add(RegisteredPin);
		}
	}
}


UCustomizableSkeletalComponent* GetPlayerCustomizableSkeletalComponent(const int32 SlotID, const UWorld* CurrentWorld, const int32 PlayerIndex)
{
	// Get customizable skeletal component attached to player pawn
	UCustomizableSkeletalComponent* SelectedCustomizableSkeletalComponent = nullptr;
	{
		AActor* PlayerPawn = reinterpret_cast<AActor*>(UGameplayStatics::GetPlayerPawn(CurrentWorld, PlayerIndex));
		int32 IndexFound = INDEX_NONE;
		for (TObjectIterator<UCustomizableSkeletalComponent> CustomizableSkeletalComponent; CustomizableSkeletalComponent; ++CustomizableSkeletalComponent)
		{
			if (CustomizableSkeletalComponent->IsValidLowLevel() && !CustomizableSkeletalComponent->IsTemplate())
			{
				AActor* CustomizableActor = CustomizableSkeletalComponent->GetAttachmentRootActor();
				if (CustomizableActor && PlayerPawn == CustomizableActor)
				{
					++IndexFound;
					SelectedCustomizableSkeletalComponent = *CustomizableSkeletalComponent;
					if (IndexFound == SlotID)
					{
						break;
					}
				}
			}
		}
	}


	// If none found, try getting a component without caring about the actor
	if (!SelectedCustomizableSkeletalComponent)
	{
		AActor* PlayerPawn = reinterpret_cast<AActor*>(UGameplayStatics::GetPlayerPawn(CurrentWorld, PlayerIndex));
		int32 IndexFound = INDEX_NONE;
		for (TObjectIterator<UCustomizableSkeletalComponent> CustomizableSkeletalComponent; CustomizableSkeletalComponent; ++CustomizableSkeletalComponent)
		{
			if (CustomizableSkeletalComponent->IsValidLowLevel() && !CustomizableSkeletalComponent->IsTemplate())
			{
				++IndexFound;
				SelectedCustomizableSkeletalComponent = *CustomizableSkeletalComponent;
				if (IndexFound == SlotID)
				{
					break;
				}
			}
		}
	}

	return SelectedCustomizableSkeletalComponent;
}


void FCustomizableObjectModule::ToggleDGGUI(const TArray<FString>& Arguments)
{
	int32 SlotID = INDEX_NONE;
	if (Arguments.Num() >= 1)
	{
		SlotID = FCString::Atoi(*Arguments[0]);
	}

	const UWorld* CurrentWorld = []() -> const UWorld*
	{
		UWorld* WorldForCurrentCOI = nullptr;
		if (GEngine)
		{
			const TIndirectArray<FWorldContext>& WorldContexts = GEngine->GetWorldContexts();
			for (const FWorldContext& Context : WorldContexts)
			{
				if ((Context.WorldType == EWorldType::Game) && (Context.World() != NULL))
				{
					WorldForCurrentCOI = Context.World();
				}
			}
			// Fall back to GWorld if we don't actually have a world.
			if (WorldForCurrentCOI == nullptr)
			{
				WorldForCurrentCOI = GWorld;
			}
		}
		return WorldForCurrentCOI;
	}();

	const int32 PlayerIndex = 0;
	if (UDGGUI::CloseExistingDGGUI(CurrentWorld))
	{
		return;
	}
	else if (UCustomizableSkeletalComponent* SelectedCustomizableSkeletalComponent = GetPlayerCustomizableSkeletalComponent(SlotID, CurrentWorld, PlayerIndex))
	{
		UDGGUI::OpenDGGUI(SlotID, SelectedCustomizableSkeletalComponent, CurrentWorld, PlayerIndex);
	}
}