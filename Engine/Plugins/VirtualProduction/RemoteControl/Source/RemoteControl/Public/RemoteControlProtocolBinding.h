// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Class.h"
#include "UObject/StructOnScope.h"

#include "RemoteControlProtocolBinding.generated.h"

class FCborWriter;
class URemoteControlPreset;

struct FRemoteControlProtocolBinding;
struct FRemoteControlProtocolEntity;
struct FRemoteControlProtocolMapping;
struct FExposedProperty;
struct FRCObjectReference;

using FGetProtocolMappingCallback = TFunctionRef<void(FRemoteControlProtocolMapping&)>;

/*
 * Mapping of the range of the values for the protocol
 * This class holds a generic range buffer.
 * For example, it could be FFloatProperty 4 bytes
 * Or it could be any UScripSctruct, like FVector - 12 bytes
 * Or any custom struct, arrays, maps, sets, or primitive properties
 */
USTRUCT()
struct REMOTECONTROL_API FRemoteControlProtocolMapping
{
	GENERATED_BODY()

	friend struct FRemoteControlProtocolBinding;
	friend struct FRemoteControlProtocolEntity;

	friend uint32 REMOTECONTROL_API GetTypeHash(const FRemoteControlProtocolMapping& InProtocolMapping);

private:
	/**
	 * Traits class which tests if a range value type is primitive type.
	 */
	template <typename T>
	struct TIsSupportedRangeValueType
	{
		enum { Value = false };
	};

	template <typename PropertyType>
	struct TIsSupportedRangePropertyTypeBase
	{
		enum { Value = false };
	};

	template <typename PropertyType, typename Enable = void>
	struct TIsSupportedRangePropertyType : TIsSupportedRangePropertyType<PropertyType> {};

	template <typename PropertyType>
    struct TIsSupportedRangePropertyType<
            PropertyType,
            typename TEnableIf<TIsDerivedFrom<PropertyType, FNumericProperty>::Value>::Type>
        : TIsSupportedRangePropertyTypeBase<PropertyType>
	{
		enum
		{
			Value = true
        };
	};

public:
	FRemoteControlProtocolMapping() = default;
	FRemoteControlProtocolMapping(FProperty* InProperty, uint8 InRangeValueSize);

	bool operator==(const FRemoteControlProtocolMapping& InProtocolMapping) const;
	bool operator==(FGuid InProtocolMappingId) const;

public:
	/** Get Binding Range Id. */
	const FGuid& GetId() const { return Id; }

	/** Get Binding Range Value */
	template<typename ValueType>
	ValueType GetRangeValue()
	{
		check(TIsSupportedRangeValueType<ValueType>::Value);
		check(InterpolationRangePropertyData.Num() && InterpolationRangePropertyData.Num() == sizeof(ValueType));
		return *reinterpret_cast<ValueType*>(InterpolationRangePropertyData.GetData());
	}

	/** Get Binding Range Struct Value */
	template <typename ValueType, typename PropertyType>
	typename TEnableIf<TIsSame<FStructProperty, PropertyType>::Value, ValueType>::Type
	GetRangeValue()
	{
		check(InterpolationRangePropertyData.Num() && InterpolationRangePropertyData.Num() == sizeof(ValueType))
		return *reinterpret_cast<ValueType*>(InterpolationRangePropertyData.GetData());
	}
	
	/** Set Binding Range Value based on template value input */
	template<typename ValueType>
	void SetRangeValue(ValueType InRangeValue)
	{
		check(TIsSupportedRangeValueType<ValueType>::Value);
		check(InterpolationRangePropertyData.Num() && InterpolationRangePropertyData.Num() == sizeof(ValueType));
		*reinterpret_cast<ValueType*>(InterpolationRangePropertyData.GetData()) = InRangeValue;
	}

	/** Set Binding Range Struct Value based on templated value input */
	template<typename ValueType, typename PropertyType>
	typename TEnableIf<TIsSame<FStructProperty, PropertyType>::Value, void>::Type
	SetRangeValue(ValueType InRangeValue)
	{
		check(InterpolationRangePropertyData.Num() && InterpolationRangePropertyData.Num() == sizeof(ValueType));
		*reinterpret_cast<ValueType*>(InterpolationRangePropertyData.GetData()) = InRangeValue;
	}

	/** Get Mapping Property Value as a primitive type */
	template<typename ValueType>
	ValueType GetMappingValueAsPrimitive()
	{
		check(TIsSupportedRangeValueType<ValueType>::Value);
		check(InterpolationMappingPropertyData.Num() && InterpolationMappingPropertyData.Num() == sizeof(ValueType));
		return *reinterpret_cast<ValueType*>(InterpolationMappingPropertyData.GetData());
	}

