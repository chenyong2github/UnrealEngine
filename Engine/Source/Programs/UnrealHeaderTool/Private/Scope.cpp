// Copyright Epic Games, Inc. All Rights Reserved.

#include "Scope.h"
#include "UnrealHeaderTool.h"
#include "UObject/ErrorException.h"
#include "ParserHelper.h"
#include "UnrealTypeDefinitionInfo.h"
#include "ClassMaps.h"

FScope::FScope(FScope* InParent)
	: Parent(InParent)
{ }

FScope::FScope()
	: Parent(nullptr)
{

}

void FScope::AddType(FUnrealFieldDefinitionInfo& Type)
{
	TypeMap.Add(Type.GetFName(), &Type);
}

/**
 * Dispatch type to one of three arrays Enums, Structs and DelegateFunctions.
 *
 * @param Type Input type.
 * @param Enums (Output parameter) Array to fill with enums.
 * @param Structs (Output parameter) Array to fill with structs.
 * @param DelegateFunctions (Output parameter) Array to fill with delegate functions.
 */
void DispatchType(FUnrealFieldDefinitionInfo& FieldDef, TArray<FUnrealEnumDefinitionInfo*>& Enums, TArray<FUnrealScriptStructDefinitionInfo*>& Structs, TArray<FUnrealFunctionDefinitionInfo*>& DelegateFunctions)
{
	if (UHTCast<FUnrealClassDefinitionInfo>(FieldDef) != nullptr)
	{
		// Inner scopes.
		FieldDef.GetScope()->SplitTypesIntoArrays(Enums, Structs, DelegateFunctions);
	}
	else if (FUnrealEnumDefinitionInfo* EnumDef = UHTCast<FUnrealEnumDefinitionInfo>(FieldDef))
	{
		Enums.Add(EnumDef);
	}
	else if (FUnrealScriptStructDefinitionInfo* ScriptStructDef = UHTCast<FUnrealScriptStructDefinitionInfo>(FieldDef))
	{
		Structs.Add(ScriptStructDef);
	}
	else if (FUnrealFunctionDefinitionInfo* FunctionDef = UHTCast<FUnrealFunctionDefinitionInfo>(FieldDef))
	{
		if (FunctionDef->IsDelegateFunction())
		{
			bool bAdded = false;
			if (FunctionDef->GetSuperFunction() == nullptr)
			{
				DelegateFunctions.Add(FunctionDef);
				bAdded = true;
			}
			check(bAdded);
		}
	}
}

void FScope::SplitTypesIntoArrays(TArray<FUnrealEnumDefinitionInfo*>& Enums, TArray<FUnrealScriptStructDefinitionInfo*>& Structs, TArray<FUnrealFunctionDefinitionInfo*>& DelegateFunctions)
{
	for (TPair<FName, FUnrealFieldDefinitionInfo*>& TypePair : TypeMap)
	{
		DispatchType(*TypePair.Value, Enums, Structs, DelegateFunctions);
	}
}

FUnrealFieldDefinitionInfo* FScope::FindTypeByName(FName Name)
{
	if (!Name.IsNone())
	{
		TDeepScopeTypeIterator<FUnrealFieldDefinitionInfo, false> TypeIterator(this);

		while (TypeIterator.MoveNext())
		{
			FUnrealFieldDefinitionInfo* Type = *TypeIterator;
			if (Type->GetFName() == Name)
			{
				return Type;
			}
		}
	}

	return nullptr;
}

const FUnrealFieldDefinitionInfo* FScope::FindTypeByName(FName Name) const
{
	if (!Name.IsNone())
	{
		TScopeTypeIterator<FUnrealFieldDefinitionInfo, true> TypeIterator = GetTypeIterator();

		while (TypeIterator.MoveNext())
		{
			FUnrealFieldDefinitionInfo* Type = *TypeIterator;
			if (Type->GetFName() == Name)
			{
				return Type;
			}
		}
	}

	return nullptr;
}

bool FScope::IsFileScope() const
{
	return Parent == nullptr;
}

bool FScope::ContainsTypes() const
{
	return TypeMap.Num() > 0;
}

FFileScope* FScope::GetFileScope()
{
	FScope* CurrentScope = this;
	while (!CurrentScope->IsFileScope())
	{
		CurrentScope = const_cast<FScope*>(CurrentScope->GetParent());
	}

	return CurrentScope->AsFileScope();
}

FFileScope::FFileScope(FName InName, FUnrealSourceFile* InSourceFile)
	: SourceFile(InSourceFile), Name(InName)
{ }

void FFileScope::IncludeScope(FFileScope* IncludedScope)
{
	IncludedScopes.Add(IncludedScope);
}

FUnrealSourceFile* FFileScope::GetSourceFile() const
{
	return SourceFile;
}

FName FFileScope::GetName() const
{
	return Name;
}

FName FStructScope::GetName() const
{
	return Struct->GetFName();
}
