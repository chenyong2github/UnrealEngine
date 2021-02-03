// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MaterialShared.cpp: Shared material implementation.
=============================================================================*/

#include "Materials/MaterialUniformExpressions.h"
#include "CoreGlobals.h"
#include "SceneManagement.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceSupport.h"
#include "Materials/MaterialParameterCollection.h"
#include "ExternalTexture.h"
#include "Misc/UObjectToken.h"

#include "RenderCore.h"
#include "VirtualTexturing.h"
#include "VT/RuntimeVirtualTexture.h"

TLinkedList<FMaterialUniformExpressionType*>*& FMaterialUniformExpressionType::GetTypeList()
{
	static TLinkedList<FMaterialUniformExpressionType*>* TypeList = NULL;
	return TypeList;
}

TMap<FName,FMaterialUniformExpressionType*>& FMaterialUniformExpressionType::GetTypeMap()
{
	static TMap<FName,FMaterialUniformExpressionType*> TypeMap;

	// Move types from the type list to the type map.
	TLinkedList<FMaterialUniformExpressionType*>* TypeListLink = GetTypeList();
	while(TypeListLink)
	{
		TLinkedList<FMaterialUniformExpressionType*>* NextLink = TypeListLink->Next();
		FMaterialUniformExpressionType* Type = **TypeListLink;

		TypeMap.Add(FName(Type->Name),Type);
		TypeListLink->Unlink();
		delete TypeListLink;

		TypeListLink = NextLink;
	}

	return TypeMap;
}

static FGuid GetExternalTextureGuid(const FMaterialRenderContext& Context, const FGuid& ExternalTextureGuid, const FName& ParameterName, int32 SourceTextureIndex)
{
	FGuid GuidToLookup;
	if (ExternalTextureGuid.IsValid())
	{
		// Use the compile-time GUID if it is set
		GuidToLookup = ExternalTextureGuid;
	}
	else
	{
		const UTexture* TextureParameterObject = nullptr;
		if (!ParameterName.IsNone() && Context.MaterialRenderProxy && Context.MaterialRenderProxy->GetTextureValue(ParameterName, &TextureParameterObject, Context) && TextureParameterObject)
		{
			GuidToLookup = TextureParameterObject->GetExternalTextureGuid();
		}
		else
		{
			// Otherwise attempt to use the texture index in the material, if it's valid
			const UTexture* TextureObject = SourceTextureIndex != INDEX_NONE ? GetIndexedTexture<UTexture>(Context.Material, SourceTextureIndex) : nullptr;
			if (TextureObject)
			{
				GuidToLookup = TextureObject->GetExternalTextureGuid();
			}
		}
	}
	return GuidToLookup;
}

static void GetTextureParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, int32 TextureIndex, const FMaterialRenderContext& Context, const UTexture*& OutValue)
{
	if (ParameterInfo.Name.IsNone())
	{
		OutValue = GetIndexedTexture<UTexture>(Context.Material, TextureIndex);
	}
	else if (!Context.MaterialRenderProxy || !Context.MaterialRenderProxy->GetTextureValue(ParameterInfo, &OutValue, Context))
	{
		UTexture* Value = nullptr;

		if (Context.Material.HasMaterialLayers())
		{
			UMaterialInterface* Interface = Context.Material.GetMaterialInterface();
			if (!Interface || !Interface->GetTextureParameterDefaultValue(ParameterInfo, Value))
			{
				Value = GetIndexedTexture<UTexture>(Context.Material, TextureIndex);
			}
		}
		else
		{
			Value = GetIndexedTexture<UTexture>(Context.Material, TextureIndex);
		}

		OutValue = Value;
	}
}

static void GetTextureParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, int32 TextureIndex, const FMaterialRenderContext& Context, const URuntimeVirtualTexture*& OutValue)
{
	if (ParameterInfo.Name.IsNone())
	{
		OutValue = GetIndexedTexture<URuntimeVirtualTexture>(Context.Material, TextureIndex);
	}
	else if (!Context.MaterialRenderProxy || !Context.MaterialRenderProxy->GetTextureValue(ParameterInfo, &OutValue, Context))
	{
		URuntimeVirtualTexture* Value = nullptr;

		if (Context.Material.HasMaterialLayers())
		{
			UMaterialInterface* Interface = Context.Material.GetMaterialInterface();
			if (!Interface || !Interface->GetRuntimeVirtualTextureParameterDefaultValue(ParameterInfo, Value))
			{
				Value = GetIndexedTexture<URuntimeVirtualTexture>(Context.Material, TextureIndex);
			}
		}
		else
		{
			Value = GetIndexedTexture<URuntimeVirtualTexture>(Context.Material, TextureIndex);
		}

		OutValue = Value;
	}
}

FMaterialUniformExpressionType::FMaterialUniformExpressionType(const TCHAR* InName)
	: Name(InName)
{
	// Put the type in the type list until the name subsystem/type map are initialized.
	(new TLinkedList<FMaterialUniformExpressionType*>(this))->LinkHead(GetTypeList());
}

void FMaterialUniformExpression::WriteNumberOpcodes(FMaterialPreshaderData& OutData) const
{
	UE_LOG(LogMaterial, Warning, TEXT("Missing WriteNumberOpcodes impl for %s"), GetType()->GetName());
	OutData.WriteOpcode(EMaterialPreshaderOpcode::ConstantZero);
}

void FUniformParameterOverrides::SetScalarOverride(const FHashedMaterialParameterInfo& ParameterInfo, float Value, bool bOverride)
{
	if (bOverride)
	{
		ScalarOverrides.FindOrAdd(ParameterInfo) = Value;
	}
	else
	{
		ScalarOverrides.Remove(ParameterInfo);
	}
}

void FUniformParameterOverrides::SetVectorOverride(const FHashedMaterialParameterInfo& ParameterInfo, const FLinearColor& Value, bool bOverride)
{
	if (bOverride)
	{
		VectorOverrides.FindOrAdd(ParameterInfo) = Value;
	}
	else
	{
		VectorOverrides.Remove(ParameterInfo);
	}
}

bool FUniformParameterOverrides::GetScalarOverride(const FHashedMaterialParameterInfo& ParameterInfo, float& OutValue) const
{
	const float* Result = ScalarOverrides.Find(ParameterInfo);
	if (Result)
	{
		OutValue = *Result;
		return true;
	}
	return false;
}

bool FUniformParameterOverrides::GetVectorOverride(const FHashedMaterialParameterInfo& ParameterInfo, FLinearColor& OutValue) const
{
	const FLinearColor* Result = VectorOverrides.Find(ParameterInfo);
	if (Result)
	{
		OutValue = *Result;
		return true;
	}
	return false;
}

void FUniformParameterOverrides::SetTextureOverride(EMaterialTextureParameterType Type, const FHashedMaterialParameterInfo& ParameterInfo, UTexture* Texture)
{
	check(IsInGameThread());
	const uint32 TypeIndex = (uint32)Type;
	if (Texture)
	{
		GameThreadTextureOverides[TypeIndex].FindOrAdd(ParameterInfo) = Texture;
	}
	else
	{
		GameThreadTextureOverides[TypeIndex].Remove(ParameterInfo);
	}

	FUniformParameterOverrides* Self = this;
	ENQUEUE_RENDER_COMMAND(SetTextureOverrideCommand)(
		[Self, TypeIndex, ParameterInfo, Texture](FRHICommandListImmediate& RHICmdList)
	{
		if (Texture)
		{
			Self->RenderThreadTextureOverrides[TypeIndex].FindOrAdd(ParameterInfo) = Texture;
		}
		else
		{
			Self->RenderThreadTextureOverrides[TypeIndex].Remove(ParameterInfo);
		}
	});
}

UTexture* FUniformParameterOverrides::GetTextureOverride_GameThread(EMaterialTextureParameterType Type, const FHashedMaterialParameterInfo& ParameterInfo) const
{
	check(IsInGameThread());
	const uint32 TypeIndex = (uint32)Type;
	UTexture* const* Result = GameThreadTextureOverides[TypeIndex].Find(ParameterInfo);
	return Result ? *Result : nullptr;
}

UTexture* FUniformParameterOverrides::GetTextureOverride_RenderThread(EMaterialTextureParameterType Type, const FHashedMaterialParameterInfo& ParameterInfo) const
{
	check(IsInParallelRenderingThread());
	const uint32 TypeIndex = (uint32)Type;
	UTexture* const* Result = RenderThreadTextureOverrides[TypeIndex].Find(ParameterInfo);
	return Result ? *Result : nullptr;
}

bool FUniformExpressionSet::IsEmpty() const
{
	for (uint32 TypeIndex = 0u; TypeIndex < NumMaterialTextureParameterTypes; ++TypeIndex)
	{
		if (UniformTextureParameters[TypeIndex].Num() != 0)
		{
			return false;
		}
	}

	return UniformVectorParameters.Num() == 0
		&& UniformScalarParameters.Num() == 0
		&& UniformVectorPreshaders.Num() == 0
		&& UniformScalarPreshaders.Num() == 0
		&& UniformExternalTextureParameters.Num() == 0
		&& VTStacks.Num() == 0
		&& ParameterCollections.Num() == 0;
}

bool FUniformExpressionSet::operator==(const FUniformExpressionSet& ReferenceSet) const
{
	for (uint32 TypeIndex = 0u; TypeIndex < NumMaterialTextureParameterTypes; ++TypeIndex)
	{
		if (UniformTextureParameters[TypeIndex].Num() != ReferenceSet.UniformTextureParameters[TypeIndex].Num())
		{
			return false;
		}
	}

	if (UniformScalarParameters.Num() != ReferenceSet.UniformScalarParameters.Num()
		|| UniformVectorParameters.Num() != ReferenceSet.UniformVectorParameters.Num()
		|| UniformScalarPreshaders.Num() != ReferenceSet.UniformScalarPreshaders.Num()
		|| UniformVectorPreshaders.Num() != ReferenceSet.UniformVectorPreshaders.Num()
		|| UniformExternalTextureParameters.Num() != ReferenceSet.UniformExternalTextureParameters.Num()
		|| VTStacks.Num() != ReferenceSet.VTStacks.Num()
		|| ParameterCollections.Num() != ReferenceSet.ParameterCollections.Num())
	{
		return false;
	}

	for (int32 i = 0; i < UniformScalarParameters.Num(); i++)
	{
		if (UniformScalarParameters[i] != ReferenceSet.UniformScalarParameters[i])
		{
			return false;
		}
	}

	for (int32 i = 0; i < UniformVectorParameters.Num(); i++)
	{
		if (UniformVectorParameters[i] != ReferenceSet.UniformVectorParameters[i])
		{
			return false;
		}
	}

	for (int32 i = 0; i < UniformScalarPreshaders.Num(); i++)
	{
		if (UniformScalarPreshaders[i] != ReferenceSet.UniformScalarPreshaders[i])
		{
			return false;
		}
	}

	for (int32 i = 0; i < UniformVectorPreshaders.Num(); i++)
	{
		if (UniformVectorPreshaders[i] != ReferenceSet.UniformVectorPreshaders[i])
		{
			return false;
		}
	}

	for (uint32 TypeIndex = 0u; TypeIndex < NumMaterialTextureParameterTypes; ++TypeIndex)
	{
		for (int32 i = 0; i < UniformTextureParameters[TypeIndex].Num(); i++)
		{
			if (UniformTextureParameters[TypeIndex][i] != ReferenceSet.UniformTextureParameters[TypeIndex][i])
			{
				return false;
			}
		}
	}

	for (int32 i = 0; i < UniformExternalTextureParameters.Num(); i++)
	{
		if (UniformExternalTextureParameters[i] != ReferenceSet.UniformExternalTextureParameters[i])
		{
			return false;
		}
	}

	for (int32 i = 0; i < VTStacks.Num(); i++)
	{
		if (VTStacks[i] != ReferenceSet.VTStacks[i])
		{
			return false;
		}
	}

	for (int32 i = 0; i < ParameterCollections.Num(); i++)
	{
		if (ParameterCollections[i] != ReferenceSet.ParameterCollections[i])
		{
			return false;
		}
	}

	if (UniformPreshaderData != ReferenceSet.UniformPreshaderData)
	{
		return false;
	}

	return true;
}

