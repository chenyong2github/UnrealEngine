// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Framework/TypedElementQueryBuilder.h"

namespace TypedElementQueryBuilder
{
	const UScriptStruct* Type(FTopLevelAssetPath Name)
	{
		const UScriptStruct* StructInfo = TypeOptional(Name);
		checkf(StructInfo, TEXT("Type name '%s' used as part of building a typed element query was not found."), *Name.ToString());
		return StructInfo;
	}

	const UScriptStruct* TypeOptional(FTopLevelAssetPath Name)
	{
		constexpr bool bExactMatch = true;
		return static_cast<UScriptStruct*>(StaticFindObject(UScriptStruct::StaticClass(), Name, bExactMatch));
	}

	const UScriptStruct* operator""_Type(const char* Name, std::size_t NameSize)
	{
		return Type(FTopLevelAssetPath{ FAnsiStringView{ Name, IntCastChecked<int32>(NameSize) } });
	}

	const UScriptStruct* operator""_TypeOptional(const char* Name, std::size_t NameSize)
	{
		return TypeOptional(FTopLevelAssetPath{ FAnsiStringView{ Name, IntCastChecked<int32>(NameSize) } });
	}

	/**
	 * DependsOn
	 */

	Dependency::Dependency(ITypedElementDataStorageInterface::QueryDescription* Query)
		: Query(Query)
	{
	}

	Dependency& Dependency::ReadOnly(const UClass* Target)
	{
		checkf(Target, TEXT("The Dependency section in the Typed Elements query builder doesn't support nullptrs as Read-Only input."));
		Query->Dependencies.Emplace(Target, EAccessType::ReadOnly);
		return *this;
	}

	Dependency& Dependency::ReadOnly(std::initializer_list<const UClass*> Targets)
	{
		for (const UClass* Target : Targets)
		{
			ReadOnly(Targets);
		}
		return *this;
	}

	Dependency& Dependency::ReadWrite(const UClass* Target)
	{
		checkf(Target, TEXT("The Dependency section in the Typed Elements query builder doesn't support nullptrs as Read/Write input."));
		Query->Dependencies.Emplace(Target, EAccessType::ReadWrite);
		return *this;
	}

	Dependency& Dependency::ReadWrite(std::initializer_list<const UClass*> Targets)
	{
		for (const UClass* Target : Targets)
		{
			ReadWrite(Targets);
		}
		return *this;
	}

	ITypedElementDataStorageInterface::QueryDescription&& Dependency::Commit()
	{
		return MoveTemp(*Query);
	}


	/**
	 * SimpleQuery
	 */

	SimpleQuery::SimpleQuery(ITypedElementDataStorageInterface::QueryDescription* Query)
		: Query(Query)
	{
		Query->bSimpleQuery = true;
	}

	SimpleQuery& SimpleQuery::All(const UScriptStruct* Target)
	{
		if (Target)
		{
			Query->ConditionTypes.Add(ITypedElementDataStorageInterface::QueryDescription::OperatorType::SimpleAll);
			Query->ConditionOperators.AddZeroed_GetRef().Type = Target;
		}
		return *this;
	}

	SimpleQuery& SimpleQuery::All(std::initializer_list<const UScriptStruct*> Targets)
	{
		for (const UScriptStruct* Target : Targets)
		{
			All(Target);
		}
		return *this;
	}

	SimpleQuery& SimpleQuery::Any(const UScriptStruct* Target)
	{
		if (Target)
		{
			Query->ConditionTypes.Add(ITypedElementDataStorageInterface::QueryDescription::OperatorType::SimpleAny);
			Query->ConditionOperators.AddZeroed_GetRef().Type = Target;
		}
		return *this;
	}

	SimpleQuery& SimpleQuery::Any(std::initializer_list<const UScriptStruct*> Targets)
	{
		for (const UScriptStruct* Target : Targets)
		{
			Any(Target);
		}
		return *this;
	}

	SimpleQuery& SimpleQuery::None(const UScriptStruct* Target)
	{
		if (Target)
		{
			Query->ConditionTypes.Add(ITypedElementDataStorageInterface::QueryDescription::OperatorType::SimpleNone);
			Query->ConditionOperators.AddZeroed_GetRef().Type = Target;
		}
		return *this;
	}

	SimpleQuery& SimpleQuery::None(std::initializer_list<const UScriptStruct*> Targets)
	{
		for (const UScriptStruct* Target : Targets)
		{
			None(Target);
		}
		return *this;
	}

	Dependency SimpleQuery::DependsOn()
	{
		return Dependency{ Query };
	}


	/**
	 * Select
	 */
	
	Select& Select::ReadOnly(const UScriptStruct* Target)
	{
		checkf(Target, TEXT("The Select section in the Typed Elements query builder doesn't support nullptrs as Read-Only input."));
		Query.Selection.Emplace(Target, EAccessType::ReadOnly);
		return *this;
	}

	Select& Select::ReadOnly(std::initializer_list<const UScriptStruct*> Targets)
	{
		for (const UScriptStruct* Target : Targets)
		{
			ReadOnly(Target);
		}
		return *this;
	}

	Select& Select::ReadWrite(const UScriptStruct* Target)
	{
		checkf(Target, TEXT("The Select section in the Typed Elements query builder doesn't support nullptrs as Read/Write input."));
		Query.Selection.Emplace(Target, EAccessType::ReadWrite);
		return *this;
	}

	Select& Select::ReadWrite(std::initializer_list<const UScriptStruct*> Targets)
	{
		for (const UScriptStruct* Target : Targets)
		{
			ReadWrite(Target);
		}
		return *this;
	}

	SimpleQuery Select::Where()
	{
		return SimpleQuery{ &Query };
	}

	Dependency Select::DependsOn()
	{
		return Dependency{ &Query };
	}

	ITypedElementDataStorageInterface::QueryDescription&& Select::Commit()
	{
		return MoveTemp(Query);
	}
}