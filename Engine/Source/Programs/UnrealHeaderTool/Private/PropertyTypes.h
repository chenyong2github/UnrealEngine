// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HeaderParser.h" // for EVariableCategory
#include "UObject/Stack.h"

class FProperty;
class FString;
class FUnrealPropertyDefinitionInfo;
class FUnrealSourceFile;

struct FPropertyTraits
{
	/**
	 * Transforms CPP-formated string containing default value, to inner formated string
	 * If it cannot be transformed empty string is returned.
	 *
	 * @param PropDef The property that owns the default value.
	 * @param CppForm A CPP-formated string.
	 * @param out InnerForm Inner formated string
	 * @return true on success, false otherwise.
	 */
	static bool DefaultValueStringCppFormatToInnerFormat(const FUnrealPropertyDefinitionInfo& PropDef, const FString& CppForm, FString& InnerForm);

	/**
	 * Given a property definition token, create the property definition and then underlying engine FProperty
	 *
	 * @param Token The definition of the property
	 * @param Outer The parent object owning the property
	 * @param Name The name of the property
	 * @param ObjectFlags The flags associated with the property
	 * @param VariableCategory The parsing context of the property
	 * @param Dimensions When this is a static array, this represents the dimensions value
	 * @param SourceFile The source file containing the property
	 * @param LineNumber Line number of the property
	 * @param ParsePosition Character position of the property in the header
	 * @return The pointer to the newly created property.  It will be attached to the definition by the caller
	 */
	static FUnrealPropertyDefinitionInfo& CreateProperty(const FPropertyBase& VarProperty, FUnrealTypeDefinitionInfo& Outer, const FName& Name, EObjectFlags ObjectFlags, EVariableCategory::Type VariableCategory, const TCHAR* Dimensions, FUnrealSourceFile& SourceFile, int LineNumber, int ParsePosition);

	/**
	 * Test to see if the property can be used in a blueprint
	 * 
	 * @param PropDef The property in question
	 * @param bMemberVariable If true, this is a member variable being tested
	 * @return Return true if the property is supported in blueprints
	 */
	static bool IsSupportedByBlueprint(const FUnrealPropertyDefinitionInfo& PropDef, bool bMemberVariable);
};
