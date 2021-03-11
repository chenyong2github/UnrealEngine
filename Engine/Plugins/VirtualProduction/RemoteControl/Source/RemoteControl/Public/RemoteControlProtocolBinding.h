// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Class.h"
#include "UObject/StructOnScope.h"
#include "UObject/WeakObjectPtr.h"

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

	template <> struct TIsSupportedRangeValueType<int8>		{ enum { Value = true }; };
	template <> struct TIsSupportedRangeValueType<int16>	{ enum { Value = true }; };
	template <> struct TIsSupportedRangeValueType<int32>	{ enum { Value = true }; };
	template <> struct TIsSupportedRangeValueType<int64>	{ enum { Value = true }; };
	template <> struct TIsSupportedRangeValueType<uint8>	{ enum { Value = true }; };
	template <> struct TIsSupportedRangeValueType<uint16>	{ enum { Value = true }; };
	template <> struct TIsSupportedRangeValueType<uint32>	{ enum { Value = true }; };
	template <> struct TIsSupportedRangeValueType<uint64>	{ enum { Value = true }; };
	template <> struct TIsSupportedRangeValueType<float>	{ enum { Value = true }; };
	template <> struct TIsSupportedRangeValueType<double>	{ enum { Value = true }; };
	template <> struct TIsSupportedRangeValueType<bool>		{ enum { Value = true }; };


public:
	FRemoteControlProtocolMapping() = default;
	FRemoteControlProtocolMapping(FProperty* InProperty, uint8 InRangeValueSize);

	bool operator==(const FRemoteControlProtocolMapping& InProtocolMapping) const;
	bool operator==(FGuid InProtocolMappingId) const;

public:
	/** Get Binding Range Id. */
	const FGuid& GetId() const { return Id; }

	/** Get Binding Range Value */
	template<typename T>
	T GetRangeValue()
	{
		check(TIsSupportedRangeValueType<T>::Value);
		check(InterpolationRangePropertyData.Num() && InterpolationRangePropertyData.Num() == sizeof(T));
		return *reinterpret_cast<T*>(InterpolationRangePropertyData.GetData());
	}

	/** Set Binding Range Value based on template value input */
	template<typename T>
	void SetRangeValue(T InRangeValue)
	{
		check(TIsSupportedRangeValueType<T>::Value);
		check(InterpolationRangePropertyData.Num() && InterpolationRangePropertyData.Num() == sizeof(T));
		*reinterpret_cast<T*>(InterpolationRangePropertyData.GetData()) = InRangeValue;
	}

	/** Get Mapping Property Value as a primitive type */
	template<typename T>
	T GetMappingValueAsPrimitive()
	{
		check(TIsSupportedRangeValueType<T>::Value);
		check(InterpolationMappingPropertyData.Num() && InterpolationMappingPropertyData.Num() == sizeof(T));
		return *reinterpret_cast<T*>(InterpolationMappingPropertyData.GetData());
	}

	/** Set primitive Mapping Property Value based on template value input */
	template<typename T>
	void SetRangeValueAsPrimitive(T InMappingPropertyValue)
	{
		check(TIsSupportedRangeValueType<T>::Value);
		check(InterpolationMappingPropertyData.Num() && InterpolationMappingPropertyData.Num() == sizeof(T));
		*reinterpret_cast<T*>(InterpolationMappingPropertyData.GetData()) = InMappingPropertyValue;
	}

	/** Get Mapping value as a Struct on Scope, only in case BoundProperty is FStructProperty */
	TSharedPtr<FStructOnScope> GetMappingPropertyAsStructOnScope();

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

	/** Get exposed property id */
	const FGuid& GetPropertyId() const { return PropertyId; }

	/**
	 * Interpolate and apply protocol value to the property
	 * @param InProtocolValue double value from the protocol
	 * @return true of applied successfully
	 */
	bool ApplyProtocolValueToProperty(double InProtocolValue);

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
	bool GetInterpolatedPropertyBuffer(FProperty* InProperty, double InProtocolValue, TArray<uint8>& OutBuffer);

private:
	/** Get Ranges and Mapping Value pointers */
	TArray<TPair<const uint8*, const uint8*>> GetRangeMappingBuffers() const;

public:
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