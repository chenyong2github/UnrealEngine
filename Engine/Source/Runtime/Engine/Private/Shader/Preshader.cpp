// Copyright Epic Games, Inc. All Rights Reserved.
#include "Shader/Preshader.h"
#include "Shader/PreshaderEvaluate.h"
#include "Materials/Material.h"
#include "Materials/MaterialUniformExpressions.h"
#include "Engine/Texture.h"
#include "VT/RuntimeVirtualTexture.h"
#include "ExternalTexture.h"

IMPLEMENT_TYPE_LAYOUT(UE::Shader::FPreshaderData);

void UE::Shader::FPreshaderData::WriteData(const void* Value, uint32 Size)
{
	Data.Append((uint8*)Value, Size);
}

void UE::Shader::FPreshaderData::WriteName(const FScriptName& Name)
{
	int32 Index = Names.Find(Name);
	if (Index == INDEX_NONE)
	{
		Index = Names.Add(Name);
	}
	check(Index >= 0 && Index <= 0xffff);
	const uint32 Offset = Data.Num();
	NameOffsets.Add(Offset);
	Write((uint16)Index);
}

void UE::Shader::FPreshaderData::Append(const FPreshaderData& InPreshader)
{
	const uint32 BaseOffset = Data.Num();
	Data.Append(InPreshader.Data);

	TArray<uint16> NameIndexRemap;
	NameIndexRemap.Empty(InPreshader.Names.Num());
	for (const FScriptName& Name : InPreshader.Names)
	{
		const int32 Index = Names.AddUnique(Name);
		check(Index >= 0 && Index <= 0xffff);
		NameIndexRemap.Add(Index);
	}

	for (uint32 PrevOffset : InPreshader.NameOffsets)
	{
		const uint32 Offset = BaseOffset + PrevOffset;
		uint16* PrevNameIndex = (uint16*)(Data.GetData() + Offset);
		const uint16 RemapIndex = NameIndexRemap[*PrevNameIndex];
		*PrevNameIndex = RemapIndex;
		NameOffsets.Add(Offset);
	}
}

UE::Shader::FPreshaderDataContext::FPreshaderDataContext(const FPreshaderData& InData)
	: Ptr(InData.Data.GetData())
	, EndPtr(Ptr + InData.Data.Num())
	, Names(InData.Names.GetData())
	, NumNames(InData.Names.Num())
{}

UE::Shader::FPreshaderDataContext::FPreshaderDataContext(const FPreshaderDataContext& InContext, uint32 InOffset, uint32 InSize)
	: Ptr(InContext.Ptr + InOffset)
	, EndPtr(Ptr + InSize)
	, Names(InContext.Names)
	, NumNames(InContext.NumNames)
{}

template<typename T>
static inline T ReadPreshaderValue(UE::Shader::FPreshaderDataContext& RESTRICT Data)
{
	T Result;
	FMemory::Memcpy(&Result, Data.Ptr, sizeof(T));
	Data.Ptr += sizeof(T);
	checkSlow(Data.Ptr <= Data.EndPtr);
	return Result;
}

template<>
inline uint8 ReadPreshaderValue<uint8>(UE::Shader::FPreshaderDataContext& RESTRICT Data)
{
	checkSlow(Data.Ptr < Data.EndPtr);
	return *Data.Ptr++;
}

template<>
FScriptName ReadPreshaderValue<FScriptName>(UE::Shader::FPreshaderDataContext& RESTRICT Data)
{
	const int32 Index = ReadPreshaderValue<uint16>(Data);
	check(Index >= 0 && Index < Data.NumNames);
	return Data.Names[Index];
}

template<>
FName ReadPreshaderValue<FName>(UE::Shader::FPreshaderDataContext& RESTRICT Data) = delete;

