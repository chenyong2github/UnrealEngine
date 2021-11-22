// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ShaderParamTypeDefinition.h"

#include "Algo/Find.h"
#include "Internationalization/Regex.h"
#include "Misc/DefaultValueHelper.h"
#include "Serialization/Archive.h"
#include "Templates/TypeHash.h"

// Storage for shared shader value types.
static uint32 GetTypeHash(const FShaderValueTypeHandle& InTypeHandle)
{
	return GetTypeHash(*InTypeHandle.ValueTypePtr);
}

struct HandleKeyFuncs : BaseKeyFuncs<FShaderValueTypeHandle,FShaderValueTypeHandle,false>
{
	static KeyInitType GetSetKey(ElementInitType Element)
	{
		return Element;
	}

	static bool Matches(KeyInitType A, KeyInitType B)
	{
		// The handle value can never be null here.
		return *A.ValueTypePtr == *B.ValueTypePtr;
	}

	static uint32 GetKeyHash(KeyInitType Key)
	{
		return GetTypeHash(*Key.ValueTypePtr);
	}
};

static TSet<FShaderValueTypeHandle, HandleKeyFuncs> GloballyKnownValueTypes;


bool FShaderValueTypeHandle::Serialize(FArchive& Ar)
{
	Ar << *this;
	return true;
}


FShaderValueTypeHandle FShaderValueType::Get(EShaderFundamentalType InType)
{
	if (InType == EShaderFundamentalType::Struct)
	{
		return {};
	}

	FShaderValueType ValueType;
	ValueType.Type = InType;
	ValueType.DimensionType = EShaderFundamentalDimensionType::Scalar;

	return GetOrCreate(MoveTemp(ValueType));
}

FShaderValueTypeHandle FShaderValueType::Get(EShaderFundamentalType InType, int32 InElemCount)
{
	if (InType == EShaderFundamentalType::Struct || InElemCount < 1 || InElemCount > 4)
	{
		return {};
	}

	FShaderValueType ValueType;
	ValueType.Type = InType;
	ValueType.DimensionType = EShaderFundamentalDimensionType::Vector;
	ValueType.VectorElemCount = InElemCount;

	return GetOrCreate(MoveTemp(ValueType));
}

FShaderValueTypeHandle FShaderValueType::Get(EShaderFundamentalType InType, int32 InRowCount, int32 InColumnCount)
{
	if (InType == EShaderFundamentalType::Struct || 
		InRowCount < 1 || InRowCount > 4 ||
		InColumnCount < 1 || InColumnCount > 4)
	{
		return {};
	}

	FShaderValueType ValueType;
	ValueType.Type = InType;
	ValueType.DimensionType = EShaderFundamentalDimensionType::Matrix;
	ValueType.MatrixRowCount = InRowCount;
	ValueType.MatrixColumnCount = InColumnCount;

	return GetOrCreate(MoveTemp(ValueType));
}

FShaderValueTypeHandle FShaderValueType::Get(
	FName InName, 
	std::initializer_list<FStructElement> InStructElements)
{
	if (InName == NAME_None)
	{
		return {};
	}

	// TODO: Check if the name and the element names are valid HLSL identifiers 
	// (i.e. identifier characters only, not a reserved keyword, and no duplicates).
	// TODO: Check if the name matches another struct with a different layout.

	FShaderValueType ValueType;
	ValueType.Name = InName;
	ValueType.Type = EShaderFundamentalType::Struct;
	ValueType.DimensionType = EShaderFundamentalDimensionType::Scalar;

	for (const FStructElement &StructElement : InStructElements)
	{
		// FIXME: We don't allow nested structs for now to avoid complicating the 
		// GetTypeDeclaration call too much.
		if (StructElement.Type.ValueTypePtr == nullptr || 
		    StructElement.Type.ValueTypePtr->Type == EShaderFundamentalType::Struct)
		{
			return {};
		}

		ValueType.StructElements.Add(StructElement);
	}

	// We don't allow empty structs.
	if (ValueType.StructElements.IsEmpty())
	{
		return {};
	}

	return GetOrCreate(MoveTemp(ValueType));
}


