// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ObjectFilter/ObjectMixerEditorObjectFilter.h"

#include "GameFramework/Actor.h"

#include "LightMixerObjectFilter.generated.h"

UCLASS(BlueprintType, EditInlineNew)
class LIGHTMIXER_API ULightMixerObjectFilter : public UObjectMixerObjectFilter
{
	GENERATED_BODY()
public:
	
	virtual TArray<UClass*> GetObjectClassesToFilter() const override
	{
		return
		{
			ULightComponent::StaticClass()
		};
	}

	virtual FText GetRowDisplayName(UObject* InObject) const override
	{
		if (InObject)
		{
			if (const AActor* Outer = InObject->GetTypedOuter<AActor>())
			{
				return FText::Format(INVTEXT("{0} ({1})"), FText::FromString(Outer->GetActorLabel()), Super::GetRowDisplayName(InObject));
			}
		}

		return Super::GetRowDisplayName(InObject);
	}

	virtual TArray<FName> GetColumnsToShowByDefault() const override
	{
		const TArray<FName> IncludeList =
		{
			"Intensity", "LightColor", "AttenuationRadius"
		};

		return IncludeList;
	}

	virtual TArray<FName> GetForceAddedColumns() const override
	{
		return {"LightColor"};
	}

	virtual bool ShouldIncludeUnsupportedProperties() const override
	{
		return true;
	}

	virtual EObjectMixerPropertyInheritanceInclusionOptions GetObjectMixerPropertyInheritanceInclusionOptions() const override
	{
		return EObjectMixerPropertyInheritanceInclusionOptions::IncludeAllParentsAndChildren;
	}
};