	/** Get Mapping Property Struct Value as a primitive type */
	template<typename ValueType, typename PropertyType>
	typename TEnableIf<TIsSame<FStructProperty, PropertyType>::Value, ValueType>::Type
    GetMappingValueAsPrimitive()
	{
		check(InterpolationMappingPropertyData.Num() && InterpolationMappingPropertyData.Num() == sizeof(ValueType));
		return *reinterpret_cast<ValueType*>(InterpolationMappingPropertyData.GetData());
	}

	/** Set primitive Mapping Property Value based on template value input */
	template<typename ValueType>
	void SetRangeValueAsPrimitive(ValueType InMappingPropertyValue)
	{
		check(TIsSupportedRangeValueType<ValueType>::Value);
		check(InterpolationMappingPropertyData.Num() && InterpolationMappingPropertyData.Num() == sizeof(ValueType));
		*reinterpret_cast<ValueType*>(InterpolationMappingPropertyData.GetData()) = InMappingPropertyValue;
	}

	/** Set primitive Mapping Property Struct Value based on template value input */
	template<typename ValueType, typename PropertyType>
	typename TEnableIf<TIsSame<FStructProperty, PropertyType>::Value, void>::Type
    SetRangeValueAsPrimitive(ValueType InMappingPropertyValue)
	{
		check(InterpolationMappingPropertyData.Num() && InterpolationMappingPropertyData.Num() == sizeof(ValueType));
		*reinterpret_cast<ValueType*>(InterpolationMappingPropertyData.GetData()) = InMappingPropertyValue;
	}

	/** Copies the underlying InterpolationRangePropertyData to the given destination, using the input property for type information */
	template <typename PropertyType>
	bool CopyRawRangeData(const PropertyType* InProperty, void* OutDestination)
	{
		return CopyRawData(InterpolationRangePropertyData, InProperty, OutDestination);
	}

	/** Copies the underlying InterpolationRangePropertyData to the given destination, using the input property for type information */
	bool CopyRawRangeData(const FProperty* InProperty, void* OutDestination);

	/** Sets the underlying InterpolationRangePropertyData to the given source, using the input property for type information */
	template <typename PropertyType>
    bool SetRawRangeData(URemoteControlPreset* InOwningPreset, const PropertyType* InProperty, const void* InSource)
	{
		return SetRawData(InOwningPreset, InterpolationRangePropertyData, InProperty, InSource);
	}

	/** Sets the underlying InterpolationRangePropertyData to the given source, using the input property for type information */
	bool SetRawRangeData(URemoteControlPreset* InOwningPreset, const FProperty* InProperty, const void* InSource);

	/** Copies the underlying InterpolationMappingPropertyData to the given destination, using the input property for type information */
	template <typename PropertyType>
    bool CopyRawMappingData(const PropertyType* InProperty, void* OutDestination)
	{
		return CopyRawData(InterpolationMappingPropertyData, InProperty, OutDestination);
	}

	/** Copies the underlying InterpolationMappingPropertyData to the given destination, using the input property for type information */
	bool CopyRawMappingData(const FProperty* InProperty, void* OutDestination);

	/** Sets the underlying InterpolationMappingPropertyData to the given source, using the input property for type information */
	template <typename PropertyType>
	bool SetRawMappingData(URemoteControlPreset* InOwningPreset, const PropertyType* InProperty, const void* InSource)
	{
		return SetRawData(InterpolationMappingPropertyData, InProperty, InSource);
	}

	/** Sets the underlying InterpolationMappingPropertyData to the given source, using the input property for type information */
	bool SetRawMappingData(URemoteControlPreset* InOwningPreset, const FProperty* InProperty, const void* InSource);
	
	/** Get Mapping value as a Struct on Scope, only in case BoundProperty is FStructProperty */
	TSharedPtr<FStructOnScope> GetMappingPropertyAsStructOnScope();

private:
	/** Checks that the source data matches the expected size of the property */
	template <typename PropertyType>
    bool PropertySizeMatchesData(const TArray<uint8>& InSource, const PropertyType* InProperty);
	
	/** Shared code between ranges/mapping data */
	template <typename PropertyType>
	bool CopyRawData(const TArray<uint8>& InSource, const PropertyType* InProperty, void* OutDestination)
	{
		static_assert(TIsDerivedFrom<PropertyType, FProperty>::Value, "PropertyType must derive from FProperty");

		check(InProperty);
		check(OutDestination);

		bool bCanCopy = ensureMsgf(TIsSupportedRangePropertyType<PropertyType>::Value, TEXT("PropertyType %s is unsupported."), *PropertyType::StaticClass()->GetName());
		bCanCopy &= ensureMsgf(PropertySizeMatchesData(InSource, InProperty), TEXT("PropertyType %s didn't match data size."), *PropertyType::StaticClass()->GetName());

		if(bCanCopy)
		{
			FMemory::Memcpy((uint8*)OutDestination, InSource.GetData(), InSource.Num());
		}

		return bCanCopy;
	}

