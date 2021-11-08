// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintNamespaceUtilities.h"
#include "Engine/Blueprint.h"
#include "AssetRegistry/AssetData.h"
#include "EdGraphSchema_K2.h"
#include "Misc/AssertionMacros.h"

namespace UE::Editor::Kismet::Private
{
	// The default Blueprint namespace to use for objects/assets if not explicitly assigned.
	static EDefaultBlueprintNamespaceType DefaultBlueprintNamespaceType = EDefaultBlueprintNamespaceType::DefaultToGlobalNamespace;

	// Delegate invoked whenever the default Blueprint namespace type is set to a different value.
	static FBlueprintNamespaceUtilities::FOnDefaultBlueprintNamespaceTypeChanged OnDefaultBlueprintNamespaceTypeChangedDelegate;

	// Helper method to convert a package path to a Blueprint namespace identifier string.
	static void ConvertPackagePathToNamespacePath(const FString& InPackagePath, FString& OutNamespacePath)
	{
		OutNamespacePath = InPackagePath;
		OutNamespacePath.ReplaceCharInline(TEXT('/'), TEXT('.'));
		if (OutNamespacePath.StartsWith(TEXT(".")))
		{
			OutNamespacePath.RemoveAt(0);
		}
	}
}

FString FBlueprintNamespaceUtilities::GetAssetNamespace(const FAssetData& InAssetData)
{
	// All assets will default to the global namespace (empty string). This will be returned if no other value is explicitly set.
	FString Namespace;

	if (InAssetData.IsValid())
	{
		if (const UObject* AssetObject = InAssetData.FastGetAsset())
		{
			Namespace = GetObjectNamespace(AssetObject);
		}
		else
		{
			using namespace UE::Editor::Kismet::Private;

			// @todo_namespaces - Add cases for unloaded UDS/UDE assets once they have a searchable namespace tag or property.

			FString TagValue;
			if (InAssetData.GetTagValue<FString>(GET_MEMBER_NAME_STRING_CHECKED(UBlueprint, BlueprintNamespace), TagValue))
			{
				Namespace = MoveTemp(TagValue);
			}
			else if (DefaultBlueprintNamespaceType == EDefaultBlueprintNamespaceType::UsePackagePathAsDefaultNamespace)
			{
				ConvertPackagePathToNamespacePath(InAssetData.PackageName.ToString(), Namespace);
			}
		}
	}

	return Namespace;
}

FString FBlueprintNamespaceUtilities::GetObjectNamespace(const UObject* InObject)
{
	// All objects default to the global namespace (empty string). This will be returned if no other paths are set.
	FString Namespace;

	if (const UField* Field = Cast<UField>(InObject))
	{
		const UStruct* OwnerStruct = Field->GetOwnerStruct();

		// If the field's owner is a function (e.g. parameter), continue up the chain until we find the outer class type.
		if (const UFunction* OwnerAsUFunction = Cast<UFunction>(OwnerStruct))
		{
			OwnerStruct = OwnerAsUFunction->GetOwnerClass();
		}

		if (OwnerStruct)
		{
			Field = OwnerStruct;
		}

		if (const FString* TypeNamespace = Field->FindMetaData(FBlueprintMetadata::MD_Namespace))
		{
			Namespace = *TypeNamespace;
		}
		else
		{
			Namespace = GetObjectNamespace(Field->GetPackage());
		}
	}
	else if (const UBlueprint* Blueprint = Cast<UBlueprint>(InObject))
	{
		if (Blueprint->GeneratedClass)
		{
			Namespace = GetObjectNamespace(Blueprint->GeneratedClass);
		}
		else if (!Blueprint->BlueprintNamespace.IsEmpty())
		{
			Namespace = Blueprint->BlueprintNamespace;
		}
		else
		{
			Namespace = GetObjectNamespace(Blueprint->GetPackage());
		}
	}
	else if (const UPackage* Package = Cast<UPackage>(InObject))
	{
		using namespace UE::Editor::Kismet::Private;

		if (DefaultBlueprintNamespaceType == EDefaultBlueprintNamespaceType::UsePackagePathAsDefaultNamespace)
		{
			const bool bIsTransientPackage = Package->HasAnyFlags(RF_Transient) || Package == GetTransientPackage();
			if (!bIsTransientPackage)
			{
				ConvertPackagePathToNamespacePath(Package->GetPathName(), Namespace);
			}
		}
	}
	else if (InObject)
	{
		Namespace = GetObjectNamespace(InObject->GetPackage());
	}

	return Namespace;
}

void FBlueprintNamespaceUtilities::SetDefaultBlueprintNamespaceType(EDefaultBlueprintNamespaceType InType)
{
	using namespace UE::Editor::Kismet::Private;

	if (InType != DefaultBlueprintNamespaceType)
	{
		DefaultBlueprintNamespaceType = InType;

		OnDefaultBlueprintNamespaceTypeChangedDelegate.Broadcast();
	}
}

EDefaultBlueprintNamespaceType FBlueprintNamespaceUtilities::GetDefaultBlueprintNamespaceType()
{
	using namespace UE::Editor::Kismet::Private;
	return DefaultBlueprintNamespaceType;
}

FBlueprintNamespaceUtilities::FOnDefaultBlueprintNamespaceTypeChanged& FBlueprintNamespaceUtilities::OnDefaultBlueprintNamespaceTypeChanged()
{
	using namespace UE::Editor::Kismet::Private;
	return OnDefaultBlueprintNamespaceTypeChangedDelegate;
}