// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"
#include "ClassDeclarationMetaData.h"
#include <atomic>

#include "ParserHelper.h" // TEMPORARY

// Forward declarations.
class FClass;
class FScope;
class FUnrealSourceFile;
struct FManifestModule;

// These are declared in this way to allow swapping out the classes for something more optimized in the future
typedef FStringOutputDevice FUHTStringBuilder;

// The FUnrealTypeDefinitionInfo represents most all types required during parsing.  At this time, these structures
// have a 1-1 correspondence with Engine types such as UClass and UProperty.  The goal is to provide a universal mechanism
// of associating compiler data with given types without needing to add extra data to the engine types.  They are also
// intended to eliminate all other UHT specific containers that map extra data with Engine classes.

// The following types are supported represented in hierarchal form below:
class FUnrealTypeDefinitionInfo;							// Base for all types, provides virtual methods to cast between all types
	class FUnrealPropertyDefinitionInfo;					// Represents properties (FField)
	class FUnrealObjectDefinitionInfo;						// Represents UObject
		class FUnrealPackageDefinitionInfo;					// Represents UPackage
		class FUnrealFieldDefinitionInfo;					// Represents UField
			class FUnrealEnumDefinitionInfo;				// Represents UEnum
			class FUnrealStructDefinitionInfo;				// Represents UStruct
				class FUnrealScriptStructDefinitionInfo;	// Represents UScriptStruct
				class FUnrealClassDefinitionInfo;			// Represents UClass
				class FUnrealFunctionDefinitionInfo;		// Represents UFunction

enum class EUnderlyingEnumType
{
	Unspecified,
	uint8,
	uint16,
	uint32,
	uint64,
	int8,
	int16,
	int32,
	int64
};

enum class ESerializerArchiveType
{
	None = 0,

	Archive = 1,
	StructuredArchiveRecord = 2
};
ENUM_CLASS_FLAGS(ESerializerArchiveType)

/**
 * Class that stores information about type definitions.
 */
class FUnrealTypeDefinitionInfo : public TSharedFromThis<FUnrealTypeDefinitionInfo>
{
public:
	virtual ~FUnrealTypeDefinitionInfo() = default;

	/**
	 * If this is a property, return the property version of the object
	 * @return Pointer to the definition info or nullptr if not of that type.
	 */
	virtual FUnrealPropertyDefinitionInfo* AsProperty();

	/**
	 * If this is a property, return the property version of the object
	 * @return Reference to the definition.  Will assert if not of that type.
	 */
	FUnrealPropertyDefinitionInfo& AsPropertyChecked()
	{
		FUnrealPropertyDefinitionInfo* Info = AsProperty();
		check(Info);
		return *Info;
	}

	/**
	 * If this is an object, return the object version of the object
	 * @return Pointer to the definition info or nullptr if not of that type.
	 */
	virtual FUnrealObjectDefinitionInfo* AsObject();

	/**
	 * If this is a object, return the object version of the object
	 * @return Reference to the definition.  Will assert if not of that type.
	 */
	FUnrealObjectDefinitionInfo& AsObjectChecked()
	{
		FUnrealObjectDefinitionInfo* Info = AsObject();
		check(Info);
		return *Info;
	}

	/**
	 * If this is a package, return the package version of the object
	 * @return Pointer to the definition info or nullptr if not of that type.
	 */
	virtual FUnrealPackageDefinitionInfo* AsPackage();

	/**
	 * If this is a package, return the package version of the object
	 * @return Reference to the definition.  Will assert if not of that type.
	 */
	FUnrealPackageDefinitionInfo& AsPackageChecked()
	{
		FUnrealPackageDefinitionInfo* Info = AsPackage();
		check(Info);
		return *Info;
	}

	/**
	 * If this is an field, return the field version of the object
	 * @return Pointer to the definition info or nullptr if not of that type.
	 */
	virtual FUnrealFieldDefinitionInfo* AsField();