	bool CopyRawData(const TArray<uint8>& InSource, const FProperty* InProperty, void* OutDestination);

	/** Shared code between ranges/mapping data */
	template <typename PropertyType>
	bool SetRawData(URemoteControlPreset* InOwningPreset, TArray<uint8>& OutDestination, const PropertyType* InProperty, const void* InSource)
	{
		static_assert(TIsDerivedFrom<PropertyType, FProperty>::Value, "PropertyType must derive from FProperty");

		check(InProperty);
		check(InSource);

		bool bCanCopy = ensureMsgf(TIsSupportedRangePropertyType<PropertyType>::Value, TEXT("PropertyType %s is unsupported."), *PropertyType::StaticClass()->GetName());
		bCanCopy &= ensureMsgf(PropertySizeMatchesData(OutDestination, InProperty), TEXT("PropertyType %s didn't match data size."), *PropertyType::StaticClass()->GetName());

		if(bCanCopy)
		{
			FMemory::Memcpy(OutDestination.GetData(), (uint8*)InSource, OutDestination.Num());
		}

		return bCanCopy;
	}

	bool SetRawData(URemoteControlPreset* InOwningPreset, TArray<uint8>& OutDestination, const FProperty* InProperty, const void* InSource);

private:
	/** Unique Id of the current binding */
	UPROPERTY()
	FGuid Id;

	/**
	 * The current integer value of the mapping range. It could be a float, int32, uint8, etc.
	 * That is based on protocol input data.
	 * 
	 *  For example, it could be uint8 in the case of one-byte mapping. In this case, the range value could be from 0 up to 255, which bound to InterpolationMappingPropertyData
	 */
	UPROPERTY()
	TArray<uint8> InterpolationRangePropertyData;

	/** 
	 * Holds the mapped property data buffer. 
	 * It could be the buffer for primitive FNumericProperty or FStructProperty or any container FProperty like FArrayProperty
	 */
	UPROPERTY()
	TArray<uint8> InterpolationMappingPropertyData;

	/** Holds the bound property path */
	UPROPERTY()
	TFieldPath<FProperty> BoundPropertyPath;
};

template <> struct FRemoteControlProtocolMapping::TIsSupportedRangeValueType<int8>		{ enum { Value = true }; };
template <> struct FRemoteControlProtocolMapping::TIsSupportedRangeValueType<int16>		{ enum { Value = true }; };
template <> struct FRemoteControlProtocolMapping::TIsSupportedRangeValueType<int32>		{ enum { Value = true }; };
template <> struct FRemoteControlProtocolMapping::TIsSupportedRangeValueType<int64>		{ enum { Value = true }; };
template <> struct FRemoteControlProtocolMapping::TIsSupportedRangeValueType<uint8>		{ enum { Value = true }; };
template <> struct FRemoteControlProtocolMapping::TIsSupportedRangeValueType<uint16>	{ enum { Value = true }; };
template <> struct FRemoteControlProtocolMapping::TIsSupportedRangeValueType<uint32>	{ enum { Value = true }; };
template <> struct FRemoteControlProtocolMapping::TIsSupportedRangeValueType<uint64>	{ enum { Value = true }; };
template <> struct FRemoteControlProtocolMapping::TIsSupportedRangeValueType<float>		{ enum { Value = true }; };
template <> struct FRemoteControlProtocolMapping::TIsSupportedRangeValueType<double>	{ enum { Value = true }; };
template <> struct FRemoteControlProtocolMapping::TIsSupportedRangeValueType<bool>		{ enum { Value = true }; };

template <> struct FRemoteControlProtocolMapping::TIsSupportedRangeValueType<FVector>		{ enum { Value = true }; };
template <> struct FRemoteControlProtocolMapping::TIsSupportedRangeValueType<FRotator>		{ enum { Value = true }; };
template <> struct FRemoteControlProtocolMapping::TIsSupportedRangeValueType<FLinearColor>	{ enum { Value = true }; };

/** Currently all numeric types are supported so we can shortcut the above (rather than doing a series of CastField's) */
template <> struct FRemoteControlProtocolMapping::TIsSupportedRangePropertyType<FNumericProperty> : TIsSupportedRangePropertyTypeBase<FNumericProperty> { enum { Value = true }; };

