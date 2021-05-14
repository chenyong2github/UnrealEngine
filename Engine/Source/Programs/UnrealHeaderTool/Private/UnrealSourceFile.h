// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Paths.h"
#include "Scope.h"
#include "HeaderProvider.h"
#include "UnrealTypeDefinitionInfo.h"
#include "GeneratedCodeVersion.h"

class UPackage;
class UClass;
class UStruct;
class FArchive;
class FStructMetaData;

enum class ETopologicalState : uint8
{
	Unmarked,
	Temporary,
	Permanent,
};

enum class ESourceFileTime : uint8
{
	Load,
	PreParse,
	Parse,
	Generate,
	Count,
};

/**
 * Contains information about source file that defines various UHT aware types.
 */
class FUnrealSourceFile : public TSharedFromThis<FUnrealSourceFile>
{
public:
	// Constructor.
	FUnrealSourceFile(UPackage* InPackage, const FString& InFilename)
		: Scope                (MakeShareable(new FFileScope(*(FString(TEXT("__")) + FPaths::GetBaseFilename(InFilename) + FString(TEXT("__File"))), this)))
		, Filename             (InFilename)
		, Package              (InPackage)
	{
		if (GetStrippedFilename() != "NoExportTypes")
		{
			Includes.Emplace(FHeaderProvider(EHeaderProviderSourceType::FileName, "NoExportTypes.h"));
		}
	}

	/**
	 * Adds given class to class definition list for this source file.
	 *
	 * @param ClassDecl Declaration information about the class
	 */
	void AddDefinedClass(TSharedRef<FUnrealTypeDefinitionInfo> ClassDecl);

	/**
	 * Gets array with classes defined in this source file with parsing info.
	 *
	 * @returns Array with classes defined in this source file with parsing info.
	 */
	TArray<TSharedRef<FUnrealTypeDefinitionInfo>>& GetDefinedClasses()
	{
		return DefinedClasses;
	}

	const TArray<TSharedRef<FUnrealTypeDefinitionInfo>>& GetDefinedClasses() const
	{
		return DefinedClasses;
	}

	/**
	 * Gets number of types defined in this source file.
	 */
	int32 GetDefinedClassesCount() const
	{
		return DefinedClasses.Num();
	}

	/**
	 * Adds given enum to enum definition list for this source file.
	 *
	 * @param EnumDecl Declaration information about the enum
	 */
	void AddDefinedEnum(TSharedRef<FUnrealTypeDefinitionInfo> EnumDecl);

	/**
	 * Gets array with enums defined in this source file with parsing info.
	 *
	 * @returns Array with enum defined in this source file with parsing info.
	 */
	TArray<TSharedRef<FUnrealTypeDefinitionInfo>>& GetDefinedEnums()
	{
		return DefinedEnums;
	}

	const TArray<TSharedRef<FUnrealTypeDefinitionInfo>>& GetDefinedEnums() const
	{
		return DefinedEnums;
	}

	/**
	 * Gets number of types defined in this source file.
	 */
	int32 GetDefinedEnumsCount() const
	{
		return DefinedEnums.Num();
	}

	/**
	 * Adds given struct to struct definition list for this source file.
	 *
	 * @param StructDecl Declaration information about the struct
	 */
	void AddDefinedStruct(TSharedRef<FUnrealTypeDefinitionInfo> StructDecl);

	/**
	 * Gets array with structs defined in this source file with parsing info.
	 *
	 * @returns Array with structs defined in this source file with parsing info.
	 */
	TArray<TSharedRef<FUnrealTypeDefinitionInfo>>& GetDefinedStructs()
	{
		return DefinedStructs;
	}

	const TArray<TSharedRef<FUnrealTypeDefinitionInfo>>& GetDefinedStructs() const
	{
		return DefinedStructs;
	}

	/**
	 * Gets number of types defined in this source file.
	 */
	int32 GetDefinedStructsCount() const
	{
		return DefinedStructs.Num();
	}

	/**
	 * Gets generated header filename.
	 */
	const FString& GetGeneratedHeaderFilename() const
	{
		if (GeneratedHeaderFilename.Len() == 0)
		{
			GeneratedHeaderFilename = FString::Printf(TEXT("%s.generated.h"), *FPaths::GetBaseFilename(Filename));
		}

		return GeneratedHeaderFilename;
	}

