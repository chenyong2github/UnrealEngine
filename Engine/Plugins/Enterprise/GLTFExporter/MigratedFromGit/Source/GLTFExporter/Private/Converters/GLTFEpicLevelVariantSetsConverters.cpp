// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFEpicLevelVariantSetsConverters.h"
#include "Converters/GLTFVariantUtility.h"
#include "Builders/GLTFConvertBuilder.h"
#include "LevelVariantSets.h"
#include "VariantSet.h"
#include "Variant.h"
#include "VariantObjectBinding.h"
#include "PropertyValue.h"
#include "PropertyValueMaterial.h"
#include "Components/MeshComponent.h"

FGLTFJsonEpicLevelVariantSetsIndex FGLTFEpicLevelVariantSetsConverter::Convert(const ULevelVariantSets* LevelVariantSets)
{
	FGLTFJsonEpicLevelVariantSets JsonLevelVariantSets;
	LevelVariantSets->GetName(JsonLevelVariantSets.Name);

	for (const UVariantSet* VariantSet: LevelVariantSets->GetVariantSets())
	{
		FGLTFJsonEpicVariantSet JsonVariantSet;
		JsonVariantSet.Name = VariantSet->GetDisplayText().ToString();

		for (const UVariant* Variant: VariantSet->GetVariants())
		{
			FGLTFJsonEpicVariant JsonVariant;
			if (TryParseVariant(JsonVariant, Variant))
			{
				JsonVariantSet.Variants.Add(JsonVariant);
			}
		}

		if (JsonVariantSet.Variants.Num() > 0)
		{
			JsonLevelVariantSets.VariantSets.Add(JsonVariantSet);
		}
	}

	if (JsonLevelVariantSets.VariantSets.Num() == 0)
	{
		return FGLTFJsonEpicLevelVariantSetsIndex(INDEX_NONE);
	}

	return Builder.AddEpicLevelVariantSets(JsonLevelVariantSets);
}

bool FGLTFEpicLevelVariantSetsConverter::TryParseVariant(FGLTFJsonEpicVariant& OutVariant, const UVariant* Variant) const
{
	FGLTFJsonEpicVariant JsonVariant;

	for (const UVariantObjectBinding* Binding: Variant->GetBindings())
	{
		TryParseVariantBinding(JsonVariant, Binding);
	}

	if (JsonVariant.Nodes.Num() == 0)
	{
		return false;
	}

	JsonVariant.Name = Variant->GetDisplayText().ToString();
	JsonVariant.bIsActive = const_cast<UVariant*>(Variant)->IsActive();

	if (const UTexture2D* Thumbnail = const_cast<UVariant*>(Variant)->GetThumbnail())
	{
		// TODO: if thumbnail has generic name "Texture2D", give it a variant-relevant name
		JsonVariant.Thumbnail = Builder.GetOrAddTexture(Thumbnail);
	}

	OutVariant = JsonVariant;
	return true;
}

bool FGLTFEpicLevelVariantSetsConverter::TryParseVariantBinding(FGLTFJsonEpicVariant& OutVariant, const UVariantObjectBinding* Binding) const
{
	bool bHasParsedAnyProperty = false;

	for (const UPropertyValue* Property: Binding->GetCapturedProperties())
	{
		if (!const_cast<UPropertyValue*>(Property)->Resolve() || !Property->HasRecordedData())
		{
			continue;
		}

		const FName PropertyName = Property->GetPropertyName();
		const FFieldClass* PropertyClass = Property->GetPropertyClass();

		if (const UPropertyValueMaterial* MaterialProperty = Cast<UPropertyValueMaterial>(Property))
		{
			if (Builder.ExportOptions->ExportMaterialVariants != EGLTFMaterialVariantMode::None && TryParseMaterialPropertyValue(OutVariant, MaterialProperty))
			{
				bHasParsedAnyProperty = true;
			}
		}
		else if (PropertyName == TEXT("StaticMesh")) // TODO: should we not also check PropertyClass?
		{
			if (Builder.ExportOptions->bExportMeshVariants && TryParseMeshPropertyValue<UStaticMesh>(OutVariant, Property))
			{
				bHasParsedAnyProperty = true;
			}
		}
		else if (PropertyName == TEXT("SkeletalMesh")) // TODO: should we not also check PropertyClass?
		{
			if (Builder.ExportOptions->bExportMeshVariants && TryParseMeshPropertyValue<USkeletalMesh>(OutVariant, Property))
			{
				bHasParsedAnyProperty = true;
			}
		}
		else if (PropertyName == TEXT("bVisible") && PropertyClass->IsChildOf(FBoolProperty::StaticClass()))
		{
			if (Builder.ExportOptions->bExportVisibilityVariants && TryParseVisibilityPropertyValue(OutVariant, Property))
			{
				bHasParsedAnyProperty = true;
			}
		}

		// TODO: add support for more properties
	}

	return bHasParsedAnyProperty;
}