FShaderValueTypeHandle FShaderValueType::FromString(const FString& InTypeDecl)
{
	static const FRegexPattern ValueTypeDeclPattern(TEXT(R"(\s*(bool|int|uint|float)((?:[1-4])|(?:[1-4]x[1-4]))?\s*)"));

	// We really should have a FStringView version of the regex matcher. This level of string
	// copying is gross.
	FRegexMatcher Matcher(ValueTypeDeclPattern, InTypeDecl);
	if (!Matcher.FindNext())
	{
		return {};
	}

	FString FundamentalTypeStr = Matcher.GetCaptureGroup(1);
	FString DimensionTypeStr   = Matcher.GetCaptureGroup(2);

	EShaderFundamentalType FundamentalType;
	if (FundamentalTypeStr == TEXT("bool"))
	{
		FundamentalType = EShaderFundamentalType::Bool;
	}
	else if (FundamentalTypeStr == TEXT("int"))
	{
		FundamentalType = EShaderFundamentalType::Int;
	}
	else if (FundamentalTypeStr == TEXT("uint"))
	{
		FundamentalType = EShaderFundamentalType::Uint;
	}
	else if (FundamentalTypeStr == TEXT("float"))
	{
		FundamentalType = EShaderFundamentalType::Float;
	}
	else
	{
		return {};
	}

	if (DimensionTypeStr.Len() == 0)
	{
		return Get(FundamentalType);
	}
	else if (DimensionTypeStr.Len() == 1)
	{
		int32 Dim = 0;
		if (FDefaultValueHelper::ParseInt(DimensionTypeStr, Dim))
		{
			return Get(FundamentalType, Dim);
		}
	}
	else if (DimensionTypeStr.Len() == 3)
	{
		int32 Row = 0, Col = 0;
		if (FDefaultValueHelper::ParseInt(DimensionTypeStr.Left(1), Row) &&
			FDefaultValueHelper::ParseInt(DimensionTypeStr.Right(1), Col))
		{
			return Get(FundamentalType, Row, Col);
		}
	}

	// Failure to parse.
	return {};
}


FShaderValueTypeHandle FShaderValueType::GetOrCreate(FShaderValueType&& InValueType)
{
	FShaderValueTypeHandle Handle;
	Handle.ValueTypePtr = &InValueType;

	FShaderValueTypeHandle *FoundHandle = GloballyKnownValueTypes.Find(Handle);
	if (FoundHandle)
	{
		return *FoundHandle;
	}

	Handle.ValueTypePtr = new FShaderValueType(MoveTemp(InValueType));
	GloballyKnownValueTypes.Add(Handle);
	return Handle;
}

bool FShaderValueType::operator==(const FShaderValueType& InOtherType) const
{
	if (Type != InOtherType.Type)
	{
		return false;
	}

	if (Type == EShaderFundamentalType::Struct)
	{
		if (Name != InOtherType.Name || StructElements.Num() != InOtherType.StructElements.Num())
		{
			return false;
		}

		for (int32 Index = 0; Index < StructElements.Num(); Index++)
		{
			if (StructElements[Index] != InOtherType.StructElements[Index])
			{
				return false;
			}
		}
	}
	else
	{
		if (DimensionType != InOtherType.DimensionType)
		{
			return false;
		}

		if (DimensionType == EShaderFundamentalDimensionType::Vector && 
		    VectorElemCount != InOtherType.VectorElemCount)
		{
			return false;
		}
		else if (DimensionType == EShaderFundamentalDimensionType::Matrix &&
		    (MatrixRowCount != InOtherType.MatrixRowCount || 
			 MatrixColumnCount != InOtherType.MatrixColumnCount))
		{
			return false;
		}
	}

	return true;
}

