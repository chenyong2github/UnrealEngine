// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Classes.h"
#include "Async/TaskGraphInterfaces.h"

class FUnrealSourceFile;
class UPackage;
class FProperty;
class UFunction;
class UStruct;
class UField;
class UClass;
class UEnum;
class UScriptStruct;
class UDelegateFunction;
class FClassMetaData;
class FOutputDevice;
class FUnrealPackageDefinitionInfo;
struct FFuncInfo;
struct FGeneratedFileInfo;

//
//	FNativeClassHeaderGenerator
//

namespace EExportFunctionHeaderStyle
{
	enum Type
	{
		Definition,
		Declaration
	};
}

namespace EExportFunctionType
{
	enum Type
	{
		Interface,
		Function,
		Event
	};
}

class FClass;
class FClasses;
class FModuleClasses;
class FScope;

// These are declared in this way to allow swapping out the classes for something more optimized in the future
typedef FStringOutputDevice FUHTStringBuilder;

enum class EExportingState
{
	Normal,
	TypeEraseDelegates
};

enum class EExportCallbackType
{
	Interface,
	Class
};

enum class EExportClassOutFlags
{
	None = 0x0,
	NeedsPushModelHeaders = 0x1 << 0,
};
ENUM_CLASS_FLAGS(EExportClassOutFlags);

struct FPropertyNamePointerPair
{
	FPropertyNamePointerPair(FString InName, FProperty* InProp)
		: Name(MoveTemp(InName))
		, Prop(InProp)
	{
	}

	FString Name;
	FProperty* Prop;
};

FString CreateUTF8LiteralString(const FString& Str);

/**
 * Structure to load and maintain information about a generated file
 */

struct FGeneratedFileInfo
{
	FGeneratedFileInfo(bool bInAllowSaveExportedHeaders)
		: bAllowSaveExportedHeaders(bInAllowSaveExportedHeaders)
	{
	}

	/**
	 * Start the process of loading the existing version of the file.  The output file name will also be initialized.
	 */
	void StartLoad(FString&& InFilename);

	/**
	 * Get the output file name.
	 */
	FString& GetFilename() { return Filename; }
	const FString& GetFilename() const { return Filename; }

	/**
	 * Return the original contents of the output file. This string will not be valid untile the load task has completed.
	 */
	const FString& GetOriginalContents() const
	{
		return OriginalContents;
	}

	/**
	 * Return a string builder that can be used to store the new copy of the file.  The body will not be a 
	 * complete version of the new file.
	 */
	FUHTStringBuilder& GetGeneratedBody()
	{
		return GeneratedBody;
	}

	/**
	 * After the new contents of the file has been serialized into string builder returned by GetGeneratedBody, 
	 * invoke this method to generate a body hash for the new contents.
	 */
	void GenerateBodyHash();

	/**
	 * Return the generated hash for the body
	 */
	uint32 GetGeneratedBodyHash() const
	{
		return GeneratedBodyHash;
	}


	/**
	 * Store the task being used to save the updated text of the file
	 */
	void SetSaveTaskRef(FGraphEventRef&& InSaveTaskRef)
	{
		SaveTaskRef = MoveTemp(InSaveTaskRef);
	}

	/**
	 * If the save task is valid, add it to the supplied array of tasks
	 */
	void AddSaveTaskRef(FGraphEventArray& Events) const
	{
		if (SaveTaskRef.IsValid())
		{
			Events.Add(SaveTaskRef);
		}
	}

	/**
	 * If the load task is valid, add it to the supplied array of tasks.  StartLoad must have already been called.
	 */
	void AddLoadTaskRef(FGraphEventArray& Events) const
	{
		if (LoadTaskRef.IsValid())
		{
			Events.Add(LoadTaskRef);
		}
	}

	/**
	 * Set the package filename that represents the name of the file as packaged.
	 * TODO: Verify this is the same as Filename and eliminate if that is the case.
	 */
	void SetPackageFilename(FString&& InFilename)
	{
		PackageFilename = MoveTemp(InFilename);
	}