	/**
	 * If this is an field, return the field version of the object
	 * @return Reference to the definition.  Will assert if not of that type.
	 */
	FUnrealFieldDefinitionInfo& AsFieldChecked()
	{
		FUnrealFieldDefinitionInfo* Info = AsField();
		check(Info);
		return *Info;
	}

	/**
	 * If this is an enumeration, return the enumeration version of the object
	 * @return Pointer to the definition info or nullptr if not of that type.
	 */
	virtual FUnrealEnumDefinitionInfo* AsEnum();

	/**
	 * If this is an enumeration, return the enumeration version of the object
	 * @return Reference to the definition.  Will assert if not of that type.
	 */
	FUnrealEnumDefinitionInfo& AsEnumChecked()
	{
		FUnrealEnumDefinitionInfo* Info = AsEnum();
		check(Info);
		return *Info;
	}

	/**
	 * If this is a struct, return the struct version of the object
	 * @return Pointer to the definition info or nullptr if not of that type.
	 */
	virtual FUnrealStructDefinitionInfo* AsStruct();

	/**
	 * If this is a struct, return the struct version of the object
	 * @return Reference to the definition.  Will assert if not of that type.
	 */
	FUnrealStructDefinitionInfo& AsStructChecked()
	{
		FUnrealStructDefinitionInfo* Info = AsStruct();
		check(Info);
		return *Info;
	}

	/**
	 * If this is a script struct, return the script struct version of the object
	 * @return Pointer to the definition info or nullptr if not of that type.
	 */
	virtual FUnrealScriptStructDefinitionInfo* AsScriptStruct();

	/**
	 * If this is a script struct, return the script struct version of the object
	 * @return Reference to the definition.  Will assert if not of that type.
	 */
	FUnrealScriptStructDefinitionInfo& AsScriptStructChecked()
	{
		FUnrealScriptStructDefinitionInfo* Info = AsScriptStruct();
		check(Info);
		return *Info;
	}

	/**
	 * If this is a function, return the function version of the object
	 * @return Pointer to the definition info or nullptr if not of that type.
	 */
	virtual FUnrealFunctionDefinitionInfo* AsFunction();

	/**
	 * If this is a function, return the function version of the object
	 * @return Reference to the definition.  Will assert if not of that type.
	 */
	FUnrealFunctionDefinitionInfo& AsFunctionChecked()
	{
		FUnrealFunctionDefinitionInfo* Info = AsFunction();
		check(Info);
		return *Info;
	}

	/**
	 * If this is a class, return the class version of the object
	 * @return Pointer to the definition info or nullptr if not of that type.
	 */
	virtual FUnrealClassDefinitionInfo* AsClass();

	/**
	 * If this is a class, return the class version of the object
	 * @return Reference to the definition.  Will assert if not of that type.
	 */
	FUnrealClassDefinitionInfo& AsClassChecked()
	{
		FUnrealClassDefinitionInfo* Info = AsClass();
		check(Info);
		return *Info;
	}

	/**
	 * Perform any post parsing finalization and validation
	 */
	virtual void PostParseFinalize()
	{}

	/**
	 * Return the C++ type name that represents this type (i.e. UClass)
	 */
	virtual const TCHAR* GetSimplifiedTypeClass() const = 0;

	/**
	 * Return the compilation scope associated with this object
	 */
	virtual TSharedRef<FScope> GetScope();

	/**
	 * Return the CPP version of the name
	 */
	const FString& GetNameCPP() const
	{
		return NameCPP;
	}

	/**
	 * Return true if this type has source information
	 */
	bool HasSource() const
	{
		return SourceFile != nullptr;
	}

	/**
	 * Gets the line number in source file this type was defined in.
	 */
	int32 GetLineNumber() const
	{
		check(HasSource());
		return LineNumber;
	}

	/**
	 * Gets the reference to FUnrealSourceFile object that stores information about
	 * source file this type was defined in.
	 */
	FUnrealSourceFile& GetUnrealSourceFile()
	{
		check(HasSource());
		return *SourceFile;
	}

