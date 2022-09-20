// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceGrid3DCollectionUtils.h"
#include "NiagaraDataInterfaceGrid3DCollection.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceGrid3DCollection"

static int32 GNiagaraGrid3DUseRGBAGrid = 1;
static FAutoConsoleVariableRef CVarNiagaraGrid3DUseRGBAGrid(
	TEXT("fx.Niagara.Grid3D.UseRGBAGrid"),
	GNiagaraGrid3DUseRGBAGrid,
	TEXT("Use RGBA textures when possible\n"),
	ECVF_Default
);

TArray<FString> FGrid3DCollectionAttributeHelper::Channels = { "r", "g", "b", "a" };

// static member function
bool FGrid3DCollectionAttributeHelper::ShouldUseRGBAGrid(const int TotalChannels, const int TotalNumAttributes)
{
	return TotalNumAttributes == 1 && TotalChannels <= 4 && GNiagaraGrid3DUseRGBAGrid != 0;
}

bool FGrid3DCollectionAttributeHelper::SupportsRGBAGrid()
{
	return GNiagaraGrid3DUseRGBAGrid == 1;
}

FGrid3DCollectionAttributeHelper::FGrid3DCollectionAttributeHelper(const FNiagaraDataInterfaceGPUParamInfo& InParamInfo, TArray<FText>* OutWarnings /*= nullptr*/) : ParamInfo(InParamInfo)
{
	AttributeInfos.Reserve(ParamInfo.GeneratedFunctions.Num());
	TotalChannels = 0;

	for (const FNiagaraDataInterfaceGeneratedFunction& Function : ParamInfo.GeneratedFunctions)
	{
		const FName* AttributeName = Function.FindSpecifierValue(UNiagaraDataInterfaceGrid3DCollection::NAME_Attribute);
		if (AttributeName == nullptr)
		{
			continue;
		}

		if (const FAttributeInfo* ExistingAttribute = FindAttributeInfo(*AttributeName))
		{
			if (OutWarnings != nullptr)
			{
				FNiagaraTypeDefinition AttributeTypeDef = UNiagaraDataInterfaceGrid3DCollection::GetValueTypeFromFuncName(Function.DefinitionName);
				if (ExistingAttribute->TypeDef != AttributeTypeDef)
				{
					OutWarnings->Emplace(FText::Format(LOCTEXT("BadType", "Same name, different types! {0} vs {1}, Attribute {2}"), AttributeTypeDef.GetNameText(), ExistingAttribute->TypeDef.GetNameText(), FText::FromName(ExistingAttribute->Name)));
				}
			}
			continue;
		}

		FAttributeInfo& AttributeInfo = AttributeInfos.AddDefaulted_GetRef();
		AttributeInfo.Name = *AttributeName;
		AttributeInfo.TypeDef = UNiagaraDataInterfaceGrid3DCollection::GetValueTypeFromFuncName(Function.DefinitionName);
		AttributeInfo.NumChannels = UNiagaraDataInterfaceGrid3DCollection::GetComponentCountFromFuncName(Function.DefinitionName);
		AttributeInfo.ChannelOffset = TotalChannels;
		AttributeInfo.AttributeIndex = AttributeInfos.Num() - 1;
		TotalChannels += AttributeInfo.NumChannels;
	}
}

bool FGrid3DCollectionAttributeHelper::UseRGBAGrid()
{
	return ShouldUseRGBAGrid(TotalChannels, AttributeInfos.Num());
}

const FGrid3DCollectionAttributeHelper::FGrid3DCollectionAttributeHelper::FAttributeInfo* FGrid3DCollectionAttributeHelper::FindAttributeInfo(FName AttributeName) const
{
	return AttributeInfos.FindByPredicate([AttributeName](const FAttributeInfo& Info) { return Info.Name == AttributeName; });
}

#if WITH_EDITORONLY_DATA
FString FGrid3DCollectionAttributeHelper::GetPerAttributePixelOffset(const TCHAR* DataInterfaceHLSLSymbol)
{
	return FString::Printf(TEXT("int3(%s_%s[(AttributeIndex * 2) + 0].xyz)"), DataInterfaceHLSLSymbol, *UNiagaraDataInterfaceGrid3DCollection::PerAttributeDataName);
}

