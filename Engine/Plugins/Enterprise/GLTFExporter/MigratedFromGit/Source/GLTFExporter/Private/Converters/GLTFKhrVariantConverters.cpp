// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFKhrVariantConverters.h"
#include "Converters/GLTFVariantUtility.h"
#include "Converters/GLTFMeshUtility.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Rendering/SkeletalMeshRenderData.h"
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

			if (Property->IsA<UPropertyValueMaterial>())
			{
				FGLTFJsonPrimitive* Primitive = nullptr;
				FGLTFJsonMaterialIndex MaterialIndex;

				if (TryParseMaterialProperty(Primitive, MaterialIndex, Property))
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

bool FGLTFKhrMaterialVariantConverter::TryParseMaterialProperty(FGLTFJsonPrimitive*& OutPrimitive, FGLTFJsonMaterialIndex& OutMaterialIndex, const UPropertyValue* Property) const
{
	UPropertyValueMaterial* MaterialProperty = Cast<UPropertyValueMaterial>(const_cast<UPropertyValue*>(Property));
	if (MaterialProperty == nullptr)
	{
		Builder.LogWarning(FString::Printf(
			TEXT("Material property is invalid, the property will be skipped. Context: %s"),
			*FGLTFVariantUtility::GetLogContext(Property)));
		return false;
	}

	const USceneComponent* Target = static_cast<USceneComponent*>(Property->GetPropertyParentContainerAddress());
	if (Target == nullptr)
	{
		Builder.LogWarning(FString::Printf(
			TEXT("Target object for property is invalid, the property will be skipped. Context: %s"),
			*FGLTFVariantUtility::GetLogContext(Property)));
		return false;
	}

	const AActor* Owner = Target->GetOwner();
	if (Owner == nullptr)
	{
		Builder.LogWarning(FString::Printf(TEXT("Invalid scene component, the property will be skipped. Context: %s"),
			*FGLTFVariantUtility::GetLogContext(Property)));
		return false;
	}

	if (Builder.bSelectedActorsOnly && !Owner->IsSelected())
	{
		Builder.LogWarning(FString::Printf(
			TEXT("Target actor for property is not selected for export, the property will be skipped. Context: %s"),
			*FGLTFVariantUtility::GetLogContext(Property)));
		return false;
	}

	const UMeshComponent* MeshComponent = Cast<UMeshComponent>(Target);
	if (MeshComponent == nullptr)
	{
		Builder.LogWarning(FString::Printf(
			TEXT("Target object for property has no mesh-component, the property will be skipped. Context: %s"),
			*FGLTFVariantUtility::GetLogContext(Property)));
		return false;
	}

	const TArray<FCapturedPropSegment>& CapturedPropSegments = FGLTFVariantUtility::GetCapturedPropSegments(MaterialProperty);
	const int32 NumPropSegments = CapturedPropSegments.Num();

	if (NumPropSegments < 1)
	{
		Builder.LogWarning(FString::Printf(
			TEXT("Failed to parse element index to apply the material to, the property will be skipped. Context: %s"),
			*FGLTFVariantUtility::GetLogContext(MaterialProperty)));
		return false;
	}

	// NOTE: UPropertyValueMaterial::GetMaterial does *not* ensure that the recorded data has been loaded,
	// so we need to call UProperty::GetRecordedData first to make that happen.
	MaterialProperty->GetRecordedData();

	const UMaterialInterface* Material = MaterialProperty->GetMaterial();
	if (Material == nullptr)
	{
		// TODO: find way to determine whether the material is null because "None" was selected, or because it failed to resolve

		Builder.LogWarning(FString::Printf(
			TEXT("No material assigned, the property will be skipped. Context: %s"),
			*FGLTFVariantUtility::GetLogContext(MaterialProperty)));
		return false;
	}

	Builder.VariantReferenceChecker.GetOrAdd(MaterialProperty, MeshComponent);
	Builder.VariantReferenceChecker.GetOrAdd(MaterialProperty, Owner);

	const int32 MaterialIndex = CapturedPropSegments[NumPropSegments - 1].PropertyIndex;
	const FGLTFJsonMeshIndex MeshIndex = Builder.GetOrAddMesh(MeshComponent);

	OutPrimitive = &Builder.GetMesh(MeshIndex).Primitives[MaterialIndex];
	OutMaterialIndex = FGLTFVariantUtility::GetOrAddMaterial(Builder, Material, MeshComponent, MaterialIndex);

	return true;
}