template<>
FHashedMaterialParameterInfo ReadPreshaderValue<FHashedMaterialParameterInfo>(UE::Shader::FPreshaderDataContext& RESTRICT Data)
{
	const FScriptName Name = ReadPreshaderValue<FScriptName>(Data);
	const int32 Index = ReadPreshaderValue<int32>(Data);
	const TEnumAsByte<EMaterialParameterAssociation> Association = ReadPreshaderValue<TEnumAsByte<EMaterialParameterAssociation>>(Data);
	return FHashedMaterialParameterInfo(Name, Association, Index);
}

template<>
UE::Shader::FValue ReadPreshaderValue<UE::Shader::FValue>(UE::Shader::FPreshaderDataContext& RESTRICT Data)
{
	const UE::Shader::EValueType Type = (UE::Shader::EValueType)ReadPreshaderValue<uint8>(Data);
	uint32 Size = 0u;
	UE::Shader::FValue Result = UE::Shader::FValue::FromMemoryImage(Type, Data.Ptr, &Size);
	Data.Ptr += Size;
	checkSlow(Data.Ptr <= Data.EndPtr);
	return Result;
}

static void EvaluateParameter(const FUniformExpressionSet& UniformExpressionSet, uint32 ParameterIndex, const FMaterialRenderContext& Context, UE::Shader::FValue& OutValue)
{
	const FMaterialNumericParameterInfo& Parameter = UniformExpressionSet.GetNumericParameter(ParameterIndex);
	bool bFoundParameter = false;

	// First allow proxy the chance to override parameter
	if (Context.MaterialRenderProxy)
	{
		FMaterialParameterValue ParameterValue;
		if (Context.MaterialRenderProxy->GetParameterValue(Parameter.ParameterType, Parameter.ParameterInfo, ParameterValue, Context))
		{
			OutValue = ParameterValue.AsShaderValue();
			bFoundParameter = true;
		}
	}

	// Editor overrides
#if WITH_EDITOR
	if (!bFoundParameter)
	{
		bFoundParameter = Context.Material.TransientOverrides.GetNumericOverride(Parameter.ParameterType, Parameter.ParameterInfo, OutValue);
	}
#endif // WITH_EDITOR

	// Default value
	if (!bFoundParameter)
	{
		OutValue = UniformExpressionSet.GetDefaultParameterValue(Parameter.ParameterType, Parameter.DefaultValueOffset);
	}
}

template<typename Operation>
static inline void EvaluateUnaryOp(UE::Shader::FPreshaderStack& Stack, const Operation& Op)
{
	const UE::Shader::FValue Value = Stack.Pop(false);
	Stack.Add(Op(Value));
}

template<typename Operation>
static inline void EvaluateBinaryOp(UE::Shader::FPreshaderStack& Stack, const Operation& Op)
{
	const UE::Shader::FValue Value1 = Stack.Pop(false);
	const UE::Shader::FValue Value0 = Stack.Pop(false);
	Stack.Add(Op(Value0, Value1));
}

template<typename Operation>
static inline void EvaluateTernaryOp(UE::Shader::FPreshaderStack& Stack, const Operation& Op)
{
	const UE::Shader::FValue Value2 = Stack.Pop(false);
	const UE::Shader::FValue Value1 = Stack.Pop(false);
	const UE::Shader::FValue Value0 = Stack.Pop(false);
	Stack.Add(Op(Value0, Value1, Value2));
}

