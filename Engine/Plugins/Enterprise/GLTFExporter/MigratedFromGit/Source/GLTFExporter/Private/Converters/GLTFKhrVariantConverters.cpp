// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFKhrVariantConverters.h"
#include "Converters/GLTFVariantUtility.h"
#include "Builders/GLTFContainerBuilder.h"
#include "VariantObjectBinding.h"
#include "PropertyValueMaterial.h"
#include "PropertyValue.h"
#include "Variant.h"

FGLTFJsonKhrMaterialVariantIndex FGLTFKhrMaterialVariantConverter::Convert(const UVariant* Variant)
{
	if (Variant == nullptr || Builder.ExportOptions->ExportMaterialVariants == EGLTFMaterialVariantMode::None)
	{
		return FGLTFJsonKhrMaterialVariantIndex(INDEX_NONE);
	}

	FGLTFJsonKhrMaterialVariant MaterialVariant;

	// TODO: add warning if the variant name is not unique, i.e it's already used?
	// While material variants are technically allowed to use the same name, it may
	// cause confusion when trying to select the correct variant in a viewer.
	MaterialVariant.Name = Variant->GetDisplayText().ToString();

	typedef TTuple<FGLTFJsonPrimitive*, FGLTFJsonMaterialIndex> TPrimitiveMaterial;
	TArray<TPrimitiveMaterial> PrimitiveMaterials;

	for (const UVariantObjectBinding* Binding: Variant->GetBindings())
	{
		for (const UPropertyValue* Property: Binding->GetCapturedProperties())
		{
			if (!const_cast<UPropertyValue*>(Property)->Resolve() || !Property->HasRecordedData())
			{
				continue;
			}

			if (const UPropertyValueMaterial* MaterialProperty = Cast<UPropertyValueMaterial>(Property))
			{
				FGLTFJsonPrimitive* Primitive = nullptr;
				FGLTFJsonMaterialIndex MaterialIndex;

				if (TryParseMaterialProperty(Primitive, MaterialIndex, MaterialProperty))
				{
					PrimitiveMaterials.Add(MakeTuple(Primitive, MaterialIndex));
				}
			}
		}
	}

	if (PrimitiveMaterials.Num() < 1)
	{
		// TODO: add warning and / or allow unused material variants to be added?

		return FGLTFJsonKhrMaterialVariantIndex(INDEX_NONE);
	}

	const FGLTFJsonKhrMaterialVariantIndex MaterialVariantIndex = Builder.AddKhrMaterialVariant(MaterialVariant);

	for (const TPrimitiveMaterial& PrimitiveMaterial: PrimitiveMaterials)
	{
		FGLTFJsonPrimitive* Primitive = PrimitiveMaterial.Key;
		const FGLTFJsonMaterialIndex MaterialIndex = PrimitiveMaterial.Value;

		FGLTFJsonKhrMaterialVariantMapping* ExistingMapping = Primitive->KhrMaterialVariantMappings.FindByPredicate(
			[MaterialIndex](const FGLTFJsonKhrMaterialVariantMapping& Mapping)
			{
				return Mapping.Material == MaterialIndex;
			});

		if (ExistingMapping != nullptr)
		{
			ExistingMapping->Variants.AddUnique(MaterialVariantIndex);
		}
		else
		{
			FGLTFJsonKhrMaterialVariantMapping Mapping;
			Mapping.Material = MaterialIndex;
			Mapping.Variants.Add(MaterialVariantIndex);

			Primitive->KhrMaterialVariantMappings.Add(Mapping);
		}
	}

	return MaterialVariantIndex;
}

bool FGLTFKhrMaterialVariantConverter::TryParseMaterialProperty(FGLTFJsonPrimitive*& OutPrimitive, FGLTFJsonMaterialIndex& OutMaterialIndex, const UPropertyValueMaterial* Property) const
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

	const UMaterialInterface* Material = const_cast<UPropertyValueMaterial*>(Property)->GetMaterial();
	if (Material == nullptr)
	{
		// TODO: find way to determine whether the material is null because "None" was selected, or because it failed to resolve

		Builder.LogWarning(FString::Printf(
			TEXT("No material assigned, the property will be skipped. Context: %s"),
			*FGLTFVariantUtility::GetLogContext(Property)));
		return false;
	}

	Builder.RegisterObjectVariant(Target, Property);
	Builder.RegisterObjectVariant(Owner, Property); // TODO: we don't need to register this on the actor

	const int32 MaterialIndex = CapturedPropSegments[NumPropSegments - 1].PropertyIndex;
	const FGLTFJsonMeshIndex MeshIndex = Builder.GetOrAddMesh(Target);

	OutPrimitive = &Builder.GetMesh(MeshIndex).Primitives[MaterialIndex];
	OutMaterialIndex = FGLTFVariantUtility::GetOrAddMaterial(Builder, Material, Target, MaterialIndex);

	return true;
}
