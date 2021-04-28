// Copyright Epic Games, Inc. All Rights Reserved.

#include "UnrealTypeDefinitionInfo.h"
#include "NativeClassExporter.h"
#include "Scope.h"
#include "UnrealSourceFile.h"
#include "Misc/PackageName.h"
#include "UObject/ErrorException.h"

FUnrealPropertyDefinitionInfo* FUnrealTypeDefinitionInfo::AsProperty()
{
	return nullptr;
}

FUnrealObjectDefinitionInfo* FUnrealTypeDefinitionInfo::AsObject()
{
	return nullptr;
}

FUnrealPackageDefinitionInfo* FUnrealTypeDefinitionInfo::AsPackage()
{
	return nullptr;
}

FUnrealFieldDefinitionInfo* FUnrealTypeDefinitionInfo::AsField()
{
	return nullptr;
}

FUnrealEnumDefinitionInfo* FUnrealTypeDefinitionInfo::AsEnum()
{
	return nullptr;
}

FUnrealStructDefinitionInfo* FUnrealTypeDefinitionInfo::AsStruct()
{
	return nullptr;
}

FUnrealScriptStructDefinitionInfo* FUnrealTypeDefinitionInfo::AsScriptStruct()
{
	return nullptr;
}

FUnrealFunctionDefinitionInfo* FUnrealTypeDefinitionInfo::AsFunction()
{
	return nullptr;
}

FUnrealClassDefinitionInfo* FUnrealTypeDefinitionInfo::AsClass()
{
	return nullptr;
}

TSharedRef<FScope> FUnrealTypeDefinitionInfo::GetScope()
{
	if (!HasSource())
	{
		FError::Throwf(TEXT("Attempt to fetch the scope for type \"%s\" when it doesn't implement the method or there is no source file associated with the type."), *GetNameCPP());
	}
	return GetUnrealSourceFile().GetScope();
}

// Constructor
FUnrealPackageDefinitionInfo::FUnrealPackageDefinitionInfo(const FManifestModule& InModule, UPackage* InPackage)
	: FUnrealObjectDefinitionInfo(FString())
	, Module(InModule)
	, ShortUpperName(FPackageName::GetShortName(InPackage).ToUpper())
	, API(FString::Printf(TEXT("%s_API "), *ShortUpperName))
{
	SetObject(InPackage);
}

void FUnrealTypeDefinitionInfo::SetHash(uint32 InHash)
{
	Hash = InHash;
}

uint32 FUnrealTypeDefinitionInfo::GetHash(bool bIncludeNoExport) const
{
	if (Hash == 0)
	{
		FError::Throwf(TEXT("Attempt to fetch the generated hash for type \"%s\" before it has been generated.  Include dependencies, topological sort, or job graph is in error."), *GetNameCPP());
	}
	return Hash;
}

void FUnrealTypeDefinitionInfo::GetHashTag(FUHTStringBuilder& Out) const
{
	uint32 TempHash = GetHash(false);
	if (TempHash != 0)
	{
		if (Out.IsEmpty())
		{
			Out.Appendf(TEXT(" // %u"), TempHash);
		}
		else
		{
			Out.Appendf(TEXT(" %u"), TempHash);
		}
	}
}

uint32 FUnrealScriptStructDefinitionInfo::GetHash(bool bIncludeNoExport) const
{
	if (!bIncludeNoExport)
	{
		UScriptStruct* Struct = GetScriptStruct();
		if ((Struct->StructFlags & STRUCT_NoExport) != 0)
		{
			return 0;
		}
	}
	return FUnrealStructDefinitionInfo::GetHash(bIncludeNoExport);
}

TSharedRef<FScope> FUnrealStructDefinitionInfo::GetScope()
{
	if (StructScope.IsValid())
	{
		return StructScope.ToSharedRef();
	}
	else
	{
		return FUnrealFieldDefinitionInfo::GetScope();
	}
}

void FUnrealStructDefinitionInfo::SetObject(UObject* InObject)
{
	check(InObject != nullptr);

	FUnrealFieldDefinitionInfo::SetObject(InObject);


	// Don't create a scope for things without a source.  Those are builtin types
	if (HasSource())
	{
		StructScope = MakeShared<FStructScope>(static_cast<UStruct*>(InObject), &GetUnrealSourceFile().GetScope().Get());
	}
}

uint32 FUnrealClassDefinitionInfo::GetHash(bool bIncludeNoExport) const
{
	if (!bIncludeNoExport)
	{
		UClass* Class = GetClass();
		if (Class->HasAnyClassFlags(CLASS_NoExport))
		{
			return 0;
		}
	}
	return FUnrealStructDefinitionInfo::GetHash(bIncludeNoExport);
}

