// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintNamespaceUtilities.h"
#include "Engine/Blueprint.h"
#include "AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "EdGraphSchema_K2.h"
#include "Misc/AssertionMacros.h"
#include "BlueprintEditorModule.h"
#include "Toolkits/ToolkitManager.h"

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
			const UBlueprint* Blueprint = nullptr;
			if (const UClass* Class = Cast<UClass>(Field))
			{
				Blueprint = UBlueprint::GetBlueprintFromClass(Class);
			}

			if (Blueprint)
			{
				Namespace = GetObjectNamespace(Blueprint);
			}
			else
			{
				Namespace = GetObjectNamespace(Field->GetPackage());
			}
		}
	}
	else if (const UBlueprint* Blueprint = Cast<UBlueprint>(InObject))
	{
		if (!Blueprint->BlueprintNamespace.IsEmpty())
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

FString FBlueprintNamespaceUtilities::GetObjectNamespace(const FSoftObjectPath& InObjectPath)
{
	if (const UObject* Object = InObjectPath.ResolveObject())
	{
		return GetObjectNamespace(Object);
	}

	FString ObjectPathAsString = InObjectPath.ToString();
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(*ObjectPathAsString);
	if (!AssetData.IsValid() && ObjectPathAsString.RemoveFromEnd(TEXT("_C")))
	{
		AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(*ObjectPathAsString);
	}

	return GetAssetNamespace(AssetData);
}

void FBlueprintNamespaceUtilities::GetPropertyValueNamespaces(const UStruct* InStruct, const FProperty* InProperty, const void* InContainer, TSet<FString>& OutNamespaces)
{
	if (!InStruct || !InProperty || !InContainer)
	{
		return;
	}

	const UStruct* PropertyOwner = InProperty->GetOwnerStruct();
	if (!PropertyOwner)
	{
		return;
	}

	if (!ensureMsgf(InStruct == PropertyOwner, TEXT("Property %s is a member of struct %s which does not match the given struct %s"), *InProperty->GetName(), *PropertyOwner->GetName(), *InStruct->GetName()))
	{
		return;
	}

	for (int32 ArrayIdx = 0; ArrayIdx < InProperty->ArrayDim; ++ArrayIdx)
	{
		const uint8* ValuePtr = InProperty->ContainerPtrToValuePtr<uint8>(InContainer, ArrayIdx);

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(InProperty))
		{
			for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It)
			{
				GetPropertyValueNamespaces(StructProperty->Struct, *It, (*It)->ContainerPtrToValuePtr<uint8>(ValuePtr), OutNamespaces);
			}
		}
		else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(InProperty))
		{
			FScriptArrayHelper ArrayHelper(ArrayProperty, ValuePtr);
			for (int32 ValueIdx = 0; ValueIdx < ArrayHelper.Num(); ++ValueIdx)
			{
				GetPropertyValueNamespaces(InStruct, ArrayProperty->Inner, ArrayHelper.GetRawPtr(ValueIdx), OutNamespaces);
			}
		}
		else if (const FSetProperty* SetProperty = CastField<FSetProperty>(InProperty))
		{
			FScriptSetHelper SetHelper(SetProperty, ValuePtr);
			for (int32 ValueIdx = 0; ValueIdx < SetHelper.Num(); ++ValueIdx)
			{
				GetPropertyValueNamespaces(InStruct, SetProperty->ElementProp, SetHelper.GetElementPtr(ValueIdx), OutNamespaces);
			}
		}
		else if (const FMapProperty* MapProperty = CastField<FMapProperty>(InProperty))
		{
			FScriptMapHelper MapHelper(MapProperty, ValuePtr);
			for (int32 ValueIdx = 0; ValueIdx < MapHelper.Num(); ++ValueIdx)
			{
				const uint8* MapValuePtr = MapHelper.GetPairPtr(ValueIdx);
				GetPropertyValueNamespaces(InStruct, MapProperty->KeyProp, MapValuePtr, OutNamespaces);
				GetPropertyValueNamespaces(InStruct, MapProperty->ValueProp, MapValuePtr, OutNamespaces);
			}
		}
		else if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(InProperty))
		{
			const FSoftObjectPath& ObjectPath = SoftObjectProperty->GetPropertyValue(ValuePtr).ToSoftObjectPath();
			if (ObjectPath.IsValid())
			{
				FString Namespace = GetObjectNamespace(ObjectPath);
				OutNamespaces.Add(Namespace);
			}
		}
		else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(InProperty))
		{
			const UObject* ObjectValue = ObjectProperty->GetObjectPropertyValue(ValuePtr);
			if (ObjectValue)
			{
				FString Namespace = GetObjectNamespace(ObjectValue);
				OutNamespaces.Add(Namespace);
			}
		}
	}
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

void FBlueprintNamespaceUtilities::RefreshBlueprintEditorFeatures()
{
	if (!GEditor)
	{
		return;
	}

	// Refresh all relevant open Blueprint editor UI elements.
	if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
	{
		TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
		for (UObject* Asset : EditedAssets)
		{
			if (Asset && Asset->IsA<UBlueprint>())
			{
				TSharedPtr<IToolkit> AssetEditorPtr = FToolkitManager::Get().FindEditorForAsset(Asset);
				if (AssetEditorPtr.IsValid() && AssetEditorPtr->IsBlueprintEditor())
				{
					TSharedPtr<IBlueprintEditor> BlueprintEditorPtr = StaticCastSharedPtr<IBlueprintEditor>(AssetEditorPtr);
					BlueprintEditorPtr->RefreshEditors();
					BlueprintEditorPtr->RefreshInspector();
				}
			}
		}
	}
}