FString FUniformExpressionSet::GetSummaryString() const
{
	return FString::Printf(TEXT("(%u vectors, %u scalars, %u 2d tex, %u cube tex, %u 2darray tex, %u 3d tex, %u virtual tex, %u external tex, %u VT stacks, %u collections)"),
		UniformVectorPreshaders.Num(), 
		UniformScalarPreshaders.Num(),
		UniformTextureParameters[(uint32)EMaterialTextureParameterType::Standard2D].Num(),
		UniformTextureParameters[(uint32)EMaterialTextureParameterType::Cube].Num(),
		UniformTextureParameters[(uint32)EMaterialTextureParameterType::Array2D].Num(),
		UniformTextureParameters[(uint32)EMaterialTextureParameterType::Volume].Num(),
		UniformTextureParameters[(uint32)EMaterialTextureParameterType::Virtual].Num(),
		UniformExternalTextureParameters.Num(),
		VTStacks.Num(),
		ParameterCollections.Num()
		);
}

void FUniformExpressionSet::SetParameterCollections(const TArray<UMaterialParameterCollection*>& InCollections)
{
	ParameterCollections.Empty(InCollections.Num());

	for (int32 CollectionIndex = 0; CollectionIndex < InCollections.Num(); CollectionIndex++)
	{
		ParameterCollections.Add(InCollections[CollectionIndex]->StateId);
	}
}

FShaderParametersMetadata* FUniformExpressionSet::CreateBufferStruct()
{
	// Make sure FUniformExpressionSet::CreateDebugLayout() is in sync
	TArray<FShaderParametersMetadata::FMember> Members;
	uint32 NextMemberOffset = 0;

	if (VTStacks.Num())
	{
		// 2x uint4 per VTStack
		new(Members) FShaderParametersMetadata::FMember(TEXT("VTPackedPageTableUniform"), TEXT(""), NextMemberOffset, UBMT_UINT32, EShaderPrecisionModifier::Float, 1, 4, VTStacks.Num() * 2, NULL);
		NextMemberOffset += VTStacks.Num() * sizeof(FUintVector4) * 2;
	}

	const int32 NumVirtualTextures = UniformTextureParameters[(uint32)EMaterialTextureParameterType::Virtual].Num();
	if (NumVirtualTextures > 0)
	{
		// 1x uint4 per Virtual Texture
		new(Members) FShaderParametersMetadata::FMember(TEXT("VTPackedUniform"), TEXT(""), NextMemberOffset, UBMT_UINT32, EShaderPrecisionModifier::Float, 1, 4, NumVirtualTextures, NULL);
		NextMemberOffset += NumVirtualTextures * sizeof(FUintVector4);
	}

	if (UniformVectorPreshaders.Num())
	{
		new(Members) FShaderParametersMetadata::FMember(TEXT("VectorExpressions"),TEXT(""),NextMemberOffset,UBMT_FLOAT32,EShaderPrecisionModifier::Half,1,4, UniformVectorPreshaders.Num(),NULL);
		const uint32 VectorArraySize = UniformVectorPreshaders.Num() * sizeof(FVector4);
		NextMemberOffset += VectorArraySize;
	}

	if (UniformScalarPreshaders.Num())
	{
		new(Members) FShaderParametersMetadata::FMember(TEXT("ScalarExpressions"),TEXT(""),NextMemberOffset,UBMT_FLOAT32,EShaderPrecisionModifier::Half,1,4,(UniformScalarPreshaders.Num() + 3) / 4,NULL);
		const uint32 ScalarArraySize = (UniformScalarPreshaders.Num() + 3) / 4 * sizeof(FVector4);
		NextMemberOffset += ScalarArraySize;
	}

	check((NextMemberOffset % (2 * SHADER_PARAMETER_POINTER_ALIGNMENT)) == 0);

	static FString Texture2DNames[128];
	static FString Texture2DSamplerNames[128];
	static FString TextureCubeNames[128];
	static FString TextureCubeSamplerNames[128];
	static FString Texture2DArrayNames[128];
	static FString Texture2DArraySamplerNames[128];
	static FString VolumeTextureNames[128];
	static FString VolumeTextureSamplerNames[128];
	static FString ExternalTextureNames[128];
	static FString MediaTextureSamplerNames[128];
	static FString VirtualTexturePageTableNames0[128];
	static FString VirtualTexturePageTableNames1[128];
	static FString VirtualTexturePageTableIndirectionNames[128];
	static FString VirtualTexturePhysicalNames[128];
	static FString VirtualTexturePhysicalSamplerNames[128];
	static bool bInitializedTextureNames = false;
	if (!bInitializedTextureNames)
	{
		bInitializedTextureNames = true;
		for (int32 i = 0; i < 128; ++i)
		{
			Texture2DNames[i] = FString::Printf(TEXT("Texture2D_%d"), i);
			Texture2DSamplerNames[i] = FString::Printf(TEXT("Texture2D_%dSampler"), i);
			TextureCubeNames[i] = FString::Printf(TEXT("TextureCube_%d"), i);
			TextureCubeSamplerNames[i] = FString::Printf(TEXT("TextureCube_%dSampler"), i);
			Texture2DArrayNames[i] = FString::Printf(TEXT("Texture2DArray_%d"), i);
			Texture2DArraySamplerNames[i] = FString::Printf(TEXT("Texture2DArray_%dSampler"), i);
			VolumeTextureNames[i] = FString::Printf(TEXT("VolumeTexture_%d"), i);
			VolumeTextureSamplerNames[i] = FString::Printf(TEXT("VolumeTexture_%dSampler"), i);
			ExternalTextureNames[i] = FString::Printf(TEXT("ExternalTexture_%d"), i);
			MediaTextureSamplerNames[i] = FString::Printf(TEXT("ExternalTexture_%dSampler"), i);
			VirtualTexturePageTableNames0[i] = FString::Printf(TEXT("VirtualTexturePageTable0_%d"), i);
			VirtualTexturePageTableNames1[i] = FString::Printf(TEXT("VirtualTexturePageTable1_%d"), i);
			VirtualTexturePageTableIndirectionNames[i] = FString::Printf(TEXT("VirtualTexturePageTableIndirection_%d"), i);
			VirtualTexturePhysicalNames[i] = FString::Printf(TEXT("VirtualTexturePhysical_%d"), i);
			VirtualTexturePhysicalSamplerNames[i] = FString::Printf(TEXT("VirtualTexturePhysical_%dSampler"), i);
		}
	}

	for (uint32 TypeIndex = 0u; TypeIndex < NumMaterialTextureParameterTypes; ++TypeIndex)
	{
		check(UniformTextureParameters[TypeIndex].Num() <= 128);
	}
	check(VTStacks.Num() <= 128);

	for (int32 i = 0; i < UniformTextureParameters[(uint32)EMaterialTextureParameterType::Standard2D].Num(); ++i)
	{
		check((NextMemberOffset % SHADER_PARAMETER_POINTER_ALIGNMENT) == 0);
		new(Members) FShaderParametersMetadata::FMember(*Texture2DNames[i],TEXT("Texture2D"),NextMemberOffset,UBMT_TEXTURE,EShaderPrecisionModifier::Float,1,1,0,NULL);
		NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
		new(Members) FShaderParametersMetadata::FMember(*Texture2DSamplerNames[i],TEXT("SamplerState"),NextMemberOffset,UBMT_SAMPLER,EShaderPrecisionModifier::Float,1,1,0,NULL);
		NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
	}

	for (int32 i = 0; i < UniformTextureParameters[(uint32)EMaterialTextureParameterType::Cube].Num(); ++i)
	{
		check((NextMemberOffset % SHADER_PARAMETER_POINTER_ALIGNMENT) == 0);
		new(Members) FShaderParametersMetadata::FMember(*TextureCubeNames[i],TEXT("TextureCube"),NextMemberOffset,UBMT_TEXTURE,EShaderPrecisionModifier::Float,1,1,0,NULL);
		NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
		new(Members) FShaderParametersMetadata::FMember(*TextureCubeSamplerNames[i],TEXT("SamplerState"),NextMemberOffset,UBMT_SAMPLER,EShaderPrecisionModifier::Float,1,1,0,NULL);
		NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
	}

	for (int32 i = 0; i < UniformTextureParameters[(uint32)EMaterialTextureParameterType::Array2D].Num(); ++i)
	{
		check((NextMemberOffset % SHADER_PARAMETER_POINTER_ALIGNMENT) == 0);
		new(Members) FShaderParametersMetadata::FMember(*Texture2DArrayNames[i], TEXT("Texture2DArray"), NextMemberOffset, UBMT_TEXTURE, EShaderPrecisionModifier::Float, 1, 1, 0, NULL);
		NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
		new(Members) FShaderParametersMetadata::FMember(*Texture2DArraySamplerNames[i], TEXT("SamplerState"), NextMemberOffset, UBMT_SAMPLER, EShaderPrecisionModifier::Float, 1, 1, 0, NULL);
		NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
	}

	for (int32 i = 0; i < UniformTextureParameters[(uint32)EMaterialTextureParameterType::Volume].Num(); ++i)
	{
		check((NextMemberOffset % SHADER_PARAMETER_POINTER_ALIGNMENT) == 0);
		new(Members) FShaderParametersMetadata::FMember(*VolumeTextureNames[i],TEXT("Texture3D"),NextMemberOffset,UBMT_TEXTURE,EShaderPrecisionModifier::Float,1,1,0,NULL);
		NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
		new(Members) FShaderParametersMetadata::FMember(*VolumeTextureSamplerNames[i],TEXT("SamplerState"),NextMemberOffset,UBMT_SAMPLER,EShaderPrecisionModifier::Float,1,1,0,NULL);
		NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
	}

	for (int32 i = 0; i < UniformExternalTextureParameters.Num(); ++i)
	{
		check((NextMemberOffset % SHADER_PARAMETER_POINTER_ALIGNMENT) == 0);
		new(Members) FShaderParametersMetadata::FMember(*ExternalTextureNames[i], TEXT("TextureExternal"), NextMemberOffset, UBMT_TEXTURE, EShaderPrecisionModifier::Float, 1, 1, 0, NULL);
		NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
		new(Members) FShaderParametersMetadata::FMember(*MediaTextureSamplerNames[i], TEXT("SamplerState"), NextMemberOffset, UBMT_SAMPLER, EShaderPrecisionModifier::Float, 1, 1, 0, NULL);
		NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
	}

	for (int32 i = 0; i < VTStacks.Num(); ++i)
	{
		const FMaterialVirtualTextureStack& Stack = VTStacks[i];
		check((NextMemberOffset % SHADER_PARAMETER_POINTER_ALIGNMENT) == 0);
		new(Members) FShaderParametersMetadata::FMember(*VirtualTexturePageTableNames0[i], TEXT("Texture2D<uint4>"), NextMemberOffset, UBMT_TEXTURE, EShaderPrecisionModifier::Float, 1, 1, 0, NULL);
		NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
		if (Stack.GetNumLayers() > 4u)
		{
			new(Members) FShaderParametersMetadata::FMember(*VirtualTexturePageTableNames1[i], TEXT("Texture2D<uint4>"), NextMemberOffset, UBMT_TEXTURE, EShaderPrecisionModifier::Float, 1, 1, 0, NULL);
			NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
		}
		new(Members) FShaderParametersMetadata::FMember(*VirtualTexturePageTableIndirectionNames[i], TEXT("Texture2D<uint>"), NextMemberOffset, UBMT_TEXTURE, EShaderPrecisionModifier::Float, 1, 1, 0, NULL);
		NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
	}

	for (int32 i = 0; i < UniformTextureParameters[(uint32)EMaterialTextureParameterType::Virtual].Num(); ++i)
	{
		check((NextMemberOffset % SHADER_PARAMETER_POINTER_ALIGNMENT) == 0);

		// VT physical textures are bound as SRV, allows aliasing the same underlying texture with both sRGB/non-sRGB views
		new(Members) FShaderParametersMetadata::FMember(*VirtualTexturePhysicalNames[i], TEXT("Texture2D"), NextMemberOffset, UBMT_SRV, EShaderPrecisionModifier::Float, 1, 1, 0, NULL);
		NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
		new(Members) FShaderParametersMetadata::FMember(*VirtualTexturePhysicalSamplerNames[i], TEXT("SamplerState"), NextMemberOffset, UBMT_SAMPLER, EShaderPrecisionModifier::Float, 1, 1, 0, NULL);
		NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;
	}

	new(Members) FShaderParametersMetadata::FMember(TEXT("Wrap_WorldGroupSettings"),TEXT("SamplerState"),NextMemberOffset,UBMT_SAMPLER,EShaderPrecisionModifier::Float,1,1,0,NULL);
	NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;

	new(Members) FShaderParametersMetadata::FMember(TEXT("Clamp_WorldGroupSettings"),TEXT("SamplerState"),NextMemberOffset,UBMT_SAMPLER,EShaderPrecisionModifier::Float,1,1,0,NULL);
	NextMemberOffset += SHADER_PARAMETER_POINTER_ALIGNMENT;

	const uint32 StructSize = Align(NextMemberOffset, SHADER_PARAMETER_STRUCT_ALIGNMENT);
	FShaderParametersMetadata* UniformBufferStruct = new FShaderParametersMetadata(
		FShaderParametersMetadata::EUseCase::DataDrivenUniformBuffer,
		TEXT("Material"),
		TEXT("MaterialUniforms"),
		TEXT("Material"),
		nullptr,
		StructSize,
		Members);

	UniformBufferLayout = UniformBufferStruct->GetLayout();
	return UniformBufferStruct;
}

