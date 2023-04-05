// Copyright Epic Games, Inc. All Rights Reserved.

#include "Algo/Find.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#include "MuCO/CustomizableObject.h"
#include "MuCO/CustomizableObjectExtension.h"
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
