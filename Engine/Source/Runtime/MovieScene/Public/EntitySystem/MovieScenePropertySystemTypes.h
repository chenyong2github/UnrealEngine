// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/NameTypes.h"
#include "Templates/SharedPointer.h"
#include "Templates/UnrealTypeTraits.h"
#include "EntitySystem/MovieSceneComponentAccessors.h"

class UClass;
class FTrackInstancePropertyBindings;


namespace UE
{
namespace MovieScene
{

struct FCustomPropertyIndex
{
	uint16 Value;
};

struct FCompositePropertyTypeID
{
	FCompositePropertyTypeID() : TypeIndex(INDEX_NONE) {}

	static FCompositePropertyTypeID FromIndex(int32 Index)
	{
		return FCompositePropertyTypeID{ Index };
	}

	int32 AsIndex() const
	{
		return TypeIndex;
	}

	explicit operator bool() const
	{
		return TypeIndex != INDEX_NONE;
	}

private:
	FCompositePropertyTypeID(int32 InTypeIndex) : TypeIndex(InTypeIndex) {}

	friend class FPropertyRegistry;
	int32 TypeIndex;
};


template<typename PropertyType, typename OperationalType>
struct TCompositePropertyTypeID : FCompositePropertyTypeID
{};


/**
 * Structure that defines 2 static function pointers that are to be used for retrieving and applying properties of a given type
 */
template<typename PropertyType>
struct TCustomPropertyAccessorFunctions
{
	using ParamType = typename TCallTraits<PropertyType>::ParamType;

	using GetterFunc = PropertyType (*)(const UObject* Object);
	using SetterFunc = void         (*)(UObject* Object, ParamType Value);

	/** Function pointer to be used for retrieving an object's current property */
	GetterFunc Getter;

	/** Function pointer to be used for applying a new value to an object's property */
	SetterFunc Setter;
};


struct FCustomPropertyAccessor
{
	/** The class of the object that the accessor applies to */
	UClass* Class;

	/** The complete path name to the property from the class specified above */
	FName PropertyPath;

	/** (Optional) An additional tag that should be applied alongside this property accessor component */
	FComponentTypeID AdditionalTag;
};

/**
 * Complete information required for applying a custom getter/setter to an object
 */
template<typename PropertyType>
struct TCustomPropertyAccessor : FCustomPropertyAccessor
{
	TCustomPropertyAccessor(UClass* InClass, FName InPropertyPath, const TCustomPropertyAccessorFunctions<PropertyType>& InFunctions)
		: FCustomPropertyAccessor{ InClass, InPropertyPath }
		, Functions(InFunctions)
	{}

	/** Function pointers to use for interacting with the property */
	TCustomPropertyAccessorFunctions<PropertyType> Functions;
};


struct FCustomAccessorView
{
	FCustomAccessorView()
		: Base(nullptr)
		, ViewNum(0)
		, Stride(0)
	{}

	template<typename T, typename Allocator>
	explicit FCustomAccessorView(const TArray<T, Allocator>& InArray)
		: Base(reinterpret_cast<const uint8*>(InArray.GetData()))
		, ViewNum(InArray.Num())
		, Stride(sizeof(T))
	{}

	const FCustomPropertyAccessor& operator[](int32 InIndex) const
	{
		return *reinterpret_cast<const FCustomPropertyAccessor*>(Base + InIndex*Stride);
	}

	int32 Num() const
	{
		return ViewNum;
	}

	int32 FindCustomAccessorIndex(UClass* ClassType, FName PropertyPath) const
	{
		UClass* StopIterationAt = UObject::StaticClass();

		while (ClassType != StopIterationAt)
		{
			for (int32 Index = 0; Index < ViewNum; ++Index)
			{
				const FCustomPropertyAccessor& Accessor = (*this)[Index];
				if (Accessor.Class == ClassType && Accessor.PropertyPath == PropertyPath)
				{
					return Index;
				}
			}
			ClassType = ClassType->GetSuperClass();
		}

		return INDEX_NONE;
	}

private:
	const uint8* Base;
	int32 ViewNum;
	int32 Stride;
};


struct ICustomPropertyRegistration
{
	virtual ~ICustomPropertyRegistration() {}

