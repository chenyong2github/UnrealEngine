// Copyright Epic Games, Inc. All Rights Reserved.

#include "PluginReferenceDescriptor.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "JsonUtils/JsonObjectArrayUpdater.h"
#include "ProjectDescriptor.h"

#define LOCTEXT_NAMESPACE "PluginDescriptor"

namespace PluginReferenceDescriptor
{
	FString GetPluginRefKey(const FPluginReferenceDescriptor& PluginRef)
	{
		return PluginRef.Name;
	}

	bool TryGetPluginRefJsonObjectKey(const FJsonObject& JsonObject, FString& OutKey)
	{
		return JsonObject.TryGetStringField(TEXT("Name"), OutKey);
	}

	void UpdatePluginRefJsonObject(const FPluginReferenceDescriptor& PluginRef, FJsonObject& JsonObject)
	{
		PluginRef.UpdateJson(JsonObject);
	}
}

FPluginReferenceDescriptor::FPluginReferenceDescriptor( const FString& InName, bool bInEnabled )
	: Name(InName)
	, bEnabled(bInEnabled)
	, bOptional(false)
{ }


bool FPluginReferenceDescriptor::IsEnabledForPlatform( const FString& Platform ) const
{
	// If it's not enabled at all, return false
	if(!bEnabled)
	{
		return false;
	}

	// If there is a list of whitelisted platforms, and this isn't one of them, return false
	if(WhitelistPlatforms.Num() > 0 && !WhitelistPlatforms.Contains(Platform))
	{
		return false;
	}

	// If this platform is blacklisted, also return false
	if(BlacklistPlatforms.Contains(Platform))
	{
		return false;
	}

	return true;
}

bool FPluginReferenceDescriptor::IsEnabledForTarget(EBuildTargetType TargetType) const
{
    // If it's not enabled at all, return false
    if (!bEnabled)
    {
        return false;
    }

    // If there is a list of whitelisted platforms, and this isn't one of them, return false
    if (WhitelistTargets.Num() > 0 && !WhitelistTargets.Contains(TargetType))
    {
        return false;
    }

    // If this platform is blacklisted, also return false
    if (BlacklistTargets.Contains(TargetType))
    {
        return false;
    }

    return true;
}

bool FPluginReferenceDescriptor::IsEnabledForTargetConfiguration(EBuildConfiguration Configuration) const
{
	// If it's not enabled at all, return false
	if (!bEnabled)
	{
		return false;
	}

	// If there is a list of whitelisted target configurations, and this isn't one of them, return false
	if (WhitelistTargetConfigurations.Num() > 0 && !WhitelistTargetConfigurations.Contains(Configuration))
	{
		return false;
	}

	// If this target configuration is blacklisted, also return false
	if (BlacklistTargetConfigurations.Contains(Configuration))
	{
		return false;
	}

	return true;
}

bool FPluginReferenceDescriptor::IsSupportedTargetPlatform(const FString& Platform) const
{
	return SupportedTargetPlatforms.Num() == 0 || SupportedTargetPlatforms.Contains(Platform);
}

bool FPluginReferenceDescriptor::Read( const FJsonObject& Object, FText& OutFailReason )
{
	// Get the name
	if(!Object.TryGetStringField(TEXT("Name"), Name))
	{
		OutFailReason = LOCTEXT("PluginReferenceWithoutName", "Plugin references must have a 'Name' field");
		return false;
	}

	// Get the enabled field
	if(!Object.TryGetBoolField(TEXT("Enabled"), bEnabled))
	{
		OutFailReason = LOCTEXT("PluginReferenceWithoutEnabled", "Plugin references must have an 'Enabled' field");
		return false;
	}

	// Read the optional field
	Object.TryGetBoolField(TEXT("Optional"), bOptional);

	// Read the metadata for users that don't have the plugin installed
	Object.TryGetStringField(TEXT("Description"), Description);
	Object.TryGetStringField(TEXT("MarketplaceURL"), MarketplaceURL);

	// Get the platform lists
	Object.TryGetStringArrayField(TEXT("WhitelistPlatforms"), WhitelistPlatforms);
	Object.TryGetStringArrayField(TEXT("BlacklistPlatforms"), BlacklistPlatforms);

	// Get the target configuration lists
	Object.TryGetEnumArrayField(TEXT("WhitelistTargetConfigurations"), WhitelistTargetConfigurations);
	Object.TryGetEnumArrayField(TEXT("BlacklistTargetConfigurations"), BlacklistTargetConfigurations);

	// Get the target lists
	Object.TryGetEnumArrayField(TEXT("WhitelistTargets"), WhitelistTargets);
	Object.TryGetEnumArrayField(TEXT("BlacklistTargets"), BlacklistTargets);

	// Get the supported platform list
	Object.TryGetStringArrayField(TEXT("SupportedTargetPlatforms"), SupportedTargetPlatforms);

	return true;
}


