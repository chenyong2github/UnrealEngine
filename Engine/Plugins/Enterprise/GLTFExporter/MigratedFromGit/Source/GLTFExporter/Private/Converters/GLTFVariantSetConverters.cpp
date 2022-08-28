// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFVariantSetConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "LevelVariantSetsActor.h"
#include "VariantObjectBinding.h"
#include "PropertyValueMaterial.h"
#include "LevelVariantSets.h"
#include "PropertyValue.h"
#include "VariantSet.h"
#include "Variant.h"

namespace
{
	// TODO: replace hack with safe solution by adding proper API access to UPropertyValue
	class UPropertyValueHack : public UPropertyValue
	{
	public:
		const TArray<FCapturedPropSegment>& GetCapturedPropSegments() const
		{
			return CapturedPropSegments;
		}
	};
} // anonymous namespace

FGLTFJsonLevelVariantSetsIndex FGLTFLevelVariantSetsConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const ALevelVariantSetsActor* LevelVariantSetsActor)
{
	const ULevelVariantSets* LevelVariantSets = const_cast<ALevelVariantSetsActor*>(LevelVariantSetsActor)->GetLevelVariantSets(true);
	if (LevelVariantSets == nullptr)
	{
		return FGLTFJsonLevelVariantSetsIndex(INDEX_NONE);
	}

	FGLTFJsonLevelVariantSets JsonLevelVariantSets;
	JsonLevelVariantSets.Name = Name.IsEmpty() ? LevelVariantSets->GetName() : Name;

	for (const UVariantSet* VariantSet: LevelVariantSets->GetVariantSets())
	{
		FGLTFJsonVariantSet JsonVariantSet;
		JsonVariantSet.Name = VariantSet->GetDisplayText().ToString();

		for (const UVariant* Variant: VariantSet->GetVariants())
		{
			FGLTFJsonVariant JsonVariant;
			if (TryParseVariant(Builder, JsonVariant, Variant))
			{
				JsonVariantSet.Variants.Add(JsonVariant);
			}
		}

		if (JsonVariantSet.Variants.Num() > 0)
		{
			JsonLevelVariantSets.VariantSets.Add(JsonVariantSet);
		}
		else
		{
			Builder.AddWarningMessage(FString::Printf(
				TEXT("Variant-set has no supported variants and will be skipped. Context: %s"),
				*GetLogContext(VariantSet)));
		}
	}

	if (JsonLevelVariantSets.VariantSets.Num() == 0)
	{
		return FGLTFJsonLevelVariantSetsIndex(INDEX_NONE);
	}

	return Builder.AddLevelVariantSets(JsonLevelVariantSets);
}

bool FGLTFLevelVariantSetsConverter::TryParseVariant(FGLTFConvertBuilder& Builder, FGLTFJsonVariant& OutVariant, const UVariant* Variant) const
{
	FGLTFJsonVariant JsonVariant;

	for (const UVariantObjectBinding* Binding: Variant->GetBindings())
	{
		TryParseVariantBinding(Builder, JsonVariant, Binding);
	}

	if (JsonVariant.Nodes.Num() == 0)
	{
		Builder.AddWarningMessage(FString::Printf(
			TEXT("Variant has no supported bindings and will be skipped. Context: %s"),
			*GetLogContext(Variant)));
		return false;
	}

	JsonVariant.Name = Variant->GetDisplayText().ToString();
	JsonVariant.bIsActive = const_cast<UVariant*>(Variant)->IsActive();

	if (const UTexture2D* Thumbnail = const_cast<UVariant*>(Variant)->GetThumbnail())
	{
		// TODO: if thumbnail is generated from viewport, give it a variant-relevant name (instead of "Texture2D_#")
		JsonVariant.Thumbnail = Builder.GetOrAddTexture(Thumbnail);
	}

	OutVariant = JsonVariant;
	return true;
}

