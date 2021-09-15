// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraScript.h"
#include "Modules/ModuleManager.h"
#include "NiagaraScriptSourceBase.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitter.h"
#include "UObject/Package.h"
#include "NiagaraModule.h"
#include "NiagaraCustomVersion.h"
#include "NiagaraShaderCompilationManager.h"
#include "Serialization/MemoryReader.h"
#include "Misc/SecureHash.h"

#include "ProfilingDebugging/CookStats.h"
#include "Stats/Stats.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "UObject/EditorObjectVersion.h"
#include "UObject/ReleaseObjectVersion.h"
#include "NiagaraDataInterfaceSkeletalMesh.h"
#include "NiagaraDataInterfaceStaticMesh.h"
#if WITH_EDITOR
	#include "DerivedDataCacheInterface.h"
	#include "Interfaces/ITargetPlatform.h"
	#include "NiagaraSettings.h"


	// This is a version string that mimics the old versioning scheme. In case of merge conflicts with DDC versions,
	// you MUST generate a new GUID and set this new version. If you want to bump this version, generate a new guid
	// using VS->Tools->Create GUID
	#define NIAGARASCRIPT_DERIVEDDATA_VER		TEXT("179023FDDDD444DE97F61296909C2990")
#endif

#include "UObject/FortniteMainBranchObjectVersion.h"
#include "UObject/RenderingObjectVersion.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

#include "VectorVM.h"
#include "NiagaraSimulationStageBase.h"
#include "Async/Async.h"

#if ENABLE_COOK_STATS
namespace NiagaraScriptCookStats
{
	FCookStats::FDDCResourceUsageStats UsageStats;
	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
	{
		UsageStats.LogStats(AddStat, TEXT("NiagaraScript.Usage"), TEXT(""));
	});
}
#endif

int32 GNiagaraDumpKeyGen = 0;
static FAutoConsoleVariableRef CVarNiagaraDumpKeyGen(
	TEXT("fx.DumpGraphKeyGen"),
	GNiagaraDumpKeyGen,
	TEXT("If > 0 the key generation will be dumped to the log. \n"),
	ECVF_Default
);

int32 GNiagaraForceSafeScriptAttributeTrim = 0;
static FAutoConsoleVariableRef CVarNiagaraForceSsafeScriptAttributeTrim(
	TEXT("fx.ForceSafeScriptAttributeTrim"),
	GNiagaraForceSafeScriptAttributeTrim,
	TEXT("If > 0 attribute trimming will use a less aggressive algorithm for removing script attributes. \n"),
	ECVF_Default
);

FNiagaraScriptDebuggerInfo::FNiagaraScriptDebuggerInfo() : bWaitForGPU(false), FrameLastWriteId(-1), bWritten(false)
{
}


FNiagaraScriptDebuggerInfo::FNiagaraScriptDebuggerInfo(FName InName, ENiagaraScriptUsage InUsage, const FGuid& InUsageId) : HandleName(InName), Usage(InUsage), UsageId(InUsageId), FrameLastWriteId(-1), bWritten(false)
{
	if (InUsage == ENiagaraScriptUsage::ParticleGPUComputeScript)
	{
		bWaitForGPU = true;
	}
	else
	{
		bWaitForGPU = false;
	}
}


UNiagaraScriptSourceBase::UNiagaraScriptSourceBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}


FNiagaraVMExecutableData::FNiagaraVMExecutableData()
	: NumTempRegisters(0)
	, NumUserPtrs(0)
#if WITH_EDITORONLY_DATA
	, LastOpCount(0)
#endif
	, LastCompileStatus(ENiagaraScriptCompileStatus::NCS_Unknown)
#if WITH_EDITORONLY_DATA
	, bReadsAttributeData(false)
	, CompileTime(0.0f)
#endif
	, bReadsSignificanceIndex(false)
	, bNeedsGPUContextInit(false)
{
}

bool FNiagaraVMExecutableData::IsValid() const
{
	return LastCompileStatus != ENiagaraScriptCompileStatus::NCS_Unknown;
}

void FNiagaraVMExecutableData::Reset()
{
	*this = FNiagaraVMExecutableData();
}

void FNiagaraVMExecutableData::SerializeData(FArchive& Ar, bool bDDCData)
{
	UScriptStruct* FNiagaraVMExecutableDataType = FNiagaraVMExecutableData::StaticStruct();
	FNiagaraVMExecutableDataType->SerializeTaggedProperties(Ar, (uint8*)this, FNiagaraVMExecutableDataType, nullptr);
}

#if WITH_EDITORONLY_DATA
void FNiagaraVMExecutableData::BakeScriptLiterals(TArray<uint8>& OutLiterals) const
{
	OutLiterals.Empty();

	const auto& Variables = InternalParameters.Parameters;
	const int32 VariableCount = Variables.Num();

	int32 TotalSize = 0;
	for (int32 Index = 0; Index < VariableCount; ++Index)
	{
		TotalSize += Variables[Index].GetAllocatedSizeInBytes();
	}

	OutLiterals.AddZeroed(TotalSize);

	uint8* LiteralData = OutLiterals.GetData();

	for (int32 Index = 0; Index < VariableCount; ++Index)
	{
		const FNiagaraVariable& Variable = Variables[Index];
		const int32 VariableSize = Variable.GetAllocatedSizeInBytes();

		FMemory::Memcpy(LiteralData, Variable.GetData(), VariableSize);
		LiteralData += VariableSize;
	}
}

FVersionedNiagaraScriptData::FVersionedNiagaraScriptData()
	: ModuleUsageBitmask( (1 << (int32)ENiagaraScriptUsage::ParticleSpawnScript) | (1 << (int32)ENiagaraScriptUsage::ParticleSpawnScriptInterpolated) | (1 << (int32)ENiagaraScriptUsage::ParticleUpdateScript) | (1 << (int32)ENiagaraScriptUsage::ParticleEventScript) | (1 << (int32)ENiagaraScriptUsage::ParticleSimulationStageScript))
	, bDeprecated(false)
	, DeprecationRecommendation(nullptr)
	, bExperimental(false)
	, LibraryVisibility(ENiagaraScriptLibraryVisibility::Unexposed)
	, NumericOutputTypeSelectionMode(ENiagaraNumericOutputTypeSelectionMode::Largest)
{
}
#endif

UNiagaraScript::UNiagaraScript()
{
}

#if WITH_EDITORONLY_DATA
FVersionedNiagaraScriptData* UNiagaraScript::GetLatestScriptData()
{
	return const_cast<FVersionedNiagaraScriptData*>(const_cast<const UNiagaraScript*>(this)->GetLatestScriptData());
}

const FVersionedNiagaraScriptData* UNiagaraScript::GetLatestScriptData() const
{
	if (VersionData.Num() == 0)
	{
		return nullptr;
	}
	if (!bVersioningEnabled)
	{
		return &VersionData[0];
	}
	const FVersionedNiagaraScriptData* VersionedNiagaraScriptData = GetScriptData(ExposedVersion);
	ensureMsgf(VersionedNiagaraScriptData, TEXT("Invalid exposed version for Niagara script %s, asset might be corrupted!"), *this->GetFriendlyName());
	return VersionedNiagaraScriptData;
}

FVersionedNiagaraScriptData* UNiagaraScript::GetScriptData(const FGuid& VersionGuid)
{
	return const_cast<FVersionedNiagaraScriptData*>(const_cast<const UNiagaraScript*>(this)->GetScriptData(VersionGuid));
}

const FVersionedNiagaraScriptData* UNiagaraScript::GetScriptData(const FGuid& VersionGuid) const
{
	if (VersionData.Num() == 0)
	{
		return nullptr;
	}
	
	// check if we even need to support different versions
	if (!bVersioningEnabled)
    {
    	return &VersionData[0];
    }

	if (!VersionGuid.IsValid())
	{
		for (const FVersionedNiagaraScriptData& Data : VersionData)
		{
			if (Data.Version.VersionGuid == ExposedVersion)
			{
				return &Data;
			}
		}
		ensureMsgf(false, TEXT("Invalid exposed version for Niagara script %s, asset might be corrupted!"), *this->GetFriendlyName());
		return nullptr;
	}
	for (const FVersionedNiagaraScriptData& Data : VersionData)
	{
		if (Data.Version.VersionGuid == VersionGuid)
		{
			return &Data;
		}
	}
	return nullptr;
}

TArray<FNiagaraAssetVersion> UNiagaraScript::GetAllAvailableVersions() const
{
	TArray<FNiagaraAssetVersion> Versions;
	for (const FVersionedNiagaraScriptData& Data : VersionData)
	{
		Versions.Add(Data.Version);
	}
	return Versions;
}

FNiagaraAssetVersion UNiagaraScript::GetExposedVersion() const
{
	const FVersionedNiagaraScriptData* ScriptData = GetLatestScriptData();
	return ScriptData ? ScriptData->Version : FNiagaraAssetVersion();
}

FNiagaraAssetVersion const* UNiagaraScript::FindVersionData(const FGuid& VersionGuid) const
{
	for (const FVersionedNiagaraScriptData& Data : VersionData)
	{
		if (Data.Version.VersionGuid == VersionGuid)
		{
			return &Data.Version;
		}
	}
	return nullptr;
}

FGuid UNiagaraScript::AddNewVersion(int32 MajorVersion, int32 MinorVersion)
{
	// check preconditions
	check(MajorVersion >= 1);
	check(MajorVersion != 1 || MinorVersion != 0);

	FVersionedNiagaraScriptData NewVersionData;
	for (int i = VersionData.Num() - 1; i >= 0; i--)
	{
		FVersionedNiagaraScriptData& Data = VersionData[i];
		check(Data.Version.MajorVersion != MajorVersion || Data.Version.MinorVersion != MinorVersion); // the version should not already exist

		if (Data.Version.MajorVersion < MajorVersion || (Data.Version.MajorVersion == MajorVersion && Data.Version.MinorVersion < MinorVersion))
		{
			// copy the data
			NewVersionData = Data;

			FObjectDuplicationParameters ObjParameters(NewVersionData.Source, this);
			ObjParameters.DestClass = NewVersionData.Source->GetClass();
			NewVersionData.Source = Cast<UNiagaraScriptSourceBase>(StaticDuplicateObjectEx(ObjParameters));
			break;
		}
	}

	NewVersionData.VersionChangeDescription = FText();
	NewVersionData.Version = { MajorVersion, MinorVersion, FGuid::NewGuid() };

	VersionData.Add(NewVersionData);
	VersionData.Sort([](const FVersionedNiagaraScriptData& A, const FVersionedNiagaraScriptData& B) { return A.Version < B.Version; });

	return NewVersionData.Version.VersionGuid;
}

void UNiagaraScript::DeleteVersion(const FGuid& VersionGuid)
{
	check(VersionGuid != ExposedVersion);

	for (int i = 0; i < VersionData.Num(); i++)
	{
		FNiagaraAssetVersion& AssetVersion = VersionData[i].Version;
		if (AssetVersion.VersionGuid == VersionGuid)
		{
			check(AssetVersion.MajorVersion != 1 || AssetVersion.MinorVersion != 0);
			VersionData.RemoveAt(i);
			return;
		}
	}
}

void UNiagaraScript::ExposeVersion(const FGuid& VersionGuid)
{
	// check if the requested version exists in the data store
	for (FVersionedNiagaraScriptData& Data : VersionData)
	{
		if (Data.Version.VersionGuid == VersionGuid)
		{
			ExposedVersion = VersionGuid;
			Data.Version.bIsVisibleInVersionSelector = true;
			return;
		}
	}
}

void UNiagaraScript::EnableVersioning()
{
	if (bVersioningEnabled)
	{
		return;
	}
	
	ensure(VersionData.Num() == 1);
	bVersioningEnabled = true;	
	ExposedVersion = VersionData[0].Version.VersionGuid;
}

void UNiagaraScript::CheckVersionDataAvailable()
{
	if (VersionData.Num() > 0) {
		return;
	}

	// copy over existing data of assets that were created pre-versioning
	FVersionedNiagaraScriptData& Data = VersionData.AddDefaulted_GetRef();
	Data.Source = Source_DEPRECATED;
	Data.Keywords = Keywords_DEPRECATED;
	Data.Category = Category_DEPRECATED;
	Data.Highlights = Highlights_DEPRECATED;
	Data.Description = Description_DEPRECATED;
	Data.bDeprecated = bDeprecated_DEPRECATED;
	Data.NoteMessage = NoteMessage_DEPRECATED;
	Data.bExperimental = bExperimental_DEPRECATED;
	Data.ScriptMetaData = ScriptMetaData_DEPRECATED;
	Data.LibraryVisibility = LibraryVisibility_DEPRECATED;
	Data.ConversionUtility = ConversionUtility_DEPRECATED;
	Data.ModuleUsageBitmask = ModuleUsageBitmask_DEPRECATED;
	Data.DeprecationMessage = DeprecationMessage_DEPRECATED;
	Data.ExperimentalMessage = ExperimentalMessage_DEPRECATED;
	Data.CollapsedViewFormat = CollapsedViewFormat_DEPRECATED;
	Data.ProvidedDependencies = ProvidedDependencies_DEPRECATED;
	Data.RequiredDependencies = RequiredDependencies_DEPRECATED;
	Data.DeprecationRecommendation = DeprecationRecommendation_DEPRECATED;
	Data.NumericOutputTypeSelectionMode = NumericOutputTypeSelectionMode_DEPRECATED;

	ExposedVersion = Data.Version.VersionGuid;
}

#endif

bool FNiagaraVMExecutableDataId::IsValid() const
{
	return CompilerVersionID.IsValid();
}

void FNiagaraVMExecutableDataId::Invalidate()
{
	*this = FNiagaraVMExecutableDataId();
}

#if WITH_EDITORONLY_DATA

TArray<FString> FNiagaraVMExecutableDataId::GetAdditionalVariableStrings()
{
	TArray<FString> Vars;
	for (const FNiagaraVariableBase& Var : AdditionalVariables)
	{
		Vars.Emplace(Var.GetName().ToString() + TEXT(" ") + Var.GetType().GetName());
	}

	return Vars;
}

#endif

bool FNiagaraVMExecutableDataId::HasInterpolatedParameters() const
{
	return bInterpolatedSpawn;
}

bool FNiagaraVMExecutableDataId::RequiresPersistentIDs() const
{
	return bRequiresPersistentIDs;
}

/**
* Tests this set against another for equality, disregarding override settings.
*
* @param ReferenceSet	The set to compare against
* @return				true if the sets are equal
*/
bool FNiagaraVMExecutableDataId::operator==(const FNiagaraVMExecutableDataId& ReferenceSet) const
{
	if (CompilerVersionID != ReferenceSet.CompilerVersionID ||
		ScriptUsageType != ReferenceSet.ScriptUsageType ||
		ScriptUsageTypeID != ReferenceSet.ScriptUsageTypeID ||
#if WITH_EDITORONLY_DATA
		BaseScriptCompileHash != ReferenceSet.BaseScriptCompileHash ||
#endif
		bUsesRapidIterationParams != ReferenceSet.bUsesRapidIterationParams ||
		bInterpolatedSpawn != ReferenceSet.bInterpolatedSpawn ||
		bRequiresPersistentIDs != ReferenceSet.bRequiresPersistentIDs ||
		ScriptVersionID != ReferenceSet.ScriptVersionID)
	{
		return false;
	}

#if WITH_EDITORONLY_DATA
	if (ReferencedCompileHashes.Num() != ReferenceSet.ReferencedCompileHashes.Num())
	{
		return false;
	}

	for (int32 ReferencedHashIndex = 0; ReferencedHashIndex < ReferencedCompileHashes.Num(); ReferencedHashIndex++)
	{
		if (ReferencedCompileHashes[ReferencedHashIndex] != ReferenceSet.ReferencedCompileHashes[ReferencedHashIndex])
		{
			return false;
		}
	}

	if (AdditionalDefines.Num() != ReferenceSet.AdditionalDefines.Num())
	{
		return false;
	}


	for (int32 Idx = 0; Idx < ReferenceSet.AdditionalDefines.Num(); Idx++)
	{
		const FString& ReferenceStr = ReferenceSet.AdditionalDefines[Idx];

		if (AdditionalDefines[Idx] != ReferenceStr)
		{
			return false;
		}
	}

	if (AdditionalVariables.Num() != ReferenceSet.AdditionalVariables.Num())
	{
		return false;
	}

	for (int32 Idx = 0; Idx < ReferenceSet.AdditionalVariables.Num(); Idx++)
	{
		const FNiagaraVariableBase& ReferenceVar = ReferenceSet.AdditionalVariables[Idx];

		if (AdditionalVariables[Idx] != ReferenceVar)
		{
			return false;
		}
	}
#endif


	return true;
}

