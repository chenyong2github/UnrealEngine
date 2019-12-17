// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraCommon.h"
#include "NiagaraDataSet.h"
#include "NiagaraComponent.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraParameterCollection.h"
#include "NiagaraComponent.h"
#include "NiagaraScriptSourceBase.h"
#include "NiagaraStats.h"
#include "UObject/Linker.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "Modules/ModuleManager.h"
#include "NiagaraWorldManager.h"

DECLARE_CYCLE_STAT(TEXT("Niagara - Utilities - PrepareRapidIterationParameters"), STAT_Niagara_Utilities_PrepareRapidIterationParameters, STATGROUP_Niagara);

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
		}
	}
	ComponentsToReInit.Empty();

	for (UNiagaraComponent* Comp : ComponentsToReset)
	{
		if (Comp)
		{
			Comp->ResetSystem();
		}
	}
	ComponentsToReset.Empty();
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

// void FNiagaraSystemUpdateContext::Add(UNiagaraDataInterface* Interface, bool bReInit)
// {
// 	for (TObjectIterator<UNiagaraComponent> It; It; ++It)
// 	{
// 		UNiagaraComponent* Comp = *It;
// 		check(Comp);		
// 		if (FNiagaraSystemInstance* SystemInst = Comp->GetSystemInstance())
// 		{
// 			if (SystemInst->ContainsDataInterface(Interface))
// 			{
// 				AddInternal(SystemInst, bReInit);
// 			}
// 		}
// 	}
// }

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
	if (bDestroyOnAdd)
	{
		Comp->DeactivateImmediate();
	}

	if (bReInit)
	{
		ComponentsToReInit.AddUnique(Comp);
		SystemSimsToDestroy.AddUnique(Comp->GetAsset());
	}
	else
	{
		ComponentsToReset.AddUnique(Comp);
	}
}


//////////////////////////////////////////////////////////////////////////

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

FNiagaraVariable FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(FNiagaraVariable InVar, const TCHAR* InEmitterName, ENiagaraScriptUsage InUsage)
{
	FNiagaraVariable Var = InVar;

	TArray<FString> SplitName;
	Var.GetName().ToString().ParseIntoArray(SplitName, TEXT("."));
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

	FString OutVarStrName = FString::Join(SplitName, TEXT("."));
	Var.SetName(*OutVarStrName);
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
	check(false);
	return false;
}

UNiagaraDataInterface* FNiagaraScriptDataInterfaceCompileInfo::GetDefaultDataInterface() const
{
	// Note that this can be called on non-game threads. We ensure that the data interface CDO object is already in existence at application init time, so we don't allow this to be auto-created.
	UNiagaraDataInterface* Obj = CastChecked<UNiagaraDataInterface>(const_cast<UClass*>(Type.GetClass())->GetDefaultObject(false));
	return Obj;
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

#if WITH_EDITORONLY_DATA
void FNiagaraUtilities::PrepareRapidIterationParameters(const TArray<UNiagaraScript*>& Scripts, const TMap<UNiagaraScript*, UNiagaraScript*>& ScriptDependencyMap, const TMap<UNiagaraScript*, FString>& ScriptToEmitterNameMap)
{
	SCOPE_CYCLE_COUNTER(STAT_Niagara_Utilities_PrepareRapidIterationParameters);

	TMap<UNiagaraScript*, FNiagaraParameterStore> ScriptToPreparedParameterStoreMap;

	// Remove old and initialize new parameters.
	for (UNiagaraScript* Script : Scripts)
	{
		FNiagaraParameterStore& ParameterStoreToPrepare = ScriptToPreparedParameterStoreMap.FindOrAdd(Script);
		Script->RapidIterationParameters.CopyParametersTo(ParameterStoreToPrepare, false, FNiagaraParameterStore::EDataInterfaceCopyMethod::None);
		const FString* EmitterName = ScriptToEmitterNameMap.Find(Script);
		checkf(EmitterName != nullptr, TEXT("Script to emitter name map must have an entry for each script to be processed."));
		Script->GetSource()->CleanUpOldAndInitializeNewRapidIterationParameters(*EmitterName, Script->GetUsage(), Script->GetUsageId(), ParameterStoreToPrepare);
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

		bool bOverwriteParameters = false;
		if (Script->RapidIterationParameters.GetNumParameters() != PreparedParameterStore.GetNumParameters())
		{
			bOverwriteParameters = true;
		}
		else
		{
			for (const FNiagaraVariableWithOffset& ParamWithOffset : Script->RapidIterationParameters.GetSortedParameterOffsets())
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
