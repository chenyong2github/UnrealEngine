// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraCommon.h"

#include "Misc/StringBuilder.h"
#include "NiagaraComponent.h"
#include "NiagaraConstants.h"
#include "NiagaraCustomVersion.h"
#include "NiagaraParameterCollection.h"
#include "NiagaraParameterDefinitionsBase.h"
#include "NiagaraScriptSourceBase.h"
#include "NiagaraStats.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraWorldManager.h"
#include "String/ParseTokens.h"
#include "UObject/Class.h"

DECLARE_CYCLE_STAT(TEXT("Niagara - Utilities - PrepareRapidIterationParameters"), STAT_Niagara_Utilities_PrepareRapidIterationParameters, STATGROUP_Niagara);


//////////////////////////////////////////////////////////////////////////

int32 GNiagaraAllowComputeShaders = 1;
FAutoConsoleVariableRef CVarAllowComputeShaders(
	TEXT("fx.NiagaraAllowComputeShaders"),
	GNiagaraAllowComputeShaders,
	TEXT("If true, allow the usage compute shaders within Niagara."),
	ECVF_Default);

int32 GNiagaraAllowGPUParticles = 1;
FAutoConsoleVariableRef CVarAllowGPUParticles(
	TEXT("fx.NiagaraAllowGPUParticles"),
	GNiagaraAllowGPUParticles,
	TEXT("If true, allow the usage of GPU particles for Niagara."),
	ECVF_Default);

int32 GNiagaraGPUCulling = 1;
FAutoConsoleVariableRef CVarNiagaraGPUCulling(
	TEXT("Niagara.GPUCulling"),
	GNiagaraGPUCulling,
	TEXT("Whether to frustum and camera distance cull particles on the GPU"),
	ECVF_Default
);

int32 GNiagaraMaxStatInstanceReports = 20;
FAutoConsoleVariableRef CVarMaxStatInstanceReportss(
    TEXT("fx.NiagaraMaxStatInstanceReports"),
    GNiagaraMaxStatInstanceReports,
    TEXT("The max number of different instances from which stat reports are aggregated."),
    ECVF_Default);

static int32 GbMaxStatRecordedFrames = 30;
static FAutoConsoleVariableRef CVarMaxStatRecordedFrames(
    TEXT("fx.Niagara.MaxStatRecordedFrames"),
    GbMaxStatRecordedFrames,
    TEXT("The number of frames recorded for the stat performance display of niagara cpu and gpu scripts. \n"),
    ECVF_Default
);

static int32 GNiagaraLogVerboseWarnings = WITH_EDITOR ? 1 : 0;
static FAutoConsoleVariableRef CVarNiagaraLogVerboseWarnings(
	TEXT("fx.Niagara.LogVerboseWarnings"),
	GNiagaraLogVerboseWarnings,
	TEXT("Enable to output more verbose warnings to the log file, these are considered dismissable warnings but may provide information when debugging.\n")
	TEXT("Default is enabled in editor builds and disabled in non editor builds.\n"),
	ECVF_Default
);

//////////////////////////////////////////////////////////////////////////

