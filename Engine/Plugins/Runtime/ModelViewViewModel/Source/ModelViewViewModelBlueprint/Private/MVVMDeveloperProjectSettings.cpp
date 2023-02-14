// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVMDeveloperProjectSettings.h"
#include "Engine/Blueprint.h"

#include "BlueprintEditorSettings.h"
#include "MVVMBlueprintViewModelContext.h"
#include "PropertyEditorPermissionList.h"
#include "Types/MVVMExecutionMode.h"

#define LOCTEXT_NAMESPACE "MVVMDeveloperProjectSettings"

UMVVMDeveloperProjectSettings::UMVVMDeveloperProjectSettings()
{
	AllowedExecutionMode.Add(EMVVMExecutionMode::Immediate);
	AllowedExecutionMode.Add(EMVVMExecutionMode::Delayed);
	AllowedExecutionMode.Add(EMVVMExecutionMode::Tick);
	
	AllowedContextCreationType.Add(EMVVMBlueprintViewModelContextCreationType::Manual);
	AllowedContextCreationType.Add(EMVVMBlueprintViewModelContextCreationType::CreateInstance);
	AllowedContextCreationType.Add(EMVVMBlueprintViewModelContextCreationType::GlobalViewModelCollection);
	AllowedContextCreationType.Add(EMVVMBlueprintViewModelContextCreationType::PropertyPath);
	AllowedContextCreationType.Add(EMVVMBlueprintViewModelContextCreationType::PropertyPath);
	AllowedContextCreationType.Add(EMVVMBlueprintViewModelContextCreationType::Resolver);
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
	if (!FPropertyEditorPermissionList::Get().DoesPropertyPassFilter(Property->GetOwnerStruct(), Property->GetFName()))
	{
		return false;
	}

	TStringBuilder<512> StringBuilder;
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

	TStringBuilder<512> StringBuilder;
	Function->GetPathName(nullptr, StringBuilder);
	if (!GetMutableDefault<UBlueprintEditorSettings>()->GetFunctionPermissions().PassesFilter(StringBuilder.ToView()))
	{
		return false;
	}

	StringBuilder.Reset();
	Function->GetOwnerClass()->GetPathName(nullptr, StringBuilder);
	FSoftClassPath StructPath;
	StructPath.SetPath(StringBuilder);
	if (const FMVVMDeveloperProjectWidgetSettings* Settings = FieldSelectorPermissions.Find(StructPath))
	{
		return !Settings->DisallowedFieldNames.Find(Function->GetFName());
	}
	return true;
}

bool UMVVMDeveloperProjectSettings::IsConversionFunctionAllowed(const UFunction* Function) const
{
	if (ConversionFunctionFilter == EMVVMDeveloperConversionFunctionFilterType::AllowedList)
	{
		return IsFunctionAllowed(Function);
	}
	else
	{
		TStringBuilder<512> FunctionClassPath;
		Function->GetOwnerClass()->GetPathName(nullptr, FunctionClassPath);
		TStringBuilder<512> AllowedClassPath;
		for (const FSoftClassPath& SoftClass : AllowedClassForConversionFunctions)
		{
			SoftClass.ToString(AllowedClassPath);
			if (AllowedClassPath.ToView() == FunctionClassPath.ToView())
			{
				return true;
			}
			AllowedClassPath.Reset();
		}
		return false;
	}
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