#if WITH_EDITORONLY_DATA
void FNiagaraVMExecutableDataId::AppendKeyString(FString& KeyString, const FString& Delimiter, bool bAppendObjectForDebugging) const
{
	KeyString += FString::Printf(TEXT("%d%s"), (int32)ScriptUsageType, *Delimiter);
	KeyString += ScriptUsageTypeID.ToString();
	if (bAppendObjectForDebugging)
	{
		KeyString += TEXT(" [ScriptUsageType]");
	}
	KeyString += Delimiter;

	KeyString += CompilerVersionID.ToString();
	if (bAppendObjectForDebugging)
	{
		KeyString += TEXT(" [CompilerVersionID]");
	}
	KeyString += Delimiter;

	KeyString += BaseScriptCompileHash.ToString();
	if (bAppendObjectForDebugging)
	{
		KeyString += TEXT(" [BaseScriptCompileHash]");
	}
	KeyString += Delimiter;

	if (bAppendObjectForDebugging)
	{
		KeyString += TEXT("[AdditionalDefines]") + Delimiter;
	}

	if (bUsesRapidIterationParams)
	{
		KeyString += TEXT("USESRI") + Delimiter;
	}
	else
	{
		KeyString += TEXT("NORI") + Delimiter;
	}

	for (int32 Idx = 0; Idx < AdditionalDefines.Num(); Idx++)
	{
		KeyString += AdditionalDefines[Idx];
		KeyString += Delimiter;
	}

	for (int32 Idx = 0; Idx < AdditionalVariables.Num(); Idx++)
	{
		KeyString += AdditionalVariables[Idx].GetName().ToString();
		KeyString += Delimiter;
		KeyString += AdditionalVariables[Idx].GetType().GetName();
		KeyString += Delimiter;
	}

	// Add any referenced script compile hashes to the key so that we will recompile when they are changed
	for (int32 HashIndex = 0; HashIndex < ReferencedCompileHashes.Num(); HashIndex++)
	{
		KeyString += ReferencedCompileHashes[HashIndex].ToString();

		if (bAppendObjectForDebugging && DebugReferencedObjects.Num() > HashIndex)
		{
			KeyString += TEXT(" [") + DebugReferencedObjects[HashIndex] + TEXT("]") ;
		}

		if (HashIndex < ReferencedCompileHashes.Num() - 1)
		{
			KeyString += Delimiter;
		}
	}
}

#endif

#if WITH_EDITORONLY_DATA
const FName UNiagaraScript::NiagaraCustomVersionTagName("NiagaraCustomVersion");
#endif