bool FGLTFLevelVariantSetsConverter::TryParseVariantBinding(FGLTFConvertBuilder& Builder, FGLTFJsonVariant& OutVariant, const UVariantObjectBinding* Binding) const
{
	bool bHasParsedAnyProperty = false;

	for (UPropertyValue* Property: Binding->GetCapturedProperties())
	{
		if (!const_cast<UPropertyValue*>(Property)->Resolve() || !Property->HasRecordedData())
		{
			Builder.AddWarningMessage(FString::Printf(
				TEXT("Property is missing recorded data, it will be skipped. Context: %s"),
				*GetLogContext(Property)));
			continue;
		}

		const bool bIsVisibilityProperty =
			Property->GetPropertyName() == TEXT("bVisible") &&
			Property->GetPropertyClass()->IsChildOf(FBoolProperty::StaticClass());

		const bool bIsMaterialProperty = Property->IsA<UPropertyValueMaterial>();
		const bool bIsStaticMeshProperty = Property->GetPropertyName() == TEXT("StaticMesh");
		const bool bIsSkeletalMeshProperty = Property->GetPropertyName() == TEXT("SkeletalMesh");

		if (bIsVisibilityProperty)
		{
			if (TryParseVisibilityPropertyValue(Builder, OutVariant, Property))
			{
				bHasParsedAnyProperty = true;
			}
		}
		else if (bIsMaterialProperty)
		{
			if (TryParseMaterialPropertyValue(Builder, OutVariant, Property))
			{
				bHasParsedAnyProperty = true;
			}
		}
		else if (bIsStaticMeshProperty)
		{
			if (TryParseStaticMeshPropertyValue(Builder, OutVariant, Property))
			{
				bHasParsedAnyProperty = true;
			}
		}
		else if (bIsSkeletalMeshProperty)
		{
			if (TryParseSkeletalMeshPropertyValue(Builder, OutVariant, Property))
			{
				bHasParsedAnyProperty = true;
			}
		}
		else
		{
			// TODO: add support for more properties

			Builder.AddWarningMessage(FString::Printf(
				TEXT("Property is not supported and will be skipped. Context: %s"),
				*GetLogContext(Property)));
		}
	}

	if (!bHasParsedAnyProperty)
	{
		Builder.AddWarningMessage(FString::Printf(
			TEXT("Binding has no supported properties and will be skipped. Context: %s"),
			*GetLogContext(Binding)));
	}

	return bHasParsedAnyProperty;
}

bool FGLTFLevelVariantSetsConverter::TryParseVisibilityPropertyValue(FGLTFConvertBuilder& Builder, FGLTFJsonVariant& OutVariant, const UPropertyValue* Property) const
{
	if (Property->GetPropertyName() != TEXT("bVisible") ||
		!Property->GetPropertyClass()->IsChildOf(FBoolProperty::StaticClass()))
	{
		Builder.AddErrorMessage(FString::Printf(
			TEXT("Attempted to parse visibility from an incompatible property. Context: %s"),
			*GetLogContext(Property)));
		return false;
	}

	const USceneComponent* Target = static_cast<USceneComponent*>(Property->GetPropertyParentContainerAddress());
	if (Target == nullptr)
	{
		Builder.AddWarningMessage(FString::Printf(
			TEXT("Target object for property is invalid, the property will be skipped. Context: %s"),
			*GetLogContext(Property)));
		return false;
	}

	if (Builder.bSelectedActorsOnly && !Target->IsSelected())
	{
		Builder.AddWarningMessage(FString::Printf(
			TEXT("Target object for property is not selected for export, the property will be skipped. Context: %s"),
			*GetLogContext(Property)));
		return false;
	}

	bool bIsVisible;

	if (!TryGetPropertyValue(const_cast<UPropertyValue*>(Property), bIsVisible))
	{
		Builder.AddWarningMessage(FString::Printf(
			TEXT("Failed to parse recorded data for property, it will be skipped. Context: %s"),
			*GetLogContext(Property)));
		return false;
	}

	const FGLTFJsonNodeIndex NodeIndex = Builder.GetOrAddNode(Target); // TODO: what if the target is not selected for export?
	FGLTFJsonVariantNodeProperties& NodeProperties = OutVariant.Nodes.FindOrAdd(NodeIndex);

	NodeProperties.Node = NodeIndex;
	NodeProperties.bIsVisible = bIsVisible;
	return true;
}

