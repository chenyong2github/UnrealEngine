// Copyright Epic Games, Inc. All Rights Reserved.
#include "Shader/Preshader.h"
#include "Shader/PreshaderEvaluate.h"
#include "Materials/Material.h"
#include "Materials/MaterialUniformExpressions.h"
#include "Engine/Texture.h"
#include "VT/RuntimeVirtualTexture.h"
#include "ExternalTexture.h"

IMPLEMENT_TYPE_LAYOUT(UE::Shader::FPreshaderData);

namespace UE
{
namespace Shader
{

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
	const uint32 Offset = Data.Num();
	NameOffsets.Add(Offset);
	Write((uint16)Index);
}

void FPreshaderData::Append(const FPreshaderData& InPreshader)
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

FPreshaderDataContext::FPreshaderDataContext(const FPreshaderData& InData)
	: Ptr(InData.Data.GetData())
	, EndPtr(Ptr + InData.Data.Num())
	, Names(InData.Names.GetData())
	, NumNames(InData.Names.Num())
{}

FPreshaderDataContext::FPreshaderDataContext(const FPreshaderDataContext& InContext, uint32 InOffset, uint32 InSize)
	: Ptr(InContext.Ptr + InOffset)
	, EndPtr(Ptr + InSize)
	, Names(InContext.Names)
	, NumNames(InContext.NumNames)
{}

template<typename T>
static inline T ReadPreshaderValue(FPreshaderDataContext& RESTRICT Data)
{
	T Result;
	FMemory::Memcpy(&Result, Data.Ptr, sizeof(T));
	Data.Ptr += sizeof(T);
	checkSlow(Data.Ptr <= Data.EndPtr);
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
	check(Index >= 0 && Index < Data.NumNames);
	return Data.Names[Index];
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

template<>
FValue ReadPreshaderValue<FValue>(FPreshaderDataContext& RESTRICT Data)
{
	const EValueType Type = (EValueType)ReadPreshaderValue<uint8>(Data);
	uint32 Size = 0u;
	FValue Result = FValue::FromMemoryImage(Type, Data.Ptr, &Size);
	Data.Ptr += Size;
	checkSlow(Data.Ptr <= Data.EndPtr);
	return Result;
}

static void EvaluateParameter(const FUniformExpressionSet& UniformExpressionSet, uint32 ParameterIndex, const FMaterialRenderContext& Context, FValue& OutValue)
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
static inline void EvaluateUnaryOp(FPreshaderStack& Stack, const Operation& Op)
{
	const FValue Value = Stack.Pop(false);
	Stack.Add(Op(Value));
}

template<typename Operation>
static inline void EvaluateBinaryOp(FPreshaderStack& Stack, const Operation& Op)
{
	const FValue Value1 = Stack.Pop(false);
	const FValue Value0 = Stack.Pop(false);
	Stack.Add(Op(Value0, Value1));
}

template<typename Operation>
static inline void EvaluateTernaryOp(FPreshaderStack& Stack, const Operation& Op)
{
	const FValue Value2 = Stack.Pop(false);
	const FValue Value1 = Stack.Pop(false);
	const FValue Value0 = Stack.Pop(false);
	Stack.Add(Op(Value0, Value1, Value2));
}

static void EvaluateComponentSwizzle(FPreshaderStack& Stack, FPreshaderDataContext& RESTRICT Data)
{
	const uint8 NumElements = ReadPreshaderValue<uint8>(Data);
	const uint8 IndexR = ReadPreshaderValue<uint8>(Data);
	const uint8 IndexG = ReadPreshaderValue<uint8>(Data);
	const uint8 IndexB = ReadPreshaderValue<uint8>(Data);
	const uint8 IndexA = ReadPreshaderValue<uint8>(Data);

	FValue Value = Stack.Pop(false);
	FValue Result;
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
		Stack.Add(FValue((float)SizeX, (float)SizeY, (float)SizeZ));
	}
	else
	{
		Stack.Add(FValue(0.0f, 0.0f, 0.0f));
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
		Stack.Add(FValue(1.0f / (float)SizeX, 1.0f / (float)SizeY, (SizeZ > 0 ? 1.0f / (float)SizeZ : 0.0f)));
	}
	else
	{
		Stack.Add(FValue(0.0f, 0.0f, 0.0f));
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
	Stack.Add(Result);
}

static void EvaluateExternalTextureCoordinateOffset(const FMaterialRenderContext& Context, FPreshaderStack& Stack, FPreshaderDataContext& RESTRICT Data)
{
	const FGuid GuidToLookup = GetExternalTextureGuid(Context, Data);
	FLinearColor Result(0.f, 0.f, 0.f, 0.f);
	if (GuidToLookup.IsValid())
	{
		FExternalTextureRegistry::Get().GetExternalTextureCoordinateOffset(GuidToLookup, Result);
	}
	Stack.Add(Result);
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
		Stack.Add(FValue(Texture->GetUniformParameter(VectorIndex)));
	}
	else
	{
		Stack.Add(FValue(0.f, 0.f, 0.f, 0.f));
	}
}

void EvaluatePreshader(const FUniformExpressionSet* UniformExpressionSet, const FMaterialRenderContext& Context, FPreshaderStack& Stack, FPreshaderDataContext& RESTRICT Data, FValue& OutValue)
{
	uint8 const* const DataEnd = Data.EndPtr;

	Stack.Reset();
	while (Data.Ptr < DataEnd)
	{
		const EPreshaderOpcode Opcode = (EPreshaderOpcode)ReadPreshaderValue<uint8>(Data);
		switch (Opcode)
		{
		case EPreshaderOpcode::ConstantZero:
			Stack.Add(FValue(0.0f));
			break;
		case EPreshaderOpcode::Constant:
			Stack.Add(ReadPreshaderValue<FValue>(Data));
			break;
		case EPreshaderOpcode::Parameter:
			check(UniformExpressionSet);
			EvaluateParameter(*UniformExpressionSet, ReadPreshaderValue<uint16>(Data), Context, Stack.AddDefaulted_GetRef());
			break;
		case EPreshaderOpcode::Add: EvaluateBinaryOp(Stack, Add); break;
		case EPreshaderOpcode::Sub: EvaluateBinaryOp(Stack, Sub); break;
		case EPreshaderOpcode::Mul: EvaluateBinaryOp(Stack, Mul); break;
		case EPreshaderOpcode::Div: EvaluateBinaryOp(Stack, Div); break;
		case EPreshaderOpcode::Fmod: EvaluateBinaryOp(Stack, Fmod); break;
		case EPreshaderOpcode::Min: EvaluateBinaryOp(Stack, Min); break;
		case EPreshaderOpcode::Max: EvaluateBinaryOp(Stack, Max); break;
		case EPreshaderOpcode::Clamp: EvaluateTernaryOp(Stack, Clamp); break;
		case EPreshaderOpcode::Dot: EvaluateBinaryOp(Stack, Dot); break;
		case EPreshaderOpcode::Cross: EvaluateBinaryOp(Stack, Cross); break;
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

void FPreshaderData::Evaluate(FUniformExpressionSet* UniformExpressionSet, const struct FMaterialRenderContext& Context, FValue& OutValue)
{
	FPreshaderStack Stack;
	FPreshaderDataContext PreshaderContext(*this);
	EvaluatePreshader(UniformExpressionSet, Context, Stack, PreshaderContext, OutValue);
}

void FPreshaderData::AppendHash(FSHA1& OutHasher) const
{
	OutHasher.Update((uint8*)Names.GetData(), Names.Num() * Names.GetTypeSize());
	OutHasher.Update((uint8*)NameOffsets.GetData(), NameOffsets.Num() * NameOffsets.GetTypeSize());
	OutHasher.Update((uint8*)Data.GetData(), Data.Num() * Data.GetTypeSize());
}

FSHAHash FPreshaderData::GetHash() const
{
	FSHA1 Hasher;
	AppendHash(Hasher);
	return Hasher.Finalize();
}

} // namespace Shader
} // namespace UE