FString FGrid3DCollectionAttributeHelper::GetPerAttributePixelOffset() const
{
	return GetPerAttributePixelOffset(*ParamInfo.DataInterfaceHLSLSymbol);
}

FString FGrid3DCollectionAttributeHelper::GetPerAttributeUVWOffset(const TCHAR* DataInterfaceHLSLSymbol)
{
	return FString::Printf(TEXT("%s%s[(AttributeIndex * 2) + 1].xyz"), *UNiagaraDataInterfaceGrid3DCollection::PerAttributeDataName, DataInterfaceHLSLSymbol);
}

FString FGrid3DCollectionAttributeHelper::GetPerAttributeUVWOffset() const
{
	return GetPerAttributeUVWOffset(*ParamInfo.DataInterfaceHLSLSymbol);
}

FString FGrid3DCollectionAttributeHelper::GetAttributeIndex(bool bUseAttributeIndirection, const FAttributeInfo* AttributeInfo, int Channel /*= 0*/) const
{
	check(AttributeInfo);
	if (bUseAttributeIndirection)
	{
		return FString::Printf(TEXT("int(%s%s[%d].w + %d)"), *UNiagaraDataInterfaceGrid3DCollection::PerAttributeDataName, *ParamInfo.DataInterfaceHLSLSymbol, AttributeInfo->AttributeIndex, Channel);
	}
	else
	{
		return FString::Printf(TEXT("%d"), AttributeInfo->ChannelOffset + Channel);
	}
}

FString FGrid3DCollectionAttributeHelper::GetGridChannelString()
{
	FString NumChannelsString = "";
	if (TotalChannels > 1)
	{
		NumChannelsString = FString::FromInt(TotalChannels);
	}
	return NumChannelsString;
}

void FGrid3DCollectionAttributeHelper::GetChannelStrings(int AttributeIndex, int AttributeNumChannels, FString& NumChannelsString, FString& AttrGridChannels)
{
	NumChannelsString = "";
	if (AttributeNumChannels > 1)
	{
		NumChannelsString = FString::FromInt(AttributeNumChannels);
	}

	AttrGridChannels = FGrid3DCollectionAttributeHelper::Channels[AttributeIndex];
	for (int i = 1; i < AttributeNumChannels; ++i)
	{
		AttrGridChannels += FGrid3DCollectionAttributeHelper::Channels[AttributeIndex + i];
	}
}

void FGrid3DCollectionAttributeHelper::GetChannelStrings(const FAttributeInfo* AttributeInfo, FString& NumChannelsString, FString& AttrGridChannels)
{
	NumChannelsString = "";
	if (AttributeInfo->NumChannels > 1)
	{
		NumChannelsString = FString::FromInt(AttributeInfo->NumChannels);
	}

	AttrGridChannels = FGrid3DCollectionAttributeHelper::Channels[AttributeInfo->AttributeIndex];
	for (int i = 1; i < AttributeInfo->NumChannels; ++i)
	{
		AttrGridChannels += FGrid3DCollectionAttributeHelper::Channels[AttributeInfo->AttributeIndex + i];
	}
}

