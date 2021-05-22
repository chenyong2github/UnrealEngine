// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Exceptions.h"
#include "Templates/SharedPointer.h"
#include <atomic>

#include "BaseParser.h"
#include "ParserHelper.h"

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

enum class EEnforceInterfacePrefix
{
	None,
	I,
	U
};

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
	 * Sets the input line in the rare case where the definition is created before fully parsed (sparse delegates)
	 */
	void SetLineNumber(int32 InLineNumber)
	{
		LineNumber = InLineNumber;
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

	/**
	 * Add meta data for the given definition
	 */
	virtual void AddMetaData(TMap<FName, FString>&& InMetaData)
	{
		FUHTException::Throwf(*this, TEXT("Meta data can not be set for a definition of this type."));
	}

	/**
	 * Helper function that checks if the field is a dynamic type (can be constructed post-startup)
	 */
	static bool IsDynamic(const UField* Field);
	static bool IsDynamic(const FField* Field);

	/**
	 * Helper function that checks if the field is a dynamic type
	 */
	virtual bool IsDynamic() const
	{
		return false;
	}

	/**
	 * Helper function that checks if the field is belongs to a dynamic type
	 */
	virtual bool IsOwnedByDynamicType() const
	{
		return false;
	}

	static FString GetNameWithPrefix(const UClass* Class, EEnforceInterfacePrefix EnforceInterfacePrefix = EEnforceInterfacePrefix::None);

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
	FUnrealPropertyDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, int32 InParsePosition, const FPropertyBase& InVarProperty, FString&& InNameCPP)
		: FUnrealTypeDefinitionInfo(InSourceFile, InLineNumber, MoveTemp(InNameCPP))
		, PropertyBase(InVarProperty)
		, ParsePosition(InParsePosition)
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
	 * Perform any post parsing finalization and validation
	 */
	virtual void PostParseFinalize() override;

	/**
	 * Return the Engine instance associated with the compiler instance
	 */
	FProperty* GetProperty() const
	{
		check(Property);
		return Property;
	}

	/**
	 * Sets the engine type
	 */
	void SetProperty(FProperty* InProperty)
	{
		Property = InProperty;
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
	 * Set the unsized flag
	 */
	void SetUnsized(bool bInUnsized)
	{
		bUnsized = bInUnsized;
	}

	/**
	 * Return the allocator type
	 */
	EAllocatorType GetAllocatorType() const
	{
		return AllocatorType;
	}

	/**
	 * Set the allocator type
	 */
	void SetAllocatorType(EAllocatorType InAllocatorType)
	{
		AllocatorType = InAllocatorType;
	}

	/**
	 * Return the token associated with the property parsing
	 */
	FPropertyBase& GetPropertyBase()
	{
		return PropertyBase;
	}
	const FPropertyBase& GetPropertyBase() const
	{
		return PropertyBase;
	}

	/**
	 * Add meta data for the given definition
	 */
	virtual void AddMetaData(TMap<FName, FString>&& InMetaData) override;

	/**
	 * Get the associated key property definition (valid for maps)
	 */
	FUnrealPropertyDefinitionInfo& GetKeyPropDef() const
	{
		check(KeyPropDef);
		return *KeyPropDef;
	}

	/**
	 * Sets the associated key property definition (valid for maps)
	 */
	void SetKeyPropDef(FUnrealPropertyDefinitionInfo& InKeyPropDef)
	{
		check(KeyPropDef == nullptr);
		KeyPropDef = &InKeyPropDef;
	}

	/**
	 * Get the associated value property definition (valid for maps, sets, and dynamic arrays)
	 */
	FUnrealPropertyDefinitionInfo& GetValuePropDef() const
	{
		check(ValuePropDef);
		return *ValuePropDef;
	}

	/**
	 * Sets the associated value property definition (valid for maps, sets, and dynamic arrays)
	 */
	void SetValuePropDef(FUnrealPropertyDefinitionInfo& InValuePropDef)
	{
		check(ValuePropDef == nullptr);
		ValuePropDef = &InValuePropDef;
	}

	/**
	 * Get the parsing position of the property
	 */
	int32 GetParsePosition() const
	{
		return ParsePosition;
	}

	/**
	 * Determines whether this property's type is compatible with another property's type.
	 *
	 * @param	Other							the property to check against this one.
	 *											Given the following example expressions, VarA is Other and VarB is 'this':
	 *												VarA = VarB;
	 *
	 *												function func(type VarB) {}
	 *												func(VarA);
	 *
	 *												static operator==(type VarB_1, type VarB_2) {}
	 *												if ( VarA_1 == VarA_2 ) {}
	 *
	 * @param	bDisallowGeneralization			controls whether it should be considered a match if this property's type is a generalization
	 *											of the other property's type (or vice versa, when dealing with structs
	 * @param	bIgnoreImplementedInterfaces	controls whether two types can be considered a match if one type is an interface implemented
	 *											by the other type.
	 */
	bool MatchesType(const FUnrealPropertyDefinitionInfo& Other, bool bDisallowGeneralization, bool bIgnoreImplementedInterfaces = false) const
	{
		return GetPropertyBase().MatchesType(Other.GetPropertyBase(), bDisallowGeneralization, bIgnoreImplementedInterfaces);
	}

	/**
	 * Return the type package name
	 */
	const FString& GetTypePackageName() const
	{
		return TypePackageName;
	}

	/**
	 * Helper function that checks if the field is a dynamic type
	 */
	virtual bool IsDynamic() const override;

	/**
	 * Helper function that checks if the field is belongs to a dynamic type
	 */
	virtual bool IsOwnedByDynamicType() const override;

private:
	FPropertyBase PropertyBase;
	FString ArrayDimensions;
	FString TypePackageName;
	FUnrealPropertyDefinitionInfo* KeyPropDef = nullptr;
	FUnrealPropertyDefinitionInfo* ValuePropDef = nullptr;
	FProperty* Property = nullptr;
	int32 ParsePosition;
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

	/**
	 * Return the "outer" object that contains the definition for this object
	 */
	FUnrealObjectDefinitionInfo* GetOuter() const
	{
		return Outer;
	}

protected:
	// Used only by packages
	explicit FUnrealObjectDefinitionInfo(FString&& InNameCPP)
		: FUnrealTypeDefinitionInfo(MoveTemp(InNameCPP))
	{ }

	FUnrealObjectDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, FString&& InNameCPP, FUnrealObjectDefinitionInfo& InOuter)
		: FUnrealTypeDefinitionInfo(InSourceFile, InLineNumber, MoveTemp(InNameCPP))
		, Outer(&InOuter)
	{ }