	/**
	 * If the package filename has been set, add it to the set of given file names
	 */
	void AddPackageFilename(TSet<FString>& PackageHeaderPaths)
	{
		if (!PackageFilename.IsEmpty())
		{
			PackageHeaderPaths.Add(MoveTemp(PackageFilename));
		}
	}

	/**
	 * Set the name of the temporary location of the file.  It will be moved as part of the saving process.
	 */
	void SetTempFilename(FString&& InFilename)
	{
		TempFilename = MoveTemp(InFilename);
	}

	/**
	 * If the temp file name is set, add it to the list of temporary file names
	 */
	void AddTempFilename(TArray<FString>& TempHeaderPaths)
	{
		if (!TempFilename.IsEmpty())
		{
			TempHeaderPaths.Add(MoveTemp(TempFilename));
		}
	}

	/**
	 * If true, the existing version of the file will be read and the new version will be saved.
	 */
	bool AllowSaveExportedHeaders() const
	{
		return bAllowSaveExportedHeaders;
	}

private:
	bool bAllowSaveExportedHeaders = true;
	uint32 GeneratedBodyHash = 0;
	FString Filename;
	FString PackageFilename;
	FString TempFilename;
	FString OriginalContents;
	FUHTStringBuilder GeneratedBody;
	FGraphEventRef LoadTaskRef;
	FGraphEventRef SaveTaskRef;
};

/**
 * For every FUnrealSourceFile being processed, an instance of this class represents the data associated with generating the new output.
 */
struct FGeneratedCPP
{
	
	/**
	 * Construct a new instance that refers to the source package and file
	 */
	FGeneratedCPP(FUnrealPackageDefinitionInfo& InPackageDef, FUnrealSourceFile& InSourceFile);

	/**
	 * If this source is to be exported, verify that the final generation task has been set and add it to the output.
	 * This method is used to make sure that any dependent files have been generated before the file in question is
	 * generated.
	 */
	void AddGenerateTaskRef(FGraphEventArray& Events) const;

	/**
	 * If this source is to be exported, verify that the export task has been set and add it to the output.
	 * This method is used to make sure that the complete export process for the file, excluding saving task,
	 * has completed.
	 */
	void AddExportTaskRef(FGraphEventArray& Events) const;

	/**
	 * The package definition being exported
	 */
	FUnrealPackageDefinitionInfo& PackageDef;

	/**
	 * The source file being exported
	 */
	FUnrealSourceFile& SourceFile;

	/**
	 * The old and new header information.
	 */
	FGeneratedFileInfo Header;

	/**
	 * The old and new source information
	 */
	FGeneratedFileInfo Source;

	// The following information is collected during generation process
	TSet<FString> CrossModuleReferences;
	TSet<FString> ForwardDeclarations;
	FUHTStringBuilder GeneratedFunctionDeclarations;
	EExportClassOutFlags ExportFlags = EExportClassOutFlags::None;

	/**
	 * This task represents the task that generates the source
	 */
	FGraphEventRef GenerateTaskRef;

	/**
	 * This task represents the task that completes the export process of the source
	 */
	FGraphEventRef ExportTaskRef;
};

/**
 * Structure used to perform output generation
 */
struct FNativeClassHeaderGenerator
{
private:
	FUnrealPackageDefinitionInfo& PackageDef;

	/**
	 * Gets API string for this header with trailing space.
	 */
	const FString& GetAPIString() const;

	/** A collection of structures used to gather various kinds of references conveniently grouped together to make passing easier */
	struct FReferenceGatherers
	{
		FReferenceGatherers(TSet<FString>* InUniqueCrossModuleReferences,TSet<FString> & InForwardDeclarations)
			: UniqueCrossModuleReferences(InUniqueCrossModuleReferences)
			, ForwardDeclarations(InForwardDeclarations)
		{
		}

