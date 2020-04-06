// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Shader.cpp: Shader implementation.
=============================================================================*/

#include "Shader.h"
#include "Misc/CoreMisc.h"
#include "Stats/StatsMisc.h"
#include "Serialization/MemoryWriter.h"
#include "VertexFactory.h"
#include "ProfilingDebugging/DiagnosticTable.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/IShaderFormat.h"
#include "ShaderCodeLibrary.h"
#include "ShaderCore.h"
#include "RenderUtils.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopeLock.h"
#include "UObject/RenderingObjectVersion.h"
#include "UObject/FortniteMainBranchObjectVersion.h"
#include "Misc/ScopeRWLock.h"
#include "ProfilingDebugging/LoadTimeTracker.h"

#if WITH_EDITORONLY_DATA
#include "Interfaces/IShaderFormat.h"
#endif

FShaderMapBase::FShaderMapBase(const FTypeLayoutDesc& InContentTypeLayout)
	: ContentTypeLayout(InContentTypeLayout)
	, PointerTable(nullptr)
	, Content(nullptr)
	, FrozenContentSize(0u)
	, NumFrozenShaders(0u)
{}

FShaderMapBase::~FShaderMapBase()
{
	DestroyContent();
	if (PointerTable)
	{
		delete PointerTable;
	}
}

FShaderMapResourceCode* FShaderMapBase::GetResourceCode()
{
	if (!Code)
	{
		Code = new FShaderMapResourceCode();
	}
	return Code;
}

void FShaderMapBase::AssignContent(FShaderMapContent* InContent)
{
	check(!Content);
	check(!PointerTable);
	Content = InContent;
	PointerTable = CreatePointerTable();
}

void FShaderMapBase::FinalizeContent()
{
	if (Content && FrozenContentSize == 0u)
	{
		Content->Validate(*this);

		FMemoryImage MemoryImage;
		MemoryImage.TargetLayoutParameters.InitializeForCurrent();
		MemoryImage.PointerTable = PointerTable;
		FMemoryImageWriter Writer(MemoryImage);

		Writer.WriteObject(Content, ContentTypeLayout);

		FMemoryImageResult MemoryImageResult;
		MemoryImage.Flatten(MemoryImageResult, true);

		DestroyContent();

		{
			FrozenContentSize = MemoryImageResult.Bytes.Num();
			check(FrozenContentSize > 0u);
			void* ContentMemory = FMemory::Malloc(FrozenContentSize);
			FMemory::Memcpy(ContentMemory, MemoryImageResult.Bytes.GetData(), FrozenContentSize);
			Content = static_cast<FShaderMapContent*>(ContentMemory);
			MemoryImageResult.ApplyPatches(Content);
			NumFrozenShaders = Content->GetNumShaders();
		}

		INC_DWORD_STAT_BY(STAT_Shaders_ShaderMemory, FrozenContentSize);
		INC_DWORD_STAT_BY(STAT_Shaders_NumShadersLoaded, NumFrozenShaders);
	}

	check(Code);
	Code->Finalize();
	Resource = new FShaderMapResource_InlineCode(GetShaderPlatform(), Code);
	BeginInitResource(Resource);

	INC_DWORD_STAT_BY(STAT_Shaders_ShaderResourceMemory, Resource->GetSizeBytes());
}

void FShaderMapBase::UnfreezeContent()
{
	if (Content && FrozenContentSize > 0u)
	{
		void* UnfrozenMemory = FMemory::Malloc(ContentTypeLayout.Size, ContentTypeLayout.Alignment);

		FMemoryUnfreezeContent Context;
		Context.PrevPointerTable = PointerTable;
		Context.UnfreezeObject(Content, ContentTypeLayout, UnfrozenMemory);

		DestroyContent();

		Content = static_cast<FShaderMapContent*>(UnfrozenMemory);
	}
}