	virtual FCustomAccessorView GetAccessors() const = 0;
};

/** Generally static collection of accessors for a given type of property */
template<typename PropertyType, int InlineSize = 8>
struct TCustomPropertyRegistration : ICustomPropertyRegistration
{
	using GetterFunc = typename TCustomPropertyAccessorFunctions<PropertyType>::GetterFunc;
	using SetterFunc = typename TCustomPropertyAccessorFunctions<PropertyType>::SetterFunc;

	virtual FCustomAccessorView GetAccessors() const override
	{
		return FCustomAccessorView(CustomAccessors);
	}

	void Add(UClass* ClassType, FName PropertyName, GetterFunc Getter, SetterFunc Setter)
	{
		CustomAccessors.Add(TCustomPropertyAccessor<PropertyType>{ ClassType, PropertyName, { Getter, Setter } });
	}

private:

	/** */
	TArray<TCustomPropertyAccessor<PropertyType>, TInlineAllocator<InlineSize>> CustomAccessors;
};


template<typename PropertyType, typename OperationalType = PropertyType>
struct TPropertyComponents
{
	FComponentTypeID                  PropertyTag;
	TComponentTypeID<PropertyType>    PreAnimatedValue;
	TComponentTypeID<OperationalType> InitialValue;

	TCompositePropertyTypeID<PropertyType, OperationalType> CompositeID;
};

/**
 * Stateless entity task that will apply values to properties. Three types of property are supported: Custom native accessor functions, fast pointer offset, or FTrackInstancePropertyBindings
 * 
 * Can be invoked in one of 2 ways: either with a specific property type through a per-entity iteration:
 *
 *     TComponentTypeID<FCustomPropertyIndex> CustomProperty = ...;
 *     TComponentTypeID<FTransform> TransformComponent = ...;
 *     TComponentTypeID<UObject*> BoundObject = ...;
 *
 *     FEntityTaskBuilder()
 *     .Read(BoundObject)
 *     .Read(CustomProperty)
 *     .Read(TransformComponent)
 *     .Dispatch_PerEntity<TSetPropertyValues<FTransform>>( ... );
 *
 * Or via a combinatorial task that iterates all entities with any one of the property components:
 *
 *     TComponentTypeID<uint16> FastPropertyOffset = ...;
 *     TComponentTypeID<TSharedPtr<FTrackInstancePropertyBindings>> SlowProperty = ...;
 *
 *     FEntityTaskBuilder()
 *     .Read(BoundObject)
 *     .ReadOneOf(CustomProperty, FastProperty, SlowProperty)
 *     .Read(TransformComponent)
 *     .Dispatch_PerAllocation<TSetPropertyValues<FTransform>>( ... );
 */
template<typename PropertyType>
struct TSetPropertyValues
{
	using ParamType = typename TCallTraits<PropertyType>::ParamType;

	explicit TSetPropertyValues(ICustomPropertyRegistration* InCustomProperties)
		: CustomProperties(InCustomProperties)
	{}

	/**
	 * Run before this task executes any logic over entities and components
	 */
	void PreTask();


	/**
	 * Task callback that applies a value to an object property via a custom native setter function
	 * Must be invoked with a task builder with the specified parameters:
	 *
	 *     FEntityTaskBuilder()
	 *     .Read( TComponentTypeID<UObject*>(...) )
	 *     .Read( TComponentTypeID<FCustomPropertyIndex>(...) )
	 *     .Read( TComponentTypeID<PropertyType>(...) )
	 *     .Dispatch_PerEntity<TSetPropertyValues<PropertyType>>(...);
	 */
	void ForEachEntity(UObject* InObject, FCustomPropertyIndex CustomIndex, ParamType ValueToSet);
	static void ForEachEntity(UObject* InObject, const FCustomPropertyAccessor& CustomAccessor, ParamType ValueToSet);


	/**
	 * Task callback that applies a value to an object property via a fast pointer offset
	 * Must be invoked with a task builder with the specified parameters:
	 *
	 *     FEntityTaskBuilder()
	 *     .Read( TComponentTypeID<UObject*>(...) )
	 *     .Read( TComponentTypeID<uint16>(...) )
	 *     .Read( TComponentTypeID<PropertyType>(...) )
	 *     .Dispatch_PerEntity<TSetPropertyValues<PropertyType>>(...);
	 */
	static void ForEachEntity(UObject* InObject, uint16 PropertyOffset, ParamType ValueToSet);


