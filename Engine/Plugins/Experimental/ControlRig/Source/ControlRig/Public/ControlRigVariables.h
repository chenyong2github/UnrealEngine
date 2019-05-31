// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ControlRig.h"
#include "ControlRigVariables.generated.h"

USTRUCT()
struct FControlRigIOVariable
{
	GENERATED_BODY()

	/* Segment split by . in the full path */
	UPROPERTY()
	FString PropertyPath;

	UPROPERTY()
	FString PropertyType;

	UPROPERTY()
	uint8 Size;

	FControlRigIOVariable()
		:Size(0)
	{}
};

/**
 * Current Types
 */
namespace FControlRigIOTypes
{
	extern CONTROLRIG_API const FName CR_Boolean;
	extern CONTROLRIG_API const FName CR_Byte;
	extern CONTROLRIG_API const FName CR_Int;
	extern CONTROLRIG_API const FName CR_Int64;
	extern CONTROLRIG_API const FName CR_Float;
	extern CONTROLRIG_API const FName CR_Name;
	extern CONTROLRIG_API const FName CR_Struct;

	template<typename VarType>
	inline FName GetTypeString()
	{
		return NAME_None;
	}

	template<>
	inline FName GetTypeString<bool>()
	{
		return FControlRigIOTypes::CR_Boolean;
	}

	template<>
	inline FName GetTypeString<uint8>()
	{
		return FControlRigIOTypes::CR_Byte;
	}

	template<>
	inline FName GetTypeString<int32>()
	{
		return FControlRigIOTypes::CR_Int;
	}

	template<>
	inline FName GetTypeString<int64>()
	{
		return FControlRigIOTypes::CR_Int64;
	}

	template<>
	inline FName GetTypeString<float>()
	{
		return FControlRigIOTypes::CR_Float;
	}

	template<>
	inline FName GetTypeString<FName>()
	{
		return FControlRigIOTypes::CR_Name;
	}

};

/**
 *  Current Supported Types at the bottom
 * 
 */
class FControlRigIOHelper
{
public:
	static FString GetFriendlyTypeName(const UProperty* Property)
	{
		check(Property);

		if (Property->IsA(UStructProperty::StaticClass()) || Property->IsA(UClassProperty::StaticClass()) ||
			Property->IsA(UObjectProperty::StaticClass()))
		{
			// I'm sure I'll have to add more types to remove prefix
			FString PropertyText = Property->GetCPPType();
			// if not, I think something is wrong
			if (ensure(PropertyText.Len() > 1))
			{
				return PropertyText.RightChop(1);
			}
		}

		if (Property->IsA(UIntProperty::StaticClass()))
		{
			return FControlRigIOTypes::CR_Int.ToString();
		}

		if (Property->IsA(UByteProperty::StaticClass()))
		{
			return FControlRigIOTypes::CR_Byte.ToString();
		}

		if (Property->IsA(UNameProperty::StaticClass()))
		{
			return FControlRigIOTypes::CR_Name.ToString();
		}
		return Property->GetCPPType();
	}

	static bool CanConvert(const FName& InType1, const FName& InType2)
	{
		if (InType1 == InType2)
		{
			return true;
		}

		if (InType1 == FControlRigIOTypes::CR_Boolean ||
			InType1 == FControlRigIOTypes::CR_Byte ||
			InType1 == FControlRigIOTypes::CR_Int ||
			InType1 == FControlRigIOTypes::CR_Int64 ||
			InType1 == FControlRigIOTypes::CR_Float)
		{
			return (InType2 == FControlRigIOTypes::CR_Boolean ||
				InType2 == FControlRigIOTypes::CR_Byte ||
				InType2 == FControlRigIOTypes::CR_Int ||
				InType2 == FControlRigIOTypes::CR_Int64 ||
				InType2 == FControlRigIOTypes::CR_Float);
		}

		return false;
	}

	// do we need this?
	template<typename SourceType, typename TargetType>
	static TargetType ConvertValue(SourceType Source)
	{
		// this only works if define type
		TargetType OutValue;
		ConvertType(Source, OutValue);
		return OutValue;
	}

	// this is here for template function compile
	// it shouldn't ever get here
	template< typename SameType >
	static void ConvertType(SameType In, SameType& Out)
	{
		Out = In;
	}

