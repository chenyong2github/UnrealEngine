// Copyright Epic Games, Inc. All Rights Reserved.
#include "Shader/ShaderTypes.h"
#include "Misc/StringBuilder.h"
#include "Hash/xxhash.h"
#include "Misc/MemStackUtility.h"
#include "Misc/LargeWorldRenderPosition.h"
#include "Engine/Texture.h"

namespace UE
{
namespace Shader
{

const TCHAR* FType::GetName() const
{
	if (StructType)
	{
		check(ValueType == EValueType::Struct);
		return StructType->Name;
	}

	check(ValueType != EValueType::Struct);
	const FValueTypeDescription TypeDesc = GetValueTypeDescription(ValueType);
	return TypeDesc.Name;
}

FType FType::GetDerivativeType() const
{
	if (StructType)
	{
		check(ValueType == EValueType::Struct);
		return StructType->DerivativeType; // will convert to 'Void' if DerivativeType is nullptr
	}

	check(ValueType != EValueType::Struct);
	const FValueTypeDescription TypeDesc = GetValueTypeDescription(ValueType);
	if (IsNumericType(TypeDesc.ComponentType))
	{
		return MakeValueType(EValueComponentType::Float, TypeDesc.NumComponents);
	}
	return EValueType::Void;
}

int32 FType::GetNumComponents() const
{
	if (StructType)
	{
		check(ValueType == EValueType::Struct);
		return StructType->ComponentTypes.Num();
	}

	check(ValueType != EValueType::Struct);
	const FValueTypeDescription TypeDesc = GetValueTypeDescription(ValueType);
	return TypeDesc.NumComponents;
}

int32 FType::GetNumFlatFields() const
{
	if (StructType)
	{
		check(ValueType == EValueType::Struct);
		return StructType->FlatFieldTypes.Num();
	}

	check(ValueType != EValueType::Struct);
	return 1;
}

EValueComponentType FType::GetComponentType(int32 Index) const
{
	if (StructType)
	{
		check(ValueType == EValueType::Struct);
		return StructType->ComponentTypes.IsValidIndex(Index) ? StructType->ComponentTypes[Index] : EValueComponentType::Void;
	}
	else
	{
		const FValueTypeDescription TypeDesc = GetValueTypeDescription(ValueType);
		return (Index >= 0 && Index < TypeDesc.NumComponents) ? TypeDesc.ComponentType : EValueComponentType::Void;
	}
}

EValueType FType::GetFlatFieldType(int32 Index) const
{
	if (StructType)
	{
		check(ValueType == EValueType::Struct);
		return StructType->FlatFieldTypes.IsValidIndex(Index) ? StructType->FlatFieldTypes[Index] : EValueType::Void;
	}
	else
	{
		return (Index == 0) ? ValueType : EValueType::Void;
	}
}

bool FType::Merge(const FType& OtherType)
{
	if (ValueType == EValueType::Void)
	{
		ValueType = OtherType.ValueType;
		StructType = OtherType.StructType;
		return true;
	}

	if (IsStruct() || OtherType.IsStruct())
	{
		return StructType == OtherType.StructType;
	}

	if (ValueType != OtherType.ValueType)
	{
		const FValueTypeDescription TypeDesc = GetValueTypeDescription(ValueType);
		const FValueTypeDescription OtherTypeDesc = GetValueTypeDescription(OtherType);
		const EValueComponentType ComponentType = CombineComponentTypes(TypeDesc.ComponentType, OtherTypeDesc.ComponentType);
		if (ComponentType == EValueComponentType::Void)
		{
			return false;
		}

		const int8 NumComponents = FMath::Max(TypeDesc.NumComponents, OtherTypeDesc.NumComponents);
		ValueType = MakeValueType(ComponentType, NumComponents);
	}
	return true;
}

const FStructField* FStructType::FindFieldByName(const TCHAR* InName) const
{
	for (const FStructField& Field : Fields)
	{
		if (FCString::Strcmp(Field.Name, InName) == 0)
		{
			return &Field;
		}
	}

	return nullptr;
}

namespace Private
{
struct FCastFloat
{
	using FComponentType = float;
	inline float operator()(EValueComponentType Type, const FValueComponent& Component) const
	{
		switch (Type)
		{
		case EValueComponentType::Float: return Component.Float;
		case EValueComponentType::Double: return (float)Component.Double;
		case EValueComponentType::Int: return (float)Component.Int;
		case EValueComponentType::Bool: return (float)Component.Bool;
		default: return 0.0f;
		}
	}
};

struct FCastDouble
{
	using FComponentType = double;
	inline double operator()(EValueComponentType Type, const FValueComponent& Component) const
	{
		switch (Type)
		{
		case EValueComponentType::Float: return (double)Component.Float;
		case EValueComponentType::Double: return Component.Double;
		case EValueComponentType::Int: return (double)Component.Int;
		case EValueComponentType::Bool: return (double)Component.Bool;
		default: return 0.0f;
		}
	}
};

struct FCastInt
{
	using FComponentType = int32;
	inline int32 operator()(EValueComponentType Type, const FValueComponent& Component) const
	{
		switch (Type)
		{
		case EValueComponentType::Float: return (int32)Component.Float;
		case EValueComponentType::Double: return (int32)Component.Double;
		case EValueComponentType::Int: return Component.Int;
		case EValueComponentType::Bool: return Component.Bool ? 1 : 0;
		default: return false;
		}
	}
};

struct FCastBool
{
	using FComponentType = bool;
	inline bool operator()(EValueComponentType Type, const FValueComponent& Component) const
	{
		switch (Type)
		{
		case EValueComponentType::Float: return Component.Float != 0.0f;
		case EValueComponentType::Double: return Component.Double != 0.0;
		case EValueComponentType::Int: return Component.Int != 0;
		case EValueComponentType::Bool: return Component.AsBool();
		default: return false;
		}
	}
};

template<typename Operation, typename ResultType>
void AsType(const Operation& Op, const FValue& Value, ResultType& OutResult)
{
	using FComponentType = typename Operation::FComponentType;
	const FValueTypeDescription TypeDesc = GetValueTypeDescription(Value.Type);
	if (TypeDesc.NumComponents == 1)
	{
		const FComponentType Component = Op(TypeDesc.ComponentType, Value.Component[0]);
		for (int32 i = 0; i < 4; ++i)
		{
			OutResult[i] = Component;
		}
	}
	else
	{
		const int32 NumComponents = FMath::Min<int32>(TypeDesc.NumComponents, 4);
		for (int32 i = 0; i < NumComponents; ++i)
		{
			OutResult[i] = Op(TypeDesc.ComponentType, Value.Component[i]);
		}
		for (int32 i = NumComponents; i < 4; ++i)
		{
			OutResult[i] = (FComponentType)0;
		}
	}
}

template<typename Operation>
void Cast(const Operation& Op, const FValue& Value, FValue& OutResult)
{
	using FComponentType = typename Operation::FComponentType;
	const FValueTypeDescription ValueTypeDesc = GetValueTypeDescription(Value.Type);
	const FValueTypeDescription ResultTypeDesc = GetValueTypeDescription(OutResult.Type);
	const int32 NumCopyComponents = FMath::Min<int32>(ValueTypeDesc.NumComponents, ResultTypeDesc.NumComponents);
	for (int32 i = 0; i < NumCopyComponents; ++i)
	{
		OutResult.Component.Add(Op(ValueTypeDesc.ComponentType, Value.Component[i]));
	}

	if (NumCopyComponents < ResultTypeDesc.NumComponents)
	{
		if (NumCopyComponents == 1)
		{
			const FValueComponent Component = OutResult.Component[0];
			for (int32 i = 1; i < ResultTypeDesc.NumComponents; ++i)
			{
				OutResult.Component.Add(Component);
			}
		}
		else
		{
			for (int32 i = NumCopyComponents; i < ResultTypeDesc.NumComponents; ++i)
			{
				OutResult.Component.AddDefaulted();
			}
		}
	}
}

void FormatComponent_Double(double Value, int32 NumComponents, EValueStringFormat Format, FStringBuilderBase& OutResult)
{
	if (Format == EValueStringFormat::HLSL)
	{
		OutResult.Appendf(TEXT("%0.8f"), Value);
	}
	else
	{
		// Shorter format for more components
		switch (NumComponents)
		{
		default: OutResult.Appendf(TEXT("%.2g"), Value); break;
		case 3: OutResult.Appendf(TEXT("%.3g"), Value); break;
		case 2: OutResult.Appendf(TEXT("%.3g"), Value); break;
		case 1: OutResult.Appendf(TEXT("%.4g"), Value); break;
		}
	}
}

} // namespace Private

EValueType FTextureValue::GetType() const
{
	if (Texture)
	{
		const EMaterialValueType MaterialType = Texture->GetMaterialType();
		switch (MaterialType)
		{
		case MCT_Texture2D: return EValueType::Texture2D;
		case MCT_Texture2DArray: return EValueType::Texture2DArray;
		case MCT_TextureCube: return EValueType::TextureCube;
		case MCT_TextureCubeArray: return EValueType::TextureCubeArray;
		case MCT_VolumeTexture: return EValueType::Texture3D;
		case MCT_TextureExternal: return EValueType::TextureExternal;
		case MCT_TextureVirtual: return EValueType::Texture2D; // TODO
		default: checkNoEntry(); break;
		}
	}
	else if (ExternalTextureGuid.IsValid())
	{
		return EValueType::TextureExternal;
	}

	return EValueType::Void;
}

FValue::FValue(const FTextureValue* InValue) : Type(EValueType::Void)
{
	if (InValue)
	{
		Type = InValue->GetType();
		if (Type != EValueType::Void)
		{
			Component.Add(InValue);
		}
	}
}

FValue FValue::FromMemoryImage(EValueType Type, const void* Data, uint32* OutSizeInBytes)
{
	check(IsNumericType(Type));
	const FValueTypeDescription TypeDesc = GetValueTypeDescription(Type);

	FValue Result(TypeDesc.ComponentType, TypeDesc.NumComponents);
	const uint8* Bytes = static_cast<const uint8*>(Data);
	const uint32 ComponentSizeInBytes = GetComponentTypeSizeInBytes(TypeDesc.ComponentType);
	if (ComponentSizeInBytes > 0u)
	{
		for (int32 i = 0u; i < TypeDesc.NumComponents; ++i)
		{
			FMemory::Memcpy(&Result.Component[i].Packed, Bytes, ComponentSizeInBytes);
			Bytes += ComponentSizeInBytes;
		}
	}
	if (OutSizeInBytes)
	{
		*OutSizeInBytes = (uint32)(Bytes - static_cast<const uint8*>(Data));
	}
	return Result;
}

FMemoryImageValue FValue::AsMemoryImage() const
{
	check(Type.IsNumeric());
	const FValueTypeDescription TypeDesc = GetValueTypeDescription(Type);

	FMemoryImageValue Result;
	uint8* Bytes = Result.Bytes;
	const uint32 ComponentSizeInBytes = GetComponentTypeSizeInBytes(TypeDesc.ComponentType);
	if (ComponentSizeInBytes > 0u)
	{
		for (int32 i = 0u; i < TypeDesc.NumComponents; ++i)
		{
			FMemory::Memcpy(Bytes, &Component[i].Packed, ComponentSizeInBytes);
			Bytes += ComponentSizeInBytes;
		}
	}
	Result.Size = (uint32)(Bytes - Result.Bytes);
	check(Result.Size <= FMemoryImageValue::MaxSize);
	return Result;
}

FFloatValue FValue::AsFloat() const
{
	FFloatValue Result;
	Private::AsType(Private::FCastFloat(), *this, Result);
	return Result;
}

FDoubleValue FValue::AsDouble() const
{
	FDoubleValue Result;
	Private::AsType(Private::FCastDouble(), *this, Result);
	return Result;
}

FLinearColor FValue::AsLinearColor() const
{
	const FFloatValue Result = AsFloat();
	return FLinearColor(Result.Component[0], Result.Component[1], Result.Component[2], Result.Component[3]);
}

FVector4d FValue::AsVector4d() const
{
	const FDoubleValue Result = AsDouble();
	return FVector4d(Result.Component[0], Result.Component[1], Result.Component[2], Result.Component[3]);
}

FIntValue FValue::AsInt() const
{
	FIntValue Result;
	Private::AsType(Private::FCastInt(), *this, Result);
	return Result;
}

FBoolValue FValue::AsBool() const
{
	FBoolValue Result;
	Private::AsType(Private::FCastBool(), *this, Result);
	return Result;
}

float FValue::AsFloatScalar() const
{
	FFloatValue Result;
	Private::AsType(Private::FCastFloat(), *this, Result);
	return Result[0];
}

bool FValue::AsBoolScalar() const
{
	const FBoolValue Result = AsBool();
	for (int32 i = 0; i < 4; ++i)
	{
		if (Result.Component[i])
		{
			return true;
		}
	}
	return false;
}

const FTextureValue* FValue::AsTexture() const
{
	if (Type.IsTexture() && Component.Num() > 0)
	{
		return Component[0].Texture;
	}
	return nullptr;
}

FValueComponentTypeDescription GetValueComponentTypeDescription(EValueComponentType Type)
{
	switch (Type)
	{
	case EValueComponentType::Void: return FValueComponentTypeDescription(TEXT("void"), 0u, EComponentBound::Zero, EComponentBound::Zero);
	case EValueComponentType::Float: return FValueComponentTypeDescription(TEXT("float"), sizeof(float), EComponentBound::NegFloatMax, EComponentBound::FloatMax);
	case EValueComponentType::Double: return FValueComponentTypeDescription(TEXT("double"), sizeof(double), EComponentBound::NegDoubleMax, EComponentBound::DoubleMax);
	case EValueComponentType::Int: return FValueComponentTypeDescription(TEXT("int"), sizeof(int32), EComponentBound::IntMin, EComponentBound::IntMax);
	case EValueComponentType::Bool: return FValueComponentTypeDescription(TEXT("bool"), 1u, EComponentBound::Zero, EComponentBound::One);
	case EValueComponentType::Texture2D: return FValueComponentTypeDescription(TEXT("Texture2D"), sizeof(void*), EComponentBound::Zero, EComponentBound::Zero);
	case EValueComponentType::Texture2DArray: return FValueComponentTypeDescription(TEXT("Texture2DArray"), sizeof(void*), EComponentBound::Zero, EComponentBound::Zero);
	case EValueComponentType::TextureCube: return FValueComponentTypeDescription(TEXT("TextureCube"), sizeof(void*), EComponentBound::Zero, EComponentBound::Zero);
	case EValueComponentType::TextureCubeArray: return FValueComponentTypeDescription(TEXT("TextureCubeArray"), sizeof(void*), EComponentBound::Zero, EComponentBound::Zero);
	case EValueComponentType::Texture3D: return FValueComponentTypeDescription(TEXT("Texture3D"), sizeof(void*), EComponentBound::Zero, EComponentBound::Zero);
	case EValueComponentType::TextureExternal: return FValueComponentTypeDescription(TEXT("TextureExternal"), sizeof(void*), EComponentBound::Zero, EComponentBound::Zero);
	default: checkNoEntry() return FValueComponentTypeDescription();
	}
}

EValueComponentType CombineComponentTypes(EValueComponentType Lhs, EValueComponentType Rhs)
{
	if (Lhs == Rhs)
	{
		return Lhs;
	}
	else if (Lhs == EValueComponentType::Void)
	{
		return Rhs;
	}
	else if (Rhs == EValueComponentType::Void)
	{
		return Lhs;
	}
	else if (Lhs == EValueComponentType::Double || Rhs == EValueComponentType::Double)
	{
		return EValueComponentType::Double;
	}
	else if (Lhs == EValueComponentType::Float || Rhs == EValueComponentType::Float)
	{
		return EValueComponentType::Float;
	}
	else if(IsNumericType(Lhs) && IsNumericType(Rhs))
	{
		return EValueComponentType::Int;
	}
	else
	{
		return EValueComponentType::Void;
	}
}

const TCHAR* FValueComponent::ToString(EValueComponentType Type, FStringBuilderBase& OutString) const
{
	switch (Type)
	{
	case EValueComponentType::Int: OutString.Appendf(TEXT("%d"), Int); break;
	case EValueComponentType::Bool: OutString.Append(AsBool() ? TEXT("true") : TEXT("false")); break;
	case EValueComponentType::Float: OutString.Appendf(TEXT("%#.9gf"), Float); break;
	default: checkNoEntry(); break; // TODO - double
	}
	return OutString.ToString();
}

const TCHAR* FValue::ToString(EValueStringFormat Format, FStringBuilderBase& OutString) const
{
	const int32 NumComponents = Type.GetNumComponents();
	const TCHAR* TypeName = Type.GetName();
	const TCHAR* ClosingSuffix = nullptr;

	if (Format == EValueStringFormat::HLSL)
	{
		if (Type.IsStruct())
		{
			OutString.Append(TEXT("{ "));
			ClosingSuffix = TEXT(" }");
		}
		else
		{
			const FValueTypeDescription TypeDesc = GetValueTypeDescription(Type.ValueType);
			if (TypeDesc.ComponentType != EValueComponentType::Double)
			{
				OutString.Appendf(TEXT("%s("), TypeDesc.Name);
				ClosingSuffix = TEXT(")");
			}
		}
	}

	for (int32 Index = 0; Index < NumComponents; ++Index)
	{
		if (Index > 0)
		{
			OutString.Append(TEXT(", "));
		}
		const EValueComponentType ComponentType = Type.GetComponentType(Index);
		switch (ComponentType)
		{
		case EValueComponentType::Int: OutString.Appendf(TEXT("%d"), Component[Index].Int); break;
		case EValueComponentType::Bool: OutString.Append(Component[Index].Bool ? TEXT("true") : TEXT("false")); break;
		case EValueComponentType::Float: Private::FormatComponent_Double((double)Component[Index].Float, NumComponents, Format, OutString); break;
		case EValueComponentType::Double: Private::FormatComponent_Double(Component[Index].Double, NumComponents, Format, OutString); break;
		default: checkNoEntry(); break;
		}
	}

	if (ClosingSuffix)
	{
		OutString.Append(ClosingSuffix);
	}




	/*if (Format == EValueStringFormat::HLSL && ComponentType == EValueComponentType::Double)
	{
		// Construct an HLSL LWC Vector
		TStringBuilder<256> TileValue;
		TStringBuilder<256> OffsetValue;
		for (int32 i = 0; i < NumComponents; ++i)
		{
			if (i > 0)
			{
				TileValue.Append(TEXT(", "));
				OffsetValue.Append(TEXT(", "));
			}

			const FLargeWorldRenderScalar Value(Component[i].Double);
			Private::FormatComponent_Double(Value.GetTileAsDouble(), NumComponents, Format, TileValue);
			Private::FormatComponent_Double(Value.GetOffsetAsDouble(), NumComponents, Format, OffsetValue);
		}

		if (NumComponents > 1)
		{
			OutString.Appendf(TEXT("MakeLWCVector%d(float%d(%s), float%d(%s))"), NumComponents, NumComponents, TileValue.ToString(), NumComponents, OffsetValue.ToString());
		}
		else
		{
			OutString.Appendf(TEXT("MakeLWCScalar(%s, %s)"), TileValue.ToString(), OffsetValue.ToString());
		}
	}
	else
	{
		if (Format == EValueStringFormat::HLSL)
		{
			const TCHAR* ComponentName = nullptr;
			switch (ComponentType)
			{
			case EValueComponentType::Int: ComponentName = TEXT("int"); break;
			case EValueComponentType::Bool: ComponentName = TEXT("bool"); break;
			case EValueComponentType::Float: ComponentName = TEXT("float"); break;
			default: checkNoEntry(); break; // Double is handled by above case
			}
			if (NumComponents > 1)
			{
				OutString.Appendf(TEXT("%s%d("), ComponentName, NumComponents);
			}
			else
			{
				OutString.Appendf(TEXT("%s("), ComponentName);
			}
		}

		for (int32 i = 0; i < NumComponents; ++i)
		{
			if (i > 0)
			{
				OutString.Append(TEXT(", "));
			}

			switch (ComponentType)
			{
			case EValueComponentType::Int: OutString.Appendf(TEXT("%d"), Component[i].Int); break;
			case EValueComponentType::Bool: OutString.Append(Component[i].Bool ? TEXT("true") : TEXT("false")); break;
			case EValueComponentType::Float: Private::FormatComponent_Double((double)Component[i].Float, NumComponents, Format, OutString); break;
			case EValueComponentType::Double: Private::FormatComponent_Double(Component[i].Double, NumComponents, Format, OutString); break;
			default: checkNoEntry(); break;
			}
		}

		if (Format == EValueStringFormat::HLSL)
		{
			OutString.Append(TEXT(")"));
		}
	}*/

	return OutString.ToString();
}

FValueTypeDescription GetValueTypeDescription(EValueType Type)
{
	switch (Type)
	{
	case EValueType::Void: return FValueTypeDescription(TEXT("void"), EValueComponentType::Void, 0);
	case EValueType::Float1: return FValueTypeDescription(TEXT("float"), EValueComponentType::Float, 1);
	case EValueType::Float2: return FValueTypeDescription(TEXT("float2"), EValueComponentType::Float, 2);
	case EValueType::Float3: return FValueTypeDescription(TEXT("float3"), EValueComponentType::Float, 3);
	case EValueType::Float4: return FValueTypeDescription(TEXT("float4"), EValueComponentType::Float, 4);
	case EValueType::Double1: return FValueTypeDescription(TEXT("FLWCScalar"), EValueComponentType::Double, 1);
	case EValueType::Double2: return FValueTypeDescription(TEXT("FLWCVector2"), EValueComponentType::Double, 2);
	case EValueType::Double3: return FValueTypeDescription(TEXT("FLWCVector3"), EValueComponentType::Double, 3);
	case EValueType::Double4: return FValueTypeDescription(TEXT("FLWCVector4"), EValueComponentType::Double, 4);
	case EValueType::Int1: return FValueTypeDescription(TEXT("int"), EValueComponentType::Int, 1);
	case EValueType::Int2: return FValueTypeDescription(TEXT("int2"), EValueComponentType::Int, 2);
	case EValueType::Int3: return FValueTypeDescription(TEXT("int3"), EValueComponentType::Int, 3);
	case EValueType::Int4: return FValueTypeDescription(TEXT("int4"), EValueComponentType::Int, 4);
	case EValueType::Bool1: return FValueTypeDescription(TEXT("bool"), EValueComponentType::Bool, 1);
	case EValueType::Bool2: return FValueTypeDescription(TEXT("bool2"), EValueComponentType::Bool, 2);
	case EValueType::Bool3: return FValueTypeDescription(TEXT("bool3"), EValueComponentType::Bool, 3);
	case EValueType::Bool4: return FValueTypeDescription(TEXT("bool4"), EValueComponentType::Bool, 4);
	case EValueType::Float4x4: return FValueTypeDescription(TEXT("float4x4"), EValueComponentType::Float, 16);
	case EValueType::Double4x4: return FValueTypeDescription(TEXT("FLWCMatrix"), EValueComponentType::Double, 16);
	case EValueType::DoubleInverse4x4: return FValueTypeDescription(TEXT("FLWCInverseMatrix"), EValueComponentType::Double, 16);
	case EValueType::Struct: return FValueTypeDescription(TEXT("struct"), EValueComponentType::Void, 0);
	case EValueType::Texture2D: return FValueTypeDescription(TEXT("FTexture2D"), EValueComponentType::Texture2D, 1);
	case EValueType::Texture2DArray: return FValueTypeDescription(TEXT("FTexture2DArray"), EValueComponentType::Texture2DArray, 1);
	case EValueType::TextureCube: return FValueTypeDescription(TEXT("FTextureCube"), EValueComponentType::TextureCube, 1);
	case EValueType::TextureCubeArray: return FValueTypeDescription(TEXT("FTextureCubeArray"), EValueComponentType::TextureCubeArray, 1);
	case EValueType::Texture3D: return FValueTypeDescription(TEXT("FTexture3D"), EValueComponentType::Texture3D, 1);
	case EValueType::TextureExternal: return FValueTypeDescription(TEXT("FTextureExternal"), EValueComponentType::TextureExternal, 1);
	default: checkNoEntry(); return FValueTypeDescription(TEXT("<INVALID>"), EValueComponentType::Void, 0);
	}
}

EValueType MakeValueType(EValueComponentType ComponentType, int32 NumComponents)
{
	if (ComponentType == EValueComponentType::Void || NumComponents == 0)
	{
		return EValueType::Void;
	}

	switch (ComponentType)
	{
	case EValueComponentType::Float:
		switch (NumComponents)
		{
		case 1: return EValueType::Float1;
		case 2: return EValueType::Float2;
		case 3: return EValueType::Float3;
		case 4: return EValueType::Float4;
		case 16: return EValueType::Float4x4;
		default: break;
		}
		break;
	case EValueComponentType::Double:
		switch (NumComponents)
		{
		case 1: return EValueType::Double1;
		case 2: return EValueType::Double2;
		case 3: return EValueType::Double3;
		case 4: return EValueType::Double4;
		case 16: return EValueType::Double4x4;
		default: break;
		}
		break;
	case EValueComponentType::Int:
		switch (NumComponents)
		{
		case 1: return EValueType::Int1;
		case 2: return EValueType::Int2;
		case 3: return EValueType::Int3;
		case 4: return EValueType::Int4;
		default: break;
		}
		break;
	case EValueComponentType::Bool:
		switch (NumComponents)
		{
		case 1: return EValueType::Bool1;
		case 2: return EValueType::Bool2;
		case 3: return EValueType::Bool3;
		case 4: return EValueType::Bool4;
		default: break;
		}
		break;
	case EValueComponentType::Texture2D: check(NumComponents == 1); return EValueType::Texture2D; break;
	case EValueComponentType::Texture2DArray: check(NumComponents == 1); return EValueType::Texture2DArray; break;
	case EValueComponentType::TextureCube: check(NumComponents == 1); return EValueType::TextureCube; break;
	case EValueComponentType::TextureCubeArray: check(NumComponents == 1); return EValueType::TextureCubeArray; break;
	case EValueComponentType::Texture3D: check(NumComponents == 1); return EValueType::Texture3D; break;
	case EValueComponentType::TextureExternal: check(NumComponents == 1); return EValueType::TextureExternal; break;
	default:
		break;
	}

	checkNoEntry();
	return EValueType::Void;
}

EValueType MakeValueType(EValueType BaseType, int32 NumComponents)
{
	return MakeValueType(GetValueTypeDescription(BaseType).ComponentType, NumComponents);
}

EValueType MakeValueTypeWithRequestedNumComponents(EValueType BaseType, int8 RequestedNumComponents)
{
	const FValueTypeDescription TypeDesc = GetValueTypeDescription(BaseType);
	return MakeValueType(TypeDesc.ComponentType, FMath::Min(TypeDesc.NumComponents, RequestedNumComponents));
}

EValueType MakeNonLWCType(EValueType Type)
{
	const FValueTypeDescription TypeDesc = GetValueTypeDescription(Type);
	return MakeValueType(MakeNonLWCType(TypeDesc.ComponentType), TypeDesc.NumComponents);
}

EValueType MakeArithmeticResultType(EValueType Lhs, EValueType Rhs, FString& OutErrorMessage)
{
	const FValueTypeDescription LhsDesc = GetValueTypeDescription(Lhs);
	const FValueTypeDescription RhsDesc = GetValueTypeDescription(Rhs);
	// Types with 0 components are non-arithmetic
	if (LhsDesc.NumComponents > 0 && RhsDesc.NumComponents > 0)
	{
		if (Lhs == Rhs)
		{
			return Lhs;
		}

		EValueComponentType ComponentType = EValueComponentType::Void;
		if (LhsDesc.ComponentType == RhsDesc.ComponentType)
		{
			ComponentType = LhsDesc.ComponentType;
		}
		else if (LhsDesc.ComponentType == EValueComponentType::Double || RhsDesc.ComponentType == EValueComponentType::Double)
		{
			ComponentType = EValueComponentType::Double;
		}
		else if (LhsDesc.ComponentType == EValueComponentType::Float || RhsDesc.ComponentType == EValueComponentType::Float)
		{
			ComponentType = EValueComponentType::Float;
		}
		else
		{
			ComponentType = EValueComponentType::Int;
		}

		if (ComponentType != EValueComponentType::Void)
		{
			if (LhsDesc.NumComponents == 1 || RhsDesc.NumComponents == 1)
			{
				// single component type is valid to combine with other type
				return MakeValueType(ComponentType, FMath::Max(LhsDesc.NumComponents, RhsDesc.NumComponents));
			}
			else if (LhsDesc.NumComponents == RhsDesc.NumComponents)
			{
				return MakeValueType(ComponentType, LhsDesc.NumComponents);
			}
		}

		OutErrorMessage = FString::Printf(TEXT("Arithmetic between types %s and %s are undefined"), LhsDesc.Name, RhsDesc.Name);
	}
	else
	{
		OutErrorMessage = FString::Printf(TEXT("Attempting to perform arithmetic on non-numeric types: %s %s"), LhsDesc.Name, RhsDesc.Name);
	}

	return EValueType::Void;
}

EValueType MakeComparisonResultType(EValueType Lhs, EValueType Rhs, FString& OutErrorMessage)
{
	const FValueTypeDescription LhsDesc = GetValueTypeDescription(Lhs);
	const FValueTypeDescription RhsDesc = GetValueTypeDescription(Rhs);

	if (Lhs == Rhs)
	{
		if (LhsDesc.NumComponents > 0)
		{
			return MakeValueType(EValueComponentType::Bool, LhsDesc.NumComponents);
		}
		else
		{
			OutErrorMessage = FString::Printf(TEXT("Attempting to perform comparison on non-numeric types: %s %s"), LhsDesc.Name, RhsDesc.Name);
		}
	}
	else
	{
		OutErrorMessage = FString::Printf(TEXT("Comparison between types %s and %s are undefined"), LhsDesc.Name, RhsDesc.Name);
	}

	return EValueType::Void;
}

FStructTypeRegistry::FStructTypeRegistry(FMemStackBase& InAllocator)
	: Allocator(&InAllocator)
{
}

void FStructTypeRegistry::EmitDeclarationsCode(FStringBuilderBase& OutCode) const
{
	for (const auto& It : Types)
	{
		const FStructType* StructType = It.Value;

		OutCode.Appendf(TEXT("struct %s\n"), StructType->Name);
		OutCode.Append(TEXT("{\n"));
		for (const FStructField& Field : StructType->Fields)
		{
			OutCode.Appendf(TEXT("\t%s %s;\n"), Field.Type.GetName(), Field.Name);
		}
		OutCode.Append(TEXT("};\n"));

		for (const FStructField& Field : StructType->Fields)
		{
			OutCode.Appendf(TEXT("%s %s_Set%s(%s Self, %s Value) { Self.%s = Value; return Self; }\n"),
				StructType->Name, StructType->Name, Field.Name, StructType->Name, Field.Type.GetName(), Field.Name);
		}
		OutCode.Append(TEXT("\n"));
	}
}

namespace Private
{
void SetFieldType(EValueType* FieldTypes, EValueComponentType* ComponentTypes, int32 FieldIndex, int32 ComponentIndex, const FType& Type)
{
	if (Type.IsStruct())
	{
		for (const FStructField& Field : Type.StructType->Fields)
		{
			SetFieldType(FieldTypes, ComponentTypes, FieldIndex + Field.FlatFieldIndex, ComponentIndex + Field.ComponentIndex, Field.Type);
		}
	}
	else
	{
		FieldTypes[FieldIndex] = Type.ValueType;
		const FValueTypeDescription TypeDesc = GetValueTypeDescription(Type.ValueType);
		for (int32 i = 0; i < TypeDesc.NumComponents; ++i)
		{
			ComponentTypes[ComponentIndex + i] = TypeDesc.ComponentType;
		}
	}
}
} // namespace Private

const FStructType* FStructTypeRegistry::NewType(const FStructTypeInitializer& Initializer)
{
	TArray<FStructFieldInitializer, TInlineAllocator<16>> DerivativeFields;

	const int32 NumFields = Initializer.Fields.Num();
	FStructField* Fields = new(*Allocator) FStructField[NumFields];
	int32 ComponentIndex = 0;
	int32 FlatFieldIndex = 0;
	uint64 Hash = 0u;
	{
		FXxHash64Builder Hasher;
		Hasher.Update(Initializer.Name.GetData(), Initializer.Name.Len() * sizeof(TCHAR));

		for (int32 FieldIndex = 0; FieldIndex < NumFields; ++FieldIndex)
		{
			const FStructFieldInitializer& FieldInitializer = Initializer.Fields[FieldIndex];
			const FType& FieldType = FieldInitializer.Type;

			Hasher.Update(FieldInitializer.Name.GetData(), FieldInitializer.Name.Len() * sizeof(TCHAR));
			if (FieldType.IsStruct())
			{
				Hasher.Update(&FieldType.StructType->Hash, sizeof(FieldType.StructType->Hash));
			}
			else
			{
				Hasher.Update(&FieldType.ValueType, sizeof(FieldType.ValueType));
			}

			FStructField& Field = Fields[FieldIndex];
			Field.Name = MemStack::AllocateString(*Allocator, FieldInitializer.Name);
			Field.Type = FieldType;
			Field.ComponentIndex = ComponentIndex;
			Field.FlatFieldIndex = FlatFieldIndex;
			ComponentIndex += FieldType.GetNumComponents();
			FlatFieldIndex += FieldType.GetNumFlatFields();

			if (!Initializer.bIsDerivativeType)
			{
				const FType FieldDerivativeType = FieldType.GetDerivativeType();
				if (!FieldDerivativeType.IsVoid())
				{
					DerivativeFields.Emplace(FieldInitializer.Name, FieldDerivativeType);
				}
			}
		}
		Hash = Hasher.Finalize().Hash;
	}

	FStructType const* const* PrevStructType = Types.Find(Hash);
	if (PrevStructType)
	{
		return *PrevStructType;
	}

	EValueComponentType* ComponentTypes = new(*Allocator) EValueComponentType[ComponentIndex];
	EValueType* FlatFieldTypes = new(*Allocator) EValueType[FlatFieldIndex];
	for (int32 FieldIndex = 0; FieldIndex < NumFields; ++FieldIndex)
	{
		const FStructField& Field = Fields[FieldIndex];
		Private::SetFieldType(FlatFieldTypes, ComponentTypes, Field.FlatFieldIndex, Field.ComponentIndex, Field.Type);
	}

	FStructType* StructType = new(*Allocator) FStructType();
	StructType->Name = MemStack::AllocateString(*Allocator, Initializer.Name);
	StructType->Hash = Hash;
	StructType->Fields = MakeArrayView(Fields, NumFields);
	StructType->ComponentTypes = MakeArrayView(ComponentTypes, ComponentIndex);
	StructType->FlatFieldTypes = MakeArrayView(FlatFieldTypes, FlatFieldIndex);

	Types.Add(Hash, StructType);

	if (DerivativeFields.Num() > 0)
	{
		const FString DerivativeTypeName = FString(Initializer.Name) + TEXT("_Deriv");
		FStructTypeInitializer DerivativeTypeInitializer;
		DerivativeTypeInitializer.Name = DerivativeTypeName;
		DerivativeTypeInitializer.Fields = DerivativeFields;
		DerivativeTypeInitializer.bIsDerivativeType = true;
		StructType->DerivativeType = NewType(DerivativeTypeInitializer);
	}

	return StructType;
}

const FStructType* FStructTypeRegistry::FindType(uint64 Hash) const
{
	FStructType const* const* PrevType = Types.Find(Hash);
	return PrevType ? *PrevType : nullptr;
}

namespace Private
{

/** Converts an arbitrary number into a safe divisor. i.e. FMath::Abs(Number) >= DELTA */
/**
 * FORCENOINLINE is required to discourage compiler from vectorizing the Div operation, which may tempt it into optimizing divide as A * rcp(B)
 * This will break shaders that are depending on exact divide results (see SubUV material function)
 * Technically this could still happen for a scalar divide, but it doesn't seem to occur in practice
 */
template<typename T>
FORCENOINLINE T GetSafeDivisor(T Number)
{
	if (FMath::Abs(Number) < (T)DELTA)
	{
		if (Number < 0)
		{
			return -(T)DELTA;
		}
		else
		{
			return (T)DELTA;
		}
	}
	else
	{
		return Number;
	}
}

template<>
FORCENOINLINE int32 GetSafeDivisor<int32>(int32 Number)
{
	return Number != 0 ? Number : 1;
}

struct FOpBase
{
	static constexpr bool SupportsDouble = true;
	static constexpr bool SupportsInt = true;
};

struct FOpBaseNoInt : public FOpBase
{
	static constexpr bool SupportsInt = false;
};

struct FOpNeg : public FOpBase { template<typename T> T operator()(T Value) const { return -Value; } };
struct FOpAbs : public FOpBase { template<typename T> T operator()(T Value) const { return FMath::Abs(Value); } };
struct FOpSign : public FOpBase { template<typename T> T operator()(T Value) const { return FMath::Sign(Value); } };
struct FOpSaturate : public FOpBaseNoInt { template<typename T> T operator()(T Value) const { return FMath::Clamp(Value, (T)0, (T)1); } };
struct FOpFloor : public FOpBaseNoInt { template<typename T> T operator()(T Value) const { return FMath::FloorToFloat(Value); } };
struct FOpCeil : public FOpBaseNoInt { template<typename T> T operator()(T Value) const { return FMath::CeilToFloat(Value); } };
struct FOpRound : public FOpBaseNoInt { template<typename T> T operator()(T Value) const { return FMath::RoundToFloat(Value); } };
struct FOpTrunc : public FOpBaseNoInt { template<typename T> T operator()(T Value) const { return FMath::TruncToFloat(Value); } };
struct FOpFrac : public FOpBaseNoInt { template<typename T> T operator()(T Value) const { return FMath::Frac(Value); } };
struct FOpFractional : public FOpBaseNoInt { template<typename T> T operator()(T Value) const { return FMath::Fractional(Value); } };
struct FOpSqrt : public FOpBaseNoInt { template<typename T> T operator()(T Value) const { return FMath::Sqrt(Value); } };
struct FOpRcp : public FOpBaseNoInt { template<typename T> T operator()(T Value) const { return (T)1 / GetSafeDivisor(Value); } };
struct FOpLog2 : public FOpBaseNoInt { template<typename T> T operator()(T Value) const { return FMath::Log2(Value); } };
struct FOpLog10 : public FOpBaseNoInt
{
	template<typename T> T operator()(T Value) const
	{
		static const T LogToLog10 = (T)1.0 / FMath::Loge((T)10);
		return FMath::Loge(Value) * LogToLog10;
	}
};
struct FOpSin : public FOpBaseNoInt { template<typename T> T operator()(T Value) const { return FMath::Sin(Value); } };
struct FOpCos : public FOpBaseNoInt { template<typename T> T operator()(T Value) const { return FMath::Cos(Value); } };
struct FOpTan : public FOpBaseNoInt { template<typename T> T operator()(T Value) const { return FMath::Tan(Value); } };
struct FOpAsin : public FOpBaseNoInt { template<typename T> T operator()(T Value) const { return FMath::Asin(Value); } };
struct FOpAcos : public FOpBaseNoInt { template<typename T> T operator()(T Value) const { return FMath::Acos(Value); } };
struct FOpAtan : public FOpBaseNoInt { template<typename T> T operator()(T Value) const { return FMath::Atan(Value); } };
struct FOpAdd : public FOpBase { template<typename T> T operator()(T Lhs, T Rhs) const { return Lhs + Rhs; } };
struct FOpSub : public FOpBase { template<typename T> T operator()(T Lhs, T Rhs) const { return Lhs - Rhs; } };
struct FOpMul : public FOpBase { template<typename T> T operator()(T Lhs, T Rhs) const { return Lhs * Rhs; } };
struct FOpDiv : public FOpBase { template<typename T> T operator()(T Lhs, T Rhs) const { return Lhs / GetSafeDivisor(Rhs); } };
struct FOpMin : public FOpBase { template<typename T> T operator()(T Lhs, T Rhs) const { return FMath::Min(Lhs, Rhs); } };
struct FOpMax : public FOpBase { template<typename T> T operator()(T Lhs, T Rhs) const { return FMath::Max(Lhs, Rhs); } };
struct FOpFmod : public FOpBaseNoInt { template<typename T> T operator()(T Lhs, T Rhs) const { return FMath::Fmod(Lhs, Rhs); } };
struct FOpAtan2 : public FOpBaseNoInt { template<typename T> T operator()(T Lhs, T Rhs) const { return FMath::Atan2(Lhs, Rhs); } };
struct FOpLess : public FOpBase { template<typename T> bool operator()(T Lhs, T Rhs) const { return Lhs < Rhs; } };
struct FOpGreater : public FOpBase { template<typename T> bool operator()(T Lhs, T Rhs) const { return Lhs > Rhs; } };
struct FOpLessEqual : public FOpBase { template<typename T> bool operator()(T Lhs, T Rhs) const { return Lhs <= Rhs; } };
struct FOpGreaterEqual : public FOpBase { template<typename T> bool operator()(T Lhs, T Rhs) const { return Lhs >= Rhs; } };

template<typename Operation>
inline FValue UnaryOp(const Operation& Op, const FValue& Value)
{
	if (Value.Type.IsStruct())
	{
		return FValue();
	}
	const FValueTypeDescription TypeDesc = GetValueTypeDescription(Value.Type);
	const int8 NumComponents = TypeDesc.NumComponents;
	
	FValue Result;
	if constexpr (Operation::SupportsDouble)
	{
		if (TypeDesc.ComponentType == EValueComponentType::Double)
		{
			Result.Type = MakeValueType(EValueComponentType::Double, NumComponents);
			const FDoubleValue Cast = Value.AsDouble();
			for (int32 i = 0; i < NumComponents; ++i)
			{
				Result.Component.Add(Op(Cast.Component[i]));
			}
			return Result;
		}
	}

	if constexpr (Operation::SupportsInt)
	{
		if (TypeDesc.ComponentType != EValueComponentType::Float)
		{
			Result.Type = MakeValueType(EValueComponentType::Int, NumComponents);
			const FIntValue Cast = Value.AsInt();
			for (int32 i = 0; i < NumComponents; ++i)
			{
				Result.Component.Add(Op(Cast.Component[i]));
			}
			return Result;
		}
	}

	Result.Type = MakeValueType(EValueComponentType::Float, NumComponents);
	const FFloatValue Cast = Value.AsFloat();
	for (int32 i = 0; i < NumComponents; ++i)
	{
		Result.Component.Add(Op(Cast.Component[i]));
	}
	return Result;
}

inline int8 GetNumComponentsResult(int8 Lhs, int8 Rhs)
{
	// operations between scalar and non-scalar will splat the scalar value
	// otherwise, operations should only be between types with same number of components
	return (Lhs == 1 || Rhs == 1) ? FMath::Max(Lhs, Rhs) : FMath::Min(Lhs, Rhs);
}

template<typename Operation>
inline FValue BinaryOp(const Operation& Op, const FValue& Lhs, const FValue& Rhs)
{
	if (Lhs.Type.IsStruct() || Rhs.Type.IsStruct())
	{
		return FValue();
	}
	const FValueTypeDescription LhsDesc = GetValueTypeDescription(Lhs.Type);
	const FValueTypeDescription RhsDesc = GetValueTypeDescription(Rhs.Type);
	const int8 NumComponents = GetNumComponentsResult(LhsDesc.NumComponents, RhsDesc.NumComponents);
	
	FValue Result;
	if constexpr (Operation::SupportsDouble)
	{
		if (LhsDesc.ComponentType == EValueComponentType::Double || RhsDesc.ComponentType == EValueComponentType::Double)
		{
			Result.Type = MakeValueType(EValueComponentType::Double, NumComponents);
			const FDoubleValue LhsCast = Lhs.AsDouble();
			const FDoubleValue RhsCast = Rhs.AsDouble();
			for (int32 i = 0; i < NumComponents; ++i)
			{
				Result.Component.Add(Op(LhsCast.Component[i], RhsCast.Component[i]));
			}
			return Result;
		}
	}

	if constexpr (Operation::SupportsInt)
	{
		if (LhsDesc.ComponentType != EValueComponentType::Float && RhsDesc.ComponentType != EValueComponentType::Float)
		{
			Result.Type = MakeValueType(EValueComponentType::Int, NumComponents);
			const FIntValue LhsCast = Lhs.AsInt();
			const FIntValue RhsCast = Rhs.AsInt();
			for (int32 i = 0; i < NumComponents; ++i)
			{
				Result.Component.Add(Op(LhsCast.Component[i], RhsCast.Component[i]));
			}
			return Result;
		}
	}

	Result.Type = MakeValueType(EValueComponentType::Float, NumComponents);
	const FFloatValue LhsCast = Lhs.AsFloat();
	const FFloatValue RhsCast = Rhs.AsFloat();
	for (int32 i = 0; i < NumComponents; ++i)
	{
		Result.Component.Add(Op(LhsCast.Component[i], RhsCast.Component[i]));
	}
	return Result;
}

template<typename Operation>
inline FValue CompareOp(const Operation& Op, const FValue& Lhs, const FValue& Rhs)
{
	if (Lhs.Type.IsStruct() || Rhs.Type.IsStruct())
	{
		return FValue();
	}
	const FValueTypeDescription LhsDesc = GetValueTypeDescription(Lhs.Type);
	const FValueTypeDescription RhsDesc = GetValueTypeDescription(Rhs.Type);
	const int8 NumComponents = GetNumComponentsResult(LhsDesc.NumComponents, RhsDesc.NumComponents);

	FValue Result;
	Result.Type = MakeValueType(EValueComponentType::Bool, NumComponents);
	if constexpr (Operation::SupportsDouble)
	{
		if (LhsDesc.ComponentType == EValueComponentType::Double || RhsDesc.ComponentType == EValueComponentType::Double)
		{
			const FDoubleValue LhsCast = Lhs.AsDouble();
			const FDoubleValue RhsCast = Rhs.AsDouble();
			for (int32 i = 0; i < NumComponents; ++i)
			{
				Result.Component.Add(Op(LhsCast.Component[i], RhsCast.Component[i]));
			}
			return Result;
		}
	}

	if constexpr (Operation::SupportsInt)
	{
		if (LhsDesc.ComponentType != EValueComponentType::Float && RhsDesc.ComponentType != EValueComponentType::Float)
		{
			const FIntValue LhsCast = Lhs.AsInt();
			const FIntValue RhsCast = Rhs.AsInt();
			for (int32 i = 0; i < NumComponents; ++i)
			{
				Result.Component.Add(Op(LhsCast.Component[i], RhsCast.Component[i]));
			}
			return Result;
		}
	}

	const FFloatValue LhsCast = Lhs.AsFloat();
	const FFloatValue RhsCast = Rhs.AsFloat();
	for (int32 i = 0; i < NumComponents; ++i)
	{
		Result.Component.Add(Op(LhsCast.Component[i], RhsCast.Component[i]));
	}
	return Result;
}

} // namespace Private

bool operator==(const FValue& Lhs, const FValue& Rhs)
{
	if (Lhs.Type != Rhs.Type)
	{
		return false;
	}

	check(Lhs.Component.Num() == Rhs.Component.Num());
	for (int32 i = 0u; i < Lhs.Component.Num(); ++i)
	{
		if (Lhs.Component[i].Packed != Rhs.Component[i].Packed)
		{
			return false;
		}
	}
	return true;
}

uint32 GetTypeHash(const FType& Type)
{
	uint32 Result = ::GetTypeHash(Type.ValueType);
	if (Type.IsStruct())
	{
		Result = HashCombine(Result, ::GetTypeHash(Type.StructType));
	}
	return Result;
}

uint32 GetTypeHash(const FValue& Value)
{
	uint32 Result = GetTypeHash(Value.Type);
	const int32 NumComponents = Value.Type.GetNumComponents();
	for(int32 Index = 0; Index < NumComponents; ++Index)
	{
		uint32 ComponentHash = 0u;
		switch (Value.Type.GetComponentType(Index))
		{
		case EValueComponentType::Float: ComponentHash = ::GetTypeHash(Value.Component[Index].Float); break;
		case EValueComponentType::Double: ComponentHash = ::GetTypeHash(Value.Component[Index].Double); break;
		case EValueComponentType::Int: ComponentHash = ::GetTypeHash(Value.Component[Index].Int); break;
		case EValueComponentType::Bool: ComponentHash = ::GetTypeHash(Value.Component[Index].Bool); break;
		default: checkNoEntry(); break;
		}
		Result = HashCombine(Result, ComponentHash);
	}
	return Result;
}

FValue Neg(const FValue& Value)
{
	return Private::UnaryOp(Private::FOpNeg(), Value);
}

FValue Abs(const FValue& Value)
{
	return Private::UnaryOp(Private::FOpAbs(), Value);
}

FValue Saturate(const FValue& Value)
{
	return Private::UnaryOp(Private::FOpSaturate(), Value);
}

FValue Floor(const FValue& Value)
{
	return Private::UnaryOp(Private::FOpFloor(), Value);
}

FValue Ceil(const FValue& Value)
{
	return Private::UnaryOp(Private::FOpCeil(), Value);
}

FValue Round(const FValue& Value)
{
	return Private::UnaryOp(Private::FOpRound(), Value);
}

FValue Trunc(const FValue& Value)
{
	return Private::UnaryOp(Private::FOpTrunc(), Value);
}

FValue Sign(const FValue& Value)
{
	return Private::UnaryOp(Private::FOpSign(), Value);
}

FValue Frac(const FValue& Value)
{
	return Private::UnaryOp(Private::FOpFrac(), Value);
}

FValue Fractional(const FValue& Value)
{
	return Private::UnaryOp(Private::FOpFractional(), Value);
}

FValue Sqrt(const FValue& Value)
{
	return Private::UnaryOp(Private::FOpSqrt(), Value);
}

FValue Rcp(const FValue& Value)
{
	return Private::UnaryOp(Private::FOpRcp(), Value);
}

FValue Log2(const FValue& Value)
{
	return Private::UnaryOp(Private::FOpLog2(), Value);
}

FValue Log10(const FValue& Value)
{
	return Private::UnaryOp(Private::FOpLog10(), Value);
}

FValue Sin(const FValue& Value)
{
	return Private::UnaryOp(Private::FOpSin(), Value);
}

FValue Cos(const FValue& Value)
{
	return Private::UnaryOp(Private::FOpCos(), Value);
}

FValue Tan(const FValue& Value)
{
	return Private::UnaryOp(Private::FOpTan(), Value);
}

FValue Asin(const FValue& Value)
{
	return Private::UnaryOp(Private::FOpAsin(), Value);
}

FValue Acos(const FValue& Value)
{
	return Private::UnaryOp(Private::FOpAcos(), Value);
}

FValue Atan(const FValue& Value)
{
	return Private::UnaryOp(Private::FOpAtan(), Value);
}

FValue Add(const FValue& Lhs, const FValue& Rhs)
{
	return Private::BinaryOp(Private::FOpAdd(), Lhs, Rhs);
}

FValue Sub(const FValue& Lhs, const FValue& Rhs)
{
	return Private::BinaryOp(Private::FOpSub(), Lhs, Rhs);
}

FValue Mul(const FValue& Lhs, const FValue& Rhs)
{
	return Private::BinaryOp(Private::FOpMul(), Lhs, Rhs);
}

FValue Div(const FValue& Lhs, const FValue& Rhs)
{
	return Private::BinaryOp(Private::FOpDiv(), Lhs, Rhs);
}

FValue Less(const FValue& Lhs, const FValue& Rhs)
{
	return Private::CompareOp(Private::FOpLess(), Lhs, Rhs);
}

FValue Greater(const FValue& Lhs, const FValue& Rhs)
{
	return Private::CompareOp(Private::FOpGreater(), Lhs, Rhs);
}

FValue LessEqual(const FValue& Lhs, const FValue& Rhs)
{
	return Private::CompareOp(Private::FOpLessEqual(), Lhs, Rhs);
}

FValue GreaterEqual(const FValue& Lhs, const FValue& Rhs)
{
	return Private::CompareOp(Private::FOpGreaterEqual(), Lhs, Rhs);
}

FValue Min(const FValue& Lhs, const FValue& Rhs)
{
	return Private::BinaryOp(Private::FOpMin(), Lhs, Rhs);
}

FValue Max(const FValue& Lhs, const FValue& Rhs)
{
	return Private::BinaryOp(Private::FOpMax(), Lhs, Rhs);
}

FValue Fmod(const FValue& Lhs, const FValue& Rhs)
{
	return Private::BinaryOp(Private::FOpFmod(), Lhs, Rhs);
}

FValue Atan2(const FValue& Lhs, const FValue& Rhs)
{
	return Private::BinaryOp(Private::FOpAtan2(), Lhs, Rhs);
}

FValue Clamp(const FValue& Value, const FValue& Low, const FValue& High)
{
	return Min(Max(Value, Low), High);
}

FValue Dot(const FValue& Lhs, const FValue& Rhs)
{
	if (Lhs.Type.IsStruct() || Rhs.Type.IsStruct())
	{
		return FValue();
	}
	const FValueTypeDescription LhsDesc = GetValueTypeDescription(Lhs.Type);
	const FValueTypeDescription RhsDesc = GetValueTypeDescription(Rhs.Type);
	const int8 NumComponents = Private::GetNumComponentsResult(LhsDesc.NumComponents, RhsDesc.NumComponents);

	FValue Result;
	if (LhsDesc.ComponentType == EValueComponentType::Double || RhsDesc.ComponentType == EValueComponentType::Double)
	{
		Result.Type = EValueType::Double1;
		const FDoubleValue LhsValue = Lhs.AsDouble();
		const FDoubleValue RhsValue = Rhs.AsDouble();
		double ComponentValue = 0.0;
		for (int32 i = 0; i < NumComponents; ++i)
		{
			ComponentValue += LhsValue.Component[i] * RhsValue.Component[i];
		}
		Result.Component.Add(ComponentValue);
	}
	else if (LhsDesc.ComponentType == EValueComponentType::Float || RhsDesc.ComponentType == EValueComponentType::Float)
	{
		Result.Type = EValueType::Float1;
		const FFloatValue LhsValue = Lhs.AsFloat();
		const FFloatValue RhsValue = Rhs.AsFloat();
		float ComponentValue = 0.0f;
		for (int32 i = 0; i < NumComponents; ++i)
		{
			ComponentValue += LhsValue.Component[i] * RhsValue.Component[i];
		}
		Result.Component.Add(ComponentValue);
	}
	else
	{
		Result.Type = EValueType::Int1;
		const FIntValue LhsValue = Lhs.AsInt();
		const FIntValue RhsValue = Rhs.AsInt();
		int32 ComponentValue = 0;
		for (int32 i = 0; i < NumComponents; ++i)
		{
			ComponentValue += LhsValue.Component[i] * RhsValue.Component[i];
		}
		Result.Component.Add(ComponentValue);
	}
	return Result;
}

FValue Cross(const FValue& Lhs, const FValue& Rhs)
{
	if (Lhs.Type.IsStruct() || Rhs.Type.IsStruct())
	{
		return FValue();
	}
	const FValueTypeDescription LhsDesc = GetValueTypeDescription(Lhs.Type);
	const FValueTypeDescription RhsDesc = GetValueTypeDescription(Rhs.Type);

	FValue Result;
	if (LhsDesc.ComponentType == EValueComponentType::Double || RhsDesc.ComponentType == EValueComponentType::Double)
	{
		Result.Type = EValueType::Double3;
		const FDoubleValue LhsValue = Lhs.AsDouble();
		const FDoubleValue RhsValue = Rhs.AsDouble();

		Result.Component.Add(LhsValue.Component[1] * RhsValue.Component[2] - LhsValue.Component[2] * RhsValue.Component[1]);
		Result.Component.Add(LhsValue.Component[2] * RhsValue.Component[0] - LhsValue.Component[0] * RhsValue.Component[2]);
		Result.Component.Add(LhsValue.Component[0] * RhsValue.Component[1] - LhsValue.Component[1] * RhsValue.Component[0]);
	}
	else if (LhsDesc.ComponentType == EValueComponentType::Float || RhsDesc.ComponentType == EValueComponentType::Float)
	{
		Result.Type = EValueType::Float3;
		const FFloatValue LhsValue = Lhs.AsFloat();
		const FFloatValue RhsValue = Rhs.AsFloat();
		
		Result.Component.Add(LhsValue.Component[1] * RhsValue.Component[2] - LhsValue.Component[2] * RhsValue.Component[1]);
		Result.Component.Add(LhsValue.Component[2] * RhsValue.Component[0] - LhsValue.Component[0] * RhsValue.Component[2]);
		Result.Component.Add(LhsValue.Component[0] * RhsValue.Component[1] - LhsValue.Component[1] * RhsValue.Component[0]);
	}
	else
	{
		Result.Type = EValueType::Int3;
		const FIntValue LhsValue = Lhs.AsInt();
		const FIntValue RhsValue = Rhs.AsInt();

		Result.Component.Add(LhsValue.Component[1] * RhsValue.Component[2] - LhsValue.Component[2] * RhsValue.Component[1]);
		Result.Component.Add(LhsValue.Component[2] * RhsValue.Component[0] - LhsValue.Component[0] * RhsValue.Component[2]);
		Result.Component.Add(LhsValue.Component[0] * RhsValue.Component[1] - LhsValue.Component[1] * RhsValue.Component[0]);
	}
	return Result;
}

FValue Append(const FValue& Lhs, const FValue& Rhs)
{
	if (Lhs.Type.IsStruct() || Rhs.Type.IsStruct())
	{
		return FValue();
	}
	const FValueTypeDescription LhsDesc = GetValueTypeDescription(Lhs.Type);
	const FValueTypeDescription RhsDesc = GetValueTypeDescription(Rhs.Type);

	FValue Result;
	const int32 NumComponents = FMath::Min<int32>(LhsDesc.NumComponents + RhsDesc.NumComponents, 4);
	if (LhsDesc.ComponentType == RhsDesc.ComponentType)
	{
		// If both values have the same component type, use as-is
		// (otherwise will need to convert)
		
		Result.Type = MakeValueType(LhsDesc.ComponentType, NumComponents);
		for (int32 i = 0; i < LhsDesc.NumComponents && Result.Component.Num() < NumComponents; ++i)
		{
			Result.Component.Add(Lhs.Component[i]);
		}
		for (int32 i = 0; i < RhsDesc.NumComponents && Result.Component.Num() < NumComponents; ++i)
		{
			Result.Component.Add(Rhs.Component[i]);
		}
	}
	else if (LhsDesc.ComponentType == EValueComponentType::Double || RhsDesc.ComponentType == EValueComponentType::Double)
	{
		Result.Type = MakeValueType(EValueComponentType::Double, NumComponents);
		const FDoubleValue LhsValue = Lhs.AsDouble();
		const FDoubleValue RhsValue = Rhs.AsDouble();
		for (int32 i = 0; i < LhsDesc.NumComponents && Result.Component.Num() < NumComponents; ++i)
		{
			Result.Component.Add(LhsValue.Component[i]);
		}
		for (int32 i = 0; i < RhsDesc.NumComponents && Result.Component.Num() < NumComponents; ++i)
		{
			Result.Component.Add(RhsValue.Component[i]);
		}
	}
	else if (LhsDesc.ComponentType == EValueComponentType::Float || RhsDesc.ComponentType == EValueComponentType::Float)
	{
		Result.Type = MakeValueType(EValueComponentType::Float, NumComponents);
		const FFloatValue LhsValue = Lhs.AsFloat();
		const FFloatValue RhsValue = Rhs.AsFloat();
		for (int32 i = 0; i < LhsDesc.NumComponents && Result.Component.Num() < NumComponents; ++i)
		{
			Result.Component.Add(LhsValue.Component[i]);
		}
		for (int32 i = 0; i < RhsDesc.NumComponents && Result.Component.Num() < NumComponents; ++i)
		{
			Result.Component.Add(RhsValue.Component[i]);
		}
	}
	else
	{
		Result.Type = MakeValueType(EValueComponentType::Int, NumComponents);
		const FIntValue LhsValue = Lhs.AsInt();
		const FIntValue RhsValue = Rhs.AsInt();
		for (int32 i = 0; i < LhsDesc.NumComponents && Result.Component.Num() < NumComponents; ++i)
		{
			Result.Component.Add(LhsValue.Component[i]);
		}
		for (int32 i = 0; i < RhsDesc.NumComponents && Result.Component.Num() < NumComponents; ++i)
		{
			Result.Component.Add(RhsValue.Component[i]);
		}
	}
	return Result;
}

FValue Cast(const FValue& Value, EValueType Type)
{
	const EValueType SourceType = Value.GetType();
	if (Type == SourceType)
	{
		return Value;
	}

	FValue Result;
	Result.Type = Type;

	switch (GetValueTypeDescription(Type).ComponentType)
	{
	case EValueComponentType::Float: Private::Cast(Private::FCastFloat(), Value, Result); break;
	case EValueComponentType::Double: Private::Cast(Private::FCastDouble(), Value, Result); break;
	case EValueComponentType::Int: Private::Cast(Private::FCastInt(), Value, Result); break;
	case EValueComponentType::Bool: Private::Cast(Private::FCastBool(), Value, Result); break;
	default: checkNoEntry(); break;
	}

	return Result;
}

} // namespace Shader
} // namespace UE