UNiagaraScript::UNiagaraScript(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Usage(ENiagaraScriptUsage::Function)
#if WITH_EDITORONLY_DATA
	, UsageIndex_DEPRECATED(0)
	, ModuleUsageBitmask_DEPRECATED( (1 << (int32)ENiagaraScriptUsage::ParticleSpawnScript) | (1 << (int32)ENiagaraScriptUsage::ParticleSpawnScriptInterpolated) | (1 << (int32)ENiagaraScriptUsage::ParticleUpdateScript) | (1 << (int32)ENiagaraScriptUsage::ParticleEventScript) | (1 << (int32)ENiagaraScriptUsage::ParticleSimulationStageScript))
	, LibraryVisibility_DEPRECATED(ENiagaraScriptLibraryVisibility::Unexposed)
	, NumericOutputTypeSelectionMode_DEPRECATED(ENiagaraNumericOutputTypeSelectionMode::Largest)
	, IsCooked(false)
#endif
{
#if WITH_EDITORONLY_DATA
	ScriptResource = MakeUnique<FNiagaraShaderScript>();
	ScriptResource->OnCompilationComplete().AddUniqueDynamic(this, &UNiagaraScript::RaiseOnGPUCompilationComplete);

	RapidIterationParameters.DebugName = *GetFullName();
#endif
}

UNiagaraScript::~UNiagaraScript()
{
}

#if WITH_EDITORONLY_DATA
class UNiagaraSystem* UNiagaraScript::FindRootSystem()
{
	UObject* Obj = GetOuter();
	if (UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(Obj))
	{
		Obj = Emitter->GetOuter();
	}

	if (UNiagaraSystem* Sys = Cast<UNiagaraSystem>(Obj))
	{
		return Sys;
	}

	return nullptr;
}

bool UNiagaraScript::HasIdsRequiredForShaderCaching() const
{
	return CachedScriptVMId.CompilerVersionID.IsValid() && CachedScriptVMId.BaseScriptCompileHash.IsValid();
}

FNiagaraVMExecutableDataId& UNiagaraScript::GetLastGeneratedVMId(const FGuid& VersionGuid) const
{
	if (IsVersioningEnabled())
	{
		const FVersionedNiagaraScriptData* ScriptData = GetScriptData(VersionGuid.IsValid() ? VersionGuid : ExposedVersion);
		if (ScriptData)
		{
			return ScriptData->LastGeneratedVMId;
		}
	}
	return VersionData[0].LastGeneratedVMId;
}

FString UNiagaraScript::BuildNiagaraDDCKeyString(const FNiagaraVMExecutableDataId& CompileId)
{
	enum { UE_NIAGARA_COMPILATION_DERIVEDDATA_VER = 2 };

	FString KeyString = FString::Printf(TEXT("%i_%i"),
		(int32)UE_NIAGARA_COMPILATION_DERIVEDDATA_VER, GNiagaraSkipVectorVMBackendOptimizations);

	CompileId.AppendKeyString(KeyString);
	return FDerivedDataCacheInterface::BuildCacheKey(TEXT("NiagaraScriptDerivedData"), NIAGARASCRIPT_DERIVEDDATA_VER, *KeyString);
}

FString UNiagaraScript::GetNiagaraDDCKeyString(const FGuid& ScriptVersion)
{
	return BuildNiagaraDDCKeyString(GetLastGeneratedVMId(ScriptVersion));
}

void UNiagaraScript::ComputeVMCompilationId(FNiagaraVMExecutableDataId& Id, FGuid VersionGuid) const
{
	Id = FNiagaraVMExecutableDataId();

	Id.bUsesRapidIterationParams = true;
	Id.bInterpolatedSpawn = false;
	Id.bRequiresPersistentIDs = false;
	Id.ScriptVersionID = IsVersioningEnabled() ? (VersionGuid.IsValid() ? VersionGuid : ExposedVersion) : FGuid();
	
	ENiagaraSimTarget SimTargetToBuild = ENiagaraSimTarget::CPUSim;
	// Ideally we wouldn't want to do this but rather than push the data down
	// from the emitter.  Checking all outers here to pick up simulation stages too.
	UNiagaraEmitter* OuterEmitter = GetTypedOuter<UNiagaraEmitter>();
	if (OuterEmitter != nullptr)
	{
		UNiagaraEmitter* Emitter = OuterEmitter;
		if (UNiagaraSystem* EmitterOwner = Cast<UNiagaraSystem>(Emitter->GetOuter()))
		{
			if (EmitterOwner->bBakeOutRapidIteration)
			{
				Id.bUsesRapidIterationParams = false;
			}
			if (EmitterOwner->bCompressAttributes)
			{
				Id.AdditionalDefines.Add(TEXT("CompressAttributes"));
			}

			bool TrimAttributes = EmitterOwner->bTrimAttributes;
			if (TrimAttributes)
			{
				auto TrimAttributesSupported = [=](const UNiagaraEmitter* OtherEmitter)
				{
					TArray<const UNiagaraDataInterfaceBase*> DataInterfaces;
					OtherEmitter->GraphSource->CollectDataInterfaces(DataInterfaces);

					for (const UNiagaraDataInterfaceBase* DataInterface : DataInterfaces)
					{
						if (DataInterface->HasInternalAttributeReads(OtherEmitter, Emitter))
						{
							return false;
						}
					}
					return true;
				};

				// if this emitter is being referenced by another emitter (PartilceRead) then don't worry about trimming attributes
				for (const FNiagaraEmitterHandle& EmitterHandle : EmitterOwner->GetEmitterHandles())
				{
					if (!TrimAttributesSupported(EmitterHandle.GetInstance()))
					{
						TrimAttributes = false;
						break;
					}
				}

				// disable attribute trimming if shader stages are enabled
				if (OuterEmitter->bDeprecatedShaderStagesEnabled)
				{
					TrimAttributes = false;
				}
			}

			if (TrimAttributes)
			{
				Id.AdditionalDefines.Add(GNiagaraForceSafeScriptAttributeTrim ? TEXT("TrimAttributesSafe") : TEXT("TrimAttributes"));

				TArray<FString> PreserveAttributes;

				// preserve the attributes that have been defined on the emitter directly
				for (const FString& Attribute : Emitter->AttributesToPreserve)
				{
					const FString PreserveDefine = TEXT("PreserveAttribute=") + Attribute;
					PreserveAttributes.AddUnique(PreserveDefine);
				}

				// Now preserve the attributes that have been defined on the renderers in use
				for (UNiagaraRendererProperties* RendererProperty : Emitter->GetRenderers())
				{
					for (const FNiagaraVariable& BoundAttribute : RendererProperty->GetBoundAttributes())
					{
						const FString PreserveDefine = TEXT("PreserveAttribute=") + BoundAttribute.GetName().ToString();
						PreserveAttributes.AddUnique(PreserveDefine);
					}
				}

				// We sort the keys so that it doesn't matter what order they were defined in.
				PreserveAttributes.Sort([](const FString& A, const FString& B) -> bool { return A < B; });

				Id.AdditionalDefines.Append(PreserveAttributes);
			}

			ComputeVMCompilationId_EmitterShared(Id, Emitter, EmitterOwner, ENiagaraRendererSourceDataMode::Particles);
		}

		if ((Emitter->bInterpolatedSpawning && Usage == ENiagaraScriptUsage::ParticleGPUComputeScript) ||
			(Emitter->bInterpolatedSpawning && Usage == ENiagaraScriptUsage::ParticleSpawnScript) ||
			Usage == ENiagaraScriptUsage::ParticleSpawnScriptInterpolated)
		{
			Id.bInterpolatedSpawn = true;
			Id.AdditionalDefines.Add(TEXT("InterpolatedSpawn"));
		}
		if (IsParticleScript(Usage))
		{
			SimTargetToBuild = Emitter->SimTarget;
		}
		if (Emitter->RequiresPersistentIDs())
		{
			Id.bRequiresPersistentIDs = true;
			Id.AdditionalDefines.Add(TEXT("RequiresPersistentIDs"));
		}
		if (Emitter->bLocalSpace)
		{
			Id.AdditionalDefines.Add(TEXT("Emitter.Localspace"));
		}
		if (Emitter->bDeterminism)
		{
			Id.AdditionalDefines.Add(TEXT("Emitter.Determinism"));
		}

		if (!Emitter->bBakeOutRapidIteration)
		{
			Id.bUsesRapidIterationParams = true;
		}

		if (Emitter->bSimulationStagesEnabled)
		{
			Id.AdditionalDefines.Add(TEXT("Emitter.UseSimulationStages"));

			FSHA1 HashState;
			FNiagaraCompileHashVisitor Visitor(HashState);
			for (UNiagaraSimulationStageBase* Base : Emitter->GetSimulationStages())
			{
				// bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const;
				if (Base && Base->bEnabled)
				{
					Base->AppendCompileHash(&Visitor);
				}
			}
			HashState.Final();

			TArray<uint8> DataHash;
			DataHash.AddUninitialized(FSHA1::DigestSize);
			HashState.GetHash(DataHash.GetData());

			FNiagaraCompileHash Hash(DataHash);
			Id.ReferencedCompileHashes.Add(Hash);
			Id.DebugReferencedObjects.Add(TEXT("SimulationStageHeaders"));

		}
		else if (Emitter->bDeprecatedShaderStagesEnabled)
		{
			Id.AdditionalDefines.Add(TEXT("Emitter.UseOldShaderStages"));
		}
	}

	UObject* Obj = GetOuter();
	if (UNiagaraSystem* System = Cast<UNiagaraSystem>(Obj))
	{
		if (System->bBakeOutRapidIteration)
		{
			Id.bUsesRapidIterationParams = false;
		}
		if (System->bCompressAttributes)
		{
			Id.AdditionalDefines.Add(TEXT("CompressAttributes"));
		}

		for (const FNiagaraEmitterHandle& EmitterHandle: System->GetEmitterHandles())
		{
			UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(EmitterHandle.GetInstance());
			if (Emitter && EmitterHandle.GetIsEnabled())
			{
				if (Emitter->bLocalSpace)
				{
					Id.AdditionalDefines.Add(Emitter->GetUniqueEmitterName() + TEXT(".Localspace"));
				}
				if (Emitter->bDeterminism)
				{
					Id.AdditionalDefines.Add(Emitter->GetUniqueEmitterName() + TEXT(".Determinism"));
				}

				ComputeVMCompilationId_EmitterShared(Id, Emitter, System, ENiagaraRendererSourceDataMode::Emitter);
			}
		}
	}

	switch (SimTargetToBuild)
	{
	case ENiagaraSimTarget::CPUSim:
		Id.AdditionalDefines.AddUnique(FNiagaraCompileOptions::CpuScriptDefine);
		break;
	case ENiagaraSimTarget::GPUComputeSim:
		Id.AdditionalDefines.AddUnique(FNiagaraCompileOptions::GpuScriptDefine);
		break;
	default:
		checkf(false, TEXT("Unknown sim target type!"));
	}


	// If we aren't using rapid iteration parameters, we need to bake them into the hashstate for the compile id. This
	// makes their values part of the lookup.
	if (false == Id.bUsesRapidIterationParams)
	{
		FSHA1 HashState;
		TArray<FNiagaraVariable> Vars;
		RapidIterationParameters.GetParameters(Vars);
		//UE_LOG(LogNiagara, Display, TEXT("AreScriptAndSourceSynchronized %s ======================== "), *GetFullName());
		for (int32 i = 0; i < Vars.Num(); i++)
		{
			if (Vars[i].IsDataInterface() || Vars[i].IsUObject())
			{
				// Skip these types as they don't bake out, just normal parameters get baked.
			}
			else
			{
				// Hash the name, type, and value of each parameter..
				FString VarName = Vars[i].GetName().ToString();
				FString VarTypeName = Vars[i].GetType().GetName();
				HashState.UpdateWithString(*VarName, VarName.Len());
				HashState.UpdateWithString(*VarTypeName, VarTypeName.Len());
				const uint8* VarData = RapidIterationParameters.GetParameterData(Vars[i]);
				if (VarData)
				{
					//UE_LOG(LogNiagara, Display, TEXT("Param %s %s %s"), *VarTypeName, *VarName, *ByteStr);
					HashState.Update(VarData, Vars[i].GetType().GetSize());
				}
			}
		}
		HashState.Final();

		TArray<uint8> DataHash;
		DataHash.AddUninitialized(FSHA1::DigestSize);
		HashState.GetHash(DataHash.GetData());

		FNiagaraCompileHash Hash(DataHash);
		Id.ReferencedCompileHashes.Add(Hash);
		Id.DebugReferencedObjects.Add(TEXT("RIParams"));
	}

	if (const FVersionedNiagaraScriptData* ScriptData = GetScriptData(Id.ScriptVersionID))
	{
		ScriptData->Source->ComputeVMCompilationId(Id, Usage, UsageId);
	}

	FNiagaraVMExecutableDataId& LastGeneratedVMId = GetLastGeneratedVMId(VersionGuid);
	if (GNiagaraDumpKeyGen == 1 && Id != LastGeneratedVMId)
	{
		TArray<FString> OutputByLines;
		FString StrDump;
		Id.AppendKeyString(StrDump, TEXT("\n"), true);
		StrDump.ParseIntoArrayLines(OutputByLines, false);

		UE_LOG(LogNiagara, Display, TEXT("KeyGen %s\n==================\n"), *GetPathName());
		for (int32 i = 0; i < OutputByLines.Num(); i++)
		{
			UE_LOG(LogNiagara, Display, TEXT("/*%04d*/\t\t%s"), i + 1, *OutputByLines[i]);
		}
	}

	LastGeneratedVMId = Id;
}


void UNiagaraScript::ComputeVMCompilationId_EmitterShared(FNiagaraVMExecutableDataId& Id, UNiagaraEmitter* Emitter, UNiagaraSystem* EmitterOwner, ENiagaraRendererSourceDataMode InSourceMode) const
{
	// Gather additional variables from renderers
	for (UNiagaraRendererProperties* RendererProperty : Emitter->GetRenderers())
	{
		if (RendererProperty->GetCurrentSourceMode() != InSourceMode)
			continue;

		TArray<FNiagaraVariableBase> AdditionalVariables;
		RendererProperty->GetAdditionalVariables(AdditionalVariables);
		for (const FNiagaraVariableBase& AdditionalVariable : AdditionalVariables)
		{
			if (AdditionalVariable.IsValid())
			{
				Id.AdditionalVariables.AddUnique(AdditionalVariable);
			}
		}
	}

	// Sort the additional variables by name lexically so they are always in the same order
	Id.AdditionalVariables.Sort([](const FNiagaraVariableBase& A, const FNiagaraVariableBase& B) { return A.GetName().LexicalLess(B.GetName()); });
}
#endif

bool UNiagaraScript::ContainsUsage(ENiagaraScriptUsage InUsage) const
{
	if (IsEquivalentUsage(InUsage))
	{
		return true;
	}

	if (Usage == ENiagaraScriptUsage::ParticleGPUComputeScript && IsParticleScript(InUsage))
	{
		return true;
	}

	if (Usage == ENiagaraScriptUsage::ParticleGPUComputeScript && InUsage == ENiagaraScriptUsage::ParticleSimulationStageScript)
	{
		return true;
	}

	if (InUsage == ENiagaraScriptUsage::ParticleUpdateScript && Usage == ENiagaraScriptUsage::ParticleSpawnScriptInterpolated)
	{
		return true;
	}

	if (InUsage == ENiagaraScriptUsage::EmitterSpawnScript && Usage == ENiagaraScriptUsage::SystemSpawnScript)
	{
		return true;
	}

	if (InUsage == ENiagaraScriptUsage::EmitterUpdateScript && Usage == ENiagaraScriptUsage::SystemUpdateScript)
	{
		return true;
	}

	return false;
}

const FNiagaraScriptExecutionParameterStore* UNiagaraScript::GetExecutionReadyParameterStore(ENiagaraSimTarget SimTarget)
{
#if WITH_EDITORONLY_DATA
	if (!IsCooked)
	{
		if (SimTarget == ENiagaraSimTarget::CPUSim && IsReadyToRun(ENiagaraSimTarget::CPUSim))
		{
			if (!ScriptExecutionParamStoreCPU.bInitialized)
			{
				ScriptExecutionParamStoreCPU.InitFromOwningScript(this, SimTarget, false);

				// generate the function bindings for those external functions where there's no user (per-instance) data required
				GenerateDefaultFunctionBindings();
			}
			return &ScriptExecutionParamStoreCPU;
		}
		else if (SimTarget == ENiagaraSimTarget::GPUComputeSim)
		{
			if (!ScriptExecutionParamStoreGPU.bInitialized)
			{
				ScriptExecutionParamStoreGPU.InitFromOwningScript(this, SimTarget, false);
			}
			return &ScriptExecutionParamStoreGPU;
		}
	}
#endif
	TOptional<ENiagaraSimTarget> ActualSimTarget = GetSimTarget();
	if (ActualSimTarget.IsSet())
	{
		if (ActualSimTarget == SimTarget)
		{
			return &ScriptExecutionParamStore;
		}

		UE_LOG(LogNiagara, Warning, TEXT("SimTarget is '%d' but expecting '%d' on Script '%s' Usage '%d'"), ActualSimTarget.GetValue(), SimTarget, *GetFullName(), Usage);
	}
	return nullptr;
}

TOptional<ENiagaraSimTarget> UNiagaraScript::GetSimTarget() const
{
	switch (Usage)
	{
	case ENiagaraScriptUsage::ParticleSpawnScript:
	case ENiagaraScriptUsage::ParticleSpawnScriptInterpolated:
	case ENiagaraScriptUsage::ParticleUpdateScript:
	case ENiagaraScriptUsage::ParticleEventScript:
	case ENiagaraScriptUsage::ParticleSimulationStageScript:
	case ENiagaraScriptUsage::ParticleGPUComputeScript:
		if (UNiagaraEmitter* OwningEmitter = GetTypedOuter<UNiagaraEmitter>())
		{
			if (OwningEmitter->SimTarget != ENiagaraSimTarget::CPUSim || CachedScriptVM.IsValid())
			{
				return OwningEmitter->SimTarget;
			}
		}
		break;
	case ENiagaraScriptUsage::EmitterSpawnScript:
	case ENiagaraScriptUsage::EmitterUpdateScript:
	case ENiagaraScriptUsage::SystemSpawnScript:
	case ENiagaraScriptUsage::SystemUpdateScript:
		if (CachedScriptVM.IsValid())
		{
			return ENiagaraSimTarget::CPUSim;
		}
		break;
	default:
		break;
	};
	return TOptional<ENiagaraSimTarget>();
}

void UNiagaraScript::AsyncOptimizeByteCode()
{
	if ( !CachedScriptVM.IsValid() || (CachedScriptVM.OptimizedByteCode.Num() > 0) || (CachedScriptVM.ByteCode.Num() == 0) )
	{
		return;
	}

	static const IConsoleVariable* CVarOptimizeVMCode = IConsoleManager::Get().FindConsoleVariable(TEXT("vm.OptimizeVMByteCode"));
	if (!CVarOptimizeVMCode || CVarOptimizeVMCode->GetInt() == 0 )
	{
		return;
	}

	// This has to be done game code side as we can not access anything in CachedScriptVM
	TArray<uint8, TInlineAllocator<32>> ExternalFunctionRegisterCounts;
	ExternalFunctionRegisterCounts.Reserve(CachedScriptVM.CalledVMExternalFunctions.Num());
	for (const FVMExternalFunctionBindingInfo& FunctionBindingInfo : CachedScriptVM.CalledVMExternalFunctions)
	{
		const uint8 RegisterCount = FunctionBindingInfo.GetNumInputs() + FunctionBindingInfo.GetNumOutputs();
		ExternalFunctionRegisterCounts.Add(RegisterCount);
	}

	// If we wish to release the original ByteCode we must optimize synchronously currently
	//-TODO: Find a safe point where we can release the original ByteCode
	static const IConsoleVariable* CVarFreeUnoptimizedByteCode = IConsoleManager::Get().FindConsoleVariable(TEXT("vm.FreeUnoptimizedByteCode"));
	if ((FPlatformProperties::RequiresCookedData() || IsScriptCooked()) && CVarFreeUnoptimizedByteCode && (CVarFreeUnoptimizedByteCode->GetInt() != 0) )
	{
		// use the current size of the byte code as a starting point for the allocator
		CachedScriptVM.OptimizedByteCode.Reserve(CachedScriptVM.ByteCode.Num());

		VectorVM::OptimizeByteCode(CachedScriptVM.ByteCode.GetData(), CachedScriptVM.OptimizedByteCode, MakeArrayView(ExternalFunctionRegisterCounts));
		if (CachedScriptVM.OptimizedByteCode.Num() > 0)
		{
			CachedScriptVM.ByteCode.Empty();
		}

		CachedScriptVM.OptimizedByteCode.Shrink();
	}
	else
	{
		// Async optimize the ByteCode
		AsyncTask(
			ENamedThreads::AnyThread,
			[WeakScript=TWeakObjectPtr<UNiagaraScript>(this), InExternalFunctionRegisterCounts=MoveTemp(ExternalFunctionRegisterCounts), InByteCode=CachedScriptVM.ByteCode, InCachedScriptVMId=CachedScriptVMId]() mutable
			{
				// Generate optimized byte code on any thread
				TArray<uint8> OptimizedByteCode;
				OptimizedByteCode.Reserve(InByteCode.Num());
				VectorVM::OptimizeByteCode(InByteCode.GetData(), OptimizedByteCode, MakeArrayView(InExternalFunctionRegisterCounts));

				// Kick off task to set optimized byte code on game thread
				AsyncTask(
					ENamedThreads::GameThread,
					[WeakScript, InOptimizedByteCode = MoveTemp(OptimizedByteCode), InCachedScriptVMId]() mutable
					{
						UNiagaraScript* NiagaraScript = WeakScript.Get();
						if ( (NiagaraScript != nullptr) && (NiagaraScript->CachedScriptVMId == InCachedScriptVMId) )
						{
							NiagaraScript->CachedScriptVM.OptimizedByteCode = MoveTemp(InOptimizedByteCode);
							NiagaraScript->CachedScriptVM.OptimizedByteCode.Shrink();
						}
					}
				);
			}
		);
	}
}

void UNiagaraScript::GenerateDefaultFunctionBindings()
{
	// generate the function bindings for those external functions where there's no user (per-instance) data required
	auto SimTarget = GetSimTarget();
	const int32 ExternalFunctionCount = CachedScriptVM.CalledVMExternalFunctions.Num();

	if (SimTarget.IsSet() && ExternalFunctionCount)
	{
		CachedScriptVM.CalledVMExternalFunctionBindings.Empty(ExternalFunctionCount);

		const FNiagaraScriptExecutionParameterStore* ScriptParameterStore = GetExecutionReadyParameterStore(*SimTarget);
		const auto& ScriptDataInterfaces = ScriptParameterStore->GetDataInterfaces();

		const int32 DataInterfaceCount = FMath::Min(CachedScriptVM.DataInterfaceInfo.Num(), ScriptDataInterfaces.Num());
		check(DataInterfaceCount == CachedScriptVM.DataInterfaceInfo.Num());
		check(DataInterfaceCount == ScriptDataInterfaces.Num());

		for (const FVMExternalFunctionBindingInfo& BindingInfo : CachedScriptVM.CalledVMExternalFunctions)
		{
			FVMExternalFunction& FuncBind = CachedScriptVM.CalledVMExternalFunctionBindings.AddDefaulted_GetRef();

			for (int32 DataInterfaceIt = 0; DataInterfaceIt < DataInterfaceCount; ++DataInterfaceIt)
			{
				const FNiagaraScriptDataInterfaceCompileInfo& ScriptInfo = CachedScriptVM.DataInterfaceInfo[DataInterfaceIt];

				if (ScriptInfo.UserPtrIdx == INDEX_NONE && ScriptInfo.Name == BindingInfo.OwnerName)
				{
					ScriptDataInterfaces[DataInterfaceIt]->GetVMExternalFunction(BindingInfo, nullptr, FuncBind);
				}
			}
		}
	}
}

void UNiagaraScript::PreSave(const class ITargetPlatform* TargetPlatform)
{
	Super::PreSave(TargetPlatform);

#if WITH_EDITORONLY_DATA
	// Pre-save can happen in any order for objects in the package and since this is now used to cache data for execution we need to make sure that the system compilation
	// is complete before caching the executable data.
	UNiagaraSystem* SystemOwner = FindRootSystem();
	if (SystemOwner)
	{
		SystemOwner->EnsureFullyLoaded();
		SystemOwner->WaitForCompilationComplete();
	}

	ScriptExecutionParamStore.Empty();
	ScriptExecutionBoundParameters.Empty();

	// Make sure the data interfaces are consistent to prevent crashes in later caching operations.
	if (CachedScriptVM.DataInterfaceInfo.Num() != CachedDefaultDataInterfaces.Num())
	{
		UE_LOG(LogNiagara, Warning, TEXT("Data interface count mistmatch during script presave. Invaliding compile results (see full log for details).  Script: %s"), *GetPathName());
		UE_LOG(LogNiagara, Log, TEXT("Compiled DataInterfaceInfos:"));
		for (const FNiagaraScriptDataInterfaceCompileInfo& DataInterfaceCompileInfo : CachedScriptVM.DataInterfaceInfo)
		{
			UE_LOG(LogNiagara, Log, TEXT("Name:%s, Type: %s"), *DataInterfaceCompileInfo.Name.ToString(), *DataInterfaceCompileInfo.Type.GetName());
		}
		UE_LOG(LogNiagara, Log, TEXT("Cached DataInterfaceInfos:"));
		for (const FNiagaraScriptDataInterfaceInfo& DataInterfaceCacheInfo : CachedDefaultDataInterfaces)
		{
			UE_LOG(LogNiagara, Log, TEXT("Name:%s, Type: %s, Path:%s"),
				*DataInterfaceCacheInfo.Name.ToString(), *DataInterfaceCacheInfo.Type.GetName(),
				DataInterfaceCacheInfo.DataInterface != nullptr
					? *DataInterfaceCacheInfo.DataInterface->GetPathName()
					: TEXT("None"));
		}

		InvalidateCompileResults(TEXT("Data interface count mismatch during script presave."));
		return;
	}

	if (TargetPlatform && TargetPlatform->RequiresCookedData())
	{
		TOptional<ENiagaraSimTarget> SimTarget = GetSimTarget();
		if (SimTarget)
		{
			// Partial execution of InitFromOwningScript()
			ScriptExecutionParamStore.AddScriptParams(this, SimTarget.GetValue(), false);
			FNiagaraParameterStoreBinding::GetBindingData(&ScriptExecutionParamStore, &RapidIterationParameters, ScriptExecutionBoundParameters);
		}
	}

	ResolveParameterCollectionReferences();
#endif
}

void UNiagaraScript::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FNiagaraCustomVersion::GUID);		// only changes version if not loading
	const int32 NiagaraVer = Ar.CustomVer(FNiagaraCustomVersion::GUID);

	FNiagaraParameterStore TemporaryStore;
	int32 NumRemoved = 0;
	if (Ar.IsCooking())
	{
		bool bUsesRapidIterationParams = true;

#if WITH_EDITORONLY_DATA
		if (UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(GetOuter()))
		{
			UNiagaraSystem* EmitterOwner = Cast<UNiagaraSystem>(Emitter->GetOuter());
			if (EmitterOwner && EmitterOwner->bBakeOutRapidIteration)
			{
				bUsesRapidIterationParams = false;
			}

			if (!Emitter->bBakeOutRapidIteration)
			{
				bUsesRapidIterationParams = true;
			}
		}
		else if (UNiagaraSystem* System = Cast<UNiagaraSystem>(GetOuter()))
		{
			if (System && System->bBakeOutRapidIteration)
			{
				bUsesRapidIterationParams = false;
			}
		}
#endif

		if (!bUsesRapidIterationParams)
		{
			// Copy off the parameter store for now..
			TemporaryStore = RapidIterationParameters;

			auto ParameterVariables = TemporaryStore.ReadParameterVariables();

			// Get the active parameters
			// Remove all parameters that aren't data interfaces or uobjects
			for (const FNiagaraVariableBase& Var : ParameterVariables)
			{
				if (Var.IsDataInterface() || Var.IsUObject())
					continue;
				RapidIterationParameters.RemoveParameter(Var);
				NumRemoved++;
			}

			UE_LOG(LogNiagara, Verbose, TEXT("Pruned %d/%d parameters from script %s"), NumRemoved, ParameterVariables.Num(), *GetFullName());
		}
	}

