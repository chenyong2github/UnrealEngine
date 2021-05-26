// Copyright Epic Games, Inc. All Rights Reserved.

#include "UnrealTypeDefinitionInfo.h"
#include "BaseParser.h"
#include "ClassMaps.h"
#include "HeaderParser.h"
#include "NativeClassExporter.h"
#include "PropertyTypes.h"
#include "Scope.h"
#include "StringUtils.h"
#include "UnrealHeaderTool.h"
#include "UnrealSourceFile.h"
#include "Specifiers/CheckedMetadataSpecifiers.h"
#include "Specifiers/ClassMetadataSpecifiers.h"

#include "Algo/FindSortedStringCaseInsensitive.h"
#include "Math/UnitConversion.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "UObject/Interface.h"
#include "UObject/ObjectRedirector.h"

// Globals for common class definitions
extern FUnrealClassDefinitionInfo* GUObjectDef;
extern FUnrealClassDefinitionInfo* GUClassDef;
extern FUnrealClassDefinitionInfo* GUInterfaceDef;
extern FUnrealClassDefinitionInfo* GUPackageDef;

namespace
{
	const FName NAME_IgnoreCategoryKeywordsInSubclasses(TEXT("IgnoreCategoryKeywordsInSubclasses"));
	const FName NAME_ReplaceConverted(TEXT("ReplaceConverted"));
	const FName NAME_ClassGroupNames(TEXT("ClassGroupNames"));
	const FName NAME_EditorConfig(TEXT("EditorConfig"));

