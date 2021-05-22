// Copyright Epic Games, Inc. All Rights Reserved.

#include "UnrealTypeDefinitionInfo.h"
#include "BaseParser.h"
#include "Classes.h"
#include "ClassMaps.h"
#include "HeaderParser.h"
#include "NativeClassExporter.h"
#include "Scope.h"
#include "UnrealHeaderTool.h"
#include "UnrealSourceFile.h"
#include "Misc/PackageName.h"
#include "UObject/ErrorException.h"
#include "UObject/Interface.h"
#include "Specifiers/ClassMetadataSpecifiers.h"
#include "Algo/FindSortedStringCaseInsensitive.h"

namespace
{
	const FName NAME_IgnoreCategoryKeywordsInSubclasses(TEXT("IgnoreCategoryKeywordsInSubclasses"));
	const FName NAME_ReplaceConverted(TEXT("ReplaceConverted"));
	const FName NAME_ClassGroupNames(TEXT("ClassGroupNames"));

	/**
	 * As part of the singleton name, collect the parent chain names
	 */
	void AddOuterNames(FUHTStringBuilder& Out, UObject* Outer)
	{
		if (Outer == nullptr)
		{
			return;
		}

		if (Cast<UClass>(Outer) || Cast<UScriptStruct>(Outer))
		{
			// Structs can also have UPackage outer.
			if (!Outer->IsA<UClass>() && !Outer->GetOuter()->IsA<UPackage>())
			{
				AddOuterNames(Out, Outer->GetOuter());
			}
			Out.Append(TEXT("_"));
			Out.Append(FNameLookupCPP::GetNameCPP(Cast<UStruct>(Outer)));
		}
		else if (UPackage* Package = Cast<UPackage>(Outer))
		{
			Out.Append(TEXT("_"));
			Out.Append(FPackageName::GetShortName(Outer->GetName()));
		}
		else
		{
			AddOuterNames(Out, Outer->GetOuter());
			Out.Append(TEXT("_"));
			Out.Append(Outer->GetName());
		}
	}

	/**
	 * Generates singleton name.
	 */
	void GenerateSingletonName(FUHTStringBuilder& Out, UField* Item, bool bRequiresValidObject)
	{
		check(Item);

		Out.Append(TEXT("Z_Construct_"));
		Out.Append(FNameLookupCPP::GetNameCPP(Item->GetClass()));
		AddOuterNames(Out, Item);

		if (UClass* ItemClass = Cast<UClass>(Item))
		{
			if (!bRequiresValidObject && !ItemClass->HasAllClassFlags(CLASS_Intrinsic))
			{
				Out.Append(TEXT("_NoRegister"));
			}
		}
		Out.Append(TEXT("()"));
	}

	bool IsActorClass(UClass* Class)
	{
		while (Class)
		{
			if (Class->GetFName() == NAME_Actor)
			{
				return true;
			}
			Class = Class->GetSuperClass();
		}
		return false;
	}

	template <typename T>
	FString GetTypePackageName_Inner(const T* Field)
	{
		FString PackageName = Field->GetMetaData(NAME_ReplaceConverted);
		if (PackageName.Len())
		{
			int32 ObjectDotIndex = INDEX_NONE;
			// Strip the object name
			if (PackageName.FindChar(TEXT('.'), ObjectDotIndex))
			{
				PackageName.MidInline(0, ObjectDotIndex, false);
			}
		}
		else
		{
			PackageName = Field->GetOutermost()->GetName();
		}
		return PackageName;
	}
}

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

bool FUnrealTypeDefinitionInfo::IsDynamic(const UField* Field)
{
	return Field->HasMetaData(NAME_ReplaceConverted);
}

bool FUnrealTypeDefinitionInfo::IsDynamic(const FField* Field)
{
	return Field->HasMetaData(NAME_ReplaceConverted);
}


FString FUnrealTypeDefinitionInfo::GetNameWithPrefix(const UClass* InClass, EEnforceInterfacePrefix EnforceInterfacePrefix)
{
	const TCHAR* Prefix = 0;

	if (InClass->HasAnyClassFlags(CLASS_Interface))
	{
		// Grab the expected prefix for interfaces (U on the first one, I on the second one)
		switch (EnforceInterfacePrefix)
		{
		case EEnforceInterfacePrefix::None:
			// For old-style files: "I" for interfaces, unless it's the actual "Interface" class, which gets "U"
			if (InClass->GetFName() == NAME_Interface)
			{
				Prefix = TEXT("U");
			}
			else
			{
				Prefix = TEXT("I");
			}
			break;

		case EEnforceInterfacePrefix::I:
			Prefix = TEXT("I");
			break;

		case EEnforceInterfacePrefix::U:
			Prefix = TEXT("U");
			break;

		default:
			check(false);
		}
	}
	else
	{
		// Get the expected class name with prefix
		Prefix = InClass->GetPrefixCPP();
	}

	return FString::Printf(TEXT("%s%s"), Prefix, *InClass->GetName());
}