#if WITH_EDITORONLY_DATA
	if (Ar.IsCooking() && Ar.IsSaving())
	{
		auto& ExecutableData = GetVMExecutableData();

		if (Usage != ENiagaraScriptUsage::ParticleGPUComputeScript)
		{
			ExecutableData.BakeScriptLiterals(ExecutableData.ScriptLiterals);

			// we only need the padding info for when we're dealing with GPU scripts (for
			// FNiagaraScriptInstanceParameterStore::CopyParameterDataToPaddedBuffer())
			ScriptExecutionParamStore.PaddingInfo.Empty();
		}
		else
		{
			ExecutableData.ScriptLiterals.Empty();
			ScriptExecutionParamStore.CoalescePaddingInfo();
		}

		if (!HasValidParameterBindings())
		{
			UE_LOG(LogNiagara, Warning, TEXT("Mismatch between binding between RapidIterationParamters and ScriptExecutionParameters for system %s"), *GetFullName());
		}
	}

	if (Ar.IsLoading())
	{
		IsCooked = Ar.IsFilterEditorOnly();
	}
#endif

	Super::Serialize(Ar);

	// Restore after serialize
	if (Ar.IsCooking() && NumRemoved > 0)
	{
		RapidIterationParameters = TemporaryStore;
	}

	bool IsValidShaderScript = false;
	if (NiagaraVer < FNiagaraCustomVersion::DontCompileGPUWhenNotNeeded)
	{
		IsValidShaderScript = Usage != ENiagaraScriptUsage::Module && Usage != ENiagaraScriptUsage::Function && Usage != ENiagaraScriptUsage::DynamicInput
			&& (NiagaraVer < FNiagaraCustomVersion::NiagaraShaderMapCooking2 || (Usage != ENiagaraScriptUsage::SystemSpawnScript && Usage != ENiagaraScriptUsage::SystemUpdateScript))
			&& (NiagaraVer < FNiagaraCustomVersion::NiagaraCombinedGPUSpawnUpdate || (Usage != ENiagaraScriptUsage::ParticleUpdateScript && Usage != ENiagaraScriptUsage::EmitterSpawnScript && Usage != ENiagaraScriptUsage::EmitterUpdateScript));
	}
	else if (NiagaraVer < FNiagaraCustomVersion::MovedToDerivedDataCache)
	{
		IsValidShaderScript = LegacyCanBeRunOnGpu();
	}
	else
	{
		IsValidShaderScript = CanBeRunOnGpu();
	}

	if (IsValidShaderScript)
	{
		if (NiagaraVer < FNiagaraCustomVersion::UseHashesToIdentifyCompileStateOfTopLevelScripts)
		{
			// In some rare cases a GPU script could have been saved in an error state in a version where skeletal mesh or static mesh data interfaces didn't work properly on GPU.
			// This would fail in the current regime.
			for (const FNiagaraScriptDataInterfaceCompileInfo& InterfaceInfo : CachedScriptVM.DataInterfaceInfo)
			{
				if (InterfaceInfo.Type.GetClass() == UNiagaraDataInterfaceSkeletalMesh::StaticClass() ||
					InterfaceInfo.Type.GetClass() == UNiagaraDataInterfaceStaticMesh::StaticClass())
				{
					IsValidShaderScript = false;
				}
			}
		}
	}

	SerializeNiagaraShaderMaps(Ar, NiagaraVer, IsValidShaderScript);
}

FNiagaraCompilerTag* FNiagaraCompilerTag::FindTag(TArray< FNiagaraCompilerTag>& InTags, const FNiagaraVariableBase& InSearchVar)
{
	for (FNiagaraCompilerTag& Tag : InTags)
	{
		if (Tag.Variable == InSearchVar)
			return &Tag;
	}
	return nullptr;
}


const FNiagaraCompilerTag* FNiagaraCompilerTag::FindTag(const TArray< FNiagaraCompilerTag>& InTags, const FNiagaraVariableBase& InSearchVar)
{
	for (const FNiagaraCompilerTag& Tag : InTags)
	{
		if (Tag.Variable == InSearchVar)
			return &Tag;
	}
	return nullptr;
}

/** Is usage A dependent on Usage B?*/
bool UNiagaraScript::IsUsageDependentOn(ENiagaraScriptUsage InUsageA, ENiagaraScriptUsage InUsageB)
{
	if (InUsageA == InUsageB)
	{
		return false;
	}

	// Usages of the same phase are interdependent because we copy the attributes from one to the other and if those got
	// out of sync, there could be problems.

	if ((InUsageA == ENiagaraScriptUsage::ParticleSpawnScript || InUsageA == ENiagaraScriptUsage::ParticleSpawnScriptInterpolated || InUsageA == ENiagaraScriptUsage::ParticleUpdateScript || InUsageA == ENiagaraScriptUsage::ParticleEventScript)
		&& (InUsageB == ENiagaraScriptUsage::ParticleSpawnScript || InUsageB == ENiagaraScriptUsage::ParticleSpawnScriptInterpolated || InUsageB == ENiagaraScriptUsage::ParticleUpdateScript || InUsageB == ENiagaraScriptUsage::ParticleEventScript))
	{
		return true;
	}

	// The GPU compute script is always dependent on the other particle scripts.
	if ((InUsageA == ENiagaraScriptUsage::ParticleGPUComputeScript)
		&& (InUsageB == ENiagaraScriptUsage::ParticleSpawnScript || InUsageB == ENiagaraScriptUsage::ParticleSpawnScriptInterpolated || InUsageB == ENiagaraScriptUsage::ParticleUpdateScript || InUsageB == ENiagaraScriptUsage::ParticleEventScript || InUsageB == ENiagaraScriptUsage::ParticleSimulationStageScript))
	{
		return true;
	}

	if ((InUsageA == ENiagaraScriptUsage::EmitterSpawnScript || InUsageA == ENiagaraScriptUsage::EmitterUpdateScript)
		&& (InUsageB == ENiagaraScriptUsage::EmitterSpawnScript || InUsageB == ENiagaraScriptUsage::EmitterUpdateScript))
	{
		return true;
	}

	if ((InUsageA == ENiagaraScriptUsage::SystemSpawnScript || InUsageA == ENiagaraScriptUsage::SystemUpdateScript)
		&& (InUsageB == ENiagaraScriptUsage::SystemSpawnScript || InUsageB == ENiagaraScriptUsage::SystemUpdateScript))
	{
		return true;
	}

	return false;
}

bool UNiagaraScript::ConvertUsageToGroup(ENiagaraScriptUsage InUsage, ENiagaraScriptGroup& OutGroup)
{
	if (IsParticleScript(InUsage) || IsStandaloneScript(InUsage))
	{
		OutGroup = ENiagaraScriptGroup::Particle;
		return true;
	}
	else if (IsEmitterSpawnScript(InUsage) || IsEmitterUpdateScript(InUsage))
	{
		OutGroup = ENiagaraScriptGroup::Emitter;
		return true;
	}
	else if (IsSystemSpawnScript(InUsage) || IsSystemUpdateScript(InUsage))
	{
		OutGroup = ENiagaraScriptGroup::System;
		return true;
	}

	return false;
}

void UNiagaraScript::PostLoad()
{
	Super::PostLoad();

	const int32 NiagaraVer = GetLinkerCustomVersion(FNiagaraCustomVersion::GUID);

#if WITH_EDITORONLY_DATA
	CheckVersionDataAvailable();
#endif
	
	RapidIterationParameters.PostLoad();

	if (FPlatformProperties::RequiresCookedData() || IsScriptCooked())
	{
		ScriptExecutionParamStore.PostLoad();

		// if our bindings aren't valid, then something has gone wrong with our cook and we need to disable this Script, which will in turn
		// disable the owning script and system
		if (!HasValidParameterBindings())
		{
			UE_LOG(LogNiagara, Error, TEXT("Mismatch between binding between RapidIterationParamters and ScriptExecutionParameters for system %s"), *GetFullName());

			CachedScriptVM.Reset();
			return;
		}

		RapidIterationParameters.Bind(&ScriptExecutionParamStore, &ScriptExecutionBoundParameters);
		ScriptExecutionParamStore.bInitialized = true;
		ScriptExecutionBoundParameters.Empty();

		// generate the function bindings for those external functions where there's no user (per-instance) data required
		GenerateDefaultFunctionBindings();
	}

	// Because we might be using these cached data interfaces, we need to make sure that they are properly postloaded.
	for (FNiagaraScriptDataInterfaceInfo& Info : CachedDefaultDataInterfaces)
	{
		if (Info.DataInterface)
		{
			Info.DataInterface->ConditionalPostLoad();
		}
	}

#if WITH_EDITORONLY_DATA
	FVersionedNiagaraScriptData* ScriptData = GetLatestScriptData();
	ensure(ScriptData);
	if (NiagaraVer < FNiagaraCustomVersion::AddSimulationStageUsageEnum)
	{
		uint8 SimulationStageIndex = (uint8)ENiagaraScriptUsage::ParticleSimulationStageScript;
		uint8 MaxIndex = (uint8)ENiagaraScriptUsage::SystemUpdateScript;
		int32& UsageBitmask = ScriptData->ModuleUsageBitmask;
		// Start at the end and shift the bits down to account for the new shader stage bit.
		for (uint8 CurrentIndex = MaxIndex; CurrentIndex > SimulationStageIndex; CurrentIndex--)
		{
			uint8 OldIndex = CurrentIndex - 1;
			if ((UsageBitmask & (1 << OldIndex)) != 0)
			{
				UsageBitmask |= 1 << CurrentIndex;
			}
			else
			{
				UsageBitmask &= ~(1 << CurrentIndex);
			}
		}
		// Clear the simulation stage bit.
		UsageBitmask &= ~(1 << SimulationStageIndex);
	}

	if (NiagaraVer < FNiagaraCustomVersion::SimulationStageInUsageBitmask)
	{
		int32& UsageBitmask = ScriptData->ModuleUsageBitmask;
		const TArray<ENiagaraScriptUsage> SupportedUsages = GetSupportedUsageContextsForBitmask(UsageBitmask);
		if (SupportedUsages.Contains(ENiagaraScriptUsage::ParticleUpdateScript))
		{
			// Set the simulation stage bit by default to true for old assets if particle update is enabled as well
			uint8 SimulationStageIndex = (uint8)ENiagaraScriptUsage::ParticleSimulationStageScript;
			UsageBitmask |= (1 << SimulationStageIndex);
		}
	}

	for (FVersionedNiagaraScriptData& Data : VersionData)
	{
		UNiagaraScriptSourceBase* Source = Data.Source;
        if (Source != nullptr)
        {
        	Source->ConditionalPostLoad();

			// Synchronize with Definitions after source scripts have been postloaded.
			FVersionedNiagaraScript& VersionedScriptAdapter = VersionedScriptAdapters.Emplace_GetRef(this, Data.Version.VersionGuid);
			VersionedScriptAdapter.PostLoadDefinitionsSubscriptions();

        	bool bScriptVMNeedsRebuild = false;
        	FString RebuildReason;
        	if (NiagaraVer < FNiagaraCustomVersion::UseHashesToIdentifyCompileStateOfTopLevelScripts && CachedScriptVMId.CompilerVersionID.IsValid())
        	{
        		FGuid BaseId = Source->GetCompileBaseId(Usage, UsageId);
        		if (BaseId.IsValid() == false)
        		{
        			UE_LOG(LogNiagara, Warning,
                        TEXT("Invalidating compile ids for script %s because it doesn't have a valid base id.  The owning asset will continue to compile on load until it is resaved."),
                        *GetPathName());
        			InvalidateCompileResults(TEXT("Script didn't have a valid base id."));
        			Source->ForceGraphToRecompileOnNextCheck();
        		}
        		else
        		{
        			FNiagaraCompileHash CompileHash = Source->GetCompileHash(Usage, UsageId);
        			if (CompileHash.IsValid())
        			{
        				CachedScriptVMId.BaseScriptCompileHash = CompileHash;
        			}
        			else
        			{
        				// If the compile hash isn't valid, the vm id needs to be recalculated and the cached vm needs to be invalidated.
        				bScriptVMNeedsRebuild = true;
        				RebuildReason = TEXT("Script did not have a valid compile hash.");
        			}
        		}
        	}

        	if (CachedScriptVMId.CompilerVersionID.IsValid() && CachedScriptVMId.CompilerVersionID != FNiagaraCustomVersion::LatestScriptCompileVersion)
        	{
        		bScriptVMNeedsRebuild = true;
        		RebuildReason = TEXT("Niagara compiler version changed since the last time the script was compiled.");
        	}

        	if (bScriptVMNeedsRebuild)
        	{
        		// Force a rebuild on the source vm ids, and then invalidate the current cache to force the script to be unsynchronized.
        		// We modify here in post load so that it will cause the owning asset to resave when running the resave commandlet.
        		bool bForceRebuild = true;
        		Modify();
        		Source->ComputeVMCompilationId(CachedScriptVMId, Usage, UsageId, bForceRebuild);
        		InvalidateCompileResults(RebuildReason);
        	}

        	// Convert visibility of old assets
        	if (NiagaraVer < FNiagaraCustomVersion::AddLibraryAssetProperty || (NiagaraVer < FNiagaraCustomVersion::AddLibraryVisibilityProperty && bExposeToLibrary_DEPRECATED))
        	{
        		ScriptData->LibraryVisibility = ENiagaraScriptLibraryVisibility::Library;
        	}
        }
	}

#endif

	ProcessSerializedShaderMaps();

#if WITH_EDITORONLY_DATA
	if (CachedScriptVMId.BaseScriptCompileHash.IsValid() && AreScriptAndSourceSynchronized())
	{
		CacheResourceShadersForRendering(false, false /*bNeedsRecompile*/);
	}
#endif

	GenerateStatIDs();

	// Optimize the VM script for runtime usage
	AsyncOptimizeByteCode();
}

bool UNiagaraScript::IsReadyToRun(ENiagaraSimTarget SimTarget) const
{
	if (SimTarget == ENiagaraSimTarget::CPUSim)
	{
		if (CachedScriptVM.IsValid())
		{
			return true;
		}
	}
	else if (SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		return CanBeRunOnGpu();
	}

	return false;
}

bool UNiagaraScript::ShouldCacheShadersForCooking(const ITargetPlatform* TargetPlatform) const
{
	if (CanBeRunOnGpu())
	{
		UNiagaraEmitter* OwningEmitter = GetTypedOuter<UNiagaraEmitter>();
		if (OwningEmitter != nullptr && OwningEmitter->SimTarget == ENiagaraSimTarget::GPUComputeSim && OwningEmitter->NeedsLoadForTargetPlatform(TargetPlatform))
		{
			return true;
		}
	}
	return false;
}

void UNiagaraScript::GenerateStatIDs()
{
#if STATS
	StatScopesIDs.Empty();
	if (IsReadyToRun(ENiagaraSimTarget::CPUSim))
	{
		StatScopesIDs.Reserve(CachedScriptVM.StatScopes.Num());
		for (FNiagaraStatScope& StatScope : CachedScriptVM.StatScopes)
		{
			StatScopesIDs.Add(FDynamicStats::CreateStatId<FStatGroup_STATGROUP_NiagaraDetailed>(StatScope.FriendlyName.ToString()));
		}
	}
#elif ENABLE_STATNAMEDEVENTS
	StatNamedEvents.Empty();

	static const IConsoleVariable* CVarOptimizeVMDetailedStats = IConsoleManager::Get().FindConsoleVariable(TEXT("vm.DetailedVMScriptStats"));
	if (CVarOptimizeVMDetailedStats && CVarOptimizeVMDetailedStats->GetInt() != 0)
	{
		if (IsReadyToRun(ENiagaraSimTarget::CPUSim))
		{
			StatNamedEvents.Reserve(CachedScriptVM.StatScopes.Num());
			for (FNiagaraStatScope& StatScope : CachedScriptVM.StatScopes)
			{
				StatNamedEvents.Add(StatScope.FriendlyName.ToString());
			}
		}
	}
#endif
}

#if WITH_EDITOR

void UNiagaraScript::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	CacheResourceShadersForRendering(true);
	CustomAssetRegistryTagCache.Reset();
	OnPropertyChangedDelegate.Broadcast(PropertyChangedEvent);
}