template <> struct FRemoteControlProtocolMapping::TIsSupportedRangePropertyType<FEnumProperty> : TIsSupportedRangePropertyTypeBase<FEnumProperty> { enum { Value = false }; };

template <> struct FRemoteControlProtocolMapping::TIsSupportedRangePropertyType<FBoolProperty> : TIsSupportedRangePropertyTypeBase<FBoolProperty> { enum { Value = TIsSupportedRangeValueType<bool>::Value }; };

template <> struct FRemoteControlProtocolMapping::TIsSupportedRangePropertyType<FStructProperty> : TIsSupportedRangePropertyTypeBase<FStructProperty> { enum { Value = true }; };

template <>
inline bool FRemoteControlProtocolMapping::PropertySizeMatchesData<FBoolProperty>(const TArray<uint8>& InSource, const FBoolProperty* InProperty)
{
	return ensure(sizeof(bool) == InSource.Num());
}

template <>
inline bool FRemoteControlProtocolMapping::PropertySizeMatchesData<FNumericProperty>(const TArray<uint8>& InSource, const FNumericProperty* InProperty)
{
	return ensure(InProperty->ElementSize == InSource.Num());
}

template <>
inline bool FRemoteControlProtocolMapping::PropertySizeMatchesData<FStructProperty>(const TArray<uint8>& InSource, const FStructProperty* InProperty)
{
	UScriptStruct* ScriptStruct = InProperty->Struct;
	return ensure(ScriptStruct->GetStructureSize() == InSource.Num());
}

/**
 * These structures serve both as properties mapping as well as UI generation
 * Protocols should implement it based on the parameters they need.
 */
USTRUCT()
struct REMOTECONTROL_API FRemoteControlProtocolEntity
{
	GENERATED_BODY()

	friend FRemoteControlProtocolBinding;

public:
	virtual ~FRemoteControlProtocolEntity(){}

	/**
	 * Initialize after allocation
	 * @param InOwner The preset that owns this entity.
	 * @param InPropertyId exposed property id.
	 */
	void Init(URemoteControlPreset* InOwner, FGuid InPropertyId);

	/** Get exposed property id */
	const FGuid& GetPropertyId() const { return PropertyId; }

	/**
	 * Interpolate and apply protocol value to the property
	 * @param InProtocolValue double value from the protocol
	 * @return true of applied successfully
	 */
	bool ApplyProtocolValueToProperty(double InProtocolValue) const;

	/** 
	 * Get bound range property. For example, the range could be bound to FFloatProperty or FIntProperty, etc.
	 * It could be 0.f bound to FStructProperty or 0 bound to FBoolProperty where 0.f or 0 are Range PropertyName
	 * Each protocol defines its own binding property type.
	 */
	virtual FName GetRangePropertyName() const { return NAME_None; }

	/** Get Size of the range property value */
	uint8 GetRangePropertySize() const;

private:

	/**
	 * Serialize interpolated property value to Cbor buffer
	 * @param InProperty Property to apply serialization 
	 * @param InProtocolValue double value from the protocol
	 * @param OutBuffer serialized buffer
	 * @return true if serialized correctly
	 */
	bool GetInterpolatedPropertyBuffer(FProperty* InProperty, double InProtocolValue, TArray<uint8>& OutBuffer) const;

private:
	/** Get Ranges and Mapping Value pointers */
	TArray<TPair<const uint8*, const uint8*>> GetRangeMappingBuffers() const;

protected:
	/** The preset that owns this entity. */
	UPROPERTY()
	TWeakObjectPtr<URemoteControlPreset> Owner;

	/** Exposed property Id */
	UPROPERTY()
	FGuid PropertyId;

	/** Should property generate transaction events? */
	UPROPERTY(EditAnywhere, Category = Mapping)
	bool bGenerateTransaction = true;

protected:
	/** 
	 * Property mapping ranges set
	 * Stores protocol mapping for this protocol binding entity
	 */
	UPROPERTY()
	TSet<FRemoteControlProtocolMapping> Mappings;
};

/**
 * Struct which holds the bound struct and serialized struct archive
 */
USTRUCT()
struct REMOTECONTROL_API FRemoteControlProtocolBinding
{
	GENERATED_BODY()

	friend uint32 REMOTECONTROL_API GetTypeHash(const FRemoteControlProtocolBinding& InProtocolBinding);

public:
	FRemoteControlProtocolBinding() = default;
	FRemoteControlProtocolBinding(const FName InProtocolName, const FGuid& InPropertyId, TSharedPtr<TStructOnScope<FRemoteControlProtocolEntity>> InRemoteControlProtocolEntityPtr);

