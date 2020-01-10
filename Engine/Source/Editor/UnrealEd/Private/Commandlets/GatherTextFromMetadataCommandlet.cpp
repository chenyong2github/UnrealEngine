// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Commandlets/GatherTextFromMetadataCommandlet.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "UObject/PropertyIterator.h"
#include "SourceCodeNavigation.h"

DEFINE_LOG_CATEGORY_STATIC(LogGatherTextFromMetaDataCommandlet, Log, All);

//////////////////////////////////////////////////////////////////////////
//GatherTextFromMetaDataCommandlet

UGatherTextFromMetaDataCommandlet::UGatherTextFromMetaDataCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int32 UGatherTextFromMetaDataCommandlet::Main( const FString& Params )
{
	// Parse command line - we're interested in the param vals
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamVals;
	UCommandlet::ParseCommandLine(*Params, Tokens, Switches, ParamVals);

	//Set config file
	const FString* ParamVal = ParamVals.Find(FString(TEXT("Config")));
	FString GatherTextConfigPath;
	
	if ( ParamVal )
	{
		GatherTextConfigPath = *ParamVal;
	}
	else
	{
		UE_LOG(LogGatherTextFromMetaDataCommandlet, Error, TEXT("No config specified."));
		return -1;
	}

	//Set config section
	ParamVal = ParamVals.Find(FString(TEXT("Section")));
	FString SectionName;

	if ( ParamVal )
	{
		SectionName = *ParamVal;
	}
	else
	{
		UE_LOG(LogGatherTextFromMetaDataCommandlet, Error, TEXT("No config section specified."));
		return -1;
	}

	//Modules to Preload
	TArray<FString> ModulesToPreload;
	GetStringArrayFromConfig(*SectionName, TEXT("ModulesToPreload"), ModulesToPreload, GatherTextConfigPath);

	for (const FString& ModuleName : ModulesToPreload)
	{
		FModuleManager::Get().LoadModule(*ModuleName);
	}

	// IncludePathFilters
	TArray<FString> IncludePathFilters;
	GetPathArrayFromConfig(*SectionName, TEXT("IncludePathFilters"), IncludePathFilters, GatherTextConfigPath);

	// IncludePaths (DEPRECATED)
	{
		TArray<FString> IncludePaths;
		GetPathArrayFromConfig(*SectionName, TEXT("IncludePaths"), IncludePaths, GatherTextConfigPath);
		if (IncludePaths.Num())
		{
			IncludePathFilters.Append(IncludePaths);
			UE_LOG(LogGatherTextFromMetaDataCommandlet, Warning, TEXT("IncludePaths detected in section %s. IncludePaths is deprecated, please use IncludePathFilters."), *SectionName);
		}
	}

	if (IncludePathFilters.Num() == 0)
	{
		UE_LOG(LogGatherTextFromMetaDataCommandlet, Error, TEXT("No include path filters in section %s."), *SectionName);
		return -1;
	}

	// ExcludePathFilters
	TArray<FString> ExcludePathFilters;
	GetPathArrayFromConfig(*SectionName, TEXT("ExcludePathFilters"), ExcludePathFilters, GatherTextConfigPath);

	// ExcludePaths (DEPRECATED)
	{
		TArray<FString> ExcludePaths;
		GetPathArrayFromConfig(*SectionName, TEXT("ExcludePaths"), ExcludePaths, GatherTextConfigPath);
		if (ExcludePaths.Num())
		{
			ExcludePathFilters.Append(ExcludePaths);
			UE_LOG(LogGatherTextFromMetaDataCommandlet, Warning, TEXT("ExcludePaths detected in section %s. ExcludePaths is deprecated, please use ExcludePathFilters."), *SectionName);
		}
	}

	// Get whether we should gather editor-only data. Typically only useful for the localization of UE4 itself.
	if (!GetBoolFromConfig(*SectionName, TEXT("ShouldGatherFromEditorOnlyData"), ShouldGatherFromEditorOnlyData, GatherTextConfigPath))
	{
		ShouldGatherFromEditorOnlyData = false;
	}

	// FieldTypesToInclude/FieldTypesToExclude
	{
		auto GetFieldTypesArrayFromConfig = [this, &SectionName, &GatherTextConfigPath](const TCHAR* InConfigKey, TArray<const UClass*>& OutFieldTypes)
		{
			TArray<FString> FieldTypeStrs;
			GetStringArrayFromConfig(*SectionName, InConfigKey, FieldTypeStrs, GatherTextConfigPath);

			TArray<const UClass*> AllFieldTypes;
			AllFieldTypes.Add(UField::StaticClass());
			GetDerivedClasses(UField::StaticClass(), (TArray<UClass*>&)AllFieldTypes);

			for (const FString& FieldTypeStr : FieldTypeStrs)
			{
				const bool bIsWildcard = FieldTypeStr.GetCharArray().Contains(TEXT('*')) || FieldTypeStr.GetCharArray().Contains(TEXT('?'));
				if (bIsWildcard)
				{
					for (const UClass* FieldType : AllFieldTypes)
					{
						if (FieldType->GetName().MatchesWildcard(FieldTypeStr))
						{
							OutFieldTypes.Add(FieldType);
						}
					}
				}
				else
				{
					const UClass* FieldType = FindObject<UClass>(ANY_PACKAGE, *FieldTypeStr);
					if (!FieldType)
					{
						UE_LOG(LogGatherTextFromMetaDataCommandlet, Warning, TEXT("Field Type %s was not found (from %s in section %s). Did you forget a ModulesToPreload entry?"), *FieldTypeStr, InConfigKey, *SectionName);
						continue;
					}

					check(FieldType->IsChildOf<UField>());
					OutFieldTypes.Add(FieldType);
					GetDerivedClasses(FieldType, (TArray<UClass*>&)OutFieldTypes);
				}
			}
		};

		GetFieldTypesArrayFromConfig(TEXT("FieldTypesToInclude"), FieldTypesToInclude);
		GetFieldTypesArrayFromConfig(TEXT("FieldTypesToExclude"), FieldTypesToExclude);
	}

	// FieldOwnerTypesToInclude/FieldOwnerTypesToExclude
	{
		auto GetFieldOwnerTypesArrayFromConfig = [this, &SectionName, &GatherTextConfigPath](const TCHAR* InConfigKey, TArray<const UStruct*>& OutFieldOwnerTypes)
		{
			TArray<FString> FieldOwnerTypeStrs;
			GetStringArrayFromConfig(*SectionName, InConfigKey, FieldOwnerTypeStrs, GatherTextConfigPath);

			TArray<const UStruct*> AllFieldOwnerTypes;
			GetObjectsOfClass(UClass::StaticClass(), (TArray<UObject*>&)AllFieldOwnerTypes, false);
			GetObjectsOfClass(UScriptStruct::StaticClass(), (TArray<UObject*>&)AllFieldOwnerTypes, false);

			for (const FString& FieldOwnerTypeStr : FieldOwnerTypeStrs)
			{
				const bool bIsWildcard = FieldOwnerTypeStr.GetCharArray().Contains(TEXT('*')) || FieldOwnerTypeStr.GetCharArray().Contains(TEXT('?'));
				if (bIsWildcard)
				{
					for (const UStruct* FieldOwnerType : AllFieldOwnerTypes)
					{
						if (FieldOwnerType->GetName().MatchesWildcard(FieldOwnerTypeStr))
						{
							OutFieldOwnerTypes.Add(FieldOwnerType);
						}
					}
				}
				else
				{
					const UStruct* FieldOwnerType = FindObject<UStruct>(ANY_PACKAGE, *FieldOwnerTypeStr);
					if (!FieldOwnerType)
					{
						UE_LOG(LogGatherTextFromMetaDataCommandlet, Warning, TEXT("Field Owner Type %s was not found (from %s in section %s). Did you forget a ModulesToPreload entry?"), *FieldOwnerTypeStr, InConfigKey, *SectionName);
						continue;
					}

					OutFieldOwnerTypes.Add(FieldOwnerType);
					if (const UClass* FieldOwnerClass = Cast<UClass>(FieldOwnerType))
					{
						GetDerivedClasses(FieldOwnerClass, (TArray<UClass*>&)OutFieldOwnerTypes);
					}
				}
			}
		};

		GetFieldOwnerTypesArrayFromConfig(TEXT("FieldOwnerTypesToInclude"), FieldOwnerTypesToInclude);
		GetFieldOwnerTypesArrayFromConfig(TEXT("FieldOwnerTypesToExclude"), FieldOwnerTypesToExclude);
	}

	FGatherParameters Arguments;
	GetStringArrayFromConfig(*SectionName, TEXT("InputKeys"), Arguments.InputKeys, GatherTextConfigPath);
	GetStringArrayFromConfig(*SectionName, TEXT("OutputNamespaces"), Arguments.OutputNamespaces, GatherTextConfigPath);
	TArray<FString> OutputKeys;
	GetStringArrayFromConfig(*SectionName, TEXT("OutputKeys"), OutputKeys, GatherTextConfigPath);
	for(const auto& OutputKey : OutputKeys)
	{
		Arguments.OutputKeys.Add(FText::FromString(OutputKey));
	}

	// Execute gather.
	GatherTextFromUObjects(IncludePathFilters, ExcludePathFilters, Arguments);

	// Add any manifest dependencies if they were provided
	TArray<FString> ManifestDependenciesList;
	GetPathArrayFromConfig(*SectionName, TEXT("ManifestDependencies"), ManifestDependenciesList, GatherTextConfigPath);

	for (const FString& ManifestDependency : ManifestDependenciesList)
	{
		FText OutError;
		if (!GatherManifestHelper->AddDependency(ManifestDependency, &OutError))
		{
			UE_LOG(LogGatherTextFromMetaDataCommandlet, Error, TEXT("The GatherTextFromMetaData commandlet couldn't load the specified manifest dependency: '%'. %s"), *ManifestDependency, *OutError.ToString());
			return -1;
		}
	}

	return 0;
}