#define CHECK_SHADERMAP_DEPENDENCIES (WITH_EDITOR || !(UE_BUILD_SHIPPING || UE_BUILD_TEST))

bool FShaderMapBase::Serialize(FArchive& Ar, bool bInlineShaderResources, bool bLoadedByCookedMaterial)
{
	LLM_SCOPE(ELLMTag::Shaders);
	bool bContentValid = true;
	if (Ar.IsSaving())
	{
		check(Content);
		Content->Validate(*this);

		FShaderMapPointerTable* SavePointerTable = CreatePointerTable();

		FMemoryImage MemoryImage;
		MemoryImage.PrevPointerTable = PointerTable;
		MemoryImage.PointerTable = SavePointerTable;
		MemoryImage.TargetLayoutParameters.InitializeForArchive(Ar);

		FMemoryImageWriter Writer(MemoryImage);

		Writer.WriteObject(Content, ContentTypeLayout);

		FMemoryImageResult MemoryImageResult;
		MemoryImage.Flatten(MemoryImageResult, true);

		void* SaveFrozenContent = MemoryImageResult.Bytes.GetData();
		uint32 SaveFrozenContentSize = MemoryImageResult.Bytes.Num();
		check(SaveFrozenContentSize > 0u);
		Ar << SaveFrozenContentSize;
		Ar.Serialize(SaveFrozenContent, SaveFrozenContentSize);
		MemoryImageResult.SaveToArchive(Ar);
		SavePointerTable->SaveToArchive(Ar, SaveFrozenContent, bInlineShaderResources);
		delete SavePointerTable;

		int32 NumDependencies = MemoryImage.TypeDependencies.Num();
		Ar << NumDependencies;
		for (const FTypeLayoutDesc* DependencyTypeDesc : MemoryImage.TypeDependencies)
		{
			uint64 NameHash = DependencyTypeDesc->NameHash;
			FSHAHash LayoutHash;
			uint32 LayoutSize = Freeze::HashLayout(*DependencyTypeDesc, MemoryImage.TargetLayoutParameters, LayoutHash);
			Ar << NameHash;
			Ar << LayoutSize;
			Ar << LayoutHash;
		}

		bool bShareCode = false;
#if WITH_EDITOR
		bShareCode = FShaderCodeLibrary::IsEnabled() && Ar.IsCooking();
#endif // WITH_EDITOR
		Ar << bShareCode;
#if WITH_EDITOR
		if (Ar.IsCooking())
		{
			Code->NotifyShadersCooked(Ar.CookingTarget());
		}

		if (bShareCode)
		{
			FSHAHash ResourceHash = Code->ResourceHash;
			Ar << ResourceHash;
			FShaderCodeLibrary::AddShaderCode(GetShaderPlatform(), Code);
		}
		else
#endif // WITH_EDITOR
		{
			Code->Serialize(Ar, bLoadedByCookedMaterial);
		}
	}
	else
	{
		check(!PointerTable);
		PointerTable = CreatePointerTable();

		Ar << FrozenContentSize;
		// ensure frozen content is at least as big as our FShaderMapContent-derived class
		checkf(FrozenContentSize >= ContentTypeLayout.Size, TEXT("Invalid FrozenContentSize for %s, got %d, expected at least %d"), ContentTypeLayout.Name, FrozenContentSize, ContentTypeLayout.Size);

		void* ContentMemory = FMemory::Malloc(FrozenContentSize);
		Ar.Serialize(ContentMemory, FrozenContentSize);
		Content = static_cast<FShaderMapContent*>(ContentMemory);
		FMemoryImageResult::ApplyPatchesFromArchive(Content, Ar);
		PointerTable->LoadFromArchive(Ar, Content, bInlineShaderResources, bLoadedByCookedMaterial);

		int32 NumDependencies = 0;
		Ar << NumDependencies;
		if(NumDependencies > 0)
		{
#if CHECK_SHADERMAP_DEPENDENCIES
			FPlatformTypeLayoutParameters LayoutParams;
			LayoutParams.InitializeForCurrent();
#endif // CHECK_SHADERMAP_DEPENDENCIES

			// Waste a bit of time even in shipping builds skipping over this stuff
			// Could add a cook-time option to exclude dependencies completely
			for (int32 i = 0u; i < NumDependencies; ++i)
			{
				uint64 NameHash = 0u;
				uint32 SavedLayoutSize = 0u;
				FSHAHash SavedLayoutHash;
				Ar << NameHash;
				Ar << SavedLayoutSize;
				Ar << SavedLayoutHash;
#if CHECK_SHADERMAP_DEPENDENCIES
				const FTypeLayoutDesc* DependencyType = FTypeLayoutDesc::Find(NameHash);
				if (DependencyType)
				{
					FSHAHash CheckLayoutHash;
					const uint32 CheckLayoutSize = Freeze::HashLayout(*DependencyType, LayoutParams, CheckLayoutHash);
					if (CheckLayoutSize != SavedLayoutSize)
					{
						UE_LOG(LogShaders, Error, TEXT("Mismatch size for type %s, compiled size is %d, loaded size is %d"), DependencyType->Name, CheckLayoutSize, SavedLayoutSize);
						bContentValid = false;
					}
					else if (CheckLayoutHash != SavedLayoutHash)
					{
						UE_LOG(LogShaders, Error, TEXT("Mismatch hash for type %s"), DependencyType->Name);
						bContentValid = false;
					}
				}
#endif // CHECK_SHADERMAP_DEPENDENCIES
			}
		}
		
		bool bShareCode = false;
		Ar << bShareCode;
		if (bShareCode)
		{
			FSHAHash ResourceHash;
			Ar << ResourceHash;
			Resource = FShaderCodeLibrary::LoadResource(ResourceHash, &Ar);
			if (!Resource)
			{
				if (FApp::CanEverRender())	// when running -nullrhi, the resource may not be created
				{
					UE_LOG(LogShaders, Error, TEXT("Missing shader resource for hash '%s' in the shader library"), *ResourceHash.ToString());
				}
				bContentValid = false;
			}
		}
		else
		{
			Code = new FShaderMapResourceCode();
			Code->Serialize(Ar, bLoadedByCookedMaterial);
			Resource = new FShaderMapResource_InlineCode(GetShaderPlatform(), Code);
		}

		if (bContentValid)
		{
			check(Resource);
			NumFrozenShaders = Content->GetNumShaders();

			BeginInitResource(Resource);

			INC_DWORD_STAT_BY(STAT_Shaders_ShaderResourceMemory, Resource->GetSizeBytes());
			INC_DWORD_STAT_BY(STAT_Shaders_ShaderMemory, FrozenContentSize);
			INC_DWORD_STAT_BY(STAT_Shaders_NumShadersLoaded, NumFrozenShaders);
		}
		else
		{
			Resource.SafeRelease();

			// Don't call destructors here, this is basically unknown/invalid memory at this point
			FMemory::Free(Content);
			Content = nullptr;
		}
	}

	return bContentValid;
}