private:
	FUnrealObjectDefinitionInfo* Outer = nullptr;
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

	/** 
	 * Return the type package name
	 */
	const FString& GetTypePackageName() const
	{
		return TypePackageName;
	}

	/**
	 * Add meta data for the given definition
	 */
	virtual void AddMetaData(TMap<FName, FString>&& InMetaData) override;

	/**
	 * Helper function that checks if the field is a dynamic type
	 */
	virtual bool IsDynamic() const override;

	/**
	 * Helper function that checks if the field is belongs to a dynamic type
	 */
	virtual bool IsOwnedByDynamicType() const override;

private:
	FString SingletonName[2];
	FString SingletonNameChopped[2];
	FString ExternDecl[2];
	FString TypePackageName;
};

/**
 * Class that stores information about type definitions derived from UEnum
 */
class FUnrealEnumDefinitionInfo : public FUnrealFieldDefinitionInfo
{
public:
	FUnrealEnumDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, FString&& InNameCPP);

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
	struct FBaseClassInfo
	{
		FString Name;
		FUnrealStructDefinitionInfo* Struct = nullptr;
	};

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
	virtual void AddProperty(FUnrealPropertyDefinitionInfo& PropertyDef);

	/**
	 * Get the collection of properties
	 */
	const TArray<FUnrealPropertyDefinitionInfo*>& GetProperties() const
	{
		return Properties;
	}
	TArray<FUnrealPropertyDefinitionInfo*>& GetProperties()
	{
		return Properties;
	}

	/**
	 * Add a new function to the structure
	 */
	virtual void AddFunction(FUnrealFunctionDefinitionInfo& FunctionDef);

	/**
	 * Get the collection of functions
	 */
	const TArray<FUnrealFunctionDefinitionInfo*>& GetFunctions() const
	{
		return Functions;
	}
	TArray<FUnrealFunctionDefinitionInfo*>& GetFunctions()
	{
		return Functions;
	}

	/**
	 * Return the class meta data information
	 */
	FStructMetaData& GetStructMetaData()
	{
		return StructMetaData;
	}

	/**
	 * Return the super class information
	 */
	const FBaseClassInfo& GetSuperClassInfo() const
	{
		return SuperClassInfo;
	}

	FBaseClassInfo& GetSuperClassInfo()
	{
		return SuperClassInfo;
	}

	const TArray<FBaseClassInfo>& GetBaseClassInfo() const
	{
		return BaseClassInfo;
	}

	TArray<FBaseClassInfo>& GetBaseClassInfo()
	{
		return BaseClassInfo;
	}

	/**
	 * Get the contains delegates flag
	 */
	bool ContainsDelegates() const
	{
		return bContainsDelegates;
	}

	/**
	 * Sets contains delegates flag for this class.
	 */
	void MarkContainsDelegate()
	{
		bContainsDelegates = true;
	}

	/**
	 * Test to see if this struct is a child of another struct
	 */
	bool IsChildOf(FUnrealStructDefinitionInfo& ParentStruct) const
	{
		for (const FUnrealStructDefinitionInfo* Current = this; Current; Current = Current->GetSuperClassInfo().Struct)
		{
			if (Current == &ParentStruct)
			{
				return true;
			}
		}
		return false;
	}

