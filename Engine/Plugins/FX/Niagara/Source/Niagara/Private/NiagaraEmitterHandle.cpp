// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraEmitterHandle.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraScriptSourceBase.h"
#include "NiagaraCommon.h"
#include "NiagaraCustomVersion.h"

const FNiagaraEmitterHandle FNiagaraEmitterHandle::InvalidHandle;

FNiagaraEmitterHandle::FNiagaraEmitterHandle() 
	: bIsEnabled(true)
#if WITH_EDITORONLY_DATA
	, Source_DEPRECATED(nullptr)
	, LastMergedSource_DEPRECATED(nullptr)
	, bIsolated(false)
#endif
	, Instance(nullptr)
{
}

#if WITH_EDITORONLY_DATA
FNiagaraEmitterHandle::FNiagaraEmitterHandle(UNiagaraEmitter& InEmitter)
	: Id(FGuid::NewGuid())
	, IdName(*Id.ToString())
	, bIsEnabled(true)
	, Name(*InEmitter.GetUniqueEmitterName())
	, Source_DEPRECATED(nullptr)
	, LastMergedSource_DEPRECATED(nullptr)
	, bIsolated(false)
	, Instance(&InEmitter)
{
}
#endif

bool FNiagaraEmitterHandle::IsValid() const
{
	return Id.IsValid();
}

FGuid FNiagaraEmitterHandle::GetId() const
{
	return Id;
}

FName FNiagaraEmitterHandle::GetIdName() const
{
	return IdName;
}

FName FNiagaraEmitterHandle::GetName() const
{
	return Name;
}

void FNiagaraEmitterHandle::SetName(FName InName, UNiagaraSystem& InOwnerSystem)
{
	FName SanitizedName = *FNiagaraUtilities::SanitizeNameForObjectsAndPackages(InName.ToString());
	if (SanitizedName.IsEqual(Name, ENameCase::CaseSensitive, false))
	{
		return;
	}

	TSet<FName> OtherEmitterNames;
	for (const FNiagaraEmitterHandle& OtherEmitterHandle : InOwnerSystem.GetEmitterHandles())
	{
		if (OtherEmitterHandle.GetId() != GetId())
		{
			OtherEmitterNames.Add(OtherEmitterHandle.GetName());
		}
	}
	FName UniqueName = FNiagaraUtilities::GetUniqueName(SanitizedName, OtherEmitterNames);

	Name = UniqueName;
	if (Instance->SetUniqueEmitterName(Name.ToString()))
	{
 #if WITH_EDITOR
		if (InOwnerSystem.GetSystemSpawnScript() && InOwnerSystem.GetSystemSpawnScript()->GetLatestSource())
		{
			// Just invalidate the system scripts here. The emitter scripts have their important variables 
			// changed in the SetUniqueEmitterName method above.
			InOwnerSystem.GetSystemSpawnScript()->GetLatestSource()->MarkNotSynchronized(TEXT("EmitterHandleRenamed"));
		}
#endif
	}
}

bool FNiagaraEmitterHandle::GetIsEnabled() const
{
	return bIsEnabled;
}

bool FNiagaraEmitterHandle::SetIsEnabled(bool bInIsEnabled, UNiagaraSystem& InOwnerSystem, bool bRecompileIfChanged)
{
	if (bIsEnabled != bInIsEnabled)
	{
		bIsEnabled = bInIsEnabled;

#if WITH_EDITOR
		if (InOwnerSystem.GetSystemSpawnScript() && InOwnerSystem.GetSystemSpawnScript()->GetLatestSource())
		{
			// We need to get the NiagaraNodeEmitters to update their enabled state based on what happened.
			InOwnerSystem.GetSystemSpawnScript()->GetLatestSource()->RefreshFromExternalChanges();

			// Need to cause us to recompile in the future if necessary...
			FString InvalidateReason = TEXT("Emitter enabled changed.");
			InOwnerSystem.GetSystemSpawnScript()->InvalidateCompileResults(InvalidateReason);
			InOwnerSystem.GetSystemUpdateScript()->InvalidateCompileResults(InvalidateReason);

			// Clean out the emitter's compile results for cleanliness.
			if (Instance)
			{
				Instance->InvalidateCompileResults();
			}

			// In some cases we may do the recompile now.
			if (bRecompileIfChanged)
			{
				InOwnerSystem.RequestCompile(false);
			}
		}
#endif
		return true;
	}
	return false;
}

UNiagaraEmitter* FNiagaraEmitterHandle::GetInstance() const
{
	return Instance;
}

FString FNiagaraEmitterHandle::GetUniqueInstanceName()const
{
	check(Instance);
	return Instance->GetUniqueEmitterName();
}

#if WITH_EDITORONLY_DATA

bool FNiagaraEmitterHandle::NeedsRecompile() const
{
	if (GetIsEnabled())
	{
		TArray<UNiagaraScript*> Scripts;
		Instance->GetScripts(Scripts);

		for (UNiagaraScript* Script : Scripts)
		{
			if (Script->IsCompilable() && !Script->AreScriptAndSourceSynchronized())
			{
				return true;
			}
		}
	}
	return false;
}

void FNiagaraEmitterHandle::ConditionalPostLoad(int32 NiagaraCustomVersion)
{
	if (Instance != nullptr)
	{
		Instance->ConditionalPostLoad();
		if (NiagaraCustomVersion < FNiagaraCustomVersion::MoveInheritanceDataFromTheEmitterHandleToTheEmitter)
		{
			if (Source_DEPRECATED != nullptr)
			{
				Source_DEPRECATED->ConditionalPostLoad();
				Instance->Parent = Source_DEPRECATED;
				Source_DEPRECATED = nullptr;
			}
			if (LastMergedSource_DEPRECATED != nullptr)
			{
				LastMergedSource_DEPRECATED->ConditionalPostLoad();
				Instance->ParentAtLastMerge = LastMergedSource_DEPRECATED;
				Instance->ParentAtLastMerge->Rename(nullptr, Instance, REN_ForceNoResetLoaders);
				LastMergedSource_DEPRECATED = nullptr;
			}
		}

		FText Reason;
		if (Instance->GetFName().IsValidObjectName(Reason) == false)
		{
			UNiagaraSystem* OwningSystem = Instance->GetTypedOuter<UNiagaraSystem>();
			if (OwningSystem != nullptr)
			{
				// If the name isn't a valid object name, set the name again so that it will be properly sanitized.
				SetName(Name, *OwningSystem);
			}
		}
	}
}

bool FNiagaraEmitterHandle::UsesEmitter(const UNiagaraEmitter& InEmitter) const
{
	return Instance == &InEmitter || (Instance != nullptr && Instance->UsesEmitter(InEmitter));
}

void FNiagaraEmitterHandle::ClearEmitter()
{
	Instance = nullptr;
	Source_DEPRECATED = nullptr;
	LastMergedSource_DEPRECATED = nullptr;
}


#endif