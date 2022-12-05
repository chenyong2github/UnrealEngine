// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextInterfaceGraph_EditorData.h"

#include "ControlRigDefines.h"
#include "AnimNextInterfaceGraph.h"
#include "AnimNextInterfaceGraph_EdGraphSchema.h"
#include "AnimNextInterfaceUncookedOnlyUtils.h"
#include "AnimNextInterfaceExecuteContext.h"
#include "Rigs/RigHierarchyPose.h"
#include "RigVMModel/RigVMFunctionLibrary.h"
#include "RigVMModel/RigVMNotifications.h"
#include "RigVMModel/Nodes/RigVMCollapseNode.h"
#include "Curves/CurveFloat.h"

UAnimNextInterfaceGraph_EditorData::UAnimNextInterfaceGraph_EditorData(const FObjectInitializer& ObjectInitializer)
{
	RigVMClient.Reset();
	RigVMClient.SetOuterClientHost(this, GET_MEMBER_NAME_CHECKED(UAnimNextInterfaceGraph_EditorData, RigVMClient));
	{
		TGuardValue<bool> DisableClientNotifs(RigVMClient.bSuspendNotifications, true);
		RigVMClient.AddModel(TEXT("RigVMGraph"), false, &ObjectInitializer);
		RigVMClient.GetOrCreateFunctionLibrary(false, &ObjectInitializer);
	}
	
	auto MakeEdGraph = [this, &ObjectInitializer](FName InName) -> UAnimNextInterfaceGraph_EdGraph*
	{
		UAnimNextInterfaceGraph_EdGraph* EdGraph = ObjectInitializer.CreateDefaultSubobject<UAnimNextInterfaceGraph_EdGraph>(this, InName);
		EdGraph->Schema = UAnimNextInterfaceGraph_EdGraphSchema::StaticClass();
		EdGraph->bAllowRenaming = false;
		EdGraph->bEditable = true;
		EdGraph->bAllowDeletion = false;
		EdGraph->bIsFunctionDefinition = false;
		EdGraph->Initialize(this);

		return EdGraph;
	};

	RootGraph = MakeEdGraph(TEXT("RootEdGraph"));
	FunctionLibraryEdGraph = MakeEdGraph(TEXT("RigVMFunctionLibraryEdGraph"));
}

void UAnimNextInterfaceGraph_EditorData::Serialize(FArchive& Ar)
{
	RigVMClient.SetOuterClientHost(this, GET_MEMBER_NAME_CHECKED(UAnimNextInterfaceGraph_EditorData, RigVMClient));

	UObject::Serialize(Ar);

	if(Ar.IsLoading())
	{
		if(RigVMGraph_DEPRECATED || RigVMFunctionLibrary_DEPRECATED)
		{
			TGuardValue<bool> DisableClientNotifs(RigVMClient.bSuspendNotifications, true);
			RigVMClient.SetFromDeprecatedData(RigVMGraph_DEPRECATED, RigVMFunctionLibrary_DEPRECATED);
		}
	}
}

void UAnimNextInterfaceGraph_EditorData::Initialize(bool bRecompileVM)
{
	UAnimNextInterfaceGraph* AnimNextInterfaceGraph = GetTypedOuter<UAnimNextInterfaceGraph>();

	if (RigVMClient.GetController(0) == nullptr)
	{
		check(RigVMClient.Num() == 1);
		check(RigVMClient.GetFunctionLibrary());
		
		RigVMClient.GetOrCreateController(RigVMClient.GetDefaultModel());
		RigVMClient.GetOrCreateController(RigVMClient.GetFunctionLibrary());

		// Init function library controllers
		for(URigVMLibraryNode* LibraryNode : RigVMClient.GetFunctionLibrary()->GetFunctions())
		{
			RigVMClient.GetOrCreateController(LibraryNode->GetContainedGraph());
		}
		
		if(bRecompileVM)
		{
			UE::AnimNext::InterfaceGraphUncookedOnly::FUtils::Compile(AnimNextInterfaceGraph);
		}
	}

	RootGraph->Initialize(this);
	FunctionLibraryEdGraph->Initialize(this);
	if(EntryPointGraph)
	{
		EntryPointGraph->Initialize(this);
	}
}

