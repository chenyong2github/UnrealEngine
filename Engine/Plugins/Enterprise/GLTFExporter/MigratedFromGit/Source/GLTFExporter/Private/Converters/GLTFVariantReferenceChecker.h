// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Converters/GLTFConverter.h"
#include "PropertyValue.h"

class FGLTFVariantReferenceChecker final : public TGLTFConverter<bool, const UPropertyValue*, const UObject*>
{
	typedef TTuple<const UPropertyValue*, const UObject*> TPropertyReference;

	TArray<TPropertyReference> References;

	virtual bool Convert(const UPropertyValue* PropertyValue, const UObject* PropertyReference) override
	{
		if (PropertyValue == nullptr || PropertyReference == nullptr)
		{
			return false;
		}

		References.Add(TPropertyReference(PropertyValue, PropertyReference));
		return true;
	}

public:

	bool IsReferenced(const UObject* Object) const
	{
		return References.FindByPredicate([&Object](const TPropertyReference& PropertyReference)
		{
			return PropertyReference.Value == Object;
		}) != nullptr;
	}
};