	/**
	 * Gets module relative path.
	 */
	const FString& GetModuleRelativePath() const
	{
		return ModuleRelativePath;
	}

	/**
	 * Gets stripped filename.
	 */
	const FString& GetStrippedFilename() const;

	/**
	 * Gets unique file id.
	 */
	const FString& GetFileId() const;

	/**
	 * Gets define name of this source file.
	 */
	FString GetFileDefineName() const;

	/**
	 * Gets file-wise generated body macro name.
	 *
	 * @param LineNumber Number at which generated body macro is.
	 * @param bLegacy Tells if method should get legacy generated body macro.
	 */
	FString GetGeneratedBodyMacroName(int32 LineNumber, bool bLegacy = false) const;

	/**
	 * Gets file-wise generated body macro name.
	 *
	 * @param LineNumber Number at which generated body macro is.
	 * @param Suffix Suffix to add to generated body macro name.
	 */
	FString GetGeneratedMacroName(int32 LineNumber, const TCHAR* Suffix) const;

	/**
	 * Gets file-wise generated body macro name.
	 *
	 * @param StructData Struct metadata for which to get generated body macro name.
	 * @param Suffix Suffix to add to generated body macro name.
	 */
	FString GetGeneratedMacroName(FStructMetaData& StructData, const TCHAR* Suffix = nullptr) const;

	/**
	 * Gets scope for this file.
	 */
	TSharedRef<FFileScope> GetScope() const
	{
		return Scope;
	}

	/**
	 * Gets package this file is in.
	 */
	UPackage* GetPackage() const
	{
		return Package;
	}

	/**
	 * Gets filename.
	 */
	const FString& GetFilename() const
	{
		return Filename;
	}

	/**
	 * Gets generated filename.
	 */
	const FString& GetGeneratedFilename() const
	{
		return GeneratedFilename;
	}

	/**
	 * Gets include path.
	 */
	const FString& GetIncludePath() const
	{
		return IncludePath;
	}

	/**
	 * Gets content.
	 */
	const FString& GetContent() const;

	/**
	 * Gets includes.
	 */
	TArray<FHeaderProvider>& GetIncludes()
	{
		return Includes;
	}

	/**
	 * Gets includes. Const version.
	 */
	const TArray<FHeaderProvider>& GetIncludes() const
	{
		return Includes;
	}

	/**
	 * Add an include for a class if required
	 */
	void AddClassIncludeIfNeeded(int32 InputLine, const FString& ClassNameWithoutPrefix, const FString& DependencyClassName);

	/**
	 * Add an include for a script struct if required
	 */
	void AddScriptStructIncludeIfNeeded(int32 InputLine, const FString& StructNameWithoutPrefix, const FString& DependencyStructName);

	/**
	 * Add an include for a type definition if required
	 */
	void AddTypeDefIncludeIfNeeded(FUnrealTypeDefinitionInfo& TypeDef);

	/**
	 * Add an include for a type definition if required
	 */
	void AddTypeDefIncludeIfNeeded(UField* Field);

	/**
	 * Gets generated code version for given UStruct.
	 */
	EGeneratedCodeVersion GetGeneratedCodeVersionForStruct(UStruct* Struct) const;

	/**
	 * Gets generated code versions.
	 */
	TMap<UStruct*, EGeneratedCodeVersion>& GetGeneratedCodeVersions()
	{
		return GeneratedCodeVersions;
	}

	/**
	 * Gets generated code versions. Const version.
	 */
	const TMap<UStruct*, EGeneratedCodeVersion>& GetGeneratedCodeVersions() const
	{
		return GeneratedCodeVersions;
	}

	/**
	 * Sets generated filename.
	 */
	void SetGeneratedFilename(FString&& GeneratedFilename);

	/**
	 * Sets has changed flag.
	 */
	void SetHasChanged(bool bHasChanged);

	/**
	 * Sets module relative path.
	 */
	void SetModuleRelativePath(FString&& ModuleRelativePath);

	/**
	 * Sets include path.
	 */
	void SetIncludePath(FString&& IncludePath);