bool FPluginReferenceDescriptor::ReadArray( const FJsonObject& Object, const TCHAR* Name, TArray<FPluginReferenceDescriptor>& OutPlugins, FText& OutFailReason )
{
	const TArray< TSharedPtr<FJsonValue> > *Array;

	if (Object.TryGetArrayField(Name, Array))
	{
		for (const TSharedPtr<FJsonValue> &Item : *Array)
		{
			const TSharedPtr<FJsonObject> *ObjectPtr;

			if (Item.IsValid() && Item->TryGetObject(ObjectPtr))
			{
				FPluginReferenceDescriptor Plugin;

				if (!Plugin.Read(*ObjectPtr->Get(), OutFailReason))
				{
					return false;
				}

				OutPlugins.Add(Plugin);
			}
		}
	}

	return true;
}


void FPluginReferenceDescriptor::Write(TJsonWriter<>& Writer) const
{
	TSharedRef<FJsonObject> PluginRefJsonObject = MakeShared<FJsonObject>();
	UpdateJson(*PluginRefJsonObject);

	FJsonSerializer::Serialize(PluginRefJsonObject, Writer);
}

void FPluginReferenceDescriptor::UpdateJson(FJsonObject& JsonObject) const
{
	JsonObject.SetStringField(TEXT("Name"), Name);
	JsonObject.SetBoolField(TEXT("Enabled"), bEnabled);

	if (bEnabled && bOptional)
	{
		JsonObject.SetBoolField(TEXT("Optional"), bOptional);
	}
	else
	{
		JsonObject.RemoveField(TEXT("Optional"));
	}

	if (Description.Len() > 0)
	{
		JsonObject.SetStringField(TEXT("Description"), Description);
	}
	else
	{
		JsonObject.RemoveField(TEXT("Description"));
	}

	if (MarketplaceURL.Len() > 0)
	{
		JsonObject.SetStringField(TEXT("MarketplaceURL"), MarketplaceURL);
	}
	else
	{
		JsonObject.RemoveField(TEXT("MarketplaceURL"));
	}

	if (WhitelistPlatforms.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> WhitelistPlatformValues;
		for (const FString& WhitelistPlatform : WhitelistPlatforms)
		{
			WhitelistPlatformValues.Add(MakeShareable(new FJsonValueString(WhitelistPlatform)));
		}
		JsonObject.SetArrayField(TEXT("WhitelistPlatforms"), WhitelistPlatformValues);
	}
	else
	{
		JsonObject.RemoveField(TEXT("WhitelistPlatforms"));
	}

	if (BlacklistPlatforms.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> BlacklistPlatformValues;
		for (const FString& BlacklistPlatform : BlacklistPlatforms)
		{
			BlacklistPlatformValues.Add(MakeShareable(new FJsonValueString(BlacklistPlatform)));
		}
		JsonObject.SetArrayField(TEXT("BlacklistPlatforms"), BlacklistPlatformValues);
	}
	else
	{
		JsonObject.RemoveField(TEXT("BlacklistPlatforms"));
	}

	if (WhitelistTargetConfigurations.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> WhitelistTargetConfigurationValues;
		for (EBuildConfiguration WhitelistTargetConfiguration : WhitelistTargetConfigurations)
		{
			WhitelistTargetConfigurationValues.Add(MakeShareable(new FJsonValueString(LexToString(WhitelistTargetConfiguration))));
		}
		JsonObject.SetArrayField(TEXT("WhitelistTargetConfigurations"), WhitelistTargetConfigurationValues);
	}
	else
	{
		JsonObject.RemoveField(TEXT("WhitelistTargetConfigurations"));
	}

	if (BlacklistTargetConfigurations.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> BlacklistTargetConfigurationValues;
		for (EBuildConfiguration BlacklistTargetConfiguration : BlacklistTargetConfigurations)
		{
			BlacklistTargetConfigurationValues.Add(MakeShareable(new FJsonValueString(LexToString(BlacklistTargetConfiguration))));
		}
		JsonObject.SetArrayField(TEXT("BlacklistTargetConfigurations"), BlacklistTargetConfigurationValues);
	}
	else
	{
		JsonObject.RemoveField(TEXT("BlacklistTargetConfigurations"));
	}

	if (WhitelistTargets.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> WhitelistTargetValues;
		for (EBuildTargetType WhitelistTarget : WhitelistTargets)
		{
			WhitelistTargetValues.Add(MakeShareable(new FJsonValueString(LexToString(WhitelistTarget))));
		}
		JsonObject.SetArrayField(TEXT("WhitelistTargets"), WhitelistTargetValues);
	}
	else
	{
		JsonObject.RemoveField(TEXT("WhitelistTargets"));
	}

	if (BlacklistTargets.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> BlacklistTargetValues;
		for (EBuildTargetType BlacklistTarget : BlacklistTargets)
		{
			BlacklistTargetValues.Add(MakeShareable(new FJsonValueString(LexToString(BlacklistTarget))));
		}
		JsonObject.SetArrayField(TEXT("BlacklistTargets"), BlacklistTargetValues);
	}
	else
	{
		JsonObject.RemoveField(TEXT("BlacklistTargets"));
	}

	if (SupportedTargetPlatforms.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> SupportedTargetPlatformValues;
		for (const FString& SupportedTargetPlatform : SupportedTargetPlatforms)
		{
			SupportedTargetPlatformValues.Add(MakeShareable(new FJsonValueString(SupportedTargetPlatform)));
		}
		JsonObject.SetArrayField(TEXT("SupportedTargetPlatforms"), SupportedTargetPlatformValues);
	}
	else
	{
		JsonObject.RemoveField(TEXT("SupportedTargetPlatforms"));
	}
}

