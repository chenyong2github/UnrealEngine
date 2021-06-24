// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GlobalShader.cpp: Global shader implementation.
=============================================================================*/

#include "GlobalShader.h"

#include "Misc/MessageDialog.h"
#include "HAL/FileManager.h"
#include "Serialization/NameAsStringProxyArchive.h"
#include "Interfaces/ITargetPlatform.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/App.h"
#include "StaticBoundShaderState.h"


/** The global shader map. */
FGlobalShaderMap* GGlobalShaderMap[SP_NumPlatforms] = {};

IMPLEMENT_TYPE_LAYOUT(FGlobalShader);
IMPLEMENT_TYPE_LAYOUT(FGlobalShaderMapContent);

IMPLEMENT_SHADER_TYPE(,FNULLPS,TEXT("/Engine/Private/NullPixelShader.usf"),TEXT("Main"),SF_Pixel);

/** Used to identify the global shader map in compile queues. */
const int32 GlobalShaderMapId = 0;

FGlobalShaderMapId::FGlobalShaderMapId(EShaderPlatform Platform, const ITargetPlatform* TargetPlatform)
{
	LayoutParams.InitializeForPlatform(TargetPlatform);
	const EShaderPermutationFlags PermutationFlags = GetShaderPermutationFlags(LayoutParams);
	TArray<FShaderType*> ShaderTypes;
	TArray<const FShaderPipelineType*> ShaderPipelineTypes;

	for (TLinkedList<FShaderType*>::TIterator ShaderTypeIt(FShaderType::GetTypeList()); ShaderTypeIt; ShaderTypeIt.Next())
	{
		FGlobalShaderType* GlobalShaderType = ShaderTypeIt->GetGlobalShaderType();
		if (!GlobalShaderType)
		{
			continue;
		}

		bool bList = false;
		for (int32 PermutationId = 0; PermutationId < GlobalShaderType->GetPermutationCount(); PermutationId++)
		{
			if (GlobalShaderType->ShouldCompilePermutation(Platform, PermutationId, PermutationFlags))
			{
				bList = true;
				break;
			}
		}

		if (bList)
		{
			ShaderTypes.Add(GlobalShaderType);
		}
	}

	for (TLinkedList<FShaderPipelineType*>::TIterator ShaderPipelineIt(FShaderPipelineType::GetTypeList()); ShaderPipelineIt; ShaderPipelineIt.Next())
	{
		const FShaderPipelineType* Pipeline = *ShaderPipelineIt;
		if (Pipeline->IsGlobalTypePipeline())
		{
			int32 NumStagesNeeded = 0;
			auto& StageTypes = Pipeline->GetStages();
			for (const FShaderType* Shader : StageTypes)
			{
				const FGlobalShaderType* GlobalShaderType = Shader->GetGlobalShaderType();
				if (GlobalShaderType->ShouldCompilePermutation(Platform, /* PermutationId = */ 0, PermutationFlags))
				{
					++NumStagesNeeded;
				}
				else
				{
					break;
				}
			}

			if (NumStagesNeeded == StageTypes.Num())
			{
				ShaderPipelineTypes.Add(Pipeline);
			}
		}
	}

	// Individual shader dependencies
	ShaderTypes.Sort(FCompareShaderTypes());
	for (int32 TypeIndex = 0; TypeIndex < ShaderTypes.Num(); TypeIndex++)
	{
		FShaderType* ShaderType = ShaderTypes[TypeIndex];
		FShaderTypeDependency Dependency(ShaderType, Platform);

		const TCHAR* ShaderFilename = ShaderType->GetShaderFilename();

		TArray<FShaderTypeDependency>& Dependencies = ShaderFilenameToDependenciesMap.FindOrAdd(ShaderFilename);

		Dependencies.Add(Dependency);
	}

	// Shader pipeline dependencies
	ShaderPipelineTypes.Sort(FCompareShaderPipelineNameTypes());
	for (int32 TypeIndex = 0; TypeIndex < ShaderPipelineTypes.Num(); TypeIndex++)
	{
		const FShaderPipelineType* Pipeline = ShaderPipelineTypes[TypeIndex];
		FShaderPipelineTypeDependency Dependency(Pipeline, Platform);
		ShaderPipelineTypeDependencies.Add(Dependency);
	}
}