	const FUnrealSourceFile& GetUnrealSourceFile() const
	{
		check(HasSource());
		return *SourceFile;
	}

	/**
	 * Set the hash calculated from the generated code for this type
	 */
	void SetHash(uint32 InHash);

	/**
	 * Return the previously set hash. This method will assert if the hash has not been set.
	 */
	virtual uint32 GetHash(bool bIncludeNoExport = true) const;

	/**
	 * Return the hash as a code comment.
	 */
	void GetHashTag(FUHTStringBuilder& Out) const;

protected:
	explicit FUnrealTypeDefinitionInfo(FString&& InNameCPP)
		: NameCPP(MoveTemp(InNameCPP))
	{ }

	FUnrealTypeDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, FString&& InNameCPP)
		: NameCPP(MoveTemp(InNameCPP))
		, SourceFile(&InSourceFile)
		, LineNumber(InLineNumber)
	{ }

private:
	FString NameCPP;
	FUnrealSourceFile* SourceFile = nullptr;
	int32 LineNumber = 0;
	std::atomic<uint32> Hash = 0;
};

/**
 * Class that stores information about type definitions derived from UProperty
 */
class FUnrealPropertyDefinitionInfo 
	: public FUnrealTypeDefinitionInfo
{
public:
	FUnrealPropertyDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, FProperty* InProperty)
		: FUnrealTypeDefinitionInfo(InSourceFile, InLineNumber, FString())
		, Property(InProperty)
	{ }

	FUnrealPropertyDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, FProperty* InProperty, bool bInUnsized)
		: FUnrealTypeDefinitionInfo(InSourceFile, InLineNumber, FString())
		, Property(InProperty)
		, bUnsized(bInUnsized)
	{ }

	FUnrealPropertyDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, FProperty* InProperty, EAllocatorType InAllocatorType)
		: FUnrealTypeDefinitionInfo(InSourceFile, InLineNumber, FString())
		, Property(InProperty)
		, AllocatorType(InAllocatorType)
	{ }

	virtual FUnrealPropertyDefinitionInfo* AsProperty() override
	{
		return this;
	}

	/**
	 * Return the C++ type name that represents this type (i.e. UClass)
	 */
	virtual const TCHAR* GetSimplifiedTypeClass() const override
	{
		return TEXT("UProperty");
	}

	/**
	 * Return the Engine instance associated with the compiler instance
	 */
	FProperty* GetProperty() const
	{
		check(Property);
		return Property;
	}

	/**
	 * Set the string that represents the array dimensions
	 */
	void SetArrayDimensions(const FString& InArrayDimensions)
	{
		ArrayDimensions = InArrayDimensions;
		check(!ArrayDimensions.IsEmpty());
	}

	/**
	 * Get the string that represents the array dimensions.  A nullptr is returned if the property doesn't have any dimensions
	 */
	const TCHAR* GetArrayDimensions() const
	{
		return ArrayDimensions.IsEmpty() ? nullptr : *ArrayDimensions;
	}

	/**
	 * Return true if the property is unsized
	 */
	bool IsUnsized() const
	{
		return bUnsized;
	}

	/**
	 * Return the allocator type
	 */
	EAllocatorType GetAllocatorType() const
	{
		return AllocatorType;
	}

	/**
	 * Return the token associated with the property parsing
	 */
	FToken& GetToken()
	{
		return Token;
	}
	const FToken& GetToken() const
	{
		return Token;
	}

private:
	FToken Token;
	FString ArrayDimensions;
	FProperty* Property = nullptr;
	EAllocatorType AllocatorType = EAllocatorType::Default;
	bool bUnsized = false;
};

/**
 * Class that stores information about type definitions derived from UObject.
 */