void UAnimNextInterfaceGraph_EditorData::PostLoad()
{
	Super::PostLoad();
	
	Initialize(/*bRecompileVM*/false);
}

FRigVMClient* UAnimNextInterfaceGraph_EditorData::GetRigVMClient()
{
	return &RigVMClient;
}

const FRigVMClient* UAnimNextInterfaceGraph_EditorData::GetRigVMClient() const
{
	return &RigVMClient;
}

IRigVMGraphFunctionHost* UAnimNextInterfaceGraph_EditorData::GetRigVMGraphFunctionHost()
{
	return this;
}

const IRigVMGraphFunctionHost* UAnimNextInterfaceGraph_EditorData::GetRigVMGraphFunctionHost() const
{
	return this;
}


void UAnimNextInterfaceGraph_EditorData::HandleRigVMGraphAdded(const FRigVMClient* InClient, const FString& InNodePath)
{
	if(URigVMGraph* RigVMGraph = InClient->GetModel(InNodePath))
	{
		RigVMGraph->SetExecuteContextStruct(FRigVMExecuteContext::StaticStruct());
	}
}

void UAnimNextInterfaceGraph_EditorData::HandleConfigureRigVMController(const FRigVMClient* InClient,
                                                                    URigVMController* InControllerToConfigure)
{
	InControllerToConfigure->OnModified().AddUObject(this, &UAnimNextInterfaceGraph_EditorData::HandleModifiedEvent);

	InControllerToConfigure->UnfoldStructDelegate.BindLambda([](const UStruct* InStruct) -> bool
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

	TWeakObjectPtr<UAnimNextInterfaceGraph_EditorData> WeakThis(this);

	// this delegate is used by the controller to determine variable validity
	// during a bind process. the controller itself doesn't own the variables,
	// so we need a delegate to request them from the owning blueprint
	InControllerToConfigure->GetExternalVariablesDelegate.BindLambda([](URigVMGraph* InGraph) -> TArray<FRigVMExternalVariable>
	{
		if (InGraph)
		{
			if(UAnimNextInterfaceGraph_EditorData* EditorData = InGraph->GetTypedOuter<UAnimNextInterfaceGraph_EditorData>())
			{
				if (UAnimNextInterfaceGraph* Graph = EditorData->GetTypedOuter<UAnimNextInterfaceGraph>())
				{
					return Graph->GetRigVMExternalVariables();
				}
			}
		}
		return TArray<FRigVMExternalVariable>();
	});

	// this delegate is used by the controller to retrieve the current bytecode of the VM
	InControllerToConfigure->GetCurrentByteCodeDelegate.BindLambda([WeakThis]() -> const FRigVMByteCode*
	{
		if (WeakThis.IsValid())
		{
			if(UAnimNextInterfaceGraph* Graph = WeakThis->GetTypedOuter<UAnimNextInterfaceGraph>())
			{
				if (Graph->RigVM)
				{
					return &Graph->RigVM->GetByteCode();
				}
			}
		}
		return nullptr;

	});

#if WITH_EDITOR
	InControllerToConfigure->SetupDefaultUnitNodeDelegates(TDelegate<FName(FRigVMExternalVariable, FString)>::CreateLambda(
		[](FRigVMExternalVariable InVariableToCreate, FString InDefaultValue) -> FName
		{
			return NAME_None;
		}
	));
#endif

}

UObject* UAnimNextInterfaceGraph_EditorData::GetEditorObjectForRigVMGraph(URigVMGraph* InVMGraph) const
{
	if(InVMGraph)
	{
		TArray<UAnimNextInterfaceGraph_EdGraph*> AllGraphs = {RootGraph, EntryPointGraph, FunctionLibraryEdGraph};
		for(UAnimNextInterfaceGraph_EdGraph* EdGraph : AllGraphs)
		{
			if(EdGraph)
			{
				if(EdGraph->ModelNodePath == InVMGraph->GetNodePath())
				{
					return EdGraph;
				}
			}
		}
	}
	return nullptr;
}

FRigVMGraphFunctionStore* UAnimNextInterfaceGraph_EditorData::GetRigVMGraphFunctionStore()
{
	return &GraphFunctionStore;
}

const FRigVMGraphFunctionStore* UAnimNextInterfaceGraph_EditorData::GetRigVMGraphFunctionStore() const
{
	return &GraphFunctionStore;
}


void UAnimNextInterfaceGraph_EditorData::RecompileVM()
{
	UE::AnimNext::InterfaceGraphUncookedOnly::FUtils::Compile(GetTypedOuter<UAnimNextInterfaceGraph>());
}

void UAnimNextInterfaceGraph_EditorData::RecompileVMIfRequired()
{
	if (bVMRecompilationRequired)
	{
		RecompileVM();
	}
}

void UAnimNextInterfaceGraph_EditorData::RequestAutoVMRecompilation()
{
	bVMRecompilationRequired = true;
	if (bAutoRecompileVM)
	{
		RecompileVMIfRequired();
	}
}

void UAnimNextInterfaceGraph_EditorData::HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject)
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
		{
			if (InGraph)
			{
				InGraph->ClearAST();
			}
			break;
		}

	case ERigVMGraphNotifType::PinDefaultValueChanged:
		{
			if (InGraph->GetRuntimeAST().IsValid())
			{
				URigVMPin* RootPin = CastChecked<URigVMPin>(InSubject)->GetRootPin();
				FRigVMASTProxy RootPinProxy = FRigVMASTProxy::MakeFromUObject(RootPin);
				const FRigVMExprAST* Expression = InGraph->GetRuntimeAST()->GetExprForSubject(RootPinProxy);
				if (Expression == nullptr)
				{
					InGraph->ClearAST();
					break;
				}
				else if (Expression->NumParents() > 1)
				{
					InGraph->ClearAST();
					break;
				}
			}
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

URigVMGraph* UAnimNextInterfaceGraph_EditorData::GetVMGraphForEdGraph(const UEdGraph* InGraph) const
{
	if (InGraph == RootGraph)
	{
		return RigVMClient.GetDefaultModel();
	}
	else
	{
		const UAnimNextInterfaceGraph_EdGraph* Graph = Cast<UAnimNextInterfaceGraph_EdGraph>(InGraph);
		check(Graph);

		if (Graph->bIsFunctionDefinition)
		{
			if (URigVMLibraryNode* LibraryNode = RigVMClient.GetFunctionLibrary()->FindFunction(*Graph->ModelNodePath))
			{
				return LibraryNode->GetContainedGraph();
			}
		}
	}

	return nullptr;
}

void UAnimNextInterfaceGraph_EditorData::CreateEdGraphForCollapseNode(URigVMCollapseNode* InNode)
{
	if (InNode->GetGraph()->IsA<URigVMFunctionLibrary>())
	{
		if (URigVMGraph* ContainedGraph = InNode->GetContainedGraph())
		{
			if (EntryPointGraph == nullptr)
			{
				// create a sub graph
				UAnimNextInterfaceGraph_EdGraph* RigFunctionGraph = NewObject<UAnimNextInterfaceGraph_EdGraph>(this, *InNode->GetName(), RF_Transactional);
				RigFunctionGraph->Schema = UAnimNextInterfaceGraph_EdGraphSchema::StaticClass();
				RigFunctionGraph->bAllowRenaming = true;
				RigFunctionGraph->bEditable = true;
				RigFunctionGraph->bAllowDeletion = true;
				RigFunctionGraph->ModelNodePath = ContainedGraph->GetNodePath();
				RigFunctionGraph->bIsFunctionDefinition = true;

				EntryPointGraph = RigFunctionGraph;
				RigFunctionGraph->Initialize(this);

				RigVMClient.GetOrCreateController(ContainedGraph)->ResendAllNotifications();
			}
		}
	}
}