void UNiagaraScript::PostEditChangeVersionedProperty(FPropertyChangedEvent& PropertyChangedEvent, const FGuid& Version)
{
	FName PropertyName = PropertyChangedEvent.Property->GetFName();

	if (UNiagaraScriptSourceBase* Source = GetSource(Version))
	{
		if (PropertyName == GET_MEMBER_NAME_CHECKED(FVersionedNiagaraScriptData, bDeprecated) || 
         PropertyName == GET_MEMBER_NAME_CHECKED(FVersionedNiagaraScriptData, DeprecationMessage) ||
         PropertyName == GET_MEMBER_NAME_CHECKED(FVersionedNiagaraScriptData, DeprecationRecommendation))
		{
			Source->MarkNotSynchronized(TEXT("Deprecation changed."));
		}

		if (PropertyName == GET_MEMBER_NAME_CHECKED(FVersionedNiagaraScriptData, bExperimental) || 
            PropertyName == GET_MEMBER_NAME_CHECKED(FVersionedNiagaraScriptData, ExperimentalMessage))
		{
			Source->MarkNotSynchronized(TEXT("Experimental changed."));
		}
	
		if (PropertyName == GET_MEMBER_NAME_CHECKED(FVersionedNiagaraScriptData, NoteMessage))
		{
			Source->MarkNotSynchronized(TEXT("Note changed."));
		}
	}

	PostEditChangeProperty(PropertyChangedEvent);
}

#endif

#if WITH_EDITORONLY_DATA
UNiagaraScriptSourceBase* UNiagaraScript::GetSource(const FGuid& VersionGuid)
{
	if (VersionData.Num() == 0)
	{
		// this should only happen when loading old assets where our PostLoad method was not yet called 
		return Source_DEPRECATED;
	}
	FVersionedNiagaraScriptData* ScriptData = GetScriptData(VersionGuid);
	ensure(ScriptData);
	return ScriptData ? ScriptData->Source : nullptr;
}

const UNiagaraScriptSourceBase* UNiagaraScript::GetSource(const FGuid& VersionGuid) const
{
	return const_cast<UNiagaraScript*>(this)->GetSource(VersionGuid);
}

UNiagaraScriptSourceBase* UNiagaraScript::GetLatestSource()
{
	return GetSource(FGuid());
}

const UNiagaraScriptSourceBase* UNiagaraScript::GetLatestSource() const
{
	return GetSource(FGuid());
}

void UNiagaraScript::SetLatestSource(UNiagaraScriptSourceBase* InSource)
{
	SetSource(InSource, FGuid());
}

void UNiagaraScript::SetSource(UNiagaraScriptSourceBase* InSource, const FGuid& VersionGuid)
{
	CheckVersionDataAvailable();
	GetScriptData(VersionGuid)->Source = InSource;
}

bool UNiagaraScript::AreScriptAndSourceSynchronized(const FGuid& VersionGuid) const
{
	static const bool bNoShaderCompile = FParse::Param(FCommandLine::Get(), TEXT("NoShaderCompile"));
	if (bNoShaderCompile)
	{
		return false;
	}

	const FVersionedNiagaraScriptData* ScriptData = GetScriptData(VersionGuid);
	if (ScriptData && ScriptData->Source)
	{
		FNiagaraVMExecutableDataId NewId;
		ComputeVMCompilationId(NewId, VersionGuid);
		bool bSynchronized = (NewId.IsValid() && NewId == CachedScriptVMId);
		if (!bSynchronized && NewId.IsValid() && CachedScriptVMId.IsValid() && CachedScriptVM.IsValid())
		{
			if (NewId != LastReportedVMId)
			{
				if (GEnableVerboseNiagaraChangeIdLogging)
				{
					if (NewId.BaseScriptCompileHash != CachedScriptVMId.BaseScriptCompileHash)
					{
						UE_LOG(LogNiagara, Log, TEXT("AreScriptAndSourceSynchronized base script compile hashes don't match. %s != %s, script %s"),
							*NewId.BaseScriptCompileHash.ToString(), *CachedScriptVMId.BaseScriptCompileHash.ToString(), *GetPathName());
					}

					if (NewId.ReferencedCompileHashes.Num() != CachedScriptVMId.ReferencedCompileHashes.Num())
					{
						UE_LOG(LogNiagara, Log, TEXT("AreScriptAndSourceSynchronized num referenced compile hashes don't match. %d != %d, script %s"),
							NewId.ReferencedCompileHashes.Num(), CachedScriptVMId.ReferencedCompileHashes.Num(), *GetPathName());
					}
					else
					{
						for (int32 i = 0; i < NewId.ReferencedCompileHashes.Num(); i++)
						{
							if (NewId.ReferencedCompileHashes[i] != CachedScriptVMId.ReferencedCompileHashes[i])
							{
								UE_LOG(LogNiagara, Log, TEXT("AreScriptAndSourceSynchronized referenced compile hash %d doesn't match. %s != %s, script %s, source %s"),
									i, *NewId.ReferencedCompileHashes[i].ToString(), *CachedScriptVMId.ReferencedCompileHashes[i].ToString(), *GetPathName(),
									*NewId.DebugReferencedObjects[i]);
							}
						}
					}
				}
				LastReportedVMId = NewId;
			}
		}

		return bSynchronized;
	}
	return false;
}

void UNiagaraScript::MarkScriptAndSourceDesynchronized(FString Reason, const FGuid& VersionGuid)
{
	if (UNiagaraScriptSourceBase* Source = GetSource(VersionGuid))
	{
		Source->MarkNotSynchronized(Reason);
	}
}

bool UNiagaraScript::HandleVariableRenames(const TMap<FNiagaraVariable, FNiagaraVariable>& OldToNewVars, const FString& UniqueEmitterName)
{
	bool bConvertedAnything = false;
	auto Iter = OldToNewVars.CreateConstIterator();
	while (Iter)
	{
		// Sometimes the script is under the generic name, other times it has been converted to the unique emitter name. Handle both cases below...
		FNiagaraVariable RISrcVarA = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(Iter->Key, !UniqueEmitterName.IsEmpty() ? TEXT("Emitter") : nullptr , GetUsage());
		FNiagaraVariable RISrcVarB = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(Iter->Key, !UniqueEmitterName.IsEmpty() ? *UniqueEmitterName : nullptr, GetUsage());
		FNiagaraVariable RIDestVarA = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(Iter->Value, !UniqueEmitterName.IsEmpty() ? TEXT("Emitter") : nullptr, GetUsage());
		FNiagaraVariable RIDestVarB = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(Iter->Value, !UniqueEmitterName.IsEmpty() ? *UniqueEmitterName : nullptr, GetUsage());

		{
			if (nullptr != RapidIterationParameters.FindParameterOffset(RISrcVarA))
			{
				RapidIterationParameters.RenameParameter(RISrcVarA, RIDestVarA.GetName());
				UE_LOG(LogNiagara, Log, TEXT("Converted RI variable \"%s\" to \"%s\" in Script \"%s\""), *RISrcVarA.GetName().ToString(), *RIDestVarA.GetName().ToString(), *GetFullName());
				bConvertedAnything = true;
			}
			else if (nullptr != RapidIterationParameters.FindParameterOffset(RISrcVarB))
			{
				RapidIterationParameters.RenameParameter(RISrcVarB, RIDestVarB.GetName());
				UE_LOG(LogNiagara, Log, TEXT("Converted RI variable \"%s\" to \"%s\" in Script \"%s\""), *RISrcVarB.GetName().ToString(), *RIDestVarB.GetName().ToString(), *GetFullName());
				bConvertedAnything = true;
			}
		}

		{
			// Go ahead and convert the stored VM executable data too. I'm not 100% sure why this is necessary, since we should be recompiling.
			int32 VarIdx = GetVMExecutableData().Parameters.Parameters.IndexOfByKey(RISrcVarA);
			if (VarIdx != INDEX_NONE)
			{
				GetVMExecutableData().Parameters.Parameters[VarIdx].SetName(RIDestVarA.GetName());
				UE_LOG(LogNiagara, Log, TEXT("Converted exec param variable \"%s\" to \"%s\" in Script \"%s\""), *RISrcVarA.GetName().ToString(), *RIDestVarA.GetName().ToString(), *GetFullName());
				bConvertedAnything = true;
			}

			VarIdx = GetVMExecutableData().Parameters.Parameters.IndexOfByKey(RISrcVarB);
			if (VarIdx != INDEX_NONE)
			{
				GetVMExecutableData().Parameters.Parameters[VarIdx].SetName(RIDestVarB.GetName());
				UE_LOG(LogNiagara, Log, TEXT("Converted exec param  variable \"%s\" to \"%s\" in Script \"%s\""), *RISrcVarB.GetName().ToString(), *RIDestVarB.GetName().ToString(), *GetFullName());
				bConvertedAnything = true;
			}
		}

		{
			// Also handle any data set mappings...
			auto DS2PIterator = GetVMExecutableData().DataSetToParameters.CreateIterator();
			while (DS2PIterator)
			{
				for (int32 i = 0; i < DS2PIterator.Value().Parameters.Num(); i++)
				{
					FNiagaraVariable Var = DS2PIterator.Value().Parameters[i];
					if (Var == RISrcVarA)
					{
						DS2PIterator.Value().Parameters[i].SetName(RIDestVarA.GetName());
						bConvertedAnything = true;
					}
					else if (Var == RISrcVarB)
					{
						DS2PIterator.Value().Parameters[i].SetName(RIDestVarB.GetName());
						bConvertedAnything = true;
					}
				}
				++DS2PIterator;
			}
		}
		++Iter;
	}

	if (bConvertedAnything)
	{
		InvalidateExecutionReadyParameterStores();
	}

	return bConvertedAnything;
}

static bool ValidateExecData(const UNiagaraScript* Script, const FNiagaraVMExecutableData& ExecData, FString& ErrorString)
{
	bool IsValid = true;

	for (const auto& Attribute : ExecData.Attributes)
	{
		if (!Attribute.IsValid())
		{
			ErrorString.Appendf(TEXT("Failure - %s - Attribute [%s] is invalid!\n"), Script ? *Script->GetFullName() : TEXT("<unknown>"), *Attribute.GetName().ToString());
			IsValid = false;
		}
	}

	for (const auto& Parameter : ExecData.Parameters.Parameters)
	{
		if (!Parameter.IsValid())
		{
			ErrorString.Appendf(TEXT("Failure - %s - Parameter [%s] is invalid!\n"), Script ? *Script->GetFullName() : TEXT("<unknown>"), *Parameter.GetName().ToString());
			IsValid = false;
		}
	}

	return IsValid;
}

bool UNiagaraScript::BinaryToExecData(const UNiagaraScript* Script, const TArray<uint8>& InBinaryData, FNiagaraVMExecutableData& OutExecData)
{
	check(IsInGameThread());
	if (InBinaryData.Num() == 0)
	{
		return false;
	}

	FMemoryReader Ar(InBinaryData, true);
	FObjectAndNameAsStringProxyArchive SafeAr(Ar, false);
	OutExecData.SerializeData(SafeAr, true);

	FString ValidationErrors;
	if (!ValidateExecData(Script, OutExecData, ValidationErrors))
	{
		UE_LOG(LogNiagara, Display, TEXT("Failed to validate FNiagaraVMExecutableData received from DDC, rejecting!  Reasons:\n%s"), *ValidationErrors);
		return false;
	}

	return !SafeAr.IsError();
}

bool UNiagaraScript::ExecToBinaryData(const UNiagaraScript* Script, TArray<uint8>& OutBinaryData, FNiagaraVMExecutableData& InExecData)
{
	check(IsInGameThread());

	FString ValidationErrors;
	if (!ValidateExecData(Script, InExecData, ValidationErrors))
	{
		UE_LOG(LogNiagara, Error, TEXT("Failed to validate FNiagaraVMExecutableData being pushed to DDC, rejecting!  Errors:\n%s"), *ValidationErrors);
		return false;
	}

	FMemoryWriter Ar(OutBinaryData, true);
	FObjectAndNameAsStringProxyArchive SafeAr(Ar, false);
	InExecData.SerializeData(SafeAr, true);

	return OutBinaryData.Num() != 0 && !SafeAr.IsError();
}

void WriteTextFileToDisk(FString SaveDirectory, FString FileName, FString TextToSave, bool bAllowOverwriting)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// CreateDirectoryTree returns true if the destination
	// directory existed prior to call or has been created
	// during the call.
	if (PlatformFile.CreateDirectoryTree(*SaveDirectory))
	{
		// Get absolute file path
		FString AbsoluteFilePath = SaveDirectory + "/" + FileName;

		// Allow overwriting or file doesn't already exist
		if (bAllowOverwriting || !PlatformFile.FileExists(*AbsoluteFilePath))
		{
			if (FFileHelper::SaveStringToFile(TextToSave, *AbsoluteFilePath))
			{
				UE_LOG(LogNiagara, Log, TEXT("Wrote file to %s"), *AbsoluteFilePath);
				return;
			}

		}
	}
}

UNiagaraDataInterface* UNiagaraScript::CopyDataInterface(UNiagaraDataInterface* Src, UObject* Owner)
{
	if (Src)
	{
		UNiagaraDataInterface* DI = NewObject<UNiagaraDataInterface>(Owner, const_cast<UClass*>(Src->GetClass()), NAME_None, RF_Transactional | RF_Public);
		Src->CopyTo(DI);
		return DI;
	}
	return nullptr;
}

#if WITH_EDITORONLY_DATA
const FVersionedNiagaraScript FVersionedNiagaraScriptWeakPtr::Pin() const
{
	if (Script.IsValid())
	{
		return FVersionedNiagaraScript(Script.Get(), Version);
	}
	return FVersionedNiagaraScript();
}

FVersionedNiagaraScript FVersionedNiagaraScriptWeakPtr::Pin()
{
	if (Script.IsValid())
	{
		FVersionedNiagaraScript PinnedVersionedNiagaraScript = FVersionedNiagaraScript(Script.Get(), Version);
		return PinnedVersionedNiagaraScript;
	}
	return FVersionedNiagaraScript();
}

TArray<UNiagaraScriptSourceBase*> FVersionedNiagaraScriptWeakPtr::GetAllSourceScripts()
{
	if (Script.IsValid())
	{
		return { Script.Get()->GetSource(Version) };
	}
	return TArray<UNiagaraScriptSourceBase*>();
}

FString FVersionedNiagaraScriptWeakPtr::GetSourceObjectPathName() const
{
	return Script.IsValid() ? Script.Get()->GetPathName() : FString();
}

TArray<UNiagaraScriptSourceBase*> FVersionedNiagaraScript::GetAllSourceScripts()
{
	if (Script != nullptr)
	{
		return { Script->GetSource(Version) };
	}
	return TArray<UNiagaraScriptSourceBase*>();
}

FString FVersionedNiagaraScript::GetSourceObjectPathName() const
{
	return Script ? Script->GetPathName() : FString();
}

FVersionedNiagaraScriptWeakPtr FVersionedNiagaraScript::ToWeakPtr()
{
	FVersionedNiagaraScriptWeakPtr WeakVersionedNiagaraScript = FVersionedNiagaraScriptWeakPtr(Script, Version);
	return WeakVersionedNiagaraScript;
}

FVersionedNiagaraScriptData* FVersionedNiagaraScript::GetScriptData() const
{
	return Script ? Script->GetScriptData(Version) : nullptr;
}
#endif

UNiagaraDataInterface* ResolveDataInterface(FNiagaraCompileRequestDataBase* InBase, FName VariableName)
{
	UNiagaraDataInterface* const* FoundDI = InBase->GetObjectNameMap().Find(VariableName);
	if (FoundDI && *(FoundDI))
	{
		return *FoundDI;
	}
	return nullptr;
}

void DumpNameMap(FNiagaraCompileRequestDataBase* InBase)
{
	for (const TPair<FName, UNiagaraDataInterface*>& Pair : InBase->GetObjectNameMap())
	{
		UE_LOG(LogNiagara, Log, TEXT("%s -> %s"), *Pair.Key.ToString(), *GetPathNameSafe(Pair.Value));
	}
}



