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
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (Blueprint && Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(UControlRig::StaticClass()))
	{
		return true;
	}

	return false;
}

void FControlRigBlueprintCompiler::Compile(UBlueprint* Blueprint, const FKismetCompilerOptions& CompileOptions, FCompilerResultsLog& Results)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FControlRigBlueprintCompilerContext Compiler(Blueprint, Results, CompileOptions);
	Compiler.Compile();
}

void FControlRigBlueprintCompilerContext::MarkCompilationFailed(const FString& Message)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

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
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

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
				if (ControlRigBlueprint->Model == nullptr)
				{
					ControlRigBlueprint->PopulateModelFromGraph(RigGraph);
				}

				FControlRigGraphTraverser Traverser(ControlRigBlueprint->Model);
				Traverser.TraverseAndBuildPropertyLinks(ControlRigBlueprint);

				bool bEncounteredChange = false;
				for (UEdGraphNode* Node : RigGraph->Nodes)
				{
					UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(Node);
					if (RigNode)
					{
						bool bDisplayAsDisabled = !Traverser.IsWiredToExecution(RigNode->PropertyName);
						if (bDisplayAsDisabled != RigNode->IsDisplayAsDisabledForced())
						{
							RigNode->SetForceDisplayAsDisabled(bDisplayAsDisabled);
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
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	BuildPropertyLinks();
}

void FControlRigBlueprintCompilerContext::PostCompile()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigBlueprint* ControlRigBlueprint = Cast<UControlRigBlueprint>(Blueprint);
	if (ControlRigBlueprint)
	{
		// create sorted graph
		TArray<FControlRigOperator>& Operators = ControlRigBlueprint->GetControlRigBlueprintGeneratedClass()->Operators;
		const TArray<FControlRigBlueprintPropertyLink>& PropertyLinks = ControlRigBlueprint->PropertyLinks;

		TArray<FName> UnitNames;
		TMap<FName, int32> UnitNameToIndex;
		{
			DECLARE_SCOPE_HIERARCHICAL_COUNTER(CreateUnitNameMap)
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
			for (const FControlRigOperator& Operator : Operators)
			{
				FName UnitName = *Operator.CachedPropertyPath1.ToString();
				FStructProperty* StructProperty = CastField<FStructProperty>(ControlRigBlueprint->GeneratedClass->FindPropertyByName(UnitName));
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
			DECLARE_SCOPE_HIERARCHICAL_COUNTER(CycleDetection)

			// add all of the nodes
			FControlRigDAG SortGraph;
			for (int32 UnitIndex = 0; UnitIndex < UnitNames.Num(); UnitIndex++)
			{
				bool IsMutableUnit = false;
				bool IsOutputParameter = false;
				FStructProperty* StructProperty = CastField<FStructProperty>(ControlRigBlueprint->GeneratedClass->FindPropertyByName(UnitNames[UnitIndex]));
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

				const FControlRigModelNode* UnitNode = ControlRigBlueprint->Model->FindNode(UnitNames[UnitIndex]);
				if (UnitNode)
				{
					IsOutputParameter = UnitNode->IsParameter() && UnitNode->ParameterType == EControlRigModelParameterType::Output;
				}
				SortGraph.AddNode(IsMutableUnit, IsOutputParameter, UnitNames[UnitIndex]);
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

			TArray<FControlRigDAG::FNode> UnitOrder, UnitCycle;
			if (!SortGraph.TopologicalSort(UnitOrder, UnitCycle))
			{
#if WITH_EDITORONLY_DATA
				TSet<FName> UnitNamesInCycle;
				for (const FControlRigDAG::FNode& NodeInCycle : UnitCycle)
				{
					UnitNamesInCycle.Add(NodeInCycle.Name);
				}

				for (UEdGraph* UbergraphPage : Blueprint->UbergraphPages)
				{
					if (UControlRigGraph* ControlRigGraph = Cast<UControlRigGraph>(UbergraphPage))
					{
						for (UEdGraphNode* Node : ControlRigGraph->Nodes)
						{
							if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(Node))
							{
								if (FStructProperty* Property = RigNode->GetUnitProperty())
								{
									if (UnitNamesInCycle.Contains(Property->GetFName()))
									{
										RigNode->ErrorMsg = TEXT("The node is part of a cycle.");
										RigNode->ErrorType = EMessageSeverity::Error;
										RigNode->bHasCompilerMessage = true;

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

#if WITH_EDITORONLY_DATA
			// clear the errors on the graph
			{
				DECLARE_SCOPE_HIERARCHICAL_COUNTER(ClearNodeErrors)

				for (UEdGraph* UbergraphPage : Blueprint->UbergraphPages)
				{
					if (UControlRigGraph* ControlRigGraph = Cast<UControlRigGraph>(UbergraphPage))
					{
						for (UEdGraphNode* Node : ControlRigGraph->Nodes)
						{
							if (Node->ErrorType < (int32)EMessageSeverity::Info + 1)
							{
								Node->ErrorMsg.Reset();
								Node->ErrorType = (int32)EMessageSeverity::Info + 1;
								Node->bHasCompilerMessage = false;
							}
						}
					}
				}
			}
#endif

			{
				DECLARE_SCOPE_HIERARCHICAL_COUNTER(CreateOperatorsForVM)

				for (const FControlRigDAG::FNode Node : UnitOrder)
				{
					// copy all properties not originating from a unit
					for (const FControlRigDAG::FPin& Pin : Node.Outputs)
					{
						const int32 PropertyLinkIndex = Pin.Link;
						const FControlRigBlueprintPropertyLink& Link = PropertyLinks[PropertyLinkIndex];
						FString SourceUnitName = Link.GetSourceUnitName();
						FStructProperty* StructProperty = CastField<FStructProperty>(ControlRigBlueprint->GeneratedClass->FindPropertyByName(Node.Name));
						if (StructProperty)
						{
							if (StructProperty->Struct->IsChildOf(FRigUnit::StaticStruct()))
							{
								continue;
							}
						}

						Operators.Add(FControlRigOperator(EControlRigOpCode::Copy, Link.GetSourcePropertyPath(), Link.GetDestPropertyPath()));
					}

					FStructProperty* StructProperty = CastField<FStructProperty>(ControlRigBlueprint->GeneratedClass->FindPropertyByName(Node.Name));
					if (StructProperty)
					{
						if (StructProperty->Struct->IsChildOf(FRigUnit::StaticStruct()))
						{
							Operators.Add(FControlRigOperator(EControlRigOpCode::Exec, Node.Name.ToString(), FCachedPropertyPath()));
						}
					}

					// copy all properties to outputs
					for (const FControlRigDAG::FPin& Pin : Node.Outputs)
					{
						const int32 PropertyLinkIndex = Pin.Link;
						const FControlRigBlueprintPropertyLink& Link = PropertyLinks[PropertyLinkIndex];
						Operators.Add(FControlRigOperator(EControlRigOpCode::Copy, Link.GetSourcePropertyPath(), Link.GetDestPropertyPath()));
					}
				}
			}

			Operators.Add(FControlRigOperator(EControlRigOpCode::Done));
		}

		// guard against the control rig failing due to serialization changes
		if (PreviousOperatorCount > 1 && Operators.Num() == 1 && bIsFromVersionBeforeBeginExecution)
		{
			FString Message = FString::Printf(TEXT("The ControlRig '%s' needs to be recompiled in the editor."), *ControlRigBlueprint->GetOuter()->GetPathName());
			MarkCompilationFailed(Message);
			return;
		}

		// update allow source access properties
		{
			DECLARE_SCOPE_HIERARCHICAL_COUNTER(AllowAccessToSourceProperties)

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
			for (TFieldIterator<FProperty> It(MyClass); It; ++It)
			{
				if (FStructProperty* StructProperty = CastField<FStructProperty>(*It))
				{
					for (TFieldIterator<FProperty> It2(StructProperty->Struct); It2; ++It2)
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

	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER(FKismetCompilerContext::PostCompile)
		FKismetCompilerContext::PostCompile();
	}

	// ask the model to update all defaults
	if (ControlRigBlueprint)
	{
		if (ControlRigBlueprint->ModelController)
		{
			DECLARE_SCOPE_HIERARCHICAL_COUNTER(UpdateModelAfterCompilation)

			// ensure that blueprint storage arrays have the right size.
			// they might get out of sync due to compilation order.
			for (const FControlRigModelNode& Node : ControlRigBlueprint->Model->Nodes())
			{
				for (const FControlRigModelPin& Pin : Node.Pins)
				{
					if (Pin.Direction != EGPD_Input)
					{
						continue;
					}
					if (!Pin.IsArray())
					{
						continue;
					}

					int32 ArraySize = Pin.ArraySize();
					FString PinPath = ControlRigBlueprint->Model->GetPinPath(Pin.GetPair());
					ControlRigBlueprint->PerformArrayOperation(PinPath, [ArraySize](FScriptArrayHelper& InArrayHelper, int32 InArrayIndex)
					{
						while (InArrayHelper.Num() < ArraySize)
						{
							InArrayHelper.AddValue();
						}
						while (InArrayHelper.Num() > ArraySize)
						{
							InArrayHelper.RemoveValues(InArrayHelper.Num() - 1);
						}
						return true;
					}, true, true);
				}
			}

			TGuardValue<bool> GuardNotifs(ControlRigBlueprint->bSuspendModelNotificationsForOthers, true);
			ControlRigBlueprint->ModelController->ResendAllPinDefaultNotifications();
		}
	}
}

void FControlRigBlueprintCompilerContext::CopyTermDefaultsToDefaultObject(UObject* DefaultObject)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FKismetCompilerContext::CopyTermDefaultsToDefaultObject(DefaultObject);

	UControlRigBlueprint* ControlRigBlueprint = Cast<UControlRigBlueprint>(Blueprint);
	if (ControlRigBlueprint)
	{
		ControlRigBlueprint->CleanupBoneHierarchyDeprecated();

		UControlRig* ControlRig = CastChecked<UControlRig>(DefaultObject);
		ControlRig->Hierarchy = ControlRigBlueprint->HierarchyContainer;
		// copy available rig units info, so that control rig can do things with it
		ControlRig->AllowSourceAccessProperties = ControlRigBlueprint->AllowSourceAccessProperties;
		ControlRigBlueprint->UpdateParametersOnControlRig(ControlRig);
	}
}

void FControlRigBlueprintCompilerContext::EnsureProperGeneratedClass(UClass*& TargetUClass)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if( TargetUClass && !((UObject*)TargetUClass)->IsA(UControlRigBlueprintGeneratedClass::StaticClass()) )
	{
		FKismetCompilerUtilities::ConsignToOblivion(TargetUClass, Blueprint->bIsRegeneratingOnLoad);
		TargetUClass = nullptr;
	}
}

void FControlRigBlueprintCompilerContext::SpawnNewClass(const FString& NewClassName)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

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
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	NewControlRigBlueprintGeneratedClass = CastChecked<UControlRigBlueprintGeneratedClass>(ClassToUse);
}

void FControlRigBlueprintCompilerContext::CleanAndSanitizeClass(UBlueprintGeneratedClass* ClassToClean, UObject*& InOldCDO)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FKismetCompilerContext::CleanAndSanitizeClass(ClassToClean, InOldCDO);

	// Make sure our typed pointer is set
	check(ClassToClean == NewClass && NewControlRigBlueprintGeneratedClass == NewClass);

	// Reser cached unit properties
	NewControlRigBlueprintGeneratedClass->ControlUnitProperties.Empty();
	NewControlRigBlueprintGeneratedClass->RigUnitProperties.Empty();
}

#undef LOCTEXT_NAMESPACE