FString FNiagaraTypeHelper::ToString(const uint8* ValueData, const UObject* StructOrEnum)
{
	FString Ret;
	if (const UEnum* Enum = Cast<const UEnum>(StructOrEnum))
	{
		Ret = Enum->GetNameStringByValue(*(int32*)ValueData);
	}
	else if (const UScriptStruct* Struct = Cast<const UScriptStruct>(StructOrEnum))
	{
		if (Struct == FNiagaraTypeDefinition::GetFloatStruct())
		{
			Ret += FString::Printf(TEXT("%g "), *(float*)ValueData);
		}
		else if (Struct == FNiagaraTypeDefinition::GetIntStruct())
		{
			Ret += FString::Printf(TEXT("%d "), *(int32*)ValueData);
		}
		else if (Struct == FNiagaraTypeDefinition::GetBoolStruct())
		{
			int32 Val = *(int32*)ValueData;
			Ret += Val == 0xFFFFFFFF ? (TEXT("True")) : (Val == 0x0 ? TEXT("False") : TEXT("Invalid"));
		}
		else
		{
			for (TFieldIterator<FProperty> PropertyIt(Struct, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
			{
				const FProperty* Property = *PropertyIt;
				const uint8* PropPtr = ValueData + PropertyIt->GetOffset_ForInternal();
				if (Property->IsA(FFloatProperty::StaticClass()))
				{
					Ret += FString::Printf(TEXT("%s: %g "), *Property->GetNameCPP(), *(float*)PropPtr);
				}
				else if (Property->IsA(FUInt16Property::StaticClass()))
				{
					FFloat16 Val = *(FFloat16*)PropPtr;
					Ret += FString::Printf(TEXT("%s: %f "), *Property->GetNameCPP(), Val.GetFloat());
				}
				else if (Property->IsA(FIntProperty::StaticClass()))
				{
					Ret += FString::Printf(TEXT("%s: %d "), *Property->GetNameCPP(), *(int32*)PropPtr);
				}
				else if (Property->IsA(FBoolProperty::StaticClass()))
				{
					int32 Val = *(int32*)ValueData;
					FString BoolStr = Val == 0xFFFFFFFF ? (TEXT("True")) : (Val == 0x0 ? TEXT("False") : TEXT("Invalid"));
					Ret += FString::Printf(TEXT("%s: %d "), *Property->GetNameCPP(), *BoolStr);
				}
				else if (const FStructProperty* StructProp = CastFieldChecked<const FStructProperty>(Property))
				{
					Ret += FString::Printf(TEXT("%s: (%s) "), *Property->GetNameCPP(), *FNiagaraTypeHelper::ToString(PropPtr, StructProp->Struct));
				}
				else
				{
					check(false);
					Ret += TEXT("Unknown Type");
				}
			}
		}
	}
	return Ret;
}
//////////////////////////////////////////////////////////////////////////

FNiagaraSystemUpdateContext::~FNiagaraSystemUpdateContext()
{
	CommitUpdate();
}

void FNiagaraSystemUpdateContext::CommitUpdate()
{
	for (UNiagaraSystem* Sys : SystemSimsToDestroy)
	{
		if(Sys)
		{
			FNiagaraWorldManager::DestroyAllSystemSimulations(Sys);
		}		
	}
	SystemSimsToDestroy.Empty();

	for (UNiagaraComponent* Comp : ComponentsToReInit)
	{
		if (Comp)
		{
			Comp->ReinitializeSystem();
			Comp->EndUpdateContextReset();
			PostWork.ExecuteIfBound(Comp);
		}
	}
	ComponentsToReInit.Empty();

	for (UNiagaraComponent* Comp : ComponentsToReset)
	{
		if (Comp)
		{
			Comp->ResetSystem();
			Comp->EndUpdateContextReset();
			PostWork.ExecuteIfBound(Comp);
		}
	}
	ComponentsToReset.Empty();

	for (UNiagaraComponent* Comp : ComponentsToNotifySimDestroy)
	{
		if (Comp)
		{
			if (FNiagaraSystemInstance* SystemInstance = Comp->GetSystemInstance())
			{
				SystemInstance->OnSimulationDestroyed();
			}			
			Comp->EndUpdateContextReset();
			PostWork.ExecuteIfBound(Comp);
		}
	}
	ComponentsToReInit.Empty();
}

void FNiagaraSystemUpdateContext::AddAll(bool bReInit)
{
	for (TObjectIterator<UNiagaraComponent> It; It; ++It)
	{
		UNiagaraComponent* Comp = *It;
		check(Comp);

		AddInternal(Comp, bReInit);
	}
}

void FNiagaraSystemUpdateContext::Add(UNiagaraComponent* Component, bool bReInit)
{
	check(Component);
	AddInternal(Component, bReInit);
}

void FNiagaraSystemUpdateContext::Add(const UNiagaraSystem* System, bool bReInit)
{
	for (TObjectIterator<UNiagaraComponent> It; It; ++It)
	{
		UNiagaraComponent* Comp = *It;
		check(Comp);
		if (Comp->GetAsset() == System)
		{
			AddInternal(Comp, bReInit);
		}
	}
}
#if WITH_EDITORONLY_DATA

void FNiagaraSystemUpdateContext::Add(const UNiagaraEmitter* Emitter, bool bReInit)
{
	for (TObjectIterator<UNiagaraComponent> It; It; ++It)
	{
		UNiagaraComponent* Comp = *It;
		check(Comp);
		FNiagaraSystemInstance* SystemInst = Comp->GetSystemInstance();
		if (SystemInst && SystemInst->UsesEmitter(Emitter))
		{
			AddInternal(Comp, bReInit);
		}
	}
}

void FNiagaraSystemUpdateContext::Add(const UNiagaraScript* Script, bool bReInit)
{
	for (TObjectIterator<UNiagaraComponent> It; It; ++It)
	{
		UNiagaraComponent* Comp = *It;
		check(Comp);
		UNiagaraSystem* System = Comp->GetAsset();
		if (System && System->UsesScript(Script))
		{
			AddInternal(Comp, bReInit);
		}
	}
}

void FNiagaraSystemUpdateContext::Add(const UNiagaraParameterCollection* Collection, bool bReInit)
{
	for (TObjectIterator<UNiagaraComponent> It; It; ++It)
	{
		UNiagaraComponent* Comp = *It;
		check(Comp);
		FNiagaraSystemInstance* SystemInst = Comp->GetSystemInstance();
		if (SystemInst && SystemInst->UsesCollection(Collection))
		{
			AddInternal(Comp, bReInit);
		}
	}
}
#endif

void FNiagaraSystemUpdateContext::AddInternal(UNiagaraComponent* Comp, bool bReInit)
{
	PreWork.ExecuteIfBound(Comp);

	Comp->BeginUpdateContextReset();

	// Ensure we wait for any concurrent work to complete
	if (FNiagaraSystemInstance* SystemInstance = Comp->GetSystemInstance())
	{
		SystemInstance->WaitForConcurrentTickAndFinalize();
		if (Comp->IsPendingKill())
		{
			return;
		}
	}

	if (bReInit && bDestroySystemSim)
	{
		//Always destroy the system sims on a reinit, even if we're not reactivating the component.
		SystemSimsToDestroy.AddUnique(Comp->GetAsset());
	}

	bool bIsActive = (Comp->IsActive() && Comp->GetRequestedExecutionState() == ENiagaraExecutionState::Active) || Comp->IsRegisteredWithScalabilityManager();
	
	if (bDestroyOnAdd)
	{
		Comp->DeactivateImmediate();
	}

	if (bIsActive || bOnlyActive == false)
	{
		if (bReInit)
		{
			ComponentsToReInit.AddUnique(Comp);
		}
		else
		{
			ComponentsToReset.AddUnique(Comp);
		}		
		return;
	}
	else if (bReInit)
	{
		// Inactive components that have references to the simulations we're about to destroy need to clear them out in case they get reactivated.
		// Otherwise, they will hold reference and bind or remain bound to a system simulation that has been abandoned by the world manager
		if (FNiagaraSystemInstance* SystemInstance = Comp->GetSystemInstance())
		{
			if (!SystemInstance->IsSolo() && SystemInstance->GetSystemSimulation().IsValid())
			{
				ComponentsToNotifySimDestroy.Add(Comp);
				return;
			}
		}
	}
	
	// If we got here, we didn't add the component to any list, so end the reset immediately
	Comp->EndUpdateContextReset();
}

//////////////////////////////////////////////////////////////////////////

#if STATS
FStatExecutionTimer::FStatExecutionTimer()
{
	CapturedTimings.Reserve(GbMaxStatRecordedFrames);
}

void FStatExecutionTimer::AddTiming(float NewTiming)
{
	if (CapturedTimings.Num() < GbMaxStatRecordedFrames)
	{
		CapturedTimings.Add(NewTiming);
	}
	else if (CapturedTimings.IsValidIndex(CurrentIndex))
	{
		CapturedTimings[CurrentIndex] = NewTiming;
		CurrentIndex = (CurrentIndex + 1) % GbMaxStatRecordedFrames;
	}
}

void FNiagaraStatDatabase::AddStatCapture(FStatReportKey ReportKey, TMap<TStatIdData const*, float> CapturedData)
{
	if (CapturedData.Num() == 0)
	{
		return;
	}
	FScopeLock Lock(&CriticalSection);
	if (StatCaptures.Num() > GNiagaraMaxStatInstanceReports)
	{
		// we don't need data from too many emitter instances. If we already have enough, delete an old data point.
		TArray<FStatReportKey> Keys;
		StatCaptures.GetKeys(Keys);
		StatCaptures.Remove(Keys[FMath::RandHelper(Keys.Num())]);
	}

	TMap<TStatIdData const*, FStatExecutionTimer>& InstanceData = StatCaptures.FindOrAdd(ReportKey);
	for (const auto& Entry : CapturedData)
	{		
		InstanceData.FindOrAdd(Entry.Key).AddTiming(Entry.Value);
	}
}

void FNiagaraStatDatabase::ClearStatCaptures()
{
	FScopeLock Lock(&CriticalSection);
	StatCaptures.Empty();
}

float FNiagaraStatDatabase::GetRuntimeStat(FName StatName, ENiagaraScriptUsage Usage, ENiagaraStatEvaluationType EvaluationType)
{
	FScopeLock Lock(&CriticalSection);
	int32 ValueCount = 0;
	float Sum = 0;
	float Max = 0;
	for (const auto& EmitterEntry : StatCaptures)
	{
		if (Usage != EmitterEntry.Key.Value)
		{
			continue;
		}
		for (const TTuple<TStatIdData const*, FStatExecutionTimer>& StatEntry : EmitterEntry.Value)
		{
			if (MinimalNameToName(StatEntry.Key->Name) == StatName)
			{
				ValueCount = StatEntry.Value.CapturedTimings.Num();
				for (int i = 0; i < ValueCount; i++)
				{
					float Value = StatEntry.Value.CapturedTimings[i];
					Max = FMath::Max(Max, Value);
					Sum += Value;
				}
				break;
			}
		}
	}
	if (EvaluationType == ENiagaraStatEvaluationType::Maximum)
	{
		return Max;
	}
	return ValueCount == 0 ? 0 : Sum / ValueCount;
}

float FNiagaraStatDatabase::GetRuntimeStat(ENiagaraScriptUsage Usage, ENiagaraStatEvaluationType EvaluationType)
{
	FScopeLock Lock(&CriticalSection);
	int32 ValueCount = 0;
	float Sum = 0;
	float Max = 0;
	for (const auto& EmitterEntry : StatCaptures)
	{
		if (Usage != EmitterEntry.Key.Value)
		{
			continue;
		}
		for (const TTuple<TStatIdData const*, FStatExecutionTimer>& StatEntry : EmitterEntry.Value)
		{
			for (int i = 0; i < StatEntry.Value.CapturedTimings.Num(); i++)
			{
				float Value = StatEntry.Value.CapturedTimings[i];
				Max = FMath::Max(Max, Value);
				Sum += Value;
				ValueCount++;
			}
		}
	}
	if (EvaluationType == ENiagaraStatEvaluationType::Maximum)
	{
		return Max;
	}
	return ValueCount == 0 ? 0 : Sum / ValueCount;
}

TMap<ENiagaraScriptUsage, TSet<FName>> FNiagaraStatDatabase::GetAvailableStatNames()
{
	FScopeLock Lock(&CriticalSection);
	TMap<ENiagaraScriptUsage, TSet<FName>> Result;
	for (const auto& EmitterEntry : StatCaptures)
	{
		for (const auto& StatEntry : EmitterEntry.Value)
		{
			ENiagaraScriptUsage Usage = EmitterEntry.Key.Value; 
			Result.FindOrAdd(Usage).Add(MinimalNameToName(StatEntry.Key->Name));
		}
	}
	return Result;
}
#endif

void  FNiagaraVariableAttributeBinding::SetValue(const FName& InValue, const UNiagaraEmitter* InEmitter, ENiagaraRendererSourceDataMode InSourceMode)
{
	RootVariable.SetName(InValue);

	const bool bIsRootParticleValue = RootVariable.IsInNameSpace(FNiagaraConstants::ParticleAttributeNamespace);
	const bool bIsRootUnaliasedEmitterValue = RootVariable.IsInNameSpace(FNiagaraConstants::EmitterNamespace);
	const bool bIsAliasedEmitterValue = InEmitter ? RootVariable.IsInNameSpace(InEmitter->GetUniqueEmitterName()) : false;
	const bool bIsRootSystemValue = RootVariable.IsInNameSpace(FNiagaraConstants::SystemNamespace);
	const bool bIsRootUserValue = RootVariable.IsInNameSpace(FNiagaraConstants::UserNamespace);
	const bool bIsStackContextValue = RootVariable.IsInNameSpace(FNiagaraConstants::StackContextNamespace);

	// We clear out the namespace for the sourcemode so that we can keep the values up-to-date if you change the source mode.
	if ((bIsStackContextValue || bIsRootParticleValue) && InSourceMode == ENiagaraRendererSourceDataMode::Particles)
	{
		RootVariable.SetName(FNiagaraConstants::GetAttributeAsParticleDataSetKey(RootVariable).GetName());
		BindingSourceMode = ENiagaraBindingSource::ImplicitFromSource;
	}
	else if ((bIsStackContextValue || bIsRootUnaliasedEmitterValue) && InSourceMode == ENiagaraRendererSourceDataMode::Emitter)
	{
		RootVariable.SetName(FNiagaraConstants::GetAttributeAsEmitterDataSetKey(RootVariable).GetName());
		BindingSourceMode = ENiagaraBindingSource::ImplicitFromSource;
	}
	else if (bIsAliasedEmitterValue && InSourceMode == ENiagaraRendererSourceDataMode::Emitter)
	{
		// First, replace unaliased emitter namespace with "Emitter" namespace
		RootVariable = FNiagaraUtilities::ResolveAliases(RootVariable, FNiagaraAliasContext()
			.ChangeEmitterNameToEmitter(InEmitter->GetUniqueEmitterName()));

		// Now strip out "Emitter"
		RootVariable.SetName(FNiagaraConstants::GetAttributeAsEmitterDataSetKey(RootVariable).GetName());
		BindingSourceMode = ENiagaraBindingSource::ImplicitFromSource;
	}
	else if (bIsRootParticleValue)
	{
		RootVariable.SetName(FNiagaraConstants::GetAttributeAsParticleDataSetKey(RootVariable).GetName());
		BindingSourceMode = ENiagaraBindingSource::ExplicitParticles;
	}
	else if (bIsRootUnaliasedEmitterValue || bIsAliasedEmitterValue)
	{
		// First, replace unaliased emitter namespace with "Emitter" namespace
		if (InEmitter != nullptr)
		{
			RootVariable = FNiagaraUtilities::ResolveAliases(RootVariable, FNiagaraAliasContext()
				.ChangeEmitterNameToEmitter(InEmitter->GetUniqueEmitterName()));
		}

		// Now strip out "Emitter"
		RootVariable.SetName(FNiagaraConstants::GetAttributeAsEmitterDataSetKey(RootVariable).GetName());
		BindingSourceMode = ENiagaraBindingSource::ExplicitEmitter;
	}
	else if (bIsRootSystemValue)
	{
		BindingSourceMode = ENiagaraBindingSource::ExplicitSystem;
	}
	else if (bIsRootUserValue)
	{
		BindingSourceMode = ENiagaraBindingSource::ExplicitUser;
	}
	else if (bIsStackContextValue)
	{
		ensureMsgf(!bIsStackContextValue, TEXT("Should not get to this point! Should be covered by first two branch expresssions."));
	}

	CacheValues(InEmitter, InSourceMode);	
}

void FNiagaraVariableAttributeBinding::SetAsPreviousValue(const FNiagaraVariableBase& Src, const UNiagaraEmitter* InEmitter, ENiagaraRendererSourceDataMode InSourceMode)
{
	static const FString PreviousNamespace = FNiagaraConstants::PreviousNamespace.ToString();

	RootVariable = ParamMapVariable = DataSetVariable = Src;

	// Split out the name and it's namespace
	TArray<FString> SplitName;
	Src.GetName().ToString().ParseIntoArray(SplitName, TEXT("."));

	// If the name already contains a "Previous" in the name, just go with that
	bool bIsPrev = false;
	for (const FString& Split : SplitName)
	{
		if (Split.Equals(PreviousNamespace, ESearchCase::IgnoreCase))
		{
			bIsPrev = true;
			break;
		}
	}

	if (bIsPrev)
	{
		SetValue(Src.GetName(), InEmitter, InSourceMode);
	}
	else
	{
		// insert "Previous" into the name, after the first namespace. Or the beginning, if it has none
		const int32 Location = SplitName.Num() > 1 ? 1 : 0;
		SplitName.Insert(PreviousNamespace, Location);

		FString PrevName = FString::Join(SplitName, TEXT("."));
		SetValue(*PrevName, InEmitter, InSourceMode);
	}
}

void FNiagaraVariableAttributeBinding::SetAsPreviousValue(const FNiagaraVariableAttributeBinding& Src, const UNiagaraEmitter* InEmitter, ENiagaraRendererSourceDataMode InSourceMode)
{
	static const FString PreviousNamespace = FNiagaraConstants::PreviousNamespace.ToString();

	RootVariable = ParamMapVariable = DataSetVariable = Src.RootVariable;

	// Split out the name and it's namespace
	TArray<FString> SplitName;
	Src.RootVariable.GetName().ToString().ParseIntoArray(SplitName, TEXT("."));

	// If the name already contains a "Previous" in the name, just go with that
	bool bIsPrev = false;
	for (const FString& Split : SplitName)
	{
		if (Split.Equals(PreviousNamespace, ESearchCase::IgnoreCase))
		{
			bIsPrev = true;
			break;
		}
	}

	if (bIsPrev)
	{
		SetValue(Src.RootVariable.GetName(), InEmitter, InSourceMode);
	}
	else
	{
		// insert "Previous" into the name, after the first namespace. Or the beginning, if it has none
		const int32 Location = SplitName.Num() > 1 ? 1 : 0;
		SplitName.Insert(PreviousNamespace, Location);

		FString PrevName = FString::Join(SplitName, TEXT("."));
		SetValue(*PrevName, InEmitter, InSourceMode);
	}
}

void FNiagaraVariableAttributeBinding::Setup(const FNiagaraVariableBase& InRootVar, const FNiagaraVariable& InDefaultValue, ENiagaraRendererSourceDataMode InSourceMode)
{
	RootVariable = InRootVar;
	if (InDefaultValue.IsDataAllocated() && InDefaultValue.GetType() == InRootVar.GetType())
	{
		RootVariable.SetData(InDefaultValue.GetData());
	}
	SetValue(InRootVar.GetName(), nullptr, InSourceMode);
}

#if WITH_EDITORONLY_DATA
FString FNiagaraVariableAttributeBinding::GetDefaultValueString() const
{
	FString DefaultValueStr = RootVariable.GetName().ToString();

	if (!RootVariable.GetName().IsValid() || RootVariable.IsDataAllocated() == true)
	{
		DefaultValueStr = RootVariable.GetType().ToString(RootVariable.GetData());
		DefaultValueStr.TrimEndInline();
	}
	return DefaultValueStr;
}

#endif

void FNiagaraVariableAttributeBinding::PostLoad(ENiagaraRendererSourceDataMode InSourceMode)
{
#if WITH_EDITORONLY_DATA
	if (BoundVariable.IsValid())
	{
		RootVariable.SetType(DataSetVariable.GetType()); //Sometimes the BoundVariable was bogus in the past. THe DataSet shouldn't be though.
		SetValue(BoundVariable.GetName(), nullptr, InSourceMode);
		BoundVariable = FNiagaraVariable();
	}
#endif

}

void FNiagaraVariableAttributeBinding::Dump() const
{
	UE_LOG(LogNiagara, Log, TEXT("PostLoad for FNiagaraVariableAttributeBinding...."));
	UE_LOG(LogNiagara, Log, TEXT("ParamMapVariable: %s %s"), *ParamMapVariable.GetName().ToString(), *ParamMapVariable.GetType().GetName());
	UE_LOG(LogNiagara, Log, TEXT("DataSetVariable: %s %s"), *DataSetVariable.GetName().ToString(), *DataSetVariable.GetType().GetName());
	UE_LOG(LogNiagara, Log, TEXT("RootVariable: %s %s"), *RootVariable.GetName().ToString(), *RootVariable.GetType().GetName());
#if WITH_EDITORONLY_DATA
	UE_LOG(LogNiagara, Log, TEXT("BoundVariable: %s %s"), *BoundVariable.GetName().ToString(), *BoundVariable.GetType().GetName());
	UE_LOG(LogNiagara, Log, TEXT("CachedDisplayName: %s"), *CachedDisplayName.ToString());
#endif
	UE_LOG(LogNiagara, Log, TEXT("BindingSourceMode: %d     bBindingExistsOnSource: %d     bIsCachedParticleValue: %d"), (int32)BindingSourceMode.GetValue(),
		bBindingExistsOnSource ? 1 : 0, bIsCachedParticleValue ? 1 : 0 );
}

void FNiagaraVariableAttributeBinding::ResetToDefault(const FNiagaraVariableAttributeBinding& InOther, const UNiagaraEmitter* InEmitter, ENiagaraRendererSourceDataMode InSourceMode)
{
	if (InOther.BindingSourceMode == ImplicitFromSource || InOther.BindingSourceMode == ENiagaraBindingSource::ExplicitEmitter || InOther.BindingSourceMode == ENiagaraBindingSource::ExplicitParticles)
	{
		// The default may have been set with a different source mode, so we can't copy values over directly. Instead, we need to copy the implicit values over.
		FNiagaraVariable TempVar = InOther.RootVariable;
		if ((InSourceMode == ENiagaraRendererSourceDataMode::Emitter && InOther.BindingSourceMode == ENiagaraBindingSource::ImplicitFromSource) ||
			InOther.BindingSourceMode == ENiagaraBindingSource::ExplicitEmitter)
		{
			ensure(!InOther.DataSetVariable.IsInNameSpace(FNiagaraConstants::EmitterNamespace));
			TempVar.SetName(*(FNiagaraConstants::EmitterNamespace.ToString() + TEXT(".") + InOther.DataSetVariable.GetName().ToString()));
		}
		else if ((InSourceMode == ENiagaraRendererSourceDataMode::Particles && InOther.BindingSourceMode == ENiagaraBindingSource::ImplicitFromSource) ||
			InOther.BindingSourceMode == ENiagaraBindingSource::ExplicitParticles)
		{
			ensure(!InOther.DataSetVariable.IsInNameSpace(FNiagaraConstants::ParticleAttributeNamespace));
			TempVar.SetName(*(FNiagaraConstants::ParticleAttributeNamespace.ToString() + TEXT(".") + InOther.DataSetVariable.GetName().ToString()));
		}

		SetValue(TempVar.GetName(), nullptr, InSourceMode);
	}
	else
	{
		SetValue(InOther.RootVariable.GetName(), InEmitter, InSourceMode);
	}
}

bool FNiagaraVariableAttributeBinding::MatchesDefault(const FNiagaraVariableAttributeBinding& InOther, ENiagaraRendererSourceDataMode InSourceMode) const
{
	if (DataSetVariable.GetName() != InOther.DataSetVariable.GetName())
		return false;
	if (RootVariable.GetName() != InOther.RootVariable.GetName())
		return false;
	return true;
}


bool FNiagaraVariableAttributeBinding::RenameVariableIfMatching(const FNiagaraVariableBase& OldVariable, const FNiagaraVariableBase& NewVariable, const UNiagaraEmitter* InEmitter, ENiagaraRendererSourceDataMode InSourceMode)
{
	// First try a namespace mangling - free match.
	if (OldVariable.GetName() == ParamMapVariable.GetName() && OldVariable.GetType() == ParamMapVariable.GetType())
	{
		SetValue(NewVariable.GetName(), InEmitter, InSourceMode);
		return true;
	}

	// Now we need to deal with any aliased emitter namespaces for the match. If so resolve the aliases then try the match.
	FNiagaraVariable OldVarAliased = OldVariable;
	if (OldVariable.IsInNameSpace(FNiagaraConstants::EmitterNamespace))
	{
		// First, resolve any aliases
		OldVarAliased = FNiagaraUtilities::ResolveAliases(OldVariable, FNiagaraAliasContext()
			.ChangeEmitterToEmitterName(InEmitter->GetUniqueEmitterName()));
	}
	if (OldVarAliased.GetName() == ParamMapVariable.GetName() && OldVarAliased.GetType() == ParamMapVariable.GetType())
	{
		SetValue(NewVariable.GetName(), InEmitter, InSourceMode);
		return true;
	}
	return false;
}

bool FNiagaraVariableAttributeBinding::Matches(const FNiagaraVariableBase& OldVariable,  const UNiagaraEmitter* InEmitter, ENiagaraRendererSourceDataMode InSourceMode)
{
	// First try a namespace mangling - free match.
	if (OldVariable.GetName() == ParamMapVariable.GetName() && OldVariable.GetType() == ParamMapVariable.GetType())
	{
		return true;
	}

	// Now we need to deal with any aliased emitter namespaces for the match. If so resolve the aliases then try the match.
	FNiagaraVariable OldVarAliased = OldVariable;
	if (InEmitter && OldVariable.IsInNameSpace(FNiagaraConstants::EmitterNamespace))
	{
		// First, resolve any aliases
		OldVarAliased = FNiagaraUtilities::ResolveAliases(OldVariable, FNiagaraAliasContext()
			.ChangeEmitterToEmitterName(InEmitter->GetUniqueEmitterName()));
	}
	if (OldVarAliased.GetName() == ParamMapVariable.GetName() && OldVarAliased.GetType() == ParamMapVariable.GetType())
	{
		return true;
	}
	return false;
}

void FNiagaraVariableAttributeBinding::CacheValues(const UNiagaraEmitter* InEmitter, ENiagaraRendererSourceDataMode InSourceMode)
{
	// Some older values may have had the root with the emitter unqiue name as the namespace, fix this up
	// to meet the new assumptions.
	if (InEmitter && RootVariable.IsInNameSpace(InEmitter->GetUniqueEmitterName()))
	{
		// First, replace unaliased emitter namespace with "Emitter" namespace
		RootVariable = FNiagaraUtilities::ResolveAliases(RootVariable, FNiagaraAliasContext()
			.ChangeEmitterNameToEmitter(InEmitter->GetUniqueEmitterName()));

		// Now strip out "Emitter"
		RootVariable.SetName(FNiagaraConstants::GetAttributeAsEmitterDataSetKey(RootVariable).GetName());
	}

	DataSetVariable = ParamMapVariable = (const FNiagaraVariableBase&)RootVariable;
	bBindingExistsOnSource = false;

	// Decide if this is going to be bound to a particle attribute (needed for use by the renderers, for instance)
	if (BindingSourceMode == ENiagaraBindingSource::ExplicitParticles || (InSourceMode == ENiagaraRendererSourceDataMode::Particles && BindingSourceMode == ENiagaraBindingSource::ImplicitFromSource))
	{
		bIsCachedParticleValue = true;
	}
	else
	{
		bIsCachedParticleValue = false;
	}

	// If this is one of the possible namespaces that is implicitly defined, go ahead and expand the full namespace. RootVariable should be non-namespaced at this point.
	if ((InSourceMode == ENiagaraRendererSourceDataMode::Emitter && BindingSourceMode == ENiagaraBindingSource::ImplicitFromSource) ||
		BindingSourceMode == ENiagaraBindingSource::ExplicitEmitter)
	{
		ensure(!DataSetVariable.IsInNameSpace(FNiagaraConstants::EmitterNamespace));
		ParamMapVariable.SetName(*(FNiagaraConstants::EmitterNamespace.ToString() + TEXT(".") + DataSetVariable.GetName().ToString()));
	}
	else if ((InSourceMode == ENiagaraRendererSourceDataMode::Particles && BindingSourceMode == ENiagaraBindingSource::ImplicitFromSource) ||
		BindingSourceMode == ENiagaraBindingSource::ExplicitParticles)
	{
		ensure(!DataSetVariable.IsInNameSpace(FNiagaraConstants::ParticleAttributeNamespace));
		ParamMapVariable.SetName(*(FNiagaraConstants::ParticleAttributeNamespace.ToString() + TEXT(".") + DataSetVariable.GetName().ToString()));
	}

#if WITH_EDITORONLY_DATA
	CachedDisplayName = ParamMapVariable.GetName();
#endif

	// Now resolve if this variable actually exists.
	if (InEmitter)
	{
		if (BindingSourceMode == ENiagaraBindingSource::ExplicitEmitter || (InSourceMode == ENiagaraRendererSourceDataMode::Emitter && BindingSourceMode == ENiagaraBindingSource::ImplicitFromSource))
		{
			// Replace  "Emitter" namespace with unaliased emitter namespace 
			FNiagaraAliasContext ResolveAliasesContext(FNiagaraAliasContext::ERapidIterationParameterMode::EmitterOrParticleScript);
			ResolveAliasesContext.ChangeEmitterToEmitterName(InEmitter->GetUniqueEmitterName());
			ParamMapVariable = FNiagaraUtilities::ResolveAliases(ParamMapVariable,  ResolveAliasesContext);
			DataSetVariable = FNiagaraUtilities::ResolveAliases(DataSetVariable, ResolveAliasesContext);
		}

		if (BindingSourceMode == ENiagaraBindingSource::ExplicitParticles || (InSourceMode == ENiagaraRendererSourceDataMode::Particles && BindingSourceMode == ENiagaraBindingSource::ImplicitFromSource))
			bBindingExistsOnSource = InEmitter->CanObtainParticleAttribute(DataSetVariable);
		else if (BindingSourceMode == ENiagaraBindingSource::ExplicitEmitter || (InSourceMode == ENiagaraRendererSourceDataMode::Emitter && BindingSourceMode == ENiagaraBindingSource::ImplicitFromSource))
			bBindingExistsOnSource = InEmitter->CanObtainEmitterAttribute(ParamMapVariable);
		else if (BindingSourceMode == ENiagaraBindingSource::ExplicitSystem)
			bBindingExistsOnSource = InEmitter->CanObtainSystemAttribute(ParamMapVariable);
		else if (BindingSourceMode == ENiagaraBindingSource::ExplicitUser)
			bBindingExistsOnSource = InEmitter->CanObtainUserVariable(ParamMapVariable);
	}

}


//////////////////////////////////////////////////////////////////////////
const FNiagaraVariableBase& FNiagaraMaterialAttributeBinding::GetParamMapBindableVariable() const
{
	return ResolvedNiagaraVariable;
}


bool FNiagaraMaterialAttributeBinding::RenameVariableIfMatching(const FNiagaraVariableBase& OldVariable, const FNiagaraVariableBase& NewVariable, const UNiagaraEmitter* InEmitter, ENiagaraRendererSourceDataMode InSourceMode)
{
	// First try a namespace mangling - free match.
	if (OldVariable.GetName() == NiagaraVariable.GetName() && OldVariable.GetType() == NiagaraVariable.GetType())
	{
		NiagaraVariable = NewVariable;
		CacheValues(InEmitter);
		return true;
	}

	// Now we need to deal with any aliased emitter namespaces for the match. If so resolve the aliases then try the match.
	FNiagaraVariable OldVarAliased = OldVariable;
	if (OldVariable.IsInNameSpace(InEmitter->GetUniqueEmitterName()))
	{
		// First, resolve any aliases
		OldVarAliased = FNiagaraUtilities::ResolveAliases(OldVariable, FNiagaraAliasContext()
			.ChangeEmitterNameToEmitter(InEmitter->GetUniqueEmitterName()));
	}
	if (OldVarAliased.GetName() == NiagaraVariable.GetName() && OldVarAliased.GetType() == NiagaraVariable.GetType())
	{
		NiagaraVariable = NewVariable;
		CacheValues(InEmitter);
		return true;
	}
	return false;
}

bool FNiagaraMaterialAttributeBinding::Matches(const FNiagaraVariableBase& OldVariable, const UNiagaraEmitter* InEmitter, ENiagaraRendererSourceDataMode InSourceMode)
{
	// First try a namespace mangling - free match.
	if (OldVariable.GetName() == NiagaraVariable.GetName() && OldVariable.GetType() == NiagaraVariable.GetType())
	{
		return true;
	}

	// Now we need to deal with any aliased emitter namespaces for the match. If so resolve the aliases then try the match.
	FNiagaraVariable OldVarAliased = OldVariable;
	if (OldVariable.IsInNameSpace(InEmitter->GetUniqueEmitterName()))
	{
		// First, resolve any aliases
		OldVarAliased = FNiagaraUtilities::ResolveAliases(OldVariable, FNiagaraAliasContext()
			.ChangeEmitterNameToEmitter(InEmitter->GetUniqueEmitterName()));
	}
	if (OldVarAliased.GetName() == NiagaraVariable.GetName() && OldVarAliased.GetType() == NiagaraVariable.GetType())
	{
		return true;
	}
	return false;
}

void FNiagaraMaterialAttributeBinding::CacheValues(const UNiagaraEmitter* InEmitter) 
{
	if (InEmitter != nullptr)
	{
		ResolvedNiagaraVariable = FNiagaraUtilities::ResolveAliases(NiagaraVariable, 
			FNiagaraAliasContext(FNiagaraAliasContext::ERapidIterationParameterMode::EmitterOrParticleScript)
			.ChangeEmitterToEmitterName(InEmitter->GetUniqueEmitterName()));
	}
	else
	{
		ResolvedNiagaraVariable = NiagaraVariable;
	}
}

//////////////////////////////////////////////////////////////////////////

#if !NO_LOGGING
bool FNiagaraUtilities::LogVerboseWarnings()
{
	return GNiagaraLogVerboseWarnings != 0;
}
#endif

bool FNiagaraUtilities::AllowGPUParticles(EShaderPlatform ShaderPlatform)
{
	return FNiagaraUtilities::SupportsComputeShaders(ShaderPlatform) && GNiagaraAllowGPUParticles && GNiagaraAllowComputeShaders && GRHISupportsDrawIndirect;
}

bool FNiagaraUtilities::AllowComputeShaders(EShaderPlatform ShaderPlatform)
{
	return RHISupportsComputeShaders(ShaderPlatform) && GNiagaraAllowComputeShaders && GRHISupportsDrawIndirect;
}

bool FNiagaraUtilities::AllowGPUSorting(EShaderPlatform ShaderPlatform)
{
	static const IConsoleVariable* AllowGPUSortingCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("FX.AllowGPUSorting"));
	return ensure(AllowGPUSortingCVar) && (AllowGPUSortingCVar->GetInt() != 0);
}

bool FNiagaraUtilities::AllowGPUCulling(EShaderPlatform ShaderPlatform)
{
	return GNiagaraGPUCulling && AllowGPUSorting(ShaderPlatform) && AllowComputeShaders(ShaderPlatform);
}

ENiagaraCompileUsageStaticSwitch FNiagaraUtilities::ConvertScriptUsageToStaticSwitchUsage(ENiagaraScriptUsage ScriptUsage)
{
	if (ScriptUsage == ENiagaraScriptUsage::ParticleEventScript)
	{
		return ENiagaraCompileUsageStaticSwitch::Event;
	}
	if (ScriptUsage == ENiagaraScriptUsage::ParticleSimulationStageScript)
	{
		return ENiagaraCompileUsageStaticSwitch::SimulationStage;
	}
	if (ScriptUsage == ENiagaraScriptUsage::EmitterSpawnScript || ScriptUsage == ENiagaraScriptUsage::SystemSpawnScript || ScriptUsage == ENiagaraScriptUsage::ParticleSpawnScriptInterpolated || ScriptUsage == ENiagaraScriptUsage::ParticleSpawnScript)
	{
		return ENiagaraCompileUsageStaticSwitch::Spawn;
	}
	if (ScriptUsage == ENiagaraScriptUsage::EmitterUpdateScript || ScriptUsage == ENiagaraScriptUsage::SystemUpdateScript || ScriptUsage == ENiagaraScriptUsage::ParticleUpdateScript)
	{
		return ENiagaraCompileUsageStaticSwitch::Update;
	}
	return ENiagaraCompileUsageStaticSwitch::Default;
}

ENiagaraScriptContextStaticSwitch FNiagaraUtilities::ConvertScriptUsageToStaticSwitchContext(ENiagaraScriptUsage ScriptUsage)
{
	if (ScriptUsage == ENiagaraScriptUsage::SystemSpawnScript || ScriptUsage == ENiagaraScriptUsage::SystemUpdateScript)
	{
		return ENiagaraScriptContextStaticSwitch::System;
	}
	if (ScriptUsage == ENiagaraScriptUsage::EmitterSpawnScript || ScriptUsage == ENiagaraScriptUsage::EmitterUpdateScript)
	{
		return ENiagaraScriptContextStaticSwitch::Emitter;
	}
	return ENiagaraScriptContextStaticSwitch::Particle;
}

FName NIAGARA_API FNiagaraUtilities::GetUniqueName(FName CandidateName, const TSet<FName>& ExistingNames)
{
	if (ExistingNames.Contains(CandidateName) == false)
	{
		return CandidateName;
	}

	FString CandidateNameString = CandidateName.ToString();
	FString BaseNameString = CandidateNameString;
	if (CandidateNameString.Len() >= 3 && CandidateNameString.Right(3).IsNumeric())
	{
		BaseNameString = CandidateNameString.Left(CandidateNameString.Len() - 3);
	}

	FName UniqueName = FName(*BaseNameString);
	int32 NameIndex = 1;
	while (ExistingNames.Contains(UniqueName))
	{
		UniqueName = FName(*FString::Printf(TEXT("%s%03i"), *BaseNameString, NameIndex));
		NameIndex++;
	}

	return UniqueName;
}

FString FNiagaraUtilities::CreateRapidIterationConstantName(FName InVariableName, const TCHAR* InEmitterName, ENiagaraScriptUsage InUsage)
{
	TArray<FString> SplitName;
	InVariableName.ToString().ParseIntoArray(SplitName, TEXT("."));
	int32 NumSlots = SplitName.Num();
	if (InEmitterName != nullptr)
	{
		for (int32 i = 0; i < NumSlots; i++)
		{
			if (SplitName[i] == TEXT("Emitter"))
			{
				SplitName[i] = InEmitterName;
			}
		}

		if (NumSlots >= 3 && SplitName[0] == InEmitterName)
		{
			// Do nothing
			UE_LOG(LogNiagara, Log, TEXT("ConvertVariableToRapidIterationConstantName Got here!"));
		}
		else
		{
			SplitName.Insert(InEmitterName, 0);
		}
		SplitName.Insert(TEXT("Constants"), 0);
	}
	else
	{
		SplitName.Insert(TEXT("Constants"), 0);
	}

	return FString::Join(SplitName, TEXT("."));
}

FNiagaraVariable FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(FNiagaraVariable InVar, const TCHAR* InEmitterName, ENiagaraScriptUsage InUsage)
{
	FNiagaraVariable Var = InVar;

	Var.SetName(*CreateRapidIterationConstantName(Var.GetName(), InEmitterName, InUsage));

	return Var;
}

void FNiagaraUtilities::CollectScriptDataInterfaceParameters(const UObject& Owner, const TArrayView<UNiagaraScript*>& Scripts, FNiagaraParameterStore& OutDataInterfaceParameters)
{
	for (UNiagaraScript* Script : Scripts)
	{
		for (FNiagaraScriptDataInterfaceInfo& DataInterfaceInfo : Script->GetCachedDefaultDataInterfaces())
		{
			if (DataInterfaceInfo.RegisteredParameterMapWrite != NAME_None)
			{
				FNiagaraVariable DataInterfaceParameter(DataInterfaceInfo.Type, DataInterfaceInfo.RegisteredParameterMapWrite);
				if (OutDataInterfaceParameters.AddParameter(DataInterfaceParameter, false, false))
				{
					OutDataInterfaceParameters.SetDataInterface(DataInterfaceInfo.DataInterface, DataInterfaceParameter);
				}
				else
				{
					UE_LOG(LogNiagara, Error, TEXT("Duplicate data interface parameter writes found, simulation will be incorrect.  Owner: %s Parameter: %s"),
						*Owner.GetPathName(), *DataInterfaceInfo.RegisteredParameterMapWrite.ToString());
				}
			}
		}
	}
}

bool FNiagaraScriptDataInterfaceCompileInfo::CanExecuteOnTarget(ENiagaraSimTarget SimTarget) const
{
	// Note that this can be called on non-game threads. We ensure that the data interface CDO object is already in existence at application init time.
	UNiagaraDataInterface* Obj = GetDefaultDataInterface();
	if (Obj)
	{
		return Obj->CanExecuteOnTarget(SimTarget);
	}
	UE_LOG(LogNiagara, Error, TEXT("Failed to call CanExecuteOnTarget for DataInterface \"%s\". Perhaps missing a plugin for your project?"), *Name.ToString());
	return false;
}

UNiagaraDataInterface* FNiagaraScriptDataInterfaceCompileInfo::GetDefaultDataInterface() const
{
	// Note that this can be called on non-game threads. We ensure that the data interface CDO object is already in existence at application init time, so we don't allow this to be auto-created.
	if (Type.IsDataInterface())
	{
		const UClass* TargetClass = const_cast<UClass*>(Type.GetClass());
		if (TargetClass)
		{
			UNiagaraDataInterface* Obj = Cast<UNiagaraDataInterface>(TargetClass->GetDefaultObject(false));
			if (Obj)
				return Obj;

			UE_LOG(LogNiagara, Error, TEXT("Failed to create default object for class \"%s\". Perhaps missing a plugin for your project?"), *TargetClass->GetName());
			return nullptr;
		}

	}
	UE_LOG(LogNiagara, Error, TEXT("Failed to create default object for compiled variable \"%s\". Perhaps missing a plugin for your project?"), *this->Name.ToString());
	return nullptr;
}

bool FNiagaraScriptDataInterfaceCompileInfo::NeedsPerInstanceBinding()const
{
	if (Name.ToString().StartsWith(TEXT("User.")))
		return true;
	UNiagaraDataInterface* Obj = GetDefaultDataInterface();
	if (Obj && Obj->PerInstanceDataSize() > 0)
		return true;
	return false;
}

bool FNiagaraScriptDataInterfaceCompileInfo::MatchesClass(const UClass* InClass) const
{
	UNiagaraDataInterface* Obj = GetDefaultDataInterface();
	if (Obj && Obj->GetClass() == InClass)
		return true;
	return false;
}


void FNiagaraUtilities::DumpHLSLText(const FString& SourceCode, const FString& DebugName)
{
	UE_LOG(LogNiagara, Display, TEXT("Compile output as text: %s"), *DebugName);
	UE_LOG(LogNiagara, Display, TEXT("==================================================================================="));
	TArray<FString> OutputByLines;
	SourceCode.ParseIntoArrayLines(OutputByLines, false);
	for (int32 i = 0; i < OutputByLines.Num(); i++)
	{
		UE_LOG(LogNiagara, Display, TEXT("/*%04d*/\t\t%s"), i + 1, *OutputByLines[i]);
	}
	UE_LOG(LogNiagara, Display, TEXT("==================================================================================="));
}

FString FNiagaraUtilities::SystemInstanceIDToString(FNiagaraSystemInstanceID ID)
{
	TCHAR Buffer[17];
	uint64 Value = ID;
	for (int i = 15; i >= 0; --i)
	{
		TCHAR ch = Value & 0xf;
		Value >>= 4;
		Buffer[i] = (ch >= 10 ? TCHAR('A' - 10) : TCHAR('0')) + ch;
	}
	Buffer[16] = 0;

	return FString(Buffer);
}

EPixelFormat FNiagaraUtilities::BufferFormatToPixelFormat(ENiagaraGpuBufferFormat NiagaraFormat)
{
	switch (NiagaraFormat)
	{
		case ENiagaraGpuBufferFormat::Float:					return EPixelFormat::PF_R32_FLOAT;
		case ENiagaraGpuBufferFormat::HalfFloat:				return EPixelFormat::PF_R16F;
		case ENiagaraGpuBufferFormat::UnsignedNormalizedByte:	return EPixelFormat::PF_R8;
	}
	UE_LOG(LogNiagara, Error, TEXT("NiagaraFormat(%d) is invalid, returning float format"), NiagaraFormat);
	return EPixelFormat::PF_R32_FLOAT;
}

ETextureRenderTargetFormat FNiagaraUtilities::BufferFormatToRenderTargetFormat(ENiagaraGpuBufferFormat NiagaraFormat)
{
	switch (NiagaraFormat)
	{
		case ENiagaraGpuBufferFormat::Float:					return ETextureRenderTargetFormat::RTF_R32f;
		case ENiagaraGpuBufferFormat::HalfFloat:				return ETextureRenderTargetFormat::RTF_R16f;
		case ENiagaraGpuBufferFormat::UnsignedNormalizedByte:	return ETextureRenderTargetFormat::RTF_R8;
	}
	UE_LOG(LogNiagara, Error, TEXT("NiagaraFormat(%d) is invalid, returning float format"), NiagaraFormat);
	return ETextureRenderTargetFormat::RTF_R32f;
}

FString FNiagaraUtilities::SanitizeNameForObjectsAndPackages(const FString& InName)
{
	FString SanitizedName = InName;

	const TCHAR* InvalidObjectChar = INVALID_OBJECTNAME_CHARACTERS;
	while (*InvalidObjectChar)
	{
		SanitizedName.ReplaceCharInline(*InvalidObjectChar, TCHAR('_'), ESearchCase::CaseSensitive);
		++InvalidObjectChar;
	}

	const TCHAR* InvalidPackageChar = INVALID_LONGPACKAGE_CHARACTERS;
	while (*InvalidPackageChar)
	{
		SanitizedName.ReplaceCharInline(*InvalidPackageChar, TCHAR('_'), ESearchCase::CaseSensitive);
		++InvalidPackageChar;
	}

	return SanitizedName;
}

const FString FNiagaraAliasContext::EmitterNamespaceString = TEXT("Emitter");
const FString FNiagaraAliasContext::ModuleNamespaceString = TEXT("Module");
const FString FNiagaraAliasContext::StackContextNamespaceString = TEXT("StackContext");
const FString FNiagaraAliasContext::RapidIterationParametersNamespaceString = TEXT("Constants");
const FString FNiagaraAliasContext::EngineNamespaceString = TEXT("Engine");
const FString FNiagaraAliasContext::AssignmentNodePrefix = TRANSLATOR_SET_VARIABLES_STR;

FNiagaraAliasContext& FNiagaraAliasContext::ChangeEmitterToEmitterName(const FString& InEmitterName)
{
	EmitterMapping = TPair<FString, FString>(EmitterNamespaceString, InEmitterName);
	EmitterName = InEmitterName;
	return *this;
}

FNiagaraAliasContext& FNiagaraAliasContext::ChangeEmitterNameToEmitter(const FString& InEmitterName)
{
	EmitterMapping = TPair<FString, FString>(InEmitterName, EmitterNamespaceString);
	EmitterName = InEmitterName;
	return *this;
}

FNiagaraAliasContext& FNiagaraAliasContext::ChangeEmitterName(const FString& InOldEmitterName, const FString& InNewEmitterName)
{
	EmitterMapping = TPair<FString, FString>(InOldEmitterName, InNewEmitterName);
	EmitterName = InNewEmitterName;
	return *this;
}

FNiagaraAliasContext& FNiagaraAliasContext::ChangeModuleToModuleName(const FString& InModuleName)
{
	ModuleMapping = TPair<FString, FString>(ModuleNamespaceString, InModuleName);
	ModuleName = InModuleName;
	return *this;
}

FNiagaraAliasContext& FNiagaraAliasContext::ChangeModuleNameToModule(const FString& InModuleName)
{
	ModuleMapping = TPair<FString, FString>(InModuleName, ModuleNamespaceString);
	ModuleName = InModuleName;
	return *this;
}

FNiagaraAliasContext& FNiagaraAliasContext::ChangeModuleName(const FString& InOldModuleName, const FString& InNewModuleName)
{
	ModuleMapping = TPair<FString, FString>(InOldModuleName, InNewModuleName);
	ModuleName = InNewModuleName;
	return *this;
}

FNiagaraAliasContext& FNiagaraAliasContext::ChangeStackContext(const FString& InStackContextName)
{
	StackContextMapping = TPair<FString, FString>(StackContextNamespaceString, InStackContextName);
	StackContextName = InStackContextName;
	return *this;
}

void AliasRapidIterationConstant(const FNiagaraAliasContext& InContext, TArray<FStringView, TInlineAllocator<16>>& InOutSplitName, int32& OutAssignmentNamespaceIndex)
{
	if(ensureMsgf(InContext.GetRapidIterationParameterMode() != FNiagaraAliasContext::ERapidIterationParameterMode::None, TEXT("Can not resolve a rapid iteration variable without specifying the mode in the context.")))
	{
		// Rapid iteration parameters are in the following format:
		//     Constants.[Emitter Name - Optional - Only in non-system scripts].[Module Name].[Assignment Namespace - Optional].[Value Name]
		int32 MinParts;
		int32 EmitterNameIndex;
		int32 ModuleNameIndex;
		if (InContext.GetRapidIterationParameterMode() == FNiagaraAliasContext::ERapidIterationParameterMode::SystemScript)
		{
			MinParts = 3;
			EmitterNameIndex = INDEX_NONE;
			ModuleNameIndex = 1;
		}
		else
		{
			MinParts = 4;
			EmitterNameIndex = 1;
			ModuleNameIndex = 2;
		}

		if (ensureMsgf(InOutSplitName.Num() >= MinParts, TEXT("Can not resolve malformed rapid iteration parameter")))
		{
			const TOptional<TPair<FString, FString>>& EmitterMapping = InContext.GetEmitterMapping();
			const TOptional<TPair<FString, FString>>& ModuleMapping = InContext.GetModuleMapping();
			if (EmitterNameIndex != INDEX_NONE && EmitterMapping.IsSet() &&
				InOutSplitName[EmitterNameIndex].Equals(EmitterMapping.GetValue().Key))
			{
				InOutSplitName[EmitterNameIndex] = EmitterMapping.GetValue().Value;
			}
			if (ModuleMapping.IsSet() &&
				InOutSplitName[ModuleNameIndex].Equals(ModuleMapping.GetValue().Key))
			{
				InOutSplitName[ModuleNameIndex] = ModuleMapping.GetValue().Value;
			}

			OutAssignmentNamespaceIndex = InOutSplitName[ModuleNameIndex].StartsWith(FNiagaraAliasContext::AssignmentNodePrefix) ? ModuleNameIndex + 1 : INDEX_NONE;
		}
	}
}

void AliasEngineSuppliedEmitterValue(const FNiagaraAliasContext& InContext, TArray<FStringView, TInlineAllocator<16>>& InOutSplitName)
{
	// Certain engine supplied values must be aliased per emitter.  Format:
	//     Engine.[Emitter Name - Optional].[Value Name]
	const TOptional<TPair<FString, FString>>& EmitterMapping = InContext.GetEmitterMapping();
	if (EmitterMapping.IsSet() && InOutSplitName.Num() > 2 && InOutSplitName[1].Equals(EmitterMapping.GetValue().Key))
	{
		InOutSplitName[1] = EmitterMapping.GetValue().Value;
	}
}

void AliasStandardParameter(const FNiagaraAliasContext& InContext, TArray<FStringView, TInlineAllocator<16>>& InOutSplitName, int32& OutAssignmentNamespaceIndex)
{
	// Standard parameter format:
	//     [Namespace - dataset, transient, or module].[Assignment Namespace - Optional].[Value Name]
	const TOptional<TPair<FString, FString>>& EmitterMapping = InContext.GetEmitterMapping();
	const TOptional<TPair<FString, FString>>& ModuleMapping = InContext.GetModuleMapping();
	const TOptional<TPair<FString, FString>>& StackContextMapping = InContext.GetStackContextMapping();

	// First alias the stack context mapping since it might map to emitter which would need to be further aliased.
	if (StackContextMapping.IsSet() &&
		InOutSplitName[0].Equals(StackContextMapping.GetValue().Key))
	{
		InOutSplitName[0] = StackContextMapping.GetValue().Value;
	}

	// Alias the emitter mapping next, and if that was not aliased, handle the module mapping.
	if (EmitterMapping.IsSet() &&
		InOutSplitName[0] == EmitterMapping.GetValue().Key)
	{
		InOutSplitName[0] = EmitterMapping.GetValue().Value;
	}
	else if (ModuleMapping.IsSet() &&
		InOutSplitName[0] == ModuleMapping.GetValue().Key)
	{
		InOutSplitName[0] = ModuleMapping.GetValue().Value;
	}

	// If there are more than 2 parts in the parameter, and it's not an assignment node, then
	// it may be a module specific dataset value, so the 2nd position must be checked for the
	// module mapping.
	if (InOutSplitName.Num() > 1 && ModuleMapping.IsSet() &&
		InOutSplitName[1] == ModuleMapping.GetValue().Key)
	{
		InOutSplitName[1] = ModuleMapping.GetValue().Value;
	}

	OutAssignmentNamespaceIndex = InOutSplitName[0].StartsWith(FNiagaraAliasContext::AssignmentNodePrefix) ? 1 : INDEX_NONE;
}

void AliasAssignmentInputNamespace(const FNiagaraAliasContext& InContext, int32& InAssignmentNamespaceIndex, TArray<FStringView, TInlineAllocator<16>>& InOutSplitName)
{
	if(InAssignmentNamespaceIndex < InOutSplitName.Num())
	{
		const TOptional<TPair<FString, FString>>& EmitterMapping = InContext.GetEmitterMapping();
		const TOptional<TPair<FString, FString>>& StackContextMapping = InContext.GetStackContextMapping();

		if (StackContextMapping.IsSet() &&
			InOutSplitName[InAssignmentNamespaceIndex].Equals(StackContextMapping.GetValue().Key))
		{
			InOutSplitName[InAssignmentNamespaceIndex] = StackContextMapping.GetValue().Value;
		}

		if (EmitterMapping.IsSet() &&
			InOutSplitName[InAssignmentNamespaceIndex].Equals(EmitterMapping.GetValue().Key))
		{
			InOutSplitName[InAssignmentNamespaceIndex] = EmitterMapping.GetValue().Value;
		}
	}
}

FNiagaraVariable FNiagaraUtilities::ResolveAliases(const FNiagaraVariable& InVar, const FNiagaraAliasContext& InContext)
{
	FNiagaraVariable OutVar = InVar;

	TStringBuilder<128> VarName;
	InVar.GetName().ToString(VarName);
	TArray<FStringView, TInlineAllocator<16>> SplitName;
	UE::String::ParseTokens(VarName, TEXT('.'), [&SplitName](FStringView Token) { SplitName.Add(Token); });

	int32 AssignmentNamespaceIndex = INDEX_NONE;
	if (SplitName[0].Equals(FNiagaraAliasContext::RapidIterationParametersNamespaceString))
	{
		AliasRapidIterationConstant(InContext, SplitName, AssignmentNamespaceIndex);
	}
	else if (SplitName[0].Equals(FNiagaraAliasContext::EngineNamespaceString))
	{
		AliasEngineSuppliedEmitterValue(InContext, SplitName);
	}
	else
	{
		AliasStandardParameter(InContext, SplitName, AssignmentNamespaceIndex);
	}
	if(AssignmentNamespaceIndex != INDEX_NONE)
	{
		AliasAssignmentInputNamespace(InContext, AssignmentNamespaceIndex, SplitName);
	}

	TStringBuilder<128> OutVarStrName;
	OutVarStrName.Join(SplitName, TEXT("."));

	OutVar.SetName(OutVarStrName.ToString());
	return OutVar;
}

#if WITH_EDITORONLY_DATA
void FNiagaraUtilities::PrepareRapidIterationParameters(const TArray<UNiagaraScript*>& Scripts, const TMap<UNiagaraScript*, UNiagaraScript*>& ScriptDependencyMap, const TMap<UNiagaraScript*, const UNiagaraEmitter*>& ScriptToEmitterMap)
{
	SCOPE_CYCLE_COUNTER(STAT_Niagara_Utilities_PrepareRapidIterationParameters);

	TMap<UNiagaraScript*, FNiagaraParameterStore> ScriptToPreparedParameterStoreMap;

	// Remove old and initialize new parameters.
	for (UNiagaraScript* Script : Scripts)
	{
		FNiagaraParameterStore& ParameterStoreToPrepare = ScriptToPreparedParameterStoreMap.FindOrAdd(Script);
		Script->RapidIterationParameters.CopyParametersTo(ParameterStoreToPrepare, false, FNiagaraParameterStore::EDataInterfaceCopyMethod::None);
		ParameterStoreToPrepare.ParameterGuidMapping = Script->RapidIterationParameters.ParameterGuidMapping;
		checkf(ScriptToEmitterMap.Find(Script) != nullptr, TEXT("Script to emitter name map must have an entry for each script to be processed."));
		if (const UNiagaraEmitter* const* Emitter = ScriptToEmitterMap.Find(Script))
		{
			Script->GetLatestSource()->CleanUpOldAndInitializeNewRapidIterationParameters(*Emitter, Script->GetUsage(), Script->GetUsageId(), ParameterStoreToPrepare);
		}
	}

	// Copy parameters for dependencies.
	for (auto It = ScriptToPreparedParameterStoreMap.CreateIterator(); It; ++It)
	{
		UNiagaraScript* Script = It.Key();
		FNiagaraParameterStore& PreparedParameterStore = It.Value();
		UNiagaraScript*const* DependentScriptPtr = ScriptDependencyMap.Find(Script);
		if (DependentScriptPtr != nullptr)
		{
			UNiagaraScript* DependentScript = *DependentScriptPtr;
			FNiagaraParameterStore* DependentPreparedParameterStore = ScriptToPreparedParameterStoreMap.Find(DependentScript);
			checkf(DependentPreparedParameterStore != nullptr, TEXT("Dependent scripts must be one of the scripts being processed."));
			PreparedParameterStore.CopyParametersTo(*DependentPreparedParameterStore, false, FNiagaraParameterStore::EDataInterfaceCopyMethod::None);
		}
	}

	// Resolve prepared parameters with the source parameters.
	for (auto It = ScriptToPreparedParameterStoreMap.CreateIterator(); It; ++It)
	{
		UNiagaraScript* Script = It.Key();
		FNiagaraParameterStore& PreparedParameterStore = It.Value();

		auto RapidIterationParameters = Script->RapidIterationParameters.ReadParameterVariables();

		bool bOverwriteParameters = false;
		if (RapidIterationParameters.Num() != PreparedParameterStore.ReadParameterVariables().Num())
		{
			bOverwriteParameters = true;
		}
		else
		{
			for (const FNiagaraVariableWithOffset& ParamWithOffset : RapidIterationParameters)
			{
				const FNiagaraVariable& SourceParameter = ParamWithOffset;
				const int32 SourceOffset = ParamWithOffset.Offset;

				int32 PreparedOffset = PreparedParameterStore.IndexOf(SourceParameter);
				if (PreparedOffset == INDEX_NONE)
				{
					bOverwriteParameters = true;
					break;
				}
				else
				{
					if (FMemory::Memcmp(
						Script->RapidIterationParameters.GetParameterData(SourceOffset),
						PreparedParameterStore.GetParameterData(PreparedOffset),
						SourceParameter.GetSizeInBytes()) != 0)
					{
						bOverwriteParameters = true;
						break;
					}
				}
			}
		}

		if (bOverwriteParameters)
		{
			Script->RapidIterationParameters = PreparedParameterStore;
		}
	}
}
#endif

//////////////////////////////////////////////////////////////////////////

FNiagaraUserParameterBinding::FNiagaraUserParameterBinding()
	: Parameter(FNiagaraTypeDefinition::GetUObjectDef(), NAME_None)
	{

	}

FNiagaraUserParameterBinding::FNiagaraUserParameterBinding(const FNiagaraTypeDefinition& InMaterialDef)
	: Parameter(InMaterialDef, NAME_None)
{
}

//////////////////////////////////////////////////////////////////////////

bool FVMExternalFunctionBindingInfo::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FNiagaraCustomVersion::GUID);

	if (Ar.IsLoading() || Ar.IsSaving())
	{
		UScriptStruct* Struct = FVMExternalFunctionBindingInfo::StaticStruct();
		Struct->SerializeTaggedProperties(Ar, (uint8*)this, Struct, nullptr);
	}

#if WITH_EDITORONLY_DATA
	const int32 NiagaraVersion = Ar.CustomVer(FNiagaraCustomVersion::GUID);

	if (NiagaraVersion < FNiagaraCustomVersion::MemorySaving)
	{
		for (auto it = Specifiers_DEPRECATED.CreateConstIterator(); it; ++it)
		{
			FunctionSpecifiers.Emplace(it->Key, it->Value);
		}
	}
#endif

	return true;
}

const FString FNiagaraCompileOptions::CpuScriptDefine = TEXT("CPUSim");
const FString FNiagaraCompileOptions::GpuScriptDefine = TEXT("GPUComputeSim");

FSynchronizeWithParameterDefinitionsArgs::FSynchronizeWithParameterDefinitionsArgs()
	: SpecificDefinitionsUniqueIds(TArray<FGuid>())
	, SpecificDestScriptVarIds(TArray<FGuid>())
	, bForceGatherDefinitions(false)
	, bForceSynchronizeParameters(false)
	, bSubscribeAllNameMatchParameters(false)
	, AdditionalOldToNewNames()
{
}