void FUnrealPropertyDefinitionInfo::PostParseFinalize()
{
	TypePackageName = GetTypePackageName_Inner(GetProperty());
}

void FUnrealPropertyDefinitionInfo::AddMetaData(TMap<FName, FString>&& InMetaData)
{
	// only add if we have some!
	if (InMetaData.Num())
	{
		check(Property);

		UPackage* Package = Property->GetOutermost();
		// get (or create) a metadata object for this package
		UMetaData* MetaData = Package->GetMetaData();

		for (TPair<FName, FString>& MetaKeyValue : InMetaData)
		{
			Property->SetMetaData(MetaKeyValue.Key, MoveTemp(MetaKeyValue.Value));
		}
	}
}

bool FUnrealPropertyDefinitionInfo::IsDynamic() const
{
	return GetProperty()->HasMetaData(NAME_ReplaceConverted);
}

bool FUnrealPropertyDefinitionInfo::IsOwnedByDynamicType() const
{
	for (FFieldVariant Owner = GetProperty()->GetOwnerVariant(); Owner.IsValid(); Owner = Owner.GetOwnerVariant())
	{
		if (Owner.IsUObject())
		{
			return GTypeDefinitionInfoMap.FindChecked<FUnrealObjectDefinitionInfo>(Owner.ToUObject()).IsOwnedByDynamicType();
		}
		else
		{
			return GTypeDefinitionInfoMap.FindChecked(Owner.ToField()).IsOwnedByDynamicType();
		}
	}
	return false;
}

FUnrealPackageDefinitionInfo::FUnrealPackageDefinitionInfo(const FManifestModule& InModule, UPackage* InPackage)
	: FUnrealObjectDefinitionInfo(FString())
	, Module(InModule)
	, ShortUpperName(FPackageName::GetShortName(InPackage).ToUpper())
	, API(FString::Printf(TEXT("%s_API "), *ShortUpperName))
{
	SetObject(InPackage);
}

void FUnrealPackageDefinitionInfo::PostParseFinalize()
{
	UPackage* Package = GetPackage();

	FString PackageName = Package->GetName();
	PackageName.ReplaceInline(TEXT("/"), TEXT("_"), ESearchCase::CaseSensitive);

	SingletonName.Appendf(TEXT("Z_Construct_UPackage_%s()"), *PackageName);
	SingletonNameChopped = SingletonName.LeftChop(2);
	ExternDecl.Appendf(TEXT("\tUPackage* %s;\r\n"), *SingletonName);
}

void FUnrealPackageDefinitionInfo::AddCrossModuleReference(TSet<FString>* UniqueCrossModuleReferences) const
{
	if (UniqueCrossModuleReferences)
	{
		UniqueCrossModuleReferences->Add(GetExternDecl());
	}
}

void FUnrealFieldDefinitionInfo::PostParseFinalize()
{
	const TCHAR* TypeStr = GetSimplifiedTypeClass();
	UField* Field = GetField();
	FString PackageShortName = FPackageName::GetShortName(Field->GetOutermost()).ToUpper();

	FUHTStringBuilder Out;
	GenerateSingletonName(Out, Field, false);
	ExternDecl[0].Appendf(TEXT("\t%s_API %s* %s;\r\n"), *PackageShortName, TypeStr, *Out);
	SingletonName[0] = Out;
	SingletonNameChopped[0] = SingletonName[0].LeftChop(2);

	Out.Reset();
	GenerateSingletonName(Out, Field, true);
	ExternDecl[1].Appendf(TEXT("\t%s_API %s* %s;\r\n"), *PackageShortName, TypeStr, *Out);
	SingletonName[1] = Out;
	SingletonNameChopped[1] = SingletonName[1].LeftChop(2);

	TypePackageName = GetTypePackageName_Inner(GetField());
}