void FShaderMapBase::DestroyContent()
{
	if (Content)
	{
		DEC_DWORD_STAT_BY(STAT_Shaders_ShaderMemory, FrozenContentSize);
		DEC_DWORD_STAT_BY(STAT_Shaders_NumShadersLoaded, NumFrozenShaders);

		InternalDeleteObjectFromLayout(Content, ContentTypeLayout, FrozenContentSize > 0u);
		if (FrozenContentSize > 0u)
		{
			FMemory::Free(Content);
		}

		FrozenContentSize = 0u;
		NumFrozenShaders = 0u;
		Content = nullptr;
	}
}

FShader* FShaderMapContent::GetShader(const FHashedName& TypeName, int32 PermutationId) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FShaderMapContent::GetShader);
	const int32 Index = Algo::BinarySearchBy(Shaders, FShaderMapKey(TypeName, PermutationId), FProjectShaderToKey());
	return (Index != INDEX_NONE) ? Shaders[Index].Get() : nullptr;
}

void FShaderMapContent::AddShader(FShader* Shader)
{
	check(!Shader->IsFrozen());
	checkSlow(!HasShader(Shader->GetTypeName(), Shader->GetPermutationId()));
	const int32 Index = Algo::LowerBoundBy(Shaders, FShaderMapKey(*Shader), FProjectShaderToKey());
	Shaders.Insert(Shader, Index);
}