void FGlobalShaderMapId::AppendKeyString(FString& KeyString, const TArray<FShaderTypeDependency>& Dependencies) const
{
#if WITH_EDITOR

	{
		const FSHAHash LayoutHash = Freeze::HashLayout(StaticGetTypeLayoutDesc<FGlobalShaderMapContent>(), LayoutParams);
		KeyString += TEXT("_");
		KeyString += LayoutHash.ToString();
		KeyString += TEXT("_");
	}

	TMap<const TCHAR*,FCachedUniformBufferDeclaration> ReferencedUniformBuffers;

	for (int32 ShaderIndex = 0; ShaderIndex < Dependencies.Num(); ShaderIndex++)
	{
		const FShaderTypeDependency& ShaderTypeDependency = Dependencies[ShaderIndex];
		const FShaderType* ShaderType = FindShaderTypeByName(ShaderTypeDependency.ShaderTypeName);

		KeyString += TEXT("_");
		KeyString += ShaderType->GetName();
		KeyString += FString::Printf(TEXT("%i"), ShaderTypeDependency.PermutationId);

		// Add the type's source hash so that we can invalidate cached shaders when .usf changes are made
		KeyString += ShaderTypeDependency.SourceHash.ToString();

		if (const FShaderParametersMetadata* ParameterStructMetadata = ShaderType->GetRootParametersMetadata())
		{
			KeyString += FString::Printf(TEXT("%08x"), ParameterStructMetadata->GetLayoutHash());
		}

		// Add the serialization history to the key string so that we can detect changes to global shader serialization without a corresponding .usf change
		const FSHAHash LayoutHash = Freeze::HashLayout(ShaderType->GetLayout(), LayoutParams);
		KeyString += LayoutHash.ToString();

		const TMap<const TCHAR*, FCachedUniformBufferDeclaration>& ReferencedUniformBufferStructsCache = ShaderType->GetReferencedUniformBufferStructsCache();

		// Gather referenced uniform buffers
		for (TMap<const TCHAR*, FCachedUniformBufferDeclaration>::TConstIterator It(ReferencedUniformBufferStructsCache); It; ++It)
		{
			ReferencedUniformBuffers.Add(It.Key(), It.Value());
		}
	}

	for (int32 Index = 0; Index < ShaderPipelineTypeDependencies.Num(); ++Index)
	{
		const FShaderPipelineTypeDependency& Dependency = ShaderPipelineTypeDependencies[Index];
		const FShaderPipelineType* ShaderPipelineType = FShaderPipelineType::GetShaderPipelineTypeByName(Dependency.ShaderPipelineTypeName);

		KeyString += TEXT("_");
		KeyString += ShaderPipelineType->GetName();

		// Add the type's source hash so that we can invalidate cached shaders when .usf changes are made
		KeyString += Dependency.StagesSourceHash.ToString();

		for (const FShaderType* ShaderType : ShaderPipelineType->GetStages())
		{
			if (const FShaderParametersMetadata* ParameterStructMetadata = ShaderType->GetRootParametersMetadata())
			{
				KeyString += FString::Printf(TEXT("%08x"), ParameterStructMetadata->GetLayoutHash());
			}

			const TMap<const TCHAR*, FCachedUniformBufferDeclaration>& ReferencedUniformBufferStructsCache = ShaderType->GetReferencedUniformBufferStructsCache();

			// Gather referenced uniform buffers
			for (TMap<const TCHAR*, FCachedUniformBufferDeclaration>::TConstIterator It(ReferencedUniformBufferStructsCache); It; ++It)
			{
				ReferencedUniformBuffers.Add(It.Key(), It.Value());
			}
		}
	}

	{
		TArray<uint8> TempData;
		FSerializationHistory SerializationHistory;
		FMemoryWriter Ar(TempData, true);
		FShaderSaveArchive SaveArchive(Ar, SerializationHistory);

		// Save uniform buffer member info so we can detect when layout has changed
		SerializeUniformBufferInfo(SaveArchive, ReferencedUniformBuffers);

		SerializationHistory.AppendKeyString(KeyString);
	}
#endif // WITH_EDITOR
}

