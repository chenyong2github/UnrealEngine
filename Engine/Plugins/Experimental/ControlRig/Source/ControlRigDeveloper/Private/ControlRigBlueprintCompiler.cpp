// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ControlRigBlueprintCompiler.h"
#include "ControlRig.h"
#include "KismetCompiler.h"
#include "ControlRigBlueprint.h"
#include "Units/RigUnit.h"
#include "ControlRig/Private/Units/Execution/RigUnit_BeginExecution.h"
#include "Graph/ControlRigGraph.h"
#include "Graph/ControlRigGraphNode.h"
#include "Graph/ControlRigGraphSchema.h"
#include "ControlRigBlueprintGeneratedClass.h"
#include "Kismet2/KismetReinstanceUtilities.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "ControlRigGraphTraverser.h"
#include "ControlRigDAG.h"

DEFINE_LOG_CATEGORY_STATIC(LogControlRigCompiler, Log, All);
#define LOCTEXT_NAMESPACE "ControlRigBlueprintCompiler"

bool FControlRigBlueprintCompiler::CanCompile(const UBlueprint* Blueprint)
{
	if (Blueprint && Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(UControlRig::StaticClass()))
	{
		return true;
	}

	return false;
}

void FControlRigBlueprintCompiler::Compile(UBlueprint* Blueprint, const FKismetCompilerOptions& CompileOptions, FCompilerResultsLog& Results)
{
	FControlRigBlueprintCompilerContext Compiler(Blueprint, Results, CompileOptions);
	Compiler.Compile();
}

void FControlRigBlueprintCompilerContext::MarkCompilationFailed(const FString& Message)
{
	UControlRigBlueprint* ControlRigBlueprint = Cast<UControlRigBlueprint>(Blueprint);
	if (ControlRigBlueprint)
	{
		Blueprint->Status = BS_Error;
		Blueprint->MarkPackageDirty();
		UE_LOG(LogControlRigCompiler, Error, TEXT("%s"), *Message);
		MessageLog.Error(*Message);

#if WITH_EDITOR
		FNotificationInfo Info(FText::FromString(Message));
		Info.Image = FCoreStyle::Get().GetBrush(TEXT("MessageLog.Error"));
		Info.bFireAndForget = true;
		Info.FadeOutDuration = 10.0f;
		Info.ExpireDuration = 0.0f;
		TSharedPtr<SNotificationItem> NotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
		NotificationPtr->SetCompletionState(SNotificationItem::CS_Success);
#endif
	}
}

void FControlRigBlueprintCompilerContext::BuildPropertyLinks()
{
	UControlRigBlueprint* ControlRigBlueprint = Cast<UControlRigBlueprint>(Blueprint);
	if (ControlRigBlueprint)
	{
		// remove all property links
		ControlRigBlueprint->PropertyLinks.Reset();

		// Build property links from pin links
		for(UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			if (Graph->GetFName() != UControlRigGraphSchema::GraphName_ControlRig)
			{
				continue;
			}
			UControlRigGraph* RigGraph = Cast<UControlRigGraph>(Graph);
			if (RigGraph)
			{
				FControlRigGraphTraverser Traverser(ControlRigBlueprint, RigGraph);
				Traverser.TraverseAndBuildPropertyLinks();

				bool bEncounteredChange = false;
				for (UEdGraphNode* Node : RigGraph->Nodes)
				{
					UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(Node);
					if (RigNode)
					{
						bool bDisplayAsDisabled = !Traverser.IsWiredToExecution(RigNode);
						if (bDisplayAsDisabled != RigNode->IsDisplayAsDisabledForced())
						{
							RigNode->SetForceDisplayAsDisabled(bDisplayAsDisabled);
							RigNode->Modify();
							bEncounteredChange = true;
						}
					}
				}

				if (bEncounteredChange)
				{
					Graph->NotifyGraphChanged();
				}
			}
		}
	}
}

