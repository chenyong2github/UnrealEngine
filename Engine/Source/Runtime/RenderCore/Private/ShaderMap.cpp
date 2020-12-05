// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Shader.cpp: Shader implementation.
=============================================================================*/

#include "Shader.h"
#include "Misc/CoreMisc.h"
#include "Misc/StringBuilder.h"
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

void FShaderMapBase::CopyResourceCode(const FShaderMapResourceCode& Source)
{
	Code = new FShaderMapResourceCode(Source);
}

void FShaderMapBase::AssignContent(FShaderMapContent* InContent)
{
	check(!Content);
	check(!PointerTable);
	Content = InContent;
	PointerTable = CreatePointerTable();
}

void FShaderMapBase::InitResource()
{
	Resource.SafeRelease();
	if (Code)
	{
		Code->Finalize();
		Resource = new FShaderMapResource_InlineCode(GetShaderPlatform(), Code);
		BeginInitResource(Resource);
	}
}

void FShaderMapBase::AssignAndFreezeContent(const FShaderMapContent* InContent)
{
	FShaderMapPointerTable* LocalPointerTable = nullptr;
	void* LocalContentMemory = nullptr;
	uint32 LocalContentSize = 0u;
	if (InContent)
	{
		LocalPointerTable = CreatePointerTable();

		FMemoryImage MemoryImage;
		MemoryImage.TargetLayoutParameters.InitializeForCurrent();
		MemoryImage.PointerTable = LocalPointerTable;
		FMemoryImageWriter Writer(MemoryImage);

		Writer.WriteObject(InContent, ContentTypeLayout);

		FMemoryImageResult MemoryImageResult;
		MemoryImage.Flatten(MemoryImageResult, true);

		LocalContentSize = MemoryImageResult.Bytes.Num();
		check(LocalContentSize > 0u);
		LocalContentMemory = FMemory::Malloc(LocalContentSize);
		FMemory::Memcpy(LocalContentMemory, MemoryImageResult.Bytes.GetData(), LocalContentSize);
		MemoryImageResult.ApplyPatches(LocalContentMemory);
	}

	DestroyContent();

	if (LocalContentMemory)
	{
		PointerTable = LocalPointerTable;
		Content = static_cast<FShaderMapContent*>(LocalContentMemory);
		FrozenContentSize = LocalContentSize;
		NumFrozenShaders = Content->GetNumShaders();

		INC_DWORD_STAT_BY(STAT_Shaders_ShaderMemory, FrozenContentSize);
		INC_DWORD_STAT_BY(STAT_Shaders_NumShadersLoaded, NumFrozenShaders);
	}
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

bool FShaderMapBase::Serialize(FArchive& Ar, bool bInlineShaderResources, bool bLoadedByCookedMaterial, bool bInlineShaderCode)
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
		bShareCode = !bInlineShaderCode && FShaderLibraryCooker::IsShaderLibraryEnabled() && Ar.IsCooking();
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
			FShaderLibraryCooker::AddShaderCode(GetShaderPlatform(), Code, GetAssociatedAssets());
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
				// do not warn when running -nullrhi (the resource cannot be created as the shader library will not be uninitialized),
				// also do not warn for shader platforms other than current (if the game targets more than one RHI)
				if (FApp::CanEverRender() && GetShaderPlatform() == GMaxRHIShaderPlatform)
				{
					UE_LOG(LogShaders, Error, TEXT("Missing shader resource for hash '%s' for shader platform %d in the shader library"), *ResourceHash.ToString(), GetShaderPlatform());
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

FString FShaderMapBase::ToString() const
{
	TStringBuilder<32000> String;
	{
		FMemoryToStringContext Context;
		Context.PrevPointerTable = PointerTable;
		Context.String = &String;

		FPlatformTypeLayoutParameters LayoutParams;
		LayoutParams.InitializeForCurrent();

		ContentTypeLayout.ToStringFunc(Content, ContentTypeLayout, LayoutParams, Context);
	}

	if (Code)
	{
		Code->ToString(String);
	}

	return String.ToString();
}

void FShaderMapBase::DestroyContent()
{
	if (Content)
	{
		DEC_DWORD_STAT_BY(STAT_Shaders_ShaderMemory, FrozenContentSize);
		DEC_DWORD_STAT_BY(STAT_Shaders_NumShadersLoaded, NumFrozenShaders);

		InternalDeleteObjectFromLayout(Content, ContentTypeLayout, PointerTable, FrozenContentSize > 0u);
		if (FrozenContentSize > 0u)
		{
			FMemory::Free(Content);
		}

		FrozenContentSize = 0u;
		NumFrozenShaders = 0u;
		Content = nullptr;
	}
}

static uint16 MakeShaderHash(const FHashedName& TypeName, int32 PermutationId)
{
	return (uint16)CityHash128to64({ TypeName.GetHash(), (uint64)PermutationId });
}

FShader* FShaderMapContent::GetShader(const FHashedName& TypeName, int32 PermutationId) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FShaderMapContent::GetShader);
	const uint16 Hash = MakeShaderHash(TypeName, PermutationId);
	const FHashedName* RESTRICT LocalShaderTypes = ShaderTypes.GetData();
	const int32* RESTRICT LocalShaderPermutations = ShaderPermutations.GetData();
	const uint32* RESTRICT LocalNextHashIndices = ShaderHash.GetNextIndices();
	const uint32 NumShaders = Shaders.Num();

	for (uint32 Index = ShaderHash.First(Hash); ShaderHash.IsValid(Index); Index = LocalNextHashIndices[Index])
	{
		checkSlow(Index < NumShaders);
		if (LocalShaderTypes[Index] == TypeName && LocalShaderPermutations[Index] == PermutationId)
		{
			return Shaders[Index].GetChecked();
		}
	}

	return nullptr;
}

