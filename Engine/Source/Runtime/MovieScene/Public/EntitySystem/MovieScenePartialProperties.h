// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "EntitySystem/MovieSceneComponentAccessors.h"
#include "EntitySystem/MovieSceneEntitySystemTypes.h"
#include "EntitySystem/MovieScenePropertySystemTypes.h"


class UMovieSceneEntitySystemLinker;
class FTrackInstancePropertyBindings;

namespace UE
{
namespace MovieScene
{

struct FComponentMask;

template<typename InComponentType, typename ProjectionType>
struct TPartialProjection
{
	using ComponentType = InComponentType;

	ProjectionType Projection;
	TComponentTypeID<ComponentType> ComponentTypeID;
};


template<typename InIntermediateType, typename... ProjectionTypes>
struct TPartialProjections
{
	using IntermediateType = InIntermediateType;

	void Patch(IntermediateType* Properties, const FEntityAllocation* Allocation, int32 Num) const;

	TTuple< ProjectionTypes... > Composites;
};


template<typename InIntermediateType, typename ProjectionType, int NumCompositeTypes>
struct THomogenousPartialProjections
{
	using IntermediateType = InIntermediateType;

	void Patch(IntermediateType* Properties, const FEntityAllocation* Allocation, int32 Num) const;

	ProjectionType Composites[NumCompositeTypes];
};


template<typename PropertyType, typename ProjectionType>
struct TSetPartialPropertyValues
{
	using IntermediateType = typename ProjectionType::IntermediateType;

	using FThreeWayAccessor  = TMultiReadOptional<FCustomPropertyIndex, uint16, TSharedPtr<FTrackInstancePropertyBindings>>;
	using FTwoWayAccessor    = TMultiReadOptional<uint16, TSharedPtr<FTrackInstancePropertyBindings>>;

	explicit TSetPartialPropertyValues(ICustomPropertyRegistration* InCustomProperties, const ProjectionType& InProjections)
		: CustomProperties(InCustomProperties)
		, Projections(InProjections)
	{}

	/**
	 * Run before this task executes any logic over entities and components
	 */
	void PreTask()
	{
		if (CustomProperties)
		{
			CustomAccessors = CustomProperties->GetAccessors();
		}
	}

	TSetPartialPropertyValues(ProjectionType&& InProjections);
	TSetPartialPropertyValues(const ProjectionType& InProjections);

	void ForEachAllocation(const FEntityAllocation* Allocation, TRead<UObject*> BoundObjectComponents, FThreeWayAccessor PropertyBindingComponents);
	void ForEachAllocation(const FEntityAllocation* Allocation, TRead<UObject*> BoundObjectComponents, FTwoWayAccessor PropertyBindingComponents);

private:

	void ForEachCustom(const FEntityAllocation* Allocation, TArrayView<UObject* const> Objects, TArrayView<const FCustomPropertyIndex> Custom);

	void ForEachFast(const FEntityAllocation* Allocation, TArrayView<UObject* const> Objects, TArrayView<const uint16> Fast);

	void ForEachSlow(const FEntityAllocation* Allocation, TArrayView<UObject* const> Objects, TArrayView<const TSharedPtr<FTrackInstancePropertyBindings>> Slow);

private:

	ICustomPropertyRegistration* CustomProperties;
	FCustomAccessorView CustomAccessors;
	TArray<IntermediateType> IntermediateValues;
	ProjectionType Projections;
};


} // namespace MovieScene
} // namespace UE