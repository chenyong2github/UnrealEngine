// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"

class UScriptStruct;

/**
 * The TypedElementQueryBuilder allows for the construction of queries for use by the Typed Element Data Storage.
 * There are two types of queries, simple and normal. Simple queries are guaranteed to be supported by the data
 * storage backend and guaranteed to have no performance side effects. <Normal queries pending development.>
 * 
 * Queries are constructed with the following section:
 * - Select		A list of the data objects that are returned as the result of the query.
 * - Where		A list of conditions that restrict what's accepted by the query.
 * - DependsOn	A list of systems outside the data storage that will be accessed by the query('s user).
 * - Commit		Compiles the query into its final form and can be used afterwards.
 * 
 * Calls to the sections become increasingly restrictive, e.g. after calling Where only DependsOn can be
 * called again.
 * 
 * Arguments to the various functions take a pointer to a description of a UStruct. These can be provived
 * in the follow ways:
 * - By using the templated version, e.g. Any<FStructExample>()
 * - By callling the static StaticStruct() function on the UStruct, e.g. FStructExample::StaticStruct();
 * - By name using the Type or TypeOptional string operator, e.g. "/Script/ExamplePackage.FStructExample"_Type or
 *		"/Script/OptionalPackage.FStructOptional"_TypeOptional
 * All functions allow for a single type to be added or a list of types, e.g. ReadOnly(Type<FStructExample>() or
 *		ReadOnly({ Type<FStructExample1>(), FStructExample2::StaticStruct(), "/Script/ExamplePackage.FStructExample3"_Type });
 * 
 * "Select" is constructed with: 
 * - ReadOnly: Indicates that the data object will only be read from
 * - ReadWrite: Indicated that the data object will be read and written to.
 * 
 * "Where" is constructed with:
 * - All: The query will be accepted only if all the types listed here are present in a table.
 * - Any: The query will be accepted if at least one of the listed types is present in a table.
 * - Not: The query will be accepted if none of the listed types are present in a table.
 * The above construction calls can be mixed and be called multiple times.
 * All functions accept a nullptr for the type in which case the call will have no effect. This can be used to
 *		reference types in plugins that may not be loaded when using the TypeOptional string operator.

 * "DependsOn" is constructed with:
 * - ReadOnly: Indicates that the external system will only be used to read data from.
 * - ReadWrite: Indicates that the external system will be used to write data to.
 * 
 * Usage example:
 * ITypedElementDataStorageInterface::QueryDescription Query =
 *		Select{}
 *			.ReadWrite({ FDataExample1::StaticStruct() })
 *			.ReadWrite<FDataExample2, FDataExample3>()
 *			.ReadOnly<FDataExample4>()
 *		.Where()
 *			.All<FTagExample1, FDataExample5>()
 *			.Any("/Script/ExamplePackage.FStructExample"_TypeOptional)
 *			.None(FTagExample2::StaticStruct())
 *		.DependsOn()
 *			.ReadOnly<USystemExample1, USystemExample2>()
 *			.ReadWrite(USystemExample2::StaticClass())
 *		.Commit();
 * 
 * Creating a query is expensive on the builder and the backend side. It's therefore recommended to create a query
 * and store its compiled form for repeated use instead of rebuilding the query on every update.
 */

namespace TypedElementQueryBuilder
{
	using EAccessType = ITypedElementDataStorageInterface::QueryDescription::EAccessType;

	TYPEDELEMENTFRAMEWORK_API const UScriptStruct* Type(FTopLevelAssetPath Name);
	TYPEDELEMENTFRAMEWORK_API const UScriptStruct* TypeOptional(FTopLevelAssetPath Name);
	TYPEDELEMENTFRAMEWORK_API const UScriptStruct* operator""_Type(const char* Name, std::size_t NameSize);
	TYPEDELEMENTFRAMEWORK_API const UScriptStruct* operator""_TypeOptional(const char* Name, std::size_t NameSize);

	class TYPEDELEMENTFRAMEWORK_API Dependency final
	{
		friend class Select;
		friend class SimpleQuery;
	public:
		template<typename... TargetTypes>
		Dependency& ReadOnly();
		Dependency& ReadOnly(const UClass* Target);
		Dependency& ReadOnly(std::initializer_list<const UClass*> Targets);
		template<typename... TargetTypes>
		Dependency& ReadWrite();
		Dependency& ReadWrite(const UClass* Target);
		Dependency& ReadWrite(std::initializer_list<const UClass*> Targets);

		ITypedElementDataStorageInterface::QueryDescription&& Commit();

	private:
		explicit Dependency(ITypedElementDataStorageInterface::QueryDescription* Query);
		
		ITypedElementDataStorageInterface::QueryDescription* Query;
	};

	class TYPEDELEMENTFRAMEWORK_API SimpleQuery final
	{
	public:
		friend class Select;
		Dependency DependsOn();

		template<typename... TargetTypes>
		SimpleQuery& All();
		SimpleQuery& All(const UScriptStruct* Target);
		SimpleQuery& All(std::initializer_list<const UScriptStruct*> Targets);
		template<typename... TargetTypes>
		SimpleQuery& Any();
		SimpleQuery& Any(const UScriptStruct* Target);
		SimpleQuery& Any(std::initializer_list<const UScriptStruct*> Targets);
		template<typename... TargetTypes>
		SimpleQuery& None();
		SimpleQuery& None(const UScriptStruct* Target);
		SimpleQuery& None(std::initializer_list<const UScriptStruct*> Targets);

	private:
		explicit SimpleQuery(ITypedElementDataStorageInterface::QueryDescription* Query);

		ITypedElementDataStorageInterface::QueryDescription* Query;
	};

	class TYPEDELEMENTFRAMEWORK_API Select final
	{
	public:
		template<typename... TargetTypes>
		Select& ReadOnly();
		Select& ReadOnly(const UScriptStruct* Target);
		Select& ReadOnly(std::initializer_list<const UScriptStruct*> Targets);
		template<typename... TargetTypes>
		Select& ReadWrite();
		Select& ReadWrite(const UScriptStruct* Target);
		Select& ReadWrite(std::initializer_list<const UScriptStruct*> Targets);

		ITypedElementDataStorageInterface::QueryDescription&& Commit();
		SimpleQuery Where();
		Dependency DependsOn();

	private:
		ITypedElementDataStorageInterface::QueryDescription Query;
	};


	// Implementations

	template<typename... TargetTypes>
	Select& Select::ReadOnly()
	{
		ReadOnly({ TargetTypes::StaticStruct()... });
		return *this;
	}

	template<typename... TargetTypes>
	Select& Select::ReadWrite()
	{
		ReadWrite({ TargetTypes::StaticStruct()... });
		return *this;
	}


	template<typename... TargetTypes>
	SimpleQuery& SimpleQuery::All()
	{
		All({ TargetTypes::StaticStruct()... });
		return *this;
	}

	template<typename... TargetTypes>
	SimpleQuery& SimpleQuery::Any()
	{
		Any({ TargetTypes::StaticStruct()... });
		return *this;
	}

	template<typename... TargetTypes>
	SimpleQuery& SimpleQuery::None()
	{
		None({ TargetTypes::StaticStruct()... });
		return *this;
	}


	template<typename... TargetTypes>
	Dependency& Dependency::ReadOnly()
	{
		ReadOnly( { TargetTypes::StaticClass()... } );
		return *this;
	}

	template<typename... TargetTypes>
	Dependency& Dependency::ReadWrite()
	{
		ReadWrite({ TargetTypes::StaticClass()... });
		return *this;
	}

} // namespace TypedElementQueryBuilder