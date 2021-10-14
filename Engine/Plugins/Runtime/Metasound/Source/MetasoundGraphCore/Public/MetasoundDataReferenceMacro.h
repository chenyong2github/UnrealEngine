// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundDataReference.h"
#include "MetasoundVariable.h"

#define DECLARE_METASOUND_DATA_REFERENCE_CORE_TYPE(DataType, ModuleApi) \
	template<> \
	struct ::Metasound::TDataReferenceTypeInfo<DataType> \
	{ \
		static ModuleApi const TCHAR* TypeName; \
		static ModuleApi const FText& GetTypeDisplayText(); \
		static ModuleApi const FMetasoundDataTypeId TypeId; \
		\
		private: \
		\
		static const DataType* const TypePtr; \
	};

#define DECLARE_METASOUND_DATA_REFERENCE_ALIAS_TYPES(DataType, DataTypeInfoTypeName, DataReadReferenceTypeName, DataWriteReferenceTypeName) \
	typedef ::Metasound::TDataReferenceTypeInfo<DataType> DataTypeInfoTypeName; \
	\
	typedef ::Metasound::TDataReadReference<DataType> DataReadReferenceTypeName; \
	typedef ::Metasound::TDataWriteReference<DataType> DataWriteReferenceTypeName;

/** Macro to make declaring a metasound parameter simple.  */
// Declares a metasound parameter type by
// - Adding typedefs for commonly used template types.
// - Defining parameter type traits.
#define DECLARE_METASOUND_DATA_REFERENCE_TYPES(DataType, ModuleApi, DataTypeInfoTypeName, DataReadReferenceTypeName, DataWriteReferenceTypeName) \
	DECLARE_METASOUND_DATA_REFERENCE_CORE_TYPE(DataType, ModuleApi); \
	DECLARE_METASOUND_DATA_REFERENCE_ALIAS_TYPES(DataType, DataTypeInfoTypeName, DataReadReferenceTypeName, DataWriteReferenceTypeName); \
	DECLARE_METASOUND_DATA_REFERENCE_CORE_TYPE(::Metasound::TVariable<DataType>, ModuleApi); \
	DECLARE_METASOUND_DATA_REFERENCE_CORE_TYPE(TArray<DataType>, ModuleApi); \
	DECLARE_METASOUND_DATA_REFERENCE_CORE_TYPE(::Metasound::TVariable<TArray<DataType>>, ModuleApi);


#define DEFINE_METASOUND_DATA_REFERENCE_CORE_TYPE(DataType, DataTypeName, DataTypeLoctextKey) \
	const TCHAR* ::Metasound::TDataReferenceTypeInfo<DataType>::TypeName = TEXT(DataTypeName); \
	const FText& ::Metasound::TDataReferenceTypeInfo<DataType>::GetTypeDisplayText() \
	{ \
		static const FText DisplayText = NSLOCTEXT("MetaSoundCore_DataReference", DataTypeLoctextKey, DataTypeName); \
		return DisplayText; \
	} \
	const DataType* const ::Metasound::TDataReferenceTypeInfo<DataType>::TypePtr = nullptr; \
	const void* const ::Metasound::TDataReferenceTypeInfo<DataType>::TypeId = static_cast<const FMetasoundDataTypeId>(&::Metasound::TDataReferenceTypeInfo<DataType>::TypePtr);


// This only needs to be called if you don't plan on calling REGISTER_METASOUND_DATATYPE.
#define DEFINE_METASOUND_DATA_TYPE(DataType, DataTypeName) \
	DEFINE_METASOUND_DATA_REFERENCE_CORE_TYPE(DataType, DataTypeName, DataTypeName); \
	DEFINE_METASOUND_DATA_REFERENCE_CORE_TYPE(::Metasound::TVariable<DataType>, DataTypeName":Variable", DataTypeName"_Variable"); \
	DEFINE_METASOUND_DATA_REFERENCE_CORE_TYPE(TArray<DataType>, DataTypeName":Array", DataTypeName"_Array"); \
	DEFINE_METASOUND_DATA_REFERENCE_CORE_TYPE(::Metasound::TVariable<TArray<DataType>>, DataTypeName":Array:Variable", DataTypeName"_Array_Variable"); 