void UGatherTextFromMetaDataCommandlet::GatherTextFromUObjects(const TArray<FString>& IncludePaths, const TArray<FString>& ExcludePaths, const FGatherParameters& Arguments)
{
	const FFuzzyPathMatcher FuzzyPathMatcher = FFuzzyPathMatcher(IncludePaths, ExcludePaths);

	for(TObjectIterator<UField> It; It; ++It)
	{
		// Skip fields excluded by our filter
		if (!ShouldGatherFromField(*It))
		{
			continue;
		}

		FString SourceFilePath;
		FSourceCodeNavigation::FindClassHeaderPath(*It, SourceFilePath);
		SourceFilePath = FPaths::ConvertRelativePathToFull(SourceFilePath);
		check(!SourceFilePath.IsEmpty());

		const FFuzzyPathMatcher::EPathMatch PathMatch = FuzzyPathMatcher.TestPath(SourceFilePath);
		if (PathMatch != FFuzzyPathMatcher::Included)
		{
			continue;
		}

		const FName MetaDataPlatformName = GetSplitPlatformNameFromPath(SourceFilePath);

		UStruct* Struct = Cast<UStruct>(*It);
		if (Struct)
		{
			for (TFieldIterator<FField> FieldIt(Struct); FieldIt; ++FieldIt)
			{
				// Skip editor-only properties if we're not gathering for editor-only data.
				FProperty* Property = CastField<FProperty>(*FieldIt);
				if (Property && !ShouldGatherFromEditorOnlyData && Property->HasAnyPropertyFlags(CPF_EditorOnly))
				{
					continue;
				}
				GatherTextFromField(*FieldIt, Arguments, MetaDataPlatformName);
			}
		}

		GatherTextFromUObject(*It, Arguments, MetaDataPlatformName);
	}
}

