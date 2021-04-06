// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundArrayNodesRegistration.h"
#include "MetasoundDataReference.h"
#include "MetasoundEnum.h"
#include "MetasoundDataTypeRegistrationMacro.h"

namespace Metasound
{
	template<typename FromDataType, typename EnumType, EnumType DefaultValue>
	struct TIsAutoConvertible<FromDataType, TEnum<EnumType, DefaultValue>>
	{
		static constexpr bool Value = false;
	};

	template<typename ToDataType, typename EnumType, EnumType DefaultValue>
	struct TIsAutoConvertible<TEnum<EnumType, DefaultValue>, ToDataType>
	{
		static constexpr bool Value = false;
	};

	template<typename EnumType, EnumType DefaultValue>
	struct TEnableArrayNodes<TEnum<EnumType, DefaultValue>>
	{
		static constexpr bool Value = false;
	};

	// Specialization of TIsTransmittable<> to disable transmission of enums.
	template<typename EnumType, EnumType DefaultValue>
	struct TIsTransmittable<TEnum<EnumType, DefaultValue>>
	{
	public:
		static constexpr bool Value = false;
	};
}

// Helper macros: for defining/declaring a new Enum to be used in Metasounds.
// * Declares/defines a typedef FEnum<YourEnum> wrapper around the enum that you pass it
// * Declares/defines a specialization of TEnumStringHelper which is used to validate and convert between values/names.

// These helps generate a couple of boiler plate functions:
//  - GetNamespace() which will return the name of the Enum "EMyEnum" etc. 
//  - GetAllEntries() which returns all posible entries in the enum. 

/** DECLARE_METASOUND_ENUM
 * @param ENUMNAME - The typename of your EnumType you want to use for Metasounds. e.g. MyEnum
 * @param DEFAULT - A fully qualified default Enum value. e.g. EMyEnum::One
 * @param API - The module API this is declared inside e.g. METASOUNDSTANDARDNODES_API 
 * @param ENUMTYPEDEF - The name of the TEnum<YourType> wrapper type
 * @param TYPEINFO - The name of the TypeInfo type you want to define e.g. FMyEnumTypeInfo
 * @param READREF - The name of the Read Reference type you want to define. e.g FMyEnumReadRef
 * @param WRITEREF -The name of the Write Reference type you want to define e.g. FMyEnumWriteRef
 */
#define DECLARE_METASOUND_ENUM(ENUMNAME, DEFAULT, API, ENUMTYPEDEF, TYPEINFO, READREF, WRITEREF)\
	using ENUMTYPEDEF = Metasound::TEnum<ENUMNAME, DEFAULT>; \
	DECLARE_METASOUND_DATA_REFERENCE_TYPES(ENUMTYPEDEF, API, TYPEINFO, READREF, WRITEREF);\
	template<> struct API Metasound::TEnumStringHelper<ENUMNAME> : Metasound::TEnumStringHelperBase<Metasound::TEnumStringHelper<ENUMNAME>, ENUMNAME>\
	{\
		static FName GetNamespace()\
		{\
			static const FName ThisName { TEXT(#ENUMNAME) };\
			return ThisName;\
		}\
		static TArrayView<const Metasound::TEnumEntry<ENUMNAME>> GetAllEntries();\
	};

/** DEFINE_METASOUND_ENUM_BEGIN
 * @param ENUMNAME - The typename of your raw EnumType you want to use for Metasounds. e.g. EMyType
 * @param ENUMTYPEDEF - The name of the TEnum<YourType> wrapper type
 * @param DATATYPENAMESTRING - The string that will the data type name "Enum:<string>" e.g. "MyEnum"
 */
#define DEFINE_METASOUND_ENUM_BEGIN(ENUMNAME,ENUMTYPEDEF,DATATYPENAMESTRING)\
	REGISTER_METASOUND_DATATYPE(ENUMTYPEDEF, "Enum:" DATATYPENAMESTRING, ::Metasound::ELiteralType::Integer);\
	TArrayView<const Metasound::TEnumEntry<ENUMNAME>> Metasound::TEnumStringHelper<ENUMNAME>::GetAllEntries()\
	{\
		static const Metasound::TEnumEntry<ENUMNAME> Entries[] = {

/** DEFINE_METASOUND_ENUM_ENTRY - defines a single Enum Entry
  * @param ENTRY - Fully Qualified Name of Entry of the Enum. (e.g. EMyType::One)
  * @param DISPLAYNAME - FText Presented to User
  * @param TOOLTIP - FText displayed as a tooltip
  */
	#define DEFINE_METASOUND_ENUM_ENTRY(ENTRY, DISPLAYNAME, TOOLTIP) { ENTRY, TEXT(#ENTRY), DISPLAYNAME, TOOLTIP }

/** DEFINE_METASOUND_ENUM_END - macro which ends the function body of the GetAllEntries function
   */
#define DEFINE_METASOUND_ENUM_END() \
		};\
		return Entries;\
	};