	// this is here for template function compile
// it shouldn't ever get here
	template< typename Type1, typename Type2 >
	static void ConvertType(Type1 In, Type2& Out)
	{
		// if we don't support this type, ensure
		ensureMsgf(false, TEXT("InvalidType entered"));
	}

	static void ConvertType(float In, bool& Out)
	{
		Out = (In != 0.f);
	}

	static void ConvertType(float In, uint8& Out)
	{
		Out = (uint8)In;
	}

	// we're explicit about type, we don't just do default cast
	static void ConvertType(float In, int32& Out)
	{
		Out = (int32)In;
	}

	static void ConvertType(float In, int64& Out)
	{
		Out = (int64)In;
	}

	static void ConvertType(int32 In, bool& Out)
	{
		Out = (In != 0);
	}

	static void ConvertType(int32 In, uint8& Out)
	{
		Out = (uint8)In;
	}

	static void ConvertType(int32 In, float& Out)
	{
		Out = (float)In;
	}

	static void ConvertType(int32 In, int64& Out)
	{
		Out = (int64)In;
	}

	static void ConvertType(int64 In, bool& Out)
	{
		Out = (In != 0);
	}

	static void ConvertType(int64 In, uint8& Out)
	{
		Out = (uint8)In; // overflow
	}

	static void ConvertType(int64 In, float& Out)
	{
		Out = (float)In;
	}

	static void ConvertType(int64 In, int32& Out)
	{
		Out = (int32)In; // overflow
	}

	static void ConvertType(bool In, float& Out)
	{
		Out = (In)? 1.f : 0.f;
	}

	static void ConvertType(bool In, uint8& Out)
	{
		Out = (In) ? 1 : 0;
	}

	static void ConvertType(bool In, int32& Out)
	{
		Out = (In) ? 1 : 0;
	}
	static void ConvertType(bool In, int64& Out)
	{
		Out = (In) ? 1 : 0;
	}

	static void ConvertType(uint8 In, bool& Out)
	{
		Out = (In > 0 );
	}

	static void ConvertType(uint8 In, float& Out)
	{
		Out = (float)In;
	}

	static void ConvertType(uint8 In, uint8& Out)
	{
		Out = (uint8)In;
	}

	static void ConvertType(uint8 In, int32& Out)
	{
		Out = (int32)In;
	}
	static void ConvertType(uint8 In, int64& Out)
	{
		Out = (int64)In;
	}

	template<typename T>
	static bool SetInputValue(UControlRig* ControRig, const FName& InPropertyPath, const FName& InValueType, const T& Value)
	{
		// first we look for whole path, if it's matched whole path, 
		// we use whole property to copy
		FCachedPropertyPath CachedProperty;
		if (ControRig->GetInOutPropertyPath(true, InPropertyPath, CachedProperty))
		{
			UProperty* Prop = CachedProperty.GetUProperty();
			FName PropType = FName(*GetFriendlyTypeName(Prop));
			if (PropType == InValueType)
			{
				SetValueInternal(Prop, CachedProperty.GetCachedAddress(), Value);
				return true;
			}

			if (CanConvert(PropType, InValueType))
			{
				if (Prop->IsA(UBoolProperty::StaticClass()))
				{
					SetValueInternal(Prop, CachedProperty.GetCachedAddress(), ConvertValue<T, bool>(Value));
					return true;
				}

				if (Prop->IsA(UByteProperty::StaticClass()))
				{
					SetValueInternal(Prop, CachedProperty.GetCachedAddress(), ConvertValue<T, uint8>(Value));
					return true;
				}

				if (Prop->IsA(UIntProperty::StaticClass()))
				{
					SetValueInternal(Prop, CachedProperty.GetCachedAddress(), ConvertValue<T, int32>(Value));
					return true;
				}

				if (Prop->IsA(UInt64Property::StaticClass()))
				{
					SetValueInternal(Prop, CachedProperty.GetCachedAddress(), ConvertValue<T, int64>(Value));
					return true;
				}

				if (Prop->IsA(UFloatProperty::StaticClass()))
				{
					SetValueInternal(Prop, CachedProperty.GetCachedAddress(), ConvertValue<T, float>(Value));
					return true;
				}
			}
		}
		return false;
	}

