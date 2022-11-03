// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorUtilityAssetPrototype.h"
#include "EditorUtilityBlueprint.h"
#include "JsonObjectConverter.h"
#include "AssetRegistry/AssetData.h"
#include "Serialization/JsonSerializerMacros.h"

const FName AssetActionUtilityTags::BlutilityTagVersion(TEXT("BlutilityTagVersion"));
const FName AssetActionUtilityTags::SupportedClasses(TEXT("SupportedClasses"));
const FName AssetActionUtilityTags::IsActionForBlueprint(TEXT("IsActionForBlueprint"));
const FName AssetActionUtilityTags::CallableFunctions(TEXT("CallableFunctions"));

const int32 AssetActionUtilityTags::TagVersion = 1;

UObject* FAssetActionUtilityPrototype::LoadUtilityAsset() const
{
	if (const UEditorUtilityBlueprint* Blueprint = Cast<UEditorUtilityBlueprint>(UtilityBlueprintAsset.GetAsset()))
	{
		if (const UClass* BPClass = Blueprint->GeneratedClass.Get())
		{
			return BPClass->GetDefaultObject();
		}
	}

	return nullptr;
};

bool FAssetActionUtilityPrototype::IsLatestVersion() const
{
	int32 Version;
    if (UtilityBlueprintAsset.GetTagValue(AssetActionUtilityTags::BlutilityTagVersion, Version))
    {
    	return Version == AssetActionUtilityTags::TagVersion;
    }
    
    return false;
}

bool FAssetActionUtilityPrototype::AreSupportedClassesForBlueprints() const
{
	FString IsActionForBlueprintString;
	if (UtilityBlueprintAsset.GetTagValue(AssetActionUtilityTags::IsActionForBlueprint, IsActionForBlueprintString))
	{
		bool IsActionForBlueprint = false;
		LexFromString(IsActionForBlueprint, *IsActionForBlueprintString);
		return IsActionForBlueprint;
	}

	return false;
}

TArray<TSoftClassPtr<UObject>> FAssetActionUtilityPrototype::GetSupportedClasses() const
{
	FString SupportedClassPaths;
	if (UtilityBlueprintAsset.GetTagValue(AssetActionUtilityTags::SupportedClasses, SupportedClassPaths))
	{
		TArray<FString> ClassPaths;
		SupportedClassPaths.ParseIntoArray(ClassPaths, TEXT(","));
		TArray<TSoftClassPtr<UObject>> ClassPtrs;
		Algo::Transform(ClassPaths, ClassPtrs, [](const FString& ClassPath) { return TSoftClassPtr<UObject>(FSoftClassPath(ClassPath)); });
		return ClassPtrs;
	}

	return TArray<TSoftClassPtr<UObject>>();
}

TArray<FBlutilityFunctionData> FAssetActionUtilityPrototype::GetCallableFunctions() const
{
	TArray<FBlutilityFunctionData> FunctionDatas;
	
	FString FunctionDataJson;
	if (UtilityBlueprintAsset.GetTagValue(AssetActionUtilityTags::CallableFunctions, FunctionDataJson))
	{
		FJsonObjectConverter::JsonArrayStringToUStruct(FunctionDataJson, &FunctionDatas, 0, 0);
	}

	return FunctionDatas;
}

void FAssetActionUtilityPrototype::AddTagsFor_Version(TArray<UObject::FAssetRegistryTag>& OutTags)
{
	// Adding a version to the tags just in case we need to know 'this blutility is out of date and we can't go based off the cached data because the format isn't followed, or this is a pre-tagged version'
	OutTags.Add(UObject::FAssetRegistryTag(AssetActionUtilityTags::BlutilityTagVersion, LexToString(AssetActionUtilityTags::TagVersion), UObject::FAssetRegistryTag::TT_Hidden));	
}

void FAssetActionUtilityPrototype::AddTagsFor_SupportedClasses(const TArray<TSoftClassPtr<UObject>>& SupportedClasses, TArray<UObject::FAssetRegistryTag>& OutTags)
{
	const FString SupportedClassesString = FString::JoinBy(SupportedClasses, TEXT(","), [](const TSoftClassPtr<UObject>& SupportedClass) { return SupportedClass.ToString(); });
	OutTags.Add(UObject::FAssetRegistryTag(AssetActionUtilityTags::SupportedClasses, SupportedClassesString, UObject::FAssetRegistryTag::TT_Hidden));
}

void FAssetActionUtilityPrototype::AddTagsFor_IsActionForBlueprints(bool IsActionForBlueprints, TArray<UObject::FAssetRegistryTag>& OutTags)
{
	const bool ActionForBlueprint = IsActionForBlueprints;
	OutTags.Add(UObject::FAssetRegistryTag(AssetActionUtilityTags::IsActionForBlueprint, ActionForBlueprint ? TEXT("True") : TEXT("False"), UObject::FAssetRegistryTag::TT_Hidden));
}

void FAssetActionUtilityPrototype::AddTagsFor_CallableFunctions(const UObject* FunctionsSource, TArray<UObject::FAssetRegistryTag>& OutTags)
{
	check(FunctionsSource);
	
	const static FName NAME_CallInEditor(TEXT("CallInEditor"));
	const static FName NAME_Category(TEXT("Category"));

	TArray<FBlutilityFunctionData> FunctionDatas;
	for (TFieldIterator<UFunction> FunctionIt(FunctionsSource->GetClass(), EFieldIterationFlags::IncludeSuper); FunctionIt; ++FunctionIt)
	{
		if (UFunction* Func = *FunctionIt)
		{
			if (Func->HasMetaData(NAME_CallInEditor) && Func->GetReturnProperty() == nullptr)
			{
				FBlutilityFunctionData Function;
				Function.Class = Func->GetOuterUClass();
				Function.Name = Func->GetFName();
				Function.NameText = Func->GetDisplayNameText();
				Function.Category = Func->GetMetaData(NAME_Category);
				Function.TooltipText = Func->GetToolTipText();
				
				FunctionDatas.Add(MoveTemp(Function));
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> FunctionDataJsonValues;
	for (const FBlutilityFunctionData& Highlight : FunctionDatas)
	{
		TSharedPtr<FJsonObject> FunctionDataJO = FJsonObjectConverter::UStructToJsonObject(Highlight);
		if (FunctionDataJO.IsValid())
		{
			FunctionDataJsonValues.Add(MakeShared<FJsonValueObject>(FunctionDataJO));
		}
	}

	if (FunctionDataJsonValues.Num() > 0)
	{
		FString FunctionDataJson;
		{
			TSharedRef<FJsonValueArray> HighlightValuesArrayValue = MakeShared<FJsonValueArray>(FunctionDataJsonValues);
			TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&FunctionDataJson);
			FJsonSerializer::Serialize(HighlightValuesArrayValue, FString(), JsonWriter);
		}

		OutTags.Add(UObject::FAssetRegistryTag(AssetActionUtilityTags::CallableFunctions, FunctionDataJson, UObject::FAssetRegistryTag::TT_Hidden));
	}
}