template <typename TFieldType>
void GatherTextFromFieldImplementation(TFieldType* const Field, const UGatherTextFromMetaDataCommandlet::FGatherParameters& Arguments, const FName InPlatformName, FLocTextHelper* GatherManifestHelper)
{
	for (int32 i = 0; i < Arguments.InputKeys.Num(); ++i)
	{
		FFormatNamedArguments PatternArguments;
		PatternArguments.Add(TEXT("FieldPath"), FText::FromString(Field->GetFullGroupName(false)));

		if (Field->HasMetaData(*Arguments.InputKeys[i]))
		{
			const FString& MetaDataValue = Field->GetMetaData(*Arguments.InputKeys[i]);
			if (!MetaDataValue.IsEmpty())
			{
				PatternArguments.Add(TEXT("MetaDataValue"), FText::FromString(MetaDataValue));

				const UStruct* FieldOwnerType = Field->GetOwnerStruct();
				const FString Namespace = Arguments.OutputNamespaces[i];
				FLocItem LocItem(MetaDataValue);
				FManifestContext Context;
				Context.Key = FText::Format(Arguments.OutputKeys[i], PatternArguments).ToString();
				Context.SourceLocation = FString::Printf(TEXT("From metadata for key %s of member %s in %s (type: %s, owner: %s)"), *Arguments.InputKeys[i], *Field->GetName(), *Field->GetFullGroupName(true), *Field->GetClass()->GetName(), FieldOwnerType ? *FieldOwnerType->GetName() : TEXT("<null>"));
				Context.PlatformName = InPlatformName;
				GatherManifestHelper->AddSourceText(Namespace, LocItem, Context);
			}
		}
	}
}