void FControlRigBlueprintCompilerContext::MergeUbergraphPagesIn(UEdGraph* Ubergraph)
{
	BuildPropertyLinks();
}

void FControlRigBlueprintCompilerContext::PostCompile()
{
	UControlRigBlueprint* ControlRigBlueprint = Cast<UControlRigBlueprint>(Blueprint);
	if (ControlRigBlueprint)
	{
		// create sorted graph
		TArray<FControlRigOperator>& Operators = ControlRigBlueprint->Operators;
		const TArray<FControlRigBlueprintPropertyLink>& PropertyLinks = ControlRigBlueprint->PropertyLinks;

		TArray<FName> UnitNames;
		TMap<FName, int32> UnitNameToIndex;
		for (const FControlRigBlueprintPropertyLink& Link : PropertyLinks)
		{
			FName SourceUnitName(*Link.GetSourceUnitName());
			if (!UnitNameToIndex.Contains(SourceUnitName))
			{
				UnitNames.Add(SourceUnitName);
				UnitNameToIndex.Add(SourceUnitName, UnitNameToIndex.Num());
			}
			FName DestUnitName(*Link.GetDestUnitName());
			if (!UnitNameToIndex.Contains(DestUnitName))
			{
				UnitNames.Add(DestUnitName);
				UnitNameToIndex.Add(DestUnitName, UnitNameToIndex.Num());
			}
		}

		// for UE-4.23.0:
		// determine if this control rig has been built with a previous version.
		// to do that we check if we had any mutable units - but no begin execution unit.
		// given that the current traverser is based on the begin execution unit we know
		// that the operator stack is from a previous version.
		bool bIsFromVersionBeforeBeginExecution = false;
		if (Operators.Num() > 1) // 1 is the "done" operator
		{
			bIsFromVersionBeforeBeginExecution = true;
			for (const FControlRigOperator& Operator : ControlRigBlueprint->Operators)
			{
				FName UnitName = *Operator.PropertyPath1;
				UStructProperty* StructProperty = Cast<UStructProperty>(ControlRigBlueprint->GeneratedClass->FindPropertyByName(UnitName));
				if (StructProperty)
				{
					if (StructProperty->Struct->IsChildOf(FRigUnit_BeginExecution::StaticStruct()))
					{
						bIsFromVersionBeforeBeginExecution = false;
						break;
					}
				}
			}
		}

		int32 PreviousOperatorCount = Operators.Num();
		Operators.Reset();

		if (UnitNames.Num() > 0)
		{
			// add all of the nodes
			FControlRigDAG SortGraph;
			for (int32 UnitIndex = 0; UnitIndex < UnitNames.Num(); UnitIndex++)
			{
				bool IsMutableUnit = false;
				UStructProperty* StructProperty = Cast<UStructProperty>(ControlRigBlueprint->GeneratedClass->FindPropertyByName(UnitNames[UnitIndex]));
				if (StructProperty)
				{
					if (StructProperty->Struct->IsChildOf(FRigUnitMutable::StaticStruct()))
					{
						IsMutableUnit = true;
					}
					else if (StructProperty->Struct->IsChildOf(FRigUnit_BeginExecution::StaticStruct()))
					{
						IsMutableUnit = true;
					}
				}
				SortGraph.AddNode(IsMutableUnit);
			}

			// add all of the links
			for (const FControlRigBlueprintPropertyLink& Link : PropertyLinks)
			{
				FName SourceUnitName(*Link.GetSourceUnitName());
				FName DestUnitName(*Link.GetDestUnitName());
				int32 SourceUnitIndex = UnitNameToIndex.FindChecked(SourceUnitName);
				int32 DestUnitIndex = UnitNameToIndex.FindChecked(DestUnitName);
				SortGraph.AddLink(SourceUnitIndex, DestUnitIndex, Link.GetSourceLinkIndex(), Link.GetDestLinkIndex());
			}

			// enable this for creating a new test case
			//SortGraph.DumpDag();

			TArray<int32> UnitOrder, UnitCycle;
			if (!SortGraph.TopologicalSort(UnitOrder, UnitCycle))
			{
#if WITH_EDITOR
				TSet<FName> UnitNamesInCycle;
				for (int32 IndexInCycle : UnitCycle)
				{
					UnitNamesInCycle.Add(UnitNames[IndexInCycle]);
				}

				for (UEdGraph* UbergraphPage : Blueprint->UbergraphPages)
				{
					if (UControlRigGraph* ControlRigGraph = Cast<UControlRigGraph>(UbergraphPage))
					{
						for (UEdGraphNode* Node : ControlRigGraph->Nodes)
						{
							if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(Node))
							{
								if (UStructProperty* Property = RigNode->GetUnitProperty())
								{
									if (UnitNamesInCycle.Contains(Property->GetFName()))
									{
										RigNode->ErrorMsg = TEXT("The node is part of a cycle.");
										RigNode->ErrorType = EMessageSeverity::Error;
										RigNode->bHasCompilerMessage = true;
										RigNode->Modify();

										MessageLog.Error(*FString::Printf(TEXT("Node '%s' is part of a cycle."), *Property->GetName()));
									}
								}
							}
						}
					}
				}
#endif

				// we found a cycle - so mark all nodes with errors
				MarkCompilationFailed(TEXT("The Control Rig compiler detected a cycle in the graph."));
				return;
			}

#if WITH_EDITOR
			// clear the errors on the graph
			for (UEdGraph* UbergraphPage : Blueprint->UbergraphPages)
			{
				if (UControlRigGraph* ControlRigGraph = Cast<UControlRigGraph>(UbergraphPage))
				{
					for (UEdGraphNode* Node : ControlRigGraph->Nodes)
					{
						if(Node->ErrorType < (int32)EMessageSeverity::Info+1)
						{
							Node->ErrorMsg.Reset();
							Node->ErrorType = (int32)EMessageSeverity::Info+1;
							Node->bHasCompilerMessage = false;
							Node->Modify();
						}
					}
				}
			}

			int32 SortedPropertyLinkIndex = 1;
			for (const int32 UnitIndex : UnitOrder)
			{
				UE_LOG(LogControlRigCompiler, Log, TEXT("%d. %s"), SortedPropertyLinkIndex++, *UnitNames[UnitIndex].ToString());
				for (FControlRigDAG::FPinMap::TConstIterator Iter = SortGraph.NodeOutputs[UnitIndex].CreateConstIterator(); Iter; ++Iter)
				{
					int32 Index = Iter.Value().Link;
					UE_LOG(LogControlRigCompiler, Log, TEXT("%d. %s -> %s"), SortedPropertyLinkIndex++, *PropertyLinks[Index].GetSourcePropertyPath(), *PropertyLinks[Index].GetDestPropertyPath());
				}
			}
#endif

			for (const int32 UnitIndex : UnitOrder)
			{
				// copy all properties not originating from a unit
				for (FControlRigDAG::FPinMap::TConstIterator Iter = SortGraph.NodeOutputs[UnitIndex].CreateConstIterator(); Iter; ++Iter)
				{
					int32 PropertyLinkIndex = Iter.Value().Link;
					const FControlRigBlueprintPropertyLink& Link = PropertyLinks[PropertyLinkIndex];
					FString SourceUnitName = Link.GetSourceUnitName();
					UStructProperty* StructProperty = Cast<UStructProperty>(ControlRigBlueprint->GeneratedClass->FindPropertyByName(UnitNames[UnitIndex]));
					if (StructProperty)
					{
						if (StructProperty->Struct->IsChildOf(FRigUnit::StaticStruct()))
						{
							continue;
						}
					}

					Operators.Add(FControlRigOperator(EControlRigOpCode::Copy, Link.GetSourcePropertyPath(), Link.GetDestPropertyPath()));
				}

				UStructProperty* StructProperty = Cast<UStructProperty>(ControlRigBlueprint->GeneratedClass->FindPropertyByName(UnitNames[UnitIndex]));
				if (StructProperty)
				{
					if (StructProperty->Struct->IsChildOf(FRigUnit::StaticStruct()))
					{
						Operators.Add(FControlRigOperator(EControlRigOpCode::Exec, UnitNames[UnitIndex].ToString(), TEXT("")));
					}
				}

				// copy all properties to outputs
				for (FControlRigDAG::FPinMap::TConstIterator Iter = SortGraph.NodeOutputs[UnitIndex].CreateConstIterator(); Iter; ++Iter)
				{
					int32 PropertyLinkIndex = Iter.Value().Link;
					const FControlRigBlueprintPropertyLink& Link = PropertyLinks[PropertyLinkIndex];
					Operators.Add(FControlRigOperator(EControlRigOpCode::Copy, Link.GetSourcePropertyPath(), Link.GetDestPropertyPath()));
				}
			}
		}

		Operators.Add(FControlRigOperator(EControlRigOpCode::Done));

		// guard against the control rig failing due to serialization changes
		if (PreviousOperatorCount > 1 && Operators.Num() == 1 && bIsFromVersionBeforeBeginExecution)
		{
			FString Message = FString::Printf(TEXT("The ControlRig '%s' needs to be recompiled in the editor."), *ControlRigBlueprint->GetOuter()->GetPathName());
			MarkCompilationFailed(Message);
			return;
		}

		// update allow source access properties
		{
			TArray<FName> SourcePropertyLinkArray;
			TArray<FName> DestPropertyLinkArray;

			auto GetPartialPath = [](FString Input) -> FString
			{
				const TCHAR SearchChar = TCHAR('.');
				int32 ParseIndex = 0;
				if (Input.FindChar(SearchChar, ParseIndex))
				{
					FString Root = Input.Left(ParseIndex+1);
					FString Child = Input.RightChop(ParseIndex+1);
					int32 SubParseIndex = 0;
					
					if (Child.FindChar(SearchChar, SubParseIndex))
					{
						return (Root + Child.Left(SubParseIndex));
					}
				}

				return Input;
			};

			//@todo: think about using ordered properties at the end
			for (const FControlRigBlueprintPropertyLink& PropertyLink: PropertyLinks)
			{
				SourcePropertyLinkArray.Add(FName(*GetPartialPath(PropertyLink.GetSourcePropertyPath())));
				DestPropertyLinkArray.Add(FName(*GetPartialPath(PropertyLink.GetDestPropertyPath())));
			}

			ControlRigBlueprint->AllowSourceAccessProperties.Reset();

			TArray<FName> PropertyList;
			UClass* MyClass = ControlRigBlueprint->GeneratedClass;
			for (TFieldIterator<UProperty> It(MyClass); It; ++It)
			{
				if (UStructProperty* StructProperty = Cast<UStructProperty>(*It))
				{
					for (TFieldIterator<UProperty> It2(StructProperty->Struct); It2; ++It2)
					{
						if ((*It2)->HasMetaData(TEXT("AllowSourceAccess")))
						{
							FString PartialPropertyPath = StructProperty->GetName() + TEXT(".") + (*It2)->GetName();
							PropertyList.Add(FName(*PartialPropertyPath));
						}
					}
				}
			}

			// this is prototype code and really slow
			for (int32 Index = 0; Index < PropertyList.Num(); ++Index)
			{
				// find source brute force
 				const FName& PropertyToSearch = PropertyList[Index];
				int32 DestIndex = DestPropertyLinkArray.Find(PropertyToSearch);
 				FName LastSource;
				if (DestIndex != INDEX_NONE)
				{
					FString& Value = ControlRigBlueprint->AllowSourceAccessProperties.Add(PropertyToSearch);
					Value = SourcePropertyLinkArray[DestIndex].ToString();
				}

			}
		}
	}

	FKismetCompilerContext::PostCompile();

	// We need to copy any pin defaults over to underlying properties once the class is built
	// as the defaults may not have been propagated from new nodes yet
	for(UEdGraph* UbergraphPage : Blueprint->UbergraphPages)
	{
		if(UControlRigGraph* ControlRigGraph = Cast<UControlRigGraph>(UbergraphPage))
		{
			for(UEdGraphNode* Node : ControlRigGraph->Nodes)
			{
				if(UControlRigGraphNode* ControlRigGraphNode = Cast<UControlRigGraphNode>(Node))
				{
					for(UEdGraphPin* Pin : ControlRigGraphNode->Pins)
					{
						ControlRigGraphNode->CopyPinDefaultsToProperties(Pin, false, false);
					}
				}
			}
		}
	}
}