class FUnrealObjectDefinitionInfo
	: public FUnrealTypeDefinitionInfo
{
public:

	virtual FUnrealObjectDefinitionInfo* AsObject() override
	{
		return this;
	}

	/**
	 * Return the Engine instance associated with the compiler instance
	 */
	UObject* GetObject() const
	{
		check(Object);
		return Object;
	}

	/**
	 * Set the Engine instance associated with the compiler instance
	 */
	virtual void SetObject(UObject* InObject)
	{
		check(InObject != nullptr);
		check(Object == nullptr);
		Object = InObject;
	}

protected:
	using FUnrealTypeDefinitionInfo::FUnrealTypeDefinitionInfo;

private:
	UObject* Object = nullptr;
};

/**
 * Class that stores information about packages.
 */
class FUnrealPackageDefinitionInfo : public FUnrealObjectDefinitionInfo
{
public:
	// Constructor
	FUnrealPackageDefinitionInfo(const FManifestModule& InModule, UPackage* InPackage);

	virtual FUnrealPackageDefinitionInfo* AsPackage() override
	{
		return this;
	}

	/**
	 * Return the Engine instance associated with the compiler instance
	 */
	UPackage* GetPackage() const
	{
		return static_cast<UPackage*>(GetObject());
	}

	/**
	 * Return the C++ type name that represents this type (i.e. UClass)
	 */
	virtual const TCHAR* GetSimplifiedTypeClass() const override
	{
		return TEXT("UPackage");
	}

	/**
	 * Return the module information from the manifest associated with this package
	 */
	const FManifestModule& GetModule()
	{
		return Module;
	}

	/**
	 * Return a collection of all source files contained within this package.
	 * This collection is always valid.
	 */
	TArray<TSharedRef<FUnrealSourceFile>>& GetAllSourceFiles()
	{
		return AllSourceFiles;
	}

	/**
	 * Return a collection of all classes associated with this package.  This is not valid until parsing begins.
	 */
	TArray<UClass*>& GetAllClasses()
	{
		return AllClasses;
	}

	/**
	 * If true, this package should generate the classes H file.  This is not valid until code generation begins.
	 */
	bool GetWriteClassesH() const
	{
		return bWriteClassesH;
	}

	/**
	 * Set the flag indicating that the classes H file should be generated.
	 */
	void SetWriteClassesH(bool bInWriteClassesH)
	{
		bWriteClassesH = bInWriteClassesH;
	}

	/**
	 * Return a string that references the "PACKAGE_API " macro with a trailing space.
	 */
	const FString& GetAPI() const
	{
		return API;
	}

	/**
	 * Get the short name of the package uppercased.
	 */
	const FString& GetShortUpperName() const
	{
		return ShortUpperName;
	}

	/**
	 * Perform any post parsing finalization and validation
	 */
	virtual void PostParseFinalize() override;

	/**
	 * Add a unique cross module reference for this field
	 */
	void AddCrossModuleReference(TSet<FString>* UniqueCrossModuleReferences) const;

	/**
	 * Return the name of the singleton for this field.  Only valid post parsing
	 */
	const FString& GetSingletonName() const
	{
		return SingletonName;
	}

	/**
	 * Return the name of the singleton without the trailing "()" for this field.  Only valid post parsing
	 */
	const FString& GetSingletonNameChopped() const
	{
		return SingletonNameChopped;
	}

	/**
	 * Return the external declaration for this field.  Only valid post parsing
	 */
	const FString& GetExternDecl() const
	{
		return ExternDecl;
	}

private:
	const FManifestModule& Module;
	TArray<TSharedRef<FUnrealSourceFile>> AllSourceFiles;
	TArray<UClass*> AllClasses;
	FString SingletonName;
	FString SingletonNameChopped;
	FString ExternDecl;
	FString ShortUpperName;
	FString API;
	bool bWriteClassesH = false;
};

/**
 * Class that stores information about type definitions derived from UField.
 */