bool FGLTFLevelVariantSetsConverter::TryParseMaterialPropertyValue(FGLTFConvertBuilder& Builder, FGLTFJsonVariant& OutVariant, const UPropertyValue* Property) const
{
	UPropertyValueMaterial* MaterialProperty = Cast<UPropertyValueMaterial>(const_cast<UPropertyValue*>(Property));
	if (!MaterialProperty)
	{
		Builder.AddErrorMessage(FString::Printf(
			TEXT("Attempted to parse material from an incompatible property. Context: %s"),
			*GetLogContext(Property)));
		return false;
	}

	const USceneComponent* Target = static_cast<USceneComponent*>(Property->GetPropertyParentContainerAddress());
	if (Target == nullptr)
	{
		Builder.AddWarningMessage(FString::Printf(
			TEXT("Target object for property is invalid, the property will be skipped. Context: %s"),
			*GetLogContext(Property)));
		return false;
	}

	if (Builder.bSelectedActorsOnly && !Target->IsSelected())
	{
		Builder.AddWarningMessage(FString::Printf(
			TEXT("Target object for property is not selected for export, the property will be skipped. Context: %s"),
			*GetLogContext(Property)));
		return false;
	}

	// NOTE: UPropertyValueMaterial::GetMaterial does *not* ensure that the recorded data has been loaded,
	// so we need to call UProperty::GetRecordedData first to make that happen.
	const_cast<UPropertyValueMaterial*>(MaterialProperty)->GetRecordedData();

	const UMaterialInterface* Material = MaterialProperty->GetMaterial();
	if (Material == nullptr)
	{
		Builder.AddWarningMessage(FString::Printf(
			TEXT("Failed to parse recorded data for property, it will be skipped. Context: %s"),
			*GetLogContext(Property)));
		return false;
	}

	const TArray<FCapturedPropSegment>& CapturedPropSegments = reinterpret_cast<UPropertyValueHack*>(MaterialProperty)->GetCapturedPropSegments();
	const int32 NumPropSegments = CapturedPropSegments.Num();

	if (NumPropSegments < 1)
	{
		Builder.AddWarningMessage(FString::Printf(
			TEXT("Failed to parse element index to apply the material to, the property will be skipped. Context: %s"),
			*GetLogContext(Property)));
		return false;
	}

	const FGLTFJsonMaterialIndex MaterialIndex = Builder.GetOrAddMaterial(Material);
	const int32 ElementIndex = CapturedPropSegments[NumPropSegments - 1].PropertyIndex;

	FGLTFJsonVariantMaterial VariantMaterial;
	VariantMaterial.Material = MaterialIndex;
	VariantMaterial.Index = ElementIndex;

	const FGLTFJsonNodeIndex NodeIndex = Builder.GetOrAddNode(Target);
	FGLTFJsonVariantNodeProperties& NodeProperties = OutVariant.Nodes.FindOrAdd(NodeIndex);

	NodeProperties.Node = NodeIndex;
	NodeProperties.Materials.Add(VariantMaterial);
	return true;
}

bool FGLTFLevelVariantSetsConverter::TryParseStaticMeshPropertyValue(FGLTFConvertBuilder& Builder, FGLTFJsonVariant& OutVariant, const UPropertyValue* Property) const
{
	if (Property->GetPropertyName() != TEXT("StaticMesh"))
	{
		Builder.AddErrorMessage(FString::Printf(
			TEXT("Attempted to parse static mesh from an incompatible property. Context: %s"),
			*GetLogContext(Property)));
		return false;
	}

	const USceneComponent* Target = static_cast<USceneComponent*>(Property->GetPropertyParentContainerAddress());
	if (Target == nullptr)
	{
		Builder.AddWarningMessage(FString::Printf(
			TEXT("Target object for property is invalid, the property will be skipped. Context: %s"),
			*GetLogContext(Property)));
		return false;
	}

	if (Builder.bSelectedActorsOnly && !Target->IsSelected())
	{
		Builder.AddWarningMessage(FString::Printf(
			TEXT("Target object for property is not selected for export, the property will be skipped. Context: %s"),
			*GetLogContext(Property)));
		return false;
	}

	const UStaticMesh* StaticMesh;

	if (!TryGetPropertyValue(const_cast<UPropertyValue*>(Property), StaticMesh) || StaticMesh == nullptr)
	{
		Builder.AddWarningMessage(FString::Printf(
			TEXT("Failed to parse recorded data for property, it will be skipped. Context: %s"),
			*GetLogContext(Property)));
		return false;
	}

	const FGLTFJsonNodeIndex NodeIndex = Builder.GetOrAddNode(Target);
	const FGLTFJsonMeshIndex MeshIndex = Builder.GetOrAddMesh(StaticMesh);
	FGLTFJsonVariantNodeProperties& NodeProperties = OutVariant.Nodes.FindOrAdd(NodeIndex);

	NodeProperties.Node = NodeIndex;
	NodeProperties.Mesh = MeshIndex;
	return true;
}