	/**
	 * Task callback that applies a value to an object property via a slow (legacy) track instance binding
	 * Must be invoked with a task builder with the specified parameters:
	 *
	 *     FEntityTaskBuilder()
	 *     .Read( TComponentTypeID<UObject*>(...) )
	 *     .Read( TComponentTypeID<TSharedPtr<FTrackInstancePropertyBindings>>(...) )
	 *     .Read( TComponentTypeID<PropertyType>(...) )
	 *     .Dispatch_PerEntity<TSetPropertyValues<PropertyType>>(...);
	 */
	static void ForEachEntity(UObject* InObject, TSharedPtr<FTrackInstancePropertyBindings> PropertyBindings, ParamType ValueToSet);

public:

	using FThreeWayAccessor  = TMultiReadOptional<FCustomPropertyIndex, uint16, TSharedPtr<FTrackInstancePropertyBindings>>;
	using FTwoWayAccessor    = TMultiReadOptional<uint16, TSharedPtr<FTrackInstancePropertyBindings>>;

	/**
	 * Task callback that applies properties for a whole allocation of entities with either an FCustomPropertyIndex, uint16 or TSharedPtr<FTrackInstancePropertyBindings> property component
	 * Must be invoked with a task builder with the specified parameters:
	 *
	 *     FEntityTaskBuilder()
	 *     .Read(      TComponentTypeID<UObject*>(...) )
	 *     .ReadOneOf( TComponentTypeID<FCustomPropertyIndex>(...), TComponentTypeID<uint16>(...), TComponentTypeID<TSharedPtr<FTrackInstancePropertyBindings>>(...) )
	 *     .Read(      TComponentTypeID<PropertyType>(...) )
	 *     .Dispatch_PerAllocation<TSetPropertyValues<PropertyType>>(...);
	 */
	void ForEachAllocation(const FEntityAllocation* Allocation, TRead<UObject*> BoundObjectComponents, FThreeWayAccessor ResolvedPropertyComponents, TRead<PropertyType> PropertyValueComponents);


	/**
	 * Task callback that applies properties for a whole allocation of entities with either a uint16 or TSharedPtr<FTrackInstancePropertyBindings> property component
	 * Must be invoked with a task builder with the specified parameters:
	 *
	 *     FEntityTaskBuilder()
	 *     .Read(      TComponentTypeID<UObject*>(...) )
	 *     .ReadOneOf( TComponentTypeID<uint16>(...), TComponentTypeID<TSharedPtr<FTrackInstancePropertyBindings>>(...) )
	 *     .Read(      TComponentTypeID<PropertyType>(...) )
	 *     .Dispatch_PerAllocation<TSetPropertyValues<PropertyType>>(...);
	 */
	void ForEachAllocation(const FEntityAllocation* Allocation, TRead<UObject*> BoundObjectComponents, FTwoWayAccessor ResolvedPropertyComponents, TRead<PropertyType> PropertyValueComponents);

private:

	ICustomPropertyRegistration* CustomProperties;
	FCustomAccessorView CustomAccessors;
};


/**
 * Stateless entity task that writes current property values to the specified intermediate component.
 * Three types of property are supported: Custom native accessor functions, fast pointer offset, or FTrackInstancePropertyBindings.
 * 
 * Can be invoked in one of 2 ways: either with a specific property type through a per-entity iteration:
 *
 *     TComponentTypeID<FCustomPropertyIndex> CustomProperty = ...;
 *     TComponentTypeID<FMyOperationalType> IntermediateTransformComponent = ...;
 *     TComponentTypeID<UObject*> BoundObject = ...;
 *
 *     FEntityTaskBuilder()
 *     .Read(BoundObject)
 *     .Read(CustomProperty)
 *     .Write(IntermediateTransformComponent)
 *     .Dispatch_PerEntity<TGetPropertyValues<FTransform, FMyOperationalType>>( ... );
 *
 * Or via a combinatorial task that iterates all entities with any one of the property components:
 *
 *     TComponentTypeID<uint16> FastPropertyOffset = ...;
 *     TComponentTypeID<TSharedPtr<FTrackInstancePropertyBindings>> SlowProperty = ...;
 *
 *     FEntityTaskBuilder()
 *     .Read(BoundObject)
 *     .ReadOneOf(CustomProperty, FastProperty, SlowProperty)
 *     .Write(IntermediateTransformComponent)
 *     .Dispatch_PerAllocation<TGetPropertyValues<FTransform, FMyOperationalType>>( ... );
 */
template<typename PropertyType, typename OperationalType = PropertyType>
struct TGetPropertyValues
{
	explicit TGetPropertyValues(ICustomPropertyRegistration* InCustomProperties)
		: CustomProperties(InCustomProperties)
	{}