void UNiagaraScript::SetVMCompilationResults(const FNiagaraVMExecutableDataId& InCompileId, FNiagaraVMExecutableData& InScriptVM, FNiagaraCompileRequestDataBase* InRequestData)
{
	check(InRequestData != nullptr);

	CachedScriptVMId = InCompileId;
	CachedScriptVM = InScriptVM;
	CachedParameterCollectionReferences.Empty();
	// Proactively clear out the script resource, because it might be stale now.
	ScriptResource->Invalidate();

	if (CachedScriptVM.LastCompileStatus == ENiagaraScriptCompileStatus::NCS_Error)
	{
		// Compiler errors for Niagara will have a strong UI impact but the game should still function properly, there
		// will just be oddities in the visuals. It should be acted upon, but in no way should the game be blocked from
		// a successful cook because of it. Therefore, we do a warning.
		if (!CachedScriptVM.ErrorMsg.IsEmpty())
		{
			UE_ASSET_LOG(LogNiagara, Warning, this, TEXT("%s"), *CachedScriptVM.ErrorMsg);
		}
	}

	ResolveParameterCollectionReferences();

	CachedDefaultDataInterfaces.Empty(CachedScriptVM.DataInterfaceInfo.Num());
	for (const FNiagaraScriptDataInterfaceCompileInfo& Info : CachedScriptVM.DataInterfaceInfo)
	{
		int32 Idx = CachedDefaultDataInterfaces.AddDefaulted();
		CachedDefaultDataInterfaces[Idx].UserPtrIdx = Info.UserPtrIdx;
		CachedDefaultDataInterfaces[Idx].Name = InRequestData->ResolveEmitterAlias(Info.Name);
		CachedDefaultDataInterfaces[Idx].Type = Info.Type;
		CachedDefaultDataInterfaces[Idx].RegisteredParameterMapRead = InRequestData->ResolveEmitterAlias(Info.RegisteredParameterMapRead);
		CachedDefaultDataInterfaces[Idx].RegisteredParameterMapWrite = InRequestData->ResolveEmitterAlias(Info.RegisteredParameterMapWrite);

		// We compiled it just a bit ago, so we should be able to resolve it from the table that we passed in.
		UNiagaraDataInterface* FindDIById = ResolveDataInterface(InRequestData, CachedDefaultDataInterfaces[Idx].Name);
		if (FindDIById != nullptr )
		{
			CachedDefaultDataInterfaces[Idx].DataInterface = CopyDataInterface(FindDIById, this);
			check(CachedDefaultDataInterfaces[Idx].DataInterface != nullptr);
		}

		if (CachedDefaultDataInterfaces[Idx].DataInterface == nullptr)
		{
			// Use the CDO since we didn't have a default..
			UObject* Obj = const_cast<UClass*>(Info.Type.GetClass())->GetDefaultObject(true);
			CachedDefaultDataInterfaces[Idx].DataInterface = Cast<UNiagaraDataInterface>(CopyDataInterface(CastChecked<UNiagaraDataInterface>(Obj), this));

			if (Info.bIsPlaceholder == false)
			{
				UE_LOG(LogNiagara, Warning, TEXT("We somehow ended up with a data interface that we couldn't match post compile. This shouldn't happen. Creating a dummy to prevent crashes. DataInterfaceInfoName:%s Object:%s"), *Info.Name.ToString(), *GetPathNameSafe(this));
				UE_LOG(LogNiagara, Log, TEXT("Object to Name map contents:"));
				DumpNameMap(InRequestData);
			}
		}
		check(CachedDefaultDataInterfaces[Idx].DataInterface != nullptr);
	}

	GenerateStatIDs();

	// Now go ahead and trigger the GPU script compile now that we have a compiled GPU hlsl script.
	if (Usage == ENiagaraScriptUsage::ParticleGPUComputeScript)
	{
		if (CachedScriptVMId.CompilerVersionID.IsValid() && CachedScriptVMId.BaseScriptCompileHash.IsValid())
		{
			CacheResourceShadersForRendering(false, true);
		}
		else
		{
			UE_LOG(LogNiagara, Warning,
				TEXT("Could not cache resource shaders for rendering for script %s because it had an invalid cached script id. This should be fixed by force recompiling the owning asset using the 'Full Rebuild' option and then saving the asset."),
				*GetPathName());
		}
	}

	InvalidateExecutionReadyParameterStores();

	AsyncOptimizeByteCode();

	OnVMScriptCompiled().Broadcast(this, InCompileId.ScriptVersionID);
}

void UNiagaraScript::InvalidateExecutionReadyParameterStores()
{
#if WITH_EDITORONLY_DATA
	// Make sure that we regenerate any parameter stores, since they must be kept in sync with the layout from script compilation.
	ScriptExecutionParamStoreCPU.Empty();
	ScriptExecutionParamStoreGPU.Empty();
#endif
}

void UNiagaraScript::RequestCompile(const FGuid& ScriptVersion, bool bForceCompile)
{
	FVersionedNiagaraScriptData* ScriptData = GetScriptData(ScriptVersion);
	if (ScriptData && (!AreScriptAndSourceSynchronized(ScriptVersion) || bForceCompile))
	{
		FNiagaraVMExecutableDataId& LastGeneratedVMId = GetLastGeneratedVMId(ScriptVersion);
		if (IsCompilable() == false)
		{
			CachedScriptVM.LastCompileStatus = ENiagaraScriptCompileStatus::NCS_Unknown;
			CachedScriptVMId = LastGeneratedVMId;
			return;
		}

		COOK_STAT(auto Timer = NiagaraScriptCookStats::UsageStats.TimeSyncWork());

		CachedScriptVM.LastCompileStatus = ENiagaraScriptCompileStatus::NCS_BeingCreated;

		TArray<TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe>> DependentRequests;
		TArray<uint8> OutData;
		INiagaraModule& NiagaraModule = FModuleManager::Get().LoadModuleChecked<INiagaraModule>(TEXT("Niagara"));
		TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe> RequestData = NiagaraModule.Precompile(this, ScriptVersion);

		if (RequestData.IsValid() == false)
		{
			COOK_STAT(Timer.TrackCyclesOnly());
			UE_LOG(LogNiagara, Error, TEXT("Failed to precompile %s.  This is due to unexpected invalid or broken data.  Additional details should be in the log."), *GetPathName());
			return;
		}

		// check the ddc first
		if (GetDerivedDataCacheRef().GetSynchronous(*GetNiagaraDDCKeyString(ScriptVersion), OutData, GetPathName()))
		{
			FNiagaraVMExecutableData ExeData;
			if (BinaryToExecData(this, OutData, ExeData))
			{
				COOK_STAT(Timer.AddHit(OutData.Num()));
				SetVMCompilationResults(LastGeneratedVMId, ExeData, RequestData.Get());
				return;
			}
		}

		ActiveCompileRoots.Empty();
		RequestData->GetReferencedObjects(ActiveCompileRoots);

		FNiagaraCompileOptions Options(GetUsage(), GetUsageId(), ScriptData->ModuleUsageBitmask, GetPathName(), GetFullName(), GetName());
		int32 JobHandle = NiagaraModule.StartScriptCompileJob(RequestData.Get(), Options);
		TSharedPtr<FNiagaraVMExecutableData> ExeData = NiagaraModule.GetCompileJobResult(JobHandle, true);
		if (ExeData)
		{
			SetVMCompilationResults(LastGeneratedVMId, *ExeData, RequestData.Get());
			// save result to the ddc
			if (ExecToBinaryData(this, OutData, *ExeData))
			{
				COOK_STAT(Timer.AddMiss(OutData.Num()));
				GetDerivedDataCacheRef().Put(*GetNiagaraDDCKeyString(ScriptVersion), OutData, GetPathName());
			}
		}
		ActiveCompileRoots.Empty();
	}
	else
	{
		UE_LOG(LogNiagara, Verbose, TEXT("Script '%s' is in-sync skipping compile.."), *GetFullName());
	}
}

bool UNiagaraScript::RequestExternallyManagedAsyncCompile(const TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe>& RequestData, FNiagaraVMExecutableDataId& OutCompileId, uint32& OutAsyncHandle)
{
	COOK_STAT(auto Timer = NiagaraScriptCookStats::UsageStats.TimeSyncWork());
	COOK_STAT(Timer.TrackCyclesOnly());

	FNiagaraVMExecutableDataId& LastGeneratedVMId = GetLastGeneratedVMId();
	OutCompileId = LastGeneratedVMId;

	FVersionedNiagaraScriptData* ScriptData = GetScriptData(LastGeneratedVMId.ScriptVersionID);
	if (ensure(ScriptData) && !AreScriptAndSourceSynchronized(LastGeneratedVMId.ScriptVersionID))
	{
		if (IsCompilable() == false)
		{
			OutAsyncHandle = (uint32)INDEX_NONE;
			CachedScriptVM.LastCompileStatus = ENiagaraScriptCompileStatus::NCS_Unknown;
			CachedScriptVMId = LastGeneratedVMId;
			return false;
		}

		INiagaraModule& NiagaraModule = FModuleManager::Get().LoadModuleChecked<INiagaraModule>(TEXT("Niagara"));
		CachedScriptVM.LastCompileStatus = ENiagaraScriptCompileStatus::NCS_BeingCreated;

		FNiagaraCompileOptions Options(GetUsage(), GetUsageId(), ScriptData->ModuleUsageBitmask, GetPathName(), GetFullName(), GetName());
		Options.AdditionalDefines = LastGeneratedVMId.AdditionalDefines;
		Options.AdditionalVariables = LastGeneratedVMId.AdditionalVariables;
		OutAsyncHandle = NiagaraModule.StartScriptCompileJob(RequestData.Get(), Options);
		UE_LOG(LogNiagara, Verbose, TEXT("Script '%s' is requesting compile.."), *GetFullName());
		return true;
	}
	else
	{
		OutAsyncHandle = (uint32)INDEX_NONE;
		UE_LOG(LogNiagara, Verbose, TEXT("Script '%s' is in-sync skipping compile.."), *GetFullName());
		return false;
	}
}
#endif

void UNiagaraScript::RaiseOnGPUCompilationComplete()
{
#if WITH_EDITORONLY_DATA
	OnGPUScriptCompiled().Broadcast(this, FGuid());
	FNiagaraSystemUpdateContext(this, true);

	if (UNiagaraEmitter* EmitterOwner = Cast<UNiagaraEmitter>(GetOuter()) )
	{
		EmitterOwner->CacheFromShaderCompiled();
	}
#endif
}

void UNiagaraScript::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	Super::GetAssetRegistryTags(OutTags);
#if WITH_EDITORONLY_DATA

	if (CustomAssetRegistryTagCache.IsSet() == false)
	{
		CustomAssetRegistryTagCache = TMap<FName, FString>();
	}

	const FVersionedNiagaraScriptData* ScriptData = GetLatestScriptData();

	// Dependencies
	TArray<FName> ProvidedDependencies = ScriptData ? ScriptData->ProvidedDependencies : ProvidedDependencies_DEPRECATED; 
	if (ProvidedDependencies.Num() > 0)
	{
		FName ProvidedDependenciesName = GET_MEMBER_NAME_CHECKED(FVersionedNiagaraScriptData, ProvidedDependencies);
		FString* ProvidedDependenciesTags = CustomAssetRegistryTagCache->Find(ProvidedDependenciesName);
		if(ProvidedDependenciesTags == nullptr)
		{
			ProvidedDependenciesTags = &CustomAssetRegistryTagCache->Add(ProvidedDependenciesName);
			for (FName ProvidedDependency : ProvidedDependencies)
			{
				ProvidedDependenciesTags->Append(ProvidedDependency.ToString() + ",");
			}
		}

		OutTags.Add(FAssetRegistryTag(ProvidedDependenciesName, *ProvidedDependenciesTags, FAssetRegistryTag::TT_Hidden));
	}

	// Highlights
	TArray<FNiagaraScriptHighlight> Highlights = ScriptData ? ScriptData->Highlights : Highlights_DEPRECATED; 
	if (Highlights.Num() > 0)
	{
		FName HighlightsName = GET_MEMBER_NAME_CHECKED(FVersionedNiagaraScriptData, Highlights);
		FString* HighlightsTags = CustomAssetRegistryTagCache->Find(HighlightsName);
		if (HighlightsTags == nullptr)
		{
			HighlightsTags = &CustomAssetRegistryTagCache->Add(HighlightsName);
			FNiagaraScriptHighlight::ArrayToJson(Highlights, *HighlightsTags);
		}

		OutTags.Add(FAssetRegistryTag(HighlightsName, *HighlightsTags, FAssetRegistryTag::TT_Hidden));
	}

	// Category
	FText CategoryText = ScriptData ? ScriptData->Category : Category_DEPRECATED;
	if (!CategoryText.IsEmpty())
	{
		FName CategoryName = GET_MEMBER_NAME_CHECKED(FVersionedNiagaraScriptData, Category);
		FString& CategoryTag = CustomAssetRegistryTagCache->FindOrAdd(CategoryName);
		CategoryTag = CategoryText.ToString();

		OutTags.Add(FAssetRegistryTag(CategoryName, CategoryTag, FAssetRegistryTag::TT_Alphabetical));
	}

	// Description
	FText DescriptionText = ScriptData ? ScriptData->Description : Description_DEPRECATED;
	if (!DescriptionText.IsEmpty())
	{
		FName DescriptionName = GET_MEMBER_NAME_CHECKED(FVersionedNiagaraScriptData, Description);
		FString& DescriptionTag = CustomAssetRegistryTagCache->FindOrAdd(DescriptionName);
		DescriptionTag = DescriptionText.ToString();

		OutTags.Add(FAssetRegistryTag(DescriptionName, DescriptionTag, FAssetRegistryTag::TT_Alphabetical));
	}

	// Keywords
	FText KeywordsText = ScriptData ? ScriptData->Keywords : Keywords_DEPRECATED;
	if (!KeywordsText.IsEmpty())
	{
		FName KeywordsName = GET_MEMBER_NAME_CHECKED(FVersionedNiagaraScriptData, Keywords);
		FString& KeywordsTag = CustomAssetRegistryTagCache->FindOrAdd(KeywordsName);
		KeywordsTag = KeywordsText.ToString();

		OutTags.Add(FAssetRegistryTag(KeywordsName, KeywordsTag, FAssetRegistryTag::TT_Alphabetical));
	}

	// Visibility
	ENiagaraScriptLibraryVisibility Visibility = ScriptData ? ScriptData->LibraryVisibility : LibraryVisibility_DEPRECATED;
	FName VisibilityName = GET_MEMBER_NAME_CHECKED(FVersionedNiagaraScriptData, LibraryVisibility);
	FString& VisibilityTag = CustomAssetRegistryTagCache->FindOrAdd(VisibilityName);
	UEnum* VisibilityEnum = StaticEnum<ENiagaraScriptLibraryVisibility>();
	VisibilityTag = VisibilityEnum->GetNameStringByValue((int64)Visibility);
	OutTags.Add(FAssetRegistryTag(VisibilityName, VisibilityTag, FAssetRegistryTag::TT_Alphabetical));

	// Usage bitmask
	int32 UsageBitmask = ScriptData ? ScriptData->ModuleUsageBitmask : ModuleUsageBitmask_DEPRECATED;
	FName UsageBitmaskName = GET_MEMBER_NAME_CHECKED(FVersionedNiagaraScriptData, ModuleUsageBitmask);
	FString& UsageBitmaskTag = CustomAssetRegistryTagCache->FindOrAdd(UsageBitmaskName);
	UsageBitmaskTag = FString::FromInt(UsageBitmask);
	OutTags.Add(FAssetRegistryTag(UsageBitmaskName, UsageBitmaskTag, FAssetRegistryTag::TT_Hidden));

	// Deprecation
	bool bDeprecated = ScriptData ? ScriptData->bDeprecated : bDeprecated_DEPRECATED;
	FName DeprecatedName = GET_MEMBER_NAME_CHECKED(FVersionedNiagaraScriptData, bDeprecated);
	FString& DeprecatedTag = CustomAssetRegistryTagCache->FindOrAdd(DeprecatedName);
	DeprecatedTag = FString::FromInt(bDeprecated);
	OutTags.Add(FAssetRegistryTag(DeprecatedName, DeprecatedTag, FAssetRegistryTag::TT_Hidden));

	// Suggested
	bool bSuggested = ScriptData ? ScriptData->bSuggested : false;
	FName SuggestedName = GET_MEMBER_NAME_CHECKED(FVersionedNiagaraScriptData, bSuggested);
	FString& SuggestedTag = CustomAssetRegistryTagCache->FindOrAdd(SuggestedName);
	SuggestedTag = FString::FromInt(bSuggested);
	OutTags.Add(FAssetRegistryTag(SuggestedName, SuggestedTag, FAssetRegistryTag::TT_Hidden));

	// Add the current custom version to the tags so that tags can be fixed up in the future without having to load
	// the whole asset.
	const int32 NiagaraVer = GetLinkerCustomVersion(FNiagaraCustomVersion::GUID);
	OutTags.Add(FAssetRegistryTag(NiagaraCustomVersionTagName, FString::FromInt(NiagaraVer), FAssetRegistryTag::TT_Hidden));