FShader* FShaderMapContent::FindOrAddShader(FShader* Shader)
{
	check(!Shader->IsFrozen());
	const int32 Index = Algo::LowerBoundBy(Shaders, FShaderMapKey(*Shader), FProjectShaderToKey());
	if (Index < Shaders.Num())
	{
		FShader* PrevShader = Shaders[Index];
		if (PrevShader->GetTypeName() == Shader->GetTypeName() && PrevShader->GetPermutationId() == Shader->GetPermutationId())
		{
			DeleteObjectFromLayout(Shader);
			return PrevShader;
		}
	}

	Shaders.Insert(Shader, Index);
	return Shader;
}

void FShaderMapContent::AddShaderPipeline(FShaderPipeline* Pipeline)
{
	checkSlow(!HasShaderPipeline(Pipeline->TypeName));
	const int32 Index = Algo::LowerBoundBy(ShaderPipelines, Pipeline->TypeName, FProjectShaderPipelineToKey());
	ShaderPipelines.Insert(Pipeline, Index);
}

FShaderPipeline* FShaderMapContent::FindOrAddShaderPipeline(FShaderPipeline* Pipeline)
{
	const int32 Index = Algo::LowerBoundBy(ShaderPipelines, Pipeline->TypeName, FProjectShaderPipelineToKey());
	if (Index < ShaderPipelines.Num())
	{
		FShaderPipeline* PrevShaderPipeline = ShaderPipelines[Index];
		if (PrevShaderPipeline->TypeName == Pipeline->TypeName)
		{
			delete Pipeline;
			return PrevShaderPipeline;
		}
	}

	ShaderPipelines.Insert(Pipeline, Index);
	return Pipeline;
}

/**
 * Removes the shader of the given type from the shader map
 * @param Type Shader type to remove the entry for
 */
void FShaderMapContent::RemoveShaderTypePermutaion(const FShaderType* Type, int32 PermutationId)
{
	const int32 Index = Algo::BinarySearchBy(Shaders, FShaderMapKey(Type->GetHashedName(), PermutationId), FProjectShaderToKey());
	if (Index != INDEX_NONE)
	{
		Shaders.RemoveAt(Index, 1, false);
	}
}

void FShaderMapContent::RemoveShaderPipelineType(const FShaderPipelineType* ShaderPipelineType)
{
	const int32 Index = Algo::BinarySearchBy(ShaderPipelines, ShaderPipelineType->GetHashedName(), FProjectShaderPipelineToKey());
	if (Index != INDEX_NONE)
	{
		FShaderPipeline* Pipeline = ShaderPipelines[Index];
		delete Pipeline;
		ShaderPipelines.RemoveAt(Index, 1, false);
	}
}

void FShaderMapContent::GetShaderList(const FShaderMapBase& InShaderMap, const FSHAHash& InMaterialShaderMapHash, TMap<FShaderId, TShaderRef<FShader>>& OutShaders) const
{
	for (FShader* Shader : Shaders)
	{
		const FShaderId ShaderId(
			Shader->GetType(InShaderMap.GetPointerTable()),
			InMaterialShaderMapHash,
			FHashedName(),
			Shader->GetVertexFactoryType(InShaderMap.GetPointerTable()),
			Shader->GetPermutationId(),
			GetShaderPlatform());

		OutShaders.Add(ShaderId, TShaderRef<FShader>(Shader, InShaderMap));
	}

	for (const FShaderPipeline* ShaderPipeline : ShaderPipelines)
	{
		for (const TShaderRef<FShader>& Shader : ShaderPipeline->GetShaders(InShaderMap))
		{
			const FShaderId ShaderId(
				Shader.GetType(),
				InMaterialShaderMapHash,
				ShaderPipeline->TypeName,
				Shader.GetVertexFactoryType(),
				Shader->GetPermutationId(),
				GetShaderPlatform());

			OutShaders.Add(ShaderId, Shader);
		}
	}
}