void FControlRigBlueprintCompilerContext::CopyTermDefaultsToDefaultObject(UObject* DefaultObject)
{
	FKismetCompilerContext::CopyTermDefaultsToDefaultObject(DefaultObject);

	UControlRigBlueprint* ControlRigBlueprint = Cast<UControlRigBlueprint>(Blueprint);
	if (ControlRigBlueprint)
	{
		UControlRig* ControlRig = CastChecked<UControlRig>(DefaultObject);
		ControlRig->Operators = ControlRigBlueprint->Operators;
		ControlRig->Hierarchy.BaseHierarchy = ControlRigBlueprint->Hierarchy;
		// copy available rig units info, so that control rig can do things with it
		ControlRig->AllowSourceAccessProperties = ControlRigBlueprint->AllowSourceAccessProperties;
	}
}

void FControlRigBlueprintCompilerContext::EnsureProperGeneratedClass(UClass*& TargetUClass)
{
	if( TargetUClass && !((UObject*)TargetUClass)->IsA(UControlRigBlueprintGeneratedClass::StaticClass()) )
	{
		FKismetCompilerUtilities::ConsignToOblivion(TargetUClass, Blueprint->bIsRegeneratingOnLoad);
		TargetUClass = nullptr;
	}
}

void FControlRigBlueprintCompilerContext::SpawnNewClass(const FString& NewClassName)
{
	NewControlRigBlueprintGeneratedClass = FindObject<UControlRigBlueprintGeneratedClass>(Blueprint->GetOutermost(), *NewClassName);

	if (NewControlRigBlueprintGeneratedClass == nullptr)
	{
		NewControlRigBlueprintGeneratedClass = NewObject<UControlRigBlueprintGeneratedClass>(Blueprint->GetOutermost(), FName(*NewClassName), RF_Public | RF_Transactional);
	}
	else
	{
		// Already existed, but wasn't linked in the Blueprint yet due to load ordering issues
		FBlueprintCompileReinstancer::Create(NewControlRigBlueprintGeneratedClass);
	}
	NewClass = NewControlRigBlueprintGeneratedClass;
}

void FControlRigBlueprintCompilerContext::OnNewClassSet(UBlueprintGeneratedClass* ClassToUse)
{
	NewControlRigBlueprintGeneratedClass = CastChecked<UControlRigBlueprintGeneratedClass>(ClassToUse);
}

void FControlRigBlueprintCompilerContext::CleanAndSanitizeClass(UBlueprintGeneratedClass* ClassToClean, UObject*& InOldCDO)
{
	FKismetCompilerContext::CleanAndSanitizeClass(ClassToClean, InOldCDO);

	// Make sure our typed pointer is set
	check(ClassToClean == NewClass && NewControlRigBlueprintGeneratedClass == NewClass);

	// Reser cached unit properties
	NewControlRigBlueprintGeneratedClass->ControlUnitProperties.Empty();
	NewControlRigBlueprintGeneratedClass->RigUnitProperties.Empty();
}

#undef LOCTEXT_NAMESPACE
