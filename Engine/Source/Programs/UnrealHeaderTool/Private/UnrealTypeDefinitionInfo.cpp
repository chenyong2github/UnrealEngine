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

namespace
{
	const FName NAME_IgnoreCategoryKeywordsInSubclasses(TEXT("IgnoreCategoryKeywordsInSubclasses"));
	const FName NAME_ReplaceConverted(TEXT("ReplaceConverted"));
	const FName NAME_ClassGroupNames(TEXT("ClassGroupNames"));
	const FName NAME_EditorConfig(TEXT("EditorConfig"));
	const FName NAME_AdvancedClassDisplay(TEXT("AdvancedClassDisplay"));

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

	bool IsActorClass(const FUnrealClassDefinitionInfo& ClassDef)
	{
		for (const FUnrealClassDefinitionInfo* TestDef = &ClassDef; TestDef; TestDef = TestDef->GetSuperClass())
		{
			if (TestDef->GetFName() == NAME_Actor)
			{
				return true;
			}
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
			TypeDef.LogWarning(TEXT("Remapping old metadata key '%s' to new key '%s', please update the declaration."), *CurrentKey.ToString(), *NewKey.ToString());
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
		Result = GetMetaDataMap().Find(Key);
	}
#if UHT_ENABLE_ENGINE_TYPE_CHECKS
	CheckFindMetaData(Key, Result);
#endif
	return Result;
}

void FUHTMetaData::SetMetaDataHelper(const FName& Key, const TCHAR* InValue) 
{
	GetMetaDataMap().Add(Key, InValue);
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

void FUHTMetaData::GetStringArrayMetaData(const FName& Key, TArray<FString>& Out) const
{
	if (const FString* String = FindMetaData(Key))
	{
		String->ParseIntoArray(Out, TEXT(" "), true);
	}
}

TArray<FString> FUHTMetaData::GetStringArrayMetaData(const FName& Key) const
{
	TArray<FString> Out;
	GetStringArrayMetaData(Key, Out);
	return Out;
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
		Throwf(TEXT("Attempt to fetch the scope for type \"%s\" when it doesn't implement the method or there is no source file associated with the type."), *GetNameCPP());
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
		Throwf(TEXT("Attempt to fetch the generated hash for type \"%s\" before it has been generated.  Include dependencies, topological sort, or job graph is in error."), *GetNameCPP());
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

void FUnrealTypeDefinitionInfo::CreateUObjectEngineTypes(ECreateEngineTypesPhase Phase)
{
	EFinalizeState& FinalizeState = CreateUObectEngineTypesState[uint8(Phase)];
	switch (FinalizeState)
	{
	case EFinalizeState::None:
		FinalizeState = EFinalizeState::InProgress;
		CreateUObjectEngineTypesInternal(Phase);
		FinalizeState = EFinalizeState::Finished;
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
			Throwf(TEXT("Metadata value for '%s' is non-numeric : '%s'"), *InKey.ToString(), *InValue);
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
					Throwf(TEXT("%s doesn't make sense on static method '%s' in a blueprint function library"), *InKey.ToString(), *FuncDef->GetName());
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
				FuncDef->LogError(TEXT("Commutative associative binary operators must have exactly 2 parameters of the same type and a return value."));
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
						FuncDef->LogError(TEXT("Function does not have a parameter named '%s'"), *Entry);
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
								FuncDef->LogError(TEXT("Function already specified an ExpandEnumAsExec input (%s), but '%s' is also an input parameter. Only one is permitted."), *FirstInputDef->GetName(), *Entry);
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
			Throwf(TEXT("'%s' metadata was '%s' but it must be %s or %s"), *InKey.ToString(), *InValue, *ExperimentalValue, *EarlyAccessValue);
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
				Throwf(TEXT("'Units' meta data can only be applied to numeric and struct properties"));
			}
		}

		if (!FUnitConversion::UnitFromString(*InValue))
		{
			Throwf(TEXT("Unrecognized units (%s) specified for property '%s'"), *InValue, *GetFullName());
		}
	}
	break;

	case ECheckedMetadataSpecifier::DocumentationPolicy:
	{
		const TCHAR* StrictValue = TEXT("Strict");
		if (InValue != StrictValue)
		{
			Throwf(TEXT("'%s' metadata was '%s' but it must be %s"), *InKey.ToString(), *InValue, *StrictValue);
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

FUnrealObjectDefinitionInfo::FUnrealObjectDefinitionInfo(UObject* InObject)
	: FUnrealTypeDefinitionInfo(InObject->GetName())
	, Name(InObject->GetFName())
	, InternalObjectFlags(InObject->GetInternalFlags())
{
	SetObject(InObject);
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

void FUnrealObjectDefinitionInfo::AddMetaData(TMap<FName, FString>&& InMetaData)
{
	// only add if we have some!
	if (InMetaData.Num())
	{

		if (TMap<FName, FString>* UObjectMetaDataMap = GetUObjectMetaDataMap())
		{
			UObjectMetaDataMap->Append(InMetaData);
		}
		else if (UMetaData* UObjectMetaData = GetUObjectMetaData())
		{
			UObjectMetaData->SetObjectValues(GetObject(), InMetaData); // This is valid
		}

		GetMetaDataMap().Append(MoveTemp(InMetaData));
	}
}

void FUnrealObjectDefinitionInfo::SetMetaDataHelper(const FName& Key, const TCHAR* InValue)
{
	if (TMap<FName, FString>* UObjectMetaDataMap = GetUObjectMetaDataMap())
	{
		UObjectMetaDataMap->Add(Key, InValue);
	}
	else if (UMetaData* UObjectMetaData = GetUObjectMetaData())
	{
		UObjectMetaData->SetValue(GetObject(), Key, InValue); // This is valid
	}

	GetMetaDataMap().Add(Key, InValue);
}

FUnrealPackageDefinitionInfo::FUnrealPackageDefinitionInfo(const FManifestModule& InModule, UPackage* InPackage)
	: FUnrealObjectDefinitionInfo(InPackage)
	, Module(InModule)
	, ShortUpperName(FPackageName::GetShortName(InPackage).ToUpper())
	, API(FString::Printf(TEXT("%s_API "), *ShortUpperName))
{
}

void FUnrealPackageDefinitionInfo::CreateUObjectEngineTypesInternal(ECreateEngineTypesPhase Phase)
{
	for (TSharedRef<FUnrealSourceFile>& LocalSourceFile : GetAllSourceFiles())
	{
		for (TSharedRef<FUnrealTypeDefinitionInfo>& TypeDef : LocalSourceFile->GetDefinedTypes())
		{
			TypeDef->CreateUObjectEngineTypes(Phase);
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

FUnrealEnumDefinitionInfo::FUnrealEnumDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, FString&& InNameCPP, FName InName, UEnum::ECppForm InCppForm, EUnderlyingEnumType InUnderlyingType)
	: FUnrealFieldDefinitionInfo(InSourceFile, InLineNumber, MoveTemp(InNameCPP), InName, InSourceFile.GetPackageDef())
	, UnderlyingType(InUnderlyingType)
	, CppForm(InCppForm)
{ }

FString FUnrealEnumDefinitionInfo::GenerateEnumPrefix() const
{
	FString Prefix;
	if (Names.Num() > 0)
	{
		Names[0].Key.ToString(Prefix);

		// For each item in the enumeration, trim the prefix as much as necessary to keep it a prefix.
		// This ensures that once all items have been processed, a common prefix will have been constructed.
		// This will be the longest common prefix since as little as possible is trimmed at each step.
		for (int32 NameIdx = 1; NameIdx < Names.Num(); NameIdx++)
		{
			FString EnumItemName = Names[NameIdx].Key.ToString();

			// Find the length of the longest common prefix of Prefix and EnumItemName.
			int32 PrefixIdx = 0;
			while (PrefixIdx < Prefix.Len() && PrefixIdx < EnumItemName.Len() && Prefix[PrefixIdx] == EnumItemName[PrefixIdx])
			{
				PrefixIdx++;
			}

			// Trim the prefix to the length of the common prefix.
			Prefix.LeftInline(PrefixIdx, false);
		}

		// Find the index of the rightmost underscore in the prefix.
		int32 UnderscoreIdx = Prefix.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);

		// If an underscore was found, trim the prefix so only the part before the rightmost underscore is included.
		if (UnderscoreIdx > 0)
		{
			Prefix.LeftInline(UnderscoreIdx, false);
		}
		else
		{
			// no underscores in the common prefix - this probably indicates that the names
			// for this enum are not using Epic's notation, so just empty the prefix so that
			// the max item will use the full name of the enum
			Prefix.Empty();
		}
	}

	// If no common prefix was found, or if the enum does not contain any entries,
	// use the name of the enumeration instead.
	if (Prefix.Len() == 0)
	{
		Prefix = GetName();
	}

#if UHT_ENABLE_ENGINE_TYPE_CHECKS
	check(GetEnumSafe() == nullptr || GetEnumSafe()->GenerateEnumPrefix() == Prefix);
#endif
	return Prefix;
}

FString FUnrealEnumDefinitionInfo::GenerateFullEnumName(const TCHAR* InEnumName) const
{
	if (GetCppForm() == UEnum::ECppForm::Regular || UEnum::IsFullEnumName(InEnumName))
	{
		return InEnumName;
	}

	return FString::Printf(TEXT("%s::%s"), *GetName(), InEnumName);
}

bool FUnrealEnumDefinitionInfo::ContainsExistingMax() const
{
	if (GetIndexByName(*GenerateFullEnumName(TEXT("MAX")), EGetByNameFlags::CaseSensitive) != INDEX_NONE)
	{
#if UHT_ENABLE_ENGINE_TYPE_CHECKS
		check(GetEnumSafe() == nullptr || GetEnumSafe()->ContainsExistingMax() == true);
#endif
		return true;
	}

	FName MaxEnumItem = *GenerateFullEnumName(*(GenerateEnumPrefix() + TEXT("_MAX")));
	if (GetIndexByName(MaxEnumItem, EGetByNameFlags::CaseSensitive) != INDEX_NONE)
	{
#if UHT_ENABLE_ENGINE_TYPE_CHECKS
		check(GetEnumSafe() == nullptr || GetEnumSafe()->ContainsExistingMax() == true);
#endif
		return true;
	}

#if UHT_ENABLE_ENGINE_TYPE_CHECKS
	check(GetEnumSafe() == nullptr || GetEnumSafe()->ContainsExistingMax() == false);
#endif
	return false;
}

int64 FUnrealEnumDefinitionInfo::GetMaxEnumValue() const
{
	int32 NamesNum = Names.Num();
	if (NamesNum == 0)
	{
		return 0;
	}

	int64 MaxValue = Names[0].Value;
	for (int32 i = 0; i < NamesNum; ++i)
	{
		int64 CurrentValue = Names[i].Value;
		if (CurrentValue > MaxValue)
		{
			MaxValue = CurrentValue;
		}
	}

#if UHT_ENABLE_ENGINE_TYPE_CHECKS
	check(GetEnumSafe() == nullptr || GetEnumSafe()->GetMaxEnumValue() == MaxValue);
#endif
	return MaxValue;
}

FName FUnrealEnumDefinitionInfo::GetNameByIndex(int32 Index) const
{
	if (Names.IsValidIndex(Index))
	{
#if UHT_ENABLE_ENGINE_TYPE_CHECKS
		check(GetEnumSafe() == nullptr || GetEnumSafe()->GetNameByIndex(Index) == Names[Index].Key);
#endif
		return Names[Index].Key;
	}

#if UHT_ENABLE_ENGINE_TYPE_CHECKS
	check(GetEnumSafe() == nullptr || GetEnumSafe()->GetNameByIndex(Index) == NAME_None);
#endif
	return NAME_None;
}

bool FUnrealEnumDefinitionInfo::IsValidEnumValue(int64 InValue) const
{
	int32 NamesNum = Names.Num();
	for (int32 i = 0; i < NamesNum; ++i)
	{
		int64 CurrentValue = Names[i].Value;
		if (CurrentValue == InValue)
		{
#if UHT_ENABLE_ENGINE_TYPE_CHECKS
			check(GetEnumSafe() == nullptr || GetEnumSafe()->IsValidEnumValue(InValue) == true);
#endif
			return true;
		}
	}

#if UHT_ENABLE_ENGINE_TYPE_CHECKS
	check(GetEnumSafe() == nullptr || GetEnumSafe()->IsValidEnumValue(InValue) == false);
#endif
	return false;
}

int32 FUnrealEnumDefinitionInfo::GetIndexByName(const FName InName, EGetByNameFlags Flags) const
{
	ENameCase ComparisonMethod = !!(Flags & EGetByNameFlags::CaseSensitive) ? ENameCase::CaseSensitive : ENameCase::IgnoreCase;

	// First try the fast path
	const int32 Count = Names.Num();
	for (int32 Counter = 0; Counter < Count; ++Counter)
	{
		if (Names[Counter].Key.IsEqual(InName, ComparisonMethod))
		{
#if UHT_ENABLE_ENGINE_TYPE_CHECKS
			check(GetEnumSafe() == nullptr || GetEnumSafe()->GetIndexByName(InName, Flags) == Counter);
#endif
			return Counter;
		}
	}

	// Otherwise see if it is in the redirect table
	int32 Result = GetIndexByNameString(InName.ToString(), Flags);
	return Result;
}

int32 FUnrealEnumDefinitionInfo::GetIndexByNameString(const FString& InSearchString, EGetByNameFlags Flags) const
{
	ENameCase         NameComparisonMethod = !!(Flags & EGetByNameFlags::CaseSensitive) ? ENameCase::CaseSensitive : ENameCase::IgnoreCase;
	ESearchCase::Type StringComparisonMethod = !!(Flags & EGetByNameFlags::CaseSensitive) ? ESearchCase::CaseSensitive : ESearchCase::IgnoreCase;

	FString SearchEnumEntryString = InSearchString;
	FString ModifiedEnumEntryString;

	// Strip or add the namespace
	int32 DoubleColonIndex = SearchEnumEntryString.Find(TEXT("::"), ESearchCase::CaseSensitive);
	if (DoubleColonIndex == INDEX_NONE)
	{
		ModifiedEnumEntryString = GenerateFullEnumName(*SearchEnumEntryString);
	}
	else
	{
		ModifiedEnumEntryString = SearchEnumEntryString.RightChop(DoubleColonIndex + 2);
	}

	if (DoubleColonIndex != INDEX_NONE)
	{
		// If we didn't find a value redirect and our original string was namespaced, we need to fix the namespace now as it may have changed due to enum type redirect
		SearchEnumEntryString = GenerateFullEnumName(*ModifiedEnumEntryString);
	}

	// Search for names both with and without namespace
	FName SearchName = FName(*SearchEnumEntryString, FNAME_Find);
	FName ModifiedName = FName(*ModifiedEnumEntryString, FNAME_Find);

	const int32 Count = Names.Num();
	for (int32 Counter = 0; Counter < Count; ++Counter)
	{
		if (Names[Counter].Key.IsEqual(SearchName, NameComparisonMethod) || Names[Counter].Key.IsEqual(ModifiedName, NameComparisonMethod))
		{
			return Counter;
		}
	}

	if (!InSearchString.Equals(SearchEnumEntryString, StringComparisonMethod))
	{
		// There was an actual redirect, and we didn't find it
		//UE_LOG(LogEnum, Warning, TEXT("EnumRedirect for enum %s maps '%s' to invalid value '%s'!"), *GetName(), *InSearchString, *SearchEnumEntryString);
	}
	return INDEX_NONE;
}

FString FUnrealEnumDefinitionInfo::GetNameStringByIndex(int32 InIndex) const
{
	if (Names.IsValidIndex(InIndex))
	{
		FName EnumEntryName = GetNameByIndex(InIndex);
		if (CppForm == UEnum::ECppForm::Regular)
		{
			return EnumEntryName.ToString();
		}

		// Strip the namespace from the name.
		FString EnumNameString(EnumEntryName.ToString());
		int32 ScopeIndex = EnumNameString.Find(TEXT("::"), ESearchCase::CaseSensitive);
		if (ScopeIndex != INDEX_NONE)
		{
			return EnumNameString.Mid(ScopeIndex + 2);
		}
	}
	return FString();
}

void FUnrealEnumDefinitionInfo::CreateUObjectEngineTypesInternal(ECreateEngineTypesPhase Phase)
{
	FUnrealFieldDefinitionInfo::CreateUObjectEngineTypesInternal(Phase);

	switch (Phase)
	{
	case ECreateEngineTypesPhase::Phase1:
	{
		UPackage* Package = GetPackageDef().GetPackage();
		const FString& EnumName = GetNameCPP();

		// Create enum definition.
		UEnum* Enum = new(EC_InternalUseOnlyConstructor, Package, FName(EnumName), RF_Public) UEnum(FObjectInitializer());
		Enum->SetEnums(Names, CppForm, EnumFlags, false);
		Enum->CppType = CppType;
		Enum->GetPackage()->GetMetaData()->SetObjectValues(Enum, GetMetaDataMap());
		SetObject(Enum);
		GTypeDefinitionInfoMap.AddObjectLookup(Enum, SharedThis(this));
		break;
	}

	case ECreateEngineTypesPhase::Phase2:
		break;
	}
}

FUnrealStructDefinitionInfo::FUnrealStructDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, FString&& InNameCPP, FName InName, FUnrealObjectDefinitionInfo& InOuter)
	: FUnrealFieldDefinitionInfo(InSourceFile, InLineNumber, MoveTemp(InNameCPP), InName, InOuter)
	, StructScope(MakeShared<FStructScope>(*this, &InSourceFile.GetScope().Get()))
{
}

bool FUnrealStructDefinitionInfo::IsChildOf(const FUnrealStructDefinitionInfo& SomeBase) const
{
	for (const FUnrealStructDefinitionInfo* Current = this; Current; Current = Current->GetSuperStructInfo().Struct)
	{
		if (Current == &SomeBase)
		{
#if UHT_ENABLE_ENGINE_TYPE_CHECKS
			check(GetStructSafe() == nullptr || SomeBase.GetStructSafe() == nullptr || GetStructSafe()->IsChildOf(SomeBase.GetStructSafe()) == true);
#endif
			return true;
		}
	}
#if UHT_ENABLE_ENGINE_TYPE_CHECKS
	check(GetStructSafe() == nullptr || SomeBase.GetStructSafe() == nullptr || GetStructSafe()->IsChildOf(SomeBase.GetStructSafe()) == false);
#endif
	return false;
}

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

void FUnrealStructDefinitionInfo::CreateUObjectEngineTypesInternal(ECreateEngineTypesPhase Phase)
{
	FUnrealFieldDefinitionInfo::CreateUObjectEngineTypesInternal(Phase);

	switch (Phase)
	{
	case ECreateEngineTypesPhase::Phase1:
		if (SuperStructInfo.Struct)
		{
			SuperStructInfo.Struct->CreateUObjectEngineTypes(Phase);
		}

		for (const FBaseStructInfo& Info : BaseStructInfos)
		{
			if (Info.Struct)
			{
				Info.Struct->CreateUObjectEngineTypes(Phase);
			}
		}
		break;

	case ECreateEngineTypesPhase::Phase2:
		for (TSharedRef<FUnrealFunctionDefinitionInfo> FunctionDef : Functions)
		{
			FunctionDef->CreateUObjectEngineTypes(Phase);
		}
		break;
	}
}

void FUnrealStructDefinitionInfo::PostParseFinalizeInternal()
{
	FUnrealFieldDefinitionInfo::PostParseFinalizeInternal();

	if (SuperStructInfo.Struct)
	{
		SuperStructInfo.Struct->PostParseFinalize();
	}

	for (const FBaseStructInfo& Info : BaseStructInfos)
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
}

FUnrealScriptStructDefinitionInfo::FUnrealScriptStructDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, FString&& InNameCPP, FName InName)
	: FUnrealStructDefinitionInfo(InSourceFile, InLineNumber, MoveTemp(InNameCPP), InName, InSourceFile.GetPackageDef())
{ }

uint32 FUnrealScriptStructDefinitionInfo::GetHash(bool bIncludeNoExport) const
{
	if (!bIncludeNoExport)
	{
		if (HasAnyStructFlags(STRUCT_NoExport))
		{
			return 0;
		}
	}
	return FUnrealStructDefinitionInfo::GetHash(bIncludeNoExport);
}

void FUnrealScriptStructDefinitionInfo::CreateUObjectEngineTypesInternal(ECreateEngineTypesPhase Phase)
{
	FUnrealStructDefinitionInfo::CreateUObjectEngineTypesInternal(Phase);

	switch (Phase)
	{
	case ECreateEngineTypesPhase::Phase1:
	{
		UPackage* Package = GetPackageDef().GetPackage();
		const FString& StructName = GetNameCPP();
		FString StructNameStripped = GetClassNameWithPrefixRemoved(*StructName);

		UScriptStruct* ScriptStruct = new(EC_InternalUseOnlyConstructor, Package, FName(StructNameStripped), RF_Public) UScriptStruct(FObjectInitializer());
		ScriptStruct->StructFlags = StructFlags;
		ScriptStruct->GetPackage()->GetMetaData()->SetObjectValues(ScriptStruct, GetMetaDataMap());
		if (FUnrealStructDefinitionInfo* SuperStructDef = GetSuperStruct())
		{
			ScriptStruct->SetSuperStruct(SuperStructDef->GetStruct());
		}
		SetObject(ScriptStruct);
		GTypeDefinitionInfoMap.AddObjectLookup(ScriptStruct, SharedThis(this));

		// The structure needs to be prepared at this point.  
		ScriptStruct->PrepareCppStructOps();
		break;
	}

	case ECreateEngineTypesPhase::Phase2:
		break;
	}
}

void FUnrealScriptStructDefinitionInfo::PostParseFinalizeInternal()
{
	FUnrealStructDefinitionInfo::PostParseFinalizeInternal();
}

FUnrealClassDefinitionInfo::FUnrealClassDefinitionInfo(FUnrealSourceFile& InSourceFile, int32 InLineNumber, FString&& InNameCPP, FName InName, bool bInIsInterface)
	: FUnrealStructDefinitionInfo(InSourceFile, InLineNumber, MoveTemp(InNameCPP), InName, InSourceFile.GetPackageDef())
	, bIsInterface(bInIsInterface)
{
	if (bInIsInterface)
	{
		ClassFlags |= CLASS_Interface;
		GetStructMetaData().ParsedInterface = EParsedInterface::ParsedUInterface;
	}
}

/**
 * Returns the struct/ class prefix used for the C++ declaration of this struct/ class.
 * Classes deriving from AActor have an 'A' prefix and other UObject classes an 'U' prefix.
 *
 * @return Prefix character used for C++ declaration of this struct/ class.
 */
const TCHAR* FUnrealClassDefinitionInfo::GetPrefixCPP() const
{
	if (IsActorClass(*this))
	{
		if (HasAnyClassFlags(CLASS_Deprecated))
		{
			return TEXT("ADEPRECATED_");
		}
		else
		{
			return TEXT("A");
		}
	}
	else
	{
		if (HasAnyClassFlags(CLASS_Deprecated))
		{
			return TEXT("UDEPRECATED_");
		}
		else
		{
			return TEXT("U");
		}
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

	Parser.Throwf(MoveTemp(ErrorMsg));

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
		if (HasAnyClassFlags(CLASS_NoExport))
		{
			return 0;
		}
	}
	return FUnrealStructDefinitionInfo::GetHash(bIncludeNoExport);
}

void FUnrealClassDefinitionInfo::CreateUObjectEngineTypesInternal(ECreateEngineTypesPhase Phase)
{
	FUnrealStructDefinitionInfo::CreateUObjectEngineTypesInternal(Phase);

	switch (Phase)
	{
	case ECreateEngineTypesPhase::Phase1:
	{
		check(ClassWithin);
		if (GetObjectSafe() == nullptr)
		{
			UPackage* Package = GetPackageDef().GetPackage();
			const FString& ClassName = GetNameCPP();
			FString ClassNameStripped = GetClassNameWithPrefixRemoved(*ClassName);

			// Create new class.
			UClass* Class = new(EC_InternalUseOnlyConstructor, Package, *ClassNameStripped, RF_Public | RF_Standalone) UClass(FObjectInitializer(), nullptr);

			Class->ClassFlags = ClassFlags;
			Class->ClassCastFlags = ClassCastFlags;
			Class->PropertiesSize = PropertiesSize;
			Class->ClassConfigName = ClassConfigName;
			Class->ClassWithin = ClassWithin->GetClassSafe();
			Class->SetInternalFlags(GetInternalFlags());
			Class->GetPackage()->GetMetaData()->SetObjectValues(Class, GetMetaDataMap());

			// Setup the base class
			if (FUnrealClassDefinitionInfo* SuperClassDef = GetSuperClass())
			{
				Class->SetSuperStruct(SuperClassDef->GetClass());
			}

			// Add the class flags from the interfaces
			for (FUnrealStructDefinitionInfo::FBaseStructInfo& BaseClassInfo : GetBaseStructInfos())
			{
				if (FUnrealClassDefinitionInfo* BaseClassDef = UHTCast<FUnrealClassDefinitionInfo>(BaseClassInfo.Struct))
				{
					if (BaseClassDef->HasAnyClassFlags(CLASS_Interface))
					{
						Class->Interfaces.Emplace(BaseClassDef->GetClass(), 1, false);
					}
				}
			}
			SetObject(Class);
		}

		// Even if we already had an object, add it back to the types regardless
		GTypeDefinitionInfoMap.AddObjectLookup(GetObjectSafe(), SharedThis(this));
		break;
	}

	case ECreateEngineTypesPhase::Phase2:
		break;
	}
}

void FUnrealClassDefinitionInfo::PostParseFinalizeInternal()
{
	FUnrealStructDefinitionInfo::PostParseFinalizeInternal();

	if (IsInterface() && GetStructMetaData().ParsedInterface == EParsedInterface::ParsedUInterface)
	{
		FString UName = GetNameCPP();
		FString IName = TEXT("I") + UName.RightChop(1);
		Throwf(TEXT("UInterface '%s' parsed without a corresponding '%s'"), *UName, *IName);
	}

	FHeaderParser::CheckSparseClassData(*this);

	// Collect the class replication properties
	if (FUnrealClassDefinitionInfo* SuperClassDef = GetSuperClass())
	{
		ClassReps = SuperClassDef->GetClassReps();
		FirstOwnedClassRep = ClassReps.Num();
	}
	for (TSharedRef<FUnrealPropertyDefinitionInfo> PropertyDef : GetProperties())
	{
		if (PropertyDef->HasAnyPropertyFlags(CPF_Net))
		{
			ClassReps.Add(&*PropertyDef);
		}
	}

	// Initialize the class object
	UClass* Class = GetClass();

	// Clear the property size
	Class->PropertiesSize = 0;

	// Make the visible to the package
	Class->ClearFlags(RF_Transient);
	check(Class->HasAnyFlags(RF_Public));
	check(Class->HasAnyFlags(RF_Standalone));

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

bool FUnrealClassDefinitionInfo::ImplementsInterface(const FUnrealClassDefinitionInfo& SomeInterface) const
{
	if (SomeInterface.HasAnyClassFlags(CLASS_Interface) && &SomeInterface != GUInterfaceDef)
	{
		for (const FUnrealClassDefinitionInfo* CurrentClassDef = this; CurrentClassDef; CurrentClassDef = CurrentClassDef->GetSuperClass())
		{
			// SomeInterface might be a base interface of our implemented interface
			for (const FBaseStructInfo& BaseStructInfo : GetBaseStructInfos())
			{
				if (const FUnrealClassDefinitionInfo* InterfaceDef = UHTCast<FUnrealClassDefinitionInfo>(BaseStructInfo.Struct))
				{
					if (InterfaceDef->IsChildOf(SomeInterface))
					{
						return true;
					}
				}
			}
		}
	}
	return false;
}

void FUnrealClassDefinitionInfo::InitializeFromExistingUObject(UClass* Class)
{
	ClassFlags = Class->ClassFlags;
	ClassCastFlags = Class->ClassCastFlags;
	InitialEngineClassFlags = Class->ClassFlags;
	PropertiesSize = Class->PropertiesSize;
	ClassConfigName = Class->ClassConfigName;
	SetInternalFlags(Class->GetInternalFlags());
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

			ParsedMetaData.Add(NAME_IgnoreCategoryKeywordsInSubclasses, TEXT("true"));
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

			Throwf(TEXT("The dependsOn specifier is deprecated. Please use #include \"ClassHeaderFilename.h\" instead."));
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
			ParsedMetaData.Add(NAME_EditorConfig, FHeaderParser::RequireExactlyOneSpecifierValue(*this, PropSpecifier));
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
			ParsedMetaData.Add(NAME_AdvancedClassDisplay, TEXT("true"));
			break;

		case EClassMetadataSpecifier::ConversionRoot:

			ParsedMetaData.Add(FHeaderParserNames::NAME_IsConversionRoot, TEXT("true"));
			break;

		case EClassMetadataSpecifier::NeedsDeferredDependencyLoading:

			ParsedClassFlags |= CLASS_NeedsDeferredDependencyLoading;
			break;

		default:
			Throwf(TEXT("Unknown class specifier '%s'"), *PropSpecifier.Key);
		}
	}
	SetClassFlags(ParsedClassFlags);
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
	// Add parent categories. We store the opposite of HideCategories and HideFunctions in a separate array anyway.
	HideCategories.Append(GetStringArrayMetaData(FHeaderParserNames::NAME_HideCategories));
	ShowSubCatgories.Append(GetStringArrayMetaData(FHeaderParserNames::NAME_ShowCategories));
	HideFunctions.Append(GetStringArrayMetaData(FHeaderParserNames::NAME_HideFunctions));

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
	TArray<FString> ParentAutoExpandCategories = GetStringArrayMetaData(FHeaderParserNames::NAME_AutoExpandCategories);
	TArray<FString> ParentAutoCollapseCategories = GetStringArrayMetaData(FHeaderParserNames::NAME_AutoCollapseCategories);

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

void FUnrealClassDefinitionInfo::MergeAndValidateClassFlags(const FString& DeclaredClassName)
{
	if (bWantsToBePlaceable)
	{
		if (!HasAnyClassFlags(CLASS_NotPlaceable))
		{
			Throwf(TEXT("The 'placeable' specifier is only allowed on classes which have a base class that's marked as not placeable. Classes are assumed to be placeable by default."));
		}
		ClearClassFlags(CLASS_NotPlaceable);
		bWantsToBePlaceable = false; // Reset this flag after it's been merged
	}

	// Now merge all remaining flags/properties
	SetClassFlags(ParsedClassFlags);
	SetClassConfigName(FName(*ConfigName));

	SetAndValidateWithinClass();
	SetAndValidateConfigName();

	if (HasAnyClassFlags(CLASS_EditInlineNew))
	{
		// don't allow actor classes to be declared editinlinenew
		if (IsActorClass(*this))
		{
			Throwf(TEXT("Invalid class attribute: Creating actor instances via the property window is not allowed"));
		}
	}

	// Make sure both RequiredAPI and MinimalAPI aren't specified
	if (HasAllClassFlags(CLASS_MinimalAPI | CLASS_RequiredAPI))
	{
		Throwf(TEXT("MinimalAPI cannot be specified when the class is fully exported using a MODULENAME_API macro"));
	}

	// All classes must start with a valid Unreal prefix
	const FString ExpectedClassName = GetNameWithPrefix();
	if (DeclaredClassName != ExpectedClassName)
	{
		Throwf(TEXT("Class name '%s' is invalid, should be identified as '%s'"), *DeclaredClassName, *ExpectedClassName);
	}

	// This check only works if we already have an object.  This has to be moved to the engine type create code
	if (InitialEngineClassFlags != CLASS_None)
	{

		// if the class's class flags didn't contain CLASS_NoExport before it was parsed, it means either:
		// a) the DECLARE_CLASS macro for this native class doesn't contain the CLASS_NoExport flag (this is an error)
		// b) this is a new native class, which isn't yet hooked up to static registration (this is OK)
		if (HasAnyClassFlags(CLASS_NoExport) && !EnumHasAnyFlags(InitialEngineClassFlags, CLASS_NoExport))
		{
			if (!HasAnyClassFlags(CLASS_Intrinsic) && EnumHasAnyFlags(InitialEngineClassFlags, CLASS_Native))
			{
				Throwf(TEXT("'noexport': Must include CLASS_NoExport in native class declaration"));
			}
		}

		if (!HasAnyClassFlags(CLASS_Abstract) && EnumHasAnyFlags(InitialEngineClassFlags, CLASS_Abstract))
		{
			if (HasAnyClassFlags(CLASS_NoExport))
			{
				Throwf(TEXT("'abstract': NoExport class missing abstract keyword from class declaration (must change C++ version first)"));
				SetClassFlags(CLASS_Abstract);
			}
			else if (IsNative())
			{
				Throwf(TEXT("'abstract': missing abstract keyword from class declaration - class will no longer be exported as abstract"));
			}
		}
	}
}

void FUnrealClassDefinitionInfo::SetAndValidateConfigName()
{
	if (ConfigName.IsEmpty() == false)
	{
		// if the user specified "inherit", we're just going to use the parent class's config filename
		// this is not actually necessary but it can be useful for explicitly communicating config-ness
		if (ConfigName == TEXT("inherit"))
		{
			FUnrealClassDefinitionInfo* SuperClassDef = GetSuperClass();
			if (!SuperClassDef)
			{
				Throwf(TEXT("Cannot inherit config filename: %s has no super class"), *GetName());
			}

			if (SuperClassDef->GetClassConfigName() == NAME_None)
			{
				Throwf(TEXT("Cannot inherit config filename: parent class %s is not marked config."), *SuperClassDef->GetPathName());
			}
		}
		else
		{
			// otherwise, set the config name to the parsed identifier
			SetClassConfigName(FName(*ConfigName));
		}
	}
	else
	{
		// Invalidate config name if not specifically declared.
		SetClassConfigName(NAME_None);
	}
}

void FUnrealClassDefinitionInfo::SetAndValidateWithinClass()
{
	// Process all of the class specifiers
	if (ClassWithinStr.IsEmpty() == false)
	{
		FUnrealClassDefinitionInfo* RequiredWithinClassDef = FUnrealClassDefinitionInfo::FindClass(*ClassWithinStr);
		if (!RequiredWithinClassDef)
		{
			Throwf(TEXT("Within class '%s' not found."), *ClassWithinStr);
		}
		if (RequiredWithinClassDef->IsChildOf(*GUInterfaceDef))
		{
			Throwf(TEXT("Classes cannot be 'within' interfaces"));
		}
		else if (ClassWithin == NULL || ClassWithin == GUObjectDef || RequiredWithinClassDef->IsChildOf(*ClassWithin))
		{
			SetClassWithin(RequiredWithinClassDef);
		}
		else if (ClassWithin != RequiredWithinClassDef)
		{
			Throwf(TEXT("%s must be within %s, not %s"), *GetPathName(), *ClassWithin->GetPathName(), *RequiredWithinClassDef->GetPathName());
		}
	}
	else
	{
		// Make sure there is a valid within
		SetClassWithin(GetSuperClass() ? GetSuperClass()->GetClassWithin() : GUObjectDef);
	}

	FUnrealClassDefinitionInfo* ExpectedWithinDef = GetSuperClass() ? GetSuperClass()->GetClassWithin() : GUObjectDef;

	if (!ClassWithin->IsChildOf(*ExpectedWithinDef))
	{
		Throwf(TEXT("Parent class declared within '%s'.  Cannot override within class with '%s' since it isn't a child"), *ExpectedWithinDef->GetName(), *ClassWithin->GetName());
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
	GetStringArrayMetaData(FHeaderParserNames::NAME_SparseClassDataTypes, OutSparseClassDataTypes);
}

FString FUnrealClassDefinitionInfo::GetNameWithPrefix(EEnforceInterfacePrefix EnforceInterfacePrefix) const
{
	const TCHAR* Prefix = nullptr;

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

void FUnrealFunctionDefinitionInfo::CreateUObjectEngineTypesInternal(ECreateEngineTypesPhase Phase)
{
	// Invoke the base class creatation
	FUnrealStructDefinitionInfo::CreateUObjectEngineTypesInternal(Phase);

	switch (Phase)
	{
	case ECreateEngineTypesPhase::Phase1:
		break;

	case ECreateEngineTypesPhase::Phase2:
	{
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
		Function->GetPackage()->GetMetaData()->SetObjectValues(Function, GetMetaDataMap());

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
		break;
	}
	}
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
