// Copyright Epic Games, Inc. All Rights Reserved.


#include "ParserHelper.h"
#include "UnrealHeaderTool.h"
#include "Algo/Find.h"
#include "Misc/DefaultValueHelper.h"
#include "UnrealTypeDefinitionInfo.h"
#include "ClassMaps.h"

/////////////////////////////////////////////////////
// FClassMetaData

/**
 * Finds the metadata for the property specified
 *
 * @param	Prop	the property to search for
 *
 * @return	pointer to the metadata for the property specified, or NULL
 *			if the property doesn't exist in the list (for example, if it
 *			is declared in a package that is already compiled and has had its
 *			source stripped)
 */
FToken* FClassMetaData::FindTokenData(FProperty* Prop)
{
	check(Prop);
	FUnrealPropertyDefinitionInfo& PropDef = GTypeDefinitionInfoMap.FindChecked(Prop).AsPropertyChecked();
	return &PropDef.GetToken();
}

void FClassMetaData::AddProperty(FToken&& PropertyToken)
{
	FProperty* Prop = PropertyToken.TokenProperty;
	check(Prop);

	FUnrealPropertyDefinitionInfo& PropDef = GTypeDefinitionInfoMap.FindChecked(Prop).AsPropertyChecked();
	PropDef.GetToken() = MoveTemp(PropertyToken);

	// Add the property to the owner
	UObject* Outer = Prop->GetOwner<UObject>();
	check(Outer);
	GTypeDefinitionInfoMap.FindChecked(Outer).AsStructChecked().AddProperty(PropDef);

	// update the optimization flags
	if (!bContainsDelegates)
	{
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

void FClassMetaData::AddInheritanceParent(FString&& InParent, FUnrealSourceFile* UnrealSourceFile)
{
	MultipleInheritanceParents.Add(new FMultipleInheritanceBaseClass(MoveTemp(InParent)));
}

void FClassMetaData::AddInheritanceParent(UClass* ImplementedInterfaceClass, FUnrealSourceFile* UnrealSourceFile)
{
	MultipleInheritanceParents.Add(new FMultipleInheritanceBaseClass(ImplementedInterfaceClass));
}

/////////////////////////////////////////////////////
// FPropertyBase

const TCHAR* FPropertyBase::GetPropertyTypeText( EPropertyType Type )
{
	switch ( Type )
	{
		CASE_TEXT(CPT_None);
		CASE_TEXT(CPT_Byte);
		CASE_TEXT(CPT_Int8);
		CASE_TEXT(CPT_Int16);
		CASE_TEXT(CPT_Int);
		CASE_TEXT(CPT_Int64);
		CASE_TEXT(CPT_UInt16);
		CASE_TEXT(CPT_UInt32);
		CASE_TEXT(CPT_UInt64);
		CASE_TEXT(CPT_Bool);
		CASE_TEXT(CPT_Bool8);
		CASE_TEXT(CPT_Bool16);
		CASE_TEXT(CPT_Bool32);
		CASE_TEXT(CPT_Bool64);
		CASE_TEXT(CPT_Float);
		CASE_TEXT(CPT_Double);
		CASE_TEXT(CPT_ObjectReference);
		CASE_TEXT(CPT_Interface);
		CASE_TEXT(CPT_Name);
		CASE_TEXT(CPT_Delegate);
		CASE_TEXT(CPT_Struct);
		CASE_TEXT(CPT_String);
		CASE_TEXT(CPT_Text);
		CASE_TEXT(CPT_MulticastDelegate);
		CASE_TEXT(CPT_SoftObjectReference);
		CASE_TEXT(CPT_WeakObjectReference);
		CASE_TEXT(CPT_LazyObjectReference);
		CASE_TEXT(CPT_ObjectPtrReference);
		CASE_TEXT(CPT_Map);
		CASE_TEXT(CPT_Set);
		CASE_TEXT(CPT_FieldPath);
		CASE_TEXT(CPT_MAX);
	}

	return TEXT("");
}

/////////////////////////////////////////////////////
// FToken

/**
 * Copies the properties from this token into another.
 *
 * @param	Other	the token to copy this token's properties to.
 */
FToken& FToken::operator=(const FToken& Other)
{
	FPropertyBase::operator=((FPropertyBase&)Other);

	TokenType = Other.TokenType;
	TokenName = Other.TokenName;
	bTokenNameInitialized = Other.bTokenNameInitialized;
	StartPos = Other.StartPos;
	StartLine = Other.StartLine;
	TokenProperty = Other.TokenProperty;

	FCString::Strncpy(Identifier, Other.Identifier, NAME_SIZE);
	FMemory::Memcpy(String, Other.String, sizeof(String));

	return *this;
}

FToken& FToken::operator=(FToken&& Other)
{
	FPropertyBase::operator=(MoveTemp(Other));

	TokenType = Other.TokenType;
	TokenName = Other.TokenName;
	bTokenNameInitialized = Other.bTokenNameInitialized;
	StartPos = Other.StartPos;
	StartLine = Other.StartLine;
	TokenProperty = Other.TokenProperty;

	FCString::Strncpy(Identifier, Other.Identifier, NAME_SIZE);
	FMemory::Memcpy(String, Other.String, sizeof(String));

	return *this;
}

/////////////////////////////////////////////////////
// FAdvancedDisplayParameterHandler
static const FName NAME_AdvancedDisplay(TEXT("AdvancedDisplay"));

FAdvancedDisplayParameterHandler::FAdvancedDisplayParameterHandler(const TMap<FName, FString>* MetaData)
	: NumberLeaveUnmarked(-1), AlreadyLeft(0), bUseNumber(false)
{
	if(MetaData)
	{
		const FString* FoundString = MetaData->Find(NAME_AdvancedDisplay);
		if(FoundString)
		{
			FoundString->ParseIntoArray(ParametersNames, TEXT(","), true);
			for(int32 NameIndex = 0; NameIndex < ParametersNames.Num();)
			{
				FString& ParameterName = ParametersNames[NameIndex];
				ParameterName.TrimStartAndEndInline();
				if(ParameterName.IsEmpty())
				{
					ParametersNames.RemoveAtSwap(NameIndex);
				}
				else
				{
					++NameIndex;
				}
			}
			if(1 == ParametersNames.Num())
			{
				bUseNumber = FDefaultValueHelper::ParseInt(ParametersNames[0], NumberLeaveUnmarked);
			}
		}
	}
}

bool FAdvancedDisplayParameterHandler::ShouldMarkParameter(const FString& ParameterName)
{
	if(bUseNumber)
	{
		if(NumberLeaveUnmarked < 0)
		{
			return false;
		}
		if(AlreadyLeft < NumberLeaveUnmarked)
		{
			AlreadyLeft++;
			return false;
		}
		return true;
	}
	return ParametersNames.Contains(ParameterName);
}

bool FAdvancedDisplayParameterHandler::CanMarkMore() const
{
	return bUseNumber ? (NumberLeaveUnmarked > 0) : (0 != ParametersNames.Num());
}

FClassMetaData* FCompilerMetadataManager::AddClassData(UStruct* Struct, FUnrealSourceFile* UnrealSourceFile)
{
	TUniquePtr<FClassMetaData>* pClassData = Find(Struct);
	if (pClassData == NULL)
	{
		pClassData = &Emplace(Struct, new FClassMetaData());
	}

	return pClassData->Get();
}

FClassMetaData* FCompilerMetadataManager::AddInterfaceClassData(UStruct* Struct, FUnrealSourceFile* UnrealSourceFile)
{
	FClassMetaData* ClassData = AddClassData(Struct, UnrealSourceFile);
	ClassData->ParsedInterface = EParsedInterface::ParsedUInterface;
	InterfacesToVerify.Emplace(Struct, ClassData);
	return ClassData;
}

void FCompilerMetadataManager::CheckForNoIInterfaces()
{
	for (const TPair<UStruct*, FClassMetaData*>& StructDataPair : InterfacesToVerify)
	{
		if (StructDataPair.Value->ParsedInterface == EParsedInterface::ParsedUInterface)
		{
			FString Name = StructDataPair.Key->GetName();
			FError::Throwf(TEXT("UInterface 'U%s' parsed without a corresponding 'I%s'"), *Name, *Name);
		}
	}
	InterfacesToVerify.Reset();
}