	/**
	 * As part of the singleton name, collect the parent chain names
	 */
	void AddOuterNames(FUHTStringBuilder& Out, FUnrealObjectDefinitionInfo* Outer)
	{
		if (Outer == nullptr)
		{
			return;
		}

		if (UHTCast<FUnrealClassDefinitionInfo>(Outer) != nullptr || UHTCast<FUnrealScriptStructDefinitionInfo>(Outer) != nullptr)
		{
			// Structs can also have UPackage outer.
			if (UHTCast<FUnrealClassDefinitionInfo>(Outer) == nullptr && UHTCast<FUnrealPackageDefinitionInfo>(Outer->GetOuter()) == nullptr)
			{
				AddOuterNames(Out, Outer->GetOuter());
			}
			Out.Append(TEXT("_"));
			Out.Append(UHTCastChecked<FUnrealStructDefinitionInfo>(Outer).GetAlternateNameCPP());
		}
		else if (FUnrealPackageDefinitionInfo* PackageDef = UHTCast<FUnrealPackageDefinitionInfo>(Outer))
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
	void GenerateSingletonName(FUHTStringBuilder& Out, FUnrealFieldDefinitionInfo* Item, bool bRequiresValidObject)
	{
		Out.Append(TEXT("Z_Construct_U"));
		Out.Append(Item->GetEngineClassName());
		AddOuterNames(Out, Item);

		if (FUnrealClassDefinitionInfo* ItemClass = UHTCast<FUnrealClassDefinitionInfo>(Item))
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
	FString GetTypePackageNameHelper(const T& Field)
	{
		FString PackageName = Field.GetMetaData(NAME_ReplaceConverted);
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
			PackageName = Field.GetPackageDef().GetName();
		}
		return PackageName;
	}

	/**
	 * Returns True if the given class name includes a valid Unreal prefix and matches based on the given class.
	 *
	 * @param InNameToCheck - Name w/ potential prefix to check
	 * @param OriginalClass - Class to check against
	 */
	bool ClassNameHasValidPrefix(const FString& InNameToCheck, const FUnrealClassDefinitionInfo& OriginalClass)
	{
		bool bIsLabledDeprecated;
		GetClassPrefix(InNameToCheck, bIsLabledDeprecated);

		// If the class is labeled deprecated, don't try to resolve it during header generation, valid results can't be guaranteed.
		if (bIsLabledDeprecated)
		{
			return true;
		}

		const FString OriginalClassName = OriginalClass.GetNameWithPrefix();

		bool bNamesMatch = (InNameToCheck == OriginalClassName);

		if (!bNamesMatch)
		{
			//@TODO: UCREMOVAL: I/U interface hack - Ignoring prefixing for this call
			if (OriginalClass.HasAnyClassFlags(CLASS_Interface))
			{
				bNamesMatch = InNameToCheck.Mid(1) == OriginalClassName.Mid(1);
			}
		}

		return bNamesMatch;
	}
}

void FUHTMetaData::RemapMetaData(FUnrealTypeDefinitionInfo& TypeDef, TMap<FName, FString>& MetaData)
{
	// Evaluate any key redirects on the passed in pairs
	for (TPair<FName, FString>& Pair : MetaData)
	{
		FName& CurrentKey = Pair.Key;
		FName NewKey = UMetaData::GetRemappedKeyName(CurrentKey);

		if (NewKey != NAME_None)
		{
			UE_LOG_WARNING_UHT(TypeDef, TEXT("Remapping old metadata key '%s' to new key '%s', please update the declaration."), *CurrentKey.ToString(), *NewKey.ToString());
			CurrentKey = NewKey;
		}
	}
}

FString FUHTMetaData::GetMetaDataHelper(const FName& Key, bool bAllowRemap) const
{
	// if not found, return a static empty string
	const FString* Result = FindMetaDataHelper(Key);
	if (Result == nullptr)
	{
		return FString();
	}

	if (bAllowRemap && Result->StartsWith(TEXT("ini:")))
	{
		FString ResultString = *Result;
		if (!GConfig->GetString(GetMetaDataRemapConfigName(), *Key.ToString(), ResultString, GEngineIni))
		{
			// if this fails, then use what's after the ini:
			ResultString.MidInline(4, MAX_int32, false);
		}
		return ResultString;
	}
	else
	{
		return *Result;
	}
}

const FString* FUHTMetaData::FindMetaDataHelper(const FName& Key) const
{
	const FString* Result = nullptr;

	if (Key != NAME_None)
	{
		if (TMap<FName, FString>* MetaDataMap = GetMetaDataMap())
		{
			Result = MetaDataMap->Find(Key);
		}
	}
#if UHT_ENABLE_ENGINE_TYPE_CHECKS
	CheckFindMetaData(Key, Result);
#endif
	return Result;
}

void FUHTMetaData::SetMetaDataHelper(const FName& Key, const TCHAR* InValue) 
{
	if (TMap<FName, FString>* MetaDataMap = GetMetaDataMap())
	{
		MetaDataMap->Add(Key, InValue);
	}
	else
	{
		checkf(false, TEXT("If GetMetaDataMap can return nullptr, this SetMetaDataHelper must be overridden"));
	}
}

FName FUHTMetaData::GetMetaDataKey(const TCHAR* Key, int32 NameIndex, EFindName FindName) const
{
	if (NameIndex != INDEX_NONE)
	{
		return FName(GetMetaDataIndexName(NameIndex) + TEXT(".") + Key, FindName);
	}
	else
	{
		return FName(Key, FNAME_Find);
	}
}

FName FUHTMetaData::GetMetaDataKey(FName Key, int32 NameIndex, EFindName FindName) const
{
	if (NameIndex != INDEX_NONE)
	{
		return FName(GetMetaDataIndexName(NameIndex) + TEXT(".") + Key.ToString(), FindName);
	}
	else
	{
		return Key;
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

FString FUnrealTypeDefinitionInfo::GetFilename() const
{
	return HasSource() ? SourceFile->GetFilename() : FString(TEXT("UnknownSource"));
}

TSharedRef<FScope> FUnrealTypeDefinitionInfo::GetScope()
{
	if (!HasSource())
	{
		FUHTException::Throwf(*this, TEXT("Attempt to fetch the scope for type \"%s\" when it doesn't implement the method or there is no source file associated with the type."), *GetNameCPP());
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
		FUHTException::Throwf(*this, TEXT("Attempt to fetch the generated hash for type \"%s\" before it has been generated.  Include dependencies, topological sort, or job graph is in error."), *GetNameCPP());
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

void FUnrealTypeDefinitionInfo::CreateUObjectEngineTypes()
{
	switch (CreateUObectEngineTypesState)
	{
	case EFinalizeState::None:
		CreateUObectEngineTypesState = EFinalizeState::InProgress;
		CreateUObjectEngineTypesInternal();
		CreateUObectEngineTypesState = EFinalizeState::Finished;
		break;
	case EFinalizeState::InProgress:
		checkf(false, TEXT("Recursive call to CreateUObectEngineTypes detected"))
		break;
	case EFinalizeState::Finished:
		break;
	}
}

void FUnrealTypeDefinitionInfo::PostParseFinalize()
{
	switch (PostParseFinalizeState)
	{
	case EFinalizeState::None:
		PostParseFinalizeState = EFinalizeState::InProgress;
		PostParseFinalizeInternal();
		PostParseFinalizeState = EFinalizeState::Finished;
		break;
	case EFinalizeState::InProgress:
		checkf(false, TEXT("Recursive call to PostParseFinalize detected"))
		break;
	case EFinalizeState::Finished:
		break;
	}
}

void FUnrealTypeDefinitionInfo::ValidateMetaDataFormat(const FName InKey, const FString& InValue) const
{
	ValidateMetaDataFormat(InKey, GetCheckedMetadataSpecifier(InKey), InValue);
}

void FUnrealTypeDefinitionInfo::ValidateMetaDataFormat(const FName InKey, ECheckedMetadataSpecifier InCheckedMetadataSpecifier, const FString& InValue) const
{
	switch (InCheckedMetadataSpecifier)
	{
	default:
	{
		// Don't need to validate this specifier
	}
	break;

	case ECheckedMetadataSpecifier::UIMin:
	case ECheckedMetadataSpecifier::UIMax:
	case ECheckedMetadataSpecifier::ClampMin:
	case ECheckedMetadataSpecifier::ClampMax:
	{
		if (!InValue.IsNumeric())
		{
			FUHTException::Throwf(*this, TEXT("Metadata value for '%s' is non-numeric : '%s'"), *InKey.ToString(), *InValue);
		}
	}
	break;

	case ECheckedMetadataSpecifier::BlueprintProtected:
	{
		if (const FUnrealFunctionDefinitionInfo* FuncDef = UHTCast<FUnrealFunctionDefinitionInfo>(this))
		{
			if (FuncDef->HasAnyFunctionFlags(FUNC_Static))
			{
				// Determine if it's a function library
				FUnrealClassDefinitionInfo* ClassDef = FuncDef->GetOwnerClass();
				for (; ClassDef != nullptr && ClassDef->GetSuperClass() != GUObjectDef; ClassDef = ClassDef->GetSuperClass())
				{
				}

				if (ClassDef != nullptr && ClassDef->GetName() == TEXT("BlueprintFunctionLibrary"))
				{
					FUHTException::Throwf(*this, TEXT("%s doesn't make sense on static method '%s' in a blueprint function library"), *InKey.ToString(), *FuncDef->GetName());
				}
			}
		}
	}
	break;

	case ECheckedMetadataSpecifier::CommutativeAssociativeBinaryOperator:
	{
		if (const FUnrealFunctionDefinitionInfo* FuncDef = UHTCast<FUnrealFunctionDefinitionInfo>(this))
		{
			bool bGoodParams = (FuncDef->GetProperties().Num() == 3);
			if (bGoodParams)
			{
				FUnrealPropertyDefinitionInfo* FirstParam = nullptr;
				FUnrealPropertyDefinitionInfo* SecondParam = nullptr;
				FUnrealPropertyDefinitionInfo* ReturnValue = nullptr;
				for (TSharedRef<FUnrealPropertyDefinitionInfo> PropertyDef : FuncDef->GetProperties())
				{
					if (PropertyDef->HasAnyPropertyFlags(CPF_ReturnParm))
					{
						ReturnValue = &*PropertyDef;
					}
					else
					{
						if (FirstParam == nullptr)
						{
							FirstParam = &*PropertyDef;
						}
						else if (SecondParam == nullptr)
						{
							SecondParam = &*PropertyDef;
						}
					}
				}

				if (ReturnValue == nullptr || SecondParam == nullptr || !SecondParam->SameType(*FirstParam))
				{
					bGoodParams = false;
				}
			}

			if (!bGoodParams)
			{
				UE_LOG_ERROR_UHT(*FuncDef, TEXT("Commutative associative binary operators must have exactly 2 parameters of the same type and a return value."));
			}
		}
	}
	break;

	case ECheckedMetadataSpecifier::ExpandBoolAsExecs:
	case ECheckedMetadataSpecifier::ExpandEnumAsExecs:
	{
		if (const FUnrealFunctionDefinitionInfo* FuncDef = UHTCast<FUnrealFunctionDefinitionInfo>(this))
		{
			// multiple entry parsing in the same format as eg SetParam.
			TArray<FString> RawGroupings;
			InValue.ParseIntoArray(RawGroupings, TEXT(","), false);

			FUnrealPropertyDefinitionInfo* FirstInputDef = nullptr;
			for (FString& RawGroup : RawGroupings)
			{
				RawGroup.TrimStartAndEndInline();

				TArray<FString> IndividualEntries;
				RawGroup.ParseIntoArray(IndividualEntries, TEXT("|"));

				for (FString& Entry : IndividualEntries)
				{
					Entry.TrimStartAndEndInline();
					if (Entry.IsEmpty())
					{
						continue;
					}

					FUnrealPropertyDefinitionInfo* FoundFieldDef = FHeaderParser::FindProperty(*FuncDef, *Entry, false);
					if (!FoundFieldDef)
					{
						UE_LOG_ERROR_UHT(*FuncDef, TEXT("Function does not have a parameter named '%s'"), *Entry);
					}
					else
					{
						if (!FoundFieldDef->HasAnyPropertyFlags(CPF_ReturnParm) &&
							(!FoundFieldDef->HasAnyPropertyFlags(CPF_OutParm) ||
								FoundFieldDef->HasAnyPropertyFlags(CPF_ReferenceParm)))
						{
							if (!FirstInputDef)
							{
								FirstInputDef = FoundFieldDef;
							}
							else
							{
								UE_LOG_ERROR_UHT(*FuncDef, TEXT("Function already specified an ExpandEnumAsExec input (%s), but '%s' is also an input parameter. Only one is permitted."), *FirstInputDef->GetName(), *Entry);
							}
						}
					}
				}
			}
		}
	}
	break;

	case ECheckedMetadataSpecifier::DevelopmentStatus:
	{
		const FString EarlyAccessValue(TEXT("EarlyAccess"));
		const FString ExperimentalValue(TEXT("Experimental"));
		if ((InValue != EarlyAccessValue) && (InValue != ExperimentalValue))
		{
			FUHTException::Throwf(*this, TEXT("'%s' metadata was '%s' but it must be %s or %s"), *InKey.ToString(), *InValue, *ExperimentalValue, *EarlyAccessValue);
		}
	}
	break;

	case ECheckedMetadataSpecifier::Units:
	{
		// Check for numeric property
		if (const FUnrealPropertyDefinitionInfo* PropDef = UHTCast<FUnrealPropertyDefinitionInfo>(this))
		{
			if (!PropDef->IsNumericOrNumericStaticArray() && !PropDef->IsStructOrStructStaticArray())
			{
				FUHTException::Throwf(*this, TEXT("'Units' meta data can only be applied to numeric and struct properties"));
			}
		}

		if (!FUnitConversion::UnitFromString(*InValue))
		{
			FUHTException::Throwf(*this, TEXT("Unrecognized units (%s) specified for property '%s'"), *InValue, *GetFullName());
		}
	}
	break;

	case ECheckedMetadataSpecifier::DocumentationPolicy:
	{
		const TCHAR* StrictValue = TEXT("Strict");
		if (InValue != StrictValue)
		{
			FUHTException::Throwf(*this, TEXT("'%s' metadata was '%s' but it must be %s"), *InKey.ToString(), *InValue, *StrictValue);
		}
	}
	break;
	}
}

void FUnrealPropertyDefinitionInfo::PostParseFinalizeInternal()
{
	if (GetPropertySafe() == nullptr)
	{
		FPropertyTraits::CreateEngineType(SharedThis(this));
	}
	TypePackageName = GetTypePackageNameHelper(*this);
}

bool FUnrealPropertyDefinitionInfo::IsDynamic() const
{
	return HasMetaData(NAME_ReplaceConverted);
}

bool FUnrealPropertyDefinitionInfo::IsOwnedByDynamicType() const
{
	for (FUnrealTypeDefinitionInfo* Owner = GetOuter(); Owner; Owner = Owner->GetOuter())
	{
		if (FUnrealPropertyDefinitionInfo* PropDef = UHTCast<FUnrealPropertyDefinitionInfo>(Owner))
		{
			return PropDef->IsOwnedByDynamicType();
		}
		else if (FUnrealObjectDefinitionInfo* ObjectDef = UHTCast<FUnrealObjectDefinitionInfo>(Owner))
		{
			return ObjectDef->IsOwnedByDynamicType();
		}
	}
	return false;
}

void FUnrealPropertyDefinitionInfo::SetDelegateFunctionSignature(FUnrealFunctionDefinitionInfo& DelegateFunctionDef)
{
	if (GetPropertySafe() != nullptr)
	{
		FDelegateProperty* DelegateProperty = CastFieldChecked<FDelegateProperty>(PropertyBase.ArrayType == EArrayType::None ? GetProperty() : GetValuePropDef().GetProperty());
		DelegateProperty->SignatureFunction = DelegateFunctionDef.GetFunction();
	}
	PropertyBase.FunctionDef = &DelegateFunctionDef;
}

FString FUnrealPropertyDefinitionInfo::GetEngineClassName() const
{
#if UHT_ENABLE_ENGINE_TYPE_CHECKS
	if (GetPropertySafe() != nullptr)
	{
		check(FPropertyTraits::GetEngineClassName(*this) == GetPropertySafe()->GetClass()->GetName()); // Validation
	}
#endif
	return FPropertyTraits::GetEngineClassName(*this);
}

FString FUnrealPropertyDefinitionInfo::GetPathName(FUnrealObjectDefinitionInfo* StopOuter) const
{
	TStringBuilder<256> ResultString;
	GetPathName(StopOuter, ResultString);
#if UHT_ENABLE_ENGINE_TYPE_CHECKS
	if (GetPropertySafe() != nullptr && (StopOuter == nullptr || StopOuter->GetObjectSafe() != nullptr))
	{
		TStringBuilder<256> OtherResultString;
		GetPropertySafe()->GetPathName(StopOuter ? StopOuter->GetObjectSafe() : nullptr, OtherResultString); // Validation
		check(FCString::Strcmp(OtherResultString.ToString(), ResultString.ToString()) == 0);
	}
#endif
	return FString(FStringView(ResultString));
}

void FUnrealPropertyDefinitionInfo::GetPathName(FUnrealObjectDefinitionInfo* StopOuter, FStringBuilderBase& ResultString) const
{
	TArray<FName, TInlineAllocator<16>> ParentFields;
	for (FUnrealTypeDefinitionInfo* LocalOuter = GetOuter(); LocalOuter; LocalOuter = LocalOuter->GetOuter())
	{
		if (UHTCast<FUnrealPropertyDefinitionInfo>(LocalOuter))
		{
			ParentFields.Add(LocalOuter->GetFName());
		}
		else
		{
			LocalOuter->GetPathName(StopOuter, ResultString);
			ResultString << SUBOBJECT_DELIMITER_CHAR;
			break;
		}
	}

	for (int FieldIndex = ParentFields.Num() - 1; FieldIndex >= 0; --FieldIndex)
	{
		ParentFields[FieldIndex].AppendString(ResultString);
		ResultString << TEXT(".");
	}
	GetFName().AppendString(ResultString);
}

FString FUnrealPropertyDefinitionInfo::GetFullName() const
{
	FString FullName = GetEngineClassName();
	FullName += TEXT(" ");
	FullName += GetPathName();
#if UHT_ENABLE_ENGINE_TYPE_CHECKS
	if (GetPropertySafe() != nullptr)
	{
		check(FullName == GetPropertySafe()->GetFullName()); // Validation
	}
#endif
	return FullName;
}

FString FUnrealPropertyDefinitionInfo::GetCPPType(FString* ExtendedTypeText/* = nullptr*/, uint32 CPPExportFlags/* = 0*/) const
{
	FString Out = FPropertyTraits::GetCPPType(*this, ExtendedTypeText, CPPExportFlags);
#if UHT_ENABLE_ENGINE_TYPE_CHECKS
	if (GetPropertySafe() != nullptr)
	{
		FString ExtOutTemp;
		FString* ExtOutTempPtr = ExtendedTypeText ? &ExtOutTemp : nullptr;
		check(Out == GetPropertySafe()->GetCPPType(ExtOutTempPtr, CPPExportFlags) && (ExtendedTypeText == nullptr || *ExtendedTypeText == ExtOutTemp)); // Validation
	}
#endif
	return Out;
}

FString FUnrealPropertyDefinitionInfo::GetCPPTypeForwardDeclaration() const
{
	FString Out = FPropertyTraits::GetCPPTypeForwardDeclaration(*this);
#if UHT_ENABLE_ENGINE_TYPE_CHECKS
	if (GetPropertySafe() != nullptr)
	{
		check(Out == GetPropertySafe()->GetCPPTypeForwardDeclaration()); // Validation
	}
#endif
	return Out;
}

struct FUHTDisplayNameHelper
{
	static FString Get(const FUnrealPropertyDefinitionInfo& Property)
	{
		// The GetAuthoredNameForField only does something for user defined structures
		//if (FUnrealStructDefinitionInfo* OwnerStruct = GetOwnerStruct(Property))
		//{
		//	OwnerStruct->GetStruct()->GetAuthoredNameForField(Property.GetProperty());
		//}
		return Property.GetName();
	}

	static FString Get(const FUnrealObjectDefinitionInfo& Object)
	{
		const FUnrealClassDefinitionInfo* ClassDef = UHTCast<FUnrealClassDefinitionInfo>(Object);
		if (ClassDef && !ClassDef->HasAnyClassFlags(CLASS_Native))
		{
			FString Name = Object.GetName();
			Name.RemoveFromEnd(TEXT("_C"));
			Name.RemoveFromStart(TEXT("SKEL_"));
			return Name;
		}
		return Object.GetName();
	}
};

FUnrealObjectDefinitionInfo* FUnrealPropertyDefinitionInfo::GetOwnerObject() const
{
	for (FUnrealTypeDefinitionInfo* TypeDef = GetOuter(); TypeDef; TypeDef = TypeDef->GetOuter())
	{
		if (FUnrealObjectDefinitionInfo* ObjectDef = UHTCast<FUnrealObjectDefinitionInfo>(TypeDef))
		{
			return ObjectDef;
		}
	}
	return nullptr;
}

FUnrealStructDefinitionInfo* FUnrealPropertyDefinitionInfo::GetOwnerStruct() const
{
	for (FUnrealTypeDefinitionInfo* TypeDef = GetOuter(); TypeDef; TypeDef = TypeDef->GetOuter())
	{
		if (FUnrealStructDefinitionInfo* StructDef = UHTCast<FUnrealStructDefinitionInfo>(TypeDef))
		{
			return StructDef;
		}
	}
	return nullptr;
}

FString FUnrealPropertyDefinitionInfo::GetFullGroupName(bool bStartWithOuter) const
{
	if (bStartWithOuter)
	{
		if (FUnrealTypeDefinitionInfo* Owner = GetOuter())
		{
			if (FUnrealObjectDefinitionInfo* ObjectOwner = UHTCast<FUnrealObjectDefinitionInfo>(Owner))
			{
				return ObjectOwner->GetPathName(&ObjectOwner->GetPackageDef());
			}
			else
			{
				FUnrealPropertyDefinitionInfo& PropertyOwner = UHTCastChecked<FUnrealPropertyDefinitionInfo>(Owner);
				return PropertyOwner.GetPathName(&PropertyOwner.GetOwnerObject()->GetPackageDef());
			}
		}
		else
		{
			return FString();
		}
	}
	else
	{
		FUnrealObjectDefinitionInfo* ObjectOuter = GetOwnerObject();
		return GetPathName(ObjectOuter ? &ObjectOuter->GetPackageDef() : nullptr);
	}
}

FText FUnrealPropertyDefinitionInfo::GetDisplayNameText() const
{
	FText LocalizedDisplayName;

	static const FString Namespace = TEXT("UObjectDisplayNames");
	static const FName NAME_DisplayName(TEXT("DisplayName"));

	const FString Key = GetFullGroupName(false);

	FString NativeDisplayName;
	if (const FString* FoundMetaData = FindMetaData(NAME_DisplayName))
	{
		NativeDisplayName = *FoundMetaData;
	}
	else
	{
		NativeDisplayName = FName::NameToDisplayString(FUHTDisplayNameHelper::Get(*this), IsBooleanOrBooleanStaticArray());
	}

	if (!(FText::FindText(Namespace, Key, /*OUT*/LocalizedDisplayName, &NativeDisplayName)))
	{
		LocalizedDisplayName = FText::FromString(NativeDisplayName);
	}

#if UHT_ENABLE_ENGINE_TYPE_CHECKS
	if (GetPropertySafe())
	{
		check(LocalizedDisplayName.ToString() == GetPropertySafe()->GetDisplayNameText().ToString());
	}
#endif
	return LocalizedDisplayName;
}

FText FUnrealPropertyDefinitionInfo::GetToolTipText(bool bShortTooltip) const
{
	bool bFoundShortTooltip = false;
	static const FName NAME_Tooltip(TEXT("Tooltip"));
	static const FName NAME_ShortTooltip(TEXT("ShortTooltip"));
	FText LocalizedToolTip;
	FString NativeToolTip;

	if (bShortTooltip)
	{
		NativeToolTip = GetMetaData(NAME_ShortTooltip);
		if (NativeToolTip.IsEmpty())
		{
			NativeToolTip = GetMetaData(NAME_Tooltip);
		}
		else
		{
			bFoundShortTooltip = true;
		}
	}
	else
	{
		NativeToolTip = GetMetaData(NAME_Tooltip);
	}

	const FString Namespace = bFoundShortTooltip ? TEXT("UObjectShortTooltips") : TEXT("UObjectToolTips");
	const FString Key = GetFullGroupName(false);
	if (!FText::FindText(Namespace, Key, /*OUT*/LocalizedToolTip, &NativeToolTip))
	{
		if (NativeToolTip.IsEmpty())
		{
			NativeToolTip = FName::NameToDisplayString(FUHTDisplayNameHelper::Get(*this), IsBooleanOrBooleanStaticArray());
		}
		else
		{
			static const FString DoxygenSee(TEXT("@see"));
			static const FString TooltipSee(TEXT("See:"));
			if (NativeToolTip.ReplaceInline(*DoxygenSee, *TooltipSee) > 0)
			{
				NativeToolTip.TrimEndInline();
			}
		}
		LocalizedToolTip = FText::FromString(NativeToolTip);
	}

#if UHT_ENABLE_ENGINE_TYPE_CHECKS
	if (GetPropertySafe())
	{
		check(LocalizedToolTip.ToString() == GetPropertySafe()->GetToolTipText(bShortTooltip).ToString());
	}
#endif
	return LocalizedToolTip;
}

FUnrealPackageDefinitionInfo& FUnrealPropertyDefinitionInfo::GetPackageDef() const
{
	if (HasSource())
	{
		return GetUnrealSourceFile().GetPackageDef();
	}
	return GTypeDefinitionInfoMap.FindChecked<FUnrealPackageDefinitionInfo>(GetProperty()->GetOutermost());
}

bool FUnrealPropertyDefinitionInfo::SameType(const FUnrealPropertyDefinitionInfo& Other) const
{
	bool bResults = FPropertyTraits::SameType(*this, Other);
#if UHT_ENABLE_ENGINE_TYPE_CHECKS
	if (GetPropertySafe() && Other.GetPropertySafe())
	{
		check(GetPropertySafe()->SameType(Other.GetPropertySafe()) == bResults)
	}
#endif
	return bResults;
}

FUnrealPackageDefinitionInfo& FUnrealObjectDefinitionInfo::GetPackageDef() const
{
	if (HasSource())
	{
		return GetUnrealSourceFile().GetPackageDef();
	}
	return GTypeDefinitionInfoMap.FindChecked<FUnrealPackageDefinitionInfo>(GetObject()->GetPackage());
}

FUnrealObjectDefinitionInfo::FUnrealObjectDefinitionInfo(UObject* Object)
	: FUnrealTypeDefinitionInfo(Object->GetName())
	, Name(Object->GetFName())
	, InternalObjectFlags(Object->GetInternalFlags())
{
}

FString FUnrealObjectDefinitionInfo::GetFullName() const
{	
	FString FullName = GetEngineClassName();
	FullName += TEXT(" ");
	FullName += GetPathName();
#if UHT_ENABLE_ENGINE_TYPE_CHECKS
	if (GetObjectSafe() != nullptr)
	{
		check(FullName == GetObjectSafe()->GetFullName()); // Validation
	}
#endif
	return FullName;
}

FString FUnrealObjectDefinitionInfo::GetPathName(FUnrealObjectDefinitionInfo* StopOuter) const
{
	TStringBuilder<256> ResultBuilder;
	GetPathName(StopOuter, ResultBuilder);
#if UHT_ENABLE_ENGINE_TYPE_CHECKS
	if (GetObjectSafe() != nullptr && (StopOuter == nullptr || StopOuter->GetObjectSafe() != nullptr))
	{
		TStringBuilder<256> OtherResultString;
		GetObjectSafe()->GetPathName(StopOuter ? StopOuter->GetObjectSafe() : nullptr, OtherResultString); // Validation
		check(FCString::Strcmp(OtherResultString.ToString(), ResultBuilder.ToString()) == 0);
	}
#endif
	return FString(FStringView(ResultBuilder));
}

void FUnrealObjectDefinitionInfo::GetPathName(FUnrealObjectDefinitionInfo* StopOuter, FStringBuilderBase& ResultString) const
{
	if (this != StopOuter && this != NULL)
	{
		FUnrealObjectDefinitionInfo* ObjOuter = GetOuter();
		if (ObjOuter && ObjOuter != StopOuter)
		{
			ObjOuter->GetPathName(StopOuter, ResultString);

			// SUBOBJECT_DELIMITER_CHAR is used to indicate that this object's outer is not a UPackage
			if (UHTCast<FUnrealPackageDefinitionInfo>(ObjOuter) == nullptr
				&& UHTCast<FUnrealPackageDefinitionInfo>(ObjOuter->GetOuter()) != nullptr)
			{
				ResultString << SUBOBJECT_DELIMITER_CHAR;
			}
			else
			{
				ResultString << TEXT('.');
			}
		}
		GetFName().AppendString(ResultString);
	}
	else
	{
		ResultString << TEXT("None");
	}
}

FString FUnrealObjectDefinitionInfo::GetFullGroupName(bool bStartWithOuter) const
{
	const FUnrealObjectDefinitionInfo* Obj = bStartWithOuter ? GetOuter() : this;
	return Obj ? Obj->GetPathName(&GetPackageDef()) : TEXT("");
}

void FUnrealObjectDefinitionInfo::SetMetaDataHelper(const FName& Key, const TCHAR* InValue) 
{
	// If we don't have an existing meta data map, then we must be using the meta data off
	// the object and no values have been set yet.  In that case just set the whole block
	TMap<FName, FString>* MetaDataMap = GetMetaDataMap();
	if (MetaDataMap == nullptr)
	{
		UMetaData* MetaData = GetUObjectMetaData();
		MetaData->SetValue(GetObject(), Key, InValue); // This is valid
	}

	// Otherwise we are either using the objects meta data and it already exists, or
	// we are using a UHT private meta data block.  If we have both, then first
	// set the object followed by the private copy.
	else
	{
		TMap<FName, FString>* UObjectMetaDataMap = GetUObjectMetaDataMap();
		if (MetaDataMap != UObjectMetaDataMap)
		{
			if (UObjectMetaDataMap != nullptr)
			{
				UObjectMetaDataMap->Add(Key, InValue);
			}
			else if (UMetaData* MetaData = GetUObjectMetaData())
			{
				MetaData->SetValue(GetObject(), Key, InValue); // This is valid
			}
		}

		MetaDataMap->Add(Key, InValue);
	}
}

FUnrealPackageDefinitionInfo::FUnrealPackageDefinitionInfo(const FManifestModule& InModule, UPackage* InPackage)
	: FUnrealObjectDefinitionInfo(InPackage)
	, Module(InModule)
	, ShortUpperName(FPackageName::GetShortName(InPackage).ToUpper())
	, API(FString::Printf(TEXT("%s_API "), *ShortUpperName))
{
	SetObject(InPackage);
}

void FUnrealPackageDefinitionInfo::CreateUObjectEngineTypesInternal()
{
	for (TSharedRef<FUnrealSourceFile>& LocalSourceFile : GetAllSourceFiles())
	{
		for (TSharedRef<FUnrealTypeDefinitionInfo>& TypeDef : LocalSourceFile->GetDefinedTypes())
		{
			TypeDef->CreateUObjectEngineTypes();
		}
	}
}

void FUnrealPackageDefinitionInfo::PostParseFinalizeInternal()
{
	UPackage* Package = GetPackage();

	FString PackageName = Package->GetName();
	PackageName.ReplaceInline(TEXT("/"), TEXT("_"), ESearchCase::CaseSensitive);

	SingletonName.Appendf(TEXT("Z_Construct_UPackage_%s()"), *PackageName);
	SingletonNameChopped = SingletonName.LeftChop(2);
	ExternDecl.Appendf(TEXT("\tUPackage* %s;\r\n"), *SingletonName);

	for (TSharedRef<FUnrealSourceFile>& LocalSourceFile : GetAllSourceFiles())
	{
		for (TSharedRef<FUnrealTypeDefinitionInfo>& TypeDef : LocalSourceFile->GetDefinedTypes())
		{
			TypeDef->PostParseFinalize();
		}
	}
}

void FUnrealPackageDefinitionInfo::AddCrossModuleReference(TSet<FString>* UniqueCrossModuleReferences) const
{
	if (UniqueCrossModuleReferences)
	{
		UniqueCrossModuleReferences->Add(GetExternDecl());
	}
}

void FUnrealFieldDefinitionInfo::PostParseFinalizeInternal()
{
	const FString& ClassName = GetEngineClassName(true);
	const FString& PackageShortName = GetPackageDef().GetShortUpperName();

	FUHTStringBuilder Out;
	GenerateSingletonName(Out, this, false);
	ExternDecl[0].Appendf(TEXT("\t%s_API U%s* %s;\r\n"), *PackageShortName, *ClassName, *Out);
	SingletonName[0] = Out;
	SingletonNameChopped[0] = SingletonName[0].LeftChop(2);

	Out.Reset();
	GenerateSingletonName(Out, this, true);
	ExternDecl[1].Appendf(TEXT("\t%s_API U%s* %s;\r\n"), *PackageShortName, *ClassName, *Out);
	SingletonName[1] = Out;
	SingletonNameChopped[1] = SingletonName[1].LeftChop(2);

	TypePackageName = GetTypePackageNameHelper(*this);
}

FText FUnrealFieldDefinitionInfo::GetToolTipText(bool bShortTooltip) const
{
	bool bFoundShortTooltip = false;
	static const FName NAME_Tooltip(TEXT("Tooltip"));
	static const FName NAME_ShortTooltip(TEXT("ShortTooltip"));
	FText LocalizedToolTip;
	FString NativeToolTip;

	if (bShortTooltip)
	{
		NativeToolTip = GetMetaData(NAME_ShortTooltip);
		if (NativeToolTip.IsEmpty())
		{
			NativeToolTip = GetMetaData(NAME_Tooltip);
		}
		else
		{
			bFoundShortTooltip = true;
		}
	}
	else
	{
		NativeToolTip = GetMetaData(NAME_Tooltip);
	}

	const FString Namespace = bFoundShortTooltip ? TEXT("UObjectShortTooltips") : TEXT("UObjectToolTips");
	const FString Key = GetFullGroupName(false);
	if (!FText::FindText(Namespace, Key, /*OUT*/LocalizedToolTip, &NativeToolTip))
	{
		if (NativeToolTip.IsEmpty())
		{
			NativeToolTip = FName::NameToDisplayString(FUHTDisplayNameHelper::Get(*this), false);
		}
		else if (!bShortTooltip && IsNative())
		{
			UField::FormatNativeToolTip(NativeToolTip, true);
		}
		LocalizedToolTip = FText::FromString(NativeToolTip);
	}

#if UHT_ENABLE_ENGINE_TYPE_CHECKS
	if (GetFieldSafe())
	{
		check(LocalizedToolTip.ToString() == GetFieldSafe()->GetToolTipText(bShortTooltip).ToString());
	}
#endif
	return LocalizedToolTip;
}

void FUnrealFieldDefinitionInfo::AddCrossModuleReference(TSet<FString>* UniqueCrossModuleReferences, bool bRequiresValidObject) const
{
	// We don't need to export UFunction externs, though we may need the externs for UDelegateFunctions
	if (UniqueCrossModuleReferences)
	{
		const FUnrealFunctionDefinitionInfo* FunctionDef = UHTCast<FUnrealFunctionDefinitionInfo>(this);
		if (FunctionDef == nullptr || FunctionDef->IsDelegateFunction())
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

		// If we don't have an existing meta data map, then we must be using the meta data off
		// the object and no values have been set yet.  In that case just set the whole block
		TMap<FName, FString>* MetaDataMap = GetMetaDataMap();
		if (MetaDataMap == nullptr)
		{
			UMetaData* MetaData = GetUObjectMetaData();
			MetaData->SetObjectValues(GetObject(), MoveTemp(InMetaData)); // This is valid
		}

		// Otherwise we are either using the objects meta data and it already exists, or
		// we are using a UHT private meta data block.  If we have both, then first
		// set the object followed by the private copy.
		else
		{
			TMap<FName, FString>* UObjectMetaDataMap = GetUObjectMetaDataMap();
			if (MetaDataMap != UObjectMetaDataMap)
			{
				if (UObjectMetaDataMap != nullptr)
				{
					UObjectMetaDataMap->Append(InMetaData);
				}
				else if (UMetaData* MetaData = GetUObjectMetaData())
				{
					MetaData->SetObjectValues(GetObject(), InMetaData); // This is valid
				}
			}

			MetaDataMap->Append(MoveTemp(InMetaData));
		}
	}
}

bool FUnrealFieldDefinitionInfo::IsDynamic() const
{
	return HasMetaData(NAME_ReplaceConverted);
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

FUnrealClassDefinitionInfo* FUnrealFieldDefinitionInfo::GetOwnerClass() const
{
	for (FUnrealTypeDefinitionInfo* TypeDef = GetOuter(); TypeDef; TypeDef = TypeDef->GetOuter())
	{
		if (FUnrealClassDefinitionInfo* ClassDef = UHTCast<FUnrealClassDefinitionInfo>(TypeDef))
		{
			return ClassDef;
		}
	}
	return nullptr;
}

FUnrealEnumDefinitionInfo::FUnrealEnumDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, FString&& InNameCPP, FName InName)
	: FUnrealFieldDefinitionInfo(InSourceFile, InLineNumber, MoveTemp(InNameCPP), InName, InSourceFile.GetPackageDef())
{ }

void FUnrealStructDefinitionInfo::AddProperty(TSharedRef<FUnrealPropertyDefinitionInfo> PropertyDef)
{
	Properties.Add(PropertyDef);

	// update the optimization flags
	if (!bContainsDelegates)
	{
		const FPropertyBase& PropertyBase = PropertyDef->GetPropertyBase();
		if (PropertyDef->IsDelegateOrDelegateStaticArray() || PropertyDef->IsMulticastDelegateOrMulticastDelegateStaticArray())
		{
			bContainsDelegates = true;
		}
		else if (PropertyDef->IsDynamicArray())
		{
			const FUnrealPropertyDefinitionInfo& ValuePropertyDef = PropertyDef->GetValuePropDef();
			if (ValuePropertyDef.IsDelegateOrDelegateStaticArray() || ValuePropertyDef.IsMulticastDelegateOrMulticastDelegateStaticArray())
			{
				bContainsDelegates = true;
			}
		}
	}
}

void FUnrealStructDefinitionInfo::CreateUObjectEngineTypesInternal()
{
	FUnrealFieldDefinitionInfo::CreateUObjectEngineTypesInternal();

	if (SuperStructInfo.Struct)
	{
		SuperStructInfo.Struct->CreateUObjectEngineTypes();
	}

	for (const FBaseStructInfo& Info : BaseStructInfo)
	{
		if (Info.Struct)
		{
			Info.Struct->CreateUObjectEngineTypes();
		}
	}

	for (TSharedRef<FUnrealFunctionDefinitionInfo> FunctionDef : Functions)
	{
		FunctionDef->CreateUObjectEngineTypes();
	}
}

void FUnrealStructDefinitionInfo::PostParseFinalizeInternal()
{
	FUnrealFieldDefinitionInfo::PostParseFinalizeInternal();

	if (SuperStructInfo.Struct)
	{
		SuperStructInfo.Struct->PostParseFinalize();
	}

	for (const FBaseStructInfo& Info : BaseStructInfo)
	{
		if (Info.Struct)
		{
			Info.Struct->PostParseFinalize();
		}
	}

	for (TSharedRef<FUnrealFunctionDefinitionInfo> FunctionDef : Functions)
	{
		FunctionDef->PostParseFinalize();
	}

	for (TSharedRef<FUnrealPropertyDefinitionInfo> PropertyDef : Properties)
	{
		PropertyDef->PostParseFinalize();
	}

	GetStruct()->Bind();

	// Internals will assert of we are relinking an intrinsic 
	bool bRelinkExistingProperties = true;
	if (FUnrealClassDefinitionInfo* ClassDef = UHTCast<FUnrealClassDefinitionInfo>(this))
	{
		bRelinkExistingProperties = !ClassDef->HasAnyClassFlags(CLASS_Intrinsic);
	}
	GetStruct()->StaticLink(bRelinkExistingProperties);
}

void FUnrealStructDefinitionInfo::CreatePropertyEngineTypes()
{
	for (TSharedRef<FUnrealPropertyDefinitionInfo> PropertyDef : Properties)
	{
		if (PropertyDef->GetPropertySafe() == nullptr)
		{
			FPropertyTraits::CreateEngineType(PropertyDef);
		}
	}
}

void FUnrealStructDefinitionInfo::AddFunction(TSharedRef<FUnrealFunctionDefinitionInfo> FunctionDef)
{
	Functions.Add(FunctionDef);

	// update the optimization flags
	if (!bContainsDelegates)
	{
		if (FunctionDef->HasAnyFunctionFlags(FUNC_Delegate))
		{
			bContainsDelegates = true;
		}
	}
}

FUnrealScriptStructDefinitionInfo::FUnrealScriptStructDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, FString&& InNameCPP, FName InName)
	: FUnrealStructDefinitionInfo(InSourceFile, InLineNumber, MoveTemp(InNameCPP), InName, InSourceFile.GetPackageDef())
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

FUnrealClassDefinitionInfo::FUnrealClassDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, FString&& InNameCPP, FName InName, bool bInIsInterface)
	: FUnrealStructDefinitionInfo(InSourceFile, InLineNumber, MoveTemp(InNameCPP), InName, InSourceFile.GetPackageDef())
	, bIsInterface(bInIsInterface)
{
	if (bInIsInterface)
	{
		GetStructMetaData().ParsedInterface = EParsedInterface::ParsedUInterface;
	}
}

FUnrealClassDefinitionInfo* FUnrealClassDefinitionInfo::FindClass(const TCHAR* ClassName)
{
	FUnrealClassDefinitionInfo* ClassDef = nullptr;
	{
		ClassDef = GTypeDefinitionInfoMap.FindByName<FUnrealClassDefinitionInfo>(ClassName);
	}

	if (ClassDef != nullptr)
	{
		if (UObjectRedirector* RenamedClassRedirector = FindObject<UObjectRedirector>(ANY_PACKAGE, ClassName))
		{
			UClass* RedierClass = CastChecked<UClass>(RenamedClassRedirector->DestinationObject);
			if (RedierClass != nullptr)
			{
				ClassDef = &GTypeDefinitionInfoMap.FindChecked<FUnrealClassDefinitionInfo>(RedierClass);
			}
		}
	}

	return ClassDef;
}

FUnrealClassDefinitionInfo* FUnrealClassDefinitionInfo::FindScriptClassOrThrow(const FHeaderParser& Parser, const FString& InClassName)
{
	FString ErrorMsg;
	if (FUnrealClassDefinitionInfo* ResultDef = FindScriptClass(InClassName, &ErrorMsg))
	{
		return ResultDef;
	}

	FUHTException::Throwf(Parser, MoveTemp(ErrorMsg));

	// Unreachable, but compiler will warn otherwise because FUHTException::Throwf isn't declared noreturn
	return nullptr;
}

FUnrealClassDefinitionInfo* FUnrealClassDefinitionInfo::FindScriptClass(const FString& InClassName, FString* OutErrorMsg)
{
	// Strip the class name of its prefix and then do a search for the class
	FString ClassNameStripped = GetClassNameWithPrefixRemoved(InClassName);
	if (FUnrealClassDefinitionInfo* FoundClassDef = FindClass(*ClassNameStripped))
	{
		// If the class was found with the stripped class name, verify that the correct prefix was used and throw an error otherwise
		if (!ClassNameHasValidPrefix(InClassName, *FoundClassDef))
		{
			if (OutErrorMsg)
			{
				*OutErrorMsg = FString::Printf(TEXT("Class '%s' has an incorrect prefix, expecting '%s'"), *InClassName, *FoundClassDef->GetNameWithPrefix());
			}
			return nullptr;
		}

		return FoundClassDef;
	}

	// Couldn't find the class with a class name stripped of prefix (or a prefix was not found)
	// See if the prefix was forgotten by trying to find the class with the given identifier
	if (FUnrealClassDefinitionInfo* FoundClassDef = FindClass(*InClassName))
	{
		// If the class was found with the given identifier, the user forgot to use the correct Unreal prefix	
		if (OutErrorMsg)
		{
			*OutErrorMsg = FString::Printf(TEXT("Class '%s' is missing a prefix, expecting '%s'"), *InClassName, *FoundClassDef->GetNameWithPrefix());
		}
	}
	else
	{
		// If the class was still not found, it wasn't a valid identifier
		if (OutErrorMsg)
		{
			*OutErrorMsg = FString::Printf(TEXT("Class '%s' not found."), *InClassName);
		}
	}

	return nullptr;
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


void FUnrealClassDefinitionInfo::PostParseFinalizeInternal()
{
	UClass* Class = GetClass();

	FUnrealStructDefinitionInfo::PostParseFinalizeInternal();

	if (IsInterface() && GetStructMetaData().ParsedInterface == EParsedInterface::ParsedUInterface)
	{
		FString UName = GetNameCPP();
		FString IName = TEXT("I") + UName.RightChop(1);
		FUHTException::Throwf(*this, TEXT("UInterface '%s' parsed without a corresponding '%s'"), *UName, *IName);
	}

	FHeaderParser::CheckSparseClassData(*this);

	// Finalize all of the children introduced in this class
	for (TSharedRef<FUnrealFunctionDefinitionInfo> FunctionDef : GetFunctions())
	{
		UFunction* Function = FunctionDef->GetFunction();
		Class->AddFunctionToFunctionMap(Function, Function->GetFName());
	}

	if (!HasAnyClassFlags(CLASS_Native))
	{
		//Class->UnMark(EObjectMark(OBJECTMARK_TagImp | OBJECTMARK_TagExp));
	}
	else if (!HasAnyClassFlags(CLASS_NoExport | CLASS_Intrinsic))
	{
		GetPackageDef().SetWriteClassesH(true);
		//Class->UnMark(OBJECTMARK_TagImp);
		//Class->Mark(OBJECTMARK_TagExp);
	}

	// This needs to be done outside of parallel blocks because it will modify UClass memory.
	// Later calls to SetUpUhtReplicationData inside parallel blocks should be fine, because
	// they will see the memory has already been set up, and just return the parent pointer.
	Class->SetUpUhtReplicationData();
}

void FUnrealClassDefinitionInfo::ParseClassProperties(TArray<FPropertySpecifier>&& InClassSpecifiers, const FString& InRequiredAPIMacroIfPresent)
{
	ParsedClassFlags = CLASS_None;
	// Record that this class is RequiredAPI if the CORE_API style macro was present
	if (!InRequiredAPIMacroIfPresent.IsEmpty())
	{
		ParsedClassFlags |= CLASS_RequiredAPI;
	}
	ParsedClassFlags |= CLASS_Native;

	// Process all of the class specifiers

	for (FPropertySpecifier& PropSpecifier : InClassSpecifiers)
	{
		switch ((EClassMetadataSpecifier)Algo::FindSortedStringCaseInsensitive(*PropSpecifier.Key, GClassMetadataSpecifierStrings))
		{
		case EClassMetadataSpecifier::NoExport:

			// Don't export to C++ header.
			ParsedClassFlags |= CLASS_NoExport;
			break;

		case EClassMetadataSpecifier::Intrinsic:
			ParsedClassFlags |= CLASS_Intrinsic;
			break;

		case EClassMetadataSpecifier::ComponentWrapperClass:

			MetaData.Add(NAME_IgnoreCategoryKeywordsInSubclasses, TEXT("true"));
			break;

		case EClassMetadataSpecifier::Within:

			ClassWithinStr = FHeaderParser::RequireExactlyOneSpecifierValue(*this, PropSpecifier);
			break;

		case EClassMetadataSpecifier::EditInlineNew:

			// Class can be constructed from the New button in editinline
			ParsedClassFlags |= CLASS_EditInlineNew;
			break;

		case EClassMetadataSpecifier::NotEditInlineNew:

			// Class cannot be constructed from the New button in editinline
			ParsedClassFlags &= ~CLASS_EditInlineNew;
			break;

		case EClassMetadataSpecifier::Placeable:

			bWantsToBePlaceable = true;
			ParsedClassFlags &= ~CLASS_NotPlaceable;
			break;

		case EClassMetadataSpecifier::DefaultToInstanced:

			// these classed default to instanced.
			ParsedClassFlags |= CLASS_DefaultToInstanced;
			break;

		case EClassMetadataSpecifier::NotPlaceable:

			// Don't allow the class to be placed in the editor.
			ParsedClassFlags |= CLASS_NotPlaceable;
			break;

		case EClassMetadataSpecifier::HideDropdown:

			// Prevents class from appearing in class comboboxes in the property window
			ParsedClassFlags |= CLASS_HideDropDown;
			break;

		case EClassMetadataSpecifier::DependsOn:

			FUHTException::Throwf(*this, TEXT("The dependsOn specifier is deprecated. Please use #include \"ClassHeaderFilename.h\" instead."));
			break;

		case EClassMetadataSpecifier::MinimalAPI:

			ParsedClassFlags |= CLASS_MinimalAPI;
			break;

		case EClassMetadataSpecifier::Const:

			ParsedClassFlags |= CLASS_Const;
			break;

		case EClassMetadataSpecifier::PerObjectConfig:

			ParsedClassFlags |= CLASS_PerObjectConfig;
			break;

		case EClassMetadataSpecifier::ConfigDoNotCheckDefaults:

			ParsedClassFlags |= CLASS_ConfigDoNotCheckDefaults;
			break;

		case EClassMetadataSpecifier::Abstract:

			// Hide all editable properties.
			ParsedClassFlags |= CLASS_Abstract;
			break;

		case EClassMetadataSpecifier::Deprecated:

			ParsedClassFlags |= CLASS_Deprecated;

			// Don't allow the class to be placed in the editor.
			ParsedClassFlags |= CLASS_NotPlaceable;

			break;

		case EClassMetadataSpecifier::Transient:

			// Transient class.
			ParsedClassFlags |= CLASS_Transient;
			break;

		case EClassMetadataSpecifier::NonTransient:

			// this child of a transient class is not transient - remove the transient flag
			ParsedClassFlags &= ~CLASS_Transient;
			break;

		case EClassMetadataSpecifier::CustomConstructor:

			// we will not export a constructor for this class, assuming it is in the CPP block
			ParsedClassFlags |= CLASS_CustomConstructor;
			break;

		case EClassMetadataSpecifier::Config:

			// Class containing config properties - parse the name of the config file to use
			ConfigName = FHeaderParser::RequireExactlyOneSpecifierValue(*this, PropSpecifier);
			break;

		case EClassMetadataSpecifier::DefaultConfig:

			// Save object config only to Default INIs, never to local INIs.
			ParsedClassFlags |= CLASS_DefaultConfig;
			break;

		case EClassMetadataSpecifier::GlobalUserConfig:

			// Save object config only to global user overrides, never to local INIs
			ParsedClassFlags |= CLASS_GlobalUserConfig;
			break;

		case EClassMetadataSpecifier::ProjectUserConfig:

			// Save object config only to project user overrides, never to INIs that are checked in
			ParsedClassFlags |= CLASS_ProjectUserConfig;
			break;

		case EClassMetadataSpecifier::EditorConfig:
			// Save EditorConfig properties to the given JSON file.
			MetaData.Add(NAME_EditorConfig, FHeaderParser::RequireExactlyOneSpecifierValue(*this, PropSpecifier));
			break;

		case EClassMetadataSpecifier::ShowCategories:

			FHeaderParser::RequireSpecifierValue(*this, PropSpecifier);

			for (FString& Value : PropSpecifier.Values)
			{
				ShowCategories.AddUnique(MoveTemp(Value));
			}
			break;

		case EClassMetadataSpecifier::HideCategories:

			FHeaderParser::RequireSpecifierValue(*this, PropSpecifier);

			for (FString& Value : PropSpecifier.Values)
			{
				HideCategories.AddUnique(MoveTemp(Value));
			}
			break;

		case EClassMetadataSpecifier::ShowFunctions:

			FHeaderParser::RequireSpecifierValue(*this, PropSpecifier);

			for (const FString& Value : PropSpecifier.Values)
			{
				HideFunctions.RemoveSwap(Value);
			}
			break;

		case EClassMetadataSpecifier::HideFunctions:

			FHeaderParser::RequireSpecifierValue(*this, PropSpecifier);

			for (FString& Value : PropSpecifier.Values)
			{
				HideFunctions.AddUnique(MoveTemp(Value));
			}
			break;

			// Currently some code only handles a single sidecar data structure so we enforce that here
		case EClassMetadataSpecifier::SparseClassDataTypes:

			SparseClassDataTypes.AddUnique(FHeaderParser::RequireExactlyOneSpecifierValue(*this, PropSpecifier));
			break;

		case EClassMetadataSpecifier::ClassGroup:

			FHeaderParser::RequireSpecifierValue(*this, PropSpecifier);

			for (FString& Value : PropSpecifier.Values)
			{
				ClassGroupNames.Add(MoveTemp(Value));
			}
			break;

		case EClassMetadataSpecifier::AutoExpandCategories:

			FHeaderParser::RequireSpecifierValue(*this, PropSpecifier);

			for (FString& Value : PropSpecifier.Values)
			{
				AutoCollapseCategories.RemoveSwap(Value);
				AutoExpandCategories.AddUnique(MoveTemp(Value));
			}
			break;

		case EClassMetadataSpecifier::AutoCollapseCategories:

			FHeaderParser::RequireSpecifierValue(*this, PropSpecifier);

			for (FString& Value : PropSpecifier.Values)
			{
				AutoExpandCategories.RemoveSwap(Value);
				AutoCollapseCategories.AddUnique(MoveTemp(Value));
			}
			break;

		case EClassMetadataSpecifier::DontAutoCollapseCategories:

			FHeaderParser::RequireSpecifierValue(*this, PropSpecifier);

			for (const FString& Value : PropSpecifier.Values)
			{
				AutoCollapseCategories.RemoveSwap(Value);
			}
			break;

		case EClassMetadataSpecifier::CollapseCategories:

			// Class' properties should not be shown categorized in the editor.
			ParsedClassFlags |= CLASS_CollapseCategories;
			break;

		case EClassMetadataSpecifier::DontCollapseCategories:

			// Class' properties should be shown categorized in the editor.
			ParsedClassFlags &= ~CLASS_CollapseCategories;
			break;

		case EClassMetadataSpecifier::AdvancedClassDisplay:

			// By default the class properties are shown in advanced sections in UI
			MetaData.Add(FHeaderParserNames::NAME_AdvancedClassDisplay, TEXT("true"));
			break;

		case EClassMetadataSpecifier::ConversionRoot:

			MetaData.Add(FHeaderParserNames::NAME_IsConversionRoot, TEXT("true"));
			break;

		case EClassMetadataSpecifier::NeedsDeferredDependencyLoading:

			ParsedClassFlags |= CLASS_NeedsDeferredDependencyLoading;
			break;

		default:
			FUHTException::Throwf(*this, TEXT("Unknown class specifier '%s'"), *PropSpecifier.Key);
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

void FUnrealClassDefinitionInfo::MergeAndValidateClassFlags(const FString& DeclaredClassName, EClassFlags PreviousClassFlags)
{
	UClass* Class = GetClass();
	if (bWantsToBePlaceable)
	{
		if (!(Class->ClassFlags & CLASS_NotPlaceable))
		{
			FUHTException::Throwf(*this, TEXT("The 'placeable' specifier is only allowed on classes which have a base class that's marked as not placeable. Classes are assumed to be placeable by default."));
		}
		Class->ClassFlags &= ~CLASS_NotPlaceable;
		bWantsToBePlaceable = false; // Reset this flag after it's been merged
	}

	// Now merge all remaining flags/properties
	Class->ClassFlags |= ParsedClassFlags;
	Class->ClassConfigName = FName(*ConfigName);

	SetAndValidateWithinClass();
	SetAndValidateConfigName();

	if (!!(Class->ClassFlags & CLASS_EditInlineNew))
	{
		// don't allow actor classes to be declared editinlinenew
		if (IsActorClass(Class))
		{
			FUHTException::Throwf(*this, TEXT("Invalid class attribute: Creating actor instances via the property window is not allowed"));
		}
	}

	// Make sure both RequiredAPI and MinimalAPI aren't specified
	if (Class->HasAllClassFlags(CLASS_MinimalAPI | CLASS_RequiredAPI))
	{
		FUHTException::Throwf(*this, TEXT("MinimalAPI cannot be specified when the class is fully exported using a MODULENAME_API macro"));
	}

	// All classes must start with a valid Unreal prefix
	const FString ExpectedClassName = GetNameWithPrefix();
	if (DeclaredClassName != ExpectedClassName)
	{
		FUHTException::Throwf(*this, TEXT("Class name '%s' is invalid, should be identified as '%s'"), *DeclaredClassName, *ExpectedClassName);
	}

	if ((Class->ClassFlags & CLASS_NoExport))
	{
		// if the class's class flags didn't contain CLASS_NoExport before it was parsed, it means either:
		// a) the DECLARE_CLASS macro for this native class doesn't contain the CLASS_NoExport flag (this is an error)
		// b) this is a new native class, which isn't yet hooked up to static registration (this is OK)
		if (!(Class->ClassFlags & CLASS_Intrinsic) && (PreviousClassFlags & CLASS_NoExport) == 0 &&
			(PreviousClassFlags & CLASS_Native) != 0)	// a new native class (one that hasn't been compiled into C++ yet) won't have this set
		{
			FUHTException::Throwf(*this, TEXT("'noexport': Must include CLASS_NoExport in native class declaration"));
		}
	}

	if (!Class->HasAnyClassFlags(CLASS_Abstract) && ((PreviousClassFlags & CLASS_Abstract) != 0))
	{
		if (Class->HasAnyClassFlags(CLASS_NoExport))
		{
			FUHTException::Throwf(*this, TEXT("'abstract': NoExport class missing abstract keyword from class declaration (must change C++ version first)"));
			Class->ClassFlags |= CLASS_Abstract;
		}
		else if (IsNative())
		{
			FUHTException::Throwf(*this, TEXT("'abstract': missing abstract keyword from class declaration - class will no longer be exported as abstract"));
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
				FUHTException::Throwf(*this, TEXT("Cannot inherit config filename: %s has no super class"), *Class->GetName());
			}

			if (SuperClass->ClassConfigName == NAME_None)
			{
				FUHTException::Throwf(*this, TEXT("Cannot inherit config filename: parent class %s is not marked config."), *SuperClass->GetPathName());
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
	if (ClassWithinStr.IsEmpty() == false)
	{
		FUnrealClassDefinitionInfo* RequiredWithinClassDef = FUnrealClassDefinitionInfo::FindClass(*ClassWithinStr);
		if (!RequiredWithinClassDef)
		{
			FUHTException::Throwf(*this, TEXT("Within class '%s' not found."), *ClassWithinStr);
		}
		UClass* RequiredWithinClass = RequiredWithinClassDef->GetClass();
		if (RequiredWithinClass->IsChildOf(UInterface::StaticClass()))
		{
			FUHTException::Throwf(*this, TEXT("Classes cannot be 'within' interfaces"));
		}
		else if (Class->ClassWithin == NULL || Class->ClassWithin == UObject::StaticClass() || RequiredWithinClass->IsChildOf(Class->ClassWithin))
		{
			Class->ClassWithin = RequiredWithinClass;
		}
		else if (Class->ClassWithin != RequiredWithinClass)
		{
			FUHTException::Throwf(*this, TEXT("%s must be within %s, not %s"), *Class->GetPathName(), *Class->ClassWithin->GetPathName(), *RequiredWithinClass->GetPathName());
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
		FUHTException::Throwf(*this, TEXT("Parent class declared within '%s'.  Cannot override within class with '%s' since it isn't a child"), *ExpectedWithin->GetName(), *Class->ClassWithin->GetName());
	}

	ClassWithin = &GTypeDefinitionInfoMap.FindChecked<FUnrealClassDefinitionInfo>(Class->ClassWithin);
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

FString FUnrealClassDefinitionInfo::GetNameWithPrefix(EEnforceInterfacePrefix EnforceInterfacePrefix) const
{
	const TCHAR* Prefix = 0;

	if (HasAnyClassFlags(CLASS_Interface))
	{
		// Grab the expected prefix for interfaces (U on the first one, I on the second one)
		switch (EnforceInterfacePrefix)
		{
		case EEnforceInterfacePrefix::None:
			// For old-style files: "I" for interfaces, unless it's the actual "Interface" class, which gets "U"
			if (GetFName() == NAME_Interface)
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
		Prefix = GetPrefixCPP();
	}

	return FString::Printf(TEXT("%s%s"), Prefix, *GetName());
}

FUnrealClassDefinitionInfo* FUnrealClassDefinitionInfo::GetSuperClass() const
{
	return UHTCast<FUnrealClassDefinitionInfo>(GetSuperStruct());
}

void FUnrealFunctionDefinitionInfo::AddProperty(TSharedRef<FUnrealPropertyDefinitionInfo> PropertyDef)
{
	check(PropertyDef->HasAnyPropertyFlags(CPF_Parm));

	if (PropertyDef->HasAnyPropertyFlags(CPF_ReturnParm))
	{
		check(ReturnProperty == nullptr);
		ReturnProperty = PropertyDef;
	}
	FUnrealStructDefinitionInfo::AddProperty(PropertyDef);
}

FUnrealFunctionDefinitionInfo* FUnrealFunctionDefinitionInfo::GetSuperFunction() const
{
	return UHTCast<FUnrealFunctionDefinitionInfo>(GetSuperStruct());
}

void FUnrealFunctionDefinitionInfo::CreateUObjectEngineTypesInternal()
{
	// Invoke the base class creatation
	FUnrealStructDefinitionInfo::CreateUObjectEngineTypesInternal();

	// We have to precreate the function prior to invoking the parent finalize
	UFunction* Function = nullptr;
	switch (FunctionType)
	{
	case EFunctionType::Function:
		Function = new(EC_InternalUseOnlyConstructor, GetOuter()->GetObject(), *GetNameCPP(), RF_Public) UFunction(FObjectInitializer(), nullptr);
		break;
	case EFunctionType::Delegate:
		Function = new(EC_InternalUseOnlyConstructor, GetOuter()->GetObject(), *GetNameCPP(), RF_Public) UDelegateFunction(FObjectInitializer(), nullptr);
		break;
	case EFunctionType::SparseDelegate:
	{
		USparseDelegateFunction* USPF = new(EC_InternalUseOnlyConstructor, GetOuter()->GetObject(), *GetNameCPP(), RF_Public) USparseDelegateFunction(FObjectInitializer(), nullptr);
		USPF->OwningClassName = SparseOwningClassName;
		USPF->DelegateName = SparseDelegateName;
		Function = USPF;
		break;
	}
	}
	check(Function);

	Function->ReturnValueOffset = MAX_uint16;
	Function->FirstPropertyToInit = nullptr;
	Function->FunctionFlags |= FunctionData.FunctionFlags;

	SetObject(Function);
	GTypeDefinitionInfoMap.Add(Function, SharedThis(this));

	if (FUnrealStructDefinitionInfo* StructDef = UHTCast<FUnrealStructDefinitionInfo>(GetOuter()))
	{
		UStruct* Struct = StructDef->GetStruct();
		Function->Next = Struct->Children;
		Struct->Children = Function;
	}
	Function->NumParms = uint8(GetProperties().Num());
	Function->Bind();
}

void FUnrealFunctionDefinitionInfo::PostParseFinalizeInternal()
{
	// Invoke the base class finalization
	FUnrealStructDefinitionInfo::PostParseFinalizeInternal();

	UFunction* Function = GetFunction();

	// The following code is only performed on functions in a class.  
	if (UHTCast<FUnrealClassDefinitionInfo>(GetOuter()) != nullptr)
	{

		// Fix up any structs that were used as a parameter in a delegate before being defined
		if (HasAnyFunctionFlags(FUNC_Delegate))
		{
			for (TSharedRef<FUnrealPropertyDefinitionInfo> PropertyDef : GetProperties())
			{
				if (PropertyDef->IsStructOrStructStaticArray())
				{
					FUnrealScriptStructDefinitionInfo& StructDef = UHTCastChecked<FUnrealScriptStructDefinitionInfo>(PropertyDef->GetPropertyBase().ClassDef);
					if (StructDef.HasAnyStructFlags(STRUCT_HasInstancedReference))
					{
						PropertyDef->SetPropertyFlags(CPF_ContainsInstancedReference);
					}
				}
			}
		}

		Function->StaticLink(true);

		// Compute the function parameter size, propagate some flags to the outer function, and save the return offset
		// Must be done in a second phase, as StaticLink resets various fields again!
		Function->ParmsSize = 0;
		for (TSharedRef<FUnrealPropertyDefinitionInfo> PropertyDef : GetProperties())
		{
			if (PropertyDef->HasSpecificPropertyFlags(CPF_ReturnParm | CPF_OutParm, CPF_OutParm))
			{
				SetFunctionFlags(FUNC_HasOutParms);
			}

			if (PropertyDef->IsStructOrStructStaticArray())
			{
				FUnrealScriptStructDefinitionInfo& StructDef = UHTCastChecked<FUnrealScriptStructDefinitionInfo>(PropertyDef->GetPropertyBase().ClassDef);
				if (StructDef.HasDefaults())
				{
					SetFunctionFlags(FUNC_HasDefaults);
				}
			}
		}
	}
}
