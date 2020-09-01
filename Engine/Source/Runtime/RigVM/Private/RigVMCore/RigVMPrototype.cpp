// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVMPrototype.h"
#include "RigVMCore/RigVMRegistry.h"
#include "RigVMCore/RigVMStruct.h"

////////////////////////////////////////////////////////////////////////////////////////////////

FRigVMPrototypeArg::FRigVMPrototypeArg()
	: Name(NAME_None)
	, Direction(ERigVMPinDirection::IO)
	, bSingleton(true)
{
}

FRigVMPrototypeArg::FRigVMPrototypeArg(FProperty* InProperty)
	: Name(InProperty->GetFName())
	, Direction(ERigVMPinDirection::IO)
	, bSingleton(true)
{
	FType Type(InProperty->GetCPPType());

	if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(InProperty))
	{
		InProperty = ArrayProperty->Inner;
	}

	if (FStructProperty* StructProperty = CastField<FStructProperty>(InProperty))
	{
		Type.CPPTypeObject = StructProperty->Struct;
	}
	else if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(InProperty))
	{
		Type.CPPTypeObject = EnumProperty->GetEnum();
	}

	Types.Add(Type);

#if WITH_EDITOR
	Direction = FRigVMStruct::GetPinDirectionFromProperty(InProperty);
#endif
}

bool FRigVMPrototypeArg::SupportsType(const FString& InCPPType) const
{
	return SupportsType(InCPPType, TArray<int32>());
}

bool FRigVMPrototypeArg::SupportsType(const FString& InCPPType, TArray<int32> InFunctionIndices) const
{
	if (InFunctionIndices.Num() == 0)
	{
		return Types.Contains(InCPPType);
	}

	for (int32 FunctionIndex : InFunctionIndices)
	{
		if (Types[FunctionIndex] == InCPPType)
		{
			return true;
		}
	}

	return false;
}

bool FRigVMPrototypeArg::IsSingleton() const
{
	if (bSingleton)
	{
		return true;
	}
	return IsSingleton(TArray<int32>());
}

bool FRigVMPrototypeArg::IsSingleton(TArray<int32> InFunctionIndices) const
{
	if (bSingleton)
	{
		return true;
	}
	else if(InFunctionIndices.Num() == 0)
	{
		return false;
	}

	FString TypeToCheck = Types[InFunctionIndices[0]];
	for (int32 FunctionIndex = 1; FunctionIndex < InFunctionIndices.Num(); FunctionIndex++)
	{
		if (Types[InFunctionIndices[FunctionIndex]] != TypeToCheck)
		{
			return false;
		}
	}
	return true;
}

TArray<FRigVMPrototypeArg::FType> FRigVMPrototypeArg::GetSupportedTypes() const
{
	TArray<FType> SupportedTypes;
	for (const FType& Type : Types)
	{
		SupportedTypes.AddUnique(Type);
	}
	return SupportedTypes;
}

TArray<FString> FRigVMPrototypeArg::GetSupportedTypeStrings() const
{
	TArray<FString> SupportedTypes;
	for (const FType& Type : Types)
	{
		SupportedTypes.AddUnique((FString)Type);
	}
	return SupportedTypes;
}

////////////////////////////////////////////////////////////////////////////////////////////////

FRigVMPrototype::FRigVMPrototype()
	: Index(INDEX_NONE)
	, Notation(NAME_None)
{

}

FRigVMPrototype::FRigVMPrototype(UScriptStruct* InStruct, const FString& InPrototypeName, int32 InFunctionIndex)
	: Index(INDEX_NONE)
	, Notation(NAME_None)
{
	TArray<FString> ArgNames;
	for (TFieldIterator<FProperty> It(InStruct); It; ++It)
	{
		FRigVMPrototypeArg Arg(*It);
		Args.Add(Arg);
		ArgNames.Add(Arg.Name.ToString());
	}

	if (ArgNames.Num() > 0)
	{
		FString NotationStr = FString::Printf(TEXT("%s(%s)"), *InPrototypeName, *FString::Join(ArgNames, TEXT(",")));
		Notation = *NotationStr;
		Functions.Add(InFunctionIndex);
	}
}