void FShaderMapContent::AddShader(const FHashedName& TypeName, int32 PermutationId, FShader* Shader)
{
	check(!Shader->IsFrozen());
	checkSlow(!HasShader(TypeName, PermutationId));

	const uint16 Hash = MakeShaderHash(TypeName, PermutationId);
	const int32 Index = Shaders.Add(Shader);
	ShaderTypes.Add(TypeName);
	ShaderPermutations.Add(PermutationId);
	check(ShaderTypes.Num() == Shaders.Num());
	check(ShaderPermutations.Num() == Shaders.Num());
	ShaderHash.Add(Hash, Index);
}

FShader* FShaderMapContent::FindOrAddShader(const FHashedName& TypeName, int32 PermutationId, FShader* Shader)
{
	check(!Shader->IsFrozen());

	const uint16 Hash = MakeShaderHash(TypeName, PermutationId);
	for (uint32 Index = ShaderHash.First(Hash); ShaderHash.IsValid(Index); Index = ShaderHash.Next(Index))
	{
		if (ShaderTypes[Index] == TypeName && ShaderPermutations[Index] == PermutationId)
		{
			DeleteObjectFromLayout(Shader);
			return Shaders[Index].GetChecked();
		}
	}

	const int32 Index = Shaders.Add(Shader);
	ShaderHash.Add(Hash, Index);
	ShaderTypes.Add(TypeName);
	ShaderPermutations.Add(PermutationId);
	check(ShaderTypes.Num() == Shaders.Num());
	check(ShaderPermutations.Num() == Shaders.Num());
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
void FShaderMapContent::RemoveShaderTypePermutaion(const FHashedName& TypeName, int32 PermutationId)
{
	const uint16 Hash = MakeShaderHash(TypeName, PermutationId);

	for (uint32 Index = ShaderHash.First(Hash); ShaderHash.IsValid(Index); Index = ShaderHash.Next(Index))
	{
		FShader* Shader = Shaders[Index].GetChecked();
		if (ShaderTypes[Index] == TypeName && ShaderPermutations[Index] == PermutationId)
		{
			DeleteObjectFromLayout(Shader);

			// Replace the shader we're removing with the last shader in the list
			Shaders.RemoveAtSwap(Index, 1, false);
			ShaderTypes.RemoveAtSwap(Index, 1, false);
			ShaderPermutations.RemoveAtSwap(Index, 1, false);
			check(ShaderTypes.Num() == Shaders.Num());
			check(ShaderPermutations.Num() == Shaders.Num());
			ShaderHash.Remove(Hash, Index);

			// SwapIndex is the old index of the shader at the end of the list, that's now been moved to replace the current shader
			const int32 SwapIndex = Shaders.Num();
			if (Index != SwapIndex)
			{
				// We need to update the hash table to reflect shader previously at SwapIndex being moved to Index
				// Here we construct the hash from values at Index, since type/permutation have already been moved
				const uint16 SwapHash = MakeShaderHash(ShaderTypes[Index], ShaderPermutations[Index]);
				ShaderHash.Remove(SwapHash, SwapIndex);
				ShaderHash.Add(SwapHash, Index);
			}

			break;
		}
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
	for (int32 ShaderIndex = 0; ShaderIndex < Shaders.Num(); ++ShaderIndex)
	{
		FShader* Shader = Shaders[ShaderIndex].GetChecked();
		const FShaderId ShaderId(
			Shader->GetType(InShaderMap.GetPointerTable()),
			InMaterialShaderMapHash,
			FHashedName(),
			Shader->GetVertexFactoryType(InShaderMap.GetPointerTable()),
			ShaderPermutations[ShaderIndex],
			GetShaderPlatform());

		OutShaders.Add(ShaderId, TShaderRef<FShader>(Shader, InShaderMap));
	}

	for (const FShaderPipeline* ShaderPipeline : ShaderPipelines)
	{
		for (uint32 Frequency = 0u; Frequency < SF_NumGraphicsFrequencies; ++Frequency)
		{
			FShader* Shader = ShaderPipeline->Shaders[Frequency].Get();
			if (Shader)
			{
				const FShaderId ShaderId(
					Shader->GetType(InShaderMap.GetPointerTable()),
					InMaterialShaderMapHash,
					ShaderPipeline->TypeName,
					Shader->GetVertexFactoryType(InShaderMap.GetPointerTable()),
					ShaderPipeline->PermutationIds[Frequency],
					GetShaderPlatform());
				OutShaders.Add(ShaderId, TShaderRef<FShader>(Shader, InShaderMap));
			}
		}
	}
}

void FShaderMapContent::GetShaderList(const FShaderMapBase& InShaderMap, TMap<FHashedName, TShaderRef<FShader>>& OutShaders) const
{
	for (int32 ShaderIndex = 0; ShaderIndex < Shaders.Num(); ++ShaderIndex)
	{
		FShader* Shader = Shaders[ShaderIndex].GetChecked();
		OutShaders.Add(ShaderTypes[ShaderIndex], TShaderRef<FShader>(Shader, InShaderMap));
	}

	for (const FShaderPipeline* ShaderPipeline : ShaderPipelines)
	{
		for (const TShaderRef<FShader>& Shader : ShaderPipeline->GetShaders(InShaderMap))
		{
			OutShaders.Add(Shader.GetType()->GetHashedName(), Shader);
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

void FShaderMapContent::Validate(const FShaderMapBase& InShaderMap) const
{
	for (const FShader* Shader : Shaders)
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
	for (int32 ShaderIndex = 0; ShaderIndex < Shaders.Num(); ++ShaderIndex)
	{
		const int32 PermutationId = ShaderPermutations[ShaderIndex];
		Shaders[ShaderIndex]->SaveShaderStableKeys(InShaderMap.GetPointerTable(), TargetShaderPlatform, PermutationId, SaveKeyVal);
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

struct FSortedShaderEntry
{
	FHashedName TypeName;
	int32 PermutationId;
	int32 Index;

	friend bool operator<(const FSortedShaderEntry& Lhs, const FSortedShaderEntry& Rhs)
	{
		if (Lhs.TypeName != Rhs.TypeName)
		{
			return Lhs.TypeName < Rhs.TypeName;
		}
		return Lhs.PermutationId < Rhs.PermutationId;
	}
};

void FShaderMapContent::Finalize(const FShaderMapResourceCode* Code)
{
	check(Code);

	for (FShader* Shader : Shaders)
	{
		Shader->Finalize(Code);
	}

	for (FShaderPipeline* Pipeline : ShaderPipelines)
	{
		Pipeline->Finalize(Code);
	}

	// Sort the shaders by type/permutation, so they are consistently ordered
	TArray<FSortedShaderEntry> SortedEntries;
	SortedEntries.Empty(Shaders.Num());
	for (int32 ShaderIndex = 0; ShaderIndex < Shaders.Num(); ++ShaderIndex)
	{
		FSortedShaderEntry& Entry = SortedEntries.AddDefaulted_GetRef();
		Entry.TypeName = ShaderTypes[ShaderIndex];
		Entry.PermutationId = ShaderPermutations[ShaderIndex];
		Entry.Index = ShaderIndex;
	}
	SortedEntries.Sort();

	// Choose a good hash size based on the number of shaders we have
	const uint32 HashSize = FMath::RoundUpToPowerOfTwo(FMath::Max((Shaders.Num() * 3) / 2, 1));
	FMemoryImageHashTable NewShaderHash(HashSize, Shaders.Num());
	TMemoryImageArray<TMemoryImagePtr<FShader>> NewShaders;
	NewShaders.Empty(Shaders.Num());
	ShaderTypes.Empty(Shaders.Num());
	ShaderPermutations.Empty(Shaders.Num());

	for (int32 SortedIndex = 0; SortedIndex < SortedEntries.Num(); ++SortedIndex)
	{
		const FSortedShaderEntry& SortedEntry = SortedEntries[SortedIndex];

		const uint16 Key = MakeShaderHash(SortedEntry.TypeName, SortedEntry.PermutationId);
		NewShaders.Add(Shaders[SortedEntry.Index]);
		ShaderTypes.Add(SortedEntry.TypeName);
		ShaderPermutations.Add(SortedEntry.PermutationId);
		NewShaderHash.Add(Key, SortedIndex);
	}

	Shaders = MoveTemp(NewShaders);
	ShaderHash = MoveTemp(NewShaderHash);
}

void FShaderMapContent::UpdateHash(FSHA1& Hasher) const
{
	for (int32 ShaderIndex = 0; ShaderIndex < Shaders.Num(); ++ShaderIndex)
	{
		const uint64 TypeNameHash = ShaderTypes[ShaderIndex].GetHash();
		const int32 PermutationId = ShaderPermutations[ShaderIndex];
		Hasher.Update((uint8*)&TypeNameHash, sizeof(TypeNameHash));
		Hasher.Update((uint8*)&PermutationId, sizeof(PermutationId));
	}

	for (const FShaderPipeline* Pipeline : GetShaderPipelines())
	{
		const uint64 TypeNameHash = Pipeline->TypeName.GetHash();
		Hasher.Update((uint8*)&TypeNameHash, sizeof(TypeNameHash));
	}
}

void FShaderMapContent::Empty(const FPointerTableBase* PointerTable)
{
	EmptyShaderPipelines(PointerTable);
	for (int32 i = 0; i < Shaders.Num(); ++i)
	{
		TMemoryImagePtr<FShader>& Shader = Shaders[i];
		// It's possible that frozen shader map may have certain shaders embedded that are compiled out of the target build
		// In this case, we won't be able to find the shader type, and SafeDelete() will crash, as DeleteObjectFromLayout() relies on getting FTypeLayoutDesc from the shader type
		// In the future, we should ensure that we're not including these shaders at all, but for now it should be OK to skip them
		if (Shader->GetType(PointerTable))
		{
			Shader.SafeDelete(PointerTable);
		}
		else
		{
			// If we can't find the type, and the shadermap isn't frozen, then something has gone wrong
			checkf(Shader.IsFrozen(), TEXT("Shader type %016X is missing, but shader isn't frozen"), ShaderTypes[i].GetHash());
		}
	}
	Shaders.Empty();
	ShaderTypes.Empty();
	ShaderPermutations.Empty();
	ShaderHash.Clear();
}

void FShaderMapContent::EmptyShaderPipelines(const FPointerTableBase* PointerTable)
{
	for (TMemoryImagePtr<FShaderPipeline>& Pipeline : ShaderPipelines)
	{
		Pipeline.SafeDelete(PointerTable);
	}
	ShaderPipelines.Empty();
}