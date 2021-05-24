// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Exceptions.h"
#include "Templates/SharedPointer.h"
#include <atomic>

#include "BaseParser.h"
#include "ParserHelper.h"

// Forward declarations.
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

	/**
	 * Return the "outer" object that contains the definition for this object
	 */
	FUnrealTypeDefinitionInfo* GetOuter() const
	{
		return Outer;
	}

protected:
	explicit FUnrealTypeDefinitionInfo(FString&& InNameCPP)
		: NameCPP(MoveTemp(InNameCPP))
	{ }

	FUnrealTypeDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, FString&& InNameCPP, FUnrealTypeDefinitionInfo& InOuter)
		: NameCPP(MoveTemp(InNameCPP))
		, Outer(&InOuter)
		, SourceFile(&InSourceFile)
		, LineNumber(InLineNumber)
	{ }

private:
	FString NameCPP;
	FUnrealTypeDefinitionInfo* Outer = nullptr;
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
	FUnrealPropertyDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, int32 InParsePosition, const FPropertyBase& InVarProperty, FString&& InNameCPP, FUnrealTypeDefinitionInfo& InOuter)
		: FUnrealTypeDefinitionInfo(InSourceFile, InLineNumber, MoveTemp(InNameCPP), InOuter)
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
		check(Property == nullptr || Property == InProperty);
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
	 * Returns the name of this object (with no path information)
	 */
	FString GetName() const
	{
		return GetObject()->GetName();
	}

	/** Returns the logical name of this object */
	FName GetFName() const
	{
		return GetObject()->GetFName();
	}

	/**
	 * Returns the fully qualified pathname for this object, in the format:
	 * 'Outermost.[Outer:]Name'
	 *
	 * @param	StopOuter	if specified, indicates that the output string should be relative to this object.  if StopOuter
	 *						does not exist in this object's Outer chain, the result would be the same as passing NULL.
	 *
	 * @note	safe to call on NULL object pointers!
	 */
	FString GetPathName(const FUnrealObjectDefinitionInfo* StopOuter = nullptr) const
	{
		return GetObject()->GetPathName(StopOuter ? StopOuter->GetObject() : nullptr);
	}

	/**
	 * Return the package associated with this object
	 */
	FUnrealPackageDefinitionInfo& GetPackageDef() const;

	/**
	 * Return the "outer" object that contains the definition for this object
	 */
	FUnrealObjectDefinitionInfo* GetOuter() const
	{
		return static_cast<FUnrealObjectDefinitionInfo*>(FUnrealTypeDefinitionInfo::GetOuter());
	}

	/**
	 * Helper methods to remove dependencies on engine types
	 */
	bool IsADelegateFunction() const
	{
		return GetObject()->IsA<UDelegateFunction>();
	}