bool FGrid3DCollectionAttributeHelper::WriteGetHLSL(AttributeRetrievalMode AttributeStorage, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, FString& OutHLSL)
{
	const FName* AttributeName = FunctionInfo.FindSpecifierValue(UNiagaraDataInterfaceGrid3DCollection::NAME_Attribute);
	if (AttributeName == nullptr)
	{
		return false;
	}

	const FAttributeInfo* AttributeInfo = FindAttributeInfo(*AttributeName);
	if (AttributeInfo == nullptr)
	{
		return false;
	}

	const FString GridNameHLSL = ParamInfo.DataInterfaceHLSLSymbol + UNiagaraDataInterfaceGrid3DCollection::GridName;

	if (AttributeStorage == AttributeRetrievalMode::RGBAGrid)
	{

		FString NumChannelsString;
		FString AttrGridChannels;

		GetChannelStrings(AttributeInfo, NumChannelsString, AttrGridChannels);


		OutHLSL.Appendf(TEXT("void %s(int IndexX, int IndexY, int IndexZ, out float%s Value)\n"), *FunctionInfo.InstanceName, *NumChannelsString);
		OutHLSL.Appendf(TEXT("{\n"));
		OutHLSL.Appendf(TEXT("	Value = %s.Load(int4(IndexX, IndexY, IndexZ, 0)).%s;\n"), *GridNameHLSL, *AttrGridChannels);
		OutHLSL.Appendf(TEXT("}\n"));
	}
	else
	{
		if (AttributeInfo->NumChannels == 1)
		{
			OutHLSL.Appendf(TEXT("void %s(int IndexX, int IndexY, int IndexZ, out float Value)\n"), *FunctionInfo.InstanceName);
			OutHLSL.Appendf(TEXT("{\n"));
			OutHLSL.Appendf(TEXT("	int z %s;\n"), *GetAttributeIndex(AttributeStorage == AttributeRetrievalMode::Indirection, AttributeInfo));
			OutHLSL.Appendf(TEXT("	int3 PixelOffset = int3(IndexX, IndexY, IndexZ) + %s;\n"), *GetPerAttributePixelOffset());
			OutHLSL.Appendf(TEXT("	Value = %s.Load(int4(PixelOffset, 0));\n"), *GridNameHLSL);
			OutHLSL.Appendf(TEXT("}\n"));
		}
		else
		{
			OutHLSL.Appendf(TEXT("void %s(int IndexX, int IndexY, int IndexZ, out float%d Value)\n"), *FunctionInfo.InstanceName, AttributeInfo->NumChannels);
			OutHLSL.Appendf(TEXT("{\n"));
			for (int32 i = 0; i < AttributeInfo->NumChannels; ++i)
			{
				OutHLSL.Appendf(TEXT("	{\n"));
				OutHLSL.Appendf(TEXT("		int AttributeIndex = %s;\n"), *GetAttributeIndex(AttributeStorage == AttributeRetrievalMode::Indirection, AttributeInfo, i));
				OutHLSL.Appendf(TEXT("		int3 PixelOffset = int3(IndexX, IndexY, IndexZ) + %s;\n"), *GetPerAttributePixelOffset());
				OutHLSL.Appendf(TEXT("		Value[%d] = %s.Load(int4(PixelOffset, 0));\n"), i, *GridNameHLSL);
				OutHLSL.Appendf(TEXT("	}\n"));
			}
			OutHLSL.Appendf(TEXT("}\n"));
		}
	}

	return true;
}

bool FGrid3DCollectionAttributeHelper::WriteGetAtIndexHLSL(AttributeRetrievalMode AttributeStorage, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int NumChannels, FString& OutHLSL)
{
	FString NumChannelsString;
	FString AttrGridChannels;

	const FString GridNameHLSL = ParamInfo.DataInterfaceHLSLSymbol + UNiagaraDataInterfaceGrid3DCollection::GridName;

	GetChannelStrings(0, NumChannels, NumChannelsString, AttrGridChannels);

	if (AttributeInfos.Num() > 1)
	{
		//#todo(dmp): fill this in
		return false;
	}
	else
	{
		OutHLSL.Appendf(TEXT("void %s(int IndexX, int IndexY, int IndexZ, int AttributeIndex, out float%s Value)\n"), *FunctionInfo.InstanceName, *NumChannelsString);
		OutHLSL.Appendf(TEXT("{\n"));
		OutHLSL.Appendf(TEXT("	Value = %s.Load(int4(IndexX, IndexY, IndexZ, 0)).%s;\n"), *GridNameHLSL, *AttrGridChannels);
		OutHLSL.Appendf(TEXT("}\n"));
	}

	return true;
}

bool FGrid3DCollectionAttributeHelper::WriteSetAtIndexHLSL(AttributeRetrievalMode AttributeStorage, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int NumChannels, FString& OutHLSL)
{
	FString NumChannelsString;
	FString AttrGridChannels;

	const FString OutputGridNameHLSL = ParamInfo.DataInterfaceHLSLSymbol + UNiagaraDataInterfaceGrid3DCollection::OutputGridName;

	GetChannelStrings(0, NumChannels, NumChannelsString, AttrGridChannels);

	OutHLSL.Appendf(TEXT("void %s(int IndexX, int IndexY, int IndexZ, int AttributeIndex, float%s Value)\n"), *FunctionInfo.InstanceName, *NumChannelsString);
	OutHLSL.Appendf(TEXT("{\n"));

	// more than 1 attribute, we must read first
	if (AttributeInfos.Num() > 1)
	{
		//#todo(dmp): fill this in
		return false;
	}
	else
	{
		OutHLSL.Appendf(TEXT("	%s[float3(IndexX, IndexY, IndexZ)].%s = Value;\n"), *OutputGridNameHLSL, *AttrGridChannels);
	}
	OutHLSL.Appendf(TEXT("}\n"));

	return true;
}