FUniformExpressionSet::FVTPackedStackAndLayerIndex FUniformExpressionSet::GetVTStackAndLayerIndex(int32 UniformExpressionIndex) const
{
	for (int32 VTStackIndex = 0; VTStackIndex < VTStacks.Num(); ++VTStackIndex)
	{
		const FMaterialVirtualTextureStack& VTStack = VTStacks[VTStackIndex];
		const int32 LayerIndex = VTStack.FindLayer(UniformExpressionIndex);
		if (LayerIndex >= 0)
		{
			return FVTPackedStackAndLayerIndex(VTStackIndex, LayerIndex);
		}
	}

	checkNoEntry();
	return FVTPackedStackAndLayerIndex(0xffff, 0xffff);
}

void FMaterialPreshaderData::WriteData(const void* Value, uint32 Size)
{
	Data.Append((uint8*)Value, Size);
}

void FMaterialPreshaderData::WriteName(const FScriptName& Name)
{
	int32 Index = Names.Find(Name);
	if (Index == INDEX_NONE)
	{
		Index = Names.Add(Name);
	}
	check(Index >= 0 && Index <= 0xffff);
	Write((uint16)Index);
}

namespace
{
	struct FPreshaderDataContext
	{
		explicit FPreshaderDataContext(const FMaterialPreshaderData& InData)
			: Ptr(InData.Data.GetData())
			, EndPtr(Ptr + InData.Data.Num())
			, Names(InData.Names.GetData())
			, NumNames(InData.Names.Num())
		{}

		explicit FPreshaderDataContext(const FPreshaderDataContext& InContext, const FMaterialUniformPreshaderHeader& InHeader)
			: Ptr(InContext.Ptr + InHeader.OpcodeOffset)
			, EndPtr(Ptr + InHeader.OpcodeSize)
			, Names(InContext.Names)
			, NumNames(InContext.NumNames)
		{}

		const uint8* RESTRICT Ptr;
		const uint8* RESTRICT EndPtr;
		const FScriptName* RESTRICT Names;
		int32 NumNames;
	};

	template<typename T>
	inline T ReadPreshaderValue(FPreshaderDataContext& RESTRICT Data)
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
}

static void GetVectorParameter(const FUniformExpressionSet& UniformExpressionSet, uint32 ParameterIndex, const FMaterialRenderContext& Context, FLinearColor& OutValue)
{
	const FMaterialVectorParameterInfo& Parameter = UniformExpressionSet.GetVectorParameter(ParameterIndex);

	OutValue.R = OutValue.G = OutValue.B = OutValue.A = 0;
	bool bNeedsDefaultValue = false;
	if (!Context.MaterialRenderProxy || !Context.MaterialRenderProxy->GetVectorValue(Parameter.ParameterInfo, &OutValue, Context))
	{
		const bool bOveriddenParameterOnly = Parameter.ParameterInfo.Association == EMaterialParameterAssociation::GlobalParameter;

		if (Context.Material.HasMaterialLayers())
		{
			UMaterialInterface* Interface = Context.Material.GetMaterialInterface();
			if (!Interface || !Interface->GetVectorParameterDefaultValue(Parameter.ParameterInfo, OutValue, bOveriddenParameterOnly))
			{
				bNeedsDefaultValue = true;
			}
		}
		else
		{
			bNeedsDefaultValue = true;
		}
	}

	if (bNeedsDefaultValue)
	{
#if WITH_EDITOR
		if (!Context.Material.TransientOverrides.GetVectorOverride(Parameter.ParameterInfo, OutValue))
#endif // WITH_EDITOR
		{
			Parameter.GetDefaultValue(OutValue);
		}
	}
}

static void GetScalarParameter(const FUniformExpressionSet& UniformExpressionSet, uint32 ParameterIndex, const FMaterialRenderContext& Context, FLinearColor& OutValue)
{
	const FMaterialScalarParameterInfo& Parameter = UniformExpressionSet.GetScalarParameter(ParameterIndex);

	OutValue.A = 0;

	bool bNeedsDefaultValue = false;
	if (!Context.MaterialRenderProxy || !Context.MaterialRenderProxy->GetScalarValue(Parameter.ParameterInfo, &OutValue.A, Context))
	{
		const bool bOveriddenParameterOnly = Parameter.ParameterInfo.Association == EMaterialParameterAssociation::GlobalParameter;

		if (Context.Material.HasMaterialLayers())
		{
			UMaterialInterface* Interface = Context.Material.GetMaterialInterface();
			if (!Interface || !Interface->GetScalarParameterDefaultValue(Parameter.ParameterInfo, OutValue.A, bOveriddenParameterOnly))
			{
				bNeedsDefaultValue = true;
			}
		}
		else
		{
			bNeedsDefaultValue = true;
		}
	}

	if (bNeedsDefaultValue)
	{
#if WITH_EDITOR
		if (!Context.Material.TransientOverrides.GetScalarOverride(Parameter.ParameterInfo, OutValue.A))
#endif // WITH_EDITOR
		{
			Parameter.GetDefaultValue(OutValue.A);
		}
	}

	OutValue.R = OutValue.G = OutValue.B = OutValue.A;
}

using FPreshaderStack = TArray<FLinearColor, TInlineAllocator<64u>>;

template<typename Operation>
static inline void EvaluateUnaryOp(FPreshaderStack& Stack, const Operation& Op)
{
	const FLinearColor Value = Stack.Pop(false);
	Stack.Add(FLinearColor(Op(Value.R), Op(Value.G), Op(Value.B), Op(Value.A)));
}

template<typename Operation>
static inline void EvaluateBinaryOp(FPreshaderStack& Stack, const Operation& Op)
{
	const FLinearColor Value1 = Stack.Pop(false);
	const FLinearColor Value0 = Stack.Pop(false);
	Stack.Add(FLinearColor(Op(Value0.R, Value1.R), Op(Value0.G, Value1.G), Op(Value0.B, Value1.B), Op(Value0.A, Value1.A)));
}

template<typename Operation>
static inline void EvaluateTernaryOp(FPreshaderStack& Stack, const Operation& Op)
{
	const FLinearColor Value2 = Stack.Pop(false);
	const FLinearColor Value1 = Stack.Pop(false);
	const FLinearColor Value0 = Stack.Pop(false);
	Stack.Add(FLinearColor(Op(Value0.R, Value1.R, Value2.R), Op(Value0.G, Value1.G, Value2.G), Op(Value0.B, Value1.B, Value2.B), Op(Value0.A, Value1.A, Value2.A)));
}

static void EvaluateDot(FPreshaderStack& Stack, FPreshaderDataContext& RESTRICT Data)
{
	const uint8 ValueType = ReadPreshaderValue<uint8>(Data);
	const FLinearColor Value1 = Stack.Pop(false);
	const FLinearColor Value0 = Stack.Pop(false);
	float Result = Value0.R * Value1.R;
	Result += (ValueType >= MCT_Float2) ? Value0.G * Value1.G : 0;
	Result += (ValueType >= MCT_Float3) ? Value0.B * Value1.B : 0;
	Result += (ValueType >= MCT_Float4) ? Value0.A * Value1.A : 0;
	Stack.Add(FLinearColor(Result, Result, Result, Result));
}

static void EvaluateCross(FPreshaderStack& Stack, FPreshaderDataContext& RESTRICT Data)
{
	const uint8 ValueType = ReadPreshaderValue<uint8>(Data);
	FLinearColor ValueB = Stack.Pop(false);
	FLinearColor ValueA = Stack.Pop(false);
	
	// Must be Float3, replicate CoerceParameter behavior
	switch (ValueType)
	{
	case MCT_Float:
		ValueA.B = ValueA.G = ValueA.R;
		ValueB.B = ValueB.G = ValueB.R;
		break;
	case MCT_Float1:
		ValueA.B = ValueA.G = 0.f;
		ValueB.B = ValueB.G = 0.f;
		break;
	case MCT_Float2:
		ValueA.B = 0.f;
		ValueB.B = 0.f;
		break;
	};

	const FVector Cross = FVector::CrossProduct(FVector(ValueA), FVector(ValueB));
	Stack.Add(FLinearColor(Cross.X, Cross.Y, Cross.Z, 0.0f));
}