	void PreTask();

	/**
	 * Task callback that retrieves the object's current value via a custom native setter function, and writes it to the specified output variable
	 * Must be invoked with a task builder with the specified parameters:
	 *
	 *     FEntityTaskBuilder()
	 *     .Read( TComponentTypeID<UObject*>(...) )
	 *     .Read( TComponentTypeID<FCustomPropertyIndex>(...) )
	 *     .Write( TComponentTypeID<OperationalType>(...) )
	 *     .Dispatch_PerEntity<TGetPropertyValues<PropertyType, OperationalType>>(...);
	 */
	void ForEachEntity(UObject* InObject, FCustomPropertyIndex CustomPropertyIndex, OperationalType& OutValue);

	/**
	 * Task callback that retrieves the object's current value via a fast pointer offset, and writes it to the specified output variable
	 * Must be invoked with a task builder with the specified parameters:
	 *
	 *     FEntityTaskBuilder()
	 *     .Read( TComponentTypeID<UObject*>(...) )
	 *     .Read( TComponentTypeID<uint16>(...) )
	 *     .Read( TComponentTypeID<OperationalType>(...) )
	 *     .Dispatch_PerEntity<TGetPropertyValues<PropertyType, OperationalType>>(...);
	 */
	void ForEachEntity(UObject* InObject, uint16 PropertyOffset, OperationalType& OutValue);

	/**
	 * Task callback that retrieves the object's current value via a slow (legacy) track instance binding, and writes it to the specified output variable
	 * Must be invoked with a task builder with the specified parameters:
	 *
	 *     FEntityTaskBuilder()
	 *     .Read( TComponentTypeID<UObject*>(...) )
	 *     .Read( TComponentTypeID<TSharedPtr<FTrackInstancePropertyBindings>>(...) )
	 *     .Read( TComponentTypeID<OperationalType>(...) )
	 *     .Dispatch_PerEntity<TGetPropertyValues<PropertyType, OperationalType>>(...);
	 */
	void ForEachEntity(UObject* InObject, TSharedPtr<FTrackInstancePropertyBindings> PropertyBindings, OperationalType& OutValue);

public:

	using FThreeWayAccessor  = TMultiReadOptional<FCustomPropertyIndex, uint16, TSharedPtr<FTrackInstancePropertyBindings>>;
	using FTwoWayAccessor    = TMultiReadOptional<uint16, TSharedPtr<FTrackInstancePropertyBindings>>;


	/**
	 * Task callback that writes current property values for objects into an output component for a whole allocation of entities with either an FCustomPropertyIndex, uint16 or TSharedPtr<FTrackInstancePropertyBindings> property component
	 * Must be invoked with a task builder with the specified parameters:
	 *
	 *     FEntityTaskBuilder()
	 *     .Read(      TComponentTypeID<UObject*>(...) )
	 *     .ReadOneOf( TComponentTypeID<FCustomPropertyIndex>(...), TComponentTypeID<uint16>(...), TComponentTypeID<TSharedPtr<FTrackInstancePropertyBindings>>(...) )
	 *     .Write(     TComponentTypeID<OperationalType>(...) )
	 *     .Dispatch_PerAllocation<TGetPropertyValues<PropertyType, OperationalType>>(...);
	 */
	void ForEachAllocation(const FEntityAllocation* Allocation, TRead<UObject*> BoundObjectComponents, FThreeWayAccessor ResolvedPropertyComponents, TWrite<OperationalType> OutValueComponents);


	/**
	 * Task callback that writes current property values for objects into an output component for a whole allocation of entities with either a uint16 or TSharedPtr<FTrackInstancePropertyBindings> property component
	 * Must be invoked with a task builder with the specified parameters:
	 *
	 *     FEntityTaskBuilder()
	 *     .Read(      TComponentTypeID<UObject*>(...) )
	 *     .ReadOneOf( TComponentTypeID<uint16>(...), TComponentTypeID<TSharedPtr<FTrackInstancePropertyBindings>>(...) )
	 *     .Read(      TComponentTypeID<OperationalType>(...) )
	 *     .Dispatch_PerAllocation<TGetPropertyValues<PropertyType ,OperationalType>>(...);
	 */
	void ForEachAllocation(const FEntityAllocation* Allocation, TRead<UObject*> BoundObjectComponents, FTwoWayAccessor ResolvedPropertyComponents, TWrite<OperationalType> OutValueComponents);

private:

