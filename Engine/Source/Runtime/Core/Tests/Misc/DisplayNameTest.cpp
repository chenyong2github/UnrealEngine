// Copyright Epic Games, Inc. All Rights Reserved.
#include "UObject/UnrealNames.h"
#include "TestHarness.h"

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Misc::FName::Display Name", "[Core][Misc][Smoke]")
{
	TEST_EQUAL(TEXT("Boolean"), FName::NameToDisplayString(TEXT("bTest"), true), TEXT("Test"));
	TEST_EQUAL(TEXT("Boolean Lower"), FName::NameToDisplayString(TEXT("bTwoWords"), true), TEXT("Two Words"));
	TEST_EQUAL(TEXT("Lower Boolean"), FName::NameToDisplayString(TEXT("boolean"), true), TEXT("Boolean"));
	TEST_EQUAL(TEXT("Almost Boolean"), FName::NameToDisplayString(TEXT("bNotBoolean"), false), TEXT("B Not Boolean"));
	TEST_EQUAL(TEXT("Boolean No Prefix"), FName::NameToDisplayString(TEXT("NonprefixBoolean"), true), TEXT("Nonprefix Boolean"));
	TEST_EQUAL(TEXT("Lower Boolean No Prefix"), FName::NameToDisplayString(TEXT("lowerNonprefixBoolean"), true), TEXT("Lower Nonprefix Boolean"));
	TEST_EQUAL(TEXT("Lower Camel Case"), FName::NameToDisplayString(TEXT("lowerCase"), false), TEXT("Lower Case"));
	TEST_EQUAL(TEXT("With Underscores"), FName::NameToDisplayString(TEXT("With_Underscores"), false), TEXT("With Underscores"));
	TEST_EQUAL(TEXT("Lower Underscores"), FName::NameToDisplayString(TEXT("lower_underscores"), false), TEXT("Lower Underscores"));
	TEST_EQUAL(TEXT("Mixed Underscores"), FName::NameToDisplayString(TEXT("mixed_Underscores"), false), TEXT("Mixed Underscores"));
	TEST_EQUAL(TEXT("Mixed Underscores"), FName::NameToDisplayString(TEXT("Mixed_underscores"), false), TEXT("Mixed Underscores"));
	TEST_EQUAL(TEXT("Article in String"), FName::NameToDisplayString(TEXT("ArticleInString"), false), TEXT("Article in String"));
	TEST_EQUAL(TEXT("One or Two"), FName::NameToDisplayString(TEXT("OneOrTwo"), false), TEXT("One or Two"));
	TEST_EQUAL(TEXT("One and Two"), FName::NameToDisplayString(TEXT("OneAndTwo"), false), TEXT("One and Two"));
	TEST_EQUAL(TEXT("-1.5"), FName::NameToDisplayString(TEXT("-1.5"), false), TEXT("-1.5"));
	TEST_EQUAL(TEXT("1234"), FName::NameToDisplayString(TEXT("1234"), false), TEXT("1234"));
	TEST_EQUAL(TEXT("1234.5"), FName::NameToDisplayString(TEXT("1234.5"), false), TEXT("1234.5"));
	TEST_EQUAL(TEXT("-1234.5"), FName::NameToDisplayString(TEXT("-1234.5"), false), TEXT("-1234.5"));
	TEST_EQUAL(TEXT("Text (In Parens)"), FName::NameToDisplayString(TEXT("Text (in parens)"), false), TEXT("Text (In Parens)"));

	TEST_EQUAL(TEXT("Text 3D"), FName::NameToDisplayString(TEXT("Text3D"), false), TEXT("Text 3D"));
	TEST_EQUAL(TEXT("Plural CAPs"), FName::NameToDisplayString(TEXT("PluralCAPs"), false), TEXT("Plural CAPs"));
	TEST_EQUAL(TEXT("FBXEditor"), FName::NameToDisplayString(TEXT("FBXEditor"), false), TEXT("FBXEditor"));
	TEST_EQUAL(TEXT("FBX_Editor"), FName::NameToDisplayString(TEXT("FBX_Editor"), false), TEXT("FBX Editor"));
}