void UGatherTextFromMetaDataCommandlet::GatherTextFromUObject(UField* const Field, const FGatherParameters& Arguments, const FName InPlatformName)
{
	// Gather for object.
	{
		if (!Field->HasMetaData(TEXT("DisplayName")))
		{
			Field->SetMetaData(TEXT("DisplayName"), *FName::NameToDisplayString(Field->GetName(), false));
		}

		GatherTextFromFieldImplementation<UField>(Field, Arguments, InPlatformName, GatherManifestHelper.Get());
	}

	// For enums, also gather for enum values.
	{
		UEnum* Enum = Cast<UEnum>(Field);
		if (Enum)
		{
			const int32 ValueCount = Enum->NumEnums();
			for (int32 i = 0; i < ValueCount; ++i)
			{
				if (!Enum->HasMetaData(TEXT("DisplayName"), i))
				{
					Enum->SetMetaData(TEXT("DisplayName"), *FName::NameToDisplayString(Enum->GetNameStringByIndex(i), false), i);
				}

				for (int32 j = 0; j < Arguments.InputKeys.Num(); ++j)
				{
					FFormatNamedArguments PatternArguments;
					PatternArguments.Add(TEXT("FieldPath"), FText::FromString(Enum->GetFullGroupName(false) + TEXT(".") + Enum->GetNameStringByIndex(i)));

					if (Enum->HasMetaData(*Arguments.InputKeys[j], i))
					{
						const FString& MetaDataValue = Enum->GetMetaData(*Arguments.InputKeys[j], i);
						if (!MetaDataValue.IsEmpty())
						{
							PatternArguments.Add(TEXT("MetaDataValue"), FText::FromString(MetaDataValue));

							const FString Namespace = Arguments.OutputNamespaces[j];
							FLocItem LocItem(MetaDataValue);
							FManifestContext Context;
							Context.Key = FText::Format(Arguments.OutputKeys[j], PatternArguments).ToString();
							Context.SourceLocation = FString::Printf(TEXT("From metadata for key %s of enum value %s of enum %s in %s"), *Arguments.InputKeys[j], *Enum->GetNameStringByIndex(i), *Enum->GetName(), *Enum->GetFullGroupName(true));
							Context.PlatformName = InPlatformName;
							GatherManifestHelper->AddSourceText(Namespace, LocItem, Context);
						}
					}
				}
			}
		}
	}
}

bool UGatherTextFromMetaDataCommandlet::ShouldGatherFromField(const UField* Field)
{
	auto ShouldGatherFieldByType = [this, Field]()
	{
		if (FieldTypesToInclude.Num() == 0 && FieldTypesToExclude.Num() == 0)
		{
			return true;
		}

		const UClass* FieldClass = Field->GetClass();
		return (FieldTypesToInclude.Num() == 0 || FieldTypesToInclude.Contains(FieldClass))
			&& (FieldTypesToExclude.Num() == 0 || !FieldTypesToExclude.Contains(FieldClass));
	};

	auto ShouldGatherFieldByOwnerType = [this, Field]()
	{
		if (FieldOwnerTypesToInclude.Num() == 0 && FieldOwnerTypesToExclude.Num() == 0)
		{
			return true;
		}

		const UStruct* FieldOwnerType = Field->GetOwnerStruct();
		if (FieldOwnerType)
		{
			// Only properties and functions will have an owner struct type
			return (FieldOwnerTypesToInclude.Num() == 0 || FieldOwnerTypesToInclude.Contains(FieldOwnerType))
				&& (FieldOwnerTypesToExclude.Num() == 0 || !FieldOwnerTypesToExclude.Contains(FieldOwnerType));
		}

		return true;
	};

	return ShouldGatherFieldByType() && ShouldGatherFieldByOwnerType();
}

void UGatherTextFromMetaDataCommandlet::GatherTextFromField(FField* const Field, const FGatherParameters& Arguments, const FName InPlatformName)
{
	if (!Field->HasMetaData(TEXT("DisplayName")))
	{
		Field->SetMetaData(TEXT("DisplayName"), *FName::NameToDisplayString(Field->GetName(), Field->IsA(FBoolProperty::StaticClass())));
	}

	GatherTextFromFieldImplementation<FField>(Field, Arguments, InPlatformName, GatherManifestHelper.Get());
}