	bool operator==(const FRemoteControlProtocolBinding& InProtocolBinding) const;
	bool operator==(FGuid InProtocolBindingId) const;

public:

	/** Get protocol binding id */
	const FGuid& GetId() const { return Id; }

	/** Get protocol bound protocol name, such as MIDI, DMX, OSC, etc */
	FName GetProtocolName() const { return ProtocolName; }

	/** Get exposed property id */
	const FGuid& GetPropertyId() const { return PropertyId; }

	/** Get bound struct scope wrapper */
	TSharedPtr<FStructOnScope> GetStructOnScope() const;

	/** Get pointer to the StructOnScope */
	TSharedPtr<TStructOnScope<FRemoteControlProtocolEntity>> GetRemoteControlProtocolEntityPtr() const { return RemoteControlProtocolEntityPtr; }

	/**
	 * Add binding mapping to the protocol bound struct
	 * @param InProtocolMapping input mapping with value range and value data
	 */
	void AddMapping(const FRemoteControlProtocolMapping& InProtocolMapping);

	/**
	 * Remove the bound struct mapping based on given mapping id
	 * @param InMappingId mapping unique id
	 * @return The number of elements removed.
	 */
	int32 RemoveMapping(const FGuid& InMappingId);

	/**
	 * Empty all mapping for the bound struct
	 */
	void ClearMappings();

	/**
	 * Remove the bound struct mapping based on given mapping id
	 * @param InMappingId mapping unique id
	 * @return FRemoteControlProtocolMapping struct pointer
	 */
	FRemoteControlProtocolMapping* FindMapping(const FGuid& InMappingId);

	/**
	 * Loop through all mappings
	 * @param InCallback mapping reference callback with mapping struct reference as an argument
	 * @return FRemoteControlProtocolMapping struct pointer
	 */
	void ForEachMapping(FGetProtocolMappingCallback InCallback);

	/**
	 * Set range value which is bound to mapping struct
	 * @param InMappingId mapping unique id
	 * @param InRangeValue range property value
	 * @return true if it was set successfully
	 */
	template<typename T>
	bool SetRangeToMapping(const FGuid& InMappingId, T InRangeValue)
	{
		if (FRemoteControlProtocolMapping* Mapping = FindMapping(InMappingId))
		{
			Mapping->SetRangeValue(InRangeValue);

			return true;
		}

		return false;
	}

	/**
	 * Set mapping property data to bound mapping struct.
	 * Range data could be a value container of the primitive value like FFloatProperty.
	 * And it could be more complex Properties such as FStructProperty data pointer.
	 *
	 * @param InMappingId mapping unique id
	 * @param InPropertyValuePtr property value pointer
	 * @return true if it was set successfully
	 */
	bool SetPropertyDataToMapping(const FGuid& InMappingId, const void* InPropertyValuePtr);

	/** Checks if the given ValueType is supported */
	template <typename ValueType>
	static bool IsRangeTypeSupported() { return FRemoteControlProtocolMapping::TIsSupportedRangeValueType<ValueType>::Value; }

	/** Checks if the given PropertyType (FProperty) is supported */
	template <typename PropertyType>
    static bool IsRangePropertyTypeSupported() { return FRemoteControlProtocolMapping::TIsSupportedRangeValueType<PropertyType>::Value; }

	/** Custom struct serialize */
	bool Serialize(FArchive& Ar);
	friend FArchive& operator<<(FArchive& Ar, FRemoteControlProtocolBinding& InProtocolBinding);

private:
	/** Return FRemoteControlProtocolEntity pointer from RemoteControlProtocolEntityPtr */
	FRemoteControlProtocolEntity* GetRemoteControlProtocolEntity();

protected:
	/** Binding Id */
	UPROPERTY()
	FGuid Id;

	/** Protocol name which we using for binding */
	UPROPERTY()
	FName ProtocolName;

	/** Property Unique ID */
	UPROPERTY()
	FGuid PropertyId;

	/** Property name which we using for protocol range mapping */
	UPROPERTY()
	FName MappingPropertyName;

private:
	/** Pointer to struct on scope */
	TSharedPtr<TStructOnScope<FRemoteControlProtocolEntity>> RemoteControlProtocolEntityPtr;
};

/**
 * TypeTraits to define FRemoteControlProtocolBinding with a Serialize function
 */
template<>
struct TStructOpsTypeTraits<FRemoteControlProtocolBinding> : public TStructOpsTypeTraitsBase2<FRemoteControlProtocolBinding>
{
	enum
	{
		WithSerializer = true,
	};
};