private:
	TSharedPtr<FScope> StructScope;

	/** Properties of the structure */
	TArray<FUnrealPropertyDefinitionInfo*> Properties;

	/** Functions of the structure */
	TArray<FUnrealFunctionDefinitionInfo*> Functions;

	FStructMetaData StructMetaData;
	FBaseClassInfo SuperClassInfo;
	TArray<FBaseClassInfo> BaseClassInfo;

	/** whether this struct declares delegate functions or properties */
	bool bContainsDelegates = false;
};

/**
 * Class that stores information about type definitions derived from UScriptStruct
 */
class FUnrealScriptStructDefinitionInfo : public FUnrealStructDefinitionInfo
{
public:
	FUnrealScriptStructDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, FString&& InNameCPP);

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
};

/**
 * Class that stores information about type definitions derrived from UFunction
 */
class FUnrealFunctionDefinitionInfo : public FUnrealStructDefinitionInfo
{
public:
	FUnrealFunctionDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, FString&& InNameCPP, FUnrealObjectDefinitionInfo& InOuter, FFuncInfo&& InFuncInfo)
		: FUnrealStructDefinitionInfo(InSourceFile, InLineNumber, MoveTemp(InNameCPP), InOuter)
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

	/**
	 * Adds a new function property to be tracked.  Determines whether the property is a
	 * function parameter, local property, or return value, and adds it to the appropriate
	 * list
	 */
	virtual void AddProperty(FUnrealPropertyDefinitionInfo& PropertyDef) override;

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
{
public:
	FUnrealClassDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, FString&& InNameCPP, bool bInIsInterface);

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

	FUnrealClassDefinitionInfo* GetSuperClass() const
	{
		if (FUnrealStructDefinitionInfo* SuperClass = GetSuperClassInfo().Struct)
		{
			return &SuperClass->AsClassChecked();
		}
		return nullptr;
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
	 * Used to safely check whether the passed in flag is set.
	 *
	 * @param	FlagsToCheck		Class flag(s) to check for
	 * @return	true if the passed in flag is set, false otherwise
	 *			(including no flag passed in, unless the FlagsToCheck is CLASS_AllFlags)
	 */
	FORCEINLINE bool HasAnyClassFlags(EClassFlags FlagsToCheck) const
	{
		return EnumHasAnyFlags(ClassFlags, FlagsToCheck) != 0 || GetClass()->HasAnyClassFlags(FlagsToCheck);
	}

	/**
	 * Used to safely check whether all of the passed in flags are set.
	 *
	 * @param FlagsToCheck	Class flags to check for
	 * @return true if all of the passed in flags are set (including no flags passed in), false otherwise
	 */
	FORCEINLINE bool HasAllClassFlags(EClassFlags FlagsToCheck) const
	{
		return EnumHasAllFlags(ClassFlags, FlagsToCheck) || GetClass()->HasAllClassFlags(FlagsToCheck);
	}

	/**
	 * Gets the class flags.
	 *
	 * @return	The class flags.
	 */
	FORCEINLINE EClassFlags GetClassFlags() const
	{
		return ClassFlags;
	}

	/**
	 * Used to safely check whether the passed in flag is set in the whole hierarchy
	 *
	 * @param	FlagsToCheck		Class flag(s) to check for
	 * @return	true if the passed in flag is set, false otherwise
	 *			(including no flag passed in, unless the FlagsToCheck is CLASS_AllFlags)
	 */
	bool HierarchyHasAnyClassFlags(EClassFlags FlagsToCheck) const
	{
		for (const FUnrealClassDefinitionInfo* ClassDef = this; ClassDef; ClassDef = ClassDef->GetSuperClass())
		{
			if (ClassDef->HasAnyClassFlags(FlagsToCheck))
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Used to safely check whether all of the passed in flags are set.
	 *
	 * @param FlagsToCheck	Class flags to check for
	 * @return true if all of the passed in flags are set (including no flags passed in), false otherwise
	 */
	bool HierarchyHasAllClassFlags(EClassFlags FlagsToCheck) const
	{
		for (const FUnrealClassDefinitionInfo* ClassDef = this; ClassDef; ClassDef = ClassDef->GetSuperClass())
		{
			if (ClassDef->HasAllClassFlags(FlagsToCheck))
			{
				return true;
			}
		}
		return false;
	}

	/**
	* Parse Class's properties to generate its declaration data.
	*
	* @param	InClassSpecifiers Class properties collected from its UCLASS macro
	* @param	InRequiredAPIMacroIfPresent *_API macro if present (empty otherwise)
	* @param	OutClassData Parsed class meta data
	*/
	void ParseClassProperties(TArray<FPropertySpecifier>&& InClassSpecifiers, const FString& InRequiredAPIMacroIfPresent);

	/**
	* Merges all category properties with the class which at this point only has its parent propagated categories
	*/
	void MergeClassCategories();

	/**
	* Merges all class flags and validates them
	*
	* @param	DeclaredClassName Name this class was declared with (for validation)
	* @param	PreviousClassFlags Class flags before resetting the class (for validation)
	*/
	void MergeAndValidateClassFlags(const FString& DeclaredClassName, uint32 PreviousClassFlags);

	/**
	 * Add the category meta data
	 */
	void MergeCategoryMetaData(TMap<FName, FString>& InMetaData) const;


	void GetSparseClassDataTypes(TArray<FString>& OutSparseClassDataTypes) const;

public:
	EClassFlags ClassFlags = CLASS_None;
	FString ClassWithin;
	FString ConfigName;
	TMap<FName, FString> MetaData;

private:
	/** Merges all 'show' categories */
	void MergeShowCategories();
	/** Sets and validates 'within' property */
	void SetAndValidateWithinClass();
	/** Sets and validates 'ConfigName' property */
	void SetAndValidateConfigName();

	void GetHideCategories(TArray<FString>& OutHideCategories) const;
	void GetShowCategories(TArray<FString>& OutShowCategories) const;

	FString EnclosingDefine;
	ESerializerArchiveType ArchiveType = ESerializerArchiveType::None;
	TArray<FString> ShowCategories;
	TArray<FString> ShowFunctions;
	TArray<FString> DontAutoCollapseCategories;
	TArray<FString> HideCategories;
	TArray<FString> ShowSubCatgories;
	TArray<FString> HideFunctions;
	TArray<FString> AutoExpandCategories;
	TArray<FString> AutoCollapseCategories;
	TArray<FString> DependsOn;
	TArray<FString> ClassGroupNames;
	TArray<FString> SparseClassDataTypes;
	bool bIsInterface = false;
	bool bWantsToBePlaceable = false;
};

template <typename To>
struct FUHTCastImplTo
{};

template <>
struct FUHTCastImplTo<FUnrealTypeDefinitionInfo>
{
	template <typename From>
	FUnrealTypeDefinitionInfo* CastImpl(From& Src)
	{
		return &Src;
	}

	template <>
	FUnrealTypeDefinitionInfo* CastImpl(FUnrealTypeDefinitionInfo& Src)
	{
		return &Src;
	}
};

#define UHT_CAST_IMPL(TypeName, RoutineName) \
	template <> \
	struct FUHTCastImplTo<TypeName> \
	{ \
		template <typename From> \
		TypeName* CastImpl(From& Src) \
		{ \
			return Src.RoutineName(); \
		} \
		template <> \
		TypeName* CastImpl(TypeName& Src) \
		{ \
			return &Src; \
		} \
	};

UHT_CAST_IMPL(FUnrealPropertyDefinitionInfo, AsProperty);
UHT_CAST_IMPL(FUnrealObjectDefinitionInfo, AsObject);
UHT_CAST_IMPL(FUnrealPackageDefinitionInfo, AsPackage);
UHT_CAST_IMPL(FUnrealFieldDefinitionInfo, AsField);
UHT_CAST_IMPL(FUnrealEnumDefinitionInfo, AsEnum);
UHT_CAST_IMPL(FUnrealStructDefinitionInfo, AsStruct);
UHT_CAST_IMPL(FUnrealScriptStructDefinitionInfo, AsScriptStruct);
UHT_CAST_IMPL(FUnrealClassDefinitionInfo, AsClass);
UHT_CAST_IMPL(FUnrealFunctionDefinitionInfo, AsFunction);

#undef UHT_CAST_IMPL

template <typename To, typename From>
To* UHTCast(TSharedRef<From>* Src)
{
	return Src ? FUHTCastImplTo<To>().template CastImpl<From>(**Src) : nullptr;
}

template <typename To, typename From>
To* UHTCast(TSharedRef<From>& Src)
{
	return FUHTCastImplTo<To>().template CastImpl<From>(*Src);
}

template <typename To, typename From>
To* UHTCast(From* Src)
{
	return Src ? FUHTCastImplTo<To>().template CastImpl<From>(*Src) : nullptr;
}

template <typename To, typename From>
const To* UHTCast(const From* Src)
{
	return Src ? FUHTCastImplTo<To>().template CastImpl<From>(const_cast<From&>(*Src)) : nullptr;
}

template <typename To, typename From>
To* UHTCast(From& Src)
{
	return FUHTCastImplTo<To>().template CastImpl<From>(Src);
}

template <typename To, typename From>
const To* UHTCast(const From& Src)
{
	return FUHTCastImplTo<To>().template CastImpl<From>(const_cast<From&>(Src));
}

template <typename To, typename From>
To& UHTCastChecked(TSharedRef<From>* Src)
{
	To* Dst = UHTCast<To, From>(Src);
	check(Dst);
	return *Dst;
}

template <typename To, typename From>
To& UHTCastChecked(TSharedRef<From>& Src)
{
	To* Dst = UHTCast<To, From>(Src);
	check(Dst);
	return *Dst;
}

template <typename To, typename From>
To& UHTCastChecked(From* Src)
{
	To* Dst = UHTCast<To, From>(Src);
	check(Dst);
	return *Dst;
}

template <typename To, typename From>
const To& UHTCastChecked(const From* Src)
{
	const To* Dst = UHTCast<To, From>(Src);
	check(Dst);
	return *Dst;
}

template <typename To, typename From>
To& UHTCastChecked(From& Src)
{
	To* Dst = UHTCast<To, From>(Src);
	check(Dst);
	return *Dst;
}

template <typename To, typename From>
const To& UHTCastChecked(const From& Src)
{
	const To* Dst = UHTCast<To, From>(Src);
	check(Dst);
	return *Dst;
}

template <class T>
const TArray<T*>& GetFieldsFromDef(const FUnrealStructDefinitionInfo& InStructDef);

template <>
inline const TArray<FUnrealPropertyDefinitionInfo*>& GetFieldsFromDef(const FUnrealStructDefinitionInfo& InStructDef)
{
	return InStructDef.GetProperties();
}

template <>
inline const TArray<FUnrealFunctionDefinitionInfo*>& GetFieldsFromDef(const FUnrealStructDefinitionInfo& InStructDef)
{
	return InStructDef.GetFunctions();
}

//
// For iterating through a linked list of fields.
//
template <class T>
class TUHTFieldIterator
{
private:
	/** The object being searched for the specified field */
	const FUnrealStructDefinitionInfo* StructDef;
	/** The current location in the list of fields being iterated */
	T* const* Field;
	T* const* End;
	/** Whether to include the super class or not */
	const bool bIncludeSuper;

public:
	TUHTFieldIterator(const FUnrealStructDefinitionInfo* InStructDef, EFieldIteratorFlags::SuperClassFlags InSuperClassFlags = EFieldIteratorFlags::IncludeSuper)
		: StructDef(InStructDef)
		, Field(UpdateRange(InStructDef))
		, bIncludeSuper(InSuperClassFlags == EFieldIteratorFlags::IncludeSuper)
	{
		IterateToNext();
	}

	TUHTFieldIterator(const TUHTFieldIterator& rhs) = default;

	/** conversion to "bool" returning true if the iterator is valid. */
	FORCEINLINE explicit operator bool() const
	{
		return Field != NULL;
	}
	/** inverse of the "bool" operator */
	FORCEINLINE bool operator !() const
	{
		return !(bool)*this;
	}

	inline friend bool operator==(const TUHTFieldIterator<T>& Lhs, const TUHTFieldIterator<T>& Rhs) { return Lhs.Field == Rhs.Field; }
	inline friend bool operator!=(const TUHTFieldIterator<T>& Lhs, const TUHTFieldIterator<T>& Rhs) { return Lhs.Field != Rhs.Field; }

	inline void operator++()
	{
		checkSlow(Field != End);
		++Field;
		IterateToNext();
	}
	inline T* operator*()
	{
		checkSlow(Field != End);
		return *Field;
	}
	inline const T* operator*() const
	{
		checkSlow(Field != End);
		return *Field;
	}
	inline T* operator->()
	{
		checkSlow(Field != End);
		return *Field;
	}
	inline const FUnrealStructDefinitionInfo* GetStructDef()
	{
		return StructDef;
	}
protected:
	inline T* const* UpdateRange(const FUnrealStructDefinitionInfo* InStructDef)
	{
		if (InStructDef)
		{
			auto& Array = GetFieldsFromDef<T>(*InStructDef);
			T* const* Out = Array.GetData();
			End = Out + Array.Num();
			return Out;
		}
		else
		{
			End = nullptr;
			return nullptr;
		}
	}

	inline void IterateToNext()
	{
		T* const* CurrentField = Field;
		const FUnrealStructDefinitionInfo* CurrentStructDef = StructDef;

		while (CurrentStructDef)
		{
			if (CurrentField != End)
			{
				StructDef = CurrentStructDef;
				Field = CurrentField;
				return;
			}

			if (bIncludeSuper)
			{
				CurrentStructDef = CurrentStructDef->GetSuperClassInfo().Struct;
				if (CurrentStructDef)
				{
					CurrentField = UpdateRange(CurrentStructDef);
					continue;
				}
			}

			break;
		}

		StructDef = CurrentStructDef;
		Field = nullptr;
	}
};

template <typename T>
struct TUHTFieldRange
{
	TUHTFieldRange(const FUnrealStructDefinitionInfo& InStruct, EFieldIteratorFlags::SuperClassFlags InSuperClassFlags = EFieldIteratorFlags::IncludeSuper)
		: Begin(&InStruct, InSuperClassFlags)
	{
	}

	friend TUHTFieldIterator<T> begin(const TUHTFieldRange& Range) { return Range.Begin; }
	friend TUHTFieldIterator<T> end(const TUHTFieldRange& Range) { return TUHTFieldIterator<T>(nullptr); }

	TUHTFieldIterator<T> Begin;
};

