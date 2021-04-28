// Copyright Epic Games, Inc. All Rights Reserved.

#include "UnrealSourceFile.h"
#include "UnrealHeaderTool.h"
#include "Misc/PackageName.h"
#include "HeaderParser.h"
#include "Algo/Find.h"
#include "StringUtils.h"
#include "Exceptions.h"
#include "ClassMaps.h"

void FUnrealSourceFile::AddDefinedClass(TSharedRef<FUnrealTypeDefinitionInfo> ClassDecl)
{
	DefinedClasses.Add(MoveTemp(ClassDecl));
}

void FUnrealSourceFile::AddDefinedEnum(TSharedRef<FUnrealTypeDefinitionInfo> EnumDecl)
{
	DefinedEnums.Add(MoveTemp(EnumDecl));
}

void FUnrealSourceFile::AddDefinedStruct(TSharedRef<FUnrealTypeDefinitionInfo> StructDecl)
{
	DefinedStructs.Add(MoveTemp(StructDecl));
}

const FString& FUnrealSourceFile::GetFileId() const
{
	if (FileId.Len() == 0)
	{
		FString StdFilename = Filename;

		FPaths::MakeStandardFilename(StdFilename);

		bool bRelativePath = FPaths::IsRelative(StdFilename);

		if (!bRelativePath)
		{
			// If path is still absolute that means MakeStandardFilename has failed
			// In this case make it relative to the current project. 
			bRelativePath = FPaths::MakePathRelativeTo(StdFilename, *FPaths::GetPath(FPaths::GetProjectFilePath()));
		}

		// If the path has passed either MakeStandardFilename or MakePathRelativeTo it should be using internal path seperators
		if (bRelativePath)
		{
			// Remove any preceding parent directory paths
			while (StdFilename.RemoveFromStart(TEXT("../")));
		}

		FStringOutputDevice Out;

		for (TCHAR Char : StdFilename)
		{
			if (FChar::IsAlnum(Char))
			{
				Out.AppendChar(Char);
			}
			else
			{
				Out.AppendChar('_');
			}
		}

		FileId = MoveTemp(Out);
	}

	return FileId;
}

const FString& FUnrealSourceFile::GetStrippedFilename() const
{
	if (StrippedFilename.Len() == 0)
	{
		StrippedFilename = FPaths::GetBaseFilename(Filename);
	}

	return StrippedFilename;
}

FString FUnrealSourceFile::GetGeneratedMacroName(FClassMetaData* ClassData, const TCHAR* Suffix) const
{
	return GetGeneratedMacroName(ClassData->GetGeneratedBodyLine(), Suffix);
}

FString FUnrealSourceFile::GetGeneratedMacroName(int32 LineNumber, const TCHAR* Suffix) const
{
	if (Suffix != nullptr)
	{
		return FString::Printf(TEXT("%s_%d%s"), *GetFileId(), LineNumber, Suffix);
	}

	return FString::Printf(TEXT("%s_%d"), *GetFileId(), LineNumber);
}

FString FUnrealSourceFile::GetGeneratedBodyMacroName(int32 LineNumber, bool bLegacy) const
{
	return GetGeneratedMacroName(LineNumber, *FString::Printf(TEXT("%s%s"), TEXT("_GENERATED_BODY"), bLegacy ? TEXT("_LEGACY") : TEXT("")));
}

void FUnrealSourceFile::SetGeneratedFilename(FString&& InGeneratedFilename)
{
	GeneratedFilename = MoveTemp(InGeneratedFilename);
}

void FUnrealSourceFile::SetHasChanged(bool bInHasChanged)
{
	bHasChanged = bInHasChanged;
}

void FUnrealSourceFile::SetModuleRelativePath(FString&& InModuleRelativePath)
{
	ModuleRelativePath = MoveTemp(InModuleRelativePath);
}

void FUnrealSourceFile::SetIncludePath(FString&& InIncludePath)
{
	IncludePath = MoveTemp(InIncludePath);
}