bool FGLTFEpicLevelVariantSetsConverter::TryParseVisibilityPropertyValue(FGLTFJsonEpicVariant& OutVariant, const UPropertyValue* Property) const
{
	const USceneComponent* Target = static_cast<USceneComponent*>(Property->GetPropertyParentContainerAddress());
	if (Target == nullptr)
	{
		Builder.LogWarning(FString::Printf(
			TEXT("Property must belong to a scene component, the property will be skipped. Context: %s"),
			*FGLTFVariantUtility::GetLogContext(Property)));
		return false;
	}

	const AActor* Owner = Target->GetOwner();
	if (Owner == nullptr)
	{
		Builder.LogWarning(FString::Printf(
			TEXT("Property must belong to an actor, the property will be skipped. Context: %s"),
			*FGLTFVariantUtility::GetLogContext(Property)));
		return false;
	}

	if (Builder.bSelectedActorsOnly && !Owner->IsSelected())
	{
		Builder.LogWarning(FString::Printf(
			TEXT("Property doesn't belong to an actor selected for export, the property will be skipped. Context: %s"),
			*FGLTFVariantUtility::GetLogContext(Property)));
		return false;
	}

	bool bIsVisible;

	if (!FGLTFVariantUtility::TryGetPropertyValue(const_cast<UPropertyValue*>(Property), bIsVisible))
	{
		Builder.LogWarning(FString::Printf(
			TEXT("Failed to parse recorded data for property, it will be skipped. Context: %s"),
			*FGLTFVariantUtility::GetLogContext(Property)));
		return false;
	}

	Builder.RegisterObjectVariant(Owner, Property);

	const FGLTFJsonNodeIndex NodeIndex = Builder.GetOrAddNode(Target);
	const FGLTFJsonNodeIndex ComponentNodeIndex = Builder.GetComponentNodeIndex(NodeIndex);

	FGLTFJsonEpicVariantNodeProperties& NodeProperties = OutVariant.Nodes.FindOrAdd(ComponentNodeIndex);

	NodeProperties.Node = ComponentNodeIndex;
	NodeProperties.bIsVisible = bIsVisible;
	return true;
}