static void EvaluateComponentSwizzle(UE::Shader::FPreshaderStack& Stack, UE::Shader::FPreshaderDataContext& RESTRICT Data)
{
	const uint8 NumElements = ReadPreshaderValue<uint8>(Data);
	const uint8 IndexR = ReadPreshaderValue<uint8>(Data);
	const uint8 IndexG = ReadPreshaderValue<uint8>(Data);
	const uint8 IndexB = ReadPreshaderValue<uint8>(Data);
	const uint8 IndexA = ReadPreshaderValue<uint8>(Data);

	UE::Shader::FValue Value = Stack.Pop(false);
	UE::Shader::FValue Result;
	Result.ComponentType = Value.ComponentType;
	Result.NumComponents = NumElements;

	switch (NumElements)
	{
	case 4:
		Result.Component[3] = Value.GetComponent(IndexA);
		// Fallthrough...
	case 3:
		Result.Component[2] = Value.GetComponent(IndexB);
		// Fallthrough...
	case 2:
		Result.Component[1] = Value.GetComponent(IndexG);
		// Fallthrough...
	case 1:
		Result.Component[0] = Value.GetComponent(IndexR);
		break;
	default:
		UE_LOG(LogMaterial, Fatal, TEXT("Invalid number of swizzle elements: %d"), NumElements);
		break;
	}
	Stack.Add(Result);
}

static const UTexture* GetTextureParameter(const FMaterialRenderContext& Context, UE::Shader::FPreshaderDataContext& RESTRICT Data)
{
	const FHashedMaterialParameterInfo ParameterInfo = ReadPreshaderValue<FHashedMaterialParameterInfo>(Data);
	const int32 TextureIndex = ReadPreshaderValue<int32>(Data);

	const UTexture* Texture = nullptr;
	Context.GetTextureParameterValue(ParameterInfo, TextureIndex, Texture);
	return Texture;
}

static void EvaluateTextureSize(const FMaterialRenderContext& Context, UE::Shader::FPreshaderStack& Stack, UE::Shader::FPreshaderDataContext& RESTRICT Data)
{
	const UTexture* Texture = GetTextureParameter(Context, Data);
	if (Texture && Texture->GetResource())
	{
		const uint32 SizeX = Texture->GetResource()->GetSizeX();
		const uint32 SizeY = Texture->GetResource()->GetSizeY();
		const uint32 SizeZ = Texture->GetResource()->GetSizeZ();
		Stack.Add(UE::Shader::FValue((float)SizeX, (float)SizeY, (float)SizeZ));
	}
	else
	{
		Stack.Add(UE::Shader::FValue(0.0f, 0.0f, 0.0f));
	}
}

static void EvaluateTexelSize(const FMaterialRenderContext& Context, UE::Shader::FPreshaderStack& Stack, UE::Shader::FPreshaderDataContext& RESTRICT Data)
{
	const UTexture* Texture = GetTextureParameter(Context, Data);
	if (Texture && Texture->GetResource())
	{
		const uint32 SizeX = Texture->GetResource()->GetSizeX();
		const uint32 SizeY = Texture->GetResource()->GetSizeY();
		const uint32 SizeZ = Texture->GetResource()->GetSizeZ();
		Stack.Add(UE::Shader::FValue(1.0f / (float)SizeX, 1.0f / (float)SizeY, (SizeZ > 0 ? 1.0f / (float)SizeZ : 0.0f)));
	}
	else
	{
		Stack.Add(UE::Shader::FValue(0.0f, 0.0f, 0.0f));
	}
}

static FGuid GetExternalTextureGuid(const FMaterialRenderContext& Context, UE::Shader::FPreshaderDataContext& RESTRICT Data)
{
	const FScriptName ParameterName = ReadPreshaderValue<FScriptName>(Data);
	const FGuid ExternalTextureGuid = ReadPreshaderValue<FGuid>(Data);
	const int32 TextureIndex = ReadPreshaderValue<int32>(Data);
	return Context.GetExternalTextureGuid(ExternalTextureGuid, ScriptNameToName(ParameterName), TextureIndex);
}

static void EvaluateExternalTextureCoordinateScaleRotation(const FMaterialRenderContext& Context, UE::Shader::FPreshaderStack& Stack, UE::Shader::FPreshaderDataContext& RESTRICT Data)
{
	const FGuid GuidToLookup = GetExternalTextureGuid(Context, Data);
	FLinearColor Result(1.f, 0.f, 0.f, 1.f);
	if (GuidToLookup.IsValid())
	{
		FExternalTextureRegistry::Get().GetExternalTextureCoordinateScaleRotation(GuidToLookup, Result);
	}
	Stack.Add(Result);
}