bool FGLTFLevelVariantSetsConverter::TryParseSkeletalMeshPropertyValue(FGLTFConvertBuilder& Builder, FGLTFJsonVariant& OutVariant, const UPropertyValue* Property) const
{
	if (Property->GetPropertyName() != TEXT("SkeletalMesh"))
	{
		Builder.AddErrorMessage(FString::Printf(
			TEXT("Attempted to parse skeletal mesh from an incompatible property. Context: %s"),
			*GetLogContext(Property)));
		return false;
	}

	const USceneComponent* Target = static_cast<USceneComponent*>(Property->GetPropertyParentContainerAddress());
	if (Target == nullptr)
	{
		Builder.AddWarningMessage(FString::Printf(
			TEXT("Target object for property is invalid, the property will be skipped. Context: %s"),
			*GetLogContext(Property)));
		return false;
	}

	if (Builder.bSelectedActorsOnly && !Target->IsSelected())
	{
		Builder.AddWarningMessage(FString::Printf(
			TEXT("Target object for property is not selected for export, the property will be skipped. Context: %s"),
			*GetLogContext(Property)));
		return false;
	}

	const USkeletalMesh* SkeletalMesh;

	if (!TryGetPropertyValue(const_cast<UPropertyValue*>(Property), SkeletalMesh) || SkeletalMesh == nullptr)
	{
		Builder.AddWarningMessage(FString::Printf(
			TEXT("Failed to parse recorded data for property, it will be skipped. Context: %s"),
			*GetLogContext(Property)));
		return false;
	}

	const FGLTFJsonNodeIndex NodeIndex = Builder.GetOrAddNode(Target);
	const FGLTFJsonMeshIndex MeshIndex = Builder.GetOrAddMesh(SkeletalMesh);
	FGLTFJsonVariantNodeProperties& NodeProperties = OutVariant.Nodes.FindOrAdd(NodeIndex);

	NodeProperties.Node = NodeIndex;
	NodeProperties.Mesh = MeshIndex;
	return true;
}

template<typename T>
bool FGLTFLevelVariantSetsConverter::TryGetPropertyValue(UPropertyValue* Property, T& OutValue) const
{
	if (Property == nullptr || !Property->HasRecordedData())
	{
		return false;
	}

	FMemory::Memcpy(&OutValue, Property->GetRecordedData().GetData(), sizeof(T));
	return true;
}

FString FGLTFLevelVariantSetsConverter::GetLogContext(const UPropertyValue* Property) const
{
	const UVariantObjectBinding* Parent = Property->GetParent();
	return GetLogContext(Parent) + TEXT("/") + Property->GetFullDisplayString();
}

FString FGLTFLevelVariantSetsConverter::GetLogContext(const UVariantObjectBinding* Binding) const
{
	const UVariant* Parent = const_cast<UVariantObjectBinding*>(Binding)->GetParent();
	return GetLogContext(Parent) + TEXT("/") + Binding->GetDisplayText().ToString();
}

FString FGLTFLevelVariantSetsConverter::GetLogContext(const UVariant* Variant) const
{
	const UVariantSet* Parent = const_cast<UVariant*>(Variant)->GetParent();
	return GetLogContext(Parent) + TEXT("/") + Variant->GetDisplayText().ToString();
}

FString FGLTFLevelVariantSetsConverter::GetLogContext(const UVariantSet* VariantSet) const
{
	const ULevelVariantSets* Parent = const_cast<UVariantSet*>(VariantSet)->GetParent();
	return GetLogContext(Parent) + TEXT("/") + VariantSet->GetDisplayText().ToString();
}

FString FGLTFLevelVariantSetsConverter::GetLogContext(const ULevelVariantSets* LevelVariantSets) const
{
	return LevelVariantSets->GetName();
}