class FUnrealFieldDefinitionInfo 
	: public FUnrealObjectDefinitionInfo
{
protected:
	using FUnrealObjectDefinitionInfo::FUnrealObjectDefinitionInfo;

public:

	virtual FUnrealFieldDefinitionInfo* AsField() override
	{
		return this;
	}

	/**
	 * Return the Engine instance associated with the compiler instance
	 */
	UField* GetField() const
	{
		return static_cast<UField*>(GetObject());
	}

	/**
	 * Perform any post parsing finalization and validation
	 */
	virtual void PostParseFinalize() override;

	/**
	 * Add a unique cross module reference for this field
	 */
	void AddCrossModuleReference(TSet<FString>* UniqueCrossModuleReferences, bool bRequiresValidObject) const;

	/**
	 * Return the name of the singleton for this field.  Only valid post parsing
	 */
	const FString& GetSingletonName(bool bRequiresValidObject) const
	{
		return SingletonName[bRequiresValidObject];
	}

	/**
	 * Return the name of the singleton without the trailing "()" for this field.  Only valid post parsing
	 */
	const FString& GetSingletonNameChopped(bool bRequiresValidObject) const
	{
		return SingletonNameChopped[bRequiresValidObject];
	}

	/**
	 * Return the external declaration for this field.  Only valid post parsing
	 */
	const FString& GetExternDecl(bool bRequiresValidObject) const
	{
		return ExternDecl[bRequiresValidObject];
	}

private:
	FString SingletonName[2];
	FString SingletonNameChopped[2];
	FString ExternDecl[2];
};

/**
 * Class that stores information about type definitions derived from UEnum
 */
class FUnrealEnumDefinitionInfo : public FUnrealFieldDefinitionInfo
{
public:
	FUnrealEnumDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, FString&& InNameCPP)
		: FUnrealFieldDefinitionInfo(InSourceFile, InLineNumber, MoveTemp(InNameCPP))
	{ }

	virtual FUnrealEnumDefinitionInfo* AsEnum() override
	{
		return this;
	}

	/**
	 * Return the C++ type name that represents this type (i.e. UClass)
	 */
	virtual const TCHAR* GetSimplifiedTypeClass() const override
	{
		return TEXT("UEnum");
	}

	/**
	 * Return the Engine instance associated with the compiler instance
	 */
	UEnum* GetEnum() const
	{
		return static_cast<UEnum*>(GetObject());
	}

	/**
	 * Returns the underlying enumeration type
	 */
	EUnderlyingEnumType GetUnderlyingType() const
	{
		return UnderlyingType;
	}

	/** 
	 * Set the underlying enum type
	 */
	void SetUnderlyingType(EUnderlyingEnumType InUnderlyingType)
	{
		UnderlyingType = InUnderlyingType;
	}

	/**
	 * Return true if the enumeration is editor only
	 */
	bool IsEditorOnly() const
	{
		return bIsEditorOnly;
	}

	/**
	 * Make the enumeration editor only
	 */
	void MakeEditorOnly()
	{
		bIsEditorOnly = true;
	}

private:
	EUnderlyingEnumType UnderlyingType = EUnderlyingEnumType::Unspecified;
	bool bIsEditorOnly = false;
};

/**
 * Class that stores information about type definitions derived from UStruct
 */
class FUnrealStructDefinitionInfo : public FUnrealFieldDefinitionInfo
{
public:
	using FUnrealFieldDefinitionInfo::FUnrealFieldDefinitionInfo;

	virtual FUnrealStructDefinitionInfo* AsStruct() override
	{
		return this;
	}

	virtual TSharedRef<FScope> GetScope() override;

	virtual void SetObject(UObject* InObject) override;

	/**
	 * Return the Engine instance associated with the compiler instance
	 */
	UStruct* GetStruct() const
	{
		return static_cast<UStruct*>(GetObject());
	}

	/**
	 * Add a new property to the structure
	 */
	virtual void AddProperty(FUnrealPropertyDefinitionInfo& PropertyDef)
	{
		Properties.Add(&PropertyDef);
	}