		/** Set of already exported cross-module references, to prevent duplicates */
		TSet<FString>* UniqueCrossModuleReferences;
		/** Forward declarations that we need. */
		TSet<FString>& ForwardDeclarations;
	};

	/**
	 * Exports the struct's C++ properties to the HeaderText output device and adds special
	 * compiler directives for GCC to pack as we expect.
	 *
	 * @param	Out				alternate output device
	 * @param	Struct			UStruct to export properties
	 * @param	TextIndent		Current text indentation
	 */
	static void ExportProperties(FOutputDevice& Out, UStruct* Struct, int32 TextIndent);

	/** Return the name of the singleton function */
	static const FString& GetPackageSingletonName(UPackage* Item, TSet<FString>* UniqueCrossModuleReferences);

	/** Return the address of the singleton function */
	static const FString& GetPackageSingletonNameFuncAddr(UPackage* Item, TSet<FString>* UniqueCrossModuleReferences);

	/** Return the address of the singleton function - handles nullptr */
	static const FString& GetSingletonNameFuncAddr(UField* Item, TSet<FString>* UniqueCrossModuleReferences, bool bRequiresValidObject = true);

	/**
	 * Returns the name (overridden if marked up) or "" wrappers for use in a string literal.
	 */
	template <typename T>
	static FString GetUTF8OverriddenNameForLiteral(const T* Item)
	{
		const FString& OverriddenName = Item->GetMetaData(TEXT("OverrideNativeName"));
		if (!OverriddenName.IsEmpty())
		{
			return CreateUTF8LiteralString(OverriddenName);
		}
		return CreateUTF8LiteralString(Item->GetName());
	}

	/**
	 * Export functions used to find and call C++ or script implementation of a script function in the interface 
	 */
	void ExportInterfaceCallFunctions(FOutputDevice& OutCpp, FUHTStringBuilder& Out, FReferenceGatherers& OutReferenceGatherers, const TArray<UFunction*>& CallbackFunctions, const TCHAR* ClassName) const;

private:

	// Constructor
	FNativeClassHeaderGenerator(
		FUnrealPackageDefinitionInfo& PackageDef
	);

	/**
	 * After all of the dependency checking, and setup for isolating the generated code, actually export the class
	 *
	 * @param	OutEnums			Output device for enums declarations.
	 * @param	OutputGetter		The function to call to get the output.
	 * @param	SourceFile			Source file to export.
	 */
	void ExportClassFromSourceFileInner(
		FOutputDevice&           OutGeneratedHeaderText,
		FOutputDevice&           OutputGetter,
		FOutputDevice&           OutDeclarations,
		FReferenceGatherers&     OutReferenceGatherers, 
		FClass*                  Class,
		const FUnrealSourceFile& SourceFile,
		EExportClassOutFlags&    OutFlags
	) const;

	/**
	 * After all of the dependency checking, but before actually exporting the class, set up the generated code
	 */
	static bool WriteHeader(FGeneratedFileInfo& FileInfo, const FString& InBodyText, const TSet<FString>& InAdditionalHeaders, const TSet<FString>& ForwardDeclarations);

	/**
	 * Write the body of a source file using a standard format
	 */
	static bool WriteSource(const FManifestModule& Module, FGeneratedFileInfo& FileInfo, const FString& InBodyText, FUnrealSourceFile* InSourceFile, const TSet<FString>& InCrossModuleReferences);

	/**
	 * Returns a string in the format CLASS_Something|CLASS_Something which represents all class flags that are set for the specified
	 * class which need to be exported as part of the DECLARE_CLASS macro
	 */
	static FString GetClassFlagExportText( UClass* Class );

	/**
	 * Exports the header text for the enum specified
	 * 
	 * @param	Out		the output device for the mirror struct
	 * @param	Enums	the enum to export
	 */
	void ExportEnum(FOutputDevice& Out, UEnum* Enum) const;

