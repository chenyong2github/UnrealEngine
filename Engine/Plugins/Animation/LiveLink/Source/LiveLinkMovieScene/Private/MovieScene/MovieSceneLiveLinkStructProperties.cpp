// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MovieScene/MovieSceneLiveLinkStructProperties.h"

#include "MovieScene/MovieSceneLiveLinkEnumHandler.h"
#include "MovieScene/MovieSceneLiveLinkPropertyHandler.h"
#include "MovieScene/MovieSceneLiveLinkTransformHandler.h"


namespace LiveLinkPropertiesUtils
{
	TSharedPtr<IMovieSceneLiveLinkPropertyHandler> CreateHandlerFromProperty(UProperty* InProperty, const FLiveLinkStructPropertyBindings& InBinding, FLiveLinkPropertyData* InPropertyData)
	{
		if (InProperty->IsA(UFloatProperty::StaticClass()))
		{
			return MakeShared<FMovieSceneLiveLinkPropertyHandler<float>>(InBinding, InPropertyData);
		}
		else if (InProperty->IsA(UIntProperty::StaticClass()))
		{
			return MakeShared<FMovieSceneLiveLinkPropertyHandler<int32>>(InBinding, InPropertyData);
		}
		else if (InProperty->IsA(UBoolProperty::StaticClass()))
		{
			return MakeShared<FMovieSceneLiveLinkPropertyHandler<bool>>(InBinding, InPropertyData);
		}
		else if (InProperty->IsA(UStrProperty::StaticClass()))
		{
			return MakeShared<FMovieSceneLiveLinkPropertyHandler<FString>>(InBinding, InPropertyData);
		}
		else if (InProperty->IsA(UByteProperty::StaticClass()))
		{
			return MakeShared<FMovieSceneLiveLinkPropertyHandler<uint8>>(InBinding, InPropertyData);
		}
		else if (InProperty->IsA(UEnumProperty::StaticClass()))
		{
			return MakeShared<FMovieSceneLiveLinkEnumHandler>(InBinding, InPropertyData);
		}
		else if (InProperty->IsA(UStructProperty::StaticClass()))
		{
			if (UStructProperty* StructProperty = Cast<UStructProperty>(InProperty))
			{
				if (StructProperty->Struct->GetFName() == NAME_Transform)
				{
					return MakeShared<FMovieSceneLiveLinkTransformHandler>(InBinding, InPropertyData);
				}
				else if (StructProperty->Struct->GetFName() == NAME_Vector)
				{
					return MakeShared<FMovieSceneLiveLinkPropertyHandler<FVector>>(InBinding, InPropertyData);
				}
				else if (StructProperty->Struct->GetFName() == NAME_Color)
				{
					return MakeShared<FMovieSceneLiveLinkPropertyHandler<FColor>>(InBinding, InPropertyData);
				}
			}
		}
		
		return TSharedPtr<IMovieSceneLiveLinkPropertyHandler>();
	}

	TSharedPtr<IMovieSceneLiveLinkPropertyHandler> CreatePropertyHandler(const UScriptStruct& InStruct, FLiveLinkPropertyData* InPropertyData)
	{
		FLiveLinkStructPropertyBindings PropertyBinding(InPropertyData->PropertyName, InPropertyData->PropertyName.ToString());
		UProperty* PropertyPtr = PropertyBinding.GetProperty(InStruct);
		if (PropertyPtr == nullptr)
		{
			return TSharedPtr<IMovieSceneLiveLinkPropertyHandler>();
		}

		if (PropertyPtr->IsA(UArrayProperty::StaticClass()))
		{
			return CreateHandlerFromProperty(CastChecked<UArrayProperty>(PropertyPtr)->Inner, PropertyBinding, InPropertyData);
		}
		else
		{
			return CreateHandlerFromProperty(PropertyPtr, PropertyBinding, InPropertyData);
		}
	}

}