	/**
	 * Get the collection of properties
	 */
	const TArray<FUnrealPropertyDefinitionInfo*> GetProperties() const
	{
		return Properties;
	}
	TArray<FUnrealPropertyDefinitionInfo*> GetParameters()
	{
		return Properties;
	}

	/**
	 * Return the class meta data information
	 */
	FStructMetaData& GetStructMetaData()
	{
		return StructMetaData;
	}

private:
	TSharedPtr<FScope> StructScope;

	/** Properties of the structure */
	TArray<FUnrealPropertyDefinitionInfo*> Properties;

	FStructMetaData StructMetaData;
};

/**
 * Class that stores information about type definitions derived from UScriptStruct
 */
class FUnrealScriptStructDefinitionInfo : public FUnrealStructDefinitionInfo
{
public:
	FUnrealScriptStructDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, FString&& InNameCPP, FString&& InParentScopeCPP, FString&& InParentNameCPP)
		: FUnrealStructDefinitionInfo(InSourceFile, InLineNumber, MoveTemp(InNameCPP))
		, ParentScopeCPP(MoveTemp(InParentScopeCPP))
		, ParentNameCPP(MoveTemp(InParentNameCPP))
	{ }

	virtual FUnrealScriptStructDefinitionInfo* AsScriptStruct() override
	{
		return this;
	}

	virtual uint32 GetHash(bool bIncludeNoExport = true) const override;

	/**
	 * Return the C++ type name that represents this type (i.e. UClass)
	 */
	virtual const TCHAR* GetSimplifiedTypeClass() const override
	{
		return TEXT("UScriptStruct");
	}

	/**
	 * Return the Engine instance associated with the compiler instance
	 */
	UScriptStruct* GetScriptStruct() const
	{
		return static_cast<UScriptStruct*>(GetObject());
	}

	/**
	 * Return the parent structure scoped name (currently blank)
	 */
	const FString& GetParentScopeCPP() const
	{
		return ParentScopeCPP;
	}

	/**
	 * Return the name of the parent structure.  If not derived from another struct, it will be empty
	 */
	const FString& GetParentNameCPP() const
	{
		return ParentNameCPP;
	}

private:
	FString ParentScopeCPP;
	FString ParentNameCPP;
};

/**
 * Class that stores information about type definitions derrived from UFunction
 */
class FUnrealFunctionDefinitionInfo : public FUnrealStructDefinitionInfo
{
public:
	FUnrealFunctionDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, FString&& InNameCPP, FFuncInfo&& InFuncInfo)
		: FUnrealStructDefinitionInfo(InSourceFile, InLineNumber, MoveTemp(InNameCPP))
		, FunctionData(MoveTemp(InFuncInfo))
	{ }

	virtual FUnrealFunctionDefinitionInfo* AsFunction() override
	{
		return this;
	}

	/**
	 * Return the C++ type name that represents this type (i.e. UClass)
	 */
	virtual const TCHAR* GetSimplifiedTypeClass() const override
	{
		return TEXT("UFunction");
	}

	/**
	 * Return the Engine instance associated with the compiler instance
	 */
	UFunction* GetFunction() const
	{
		return static_cast<UFunction*>(GetField());
	}

	/** @name getters */
	//@{
	FFuncInfo& GetFunctionData() { return FunctionData; }
	const FFuncInfo& GetFunctionData() const { return FunctionData; }
	FUnrealPropertyDefinitionInfo* GetReturn() const { return ReturnProperty; }
	//@}

	void UpdateFunctionData(FFuncInfo& UpdatedFuncData)
	{
		//@TODO: UCREMOVAL: Some more thorough evaluation should be done here
		FunctionData.FunctionFlags |= UpdatedFuncData.FunctionFlags;
		FunctionData.FunctionExportFlags |= UpdatedFuncData.FunctionExportFlags;
	}

	/**
	 * Adds a new function property to be tracked.  Determines whether the property is a
	 * function parameter, local property, or return value, and adds it to the appropriate
	 * list
	 *
	 * @param	PropertyToken	the property to add
	 */
	virtual void AddProperty(FUnrealPropertyDefinitionInfo& PropertyDef) override
	{
		const FProperty* Prop = PropertyDef.GetProperty();
		check((Prop->PropertyFlags & CPF_Parm) != 0);

		if ((Prop->PropertyFlags & CPF_ReturnParm) != 0)
		{
			check(ReturnProperty == nullptr);
			ReturnProperty = &PropertyDef;
		}
		else
		{
			FUnrealStructDefinitionInfo::AddProperty(PropertyDef);
		}
	}

	/**
	 * Sets the specified function export flags
	 */
	void SetFunctionExportFlag(uint32 NewFlags)
	{
		FunctionData.FunctionExportFlags |= NewFlags;
	}

	/**
	 * Clears the specified function export flags
	 */
	void ClearFunctionExportFlags(uint32 ClearFlags)
	{
		FunctionData.FunctionExportFlags &= ~ClearFlags;
	}

