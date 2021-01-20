// Copyright Epic Games, Inc. All Rights Reserved.

#include "EntitySystem/MovieScenePropertySystemTypes.h"
#include "CoreTypes.h"
#include "UObject/NameTypes.h"
#include "Templates/SharedPointer.h"
#include "Templates/UnrealTypeTraits.h"
#include "Containers/SparseArray.h"
#include "MovieSceneCommonHelpers.h"
#include "EntitySystem/MovieSceneEntitySystemTypes.h"
#include "EntitySystem/MovieSceneComponentRegistry.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneComponentAccessors.h"
#include "EntitySystem/MovieSceneOperationalTypeConversions.h"
#include "EntitySystem/MovieScenePropertyBinding.h"


namespace UE
{
namespace MovieScene
{

template<typename PropertyType>
void TSetPropertyValues<PropertyType>::PreTask()
{
	if (CustomProperties)
	{
		CustomAccessors = CustomProperties->GetAccessors();
	}
}

template<typename PropertyType>
void TSetPropertyValues<PropertyType>::ForEachEntity(UObject* InObject, FCustomPropertyIndex CustomIndex, ParamType ValueToSet)
{
	ForEachEntity(InObject, CustomAccessors[CustomIndex.Value], ValueToSet);
}

template<typename PropertyType>
void TSetPropertyValues<PropertyType>::ForEachEntity(UObject* InObject, const FCustomPropertyAccessor& BaseCustomAccessor, ParamType ValueToSet)
{

	const TCustomPropertyAccessor<PropertyType>& CustomAccessor = static_cast<const TCustomPropertyAccessor<PropertyType>&>(BaseCustomAccessor);
	CustomAccessor.Functions.Setter(InObject, ValueToSet);
}

template<typename PropertyType>
void TSetPropertyValues<PropertyType>::ForEachEntity(UObject* InObject, uint16 PropertyOffset, ParamType ValueToSet)
{
	// Would really like to avoid branching here, but if we encounter this data the options are either handle it gracefully, stomp a vtable, or report a fatal error.
	if (ensureAlwaysMsgf(PropertyOffset != 0, TEXT("Invalid property offset specified (ptr+%d bytes) for property on object %s. This would otherwise overwrite the object's vfptr."), PropertyOffset, *InObject->GetName()))
	{
		PropertyType* PropertyAddress = reinterpret_cast<PropertyType*>( reinterpret_cast<uint8*>(InObject) + PropertyOffset );
		*PropertyAddress = ValueToSet;
	}
}

template<typename PropertyType>
void TSetPropertyValues<PropertyType>::ForEachEntity(UObject* InObject, TSharedPtr<FTrackInstancePropertyBindings> PropertyBindings, ParamType ValueToSet)
{
	PropertyBindings->CallFunction<PropertyType>(*InObject, ValueToSet);
}

template<typename PropertyType>
void TSetPropertyValues<PropertyType>::ForEachAllocation(const FEntityAllocation* Allocation, TRead<UObject*> BoundObjectComponents, FThreeWayAccessor ResolvedPropertyComponents, TRead<PropertyType> PropertyValueComponents)
{
	const int32 Num = Allocation->Num();
	if (const FCustomPropertyIndex* Custom = ResolvedPropertyComponents.template Get<0>())
	{
		for (int32 Index = 0; Index < Num; ++Index)
		{
			ForEachEntity(BoundObjectComponents[Index], *Custom++, PropertyValueComponents[Index]);
		}
	}
	else if (const uint16* Fast = ResolvedPropertyComponents.template Get<1>())
	{
		for (int32 Index = 0; Index < Num; ++Index)
		{
			ForEachEntity(BoundObjectComponents[Index], *Fast++, PropertyValueComponents[Index]);
		}
	}
	else if (const TSharedPtr<FTrackInstancePropertyBindings>* Slow = ResolvedPropertyComponents.template Get<2>())
	{
		for (int32 Index = 0; Index < Num; ++Index)
		{
			ForEachEntity(BoundObjectComponents[Index], *Slow++, PropertyValueComponents[Index]);
		}
	}
}

template<typename PropertyType>
void TSetPropertyValues<PropertyType>::ForEachAllocation(const FEntityAllocation* Allocation, TRead<UObject*> BoundObjectComponents, FTwoWayAccessor ResolvedPropertyComponents, TRead<PropertyType> PropertyValueComponents)
{
	const int32 Num = Allocation->Num();
	if (const uint16* Fast = ResolvedPropertyComponents.template Get<0>())
	{
		for (int32 Index = 0; Index < Num; ++Index)
		{
			ForEachEntity(BoundObjectComponents[Index], *Fast++, PropertyValueComponents[Index]);
		}
	}
	else if (const TSharedPtr<FTrackInstancePropertyBindings>* Slow = ResolvedPropertyComponents.template Get<1>())
	{
		for (int32 Index = 0; Index < Num; ++Index)
		{
			ForEachEntity(BoundObjectComponents[Index], *Slow++, PropertyValueComponents[Index]);
		}
	}
}


template<typename PropertyType, typename OperationalType>
void TGetPropertyValues<PropertyType, OperationalType>::PreTask()
{
	if (CustomProperties)
	{
		CustomAccessors = CustomProperties->GetAccessors();
	}
}

template<typename PropertyType, typename OperationalType>
void TGetPropertyValues<PropertyType, OperationalType>::ForEachEntity(UObject* InObject, FCustomPropertyIndex CustomPropertyIndex, OperationalType& OutValue)
{
	const TCustomPropertyAccessor<PropertyType>& CustomAccessor = static_cast<const TCustomPropertyAccessor<PropertyType>&>(CustomAccessors[CustomPropertyIndex.Value]);
	ConvertOperationalProperty(CustomAccessor.Functions.Getter(InObject), OutValue);
}

template<typename PropertyType, typename OperationalType>
void TGetPropertyValues<PropertyType, OperationalType>::ForEachEntity(UObject* InObject, uint16 PropertyOffset, OperationalType& OutValue)
{
	// Would really like to avoid branching here, but if we encounter this data the options are either handle it gracefully, stomp a vtable, or report a fatal error.
	if (ensureAlwaysMsgf(PropertyOffset != 0, TEXT("Invalid property offset specified (ptr+%d bytes) for property on object %s. This would otherwise overwrite the object's vfptr."), PropertyOffset, *InObject->GetName()))
	{
		PropertyType* PropertyAddress = reinterpret_cast<PropertyType*>( reinterpret_cast<uint8*>(InObject) + PropertyOffset );

		ConvertOperationalProperty(*PropertyAddress, OutValue);
	}
}

template<typename PropertyType, typename OperationalType>
void TGetPropertyValues<PropertyType, OperationalType>::ForEachEntity(UObject* InObject, TSharedPtr<FTrackInstancePropertyBindings> PropertyBindings, OperationalType& OutValue)
{
	ConvertOperationalProperty(PropertyBindings->GetCurrentValue<PropertyType>(*InObject), OutValue);
}

template<typename PropertyType, typename OperationalType>
void TGetPropertyValues<PropertyType, OperationalType>::ForEachAllocation(const FEntityAllocation* Allocation, TRead<UObject*> BoundObjectComponents, FThreeWayAccessor ResolvedPropertyComponents, TWrite<OperationalType> OutValueComponents)
{
	const int32 Num = Allocation->Num();
	if (const FCustomPropertyIndex* Custom = ResolvedPropertyComponents.template Get<0>())
	{
		for (int32 Index = 0; Index < Num; ++Index)
		{
			ForEachEntity(BoundObjectComponents[Index], Custom[Index], OutValueComponents[Index]);
		}
	}
	else if (const uint16* Fast = ResolvedPropertyComponents.template Get<1>())
	{
		for (int32 Index = 0; Index < Num; ++Index)
		{
			ForEachEntity(BoundObjectComponents[Index], Fast[Index], OutValueComponents[Index]);
		}
	}
	else if (const TSharedPtr<FTrackInstancePropertyBindings>* Slow = ResolvedPropertyComponents.template Get<2>())
	{
		for (int32 Index = 0; Index < Num; ++Index)
		{
			ForEachEntity(BoundObjectComponents[Index], Slow[Index], OutValueComponents[Index]);
		}
	}
}

template<typename PropertyType, typename OperationalType>
void TGetPropertyValues<PropertyType, OperationalType>::ForEachAllocation(const FEntityAllocation* Allocation, TRead<UObject*> BoundObjectComponents, FTwoWayAccessor ResolvedPropertyComponents, TWrite<OperationalType> OutValueComponents)
{
	const int32 Num = Allocation->Num();
	if (const uint16* Fast = ResolvedPropertyComponents.template Get<0>())
	{
		for (int32 Index = 0; Index < Num; ++Index)
		{
			ForEachEntity(BoundObjectComponents[Index], Fast[Index], OutValueComponents[Index]);
		}
	}
	else if (const TSharedPtr<FTrackInstancePropertyBindings>* Slow = ResolvedPropertyComponents.template Get<1>())
	{
		for (int32 Index = 0; Index < Num; ++Index)
		{
			ForEachEntity(BoundObjectComponents[Index], Slow[Index], OutValueComponents[Index]);
		}
	}
}


template<typename PropertyType, typename ProjectionType, typename... CompositeTypes, int... Indices>
void TSetCompositePropertyValuesImpl<TIntegerSequence<int, Indices...>, PropertyType, ProjectionType, CompositeTypes...>::PreTask()
{
	if (CustomProperties)
	{
		CustomAccessors = CustomProperties->GetAccessors();
	}
}

template<typename PropertyType, typename ProjectionType, typename... CompositeTypes, int... Indices>
void TSetCompositePropertyValuesImpl<TIntegerSequence<int, Indices...>, PropertyType, ProjectionType, CompositeTypes...>::ForEachEntity(UObject* InObject, FCustomPropertyIndex CustomPropertyIndex, typename TCallTraits<CompositeTypes>::ParamType... CompositeResults)
{
	const TCustomPropertyAccessor<PropertyType>& CustomAccessor = static_cast<const TCustomPropertyAccessor<PropertyType>&>(CustomAccessors[CustomPropertyIndex.Value]);

	PropertyType Result = Invoke(Projection, CompositeResults...);
	CustomAccessor.Functions.Setter(InObject, Result);
}

template<typename PropertyType, typename ProjectionType, typename... CompositeTypes, int... Indices>
void TSetCompositePropertyValuesImpl<TIntegerSequence<int, Indices...>, PropertyType, ProjectionType, CompositeTypes...>::ForEachEntity(UObject* InObject, uint16 PropertyOffset, typename TCallTraits<CompositeTypes>::ParamType... CompositeResults)
{
	// Would really like to avoid branching here, but if we encounter this data the options are either handle it gracefully, stomp a vtable, or report a fatal error.
	if (ensureAlwaysMsgf(PropertyOffset != 0, TEXT("Invalid property offset specified (ptr+%d bytes) for property on object %s. This would otherwise overwrite the object's vfptr."), PropertyOffset, *InObject->GetName()))
	{
		PropertyType Result = Invoke(Projection, CompositeResults...);

		PropertyType* PropertyAddress = reinterpret_cast<PropertyType*>( reinterpret_cast<uint8*>(InObject) + PropertyOffset );
		*PropertyAddress = Result;
	}
}

template<typename PropertyType, typename ProjectionType, typename... CompositeTypes, int... Indices>
void TSetCompositePropertyValuesImpl<TIntegerSequence<int, Indices...>, PropertyType, ProjectionType, CompositeTypes...>::ForEachEntity(UObject* InObject, TSharedPtr<FTrackInstancePropertyBindings> PropertyBindings, typename TCallTraits<CompositeTypes>::ParamType... CompositeResults)
{
	PropertyType Result = Invoke(Projection, CompositeResults...);
	PropertyBindings->CallFunction<PropertyType>(*InObject, Result);
}

template<typename PropertyType, typename ProjectionType, typename... CompositeTypes, int... Indices>
void TSetCompositePropertyValuesImpl<TIntegerSequence<int, Indices...>, PropertyType, ProjectionType, CompositeTypes...>::ForEachAllocation(const FEntityAllocation* Allocation, TRead<UObject*> BoundObjectComponents, FThreeWayAccessor ResolvedPropertyComponents, TRead<CompositeTypes>... VariadicComponents)
{
	const int32 Num = Allocation->Num();
	if (const FCustomPropertyIndex* Custom = ResolvedPropertyComponents.template Get<0>())
	{
		for (int32 Index = 0; Index < Num; ++Index )
		{
			ForEachEntity( BoundObjectComponents[Index], Custom[Index], VariadicComponents[Index]... );
		}
	}
	else if (const uint16* Fast = ResolvedPropertyComponents.template Get<1>())
	{
		for (int32 Index = 0; Index < Num; ++Index )
		{
			ForEachEntity( BoundObjectComponents[Index], Fast[Index], VariadicComponents[Index]... );
		}
	}
	else if (const TSharedPtr<FTrackInstancePropertyBindings>* Slow = ResolvedPropertyComponents.template Get<2>())
	{
		for (int32 Index = 0; Index < Num; ++Index )
		{
			ForEachEntity( BoundObjectComponents[Index], Slow[Index], VariadicComponents[Index]... );
		}
	}
}

template<typename PropertyType, typename ProjectionType, typename... CompositeTypes, int... Indices>
void TSetCompositePropertyValuesImpl<TIntegerSequence<int, Indices...>, PropertyType, ProjectionType, CompositeTypes...>::ForEachAllocation(const FEntityAllocation* Allocation, TRead<UObject*> BoundObjectComponents, FTwoWayAccessor ResolvedPropertyComponents, TRead<CompositeTypes>... VariadicComponents)
{
	const int32 Num = Allocation->Num();
	if (const uint16* Fast = ResolvedPropertyComponents.template Get<0>())
	{
		for (int32 Index = 0; Index < Num; ++Index )
		{
			ForEachEntity( BoundObjectComponents[Index], Fast[Index], VariadicComponents[Index]... );
		}
	}
	else if (const TSharedPtr<FTrackInstancePropertyBindings>* Slow = ResolvedPropertyComponents.template Get<1>())
	{
		for (int32 Index = 0; Index < Num; ++Index )
		{
			ForEachEntity( BoundObjectComponents[Index], Slow[Index], VariadicComponents[Index]... );
		}
	}
}

} // namespace MovieScene
} // namespace UE