void FShaderMapContent::GetShaderList(const FShaderMapBase& InShaderMap, TMap<FHashedName, TShaderRef<FShader>>& OutShaders) const
{
	for (FShader* Shader : Shaders)
	{
		OutShaders.Add(Shader->GetTypeName(), TShaderRef<FShader>(Shader, InShaderMap));
	}

	for (const FShaderPipeline* ShaderPipeline : ShaderPipelines)
	{
		for (const TShaderRef<FShader>& Shader : ShaderPipeline->GetShaders(InShaderMap))
		{
			OutShaders.Add(Shader->GetTypeName(), Shader);
		}
	}
}

void FShaderMapContent::GetShaderPipelineList(const FShaderMapBase& InShaderMap, TArray<FShaderPipelineRef>& OutShaderPipelines, FShaderPipeline::EFilter Filter) const
{
	for (FShaderPipeline* Pipeline : ShaderPipelines)
	{
		const FShaderPipelineType* PipelineType = FShaderPipelineType::GetShaderPipelineTypeByName(Pipeline->TypeName);
		if (PipelineType->ShouldOptimizeUnusedOutputs(Platform) && Filter == FShaderPipeline::EOnlyShared)
		{
			continue;
		}
		else if (!PipelineType->ShouldOptimizeUnusedOutputs(Platform) && Filter == FShaderPipeline::EOnlyUnique)
		{
			continue;
		}
		OutShaderPipelines.Add(FShaderPipelineRef(Pipeline, InShaderMap));
	}
}

void FShaderMapContent::Validate(const FShaderMapBase& InShaderMap)
{
	for (FShader* Shader : Shaders)
	{
		checkf(Shader->GetResourceIndex() != INDEX_NONE, TEXT("Missing resource for %s"), Shader->GetType(InShaderMap.GetPointerTable())->GetName());
	}

	/*for(FShaderPipeline* Pipeline : ShaderPipelines)
	{
		for(const TShaderRef<FShader>& Shader : Pipeline->GetShaders(InPtrTable))
		{
			checkf(Shader.GetResource(), TEXT("Missing resource for %s"), Shader.GetType()->GetName());
		}
	}*/
}

#if WITH_EDITOR
static bool CheckOutdatedShaderType(EShaderPlatform Platform, const TShaderRef<FShader>& Shader, TArray<const FShaderType*>& OutdatedShaderTypes, TArray<const FVertexFactoryType*>& OutdatedFactoryTypes)
{
	const FShaderType* Type = Shader.GetType();
	const bool bOutdatedShader = Type->GetSourceHash(Platform) != Shader->GetHash();

	const FVertexFactoryType* VFType = Shader.GetVertexFactoryType();
	const bool bOutdatedVertexFactory = VFType && VFType->GetSourceHash(Platform) != Shader->GetVertexFactoryHash();

	if (bOutdatedShader)
	{
		OutdatedShaderTypes.AddUnique(Type);
	}
	if (bOutdatedVertexFactory)
	{
		OutdatedFactoryTypes.AddUnique(VFType);
	}

	return bOutdatedShader || bOutdatedVertexFactory;
}

