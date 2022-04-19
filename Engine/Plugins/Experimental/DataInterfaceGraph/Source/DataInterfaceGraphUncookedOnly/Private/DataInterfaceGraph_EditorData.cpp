// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataInterfaceGraph_EditorData.h"

#include "ControlRigDefines.h"
#include "DataInterfaceGraph.h"
#include "DataInterfaceGraph_EdGraphSchema.h"
#include "DataInterfaceUncookedOnlyUtils.h"
#include "DataInterfaceExecuteContext.h"
#include "Rigs/RigHierarchyPose.h"
#include "RigVMModel/RigVMFunctionLibrary.h"
#include "RigVMModel/RigVMNotifications.h"
#include "RigVMModel/Nodes/RigVMCollapseNode.h"
#include "Curves/CurveFloat.h"

UDataInterfaceGraph_EditorData::UDataInterfaceGraph_EditorData(const FObjectInitializer& ObjectInitializer)
{
	RigVMGraph = ObjectInitializer.CreateDefaultSubobject<URigVMGraph>(this, TEXT("RigVMGraph"));
	RigVMFunctionLibrary = ObjectInitializer.CreateDefaultSubobject<URigVMFunctionLibrary>(this, TEXT("RigVMFunctionLibrary"));
	RigVMGraph->SetDefaultFunctionLibrary(RigVMFunctionLibrary);
	
	auto MakeEdGraph = [this, &ObjectInitializer](FName InName) -> UDataInterfaceGraph_EdGraph*
	{
		UDataInterfaceGraph_EdGraph* EdGraph = ObjectInitializer.CreateDefaultSubobject<UDataInterfaceGraph_EdGraph>(this, InName);
		EdGraph->Schema = UDataInterfaceGraph_EdGraphSchema::StaticClass();
		EdGraph->bAllowRenaming = false;
		EdGraph->bEditable = false;
		EdGraph->bAllowDeletion = false;
		EdGraph->bIsFunctionDefinition = false;
		EdGraph->Initialize(this);

		return EdGraph;
	};

	RootGraph = MakeEdGraph(TEXT("RootEdGraph"));
	FunctionLibraryEdGraph = MakeEdGraph(TEXT("RigVMFunctionLibraryEdGraph"));
}

void UDataInterfaceGraph_EditorData::Initialize(bool bRecompileVM)
{
	UDataInterfaceGraph* DataInterfaceGraph = GetTypedOuter<UDataInterfaceGraph>();
	
	if (Controllers.Num() == 0)
	{
		GetOrCreateRigVMController(RigVMGraph);

		// Init function library controllers
		GetOrCreateRigVMController(RigVMFunctionLibrary);
		for(URigVMLibraryNode* LibraryNode : RigVMFunctionLibrary->GetFunctions())
		{
			GetOrCreateRigVMController(LibraryNode->GetContainedGraph());
		}
		
		if(bRecompileVM)
		{
			UE::DataInterfaceGraphUncookedOnly::FUtils::Compile(DataInterfaceGraph);
		}
	}

	RootGraph->Initialize(this);
	FunctionLibraryEdGraph->Initialize(this);
	if(EntryPointGraph)
	{
		EntryPointGraph->Initialize(this);
	}
}

void UDataInterfaceGraph_EditorData::PostLoad()
{
	Super::PostLoad();
	
	Initialize(/*bRecompileVM*/false);
}

void UDataInterfaceGraph_EditorData::RecompileVM()
{
	UE::DataInterfaceGraphUncookedOnly::FUtils::Compile(GetTypedOuter<UDataInterfaceGraph>());
}

void UDataInterfaceGraph_EditorData::RecompileVMIfRequired()
{
	if (bVMRecompilationRequired)
	{
		RecompileVM();
	}
}

void UDataInterfaceGraph_EditorData::RequestAutoVMRecompilation()
{
	bVMRecompilationRequired = true;
	if (bAutoRecompileVM)
	{
		RecompileVMIfRequired();
	}
}

void UDataInterfaceGraph_EditorData::HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject)
{
	bool bNotifForOthersPending = true;

	switch(InNotifType)
	{
	case ERigVMGraphNotifType::NodeAdded:
		{
			if (URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(InSubject))
			{
				CreateEdGraphForCollapseNode(CollapseNode);
				break;
			}
		}
		// Fall through to the next case
	case ERigVMGraphNotifType::LinkAdded:
	case ERigVMGraphNotifType::LinkRemoved:
	case ERigVMGraphNotifType::PinArraySizeChanged:
	case ERigVMGraphNotifType::PinDirectionChanged:
	case ERigVMGraphNotifType::PinDefaultValueChanged:
		{
			RequestAutoVMRecompilation();
			break;
		}
	}
	
	// if the notification still has to be sent...
	if (bNotifForOthersPending && !bSuspendModelNotificationsForOthers)
	{
		if (ModifiedEvent.IsBound())
		{
			ModifiedEvent.Broadcast(InNotifType, InGraph, InSubject);
		}
	}
}

URigVMGraph* UDataInterfaceGraph_EditorData::GetRigVMGraph(const UObject* InEditorObject) const
{
	return GetVMGraphForEdGraph(CastChecked<UEdGraph>(InEditorObject));
}