FName FRigVMPrototype::GetNotationFromStruct(UScriptStruct* InStruct, const FString& InPrototypeName)
{
	if (InStruct == nullptr || InPrototypeName.IsEmpty())
	{
		return NAME_None;
	}

	TArray<FString> ArgNames;
	for (TFieldIterator<FProperty> It(InStruct); It; ++It)
	{
		FRigVMPrototypeArg Arg(*It);
		ArgNames.Add(Arg.Name.ToString());
	}

	if (ArgNames.Num() == 0)
	{
		return NAME_None;
	}
	
	FString NotationStr = FString::Printf(TEXT("%s(%s)"), *InPrototypeName, *FString::Join(ArgNames, TEXT(",")));
	return *NotationStr;
}


bool FRigVMPrototype::IsValid() const
{
	return !Notation.IsNone();
}

const FName& FRigVMPrototype::GetNotation() const
{
	return Notation;
}

FName FRigVMPrototype::GetName() const
{
	FString Left;
	if (GetNotation().ToString().Split(TEXT("::"), &Left, nullptr))
	{
		return *Left;
	}
	return NAME_None;
}


bool FRigVMPrototype::IsCompatible(const FRigVMPrototype& InOther) const
{
	if (!IsValid() || !InOther.IsValid())
	{
		return false;
	}

	if(Notation != InOther.Notation)
	{
		return false;
	}

	for (int32 ArgIndex = 0; ArgIndex < Args.Num(); ArgIndex++)
	{
		if (Args[ArgIndex].Direction != InOther.Args[ArgIndex].Direction)
		{
			return false;
		}
	}

	return true;
}

bool FRigVMPrototype::Merge(const FRigVMPrototype& InOther)
{
	if (!IsCompatible(InOther))
	{
		return false;
	}

	if (InOther.Functions.Num() != 1)
	{
		return false;
	}

	TArray<FRigVMPrototypeArg> NewArgs;

	for (int32 ArgIndex = 0; ArgIndex < Args.Num(); ArgIndex++)
	{
		if (InOther.Args[ArgIndex].Types.Num() != 1)
		{
			return false;
		}

		NewArgs.Add(Args[ArgIndex]);
		NewArgs[ArgIndex].Types.Add(InOther.Args[ArgIndex].Types[0]);

		if (!Args[ArgIndex].Types.Contains(InOther.Args[ArgIndex].Types[0]))
		{
			NewArgs[ArgIndex].bSingleton = false;
		}
	}

	Args = NewArgs;

	Functions.Add(InOther.Functions[0]);
	return true;
}

const FRigVMPrototypeArg* FRigVMPrototype::FindArg(const FName& InArgName) const
{
	for (const FRigVMPrototypeArg& Arg : Args)
	{
		if (Arg.Name == InArgName)
		{
			return &Arg;
		}
	}
	return nullptr;
}

bool FRigVMPrototype::ArgSupportsType(const FName& InArgName, const FString& InCPPType, const FRigVMPrototype::FTypeMap& InTypes) const
{
	if (const FRigVMPrototypeArg* Arg = FindArg(InArgName))
	{
		if(InTypes.Num() == 0)
		{
			return Arg->SupportsType(InCPPType);
		}

		FTypeMap Types = InTypes;
		Types.FindOrAdd(InArgName) = InCPPType;

		int32 FunctionIndex = INDEX_NONE;
		Resolve(Types, FunctionIndex);

		return Types.FindChecked(InArgName) == InCPPType;
	}
	return false;
}

const FRigVMFunction* FRigVMPrototype::GetFunction(int32 InIndex) const
{
	FRigVMRegistry& Registry = FRigVMRegistry::Get();
	return &Registry.GetFunctions()[Functions[InIndex]];
}