uint32 GetTypeHash(const FShaderValueType& InShaderValueType)
{
	uint32 Hash = GetTypeHash(int32(InShaderValueType.Type));

	if (InShaderValueType.Type == EShaderFundamentalType::Struct)
	{
		Hash = HashCombine(Hash, GetTypeHash(InShaderValueType.Name));
		Hash = HashCombine(Hash, GetTypeHash(InShaderValueType.StructElements.Num()));
		for (const FShaderValueType::FStructElement& StructElement : InShaderValueType.StructElements)
		{
			Hash = HashCombine(Hash, GetTypeHash(StructElement.Name));
			Hash = HashCombine(Hash, GetTypeHash(*StructElement.Type.ValueTypePtr));
		}
	}
	else
	{
		Hash = HashCombine(Hash, GetTypeHash(int32(InShaderValueType.DimensionType)));

		if (InShaderValueType.DimensionType == EShaderFundamentalDimensionType::Vector)
		{
			Hash = HashCombine(Hash, GetTypeHash(int32(InShaderValueType.VectorElemCount)));
		}
		else if (InShaderValueType.DimensionType == EShaderFundamentalDimensionType::Matrix)
		{
			Hash = HashCombine(Hash, GetTypeHash(int32(InShaderValueType.MatrixRowCount)));
			Hash = HashCombine(Hash, GetTypeHash(int32(InShaderValueType.MatrixColumnCount)));
		}
	}
	return Hash;
}

FString FShaderValueType::ToString() const
{
	// FIXME: Cache on create?
	FString BaseName;
	switch (Type)
	{
	case EShaderFundamentalType::Bool:
		BaseName = "bool";
		break;
	case EShaderFundamentalType::Int:
		BaseName = "int";
		break;
	case EShaderFundamentalType::Uint:
		BaseName = "uint";
		break;
	case EShaderFundamentalType::Float:
		BaseName = "float";
		break;

	case EShaderFundamentalType::Struct:
		return Name.ToString();
	}

	if (DimensionType == EShaderFundamentalDimensionType::Vector)
	{
		BaseName.Appendf(TEXT("%d"), VectorElemCount);
	}
	else if (DimensionType == EShaderFundamentalDimensionType::Matrix)
	{
		BaseName.Appendf(TEXT("%dx%d"), MatrixRowCount, MatrixColumnCount);
	}

	return BaseName;
}

FString FShaderValueType::GetTypeDeclaration() const
{
	// FIXME: Cache on create?
	if (Type != EShaderFundamentalType::Struct)
	{
		return {};
	}

	TArray<FString> Elements;
	Elements.Reserve(StructElements.Num());
	for (const FStructElement& StructElement : StructElements)
	{
		Elements.Add(FString::Printf(TEXT("    %s %s;\n"), *StructElement.Type.ValueTypePtr->ToString(), *StructElement.Name.ToString()));
	}

	return FString::Printf(TEXT("struct %s {\n%s}"), 
		*Name.ToString(),
		*FString::Join(Elements, TEXT("")));
}


int32 FShaderValueType::GetResourceElementSize() const
{
	int32 Size = 0;
	
	switch (Type)
	{
	case EShaderFundamentalType::Bool:
	case EShaderFundamentalType::Int:
	case EShaderFundamentalType::Uint:
	case EShaderFundamentalType::Float:
		Size = 4;		// Yes, even for bool.
		break;
	case EShaderFundamentalType::Struct:
		for (const FStructElement& Elem: StructElements)
		{
			Size += Elem.Type->GetResourceElementSize();
		}
		break;
	}

	switch(DimensionType)
	{
	case EShaderFundamentalDimensionType::Scalar:
		break;
	case EShaderFundamentalDimensionType::Vector:
		Size *= VectorElemCount;
		break;
	case EShaderFundamentalDimensionType::Matrix:
		Size *= MatrixRowCount * MatrixColumnCount;
		break;
	}

	return Size;
}


FString FShaderValueType::GetZeroValueAsString() const
{
	FString FundamentalZeroConstant;
	switch(Type)
	{
	case EShaderFundamentalType::None:
		checkNoEntry();
		break;
	case EShaderFundamentalType::Bool:
		FundamentalZeroConstant = TEXT("false");
		break;
	case EShaderFundamentalType::Int:
	case EShaderFundamentalType::Uint:
		FundamentalZeroConstant = TEXT("0");
		break;
	case EShaderFundamentalType::Float:
		FundamentalZeroConstant = TEXT("0.0f");
		break;
	case EShaderFundamentalType::Struct:
		checkf(Type != EShaderFundamentalType::Struct, TEXT("Structs not supported yet.")); //-V547
		break;
	}

	int32 ValueCount = 0;
	switch(DimensionType)
	{
	case EShaderFundamentalDimensionType::Scalar:
		ValueCount = 1;
		break;
		
	case EShaderFundamentalDimensionType::Vector:
		ValueCount = VectorElemCount;
		break;
		
	case EShaderFundamentalDimensionType::Matrix:
		ValueCount = MatrixRowCount * MatrixColumnCount;
		break;
	}

	TArray<FString> ValueArray;
	ValueArray.Init(FundamentalZeroConstant, ValueCount);

	return FString::Printf(TEXT("%s(%s)"), *ToString(), *FString::Join(ValueArray, TEXT(", ")));
}


