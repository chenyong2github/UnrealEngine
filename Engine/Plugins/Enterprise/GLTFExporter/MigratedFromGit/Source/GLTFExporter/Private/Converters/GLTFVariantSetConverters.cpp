// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFVariantSetConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "LevelVariantSetsActor.h"
#include "VariantObjectBinding.h"
#include "LevelVariantSets.h"
#include "PropertyValue.h"
#include "VariantSet.h"
#include "Variant.h"

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
			const FString ThumbnailPrefix = JsonLevelVariantSets.Name + TEXT("_") + JsonVariantSet.Name + TEXT("_");

			FGLTFJsonVariant JsonVariant;
			if (TryParseJsonVariant(Builder, JsonVariant, Variant, ThumbnailPrefix))
			{
				JsonVariantSet.Variants.Add(JsonVariant);
			}
		}

		// TODO: allow sets without any variants?
		if (JsonVariantSet.Variants.Num() > 0)
		{
			JsonLevelVariantSets.VariantSets.Add(JsonVariantSet);
		}
	}

	if (JsonLevelVariantSets.VariantSets.Num() == 0)
	{
		// TODO: allow level variant sets without any variant sets?
		return FGLTFJsonLevelVariantSetsIndex(INDEX_NONE);
	}

	return Builder.AddLevelVariantSets(JsonLevelVariantSets);
}

bool FGLTFLevelVariantSetsConverter::TryParseJsonVariant(FGLTFConvertBuilder& Builder, FGLTFJsonVariant& OutVariant, const UVariant* Variant, const FString& ThumbnailPrefix) const
{
	FGLTFJsonVariant JsonVariant;

	for (const UVariantObjectBinding* Binding: Variant->GetBindings())
	{
		FGLTFJsonVariantNode JsonVariantNode;
		if (TryParseJsonVariantNode(Builder, JsonVariantNode, Binding))
		{
			JsonVariant.Nodes.Add(JsonVariantNode);
		}
	}

	if (JsonVariant.Nodes.Num() == 0)
	{
		// TODO: allow variants without nodes?
		return false;
	}

	JsonVariant.Name = Variant->GetDisplayText().ToString();
	JsonVariant.bIsActive = const_cast<UVariant*>(Variant)->IsActive();

	if (const UTexture2D* Thumbnail = const_cast<UVariant*>(Variant)->GetThumbnail())
	{
		const FString ThumbnailName = ThumbnailPrefix + JsonVariant.Name + TEXT("_thumbnail");
		JsonVariant.Thumbnail = Builder.AddImage(Thumbnail->Source, ThumbnailName);
	}

	OutVariant = JsonVariant;
	return true;
}

bool FGLTFLevelVariantSetsConverter::TryParseJsonVariantNode(FGLTFConvertBuilder& Builder, FGLTFJsonVariantNode& OutVariantNode, const UVariantObjectBinding* Binding) const
{
	const UVariant* Variant = const_cast<UVariantObjectBinding*>(Binding)->GetParent();

	const AActor* Actor = Cast<AActor>(Binding->GetObject());
	if (Actor == nullptr)
	{
		Builder.AddWarningMessage(FString::Printf(
			TEXT("Actor '%s' for variant '%s' was not found and will be skipped. Context: %s"),
			*Binding->GetDisplayText().ToString(),
			*Variant->GetDisplayText().ToString(),
			*GetLogContext(Variant)));
		return false;
	}

	if (Builder.bSelectedActorsOnly && !Actor->IsSelected())
	{
		Builder.AddWarningMessage(FString::Printf(
			TEXT("Actor '%s' for variant '%s' is not selected and will be skipped. Context: %s"),
			*Binding->GetDisplayText().ToString(),
			*Variant->GetDisplayText().ToString(),
			*GetLogContext(Variant)));
		return false;
	}

	FGLTFJsonVariantNode JsonVariantNode;
	bool bHasAnyProperty = false;

	for (UPropertyValue* Property: Binding->GetCapturedProperties())
	{
		if (!Property->Resolve())
		{
			Builder.AddWarningMessage(FString::Printf(
				TEXT("Property '%s' for actor '%s' in variant '%s' failed to resolve and will be skipped. Context: %s"),
				*Property->GetFullDisplayString(),
				*Binding->GetDisplayText().ToString(),
				*Variant->GetDisplayText().ToString(),
				*GetLogContext(Variant)));
			continue;
		}

		const FFieldClass* PropertyClass = Property->GetPropertyClass();
		const FName& PropertyName = Property->GetPropertyName();

		if (PropertyName == TEXT("bVisible") && PropertyClass->IsChildOf(FBoolProperty::StaticClass()))
		{
			bool bIsVisible;
			if (TryGetPropertyValue(Property, bIsVisible))
			{
				JsonVariantNode.bIsVisible = bIsVisible;
				bHasAnyProperty = true;
			}
		}
		else
		{
			// TODO: handle more properties
		}
	}

	if (!bHasAnyProperty)
	{
		// TODO: allow variant nodes without any properties?
		return false;
	}

	const FGLTFJsonNodeIndex Node = Builder.GetOrAddNode(Actor);
	if (Node == INDEX_NONE)
	{
		Builder.AddWarningMessage(FString::Printf(
			TEXT("Actor '%s' for variant '%s' could not be exported and will be skipped. Context: %s"),
			*Binding->GetDisplayText().ToString(),
			*Variant->GetDisplayText().ToString(),
			*GetLogContext(Variant)));
		return false;
	}

	JsonVariantNode.Node = Node;
	OutVariantNode = JsonVariantNode;
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

FString FGLTFLevelVariantSetsConverter::GetLogContext(const UVariant* Variant) const
{
	UVariantSet* VariantSet = const_cast<UVariant*>(Variant)->GetParent();
	ULevelVariantSets* LevelVariantSets = VariantSet->GetParent();

	return LevelVariantSets->GetName() + TEXT("/") + VariantSet->GetDisplayText().ToString() + TEXT("/") + Variant->GetDisplayText().ToString();
}
