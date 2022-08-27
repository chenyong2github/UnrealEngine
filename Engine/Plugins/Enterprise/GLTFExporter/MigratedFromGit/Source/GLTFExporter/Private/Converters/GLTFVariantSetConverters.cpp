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
	const AActor* Actor = Cast<AActor>(Binding->GetObject());
	if (Actor == nullptr)
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to find actor for variant binding %s"), *Binding->GetObjectPath()));
		return false;
	}

	FGLTFJsonVariantNode JsonVariantNode;
	bool bHasAnyProperty = false;

	for (UPropertyValue* Property: Binding->GetCapturedProperties())
	{
		if (Property->Resolve())
		{
			// TODO: handle failure to resolve property (or force resolve it somehow)
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
		// NOTE: this could occur if the user is exporting selected actors only, if the actor isn't part of the selection
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export node for actor used by variant binding %s"), *Binding->GetObjectPath()));
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
