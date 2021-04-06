// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundPrimitives.h"

#include "MetasoundAudioBuffer.h"
#include "MetasoundDataTypeRegistrationMacro.h"
#include "MetasoundRouter.h"
#include "MetasoundTime.h"
#include "MetasoundTrigger.h"

#include <type_traits>

namespace Metasound
{
	// Disable auto-conversion using the FAudioBuffer(int32) constructor
	template<typename FromDataType>
	struct TIsAutoConvertible<FromDataType, FAudioBuffer>
	{
		static constexpr bool bIsConvertible = std::is_convertible<FromDataType, FAudioBuffer>::value;

		static constexpr bool bIsFromArithmeticType = std::is_arithmetic<FromDataType>::value;

	public:

		static constexpr bool Value = bIsConvertible && (!bIsFromArithmeticType);
	};

	// Disable auto-conversions based on FTrigger implicit converters
	template<typename ToDataType>
	struct TIsAutoConvertible<FTrigger, ToDataType>
	{
		static constexpr bool bIsConvertible = std::is_convertible<FTrigger, ToDataType>::value;

		static constexpr bool bIsToArithmeticType = std::is_arithmetic<ToDataType>::value;

	public:

		static constexpr bool Value = bIsConvertible && (!bIsToArithmeticType);
	};
}

REGISTER_METASOUND_DATATYPE(bool, "Bool", ::Metasound::ELiteralType::Boolean)
REGISTER_METASOUND_DATATYPE(int32, "Int32", ::Metasound::ELiteralType::Integer)
//REGISTER_METASOUND_DATATYPE(int64, "Int64", ::Metasound::ELiteralType::Integer)
REGISTER_METASOUND_DATATYPE(float, "Float", ::Metasound::ELiteralType::Float)
//REGISTER_METASOUND_DATATYPE(double, "Double", ::Metasound::ELiteralType::Float)
REGISTER_METASOUND_DATATYPE(FString, "String", ::Metasound::ELiteralType::String)

REGISTER_METASOUND_DATATYPE(Metasound::FTrigger, "Trigger", ::Metasound::ELiteralType::Boolean)
REGISTER_METASOUND_DATATYPE(Metasound::FTime, "Time", ::Metasound::ELiteralType::Float)
REGISTER_METASOUND_DATATYPE(Metasound::FAudioBuffer, "Audio")
REGISTER_METASOUND_DATATYPE(Metasound::FSendAddress, "Transmission:Address", ::Metasound::ELiteralType::String)
