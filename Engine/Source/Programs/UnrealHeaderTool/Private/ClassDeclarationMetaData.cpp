// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClassDeclarationMetaData.h"
#include "UnrealHeaderTool.h"
#include "UObject/ErrorException.h"
#include "UObject/Object.h"
#include "UObject/Interface.h"
#include "BaseParser.h"
#include "HeaderParser.h"
#include "Classes.h"
#include "Specifiers/ClassMetadataSpecifiers.h"
#include "Algo/FindSortedStringCaseInsensitive.h"

// Utility functions
namespace
{
	static const FName NAME_IgnoreCategoryKeywordsInSubclasses(TEXT("IgnoreCategoryKeywordsInSubclasses"));
	bool IsActorClass(UClass *Class)
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
}

FClassDeclarationMetaData::FClassDeclarationMetaData()
	: ClassFlags(CLASS_None)
	, WantsToBePlaceable(false)
{
}

void FClassDeclarationMetaData::ParseClassProperties(TArray<FPropertySpecifier>&& InClassSpecifiers, const FString& InRequiredAPIMacroIfPresent)
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

				WantsToBePlaceable = true;
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
				ClassFlags |= CLASS_AdvancedDisplay;
				break;

			case EClassMetadataSpecifier::ConversionRoot:

				MetaData.Add(FHeaderParserNames::NAME_IsConversionRoot, TEXT("true"));
				break;

			default:
				FError::Throwf(TEXT("Unknown class specifier '%s'"), *PropSpecifier.Key);
		}
	}
}

void FClassDeclarationMetaData::MergeShowCategories()
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

void FClassDeclarationMetaData::MergeClassCategories(FClass* Class)
{
	TArray<FString> ParentHideCategories;
	TArray<FString> ParentShowSubCatgories;
	TArray<FString> ParentHideFunctions;
	TArray<FString> ParentAutoExpandCategories;
	TArray<FString> ParentAutoCollapseCategories;
	Class->GetHideCategories(ParentHideCategories);
	Class->GetShowCategories(ParentShowSubCatgories);
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

void FClassDeclarationMetaData::MergeAndValidateClassFlags(const FString& DeclaredClassName, uint32 PreviousClassFlags, FClass* Class, const FClasses& AllClasses)
{
	if (WantsToBePlaceable)
	{
		if (!(Class->ClassFlags & CLASS_NotPlaceable))
		{
			FError::Throwf(TEXT("The 'placeable' specifier is only allowed on classes which have a base class that's marked as not placeable. Classes are assumed to be placeable by default."));
		}
		Class->ClassFlags &= ~CLASS_NotPlaceable;
		WantsToBePlaceable = false; // Reset this flag after it's been merged
	}
	
	// Now merge all remaining flags/properties
	Class->ClassFlags |= ClassFlags;
	Class->ClassConfigName = FName(*ConfigName);

	SetAndValidateWithinClass(Class, AllClasses);
	SetAndValidateConfigName(Class);

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
	const FString ExpectedClassName = Class->GetNameWithPrefix();
	if (DeclaredClassName != ExpectedClassName)
	{
		FError::Throwf(TEXT("Class name '%s' is invalid, should be identified as '%s'"), *DeclaredClassName, *ExpectedClassName);
	}

	if ((Class->ClassFlags&CLASS_NoExport))
	{
		// if the class's class flags didn't contain CLASS_NoExport before it was parsed, it means either:
		// a) the DECLARE_CLASS macro for this native class doesn't contain the CLASS_NoExport flag (this is an error)
		// b) this is a new native class, which isn't yet hooked up to static registration (this is OK)
		if (!(Class->ClassFlags&CLASS_Intrinsic) && (PreviousClassFlags & CLASS_NoExport) == 0 &&
			(PreviousClassFlags&CLASS_Native) != 0)	// a new native class (one that hasn't been compiled into C++ yet) won't have this set
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

void FClassDeclarationMetaData::SetAndValidateConfigName(FClass* Class)
{
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

void FClassDeclarationMetaData::SetAndValidateWithinClass(FClass* Class, const FClasses& AllClasses)
{
	// Process all of the class specifiers
	if (ClassWithin.IsEmpty() == false)
	{
		UClass* RequiredWithinClass = AllClasses.FindClass(*ClassWithin);
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