bool FRigVMPrototype::Resolve(FRigVMPrototype::FTypeMap& InOutTypes, int32& OutFunctionIndex) const
{
	FTypeMap InputTypes = InOutTypes;
	InOutTypes.Reset();

	OutFunctionIndex = INDEX_NONE;
	
	TArray<int32> FunctionIndices;
	for (int32 FunctionIndex = 0; FunctionIndex < Functions.Num(); FunctionIndex++)
	{
		FunctionIndices.Add(FunctionIndex);
	}
	
	for (const FRigVMPrototypeArg& Arg : Args)
	{
		if (Arg.bSingleton)
		{
			InOutTypes.Add(Arg.Name, Arg.Types[0]);
		}
		else if (const FRigVMPrototypeArg::FType* InputType = InputTypes.Find(Arg.Name))
		{
			bool bFoundMatch = false;
			for (int32 FunctionIndex = 0; FunctionIndex < Arg.Types.Num(); FunctionIndex++)
			{
				if (Arg.Types[FunctionIndex] != *InputType)
				{
					FunctionIndices.Remove(FunctionIndex);
				}
				else
				{
					bFoundMatch = true;
				}
			}
			InOutTypes.Add(Arg.Name, bFoundMatch ? *InputType : FRigVMPrototypeArg::FType());
		}
		else
		{
			InOutTypes.Add(Arg.Name, FRigVMPrototypeArg::FType());
		}
	}

	if (FunctionIndices.Num() == 1)
	{
		OutFunctionIndex = Functions[FunctionIndices[0]];

		InOutTypes.Reset();
		for (const FRigVMPrototypeArg& Arg : Args)
		{
			InOutTypes.Add(Arg.Name, Arg.Types[FunctionIndices[0]]);
		}
	}
	else if (FunctionIndices.Num() > 1)
	{
		for (const FRigVMPrototypeArg& Arg : Args)
		{
			if (Arg.IsSingleton(FunctionIndices))
			{
				InOutTypes.FindChecked(Arg.Name) = Arg.Types[FunctionIndices[0]];
			}
		}
	}

	return FunctionIndices.Num() == 1;
}

#if WITH_EDITOR

FString FRigVMPrototype::GetCategory() const
{
	FString Category;
	GetFunction(0)->Struct->GetStringMetaDataHierarchical(FRigVMStruct::CategoryMetaName, &Category);

	if (Category.IsEmpty())
	{
		return Category;
	}

	for (int32 FunctionIndex = 1; FunctionIndex < NumFunctions(); FunctionIndex++)
	{
		FString OtherCategory;
		if (GetFunction(FunctionIndex)->Struct->GetStringMetaDataHierarchical(FRigVMStruct::CategoryMetaName, &OtherCategory))
		{
			while (!OtherCategory.StartsWith(Category, ESearchCase::IgnoreCase))
			{
				FString Left;
				if (Category.Split(TEXT("|"), &Left, nullptr, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
				{
					Category = Left;
				}
				else
				{
					return FString();
				}

			}
		}
	}

	return Category;
}

FString FRigVMPrototype::GetKeywords() const
{
	FString KeywordsMetadata = GetName().ToString();

	for (int32 FunctionIndex = 0; FunctionIndex < NumFunctions(); FunctionIndex++)
	{
		if (const FRigVMFunction* Function = GetFunction(FunctionIndex))
		{
			FString FunctionKeyWordsMetadata;
			Function->Struct->GetStringMetaDataHierarchical(FRigVMStruct::KeywordsMetaName, &FunctionKeyWordsMetadata);

			if (!FunctionKeyWordsMetadata.IsEmpty())
			{
				if (KeywordsMetadata.IsEmpty())
				{
					KeywordsMetadata = FunctionKeyWordsMetadata;
				}
				else
				{
					KeywordsMetadata = KeywordsMetadata + TEXT(",") + FunctionKeyWordsMetadata;
				}
			}
		}
	}

	return KeywordsMetadata;
}

#endif