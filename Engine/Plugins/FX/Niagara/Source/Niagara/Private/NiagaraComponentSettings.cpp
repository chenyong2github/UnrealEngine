// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraComponentSettings.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstance.h"

namespace FNiagaraComponentSettings
{
	struct FNiagaraEmitterNameSettingsRef
	{
		FName SystemName;
		FName EmitterName;

		FNiagaraEmitterNameSettingsRef() = default;
		explicit FNiagaraEmitterNameSettingsRef(const FStringView& Pair)
		{
			int32 SepIndex;
			Pair.FindChar(':', SepIndex);
			if (SepIndex != INDEX_NONE && Pair.Len() > SepIndex + 1)
			{
				SystemName = FName(Pair.Mid(0, SepIndex));
				EmitterName = FName(Pair.Mid(SepIndex + 1));
			}
		}

		bool operator==(const FNiagaraEmitterNameSettingsRef& Other) const { return SystemName == Other.SystemName && EmitterName == Other.EmitterName; }
		bool operator!=(const FNiagaraEmitterNameSettingsRef& Other) const { return !(*this == Other); }
		bool IsValid() const { return !SystemName.IsNone() && !EmitterName.IsNone(); }
	};
	
	uint32 GetTypeHash(const FNiagaraEmitterNameSettingsRef& Var)
	{
		return HashCombine(GetTypeHash(Var.SystemName), GetTypeHash(Var.EmitterName));
	}

	bool									bUseSystemDenyList = false;
	bool									bUseEmitterDenyList = false;

	TSet<FName>								SystemDenyList;
	TSet<FNiagaraEmitterNameSettingsRef>	EmitterDenyList;
	TSet<FNiagaraEmitterNameSettingsRef>	GpuEmitterDenyList;
	TSet<FName>								GpuDataInterfaceDenyList;

	FString									SystemDenyListString;
	FString									EmitterDenyListString;
	FString									GpuEmitterDenyListString;
	FString									GpuDataInterfaceDenyListString;

	static bool ParseIntoSet(const FString& StringList, TSet<FName>& OutSet)
	{
		TArray<FString> Names;
		StringList.ParseIntoArray(Names, TEXT(","));

		TSet<FName> ExistingSet;
		Swap(ExistingSet, OutSet);
		for (const FString& Name : Names)
		{
			OutSet.Emplace(Name);
		}
		return ExistingSet.Difference(OutSet).Num() > 0;
	}

	static bool ParseIntoSet(const FString& StringList, TSet<FNiagaraEmitterNameSettingsRef>& OutSet)
	{
		TArray<FString> Pairs;
		StringList.ParseIntoArray(Pairs, TEXT(","));

		TSet<FNiagaraEmitterNameSettingsRef> ExistingSet;
		Swap(ExistingSet, OutSet);
		for (const FString& Pair : Pairs)
		{
			FNiagaraEmitterNameSettingsRef EmitterRef(Pair);
			if (EmitterRef.IsValid())
			{
				OutSet.Emplace(EmitterRef);
			}
		}
		return ExistingSet.Difference(OutSet).Num() > 0;
	}

	static void UpdateSystemDenyList(IConsoleVariable*)
	{
		const bool bWasChanged = ParseIntoSet(SystemDenyListString, SystemDenyList);
		bUseSystemDenyList = SystemDenyList.Num() > 0;
		if (bWasChanged)
		{
			for (TObjectIterator<UNiagaraSystem> It; It; ++It)
			{
				if (UNiagaraSystem* System = *It)
				{
					System->UpdateScalability();
				}
			}
		}
	}

	static void UpdateEmitterDenyList(IConsoleVariable*)
	{
		bool bWasChanged = ParseIntoSet(EmitterDenyListString, EmitterDenyList);
		bWasChanged |= ParseIntoSet(GpuEmitterDenyListString, GpuEmitterDenyList);
		bWasChanged |= ParseIntoSet(GpuDataInterfaceDenyListString, GpuDataInterfaceDenyList);
		bUseEmitterDenyList = EmitterDenyList.Num() > 0 || GpuEmitterDenyList.Num() > 0 || GpuDataInterfaceDenyList.Num() > 0;
		if (bWasChanged)
		{
			for (TObjectIterator<UNiagaraSystem> It; It; ++It)
			{
				if (UNiagaraSystem* System = *It)
				{
					System->UpdateScalability();
				}
			}
		}
	}