bool FGLTFEpicLevelVariantSetsConverter::TryParseMaterialPropertyValue(FGLTFJsonEpicVariant& OutVariant, const UPropertyValueMaterial* Property) const
{
	const UMeshComponent* Target = static_cast<UMeshComponent*>(Property->GetPropertyParentContainerAddress());
	if (Target == nullptr)
	{
		Builder.LogWarning(FString::Printf(
			TEXT("Variant property %s must belong to a mesh component, the property will be skipped"),
			*FGLTFVariantUtility::GetLogContext(Property)));
		return false;
	}

	const AActor* Owner = Target->GetOwner();
	if (Owner == nullptr)
	{
		Builder.LogWarning(FString::Printf(
			TEXT("Variant property %s must belong to an actor, the property will be skipped"),
			*FGLTFVariantUtility::GetLogContext(Property)));
		return false;
	}

	if (Builder.bSelectedActorsOnly && !Owner->IsSelected())
	{
		Builder.LogWarning(FString::Printf(
			TEXT("Variant property %s doesn't belong to an actor selected for export, the property will be skipped"),
			*FGLTFVariantUtility::GetLogContext(Property)));
		return false;
	}

	const TArray<FCapturedPropSegment>& CapturedPropSegments = FGLTFVariantUtility::GetCapturedPropSegments(Property);
	const int32 NumPropSegments = CapturedPropSegments.Num();

	if (NumPropSegments < 1)
	{
		Builder.LogWarning(FString::Printf(
			TEXT("Failed to parse material index for variant property %s, the property will be skipped"),
			*FGLTFVariantUtility::GetLogContext(Property)));
		return false;
	}

	// NOTE: UPropertyValueMaterial::GetMaterial does *not* ensure that the recorded data has been loaded,
	// so we need to call UProperty::GetRecordedData first to make that happen.
	const_cast<UPropertyValueMaterial*>(Property)->GetRecordedData();

	// TODO: find way to determine whether the material is null because "None" was selected, or because it failed to resolve
	const UMaterialInterface* Material = const_cast<UPropertyValueMaterial*>(Property)->GetMaterial();
	const int32 MaterialIndex = CapturedPropSegments[NumPropSegments - 1].PropertyIndex;

	Builder.RegisterObjectVariant(Owner, Property);

	FGLTFJsonEpicVariantMaterial VariantMaterial;
	VariantMaterial.Material = FGLTFVariantUtility::GetOrAddMaterial(Builder, Material, Target, MaterialIndex);
	VariantMaterial.Index = MaterialIndex;

	const FGLTFJsonNodeIndex NodeIndex = Builder.GetOrAddNode(Target);
	const FGLTFJsonNodeIndex ComponentNodeIndex = Builder.GetComponentNodeIndex(NodeIndex);
	FGLTFJsonEpicVariantNodeProperties& NodeProperties = OutVariant.Nodes.FindOrAdd(ComponentNodeIndex);

	NodeProperties.Node = ComponentNodeIndex;
	NodeProperties.Materials.Add(VariantMaterial);
	return true;
}

template <typename MeshType>
bool FGLTFEpicLevelVariantSetsConverter::TryParseMeshPropertyValue(FGLTFJsonEpicVariant& OutVariant, const UPropertyValue* Property) const
{
	const UMeshComponent* Target = static_cast<UMeshComponent*>(Property->GetPropertyParentContainerAddress());
	if (Target == nullptr)
	{
		Builder.LogWarning(FString::Printf(
			TEXT("Variant property %s must belong to a mesh component, the property will be skipped"),
			*FGLTFVariantUtility::GetLogContext(Property)));
		return false;
	}

	const AActor* Owner = Target->GetOwner();
	if (Owner == nullptr)
	{
		Builder.LogWarning(FString::Printf(
			TEXT("Variant property %s must belong to an actor, the property will be skipped"),
			*FGLTFVariantUtility::GetLogContext(Property)));
		return false;
	}

	if (Builder.bSelectedActorsOnly && !Owner->IsSelected())
	{
		Builder.LogWarning(FString::Printf(
			TEXT("Variant property %s doesn't belong to an actor selected for export, the property will be skipped"),
			*FGLTFVariantUtility::GetLogContext(Property)));
		return false;
	}

	const MeshType* Mesh;

	if (!FGLTFVariantUtility::TryGetPropertyValue(const_cast<UPropertyValue*>(Property), Mesh))
	{
		Builder.LogWarning(FString::Printf(
			TEXT("Failed to parse recorded data for variant property %s, it will be skipped"),
			*FGLTFVariantUtility::GetLogContext(Property)));
		return false;
	}

	Builder.RegisterObjectVariant(Owner, Property);

	const FGLTFMaterialArray OverrideMaterials(Target->OverrideMaterials);
	const FGLTFJsonNodeIndex NodeIndex = Builder.GetOrAddNode(Target);
	const FGLTFJsonMeshIndex MeshIndex = Builder.GetOrAddMesh(Mesh, OverrideMaterials);
	const FGLTFJsonNodeIndex ComponentNodeIndex = Builder.GetComponentNodeIndex(NodeIndex);
	FGLTFJsonEpicVariantNodeProperties& NodeProperties = OutVariant.Nodes.FindOrAdd(ComponentNodeIndex);

	NodeProperties.Node = ComponentNodeIndex;
	NodeProperties.Mesh = MeshIndex;
	return true;
}