void FUnrealFieldDefinitionInfo::AddCrossModuleReference(TSet<FString>* UniqueCrossModuleReferences, bool bRequiresValidObject) const
{
	// We don't need to export UFunction externs, though we may need the externs for UDelegateFunctions
	if (UniqueCrossModuleReferences)
	{
		UField* Field = GetField();
		if (!Field->IsA<UFunction>() || Field->IsA<UDelegateFunction>())
		{
			UniqueCrossModuleReferences->Add(GetExternDecl(bRequiresValidObject));
		}
	}
}

void FUnrealFieldDefinitionInfo::AddMetaData(TMap<FName, FString>&& InMetaData)
{
	// only add if we have some!
	if (InMetaData.Num())
	{
		UField* Field = GetField();
		check(Field);

		// get (or create) a metadata object for this package
		UMetaData* MetaData = Field->GetOutermost()->GetMetaData();
		TMap<FName, FString>* ExistingMetaData = MetaData->GetMapForObject(Field);
		if (ExistingMetaData && ExistingMetaData->Num())
		{
			// Merge the existing metadata
			TMap<FName, FString> MergedMetaData;
			MergedMetaData.Reserve(InMetaData.Num() + ExistingMetaData->Num());
			MergedMetaData.Append(*ExistingMetaData);
			MergedMetaData.Append(InMetaData);
			MetaData->SetObjectValues(Field, MoveTemp(MergedMetaData));
		}
		else
		{
			// set the metadata for this field
			MetaData->SetObjectValues(Field, MoveTemp(InMetaData));
		}
	}
}

bool FUnrealFieldDefinitionInfo::IsDynamic() const
{
	return GetField()->HasMetaData(NAME_ReplaceConverted);
}

bool FUnrealFieldDefinitionInfo::IsOwnedByDynamicType() const
{
	for (const FUnrealObjectDefinitionInfo* OuterObject = GetOuter(); OuterObject; OuterObject = OuterObject->GetOuter())
	{
		if (OuterObject->IsDynamic())
		{
			return true;
		}
	}
	return false;
}

FUnrealEnumDefinitionInfo::FUnrealEnumDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, FString&& InNameCPP)
	: FUnrealFieldDefinitionInfo(InSourceFile, InLineNumber, MoveTemp(InNameCPP), InSourceFile.GetPackageDef())
{ }

void FUnrealStructDefinitionInfo::AddProperty(FUnrealPropertyDefinitionInfo& PropertyDef)
{
	Properties.Add(&PropertyDef);

	// update the optimization flags
	if (!bContainsDelegates)
	{
		FProperty* Prop = PropertyDef.GetProperty();
		if (Prop->IsA(FDelegateProperty::StaticClass()) || Prop->IsA(FMulticastDelegateProperty::StaticClass()))
		{
			bContainsDelegates = true;
		}
		else
		{
			FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop);
			if (ArrayProp != NULL)
			{
				if (ArrayProp->Inner->IsA(FDelegateProperty::StaticClass()) || ArrayProp->Inner->IsA(FMulticastDelegateProperty::StaticClass()))
				{
					bContainsDelegates = true;
				}
			}
		}
	}
}

void FUnrealStructDefinitionInfo::AddFunction(FUnrealFunctionDefinitionInfo& FunctionDef)
{
	Functions.Add(&FunctionDef);

	// update the optimization flags
	if (!bContainsDelegates)
	{
		if (FunctionDef.GetFunction()->HasAnyFunctionFlags(FUNC_Delegate))
		{
			bContainsDelegates = true;
		}
	}
}

FUnrealScriptStructDefinitionInfo::FUnrealScriptStructDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, FString&& InNameCPP)
	: FUnrealStructDefinitionInfo(InSourceFile, InLineNumber, MoveTemp(InNameCPP), InSourceFile.GetPackageDef())
{ }

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
		StructScope = MakeShared<FStructScope>(*this, static_cast<UStruct*>(InObject), &GetUnrealSourceFile().GetScope().Get());
	}
}