	/**
	 * Exports the inl text for enums declared in non-UClass headers.
	 * 
	 * @param	OutputGetter	The function to call to get the output.
	 * @param	Enum			the enum to export
	 */
	void ExportGeneratedEnumInitCode(FOutputDevice& Out, FReferenceGatherers& OutReferenceGatherers, const FUnrealSourceFile& SourceFile, UEnum* Enum) const;

	/**
	 * Exports the macro declarations for GENERATED_BODY() for each Foo in the struct specified
	 * 
	 * @param	Out				output device
	 * @param	Struct			The struct to export
	 */
	void ExportGeneratedStructBodyMacros(FOutputDevice& OutGeneratedHeaderText, FOutputDevice& Out, FReferenceGatherers& OutReferenceGatherers, const FUnrealSourceFile& SourceFile, UScriptStruct* Struct) const;

	/**
	 * Exports a local mirror of the specified struct; used to get offsets
	 * 
	 * @param	Out			the output device for the mirror struct
	 * @param	Structs		the struct to export
	 * @param	TextIndent	the current indentation of the header exporter
	 */
	static void ExportMirrorsForNoexportStruct(FOutputDevice& Out, UScriptStruct* Struct, int32 TextIndent);

	/**heade
	 * Exports the parameter struct declarations for the list of functions specified
	 * 
	 * @param	Function	the function that (may) have parameters which need to be exported
	 * @return	true		if the structure generated is not completely empty
	 */
	static bool WillExportEventParms( UFunction* Function );

	/**
	 * Exports C++ type declarations for delegates
	 *
	 * @param	Out					output device
	 * @param	SourceFile			Source file of the delegate.
	 * @param	DelegateFunctions	the functions that have parameters which need to be exported
	 */
	void ExportDelegateDeclaration(FOutputDevice& Out, FReferenceGatherers& OutReferenceGatherers, const FUnrealSourceFile& SourceFile, UFunction* Function) const;

	/**
	 * Exports C++ type definitions for delegates
	 *
	 * @param	Out					output device
	 * @param	SourceFile			Source file of the delegate.
	 * @param	DelegateFunctions	the functions that have parameters which need to be exported
	 */
	void ExportDelegateDefinition(FOutputDevice& Out, FReferenceGatherers& OutReferenceGatherers, const FUnrealSourceFile& SourceFile, UFunction* Function) const;

	/**
	 * Exports the parameter struct declarations for the given function.
	 *
	 * @param	Out					output device
	 * @param	Function			the function that have parameters which need to be exported
	 * @param	Indent				number of spaces to put before each line
	 * @param	bOutputConstructor	If true, output a constructor for the param struct
	 */
	static void ExportEventParm(FUHTStringBuilder& Out, TSet<FString>& PropertyFwd, UFunction* Function, int32 Indent, bool bOutputConstructor, EExportingState ExportingState);

	/**
	* Move the temp header files into the .h files
	* 
	* @param	PackageName	Name of the package being saved
	* @param	TempHeaderPaths	Names of all the headers to move
	*/
	static void ExportUpdatedHeaders(FString&& PackageName, TArray<FString>&& TempHeaderPaths, FGraphEventArray& InTempSaveTasks);

	/**
	 * Get the intrinsic null value for this property
	 * 
	 * @param	Prop				the property to get the null value for
	 * @param	bMacroContext		true when exporting the P_GET* macro, false when exporting the friendly C++ function header
	 * @param	bInitializer		if true, will just return ForceInit instead of FStruct(ForceInit)
	 *
	 * @return	the intrinsic null value for the property (0 for ints, TEXT("") for strings, etc.)
	 */
	static FString GetNullParameterValue( FProperty* Prop, bool bInitializer = false );

	/**
	 * Exports a native function prototype
	 * 
	 * @param	Out					Where to write the exported function prototype to.
	 * @param	FunctionData		data representing the function to export
	 * @param	FunctionType		Whether to export this function prototype as an event stub, an interface or a native function stub.
	 * @param	FunctionHeaderStyle	Whether we're outputting a declaration or definition.
	 * @param	ExtraParam			Optional extra parameter that will be added to the declaration as the first argument
	 */
	static void ExportNativeFunctionHeader(
		FOutputDevice&                   Out,
		TSet<FString>&                   OutFwdDecls,
		const FFuncInfo&                 FunctionData,
		EExportFunctionType::Type        FunctionType,
		EExportFunctionHeaderStyle::Type FunctionHeaderStyle,
		const TCHAR*                     ExtraParam,
		const TCHAR*                     APIString
	);