FGlobalShader::FGlobalShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
:	FShader(Initializer)
{}

void BackupGlobalShaderMap(FGlobalShaderBackupData& OutGlobalShaderBackup)
{
#if 0
	for (int32 i = (int32)ERHIFeatureLevel::ES2_REMOVED; i < (int32)ERHIFeatureLevel::Num; ++i)
	{
		EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform((ERHIFeatureLevel::Type)i);
		if (ShaderPlatform < EShaderPlatform::SP_NumPlatforms && GGlobalShaderMap[ShaderPlatform] != nullptr)
		{
			TUniquePtr<TArray<uint8>> ShaderData = MakeUnique<TArray<uint8>>();
			FMemoryWriter Ar(*ShaderData);
			GGlobalShaderMap[ShaderPlatform]->SerializeInline(Ar, true, true, false, nullptr);
			//GGlobalShaderMap[ShaderPlatform]->RegisterSerializedShaders(false);
			GGlobalShaderMap[ShaderPlatform]->Empty();
			OutGlobalShaderBackup.FeatureLevelShaderData[i] = MoveTemp(ShaderData);
		}
	}

	// Remove cached references to global shaders
	for (TLinkedList<FGlobalBoundShaderStateResource*>::TIterator It(FGlobalBoundShaderStateResource::GetGlobalBoundShaderStateList()); It; It.Next())
	{
		BeginUpdateResourceRHI(*It);
	}
#endif
	check(0);
}

void RestoreGlobalShaderMap(const FGlobalShaderBackupData& GlobalShaderBackup)
{
#if 0
	for (int32 i = (int32)ERHIFeatureLevel::ES2_REMOVED; i < (int32)ERHIFeatureLevel::Num; ++i)
	{
		EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform((ERHIFeatureLevel::Type)i);		
		if (GlobalShaderBackup.FeatureLevelShaderData[i] != nullptr
			&& ShaderPlatform < EShaderPlatform::SP_NumPlatforms
			&& GGlobalShaderMap[ShaderPlatform] != nullptr)
		{
			FMemoryReader Ar(*GlobalShaderBackup.FeatureLevelShaderData[i]);
			GGlobalShaderMap[ShaderPlatform]->SerializeInline(Ar, true, true, false, nullptr);
			//GGlobalShaderMap[ShaderPlatform]->RegisterSerializedShaders(false);
		}
	}
#endif
	check(0);
}


FGlobalShaderMap* GetGlobalShaderMap(EShaderPlatform Platform)
{
	// If the global shader map hasn't been created yet
	check(GGlobalShaderMap[Platform]);
	return GGlobalShaderMap[Platform];
}

FGlobalShaderMapSection* FGlobalShaderMapSection::CreateFromArchive(FArchive& Ar)
{
	FGlobalShaderMapSection* Section = new FGlobalShaderMapSection();
	if (Section->Serialize(Ar))
	{
		return Section;
	}
	delete Section;
	return nullptr;
}

bool FGlobalShaderMapSection::Serialize(FArchive& Ar)
{
	return Super::Serialize(Ar, true, false);
}

TShaderRef<FShader> FGlobalShaderMapSection::GetShader(FShaderType* ShaderType, int32 PermutationId) const
{
	FShader* Shader = GetContent()->GetShader(ShaderType, PermutationId);
	return Shader ? TShaderRef<FShader>(Shader, *this) : TShaderRef<FShader>();
}

FShaderPipelineRef FGlobalShaderMapSection::GetShaderPipeline(const FShaderPipelineType* PipelineType) const
{
	FShaderPipeline* Pipeline = GetContent()->GetShaderPipeline(PipelineType);
	return Pipeline ? FShaderPipelineRef(Pipeline, *this) : FShaderPipelineRef();
}

FGlobalShaderMap::FGlobalShaderMap(EShaderPlatform InPlatform)
	: Platform(InPlatform)
{
}

