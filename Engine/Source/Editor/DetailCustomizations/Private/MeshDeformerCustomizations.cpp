// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshDeformerCustomizations.h"

#include "Animation/MeshDeformer.h"
#include "Components/SkinnedMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "DetailWidgetRow.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyHandle.h"


namespace
{
	/** Pull the class type from the PrimaryBindingClass tag in some AssetData. */
	UClass* GetPrimaryBindingClassFromAssetData(FAssetData const& AssetData)
	{
		UClass* BindingClass = nullptr;
		FString BindingClassPath;
		if (AssetData.GetTagValue<FString>("PrimaryBindingClass", BindingClassPath))
		{
			FSoftClassPath BindingSoftClassPath(BindingClassPath);
			BindingClass = BindingSoftClassPath.ResolveClass();
		}
		return BindingClass;
	}
}


TSharedRef<IPropertyTypeCustomization> FMeshDeformerCustomization::MakeInstance()
{
	return MakeShareable(new FMeshDeformerCustomization);
}

void FMeshDeformerCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> InPropertyHandle,
	FDetailWidgetRow& InHeaderRow,
	IPropertyTypeCustomizationUtils& InCustomizationUtils)
{
	TArray<UObject*> OwningObjects;
	InPropertyHandle->GetOuterObjects(OwningObjects);
	if (OwningObjects.Num() == 0)
	{
		return;
	}

	UClass* OwnerClass = OwningObjects[0]->GetClass();

	// Filter in USkeletalMesh context should assume USkinnedMeshComponent binding :(
	if (OwnerClass == USkeletalMesh::StaticClass())
	{
		OwnerClass = USkinnedMeshComponent::StaticClass();
	}

	InHeaderRow.NameContent()
	[
		InPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(SObjectPropertyEntryBox)
		.PropertyHandle(InPropertyHandle)
		.AllowedClass(UMeshDeformer::StaticClass())

		// Disable filtering for now because of slow asset loading. 
		// This is due to kernel compilation happening in PostLoad().
		// Need to implement deferred compilation on demand and can then re-enable here.
#if 0 
		.OnShouldFilterAsset_Lambda([OwnerClass](FAssetData const& AssetData)
		{
			// Filter depending on whether the PrimaryBindingClass matches our owning object.
			// First try to load the class type from the asset registry.
			UClass* BindingClass = GetPrimaryBindingClassFromAssetData(AssetData);

			// If we can't find the tag in the registry then load the full object to get the class type (slow).
			if (BindingClass == nullptr)
			{
				if (UObject* Object = AssetData.GetAsset())
				{
					FAssetData AssetDataWithObjectRegistryTags;
					Object->GetAssetRegistryTags(AssetDataWithObjectRegistryTags);
					BindingClass = GetPrimaryBindingClassFromAssetData(AssetDataWithObjectRegistryTags);
				}
			}

			const bool bFoundBindingClass = BindingClass != nullptr;
			const bool bCanBind = OwnerClass != nullptr && BindingClass != nullptr && OwnerClass->IsChildOf(BindingClass);
			const bool bHideEntry = bFoundBindingClass && !bCanBind;

			return bHideEntry;
		})
#endif
	];
}