static void EvaluateExternalTextureCoordinateOffset(const FMaterialRenderContext& Context, UE::Shader::FPreshaderStack& Stack, UE::Shader::FPreshaderDataContext& RESTRICT Data)
{
	const FGuid GuidToLookup = GetExternalTextureGuid(Context, Data);
	FLinearColor Result(0.f, 0.f, 0.f, 0.f);
	if (GuidToLookup.IsValid())
	{
		FExternalTextureRegistry::Get().GetExternalTextureCoordinateOffset(GuidToLookup, Result);
	}
	Stack.Add(Result);
}

static void EvaluateRuntimeVirtualTextureUniform(const FMaterialRenderContext& Context, UE::Shader::FPreshaderStack& Stack, UE::Shader::FPreshaderDataContext& RESTRICT Data)
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
		Stack.Add(UE::Shader::FValue(Texture->GetUniformParameter(VectorIndex)));
	}
	else
	{
		Stack.Add(UE::Shader::FValue(0.f, 0.f, 0.f, 0.f));
	}
}

void UE::Shader::EvaluatePreshader(const FUniformExpressionSet* UniformExpressionSet, const FMaterialRenderContext& Context, FPreshaderStack& Stack, FPreshaderDataContext& RESTRICT Data, FValue& OutValue)
{
	uint8 const* const DataEnd = Data.EndPtr;

	Stack.Reset();
	while (Data.Ptr < DataEnd)
	{
		const EPreshaderOpcode Opcode = (EPreshaderOpcode)ReadPreshaderValue<uint8>(Data);
		switch (Opcode)
		{
		case EPreshaderOpcode::ConstantZero:
			Stack.Add(UE::Shader::FValue(0.0f));
			break;
		case EPreshaderOpcode::Constant:
			Stack.Add(ReadPreshaderValue<UE::Shader::FValue>(Data));
			break;
		case EPreshaderOpcode::Parameter:
			check(UniformExpressionSet);
			EvaluateParameter(*UniformExpressionSet, ReadPreshaderValue<uint16>(Data), Context, Stack.AddDefaulted_GetRef());
			break;
		case EPreshaderOpcode::Add: EvaluateBinaryOp(Stack, UE::Shader::Add); break;
		case EPreshaderOpcode::Sub: EvaluateBinaryOp(Stack, UE::Shader::Sub); break;
		case EPreshaderOpcode::Mul: EvaluateBinaryOp(Stack, UE::Shader::Mul); break;
		case EPreshaderOpcode::Div: EvaluateBinaryOp(Stack, UE::Shader::Div); break;
		case EPreshaderOpcode::Fmod: EvaluateBinaryOp(Stack, UE::Shader::Fmod); break;
		case EPreshaderOpcode::Min: EvaluateBinaryOp(Stack, UE::Shader::Min); break;
		case EPreshaderOpcode::Max: EvaluateBinaryOp(Stack, UE::Shader::Max); break;
		case EPreshaderOpcode::Clamp: EvaluateTernaryOp(Stack, UE::Shader::Clamp); break;
		case EPreshaderOpcode::Dot: EvaluateBinaryOp(Stack, UE::Shader::Dot); break;
		case EPreshaderOpcode::Cross: EvaluateBinaryOp(Stack, UE::Shader::Cross); break;
		case EPreshaderOpcode::Sqrt: EvaluateUnaryOp(Stack, UE::Shader::Sqrt); break;
		case EPreshaderOpcode::Rcp: EvaluateUnaryOp(Stack, UE::Shader::Rcp); break;
		case EPreshaderOpcode::Length: EvaluateUnaryOp(Stack, [](const UE::Shader::FValue& Value) { return Sqrt(Dot(Value, Value)); }); break;
		case EPreshaderOpcode::Normalize: EvaluateUnaryOp(Stack, [](const UE::Shader::FValue& Value) { return Div(Value, Sqrt(Dot(Value, Value))); }); break;
		case EPreshaderOpcode::Sin: EvaluateUnaryOp(Stack, UE::Shader::Sin); break;
		case EPreshaderOpcode::Cos: EvaluateUnaryOp(Stack, UE::Shader::Cos); break;
		case EPreshaderOpcode::Tan: EvaluateUnaryOp(Stack, UE::Shader::Tan); break;
		case EPreshaderOpcode::Asin: EvaluateUnaryOp(Stack, UE::Shader::Asin); break;;
		case EPreshaderOpcode::Acos: EvaluateUnaryOp(Stack, UE::Shader::Acos); break;
		case EPreshaderOpcode::Atan: EvaluateUnaryOp(Stack, UE::Shader::Atan); break;
		case EPreshaderOpcode::Atan2: EvaluateBinaryOp(Stack, UE::Shader::Atan2); break;
		case EPreshaderOpcode::Abs: EvaluateUnaryOp(Stack, UE::Shader::Abs); break;
		case EPreshaderOpcode::Saturate: EvaluateUnaryOp(Stack, UE::Shader::Saturate); break;
		case EPreshaderOpcode::Floor: EvaluateUnaryOp(Stack, UE::Shader::Floor); break;
		case EPreshaderOpcode::Ceil: EvaluateUnaryOp(Stack, UE::Shader::Ceil); break;
		case EPreshaderOpcode::Round: EvaluateUnaryOp(Stack, UE::Shader::Round); break;
		case EPreshaderOpcode::Trunc: EvaluateUnaryOp(Stack, UE::Shader::Trunc); break;
		case EPreshaderOpcode::Sign: EvaluateUnaryOp(Stack, UE::Shader::Sign); break;
		case EPreshaderOpcode::Frac: EvaluateUnaryOp(Stack, UE::Shader::Frac); break;
		case EPreshaderOpcode::Fractional: EvaluateUnaryOp(Stack, UE::Shader::Fractional); break;
		case EPreshaderOpcode::Log2: EvaluateUnaryOp(Stack, UE::Shader::Log2); break;
		case EPreshaderOpcode::Log10: EvaluateUnaryOp(Stack, UE::Shader::Log10); break;
		case EPreshaderOpcode::ComponentSwizzle: EvaluateComponentSwizzle(Stack, Data); break;
		case EPreshaderOpcode::AppendVector: EvaluateBinaryOp(Stack, UE::Shader::Append); break;
		case EPreshaderOpcode::TextureSize: EvaluateTextureSize(Context, Stack, Data); break;
		case EPreshaderOpcode::TexelSize: EvaluateTexelSize(Context, Stack, Data); break;
		case EPreshaderOpcode::ExternalTextureCoordinateScaleRotation: EvaluateExternalTextureCoordinateScaleRotation(Context, Stack, Data); break;
		case EPreshaderOpcode::ExternalTextureCoordinateOffset: EvaluateExternalTextureCoordinateOffset(Context, Stack, Data); break;
		case EPreshaderOpcode::RuntimeVirtualTextureUniform: EvaluateRuntimeVirtualTextureUniform(Context, Stack, Data); break;
		default:
			UE_LOG(LogMaterial, Fatal, TEXT("Unknown preshader opcode %d"), (uint8)Opcode);
			break;
		}
	}
	check(Data.Ptr == DataEnd);

	ensure(Stack.Num() <= 1);
	if (Stack.Num() > 0)
	{
		OutValue = Stack.Last();
	}
}

void UE::Shader::FPreshaderData::Evaluate(FUniformExpressionSet* UniformExpressionSet, const struct FMaterialRenderContext& Context, FValue& OutValue)
{
	FPreshaderStack Stack;
	FPreshaderDataContext PreshaderContext(*this);
	EvaluatePreshader(UniformExpressionSet, Context, Stack, PreshaderContext, OutValue);
}