static void EvaluateComponentSwizzle(FPreshaderStack& Stack, FPreshaderDataContext& RESTRICT Data)
{
	const uint8 NumElements = ReadPreshaderValue<uint8>(Data);
	const uint8 IndexR = ReadPreshaderValue<uint8>(Data);
	const uint8 IndexG = ReadPreshaderValue<uint8>(Data);
	const uint8 IndexB = ReadPreshaderValue<uint8>(Data);
	const uint8 IndexA = ReadPreshaderValue<uint8>(Data);

	FLinearColor Value = Stack.Pop(false);
	FLinearColor Result(0.0f, 0.0f, 0.0f, 0.0f);
	switch (NumElements)
	{
	case 1:
		// Replicate scalar
		Result.R = Result.G = Result.B = Result.A = Value.Component(IndexR);
		break;
	case 4:
		Result.A = Value.Component(IndexA);
		// Fallthrough...
	case 3:
		Result.B = Value.Component(IndexB);
		// Fallthrough...
	case 2:
		Result.G = Value.Component(IndexG);
		Result.R = Value.Component(IndexR);
		break;
	default:
		UE_LOG(LogMaterial, Fatal, TEXT("Invalid number of swizzle elements: %d"), NumElements);
		break;
	}
	Stack.Add(Result);
}

static void EvaluateAppenedVector(FPreshaderStack& Stack, FPreshaderDataContext& RESTRICT Data)
{
	const uint8 NumComponentsA = ReadPreshaderValue<uint8>(Data);

	const FLinearColor ValueB = Stack.Pop(false);
	const FLinearColor ValueA = Stack.Pop(false);

	FLinearColor Result;
	Result.R = NumComponentsA >= 1 ? ValueA.R : (&ValueB.R)[0 - NumComponentsA];
	Result.G = NumComponentsA >= 2 ? ValueA.G : (&ValueB.R)[1 - NumComponentsA];
	Result.B = NumComponentsA >= 3 ? ValueA.B : (&ValueB.R)[2 - NumComponentsA];
	Result.A = NumComponentsA >= 4 ? ValueA.A : (&ValueB.R)[3 - NumComponentsA];
	Stack.Add(Result);
}

static const UTexture* GetTextureParameter(const FMaterialRenderContext& Context, FPreshaderDataContext& RESTRICT Data)
{
	const FHashedMaterialParameterInfo ParameterInfo = ReadPreshaderValue<FHashedMaterialParameterInfo>(Data);
	const int32 TextureIndex = ReadPreshaderValue<int32>(Data);
	
	const UTexture* Texture = nullptr;
	GetTextureParameterValue(ParameterInfo, TextureIndex, Context, Texture);
	return Texture;
}