	/**
	 * Sets the contents of the header stipped of CPP content
	 */
	void SetContent(FString&& InContent);

	/**
	 * Checks if generated file has been changed.
	 */
	bool HasChanged() const;

	/**
	 * Mark the source file as being public
	 */
	void MarkPublic()
	{
		bIsPublic = true;
	}

	/**
	 * Checks if the source file is public
	 */
	bool IsPublic() const
	{
		return bIsPublic;
	}

	/**
	 * Set the topological sort state
	 */
	void SetTopologicalState(ETopologicalState InTopologicalState)
	{
		TopologicalState = InTopologicalState;
	}

	/**
	 * Get the topological sort state
	 */
	ETopologicalState GetTopologicalState() const
	{
		return TopologicalState;
	}

	/**
	 * Set the number of lines parsed 
	 */
	void SetLinesParsed(int32 InLinesParsed)
	{
		LinesParsed = InLinesParsed;
	}

	/**
	 * Get the number of lines parsed 
	 */
	int32 GetLinesParsed() const
	{
		return LinesParsed;
	}

	/**
	 * Set the number of statements parsed
	 */
	void SetStatementsParsed(int32 InStatementsParsed)
	{
		StatementsParsed = InStatementsParsed;
	}

	/**
	 * Get the number of statements parsed
	 */
	int32 GetStatementsParsed() const
	{
		return StatementsParsed;
	}
	
	/**
	 * Get a reference to a time
	 */
	double& GetTime(ESourceFileTime Time)
	{
		return Times[int32(Time)];
	}

	/**
	 * Get the ordered index
	 */
	int32 GetOrderedIndex() const
	{
		return OrderedIndex;
	}

	/**
	 * Set the ordered index
	 */
	void SetOrderedIndex(int32 InOrderedIndex)
	{
		OrderedIndex = InOrderedIndex;
	}

	/**
	 * Get the collection of singletons 
	 */
	TArray<UField*>& GetSingletons()
	{
		return Singletons;
	}

	/**
	 * Mark this source file has being referenced
	 */
	void MarkReferenced()
	{
		bIsReferenced = true;
	}

	/**
	 * Return true if this source file should be exported
	 */
	bool ShouldExport() const
	{
		return bIsReferenced || GetScope()->ContainsTypes();
	}

private:

	// File scope.
	TSharedRef<FFileScope> Scope;

	// Path of this file.
	FString Filename;

	// Stripped path of this file
	mutable FString StrippedFilename;

	// Cached FileId for this file
	mutable FString FileId;

	// Package of this file.
	UPackage* Package;

	// File name of the generated header file associated with this file.
	FString GeneratedFilename;

	// File name of the generated header file associated with this file.
	mutable FString GeneratedHeaderFilename;

	// Module relative path.
	FString ModuleRelativePath;

	// Include path.
	FString IncludePath;

	// Source file content.
	FString Content;

	// Different timers for the source
	double Times[int32(ESourceFileTime::Count)] = { 0.0 };

	// Number of statements parsed.
	int32 StatementsParsed = 0;

	// Total number of lines parsed.
	int32 LinesParsed = 0;

	// Index of the source file when ordered
	int32 OrderedIndex = 0;

	// Tells if generated header file was changed.
	bool bHasChanged = false;

	// Tells if this is a public source file
	bool bIsPublic = false;

	// Tells if this file is referenced by another
	bool bIsReferenced = false;

	// Current topological sort state
	ETopologicalState TopologicalState = ETopologicalState::Unmarked;

	// This source file includes.
	TArray<FHeaderProvider> Includes;

	// List of classes defined in this source file along with parsing info.
	TArray<TSharedRef<FUnrealTypeDefinitionInfo>> DefinedClasses;

	// List of enums defined in this source file along with parsing info.
	TArray<TSharedRef<FUnrealTypeDefinitionInfo>> DefinedEnums;

	// List of structs defined in this source file along with parsing info.
	TArray<TSharedRef<FUnrealTypeDefinitionInfo>> DefinedStructs;

	// Mapping of UStructs to versions, according to which their code should be generated.
	TMap<UStruct*, EGeneratedCodeVersion> GeneratedCodeVersions;

	// Collection of all singletons found during code generation
	TArray<UField*> Singletons;
};