URigVMGraph* UDataInterfaceGraph_EditorData::GetVMGraphForEdGraph(const UEdGraph* InGraph) const
{
	if (InGraph == RootGraph)
	{
		return RigVMGraph;
	}
	else
	{
		const UDataInterfaceGraph_EdGraph* Graph = Cast<UDataInterfaceGraph_EdGraph>(InGraph);
		check(Graph);

		if (Graph->bIsFunctionDefinition)
		{
			if (URigVMLibraryNode* LibraryNode = RigVMFunctionLibrary->FindFunction(*Graph->ModelNodePath))
			{
				return LibraryNode->GetContainedGraph();
			}
		}
	}

	return nullptr;
}

URigVMController* UDataInterfaceGraph_EditorData::GetRigVMController(const URigVMGraph* InRigVMGraph) const
{
	TObjectPtr<URigVMController> const* ControllerPtr = Controllers.Find(InRigVMGraph);
	if (ControllerPtr)
	{
		return *ControllerPtr;
	}
	return nullptr;
}

URigVMController* UDataInterfaceGraph_EditorData::GetRigVMController(const UObject* InEditorObject) const
{
	return GetRigVMController(GetVMGraphForEdGraph(CastChecked<UEdGraph>(InEditorObject)));
}

URigVMController* UDataInterfaceGraph_EditorData::GetOrCreateRigVMController(URigVMGraph* InRigVMGraph)
{
	if (URigVMController* ExistingController = GetRigVMController(InRigVMGraph))
	{
		return ExistingController;
	}

	URigVMController* Controller = NewObject<URigVMController>(this);
	Controller->SetExecuteContextStruct(FRigVMExecuteContext::StaticStruct());
	Controller->SetGraph(InRigVMGraph);
	Controller->OnModified().AddUObject(this, &UDataInterfaceGraph_EditorData::HandleModifiedEvent);

	Controller->UnfoldStructDelegate.BindLambda([](const UStruct* InStruct) -> bool
	{
		if (InStruct == TBaseStructure<FQuat>::Get())
		{
			return false;
		}
		if (InStruct == FRuntimeFloatCurve::StaticStruct())
		{
			return false;
		}
		if (InStruct == FRigPose::StaticStruct())
		{
			return false;
		}
		return true;
	});

	TWeakObjectPtr<UDataInterfaceGraph_EditorData> WeakThis(this);

	// this delegate is used by the controller to determine variable validity
	// during a bind process. the controller itself doesn't own the variables,
	// so we need a delegate to request them from the owning blueprint
	Controller->GetExternalVariablesDelegate.BindLambda([](URigVMGraph* InGraph) -> TArray<FRigVMExternalVariable>
	{
		if (InGraph)
		{
			if(UDataInterfaceGraph_EditorData* EditorData = InGraph->GetTypedOuter<UDataInterfaceGraph_EditorData>())
			{
				if (UDataInterfaceGraph* Graph = EditorData->GetTypedOuter<UDataInterfaceGraph>())
				{
					return Graph->GetRigVMExternalVariables();
				}
			}
		}
		return TArray<FRigVMExternalVariable>();
	});

	// this delegate is used by the controller to retrieve the current bytecode of the VM
	Controller->GetCurrentByteCodeDelegate.BindLambda([WeakThis]() -> const FRigVMByteCode*
	{
		if (WeakThis.IsValid())
		{
			if(UDataInterfaceGraph* Graph = WeakThis->GetTypedOuter<UDataInterfaceGraph>())
			{
				if (Graph->RigVM)
				{
					return &Graph->RigVM->GetByteCode();
				}
			}
		}
		return nullptr;

	});

	Controller->IsFunctionAvailableDelegate.BindLambda([](URigVMLibraryNode* InFunction) -> bool
	{
		return true;	// @TODO: should only allow main entry point function here
	});

	Controller->IsDependencyCyclicDelegate.BindLambda([](UObject* InDependentObject, UObject* InDependencyObject) -> bool
	{
		return false;
	});

#if WITH_EDITOR
	Controller->SetupDefaultUnitNodeDelegates(TDelegate<FName(FRigVMExternalVariable, FString)>::CreateLambda(
		[](FRigVMExternalVariable InVariableToCreate, FString InDefaultValue) -> FName
		{
			return NAME_None;
		}
	));
#endif

	Controller->RemoveStaleNodes();
	Controllers.Add(InRigVMGraph, Controller);
	return Controller;
}

URigVMController* UDataInterfaceGraph_EditorData::GetOrCreateRigVMController(const UObject* InEditorObject)
{
	return GetOrCreateRigVMController(GetVMGraphForEdGraph(CastChecked<UEdGraph>(InEditorObject)));
}

void UDataInterfaceGraph_EditorData::CreateEdGraphForCollapseNode(URigVMCollapseNode* InNode)
{
	if (InNode->GetGraph()->IsA<URigVMFunctionLibrary>())
	{
		if (URigVMGraph* ContainedGraph = InNode->GetContainedGraph())
		{
			if (EntryPointGraph == nullptr)
			{
				// create a sub graph
				UDataInterfaceGraph_EdGraph* RigFunctionGraph = NewObject<UDataInterfaceGraph_EdGraph>(this, *InNode->GetName(), RF_Transactional);
				RigFunctionGraph->Schema = UDataInterfaceGraph_EdGraphSchema::StaticClass();
				RigFunctionGraph->bAllowRenaming = true;
				RigFunctionGraph->bEditable = true;
				RigFunctionGraph->bAllowDeletion = true;
				RigFunctionGraph->ModelNodePath = ContainedGraph->GetNodePath();
				RigFunctionGraph->bIsFunctionDefinition = true;

				EntryPointGraph = RigFunctionGraph;
				RigFunctionGraph->Initialize(this);

				GetOrCreateRigVMController(ContainedGraph)->ResendAllNotifications();
			}
		}
	}
}