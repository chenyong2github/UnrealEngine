// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntitySettings.h"
#include "MassProcessingPhase.h"
#include "VisualLogger/VisualLogger.h"
#include "UObject/UObjectHash.h"
#include "MassEntityDebug.h"
#include "Misc/CoreDelegates.h"
#if WITH_EDITOR
#include "ObjectEditorUtils.h"
#endif // WITH_EDITOR

//----------------------------------------------------------------------//
//  UPipeSettings
//----------------------------------------------------------------------//
UPipeSettings::UPipeSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	for (int i = 0; i < (int)EPipeProcessingPhase::MAX; ++i)
	{
		ProcessingPhasesConfig[i].PhaseName = *EnumToString(EPipeProcessingPhase(i));
	}

	FCoreDelegates::OnPostEngineInit.AddUObject(this, &UPipeSettings::BuildProcessorListAndPhases);
}

void UPipeSettings::BeginDestroy()
{
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);
	Super::BeginDestroy();
}

void UPipeSettings::BuildProcessorListAndPhases()
{
	if (bInitialized)
	{
		return;
	}

	BuildProcessorList();
	BuildPhases();
	bInitialized = true;
}

void UPipeSettings::BuildPhases()
{
#if WITH_EDITOR
	for (int i = 0; i < int(EPipeProcessingPhase::MAX); ++i)
	{
		FPipeProcessingPhaseConfig& PhaseConfig = ProcessingPhasesConfig[i];
		PhaseConfig.PhaseProcessor = NewObject<UPipeCompositeProcessor>(this, PhaseConfig.PhaseGroupClass
			, *FString::Printf(TEXT("ProcessingPhase_%s"), *PhaseConfig.PhaseName.ToString()));
		PhaseConfig.PhaseProcessor->SetGroupName(PhaseConfig.PhaseName);
		PhaseConfig.PhaseProcessor->SetProcessingPhase(EPipeProcessingPhase(i));
		const FString PhaseDumpDependencyGraphFileName = !DumpDependencyGraphFileName.IsEmpty() ? DumpDependencyGraphFileName + TEXT("_") + PhaseConfig.PhaseName.ToString() : FString();
		PhaseConfig.PhaseProcessor->CopyAndSort(PhaseConfig, PhaseDumpDependencyGraphFileName);
		
		FStringOutputDevice Ar;
		PhaseConfig.PhaseProcessor->DebugOutputDescription(Ar);
		PhaseConfig.Description = MoveTemp(Ar);
	}
#endif // WITH_EDITOR
}

void UPipeSettings::BuildProcessorList()
{
	ProcessorCDOs.Reset();
	for (FPipeProcessingPhaseConfig& PhaseConfig : ProcessingPhasesConfig)
	{
		PhaseConfig.ProcessorCDOs.Reset();
	}

	TArray<UClass*> SubClassess;
	GetDerivedClasses(UPipeProcessor::StaticClass(), SubClassess);

	for (int i = SubClassess.Num() - 1; i; --i)
	{
		if (SubClassess[i]->HasAnyClassFlags(CLASS_Abstract))
		{
			continue;
		}

		UPipeProcessor* ProcessorCDO = GetMutableDefault<UPipeProcessor>(SubClassess[i]);
		// we explicitly restrict adding UPipeCompositeProcessor. If needed by specific project a derived class can be added
		if (ProcessorCDO && SubClassess[i] != UPipeCompositeProcessor::StaticClass()
#if WITH_EDITOR
			&& ProcessorCDO->ShouldShowUpInSettings()
#endif // WITH_EDITOR
		)
		{
			ProcessorCDOs.Add(ProcessorCDO);
			if (ProcessorCDO->ShouldAutoAddToGlobalList())
			{
				ProcessingPhasesConfig[int(ProcessorCDO->GetProcessingPhase())].ProcessorCDOs.Add(ProcessorCDO);
			}
		}
	}

	ProcessorCDOs.Sort([](UPipeProcessor& LHS, UPipeProcessor& RHS) {
		return LHS.GetName().Compare(RHS.GetName()) < 0;
	});
}

void UPipeSettings::AddToActiveProcessorsList(TSubclassOf<UPipeProcessor> ProcessorClass)
{
	if (UPipeProcessor* ProcessorCDO = GetMutableDefault<UPipeProcessor>(ProcessorClass))
	{
		if (ProcessorClass == UPipeCompositeProcessor::StaticClass())
		{
			UE_VLOG_UELOG(this, LogPipe, Log, TEXT("%s adding PipeCompositeProcessor to the global processor list is unsupported"), ANSI_TO_TCHAR(__FUNCTION__));
		}
		else if (ProcessorClass->HasAnyClassFlags(CLASS_Abstract))
		{
			UE_VLOG_UELOG(this, LogPipe, Log, TEXT("%s unable to add %s due to it being an abstract class"), ANSI_TO_TCHAR(__FUNCTION__), *ProcessorClass->GetName());
		}
		else if (ProcessorCDOs.Find(ProcessorCDO) != INDEX_NONE)
		{
			UE_VLOG_UELOG(this, LogPipe, Log, TEXT("%s already in global processor list"), *ProcessorCDO->GetName());
		}
		else 
		{
			ensureMsgf(ProcessorCDO->ShouldAutoAddToGlobalList() == false, TEXT("%s missing from the global list while it's already marked to be auto-added"), *ProcessorCDO->GetName());
			ProcessorCDOs.Add(ProcessorCDO);
			ProcessorCDO->SetShouldAutoRegisterWithGlobalList(true);
		}
	}
}

const FPipeProcessingPhaseConfig* UPipeSettings::GetProcessingPhasesConfig()
{
	BuildProcessorListAndPhases();
	return ProcessingPhasesConfig;
}

#if WITH_EDITOR
void UPipeSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// @todo replacing a valid entry in the array is not handled right now. 

	static const FName PipeName = TEXT("Pipe");
	static const FName ProcessorCDOsName = GET_MEMBER_NAME_CHECKED(UPipeSettings, ProcessorCDOs);

	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayAdd)
	{
		// ignore adding elements to arrays since it would be 'None' at first
		return;
	}

	if (PropertyChangedEvent.Property)
	{
		const FName PropName = PropertyChangedEvent.Property->GetFName();
		if (PropName == ProcessorCDOsName)
		{
			BuildProcessorList();
		}

		BuildPhases();
		OnSettingsChange.Broadcast(PropertyChangedEvent);
	}
}

void UPipeSettings::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	static const FName AutoRegisterName = TEXT("bAutoRegisterWithProcessingPhases");

	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	FProperty* Property = PropertyChangedEvent.Property;
	FProperty* MemberProperty = nullptr;
	FEditPropertyChain::TDoubleLinkedListNode* LastPropertyNode = PropertyChangedEvent.PropertyChain.GetActiveMemberNode();
	while (LastPropertyNode && LastPropertyNode->GetNextNode())
	{
		LastPropertyNode = LastPropertyNode->GetNextNode();
	}

	if (LastPropertyNode)
	{
		MemberProperty = LastPropertyNode->GetValue();
	}

	if (MemberProperty && MemberProperty->GetFName() == AutoRegisterName)
	{
		BuildProcessorList();
	}
}
#endif // WITH_EDITOR