private:

	/** info about the function associated with this FFunctionData */
	FFuncInfo FunctionData;

	/** return value for this function */
	FUnrealPropertyDefinitionInfo* ReturnProperty = nullptr;
};

/**
 * Class that stores information about type definitions derived from UClass
 */
class FUnrealClassDefinitionInfo
	: public FUnrealStructDefinitionInfo
	, public FClassDeclarationMetaData
{
public:
	FUnrealClassDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, FString&& InNameCPP, FString&& InBaseClassNameCPP, bool bInIsInterface)
		: FUnrealStructDefinitionInfo(InSourceFile, InLineNumber, MoveTemp(InNameCPP))
		, BaseClassNameCPP(MoveTemp(InBaseClassNameCPP))
		, bIsInterface(bInIsInterface)
	{
		if (bInIsInterface)
		{
			GetStructMetaData().ParsedInterface = EParsedInterface::ParsedUInterface;
		}
	}

	FUnrealClassDefinitionInfo(FString&& InNameCPP)
		: FUnrealStructDefinitionInfo(MoveTemp(InNameCPP))
	{ }

	virtual FUnrealClassDefinitionInfo* AsClass() override
	{
		return this;
	}

	/**
	 * Return the C++ type name that represents this type (i.e. UClass)
	 */
	virtual const TCHAR* GetSimplifiedTypeClass() const override
	{
		return TEXT("UClass");
	}

	virtual uint32 GetHash(bool bIncludeNoExport = true) const override;

	/**
	 * Perform any post parsing finalization and validation
	 */
	virtual void PostParseFinalize() override;

	/**
	 * Return the Engine instance associated with the compiler instance
	 */
	UClass* GetClass() const
	{
		return static_cast<UClass*>(GetObject());
	}

	/**
	 * Get the archive type
	 */
	ESerializerArchiveType GetArchiveType() const
	{
		return ArchiveType;
	}

	/**
	 * Set the archive type
	 */
	void AddArchiveType(ESerializerArchiveType InArchiveType)
	{
		ArchiveType |= InArchiveType;
	}

	/**
	 * Get the enclosing define
	 */
	const FString& GetEnclosingDefine() const
	{
		return EnclosingDefine;
	}

	/** 
	 * Set the enclosing define
	 */
	void SetEnclosingDefine(FString&& InEnclosingDefine)
	{
		EnclosingDefine = MoveTemp(InEnclosingDefine);
	}

	/**
	 * Return true if this is an interface
	 */
	bool IsInterface() const
	{
		return bIsInterface;
	}

	/** 
	 * Return the CPP name of the base class or blank if there is none.
	 */
	const FString& GetBaseClassNameCPP() const
	{
		return BaseClassNameCPP;
	}

private:
	FString BaseClassNameCPP;
	FString EnclosingDefine;
	ESerializerArchiveType ArchiveType = ESerializerArchiveType::None;
	bool bIsInterface = false;
};