bool FGrid3DCollectionAttributeHelper::WriteSampleAtIndexHLSL(AttributeRetrievalMode AttributeStorage, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int NumChannels, bool IsCubic, FString& OutHLSL)
{
	FString NumChannelsString;
	FString AttrGridChannels;

	const FString GridNameHLSL = ParamInfo.DataInterfaceHLSLSymbol + UNiagaraDataInterfaceGrid3DCollection::GridName;
	const FString SamplerNameHLSL = ParamInfo.DataInterfaceHLSLSymbol + UNiagaraDataInterfaceGrid3DCollection::SamplerName;

	GetChannelStrings(0, NumChannels, NumChannelsString, AttrGridChannels);

	if (AttributeInfos.Num() > 1)
	{
		//#todo(dmp): fill this in
		return false;
	}
	else
	{
		OutHLSL.Appendf(TEXT("void %s(float3 Unit, int AttributeIndex, out float%s Value)\n"), *FunctionInfo.InstanceName, *NumChannelsString);
		OutHLSL.Appendf(TEXT("{\n"));
		if (IsCubic)
		{
			OutHLSL.Appendf(TEXT("	Value = SampleTriCubicLagrange_%s(%s, Unit, 0).%s;\n"), *ParamInfo.DataInterfaceHLSLSymbol, *SamplerNameHLSL, *AttrGridChannels);
		}
		else
		{
			OutHLSL.Appendf(TEXT("	Value = %s.SampleLevel(%s, Unit, 0).%s;\n"), *GridNameHLSL, *SamplerNameHLSL, *AttrGridChannels);
		}

		OutHLSL.Appendf(TEXT("}\n"));
	}

	return true;
}

bool FGrid3DCollectionAttributeHelper::WriteSetHLSL(AttributeRetrievalMode AttributeStorage, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, FString& OutHLSL)
{
	const FName* AttributeName = FunctionInfo.FindSpecifierValue(UNiagaraDataInterfaceGrid3DCollection::NAME_Attribute);
	if (AttributeName == nullptr)
	{
		return false;
	}

	const FAttributeInfo* AttributeInfo = FindAttributeInfo(*AttributeName);
	if (AttributeInfo == nullptr)
	{
		return false;
	}

	const FString OutputGridNameHLSL = ParamInfo.DataInterfaceHLSLSymbol + UNiagaraDataInterfaceGrid3DCollection::OutputGridName;

	if (AttributeStorage == AttributeRetrievalMode::RGBAGrid)
	{
		FString NumChannelsString;
		FString AttrGridChannels;

		GetChannelStrings(AttributeInfo, NumChannelsString, AttrGridChannels);

		OutHLSL.Appendf(TEXT("void %s(int IndexX, int IndexY, int IndexZ, float%s Value)\n"), *FunctionInfo.InstanceName, *NumChannelsString);
		OutHLSL.Appendf(TEXT("{\n"));

		// more than 1 attrbiute, we must read first
		if (AttributeInfos.Num() > 1)
		{
			FString GridNumChannels = FString::FromInt(TotalChannels);

			OutHLSL.Appendf(TEXT("	float%s TmpValue = %s.Load(int4(IndexX, IndexY, IndexZ, 0));\n"), *GridNumChannels, *OutputGridNameHLSL);
			OutHLSL.Appendf(TEXT("	TmpValue.%s = Value;\n"), *AttrGridChannels);
			OutHLSL.Appendf(TEXT("	%s[float3(IndexX, IndexY, IndexZ)] = TmpValue;\n"), *OutputGridNameHLSL);
		}
		else
		{
			OutHLSL.Appendf(TEXT("	%s[float3(IndexX, IndexY, IndexZ)].%s = Value;\n"), *OutputGridNameHLSL, *AttrGridChannels);
		}
		OutHLSL.Appendf(TEXT("}\n"));
	}
	else
	{
		if (AttributeInfo->NumChannels == 1)
		{
			OutHLSL.Appendf(TEXT("void %s(int IndexX, int IndexY, int IndexZ, float Value)\n"), *FunctionInfo.InstanceName);
			OutHLSL.Appendf(TEXT("{\n"));
			OutHLSL.Appendf(TEXT("	int AttributeIndex = %s;\n"), *GetAttributeIndex(AttributeStorage == AttributeRetrievalMode::Indirection, AttributeInfo));
			OutHLSL.Appendf(TEXT("	int3 PixelOffset = int3(IndexX, IndexY, IndexZ) + %s;\n"), *GetPerAttributePixelOffset());
			OutHLSL.Appendf(TEXT("	%s[PixelOffset] = Value;\n"), *OutputGridNameHLSL);
			OutHLSL.Appendf(TEXT("}\n"));
		}
		else
		{
			OutHLSL.Appendf(TEXT("void %s(int IndexX, int IndexY, int IndexZ, float%d Value)\n"), *FunctionInfo.InstanceName, AttributeInfo->NumChannels);
			OutHLSL.Appendf(TEXT("{\n"));
			for (int32 i = 0; i < AttributeInfo->NumChannels; ++i)
			{
				OutHLSL.Appendf(TEXT("	{\n"));
				OutHLSL.Appendf(TEXT("		int AttributeIndex = %s;\n"), *GetAttributeIndex(AttributeStorage == AttributeRetrievalMode::Indirection, AttributeInfo, i));
				OutHLSL.Appendf(TEXT("		int3 PixelOffset = int3(IndexX, IndexY, IndexZ) + %s;\n"), *GetPerAttributePixelOffset());
				OutHLSL.Appendf(TEXT("		%s[PixelOffset] = Value[%d];\n"), *OutputGridNameHLSL, i);
				OutHLSL.Appendf(TEXT("	}\n"));
			}
			OutHLSL.Appendf(TEXT("}\n"));
		}
	}
	return true;
}

