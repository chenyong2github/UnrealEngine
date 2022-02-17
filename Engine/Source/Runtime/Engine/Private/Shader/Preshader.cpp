// Copyright Epic Games, Inc. All Rights Reserved.
#include "Shader/Preshader.h"
#include "Shader/PreshaderEvaluate.h"
#include "Materials/Material.h"
#include "Materials/MaterialUniformExpressions.h"
#include "Engine/Texture.h"
#include "VT/RuntimeVirtualTexture.h"
#include "Hash/xxhash.h"
#include "ExternalTexture.h"

IMPLEMENT_TYPE_LAYOUT(UE::Shader::FPreshaderData);
IMPLEMENT_TYPE_LAYOUT(UE::Shader::FPreshaderStructType);

namespace UE
{
namespace Shader
{

FPreshaderType::FPreshaderType(const FType& InType) : ValueType(InType.ValueType)
{
	if (InType.IsStruct())
	{
		StructTypeHash = InType.StructType->Hash;
		StructComponentTypes = InType.StructType->ComponentTypes;
	}
}

FPreshaderType::FPreshaderType(EValueType InType) : ValueType(InType)
{
}

EValueComponentType FPreshaderType::GetComponentType(int32 Index) const
{
	if (IsStruct())
	{
		return StructComponentTypes.IsValidIndex(Index) ? StructComponentTypes[Index] : EValueComponentType::Void;
	}
	else
	{
		const FValueTypeDescription TypeDesc = GetValueTypeDescription(ValueType);
		return (Index >= 0 && Index < TypeDesc.NumComponents) ? TypeDesc.ComponentType : EValueComponentType::Void;
	}
}

void FPreshaderStack::PushValue(const FValue& InValue)
{
	check(InValue.Component.Num() == InValue.Type.GetNumComponents());
	Values.Emplace(InValue.Type);
	Components.Append(InValue.Component);
}

void FPreshaderStack::PushValue(const FPreshaderValue& InValue)
{
	check(InValue.Component.Num() == InValue.Type.GetNumComponents());
	Values.Add(InValue.Type);
	Components.Append(InValue.Component.GetData(), InValue.Component.Num());
}

void FPreshaderStack::PushValue(const FPreshaderType& InType, TArrayView<const FValueComponent> InComponents)
{
	check(InComponents.Num() == InType.GetNumComponents());
	Values.Add(InType);
	Components.Append(InComponents.GetData(), InComponents.Num());
}

TArrayView<FValueComponent> FPreshaderStack::PushEmptyValue(const FPreshaderType& InType)
{
	Values.Add(InType);
	const int32 NumComponents = InType.GetNumComponents();
	const int32 ComponentIndex = Components.AddZeroed(NumComponents);
	return MakeArrayView(Components.GetData() + ComponentIndex, NumComponents);
}

FPreshaderValue FPreshaderStack::PopValue()
{
	FPreshaderValue Value;
	Value.Type = Values.Pop(false);

	const int32 NumComponents = Value.Type.GetNumComponents();
	const int32 ComponentIndex = Components.Num() - NumComponents;

	Value.Component = MakeArrayView(Components.GetData() + ComponentIndex, NumComponents);
	Components.RemoveAt(ComponentIndex, NumComponents, false);

	return Value;
}

FPreshaderValue FPreshaderStack::PeekValue(int32 Offset)
{
	FPreshaderValue Value;
	Value.Type = Values.Last(Offset);

	const int32 NumComponents = Value.Type.GetNumComponents();
	const int32 ComponentIndex = Components.Num() - NumComponents;
	Value.Component = MakeArrayView(Components.GetData() + ComponentIndex, NumComponents);
	return Value;
}

FValue FPreshaderValue::AsShaderValue(const FStructTypeRegistry* TypeRegistry) const
{
	FValue Result;
	if (!Type.IsStruct())
	{
		Result.Type = Type.ValueType;
		Result.Component.Append(Component.GetData(), Component.Num());
	}
	else if (ensure(TypeRegistry))
	{
		Result.Type = TypeRegistry->FindType(Type.StructTypeHash);
		if (Result.Type.IsStruct())
		{
			check(Result.Type.GetNumComponents() == Component.Num());
			Result.Component.Append(Component.GetData(), Component.Num());
		}
	}

	return Result;
}

void FPreshaderData::WriteData(const void* Value, uint32 Size)
{
	Data.Append((uint8*)Value, Size);
}

void FPreshaderData::WriteName(const FScriptName& Name)
{
	int32 Index = Names.Find(Name);
	if (Index == INDEX_NONE)
	{
		Index = Names.Add(Name);
	}

	check(Index >= 0 && Index <= 0xffff);
	Write((uint16)Index);
}

void FPreshaderData::WriteType(const FType& Type)
{
	Write(Type.ValueType);
	if (Type.IsStruct())
	{
		const uint64 Hash = Type.StructType->Hash;
		int32 Index = INDEX_NONE;
		for (int32 PrevIndex = 0; PrevIndex < StructTypes.Num(); ++PrevIndex)
		{
			if (StructTypes[PrevIndex].Hash == Hash)
			{
				Index = PrevIndex;
				break;
			}
		}
		if (Index == INDEX_NONE)
		{
			Index = StructTypes.Num();
			FPreshaderStructType& PreshaderStructType = StructTypes.AddDefaulted_GetRef();
			PreshaderStructType.Hash = Hash;
			PreshaderStructType.ComponentTypeIndex = StructComponentTypes.Num();
			PreshaderStructType.NumComponents = Type.StructType->ComponentTypes.Num();
			StructComponentTypes.Append(Type.StructType->ComponentTypes.GetData(), Type.StructType->ComponentTypes.Num());
		}

		check(Index >= 0 && Index <= 0xffff);
		Write((uint16)Index);
	}
}

void FPreshaderData::WriteValue(const FValue& Value)
{
	const int32 NumComponents = Value.Type.GetNumComponents();

	WriteType(Value.Type);
	for (int32 Index = 0; Index < NumComponents; ++Index)
	{
		const EValueComponentType ComponentType = Value.Type.GetComponentType(Index);
		const int32 ComponentSize = GetComponentTypeSizeInBytes(ComponentType);
		const FValueComponent Component = Value.TryGetComponent(Index);
		WriteData(&Component.Packed, ComponentSize);
	}
}

FPreshaderLabel FPreshaderData::WriteJump(EPreshaderOpcode Op)
{
	WriteOpcode(Op);
	const int32 Offset = Data.Num();
	Write((uint32)0xffffffff); // Write a placeholder for the jump offset
	return FPreshaderLabel(Offset);
}

void FPreshaderData::WriteJump(EPreshaderOpcode Op, FPreshaderLabel Label)
{
	WriteOpcode(Op);
	const int32 Offset = Data.Num();
	const int32 JumpOffset = Label.Offset - Offset - 4; // Compute the offset to jump
	Write(JumpOffset);
}

void FPreshaderData::SetLabel(FPreshaderLabel InLabel)
{
	const int32 TargetOffset = Data.Num();
	const int32 BaseOffset = InLabel.Offset;
	const int32 JumpOffset = TargetOffset - BaseOffset - 4; // Compute the offset to jump
	check(JumpOffset >= 0);

	uint8* Dst = &Data[BaseOffset];
	check(Dst[0] == 0xff && Dst[1] == 0xff && Dst[2] == 0xff && Dst[3] == 0xff);
	FMemory::Memcpy(Dst, &JumpOffset, 4); // Patch the offset into the jump opcode
}

FPreshaderLabel FPreshaderData::GetLabel()
{
	return FPreshaderLabel(Data.Num());
}

FPreshaderDataContext::FPreshaderDataContext(const FPreshaderData& InData)
	: Ptr(InData.Data.GetData())
	, EndPtr(Ptr + InData.Data.Num())
	, Names(InData.Names)
	, StructTypes(InData.StructTypes)
	, StructComponentTypes(InData.StructComponentTypes)
{}

FPreshaderDataContext::FPreshaderDataContext(const FPreshaderDataContext& InContext, uint32 InOffset, uint32 InSize)
	: Ptr(InContext.Ptr + InOffset)
	, EndPtr(Ptr + InSize)
	, Names(InContext.Names)
	, StructTypes(InContext.StructTypes)
	, StructComponentTypes(InContext.StructComponentTypes)
{}

static inline void ReadPreshaderData(FPreshaderDataContext& RESTRICT Data, int32 Size, void* Result)
{
	FMemory::Memcpy(Result, Data.Ptr, Size);
	Data.Ptr += Size;
	checkSlow(Data.Ptr <= Data.EndPtr);
}

template<typename T>
static inline T ReadPreshaderValue(FPreshaderDataContext& RESTRICT Data)
{
	T Result;
	ReadPreshaderData(Data, sizeof(T), &Result);
	return Result;
}

template<>
inline uint8 ReadPreshaderValue<uint8>(FPreshaderDataContext& RESTRICT Data)
{
	checkSlow(Data.Ptr < Data.EndPtr);
	return *Data.Ptr++;
}

template<>
FScriptName ReadPreshaderValue<FScriptName>(FPreshaderDataContext& RESTRICT Data)
{
	const int32 Index = ReadPreshaderValue<uint16>(Data);
	return Data.Names[Index];
}

template<>
FPreshaderType ReadPreshaderValue<FPreshaderType>(FPreshaderDataContext& RESTRICT Data)
{
	FPreshaderType Result;
	Result.ValueType = ReadPreshaderValue<EValueType>(Data);
	if (Result.ValueType == EValueType::Struct)
	{
		const uint16 Index = ReadPreshaderValue<uint16>(Data);
		const FPreshaderStructType& PreshaderStruct = Data.StructTypes[Index];
		Result.StructTypeHash = PreshaderStruct.Hash;
		Result.StructComponentTypes = MakeArrayView(Data.StructComponentTypes.GetData() + PreshaderStruct.ComponentTypeIndex, PreshaderStruct.NumComponents);
	}
	return Result;
}

template<>
FName ReadPreshaderValue<FName>(FPreshaderDataContext& RESTRICT Data) = delete;

template<>
FHashedMaterialParameterInfo ReadPreshaderValue<FHashedMaterialParameterInfo>(FPreshaderDataContext& RESTRICT Data)
{
	const FScriptName Name = ReadPreshaderValue<FScriptName>(Data);
	const int32 Index = ReadPreshaderValue<int32>(Data);
	const TEnumAsByte<EMaterialParameterAssociation> Association = ReadPreshaderValue<TEnumAsByte<EMaterialParameterAssociation>>(Data);
	return FHashedMaterialParameterInfo(Name, Association, Index);
}

static void EvaluateConstantZero(FPreshaderStack& Stack, FPreshaderDataContext& RESTRICT Data)
{
	const FPreshaderType Type = ReadPreshaderValue<FPreshaderType>(Data);
	Stack.PushEmptyValue(Type); // Leave the empty value zero-initialized
}

static void EvaluateConstant(FPreshaderStack& Stack, FPreshaderDataContext& RESTRICT Data)
{
	const FPreshaderType Type = ReadPreshaderValue<FPreshaderType>(Data);
	TArrayView<FValueComponent> Component = Stack.PushEmptyValue(Type);
	for (int32 Index = 0; Index < Component.Num(); ++Index)
	{
		const EValueComponentType ComponentType = Type.GetComponentType(Index);
		const int32 ComponmentSizeInBytes = GetComponentTypeSizeInBytes(ComponentType);
		ReadPreshaderData(Data, ComponmentSizeInBytes, &Component[Index].Packed);
	}
}

static void EvaluateSetField(FPreshaderStack& Stack, FPreshaderDataContext& RESTRICT Data)
{
	const FPreshaderValue Value = Stack.PopValue();
	const FPreshaderValue StructValue = Stack.PeekValue();

	const int32 ComponentIndex = ReadPreshaderValue<int32>(Data);
	const int32 ComponentNum = ReadPreshaderValue<int32>(Data);
	
	// Modify the struct value in-place
	if (Value.Component.Num() == 1)
	{
		// Splat scalar
		const FValueComponent Component = Value.Component[0];
		for (int32 Index = 0; Index < ComponentNum; ++Index)
		{
			StructValue.Component[ComponentIndex + Index] = Component;
		}
	}
	else
	{
		for (int32 Index = 0; Index < ComponentNum; ++Index)
		{
			StructValue.Component[ComponentIndex + Index] = Value.Component[Index];
		}
	}
}

static void EvaluateGetField(FPreshaderStack& Stack, FPreshaderDataContext& RESTRICT Data)
{
	const FPreshaderValue StructValue = Stack.PopValue();
	const FPreshaderType FieldType = ReadPreshaderValue<FPreshaderType>(Data);
	const int32 ComponentIndex = ReadPreshaderValue<int32>(Data);
	const int32 ComponentNum = FieldType.GetNumComponents();

	// Need to make a local copy of components, since StructValue.Component is only valid until we push the result
	TArray<FValueComponent, TInlineAllocator<64>> FieldComponents;
	FieldComponents.Empty(ComponentNum);
	for (int32 Index = 0; Index < ComponentNum; ++Index)
	{
		FieldComponents.Add(StructValue.Component[ComponentIndex + Index]);
	}
	Stack.PushValue(FieldType, FieldComponents);
}

static void EvaluatePushValue(FPreshaderStack& Stack, FPreshaderDataContext& RESTRICT Data)
{
	const int32 StackOffset = ReadPreshaderValue<uint16>(Data);
	const FPreshaderValue Value = Stack.PeekValue(StackOffset);
	// Make a local copy of the component array, as it will be invalidated when pushing the copy
	const TArray<FValueComponent, TInlineAllocator<64>> LocalComponent(Value.Component);
	Stack.PushValue(Value.Type, LocalComponent);
}

static void EvaluateAssign(FPreshaderStack& Stack)
{
	const FPreshaderValue Value = Stack.PopValue();
	// Make a local copy of the component array, as it will be invalidated when pushing the copy
	const TArray<FValueComponent, TInlineAllocator<64>> LocalComponent(Value.Component);

	// Remove the old value
	Stack.PopValue();
	// Replace with the new value
	Stack.PushValue(Value.Type, LocalComponent);
}

static void EvaluateParameter(FPreshaderStack& Stack, const FUniformExpressionSet* UniformExpressionSet, uint32 ParameterIndex, const FMaterialRenderContext& Context)
{
	if (!UniformExpressionSet)
	{
		// return 0 for parameters if we don't have UniformExpressionSet
		Stack.PushEmptyValue(EValueType::Float1);
		return;
	}

	const FMaterialNumericParameterInfo& Parameter = UniformExpressionSet->GetNumericParameter(ParameterIndex);
	bool bFoundParameter = false;

	// First allow proxy the chance to override parameter
	if (Context.MaterialRenderProxy)
	{
		FMaterialParameterValue ParameterValue;
		if (Context.MaterialRenderProxy->GetParameterValue(Parameter.ParameterType, Parameter.ParameterInfo, ParameterValue, Context))
		{
			Stack.PushValue(ParameterValue.AsShaderValue());
			bFoundParameter = true;
		}
	}

	// Editor overrides
#if WITH_EDITOR
	if (!bFoundParameter)
	{
		FValue OverrideValue;
		if (Context.Material.TransientOverrides.GetNumericOverride(Parameter.ParameterType, Parameter.ParameterInfo, OverrideValue))
		{
			Stack.PushValue(OverrideValue);
			bFoundParameter = true;
		}
	}
#endif // WITH_EDITOR

	// Default value
	if (!bFoundParameter)
	{
		Stack.PushValue(UniformExpressionSet->GetDefaultParameterValue(Parameter.ParameterType, Parameter.DefaultValueOffset));
	}
}

template<typename Operation>
static inline void EvaluateUnaryOp(FPreshaderStack& Stack, const Operation& Op)
{
	const FValue Value = Stack.PopValue().AsShaderValue();
	Stack.PushValue(Op(Value));
}

template<typename Operation>
static inline void EvaluateBinaryOp(FPreshaderStack& Stack, const Operation& Op)
{
	const FValue Value1 = Stack.PopValue().AsShaderValue();
	const FValue Value0 = Stack.PopValue().AsShaderValue();
	Stack.PushValue(Op(Value0, Value1));
}

template<typename Operation>
static inline void EvaluateTernaryOp(FPreshaderStack& Stack, const Operation& Op)
{
	const FValue Value2 = Stack.PopValue().AsShaderValue();
	const FValue Value1 = Stack.PopValue().AsShaderValue();
	const FValue Value0 = Stack.PopValue().AsShaderValue();
	Stack.PushValue(Op(Value0, Value1, Value2));
}

static void EvaluateComponentSwizzle(FPreshaderStack& Stack, FPreshaderDataContext& RESTRICT Data)
{
	const uint8 NumElements = ReadPreshaderValue<uint8>(Data);
	const uint8 IndexR = ReadPreshaderValue<uint8>(Data);
	const uint8 IndexG = ReadPreshaderValue<uint8>(Data);
	const uint8 IndexB = ReadPreshaderValue<uint8>(Data);
	const uint8 IndexA = ReadPreshaderValue<uint8>(Data);

	FValue Value = Stack.PopValue().AsShaderValue();
	FValue Result(MakeValueType(Value.Type, NumElements));
	switch (NumElements)
	{
	case 4:
		Result.Component[3] = Value.TryGetComponent(IndexA);
		// Fallthrough...
	case 3:
		Result.Component[2] = Value.TryGetComponent(IndexB);
		// Fallthrough...
	case 2:
		Result.Component[1] = Value.TryGetComponent(IndexG);
		// Fallthrough...
	case 1:
		Result.Component[0] = Value.TryGetComponent(IndexR);
		break;
	default:
		UE_LOG(LogMaterial, Fatal, TEXT("Invalid number of swizzle elements: %d"), NumElements);
		break;
	}
	Stack.PushValue(Result);
}

static const UTexture* GetTextureParameter(const FMaterialRenderContext& Context, FPreshaderDataContext& RESTRICT Data)
{
	const FHashedMaterialParameterInfo ParameterInfo = ReadPreshaderValue<FHashedMaterialParameterInfo>(Data);
	const int32 TextureIndex = ReadPreshaderValue<int32>(Data);

	const UTexture* Texture = nullptr;
	Context.GetTextureParameterValue(ParameterInfo, TextureIndex, Texture);
	return Texture;
}

static void EvaluateTextureSize(const FMaterialRenderContext& Context, FPreshaderStack& Stack, FPreshaderDataContext& RESTRICT Data)
{
	const UTexture* Texture = GetTextureParameter(Context, Data);
	if (Texture && Texture->GetResource())
	{
		const uint32 SizeX = Texture->GetResource()->GetSizeX();
		const uint32 SizeY = Texture->GetResource()->GetSizeY();
		const uint32 SizeZ = Texture->GetResource()->GetSizeZ();
		Stack.PushValue(FValue((float)SizeX, (float)SizeY, (float)SizeZ));
	}
	else
	{
		Stack.PushValue(FValue(0.0f, 0.0f, 0.0f));
	}
}

static void EvaluateTexelSize(const FMaterialRenderContext& Context, FPreshaderStack& Stack, FPreshaderDataContext& RESTRICT Data)
{
	const UTexture* Texture = GetTextureParameter(Context, Data);
	if (Texture && Texture->GetResource())
	{
		const uint32 SizeX = Texture->GetResource()->GetSizeX();
		const uint32 SizeY = Texture->GetResource()->GetSizeY();
		const uint32 SizeZ = Texture->GetResource()->GetSizeZ();
		Stack.PushValue(FValue(1.0f / (float)SizeX, 1.0f / (float)SizeY, (SizeZ > 0 ? 1.0f / (float)SizeZ : 0.0f)));
	}
	else
	{
		Stack.PushValue(FValue(0.0f, 0.0f, 0.0f));
	}
}

static FGuid GetExternalTextureGuid(const FMaterialRenderContext& Context, FPreshaderDataContext& RESTRICT Data)
{
	const FScriptName ParameterName = ReadPreshaderValue<FScriptName>(Data);
	const FGuid ExternalTextureGuid = ReadPreshaderValue<FGuid>(Data);
	const int32 TextureIndex = ReadPreshaderValue<int32>(Data);
	return Context.GetExternalTextureGuid(ExternalTextureGuid, ScriptNameToName(ParameterName), TextureIndex);
}

static void EvaluateExternalTextureCoordinateScaleRotation(const FMaterialRenderContext& Context, FPreshaderStack& Stack, FPreshaderDataContext& RESTRICT Data)
{
	const FGuid GuidToLookup = GetExternalTextureGuid(Context, Data);
	FLinearColor Result(1.f, 0.f, 0.f, 1.f);
	if (GuidToLookup.IsValid())
	{
		FExternalTextureRegistry::Get().GetExternalTextureCoordinateScaleRotation(GuidToLookup, Result);
	}
	Stack.PushValue(Result);
}

static void EvaluateExternalTextureCoordinateOffset(const FMaterialRenderContext& Context, FPreshaderStack& Stack, FPreshaderDataContext& RESTRICT Data)
{
	const FGuid GuidToLookup = GetExternalTextureGuid(Context, Data);
	FLinearColor Result(0.f, 0.f, 0.f, 0.f);
	if (GuidToLookup.IsValid())
	{
		FExternalTextureRegistry::Get().GetExternalTextureCoordinateOffset(GuidToLookup, Result);
	}
	Stack.PushValue(Result);
}

static void EvaluateRuntimeVirtualTextureUniform(const FMaterialRenderContext& Context, FPreshaderStack& Stack, FPreshaderDataContext& RESTRICT Data)
{
	const FHashedMaterialParameterInfo ParameterInfo = ReadPreshaderValue<FHashedMaterialParameterInfo>(Data);
	const int32 TextureIndex = ReadPreshaderValue<int32>(Data);
	const int32 VectorIndex = ReadPreshaderValue<int32>(Data);

	const URuntimeVirtualTexture* Texture = nullptr;
	if (ParameterInfo.Name.IsNone() || !Context.MaterialRenderProxy || !Context.MaterialRenderProxy->GetTextureValue(ParameterInfo, &Texture, Context))
	{
		Texture = GetIndexedTexture<URuntimeVirtualTexture>(Context.Material, TextureIndex);
	}
	if (Texture != nullptr && VectorIndex != INDEX_NONE)
	{
		Stack.PushValue(FValue(Texture->GetUniformParameter(VectorIndex)));
	}
	else
	{
		Stack.PushValue(FValue(0.f, 0.f, 0.f, 0.f));
	}
}

static void EvaluateJump(FPreshaderDataContext& RESTRICT Data)
{
	const int32 JumpOffset = ReadPreshaderValue<int32>(Data);
	check(Data.Ptr + JumpOffset <= Data.EndPtr);
	Data.Ptr += JumpOffset;
}

static void EvaluateJumpIfFalse(FPreshaderStack& Stack, FPreshaderDataContext& RESTRICT Data)
{
	const int32 JumpOffset = ReadPreshaderValue<int32>(Data);
	check(Data.Ptr + JumpOffset <= Data.EndPtr);

	const FValue ConditionValue = Stack.PopValue().AsShaderValue();
	if (!ConditionValue.AsBoolScalar())
	{
		Data.Ptr += JumpOffset;
	}
}

FPreshaderValue EvaluatePreshader(const FUniformExpressionSet* UniformExpressionSet, const FMaterialRenderContext& Context, FPreshaderStack& Stack, FPreshaderDataContext& RESTRICT Data)
{
	uint8 const* const DataEnd = Data.EndPtr;

	Stack.Reset();
	while (Data.Ptr < DataEnd)
	{
		const EPreshaderOpcode Opcode = (EPreshaderOpcode)ReadPreshaderValue<uint8>(Data);
		switch (Opcode)
		{
		case EPreshaderOpcode::ConstantZero: EvaluateConstantZero(Stack, Data); break;
		case EPreshaderOpcode::Constant: EvaluateConstant(Stack, Data); break;
		case EPreshaderOpcode::GetField: EvaluateGetField(Stack, Data); break;
		case EPreshaderOpcode::SetField: EvaluateSetField(Stack, Data); break;
		case EPreshaderOpcode::Parameter:
			EvaluateParameter(Stack, UniformExpressionSet, ReadPreshaderValue<uint16>(Data), Context);
			break;
		case EPreshaderOpcode::PushValue: EvaluatePushValue(Stack, Data); break;
		case EPreshaderOpcode::Assign: EvaluateAssign(Stack); break;
		case EPreshaderOpcode::Add: EvaluateBinaryOp(Stack, Add); break;
		case EPreshaderOpcode::Sub: EvaluateBinaryOp(Stack, Sub); break;
		case EPreshaderOpcode::Mul: EvaluateBinaryOp(Stack, Mul); break;
		case EPreshaderOpcode::Div: EvaluateBinaryOp(Stack, Div); break;
		case EPreshaderOpcode::Less: EvaluateBinaryOp(Stack, Less); break;
		case EPreshaderOpcode::Greater: EvaluateBinaryOp(Stack, Greater); break;
		case EPreshaderOpcode::LessEqual: EvaluateBinaryOp(Stack, LessEqual); break;
		case EPreshaderOpcode::GreaterEqual: EvaluateBinaryOp(Stack, GreaterEqual); break;
		case EPreshaderOpcode::Fmod: EvaluateBinaryOp(Stack, Fmod); break;
		case EPreshaderOpcode::Min: EvaluateBinaryOp(Stack, Min); break;
		case EPreshaderOpcode::Max: EvaluateBinaryOp(Stack, Max); break;
		case EPreshaderOpcode::Clamp: EvaluateTernaryOp(Stack, Clamp); break;
		case EPreshaderOpcode::Dot: EvaluateBinaryOp(Stack, Dot); break;
		case EPreshaderOpcode::Cross: EvaluateBinaryOp(Stack, Cross); break;
		case EPreshaderOpcode::Neg: EvaluateUnaryOp(Stack, Neg); break;
		case EPreshaderOpcode::Sqrt: EvaluateUnaryOp(Stack, Sqrt); break;
		case EPreshaderOpcode::Rcp: EvaluateUnaryOp(Stack, Rcp); break;
		case EPreshaderOpcode::Length: EvaluateUnaryOp(Stack, [](const FValue& Value) { return Sqrt(Dot(Value, Value)); }); break;
		case EPreshaderOpcode::Normalize: EvaluateUnaryOp(Stack, [](const FValue& Value) { return Div(Value, Sqrt(Dot(Value, Value))); }); break;
		case EPreshaderOpcode::Sin: EvaluateUnaryOp(Stack, Sin); break;
		case EPreshaderOpcode::Cos: EvaluateUnaryOp(Stack, Cos); break;
		case EPreshaderOpcode::Tan: EvaluateUnaryOp(Stack, Tan); break;
		case EPreshaderOpcode::Asin: EvaluateUnaryOp(Stack, Asin); break;;
		case EPreshaderOpcode::Acos: EvaluateUnaryOp(Stack, Acos); break;
		case EPreshaderOpcode::Atan: EvaluateUnaryOp(Stack, Atan); break;
		case EPreshaderOpcode::Atan2: EvaluateBinaryOp(Stack, Atan2); break;
		case EPreshaderOpcode::Abs: EvaluateUnaryOp(Stack, Abs); break;
		case EPreshaderOpcode::Saturate: EvaluateUnaryOp(Stack, Saturate); break;
		case EPreshaderOpcode::Floor: EvaluateUnaryOp(Stack, Floor); break;
		case EPreshaderOpcode::Ceil: EvaluateUnaryOp(Stack, Ceil); break;
		case EPreshaderOpcode::Round: EvaluateUnaryOp(Stack, Round); break;
		case EPreshaderOpcode::Trunc: EvaluateUnaryOp(Stack, Trunc); break;
		case EPreshaderOpcode::Sign: EvaluateUnaryOp(Stack, Sign); break;
		case EPreshaderOpcode::Frac: EvaluateUnaryOp(Stack, Frac); break;
		case EPreshaderOpcode::Fractional: EvaluateUnaryOp(Stack, Fractional); break;
		case EPreshaderOpcode::Log2: EvaluateUnaryOp(Stack, Log2); break;
		case EPreshaderOpcode::Log10: EvaluateUnaryOp(Stack, Log10); break;
		case EPreshaderOpcode::ComponentSwizzle: EvaluateComponentSwizzle(Stack, Data); break;
		case EPreshaderOpcode::AppendVector: EvaluateBinaryOp(Stack, Append); break;
		case EPreshaderOpcode::TextureSize: EvaluateTextureSize(Context, Stack, Data); break;
		case EPreshaderOpcode::TexelSize: EvaluateTexelSize(Context, Stack, Data); break;
		case EPreshaderOpcode::ExternalTextureCoordinateScaleRotation: EvaluateExternalTextureCoordinateScaleRotation(Context, Stack, Data); break;
		case EPreshaderOpcode::ExternalTextureCoordinateOffset: EvaluateExternalTextureCoordinateOffset(Context, Stack, Data); break;
		case EPreshaderOpcode::RuntimeVirtualTextureUniform: EvaluateRuntimeVirtualTextureUniform(Context, Stack, Data); break;
		case EPreshaderOpcode::Jump: EvaluateJump(Data); break;
		case EPreshaderOpcode::JumpIfFalse: EvaluateJumpIfFalse(Stack, Data); break;
		default:
			UE_LOG(LogMaterial, Fatal, TEXT("Unknown preshader opcode %d"), (uint8)Opcode);
			break;
		}
	}
	check(Data.Ptr == DataEnd);

	FPreshaderValue Result;
	if (Stack.Num() > 0)
	{
		Result = Stack.PopValue();
	}
	Stack.CheckEmpty();
	return Result;
}

FPreshaderValue FPreshaderData::Evaluate(FUniformExpressionSet* UniformExpressionSet, const struct FMaterialRenderContext& Context, FPreshaderStack& Stack) const
{
	FPreshaderDataContext PreshaderContext(*this);
	return EvaluatePreshader(UniformExpressionSet, Context, Stack, PreshaderContext);
}

FPreshaderValue FPreshaderData::EvaluateConstant(const FMaterial& Material, FPreshaderStack& Stack) const
{
	FPreshaderDataContext PreshaderContext(*this);
	return EvaluatePreshader(nullptr, FMaterialRenderContext(nullptr, Material, nullptr), Stack, PreshaderContext);
}

void FPreshaderData::AppendHash(FXxHash64Builder& OutHasher) const
{
	OutHasher.Update(Names.GetData(), Names.Num() * Names.GetTypeSize());
	OutHasher.Update(StructTypes.GetData(), StructTypes.Num() * StructTypes.GetTypeSize());
	OutHasher.Update(StructComponentTypes.GetData(), StructComponentTypes.Num() * StructComponentTypes.GetTypeSize());
	OutHasher.Update(Data.GetData(), Data.Num() * Data.GetTypeSize());
}

} // namespace Shader
} // namespace UE