	ICustomPropertyRegistration* CustomProperties;
	FCustomAccessorView CustomAccessors;
};

template<typename...> struct TSetCompositePropertyValuesImpl;

/**
 * Task implementation that combines a specific set of input components (templated as CompositeTypes) through a projection, and applies the result to an object property
 * Three types of property are supported: Custom native accessor functions, fast pointer offset, or FTrackInstancePropertyBindings.
 * 
 * Can be invoked in one of 2 ways: either with a specific property type and input components through a per-entity iteration:
 *
 *     TComponentTypeID<FCustomPropertyIndex> CustomProperty = ...;
 *     TComponentTypeID<UObject*> BoundObject = ...;
 *
 *     FEntityTaskBuilder()
 *     .Read( BoundObject )
 *     .Read( CustomProperty )
 *     .Read( TComponentTypeID<float>(...) )
 *     .Read( TComponentTypeID<float>(...) )
 *     .Read( TComponentTypeID<float>(...) )
 *     .Dispatch_PerEntity<TSetCompositePropertyValues<FVector, float, float, float>>( ..., &UKismetMathLibrary::MakeVector );
 *
 * Or via a combinatorial task that iterates all entities with any one of the property components:
 *
 *     TComponentTypeID<uint16> FastPropertyOffset = ...;
 *     TComponentTypeID<TSharedPtr<FTrackInstancePropertyBindings>> SlowProperty = ...;
 *
 *     FEntityTaskBuilder()
 *     .Read(      BoundObject )
 *     .ReadOneOf( CustomProperty, FastPropertyOffset, SlowProperty )
 *     .Read(      TComponentTypeID<float>(...) )
 *     .Read(      TComponentTypeID<float>(...) )
 *     .Read(      TComponentTypeID<float>(...) )
 *     .Dispatch_Perllocation<TSetCompositePropertyValues<FVector, float, float, float>>( ..., &UKismetMathLibrary::MakeVector );
 */
template<typename PropertyType, typename ProjectionType, typename... CompositeTypes, int... Indices>
struct TSetCompositePropertyValuesImpl<TIntegerSequence<int, Indices...>, PropertyType, ProjectionType, CompositeTypes...>
{
	explicit TSetCompositePropertyValuesImpl(ICustomPropertyRegistration* InCustomProperties, ProjectionType&& InProjection)
		: CustomProperties(InCustomProperties)
		, Projection(InProjection)
	{}

	/**
	 * Run before this task executes any logic over entities and components
	 */
	void PreTask();

	/**
	 * Task callback that applies a value to an object property via a custom native setter function
	 * Must be invoked with a task builder with the specified parameters:
	 *
	 *     FEntityTaskBuilder()
	 *     .Read( TComponentTypeID<UObject*>(...) )
	 *     .Read( TComponentTypeID<FCustomPropertyIndex>(...) )
	 *     .Read( TComponentTypeID<CompositeType[0  ]>(...) )
	 *     .Read( TComponentTypeID<CompositeType[...]>(...) )
	 *     .Read( TComponentTypeID<CompositeType[N-1]>(...) )
	 *     .Dispatch_PerEntity<TSetCompositePropertyValues<PropertyType, CompositeTypes...>>(..., Projection);
	 */
	void ForEachEntity(UObject* InObject, FCustomPropertyIndex CustomPropertyIndex, typename TCallTraits<CompositeTypes>::ParamType... CompositeResults);


	/**
	 * Task callback that applies a value to an object property via a fast pointer offset
	 * Must be invoked with a task builder with the specified parameters:
	 *
	 *     FEntityTaskBuilder()
	 *     .Read( TComponentTypeID<UObject*>(...) )
	 *     .Read( TComponentTypeID<uint16>(...) )
	 *     .Read( TComponentTypeID<CompositeType[0  ]>(...) )
	 *     .Read( TComponentTypeID<CompositeType[...]>(...) )
	 *     .Read( TComponentTypeID<CompositeType[N-1]>(...) )
	 *     .Dispatch_PerEntity<TSetCompositePropertyValues<PropertyType, CompositeTypes...>>(...);
	 */
	void ForEachEntity(UObject* InObject, uint16 PropertyOffset, typename TCallTraits<CompositeTypes>::ParamType... CompositeResults);


