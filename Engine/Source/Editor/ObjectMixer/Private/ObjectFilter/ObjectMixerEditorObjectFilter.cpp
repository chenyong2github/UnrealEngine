// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ObjectFilter/ObjectMixerEditorObjectFilter.h"

#include "GameFramework/Actor.h"

FText UObjectMixerObjectFilter::GetRowDisplayName(UObject* InObject) const
{
	if (InObject->IsValidLowLevel())
	{
		return FText::FromString(InObject->GetName());
	}

	return FText::GetEmpty();
}

bool UObjectMixerObjectFilter::GetRowEditorVisibility(UObject* InObject) const
{
	if (InObject->IsValidLowLevel())
	{
		TObjectPtr<AActor> Actor = Cast<AActor>(InObject);
	
		if (!Actor)
		{
			Actor = InObject->GetTypedOuter<AActor>();
		}

		return Actor ? !Actor->IsTemporarilyHiddenInEditor() : false;
	}

	return false;
}

void UObjectMixerObjectFilter::OnSetRowEditorVisibility(UObject* InObject, bool bNewIsVisible) const
{
	if (InObject->IsValidLowLevel())
	{
		TObjectPtr<AActor> Actor = Cast<AActor>(InObject);
	
		if (!Actor)
		{
			Actor = InObject->GetTypedOuter<AActor>();
		}

		if (Actor)
		{
			Actor->SetIsTemporarilyHiddenInEditor(!bNewIsVisible);
		}
	}
}

TArray<FName> UObjectMixerObjectFilter::GetColumnsToShowByDefault() const
{
	return {};
}

TArray<FName> UObjectMixerObjectFilter::GetColumnsFilter() const
{
	return {};
}

EObjectMixerPropertyInheritanceInclusionOptions UObjectMixerObjectFilter::GetObjectMixerPropertyInheritanceInclusionOptions() const
{
	return EObjectMixerPropertyInheritanceInclusionOptions::None;
}

bool UObjectMixerObjectFilter::ShouldIncludeUnsupportedProperties() const
{
	return false;
}

TArray<UClass*> UObjectMixerObjectFilter::GetParentAndChildClassesFromSpecifiedClasses(
	const TArray<UClass*>& InSpecifiedClasses, EObjectMixerPropertyInheritanceInclusionOptions Options)
{
	// 'None' means we only want the specified classes
	if (Options == EObjectMixerPropertyInheritanceInclusionOptions::None)
	{
		return InSpecifiedClasses;
	}
	
	TArray<UClass*> ReturnValue;

	auto GetChildClassesLambda = [](const UClass* Class, TArray<UClass*>& OutReturnValue, const bool bRecursive)
	{
		TArray<UClass*> DerivedClasses;
		GetDerivedClasses(Class, DerivedClasses, bRecursive);
		OutReturnValue.Append(DerivedClasses);
	};

	for (UClass* Class : InSpecifiedClasses)
	{
		ReturnValue.Add(Class);

		// Super Classes

			// Immediate only
		if (Options == EObjectMixerPropertyInheritanceInclusionOptions::IncludeOnlyImmediateParent ||
			Options == EObjectMixerPropertyInheritanceInclusionOptions::IncludeOnlyImmediateParentAndChildren ||
			Options == EObjectMixerPropertyInheritanceInclusionOptions::IncludeOnlyImmediateParentAndAllChildren)
		{
			if (UClass* Super = Class->GetSuperClass())
			{
				ReturnValue.Add(Super);
			}
		}
			// All Parents
		if (Options == EObjectMixerPropertyInheritanceInclusionOptions::IncludeAllParents ||
			Options == EObjectMixerPropertyInheritanceInclusionOptions::IncludeAllParentsAndChildren ||
			Options == EObjectMixerPropertyInheritanceInclusionOptions::IncludeAllParentsAndOnlyImmediateChildren)
		{
			if (UClass* Super = Class->GetSuperClass())
			{
				while (Super)
				{
					ReturnValue.Add(Super);
					Super = Super->GetSuperClass();
				}
			}
		}

		// Child Classes

			// Immediate only
		if (Options == EObjectMixerPropertyInheritanceInclusionOptions::IncludeOnlyImmediateChildren ||
			Options == EObjectMixerPropertyInheritanceInclusionOptions::IncludeOnlyImmediateParentAndChildren ||
			Options == EObjectMixerPropertyInheritanceInclusionOptions::IncludeAllParentsAndOnlyImmediateChildren)
		{
			GetChildClassesLambda(Class, ReturnValue, false);
		}
			// All Children
		if (Options == EObjectMixerPropertyInheritanceInclusionOptions::IncludeAllChildren ||
			Options == EObjectMixerPropertyInheritanceInclusionOptions::IncludeAllParentsAndChildren ||
			Options == EObjectMixerPropertyInheritanceInclusionOptions::IncludeOnlyImmediateParentAndAllChildren)
		{
			GetChildClassesLambda(Class, ReturnValue, true);
		}
	}

	return ReturnValue;
}

EFieldIterationFlags UObjectMixerObjectFilter::GetDesiredFieldIterationFlags(const bool bIncludeInheritedProperties)
{
	return bIncludeInheritedProperties ? EFieldIterationFlags::IncludeSuper : EFieldIterationFlags::Default;
}

TArray<FName> UObjectMixerObjectFilter::GenerateIncludeListFromExcludeList(const TSet<FName>& ExcludeList) const
{
	TSet<FName> IncludeList;

	const EObjectMixerPropertyInheritanceInclusionOptions Options =
		GetObjectMixerPropertyInheritanceInclusionOptions();
	TArray<UClass*> SpecifiedClasses =
		GetParentAndChildClassesFromSpecifiedClasses(GetObjectClassesToFilter(), Options);
	
	for (const UClass* Class : SpecifiedClasses)
	{
		for (TFieldIterator<FProperty> FieldIterator(Class); FieldIterator; ++FieldIterator)
		{
			if (const FProperty* Property = *FieldIterator)
			{
				IncludeList.Add(Property->GetFName());
			}
		}
	}

	return IncludeList.Difference(ExcludeList).Array();
}
