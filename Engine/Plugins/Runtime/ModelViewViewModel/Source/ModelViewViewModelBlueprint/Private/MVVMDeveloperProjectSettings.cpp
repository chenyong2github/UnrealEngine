// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVMDeveloperProjectSettings.h"
#include "Engine/Blueprint.h"

#define LOCTEXT_NAMESPACE "MVVMDeveloperProjectSettings"

UMVVMDeveloperProjectSettings::UMVVMDeveloperProjectSettings()
{
	AllowedExecutionMode.Add(EMVVMExecutionMode::Immediate);
	AllowedExecutionMode.Add(EMVVMExecutionMode::Delayed);
	AllowedExecutionMode.Add(EMVVMExecutionMode::Tick);
}

FName UMVVMDeveloperProjectSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FText UMVVMDeveloperProjectSettings::GetSectionText() const
{
	return LOCTEXT("MVVMProjectSettings", "Model View Viewmodel");
}

bool UMVVMDeveloperProjectSettings::IsPropertyAllowed(const FProperty* Property) const
{
	check(Property);
	TStringBuilder<256> StringBuilder;
	Property->GetOwnerClass()->GetPathName(nullptr, StringBuilder);
	FSoftClassPath StructPath;
	StructPath.SetPath(StringBuilder);
	if (const FMVVMDeveloperProjectWidgetSettings* Settings = FieldSelectorPermissions.Find(StructPath))
	{
		return !Settings->DisallowedFieldNames.Find(Property->GetFName());
	}
	return true;
}

bool UMVVMDeveloperProjectSettings::IsFunctionAllowed(const UFunction* Function) const
{
	check(Function);
	TStringBuilder<256> StringBuilder;
	Function->GetOwnerClass()->GetPathName(nullptr, StringBuilder);
	FSoftClassPath StructPath;
	StructPath.SetPath(StringBuilder);
	if (const FMVVMDeveloperProjectWidgetSettings* Settings = FieldSelectorPermissions.Find(StructPath))
	{
		return !Settings->DisallowedFieldNames.Find(Function->GetFName());
	}
	return true;
}

TArray<const UClass*> UMVVMDeveloperProjectSettings::GetAllowedConversionFunctionClasses() const
{
	TArray<const UClass*> Result;
	for (const FSoftClassPath& SoftClass : AllowedClassForConversionFunctions)
	{
		if (UClass* Class = SoftClass.ResolveClass())
		{
			Result.Add(Class);
		}
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