FGlobalShaderMap::~FGlobalShaderMap()
{
	ReleaseAllSections();
}

TShaderRef<FShader> FGlobalShaderMap::GetShader(FShaderType* ShaderType, int32 PermutationId) const
{
	FGlobalShaderMapSection* const* Section = SectionMap.Find(ShaderType->GetHashedShaderFilename());
	return Section ? (*Section)->GetShader(ShaderType, PermutationId) : TShaderRef<FShader>();
}

FShaderPipelineRef FGlobalShaderMap::GetShaderPipeline(const FShaderPipelineType* ShaderPipelineType) const
{
	FGlobalShaderMapSection* const* Section = SectionMap.Find(ShaderPipelineType->GetHashedPrimaryShaderFilename());
	return Section ? (*Section)->GetShaderPipeline(ShaderPipelineType) : FShaderPipelineRef();
}

void FGlobalShaderMap::BeginCreateAllShaders()
{
	for (const auto& It : SectionMap)
	{
		It.Value->GetResource()->BeginCreateAllShaders();
	}
}

#if WITH_EDITOR
void FGlobalShaderMap::GetOutdatedTypes(TArray<const FShaderType*>& OutdatedShaderTypes, TArray<const FShaderPipelineType*>& OutdatedShaderPipelineTypes, TArray<const FVertexFactoryType*>& OutdatedFactoryTypes) const
{
	for (const auto& It : SectionMap)
	{
		It.Value->GetOutdatedTypes(OutdatedShaderTypes, OutdatedShaderPipelineTypes, OutdatedFactoryTypes);
	}
}

void FGlobalShaderMap::SaveShaderStableKeys(EShaderPlatform TargetShaderPlatform)
{
	FStableShaderKeyAndValue SaveKeyVal;
	for (const auto& It : SectionMap)
	{
		It.Value->SaveShaderStableKeys(TargetShaderPlatform, SaveKeyVal);
	}
}

#endif // WITH_EDITOR

bool FGlobalShaderMap::IsEmpty() const
{
	for (const auto& It : SectionMap)
	{
		if (!It.Value->GetContent()->IsEmpty())
		{
			return false;
		}
	}
	return true;
}

bool FGlobalShaderMap::IsComplete(const ITargetPlatform* TargetPlatform) const
{
	// TODO: store these in the shadermap before it's start to be compiled?
	FPlatformTypeLayoutParameters LayoutParams;
	LayoutParams.InitializeForPlatform(TargetPlatform);
	const EShaderPermutationFlags PermutationFlags = GetShaderPermutationFlags(LayoutParams);

	FGlobalShaderMapId ShaderMapId(Platform, TargetPlatform);

	// traverse all global shader types
	for (TLinkedList<FShaderType*>::TIterator ShaderTypeIt(FShaderType::GetTypeList()); ShaderTypeIt; ShaderTypeIt.Next())
	{
		FGlobalShaderType* GlobalShaderType = ShaderTypeIt->GetGlobalShaderType();
		if (!GlobalShaderType)
		{
			continue;
		}

		int32 PermutationCountToCompile = 0;
		for (int32 PermutationId = 0; PermutationId < GlobalShaderType->GetPermutationCount(); PermutationId++)
		{
			if (GlobalShaderType->ShouldCompilePermutation(Platform, PermutationId, PermutationFlags)
				&& !HasShader(GlobalShaderType, PermutationId))
			{
				return false;
			}
		}
	}

	// traverse all pipelines. Note that there's no ShouldCompile call for them. Materials instead test individual stages, but it leads to another problems
	// like including the standalone types even if they are not going to be used. This code follows VerifyGlobalShaders() logic that includes all global pipelines unconditionally.
	for (TLinkedList<FShaderPipelineType*>::TIterator ShaderPipelineIt(FShaderPipelineType::GetTypeList()); ShaderPipelineIt; ShaderPipelineIt.Next())
	{
		const FShaderPipelineType* Pipeline = *ShaderPipelineIt;
		if (Pipeline->IsGlobalTypePipeline() && !HasShaderPipeline(Pipeline))
		{
			return false;
		}
	}

	return true;
}

