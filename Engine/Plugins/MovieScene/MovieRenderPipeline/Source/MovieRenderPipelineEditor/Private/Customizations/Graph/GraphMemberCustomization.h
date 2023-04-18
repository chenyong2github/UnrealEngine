// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DetailLayoutBuilder.h"
#include "IDetailCustomization.h"
#include "Graph/MovieGraphConfig.h"
#include "PropertyHandle.h"

#define LOCTEXT_NAMESPACE "MoviePipelineEditor"

/** Customize how members for a graph appear in the details panel. */
class FGraphMemberCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShared<FGraphMemberCustomization>();
	}

protected:
	//~ Begin IDetailCustomization interface
	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override
	{
		CustomizeDetails(*DetailBuilder);
	}

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override
	{
		TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
		DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);

		for (const TWeakObjectPtr<UObject>& CustomizedObject : ObjectsBeingCustomized)
		{
			// Note: The graph members inherit the Value property from a base class, so the enable/disable state cannot
			// be driven by UPROPERTY metadata. Hence why this needs to be done w/ a details customization.
			
			// Enable/disable the value property for inputs/outputs based on whether it is specified as a branch or not
			if (const UMovieGraphInterfaceBase* InterfaceBase = Cast<UMovieGraphInterfaceBase>(CustomizedObject))
			{
				const TSharedRef<IPropertyHandle> ValueProperty = DetailBuilder.GetProperty("Value", UMovieGraphValueContainer::StaticClass());
				if (ValueProperty->IsValidHandle())
				{
					DetailBuilder.EditDefaultProperty(ValueProperty)->IsEnabled(!InterfaceBase->bIsBranch);
				}
			}

			// Enable/disable the value property for variables based on the editable state
			if (const UMovieGraphVariable* Variable = Cast<UMovieGraphVariable>(CustomizedObject))
			{
				const TSharedRef<IPropertyHandle> ValueProperty = DetailBuilder.GetProperty("Value", UMovieGraphValueContainer::StaticClass());
				if (ValueProperty->IsValidHandle())
				{
					DetailBuilder.EditDefaultProperty(ValueProperty)->IsEnabled(Variable->IsEditable());
				}
			}
		}
	}
	//~ End IDetailCustomization interface
};

#undef LOCTEXT_NAMESPACE