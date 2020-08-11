// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/ArrayView.h"

#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieSceneSystemTaskDependencies.h"
#include "EntitySystem/MovieScenePropertySystemTypes.h"
#include "EntitySystem/IMovieScenePropertyComponentHandler.h"

#include "Misc/InlineValue.h"
#include <initializer_list>


class UMovieSceneBlenderSystem;
class UMovieSceneEntitySystemLinker;

namespace UE
{
namespace MovieScene
{

struct FPropertyDefinition;
struct FFloatDecompositionParams;
struct FPropertyCompositeDefinition;


/**
 * Stats pertaining to a given type of property including how many properties exist in the linker,
 * and how many of those are partially animated
 */
struct FPropertyStats
{
	/** The total number of properties currently animated, including partial properties */
	int32 NumProperties = 0;

	/** The number of properties partially animated */
	int32 NumPartialProperties = 0;
};


/**
 * Structure defining a type of property that can be animated by sequencer
 */
struct FPropertyDefinition
{
	FPropertyDefinition() = default;
	FPropertyDefinition(FPropertyDefinition&&) = default;
	FPropertyDefinition(const FPropertyDefinition&) = delete;

	/** Pointer to a custom getter/setter registry for short circuiting the UObject VM. Must outlive this definitions lifetime (usually these are static or singletons) */
	ICustomPropertyRegistration* CustomPropertyRegistration;

	/** A mask of which composite indices pertain to floats */
	uint32 FloatCompositeMask;

	/** The number of channels that this property comprises */
	uint16 VariableSizeCompositeOffset;

	/** The number of channels that this property comprises */
	uint16 CompositeSize;

	/** The component type or tag of the property itself */
	FComponentTypeID PropertyType;

	/** (OPTIONAL) The component type for the property's pre-animated value */
	FComponentTypeID PreAnimatedValue;

	/** The component type for this property's inital value (used for relative and/or additive blending) */
	FComponentTypeID InitialValueType;

	/** Implementation of type specific property actions such as applying properties from entities or recomposing values */
	TInlineValue<IPropertyComponentHandler, 32> Handler;
};

/** A generic definition of a composite channel that contributes to a property */
struct FPropertyCompositeDefinition
{
	/** The type of component that contains the value for this channel (ie a TComponentTypeID<float>) */
	FComponentTypeID ComponentTypeID;

	/**
	 * The offset of the member variable within the operational type of the property in bytes.
	 * Ie for FIntermediate3DTransform::T_Z, the composite offset is 8 Bytes.
	 */
	uint16 CompositeOffset;
};

template<typename PropertyType, typename OperationalType> struct TPropertyDefinitionBuilder;
template<typename PropertyType, typename OperationalType, typename... Composites> struct TCompositePropertyDefinitionBuilder;

/**
 * Central registry of all property types animatable by sequencer.
 * Once registered, properties cannot be de-registered. This vastly simplifies the lifetime and ID management of the class
 */
class MOVIESCENE_API FPropertyRegistry
{
public:

	FPropertyRegistry() = default;

	FPropertyRegistry(FPropertyRegistry&&) = delete;
	FPropertyRegistry(const FPropertyRegistry&) = delete;

	/**
	 * Define a new animatable composite property type from its components.
	 * 
	 * @param InOutPropertyComponents  The property's components that are used for animating this property. TPropertyComponents::CompositeID is written to.
	 * @return A builder class that should be used to define the composites that contribute to this property
	 */
	template<typename PropertyType, typename OperationalType>
	TCompositePropertyDefinitionBuilder<PropertyType, OperationalType> DefineCompositeProperty(TPropertyComponents<PropertyType, OperationalType>& InOutPropertyComponents)
	{
		checkf(!InOutPropertyComponents.CompositeID, TEXT("Property already defined"));

		FCompositePropertyTypeID CompositeID = DefineCompositeProperty(InOutPropertyComponents.PropertyTag,InOutPropertyComponents.InitialValue,InOutPropertyComponents.PreAnimatedValue);
		static_cast<FCompositePropertyTypeID&>(InOutPropertyComponents.CompositeID) = CompositeID;

		return TCompositePropertyDefinitionBuilder<PropertyType, OperationalType>(&Properties[CompositeID.TypeIndex], this);
	}

	/**
	 * Define a new animatable property type from its components.
	 * 
	 * @param InOutPropertyComponents  The property's components that are used for animating this property. TPropertyComponents::CompositeID is written to.
	 * @return A builder class that should be used to define the composites that contribute to this property
	 */
	template<typename PropertyType, typename OperationalType>
	TPropertyDefinitionBuilder<PropertyType, OperationalType> DefineProperty(TPropertyComponents<PropertyType, OperationalType>& InOutPropertyComponents)
	{
		checkf(!InOutPropertyComponents.CompositeID, TEXT("Property already defined"));

		FCompositePropertyTypeID CompositeID = DefineCompositeProperty(InOutPropertyComponents.PropertyTag,InOutPropertyComponents.InitialValue,InOutPropertyComponents.PreAnimatedValue);
		static_cast<FCompositePropertyTypeID&>(InOutPropertyComponents.CompositeID) = CompositeID;

		return TPropertyDefinitionBuilder<PropertyType, OperationalType>(&Properties[CompositeID.TypeIndex], this);
	}

	/**
	 * Retrieve a property definition from its ID
	 */
	const FPropertyDefinition& GetDefinition(FCompositePropertyTypeID PropertyID) const
	{
		return Properties[PropertyID.TypeIndex];
	}

	/**
	 * Access all the properties currently registered
	 */
	TArrayView<const FPropertyDefinition> GetProperties() const
	{
		return Properties;
	}

	/**
	 * Retrieve a generic representation of all the composites that contribute to a given property
	 */
	TArrayView<const FPropertyCompositeDefinition> GetComposites(const FPropertyDefinition& Property) const
	{
		const int32 CompositeOffset = Property.VariableSizeCompositeOffset;
		const int32 NumComposites   = Property.CompositeSize;
		return MakeArrayView(CompositeDefinitions.GetData() + CompositeOffset, NumComposites);
	}

	/**
	 * Retrieve a generic representation of all the composites that contribute to a given property
	 */
	TArrayView<const FPropertyCompositeDefinition> GetComposites(FCompositePropertyTypeID PropertyID) const
	{
		return GetComposites(GetDefinition(PropertyID));
	}

private:

	template<typename PropertyType, typename OperationalType>
	friend struct TPropertyDefinitionBuilder;
	
	template<typename PropertyType, typename OperationalType, typename... Composites>
	friend struct TCompositePropertyDefinitionBuilder;

	FCompositePropertyTypeID DefineCompositeProperty(FComponentTypeID PropertyType, FComponentTypeID InitialValueType, FComponentTypeID PreAnimatedValueType);

	TArray<FPropertyDefinition> Properties;

	TArray<FPropertyCompositeDefinition> CompositeDefinitions;
};




} // namespace MovieScene
} // namespace UE