void FGlobalShaderMap::Empty()
{
	for (const auto& It : SectionMap)
	{
		It.Value->GetMutableContent()->Empty(&It.Value->GetPointerTable());
	}
}

void FGlobalShaderMap::ReleaseAllSections()
{
	for (auto& It : SectionMap)
	{
		delete It.Value;
	}
	SectionMap.Empty();
}

FShader* FGlobalShaderMap::FindOrAddShader(const FShaderType* ShaderType, int32 PermutationId, FShader* Shader)
{
	const FHashedName HashedFilename(ShaderType->GetHashedShaderFilename());
	FGlobalShaderMapSection*& Section = SectionMap.FindOrAdd(HashedFilename);
	if (!Section)
	{
		Section = new FGlobalShaderMapSection(Platform, HashedFilename);
	}
	return Section->GetMutableContent()->FindOrAddShader(ShaderType->GetHashedName(), PermutationId, Shader);
}

FShaderPipeline* FGlobalShaderMap::FindOrAddShaderPipeline(const FShaderPipelineType* ShaderPipelineType, FShaderPipeline* ShaderPipeline)
{
	FGlobalShaderMapSection*& Section = SectionMap.FindOrAdd(ShaderPipelineType->GetHashedPrimaryShaderFilename());
	if (!Section)
	{
		Section = new FGlobalShaderMapSection(Platform, ShaderPipelineType->GetHashedPrimaryShaderFilename());
	}
	return Section->GetMutableContent()->FindOrAddShaderPipeline(ShaderPipeline);
}

void FGlobalShaderMap::RemoveShaderTypePermutaion(const FShaderType* Type, int32 PermutationId)
{
	FGlobalShaderMapSection** Section = SectionMap.Find(Type->GetHashedShaderFilename());
	if (Section)
	{
		(*Section)->GetMutableContent()->RemoveShaderTypePermutaion(Type->GetHashedName(), PermutationId);
	}
}

void FGlobalShaderMap::RemoveShaderPipelineType(const FShaderPipelineType* ShaderPipelineType)
{
	FGlobalShaderMapSection** Section = SectionMap.Find(ShaderPipelineType->GetHashedPrimaryShaderFilename());
	if (Section)
	{
		(*Section)->GetMutableContent()->RemoveShaderPipelineType(ShaderPipelineType);
	}
}

void FGlobalShaderMap::AddSection(FGlobalShaderMapSection* InSection)
{
	check(InSection);
	const FGlobalShaderMapContent* Content = InSection->GetContent();
	const FHashedName& HashedFilename = Content->HashedSourceFilename;

	SectionMap.Add(HashedFilename, InSection);
}

FGlobalShaderMapSection* FGlobalShaderMap::FindSection(const FHashedName& HashedShaderFilename)
{
	FGlobalShaderMapSection* const* Section = SectionMap.Find(HashedShaderFilename);
	return Section ? *Section : nullptr;
}

FGlobalShaderMapSection* FGlobalShaderMap::FindOrAddSection(const FShaderType* ShaderType)
{
	const FHashedName HashedFilename(ShaderType->GetHashedShaderFilename());
	FGlobalShaderMapSection* Section = FindSection(HashedFilename);
	if(!Section)
	{
		Section = new FGlobalShaderMapSection(Platform, HashedFilename);
		AddSection(Section);
	}
	return Section;
}

void FGlobalShaderMap::SaveToGlobalArchive(FArchive& Ar)
{
	int32 NumSections = SectionMap.Num();
	Ar << NumSections;

	for (const auto& It : SectionMap)
	{
		It.Value->Serialize(Ar);
	}
}

void FGlobalShaderMap::LoadFromGlobalArchive(FArchive& Ar)
{
	int32 NumSections = 0;
	Ar << NumSections;

	for (int32 i = 0; i < NumSections; ++i)
	{
		FGlobalShaderMapSection* Section = FGlobalShaderMapSection::CreateFromArchive(Ar);
		if (Section)
		{
			AddSection(Section);
		}
	}
}