	/**
	* Runs checks whether necessary RPC functions exist for function described by FunctionData.
	*
	* @param	FunctionData			Data representing the function to export.
	* @param	ClassName				Name of currently parsed class.
	* @param	ImplementationPosition	Position in source file of _Implementation function for function described by FunctionData.
	* @param	ValidatePosition		Position in source file of _Validate function for function described by FunctionData.
	* @param	SourceFile				Currently analyzed source file.
	*/
	void CheckRPCFunctions(FReferenceGatherers& OutReferenceGatherers, const FFuncInfo& FunctionData, const FString& ClassName, int32 ImplementationPosition, int32 ValidatePosition, const FUnrealSourceFile& SourceFile) const;

	/**
	 * Exports the native stubs for the list of functions specified
	 * 
	 * @param SourceFile	current source file
	 * @param Class			class
	 * @param ClassData		class data
	 */
	void ExportNativeFunctions(FOutputDevice& OutGeneratedHeaderText, FOutputDevice& OutGeneratedCPPText, FOutputDevice& OutMacroCalls, FOutputDevice& OutNoPureDeclsMacroCalls, FReferenceGatherers& OutReferenceGatherers, const FUnrealSourceFile& SourceFile, UClass* Class, FClassMetaData* ClassData) const;

	/**
	 * Export the actual internals to a standard thunk function
	 *
	 * @param RPCWrappers output device for writing
	 * @param Function given function
	 * @param FunctionData function data for the current function
	 * @param Parameters list of parameters in the function
	 * @param Return return parameter for the function
	 */
	void ExportFunctionThunk(FUHTStringBuilder& RPCWrappers, FReferenceGatherers& OutReferenceGatherers, UFunction* Function, const FFuncInfo& FunctionData, const TArray<FProperty*>& Parameters, FProperty* Return) const;

	/** Exports the native function registration code for the given class. */
	static void ExportNatives(FOutputDevice& Out, FClass* Class);

	/**
	 * Exports generated singleton functions for UObjects that used to be stored in .u files.
	 * 
	 * @param	Out				The destination to write to.
	 * @param	SourceFile		The source file being processed.
	 * @param	Class			Class to export
	 * @param	OutFriendText	(Output parameter) Friend text
	 */
	void ExportNativeGeneratedInitCode(FOutputDevice& Out, FOutputDevice& OutDeclarations, FReferenceGatherers& OutReferenceGatherers, const FUnrealSourceFile& SourceFile, FClass* Class, FUHTStringBuilder& OutFriendText) const;

	/**
	 * Export given function.
	 *
	 * @param	Out				The destination to write to.
	 * @param	Function		Given function.
	 * @param	bIsNoExport		Is in NoExport class.
	 */
	void ExportFunction(FOutputDevice& Out, FReferenceGatherers& OutReferenceGatherers, const FUnrealSourceFile& SourceFile, UFunction* Function, bool bIsNoExport) const;

	/**
	 * Exports a generated singleton function to setup the package for compiled-in classes.
	 * 
	 * @param	Out			The destination to write to.
	 * @param	Package		Package to export code for.
	**/
	void ExportGeneratedPackageInitCode(FOutputDevice& Out, const TCHAR* InDeclarations, uint32 CRC);