FUnrealClassDefinitionInfo::FUnrealClassDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, FString&& InNameCPP, bool bInIsInterface)
	: FUnrealStructDefinitionInfo(InSourceFile, InLineNumber, MoveTemp(InNameCPP), InSourceFile.GetPackageDef())
	, bIsInterface(bInIsInterface)
{
	if (bInIsInterface)
	{
		GetStructMetaData().ParsedInterface = EParsedInterface::ParsedUInterface;
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


void FUnrealClassDefinitionInfo::PostParseFinalize()
{
	if (IsInterface() && GetStructMetaData().ParsedInterface == EParsedInterface::ParsedUInterface)
	{
		FString UName = GetNameCPP();
		FString IName = TEXT("I") + UName.RightChop(1);
		FError::Throwf(TEXT("UInterface '%s' parsed without a corresponding '%s'"), *UName, *IName);
	}

	FUnrealStructDefinitionInfo::PostParseFinalize();
}

void FUnrealClassDefinitionInfo::ParseClassProperties(TArray<FPropertySpecifier>&& InClassSpecifiers, const FString& InRequiredAPIMacroIfPresent)
{
	ClassFlags = CLASS_None;
	// Record that this class is RequiredAPI if the CORE_API style macro was present
	if (!InRequiredAPIMacroIfPresent.IsEmpty())
	{
		ClassFlags |= CLASS_RequiredAPI;
	}
	ClassFlags |= CLASS_Native;

	// Process all of the class specifiers

	for (FPropertySpecifier& PropSpecifier : InClassSpecifiers)
	{
		switch ((EClassMetadataSpecifier)Algo::FindSortedStringCaseInsensitive(*PropSpecifier.Key, GClassMetadataSpecifierStrings))
		{
		case EClassMetadataSpecifier::NoExport:

			// Don't export to C++ header.
			ClassFlags |= CLASS_NoExport;
			break;

		case EClassMetadataSpecifier::Intrinsic:

			ClassFlags |= CLASS_Intrinsic;
			break;

		case EClassMetadataSpecifier::ComponentWrapperClass:

			MetaData.Add(NAME_IgnoreCategoryKeywordsInSubclasses, TEXT("true"));
			break;

		case EClassMetadataSpecifier::Within:

			ClassWithin = FHeaderParser::RequireExactlyOneSpecifierValue(PropSpecifier);
			break;

		case EClassMetadataSpecifier::EditInlineNew:

			// Class can be constructed from the New button in editinline
			ClassFlags |= CLASS_EditInlineNew;
			break;

		case EClassMetadataSpecifier::NotEditInlineNew:

			// Class cannot be constructed from the New button in editinline
			ClassFlags &= ~CLASS_EditInlineNew;
			break;

		case EClassMetadataSpecifier::Placeable:

			bWantsToBePlaceable = true;
			ClassFlags &= ~CLASS_NotPlaceable;
			break;

		case EClassMetadataSpecifier::DefaultToInstanced:

			// these classed default to instanced.
			ClassFlags |= CLASS_DefaultToInstanced;
			break;

		case EClassMetadataSpecifier::NotPlaceable:

			// Don't allow the class to be placed in the editor.
			ClassFlags |= CLASS_NotPlaceable;
			break;

		case EClassMetadataSpecifier::HideDropdown:

			// Prevents class from appearing in class comboboxes in the property window
			ClassFlags |= CLASS_HideDropDown;
			break;

		case EClassMetadataSpecifier::DependsOn:

			FError::Throwf(TEXT("The dependsOn specifier is deprecated. Please use #include \"ClassHeaderFilename.h\" instead."));
			break;

		case EClassMetadataSpecifier::MinimalAPI:

			ClassFlags |= CLASS_MinimalAPI;
			break;

		case EClassMetadataSpecifier::Const:

			ClassFlags |= CLASS_Const;
			break;

		case EClassMetadataSpecifier::PerObjectConfig:

			ClassFlags |= CLASS_PerObjectConfig;
			break;

		case EClassMetadataSpecifier::ConfigDoNotCheckDefaults:

			ClassFlags |= CLASS_ConfigDoNotCheckDefaults;
			break;

		case EClassMetadataSpecifier::Abstract:

			// Hide all editable properties.
			ClassFlags |= CLASS_Abstract;
			break;

		case EClassMetadataSpecifier::Deprecated:

			ClassFlags |= CLASS_Deprecated;

			// Don't allow the class to be placed in the editor.
			ClassFlags |= CLASS_NotPlaceable;

			break;

		case EClassMetadataSpecifier::Transient:

			// Transient class.
			ClassFlags |= CLASS_Transient;
			break;

		case EClassMetadataSpecifier::NonTransient:

			// this child of a transient class is not transient - remove the transient flag
			ClassFlags &= ~CLASS_Transient;
			break;

		case EClassMetadataSpecifier::CustomConstructor:

			// we will not export a constructor for this class, assuming it is in the CPP block
			ClassFlags |= CLASS_CustomConstructor;
			break;

		case EClassMetadataSpecifier::Config:

			// Class containing config properties - parse the name of the config file to use
			ConfigName = FHeaderParser::RequireExactlyOneSpecifierValue(PropSpecifier);
			break;

		case EClassMetadataSpecifier::DefaultConfig:

			// Save object config only to Default INIs, never to local INIs.
			ClassFlags |= CLASS_DefaultConfig;
			break;

		case EClassMetadataSpecifier::GlobalUserConfig:

			// Save object config only to global user overrides, never to local INIs
			ClassFlags |= CLASS_GlobalUserConfig;
			break;

		case EClassMetadataSpecifier::ProjectUserConfig:

			// Save object config only to project user overrides, never to INIs that are checked in
			ClassFlags |= CLASS_ProjectUserConfig;
			break;

		case EClassMetadataSpecifier::ShowCategories:

			FHeaderParser::RequireSpecifierValue(PropSpecifier);

			for (FString& Value : PropSpecifier.Values)
			{
				ShowCategories.AddUnique(MoveTemp(Value));
			}
			break;

		case EClassMetadataSpecifier::HideCategories:

			FHeaderParser::RequireSpecifierValue(PropSpecifier);

			for (FString& Value : PropSpecifier.Values)
			{
				HideCategories.AddUnique(MoveTemp(Value));
			}
			break;

		case EClassMetadataSpecifier::ShowFunctions:

			FHeaderParser::RequireSpecifierValue(PropSpecifier);

			for (const FString& Value : PropSpecifier.Values)
			{
				HideFunctions.RemoveSwap(Value);
			}
			break;

		case EClassMetadataSpecifier::HideFunctions:

			FHeaderParser::RequireSpecifierValue(PropSpecifier);

			for (FString& Value : PropSpecifier.Values)
			{
				HideFunctions.AddUnique(MoveTemp(Value));
			}
			break;

			// Currently some code only handles a single sidecar data structure so we enforce that here
		case EClassMetadataSpecifier::SparseClassDataTypes:

			SparseClassDataTypes.AddUnique(FHeaderParser::RequireExactlyOneSpecifierValue(PropSpecifier));
			break;

		case EClassMetadataSpecifier::ClassGroup:

			FHeaderParser::RequireSpecifierValue(PropSpecifier);

			for (FString& Value : PropSpecifier.Values)
			{
				ClassGroupNames.Add(MoveTemp(Value));
			}
			break;

		case EClassMetadataSpecifier::AutoExpandCategories:

			FHeaderParser::RequireSpecifierValue(PropSpecifier);

			for (FString& Value : PropSpecifier.Values)
			{
				AutoCollapseCategories.RemoveSwap(Value);
				AutoExpandCategories.AddUnique(MoveTemp(Value));
			}
			break;

		case EClassMetadataSpecifier::AutoCollapseCategories:

			FHeaderParser::RequireSpecifierValue(PropSpecifier);

			for (FString& Value : PropSpecifier.Values)
			{
				AutoExpandCategories.RemoveSwap(Value);
				AutoCollapseCategories.AddUnique(MoveTemp(Value));
			}
			break;

		case EClassMetadataSpecifier::DontAutoCollapseCategories:

			FHeaderParser::RequireSpecifierValue(PropSpecifier);

			for (const FString& Value : PropSpecifier.Values)
			{
				AutoCollapseCategories.RemoveSwap(Value);
			}
			break;

		case EClassMetadataSpecifier::CollapseCategories:

			// Class' properties should not be shown categorized in the editor.
			ClassFlags |= CLASS_CollapseCategories;
			break;

		case EClassMetadataSpecifier::DontCollapseCategories:

			// Class' properties should be shown categorized in the editor.
			ClassFlags &= ~CLASS_CollapseCategories;
			break;

		case EClassMetadataSpecifier::AdvancedClassDisplay:

			// By default the class properties are shown in advanced sections in UI
			MetaData.Add(FHeaderParserNames::NAME_AdvancedClassDisplay, TEXT("true"));
			break;

		case EClassMetadataSpecifier::ConversionRoot:

			MetaData.Add(FHeaderParserNames::NAME_IsConversionRoot, TEXT("true"));
			break;

		case EClassMetadataSpecifier::NeedsDeferredDependencyLoading:

			ClassFlags |= CLASS_NeedsDeferredDependencyLoading;
			break;

		default:
			FError::Throwf(TEXT("Unknown class specifier '%s'"), *PropSpecifier.Key);
		}
	}
}

void FUnrealClassDefinitionInfo::MergeShowCategories()
{
	for (const FString& Value : ShowCategories)
	{
		// if we didn't find this specific category path in the HideCategories metadata
		if (HideCategories.RemoveSwap(Value) == 0)
		{
			TArray<FString> SubCategoryList;
			Value.ParseIntoArray(SubCategoryList, TEXT("|"), true);

			FString SubCategoryPath;
			// look to see if any of the parent paths are excluded in the HideCategories list
			for (int32 CategoryPathIndex = 0; CategoryPathIndex < SubCategoryList.Num() - 1; ++CategoryPathIndex)
			{
				SubCategoryPath += SubCategoryList[CategoryPathIndex];
				// if we're hiding a parent category, then we need to flag this sub category for show
				if (HideCategories.Contains(SubCategoryPath))
				{
					ShowSubCatgories.AddUnique(Value);
					break;
				}
				SubCategoryPath += "|";
			}
		}
	}
	// Once the categories have been merged, empty the array as we will no longer need it nor should we use it
	ShowCategories.Empty();
}

void FUnrealClassDefinitionInfo::MergeClassCategories()
{
	UClass* Class = GetClass();
	TArray<FString> ParentHideCategories;
	TArray<FString> ParentShowSubCatgories;
	TArray<FString> ParentHideFunctions;
	TArray<FString> ParentAutoExpandCategories;
	TArray<FString> ParentAutoCollapseCategories;
	GetHideCategories(ParentHideCategories);
	GetShowCategories(ParentShowSubCatgories);
	Class->GetHideFunctions(ParentHideFunctions);
	Class->GetAutoExpandCategories(ParentAutoExpandCategories);
	Class->GetAutoCollapseCategories(ParentAutoCollapseCategories);

	// Add parent categories. We store the opposite of HideCategories and HideFunctions in a separate array anyway.
	HideCategories.Append(MoveTemp(ParentHideCategories));
	ShowSubCatgories.Append(MoveTemp(ParentShowSubCatgories));
	HideFunctions.Append(MoveTemp(ParentHideFunctions));

	MergeShowCategories();

	// Merge ShowFunctions and HideFunctions
	for (const FString& Value : ShowFunctions)
	{
		HideFunctions.RemoveSwap(Value);
	}
	ShowFunctions.Empty();

	// Merge DontAutoCollapseCategories and AutoCollapseCategories
	for (const FString& Value : DontAutoCollapseCategories)
	{
		AutoCollapseCategories.RemoveSwap(Value);
	}
	DontAutoCollapseCategories.Empty();

	// Merge ShowFunctions and HideFunctions
	for (const FString& Value : ShowFunctions)
	{
		HideFunctions.RemoveSwap(Value);
	}
	ShowFunctions.Empty();

	// Merge AutoExpandCategories and AutoCollapseCategories (we still want to keep AutoExpandCategories though!)
	for (const FString& Value : AutoExpandCategories)
	{
		AutoCollapseCategories.RemoveSwap(Value);
		ParentAutoCollapseCategories.RemoveSwap(Value);
	}

	// Do the same as above but the other way around
	for (const FString& Value : AutoCollapseCategories)
	{
		AutoExpandCategories.RemoveSwap(Value);
		ParentAutoExpandCategories.RemoveSwap(Value);
	}

	// Once AutoExpandCategories and AutoCollapseCategories for THIS class have been parsed, add the parent inherited categories
	AutoCollapseCategories.Append(MoveTemp(ParentAutoCollapseCategories));
	AutoExpandCategories.Append(MoveTemp(ParentAutoExpandCategories));
}

void FUnrealClassDefinitionInfo::MergeAndValidateClassFlags(const FString& DeclaredClassName, uint32 PreviousClassFlags)
{
	UClass* Class = GetClass();
	if (bWantsToBePlaceable)
	{
		if (!(Class->ClassFlags & CLASS_NotPlaceable))
		{
			FError::Throwf(TEXT("The 'placeable' specifier is only allowed on classes which have a base class that's marked as not placeable. Classes are assumed to be placeable by default."));
		}
		Class->ClassFlags &= ~CLASS_NotPlaceable;
		bWantsToBePlaceable = false; // Reset this flag after it's been merged
	}

	// Now merge all remaining flags/properties
	Class->ClassFlags |= ClassFlags;
	Class->ClassConfigName = FName(*ConfigName);

	SetAndValidateWithinClass();
	SetAndValidateConfigName();

	if (!!(Class->ClassFlags & CLASS_EditInlineNew))
	{
		// don't allow actor classes to be declared editinlinenew
		if (IsActorClass(Class))
		{
			FError::Throwf(TEXT("Invalid class attribute: Creating actor instances via the property window is not allowed"));
		}
	}

	// Make sure both RequiredAPI and MinimalAPI aren't specified
	if (Class->HasAllClassFlags(CLASS_MinimalAPI | CLASS_RequiredAPI))
	{
		FError::Throwf(TEXT("MinimalAPI cannot be specified when the class is fully exported using a MODULENAME_API macro"));
	}

	// All classes must start with a valid Unreal prefix
	const FString ExpectedClassName = GetNameWithPrefix(GetClass());
	if (DeclaredClassName != ExpectedClassName)
	{
		FError::Throwf(TEXT("Class name '%s' is invalid, should be identified as '%s'"), *DeclaredClassName, *ExpectedClassName);
	}

	if ((Class->ClassFlags & CLASS_NoExport))
	{
		// if the class's class flags didn't contain CLASS_NoExport before it was parsed, it means either:
		// a) the DECLARE_CLASS macro for this native class doesn't contain the CLASS_NoExport flag (this is an error)
		// b) this is a new native class, which isn't yet hooked up to static registration (this is OK)
		if (!(Class->ClassFlags & CLASS_Intrinsic) && (PreviousClassFlags & CLASS_NoExport) == 0 &&
			(PreviousClassFlags & CLASS_Native) != 0)	// a new native class (one that hasn't been compiled into C++ yet) won't have this set
		{
			FError::Throwf(TEXT("'noexport': Must include CLASS_NoExport in native class declaration"));
		}
	}

	if (!Class->HasAnyClassFlags(CLASS_Abstract) && ((PreviousClassFlags & CLASS_Abstract) != 0))
	{
		if (Class->HasAnyClassFlags(CLASS_NoExport))
		{
			FError::Throwf(TEXT("'abstract': NoExport class missing abstract keyword from class declaration (must change C++ version first)"));
			Class->ClassFlags |= CLASS_Abstract;
		}
		else if (Class->IsNative())
		{
			FError::Throwf(TEXT("'abstract': missing abstract keyword from class declaration - class will no longer be exported as abstract"));
		}
	}
}

void FUnrealClassDefinitionInfo::SetAndValidateConfigName()
{
	UClass* Class = GetClass();
	if (ConfigName.IsEmpty() == false)
	{
		// if the user specified "inherit", we're just going to use the parent class's config filename
		// this is not actually necessary but it can be useful for explicitly communicating config-ness
		if (ConfigName == TEXT("inherit"))
		{
			UClass* SuperClass = Class->GetSuperClass();
			if (!SuperClass)
			{
				FError::Throwf(TEXT("Cannot inherit config filename: %s has no super class"), *Class->GetName());
			}

			if (SuperClass->ClassConfigName == NAME_None)
			{
				FError::Throwf(TEXT("Cannot inherit config filename: parent class %s is not marked config."), *SuperClass->GetPathName());
			}
		}
		else
		{
			// otherwise, set the config name to the parsed identifier
			Class->ClassConfigName = FName(*ConfigName);
		}
	}
	else
	{
		// Invalidate config name if not specifically declared.
		Class->ClassConfigName = NAME_None;
	}
}

void FUnrealClassDefinitionInfo::SetAndValidateWithinClass()
{
	UClass* Class = GetClass();

	// Process all of the class specifiers
	if (ClassWithin.IsEmpty() == false)
	{
		UClass* RequiredWithinClass = FClasses::FindClass(*ClassWithin);
		if (!RequiredWithinClass)
		{
			FError::Throwf(TEXT("Within class '%s' not found."), *ClassWithin);
		}
		if (RequiredWithinClass->IsChildOf(UInterface::StaticClass()))
		{
			FError::Throwf(TEXT("Classes cannot be 'within' interfaces"));
		}
		else if (Class->ClassWithin == NULL || Class->ClassWithin == UObject::StaticClass() || RequiredWithinClass->IsChildOf(Class->ClassWithin))
		{
			Class->ClassWithin = RequiredWithinClass;
		}
		else if (Class->ClassWithin != RequiredWithinClass)
		{
			FError::Throwf(TEXT("%s must be within %s, not %s"), *Class->GetPathName(), *Class->ClassWithin->GetPathName(), *RequiredWithinClass->GetPathName());
		}
	}
	else
	{
		// Make sure there is a valid within
		Class->ClassWithin = Class->GetSuperClass()
			? Class->GetSuperClass()->ClassWithin
			: UObject::StaticClass();
	}

	UClass* ExpectedWithin = Class->GetSuperClass()
		? Class->GetSuperClass()->ClassWithin
		: UObject::StaticClass();

	if (!Class->ClassWithin->IsChildOf(ExpectedWithin))
	{
		FError::Throwf(TEXT("Parent class declared within '%s'.  Cannot override within class with '%s' since it isn't a child"), *ExpectedWithin->GetName(), *Class->ClassWithin->GetName());
	}
}

void FUnrealClassDefinitionInfo::GetHideCategories(TArray<FString>& OutHideCategories) const
{
	UClass* Class = GetClass();
	if (Class->HasMetaData(FHeaderParserNames::NAME_HideCategories))
	{
		const FString& LocalHideCategories = Class->GetMetaData(FHeaderParserNames::NAME_HideCategories);
		LocalHideCategories.ParseIntoArray(OutHideCategories, TEXT(" "), true);
	}
}

void FUnrealClassDefinitionInfo::GetShowCategories(TArray<FString>& OutShowCategories) const
{
	UClass* Class = GetClass();
	if (Class->HasMetaData(FHeaderParserNames::NAME_ShowCategories))
	{
		const FString& LocalShowCategories = Class->GetMetaData(FHeaderParserNames::NAME_ShowCategories);
		LocalShowCategories.ParseIntoArray(OutShowCategories, TEXT(" "), true);
	}
}

void FUnrealClassDefinitionInfo::MergeCategoryMetaData(TMap<FName, FString>& InMetaData) const
{
	if (ClassGroupNames.Num()) { InMetaData.Add(NAME_ClassGroupNames, FString::Join(ClassGroupNames, TEXT(" "))); }
	if (AutoCollapseCategories.Num()) { InMetaData.Add(FHeaderParserNames::NAME_AutoCollapseCategories, FString::Join(AutoCollapseCategories, TEXT(" "))); }
	if (HideCategories.Num()) { InMetaData.Add(FHeaderParserNames::NAME_HideCategories, FString::Join(HideCategories, TEXT(" "))); }
	if (ShowSubCatgories.Num()) { InMetaData.Add(FHeaderParserNames::NAME_ShowCategories, FString::Join(ShowSubCatgories, TEXT(" "))); }
	if (SparseClassDataTypes.Num()) { InMetaData.Add(FHeaderParserNames::NAME_SparseClassDataTypes, FString::Join(SparseClassDataTypes, TEXT(" "))); }
	if (HideFunctions.Num()) { InMetaData.Add(FHeaderParserNames::NAME_HideFunctions, FString::Join(HideFunctions, TEXT(" "))); }
	if (AutoExpandCategories.Num()) { InMetaData.Add(FHeaderParserNames::NAME_AutoExpandCategories, FString::Join(AutoExpandCategories, TEXT(" "))); }
}

void FUnrealClassDefinitionInfo::GetSparseClassDataTypes(TArray<FString>& OutSparseClassDataTypes) const
{
	UClass* Class = GetClass();
	if (Class->HasMetaData(FHeaderParserNames::NAME_SparseClassDataTypes))
	{
		const FString& LocalSparseClassDataTypes = Class->GetMetaData(FHeaderParserNames::NAME_SparseClassDataTypes);
		LocalSparseClassDataTypes.ParseIntoArray(OutSparseClassDataTypes, TEXT(" "), true);
	}
}

void FUnrealFunctionDefinitionInfo::AddProperty(FUnrealPropertyDefinitionInfo& PropertyDef)
{
	const FProperty* Prop = PropertyDef.GetProperty();
	check((Prop->PropertyFlags & CPF_Parm) != 0);

	if ((Prop->PropertyFlags & CPF_ReturnParm) != 0)
	{
		check(ReturnProperty == nullptr);
		ReturnProperty = &PropertyDef;
	}
	FUnrealStructDefinitionInfo::AddProperty(PropertyDef);
}