	/**
	 * Task callback that applies a value to an object property via a slow (legacy) track instance binding
	 * Must be invoked with a task builder with the specified parameters:
	 *
	 *     FEntityTaskBuilder()
	 *     .Read( TComponentTypeID<UObject*>(...) )
	 *     .Read( TComponentTypeID<TSharedPtr<FTrackInstancePropertyBindings>>(...) )
	 *     .Read( TComponentTypeID<CompositeType[0  ]>(...) )
	 *     .Read( TComponentTypeID<CompositeType[...]>(...) )
	 *     .Read( TComponentTypeID<CompositeType[N-1]>(...) )
	 *     .Dispatch_PerEntity<TSetCompositePropertyValues<PropertyType, CompositeTypes...>>(...);
	 */
	void ForEachEntity(UObject* InObject, TSharedPtr<FTrackInstancePropertyBindings> PropertyBindings, typename TCallTraits<CompositeTypes>::ParamType... CompositeResults);

public:

	using FThreeWayAccessor  = TMultiReadOptional<FCustomPropertyIndex, uint16, TSharedPtr<FTrackInstancePropertyBindings>>;
	using FTwoWayAccessor    = TMultiReadOptional<uint16, TSharedPtr<FTrackInstancePropertyBindings>>;


	/**
	 * Task callback that applies properties for a whole allocation of entities with either an FCustomPropertyIndex, uint16 or TSharedPtr<FTrackInstancePropertyBindings> property component
	 * Must be invoked with a task builder with the specified parameters:
	 *
	 *     FEntityTaskBuilder()
	 *     .Read(      TComponentTypeID<UObject*>(...) )
	 *     .ReadOneOf( TComponentTypeID<FCustomPropertyIndex>(...), TComponentTypeID<uint16>(...), TComponentTypeID<TSharedPtr<FTrackInstancePropertyBindings>>(...) )
	 *     .Read( TComponentTypeID<CompositeType[0  ]>(...) )
	 *     .Read( TComponentTypeID<CompositeType[...]>(...) )
	 *     .Read( TComponentTypeID<CompositeType[N-1]>(...) )
	 *     .Dispatch_PerAllocation<TSetCompositePropertyValues<PropertyType, CompositeTypes...>>(...);
	 */
	void ForEachAllocation(const FEntityAllocation* Allocation, TRead<UObject*> BoundObjectComponents, FThreeWayAccessor ResolvedPropertyComponents, TRead<CompositeTypes>... VariadicComponents);


	/**
	 * Task callback that applies properties for a whole allocation of entities with either a uint16 or TSharedPtr<FTrackInstancePropertyBindings> property component
	 * Must be invoked with a task builder with the specified parameters:
	 *
	 *     FEntityTaskBuilder()
	 *     .Read(      TComponentTypeID<UObject*>(...) )
	 *     .ReadOneOf( TComponentTypeID<uint16>(...), TComponentTypeID<TSharedPtr<FTrackInstancePropertyBindings>>(...) )
	 *     .Read( TComponentTypeID<CompositeType[0  ]>(...) )
	 *     .Read( TComponentTypeID<CompositeType[...]>(...) )
	 *     .Read( TComponentTypeID<CompositeType[N-1]>(...) )
	 *     .Dispatch_PerAllocation<TSetCompositePropertyValues<PropertyType, CompositeTypes...>>(...);
	 */
	void ForEachAllocation(const FEntityAllocation* Allocation, TRead<UObject*> BoundObjectComponents, FTwoWayAccessor ResolvedPropertyComponents, TRead<CompositeTypes>... VariadicComponents);

private:

	ICustomPropertyRegistration* CustomProperties;
	FCustomAccessorView CustomAccessors;

	/** A projection of the signature PropertyType(CompositeTypes...) that combines all composite inpts and produces a PropertyType value to apply to the object property */
	ProjectionType Projection;
};

/** Entity task that will apply multiple values to properties via an accumulation projection */
template<typename PropertyType, typename... CompositeTypes>
using TSetCompositePropertyValues = TSetCompositePropertyValuesImpl<TMakeIntegerSequence<int, sizeof...(CompositeTypes)>, PropertyType, PropertyType (*)(CompositeTypes...), CompositeTypes...>;


} // namespace MovieScene
} // namespace UE