protected:
	// Used only by packages
	explicit FUnrealObjectDefinitionInfo(FString&& InNameCPP)
		: FUnrealTypeDefinitionInfo(MoveTemp(InNameCPP))
	{ }

	FUnrealObjectDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, FString&& InNameCPP, FUnrealObjectDefinitionInfo& InOuter)
		: FUnrealTypeDefinitionInfo(InSourceFile, InLineNumber, MoveTemp(InNameCPP), InOuter)
	{ }

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
	 * Determines if the property has any metadata associated with the key
	 * 
	 * @param Key The key to lookup in the metadata
	 * @return true if there is a (possibly blank) value associated with this key
	 */
	bool HasMetaData(const TCHAR* Key) const { return FindMetaData(Key) != nullptr; }
	bool HasMetaData(const FName& Key) const { return FindMetaData(Key) != nullptr; }

	/**
	 * Find the metadata value associated with the key
	 *
	 * @param Key The key to lookup in the metadata
	 * @return The value associated with the key if exists, null otherwise
	 */
	const FString* FindMetaData(const TCHAR* Key) const
	{
		return GetField()->FindMetaData(Key);
	}
	const FString* FindMetaData(const FName& Key) const
	{
		return GetField()->FindMetaData(Key);
	}

	/**
	 * Find the metadata value associated with the key
	 * 
	 * @param Key The key to lookup in the metadata
	 * @return The value associated with the key
	 */
	const FString& GetMetaData(const TCHAR* Key) const
	{
		return GetField()->GetMetaData(Key);
	}
	const FString& GetMetaData(const FName& Key) const
	{
		return GetField()->GetMetaData(Key);
	}

	/**
	 * Sets the metadata value associated with the key
	 * 
	 * @param Key The key to lookup in the metadata
	 * @return The value associated with the key
	 */
	void SetMetaData(const TCHAR* Key, const TCHAR* InValue)
	{
		GetField()->SetMetaData(Key, InValue);
	}
	void SetMetaData(const FName& Key, const TCHAR* InValue)
	{
		GetField()->SetMetaData(Key, InValue);
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

	/**
	 * Return the owning class
	 */
	FUnrealClassDefinitionInfo* GetOwnerClass() const;

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

	bool HasAnyEnumFlags(EEnumFlags InFlags) const
	{
		return GetEnum()->HasAnyEnumFlags(InFlags);
	}

	/**
	 * Tests if the enum contains a MAX value
	 *
	 * @return	true if the enum contains a MAX enum, false otherwise.
	 */
	bool ContainsExistingMax() const
	{
		return GetEnum()->ContainsExistingMax();
	}

	/**
	 * @return	 The number of enum names.
	 */
	int32 NumEnums() const
	{
		return GetEnum()->NumEnums();
	}

	/** Gets max value of Enum. Defaults to zero if there are no entries. */
	int64 GetMaxEnumValue() const
	{
		return GetEnum()->GetMaxEnumValue();
	}

	/** Gets enum value by index in Names array. Asserts on invalid index */
	FORCEINLINE int64 GetValueByIndex(int32 Index) const
	{
		return GetEnum()->GetValueByIndex(Index);
	}

	/** Gets enum name by index in Names array. Returns NAME_None if Index is not valid. */
	FName GetNameByIndex(int32 Index) const
	{
		return GetEnum()->GetNameByIndex(Index);
	}

	/** Gets index of name in enum, returns INDEX_NONE and optionally errors when name is not found. This is faster than ByNameString if the FName is exact, but will fall back if needed */
	int32 GetIndexByName(FName InName, EGetByNameFlags Flags = EGetByNameFlags::None) const
	{
		return GetEnum()->GetIndexByName(InName, Flags);
	}

	/** Checks if enum has entry with given value. Includes autogenerated _MAX entry. */
	bool IsValidEnumValue(int64 InValue) const
	{
		return GetEnum()->IsValidEnumValue(InValue);
	}

	/**
	 * Returns the type of enum: whether it's a regular enum, namespaced enum or C++11 enum class.
	 *
	 * @return The enum type.
	 */
	UEnum::ECppForm GetCppForm() const
	{
		return GetEnum()->GetCppForm();
	}

	/**
	 * Returns the type of the enum
	 */
	const FString& GetCppType() const
	{
		return GetEnum()->CppType;
	}

	/**
	 * Sets the enum type
	 */
	void SetCppType(FString&& CppType)
	{
		GetEnum()->CppType = MoveTemp(CppType);
	}

	/**
	 * Find the longest common prefix of all items in the enumeration.
	 *
	 * @return	the longest common prefix between all items in the enum.  If a common prefix
	 *			cannot be found, returns the full name of the enum.
	 */
	FString GenerateEnumPrefix() const
	{
		return GetEnum()->GenerateEnumPrefix();
	}

	/**
	 * Sets the array of enums.
	 *
	 * @param InNames List of enum names.
	 * @param InCppForm The form of enum.
	 * @param bAddMaxKeyIfMissing Should a default Max item be added.
	 * @return	true unless the MAX enum already exists and isn't the last enum.
	 */
	bool SetEnums(TArray<TPair<FName, int64>>& InNames, UEnum::ECppForm InCppForm, EEnumFlags InFlags = EEnumFlags::None, bool bAddMaxKeyIfMissing = true)
	{
		return GetEnum()->SetEnums(InNames, InCppForm, InFlags, bAddMaxKeyIfMissing);
	}

	/**
	 * Wrapper method for easily determining whether this enum has metadata associated with it.
	 *
	 * @param	Key			the metadata tag to check for
	 * @param	NameIndex	if specified, will search for metadata linked to a specified value in this enum; otherwise, searches for metadata for the enum itself
	 *
	 * @return true if the specified key exists in the list of metadata for this enum, even if the value of that key is empty
	 */
	bool HasMetaData(const TCHAR* Key, int32 NameIndex = INDEX_NONE) const
	{
		return GetEnum()->HasMetaData(Key, NameIndex);
	}

	/**
	 * Return the metadata value associated with the specified key.
	 *
	 * @param	Key			the metadata tag to find the value for
	 * @param	NameIndex	if specified, will search the metadata linked for that enum value; otherwise, searches the metadata for the enum itself
	 * @param	bAllowRemap	if true, the returned value may be remapped from a .ini if the value starts with ini: Pass false when you need the exact string, including any ini:
	 *
	 * @return	the value for the key specified, or an empty string if the key wasn't found or had no value.
	 */
	FString GetMetaData(const TCHAR* Key, int32 NameIndex = INDEX_NONE, bool bAllowRemap = true) const
	{
		return GetEnum()->GetMetaData(Key, NameIndex, bAllowRemap);
	}

	/**
	 * Set the metadata value associated with the specified key.
	 *
	 * @param	Key			the metadata tag to find the value for
	 * @param	NameIndex	if specified, will search the metadata linked for that enum value; otherwise, searches the metadata for the enum itself
	 * @param	InValue		Value of the metadata for the key
	 *
	 */
	void SetMetaData(const TCHAR* Key, const TCHAR* InValue, int32 NameIndex = INDEX_NONE) const
	{
		GetEnum()->SetMetaData(Key, InValue, NameIndex);
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
	struct FBaseStructInfo
	{
		FString Name;
		FUnrealStructDefinitionInfo* Struct = nullptr;
	};

	struct FDefinitionRange
	{
		void Validate()
		{
			if (End <= Start)
			{
				FError::Throwf(TEXT("The class definition range is invalid. Most probably caused by previous parsing error."));
			}
		}

		const TCHAR* Start = nullptr;
		const TCHAR* End = nullptr;
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
	 * Return the CPP prefix 
	 */
	const TCHAR* GetPrefixCPP() const
	{
		return GetStruct()->GetPrefixCPP();
	}

	/**
	 * Returns the name used for declaring the passed in struct in C++
	 * 
	 * NOTE: This does not match the CPP name parsed from the header file.
	 *
	 * @param	bForceInterface If true, force an 'I' prefix
	 * @return	Name used for C++ declaration
	 */
	FString GetAlternateNameCPP(bool bForceInterface = false)
	{
		return FString::Printf(TEXT("%s%s"), (bForceInterface ? TEXT("I") : GetPrefixCPP()), *GetName());
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
	 * Return the super struct
	 */
	FUnrealStructDefinitionInfo* GetSuperStruct() const
	{
		return SuperStructInfo.Struct;
	}

	/**
	 * Return the super struct information
	 */
	const FBaseStructInfo& GetSuperStructInfo() const
	{
		return SuperStructInfo;
	}

	FBaseStructInfo& GetSuperStructInfo()
	{
		return SuperStructInfo;
	}

	/**
	 * Return the base struct information
	 */
	const TArray<FBaseStructInfo>& GetBaseStructInfo() const
	{
		return BaseStructInfo;
	}

	TArray<FBaseStructInfo>& GetBaseStructInfo()
	{
		return BaseStructInfo;
	}

	/** Try and find boolean metadata with the given key. If not found on this class, work up hierarchy looking for it. */
	bool GetBoolMetaDataHierarchical(const FName& Key) const
	{
		return GetStruct()->GetBoolMetaDataHierarchical(Key);
	}

	/** Try and find string metadata with the given key. If not found on this class, work up hierarchy looking for it. */
	bool GetStringMetaDataHierarchical(const FName& Key, FString* OutValue = nullptr) const
	{
		return GetStruct()->GetStringMetaDataHierarchical(Key, OutValue);
	}

	/**
	 * Determines if the struct or any of its super structs has any metadata associated with the provided key
	 *
	 * @param Key The key to lookup in the metadata
	 * @return pointer to the UStruct that has associated metadata, nullptr if Key is not associated with any UStruct in the hierarchy
	 */
	const UStruct* HasMetaDataHierarchical(const FName& Key) const
	{
		return GetStruct()->HasMetaDataHierarchical(Key);
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
		for (const FUnrealStructDefinitionInfo* Current = this; Current; Current = Current->GetSuperStructInfo().Struct)
		{
			if (Current == &ParentStruct)
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Get the generated code version
	 */
	EGeneratedCodeVersion GetGeneratedCodeVersion() const
	{
		return GeneratedCodeVersion;
	}

	/**
	 * Set the generated code version
	 */
	void SetGeneratedCodeVersion(EGeneratedCodeVersion InGeneratedCodeVersion)
	{
		GeneratedCodeVersion = InGeneratedCodeVersion;
	}

	/**
	 * Get if we have a generated body
	 */
	bool HasGeneratedBody() const
	{
		return bHasGeneratedBody;
	}

	/**
	 * Mark that we have a generated body
	 */
	void MarkGeneratedBody()
	{
		bHasGeneratedBody = true;
	}

	/**
	 * Return the definition range of the structure
	 */
	FDefinitionRange& GetDefinitionRange()
	{
		return DefinitionRange;
	}

	/**
	 * Get the RigVM information
	 */
	FRigVMStructInfo& GetRigVMInfo()
	{
		return RigVMInfo;
	}

private:
	TSharedPtr<FScope> StructScope;

	/** Properties of the structure */
	TArray<FUnrealPropertyDefinitionInfo*> Properties;

	/** Functions of the structure */
	TArray<FUnrealFunctionDefinitionInfo*> Functions;

	FStructMetaData StructMetaData;
	FBaseStructInfo SuperStructInfo;
	TArray<FBaseStructInfo> BaseStructInfo;

	FDefinitionRange DefinitionRange;

	FRigVMStructInfo RigVMInfo;

	EGeneratedCodeVersion GeneratedCodeVersion = FUHTConfig::Get().DefaultGeneratedCodeVersion;

	/** whether this struct declares delegate functions or properties */
	bool bContainsDelegates = false;

	/** If true, this struct contains the generated body macro */
	bool bHasGeneratedBody = false;
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

	/**
	 * Return the struct flags
	 */
	FORCEINLINE EStructFlags GetStructFlags() const
	{
		return GetScriptStruct()->StructFlags;
	}

	/**
	 * Used to safely check whether the passed in flag is set.
	 *
	 * @param	FlagToCheck		Class flag to check for
	 *
	 * @return	true if the passed in flag is set, false otherwise
	 *			(including no flag passed in, unless the FlagsToCheck is CLASS_AllFlags)
	 */
	FORCEINLINE bool HasAnyStructFlags(EStructFlags FlagsToCheck) const
	{
		return EnumHasAnyFlags(GetScriptStruct()->StructFlags, FlagsToCheck);
	}

	/**
	 * Used to safely check whether all of the passed in flags are set.
	 *
	 * @param FlagsToCheck	Function flags to check for
	 * @return true if all of the passed in flags are set (including no flags passed in), false otherwise
	 */
	FORCEINLINE bool HasAllFunctionFlags(EStructFlags FlagsToCheck) const
	{
		return EnumHasAllFlags(GetScriptStruct()->StructFlags, FlagsToCheck);
	}

	/**
	 * Used to safely check whether specific of the passed in flags are set.
	 *
	 * @param FlagsToCheck	Function flags to check for
	 * @param ExpectedFlags The flags from the flags to check that should be set
	 * @return true if specific of the passed in flags are set (including no flags passed in), false otherwise
	 */
	FORCEINLINE bool HasSpecificFunctionFlags(EStructFlags FlagsToCheck, EStructFlags ExpectedFlags) const
	{
		EStructFlags Flags = GetScriptStruct()->StructFlags;
		return (Flags & FlagsToCheck) == ExpectedFlags;
	}

	/**
	 * If it is native, it is assumed to have defaults because it has a constructor
	 * @return true if this struct has defaults
	 */
	FORCEINLINE bool HasDefaults() const
	{
		return !!GetScriptStruct()->GetCppStructOps();
	}


	int32 GetMacroDeclaredLineNumber() const
	{
		return MacroDeclaredLineNumber;
	}

	void SetMacroDeclaredLineNumber(int32 InMacroDeclaredLineNumber)
	{
		MacroDeclaredLineNumber = InMacroDeclaredLineNumber;
	}

private:
	int32 MacroDeclaredLineNumber = INDEX_NONE;
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
	{ 
	}

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

	/**
	 * Return the function flags
	 */
	FORCEINLINE EFunctionFlags GetFunctionFlags() const
	{
		return GetFunction()->FunctionFlags;
	}

	/**
	 * Used to safely check whether the passed in flag is set.
	 *
	 * @param	FlagToCheck		Class flag to check for
	 *
	 * @return	true if the passed in flag is set, false otherwise
	 *			(including no flag passed in, unless the FlagsToCheck is CLASS_AllFlags)
	 */
	FORCEINLINE bool HasAnyFunctionFlags(EFunctionFlags FlagsToCheck) const
	{
		return GetFunction()->HasAnyFunctionFlags(FlagsToCheck);
	}

	/**
	 * Used to safely check whether all of the passed in flags are set.
	 *
	 * @param FlagsToCheck	Function flags to check for
	 * @return true if all of the passed in flags are set (including no flags passed in), false otherwise
	 */
	FORCEINLINE bool HasAllFunctionFlags(EFunctionFlags FlagsToCheck) const
	{
		return GetFunction()->HasAllFunctionFlags(FlagsToCheck);
	}

	/**
	 * Used to safely check whether specific of the passed in flags are set.
	 *
	 * @param FlagsToCheck	Function flags to check for
	 * @param ExpectedFlags The flags from the flags to check that should be set
	 * @return true if specific of the passed in flags are set (including no flags passed in), false otherwise
	 */
	FORCEINLINE bool HasSpecificFunctionFlags(EFunctionFlags FlagsToCheck, EFunctionFlags ExpectedFlags) const
	{
		EFunctionFlags Flags = GetFunction()->FunctionFlags;
		return (Flags & FlagsToCheck) == ExpectedFlags;
	}

	/**
	 * Return the outer in Class form.  
	 */
	FUnrealClassDefinitionInfo* GetOuterClass() const;

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

	/**
	 * Return the super function
	 */
	FUnrealFunctionDefinitionInfo* GetSuperFunction() const;

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

	/**
	 * Attempts to find a class definition based on the given name
	 */
	static FUnrealClassDefinitionInfo* FindClass(const TCHAR* ClassName);

	/**
	 * Attempts to find a script class based on the given name. Will attempt to strip
	 * the prefix of the given name while searching. Throws an exception with the script error
	 * if the class could not be found.
	 *
	 * @param   InClassName  Name w/ Unreal prefix to use when searching for a class
	 * @return               The found class.
	 */
	static FUnrealClassDefinitionInfo* FindScriptClassOrThrow(const FString& InClassName);

	/**
	 * Attempts to find a script class based on the given name. Will attempt to strip
	 * the prefix of the given name while searching. Optionally returns script errors when appropriate.
	 *
	 * @param   InClassName  Name w/ Unreal prefix to use when searching for a class
	 * @param   OutErrorMsg  Error message (if any) giving the caller flexibility in how they present an error
	 * @return               The found class, or NULL if the class was not found.
	 */
	static FUnrealClassDefinitionInfo* FindScriptClass(const FString& InClassName, FString* OutErrorMsg = nullptr);

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
	 * Set the Engine instance associated with the compiler instance
	 */
	virtual void SetObject(UObject* InObject) override
	{
		UClass* Class = static_cast<UClass*>(InObject);
		InitialEngineClassFlags = Class->ClassFlags;
		Class->ClassFlags |= ParsedClassFlags;
		FUnrealStructDefinitionInfo::SetObject(InObject);
	}

	FUnrealClassDefinitionInfo* GetSuperClass() const
	{
		if (FUnrealStructDefinitionInfo* SuperClass = GetSuperStructInfo().Struct)
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
	 * Return the flags that were parsed as part of the pre-parse phase
	 */
	EClassFlags GetClassFlags() const
	{
		return GetClass()->ClassFlags;
	}

	/**
	 * Sets the given flags
	 */
	void SetClassFlags(EClassFlags FlagsToSet)
	{
		GetClass()->ClassFlags |= FlagsToSet;
	}

	/**
	 * Clears the given flags
	 */
	void ClearClassFlags(EClassFlags FlagsToClear)
	{
		GetClass()->ClassFlags &= ~FlagsToClear;
	}

	/**
	 * Used to safely check whether the passed in flag is set.
	 *
	 * @param	FlagsToCheck		Class flag(s) to check for
	 * @return	true if the passed in flag is set, false otherwise
	 *			(including no flag passed in, unless the FlagsToCheck is CLASS_AllFlags)
	 */
	bool HasAnyClassFlags(EClassFlags FlagsToCheck) const
	{

		return EnumHasAnyFlags(ParsedClassFlags, FlagsToCheck) || GetClass()->HasAnyClassFlags(FlagsToCheck);
	}

	/**
	 * Used to safely check whether all of the passed in flags are set.
	 *
	 * @param FlagsToCheck	Class flags to check for
	 * @return true if all of the passed in flags are set (including no flags passed in), false otherwise
	 */
	bool HasAllClassFlags(EClassFlags FlagsToCheck) const
	{
		return EnumHasAllFlags(ParsedClassFlags, FlagsToCheck) || GetClass()->HasAllClassFlags(FlagsToCheck);
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
	 * Return the flags that were parsed as part of the pre-parse phase
	 */
	EClassFlags GetParsedClassFlags() const
	{
		return ParsedClassFlags;
	}

	/**
	 * Return the initial flags from the class. These will be set for class definitions created directly from
	 * existing engine types.
	 */
	EClassFlags GetInitialEngineClassFlags() const
	{
		return InitialEngineClassFlags;
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
	void MergeAndValidateClassFlags(const FString& DeclaredClassName, EClassFlags PreviousClassFlags);

	/**
	 * Add the category meta data
	 */
	void MergeCategoryMetaData(TMap<FName, FString>& InMetaData) const;

	void GetSparseClassDataTypes(TArray<FString>& OutSparseClassDataTypes) const;

	/**
	 * Get the class's class within setting
	 */
	FUnrealClassDefinitionInfo* GetClassWithin() const
	{
		return ClassWithin;
	}

	/**
	 * Set the class's class within setting
	 */
	void SetClassWithin(FUnrealClassDefinitionInfo* InClassWithin)
	{
		ClassWithin = InClassWithin;
	}

	/**
	 * Get the class config name
	 */
	FName GetClassConfigName() const
	{
		return GetClass()->ClassConfigName;
	}

	/**
	 * Set the class config name
	 */
	void SetClassConfigName(FName InClassConfigName)
	{
		GetClass()->ClassConfigName = InClassConfigName;
	}

	FString GetNameWithPrefix(EEnforceInterfacePrefix EnforceInterfacePrefix = EEnforceInterfacePrefix::None) const;

public:
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
	FString EnclosingDefine;
	FString ClassWithinStr;
	FString ConfigName;
	EClassFlags ParsedClassFlags = CLASS_None;
	EClassFlags InitialEngineClassFlags = CLASS_None;
	FUnrealClassDefinitionInfo* ClassWithin = nullptr;
	ESerializerArchiveType ArchiveType = ESerializerArchiveType::None;
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
				CurrentStructDef = CurrentStructDef->GetSuperStructInfo().Struct;
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

inline FUnrealClassDefinitionInfo* FUnrealFunctionDefinitionInfo::GetOuterClass() const
{
	return UHTCast<FUnrealClassDefinitionInfo>(GetOuter());
}