	template<typename T>
	static bool GetInputValue(UControlRig* ControRig, const FName& InPropertyPath, const FName& InValueType, T& OutValue)
	{
		FCachedPropertyPath CachedProperty;
		if (ControRig->GetInOutPropertyPath(true, InPropertyPath, CachedProperty))
		{
			UProperty* Prop = CachedProperty.GetUProperty();
			FName PropType = FName(*GetFriendlyTypeName(Prop));
			if (PropType == InValueType)
			{
				GetValueInternal(Prop, CachedProperty.GetCachedAddress(), OutValue);
				return true;
			}

			if (CanConvert(PropType, InValueType))
			{
				if (Prop->IsA(UBoolProperty::StaticClass()))
				{
					bool bValue;
					GetValueInternal(Prop, CachedProperty.GetCachedAddress(), bValue);
					OutValue = ConvertValue<bool, T>(bValue);
					return true;
				}

				if (Prop->IsA(UByteProperty::StaticClass()))
				{
					uint8 Value;
					GetValueInternal(Prop, CachedProperty.GetCachedAddress(), Value);
					OutValue = ConvertValue<uint8, T>(Value);
					return true;
				}

				if (Prop->IsA(UIntProperty::StaticClass()))
				{
					int32 Value;
					GetValueInternal(Prop, CachedProperty.GetCachedAddress(), Value);
					OutValue = ConvertValue<int32, T>(Value);
					return true;
				}

				if (Prop->IsA(UInt64Property::StaticClass()))
				{
					int64 Value;
					GetValueInternal(Prop, CachedProperty.GetCachedAddress(), Value);
					OutValue = ConvertValue<int64, T>(Value);
					return true;
				}

				if (Prop->IsA(UFloatProperty::StaticClass()))
				{
					float Value;
					GetValueInternal(Prop, CachedProperty.GetCachedAddress(), Value);
					OutValue = ConvertValue<float, T>(Value);
					return true;
				}
			}
		}
		// may not be safe
		return false;
	}

	template<typename T>
	static bool GetOutputValue(UControlRig* ControRig, const FName& InPropertyPath, const FName& InValueType, T& OutValue)
	{
		FCachedPropertyPath CachedProperty;
		if (ControRig->GetInOutPropertyPath(false, InPropertyPath, CachedProperty))
		{
			UProperty* Prop = CachedProperty.GetUProperty();
			FName PropType = FName(*GetFriendlyTypeName(Prop));
			if (PropType == InValueType)
			{
				GetValueInternal(Prop, CachedProperty.GetCachedAddress(), OutValue);
				return true;
			}

			if (CanConvert(PropType, InValueType))
			{
				if (Prop->IsA(UBoolProperty::StaticClass()))
				{
					bool bValue;
					GetValueInternal(Prop, CachedProperty.GetCachedAddress(), bValue);
					OutValue = ConvertValue<bool, T>(bValue);
					return true;
				}

				if (Prop->IsA(UByteProperty::StaticClass()))
				{
					uint8 Value;
					GetValueInternal(Prop, CachedProperty.GetCachedAddress(), Value);
					OutValue = ConvertValue<uint8, T>(Value);
					return true;
				}

				if (Prop->IsA(UIntProperty::StaticClass()))
				{
					int32 Value;
					GetValueInternal(Prop, CachedProperty.GetCachedAddress(), Value);
					OutValue = ConvertValue<int32, T>(Value);
					return true;
				}

				if (Prop->IsA(UInt64Property::StaticClass()))
				{
					int64 Value;
					GetValueInternal(Prop, CachedProperty.GetCachedAddress(), Value);
					OutValue = ConvertValue<int64, T>(Value);
					return true;
				}

				if (Prop->IsA(UFloatProperty::StaticClass()))
				{
					float Value;
					GetValueInternal(Prop, CachedProperty.GetCachedAddress(), Value);
					OutValue = ConvertValue<float, T>(Value);
					return true;
				}
			}
		}
		// may not be safe
		return false;
	}
private:

	template<typename T>
	static void SetValueInternal(UProperty* Property, void* PropertyAddress, const T& Value)
	{
		if (ensure(PropertyAddress && Property && Property->GetSize() == sizeof(T)))
		{
			Property->CopyCompleteValue(PropertyAddress, &Value);
		}
	}

	template<typename T>
	static void GetValueInternal(UProperty* Property, void* PropertyAddress, T& OutValue)
	{
		if (ensure(PropertyAddress && Property && Property->GetSize() == sizeof(T)))
		{
			Property->CopyCompleteValue(&OutValue, PropertyAddress);
		}
	}
};