FArchive& operator<<(FArchive& InArchive, FShaderValueTypeHandle& InHandle)
{
	FShaderValueType ValueTypeTemp;
	FShaderValueType *ValueTypePtr;

	if (InArchive.IsLoading())
	{
		ValueTypePtr = &ValueTypeTemp;
	}
	else
	{
		ValueTypePtr = const_cast<FShaderValueType *>(InHandle.ValueTypePtr);
	}

	if (ValueTypePtr)
	{
		InArchive << ValueTypePtr->Type;
		
		if (ValueTypePtr->Type == EShaderFundamentalType::Struct)
		{
			InArchive << ValueTypePtr->Name;
			InArchive << ValueTypePtr->StructElements;
		}
		else
		{
			InArchive << ValueTypePtr->DimensionType;

			if (ValueTypePtr->DimensionType == EShaderFundamentalDimensionType::Vector)
			{
				InArchive << ValueTypePtr->VectorElemCount;
			}
			else if (ValueTypePtr->DimensionType == EShaderFundamentalDimensionType::Matrix)
			{
				InArchive << ValueTypePtr->MatrixRowCount;
				InArchive << ValueTypePtr->MatrixColumnCount;
			}
		}
	}
	else if (InArchive.IsSaving())
	{
		EShaderFundamentalType Type = EShaderFundamentalType::None;
		InArchive << Type; 
	}

	if (InArchive.IsLoading())
	{
		InHandle = FShaderValueType::GetOrCreate(MoveTemp(ValueTypeTemp));
	}

	return InArchive;
}

FArchive& operator<<(FArchive& InArchive, FShaderValueType::FStructElement& InElement)
{
	InArchive << InElement.Name;
	InArchive << InElement.Type;
	return InArchive;
}

using FResourceStingPair = TPair<EShaderResourceType, FString>;

static FResourceStingPair ResTypeStringMap[] = {
	FResourceStingPair(EShaderResourceType::Texture1D,			TEXT("Texture1D")),
	FResourceStingPair(EShaderResourceType::Texture2D,			TEXT("Texture2D")),
	FResourceStingPair(EShaderResourceType::Texture3D,			TEXT("Texture3D")),
	FResourceStingPair(EShaderResourceType::TextureCube,		TEXT("TextureCube")),
	FResourceStingPair(EShaderResourceType::StructuredBuffer,	TEXT("StructuredBuffer")),
	FResourceStingPair(EShaderResourceType::ByteAddressBuffer,	TEXT("ByteAddressBuffer")),
	FResourceStingPair(EShaderResourceType::Buffer,				TEXT("Buffer")),
	};


EShaderResourceType FShaderParamTypeDefinition::ParseResource(const FString& Str)
{
	for (auto& Pair : ResTypeStringMap)
	{
		if (Str.Contains(Pair.Value))
		{
			return Pair.Key;
		}
	}

	return EShaderResourceType::None;
}

void FShaderParamTypeDefinition::ResetTypeDeclaration(
	)
{
	FString TypeDecl;
	if (BindingType == EShaderParamBindingType::ReadWriteResource)
	{
		TypeDecl.Append(TEXT("RW"));
	}

	const bool bIsResourceType = BindingType == EShaderParamBindingType::ReadOnlyResource || BindingType == EShaderParamBindingType::ReadWriteResource;
	if (bIsResourceType)
	{
		auto* foundItem = Algo::FindByPredicate(
			ResTypeStringMap, 
			[this](const FResourceStingPair& Pair) 
			{ 
				return Pair.Key == ResourceType; 
			});
		check(foundItem);

		TypeDecl.Appendf(TEXT("%s<"), *foundItem->Value);
	}

	TypeDecl.Append(ValueType->ToString());

	if (bIsResourceType)
	{
		TypeDecl.AppendChar(TEXT('>'));
	}

	TypeDeclaration = MoveTemp(TypeDecl);
}