void FPluginReferenceDescriptor::WriteArray(TJsonWriter<>& Writer, const TCHAR* ArrayName, const TArray<FPluginReferenceDescriptor>& Plugins)
{
	if (Plugins.Num() > 0)
	{
		Writer.WriteArrayStart(ArrayName);

		for (const FPluginReferenceDescriptor& PluginRef : Plugins)
		{
			PluginRef.Write(Writer);
		}

		Writer.WriteArrayEnd();
	}
}

void FPluginReferenceDescriptor::UpdateArray(FJsonObject& JsonObject, const TCHAR* ArrayName, const TArray<FPluginReferenceDescriptor>& Plugins)
{
	typedef FJsonObjectArrayUpdater<FPluginReferenceDescriptor, FString> FPluginRefJsonArrayUpdater;

	FPluginRefJsonArrayUpdater::Execute(
		JsonObject, ArrayName, Plugins, 
		FPluginRefJsonArrayUpdater::FGetElementKey::CreateStatic(PluginReferenceDescriptor::GetPluginRefKey),
		FPluginRefJsonArrayUpdater::FTryGetJsonObjectKey::CreateStatic(PluginReferenceDescriptor::TryGetPluginRefJsonObjectKey),
		FPluginRefJsonArrayUpdater::FUpdateJsonObject::CreateStatic(PluginReferenceDescriptor::UpdatePluginRefJsonObject));
}

#undef LOCTEXT_NAMESPACE