	/**
	 * Function to output the C++ code necessary to set up the given array of properties
	 * 
	 * @param	DeclOut			String output device to send the generated declarations to
	 * @param	Out				String output device to send the generated code to
	 * @param	Scope			The scope to prefix on all variable definitions
	 * @param	Properties		Array of properties to export
	 * @param	Spaces			String of spaces to use as an indent for the declaration
	 * @param	Spaces			String of spaces to use as an indent
	 *
	 * @return      A pair of strings which represents the pointer and a count of the emitted properties.
	 */
	TTuple<FString, FString> OutputProperties(FOutputDevice& DeclOut, FOutputDevice& Out, FReferenceGatherers& OutReferenceGatherers, const TCHAR* Scope, const TArray<FProperty*>& Properties, const TCHAR* DeclSpaces, const TCHAR* Spaces) const;

	/**
	 * Function to output the C++ code necessary to set up a property
	 * 
	 * @param	DeclOut			String output device to send the generated declarations to
	 * @param	Out				String output device to send the generated code to
	 * @param	Scope			The scope to prefix on all variable definitions
	 * @param	Prop			Property to export
	 * @param	DeclSpaces		String of spaces to use as an indent for the declaration
	 * @param	Spaces			String of spaces to use as an indent
	**/
	void OutputProperty(FOutputDevice& DeclOut, FOutputDevice& Out, FReferenceGatherers& OutReferenceGatherers, const TCHAR* Scope, TArray<FPropertyNamePointerPair>& PropertyNamesAndPointers, FProperty* Prop, const TCHAR* DeclSpaces, const TCHAR* Spaces) const;

	/**
	 * Function to output the C++ code necessary to set up a property, including an array property and its inner, array dimensions, etc.
	 *
	 * @param	DeclOut			The destination to write declarations to.
	 * @param	Out				The destination to write to.
	 * @param	Prop			Property to export
	 * @param	OffsetStr		String specifying the property offset
	 * @param	Name			Name for the generated variable
	 * @param	DeclSpaces		String of spaces to use as an indent for the declaration
	 * @param	Spaces			String of spaces to use as an indent
	 * @param	SourceStruct	Structure that the property offset is relative to
	**/
	void PropertyNew(FOutputDevice& DeclOut, FOutputDevice& Out, FReferenceGatherers& OutReferenceGatherers, FProperty* Prop, const TCHAR* OffsetStr, const TCHAR* Name, const TCHAR* DeclSpaces, const TCHAR* Spaces, const TCHAR* SourceStruct = nullptr) const;

	/**
	 * Function to generate the property tag
	 *
	 * @param	Out				Destination string builder.
	 * @param	Object			UObject in question
	 */
	static void GetPropertyTag(FUHTStringBuilder& Out, UObject* Object);

	/**
	 * Function to generate the property tag
	 * 
	 * @param	Out				Destination string builder.
	 * @param	Prop			Property in question.
	 */
	static void GetPropertyTag(FUHTStringBuilder& Out, FProperty* Prop);

	/**
	 * Exports the proxy definitions for the list of enums specified
	 * 
	 * @param SourceFile	current source file
	 */
	static void ExportCallbackFunctions(
		FOutputDevice&            OutGeneratedHeaderText,
		FOutputDevice&            Out,
		TSet<FString>&            OutFwdDecls,
		const TArray<UFunction*>& CallbackFunctions,
		const TCHAR*              CallbackWrappersMacroName,
		EExportCallbackType       ExportCallbackType,
		const TCHAR*              APIString
	);

	/**
	 * Determines if the property has alternate export text associated with it and if so replaces the text in PropertyText with the
	 * alternate version. (for example, structs or properties that specify a native type using export-text).  Should be called immediately
	 * after ExportCppDeclaration()
	 *
	 * @param	Prop			the property that is being exported
	 * @param	PropertyText	the string containing the text exported from ExportCppDeclaration
	 */
	static void ApplyAlternatePropertyExportText(FProperty* Prop, FUHTStringBuilder& PropertyText, EExportingState ExportingState);

	/**
	* Create a temp header file name from the header name
	*
	* @param	CurrentFilename		The filename off of which the current filename will be generated
	* @param	bReverseOperation	Get the header from the temp file name instead
	*
	* @return	The generated string
	*/
	static FString GenerateTempHeaderName( const FString& CurrentFilename, bool bReverseOperation = false );