	static FAutoConsoleVariableRef CVarNiagaraSetSystemDenyList(
		TEXT("fx.Niagara.SetSystemDenyList"),
		SystemDenyListString,
		TEXT("Set the system deny List to use. (i.e. NS_SystemA,NS_SystemB)"),
		FConsoleVariableDelegate::CreateStatic(UpdateSystemDenyList), 
		ECVF_Scalability | ECVF_Default
	);
	static FAutoConsoleVariableRef CVarNiagaraSetEmitterDenyList(
		TEXT("fx.Niagara.SetEmitterDenyList"),
		EmitterDenyListString,
		TEXT("Set the emitter deny list to use. (i.e. NS_SystemA:EmitterA,NS_SystemB:EmitterA)"),
		FConsoleVariableDelegate::CreateStatic(UpdateEmitterDenyList),
		ECVF_Scalability | ECVF_Default
	);
	static FAutoConsoleVariableRef CVarNiagaraSetGpuEmitterDenyList(
		TEXT("fx.Niagara.SetGpuEmitterDenyList"),
		GpuEmitterDenyListString,
		TEXT("Set the Gpu emitter deny list to use. (i.e. NS_SystemA:EmitterA,NS_SystemB:EmitterA)"),
		FConsoleVariableDelegate::CreateStatic(UpdateEmitterDenyList),
		ECVF_Scalability | ECVF_Default
	);
	static FAutoConsoleVariableRef CVarNiagaraSetGpuDataInterfaceDenyList(
		TEXT("fx.Niagara.SetGpuDataInterfaceDenyList"),
		GpuDataInterfaceDenyListString,
		TEXT("Set the Gpu data interface deny list to use.  (i.e. UMyDataInteraceA,UMyDataInteraceB)"),
		FConsoleVariableDelegate::CreateStatic(UpdateEmitterDenyList),
		ECVF_Scalability | ECVF_Default
	);

	bool IsSystemAllowedToRun(const UNiagaraSystem* System)
	{
		check(System != nullptr);
		return bUseSystemDenyList ? !SystemDenyList.Contains(System->GetFName()) : true;
	}

	bool IsEmitterAllowedToRun(const FNiagaraEmitterInstance* EmitterInstance)
	{
		if (bUseEmitterDenyList)
		{
			const UNiagaraSystem* NiagaraSystem = EmitterInstance->GetParentSystemInstance()->GetSystem();
			const FVersionedNiagaraEmitter CachedEmitter = EmitterInstance->GetCachedEmitter();
			FVersionedNiagaraEmitterData* EmitterData = CachedEmitter.GetEmitterData();
			if (EmitterData == nullptr)
			{
				return false;
			}

			FNiagaraEmitterNameSettingsRef EmitterRef;
			EmitterRef.SystemName = NiagaraSystem ? NiagaraSystem->GetFName() : NAME_None;
			EmitterRef.EmitterName = FName(*CachedEmitter.Emitter->GetUniqueEmitterName());

			if (EmitterDenyList.Contains(EmitterRef))
			{
				return false;
			}

			if (EmitterData->SimTarget == ENiagaraSimTarget::GPUComputeSim)
			{
				if (GpuEmitterDenyList.Contains(EmitterRef))
				{
					return false;
				}

				if (GpuDataInterfaceDenyList.Num() > 0)
				{
					if (const UNiagaraScript* GPUComputeScript = EmitterData->GetGPUComputeScript())
					{
						for (const FNiagaraScriptDataInterfaceInfo& DefaultDIInfo : GPUComputeScript->GetCachedDefaultDataInterfaces())
						{
							if (GpuDataInterfaceDenyList.Contains(DefaultDIInfo.Type.GetFName()))
							{
								return false;
							}
						}
					}
				}
			}
		}
		return true;
	}
}