void FShaderMapContent::GetOutdatedTypes(const FShaderMapBase& InShaderMap, TArray<const FShaderType*>& OutdatedShaderTypes, TArray<const FShaderPipelineType*>& OutdatedShaderPipelineTypes, TArray<const FVertexFactoryType*>& OutdatedFactoryTypes) const
{
	for (FShader* Shader : Shaders)
	{
		CheckOutdatedShaderType(GetShaderPlatform(), TShaderRef<FShader>(Shader, InShaderMap), OutdatedShaderTypes, OutdatedFactoryTypes);
	}

	for (const FShaderPipeline* Pipeline : ShaderPipelines)
	{
		for (const TShaderRef<FShader>& Shader : Pipeline->GetShaders(InShaderMap))
		{
			if (CheckOutdatedShaderType(GetShaderPlatform(), Shader, OutdatedShaderTypes, OutdatedFactoryTypes))
			{
				const FShaderPipelineType* PipelineType = FShaderPipelineType::GetShaderPipelineTypeByName(Pipeline->TypeName);
				check(PipelineType);
				OutdatedShaderPipelineTypes.AddUnique(PipelineType);
			}
		}
	}
}

void FShaderMapContent::SaveShaderStableKeys(const FShaderMapBase& InShaderMap, EShaderPlatform TargetShaderPlatform, const struct FStableShaderKeyAndValue& SaveKeyVal)
{
	for (FShader* Shader : Shaders)
	{
		Shader->SaveShaderStableKeys(InShaderMap.GetPointerTable(), TargetShaderPlatform, SaveKeyVal);
	}

	for (const FShaderPipeline* Pipeline : ShaderPipelines)
	{
		Pipeline->SaveShaderStableKeys(InShaderMap.GetPointerTable(), TargetShaderPlatform, SaveKeyVal);
	}
}

uint32 FShaderMapContent::GetMaxTextureSamplersShaderMap(const FShaderMapBase& InShaderMap) const
{
	uint32 MaxTextureSamplers = 0;

	for (FShader* Shader : Shaders)
	{
		MaxTextureSamplers = FMath::Max(MaxTextureSamplers, Shader->GetNumTextureSamplers());
	}

	for (FShaderPipeline* Pipeline : ShaderPipelines)
	{
		for (const TShaderRef<FShader>& Shader : Pipeline->GetShaders(InShaderMap))
		{
			MaxTextureSamplers = FMath::Max(MaxTextureSamplers, Shader->GetNumTextureSamplers());
		}
	}

	return MaxTextureSamplers;
}
#endif // WITH_EDITOR

uint32 FShaderMapContent::GetNumShaders() const
{
	uint32 NumShaders = Shaders.Num();
	for (FShaderPipeline* Pipeline : ShaderPipelines)
	{
		NumShaders += Pipeline->GetNumShaders();
	}
	return NumShaders;
}

uint32 FShaderMapContent::GetMaxNumInstructionsForShader(const FShaderMapBase& InShaderMap, FShaderType* ShaderType) const
{
	uint32 MaxNumInstructions = 0;
	FShader* Shader = GetShader(ShaderType);
	if (Shader)
	{
		MaxNumInstructions = FMath::Max(MaxNumInstructions, Shader->GetNumInstructions());
	}

	for (FShaderPipeline* Pipeline : ShaderPipelines)
	{
		FShader* PipelineShader = Pipeline->GetShader(ShaderType->GetFrequency());
		if (PipelineShader)
		{
			MaxNumInstructions = FMath::Max(MaxNumInstructions, PipelineShader->GetNumInstructions());
		}
	}

	return MaxNumInstructions;
}

void FShaderMapContent::Empty()
{
	EmptyShaderPipelines();
	for (int32 i = 0; i < Shaders.Num(); ++i)
	{
		Shaders[i].SafeDelete();
	}
	Shaders.Empty();
}

void FShaderMapContent::EmptyShaderPipelines()
{
	for (TMemoryImagePtr<FShaderPipeline>& Pipeline : ShaderPipelines)
	{
		Pipeline.SafeDelete();
	}
	ShaderPipelines.Empty();
}