	/**
	 * Saves a generated header if it has changed. 
	 *
	 * @param	FileInfo			Contextual information about the file
	 * @param	NewHeaderContents	New complete contents of the file
	 * @return True if the header contents has changed, false otherwise.
	 */
	static bool SaveHeaderIfChanged(FGeneratedFileInfo& FileInfo, FString&& NewHeaderContents);

	/**
	 * Deletes all .generated.h files which do not correspond to any of the classes.
	 */
	static void DeleteUnusedGeneratedHeaders(TSet<FString>&& PackageHeaderPathSet);

	/**
	 * Exports macros that manages UObject constructors.
	 * 
	 * @param	VTableOut								The destination to write vtable helpers to.
	 * @param	StandardUObjectConstructorsMacroCall	The destination to write standard constructor macros to.
	 * @param	EnhancedUObjectConstructorsMacroCall	The destination to write enhanced constructor macros to.
	 * @param	ConstructorsMacroPrefix					Prefix for constructors macro.
	 * @param	Class									Class for which to export macros.
	 */
	static void ExportConstructorsMacros(FOutputDevice& OutGeneratedHeaderText, FOutputDevice& VTableOut, FOutputDevice& StandardUObjectConstructorsMacroCall, FOutputDevice& EnhancedUObjectConstructorsMacroCall, const FString& ConstructorsMacroPrefix, FClass* Class, const TCHAR* APIArg);

	/**
	 * Gets string with function return type.
	 *
	 * @param Function Function to get return type of.
	 * @return FString with function return type.
	 */
	static FString GetFunctionReturnString(UFunction* Function, FReferenceGatherers& OutReferenceGatherers);

	/**
	* Gets string with function parameters (with names).
	*
	* @param Function Function to get parameters of.
	* @return FString with function parameters.
	*/
	static FString GetFunctionParameterString(UFunction* Function, FReferenceGatherers& OutReferenceGatherers);

	/**
	 * Checks if function is missing "virtual" specifier.
	 *
	 * @param SourceFile SourceFile where function is declared.
	 * @param FunctionNamePosition Position of name of function in SourceFile.
	 * @return true if function misses "virtual" specifier, false otherwise.
	 */
	static bool IsMissingVirtualSpecifier(const FString& SourceFile, int32 FunctionNamePosition);

public:

	/** 
	 * Properties in source files generated from blueprint assets have a symbol name that differs from the source asset.
	 * This function returns the original name of the field (rather than the native, symbol name).
	 */
	template <typename T>
	static FString GetOverriddenName(const T* Item)
	{
		const FString& OverriddenName = Item->GetMetaData(TEXT("OverrideNativeName"));
		if (!OverriddenName.IsEmpty())
		{
			return OverriddenName.ReplaceCharWithEscapedChar();
		}
		return Item->GetName();
	}

	template <typename T>
	static FName GetOverriddenFName(const T* Item)
	{
		FString OverriddenName = Item->GetMetaData(TEXT("OverrideNativeName"));
		if (!OverriddenName.IsEmpty())
		{
			return FName(*OverriddenName);
		}
		return Item->GetFName();
	}

	template <typename T>
	static FString GetOverriddenPathName(const T* Item)
	{
		return FString::Printf(TEXT("%s.%s"), *FClass::GetTypePackageName(Item), *GetOverriddenName(Item));
	}

	/**
	 * Generate all the sources
	 *
	 * @param	GeneratedCPPs		Complete list of all source files being generated.
	 */
	static void GenerateSourceFiles(TArray<FGeneratedCPP>& GeneratedCPPs);

	/**
	 * Generate all the extra output files for the given package.
	 *
	 * @param	PackageDef			The Package definition in question.
	 * @param	GeneratedCPPs		Complete list of all source files being generated.
	 */
	static void Generate(FUnrealPackageDefinitionInfo& PackageDef, TArray<FGeneratedCPP>& GeneratedCPPs);
};

