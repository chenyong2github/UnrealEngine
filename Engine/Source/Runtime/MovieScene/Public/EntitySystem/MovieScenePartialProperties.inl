// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieScenePartialProperties.h"
#include "MovieSceneCommonHelpers.h"

namespace UE
{
namespace MovieScene
{


template<typename InIntermediateType, typename... ProjectionTypes>
void TPartialProjections<InIntermediateType, ProjectionTypes...>::Patch(IntermediateType* Properties, const FEntityAllocation* Allocation, int32 Num) const
{
	auto Visit = [Properties, Allocation, Num](auto& It)
	{
		using ComponentType = typename TDecay<decltype(It)>::Type::ComponentType;

		const FComponentHeader* Header = It.ComponentTypeID ? Allocation->FindComponentHeader(It.ComponentTypeID) : nullptr;
		if (Header)
		{
			Header->ReadWriteLock.ReadLock();

			IntermediateType*    ThisProperty  = Properties;
			const ComponentType* ThisComposite = reinterpret_cast<const ComponentType*>(Header->Components);

			for (int32 EntityIndex = 0; EntityIndex < Num; ++EntityIndex)
			{
				Invoke(It.Projection, *ThisProperty++, *ThisComposite++);
			}

			Header->ReadWriteLock.ReadUnlock();
		}
	};
	VisitTupleElements(Visit, Composites);
}


template<typename InIntermediateType, typename ProjectionType, int NumCompositeTypes>
void THomogenousPartialProjections<InIntermediateType, ProjectionType, NumCompositeTypes>::Patch(IntermediateType* Properties, const FEntityAllocation* Allocation, int32 Num) const
{
	using ComponentType = typename ProjectionType::ComponentType;

	for (int32 CompositeIndex = 0; CompositeIndex < NumCompositeTypes; ++CompositeIndex)
	{
		const FComponentHeader* Header = Composites[CompositeIndex].ComponentTypeID ? Allocation->FindComponentHeader(Composites[CompositeIndex].ComponentTypeID) : nullptr;
		if (Header)
		{
			Header->ReadWriteLock.ReadLock();

			IntermediateType*    ThisProperty  = Properties;
			const ComponentType* ThisComposite = reinterpret_cast<const ComponentType*>(Header->Components);

			for (int32 EntityIndex = 0; EntityIndex < Num; ++EntityIndex)
			{
				Invoke(Composites[CompositeIndex].Projection, *ThisProperty++, *ThisComposite++);
			}

			Header->ReadWriteLock.ReadUnlock();
		}
	}
}


template<typename PropertyType, typename ProjectionType>
TSetPartialPropertyValues<PropertyType, ProjectionType>::TSetPartialPropertyValues(ProjectionType&& InProjections)
	: Projections(MoveTemp(InProjections))
{}


template<typename PropertyType, typename ProjectionType>
TSetPartialPropertyValues<PropertyType, ProjectionType>::TSetPartialPropertyValues(const ProjectionType& InProjections)
	: Projections(InProjections)
{}


template<typename PropertyType, typename ProjectionType>
void TSetPartialPropertyValues<PropertyType, ProjectionType>::ForEachAllocation(const FEntityAllocation* Allocation, TRead<UObject*> BoundObjectComponents, FThreeWayAccessor PropertyBindingComponents)
{
	using FPropertyTuple = TTuple< const FCustomPropertyIndex*, const uint16*, const TSharedPtr<FTrackInstancePropertyBindings>* >;

	// ----------------------------------------------------------------------------------------------------------------------------
	// For partially animated composites, we first retrieve the current properties for the allocation, then go through and patch in
	// All the animated values, then apply the properties to objects

	const int32 Num = Allocation->Num();
	IntermediateValues.SetNumUninitialized(Num);

	UObject* const* RawObjectPtr = BoundObjectComponents.Resolve(Allocation);
	FPropertyTuple  Properties   = PropertyBindingComponents.Resolve(Allocation);

	if (const FCustomPropertyIndex* Custom = Properties.template Get<0>())
	{
		ForEachCustom(Allocation, MakeArrayView(RawObjectPtr, Num), MakeArrayView(Custom, Num));
	}
	else if (const uint16* Fast = Properties.template Get<1>())
	{
		ForEachFast(Allocation, MakeArrayView(RawObjectPtr, Num), MakeArrayView(Fast, Num));
	}
	else if (const TSharedPtr<FTrackInstancePropertyBindings>* Slow = Properties.template Get<2>())
	{
		ForEachSlow(Allocation, MakeArrayView(RawObjectPtr, Num), MakeArrayView(Slow, Num));
	}
}


template<typename PropertyType, typename ProjectionType>
void TSetPartialPropertyValues<PropertyType, ProjectionType>::ForEachAllocation(const FEntityAllocation* Allocation, TRead<UObject*> BoundObjectComponents, FTwoWayAccessor PropertyBindingComponents)
{
	using FPropertyTuple = TTuple< const uint16*, const TSharedPtr<FTrackInstancePropertyBindings>* >;

	// ----------------------------------------------------------------------------------------------------------------------------
	// For partially animated composites, we first retrieve the current properties for the allocation, then go through and patch in
	// All the animated values, then apply the properties to objects

	const int32 Num = Allocation->Num();
	IntermediateValues.SetNumUninitialized(Num);

	UObject* const* RawObjectPtr = BoundObjectComponents.Resolve(Allocation);
	FPropertyTuple  Properties   = PropertyBindingComponents.Resolve(Allocation);

	if (const uint16* Fast = Properties.template Get<0>())
	{
		ForEachFast(Allocation, MakeArrayView(RawObjectPtr, Num), MakeArrayView(Fast, Num));
	}
	else if (const TSharedPtr<FTrackInstancePropertyBindings>* Slow = Properties.template Get<1>())
	{
		ForEachSlow(Allocation, MakeArrayView(RawObjectPtr, Num), MakeArrayView(Slow, Num));
	}
}


template<typename PropertyType, typename ProjectionType>
void TSetPartialPropertyValues<PropertyType, ProjectionType>::ForEachCustom(const FEntityAllocation* Allocation, TArrayView<UObject* const> Objects, TArrayView<const FCustomPropertyIndex> Custom)
{
	const int32 Num = Objects.Num();

	for (int32 Index = 0; Index < Num; ++Index)
	{
		const FCustomPropertyIndex PropertyIndex = Custom[Index];
		const TCustomPropertyAccessor<PropertyType>& CustomAccessor = static_cast<const TCustomPropertyAccessor<PropertyType>&>(CustomAccessors[PropertyIndex.Value]);

		PropertyType CurrentValue = CustomAccessor.Functions.Getter(Objects[Index]);
		ConvertOperationalProperty(CurrentValue, IntermediateValues[Index]);
	}

	Projections.Patch(IntermediateValues.GetData(), Allocation, Num);

	for (int32 Index = 0; Index < Num; ++Index)
	{
		const FCustomPropertyIndex PropertyIndex = Custom[Index];
		const TCustomPropertyAccessor<PropertyType>& CustomAccessor = static_cast<const TCustomPropertyAccessor<PropertyType>&>(CustomAccessors[PropertyIndex.Value]);

		PropertyType NewValue;
		ConvertOperationalProperty(IntermediateValues[Index], NewValue);
		CustomAccessor.Functions.Setter(Objects[Index], NewValue);
	}
}


template<typename PropertyType, typename ProjectionType>
void TSetPartialPropertyValues<PropertyType, ProjectionType>::ForEachFast(const FEntityAllocation* Allocation, TArrayView<UObject* const> Objects, TArrayView<const uint16> Fast)
{
	const int32 Num = Objects.Num();

	for (int32 Index = 0; Index < Num; ++Index)
	{
		const uint16 PropertyOffset = Fast[Index];
		checkSlow(PropertyOffset != 0);

		const PropertyType* CurrentValue = reinterpret_cast<const PropertyType*>( reinterpret_cast<const uint8*>(Objects[Index]) + PropertyOffset );
		ConvertOperationalProperty(*CurrentValue, IntermediateValues[Index]);
	}

	Projections.Patch(IntermediateValues.GetData(), Allocation, Num);

	for (int32 Index = 0; Index < Num; ++Index)
	{
		PropertyType* Property = reinterpret_cast<PropertyType*>( reinterpret_cast<uint8*>(Objects[Index]) + Fast[Index] );
		ConvertOperationalProperty(IntermediateValues[Index], *Property);
	}
}


template<typename PropertyType, typename ProjectionType>
void TSetPartialPropertyValues<PropertyType, ProjectionType>::ForEachSlow(const FEntityAllocation* Allocation, TArrayView<UObject* const> Objects, TArrayView<const TSharedPtr<FTrackInstancePropertyBindings>> Slow)
{
	const int32 Num = Objects.Num();

	for (int32 Index = 0; Index < Num; ++Index)
	{
		PropertyType CurrentValue = Slow[Index]->GetCurrentValue<PropertyType>(*Objects[Index]);
		ConvertOperationalProperty(CurrentValue, IntermediateValues[Index]);
	}

	Projections.Patch(IntermediateValues.GetData(), Allocation, Num);

	for (int32 Index = 0; Index < Num; ++Index)
	{
		PropertyType NewValue;
		ConvertOperationalProperty(IntermediateValues[Index], NewValue);
		Slow[Index]->CallFunction<PropertyType>(*Objects[Index], NewValue);
	}
}


} // namespace MovieScene
} // namespace UE