static void EvaluateTextureSize(const FMaterialRenderContext& Context, FPreshaderStack& Stack, FPreshaderDataContext& RESTRICT Data)
{
	const UTexture* Texture = GetTextureParameter(Context, Data);
	if (Texture && Texture->Resource)
	{
		const uint32 SizeX = Texture->Resource->GetSizeX();
		const uint32 SizeY = Texture->Resource->GetSizeY();
		const uint32 SizeZ = Texture->Resource->GetSizeZ();
		Stack.Add(FLinearColor((float)SizeX, (float)SizeY, (float)SizeZ, 0.0f));
	}
	else
	{
		Stack.Add(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	}
}

static void EvaluateTexelSize(const FMaterialRenderContext& Context, FPreshaderStack& Stack, FPreshaderDataContext& RESTRICT Data)
{
	const UTexture* Texture = GetTextureParameter(Context, Data);
	if (Texture && Texture->Resource)
	{
		const uint32 SizeX = Texture->Resource->GetSizeX();
		const uint32 SizeY = Texture->Resource->GetSizeY();
		const uint32 SizeZ = Texture->Resource->GetSizeZ();
		Stack.Add(FLinearColor(1.0f / (float)SizeX, 1.0f / (float)SizeY, (SizeZ > 0 ? 1.0f / (float)SizeZ : 0.0f), 0.0f));
	}
	else
	{
		Stack.Add(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	}
}

static FGuid GetExternalTextureGuid(const FMaterialRenderContext& Context, FPreshaderDataContext& RESTRICT Data)
{
	const FScriptName ParameterName = ReadPreshaderValue<FScriptName>(Data);
	const FGuid ExternalTextureGuid = ReadPreshaderValue<FGuid>(Data);
	const int32 TextureIndex = ReadPreshaderValue<int32>(Data);
	return GetExternalTextureGuid(Context, ExternalTextureGuid, ScriptNameToName(ParameterName), TextureIndex);
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
		Stack.Add(FLinearColor(Texture->GetUniformParameter(VectorIndex)));
	}
	else
	{
		Stack.Add(FLinearColor(0.f, 0.f, 0.f, 0.f));
	}
}

/** Converts an arbitrary number into a safe divisor. i.e. FMath::Abs(Number) >= DELTA */
static float GetSafeDivisor(float Number)
{
	if (FMath::Abs(Number) < DELTA)
	{
		if (Number < 0.0f)
		{
			return -DELTA;
		}
		else
		{
			return +DELTA;
		}
	}
	else
	{
		return Number;
	}
}

/**
 * FORCENOINLINE is required to discourage compiler from vectorizing the Div operation, which may tempt it into optimizing divide as A * rcp(B)
 * This will break shaders that are depending on exact divide results (see SubUV material function)
 * Technically this could still happen for a scalar divide, but it doesn't seem to occur in practice
 */
FORCENOINLINE static float DivideComponent(float A, float B)
{
	return A / GetSafeDivisor(B);
}

static void EvaluatePreshader(const FUniformExpressionSet* UniformExpressionSet, const FMaterialRenderContext& Context, FPreshaderStack& Stack, FPreshaderDataContext& RESTRICT Data, FLinearColor& OutValue)
{
	static const float LogToLog10 = 1.0f / FMath::Loge(10.f);
	uint8 const* const DataEnd = Data.EndPtr;

	Stack.Reset();
	while (Data.Ptr < DataEnd)
	{
		const EMaterialPreshaderOpcode Opcode = (EMaterialPreshaderOpcode)ReadPreshaderValue<uint8>(Data);
		switch (Opcode)
		{
		case EMaterialPreshaderOpcode::ConstantZero:
			Stack.Add(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
			break;
		case EMaterialPreshaderOpcode::Constant:
			Stack.Add(ReadPreshaderValue<FLinearColor>(Data));
			break;
		case EMaterialPreshaderOpcode::VectorParameter:
			check(UniformExpressionSet);
			GetVectorParameter(*UniformExpressionSet, ReadPreshaderValue<uint16>(Data), Context, Stack.AddDefaulted_GetRef());
			break;
		case EMaterialPreshaderOpcode::ScalarParameter:
			check(UniformExpressionSet);
			GetScalarParameter(*UniformExpressionSet, ReadPreshaderValue<uint16>(Data), Context, Stack.AddDefaulted_GetRef());
			break;
		case EMaterialPreshaderOpcode::Add: EvaluateBinaryOp(Stack, [](float Lhs, float Rhs) { return Lhs + Rhs; }); break;
		case EMaterialPreshaderOpcode::Sub: EvaluateBinaryOp(Stack, [](float Lhs, float Rhs) { return Lhs - Rhs; }); break;
		case EMaterialPreshaderOpcode::Mul: EvaluateBinaryOp(Stack, [](float Lhs, float Rhs) { return Lhs * Rhs; }); break;
		case EMaterialPreshaderOpcode::Div: EvaluateBinaryOp(Stack, [](float Lhs, float Rhs) { return DivideComponent(Lhs, Rhs); }); break;
		case EMaterialPreshaderOpcode::Fmod: EvaluateBinaryOp(Stack, [](float Lhs, float Rhs) { return FMath::Fmod(Lhs, Rhs); }); break;
		case EMaterialPreshaderOpcode::Min: EvaluateBinaryOp(Stack, [](float Lhs, float Rhs) { return FMath::Min(Lhs, Rhs); }); break;
		case EMaterialPreshaderOpcode::Max: EvaluateBinaryOp(Stack, [](float Lhs, float Rhs) { return FMath::Max(Lhs, Rhs); }); break;
		case EMaterialPreshaderOpcode::Clamp: EvaluateTernaryOp(Stack, [](float A, float B, float C) { return FMath::Clamp(A, B, C); }); break;
		case EMaterialPreshaderOpcode::Dot: EvaluateDot(Stack, Data); break;
		case EMaterialPreshaderOpcode::Cross: EvaluateCross(Stack, Data); break;
		case EMaterialPreshaderOpcode::Sqrt: EvaluateUnaryOp(Stack, [](float V) { return FMath::Sqrt(V); }); break;
		case EMaterialPreshaderOpcode::Sin: EvaluateUnaryOp(Stack, [](float V) { return FMath::Sin(V); }); break;
		case EMaterialPreshaderOpcode::Cos: EvaluateUnaryOp(Stack, [](float V) { return FMath::Cos(V); }); break;
		case EMaterialPreshaderOpcode::Tan: EvaluateUnaryOp(Stack, [](float V) { return FMath::Tan(V); }); break;
		case EMaterialPreshaderOpcode::Asin: EvaluateUnaryOp(Stack, [](float V) { return FMath::Asin(V); }); break;
		case EMaterialPreshaderOpcode::Acos: EvaluateUnaryOp(Stack, [](float V) { return FMath::Acos(V); }); break;
		case EMaterialPreshaderOpcode::Atan: EvaluateUnaryOp(Stack, [](float V) { return FMath::Atan(V); }); break;
		case EMaterialPreshaderOpcode::Atan2: EvaluateBinaryOp(Stack, [](float A, float B) { return FMath::Atan2(A, B); }); break;
		case EMaterialPreshaderOpcode::Abs: EvaluateUnaryOp(Stack, [](float V) { return FMath::Abs(V); }); break;
		case EMaterialPreshaderOpcode::Saturate: EvaluateUnaryOp(Stack, [](float V) { return FMath::Clamp(V, 0.0f, 1.0f); }); break;
		case EMaterialPreshaderOpcode::Floor: EvaluateUnaryOp(Stack, [](float V) { return FMath::FloorToFloat(V); }); break;
		case EMaterialPreshaderOpcode::Ceil: EvaluateUnaryOp(Stack, [](float V) { return FMath::CeilToFloat(V); }); break;
		case EMaterialPreshaderOpcode::Round: EvaluateUnaryOp(Stack, [](float V) { return FMath::RoundToFloat(V); }); break;
		case EMaterialPreshaderOpcode::Trunc: EvaluateUnaryOp(Stack, [](float V) { return FMath::TruncToFloat(V); }); break;
		case EMaterialPreshaderOpcode::Sign: EvaluateUnaryOp(Stack, [](float V) { return FMath::Sign(V); }); break;
		case EMaterialPreshaderOpcode::Frac: EvaluateUnaryOp(Stack, [](float V) { return FMath::Frac(V); }); break;
		case EMaterialPreshaderOpcode::Fractional: EvaluateUnaryOp(Stack, [](float V) { return FMath::Fractional(V); }); break;
		case EMaterialPreshaderOpcode::Log2: EvaluateUnaryOp(Stack, [](float V) { return FMath::Log2(V); }); break;
		case EMaterialPreshaderOpcode::Log10: EvaluateUnaryOp(Stack, [](float V) { return FMath::Loge(V) * LogToLog10; }); break;
		case EMaterialPreshaderOpcode::ComponentSwizzle: EvaluateComponentSwizzle(Stack, Data); break;
		case EMaterialPreshaderOpcode::AppendVector: EvaluateAppenedVector(Stack, Data); break;
		case EMaterialPreshaderOpcode::TextureSize: EvaluateTextureSize(Context, Stack, Data); break;
		case EMaterialPreshaderOpcode::TexelSize: EvaluateTexelSize(Context, Stack, Data); break;
		case EMaterialPreshaderOpcode::ExternalTextureCoordinateScaleRotation: EvaluateExternalTextureCoordinateScaleRotation(Context, Stack, Data); break;
		case EMaterialPreshaderOpcode::ExternalTextureCoordinateOffset: EvaluateExternalTextureCoordinateOffset(Context, Stack, Data); break;
		case EMaterialPreshaderOpcode::RuntimeVirtualTextureUniform: EvaluateRuntimeVirtualTextureUniform(Context, Stack, Data); break;
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

void FMaterialUniformExpression::GetNumberValue(const struct FMaterialRenderContext& Context, FLinearColor& OutValue) const
{
	FMaterialPreshaderData PreshaderData;
	WriteNumberOpcodes(PreshaderData);

	FPreshaderStack Stack;
	FPreshaderDataContext PreshaderContext(PreshaderData);
	EvaluatePreshader(nullptr, Context, Stack, PreshaderContext, OutValue);
}

const FMaterialVectorParameterInfo* FUniformExpressionSet::FindVectorParameter(const FHashedMaterialParameterInfo& ParameterInfo) const
{
	for (const FMaterialVectorParameterInfo& Parameter : UniformVectorParameters)
	{
		if (Parameter.ParameterInfo == ParameterInfo)
		{
			return &Parameter;
		}
	}
	return nullptr;
}

const FMaterialScalarParameterInfo* FUniformExpressionSet::FindScalarParameter(const FHashedMaterialParameterInfo& ParameterInfo) const
{
	for (const FMaterialScalarParameterInfo& Parameter : UniformScalarParameters)
	{
		if (Parameter.ParameterInfo == ParameterInfo)
		{
			return &Parameter;
		}
	}
	return nullptr;
}

void FUniformExpressionSet::GetGameThreadTextureValue(EMaterialTextureParameterType Type, int32 Index, const UMaterialInterface* MaterialInterface, const FMaterial& Material, UTexture*& OutValue, bool bAllowOverride) const
{
	check(IsInGameThread());
	OutValue = NULL;
	const FMaterialTextureParameterInfo& Parameter = GetTextureParameter(Type, Index);
#if WITH_EDITOR
	if (bAllowOverride)
	{
		UTexture* OverrideTexture = Material.TransientOverrides.GetTextureOverride_GameThread(Type, Parameter.ParameterInfo);
		if (OverrideTexture)
		{
			OutValue = OverrideTexture;
			return;
		}
	}
#endif // WITH_EDITOR
	Parameter.GetGameThreadTextureValue(MaterialInterface, Material, OutValue);
}

void FUniformExpressionSet::GetTextureValue(EMaterialTextureParameterType Type, int32 Index, const FMaterialRenderContext& Context, const FMaterial& Material, const UTexture*& OutValue) const
{
	check(IsInParallelRenderingThread());
	const FMaterialTextureParameterInfo& Parameter = GetTextureParameter(Type, Index);
#if WITH_EDITOR
	{
		UTexture* OverrideTexture = Material.TransientOverrides.GetTextureOverride_RenderThread(Type, Parameter.ParameterInfo);
		if (OverrideTexture)
		{
			OutValue = OverrideTexture;
			return;
		}
	}
#endif // WITH_EDITOR
	GetTextureParameterValue(Parameter.ParameterInfo, Parameter.TextureIndex, Context, OutValue);
}

void FUniformExpressionSet::GetTextureValue(int32 Index, const FMaterialRenderContext& Context, const FMaterial& Material, const URuntimeVirtualTexture*& OutValue) const
{
	check(IsInParallelRenderingThread());
	const int32 VirtualTexturesNum = GetNumTextures(EMaterialTextureParameterType::Virtual);
	if (ensure(Index < VirtualTexturesNum))
	{
		const FMaterialTextureParameterInfo& Parameter = GetTextureParameter(EMaterialTextureParameterType::Virtual, Index);
		GetTextureParameterValue(Parameter.ParameterInfo, Parameter.TextureIndex, Context, OutValue);
	}
	else
	{
		OutValue = nullptr;
	}
}

void FUniformExpressionSet::FillUniformBuffer(const FMaterialRenderContext& MaterialRenderContext, const FUniformExpressionCache& UniformExpressionCache, uint8* TempBuffer, int TempBufferSize) const
{
	check(IsInParallelRenderingThread());

	if (UniformBufferLayout.ConstantBufferSize > 0)
	{
		// stat disabled by default due to low-value/high-frequency
		//QUICK_SCOPE_CYCLE_COUNTER(STAT_FUniformExpressionSet_FillUniformBuffer);

		void* BufferCursor = TempBuffer;
		check(BufferCursor <= TempBuffer + TempBufferSize);

		// Dump virtual texture per page table uniform data
		check(UniformExpressionCache.AllocatedVTs.Num() == VTStacks.Num());
		for ( int32 VTStackIndex = 0; VTStackIndex < VTStacks.Num(); ++VTStackIndex)
		{
			const IAllocatedVirtualTexture* AllocatedVT = UniformExpressionCache.AllocatedVTs[VTStackIndex];
			FUintVector4* VTPackedPageTableUniform = (FUintVector4*)BufferCursor;
			if (AllocatedVT)
			{
				AllocatedVT->GetPackedPageTableUniform(VTPackedPageTableUniform);
			}
			else
			{
				VTPackedPageTableUniform[0] = FUintVector4(ForceInitToZero);
				VTPackedPageTableUniform[1] = FUintVector4(ForceInitToZero);
			}
			BufferCursor = VTPackedPageTableUniform + 2;
		}
		
		// Dump virtual texture per physical texture uniform data
		for (int32 ExpressionIndex = 0; ExpressionIndex < GetNumTextures(EMaterialTextureParameterType::Virtual); ++ExpressionIndex)
		{
			const FMaterialTextureParameterInfo& Parameter = GetTextureParameter(EMaterialTextureParameterType::Virtual, ExpressionIndex);

			FUintVector4* VTPackedUniform = (FUintVector4*)BufferCursor;
			BufferCursor = VTPackedUniform + 1;

			bool bFoundTexture = false;

			// Check for streaming virtual texture
			if (!bFoundTexture)
			{
				const UTexture* Texture = nullptr;
				GetTextureValue(EMaterialTextureParameterType::Virtual, ExpressionIndex, MaterialRenderContext, MaterialRenderContext.Material, Texture);
				if (Texture != nullptr)
				{
					const FVTPackedStackAndLayerIndex StackAndLayerIndex = GetVTStackAndLayerIndex(ExpressionIndex);
					const IAllocatedVirtualTexture* AllocatedVT = UniformExpressionCache.AllocatedVTs[StackAndLayerIndex.StackIndex];
					if (AllocatedVT)
					{
						AllocatedVT->GetPackedUniform(VTPackedUniform, StackAndLayerIndex.LayerIndex);
					}
					bFoundTexture = true;
				}
			}
			
			// Now check for runtime virtual texture
			if (!bFoundTexture)
			{
				const URuntimeVirtualTexture* Texture = nullptr;
				GetTextureValue(ExpressionIndex, MaterialRenderContext, MaterialRenderContext.Material, Texture);
				if (Texture != nullptr)
				{
					IAllocatedVirtualTexture const* AllocatedVT = Texture->GetAllocatedVirtualTexture();
					if (AllocatedVT)
					{
						AllocatedVT->GetPackedUniform(VTPackedUniform, Parameter.VirtualTextureLayerIndex);
					}
				}
			}
		}

		// Dump vector expression into the buffer.
		FPreshaderStack PreshaderStack;
		FPreshaderDataContext PreshaderBaseContext(UniformPreshaderData);
		for(int32 VectorIndex = 0;VectorIndex < UniformVectorPreshaders.Num();++VectorIndex)
		{
			FLinearColor VectorValue(0, 0, 0, 0);

			const FMaterialUniformPreshaderHeader& Preshader = UniformVectorPreshaders[VectorIndex];
			FPreshaderDataContext PreshaderContext(PreshaderBaseContext, Preshader);
			EvaluatePreshader(this, MaterialRenderContext, PreshaderStack, PreshaderContext, VectorValue);
	
			FLinearColor* DestAddress = (FLinearColor*)BufferCursor;
			*DestAddress = VectorValue;
			BufferCursor = DestAddress + 1;
			check(BufferCursor <= TempBuffer + TempBufferSize);
		}

		// Dump scalar expression into the buffer.
		for(int32 ScalarIndex = 0;ScalarIndex < UniformScalarPreshaders.Num();++ScalarIndex)
		{
			FLinearColor VectorValue(0,0,0,0);

			const FMaterialUniformPreshaderHeader& Preshader = UniformScalarPreshaders[ScalarIndex];
			FPreshaderDataContext PreshaderContext(PreshaderBaseContext, Preshader);
			EvaluatePreshader(this, MaterialRenderContext, PreshaderStack, PreshaderContext, VectorValue);

			float* DestAddress = (float*)BufferCursor;
			*DestAddress = VectorValue.R;
			BufferCursor = DestAddress + 1;
			check(BufferCursor <= TempBuffer + TempBufferSize);
		}

		// Offsets the cursor to next first resource.
		BufferCursor = ((float*)BufferCursor) + ((4 - UniformScalarPreshaders.Num() % 4) % 4);
		check(BufferCursor <= TempBuffer + TempBufferSize);

#if DO_CHECK
		{
			uint32 NumPageTableTextures = 0u;
			uint32 NumPageTableIndirectionTextures = 0u;
			for (int i = 0; i < VTStacks.Num(); ++i)
			{
				NumPageTableTextures += VTStacks[i].GetNumLayers() > 4u ? 2: 1;
				NumPageTableIndirectionTextures++;
			}
	
			check(UniformBufferLayout.Resources.Num() == 
				UniformTextureParameters[(uint32)EMaterialTextureParameterType::Standard2D].Num() * 2
				+ UniformTextureParameters[(uint32)EMaterialTextureParameterType::Cube].Num() * 2
				+ UniformTextureParameters[(uint32)EMaterialTextureParameterType::Array2D].Num() * 2
				+ UniformTextureParameters[(uint32)EMaterialTextureParameterType::Volume].Num() * 2
				+ UniformExternalTextureParameters.Num() * 2
				+ UniformTextureParameters[(uint32)EMaterialTextureParameterType::Virtual].Num() * 2
				+ NumPageTableTextures
				+ NumPageTableIndirectionTextures
				+ 2);
		}
#endif // DO_CHECK

		// Cache 2D texture uniform expressions.
		for(int32 ExpressionIndex = 0;ExpressionIndex < GetNumTextures(EMaterialTextureParameterType::Standard2D);ExpressionIndex++)
		{
			const FMaterialTextureParameterInfo& Parameter = GetTextureParameter(EMaterialTextureParameterType::Standard2D, ExpressionIndex);

			const UTexture* Value = nullptr;
			GetTextureValue(EMaterialTextureParameterType::Standard2D, ExpressionIndex, MaterialRenderContext,MaterialRenderContext.Material,Value);
			if (Value)
			{
				// Pre-application validity checks (explicit ensures to avoid needless string allocation)
				//const FMaterialUniformExpressionTextureParameter* TextureParameter = (Uniform2DTextureExpressions[ExpressionIndex]->GetType() == &FMaterialUniformExpressionTextureParameter::StaticType) ?
				//	&static_cast<const FMaterialUniformExpressionTextureParameter&>(*Uniform2DTextureExpressions[ExpressionIndex]) : nullptr;

				// gmartin: Trying to locate UE-23902
				if (!Value->IsValidLowLevel())
				{
					ensureMsgf(false, TEXT("Texture not valid! UE-23902! Parameter (%s)"), *Parameter.ParameterInfo.Name.ToString());
				}

				// Trying to track down a dangling pointer bug.
				checkf(
					Value->IsA<UTexture>(),
					TEXT("Expecting a UTexture! Name(%s), Type(%s), TextureParameter(%s), Expression(%d), Material(%s)"),
					*Value->GetName(), *Value->GetClass()->GetName(),
					*Parameter.ParameterInfo.Name.ToString(),
					ExpressionIndex,
					*MaterialRenderContext.Material.GetFriendlyName());

				// Do not allow external textures to be applied to normal texture samplers
				if (Value->GetMaterialType() == MCT_TextureExternal)
				{
					FText MessageText = FText::Format(
						NSLOCTEXT("MaterialExpressions", "IncompatibleExternalTexture", " applied to a non-external Texture2D sampler. This may work by chance on some platforms but is not portable. Please change sampler type to 'External'. Parameter '{0}' (slot {1}) in material '{2}'"),
						FText::FromName(Parameter.ParameterInfo.GetName()),
						ExpressionIndex,
						FText::FromString(*MaterialRenderContext.Material.GetFriendlyName()));

					GLog->Logf(ELogVerbosity::Warning, TEXT("%s"), *MessageText.ToString());
				}
			}

			void** ResourceTableTexturePtr = (void**)((uint8*)BufferCursor + 0 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			void** ResourceTableSamplerPtr = (void**)((uint8*)BufferCursor + 1 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			BufferCursor = ((uint8*)BufferCursor) + (SHADER_PARAMETER_POINTER_ALIGNMENT * 2);
			check(BufferCursor <= TempBuffer + TempBufferSize);

			// ExternalTexture is allowed here, with warning above
			// VirtualTexture is allowed here, as these may be demoted to regular textures on platforms that don't have VT support
			const uint32 ValidTextureTypes = MCT_Texture2D | MCT_TextureVirtual | MCT_TextureExternal;

			bool bValueValid = false;

			// TextureReference.TextureReferenceRHI is cleared from a render command issued by UTexture::BeginDestroy
			// It's possible for this command to trigger before a given material is cleaned up and removed from deferred update list
			// Technically I don't think it's necessary to check 'Resource' for nullptr here, as if TextureReferenceRHI has been initialized, that should be enough
			// Going to leave the check for now though, to hopefully avoid any unexpected problems
			if (Value && Value->Resource && Value->TextureReference.TextureReferenceRHI && (Value->GetMaterialType() & ValidTextureTypes) != 0u)
			{
				FSamplerStateRHIRef* SamplerSource = &Value->Resource->SamplerStateRHI;

				const ESamplerSourceMode SourceMode = Parameter.SamplerSource;
				if (SourceMode == SSM_Wrap_WorldGroupSettings)
				{
					SamplerSource = &Wrap_WorldGroupSettings->SamplerStateRHI;
				}
				else if (SourceMode == SSM_Clamp_WorldGroupSettings)
				{
					SamplerSource = &Clamp_WorldGroupSettings->SamplerStateRHI;
				}

				if (*SamplerSource)
				{
					*ResourceTableTexturePtr = Value->TextureReference.TextureReferenceRHI;
					*ResourceTableSamplerPtr = *SamplerSource;
					bValueValid = true;
				}
				else
				{
					ensureMsgf(false,
						TEXT("Texture %s of class %s had invalid sampler source. Material %s with texture expression in slot %i. Sampler source mode %d. Resource initialized: %d"),
						*Value->GetName(), *Value->GetClass()->GetName(),
						*MaterialRenderContext.Material.GetFriendlyName(), ExpressionIndex, SourceMode,
						Value->Resource->IsInitialized());
				}
			}

			if (!bValueValid)
			{
				check(GWhiteTexture->TextureRHI);
				*ResourceTableTexturePtr = GWhiteTexture->TextureRHI;
				check(GWhiteTexture->SamplerStateRHI);
				*ResourceTableSamplerPtr = GWhiteTexture->SamplerStateRHI;
			}
		}

		// Cache cube texture uniform expressions.
		for(int32 ExpressionIndex = 0;ExpressionIndex < GetNumTextures(EMaterialTextureParameterType::Cube); ExpressionIndex++)
		{
			const FMaterialTextureParameterInfo& Parameter = GetTextureParameter(EMaterialTextureParameterType::Cube, ExpressionIndex);

			const UTexture* Value = nullptr;
			GetTextureValue(EMaterialTextureParameterType::Cube, ExpressionIndex, MaterialRenderContext,MaterialRenderContext.Material,Value);

			void** ResourceTableTexturePtr = (void**)((uint8*)BufferCursor + 0 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			void** ResourceTableSamplerPtr = (void**)((uint8*)BufferCursor + 1 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			BufferCursor = ((uint8*)BufferCursor) + (SHADER_PARAMETER_POINTER_ALIGNMENT * 2);
			check(BufferCursor <= TempBuffer + TempBufferSize);

			if(Value && Value->Resource && (Value->GetMaterialType() & MCT_TextureCube) != 0u)
			{
				check(Value->TextureReference.TextureReferenceRHI);
				*ResourceTableTexturePtr = Value->TextureReference.TextureReferenceRHI;
				FSamplerStateRHIRef* SamplerSource = &Value->Resource->SamplerStateRHI;

				const ESamplerSourceMode SourceMode = Parameter.SamplerSource;
				if (SourceMode == SSM_Wrap_WorldGroupSettings)
				{
					SamplerSource = &Wrap_WorldGroupSettings->SamplerStateRHI;
				}
				else if (SourceMode == SSM_Clamp_WorldGroupSettings)
				{
					SamplerSource = &Clamp_WorldGroupSettings->SamplerStateRHI;
				}

				check(*SamplerSource);
				*ResourceTableSamplerPtr = *SamplerSource;
			}
			else
			{
				check(GWhiteTextureCube->TextureRHI);
				*ResourceTableTexturePtr = GWhiteTextureCube->TextureRHI;
				check(GWhiteTextureCube->SamplerStateRHI);
				*ResourceTableSamplerPtr = GWhiteTextureCube->SamplerStateRHI;
			}
		}

		// Cache 2d array texture uniform expressions.
		for (int32 ExpressionIndex = 0; ExpressionIndex < GetNumTextures(EMaterialTextureParameterType::Array2D); ExpressionIndex++)
		{
			const FMaterialTextureParameterInfo& Parameter = GetTextureParameter(EMaterialTextureParameterType::Array2D, ExpressionIndex);

			const UTexture* Value;
			GetTextureValue(EMaterialTextureParameterType::Array2D, ExpressionIndex, MaterialRenderContext, MaterialRenderContext.Material, Value);

			void** ResourceTableTexturePtr = (void**)((uint8*)BufferCursor + 0 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			void** ResourceTableSamplerPtr = (void**)((uint8*)BufferCursor + 1 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			BufferCursor = ((uint8*)BufferCursor) + (SHADER_PARAMETER_POINTER_ALIGNMENT * 2);

			if (Value && Value->Resource && (Value->GetMaterialType() & MCT_Texture2DArray) != 0u)
			{
				check(Value->TextureReference.TextureReferenceRHI);
				*ResourceTableTexturePtr = Value->TextureReference.TextureReferenceRHI;
				FSamplerStateRHIRef* SamplerSource = &Value->Resource->SamplerStateRHI;
				ESamplerSourceMode SourceMode = Parameter.SamplerSource;
				if (SourceMode == SSM_Wrap_WorldGroupSettings)
				{
					SamplerSource = &Wrap_WorldGroupSettings->SamplerStateRHI;
				}
				else if (SourceMode == SSM_Clamp_WorldGroupSettings)
				{
					SamplerSource = &Clamp_WorldGroupSettings->SamplerStateRHI;
				}

				check(*SamplerSource);
				*ResourceTableSamplerPtr = *SamplerSource;
			}
			else
			{
				check(GBlackArrayTexture->TextureRHI);
				*ResourceTableTexturePtr = GBlackArrayTexture->TextureRHI;
				check(GBlackArrayTexture->SamplerStateRHI);
				*ResourceTableSamplerPtr = GBlackArrayTexture->SamplerStateRHI;
			}
		}

		// Cache volume texture uniform expressions.
		for (int32 ExpressionIndex = 0;ExpressionIndex < GetNumTextures(EMaterialTextureParameterType::Volume);ExpressionIndex++)
		{
			const FMaterialTextureParameterInfo& Parameter = GetTextureParameter(EMaterialTextureParameterType::Volume, ExpressionIndex);

			const UTexture* Value = nullptr;
			GetTextureValue(EMaterialTextureParameterType::Volume, ExpressionIndex, MaterialRenderContext,MaterialRenderContext.Material,Value);

			void** ResourceTableTexturePtr = (void**)((uint8*)BufferCursor + 0 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			void** ResourceTableSamplerPtr = (void**)((uint8*)BufferCursor + 1 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			BufferCursor = ((uint8*)BufferCursor) + (SHADER_PARAMETER_POINTER_ALIGNMENT * 2);
			check(BufferCursor <= TempBuffer + TempBufferSize);

			if(Value && Value->Resource && (Value->GetMaterialType() & MCT_VolumeTexture) != 0u)
			{
				check(Value->TextureReference.TextureReferenceRHI);
				*ResourceTableTexturePtr = Value->TextureReference.TextureReferenceRHI;
				FSamplerStateRHIRef* SamplerSource = &Value->Resource->SamplerStateRHI;

				const ESamplerSourceMode SourceMode = Parameter.SamplerSource;
				if (SourceMode == SSM_Wrap_WorldGroupSettings)
				{
					SamplerSource = &Wrap_WorldGroupSettings->SamplerStateRHI;
				}
				else if (SourceMode == SSM_Clamp_WorldGroupSettings)
				{
					SamplerSource = &Clamp_WorldGroupSettings->SamplerStateRHI;
				}

				check(*SamplerSource);
				*ResourceTableSamplerPtr = *SamplerSource;
			}
			else
			{
				check(GBlackVolumeTexture->TextureRHI);
				*ResourceTableTexturePtr = GBlackVolumeTexture->TextureRHI;
				check(GBlackVolumeTexture->SamplerStateRHI);
				*ResourceTableSamplerPtr = GBlackVolumeTexture->SamplerStateRHI;
			}
		}

		// Cache external texture uniform expressions.
		uint32 ImmutableSamplerIndex = 0;
		FImmutableSamplerState& ImmutableSamplerState = MaterialRenderContext.MaterialRenderProxy->ImmutableSamplerState;
		ImmutableSamplerState.Reset();
		for (int32 ExpressionIndex = 0; ExpressionIndex < UniformExternalTextureParameters.Num(); ExpressionIndex++)
		{
			FTextureRHIRef TextureRHI;
			FSamplerStateRHIRef SamplerStateRHI;

			void** ResourceTableTexturePtr = (void**)((uint8*)BufferCursor + 0 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			void** ResourceTableSamplerPtr = (void**)((uint8*)BufferCursor + 1 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			BufferCursor = ((uint8*)BufferCursor) + (SHADER_PARAMETER_POINTER_ALIGNMENT * 2);
			check(BufferCursor <= TempBuffer + TempBufferSize);

			if (UniformExternalTextureParameters[ExpressionIndex].GetExternalTexture(MaterialRenderContext, TextureRHI, SamplerStateRHI))
			{
				*ResourceTableTexturePtr = TextureRHI;
				*ResourceTableSamplerPtr = SamplerStateRHI;

				if (SamplerStateRHI->IsImmutable())
				{
					ImmutableSamplerState.ImmutableSamplers[ImmutableSamplerIndex++] = SamplerStateRHI;
				}
			}
			else
			{
				check(GWhiteTexture->TextureRHI);
				*ResourceTableTexturePtr = GWhiteTexture->TextureRHI;
				check(GWhiteTexture->SamplerStateRHI);
				*ResourceTableSamplerPtr = GWhiteTexture->SamplerStateRHI;
			}
		}

		// Cache virtual texture page table uniform expressions.
		for (int32 VTStackIndex = 0; VTStackIndex < VTStacks.Num(); ++VTStackIndex)
		{
			void** ResourceTablePageTexture0Ptr = (void**)((uint8*)BufferCursor + 0 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			BufferCursor = ((uint8*)BufferCursor) + SHADER_PARAMETER_POINTER_ALIGNMENT;

			void** ResourceTablePageTexture1Ptr = nullptr;
			if (VTStacks[VTStackIndex].GetNumLayers() > 4u)
			{
				ResourceTablePageTexture1Ptr = (void**)((uint8*)BufferCursor + 0 * SHADER_PARAMETER_POINTER_ALIGNMENT);
				BufferCursor = ((uint8*)BufferCursor) + SHADER_PARAMETER_POINTER_ALIGNMENT;
			}

			void** ResourceTablePageIndirectionBuffer = (void**)((uint8*)BufferCursor + 0 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			BufferCursor = ((uint8*)BufferCursor) + SHADER_PARAMETER_POINTER_ALIGNMENT;

			const IAllocatedVirtualTexture* AllocatedVT = UniformExpressionCache.AllocatedVTs[VTStackIndex];
			if (AllocatedVT != nullptr)
			{
				FRHITexture* PageTable0RHI = AllocatedVT->GetPageTableTexture(0u);
				ensure(PageTable0RHI);
				*ResourceTablePageTexture0Ptr = PageTable0RHI;

				if (ResourceTablePageTexture1Ptr != nullptr)
				{
					FRHITexture* PageTable1RHI = AllocatedVT->GetPageTableTexture(1u);
					ensure(PageTable1RHI);
					*ResourceTablePageTexture1Ptr = PageTable1RHI;
				}

				FRHITexture* PageTableIndirectionRHI = AllocatedVT->GetPageTableIndirectionTexture();
				ensure(PageTableIndirectionRHI);
				*ResourceTablePageIndirectionBuffer = PageTableIndirectionRHI;
			}
			else
			{
				// Don't have valid resources to bind for this VT, so make sure something is bound
				*ResourceTablePageTexture0Ptr = GBlackUintTexture->TextureRHI;
				if (ResourceTablePageTexture1Ptr != nullptr)
				{
					*ResourceTablePageTexture1Ptr = GBlackUintTexture->TextureRHI;
				}
				*ResourceTablePageIndirectionBuffer = GBlackUintTexture->TextureRHI;
			}
		}

		// Cache virtual texture physical uniform expressions.
		for (int32 ExpressionIndex = 0; ExpressionIndex < GetNumTextures(EMaterialTextureParameterType::Virtual); ExpressionIndex++)
		{
			const FMaterialTextureParameterInfo& Parameter = GetTextureParameter(EMaterialTextureParameterType::Virtual, ExpressionIndex);

			FTextureRHIRef TexturePhysicalRHI;
			FSamplerStateRHIRef PhysicalSamplerStateRHI;

			bool bValidResources = false;
			void** ResourceTablePhysicalTexturePtr = (void**)((uint8*)BufferCursor + 0 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			void** ResourceTablePhysicalSamplerPtr = (void**)((uint8*)BufferCursor + 1 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			BufferCursor = ((uint8*)BufferCursor) + (SHADER_PARAMETER_POINTER_ALIGNMENT * 2);

			// Check for streaming virtual texture
			if (!bValidResources)
			{
				const UTexture* Texture = nullptr;
				GetTextureValue(EMaterialTextureParameterType::Virtual, ExpressionIndex, MaterialRenderContext, MaterialRenderContext.Material, Texture);
				if (Texture && Texture->Resource)
				{
					const FVTPackedStackAndLayerIndex StackAndLayerIndex = GetVTStackAndLayerIndex(ExpressionIndex);
					FVirtualTexture2DResource* VTResource = (FVirtualTexture2DResource*)Texture->Resource;
					check(VTResource);

					const IAllocatedVirtualTexture* AllocatedVT = UniformExpressionCache.AllocatedVTs[StackAndLayerIndex.StackIndex];
					if (AllocatedVT != nullptr)
					{
						FRHIShaderResourceView* PhysicalViewRHI = AllocatedVT->GetPhysicalTextureSRV(StackAndLayerIndex.LayerIndex, VTResource->bSRGB);
						if (PhysicalViewRHI)
						{
							*ResourceTablePhysicalTexturePtr = PhysicalViewRHI;
							*ResourceTablePhysicalSamplerPtr = VTResource->SamplerStateRHI;
							bValidResources = true;
						}
					}
				}
			}
			
			// Now check for runtime virtual texture
			if (!bValidResources)
			{
				const URuntimeVirtualTexture* Texture = nullptr;
				GetTextureValue(ExpressionIndex, MaterialRenderContext, MaterialRenderContext.Material, Texture);
				if (Texture != nullptr)
				{
					IAllocatedVirtualTexture const* AllocatedVT = Texture->GetAllocatedVirtualTexture();
					if (AllocatedVT != nullptr)
					{
						const int32 LayerIndex = Parameter.VirtualTextureLayerIndex;
						FRHIShaderResourceView* PhysicalViewRHI = AllocatedVT->GetPhysicalTextureSRV(LayerIndex, Texture->IsLayerSRGB(LayerIndex));
						if (PhysicalViewRHI != nullptr)
						{
							*ResourceTablePhysicalTexturePtr = PhysicalViewRHI;
							*ResourceTablePhysicalSamplerPtr = TStaticSamplerState<SF_AnisotropicPoint, AM_Clamp, AM_Clamp, AM_Clamp, 0, 8>::GetRHI();
							bValidResources = true;
						}
					}
				}
			}
			// Don't have valid resources to bind for this VT, so make sure something is bound
			if (!bValidResources)
			{
				*ResourceTablePhysicalTexturePtr = GBlackTextureWithSRV->ShaderResourceViewRHI;
				*ResourceTablePhysicalSamplerPtr = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp, 0, 8>::GetRHI();
			}
		}

		{
			void** Wrap_WorldGroupSettingsSamplerPtr = (void**)((uint8*)BufferCursor + 0 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			check(Wrap_WorldGroupSettings->SamplerStateRHI);
			*Wrap_WorldGroupSettingsSamplerPtr = Wrap_WorldGroupSettings->SamplerStateRHI;

			void** Clamp_WorldGroupSettingsSamplerPtr = (void**)((uint8*)BufferCursor + 1 * SHADER_PARAMETER_POINTER_ALIGNMENT);
			check(Clamp_WorldGroupSettings->SamplerStateRHI);
			*Clamp_WorldGroupSettingsSamplerPtr = Clamp_WorldGroupSettings->SamplerStateRHI;

			BufferCursor = ((uint8*)BufferCursor) + (SHADER_PARAMETER_POINTER_ALIGNMENT * 2);
			check(BufferCursor <= TempBuffer + TempBufferSize);
		}
	}
}

uint32 FUniformExpressionSet::GetReferencedTexture2DRHIHash(const FMaterialRenderContext& MaterialRenderContext) const
{
	uint32 BaseHash = 0;

	for (int32 ExpressionIndex = 0; ExpressionIndex < GetNumTextures(EMaterialTextureParameterType::Standard2D); ExpressionIndex++)
	{
		const UTexture* Value;
		GetTextureValue(EMaterialTextureParameterType::Standard2D, ExpressionIndex, MaterialRenderContext, MaterialRenderContext.Material, Value);

		const uint32 ValidTextureTypes = MCT_Texture2D | MCT_TextureVirtual | MCT_TextureExternal;

		FRHITexture* TexturePtr = nullptr;
		if (Value && Value->Resource && Value->TextureReference.TextureReferenceRHI && (Value->GetMaterialType() & ValidTextureTypes) != 0u)
		{
			TexturePtr = Value->TextureReference.TextureReferenceRHI->GetReferencedTexture();
		}
		BaseHash = PointerHash(TexturePtr, BaseHash);
	}

	return BaseHash;
}

FMaterialUniformExpressionTexture::FMaterialUniformExpressionTexture() :
	TextureIndex(INDEX_NONE),
	TextureLayerIndex(INDEX_NONE),
	PageTableLayerIndex(INDEX_NONE),
#if WITH_EDITORONLY_DATA
	SamplerType(SAMPLERTYPE_Color),
#endif
	SamplerSource(SSM_FromTextureAsset),
	bVirtualTexture(false)
{}

FMaterialUniformExpressionTexture::FMaterialUniformExpressionTexture(int32 InTextureIndex, EMaterialSamplerType InSamplerType, ESamplerSourceMode InSamplerSource, bool InVirtualTexture) :
	TextureIndex(InTextureIndex),
	TextureLayerIndex(INDEX_NONE),
	PageTableLayerIndex(INDEX_NONE),
#if WITH_EDITORONLY_DATA
	SamplerType(InSamplerType),
#endif
	SamplerSource(InSamplerSource),
	bVirtualTexture(InVirtualTexture)
{
}

FMaterialUniformExpressionTexture::FMaterialUniformExpressionTexture(int32 InTextureIndex, int16 InTextureLayerIndex, int16 InPageTableLayerIndex, EMaterialSamplerType InSamplerType)
	: TextureIndex(InTextureIndex)
	, TextureLayerIndex(InTextureLayerIndex)
	, PageTableLayerIndex(InPageTableLayerIndex)
#if WITH_EDITORONLY_DATA
	, SamplerType(InSamplerType)
#endif
	, SamplerSource(SSM_Wrap_WorldGroupSettings)
	, bVirtualTexture(true)
{
}

void FMaterialUniformExpressionTexture::GetTextureParameterInfo(FMaterialTextureParameterInfo& OutParameter) const
{
	OutParameter.TextureIndex = TextureIndex;
	OutParameter.SamplerSource = SamplerSource;
	OutParameter.VirtualTextureLayerIndex = TextureLayerIndex;
}

bool FMaterialUniformExpressionTexture::IsIdentical(const FMaterialUniformExpression* OtherExpression) const
{
	if (GetType() != OtherExpression->GetType())
	{
		return false;
	}
	FMaterialUniformExpressionTexture* OtherTextureExpression = (FMaterialUniformExpressionTexture*)OtherExpression;

	return TextureIndex == OtherTextureExpression->TextureIndex && 
		TextureLayerIndex == OtherTextureExpression->TextureLayerIndex &&
		PageTableLayerIndex == OtherTextureExpression->PageTableLayerIndex &&
		bVirtualTexture == OtherTextureExpression->bVirtualTexture;
}

FMaterialUniformExpressionExternalTextureBase::FMaterialUniformExpressionExternalTextureBase(int32 InSourceTextureIndex)
	: SourceTextureIndex(InSourceTextureIndex)
{}

FMaterialUniformExpressionExternalTextureBase::FMaterialUniformExpressionExternalTextureBase(const FGuid& InGuid)
	: SourceTextureIndex(INDEX_NONE)
	, ExternalTextureGuid(InGuid)
{
}

bool FMaterialUniformExpressionExternalTextureBase::IsIdentical(const FMaterialUniformExpression* OtherExpression) const
{
	if (GetType() != OtherExpression->GetType())
	{
		return false;
	}

	const auto* Other = static_cast<const FMaterialUniformExpressionExternalTextureBase*>(OtherExpression);
	return SourceTextureIndex == Other->SourceTextureIndex && ExternalTextureGuid == Other->ExternalTextureGuid;
}

FGuid FMaterialUniformExpressionExternalTextureBase::ResolveExternalTextureGUID(const FMaterialRenderContext& Context, TOptional<FName> ParameterName) const
{
	return GetExternalTextureGuid(Context, ExternalTextureGuid, ParameterName.IsSet() ? ParameterName.GetValue() : FName(), SourceTextureIndex);
}

void FMaterialUniformExpressionExternalTexture::GetExternalTextureParameterInfo(FMaterialExternalTextureParameterInfo& OutParameter) const
{
	OutParameter.ExternalTextureGuid = ExternalTextureGuid;
	OutParameter.SourceTextureIndex = SourceTextureIndex;
}

FMaterialUniformExpressionExternalTextureParameter::FMaterialUniformExpressionExternalTextureParameter()
{}

FMaterialUniformExpressionExternalTextureParameter::FMaterialUniformExpressionExternalTextureParameter(FName InParameterName, int32 InTextureIndex)
	: Super(InTextureIndex)
	, ParameterName(InParameterName)
{}

void FMaterialUniformExpressionExternalTextureParameter::GetExternalTextureParameterInfo(FMaterialExternalTextureParameterInfo& OutParameter) const
{
	Super::GetExternalTextureParameterInfo(OutParameter);
	OutParameter.ParameterName = NameToScriptName(ParameterName);
}

bool FMaterialUniformExpressionExternalTextureParameter::IsIdentical(const FMaterialUniformExpression* OtherExpression) const
{
	if (GetType() != OtherExpression->GetType())
	{
		return false;
	}

	auto* Other = static_cast<const FMaterialUniformExpressionExternalTextureParameter*>(OtherExpression);
	return ParameterName == Other->ParameterName && Super::IsIdentical(OtherExpression);
}

void FMaterialScalarParameterInfo::GetGameThreadNumberValue(const UMaterialInterface* SourceMaterialToCopyFrom, float& OutValue) const
{
	check(IsInGameThread());
	checkSlow(SourceMaterialToCopyFrom);

	const UMaterialInterface* It = SourceMaterialToCopyFrom;

	for (;;)
	{
		const UMaterialInstance* MatInst = Cast<UMaterialInstance>(It);

		if (MatInst)
		{
			const FScalarParameterValue* ParameterValue = GameThread_FindParameterByName(MatInst->ScalarParameterValues, ParameterInfo);
			if (ParameterValue)
			{
				OutValue = ParameterValue->ParameterValue;
				break;
			}

			// go up the hierarchy
			It = MatInst->Parent;
		}
		else
		{
			// we reached the base material
			// get the copy form the base material
			GetDefaultValue(OutValue);
			break;
		}
	}
}

void FMaterialVectorParameterInfo::GetGameThreadNumberValue(const UMaterialInterface* SourceMaterialToCopyFrom, FLinearColor& OutValue) const
{
	check(IsInGameThread());
	checkSlow(SourceMaterialToCopyFrom);

	const UMaterialInterface* It = SourceMaterialToCopyFrom;

	for (;;)
	{
		const UMaterialInstance* MatInst = Cast<UMaterialInstance>(It);

		if (MatInst)
		{
			const FVectorParameterValue* ParameterValue = GameThread_FindParameterByName(MatInst->VectorParameterValues, ParameterInfo);
			if (ParameterValue)
			{
				OutValue = ParameterValue->ParameterValue;
				break;
			}

			// go up the hierarchy
			It = MatInst->Parent;
		}
		else
		{
			// we reached the base material
			// get the copy form the base material
			GetDefaultValue(OutValue);
			break;
		}
	}
}

void FMaterialTextureParameterInfo::GetGameThreadTextureValue(const UMaterialInterface* MaterialInterface, const FMaterial& Material, UTexture*& OutValue) const
{
	if (!ParameterInfo.Name.IsNone())
	{
		const bool bOverrideValuesOnly = !Material.HasMaterialLayers();
		if (!MaterialInterface->GetTextureParameterValue(ParameterInfo, OutValue, bOverrideValuesOnly))
		{
			OutValue = GetIndexedTexture<UTexture>(Material, TextureIndex);
		}
	}
	else
	{
		OutValue = GetIndexedTexture<UTexture>(Material, TextureIndex);
	}
}

bool FMaterialExternalTextureParameterInfo::GetExternalTexture(const FMaterialRenderContext& Context, FTextureRHIRef& OutTextureRHI, FSamplerStateRHIRef& OutSamplerStateRHI) const
{
	check(IsInParallelRenderingThread());
	const FGuid GuidToLookup = GetExternalTextureGuid(Context, ExternalTextureGuid, ScriptNameToName(ParameterName), SourceTextureIndex);
	return FExternalTextureRegistry::Get().GetExternalTexture(Context.MaterialRenderProxy, GuidToLookup, OutTextureRHI, OutSamplerStateRHI);
}

bool FMaterialUniformExpressionExternalTextureCoordinateScaleRotation::IsIdentical(const FMaterialUniformExpression* OtherExpression) const
{
	if (GetType() != OtherExpression->GetType() || !Super::IsIdentical(OtherExpression))
	{
		return false;
	}

	const auto* Other = static_cast<const FMaterialUniformExpressionExternalTextureCoordinateScaleRotation*>(OtherExpression);
	return ParameterName == Other->ParameterName;
}

void FMaterialUniformExpressionExternalTextureCoordinateScaleRotation::WriteNumberOpcodes(FMaterialPreshaderData& OutData) const
{
	const FScriptName Name = ParameterName.IsSet() ? NameToScriptName(ParameterName.GetValue()) : FScriptName();
	OutData.WriteOpcode(EMaterialPreshaderOpcode::ExternalTextureCoordinateScaleRotation).Write(Name).Write(ExternalTextureGuid).Write<int32>(SourceTextureIndex);
}

bool FMaterialUniformExpressionExternalTextureCoordinateOffset::IsIdentical(const FMaterialUniformExpression* OtherExpression) const
{
	if (GetType() != OtherExpression->GetType() || !Super::IsIdentical(OtherExpression))
	{
		return false;
	}

	const auto* Other = static_cast<const FMaterialUniformExpressionExternalTextureCoordinateOffset*>(OtherExpression);
	return ParameterName == Other->ParameterName;
}

void FMaterialUniformExpressionExternalTextureCoordinateOffset::WriteNumberOpcodes(FMaterialPreshaderData& OutData) const
{
	const FScriptName Name = ParameterName.IsSet() ? NameToScriptName(ParameterName.GetValue()) : FScriptName();
	OutData.WriteOpcode(EMaterialPreshaderOpcode::ExternalTextureCoordinateOffset).Write(Name).Write(ExternalTextureGuid).Write<int32>(SourceTextureIndex);
}

FMaterialUniformExpressionRuntimeVirtualTextureUniform::FMaterialUniformExpressionRuntimeVirtualTextureUniform()
	: bParameter(false)
	, TextureIndex(INDEX_NONE)
	, VectorIndex(INDEX_NONE)
{
}

FMaterialUniformExpressionRuntimeVirtualTextureUniform::FMaterialUniformExpressionRuntimeVirtualTextureUniform(int32 InTextureIndex, int32 InVectorIndex)
	: bParameter(false)
	, TextureIndex(InTextureIndex)
	, VectorIndex(InVectorIndex)
{
}

FMaterialUniformExpressionRuntimeVirtualTextureUniform::FMaterialUniformExpressionRuntimeVirtualTextureUniform(const FMaterialParameterInfo& InParameterInfo, int32 InTextureIndex, int32 InVectorIndex)
	: bParameter(true)
	, ParameterInfo(InParameterInfo)
	, TextureIndex(InTextureIndex)
	, VectorIndex(InVectorIndex)
{
}

bool FMaterialUniformExpressionRuntimeVirtualTextureUniform::IsIdentical(const FMaterialUniformExpression* OtherExpression) const
{
	if (GetType() != OtherExpression->GetType())
	{
		return false;
	}

	const auto* Other = static_cast<const FMaterialUniformExpressionRuntimeVirtualTextureUniform*>(OtherExpression);
	return ParameterInfo == Other->ParameterInfo && TextureIndex == Other->TextureIndex && VectorIndex == Other->VectorIndex;
}

void FMaterialUniformExpressionRuntimeVirtualTextureUniform::WriteNumberOpcodes(FMaterialPreshaderData& OutData) const
{
	const FHashedMaterialParameterInfo WriteParameterInfo = bParameter ? ParameterInfo : FHashedMaterialParameterInfo();
	OutData.WriteOpcode(EMaterialPreshaderOpcode::RuntimeVirtualTextureUniform).Write(WriteParameterInfo).Write((int32)TextureIndex).Write((int32)VectorIndex);
}

/**
 * Deprecated FMaterialUniformExpressionRuntimeVirtualTextureParameter in favor of FMaterialUniformExpressionRuntimeVirtualTextureUniform
 * Keep around until we no longer need to support serialization of 4.23 data
 */
class FMaterialUniformExpressionRuntimeVirtualTextureParameter_DEPRECATED : public FMaterialUniformExpressionRuntimeVirtualTextureUniform
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionRuntimeVirtualTextureParameter_DEPRECATED);
};

IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionTexture);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionConstant);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionVectorParameter);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionScalarParameter);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionTextureParameter);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionExternalTextureBase);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionExternalTexture);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionExternalTextureParameter);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionExternalTextureCoordinateScaleRotation);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionExternalTextureCoordinateOffset);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionRuntimeVirtualTextureUniform);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionFlipBookTextureParameter);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionSine);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionSquareRoot);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionLength);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionLogarithm2);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionLogarithm10);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionFoldedMath);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionPeriodic);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionAppendVector);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionMin);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionMax);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionClamp);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionSaturate);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionComponentSwizzle);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionFloor);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionCeil);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionFrac);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionFmod);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionAbs);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionTextureProperty);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionTrigMath);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionRound);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionTruncate);
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionSign);