#endif
}

void UNiagaraScript::BeginDestroy()
{
	Super::BeginDestroy();

	if (!HasAnyFlags(RF_ClassDefaultObject) && ScriptResource)
	{
		if (!ScriptResource->QueueForRelease(ReleasedByRT))
		{
			// if there was nothing to release, then we don't need to wait for anything
			ReleasedByRT = true;
		}
	}
	else
	{
		ReleasedByRT = true;
	}
}

bool UNiagaraScript::IsReadyForFinishDestroy()
{
	const bool bIsReady = Super::IsReadyForFinishDestroy();

	return bIsReady && ReleasedByRT;
}

bool UNiagaraScript::IsEditorOnly() const
{
#if WITH_EDITOR
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return false;
	}

	if (const UNiagaraEmitter* EmitterOwner = Cast<const UNiagaraEmitter>(GetOuter()))
	{
		// we want to only cook scripts that are referenced by systems (as opposed to standalone scripts that may be getting
		// referenced via an emitter's parent, this will also take care of GPUScripts that are created for CPU emitters
		TArray<UNiagaraScript*> OwnerScripts;

		EmitterOwner->GetScripts(OwnerScripts, false);

		if (!OwnerScripts.Contains(this))
		{
			return true;
		}
	}
#endif
	return Super::IsEditorOnly();
}

void UNiagaraScript::ModifyCompilationEnvironment(struct FShaderCompilerEnvironment& OutEnvironment) const
{
	// Add all data interfaces
	TSet<UClass*> DIUniqueClasses;
	for ( const FNiagaraScriptDataInterfaceInfo& DataInterfaceInfo : CachedDefaultDataInterfaces )
	{
		if ( UNiagaraDataInterface* DataInterface = DataInterfaceInfo.DataInterface )
		{
			DIUniqueClasses.Add(DataInterface->GetClass());
		}
	}

	// For each data interface allow them to modify the compilation environment
	for ( UClass* DIClass : DIUniqueClasses)
	{
		if ( UNiagaraDataInterface* DICDO = CastChecked<UNiagaraDataInterface>(DIClass->GetDefaultObject(true)) )
		{
			DICDO->ModifyCompilationEnvironment(OutEnvironment);
		}
	}
}

#if WITH_EDITOR

void UNiagaraScript::BeginCacheForCookedPlatformData(const ITargetPlatform *TargetPlatform)
{
	if (ShouldCacheShadersForCooking(TargetPlatform))
	{
		// Commandlets like DerivedDataCacheCommandlet call BeginCacheForCookedPlatformData directly on objects. This may mean that
		// we have not properly gotten the HLSL script generated by the time that we get here. This does the awkward work of
		// waiting on the parent system to finish generating the HLSL before we can begin compiling it for the GPU.
		UNiagaraSystem* SystemOwner = FindRootSystem();
		if (SystemOwner)
		{
			SystemOwner->WaitForCompilationComplete();
		}

		if (HasIdsRequiredForShaderCaching() == false)
		{
			UE_LOG(LogNiagara, Warning,
				TEXT("Could not cache cooked shader for script %s because it had an invalid cached script id.  This should be fixed by running the console command fx.PreventSystemRecompile with the owning system asset path as the argument and then resaving the assets."),
				*GetPathName());
			return;
		}

		TArray<FName> DesiredShaderFormats;
		TargetPlatform->GetAllTargetedShaderFormats(DesiredShaderFormats);

		TArray<FNiagaraShaderScript*>& CachedScriptResourcesForPlatform = CachedScriptResourcesForCooking.FindOrAdd(TargetPlatform);

		// Cache for all the shader formats that the cooking target requires
		for (int32 FormatIndex = 0; FormatIndex < DesiredShaderFormats.Num(); FormatIndex++)
		{
			const EShaderPlatform LegacyShaderPlatform = ShaderFormatToLegacyShaderPlatform(DesiredShaderFormats[FormatIndex]);
			if (FNiagaraUtilities::SupportsComputeShaders(LegacyShaderPlatform))
			{
				CacheResourceShadersForCooking(LegacyShaderPlatform, CachedScriptResourcesForPlatform, TargetPlatform);
			}
		}
	}
}

bool UNiagaraScript::IsCachedCookedPlatformDataLoaded(const ITargetPlatform* TargetPlatform)
{
	if (ShouldCacheShadersForCooking(TargetPlatform) && HasIdsRequiredForShaderCaching())
	{
		bool bHasOutstandingCompilationRequests = false;
		if (UNiagaraSystem* SystemOwner = FindRootSystem())
		{
			bHasOutstandingCompilationRequests = SystemOwner->HasOutstandingCompilationRequests();
		}

		if (!bHasOutstandingCompilationRequests)
		{
			TArray<FName> DesiredShaderFormats;
			TargetPlatform->GetAllTargetedShaderFormats(DesiredShaderFormats);

			const TArray<FNiagaraShaderScript*>* CachedScriptResourcesForPlatform = CachedScriptResourcesForCooking.Find(TargetPlatform);
			if (CachedScriptResourcesForPlatform)
			{
				for (const auto& MaterialResource : *CachedScriptResourcesForPlatform)
				{
					if (MaterialResource->IsCompilationFinished() == false)
					{
						// For now, finish compilation here until we can make sure compilation is finished in the cook commandlet asyncronously before serialize
						MaterialResource->FinishCompilation();

						if (MaterialResource->IsCompilationFinished() == false)
						{
							return false;
						}
					}
				}

				return true;
			}
		}

		return false;
	}

	return true;
}

void UNiagaraScript::CacheResourceShadersForCooking(EShaderPlatform ShaderPlatform, TArray<FNiagaraShaderScript*>& InOutCachedResources, const ITargetPlatform* TargetPlatform)
{
	if (CanBeRunOnGpu())
	{
		// spawn and update are combined on GPU, so we only compile spawn scripts
		if (Usage == ENiagaraScriptUsage::ParticleGPUComputeScript)
		{
			ERHIFeatureLevel::Type TargetFeatureLevel = GetMaxSupportedFeatureLevel(ShaderPlatform);
			const auto FindExistingScriptPredicate = [&](const FNiagaraShaderScript* ExistingScript)
			{
				return ExistingScript->MatchesScript(TargetFeatureLevel, ShaderPlatform, CachedScriptVMId);
			};

			// see if the script has already been added before adding a new version
			if (InOutCachedResources.ContainsByPredicate(FindExistingScriptPredicate))
			{
				return;
			}

			FNiagaraShaderScript *ResourceToCache = nullptr;
			FNiagaraShaderScript* NewResource = AllocateResource();
			check(CachedScriptVMId.CompilerVersionID.IsValid());
			check(CachedScriptVMId.BaseScriptCompileHash.IsValid());

			NewResource->SetScript(this, TargetFeatureLevel, ShaderPlatform, CachedScriptVMId.CompilerVersionID, CachedScriptVMId.AdditionalDefines,
				CachedScriptVMId.GetAdditionalVariableStrings(),
				CachedScriptVMId.BaseScriptCompileHash,	CachedScriptVMId.ReferencedCompileHashes,
				CachedScriptVMId.bUsesRapidIterationParams, GetFriendlyName());
			ResourceToCache = NewResource;

			check(ResourceToCache);

			CacheShadersForResources(ResourceToCache, false, false, true, TargetPlatform);

			INiagaraModule& NiagaraModule = FModuleManager::GetModuleChecked<INiagaraModule>(TEXT("Niagara"));
			NiagaraModule.ProcessShaderCompilationQueue();

			InOutCachedResources.Add(ResourceToCache);
		}
	}
}



void UNiagaraScript::CacheShadersForResources(FNiagaraShaderScript* ResourceToCache, bool bApplyCompletedShaderMapForRendering, bool bForceRecompile, bool bCooking, const ITargetPlatform* TargetPlatform)
{
	if (CanBeRunOnGpu())
	{
		// When not running in the editor, the shaders are created in-sync (in the postload) to avoid update issues.
		const bool bSync = bCooking || !GIsEditor || GIsAutomationTesting;
		const bool bSuccess = ResourceToCache->CacheShaders(bApplyCompletedShaderMapForRendering, bForceRecompile, bSync, TargetPlatform);

#if defined(NIAGARA_SCRIPT_COMPILE_LOGGING_MEDIUM)
		if (!bSuccess)
		{
			UE_LOG(LogNiagara, Warning, TEXT("Failed to compile Niagara shader %s for platform %s."),
				*GetPathName(),
				*LegacyShaderPlatformToShaderFormat(ResourceToCache->GetShaderPlatform()).ToString());

			const TArray<FString>& CompileErrors = ResourceToCache->GetCompileErrors();
			for (int32 ErrorIndex = 0; ErrorIndex < CompileErrors.Num(); ErrorIndex++)
			{
				UE_ASSET_LOG(LogNiagara, Warning, this, TEXT("	%s"), *CompileErrors[ErrorIndex]);
			}
		}
#endif
	}
}

void UNiagaraScript::CacheResourceShadersForRendering(bool bRegenerateId, bool bForceRecompile)
{
	if (bRegenerateId)
	{
		// Regenerate this script's Id if requested
		for (int32 Idx = 0; Idx < ERHIFeatureLevel::Num; Idx++)
		{
			if (ScriptResourcesByFeatureLevel[Idx])
			{
				ScriptResourcesByFeatureLevel[Idx]->ReleaseShaderMap();
				ScriptResourcesByFeatureLevel[Idx] = nullptr;
			}
		}
	}

	//UpdateResourceAllocations();

	if (CanBeRunOnGpu())
	{
		// Need to make sure the owner supports GPU scripts, otherwise this is a wasted compile.
		UNiagaraScriptSourceBase* Source = GetLatestScriptData()->Source;
		if (Source && OwnerCanBeRunOnGpu())
		{
			ERHIFeatureLevel::Type CacheFeatureLevel = GMaxRHIFeatureLevel;
			const EShaderPlatform ShaderPlatform = GShaderPlatformForFeatureLevel[CacheFeatureLevel];

			ScriptResource->SetScript(this, CacheFeatureLevel, ShaderPlatform, CachedScriptVMId.CompilerVersionID, CachedScriptVMId.AdditionalDefines,
				CachedScriptVMId.GetAdditionalVariableStrings(),
				CachedScriptVMId.BaseScriptCompileHash, CachedScriptVMId.ReferencedCompileHashes,
				CachedScriptVMId.bUsesRapidIterationParams, GetFriendlyName());

			if (FNiagaraUtilities::SupportsComputeShaders(ShaderPlatform))
			{
				CacheShadersForResources(ScriptResource.Get(), true);
				ScriptResourcesByFeatureLevel[CacheFeatureLevel] = ScriptResource.Get();
			}
		}
		else
		{
			ScriptResource->Invalidate();
		}
	}
}

FString UNiagaraScript::GetFriendlyName() const
{
	UEnum* ENiagaraScriptUsageEnum = StaticEnum<ENiagaraScriptUsage>();

	UNiagaraEmitter* EmitterObject = GetTypedOuter<UNiagaraEmitter>();
	UObject* SystemObject = EmitterObject != nullptr ? EmitterObject->GetOuter() : nullptr;
	FString FriendlyName = FString::Printf(TEXT("%s/%s/%s"),
		SystemObject ? *FPaths::MakeValidFileName(SystemObject->GetName()) : TEXT("UnknownSystem"),
		EmitterObject ? *FPaths::MakeValidFileName(EmitterObject->GetUniqueEmitterName()) : TEXT("UnknownEmitter"),
		ENiagaraScriptUsageEnum ? *FPaths::MakeValidFileName(ENiagaraScriptUsageEnum->GetNameStringByValue((int64)Usage)) : TEXT("UnknownEnum")
	);

	return FriendlyName;
}

void UNiagaraScript::SyncAliases(const FNiagaraAliasContext& ResolveAliasesContext)
{
	// First handle any rapid iteration parameters...
	{
		TArray<FNiagaraVariable> Params;
		RapidIterationParameters.GetParameters(Params);
		for (FNiagaraVariable Var : Params)
		{
			FNiagaraVariable NewVar = FNiagaraUtilities::ResolveAliases(Var, ResolveAliasesContext);
			if (NewVar.GetName() != Var.GetName())
			{
				RapidIterationParameters.RenameParameter(Var, NewVar.GetName());
			}
		}
	}

	// Now handle any compile tags overall..
	{
		for (int32 i = 0; i < GetVMExecutableData().CompileTags.Num(); i++)
		{
			const FString& Name = GetVMExecutableData().CompileTags[i].StringValue;
			if (Name.Len())
			{
				FNiagaraVariable NewVar = FNiagaraUtilities::ResolveAliases(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), *Name), ResolveAliasesContext);
				if (NewVar.GetName() != *Name)
					GetVMExecutableData().CompileTags[i].StringValue = NewVar.GetName().ToString();
			}
		}
	}

	InvalidateExecutionReadyParameterStores();

	// Now handle any Parameters overall..
	for (int32 i = 0; i < GetVMExecutableData().Parameters.Parameters.Num(); i++)
	{
		if (GetVMExecutableData().Parameters.Parameters[i].IsValid() == false)
		{
			const FNiagaraVariable& InvalidParameter = GetVMExecutableData().Parameters.Parameters[i];
			UE_LOG(LogNiagara, Error, TEXT("Invalid parameter found while syncing script aliases.  Script: %s Parameter Name: %s Parameter Type: %s"),
				*GetPathName(), *InvalidParameter.GetName().ToString(), InvalidParameter.GetType().IsValid() ? *InvalidParameter.GetType().GetName() : TEXT("Unknown"));
			continue;
		}

		FNiagaraVariable Var = GetVMExecutableData().Parameters.Parameters[i];
		FNiagaraVariable NewVar = FNiagaraUtilities::ResolveAliases(Var, ResolveAliasesContext);
		if (NewVar.GetName() != Var.GetName())
		{
			GetVMExecutableData().Parameters.Parameters[i] = NewVar;
		}
	}

	// Sync up any simulation stage name references.
	for (int32 i = 0; i < GetVMExecutableData().SimulationStageMetaData.Num(); i++)
	{
		if (!GetVMExecutableData().SimulationStageMetaData[i].IterationSource.IsNone())
		{
			FNiagaraVariable Var(FNiagaraTypeDefinition(UNiagaraDataInterface::StaticClass()), GetVMExecutableData().SimulationStageMetaData[i].IterationSource);
			FNiagaraVariable NewVar = FNiagaraUtilities::ResolveAliases(Var, ResolveAliasesContext);
			if (NewVar.GetName() != Var.GetName())
			{
				GetVMExecutableData().SimulationStageMetaData[i].IterationSource = NewVar.GetName();
			}
		}

		for (int32 DestIdx = 0; DestIdx < GetVMExecutableData().SimulationStageMetaData[i].OutputDestinations.Num(); DestIdx++)
		{
			if (!GetVMExecutableData().SimulationStageMetaData[i].OutputDestinations[DestIdx].IsNone())
			{
				FNiagaraVariable Var(FNiagaraTypeDefinition(UNiagaraDataInterface::StaticClass()), GetVMExecutableData().SimulationStageMetaData[i].OutputDestinations[DestIdx]);
				FNiagaraVariable NewVar = FNiagaraUtilities::ResolveAliases(Var, ResolveAliasesContext);
				if (NewVar.GetName() != Var.GetName())
				{
					GetVMExecutableData().SimulationStageMetaData[i].OutputDestinations[DestIdx] = NewVar.GetName();
				}
			}
		}
	}

	// Also handle any data set mappings...
	auto Iterator = GetVMExecutableData().DataSetToParameters.CreateIterator();
	while (Iterator)
	{
		for (int32 i = 0; i < Iterator.Value().Parameters.Num(); i++)
		{
			FNiagaraVariable Var = Iterator.Value().Parameters[i];
			FNiagaraVariable NewVar = FNiagaraUtilities::ResolveAliases(Var, ResolveAliasesContext);
			if (NewVar.GetName() != Var.GetName())
			{
				Iterator.Value().Parameters[i] = NewVar;
			}
		}
		++Iterator;
	}
}