bool FGrid3DCollectionAttributeHelper::WriteSampleHLSL(AttributeRetrievalMode AttributeStorage, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, bool IsCubic, FString& OutHLSL)
{
	const FName* AttributeName = FunctionInfo.FindSpecifierValue(UNiagaraDataInterfaceGrid3DCollection::NAME_Attribute);
	if (AttributeName == nullptr)
	{
		return false;
	}

	const FAttributeInfo* AttributeInfo = FindAttributeInfo(*AttributeName);
	if (AttributeInfo == nullptr)
	{
		return false;
	}

	const FString GridNameHLSL = ParamInfo.DataInterfaceHLSLSymbol + UNiagaraDataInterfaceGrid3DCollection::GridName;
	const FString SamplerNameHLSL = ParamInfo.DataInterfaceHLSLSymbol + UNiagaraDataInterfaceGrid3DCollection::SamplerName;

	if (AttributeStorage == AttributeRetrievalMode::RGBAGrid)
	{
		FString NumChannelsString;
		FString AttrGridChannels;
		GetChannelStrings(AttributeInfo, NumChannelsString, AttrGridChannels);

		OutHLSL.Appendf(TEXT("void %s(float3 Unit, out float%s Value)\n"), *FunctionInfo.InstanceName, *NumChannelsString);
		OutHLSL.Appendf(TEXT("{\n"));
		if (IsCubic)
		{
			OutHLSL.Appendf(TEXT("	Value = SampleTriCubicLagrange_%s(%s, Unit, 0).%s;\n"), *ParamInfo.DataInterfaceHLSLSymbol, *SamplerNameHLSL, *AttrGridChannels);
		}
		else
		{
			OutHLSL.Appendf(TEXT("	Value = %s.SampleLevel(%s, Unit, 0).%s;\n"), *GridNameHLSL, *SamplerNameHLSL, *AttrGridChannels);
		}

		OutHLSL.Appendf(TEXT("}\n"));
	}
	else
	{
		const FString NumAttributesNameHLSL = UNiagaraDataInterfaceRWBase::NumAttributesName + ParamInfo.DataInterfaceHLSLSymbol;
		if (AttributeInfo->NumChannels == 1)
		{
			OutHLSL.Appendf(TEXT("void %s(float3 Unit, out float Value)\n"), *FunctionInfo.InstanceName);
			OutHLSL.Appendf(TEXT("{\n"));
			OutHLSL.Appendf(TEXT("	float3 TileUVW = clamp(Unit, %s%s, %s%s) * %s%s;\n"), *UNiagaraDataInterfaceGrid3DCollection::UnitClampMinName, *ParamInfo.DataInterfaceHLSLSymbol, *UNiagaraDataInterfaceGrid3DCollection::UnitClampMaxName, *ParamInfo.DataInterfaceHLSLSymbol, *UNiagaraDataInterfaceGrid3DCollection::OneOverNumTilesName, *ParamInfo.DataInterfaceHLSLSymbol);
			OutHLSL.Appendf(TEXT("	int AttributeIndex = %s;\n"), *GetAttributeIndex(AttributeStorage == AttributeRetrievalMode::Indirection, AttributeInfo));
			OutHLSL.Appendf(TEXT("	float3 UVW = TileUVW + %s;\n"), *GetPerAttributeUVWOffset());
			if (IsCubic)
			{
				OutHLSL.Appendf(TEXT("	Value = SampleTriCubicLagrange(%s, Unit, 0).%s;\n"), *ParamInfo.DataInterfaceHLSLSymbol, *SamplerNameHLSL);
			}
			else
			{
				OutHLSL.Appendf(TEXT("	Value = %s.SampleLevel(%s, UVW, 0);\n"), *GridNameHLSL, *SamplerNameHLSL);
			}
			OutHLSL.Appendf(TEXT("}\n"));
		}
		else
		{
			OutHLSL.Appendf(TEXT("void %s(float3 Unit, out float%d Value)\n"), *FunctionInfo.InstanceName, AttributeInfo->NumChannels);
			OutHLSL.Appendf(TEXT("{\n"));
			OutHLSL.Appendf(TEXT("	float3 TileUVW = clamp(Unit, %s%s, %s%s) * %s%s;\n"), *UNiagaraDataInterfaceGrid3DCollection::UnitClampMinName, *ParamInfo.DataInterfaceHLSLSymbol, *UNiagaraDataInterfaceGrid3DCollection::UnitClampMaxName, *ParamInfo.DataInterfaceHLSLSymbol, *UNiagaraDataInterfaceGrid3DCollection::OneOverNumTilesName, *ParamInfo.DataInterfaceHLSLSymbol);
			for (int32 i = 0; i < AttributeInfo->NumChannels; ++i)
			{
				OutHLSL.Appendf(TEXT("	{\n"));
				OutHLSL.Appendf(TEXT("		int AttributeIndex = %s;\n"), *GetAttributeIndex(AttributeStorage == AttributeRetrievalMode::Indirection, AttributeInfo, i));
				OutHLSL.Appendf(TEXT("		float3 UVW = TileUVW + %s;\n"), *GetPerAttributeUVWOffset());
				if (IsCubic)
				{
					OutHLSL.Appendf(TEXT("	Value = SampleTriCubicLagrange(%s, Unit, 0).%s;\n"), *ParamInfo.DataInterfaceHLSLSymbol, *SamplerNameHLSL);
				}
				else
				{
					OutHLSL.Appendf(TEXT("Value[%d] = %s.SampleLevel(%s, UVW, 0);\n"), i, *GridNameHLSL, *SamplerNameHLSL);
				}

				OutHLSL.Appendf(TEXT("	}\n"));
			}
			OutHLSL.Appendf(TEXT("}\n"));
		}
	}
	return true;
}

bool FGrid3DCollectionAttributeHelper::WriteAttributeGetIndexHLSL(bool bUseAttributeIndirection, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, FString& OutHLSL)
{
	const FName* AttributeName = FunctionInfo.FindSpecifierValue(UNiagaraDataInterfaceGrid3DCollection::NAME_Attribute);
	if (AttributeName == nullptr)
	{
		return false;
	}

	const FAttributeInfo* AttributeInfo = FindAttributeInfo(*AttributeName);
	if (AttributeInfo == nullptr)
	{
		return false;
	}


	OutHLSL.Appendf(TEXT("void %s(out int Value)\n"), *FunctionInfo.InstanceName);
	OutHLSL.Appendf(TEXT("{\n"));
	OutHLSL.Appendf(TEXT("	Value = %s;\n"), *GetAttributeIndex(bUseAttributeIndirection, AttributeInfo));
	OutHLSL.Appendf(TEXT("}\n"));

	return true;
}
#endif //WITH_EDITORONLY_DATA

#undef LOCTEXT_NAMESPACE