void FUnrealSourceFile::SetContent(FString&& InContent)
{
	Content = MoveTemp(InContent);
}

const FString& FUnrealSourceFile::GetContent() const
{
	return Content;
}

EGeneratedCodeVersion FUnrealSourceFile::GetGeneratedCodeVersionForStruct(UStruct* Struct) const
{
	if (const EGeneratedCodeVersion* Version = GeneratedCodeVersions.Find(Struct))
	{
		return *Version;
	}

	return FUHTConfig::Get().DefaultGeneratedCodeVersion;
}

bool FUnrealSourceFile::HasChanged() const
{
	return bHasChanged;
}

FString FUnrealSourceFile::GetFileDefineName() const
{
	const FString API = FPackageName::GetShortName(Package).ToUpper();
	return FString::Printf(TEXT("%s_%s_generated_h"), *API, *GetStrippedFilename());
}

void FUnrealSourceFile::AddClassIncludeIfNeeded(int32 InputLine, const FString& ClassNameWithoutPrefix, const FString& DependencyClassName)
{
	if (!Algo::FindBy(GetDefinedClasses(), DependencyClassName, [](const TSharedRef<FUnrealTypeDefinitionInfo>& Info) { return Info->GetNameCPP(); }))
	{
		FString DependencyClassNameWithoutPrefix = GetClassNameWithPrefixRemoved(DependencyClassName);

		if (ClassNameWithoutPrefix == DependencyClassNameWithoutPrefix)
		{
			FUHTException::Throwf(*this, InputLine, TEXT("A class cannot inherit itself or a type with the same name but a different prefix"));
		}

		FString StrippedDependencyName = DependencyClassName.Mid(1);

		// Only add a stripped dependency if the stripped name differs from the stripped class name
		// otherwise it's probably a class with a different prefix.
		if (StrippedDependencyName != ClassNameWithoutPrefix)
		{
			GetIncludes().AddUnique(FHeaderProvider(EHeaderProviderSourceType::ClassName, MoveTemp(StrippedDependencyName)));
		}
	}
}

void FUnrealSourceFile::AddScriptStructIncludeIfNeeded(int32 InputLine, const FString& StructNameWithoutPrefix, const FString& DependencyStructName)
{
	if (!Algo::FindBy(GetDefinedStructs(), DependencyStructName, [](const TSharedRef<FUnrealTypeDefinitionInfo>& Info) { return Info->GetNameCPP(); }))
	{
		FString DependencyStructNameWithoutPrefix = GetClassNameWithPrefixRemoved(DependencyStructName);

		if (StructNameWithoutPrefix == DependencyStructNameWithoutPrefix)
		{
			FUHTException::Throwf(*this, InputLine, TEXT("A struct cannot inherit itself or a type with the same name but a different prefix"));
		}

		FString StrippedDependencyName = DependencyStructName.Mid(1);

		// Only add a stripped dependency if the stripped name differs from the stripped class name
		// otherwise it's probably a class with a different prefix.
		if (StrippedDependencyName != StructNameWithoutPrefix)
		{
			GetIncludes().AddUnique(FHeaderProvider(EHeaderProviderSourceType::ScriptStructName, FString(DependencyStructName))); // Structs don't use the stripped name
		}
	}
}

void FUnrealSourceFile::AddTypeDefIncludeIfNeeded(FUnrealTypeDefinitionInfo& TypeDef)
{
	check(TypeDef.HasSource());
	if (&TypeDef.GetUnrealSourceFile() != this)
	{
		GetIncludes().AddUnique(FHeaderProvider(TypeDef));
	}
}

void FUnrealSourceFile::AddTypeDefIncludeIfNeeded(UField* Field)
{
	if (Field != nullptr)
	{
		if (TSharedRef<FUnrealTypeDefinitionInfo>* TypeDef = GTypeDefinitionInfoMap.Find(Field))
		{
			AddTypeDefIncludeIfNeeded(**TypeDef);
		}
	}
}