bool UNiagaraScript::SynchronizeExecutablesWithMaster(const UNiagaraScript* Script, const TMap<FString, FString>& RenameMap)
{
	FNiagaraVMExecutableDataId Id;
	ComputeVMCompilationId(Id, FGuid());

#if 1 // TODO Shaun... turn this on...
	if (Id == Script->GetVMExecutableDataCompilationId())
	{
		CachedScriptVM.Reset();
		ScriptResource->Invalidate();

		CachedScriptVM = Script->CachedScriptVM;
		CachedScriptVMId = Script->CachedScriptVMId;
		CachedParameterCollectionReferences = Script->CachedParameterCollectionReferences;
		CachedDefaultDataInterfaces.Empty();
		for (const FNiagaraScriptDataInterfaceInfo& Info : Script->CachedDefaultDataInterfaces)
		{
			FNiagaraScriptDataInterfaceInfo AddInfo;
			AddInfo = Info;
			AddInfo.DataInterface = CopyDataInterface(Info.DataInterface, this);
			CachedDefaultDataInterfaces.Add(AddInfo);
		}

		GenerateStatIDs();

		//SyncAliases(RenameMap);

		// Now go ahead and trigger the GPU script compile now that we have a compiled GPU hlsl script.
		if (Usage == ENiagaraScriptUsage::ParticleGPUComputeScript)
		{
			CacheResourceShadersForRendering(false, true);
		}

		OnVMScriptCompiled().Broadcast(this, FGuid()); // TODO what version id to use here?
		return true;
	}
#endif

	return false;
}

void UNiagaraScript::InvalidateCompileResults(const FString& Reason)
{
	UE_LOG(LogNiagara, Verbose, TEXT("InvalidateCompileResults Script:%s Reason:%s"), *GetPathName(), *Reason);
	CachedScriptVM.Reset();
	ScriptResource->Invalidate();
	CachedScriptVMId.Invalidate();
	GetLastGeneratedVMId().Invalidate();
	CachedDefaultDataInterfaces.Reset();
}

FText UNiagaraScript::GetDescription(const FGuid& VersionGuid)
{
	FVersionedNiagaraScriptData* ScriptData = GetScriptData(VersionGuid);
	return ScriptData == nullptr || ScriptData->Description.IsEmpty() ? FText::FromString(GetName()) : ScriptData->Description;
}


UNiagaraScript::FOnScriptCompiled& UNiagaraScript::OnVMScriptCompiled()
{
	return OnVMScriptCompiledDelegate;
}

UNiagaraScript::FOnScriptCompiled& UNiagaraScript::OnGPUScriptCompiled()
{
	return OnGPUScriptCompiledDelegate;
}

UNiagaraScript::FOnPropertyChanged& UNiagaraScript::OnPropertyChanged()
{
	return OnPropertyChangedDelegate;
}

void UNiagaraScript::ResolveParameterCollectionReferences()
{
	const int32 CollectionCount = CachedScriptVM.ParameterCollectionPaths.Num();

	if (CollectionCount)
	{
		const bool RoutingPostLoad = FUObjectThreadContext::Get().IsRoutingPostLoad;

		for (int32 CollectionIt = CollectionCount - 1; CollectionIt >= 0; --CollectionIt)
		{
			FSoftObjectPath SoftPath(CachedScriptVM.ParameterCollectionPaths[CollectionIt]);

			// try to find the object if it's already loaded
			UNiagaraParameterCollection* ParamCollection = Cast<UNiagaraParameterCollection>(SoftPath.ResolveObject());

			if (!ParamCollection && !RoutingPostLoad)
			{
				// if we're not in a PostLoad then we should be able to try to directly load the object
				ParamCollection = Cast<UNiagaraParameterCollection>(SoftPath.TryLoad());
			}

			if (ParamCollection)
			{
				CachedParameterCollectionReferences.AddUnique(ParamCollection);
				CachedScriptVM.ParameterCollectionPaths.RemoveAtSwap(CollectionIt);
			}
		}
	}
}

#endif

TArray<UNiagaraParameterCollection*>& UNiagaraScript::GetCachedParameterCollectionReferences()
{
#if WITH_EDITORONLY_DATA
	ResolveParameterCollectionReferences();
#endif

	return CachedParameterCollectionReferences;
}

NIAGARA_API bool UNiagaraScript::IsScriptCompilationPending(bool bGPUScript) const
{
	if (bGPUScript)
	{
		if (ScriptResource.IsValid())
		{
			if (ScriptResource->IsShaderMapComplete())
			{
				return false;
			}

			return !ScriptResource->IsCompilationFinished();
		}
	}
	else if (CachedScriptVM.IsValid())
	{
		return (CachedScriptVM.ByteCode.Num() == 0) && (CachedScriptVM.OptimizedByteCode.Num() == 0) && (CachedScriptVM.LastCompileStatus == ENiagaraScriptCompileStatus::NCS_BeingCreated || CachedScriptVM.LastCompileStatus == ENiagaraScriptCompileStatus::NCS_Unknown);
	}
	return false;
}

NIAGARA_API bool UNiagaraScript::DidScriptCompilationSucceed(bool bGPUScript) const
{
	if (bGPUScript)
	{
		if (ScriptResource.IsValid())
		{
			if (ScriptResource->IsShaderMapComplete())
			{
				return true;
			}

			if (ScriptResource->IsCompilationFinished())
			{
				// If we failed compilation, it would be finished and Shader would be null.
				return false;
			}
		}

		// If we are on a cooked platform and we have no shader we need to check if we disabled compute shader compilation
		// in which case we lie and say the compilation was ok otherwise the rest of the system will be disabled.
		//-TODO: Strip these emitters on cook instead
		if (FPlatformProperties::RequiresCookedData() && !FNiagaraUtilities::AllowComputeShaders(GShaderPlatformForFeatureLevel[GMaxRHIFeatureLevel]))
		{
			return true;
		}
	}
	else if (CachedScriptVM.IsValid())
	{
		return (CachedScriptVM.ByteCode.Num() != 0) || (CachedScriptVM.OptimizedByteCode.Num() != 0);
	}

	return false;
}

void UNiagaraScript::SerializeNiagaraShaderMaps(FArchive& Ar, int32 NiagaraVer, bool IsValidShaderScript)
{
#if WITH_EDITORONLY_DATA
	if (Ar.IsSaving() && IsValidShaderScript)
	{
		Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);
		Ar.UsingCustomVersion(FRenderingObjectVersion::GUID);
		Ar.UsingCustomVersion(FEditorObjectVersion::GUID);
		Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);

		int32 NumResourcesToSave = 0;
		const TArray<FNiagaraShaderScript*>* ScriptResourcesToSavePtr = nullptr;

		if (Ar.IsCooking())
		{
			ScriptResourcesToSavePtr = CachedScriptResourcesForCooking.Find(Ar.CookingTarget());
			if (ScriptResourcesToSavePtr != nullptr)
			{
				NumResourcesToSave = ScriptResourcesToSavePtr->Num();
			}
		}

		Ar << NumResourcesToSave;

		if (ScriptResourcesToSavePtr != nullptr)
		{
			for (FNiagaraShaderScript* ScriptResourceToSave : (*ScriptResourcesToSavePtr))
			{
				checkf(ScriptResourceToSave != nullptr, TEXT("Invalid script resource was cached"));
				ScriptResourceToSave->SerializeShaderMap(Ar);
			}
		}
	}
#endif

	if (Ar.IsLoading())
	{
		IsValidShaderScript = (NiagaraVer >= FNiagaraCustomVersion::NiagaraShaderMaps) && (NiagaraVer < FNiagaraCustomVersion::NiagaraShaderMapCooking || IsValidShaderScript);

		if (!IsValidShaderScript)
		{
			return;
		}

#if WITH_EDITORONLY_DATA
		const bool HasEditorData = !Ar.IsFilterEditorOnly();
		if (HasEditorData)
		{
			int32 NumLoadedResources = 0;
			Ar << NumLoadedResources;
			for (int32 i = 0; i < NumLoadedResources; i++)
			{
				FNiagaraShaderScript LoadedResource;
				LoadedResource.SerializeShaderMap(Ar);
				LoadedScriptResources.Add(LoadedResource);
			}
		}
		else
#endif
		{
			check(NiagaraVer >= FNiagaraCustomVersion::NiagaraShaderMaps);
			int32 ResourceCount = 0;
			Ar << ResourceCount;

			for (int32 ResourceIt = 0; ResourceIt < ResourceCount; ++ResourceIt)
			{
				FNiagaraShaderScript Resource;
				Resource.SerializeShaderMap(Ar);

				if (!ScriptResource)
				{
					if (FNiagaraShaderMap* ShaderMap = Resource.GetGameThreadShaderMap())
					{
						if (GMaxRHIShaderPlatform == ShaderMap->GetShaderPlatform())
						{
							ScriptResource = MakeUnique<FNiagaraShaderScript>(Resource);
						}
					}
				}
			}
		}
	}
}

void UNiagaraScript::ProcessSerializedShaderMaps()
{
	check(IsInGameThread());

	bool HasScriptResource = false;

#if WITH_EDITORONLY_DATA
	for (FNiagaraShaderScript& LoadedResource : LoadedScriptResources)
	{
		FNiagaraShaderMap* LoadedShaderMap = LoadedResource.GetGameThreadShaderMap();
		if (LoadedShaderMap && LoadedShaderMap->GetShaderPlatform() == GMaxRHIShaderPlatform)
		{
			HasScriptResource = true;
			ScriptResource = MakeUnique<FNiagaraShaderScript>(LoadedResource);
			ScriptResource->OnCompilationComplete().AddUniqueDynamic(this, &UNiagaraScript::RaiseOnGPUCompilationComplete);

			ERHIFeatureLevel::Type LoadedFeatureLevel = LoadedShaderMap->GetShaderMapId().FeatureLevel;
			if (!ScriptResourcesByFeatureLevel[LoadedFeatureLevel])
			{
				ScriptResourcesByFeatureLevel[LoadedFeatureLevel] = AllocateResource();
			}

			ScriptResourcesByFeatureLevel[LoadedFeatureLevel]->SetShaderMap(LoadedShaderMap);
			break;
		}
		else
		{
			LoadedResource.DiscardShaderMap();
		}
	}
#else
	HasScriptResource = ScriptResource.IsValid();
#endif

	if (HasScriptResource)
	{
		ScriptResource->SetDataInterfaceParamInfo(CachedScriptVM.DIParamInfo);
	}
}

FNiagaraShaderScript* UNiagaraScript::AllocateResource()
{
	return new FNiagaraShaderScript();
}

#if WITH_EDITORONLY_DATA
TArray<ENiagaraScriptUsage> FVersionedNiagaraScriptData::GetSupportedUsageContexts() const
{
	return UNiagaraScript::GetSupportedUsageContextsForBitmask(ModuleUsageBitmask);
}

TArray<ENiagaraScriptUsage> UNiagaraScript::GetSupportedUsageContextsForBitmask(int32 InModuleUsageBitmask, bool bIncludeHiddenUsages)
{
	TArray<ENiagaraScriptUsage> Supported;
	UEnum* UsageEnum = StaticEnum<ENiagaraScriptUsage>();
	for (int32 i = 0; i <= (int32)ENiagaraScriptUsage::SystemUpdateScript; i++)
	{
		int32 TargetBit = (InModuleUsageBitmask >> (int32)i) & 1;
		if (TargetBit == 1 && (bIncludeHiddenUsages || UsageEnum->HasMetaData(TEXT("Hidden"), i) == false))
		{
			Supported.Add((ENiagaraScriptUsage)i);
		}
	}
	return Supported;
}

bool UNiagaraScript::IsSupportedUsageContextForBitmask(int32 InModuleUsageBitmask, ENiagaraScriptUsage InUsageContext, bool bIncludeHiddenUsages)
{
	TArray<ENiagaraScriptUsage> SupportedUsages = GetSupportedUsageContextsForBitmask(InModuleUsageBitmask, bIncludeHiddenUsages);
	for (ENiagaraScriptUsage SupportedUsage : SupportedUsages)
	{
		if (UNiagaraScript::IsEquivalentUsage(InUsageContext, SupportedUsage))
		{
			return true;
		}
	}
	return false;
}

bool UNiagaraScript::ContainsEquivilentUsage(const TArray<ENiagaraScriptUsage>& Usages, ENiagaraScriptUsage InUsage)
{
	for (ENiagaraScriptUsage Usage : Usages)
	{
		if (IsEquivalentUsage(Usage, InUsage))
		{
			return true;
		}
	}
	return false;
}

#endif

bool UNiagaraScript::CanBeRunOnGpu()const
{

	if (Usage != ENiagaraScriptUsage::ParticleGPUComputeScript)
	{
		return false;
	}
	if (!CachedScriptVM.IsValid())
	{
		return false;
	}
	for (const FNiagaraScriptDataInterfaceCompileInfo& InterfaceInfo : CachedScriptVM.DataInterfaceInfo)
	{
		if (InterfaceInfo.Type.IsValid() && !InterfaceInfo.CanExecuteOnTarget(ENiagaraSimTarget::GPUComputeSim))
		{
			return false;
		}
	}
	return true;
}


bool UNiagaraScript::OwnerCanBeRunOnGpu() const
{
	if (UNiagaraEmitter* Emitter = GetTypedOuter<UNiagaraEmitter>())
	{
		if (Emitter->SimTarget == ENiagaraSimTarget::GPUComputeSim)
		{
			return true;
		}
	}

	return false;
}

bool UNiagaraScript::LegacyCanBeRunOnGpu() const
{
	if (UNiagaraEmitter* Emitter = GetTypedOuter<UNiagaraEmitter>())
	{
		if (Emitter->SimTarget == ENiagaraSimTarget::CPUSim)
		{
			return false;
		}

		if (!IsParticleSpawnScript())
		{
			return false;
		}

		return true;
	}
	return false;
}


#if WITH_EDITORONLY_DATA
FGuid UNiagaraScript::GetBaseChangeID(const FGuid& VersionGuid) const
{
	UNiagaraScriptSourceBase* Source = GetScriptData(VersionGuid)->Source;
	return Source->GetChangeID(); 
}

ENiagaraScriptCompileStatus UNiagaraScript::GetLastCompileStatus() const
{
	if (CachedScriptVM.IsValid())
	{
		return CachedScriptVM.LastCompileStatus;
	}
	return ENiagaraScriptCompileStatus::NCS_Unknown;
}
#endif

bool UNiagaraScript::UsesCollection(const UNiagaraParameterCollection* Collection)const
{
	if (CachedScriptVM.IsValid())
	{
		if (CachedParameterCollectionReferences.Contains(Collection))
		{
			return true;
		}
#if WITH_EDITORONLY_DATA
		FSoftObjectPath SoftPath(Collection);

		if (CachedScriptVM.ParameterCollectionPaths.ContainsByPredicate([&](const FString& CollectionPath) { return SoftPath == FSoftObjectPath(CollectionPath); }))
		{
			return true;
		}
#endif
	}
	return false;
}

bool UNiagaraScript::HasValidParameterBindings() const
{
	const int32 RapidIterationParameterSize = RapidIterationParameters.GetParameterDataArray().Num();
	const int32 ScriptExecutionParameterSize = ScriptExecutionParamStore.GetParameterDataArray().Num();

	for (const auto& Binding : ScriptExecutionBoundParameters)
	{
		const int32 ParameterSize = Binding.Parameter.GetSizeInBytes();

		if (((Binding.SrcOffset + ParameterSize) > RapidIterationParameterSize)
			|| ((Binding.DestOffset + ParameterSize) > ScriptExecutionParameterSize))
		{
			return false;
		}
	}

	return true;
}
