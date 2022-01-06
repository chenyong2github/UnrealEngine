// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/RigVMController.h"
#include "RigVMModel/RigVMControllerActions.h"
#include "RigVMModel/Nodes/RigVMFunctionEntryNode.h"
#include "RigVMModel/Nodes/RigVMFunctionReturnNode.h"
#include "RigVMModel/Nodes/RigVMFunctionReferenceNode.h"
#include "RigVMCore/RigVMRegistry.h"
#include "RigVMCore/RigVMExecuteContext.h"
#include "RigVMCore/RigVMUnknownType.h"
#include "RigVMCore/RigVMByteCode.h"
#include "RigVMCompiler/RigVMCompiler.h"
#include "RigVMDeveloperModule.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/Package.h"
#include "Misc/CoreMisc.h"
#include "Algo/Sort.h"
#include "RigVMPythonUtils.h"
#include "RigVMTypeUtils.h"

#if WITH_EDITOR
#include "Exporters/Exporter.h"
#include "UnrealExporter.h"
#include "Factories.h"
#include "UObject/CoreRedirects.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "EditorStyleSet.h"
#include "AssetRegistryModule.h"
#endif

TMap<URigVMController::FControlRigStructPinRedirectorKey, FString> URigVMController::PinPathCoreRedirectors;
const TCHAR URigVMController::TArrayPrefix[] = TEXT("TArray<");
const TCHAR URigVMController::TObjectPtrPrefix[] = TEXT("TObjectPtr<");
const TCHAR URigVMController::TArrayTemplate[] = TEXT("TArray<%s>");
const TCHAR URigVMController::TObjectPtrTemplate[] = TEXT("TObjectPtr<%s%s>");

FRigVMControllerCompileBracketScope::FRigVMControllerCompileBracketScope(URigVMController* InController)
: Graph(nullptr), bSuspendNotifications(InController->bSuspendNotifications)
{
	check(InController);
	Graph = InController->GetGraph();
	check(Graph);
	
	if (bSuspendNotifications)
	{
		return;
	}
	Graph->Notify(ERigVMGraphNotifType::InteractionBracketOpened, nullptr);
}

FRigVMControllerCompileBracketScope::~FRigVMControllerCompileBracketScope()
{
	check(Graph);
	if (bSuspendNotifications)
	{
		return;
	}
	Graph->Notify(ERigVMGraphNotifType::InteractionBracketClosed, nullptr);
}

URigVMController::URigVMController()
	: bValidatePinDefaults(true)
	, bSuspendNotifications(false)
	, bReportWarningsAndErrors(true)
	, bIgnoreRerouteCompactnessChanges(false)
	, bIsRunningUnitTest(false)
{
	SetExecuteContextStruct(FRigVMExecuteContext::StaticStruct());
}

URigVMController::URigVMController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bValidatePinDefaults(true)
	, bReportWarningsAndErrors(true)
{
	ActionStack = CreateDefaultSubobject<URigVMActionStack>(TEXT("ActionStack"));
	SetExecuteContextStruct(FRigVMExecuteContext::StaticStruct());

	ActionStack->OnModified().AddLambda([&](ERigVMGraphNotifType NotifType, URigVMGraph* InGraph, UObject* InSubject) -> void {
		Notify(NotifType, InSubject);
	});
}

URigVMController::~URigVMController()
{
}

URigVMGraph* URigVMController::GetGraph() const
{
	if (Graphs.Num() == 0)
	{
		return nullptr;
	}
	return Graphs.Last();
}

void URigVMController::SetGraph(URigVMGraph* InGraph)
{
	ensure(Graphs.Num() < 2);

	URigVMGraph* LastGraph = GetGraph();
	if (LastGraph)
	{
		if(LastGraph == InGraph)
		{
			return;
		}
		LastGraph->OnModified().RemoveAll(this);
	}

	Graphs.Reset();
	if (InGraph != nullptr)
	{
		PushGraph(InGraph, false);
	}

	HandleModifiedEvent(ERigVMGraphNotifType::GraphChanged, GetGraph(), nullptr);
}

void URigVMController::PushGraph(URigVMGraph* InGraph, bool bSetupUndoRedo)
{
	URigVMGraph* LastGraph = GetGraph();
	if (LastGraph)
	{
		LastGraph->OnModified().RemoveAll(this);
	}

	check(InGraph);
	Graphs.Push(InGraph);

	InGraph->OnModified().AddUObject(this, &URigVMController::HandleModifiedEvent);

	if (bSetupUndoRedo)
	{
		ActionStack->AddAction(FRigVMPushGraphAction(InGraph));
	}
}

URigVMGraph* URigVMController::PopGraph(bool bSetupUndoRedo)
{
	ensure(Graphs.Num() > 1);
	
	URigVMGraph* LastGraph = GetGraph();
	if (LastGraph)
	{
		LastGraph->OnModified().RemoveAll(this);
	}

	Graphs.Pop();

	URigVMGraph* CurrentGraph = GetGraph();
	if (CurrentGraph)
	{
		CurrentGraph->OnModified().AddUObject(this, &URigVMController::HandleModifiedEvent);
	}

	if (bSetupUndoRedo)
	{
		ActionStack->AddAction(FRigVMPopGraphAction(LastGraph));
	}

	return LastGraph;
}

URigVMGraph* URigVMController::GetTopLevelGraph() const
{
	URigVMGraph* Graph = GetGraph();
	UObject* Outer = Graph->GetOuter();
	while (Outer)
	{
		if (URigVMGraph* OuterGraph = Cast<URigVMGraph>(Outer))
		{
			Graph = OuterGraph;
			Outer = Outer->GetOuter();
		}
		else if (URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(Outer))
		{
			Outer = Outer->GetOuter();
		}
		else
		{
			break;
		}
	}

	return Graph;
}

FRigVMGraphModifiedEvent& URigVMController::OnModified()
{
	return ModifiedEventStatic;
}

void URigVMController::Notify(ERigVMGraphNotifType InNotifType, UObject* InSubject) const
{
	if (bSuspendNotifications)
	{
		return;
	}
	if (URigVMGraph* Graph = GetGraph())
	{
		Graph->Notify(InNotifType, InSubject);
	}
}

void URigVMController::ResendAllNotifications()
{
	if (URigVMGraph* Graph = GetGraph())
	{
		for (URigVMLink* Link : Graph->Links)
		{
			Notify(ERigVMGraphNotifType::LinkRemoved, Link);
		}

		for (URigVMNode* Node : Graph->Nodes)
		{
			Notify(ERigVMGraphNotifType::NodeRemoved, Node);
		}

		for (URigVMNode* Node : Graph->Nodes)
		{
			Notify(ERigVMGraphNotifType::NodeAdded, Node);

			if (URigVMCommentNode* CommentNode = Cast<URigVMCommentNode>(Node))
			{
				Notify(ERigVMGraphNotifType::CommentTextChanged, Node);
			}
		}

		for (URigVMLink* Link : Graph->Links)
		{
			Notify(ERigVMGraphNotifType::LinkAdded, Link);
		}
	}
}

void URigVMController::HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject)
{
	switch (InNotifType)
	{
		case ERigVMGraphNotifType::GraphChanged:
		case ERigVMGraphNotifType::NodeAdded:
		case ERigVMGraphNotifType::NodeRemoved:
		case ERigVMGraphNotifType::LinkAdded:
		case ERigVMGraphNotifType::LinkRemoved:
		case ERigVMGraphNotifType::PinArraySizeChanged:
		case ERigVMGraphNotifType::ParameterAdded:
		case ERigVMGraphNotifType::ParameterRemoved:
		{
			if (InGraph)
			{
				InGraph->ClearAST();
			}
			break;
		}
		case ERigVMGraphNotifType::PinDefaultValueChanged:
		{
			if (InGraph->RuntimeAST.IsValid())
			{
				URigVMPin* RootPin = CastChecked<URigVMPin>(InSubject)->GetRootPin();
				FRigVMASTProxy RootPinProxy = FRigVMASTProxy::MakeFromUObject(RootPin);
				const FRigVMExprAST* Expression = InGraph->GetRuntimeAST()->GetExprForSubject(RootPinProxy);
				if (Expression == nullptr)
				{
					InGraph->ClearAST();
					break;
				}
				else if(Expression->NumParents() > 1)
				{
					InGraph->ClearAST();
					break;
				}
			}
			break;
		}
		case ERigVMGraphNotifType::VariableAdded:
		case ERigVMGraphNotifType::VariableRemoved:
		case ERigVMGraphNotifType::VariableRemappingChanged:
		{
			URigVMGraph* RootGraph = InGraph->GetRootGraph();
			if(URigVMFunctionLibrary* FunctionLibrary = Cast<URigVMFunctionLibrary>(RootGraph->GetRootGraph()))
			{
				URigVMNode* Node = CastChecked<URigVMNode>(InSubject);
				check(Node);

				if(URigVMLibraryNode* Function = FunctionLibrary->FindFunctionForNode(Node))
				{
					FunctionLibrary->ForEachReference(Function->GetFName(), [this](URigVMFunctionReferenceNode* Reference)
                    {
                        FRigVMControllerGraphGuard GraphGuard(this, Reference->GetGraph(), false);
                        Reference->GetGraph()->Notify(ERigVMGraphNotifType::VariableRemappingChanged, Reference);
                    });
				}
			}
		}
	}

	ModifiedEventStatic.Broadcast(InNotifType, InGraph, InSubject);
	if (ModifiedEventDynamic.IsBound())
	{
		ModifiedEventDynamic.Broadcast(InNotifType, InGraph, InSubject);
	}
}

TArray<FString> URigVMController::GeneratePythonCommands() 
{
	TArray<FString> Commands;

	const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());

	// Add local variables
	for (const FRigVMGraphVariableDescription& Variable : GetGraph()->LocalVariables)
	{
		const FString VariableName = GetSanitizedVariableName(Variable.Name.ToString());

		if (Variable.CPPTypeObject)
		{
			// FRigVMGraphVariableDescription AddLocalVariable(const FName& InVariableName, const FString& InCPPType, UObject* InCPPTypeObject, const FString& InDefaultValue, bool bSetupUndoRedo = true);
			Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_local_variable_from_object_path('%s', '%s', '%s', '%s')"),
						*GraphName,
						*VariableName,
						*Variable.CPPType,
						Variable.CPPTypeObject ? *Variable.CPPTypeObject->GetPathName() : TEXT(""),
						*Variable.DefaultValue));
		}
		else
		{
			// FRigVMGraphVariableDescription AddLocalVariable(const FName& InVariableName, const FString& InCPPType, UObject* InCPPTypeObject, const FString& InDefaultValue, bool bSetupUndoRedo = true);
			Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_local_variable('%s', '%s', None, '%s')"),
						*GraphName,
						*VariableName,
						*Variable.CPPType,
						*Variable.DefaultValue));
		}
	}
	
	
	// All nodes (except reroutes)
	for (URigVMNode* Node : GetGraph()->GetNodes())
	{
		Commands.Append(GetAddNodePythonCommands(Node));
	}

	// All links
	for (URigVMLink* Link : GetGraph()->GetLinks())
	{
		URigVMPin* SourcePin = Link->GetSourcePin();
		URigVMPin* TargetPin = Link->GetTargetPin();
		
		if (TargetPin->GetNode()->IsA<URigVMRerouteNode>())
		{
			continue;
		}

		if (SourcePin->GetInjectedNodes().Num() > 0 || TargetPin->GetInjectedNodes().Num() > 0)
		{
			continue;
		}

		// Iterate upstream until the source pin is not on a reroute node
		while (SourcePin && SourcePin->GetNode()->IsA<URigVMRerouteNode>())
		{
			const URigVMNode* Node = SourcePin->GetNode();
			if (Node->Pins.Num() > 0)
			{
				const TArray<URigVMLink*>& Links = Node->Pins[0]->GetSourceLinks();
				if (Links.Num() > 0)
				{
					SourcePin = Links[0]->GetSourcePin();
				}
				else
				{
					break;
				}
			}
		}

		if (SourcePin->GetNode()->IsA<URigVMRerouteNode>())
		{
			continue;
		}

		const FString SourcePinPath = GetSanitizedPinPath(SourcePin->GetPinPath());
		const FString TargetPinPath = GetSanitizedPinPath(TargetPin->GetPinPath());

		//bool AddLink(const FString& InOutputPinPath, const FString& InInputPinPath, bool bSetupUndoRedo = true);
		Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_link('%s', '%s')"),
					*GraphName,
					*SourcePinPath,
					*TargetPinPath));
	}

	// Reroutes
	{
		TArray<URigVMRerouteNode*> Reroutes;
		for (URigVMNode* Node : GetGraph()->GetNodes())
		{
			if (URigVMRerouteNode* Reroute = Cast<URigVMRerouteNode>(Node))
			{
				if (Reroute->Pins[0]->GetTargetLinks().Num() > 0)
				{
					Reroutes.Add(Reroute);
				}
			}
		}

		TArray<FString> ReroutesAdded;
		bool bNodesAdded = false;
		do
		{
			bNodesAdded = false;
			for (URigVMRerouteNode* Reroute : Reroutes)
			{
				const FString RerouteName = GetSanitizedNodeName(Reroute->GetName());

				if (ReroutesAdded.Contains(RerouteName))
				{
					continue;
				}

				// If this reroute has no target links, we will ignore it (it will not be created)
				if (Reroute->Pins.IsEmpty() || Reroute->Pins[0]->GetTargetLinks().IsEmpty())
				{
					continue;
				}

				TArray<URigVMPin*> TargetPins;
				for (URigVMLink* Link : Reroute->Pins[0]->GetTargetLinks())
				{
					URigVMPin* TargetPin = Link->GetTargetPin();
					if (!TargetPin->GetNode()->IsA<URigVMRerouteNode>() || ReroutesAdded.Contains(TargetPin->GetNode()->GetName()))
					{
						TargetPins.Add(TargetPin);
						continue;
					}
				}

				if (!TargetPins.IsEmpty())
				{
					URigVMPin* TargetPin = TargetPins[0];
					
					const FString TargetPinPath = GetSanitizedPinPath(TargetPin->GetPinPath());

					// AddRerouteNodeOnPin(const FString& InPinPath, bool bAsInput, bool bShowAsFullNode, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
					Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_reroute_node_on_pin('%s', True, %s, %s, '%s')"),
							*GraphName,
							*TargetPinPath,
							Reroute->GetShowsAsFullNode() ? TEXT("True") : TEXT("False"),
							*RigVMPythonUtils::Vector2DToPythonString(Reroute->GetPosition()),
							*RerouteName));

					ReroutesAdded.Add(RerouteName);
					bNodesAdded = true;

					// Add the rest of target links
					for (int32 i=1; i<TargetPins.Num(); ++i)
					{
						URigVMPin* OtherTargetPin = Reroute->Pins[0]->GetTargetLinks()[i]->GetTargetPin();
		
						if (OtherTargetPin->GetNode()->IsA<URigVMRerouteNode>())
						{
							continue;
						}

						if (OtherTargetPin->GetInjectedNodes().Num() > 0)
						{
							continue;
						}

						const FString OtherTargetPinPath = GetSanitizedPinPath(OtherTargetPin->GetPinPath());
						const FString FirstReroutePinPath = GetSanitizedPinPath(Reroute->Pins[0]->GetPinPath());

						//bool AddLink(const FString& InOutputPinPath, const FString& InInputPinPath, bool bSetupUndoRedo = true);
						Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_link('%s', '%s')"),
									*GraphName,
									*FirstReroutePinPath,
									*OtherTargetPinPath));
					}

					// Set default value in input if necessary
					URigVMPin* Pin = Reroute->Pins[0];
					const FString DefaultValue = Pin->GetDefaultValue();
					if (!DefaultValue.IsEmpty() && DefaultValue != TEXT("()"))
					{
						const FString PinPath = GetSanitizedPinPath(Pin->GetPinPath());

						Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').set_pin_default_value('%s', '%s')"),
									*GraphName,
									*PinPath,
									*Pin->GetDefaultValue()));
					}
					
					// Add source links					
					TArray<URigVMLink*> SourceLinks = Pin->GetSourceLinks(true);
					for (URigVMLink* Link : SourceLinks)
					{
						URigVMPin* SourcePin = Link->GetSourcePin(); 
						if (SourcePin->GetNode()->IsA<URigVMRerouteNode>())
						{
							continue;
						}

						const FString SourcePinPath = GetSanitizedPinPath(SourcePin->GetPinPath());
						const FString LinkTargetPinPath = GetSanitizedPinPath(Link->GetTargetPin()->GetPinPath());

						//bool AddLink(const FString& InOutputPinPath, const FString& InInputPinPath, bool bSetupUndoRedo = true);
						Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_link('%s', '%s')"),
									*GraphName,
									*SourcePinPath,
									*LinkTargetPinPath));
					}				
				}				
			}
		} while (bNodesAdded);
	}

	return Commands;
}

TArray<FString> URigVMController::GetAddNodePythonCommands(URigVMNode* Node) const
{
	TArray<FString> Commands;

	const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
	const FString NodeName = GetSanitizedNodeName(Node->GetName());

	if (const URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(Node))
	{
		if (const URigVMInjectionInfo* InjectionInfo = Cast<URigVMInjectionInfo>(UnitNode->GetOuter()))
		{
			const URigVMPin* InjectionInfoPin = InjectionInfo->GetPin();
			const FString InjectionInfoPinPath = GetSanitizedPinPath(InjectionInfoPin->GetPinPath());
			const FString InjectionInfoInputPinName = InjectionInfo->InputPin ? GetSanitizedPinName(InjectionInfo->InputPin->GetName()) : FString();
			const FString InjectionInfoOutputPinName = InjectionInfo->OutputPin ? GetSanitizedPinName(InjectionInfo->OutputPin->GetName()) : FString();

			//URigVMInjectionInfo* AddInjectedNodeFromStructPath(const FString& InPinPath, bool bAsInput, const FString& InScriptStructPath, const FName& InMethodName, const FName& InInputPinName, const FName& InOutputPinName, const FString& InNodeName = TEXT(""), bool bSetupUndoRedo = true);
			Commands.Add(FString::Printf(TEXT("%s_info = blueprint.get_controller_by_name('%s').add_injected_node_from_struct_path('%s', %s, '%s', '%s', '%s', '%s', '%s')"),
					*NodeName, 
					*GraphName, 
					*InjectionInfoPinPath, 
					InjectionInfoPin->GetDirection() == ERigVMPinDirection::Input ? TEXT("True") : TEXT("False"), 
					*UnitNode->GetScriptStruct()->GetPathName(), 
					*UnitNode->GetMethodName().ToString(), 
					*InjectionInfoInputPinName, 
					*InjectionInfoOutputPinName, 
					*UnitNode->GetName()));
		}
		else
		{
			// add_struct_node_from_struct_path(script_struct_path, method_name, position=[0.0, 0.0], node_name='', undo=True)
			Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_unit_node_from_struct_path('%s', 'Execute', %s, '%s')"),
					*GraphName,
					*UnitNode->GetScriptStruct()->GetPathName(),
					*RigVMPythonUtils::Vector2DToPythonString(UnitNode->GetPosition()),
					*NodeName));
		}
	}
	else if (const URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
	{
		const FString VariableName = GetSanitizedVariableName(VariableNode->GetVariableName().ToString());

		// add_variable_node(variable_name, cpp_type, cpp_type_object, is_getter, default_value, position=[0.0, 0.0], node_name='', undo=True)
		if (VariableNode->GetVariableDescription().CPPTypeObject)
		{
			Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_variable_node_from_object_path('%s', '%s', '%s', %s, '%s', %s, '%s')"),
					*GraphName,
					*VariableName,
					*VariableNode->GetVariableDescription().CPPType,
					*VariableNode->GetVariableDescription().CPPTypeObject->GetPathName(),
					VariableNode->IsGetter() ? TEXT("True") : TEXT("False"),
					*VariableNode->GetVariableDescription().DefaultValue,
					*RigVMPythonUtils::Vector2DToPythonString(VariableNode->GetPosition()),
					*NodeName));	
		}
		else
		{
			Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_variable_node('%s', '%s', None, %s, '%s', %s, '%s')"),
					*GraphName,
					*VariableName,
					*VariableNode->GetVariableDescription().CPPType,
					VariableNode->IsGetter() ? TEXT("True") : TEXT("False"),
					*VariableNode->GetVariableDescription().DefaultValue,
					*RigVMPythonUtils::Vector2DToPythonString(VariableNode->GetPosition()),
					*NodeName));	
		}
	}
	else if (const URigVMParameterNode* ParameterNode = Cast<URigVMParameterNode>(Node))
	{
		const FString ParameterName = GetSanitizedVariableName(ParameterNode->GetParameterName().ToString());

		// add_parameter_node_from_object_path(parameter_name, cpp_type, cpp_type_object_path, is_input, default_value, position=[0.0, 0.0], node_name='', undo=True)
		if (ParameterNode->GetParameterDescription().CPPTypeObject)
		{
			Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_parameter_node_from_object_path('%s', '%s', '%s', %s, '%s', %s, '%s')"),
					*GraphName,
					*ParameterName,
					*ParameterNode->GetParameterDescription().CPPType,
					*ParameterNode->GetParameterDescription().CPPTypeObject->GetPathName(),
					ParameterNode->IsInput() ? TEXT("True") : TEXT("False"),
					*ParameterNode->GetParameterDescription().DefaultValue,
					*RigVMPythonUtils::Vector2DToPythonString(ParameterNode->GetPosition()),
					*NodeName));	
		}
		else
		{
			Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_parameter_node('%s', '%s', None, %s, str(%s), %s, '%s')"),
					*GraphName,
					*ParameterName,
					*ParameterNode->GetParameterDescription().CPPType,
					ParameterNode->IsInput() ? TEXT("True") : TEXT("False"),
					*ParameterNode->GetParameterDescription().DefaultValue,
					*RigVMPythonUtils::Vector2DToPythonString(ParameterNode->GetPosition()),
					*NodeName));	
		}
	}
	else if (const URigVMCommentNode* CommentNode = Cast<URigVMCommentNode>(Node))
	{
		// add_comment_node(comment_text, position=[0.0, 0.0], size=[400.0, 300.0], color=[0.0, 0.0, 0.0, 0.0], node_name='', undo=True)
		Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_comment_node('%s', %s, %s, %s, '%s')"),
					*GraphName,
					*CommentNode->GetCommentText(),
					*RigVMPythonUtils::Vector2DToPythonString(CommentNode->GetPosition()),
					*RigVMPythonUtils::Vector2DToPythonString(CommentNode->GetSize()),
					*RigVMPythonUtils::LinearColorToPythonString(CommentNode->GetNodeColor()),
					*NodeName));	
	}
	else if (const URigVMBranchNode* BranchNode = Cast<URigVMBranchNode>(Node))
	{
		// add_branch_node(position=[0.0, 0.0], node_name='', undo=True)
		Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_branch_node(%s, '%s')"),
						*GraphName,
						*RigVMPythonUtils::Vector2DToPythonString(BranchNode->GetPosition()),
						*NodeName));	
	}
	else if (const URigVMIfNode* IfNode = Cast<URigVMIfNode>(Node))
	{
		// add_if_node(cpp_type, cpp_type_object_path, position=[0.0, 0.0], node_name='', undo=True)
		URigVMPin* ResultPin = IfNode->FindPin(URigVMIfNode::ResultName);
		Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_if_node('%s', '%s', %s, '%s')"),
						*GraphName,
						*ResultPin->GetCPPType(),
						*ResultPin->CPPTypeObject->GetPathName(),
						*RigVMPythonUtils::Vector2DToPythonString(IfNode->GetPosition()),
						*NodeName));
	}
	else if (const URigVMSelectNode* SelectNode = Cast<URigVMSelectNode>(Node))
	{
		// add_select_node(cpp_type, cpp_type_object_path, position=[0.0, 0.0], node_name='', undo=True)
		URigVMPin* ResultPin = SelectNode->FindPin(URigVMSelectNode::ResultName);
		Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_select_node('%s', '%s', %s, '%s')"),
						*GraphName,
						*ResultPin->GetCPPType(),
						*ResultPin->CPPTypeObject->GetPathName(),
						*RigVMPythonUtils::Vector2DToPythonString(SelectNode->GetPosition()),
						*NodeName));
	}
	else if (const URigVMPrototypeNode* PrototypeNode = Cast<URigVMPrototypeNode>(Node))
	{
		// add_prototype_node(notation, position=[0.0, 0.0], node_name='', undo=True)
		Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_prototype_node('%s', %s, '%s')"),
						*GraphName,
						*PrototypeNode->GetNotation().ToString(),
						*RigVMPythonUtils::Vector2DToPythonString(PrototypeNode->GetPosition()),
						*NodeName));
	}
	else if (const URigVMEnumNode* EnumNode = Cast<URigVMEnumNode>(Node))
	{
		// add_enum_node(cpp_type_object_path, position=[0.0, 0.0], node_name='', undo=True)
		Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_enum_node('%s', %s, '%s')"),
						*GraphName,
						*EnumNode->GetCPPTypeObject()->GetPathName(),
						*RigVMPythonUtils::Vector2DToPythonString(EnumNode->GetPosition()),
						*NodeName));
	}
	else if (const URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(Node))
	{
		const FString ContainedGraphName = GetSanitizedGraphName(LibraryNode->GetContainedGraph()->GetGraphName());

		// AddFunctionReferenceNode(URigVMLibraryNode* InFunctionDefinition, const FVector2D& InNodePosition = FVector2D::ZeroVector, const FString& InNodeName = TEXT(""), bool bSetupUndoRedo = true);
		URigVMFunctionLibrary* Library = LibraryNode->GetLibrary();
		if (!Library || Library == GetGraph()->GetDefaultFunctionLibrary())
		{
			Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_function_reference_node(function_%s, %s, '%s')"),
						*GraphName,
						*RigVMPythonUtils::NameToPep8(ContainedGraphName),
						*RigVMPythonUtils::Vector2DToPythonString(LibraryNode->GetPosition()), 
						*NodeName));
		}
		else
		{
			Commands.Add(FString::Printf(TEXT("function_blueprint = unreal.load_object(name = '%s', outer = None)"),
				*Library->GetOuter()->GetPathName()));
			Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_function_reference_node(function_blueprint.get_local_function_library().find_function('%s'), %s, '%s')"),
						*GraphName,
						*NodeName,
						*RigVMPythonUtils::Vector2DToPythonString(LibraryNode->GetPosition()), 
						*NodeName));
		}

		if (Node->IsA<URigVMCollapseNode>())
		{
			Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').promote_function_reference_node_to_collapse_node('%s')"),
					*GraphName,
					*NodeName));
			Commands.Add(FString::Printf(TEXT("library_controller.remove_function_from_library('%s')"),
					*ContainedGraphName));
		}
	}
	else if (const URigVMRerouteNode* RerouteNode = Cast<URigVMRerouteNode>(Node))
	{
		// Do nothing, we need to create the links first
	}
	else if (Node->IsA<URigVMFunctionEntryNode>() || Node->IsA<URigVMFunctionReturnNode>())
	{
		
		
	}
	else if (const URigVMArrayNode* ArrayNode = Cast<URigVMArrayNode>(Node))
	{
		FString OpCodeString = StaticEnum<ERigVMOpCode>()->GetNameStringByValue((int64)ArrayNode->GetOpCode());
		OpCodeString = RigVMPythonUtils::NameToPep8(OpCodeString);
		OpCodeString.ToUpperInline();
		
		// add_array_node(opcode, cpp_type, cpp_type_object, position=[0.0, 0.0], node_name='', undo=True)
		if (ArrayNode->GetCPPTypeObject())
		{
			Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_array_node_from_object_path(unreal.RigVMOpCode.%s, '%s', '%s', %s, '%s')"),
					*GraphName,
					*OpCodeString,
					*ArrayNode->GetCPPType(),
					*ArrayNode->GetCPPTypeObject()->GetPathName(),
					*RigVMPythonUtils::Vector2DToPythonString(ArrayNode->GetPosition()),
					*NodeName));	
		}
		else
		{
			Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_array_node(unreal.RigVMOpCode.%s, '%s', None, %s, '%s')"),
					*GraphName,
					*OpCodeString,
					*ArrayNode->GetCPPType(),
					*RigVMPythonUtils::Vector2DToPythonString(ArrayNode->GetPosition()),
					*NodeName));	
		}
	}
	else
	{
		ensure(false);
	}

	if (!Commands.IsEmpty())
	{
		for (const URigVMPin* Pin : Node->GetPins())
		{
			if (Pin->GetDirection() == ERigVMPinDirection::Output || Pin->GetDirection() == ERigVMPinDirection::Hidden)
			{
				continue;
			}
			
			const FString DefaultValue = Pin->GetDefaultValue();
			if (!DefaultValue.IsEmpty() && DefaultValue != TEXT("()"))
			{
				const FString PinPath = GetSanitizedPinPath(Pin->GetPinPath());

				Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').set_pin_default_value('%s', '%s')"),
							*GraphName,
							*PinPath,
							*Pin->GetDefaultValue()));
			}

			if (!Pin->GetBoundVariablePath().IsEmpty())
			{
				const FString PinPath = GetSanitizedPinPath(Pin->GetPinPath());

				Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').bind_pin_to_variable('%s', '%s')"),
							*GraphName,
							*PinPath,
							*Pin->GetBoundVariablePath()));
			}
		}
	}

	return Commands;
}

#if WITH_EDITOR

URigVMUnitNode* URigVMController::AddUnitNode(UScriptStruct* InScriptStruct, const FName& InMethodName, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if(!IsValidGraph())
	{
		return nullptr;
	}

	if (GetGraph()->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot add unit nodes to function library graphs."));
		return nullptr;
	}

	if (InScriptStruct == nullptr)
	{
		ReportError(TEXT("InScriptStruct is null."));
		return nullptr;
	}
	if (InMethodName == NAME_None)
	{
		ReportError(TEXT("InMethodName is None."));
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	FString FunctionName = FString::Printf(TEXT("F%s::%s"), *InScriptStruct->GetName(), *InMethodName.ToString());
	FRigVMFunctionPtr Function = FRigVMRegistry::Get().FindFunction(*FunctionName);
	if (Function == nullptr)
	{
		ReportErrorf(TEXT("RIGVM_METHOD '%s' cannot be found."), *FunctionName);
		return nullptr;
	}

	FString StructureError;
	if (!FRigVMStruct::ValidateStruct(InScriptStruct, &StructureError))
	{
		ReportErrorf(TEXT("Failed to validate struct '%s': %s"), *InScriptStruct->GetName(), *StructureError);
		return nullptr;
	}

	FStructOnScope StructOnScope(InScriptStruct);
	FRigVMStruct* StructMemory = (FRigVMStruct*)StructOnScope.GetStructMemory();
	InScriptStruct->InitializeDefaultValue((uint8*)StructMemory);
	const bool bIsEventNode = (!StructMemory->GetEventName().IsNone());
	if (bIsEventNode)
	{
		// don't allow event nodes in anything but top level graphs
		if (!Graph->IsTopLevelGraph())
		{
			ReportAndNotifyError(TEXT("Event nodes can only be added to top level graphs."));
			return nullptr;
		}

		// don't allow several event nodes in the main graph
		TObjectPtr<URigVMNode> EventNode = FindEventNode(InScriptStruct);
		if (EventNode != nullptr)
		{
			const FString ErrorMessage = FString::Printf(TEXT("Rig Graph can only contain one single %s node."),
															*InScriptStruct->GetDisplayNameText().ToString());
			ReportAndNotifyError(ErrorMessage);
			return Cast<URigVMUnitNode>(EventNode);
		}
	}
	
	FString Name = GetValidNodeName(InNodeName.IsEmpty() ? InScriptStruct->GetName() : InNodeName);
	URigVMUnitNode* Node = NewObject<URigVMUnitNode>(Graph, *Name);
	Node->ScriptStruct = InScriptStruct;
	Node->MethodName = InMethodName;
	Node->Position = InPosition;
	Node->NodeTitle = InScriptStruct->GetMetaData(TEXT("DisplayName"));
	
	FString NodeColorMetadata;
	InScriptStruct->GetStringMetaDataHierarchical(*URigVMNode::NodeColorName, &NodeColorMetadata);
	if (!NodeColorMetadata.IsEmpty())
	{
		Node->NodeColor = GetColorFromMetadata(NodeColorMetadata);
	}

	FString ExportedDefaultValue;
	CreateDefaultValueForStructIfRequired(InScriptStruct, ExportedDefaultValue);
	AddPinsForStruct(InScriptStruct, Node, nullptr, ERigVMPinDirection::Invalid, ExportedDefaultValue, true);

	Graph->Nodes.Add(Node);
	if (!bSuspendNotifications)
	{
		Graph->MarkPackageDirty();
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMAddUnitNodeAction Action;
	if (bSetupUndoRedo)
	{
		Action = FRigVMAddUnitNodeAction(Node);
		Action.Title = FString::Printf(TEXT("Add %s Node"), *Node->GetNodeTitle());
		ActionStack->BeginAction(Action);
	}

	Notify(ERigVMGraphNotifType::NodeAdded, Node);

	if (UnitNodeCreatedContext.IsValid())
	{
		if (TSharedPtr<FStructOnScope> StructScope = Node->ConstructStructInstance())
		{
			TGuardValue<FName> NodeNameScope(UnitNodeCreatedContext.NodeName, Node->GetFName());
			FRigVMStruct* StructInstance = (FRigVMStruct*)StructScope->GetStructMemory();
			StructInstance->OnUnitNodeCreated(UnitNodeCreatedContext);
		}
	}

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	if (bPrintPythonCommand)
	{
		TArray<FString> Commands = GetAddNodePythonCommands(Node);
		for (const FString& Command : Commands)
		{
			RigVMPythonUtils::Print(GetGraphOuterName(),
				FString::Printf(TEXT("%s"), *Command));
		}
	}

	return Node;
}

URigVMUnitNode* URigVMController::AddUnitNodeFromStructPath(const FString& InScriptStructPath, const FName& InMethodName, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if(!IsValidGraph())
	{
		return nullptr;
	}

	UScriptStruct* ScriptStruct = URigVMPin::FindObjectFromCPPTypeObjectPath<UScriptStruct>(InScriptStructPath);
	if (ScriptStruct == nullptr)
	{
		ReportErrorf(TEXT("Cannot find struct for path '%s'."), *InScriptStructPath);
		return nullptr;
	}

	return AddUnitNode(ScriptStruct, InMethodName, InPosition, InNodeName, bSetupUndoRedo, bPrintPythonCommand);
}

URigVMVariableNode* URigVMController::AddVariableNode(const FName& InVariableName, const FString& InCPPType, UObject* InCPPTypeObject, bool bIsGetter, const FString& InDefaultValue, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot add variables nodes to function library graphs."));
		return nullptr;
	}

	// check if the operation will cause to dirty assets
	if(bSetupUndoRedo)
	{
		if(URigVMFunctionLibrary* OuterLibrary = Graph->GetTypedOuter<URigVMFunctionLibrary>())
		{
			if(URigVMLibraryNode* OuterFunction = OuterLibrary->FindFunctionForNode(Graph->GetTypedOuter<URigVMCollapseNode>()))
			{
				// Make sure there is no local variable with that name
				bool bFoundLocalVariable = false;
				for (FRigVMGraphVariableDescription& LocalVariable : OuterFunction->GetContainedGraph()->LocalVariables)
				{
					if (LocalVariable.Name == InVariableName)
					{
						bFoundLocalVariable = true;
						break;
					}
				}

				if (!bFoundLocalVariable)
				{
					// Make sure there is no external variable with that name
					TArray<FRigVMExternalVariable> ExternalVariables = OuterFunction->GetContainedGraph()->GetExternalVariables();
					bool bFoundExternalVariable = false;
					for(const FRigVMExternalVariable& ExternalVariable : ExternalVariables)
					{
						if(ExternalVariable.Name == InVariableName)
						{
							bFoundExternalVariable = true;
							break;
						}
					}

					if(!bFoundExternalVariable)
					{
						// Warn the user the changes are not undoable
						if(RequestBulkEditDialogDelegate.IsBound())
						{
							FRigVMController_BulkEditResult Result = RequestBulkEditDialogDelegate.Execute(OuterFunction, ERigVMControllerBulkEditType::AddVariable);
							if(Result.bCanceled)
							{
								return nullptr;
							}
							bSetupUndoRedo = Result.bSetupUndoRedo;
						}
					}
				}
			}
		}
	}

	if (InCPPTypeObject == nullptr)
	{
		InCPPTypeObject = URigVMCompiler::GetScriptStructForCPPType(InCPPType);
	}
	if (InCPPTypeObject == nullptr)
	{
		InCPPTypeObject = URigVMPin::FindObjectFromCPPTypeObjectPath<UObject>(InCPPType);
	}

	FString CPPType = PostProcessCPPType(InCPPType, InCPPTypeObject);
	
	FString Name = GetValidNodeName(InNodeName.IsEmpty() ? FString(TEXT("VariableNode")) : InNodeName);
	URigVMVariableNode* Node = NewObject<URigVMVariableNode>(Graph, *Name);
	Node->Position = InPosition;

	if (!bIsGetter)
	{
		URigVMPin* ExecutePin = NewObject<URigVMPin>(Node, FRigVMStruct::ExecuteContextName);
		ExecutePin->CPPType = FString::Printf(TEXT("F%s"), *ExecuteContextStruct->GetName());
		ExecutePin->CPPTypeObject = ExecuteContextStruct;
		ExecutePin->CPPTypeObjectPath = *ExecutePin->CPPTypeObject->GetPathName();
		ExecutePin->Direction = ERigVMPinDirection::IO;
		AddNodePin(Node, ExecutePin);
	}

	URigVMPin* VariablePin = NewObject<URigVMPin>(Node, *URigVMVariableNode::VariableName);
	VariablePin->CPPType = TEXT("FName");
	VariablePin->Direction = ERigVMPinDirection::Hidden;
	VariablePin->DefaultValue = InVariableName.ToString();
	VariablePin->CustomWidgetName = TEXT("VariableName");
	AddNodePin(Node, VariablePin);

	URigVMPin* ValuePin = NewObject<URigVMPin>(Node, *URigVMVariableNode::ValueName);

	FRigVMExternalVariable ExternalVariable = GetVariableByName(InVariableName);
	if(ExternalVariable.IsValid(true))
	{
		ValuePin->CPPType = ExternalVariable.TypeName.ToString();
		ValuePin->CPPTypeObject = ExternalVariable.TypeObject;
		ValuePin->bIsDynamicArray = ExternalVariable.bIsArray;

		if(ValuePin->bIsDynamicArray && !ValuePin->CPPType.StartsWith(TArrayPrefix))
		{
			ValuePin->CPPType = FString::Printf(TArrayTemplate, *ValuePin->CPPType);
		}
	}
	else
	{
		ValuePin->CPPType = CPPType;

		if (UClass* Class = Cast<UClass>(InCPPTypeObject))
		{
			ValuePin->CPPTypeObject = Class;
			ValuePin->CPPTypeObjectPath = *ValuePin->CPPTypeObject->GetPathName();
		}
		else if (UScriptStruct* ScriptStruct = Cast<UScriptStruct>(InCPPTypeObject))
		{
			ValuePin->CPPTypeObject = ScriptStruct;
			ValuePin->CPPTypeObjectPath = *ValuePin->CPPTypeObject->GetPathName();
		}
		else if (UEnum* Enum = Cast<UEnum>(InCPPTypeObject))
		{
			ValuePin->CPPTypeObject = Enum;
			ValuePin->CPPTypeObjectPath = *ValuePin->CPPTypeObject->GetPathName();
		}
	}

	ValuePin->Direction = bIsGetter ? ERigVMPinDirection::Output : ERigVMPinDirection::Input;
	AddNodePin(Node, ValuePin);

	Graph->Nodes.Add(Node);

	if (ValuePin->IsStruct())
	{
		FString DefaultValue = InDefaultValue;
		CreateDefaultValueForStructIfRequired(ValuePin->GetScriptStruct(), DefaultValue);
		AddPinsForStruct(ValuePin->GetScriptStruct(), Node, ValuePin, ValuePin->Direction, DefaultValue, false);
	}
	else if (!InDefaultValue.IsEmpty() && InDefaultValue != TEXT("()"))
	{
		SetPinDefaultValue(ValuePin, InDefaultValue, true, false, false);
	}

	ForEveryPinRecursively(Node, [](URigVMPin* Pin) {
		Pin->bIsExpanded = false;
	});

	if (!bSuspendNotifications)
	{
		Graph->MarkPackageDirty();
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMAddVariableNodeAction Action;
	if (bSetupUndoRedo)
	{
		Action = FRigVMAddVariableNodeAction(Node);
		Action.Title = FString::Printf(TEXT("Add %s Variable"), *InVariableName.ToString());
		ActionStack->BeginAction(Action);
	}

	Notify(ERigVMGraphNotifType::NodeAdded, Node);
	Notify(ERigVMGraphNotifType::VariableAdded, Node);

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	if (bPrintPythonCommand)
	{
		TArray<FString> Commands = GetAddNodePythonCommands(Node);
		for (const FString& Command : Commands)
		{
			RigVMPythonUtils::Print(GetGraphOuterName(), 
								FString::Printf(TEXT("%s"), *Command));
		}
	}

	return Node;
}

URigVMVariableNode* URigVMController::AddVariableNodeFromObjectPath(const FName& InVariableName, const FString& InCPPType, const FString& InCPPTypeObjectPath, bool bIsGetter, const FString& InDefaultValue, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	UObject* CPPTypeObject = nullptr;
	if (!InCPPTypeObjectPath.IsEmpty())
	{
		CPPTypeObject = URigVMPin::FindObjectFromCPPTypeObjectPath<UObject>(InCPPTypeObjectPath);
		if (CPPTypeObject == nullptr)
		{
			ReportErrorf(TEXT("Cannot find cpp type object for path '%s'."), *InCPPTypeObjectPath);
			return nullptr;
		}
	}

	return AddVariableNode(InVariableName, InCPPType, CPPTypeObject, bIsGetter, InDefaultValue, InPosition, InNodeName, bSetupUndoRedo, bPrintPythonCommand);
}

void URigVMController::RefreshVariableNode(const FName& InNodeName, const FName& InVariableName, const FString& InCPPType, UObject* InCPPTypeObject, bool bSetupUndoRedo, bool bSetupOrphanPins)
{
	if (!IsValidGraph())
	{
		return;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Graph->FindNodeByName(InNodeName)))
	{
		if (URigVMPin* VariablePin = VariableNode->FindPin(URigVMVariableNode::VariableName))
		{
			if (VariablePin->Direction == ERigVMPinDirection::Visible)
			{
				if (bSetupUndoRedo)
				{
					VariablePin->Modify();
				}
				VariablePin->Direction = ERigVMPinDirection::Hidden;
				Notify(ERigVMGraphNotifType::PinDirectionChanged, VariablePin);
			}

			if (InVariableName.IsValid() && VariablePin->DefaultValue != InVariableName.ToString())
			{
				if (bSetupUndoRedo)
				{
					VariablePin->Modify();
				}
				VariablePin->DefaultValue = InVariableName.ToString();
				Notify(ERigVMGraphNotifType::PinDefaultValueChanged, VariablePin);
				Notify(ERigVMGraphNotifType::VariableRenamed, VariableNode);
			}

			if (!InCPPType.IsEmpty())
			{
				if (URigVMPin* ValuePin = VariableNode->FindPin(URigVMVariableNode::ValueName))
				{
					if (ValuePin->CPPType != InCPPType || ValuePin->GetCPPTypeObject() != InCPPTypeObject)
					{
						if (bSetupUndoRedo)
						{
							ValuePin->Modify();
						}

						// if this is an unsupported datatype...
						if (InCPPType == FName(NAME_None).ToString())
						{
							RemoveNode(VariableNode, bSetupUndoRedo);
							return;
						}

						FString CPPTypeObjectPath;
						if(InCPPTypeObject)
						{
							CPPTypeObjectPath = InCPPTypeObject->GetPathName();
						}
						ChangePinType(ValuePin, InCPPType, *CPPTypeObjectPath, bSetupUndoRedo, bSetupOrphanPins);
					}
				}
			}
		}
	}
}

void URigVMController::OnExternalVariableRemoved(const FName& InVarName, bool bSetupUndoRedo)
{
	if (!IsValidGraph())
	{
		return;
	}

	if (!InVarName.IsValid())
	{
		return;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	for (const FRigVMGraphVariableDescription& LocalVariable : Graph->GetLocalVariables())
	{
		if (InVarName == LocalVariable.Name)
		{
			return;
		}
	}
	
	const FString VarNameStr = InVarName.ToString();

	FRigVMControllerCompileBracketScope CompileScope(this);
	if (bSetupUndoRedo)
	{
		OpenUndoBracket(TEXT("Remove Variable Nodes"));
	}

	TArray<URigVMNode*> Nodes = Graph->GetNodes();
	for (URigVMNode* Node : Nodes)
	{
		if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
		{
			if (URigVMPin* VariablePin = VariableNode->FindPin(URigVMVariableNode::VariableName))
			{
				if (VariablePin->GetDefaultValue() == VarNameStr)
				{
					RemoveNode(Node, bSetupUndoRedo, true);
					continue;
				}
			}
		}
		else if(URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(Node))
		{
			FRigVMControllerGraphGuard GraphGuard(this, CollapseNode->GetContainedGraph(), bSetupUndoRedo);

			// call this function for the contained graph recursively 
			OnExternalVariableRemoved(InVarName, bSetupUndoRedo);

			// if we are a function we need to notify all references!
			if(URigVMFunctionLibrary* FunctionLibrary = Cast<URigVMFunctionLibrary>(Graph))
			{
				FunctionLibrary->ForEachReference(CollapseNode->GetFName(), [this, InVarName](URigVMFunctionReferenceNode* Reference)
				{
					if(Reference->VariableMap.Contains(InVarName))
					{
						Reference->Modify();
                        Reference->VariableMap.Remove(InVarName);

                        FRigVMControllerGraphGuard GraphGuard(this, Reference->GetGraph(), false);
                        Notify(ERigVMGraphNotifType::VariableRemappingChanged, Reference);
					}
				});
			}
		}
		else if(URigVMFunctionReferenceNode* FunctionReferenceNode = Cast<URigVMFunctionReferenceNode>(Node))
		{
			TMap<FName, FName> VariableMap = FunctionReferenceNode->GetVariableMap();
			for(const TPair<FName, FName>& VariablePair : VariableMap)
			{
				if(VariablePair.Value == InVarName)
				{
					SetRemappedVariable(FunctionReferenceNode, VariablePair.Key, NAME_None, bSetupUndoRedo);
				}
			}
		}
	}

	if (bSetupUndoRedo)
	{
		CloseUndoBracket();
	}
}

void URigVMController::OnExternalVariableRenamed(const FName& InOldVarName, const FName& InNewVarName, bool bSetupUndoRedo)
{
	if (!IsValidGraph())
	{
		return;
	}

	if (!InOldVarName.IsValid() || !InNewVarName.IsValid())
	{
		return;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	for (const FRigVMGraphVariableDescription& LocalVariable : Graph->GetLocalVariables(true))
	{
		if (InOldVarName == LocalVariable.Name)
		{
			return;
		}
	}

	const FString VarNameStr = InOldVarName.ToString();

	FRigVMControllerCompileBracketScope CompileScope(this);
	if (bSetupUndoRedo)
	{
		OpenUndoBracket(TEXT("Rename Variable Nodes"));
	}

	TArray<URigVMNode*> Nodes = Graph->GetNodes();
	for (URigVMNode* Node : Nodes)
	{
		if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
		{
			if (URigVMPin* VariablePin = VariableNode->FindPin(URigVMVariableNode::VariableName))
			{
				if (VariablePin->GetDefaultValue() == VarNameStr)
				{
					RefreshVariableNode(Node->GetFName(), InNewVarName, FString(), nullptr, bSetupUndoRedo, false);
					continue;
				}
			}
		}
		else if(URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(Node))
		{
			FRigVMControllerGraphGuard GraphGuard(this, CollapseNode->GetContainedGraph(), bSetupUndoRedo);
			OnExternalVariableRenamed(InOldVarName, InNewVarName, bSetupUndoRedo);

			// if we are a function we need to notify all references!
			if(URigVMFunctionLibrary* FunctionLibrary = Cast<URigVMFunctionLibrary>(Graph))
			{
				FunctionLibrary->ForEachReference(CollapseNode->GetFName(), [this, InOldVarName, InNewVarName](URigVMFunctionReferenceNode* Reference)
                {
					if(Reference->VariableMap.Contains(InOldVarName))
					{
						Reference->Modify();

						FName MappedVariable = Reference->VariableMap.FindChecked(InOldVarName);
						Reference->VariableMap.Remove(InOldVarName);
						Reference->VariableMap.FindOrAdd(InNewVarName) = MappedVariable; 

						FRigVMControllerGraphGuard GraphGuard(this, Reference->GetGraph(), false);
                        Notify(ERigVMGraphNotifType::VariableRemappingChanged, Reference);
                    }
                });
			}
		}
		else if(URigVMFunctionReferenceNode* FunctionReferenceNode = Cast<URigVMFunctionReferenceNode>(Node))
		{
			TMap<FName, FName> VariableMap = FunctionReferenceNode->GetVariableMap();
			for(const TPair<FName, FName>& VariablePair : VariableMap)
			{
				if(VariablePair.Value == InOldVarName)
				{
					SetRemappedVariable(FunctionReferenceNode, VariablePair.Key, InNewVarName, bSetupUndoRedo);
				}
			}
		}
	}

	if (bSetupUndoRedo)
	{
		CloseUndoBracket();
	}
}

void URigVMController::OnExternalVariableTypeChanged(const FName& InVarName, const FString& InCPPType, UObject* InCPPTypeObject, bool bSetupUndoRedo)
{
	if (!IsValidGraph())
	{
		return;
	}

	if (!InVarName.IsValid())
	{
		return;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	for (const FRigVMGraphVariableDescription& LocalVariable : Graph->GetLocalVariables(true))
	{
		if (InVarName == LocalVariable.Name)
		{
			return;
		}
	}

	const FString VarNameStr = InVarName.ToString();

	FRigVMControllerCompileBracketScope CompileScope(this);
	if (bSetupUndoRedo)
	{
		OpenUndoBracket(TEXT("Change Variable Nodes Type"));
	}

	TArray<URigVMNode*> Nodes = Graph->GetNodes();
	for (URigVMNode* Node : Nodes)
	{
		if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
		{
			if (URigVMPin* VariablePin = VariableNode->FindPin(URigVMVariableNode::VariableName))
			{
				if (VariablePin->GetDefaultValue() == VarNameStr)
				{
					RefreshVariableNode(Node->GetFName(), InVarName, InCPPType, InCPPTypeObject, bSetupUndoRedo, false);
					continue;
				}
			}
		}
		else if(URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(Node))
		{
			FRigVMControllerGraphGuard GraphGuard(this, CollapseNode->GetContainedGraph(), bSetupUndoRedo);
			OnExternalVariableTypeChanged(InVarName, InCPPType, InCPPTypeObject, bSetupUndoRedo);

			// if we are a function we need to notify all references!
			if(URigVMFunctionLibrary* FunctionLibrary = Cast<URigVMFunctionLibrary>(Graph))
			{
				FunctionLibrary->ForEachReference(CollapseNode->GetFName(), [this, InVarName](URigVMFunctionReferenceNode* Reference)
                {
                    if(Reference->VariableMap.Contains(InVarName))
                    {
                        Reference->Modify();
                        Reference->VariableMap.Remove(InVarName); 

                        FRigVMControllerGraphGuard GraphGuard(this, Reference->GetGraph(), false);
                        Notify(ERigVMGraphNotifType::VariableRemappingChanged, Reference);
                    }
                });
			}
		}
		else if(URigVMFunctionReferenceNode* FunctionReferenceNode = Cast<URigVMFunctionReferenceNode>(Node))
		{
			TMap<FName, FName> VariableMap = FunctionReferenceNode->GetVariableMap();
			for(const TPair<FName, FName>& VariablePair : VariableMap)
			{
				if(VariablePair.Value == InVarName)
				{
					SetRemappedVariable(FunctionReferenceNode, VariablePair.Key, NAME_None, bSetupUndoRedo);
				}
			}
		}

		TArray<URigVMPin*> AllPins = Node->GetAllPinsRecursively();
		for (URigVMPin* Pin : AllPins)
		{
			if (Pin->GetBoundVariableName() == InVarName.ToString())
			{
				FString BoundVariablePath = Pin->GetBoundVariablePath();
				UnbindPinFromVariable(Pin, bSetupUndoRedo);
				// try to bind it again - maybe it can be bound (due to cast rules etc)
				BindPinToVariable(Pin, BoundVariablePath, bSetupUndoRedo);
			}
		}
	}

	if (bSetupUndoRedo)
	{
		CloseUndoBracket();
	}
}

void URigVMController::OnExternalVariableTypeChangedFromObjectPath(const FName& InVarName, const FString& InCPPType, const FString& InCPPTypeObjectPath, bool bSetupUndoRedo)
{
	if (!IsValidGraph())
	{
		return;
	}

	UObject* CPPTypeObject = nullptr;
	if (!InCPPTypeObjectPath.IsEmpty())
	{
		CPPTypeObject = URigVMPin::FindObjectFromCPPTypeObjectPath<UObject>(InCPPTypeObjectPath);
		if (CPPTypeObject == nullptr)
		{
			ReportErrorf(TEXT("Cannot find cpp type object for path '%s'."), *InCPPTypeObjectPath);
			return;
		}
	}

	OnExternalVariableTypeChanged(InVarName, InCPPType, CPPTypeObject, bSetupUndoRedo);
}

URigVMVariableNode* URigVMController::ReplaceParameterNodeWithVariable(const FName& InNodeName, const FName& InVariableName, const FString& InCPPType, UObject* InCPPTypeObject, bool bSetupUndoRedo)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (URigVMParameterNode* ParameterNode = Cast<URigVMParameterNode>(Graph->FindNodeByName(InNodeName)))
	{
		URigVMPin* ParameterValuePin = ParameterNode->FindPin(URigVMParameterNode::ValueName);
		check(ParameterValuePin);

		FRigVMGraphParameterDescription Description = ParameterNode->GetParameterDescription();
		
		URigVMVariableNode* VariableNode = AddVariableNode(
			InVariableName,
			InCPPType,
			InCPPTypeObject,
			ParameterValuePin->GetDirection() == ERigVMPinDirection::Output,
			ParameterValuePin->GetDefaultValue(),
			ParameterNode->GetPosition(),
			FString(),
			bSetupUndoRedo);

		if (VariableNode)
		{
			URigVMPin* VariableValuePin = VariableNode->FindPin(URigVMVariableNode::ValueName);

			RewireLinks(
				ParameterValuePin,
				VariableValuePin,
				ParameterValuePin->GetDirection() == ERigVMPinDirection::Input,
				bSetupUndoRedo
			);

			RemoveNode(ParameterNode, bSetupUndoRedo, true);

			return VariableNode;
		}
	}

	return nullptr;
}

URigVMParameterNode* URigVMController::AddParameterNode(const FName& InParameterName, const FString& InCPPType, UObject* InCPPTypeObject, bool bIsInput, const FString& InDefaultValue, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot add parameter nodes to function library graphs."));
		return nullptr;
	}

	if (InCPPTypeObject == nullptr)
	{
		InCPPTypeObject = URigVMCompiler::GetScriptStructForCPPType(InCPPType);
	}
	if (InCPPTypeObject == nullptr)
	{
		InCPPTypeObject = URigVMPin::FindObjectFromCPPTypeObjectPath<UObject>(InCPPType);
	}

	TArray<FRigVMGraphParameterDescription> ExistingParameters = Graph->GetParameterDescriptions();
	for (const FRigVMGraphParameterDescription& ExistingParameter : ExistingParameters)
	{
		if (ExistingParameter.Name == InParameterName)
		{
			if (ExistingParameter.CPPType != InCPPType ||
				ExistingParameter.CPPTypeObject != InCPPTypeObject ||
				ExistingParameter.bIsInput != bIsInput)
			{
				ReportErrorf(TEXT("Cannot add parameter '%s' - parameter already exists."), *InParameterName.ToString());
				return nullptr;
			}
		}
	}

	FString Name = GetValidNodeName(InNodeName.IsEmpty() ? FString(TEXT("ParameterNode")) : InNodeName);
	URigVMParameterNode* Node = NewObject<URigVMParameterNode>(Graph, *Name);
	Node->Position = InPosition;

	if (!bIsInput)
	{
		URigVMPin* ExecutePin = NewObject<URigVMPin>(Node, FRigVMStruct::ExecuteContextName);
		ExecutePin->CPPType = FString::Printf(TEXT("F%s"), *ExecuteContextStruct->GetName());
		ExecutePin->CPPTypeObject = ExecuteContextStruct;
		ExecutePin->CPPTypeObjectPath = *ExecutePin->CPPTypeObject->GetPathName();
		ExecutePin->Direction = ERigVMPinDirection::IO;
		AddNodePin(Node, ExecutePin);
	}

	URigVMPin* ParameterPin = NewObject<URigVMPin>(Node, *URigVMParameterNode::ParameterName);
	ParameterPin->CPPType = TEXT("FName");
	ParameterPin->Direction = ERigVMPinDirection::Visible;
	ParameterPin->DefaultValue = InParameterName.ToString();
	ParameterPin->CustomWidgetName = TEXT("ParameterName");

	AddNodePin(Node, ParameterPin);

	URigVMPin* DefaultValuePin = nullptr;
	if (bIsInput)
	{
		DefaultValuePin = NewObject<URigVMPin>(Node, *URigVMParameterNode::DefaultName);
	}
	URigVMPin* ValuePin = NewObject<URigVMPin>(Node, *URigVMParameterNode::ValueName);

	if (DefaultValuePin)
	{
		DefaultValuePin->CPPType = PostProcessCPPType(InCPPType, InCPPTypeObject);
		DefaultValuePin->CPPTypeObject = InCPPTypeObject;
		if(DefaultValuePin->CPPTypeObject)
		{
			DefaultValuePin->CPPTypeObjectPath = *DefaultValuePin->CPPTypeObject->GetPathName();
		}
	}
	
	ValuePin->CPPType = PostProcessCPPType(InCPPType, InCPPTypeObject);
	ValuePin->CPPTypeObject = InCPPTypeObject;
	if(ValuePin->CPPTypeObject)
	{
		ValuePin->CPPTypeObjectPath = *ValuePin->CPPTypeObject->GetPathName();
	}

	if (DefaultValuePin)
	{
		DefaultValuePin->Direction = ERigVMPinDirection::Visible;
	}
	ValuePin->Direction = bIsInput ? ERigVMPinDirection::Output : ERigVMPinDirection::Input;

	if (bIsInput)
	{
		if (ValuePin->CPPType == TEXT("FName"))
		{
			ValuePin->bIsConstant = true;
		}
	}

	if (DefaultValuePin)
	{
		AddNodePin(Node, DefaultValuePin);
	}
	AddNodePin(Node, ValuePin);

	Graph->Nodes.Add(Node);

	if (ValuePin->IsStruct())
	{
		FString DefaultValue = InDefaultValue;
		CreateDefaultValueForStructIfRequired(ValuePin->GetScriptStruct(), DefaultValue);
		if (DefaultValuePin)
		{
			AddPinsForStruct(DefaultValuePin->GetScriptStruct(), Node, DefaultValuePin, DefaultValuePin->Direction, DefaultValue, false);
		}
		AddPinsForStruct(ValuePin->GetScriptStruct(), Node, ValuePin, ValuePin->Direction, DefaultValue, false);
	}
	else if (!InDefaultValue.IsEmpty() && InDefaultValue != TEXT("()"))
	{
		if (DefaultValuePin)
		{
			SetPinDefaultValue(DefaultValuePin, InDefaultValue, true, false, false);
		}
		SetPinDefaultValue(ValuePin, InDefaultValue, true, false, false);
	}

	ForEveryPinRecursively(Node, [](URigVMPin* Pin) {
		Pin->bIsExpanded = false;
	});

	if (!bSuspendNotifications)
	{
		Graph->MarkPackageDirty();
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMAddParameterNodeAction Action;
	if (bSetupUndoRedo)
	{
		Action = FRigVMAddParameterNodeAction(Node);
		Action.Title = FString::Printf(TEXT("Add %s Parameter"), *InParameterName.ToString());
		ActionStack->BeginAction(Action);
	}

	Notify(ERigVMGraphNotifType::NodeAdded, Node);
	Notify(ERigVMGraphNotifType::ParameterAdded, Node);

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	if (bPrintPythonCommand)
	{
		TArray<FString> Commands = GetAddNodePythonCommands(Node);
		for (const FString& Command : Commands)
		{
			RigVMPythonUtils::Print(GetGraphOuterName(), 
								FString::Printf(TEXT("%s"), *Command));
		}
	}

	return Node;
}

URigVMParameterNode* URigVMController::AddParameterNodeFromObjectPath(const FName& InParameterName, const FString& InCPPType, const FString& InCPPTypeObjectPath, bool bIsInput, const FString& InDefaultValue, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	UObject* CPPTypeObject = nullptr;
	if (!InCPPTypeObjectPath.IsEmpty())
	{
		CPPTypeObject = URigVMPin::FindObjectFromCPPTypeObjectPath<UObject>(InCPPTypeObjectPath);
		if (CPPTypeObject == nullptr)
		{
			ReportErrorf(TEXT("Cannot find cpp type object for path '%s'."), *InCPPTypeObjectPath);
			return nullptr;
		}
	}

	return AddParameterNode(InParameterName, InCPPType, CPPTypeObject, bIsInput, InDefaultValue, InPosition, InNodeName, bSetupUndoRedo, bPrintPythonCommand);
}

URigVMCommentNode* URigVMController::AddCommentNode(const FString& InCommentText, const FVector2D& InPosition, const FVector2D& InSize, const FLinearColor& InColor, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot add comment nodes to function library graphs."));
		return nullptr;
	}

	FString Name = GetValidNodeName(InNodeName.IsEmpty() ? FString(TEXT("CommentNode")) : InNodeName);
	URigVMCommentNode* Node = NewObject<URigVMCommentNode>(Graph, *Name);
	Node->Position = InPosition;
	Node->Size = InSize;
	Node->NodeColor = InColor;
	Node->CommentText = InCommentText;

	Graph->Nodes.Add(Node);
	if (!bSuspendNotifications)
	{
		Graph->MarkPackageDirty();
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMAddCommentNodeAction Action;
	if (bSetupUndoRedo)
	{
		Action = FRigVMAddCommentNodeAction(Node);
		Action.Title = FString::Printf(TEXT("Add Comment"));
		ActionStack->BeginAction(Action);
	}

	Notify(ERigVMGraphNotifType::NodeAdded, Node);

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	if (bPrintPythonCommand)
	{
		TArray<FString> Commands = GetAddNodePythonCommands(Node);
		for (const FString& Command : Commands)
		{
			RigVMPythonUtils::Print(GetGraphOuterName(), 
								FString::Printf(TEXT("%s"), *Command));
		}
	}

	return Node;
}

URigVMRerouteNode* URigVMController::AddRerouteNodeOnLink(URigVMLink* InLink, bool bShowAsFullNode, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if(!IsValidLinkForGraph(InLink))
	{
		return nullptr;
	}

	if (GetGraph()->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot add reroutes to function library graphs."));
		return nullptr;
	}

	URigVMPin* SourcePin = InLink->GetSourcePin();
	const URigVMPin* TargetPin = InLink->GetTargetPin();

	TGuardValue<bool> GuardCompactness(bIgnoreRerouteCompactnessChanges, true);

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMBaseAction Action;
	if (bSetupUndoRedo)
	{
		Action.Title = FString::Printf(TEXT("Add Reroute"));
		ActionStack->BeginAction(Action);
	}

	URigVMRerouteNode* Node = AddRerouteNodeOnPin(TargetPin->GetPinPath(), true, bShowAsFullNode, InPosition, InNodeName, bSetupUndoRedo);
	if (Node == nullptr)
	{
		if (bSetupUndoRedo)
		{
			ActionStack->CancelAction(Action);
		}
		return nullptr;
	}

	URigVMPin* ValuePin = Node->Pins[0];
	AddLink(SourcePin, ValuePin, bSetupUndoRedo);

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	if (bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
		const FString NodeName = GetSanitizedNodeName(Node->GetName());

		RigVMPythonUtils::Print(GetGraphOuterName(), 
							FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_reroute_node_on_link_path('%s', %s, %s, '%s')"),
											*GraphName,
											*InLink->GetPinPathRepresentation(),
											(bShowAsFullNode) ? TEXT("True") : TEXT("False"),
											*RigVMPythonUtils::Vector2DToPythonString(Node->GetPosition()),
											*NodeName));
	}

	return Node;
}

URigVMRerouteNode* URigVMController::AddRerouteNodeOnLinkPath(const FString& InLinkPinPathRepresentation, bool bShowAsFullNode, const FVector2D& InPosition, const FString&
                                                              InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMLink* Link = Graph->FindLink(InLinkPinPathRepresentation);
	return AddRerouteNodeOnLink(Link, bShowAsFullNode, InPosition, InNodeName, bSetupUndoRedo, bPrintPythonCommand);
}

URigVMRerouteNode* URigVMController::AddRerouteNodeOnPin(const FString& InPinPath, bool bAsInput, bool bShowAsFullNode, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot add reroutes to function library graphs."));
		return nullptr;
	}

	URigVMPin* Pin = Graph->FindPin(InPinPath);
	if(Pin == nullptr)
	{
		return nullptr;
	}

	TGuardValue<bool> GuardCompactness(bIgnoreRerouteCompactnessChanges, true);

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMBaseAction Action;
	if (bSetupUndoRedo)
	{
		Action.Title = FString::Printf(TEXT("Add Reroute"));
		ActionStack->BeginAction(Action);
	}

	//in case an injected node is present, use its pins for any new links
	URigVMPin *PinForLink = Pin->GetPinForLink(); 
	if (bAsInput)
	{
		BreakAllLinks(PinForLink, bAsInput, bSetupUndoRedo);
	}

	FString Name = GetValidNodeName(InNodeName.IsEmpty() ? FString(TEXT("RerouteNode")) : InNodeName);
	URigVMRerouteNode* Node = NewObject<URigVMRerouteNode>(Graph, *Name);
	Node->Position = InPosition;
	Node->bShowAsFullNode = bShowAsFullNode;

	URigVMPin* ValuePin = NewObject<URigVMPin>(Node, *URigVMRerouteNode::ValueName);
	ConfigurePinFromPin(ValuePin, Pin);
	ValuePin->Direction = ERigVMPinDirection::IO;
	AddNodePin(Node, ValuePin);

	if (ValuePin->IsStruct())
	{
		AddPinsForStruct(ValuePin->GetScriptStruct(), Node, ValuePin, ValuePin->Direction, FString(), false);
	}

	FString DefaultValue = Pin->GetDefaultValue();
	if (!DefaultValue.IsEmpty())
	{
		SetPinDefaultValue(ValuePin, Pin->GetDefaultValue(), true, false, false);
	}

	ForEveryPinRecursively(ValuePin, [](URigVMPin* Pin) {
		Pin->bIsExpanded = true;
	});

	Graph->Nodes.Add(Node);
	if (!bSuspendNotifications)
	{
		Graph->MarkPackageDirty();
	}

	if (bSetupUndoRedo)
	{
		ActionStack->AddAction(FRigVMAddRerouteNodeAction(Node));
	}

	Notify(ERigVMGraphNotifType::NodeAdded, Node);

	if (bAsInput)
	{
		AddLink(ValuePin, PinForLink, bSetupUndoRedo);
	}
	else
	{
		AddLink(PinForLink, ValuePin, bSetupUndoRedo);
	}

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	if (bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
		const FString NodeName = GetSanitizedNodeName(Node->GetName());
		// AddRerouteNodeOnPin(const FString& InPinPath, bool bAsInput, bool bShowAsFullNode, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
		
		RigVMPythonUtils::Print(GetGraphOuterName(), 
							FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_reroute_node_on_pin('%s', %s, %s, %s '%s')"),
											*GraphName,
											*GetSanitizedPinPath(InPinPath),
											(bAsInput) ? TEXT("True") : TEXT("False"),
											(bShowAsFullNode) ? TEXT("True") : TEXT("False"),
											*RigVMPythonUtils::Vector2DToPythonString(Node->GetPosition()),
											*NodeName));
	}

	return Node;
}

URigVMInjectionInfo* URigVMController::AddInjectedNode(const FString& InPinPath, bool bAsInput, UScriptStruct* InScriptStruct, const FName& InMethodName, const FName& InInputPinName, const FName& InOutputPinName, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot add injected nodes to function library graphs."));
		return nullptr;
	}

	URigVMPin* Pin = Graph->FindPin(InPinPath);
	if (Pin == nullptr)
	{
		return nullptr;
	}

	if (Pin->IsArray())
	{
		return nullptr;
	}

	if (bAsInput && !(Pin->GetDirection() == ERigVMPinDirection::Input || Pin->GetDirection() == ERigVMPinDirection::IO))
	{
		ReportError(TEXT("Pin is not an input / cannot add injected input node."));
		return nullptr;
	}
	if (!bAsInput && !(Pin->GetDirection() == ERigVMPinDirection::Output))
	{
		ReportError(TEXT("Pin is not an output / cannot add injected output node."));
		return nullptr;
	}

	if (InScriptStruct == nullptr)
	{
		ReportError(TEXT("InScriptStruct is null."));
		return nullptr;
	}

	if (InMethodName == NAME_None)
	{
		ReportError(TEXT("InMethodName is None."));
		return nullptr;
	}

	// find the input and output pins to use
	FProperty* InputProperty = InScriptStruct->FindPropertyByName(InInputPinName);
	if (InputProperty == nullptr)
	{
		ReportErrorf(TEXT("Cannot find property '%s' on struct type '%s'."), *InInputPinName.ToString(), *InScriptStruct->GetName());
		return nullptr;
	}
	if (!InputProperty->HasMetaData(FRigVMStruct::InputMetaName))
	{
		ReportErrorf(TEXT("Property '%s' on struct type '%s' is not marked as an input."), *InInputPinName.ToString(), *InScriptStruct->GetName());
		return nullptr;
	}
	FProperty* OutputProperty = InScriptStruct->FindPropertyByName(InOutputPinName);
	if (OutputProperty == nullptr)
	{
		ReportErrorf(TEXT("Cannot find property '%s' on struct type '%s'."), *InOutputPinName.ToString(), *InScriptStruct->GetName());
		return nullptr;
	}
	if (!OutputProperty->HasMetaData(FRigVMStruct::OutputMetaName))
	{
		ReportErrorf(TEXT("Property '%s' on struct type '%s' is not marked as an output."), *InOutputPinName.ToString(), *InScriptStruct->GetName());
		return nullptr;
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMBaseAction Action;
	if (bSetupUndoRedo)
	{
		Action.Title = FString::Printf(TEXT("Add Injected Node"));
		ActionStack->BeginAction(Action);
	}

	URigVMUnitNode* UnitNode = nullptr;
	{
		TGuardValue<bool> GuardNotifications(bSuspendNotifications, true);
		UnitNode = AddUnitNode(InScriptStruct, InMethodName, FVector2D::ZeroVector, InNodeName, false);
	}
	if (UnitNode == nullptr)
	{
		if (bSetupUndoRedo)
		{
			ActionStack->CancelAction(Action);
		}
		return nullptr;
	}
	else if (UnitNode->IsMutable())
	{
		ReportErrorf(TEXT("Injected node %s is mutable."), *InScriptStruct->GetName());
		RemoveNode(UnitNode, false);
		if (bSetupUndoRedo)
		{
			ActionStack->CancelAction(Action);
		}
		return nullptr;
	}

	URigVMPin* InputPin = UnitNode->FindPin(InInputPinName.ToString());
	check(InputPin);
	URigVMPin* OutputPin = UnitNode->FindPin(InOutputPinName.ToString());
	check(OutputPin);

	if (InputPin->GetCPPType() != OutputPin->GetCPPType() ||
		InputPin->IsArray() != OutputPin->IsArray())
	{
		ReportErrorf(TEXT("Injected node %s is using incompatible input and output pins."), *InScriptStruct->GetName());
		RemoveNode(UnitNode, false);
		if (bSetupUndoRedo)
		{
			ActionStack->CancelAction(Action);
		}
		return nullptr;
	}

	if (InputPin->GetCPPType() != Pin->GetCPPType() ||
		InputPin->IsArray() != Pin->IsArray())
	{
		ReportErrorf(TEXT("Injected node %s is using incompatible pin."), *InScriptStruct->GetName());
		RemoveNode(UnitNode, false);
		if (bSetupUndoRedo)
		{
			ActionStack->CancelAction(Action);
		}
		return nullptr;
	}

	URigVMInjectionInfo* InjectionInfo = NewObject<URigVMInjectionInfo>(Pin);

	// re-parent the unit node to be under the injection info
	RenameObject(UnitNode, nullptr, InjectionInfo);

	InjectionInfo->Node = UnitNode;
	InjectionInfo->bInjectedAsInput = bAsInput;
	InjectionInfo->InputPin = InputPin;
	InjectionInfo->OutputPin = OutputPin;

	if (bSetupUndoRedo)
	{
		ActionStack->AddAction(FRigVMAddInjectedNodeAction(InjectionInfo));
	}

	URigVMPin* PreviousInputPin = Pin;
	URigVMPin* PreviousOutputPin = Pin;
	if (Pin->InjectionInfos.Num() > 0)
	{
		PreviousInputPin = Pin->InjectionInfos.Last()->InputPin;
		PreviousOutputPin = Pin->InjectionInfos.Last()->OutputPin;
	}
	Pin->InjectionInfos.Add(InjectionInfo);

	Notify(ERigVMGraphNotifType::NodeAdded, UnitNode);

	// now update all of the links
	if (bAsInput)
	{
		FString PinDefaultValue = PreviousInputPin->GetDefaultValue();
		if (!PinDefaultValue.IsEmpty())
		{
			SetPinDefaultValue(InjectionInfo->InputPin, PinDefaultValue, true, false, false);
		}
		TArray<URigVMLink*> Links = PreviousInputPin->GetSourceLinks(true /* recursive */);
		BreakAllLinks(PreviousInputPin, true, false);
		AddLink(InjectionInfo->OutputPin, PreviousInputPin, false);
		if (Links.Num() > 0)
		{
			RewireLinks(PreviousInputPin, InjectionInfo->InputPin, true, false, Links);
		}
	}
	else
	{
		TArray<URigVMLink*> Links = PreviousOutputPin->GetTargetLinks(true /* recursive */);
		BreakAllLinks(PreviousOutputPin, false, false);
		AddLink(PreviousOutputPin, InjectionInfo->InputPin, false);
		if (Links.Num() > 0)
		{
			RewireLinks(PreviousOutputPin, InjectionInfo->OutputPin, false, false, Links);
		}
	}

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	if (bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
		
		RigVMPythonUtils::Print(GetGraphOuterName(), 
							FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_injected_node_from_struct_path('%s', %s, '%s', '%s', '%s', '%s', '%s')"),
											*GraphName,
											*GetSanitizedPinPath(InPinPath),
											(bAsInput) ? TEXT("True") : TEXT("False"),
											*InScriptStruct->GetPathName(),
											*InMethodName.ToString(),
											*GetSanitizedPinName(InInputPinName.ToString()),
											*GetSanitizedPinName(InOutputPinName.ToString()),
											*GetSanitizedNodeName(InNodeName)));
	}

	return InjectionInfo;

}

URigVMInjectionInfo* URigVMController::AddInjectedNodeFromStructPath(const FString& InPinPath, bool bAsInput, const FString& InScriptStructPath, const FName& InMethodName, const FName& InInputPinName, const FName& InOutputPinName, const FString& InNodeName, bool bSetupUndoRedo)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	UScriptStruct* ScriptStruct = URigVMPin::FindObjectFromCPPTypeObjectPath<UScriptStruct>(InScriptStructPath);
	if (ScriptStruct == nullptr)
	{
		ReportErrorf(TEXT("Cannot find struct for path '%s'."), *InScriptStructPath);
		return nullptr;
	}

	return AddInjectedNode(InPinPath, bAsInput, ScriptStruct, InMethodName, InInputPinName, InOutputPinName, InNodeName, bSetupUndoRedo);
}

URigVMNode* URigVMController::EjectNodeFromPin(const FString& InPinPath, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot eject nodes in function library graphs."));
		return nullptr;
	}

	URigVMPin* Pin = Graph->FindPin(InPinPath);
	if (Pin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InPinPath);
		return nullptr;
	}

	if (!Pin->HasInjectedNodes())
	{
		ReportErrorf(TEXT("Pin '%s' has no injected nodes."), *InPinPath);
		return nullptr;
	}

	URigVMInjectionInfo* Injection = Pin->InjectionInfos.Last();

	FName NodeName = Injection->Node->GetFName();
	
	TMap<FName, FString> DefaultValues;
	for (URigVMPin* PinOnNode : Injection->Node->GetPins())
	{
		if (PinOnNode->GetDirection() == ERigVMPinDirection::Input ||
			PinOnNode->GetDirection() == ERigVMPinDirection::Visible ||
			PinOnNode->GetDirection() == ERigVMPinDirection::IO)
		{
			FString DefaultValue = PinOnNode->GetDefaultValue();
			PostProcessDefaultValue(PinOnNode, DefaultValue);
			DefaultValues.Add(PinOnNode->GetFName(), DefaultValue);
		}
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMBaseAction Action;
	if (bSetupUndoRedo)
	{
		Action.Title = FString::Printf(TEXT("Eject Node"));
		ActionStack->BeginAction(Action);
	}

	FVector2D Position = Pin->GetNode()->GetPosition() + FVector2D(0.f, 12.f) * float(Pin->GetPinIndex());
	if (Pin->GetDirection() == ERigVMPinDirection::Output)
	{
		Position += FVector2D(250.f, 0.f);
	}
	else
	{
		Position -= FVector2D(250.f, 0.f);
	}

	URigVMNode* EjectedNode = nullptr;
	if (URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(Injection->Node))
	{
		EjectedNode = AddUnitNode(UnitNode->GetScriptStruct(), UnitNode->GetMethodName(), Position, FString(), bSetupUndoRedo);
	}
	else if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Injection->Node))
	{
		EjectedNode = AddVariableNode(VariableNode->GetVariableName(), VariableNode->GetCPPType(), VariableNode->GetCPPTypeObject(), true, VariableNode->GetDefaultValue(), Position, FString(), bSetupUndoRedo);
	}

	if (!EjectedNode)
	{
		ReportErrorf(TEXT("Could not eject node."));
		if (bSetupUndoRedo)
		{
			ActionStack->CancelAction(Action);
		}
		return nullptr;
	}

	for (const TPair<FName, FString>& Pair : DefaultValues)
	{
		if (Pair.Value.IsEmpty())
		{
			continue;
		}
		if (URigVMPin* PinOnNode = EjectedNode->FindPin(Pair.Key.ToString()))
		{
			SetPinDefaultValue(PinOnNode, Pair.Value, true, bSetupUndoRedo, false);
		}
	}

	TArray<URigVMLink*> PreviousLinks;
	if (Injection->InputPin)
	{
		Injection->InputPin->GetSourceLinks(true);
	}
	if (Injection->OutputPin)
	{
		PreviousLinks.Append(Injection->OutputPin->GetTargetLinks(true));
	}
	for (URigVMLink* PreviousLink : PreviousLinks)
	{
		PreviousLink->PrepareForCopy();
		PreviousLink->SourcePin = PreviousLink->TargetPin = nullptr;
	}

	RemoveNode(Injection->Node, bSetupUndoRedo);

	FString OldNodeNamePrefix = NodeName.ToString() + TEXT(".");
	FString NewNodeNamePrefix = EjectedNode->GetName() + TEXT(".");

	for (URigVMLink* PreviousLink : PreviousLinks)
	{
		FString SourcePinPath = PreviousLink->SourcePinPath;
		if (SourcePinPath.StartsWith(OldNodeNamePrefix))
		{
			SourcePinPath = NewNodeNamePrefix + SourcePinPath.RightChop(OldNodeNamePrefix.Len());
		}
		FString TargetPinPath = PreviousLink->TargetPinPath;
		if (TargetPinPath.StartsWith(OldNodeNamePrefix))
		{
			TargetPinPath = NewNodeNamePrefix + TargetPinPath.RightChop(OldNodeNamePrefix.Len());
		}

		URigVMPin* SourcePin = Graph->FindPin(SourcePinPath);
		URigVMPin* TargetPin = Graph->FindPin(TargetPinPath);
		AddLink(SourcePin, TargetPin, bSetupUndoRedo);
	}

	TArray<FName> NodeNamesToSelect;
	NodeNamesToSelect.Add(EjectedNode->GetFName());
	SetNodeSelection(NodeNamesToSelect, bSetupUndoRedo);

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	if (bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());

		RigVMPythonUtils::Print(GetGraphOuterName(), 
							FString::Printf(TEXT("blueprint.get_controller_by_name('%s').eject_node_from_pin('%s')"),
											*GraphName,
											*GetSanitizedPinPath(InPinPath)));
	}

	return EjectedNode;
}


bool URigVMController::Undo()
{
	if (!IsValidGraph())
	{
		return false;
	}

	TGuardValue<bool> GuardCompactness(bIgnoreRerouteCompactnessChanges, true);
	return ActionStack->Undo(this);
}

bool URigVMController::Redo()
{
	if (!IsValidGraph())
	{
		return false;
	}

	TGuardValue<bool> GuardCompactness(bIgnoreRerouteCompactnessChanges, true);
	return ActionStack->Redo(this);
}

bool URigVMController::OpenUndoBracket(const FString& InTitle)
{
	if (!IsValidGraph())
	{
		return false;
	}
	return ActionStack->OpenUndoBracket(InTitle);
}

bool URigVMController::CloseUndoBracket()
{
	if (!IsValidGraph())
	{
		return false;
	}
	return ActionStack->CloseUndoBracket();
}

bool URigVMController::CancelUndoBracket()
{
	if (!IsValidGraph())
	{
		return false;
	}
	return ActionStack->CancelUndoBracket();
}

FString URigVMController::ExportNodesToText(const TArray<FName>& InNodeNames)
{
	if (!IsValidGraph())
	{
		return FString();
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	UnMarkAllObjects(EObjectMark(OBJECTMARK_TagExp | OBJECTMARK_TagImp));

	FStringOutputDevice Archive;
	const FExportObjectInnerContext Context;

	TArray<FName> AllNodeNames = InNodeNames;
	for (const FName& NodeName : InNodeNames)
	{
		if (URigVMNode* Node = Graph->FindNodeByName(NodeName))
		{
			for (URigVMPin* Pin : Node->GetPins())
			{
				for (URigVMInjectionInfo* Injection : Pin->GetInjectedNodes())
				{
					AllNodeNames.AddUnique(Injection->Node->GetFName());
				}
			}
		}
	}

	// Export each of the selected nodes
	for (const FName& NodeName : InNodeNames)
	{
		if (URigVMNode* Node = Graph->FindNodeByName(NodeName))
		{
			UExporter::ExportToOutputDevice(&Context, Node, NULL, Archive, TEXT("copy"), 0, PPF_ExportsNotFullyQualified | PPF_Copy | PPF_Delimited, false, Node->GetOuter());
		}
	}

	for (URigVMLink* Link : Graph->Links)
	{
		URigVMPin* SourcePin = Link->GetSourcePin();
		URigVMPin* TargetPin = Link->GetTargetPin();
		if (SourcePin && TargetPin)
		{
			if (!AllNodeNames.Contains(SourcePin->GetNode()->GetFName()))
			{
				continue;
			}
			if (!AllNodeNames.Contains(TargetPin->GetNode()->GetFName()))
			{
				continue;
			}
			Link->PrepareForCopy();
			UExporter::ExportToOutputDevice(&Context, Link, NULL, Archive, TEXT("copy"), 0, PPF_ExportsNotFullyQualified | PPF_Copy | PPF_Delimited, false, Link->GetOuter());
		}
	}

	return MoveTemp(Archive);
}

FString URigVMController::ExportSelectedNodesToText()
{
	if (!IsValidGraph())
	{
		return FString();
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	return ExportNodesToText(Graph->GetSelectNodes());
}

struct FRigVMControllerObjectFactory : public FCustomizableTextObjectFactory
{
public:
	URigVMController* Controller;
	TArray<URigVMNode*> CreatedNodes;
	TMap<FName, FName> NodeNameMap;
	TArray<URigVMLink*> CreatedLinks;
public:
	FRigVMControllerObjectFactory(URigVMController* InController)
		: FCustomizableTextObjectFactory(GWarn)
		, Controller(InController)
	{
	}

protected:
	virtual bool CanCreateClass(UClass* ObjectClass, bool& bOmitSubObjs) const override
	{
		if (URigVMNode* DefaultNode = Cast<URigVMNode>(ObjectClass->GetDefaultObject()))
		{
			// bOmitSubObjs = true;
			return true;
		}
		if (URigVMLink* DefaultLink = Cast<URigVMLink>(ObjectClass->GetDefaultObject()))
		{
			return true;
		}

		return false;
	}

	virtual void UpdateObjectName(UClass* ObjectClass, FName& InOutObjName) override
	{
		if (URigVMNode* DefaultNode = Cast<URigVMNode>(ObjectClass->GetDefaultObject()))
		{
			FName ValidName = *Controller->GetValidNodeName(InOutObjName.ToString());
			NodeNameMap.Add(InOutObjName, ValidName);
			InOutObjName = ValidName;
		}
	}

	virtual void ProcessConstructedObject(UObject* CreatedObject) override
	{
		if (URigVMNode* CreatedNode = Cast<URigVMNode>(CreatedObject))
		{
			CreatedNodes.AddUnique(CreatedNode);

			for (URigVMPin* Pin : CreatedNode->GetPins())
			{
				for (URigVMInjectionInfo* Injection : Pin->GetInjectedNodes())
				{
					ProcessConstructedObject(Injection->Node);

					FName NewName = Injection->Node->GetFName();
					UpdateObjectName(URigVMNode::StaticClass(), NewName);
					Controller->RenameObject(Injection->Node, *NewName.ToString(), nullptr);
					Injection->InputPin = Injection->InputPin ? Injection->Node->FindPin(Injection->InputPin->GetName()) : nullptr;
					Injection->OutputPin = Injection->OutputPin ? Injection->Node->FindPin(Injection->OutputPin->GetName()) : nullptr;
				}
			}
		}
		else if (URigVMLink* CreatedLink = Cast<URigVMLink>(CreatedObject))
		{
			CreatedLinks.Add(CreatedLink);
		}
	}
};

bool URigVMController::CanImportNodesFromText(const FString& InText)
{
	if (!IsValidGraph())
	{
		return false;
	}

	if (GetGraph()->IsA<URigVMFunctionLibrary>())
	{
		return false;
	}

	FRigVMControllerObjectFactory Factory(nullptr);
	return Factory.CanCreateObjectsFromText(InText);
}

TArray<FName> URigVMController::ImportNodesFromText(const FString& InText, bool bSetupUndoRedo, bool bPrintPythonCommands)
{
	TArray<FName> NodeNames;
	if (!IsValidGraph())
	{
		return NodeNames;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	FRigVMControllerObjectFactory Factory(this);
	Factory.ProcessBuffer(Graph, RF_Transactional, InText);

	if (Factory.CreatedNodes.Num() == 0)
	{
		return NodeNames;
	}

	if (bSetupUndoRedo)
	{
		OpenUndoBracket(TEXT("Importing Nodes from Text"));
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMInverseAction AddNodesAction;
	if (bSetupUndoRedo)
	{
		ActionStack->BeginAction(AddNodesAction);
	}

	FRigVMUnitNodeCreatedContext::FScope UnitNodeCreatedScope(UnitNodeCreatedContext, ERigVMNodeCreatedReason::Paste);
	for (URigVMNode* CreatedNode : Factory.CreatedNodes)
	{
		if(!CanAddNode(CreatedNode, true))
		{
			continue;
		}

		Graph->Nodes.Add(CreatedNode);

		if (bSetupUndoRedo)
		{
			ActionStack->AddAction(FRigVMRemoveNodeAction(CreatedNode, this));
		}

		// find all nodes affected by this
		TArray<URigVMNode*> SubNodes;
		SubNodes.Add(CreatedNode);

		for(int32 SubNodeIndex=0; SubNodeIndex < SubNodes.Num(); SubNodeIndex++)
		{
			if(URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(SubNodes[SubNodeIndex]))
			{
				{
					FRigVMControllerGraphGuard GraphGuard(this, CollapseNode->GetContainedGraph(), bSetupUndoRedo);
					ReattachLinksToPinObjects();
				}
				
				SubNodes.Append(CollapseNode->GetContainedNodes());
			}
		}

		for(URigVMNode* SubNode : SubNodes)
		{
			if (URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(SubNode))
			{
				if (UnitNodeCreatedContext.IsValid())
				{
					if (TSharedPtr<FStructOnScope> StructScope = UnitNode->ConstructStructInstance())
					{
						TGuardValue<FName> NodeNameScope(UnitNodeCreatedContext.NodeName, UnitNode->GetFName());
						FRigVMStruct* StructInstance = (FRigVMStruct*)StructScope->GetStructMemory();
						StructInstance->OnUnitNodeCreated(UnitNodeCreatedContext);
					}
				}
			}

			if (URigVMFunctionReferenceNode* FunctionRefNode = Cast<URigVMFunctionReferenceNode>(SubNode))
			{
				if (URigVMFunctionLibrary* FunctionLibrary = FunctionRefNode->GetLibrary())
				{
					if (URigVMLibraryNode* FunctionDefinition = FunctionRefNode->GetReferencedNode())
					{
						FunctionLibrary->FunctionReferences.FindOrAdd(FunctionDefinition).FunctionReferences.Add(FunctionRefNode);
						FunctionLibrary->MarkPackageDirty();
					}
				}
			}

			for(URigVMPin* Pin : SubNode->Pins)
			{
				EnsurePinValidity(Pin, true);
			}
		}

		Notify(ERigVMGraphNotifType::NodeAdded, CreatedNode);

		NodeNames.Add(CreatedNode->GetFName());
	}

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(AddNodesAction);
	}

	if (Factory.CreatedLinks.Num() > 0)
	{
		FRigVMBaseAction AddLinksAction;
		if (bSetupUndoRedo)
		{
			ActionStack->BeginAction(AddLinksAction);
		}

		for (URigVMLink* CreatedLink : Factory.CreatedLinks)
		{
			FString SourceLeft, SourceRight, TargetLeft, TargetRight;
			if (URigVMPin::SplitPinPathAtStart(CreatedLink->SourcePinPath, SourceLeft, SourceRight) &&
				URigVMPin::SplitPinPathAtStart(CreatedLink->TargetPinPath, TargetLeft, TargetRight))
			{
				const FName* NewSourceNodeName = Factory.NodeNameMap.Find(*SourceLeft);
				const FName* NewTargetNodeName = Factory.NodeNameMap.Find(*TargetLeft);
				if (NewSourceNodeName && NewTargetNodeName)
				{
					CreatedLink->SourcePinPath = URigVMPin::JoinPinPath(NewSourceNodeName->ToString(), SourceRight);
					CreatedLink->TargetPinPath = URigVMPin::JoinPinPath(NewTargetNodeName->ToString(), TargetRight);
					URigVMPin* SourcePin = CreatedLink->GetSourcePin();
					URigVMPin* TargetPin = CreatedLink->GetTargetPin();

					if (SourcePin == nullptr)
					{
						URigVMNode* OriginalNode = Graph->FindNode(SourceLeft);
						if (OriginalNode && OriginalNode->IsA<URigVMFunctionEntryNode>())
						{
							CreatedLink->SourcePinPath = URigVMPin::JoinPinPath(SourceLeft, SourceRight);
							SourcePin = CreatedLink->GetSourcePin();							
						}
					}
					if (TargetPin == nullptr)
					{
						URigVMNode* OriginalNode = Graph->FindNode(TargetLeft);
						if (OriginalNode && OriginalNode->IsA<URigVMFunctionReturnNode>())
						{
							CreatedLink->TargetPinPath = URigVMPin::JoinPinPath(TargetLeft, TargetRight);
							TargetPin = CreatedLink->GetTargetPin();							
						}
					}
					
					if (SourcePin && TargetPin)
					{
						// BreakAllLinks will unbind and destroy the injected variable node
						// We need to rebind to recreate the variable node with the same name
						bool bWasBinded = TargetPin->IsBoundToVariable();
						FString VariableNodeName, BindingPath;
						if (bWasBinded)
						{
							VariableNodeName = TargetPin->GetBoundVariableNode()->GetName();
							BindingPath = TargetPin->GetBoundVariablePath();	
						}
						
						BreakAllLinksRecursive(TargetPin, true, true, bSetupUndoRedo);
						BreakAllLinks(TargetPin, true, bSetupUndoRedo);
						BreakAllLinksRecursive(TargetPin, true, false, bSetupUndoRedo);

						// recreate binding if needed
						if (bWasBinded)
						{
							BindPinToVariable(TargetPin, BindingPath, bSetupUndoRedo, VariableNodeName);
						}
						else
						{
							Graph->Links.Add(CreatedLink);
							SourcePin->Links.Add(CreatedLink);
							TargetPin->Links.Add(CreatedLink);
						}

						if (bSetupUndoRedo)
						{
							ActionStack->AddAction(FRigVMAddLinkAction(SourcePin, TargetPin));
						}
						Notify(ERigVMGraphNotifType::LinkAdded, CreatedLink);
						continue;
					}
				}
			}

			ReportErrorf(TEXT("Cannot import link '%s -> %s'."), *CreatedLink->SourcePinPath, *CreatedLink->TargetPinPath);
			DestroyObject(CreatedLink);
		}

		if (bSetupUndoRedo)
		{
			ActionStack->EndAction(AddLinksAction);
		}
	}

	if (bSetupUndoRedo)
	{
		CloseUndoBracket();
	}
	
#if WITH_EDITOR
	if (bPrintPythonCommands && !NodeNames.IsEmpty())
	{
		FString PythonContent = InText.Replace(TEXT("\\\""), TEXT("\\\\\""));
		PythonContent = InText.Replace(TEXT("'"), TEXT("\\'"));
		PythonContent = PythonContent.Replace(TEXT("\r\n"), TEXT("\\r\\n'\r\n'"));

		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());

		RigVMPythonUtils::Print(GetGraphOuterName(),
			FString::Printf(TEXT("blueprint.get_controller_by_name('%s').import_nodes_from_text('%s')"),
			*GraphName,
			*PythonContent));
	}
#endif

	return NodeNames;
}

URigVMLibraryNode* URigVMController::LocalizeFunction(
	URigVMLibraryNode* InFunctionDefinition,
	bool bLocalizeDependentPrivateFunctions,
	bool bSetupUndoRedo,
	bool bPrintPythonCommand)
{
	if(!IsValidGraph())
	{
		return nullptr;
	}

	if(InFunctionDefinition == nullptr)
	{
		return nullptr;
	}

	TArray<URigVMLibraryNode*> FunctionsToLocalize;
	FunctionsToLocalize.Add(InFunctionDefinition);

	TMap<URigVMLibraryNode*, URigVMLibraryNode*> Results = LocalizeFunctions(FunctionsToLocalize, bLocalizeDependentPrivateFunctions, bSetupUndoRedo, bPrintPythonCommand);

	URigVMLibraryNode** LocalizedFunctionPtr = Results.Find(FunctionsToLocalize[0]);
	if(LocalizedFunctionPtr)
	{
		return *LocalizedFunctionPtr;
	}
	return nullptr;
}

TMap<URigVMLibraryNode*, URigVMLibraryNode*> URigVMController::LocalizeFunctions(
	TArray<URigVMLibraryNode*> InFunctionDefinitions,
	bool bLocalizeDependentPrivateFunctions,
	bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	TMap<URigVMLibraryNode*, URigVMLibraryNode*> LocalizedFunctions;

	if(!IsValidGraph())
	{
		return LocalizedFunctions;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMFunctionLibrary* ThisLibrary = Graph->GetDefaultFunctionLibrary();
	if(ThisLibrary == nullptr)
	{
		return LocalizedFunctions;
	}

	TArray<URigVMLibraryNode*> FunctionsToLocalize;

	TArray<URigVMLibraryNode*> NodesToVisit;
	for(URigVMLibraryNode* FunctionDefinition : InFunctionDefinitions)
	{
		NodesToVisit.AddUnique(FunctionDefinition);
		FunctionsToLocalize.AddUnique(FunctionDefinition);
	}

	// find all functions to localize
	for(int32 NodeToVisitIndex=0; NodeToVisitIndex<NodesToVisit.Num(); NodeToVisitIndex++)
	{
		URigVMLibraryNode* NodeToVisit = NodesToVisit[NodeToVisitIndex];

		if(URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(NodeToVisit))
		{
			const TArray<URigVMNode*>& ContainedNodes = CollapseNode->GetContainedNodes();
			for(URigVMNode* ContainedNode : ContainedNodes)
			{
				if(URigVMLibraryNode* ContainedLibraryNode = Cast<URigVMLibraryNode>(ContainedNode))
				{
					NodesToVisit.AddUnique(ContainedLibraryNode);
				}
			}

			if(URigVMFunctionLibrary* OtherLibrary = Cast<URigVMFunctionLibrary>(CollapseNode->GetOuter()))
			{
				if(OtherLibrary != ThisLibrary)
				{
					bool bIsAvailable = false;
					if(IsFunctionAvailableDelegate.IsBound())
					{
						bIsAvailable = IsFunctionAvailableDelegate.Execute(CollapseNode);
					}

					if(!bIsAvailable)
					{
						if(!bLocalizeDependentPrivateFunctions)
						{
							ReportAndNotifyErrorf(TEXT("Cannot localize function - dependency %s is private."), *CollapseNode->GetPathName());
							return LocalizedFunctions;
						}
						
						FunctionsToLocalize.AddUnique(CollapseNode);
					}
				}
			}
		}

		else if(URigVMFunctionReferenceNode* FunctionReferencedNode = Cast<URigVMFunctionReferenceNode>(NodeToVisit))
		{
			if(FunctionReferencedNode->GetLibrary() != ThisLibrary)
			{
				if(URigVMCollapseNode* FunctionDefinition = Cast<URigVMCollapseNode>(FunctionReferencedNode->GetReferencedNode()))
				{
					NodesToVisit.AddUnique(FunctionDefinition);
				}
			}
		}
	}
	
	// sort the functions to localize based on their nesting
	Algo::Sort(FunctionsToLocalize, [](URigVMLibraryNode* A, URigVMLibraryNode* B) -> bool
	{
		check(A);
		check(B);
		return B->Contains(A);
	});

	// export all of the content for each node
	TMap<URigVMLibraryNode*, FString> ExportedTextPerFunction;
	for(URigVMLibraryNode* FunctionToLocalize : FunctionsToLocalize)
	{
		URigVMFunctionLibrary* OtherLibrary = Cast<URigVMFunctionLibrary>(FunctionToLocalize->GetOuter());
		FRigVMControllerGraphGuard GraphGuard(this, OtherLibrary, false);

		const TArray<FName> NodeNamesToExport = {FunctionToLocalize->GetFName()};
		const FString ExportedText = ExportNodesToText(NodeNamesToExport);
		ExportedTextPerFunction.Add(FunctionToLocalize, ExportedText);
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	if (bSetupUndoRedo)
	{
		OpenUndoBracket(TEXT("Localize functions"));
	}

	// import the functions to our local function library
	{
		FRigVMControllerGraphGuard GraphGuard(this, ThisLibrary, bSetupUndoRedo);

		// override the availability and check up later
		TGuardValue<FRigVMController_IsFunctionAvailableDelegate> IsFunctionAvailableGuard(IsFunctionAvailableDelegate,
			FRigVMController_IsFunctionAvailableDelegate::CreateLambda([](URigVMLibraryNode*)
			{
				return true;
			})
		);

		for(URigVMLibraryNode* FunctionToLocalize : FunctionsToLocalize)
		{
			const FString& ExportedText = ExportedTextPerFunction.FindChecked(FunctionToLocalize);
			TArray<FName> ImportedNodeNames = ImportNodesFromText(ExportedText);
			if(ImportedNodeNames.Num() != 1)
			{
				ReportErrorf(TEXT("Not possible to localize function %s"), *FunctionToLocalize->GetPathName());
				continue;
			}

			URigVMLibraryNode* LocalizedFunction = Cast<URigVMLibraryNode>(GetGraph()->FindNodeByName(ImportedNodeNames[0]));
			if(LocalizedFunction == nullptr)
			{
				ReportErrorf(TEXT("Not possible to localize function %s"), *FunctionToLocalize->GetPathName());
				continue;
			}

			LocalizedFunctions.Add(FunctionToLocalize, LocalizedFunction);
			ThisLibrary->LocalizedFunctions.FindOrAdd(FunctionToLocalize->GetPathName(), LocalizedFunction);
		}
	}

	// once we have all local functions available, clean up the references
	TArray<URigVMGraph*> GraphsToUpdate;
	GraphsToUpdate.AddUnique(Graph);
	if(URigVMFunctionLibrary* DefaultFunctionLibrary = Graph->GetDefaultFunctionLibrary())
	{
		GraphsToUpdate.AddUnique(DefaultFunctionLibrary);
	}
	for(int32 GraphToUpdateIndex=0; GraphToUpdateIndex<GraphsToUpdate.Num(); GraphToUpdateIndex++)
	{
		URigVMGraph* GraphToUpdate = GraphsToUpdate[GraphToUpdateIndex];
		
		const TArray<URigVMNode*> NodesToUpdate = GraphToUpdate->GetNodes();
		for(URigVMNode* NodeToUpdate : NodesToUpdate)
		{
			if(URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(NodeToUpdate))
			{
				GraphsToUpdate.AddUnique(CollapseNode->GetContainedGraph());
			}
			else if(URigVMFunctionReferenceNode* FunctionReferenceNode = Cast<URigVMFunctionReferenceNode>(NodeToUpdate))
			{
				URigVMLibraryNode* ReferencedNode = FunctionReferenceNode->GetReferencedNode();
				URigVMLibraryNode** RemappedNodePtr = LocalizedFunctions.Find(ReferencedNode);
				if(RemappedNodePtr)
				{
					URigVMLibraryNode* RemappedNode = *RemappedNodePtr;
					SetReferencedFunction(FunctionReferenceNode, RemappedNode, bSetupUndoRedo);
				}
			}
		}
	}

	if(bSetupUndoRedo)
	{
		CloseUndoBracket();
	}

	if (bPrintPythonCommand)
	{
		FString FunctionNames = TEXT("[");
		for (auto It = InFunctionDefinitions.CreateConstIterator(); It; ++It)
		{
			FunctionNames += FString::Printf(TEXT("unreal.load_object(name = '%s', outer = None).get_local_function_library().find_function('%s')"),
				*(*It)->GetLibrary()->GetOuter()->GetPathName(),
				*(*It)->GetName());
			if (It.GetIndex() < InFunctionDefinitions.Num() - 1)
			{
				FunctionNames += TEXT(", ");
			}
		}
		FunctionNames += TEXT("]");

		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());

		RigVMPythonUtils::Print(GetGraphOuterName(), 
							FString::Printf(TEXT("blueprint.get_controller_by_name('%s').localize_functions(%s, %s)"),
											*GraphName,
											*FunctionNames,
											(bLocalizeDependentPrivateFunctions) ? TEXT("True") : TEXT("False")));
	}

	return LocalizedFunctions;
}

FName URigVMController::GetUniqueName(const FName& InName, TFunction<bool(const FName&)> IsNameAvailableFunction, bool bAllowPeriod, bool bAllowSpace)
{
	FString SanitizedPrefix = InName.ToString();
	SanitizeName(SanitizedPrefix, bAllowPeriod, bAllowSpace);

	int32 NameSuffix = 0;
	FString Name = SanitizedPrefix;
	while (!IsNameAvailableFunction(*Name))
	{
		NameSuffix++;
		Name = FString::Printf(TEXT("%s_%d"), *SanitizedPrefix, NameSuffix);
	}
	return *Name;
}

URigVMCollapseNode* URigVMController::CollapseNodes(const TArray<FName>& InNodeNames, const FString& InCollapseNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	TArray<URigVMNode*> Nodes;
	for (const FName& NodeName : InNodeNames)
	{
		URigVMNode* Node = Graph->FindNodeByName(NodeName);
		if (Node == nullptr)
		{
			ReportErrorf(TEXT("Cannot find node '%s'."), *NodeName.ToString());
			return nullptr;
		}
		Nodes.AddUnique(Node);
	}

	URigVMCollapseNode* Node = CollapseNodes(Nodes, InCollapseNodeName, bSetupUndoRedo);
	if (Node && bPrintPythonCommand)
	{
		FString ArrayStr = TEXT("[");
		for (auto It = InNodeNames.CreateConstIterator(); It; ++It)
		{
			ArrayStr += TEXT("'") + It->ToString() + TEXT("'");
			if (It.GetIndex() < InNodeNames.Num() - 1)
			{
				ArrayStr += TEXT(", ");
			}
		}
		ArrayStr += TEXT("]");

		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());

		RigVMPythonUtils::Print(GetGraphOuterName(), 
							FString::Printf(TEXT("blueprint.get_controller_by_name('%s').collapse_nodes(%s, '%s')"),
											*GraphName,
											*ArrayStr,
											*InCollapseNodeName));
	}

	return Node;
}

TArray<URigVMNode*> URigVMController::ExpandLibraryNode(const FName& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return TArray<URigVMNode*>();
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMNode* Node = Graph->FindNodeByName(InNodeName);
	if (Node == nullptr)
	{
		ReportErrorf(TEXT("Cannot find collapse node '%s'."), *InNodeName.ToString());
		return TArray<URigVMNode*>();
	}

	URigVMLibraryNode* LibNode = Cast<URigVMLibraryNode>(Node);
	if (LibNode == nullptr)
	{
		ReportErrorf(TEXT("Node '%s' is not a library node (not collapse nor function)."), *InNodeName.ToString());
		return TArray<URigVMNode*>();
	}

	TArray<URigVMNode*> Nodes = ExpandLibraryNode(LibNode, bSetupUndoRedo);

	if (!Nodes.IsEmpty() && bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
		const FString NodeName = GetSanitizedNodeName(Node->GetName());
		
		RigVMPythonUtils::Print(GetGraphOuterName(), 
							FString::Printf(TEXT("blueprint.get_controller_by_name('%s').expand_library_node('%s')"),
											*GraphName,
											*NodeName));
	}

	return Nodes;
}

#endif

URigVMCollapseNode* URigVMController::CollapseNodes(const TArray<URigVMNode*>& InNodes, const FString& InCollapseNodeName, bool bSetupUndoRedo)
{
	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot collapse nodes in function library graphs."));
		return nullptr;
	}

	TArray<URigVMNode*> Nodes;
	for (URigVMNode* Node : InNodes)
	{
		if (!IsValidNodeForGraph(Node))
		{
			return nullptr;
		}

		// filter out certain nodes
		if (Node->IsEvent())
		{
			continue;
		}

		if (Node->IsA<URigVMFunctionEntryNode>() ||
			Node->IsA<URigVMFunctionReturnNode>())
		{
			continue;
		}

		if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
		{
			if (VariableNode->IsInputArgument())
			{
				continue;
			}
		}

		Nodes.Add(Node);
	}

	if (Nodes.Num() == 0)
	{
		return nullptr;
	}

	FBox2D Bounds = FBox2D(EForceInit::ForceInit);
	TArray<FName> NodeNames;
	for (URigVMNode* Node : Nodes)
	{
		NodeNames.Add(Node->GetFName());
		Bounds += Node->GetPosition();
	}

  	FVector2D Diagonal = Bounds.Max - Bounds.Min;
	FVector2D Center = (Bounds.Min + Bounds.Max) * 0.5f;

	bool bContainsOutputs = false;

	TArray<URigVMPin*> PinsToCollapse;
	TMap<URigVMPin*, URigVMPin*> CollapsedPins;
	TArray<URigVMLink*> LinksToRewire;
	TArray<URigVMLink*> AllLinks = Graph->GetLinks();

	auto NodeToBeCollapsed = [&Nodes](URigVMNode* InNode) -> bool
	{
		check(InNode);
		
		if(Nodes.Contains(InNode))
		{
			return true;
		}
		
		if(InNode->IsInjected()) 
		{
			InNode = InNode->GetTypedOuter<URigVMNode>();
			if(Nodes.Contains(InNode))
			{
				return true;
			}
		}

		return false;
	};
	// find all pins to collapse. we need this to find out if
	// we might have a parent pin of a given linked pin already 
	// collapsed.
	for (URigVMLink* Link : AllLinks)
	{
		URigVMPin* SourcePin = Link->GetSourcePin();
		URigVMPin* TargetPin = Link->GetTargetPin();
		bool bSourceToBeCollapsed = NodeToBeCollapsed(SourcePin->GetNode());
		bool bTargetToBeCollapsed = NodeToBeCollapsed(TargetPin->GetNode());
		if (bSourceToBeCollapsed == bTargetToBeCollapsed)
		{
			continue;
		}

		URigVMPin* PinToCollapse = SourcePin;
		PinsToCollapse.AddUnique(PinToCollapse);
		LinksToRewire.Add(Link);
	}

	// make sure that for execute pins we are on one branch only
	TArray<URigVMPin*> InputExecutePins;
	TArray<URigVMPin*> IntermediateExecutePins;
	TArray<URigVMPin*> OutputExecutePins;

	// first collect the output execute pins
	for (URigVMLink* Link : LinksToRewire)
	{
		URigVMPin* ExecutePin = Link->GetSourcePin();
		if (!ExecutePin->IsExecuteContext())
		{
			continue;
		}
		if (!NodeToBeCollapsed(ExecutePin->GetNode()))
		{
			continue;
		}
		if (!OutputExecutePins.IsEmpty())
		{
			if (bSetupUndoRedo)
			{
				ReportAndNotifyErrorf(
					TEXT("Only one set of execute branches can be collapsed, pin %s and %s are on separate branches"),
					*OutputExecutePins[0]->GetPinPath(),
					*ExecutePin->GetPinPath()
				);
			}
			return nullptr;
		}
		OutputExecutePins.Add(ExecutePin);

		while (ExecutePin)
		{
			if (IntermediateExecutePins.Contains(ExecutePin))
			{
				if (bSetupUndoRedo)
				{
					ReportAndNotifyErrorf(TEXT("Only one set of execute branches can be collapsed."));
				}
				return nullptr;
			}
			IntermediateExecutePins.Add(ExecutePin);

			// walk backwards and find all "known execute pins"
			URigVMNode* ExecuteNode = ExecutePin->GetNode();
			for (URigVMPin* Pin : ExecuteNode->GetPins())
			{
				if (Pin->GetDirection() != ERigVMPinDirection::Input &&
					Pin->GetDirection() != ERigVMPinDirection::IO)
				{
					continue;
				}
				if (!Pin->IsExecuteContext())
				{
					continue;
				}
				TArray<URigVMLink*> SourceLinks = Pin->GetSourceLinks();
				ExecutePin = nullptr;
				if (SourceLinks.Num() > 0)
				{
					URigVMPin* PreviousExecutePin = SourceLinks[0]->GetSourcePin();
					if (NodeToBeCollapsed(PreviousExecutePin->GetNode()))
					{
						if (Pin != IntermediateExecutePins.Last())
						{
							IntermediateExecutePins.Add(Pin);
						}
						ExecutePin = PreviousExecutePin;
						break;
					}
				}
			}
		}
	}
	for (URigVMLink* Link : LinksToRewire)
	{
		URigVMPin* ExecutePin = Link->GetTargetPin();
		if (!ExecutePin->IsExecuteContext())
		{
			continue;
		}
		if (!NodeToBeCollapsed(ExecutePin->GetNode()))
		{
			continue;
		}
		if (!IntermediateExecutePins.Contains(ExecutePin) && !IntermediateExecutePins.IsEmpty())
		{
			if (bSetupUndoRedo)
			{
				ReportAndNotifyErrorf(TEXT("Only one set of execute branches can be collapsed"));
			}
			return nullptr;
		}

		if (!InputExecutePins.IsEmpty())
		{
			if (bSetupUndoRedo)
			{
				ReportAndNotifyErrorf(
					TEXT("Only one set of execute branches can be collapsed, pin %s and %s are on separate branches"),
					*InputExecutePins[0]->GetPinPath(),
					*ExecutePin->GetPinPath()
				);
			}
			return nullptr;
		}
		InputExecutePins.Add(ExecutePin);
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMCollapseNodesAction CollapseAction;
	CollapseAction.Title = TEXT("Collapse Nodes");

	if (bSetupUndoRedo)
	{
		ActionStack->BeginAction(CollapseAction);
	}

	FString CollapseNodeName = GetValidNodeName(InCollapseNodeName.IsEmpty() ? FString(TEXT("CollapseNode")) : InCollapseNodeName);
	URigVMCollapseNode* CollapseNode = NewObject<URigVMCollapseNode>(Graph, *CollapseNodeName);
	CollapseNode->ContainedGraph = NewObject<URigVMGraph>(CollapseNode, TEXT("ContainedGraph"));
	CollapseNode->Position = Center;
	Graph->Nodes.Add(CollapseNode);

	// now looper over the links to be rewired
	for (URigVMLink* Link : LinksToRewire)
	{
		bool bSourceToBeCollapsed = NodeToBeCollapsed(Link->GetSourcePin()->GetNode());
		bool bTargetToBeCollapsed = NodeToBeCollapsed(Link->GetTargetPin()->GetNode());

		URigVMPin* PinToCollapse = bSourceToBeCollapsed ? Link->GetSourcePin() : Link->GetTargetPin();
		if (CollapsedPins.Contains(PinToCollapse))
		{
			continue;
		}

		if (PinToCollapse->IsExecuteContext())
		{
			bool bFoundExistingPin = false;
			for (URigVMPin* ExistingPin : CollapseNode->Pins)
			{
				if (ExistingPin->IsExecuteContext())
				{
					CollapsedPins.Add(PinToCollapse, ExistingPin);
					bFoundExistingPin = true;
					break;
				}
			}

			if (bFoundExistingPin)
			{
				continue;
			}
		}

		// for links that connect to the right side of the collapse
		// node, we need to skip sub pins of already exposed pins
		if (bSourceToBeCollapsed)
		{
			bool bParentPinCollapsed = false;
			URigVMPin* ParentPin = PinToCollapse->GetParentPin();
			while (ParentPin != nullptr)
			{
				if (PinsToCollapse.Contains(ParentPin))
				{
					bParentPinCollapsed = true;
					break;
				}
				ParentPin = ParentPin->GetParentPin();
			}

			if (bParentPinCollapsed)
			{
				continue;
			}
		}

		FName PinName = GetUniqueName(PinToCollapse->GetFName(), [CollapseNode](const FName& InName) {
			return CollapseNode->FindPin(InName.ToString()) == nullptr;
		}, false, true);

		URigVMPin* CollapsedPin = NewObject<URigVMPin>(CollapseNode, PinName);
		ConfigurePinFromPin(CollapsedPin, PinToCollapse);

		if (CollapsedPin->IsExecuteContext())
		{
			CollapsedPin->Direction = ERigVMPinDirection::IO;
			bContainsOutputs = true;
		}
		else if (CollapsedPin->GetDirection() == ERigVMPinDirection::IO)
		{
			CollapsedPin->Direction = ERigVMPinDirection::Input;
		}

		if (CollapsedPin->IsStruct())
		{
			AddPinsForStruct(CollapsedPin->GetScriptStruct(), CollapseNode, CollapsedPin, CollapsedPin->GetDirection(), FString(), false);
		}

		bContainsOutputs = bContainsOutputs || bSourceToBeCollapsed;

		AddNodePin(CollapseNode, CollapsedPin);

		FPinState PinState = GetPinState(PinToCollapse);
		ApplyPinState(CollapsedPin, PinState);

		CollapsedPins.Add(PinToCollapse, CollapsedPin);
	}

	Notify(ERigVMGraphNotifType::NodeAdded, CollapseNode);

	URigVMFunctionEntryNode* EntryNode = nullptr;
	URigVMFunctionReturnNode* ReturnNode = nullptr;
	{
		FRigVMControllerGraphGuard GraphGuard(this, CollapseNode->GetContainedGraph(), false);

		EntryNode = NewObject<URigVMFunctionEntryNode>(CollapseNode->ContainedGraph, TEXT("Entry"));
		CollapseNode->ContainedGraph->Nodes.Add(EntryNode);
		EntryNode->Position = -Diagonal * 0.5f - FVector2D(250.f, 0.f);
		RefreshFunctionPins(EntryNode, false);

		Notify(ERigVMGraphNotifType::NodeAdded, EntryNode);

		if (bContainsOutputs)
		{
			ReturnNode = NewObject<URigVMFunctionReturnNode>(CollapseNode->ContainedGraph, TEXT("Return"));
			CollapseNode->ContainedGraph->Nodes.Add(ReturnNode);
			ReturnNode->Position = FVector2D(Diagonal.X, -Diagonal.Y) * 0.5f + FVector2D(300.f, 0.f);
			RefreshFunctionPins(ReturnNode, false);

			Notify(ERigVMGraphNotifType::NodeAdded, ReturnNode);
		}
	}

	// create the new nodes within the collapse node
	TArray<FName> ContainedNodeNames;
	{
		FString TextContent = ExportNodesToText(NodeNames);

		FRigVMControllerGraphGuard GraphGuard(this, CollapseNode->GetContainedGraph(), false);
		ContainedNodeNames = ImportNodesFromText(TextContent, false);

		// move the nodes to the right place
		for (const FName& ContainedNodeName : ContainedNodeNames)
		{
			if (URigVMNode* ContainedNode = CollapseNode->GetContainedGraph()->FindNodeByName(ContainedNodeName))
			{
				if(!ContainedNode->IsInjected())
				{
					SetNodePosition(ContainedNode, ContainedNode->Position - Center, false, false);
				}
			}
		}

		for (URigVMLink* LinkToRewire : LinksToRewire)
		{
			URigVMPin* SourcePin = LinkToRewire->GetSourcePin();
			URigVMPin* TargetPin = LinkToRewire->GetTargetPin();

			if (NodeToBeCollapsed(SourcePin->GetNode()))
			{
				// if the parent pin of this was collapsed
				// it's possible that the child pin wasn't.
				if (!CollapsedPins.Contains(SourcePin))
				{
					continue;
				}

				URigVMPin* CollapsedPin = CollapsedPins.FindChecked(SourcePin);
				SourcePin = CollapseNode->ContainedGraph->FindPin(SourcePin->GetPinPath());
				TargetPin = ReturnNode->FindPin(CollapsedPin->GetName());
			}
			else
			{
				URigVMPin* CollapsedPin = CollapsedPins.FindChecked(TargetPin);
				SourcePin = EntryNode->FindPin(CollapsedPin->GetName());
				TargetPin = CollapseNode->ContainedGraph->FindPin(TargetPin->GetPinPath());
			}

			if (SourcePin && TargetPin)
			{
				if (!SourcePin->IsLinkedTo(TargetPin))
				{
					AddLink(SourcePin, TargetPin, false);
				}
			}
		}
	}

	TArray<URigVMLink*> RewiredLinks;
	for (URigVMLink* LinkToRewire : LinksToRewire)
	{
		if (RewiredLinks.Contains(LinkToRewire))
		{
			continue;
		}

		URigVMPin* SourcePin = LinkToRewire->GetSourcePin();
		URigVMPin* TargetPin = LinkToRewire->GetTargetPin();

		if (NodeToBeCollapsed(SourcePin->GetNode()))
		{
			FString SegmentPath;
			URigVMPin* PinToCheck = SourcePin;

			URigVMPin** CollapsedPinPtr = CollapsedPins.Find(PinToCheck);
			while (CollapsedPinPtr == nullptr)
			{
				if (SegmentPath.IsEmpty())
				{
					SegmentPath = PinToCheck->GetName();
				}
				else
				{
					SegmentPath = URigVMPin::JoinPinPath(PinToCheck->GetName(), SegmentPath);
				}

				PinToCheck = PinToCheck->GetParentPin();
				check(PinToCheck);

				CollapsedPinPtr = CollapsedPins.Find(PinToCheck);
			}

			URigVMPin* CollapsedPin = *CollapsedPinPtr;
			check(CollapsedPin);

			if (!SegmentPath.IsEmpty())
			{
				CollapsedPin = CollapsedPin->FindSubPin(SegmentPath);
				check(CollapsedPin);
			}

			TArray<URigVMLink*> TargetLinks = SourcePin->GetTargetLinks(false);
			for (URigVMLink* TargetLink : TargetLinks)
			{
				TargetPin = TargetLink->GetTargetPin();
				if (!CollapsedPin->IsLinkedTo(TargetPin))
				{
					AddLink(CollapsedPin, TargetPin, false);
				}
			}
			RewiredLinks.Append(TargetLinks);
		}
		else
		{
			URigVMPin* CollapsedPin = CollapsedPins.FindChecked(TargetPin);
			if (!SourcePin->IsLinkedTo(CollapsedPin))
			{
				AddLink(SourcePin, CollapsedPin, false);
			}
		}

		RewiredLinks.Add(LinkToRewire);
	}

	if (ReturnNode)
	{
		struct Local
		{
			static bool IsLinkedToEntryNode(URigVMNode* InNode, TMap<URigVMNode*, bool>& CachedMap)
			{
				if (InNode->IsA<URigVMFunctionEntryNode>())
				{
					return true;
				}

				if (!CachedMap.Contains(InNode))
				{
					CachedMap.Add(InNode, false);

					if (URigVMPin* ExecuteContextPin = InNode->FindPin(FRigVMStruct::ExecuteContextName.ToString()))
					{
						TArray<URigVMPin*> SourcePins = ExecuteContextPin->GetLinkedSourcePins();
						for (URigVMPin* SourcePin : SourcePins)
						{
							if (IsLinkedToEntryNode(SourcePin->GetNode(), CachedMap))
							{
								CachedMap.FindOrAdd(InNode) = true;
								break;
							}
						}
					}
				}

				return CachedMap.FindChecked(InNode);
			}
		};

		// check if there is a last node on the top level block what we need to hook up
		TMap<URigVMNode*, bool> IsContainedNodeLinkedToEntryNode;

		TArray<URigVMNode*> NodesForExecutePin;
		NodesForExecutePin.Add(EntryNode);
		for (int32 NodeForExecutePinIndex = 0; NodeForExecutePinIndex < NodesForExecutePin.Num(); NodeForExecutePinIndex++)
		{
			URigVMNode* NodeForExecutePin = NodesForExecutePin[NodeForExecutePinIndex];
			if (!NodeForExecutePin->IsMutable())
			{
				continue;
			}

			TArray<URigVMNode*> TargetNodes = NodeForExecutePin->GetLinkedTargetNodes();
			for(URigVMNode* TargetNode : TargetNodes)
			{
				NodesForExecutePin.AddUnique(TargetNode);
			}

			// make sure the node doesn't have any mutable nodes connected to its executecontext
			URigVMPin* ExecuteContextPin = nullptr;
			if(URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(NodeForExecutePin))
			{
				TSharedPtr<FStructOnScope> UnitScope = UnitNode->ConstructStructInstance();
				if(UnitScope.IsValid())
				{
					FRigVMStruct* Unit = (FRigVMStruct*)UnitScope->GetStructMemory();
					if(Unit->IsForLoop())
					{
						ExecuteContextPin = NodeForExecutePin->FindPin(FRigVMStruct::ForLoopCompletedPinName.ToString());
					}
				}
			}

			if(ExecuteContextPin == nullptr)
			{
				ExecuteContextPin = NodeForExecutePin->FindPin(FRigVMStruct::ExecuteContextName.ToString());
			}

			if(ExecuteContextPin)
			{
				if(!ExecuteContextPin->IsExecuteContext())
				{
					continue;
				}

				if (ExecuteContextPin->GetDirection() != ERigVMPinDirection::IO &&
					ExecuteContextPin->GetDirection() != ERigVMPinDirection::Output)
				{
					continue;
				}

				if (ExecuteContextPin->GetTargetLinks().Num() > 0)
				{
					continue;
				}

				if (!Local::IsLinkedToEntryNode(NodeForExecutePin, IsContainedNodeLinkedToEntryNode))
				{
					continue;
				}

				FRigVMControllerGraphGuard GraphGuard(this, CollapseNode->GetContainedGraph(), false);
				AddLink(ExecuteContextPin, ReturnNode->FindPin(FRigVMStruct::ExecuteContextName.ToString()), false);
				break;
			}
		}
	}

	for (const FName& NodeToRemove : NodeNames)
	{
		RemoveNodeByName(NodeToRemove, false, true);
	}

	if (bSetupUndoRedo)
	{
		CollapseAction.LibraryNodePath = CollapseNode->GetName();
		for (URigVMNode* InNode : InNodes)
		{
			CollapseAction.CollapsedNodesPaths.Add(InNode->GetName());
		}
		ActionStack->EndAction(CollapseAction);
	}

	return CollapseNode;
}

TArray<URigVMNode*> URigVMController::ExpandLibraryNode(URigVMLibraryNode* InNode, bool bSetupUndoRedo)
{
	if (!IsValidNodeForGraph(InNode))
	{
		return TArray<URigVMNode*>();
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot expand nodes in function library graphs."));
		return TArray<URigVMNode*>();
	}

	TArray<URigVMNode*> ContainedNodes = InNode->GetContainedNodes();
	TArray<URigVMLink*> ContainedLinks = InNode->GetContainedLinks();
	if (ContainedNodes.Num() == 0)
	{
		return TArray<URigVMNode*>();
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMExpandNodeAction ExpandAction;
	ExpandAction.Title = FString::Printf(TEXT("Expand '%s' Node"), *InNode->GetName());

	if (bSetupUndoRedo)
	{
		ActionStack->BeginAction(ExpandAction);
	}

	TArray<FName> NodeNames;
	FBox2D Bounds = FBox2D(EForceInit::ForceInit);
	{
		TArray<URigVMNode*> FilteredNodes;
		for (URigVMNode* Node : ContainedNodes)
		{
			if (Cast<URigVMFunctionEntryNode>(Node) != nullptr ||
				Cast<URigVMFunctionReturnNode>(Node) != nullptr)
			{
				continue;
			}

			if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
			{
				if (VariableNode->IsInputArgument())
				{
					continue;
				}
			}

			if(Node->IsInjected())
			{
				continue;
			}
			
			NodeNames.Add(Node->GetFName());
			FilteredNodes.Add(Node);
			Bounds += Node->GetPosition();
		}
		ContainedNodes = FilteredNodes;
	}

	if (ContainedNodes.Num() == 0)
	{
		if (bSetupUndoRedo)
		{
			ActionStack->CancelAction(ExpandAction);
		}
		return TArray<URigVMNode*>();
	}

	// Find local variables that need to be added as member variables. If member variables of same name and type already
	// exist, they will be reused. If a local variable is not used, it will not be created.
	if (URigVMFunctionReferenceNode* FunctionReferenceNode = Cast<URigVMFunctionReferenceNode>(InNode))
	{
		TArray<FRigVMGraphVariableDescription> LocalVariables = FunctionReferenceNode->GetContainedGraph()->LocalVariables;
		TArray<FRigVMExternalVariable> CurrentVariables = GetAllVariables();
		TArray<FRigVMGraphVariableDescription> VariablesToAdd;
		for (const URigVMNode* Node : FunctionReferenceNode->GetContainedGraph()->GetNodes())
		{
			if (const URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
			{
				if (VariableNode->IsInputArgument())
				{
					continue;
				}
				
				for (FRigVMGraphVariableDescription& LocalVariable : LocalVariables)
				{
					if (LocalVariable.Name == VariableNode->GetVariableName())
					{
						bool bVariableExists = false;
						bool bVariableIncompatible = false;
						FRigVMExternalVariable LocalVariableExternalType = LocalVariable.ToExternalVariable();
						for (FRigVMExternalVariable& CurrentVariable : CurrentVariables)
						{						
							if (CurrentVariable.Name == LocalVariable.Name)
							{
								if (CurrentVariable.TypeName != LocalVariableExternalType.TypeName ||
									CurrentVariable.TypeObject != LocalVariableExternalType.TypeObject ||
									CurrentVariable.bIsArray != LocalVariableExternalType.bIsArray)
								{
									bVariableIncompatible = true;	
								}
								bVariableExists = true;
								break;
							}
						}

						if (!bVariableExists)
						{
							VariablesToAdd.Add(LocalVariable);	
						}
						else if(bVariableIncompatible)
						{
							ReportErrorf(TEXT("Found variable %s of incompatible type with a local variable inside function %s"), *LocalVariable.Name.ToString(), *FunctionReferenceNode->GetReferencedNode()->GetName());
							if (bSetupUndoRedo)
							{
								ActionStack->CancelAction(ExpandAction);
							}
							return TArray<URigVMNode*>();
						}
						break;
					}
				}
			}
		}

		if (RequestNewExternalVariableDelegate.IsBound())
		{
			for (const FRigVMGraphVariableDescription& OldVariable : VariablesToAdd)
			{
				RequestNewExternalVariableDelegate.Execute(OldVariable, false, false);
			}
		}
	}

	FVector2D Diagonal = Bounds.Max - Bounds.Min;
	FVector2D Center = (Bounds.Min + Bounds.Max) * 0.5f;

	FString TextContent;
	{
		FRigVMControllerGraphGuard GraphGuard(this, InNode->GetContainedGraph(), false);
		TextContent = ExportNodesToText(NodeNames);
	}

	TArray<FName> ExpandedNodeNames = ImportNodesFromText(TextContent, false);
	TArray<URigVMNode*> ExpandedNodes;
	for (const FName& ExpandedNodeName : ExpandedNodeNames)
	{
		URigVMNode* ExpandedNode = Graph->FindNodeByName(ExpandedNodeName);
		check(ExpandedNode);
		ExpandedNodes.Add(ExpandedNode);
	}

	check(ExpandedNodeNames.Num() >= NodeNames.Num());

	TMap<FName, FName> NodeNameMap;
	for (int32 NodeNameIndex = 0, ExpandedNodeNameIndex = 0; NodeNameIndex < NodeNames.Num(); ExpandedNodeNameIndex++)
	{
		if (ExpandedNodes[ExpandedNodeNameIndex]->IsInjected())
		{
			continue;
		}
		NodeNameMap.Add(NodeNames[NodeNameIndex], ExpandedNodeNames[ExpandedNodeNameIndex]);
		SetNodePosition(ExpandedNodes[ExpandedNodeNameIndex], InNode->Position + ContainedNodes[NodeNameIndex]->Position - Center, false, false);
		NodeNameIndex++;
	}

	// a) store all of the pin defaults off the library node
	TMap<FString, FPinState> PinStates = GetPinStates(InNode);

	// b) create a map of new links to create by following the links to / from the library node
	TMap<FString, TArray<FString>> ToLibraryNode;
	TMap<FString, TArray<FString>> FromLibraryNode;
	TArray<URigVMPin*> LibraryPinsToReroute;

	TArray<URigVMLink*> LibraryLinks = InNode->GetLinks();
	for (URigVMLink* Link : LibraryLinks)
	{
		if (Link->GetTargetPin()->GetNode() == InNode)
		{
			if (!Link->GetTargetPin()->IsRootPin())
			{
				LibraryPinsToReroute.AddUnique(Link->GetTargetPin()->GetRootPin());
			}

			FString NodeName, PinPath;
			URigVMPin::SplitPinPathAtStart(Link->GetTargetPin()->GetPinPath(), NodeName, PinPath);
			ToLibraryNode.FindOrAdd(PinPath).Add(Link->GetSourcePin()->GetPinPath());
		}
		else
		{
			if (!Link->GetSourcePin()->IsRootPin())
			{
				LibraryPinsToReroute.AddUnique(Link->GetSourcePin()->GetRootPin());
			}

			FString NodeName, PinPath;
			URigVMPin::SplitPinPathAtStart(Link->GetSourcePin()->GetPinPath(), NodeName, PinPath);
			FromLibraryNode.FindOrAdd(PinPath).Add(Link->GetTargetPin()->GetPinPath());
		}
	}

	// c) create a map from the entry node to the contained graph
	TMap<FString, TArray<FString>> FromEntryNode;
	if (URigVMFunctionEntryNode* EntryNode = InNode->GetEntryNode())
	{
		TArray<URigVMLink*> EntryLinks = EntryNode->GetLinks();

		for (URigVMNode* Node : InNode->GetContainedGraph()->GetNodes())
		{
			if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
			{
				if (VariableNode->IsInputArgument())
				{
					EntryLinks.Append(VariableNode->GetLinks());
				}
			}
		}
		
		for (URigVMLink* Link : EntryLinks)
		{
			if (Link->GetSourcePin()->GetNode() != EntryNode && !Link->GetSourcePin()->GetNode()->IsA<URigVMVariableNode>())
			{
				continue;
			}

			if (!Link->GetSourcePin()->IsRootPin())
			{
				LibraryPinsToReroute.AddUnique(InNode->FindPin(Link->GetSourcePin()->GetRootPin()->GetName()));
			}

			FString NodeName, PinPath;
			URigVMPin::SplitPinPathAtStart(Link->GetSourcePin()->GetPinPath(), NodeName, PinPath);

			if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Link->GetSourcePin()->GetNode()))
			{
				PinPath = VariableNode->GetVariableName().ToString();
			}

			TArray<FString>& LinkedPins = FromEntryNode.FindOrAdd(PinPath);

			URigVMPin::SplitPinPathAtStart(Link->GetTargetPin()->GetPinPath(), NodeName, PinPath);
			
			if (NodeNameMap.Contains(*NodeName))
			{
				NodeName = NodeNameMap.FindChecked(*NodeName).ToString();
				LinkedPins.Add(URigVMPin::JoinPinPath(NodeName, PinPath));	
			}
			else if (NodeName == TEXT("Return"))
			{
				LinkedPins.Add(URigVMPin::JoinPinPath(NodeName, PinPath));
			}
		}
	}

	// d) create a map from the contained graph from to the return node
	TMap<FString, TArray<FString>> ToReturnNode;
	if (URigVMFunctionReturnNode* ReturnNode = InNode->GetReturnNode())
	{
		TArray<URigVMLink*> ReturnLinks = ReturnNode->GetLinks();
		for (URigVMLink* Link : ReturnLinks)
		{
			if (Link->GetTargetPin()->GetNode() != ReturnNode)
			{
				continue;
			}

			if (!Link->GetTargetPin()->IsRootPin())
			{
				LibraryPinsToReroute.AddUnique(InNode->FindPin(Link->GetTargetPin()->GetRootPin()->GetName()));
			}

			FString NodeName, PinPath;
			URigVMPin::SplitPinPathAtStart(Link->GetTargetPin()->GetPinPath(), NodeName, PinPath);

			TArray<FString>& LinkedPins = ToReturnNode.FindOrAdd(PinPath);

			URigVMPin::SplitPinPathAtStart(Link->GetSourcePin()->GetPinPath(), NodeName, PinPath);
			
			if (NodeNameMap.Contains(*NodeName))
			{
				NodeName = NodeNameMap.FindChecked(*NodeName).ToString();
				LinkedPins.Add(URigVMPin::JoinPinPath(NodeName, PinPath));	
			}
			else if (NodeName == TEXT("Entry"))
			{
				LinkedPins.Add(URigVMPin::JoinPinPath(NodeName, PinPath));
			}
		}
	}

	// e) restore all pin states on pins linked to the entry node
	for (const TPair<FString, TArray<FString>>& FromEntryPair : FromEntryNode)
	{
		FString EntryPinPath = FromEntryPair.Key;
		const FPinState* CollapsedPinState = PinStates.Find(EntryPinPath);
		if (CollapsedPinState == nullptr)
		{
			continue;
		}

		for (const FString& EntryTargetLinkPinPath : FromEntryPair.Value)
		{
			if (URigVMPin* TargetPin = GetGraph()->FindPin(EntryTargetLinkPinPath))
			{
				ApplyPinState(TargetPin, *CollapsedPinState);
			}
		}
	}

	// f) create reroutes for all pins which had wires on sub pins
	TMap<FString, URigVMPin*> ReroutedInputPins;
	TMap<FString, URigVMPin*> ReroutedOutputPins;
	FVector2D RerouteInputPosition = InNode->Position + FVector2D(-Diagonal.X, -Diagonal.Y) * 0.5 + FVector2D(-200.f, 0.f);
	FVector2D RerouteOutputPosition = InNode->Position + FVector2D(Diagonal.X, -Diagonal.Y) * 0.5 + FVector2D(250.f, 0.f);
	for (URigVMPin* LibraryPinToReroute : LibraryPinsToReroute)
	{
		if (LibraryPinToReroute->GetDirection() == ERigVMPinDirection::Input ||
			LibraryPinToReroute->GetDirection() == ERigVMPinDirection::IO)
		{
			URigVMRerouteNode* RerouteNode =
				AddFreeRerouteNode(
					true,
					LibraryPinToReroute->GetCPPType(),
					*LibraryPinToReroute->GetCPPTypeObject()->GetPathName(),
					false,
					NAME_None,
					LibraryPinToReroute->GetDefaultValue(),
					RerouteInputPosition,
					FString::Printf(TEXT("Reroute_%s"), *LibraryPinToReroute->GetName()),
					false);

			RerouteInputPosition += FVector2D(0.f, 150.f);

			URigVMPin* ReroutePin = RerouteNode->FindPin(URigVMRerouteNode::ValueName);
			ApplyPinState(ReroutePin, GetPinState(LibraryPinToReroute));
			ReroutedInputPins.Add(LibraryPinToReroute->GetName(), ReroutePin);
			ExpandedNodes.Add(RerouteNode);
		}

		if (LibraryPinToReroute->GetDirection() == ERigVMPinDirection::Output ||
			LibraryPinToReroute->GetDirection() == ERigVMPinDirection::IO)
		{
			URigVMRerouteNode* RerouteNode =
				AddFreeRerouteNode(
					true,
					LibraryPinToReroute->GetCPPType(),
					*LibraryPinToReroute->GetCPPTypeObject()->GetPathName(),
					false,
					NAME_None,
					LibraryPinToReroute->GetDefaultValue(),
					RerouteOutputPosition,
					FString::Printf(TEXT("Reroute_%s"), *LibraryPinToReroute->GetName()),
					false);

			RerouteOutputPosition += FVector2D(0.f, 150.f);

			URigVMPin* ReroutePin = RerouteNode->FindPin(URigVMRerouteNode::ValueName);
			ApplyPinState(ReroutePin, GetPinState(LibraryPinToReroute));
			ReroutedOutputPins.Add(LibraryPinToReroute->GetName(), ReroutePin);
			ExpandedNodes.Add(RerouteNode);
		}
	}

	// g) remap all output / source pins and create a final list of links to create
	TMap<FString, FString> RemappedSourcePinsForInputs;
	TMap<FString, FString> RemappedSourcePinsForOutputs;
	TArray<URigVMPin*> LibraryPins = InNode->GetAllPinsRecursively();
	for (URigVMPin* LibraryPin : LibraryPins)
	{
		FString LibraryPinPath = LibraryPin->GetPinPath();
		FString LibraryNodeName;
		URigVMPin::SplitPinPathAtStart(LibraryPinPath, LibraryNodeName, LibraryPinPath);


		struct Local
		{
			static void UpdateRemappedSourcePins(FString SourcePinPath, FString TargetPinPath, TMap<FString, FString>& RemappedSourcePins)
			{
				while (!SourcePinPath.IsEmpty() && !TargetPinPath.IsEmpty())
				{
					RemappedSourcePins.FindOrAdd(SourcePinPath) = TargetPinPath;

					FString SourceLastSegment, TargetLastSegment;
					if (!URigVMPin::SplitPinPathAtEnd(SourcePinPath, SourcePinPath, SourceLastSegment))
					{
						break;
					}
					if (!URigVMPin::SplitPinPathAtEnd(TargetPinPath, TargetPinPath, TargetLastSegment))
					{
						break;
					}
				}
			}
		};

		if (LibraryPin->GetDirection() == ERigVMPinDirection::Input ||
			LibraryPin->GetDirection() == ERigVMPinDirection::IO)
		{
			if (const TArray<FString>* LibraryPinLinksPtr = ToLibraryNode.Find(LibraryPinPath))
			{
				const TArray<FString>& LibraryPinLinks = *LibraryPinLinksPtr;
				ensure(LibraryPinLinks.Num() == 1);

				Local::UpdateRemappedSourcePins(LibraryPinPath, LibraryPinLinks[0], RemappedSourcePinsForInputs);
			}
		}
		if (LibraryPin->GetDirection() == ERigVMPinDirection::Output ||
			LibraryPin->GetDirection() == ERigVMPinDirection::IO)
		{
			if (const TArray<FString>* LibraryPinLinksPtr = ToReturnNode.Find(LibraryPinPath))
			{
				const TArray<FString>& LibraryPinLinks = *LibraryPinLinksPtr;
				ensure(LibraryPinLinks.Num() == 1);

				Local::UpdateRemappedSourcePins(LibraryPinPath, LibraryPinLinks[0], RemappedSourcePinsForOutputs);
			}
		}
	}

	// h) re-establish all of the links going to the left of the library node
	//    in this pass we only care about pins which have reroutes
	for (const TPair<FString, TArray<FString>>& ToLibraryNodePair : ToLibraryNode)
	{
		FString LibraryNodePinName, LibraryNodePinPathSuffix;
		if (!URigVMPin::SplitPinPathAtStart(ToLibraryNodePair.Key, LibraryNodePinName, LibraryNodePinPathSuffix))
		{
			LibraryNodePinName = ToLibraryNodePair.Key;
		}

		if (!ReroutedInputPins.Contains(LibraryNodePinName))
		{
			continue;
		}

		URigVMPin* ReroutedPin = ReroutedInputPins.FindChecked(LibraryNodePinName);
		URigVMPin* TargetPin = LibraryNodePinPathSuffix.IsEmpty() ? ReroutedPin : ReroutedPin->FindSubPin(LibraryNodePinPathSuffix);
		check(TargetPin);

		for (const FString& SourcePinPath : ToLibraryNodePair.Value)
		{
			URigVMPin* SourcePin = GetGraph()->FindPin(*SourcePinPath);
			if (SourcePin && TargetPin)
			{
				if (!SourcePin->IsLinkedTo(TargetPin))
				{
					AddLink(SourcePin, TargetPin, false);
				}
			}
		}
	}

	// i) re-establish all of the links going to the left of the library node (based on the entry node)
	for (const TPair<FString, TArray<FString>>& FromEntryNodePair : FromEntryNode)
	{
		FString EntryPinPath = FromEntryNodePair.Key;
		FString EntryPinPathSuffix;

		const FString* RemappedSourcePin = RemappedSourcePinsForInputs.Find(EntryPinPath);
		while (RemappedSourcePin == nullptr)
		{
			FString LastSegment;
			if (!URigVMPin::SplitPinPathAtEnd(EntryPinPath, EntryPinPath, LastSegment))
			{
				break;
			}

			if (EntryPinPathSuffix.IsEmpty())
			{
				EntryPinPathSuffix = LastSegment;
			}
			else
			{
				EntryPinPathSuffix = URigVMPin::JoinPinPath(LastSegment, EntryPinPathSuffix);
			}

			RemappedSourcePin = RemappedSourcePinsForInputs.Find(EntryPinPath);
		}

		if (RemappedSourcePin == nullptr)
		{
			continue;
		}

		FString RemappedSourcePinPath = *RemappedSourcePin;
		if (!EntryPinPathSuffix.IsEmpty())
		{
			RemappedSourcePinPath = URigVMPin::JoinPinPath(RemappedSourcePinPath, EntryPinPathSuffix);
		}

		// remap the top level pin in case we need to insert a reroute
		FString EntryPinName;
		if (!URigVMPin::SplitPinPathAtStart(FromEntryNodePair.Key, EntryPinPath, EntryPinPathSuffix))
		{
			EntryPinName = FromEntryNodePair.Key;
			EntryPinPathSuffix.Reset();
		}
		if (ReroutedInputPins.Contains(EntryPinName))
		{
			URigVMPin* ReroutedPin = ReroutedInputPins.FindChecked(EntryPinName);
			URigVMPin* TargetPin = EntryPinPathSuffix.IsEmpty() ? ReroutedPin : ReroutedPin->FindSubPin(EntryPinPathSuffix);
			check(TargetPin);
			RemappedSourcePinPath = TargetPin->GetPinPath();
		}

		for (const FString& FromEntryNodeTargetPinPath : FromEntryNodePair.Value)
		{
			TArray<URigVMPin*> TargetPins;

			URigVMPin* SourcePin = GetGraph()->FindPin(RemappedSourcePinPath);
			URigVMPin* TargetPin = GetGraph()->FindPin(FromEntryNodeTargetPinPath);

			// potentially the target pin was on the entry node,
			// so there's no node been added for it. we'll have to look into the remapped
			// pins for the "FromLibraryNode" map.
			if(TargetPin == nullptr)
			{
				FString RemappedTargetPinPath = FromEntryNodeTargetPinPath;
				FString ReturnNodeName, ReturnPinPath;
				if (URigVMPin::SplitPinPathAtStart(RemappedTargetPinPath, ReturnNodeName, ReturnPinPath))
				{
					if(Cast<URigVMFunctionReturnNode>(InNode->GetContainedGraph()->FindNode(ReturnNodeName)))
					{
						if(FromLibraryNode.Contains(ReturnPinPath))
						{
							const TArray<FString>& FromLibraryNodeTargetPins = FromLibraryNode.FindChecked(ReturnPinPath);
							for(const FString& FromLibraryNodeTargetPin : FromLibraryNodeTargetPins)
							{
								if(URigVMPin* MappedTargetPin = GetGraph()->FindPin(FromLibraryNodeTargetPin))
								{
									TargetPins.Add(MappedTargetPin);
								}
							}
						}
					}
				}
			}
			else
			{
				TargetPins.Add(TargetPin);
			}
			
			if (SourcePin)
			{
				for(URigVMPin* EachTargetPin : TargetPins)
				{
					if (!SourcePin->IsLinkedTo(EachTargetPin))
					{
						AddLink(SourcePin, EachTargetPin, false);
					}
				}
			}
		}
	}

	// j) re-establish all of the links going from the right of the library node
	//    in this pass we only check pins which have a reroute
	for (const TPair<FString, TArray<FString>>& ToReturnNodePair : ToReturnNode)
	{
		FString LibraryNodePinName, LibraryNodePinPathSuffix;
		if (!URigVMPin::SplitPinPathAtStart(ToReturnNodePair.Key, LibraryNodePinName, LibraryNodePinPathSuffix))
		{
			LibraryNodePinName = ToReturnNodePair.Key;
		}

		if (!ReroutedOutputPins.Contains(LibraryNodePinName))
		{
			continue;
		}

		URigVMPin* ReroutedPin = ReroutedOutputPins.FindChecked(LibraryNodePinName);
		URigVMPin* TargetPin = LibraryNodePinPathSuffix.IsEmpty() ? ReroutedPin : ReroutedPin->FindSubPin(LibraryNodePinPathSuffix);
		check(TargetPin);

		for (const FString& SourcePinpath : ToReturnNodePair.Value)
		{
			URigVMPin* SourcePin = GetGraph()->FindPin(*SourcePinpath);
			if (SourcePin && TargetPin)
			{
				if (!SourcePin->IsLinkedTo(TargetPin))
				{
					AddLink(SourcePin, TargetPin, false);
				}
			}
		}
	}

	// k) re-establish all of the links going from the right of the library node
	for (const TPair<FString, TArray<FString>>& FromLibraryNodePair : FromLibraryNode)
	{
		FString FromLibraryNodePinPath = FromLibraryNodePair.Key;
		FString FromLibraryNodePinPathSuffix;

		const FString* RemappedSourcePin = RemappedSourcePinsForOutputs.Find(FromLibraryNodePinPath);
		while (RemappedSourcePin == nullptr)
		{
			FString LastSegment;
			if (!URigVMPin::SplitPinPathAtEnd(FromLibraryNodePinPath, FromLibraryNodePinPath, LastSegment))
			{
				break;
			}

			if (FromLibraryNodePinPathSuffix.IsEmpty())
			{
				FromLibraryNodePinPathSuffix = LastSegment;
			}
			else
			{
				FromLibraryNodePinPathSuffix = URigVMPin::JoinPinPath(LastSegment, FromLibraryNodePinPathSuffix);
			}

			RemappedSourcePin = RemappedSourcePinsForOutputs.Find(FromLibraryNodePinPath);
		}

		if (RemappedSourcePin == nullptr)
		{
			continue;
		}

		FString RemappedSourcePinPath = *RemappedSourcePin;
		if (!FromLibraryNodePinPathSuffix.IsEmpty())
		{
			RemappedSourcePinPath = URigVMPin::JoinPinPath(RemappedSourcePinPath, FromLibraryNodePinPathSuffix);
		}

		// remap the top level pin in case we need to insert a reroute
		FString ReturnPinName, ReturnPinPathSuffix;
		if (!URigVMPin::SplitPinPathAtStart(FromLibraryNodePair.Key, ReturnPinName, ReturnPinPathSuffix))
		{
			ReturnPinName = FromLibraryNodePair.Key;
			ReturnPinPathSuffix.Reset();
		}
		if (ReroutedOutputPins.Contains(ReturnPinName))
		{
			URigVMPin* ReroutedPin = ReroutedOutputPins.FindChecked(ReturnPinName);
			URigVMPin* SourcePin = ReturnPinPathSuffix.IsEmpty() ? ReroutedPin : ReroutedPin->FindSubPin(ReturnPinPathSuffix);
			check(SourcePin);
			RemappedSourcePinPath = SourcePin->GetPinPath();
		}

		for (const FString& FromLibraryNodeTargetPinPath : FromLibraryNodePair.Value)
		{
			URigVMPin* SourcePin = GetGraph()->FindPin(*RemappedSourcePinPath);
			URigVMPin* TargetPin = GetGraph()->FindPin(FromLibraryNodeTargetPinPath);
			if (SourcePin && TargetPin)
			{
				if (!SourcePin->IsLinkedTo(TargetPin))
				{
					AddLink(SourcePin, TargetPin, false);
				}
			}
		}
	}

	// l) remove the library node from the graph
	if (bSetupUndoRedo)
	{
		ExpandAction.LibraryNodePath = InNode->GetName();
	}
	RemoveNode(InNode, false, true);

	if (bSetupUndoRedo)
	{
		for (URigVMNode* ExpandedNode : ExpandedNodes)
		{
			ExpandAction.ExpandedNodePaths.Add(ExpandedNode->GetName());
		}
		ActionStack->EndAction(ExpandAction);
	}

	return ExpandedNodes;
}

FName URigVMController::PromoteCollapseNodeToFunctionReferenceNode(const FName& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand, const FString& InExistingFunctionDefinitionPath)
{
	if (!IsValidGraph())
	{
		return NAME_None;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMNode* Result = PromoteCollapseNodeToFunctionReferenceNode(Cast<URigVMCollapseNode>(Graph->FindNodeByName(InNodeName)), bSetupUndoRedo, InExistingFunctionDefinitionPath);
	if (Result)
	{
		if (bPrintPythonCommand)
		{
			const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
			
			RigVMPythonUtils::Print(GetGraphOuterName(), 
								FString::Printf(TEXT("blueprint.get_controller_by_name('%s').promote_collapse_node_to_function_reference_node('%s')"),
													*GraphName,
													*GetSanitizedNodeName(InNodeName.ToString())));
		}
		
		return Result->GetFName();
	}
	return NAME_None;
}

FName URigVMController::PromoteFunctionReferenceNodeToCollapseNode(const FName& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand, bool bRemoveFunctionDefinition)
{
	if (!IsValidGraph())
	{
		return NAME_None;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMNode* Result = PromoteFunctionReferenceNodeToCollapseNode(Cast<URigVMFunctionReferenceNode>(Graph->FindNodeByName(InNodeName)), bSetupUndoRedo, bRemoveFunctionDefinition);
	if (Result)
	{
		return Result->GetFName();
	}
	return NAME_None;
}

URigVMFunctionReferenceNode* URigVMController::PromoteCollapseNodeToFunctionReferenceNode(URigVMCollapseNode* InCollapseNode, bool bSetupUndoRedo, const FString& InExistingFunctionDefinitionPath)
{
	if (!IsValidNodeForGraph(InCollapseNode))
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMFunctionLibrary* FunctionLibrary = Graph->GetDefaultFunctionLibrary();
	if (FunctionLibrary == nullptr)
	{
		return nullptr;
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	URigVMFunctionReferenceNode* FunctionRefNode = nullptr;

	// Create Function
	URigVMLibraryNode* FunctionDefinition = nullptr;
	if (!InExistingFunctionDefinitionPath.IsEmpty())
	{
		FunctionDefinition = FindObject<URigVMLibraryNode>(ANY_PACKAGE, *InExistingFunctionDefinitionPath);
	}

	if (FunctionDefinition == nullptr)
	{
		{
			FRigVMControllerGraphGuard GraphGuard(this, FunctionLibrary, false);
			const FString FunctionName = GetValidNodeName(InCollapseNode->GetName());
			FunctionDefinition = AddFunctionToLibrary(*FunctionName, InCollapseNode->IsMutable(), FVector2D::ZeroVector, false);		
		}
	
		// Add interface pins in function
		if (FunctionDefinition)
		{
			FRigVMControllerGraphGuard GraphGuard(this, FunctionDefinition->GetContainedGraph(), false);
			for(const URigVMPin* Pin : InCollapseNode->GetPins())
			{
				AddExposedPin(Pin->GetFName(), Pin->GetDirection(), Pin->GetCPPType(), (Pin->GetCPPTypeObject() ? *Pin->GetCPPTypeObject()->GetPathName() : TEXT("")), Pin->GetDefaultValue(), false);
			}
		}
	}

	// Copy inner graph from collapsed node to function
	if (FunctionDefinition)
	{
		FString TextContent;
		{
			FRigVMControllerGraphGuard GraphGuard(this, InCollapseNode->GetContainedGraph(), false);
			TArray<FName> NodeNames;
			for (const URigVMNode* Node : InCollapseNode->GetContainedNodes())
			{
				if (Node->IsInjected())
				{
					continue;
				}
				
				NodeNames.Add(Node->GetFName());
			}
			TextContent = ExportNodesToText(NodeNames);
		}
		{
			FRigVMControllerGraphGuard GraphGuard(this, FunctionDefinition->GetContainedGraph(), false);
			ImportNodesFromText(TextContent, false);
			if (FunctionDefinition->GetContainedGraph()->GetEntryNode() && InCollapseNode->GetContainedGraph()->GetEntryNode())
			{ 
				SetNodePosition(FunctionDefinition->GetContainedGraph()->GetEntryNode(), InCollapseNode->GetContainedGraph()->GetEntryNode()->GetPosition(), false);
			}
			
			if (FunctionDefinition->GetContainedGraph()->GetReturnNode() && InCollapseNode->GetContainedGraph()->GetReturnNode())
			{ 
				SetNodePosition(FunctionDefinition->GetContainedGraph()->GetReturnNode(), InCollapseNode->GetContainedGraph()->GetReturnNode()->GetPosition(), false);
			}

			for (const URigVMLink* InnerLink : InCollapseNode->GetContainedGraph()->GetLinks())
			{
				URigVMPin* SourcePin = InCollapseNode->GetGraph()->FindPin(InnerLink->SourcePinPath);
				URigVMPin* TargetPin = InCollapseNode->GetGraph()->FindPin(InnerLink->TargetPinPath);
				if (SourcePin && TargetPin)
				{
					if (!SourcePin->IsLinkedTo(TargetPin))
					{
						AddLink(InnerLink->SourcePinPath, InnerLink->TargetPinPath, false);	
					}
				}				
			}
		}
	}

	// Remove collapse node, add function reference, and add external links
	if (FunctionDefinition)
	{
		FString NodeName = InCollapseNode->GetName();
		FVector2D NodePosition = InCollapseNode->GetPosition();
		TMap<FString, FPinState> PinStates = GetPinStates(InCollapseNode);

		TArray<URigVMLink*> Links = InCollapseNode->GetLinks();
		TArray< TPair< FString, FString > > LinkPaths;
		for (URigVMLink* Link : Links)
		{
			LinkPaths.Add(TPair< FString, FString >(Link->GetSourcePin()->GetPinPath(), Link->GetTargetPin()->GetPinPath()));
		}

		RemoveNode(InCollapseNode, false, true);

		FunctionRefNode = AddFunctionReferenceNode(FunctionDefinition, NodePosition, NodeName, false);

		if (FunctionRefNode)
		{
			ApplyPinStates(FunctionRefNode, PinStates);
			for (const TPair<FString, FString>& LinkPath : LinkPaths)
			{
				AddLink(LinkPath.Key, LinkPath.Value, false);
			}
		}

		if (bSetupUndoRedo)
		{
			ActionStack->AddAction(FRigVMPromoteNodeAction(InCollapseNode, NodeName, FString()));
		}
	}

	return FunctionRefNode;
}

URigVMCollapseNode* URigVMController::PromoteFunctionReferenceNodeToCollapseNode(URigVMFunctionReferenceNode* InFunctionRefNode, bool bSetupUndoRedo, bool bRemoveFunctionDefinition)
{
	if (!IsValidNodeForGraph(InFunctionRefNode))
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMCollapseNode* FunctionDefinition = Cast<URigVMCollapseNode>(InFunctionRefNode->GetReferencedNode());
	if (FunctionDefinition == nullptr)
	{
		return nullptr;
	}

	// Find local variables that need to be added as member variables. If member variables of same name and type already
	// exist, they will be reused. If a local variable is not used, it will not be created.
	TArray<FRigVMGraphVariableDescription> LocalVariables = FunctionDefinition->GetContainedGraph()->LocalVariables;
	TArray<FRigVMExternalVariable> CurrentVariables = GetAllVariables();
	TArray<FRigVMGraphVariableDescription> VariablesToAdd;
	for (const URigVMNode* Node : FunctionDefinition->GetContainedGraph()->GetNodes())
	{
		if (const URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
		{
			for (FRigVMGraphVariableDescription& LocalVariable : LocalVariables)
			{
				if (LocalVariable.Name == VariableNode->GetVariableName())
				{
					bool bVariableExists = false;
					bool bVariableIncompatible = false;
					FRigVMExternalVariable LocalVariableExternalType = LocalVariable.ToExternalVariable();
					for (FRigVMExternalVariable& CurrentVariable : CurrentVariables)
					{						
						if (CurrentVariable.Name == LocalVariable.Name)
						{
							if (CurrentVariable.TypeName != LocalVariableExternalType.TypeName ||
								CurrentVariable.TypeObject != LocalVariableExternalType.TypeObject ||
								CurrentVariable.bIsArray != LocalVariableExternalType.bIsArray)
							{
								bVariableIncompatible = true;	
							}
							bVariableExists = true;
							break;
						}
					}

					if (!bVariableExists)
					{
						VariablesToAdd.Add(LocalVariable);	
					}
					else if(bVariableIncompatible)
					{
						ReportErrorf(TEXT("Found variable %s of incompatible type with a local variable inside function %s"), *LocalVariable.Name.ToString(), *FunctionDefinition->GetName());
						return nullptr;
					}
					break;
				}
			}
		}
	}

	FRigVMControllerCompileBracketScope CompileScope(this);

	FString NodeName = InFunctionRefNode->GetName();
	FVector2D NodePosition = InFunctionRefNode->GetPosition();
	TMap<FString, FPinState> PinStates = GetPinStates(InFunctionRefNode);

	TArray<URigVMLink*> Links = InFunctionRefNode->GetLinks();
	TArray< TPair< FString, FString > > LinkPaths;
	for (URigVMLink* Link : Links)
	{
		LinkPaths.Add(TPair< FString, FString >(Link->GetSourcePin()->GetPinPath(), Link->GetTargetPin()->GetPinPath()));
	}

	RemoveNode(InFunctionRefNode, false, true);

	if (RequestNewExternalVariableDelegate.IsBound())
	{
		for (const FRigVMGraphVariableDescription& OldVariable : VariablesToAdd)
		{
			RequestNewExternalVariableDelegate.Execute(OldVariable, false, false);
		}
	}

	URigVMCollapseNode* CollapseNode = DuplicateObject<URigVMCollapseNode>(FunctionDefinition, Graph, *NodeName);
	if(CollapseNode)
	{
		{
			FRigVMControllerGraphGuard Guard(this, CollapseNode->GetContainedGraph(), false);
			ReattachLinksToPinObjects();

			for (URigVMNode* Node : CollapseNode->GetContainedGraph()->GetNodes())
			{
				if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
				{
					TArray<URigVMLink*> VariableLinks = VariableNode->GetLinks();
					DetachLinksFromPinObjects(&VariableLinks);
					RepopulatePinsOnNode(VariableNode);
					ReattachLinksToPinObjects(false, &VariableLinks);
				}
			}

			CollapseNode->GetContainedGraph()->LocalVariables.Empty();
		}		
				
		CollapseNode->NodeColor = FLinearColor::White;
		CollapseNode->Position = NodePosition;
		Graph->Nodes.Add(CollapseNode);
		Notify(ERigVMGraphNotifType::NodeAdded, CollapseNode);

		ApplyPinStates(CollapseNode, PinStates);
		for (const TPair<FString, FString>& LinkPath : LinkPaths)
		{
			AddLink(LinkPath.Key, LinkPath.Value, false);
		}
	}

	if (bSetupUndoRedo)
	{
		ActionStack->AddAction(FRigVMPromoteNodeAction(InFunctionRefNode, NodeName, FunctionDefinition->GetPathName()));
	}

	if(bRemoveFunctionDefinition)
	{
		FRigVMControllerGraphGuard Guard(this, FunctionDefinition->GetRootGraph(), false);
		RemoveFunctionFromLibrary(FunctionDefinition->GetFName(), false);
	}

	return CollapseNode;
}

void URigVMController::SetReferencedFunction(URigVMFunctionReferenceNode* InFunctionRefNode, URigVMLibraryNode* InNewReferencedNode, bool bSetupUndoRedo)
{
	if(!IsValidGraph())
	{
		return;
	}
	
	URigVMLibraryNode* ReferencedNode = InFunctionRefNode->GetReferencedNode();
	
	if(URigVMFunctionLibrary* OtherLibrary = Cast<URigVMFunctionLibrary>(ReferencedNode->GetOuter()))
	{
		if(FRigVMFunctionReferenceArray* OtherReferences = OtherLibrary->FunctionReferences.Find(ReferencedNode))
		{
			OtherReferences->FunctionReferences.Remove(InFunctionRefNode);
		}
	}

	URigVMFunctionLibrary* NewLibrary = Cast<URigVMFunctionLibrary>(InNewReferencedNode->GetOuter());
	FRigVMFunctionReferenceArray& NewReferences = NewLibrary->FunctionReferences.FindOrAdd(InNewReferencedNode);
	NewReferences.FunctionReferences.Add(InFunctionRefNode);
	NewLibrary->MarkPackageDirty();

	InFunctionRefNode->SetReferencedNode(InNewReferencedNode);
	
	FRigVMControllerGraphGuard GraphGuard(this, InFunctionRefNode->GetGraph(), false);
	GetGraph()->Notify(ERigVMGraphNotifType::NodeReferenceChanged, InFunctionRefNode);
}

void URigVMController::RefreshFunctionPins(URigVMNode* InNode, bool bNotify)
{
	if (InNode == nullptr)
	{
		return;
	}

	URigVMFunctionEntryNode* EntryNode = Cast<URigVMFunctionEntryNode>(InNode);
	URigVMFunctionReturnNode* ReturnNode = Cast<URigVMFunctionReturnNode>(InNode);

	if (EntryNode || ReturnNode)
	{
		TArray<URigVMLink*> Links = InNode->GetLinks();
		DetachLinksFromPinObjects(&Links, bNotify);
		RepopulatePinsOnNode(InNode, false, bNotify);
		ReattachLinksToPinObjects(false, &Links, bNotify);
	}
}

bool URigVMController::RemoveNode(URigVMNode* InNode, bool bSetupUndoRedo, bool bRecursive, bool bPrintPythonCommand, bool bRelinkPins)
{
	if (!IsValidNodeForGraph(InNode))
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (bSetupUndoRedo)
	{
		// don't allow deletion of function entry / return nodes
		if ((Cast<URigVMFunctionEntryNode>(InNode) != nullptr && InNode->GetName() == TEXT("Entry")) ||
			(Cast<URigVMFunctionReturnNode>(InNode) != nullptr && InNode->GetName() == TEXT("Return")))
		{
			// due to earlier bugs in the copy & paste code entry and return nodes could end up in
			// root graphs - in those cases we allow deletion
			if(!Graph->IsRootGraph())
			{
				return false;
			}
		}

		// check if the operation will cause to dirty assets
		if(URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(InNode))
		{
			if(URigVMFunctionLibrary* OuterLibrary = Graph->GetTypedOuter<URigVMFunctionLibrary>())
			{
				if(URigVMLibraryNode* OuterFunction = OuterLibrary->FindFunctionForNode(Graph->GetTypedOuter<URigVMCollapseNode>()))
				{
					const FName VariableToRemove = VariableNode->GetVariableName();
					bool bIsLocalVariable = false;
					for (FRigVMGraphVariableDescription VariableDescription : OuterFunction->GetContainedGraph()->LocalVariables)
					{
						if (VariableDescription.Name == VariableToRemove)
						{
							bIsLocalVariable = true;
							break;
						}
					}

					if (!bIsLocalVariable)
					{
						TArray<FRigVMExternalVariable> ExternalVariablesWithoutVariableNode;
						{
							URigVMGraph* EditedGraph = InNode->GetGraph();
							TGuardValue<TArray<URigVMNode*>> TemporaryRemoveNodes(EditedGraph->Nodes, TArray<URigVMNode*>());
							ExternalVariablesWithoutVariableNode = EditedGraph->GetExternalVariables();
						}

						bool bFoundExternalVariable = false;
						for(const FRigVMExternalVariable& ExternalVariable : ExternalVariablesWithoutVariableNode)
						{
							if(ExternalVariable.Name == VariableToRemove)
							{
								bFoundExternalVariable = true;
								break;
							}
						}

						if(!bFoundExternalVariable)
						{
							FRigVMControllerGraphGuard Guard(this, OuterFunction->GetContainedGraph(), false);
							if(RequestBulkEditDialogDelegate.IsBound())
							{
								FRigVMController_BulkEditResult Result = RequestBulkEditDialogDelegate.Execute(OuterFunction, ERigVMControllerBulkEditType::RemoveVariable);
								if(Result.bCanceled)
								{
									return false;
								}
								bSetupUndoRedo = Result.bSetupUndoRedo;
							}
						}
					}
				}
			}
		}
	}

	TGuardValue<bool> GuardCompactness(bIgnoreRerouteCompactnessChanges, true);

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMBaseAction Action;
	if (bSetupUndoRedo)
	{
		Action = FRigVMBaseAction();
		Action.Title = FString::Printf(TEXT("Remove %s Node"), *InNode->GetNodeTitle());
		ActionStack->BeginAction(Action);
	}

	if (URigVMInjectionInfo* InjectionInfo = InNode->GetInjectionInfo())
	{
		URigVMPin* Pin = InjectionInfo->GetPin();
		check(Pin);

		Pin->InjectionInfos.Remove(InjectionInfo);

		if (InjectionInfo->bInjectedAsInput)
		{
			if (InjectionInfo->InputPin)
			{
				URigVMPin* LastInputPin = Pin;
				RewireLinks(InjectionInfo->InputPin, LastInputPin, true, false);
			}
		}
		else
		{
			if (InjectionInfo->OutputPin)
			{
				URigVMPin* LastOutputPin = Pin;
				RewireLinks(InjectionInfo->OutputPin, LastOutputPin, false, false);
			}
		}
	}

	
	if (URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(InNode))
	{
		// If we are removing a reference, remove the function references to this node in the function library
		if(URigVMFunctionReferenceNode* FunctionReferenceNode = Cast<URigVMFunctionReferenceNode>(LibraryNode))
		{
			if (URigVMFunctionLibrary* FunctionLibrary = Cast<URigVMFunctionLibrary>(FunctionReferenceNode->GetLibrary()))
			{
				FRigVMFunctionReferenceArray* References = FunctionLibrary->FunctionReferences.Find(FunctionReferenceNode->GetReferencedNode());
				if (References)
				{
					References->FunctionReferences.RemoveAll(
						[FunctionReferenceNode](TSoftObjectPtr<URigVMFunctionReferenceNode>& FunctionReferencePtr) {

							if (!FunctionReferencePtr.IsValid())
							{
								FunctionReferencePtr.LoadSynchronous();
							}
							if (!FunctionReferencePtr.IsValid())
							{
								return true;
							}
							return FunctionReferencePtr.Get() == FunctionReferenceNode;
					});
				}
			}
		}
		// If we are removing a function, remove all the references first
		else if (URigVMFunctionLibrary* FunctionLibrary = Cast<URigVMFunctionLibrary>(LibraryNode->GetGraph()))
		{
			const FRigVMFunctionReferenceArray* FunctionReferencesPtr = FunctionLibrary->FunctionReferences.Find(LibraryNode);
			if (FunctionReferencesPtr)
			{
				TArray< TSoftObjectPtr<URigVMFunctionReferenceNode> > FunctionReferences = FunctionReferencesPtr->FunctionReferences;
				for (const TSoftObjectPtr<URigVMFunctionReferenceNode>& FunctionReferencePtr : FunctionReferences)
				{
					if (!FunctionReferencesPtr->FunctionReferences.Contains(FunctionReferencePtr))
					{
						continue;
					}
					
					if (FunctionReferencePtr.IsValid())
					{
						{
							FRigVMControllerGraphGuard GraphGuard(this, FunctionReferencePtr->GetGraph(), bSetupUndoRedo);
							RemoveNode(FunctionReferencePtr.Get());

							TGuardValue<TSoftObjectPtr<URigVMLibraryNode>> ClearReferencedNodePtr(
								FunctionReferencePtr->ReferencedNodePtr,
								TSoftObjectPtr<URigVMLibraryNode>());
						}
						FunctionReferencePtr->ReferencedNodePtr.ResetWeakPtr();
					}
				}
			}
			FunctionLibrary->FunctionReferences.Remove(LibraryNode);

			for(const auto& Pair : FunctionLibrary->LocalizedFunctions)
			{
				if(Pair.Value == LibraryNode)
				{
					FunctionLibrary->LocalizedFunctions.Remove(Pair.Key);
					break;
				}
			}
		}
	}

	// try to reconnect source and target nodes based on the current links
	if (bRelinkPins)
	{
		RelinkSourceAndTargetPins(InNode, bSetupUndoRedo);
	}
	
	if (bSetupUndoRedo || bRecursive)
	{
		SelectNode(InNode, false, bSetupUndoRedo);

		for (URigVMPin* Pin : InNode->GetPins())
		{
			TArray<URigVMInjectionInfo*> InjectedNodes = Pin->GetInjectedNodes();
			for (URigVMInjectionInfo* InjectedNode : InjectedNodes)
			{
				RemoveNode(InjectedNode->Node, bSetupUndoRedo, bRecursive);
			}

			BreakAllLinks(Pin, true, bSetupUndoRedo);
			BreakAllLinks(Pin, false, bSetupUndoRedo);
			BreakAllLinksRecursive(Pin, true, false, bSetupUndoRedo);
			BreakAllLinksRecursive(Pin, false, false, bSetupUndoRedo);
		}

		if (URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(InNode))
		{
			URigVMGraph* SubGraph = CollapseNode->GetContainedGraph();
			FRigVMControllerGraphGuard GraphGuard(this, SubGraph, bSetupUndoRedo);
		
			TArray<URigVMNode*> ContainedNodes = SubGraph->GetNodes();
			for (URigVMNode* ContainedNode : ContainedNodes)
			{
				if(Cast<URigVMFunctionEntryNode>(ContainedNode) != nullptr ||
					Cast<URigVMFunctionReturnNode>(ContainedNode) != nullptr)
				{
					continue;
				}
				RemoveNode(ContainedNode, bSetupUndoRedo, bRecursive);
			}
		}
		
		if (bSetupUndoRedo)
		{
			ActionStack->AddAction(FRigVMRemoveNodeAction(InNode, this));
		}
	}

	Graph->Nodes.Remove(InNode);
	if (!bSuspendNotifications)
	{
		Graph->MarkPackageDirty();
	}

	Notify(ERigVMGraphNotifType::NodeRemoved, InNode);

	

	if (bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());

		if (Graph->IsA<URigVMFunctionLibrary>())
		{
			const FString NodeName = GetSanitizedNodeName(InNode->GetName());
			
			RigVMPythonUtils::Print(GetGraphOuterName(), 
								FString::Printf(TEXT("library_controller.remove_function_from_library('%s')"),
												*NodeName));
		}
		else
		{
			const FString NodePath = GetSanitizedPinPath(InNode->GetNodePath());

			FString PythonCmd = FString::Printf(TEXT("blueprint.get_controller_by_name('%s')."), *GraphName );
			PythonCmd += bRelinkPins ? FString::Printf(TEXT("remove_node_by_name('%s', relink_pins=True)"), *NodePath) :
									   FString::Printf(TEXT("remove_node_by_name('%s')"), *NodePath);
			
			RigVMPythonUtils::Print(GetGraphOuterName(), PythonCmd );
		}
	}

	if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(InNode))
	{
		Notify(ERigVMGraphNotifType::VariableRemoved, VariableNode);
	}
	if (URigVMParameterNode* ParameterNode = Cast<URigVMParameterNode>(InNode))
	{
		Notify(ERigVMGraphNotifType::ParameterRemoved, ParameterNode);
	}

	if (URigVMInjectionInfo* InjectionInfo = InNode->GetInjectionInfo())
	{
		DestroyObject(InjectionInfo);
	}

	DestroyObject(InNode);

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	return true;
}

bool URigVMController::RemoveNodeByName(const FName& InNodeName, bool bSetupUndoRedo, bool bRecursive, bool bPrintPythonCommand, bool bRelinkPins)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	return RemoveNode(Graph->FindNodeByName(InNodeName), bSetupUndoRedo, bRecursive, bPrintPythonCommand, bRelinkPins);
}

bool URigVMController::RenameNode(URigVMNode* InNode, const FName& InNewName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidNodeForGraph(InNode))
	{
		return false;
	}

	FName ValidNewName = *GetValidNodeName(InNewName.ToString());
	if (InNode->GetFName() == ValidNewName)
	{
		return false;
	}

	const FString OldName = InNode->GetName();
	FRigVMRenameNodeAction Action;
	if (bSetupUndoRedo)
	{
		Action = FRigVMRenameNodeAction(InNode->GetFName(), ValidNewName);
		ActionStack->BeginAction(Action);
	}

	// loop over all links and remove them
	TArray<URigVMLink*> Links = InNode->GetLinks();
	for (URigVMLink* Link : Links)
	{
		Link->PrepareForCopy();
		Notify(ERigVMGraphNotifType::LinkRemoved, Link);
	}

	InNode->PreviousName = InNode->GetFName();
	if (!RenameObject(InNode, *ValidNewName.ToString()))
	{
		ActionStack->CancelAction(Action);
		return false;
	}

	Notify(ERigVMGraphNotifType::NodeRenamed, InNode);

	// update the links once more
	for (URigVMLink* Link : Links)
	{
		Link->PrepareForCopy();
		Notify(ERigVMGraphNotifType::LinkAdded, Link);
	}

	if(URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(InNode))
	{
		if (URigVMFunctionLibrary* FunctionLibrary = Cast<URigVMFunctionLibrary>(LibraryNode->GetGraph()))
		{
			FunctionLibrary->ForEachReference(LibraryNode->GetFName(), [this, InNewName](URigVMFunctionReferenceNode* ReferenceNode)
			{
				FRigVMControllerGraphGuard GraphGuard(this, ReferenceNode->GetGraph(), false);
                RenameNode(ReferenceNode, InNewName, false);
			});
		}
	}

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	if (GetGraph()->IsA<URigVMFunctionLibrary>())
	{
		RigVMPythonUtils::Print(GetGraphOuterName(), 
							FString::Printf(TEXT("library_controller.rename_function('%s', '%s')"),
										*OldName,
										*InNewName.ToString()));
	}

	return true;
}

bool URigVMController::SelectNode(URigVMNode* InNode, bool bSelect, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidNodeForGraph(InNode))
	{
		return false;
	}

	if (InNode->IsSelected() == bSelect)
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	TArray<FName> NewSelection = Graph->GetSelectNodes();
	if (bSelect)
	{
		NewSelection.AddUnique(InNode->GetFName());
	}
	else
	{
		NewSelection.Remove(InNode->GetFName());
	}

	return SetNodeSelection(NewSelection, bSetupUndoRedo, bPrintPythonCommand);
}

bool URigVMController::SelectNodeByName(const FName& InNodeName, bool bSelect, bool bSetupUndoRedo)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	return SelectNode(Graph->FindNodeByName(InNodeName), bSelect, bSetupUndoRedo);
}

bool URigVMController::ClearNodeSelection(bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return false;
	}

	return SetNodeSelection(TArray<FName>(), bSetupUndoRedo, bPrintPythonCommand);
}

bool URigVMController::SetNodeSelection(const TArray<FName>& InNodeNames, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	FRigVMSetNodeSelectionAction Action;
	if (bSetupUndoRedo)
	{
		Action = FRigVMSetNodeSelectionAction(Graph, InNodeNames);
		ActionStack->BeginAction(Action);
	}

	bool bSelectedSomething = false;

	TArray<FName> PreviousSelection = Graph->GetSelectNodes();
	for (const FName& PreviouslySelectedNode : PreviousSelection)
	{
		if (!InNodeNames.Contains(PreviouslySelectedNode))
		{
			if(Graph->SelectedNodes.Remove(PreviouslySelectedNode) > 0)
			{
				Notify(ERigVMGraphNotifType::NodeDeselected, Graph->FindNodeByName(PreviouslySelectedNode));
				bSelectedSomething = true;
			}
		}
	}

	for (const FName& InNodeName : InNodeNames)
	{
		if (URigVMNode* NodeToSelect = Graph->FindNodeByName(InNodeName))
		{
			int32 PreviousNum = Graph->SelectedNodes.Num();
			Graph->SelectedNodes.AddUnique(InNodeName);
			if (PreviousNum != Graph->SelectedNodes.Num())
			{
				Notify(ERigVMGraphNotifType::NodeSelected, NodeToSelect);
				bSelectedSomething = true;
			}
		}
	}

	if (bSetupUndoRedo)
	{
		if (bSelectedSomething)
		{
			const TArray<FName>& SelectedNodes = Graph->GetSelectNodes();
			if (SelectedNodes.Num() == 0)
			{
				Action.Title = TEXT("Deselect all nodes.");
			}
			else
			{
				if (SelectedNodes.Num() == 1)
				{
					Action.Title = FString::Printf(TEXT("Selected node '%s'."), *SelectedNodes[0].ToString());
				}
				else
				{
					Action.Title = TEXT("Selected multiple nodes.");
				}
			}
			ActionStack->EndAction(Action);
		}
		else
		{
			ActionStack->CancelAction(Action);
		}
	}

	if (bSelectedSomething)
	{
		Notify(ERigVMGraphNotifType::NodeSelectionChanged, nullptr);
	}

	if (bPrintPythonCommand)
	{
		FString ArrayStr = TEXT("[");
		for (auto It = InNodeNames.CreateConstIterator(); It; ++It)
		{
			ArrayStr += TEXT("'") + GetSanitizedNodeName(It->ToString()) + TEXT("'");
			if (It.GetIndex() < InNodeNames.Num() - 1)
			{
				ArrayStr += TEXT(", ");
			}
		}
		ArrayStr += TEXT("]");

		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());

		RigVMPythonUtils::Print(GetGraphOuterName(), 
							FString::Printf(TEXT("blueprint.get_controller_by_name('%s').set_node_selection(%s)"),
											*GraphName,
											*ArrayStr));
	}

	return bSelectedSomething;
}

bool URigVMController::SetNodePosition(URigVMNode* InNode, const FVector2D& InPosition, bool bSetupUndoRedo, bool bMergeUndoAction, bool bPrintPythonCommand)
{
	if (!IsValidNodeForGraph(InNode))
	{
		return false;
	}

	if ((InNode->Position - InPosition).IsNearlyZero())
	{
		return false;
	}

	FRigVMSetNodePositionAction Action;
	if (bSetupUndoRedo)
	{
		Action = FRigVMSetNodePositionAction(InNode, InPosition);
		Action.Title = FString::Printf(TEXT("Set Node Position"));
		ActionStack->BeginAction(Action);
	}

	InNode->Position = InPosition;
	Notify(ERigVMGraphNotifType::NodePositionChanged, InNode);

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action, bMergeUndoAction);
	}

	if (bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
		const FString NodePath = GetSanitizedPinPath(InNode->GetNodePath());

		RigVMPythonUtils::Print(GetGraphOuterName(), 
							FString::Printf(TEXT("blueprint.get_controller_by_name('%s').set_node_position_by_name('%s', %s)"),
											*GraphName,
											*NodePath,
											*RigVMPythonUtils::Vector2DToPythonString(InPosition)));
	}

	return true;
}

bool URigVMController::SetNodePositionByName(const FName& InNodeName, const FVector2D& InPosition, bool bSetupUndoRedo, bool bMergeUndoAction, bool
                                             bPrintPythonCommand)
{
	if(!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMNode* Node = Graph->FindNodeByName(InNodeName);
	return SetNodePosition(Node, InPosition, bSetupUndoRedo, bMergeUndoAction, bPrintPythonCommand);
}

bool URigVMController::SetNodeSize(URigVMNode* InNode, const FVector2D& InSize, bool bSetupUndoRedo, bool bMergeUndoAction, bool bPrintPythonCommand)
{
	if (!IsValidNodeForGraph(InNode))
	{
		return false;
	}

	if ((InNode->Size - InSize).IsNearlyZero())
	{
		return false;
	}

	FRigVMSetNodeSizeAction Action;
	if (bSetupUndoRedo)
	{
		Action = FRigVMSetNodeSizeAction(InNode, InSize);
		Action.Title = FString::Printf(TEXT("Set Node Size"));
		ActionStack->BeginAction(Action);
	}

	InNode->Size = InSize;
	Notify(ERigVMGraphNotifType::NodeSizeChanged, InNode);

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action, bMergeUndoAction);
	}

	if (bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
		const FString NodePath = GetSanitizedPinPath(InNode->GetNodePath());

		RigVMPythonUtils::Print(GetGraphOuterName(), 
							FString::Printf(TEXT("blueprint.get_controller_by_name('%s').set_node_size_by_name('%s', %s)"),
											*GraphName,
											*NodePath,
											*RigVMPythonUtils::Vector2DToPythonString(InSize)));
	}

	return true;
}

bool URigVMController::SetNodeSizeByName(const FName& InNodeName, const FVector2D& InSize, bool bSetupUndoRedo, bool bMergeUndoAction, bool bPrintPythonCommand)
{
	if(!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMNode* Node = Graph->FindNodeByName(InNodeName);
	return SetNodeSize(Node, InSize, bSetupUndoRedo, bMergeUndoAction, bPrintPythonCommand);
}

bool URigVMController::SetNodeColor(URigVMNode* InNode, const FLinearColor& InColor, bool bSetupUndoRedo, bool bMergeUndoAction, bool bPrintPythonCommand)
{
	if (!IsValidNodeForGraph(InNode))
	{
		return false;
	}

	if ((FVector4(InNode->NodeColor) - FVector4(InColor)).IsNearlyZero3())
	{
		return false;
	}

	FRigVMSetNodeColorAction Action;
	if (bSetupUndoRedo)
	{
		Action = FRigVMSetNodeColorAction(InNode, InColor);
		Action.Title = FString::Printf(TEXT("Set Node Color"));
		ActionStack->BeginAction(Action);
	}

	InNode->NodeColor = InColor;
	Notify(ERigVMGraphNotifType::NodeColorChanged, InNode);

	if (URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(InNode))
	{
		if (URigVMFunctionLibrary* FunctionLibrary = Cast<URigVMFunctionLibrary>(LibraryNode->GetGraph()))
		{
			FunctionLibrary->ForEachReference(LibraryNode->GetFName(), [this](URigVMFunctionReferenceNode* ReferenceNode)
            {
                FRigVMControllerGraphGuard GraphGuard(this, ReferenceNode->GetGraph(), false);
				Notify(ERigVMGraphNotifType::NodeColorChanged, ReferenceNode);
            });
		}
	}

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action, bMergeUndoAction);
	}

	if (bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
		const FString NodePath = GetSanitizedPinPath(InNode->GetNodePath());

		RigVMPythonUtils::Print(GetGraphOuterName(), 
							FString::Printf(TEXT("blueprint.get_controller_by_name('%s').set_node_color_by_name('%s', %s)"),
											*GraphName,
											*NodePath,
											*RigVMPythonUtils::LinearColorToPythonString(InColor)));
	}

	return true;
}

bool URigVMController::SetNodeColorByName(const FName& InNodeName, const FLinearColor& InColor, bool bSetupUndoRedo, bool bMergeUndoAction)
{
	if(!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMNode* Node = Graph->FindNodeByName(InNodeName);
	return SetNodeColor(Node, InColor, bSetupUndoRedo, bMergeUndoAction);
}

bool URigVMController::SetNodeCategory(URigVMCollapseNode* InNode, const FString& InCategory, bool bSetupUndoRedo, bool bMergeUndoAction, bool bPrintPythonCommand)
{
	if (!IsValidNodeForGraph(InNode))
	{
		return false;
	}

	if (InNode->GetNodeCategory() == InCategory)
	{
		return false;
	}

	FRigVMSetNodeCategoryAction Action;
	if (bSetupUndoRedo)
	{
		Action = FRigVMSetNodeCategoryAction(InNode, InCategory);
		Action.Title = FString::Printf(TEXT("Set Node Category"));
		ActionStack->BeginAction(Action);
	}

	InNode->NodeCategory = InCategory;
	Notify(ERigVMGraphNotifType::NodeCategoryChanged, InNode);

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action, bMergeUndoAction);
	}

	if (bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
		const FString NodePath = GetSanitizedPinPath(InNode->GetNodePath());

		RigVMPythonUtils::Print(GetGraphOuterName(),
			FString::Printf(TEXT("blueprint.get_controller_by_name('%s').set_node_category_by_name('%s', '%s')"),
				*GraphName,
				*NodePath,
				*InCategory));
	}

	return true;
}

bool URigVMController::SetNodeCategoryByName(const FName& InNodeName, const FString& InCategory, bool bSetupUndoRedo, bool bMergeUndoAction)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMCollapseNode* Node = Cast<URigVMCollapseNode>(Graph->FindNodeByName(InNodeName));
	return SetNodeCategory(Node, InCategory, bSetupUndoRedo, bMergeUndoAction);
}

bool URigVMController::SetNodeKeywords(URigVMCollapseNode* InNode, const FString& InKeywords, bool bSetupUndoRedo, bool bMergeUndoAction, bool bPrintPythonCommand)
{
	if (!IsValidNodeForGraph(InNode))
	{
		return false;
	}

	if (InNode->GetNodeKeywords() == InKeywords)
	{
		return false;
	}

	FRigVMSetNodeKeywordsAction Action;
	if (bSetupUndoRedo)
	{
		Action = FRigVMSetNodeKeywordsAction(InNode, InKeywords);
		Action.Title = FString::Printf(TEXT("Set Node Keywords"));
		ActionStack->BeginAction(Action);
	}

	InNode->NodeKeywords = InKeywords;
	Notify(ERigVMGraphNotifType::NodeKeywordsChanged, InNode);

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action, bMergeUndoAction);
	}

	if (bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
		const FString NodePath = GetSanitizedPinPath(InNode->GetNodePath());

		RigVMPythonUtils::Print(GetGraphOuterName(),
			FString::Printf(TEXT("blueprint.get_controller_by_name('%s').set_node_keywords_by_name('%s', '%s')"),
				*GraphName,
				*NodePath,
				*InKeywords));
	}

	return true;
}

bool URigVMController::SetNodeKeywordsByName(const FName& InNodeName, const FString& InKeywords, bool bSetupUndoRedo, bool bMergeUndoAction)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMCollapseNode* Node = Cast<URigVMCollapseNode>(Graph->FindNodeByName(InNodeName));
	return SetNodeKeywords(Node, InKeywords, bSetupUndoRedo, bMergeUndoAction);
}

bool URigVMController::SetNodeDescription(URigVMCollapseNode* InNode, const FString& InDescription, bool bSetupUndoRedo, bool bMergeUndoAction, bool bPrintPythonCommand)
{
	if (!IsValidNodeForGraph(InNode))
	{
		return false;
	}

	if (InNode->GetNodeDescription() == InDescription)
	{
		return false;
	}

	FRigVMSetNodeDescriptionAction Action;
	if (bSetupUndoRedo)
	{
		Action = FRigVMSetNodeDescriptionAction(InNode, InDescription);
		Action.Title = FString::Printf(TEXT("Set Node Description"));
		ActionStack->BeginAction(Action);
	}

	InNode->NodeDescription = InDescription;
	Notify(ERigVMGraphNotifType::NodeDescriptionChanged, InNode);

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action, bMergeUndoAction);
	}

	if (bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
		const FString NodePath = GetSanitizedPinPath(InNode->GetNodePath());

		RigVMPythonUtils::Print(GetGraphOuterName(),
			FString::Printf(TEXT("blueprint.get_controller_by_name('%s').set_node_description_by_name('%s', '%s')"),
				*GraphName,
				*NodePath,
				*InDescription));
	}

	return true;
}

bool URigVMController::SetNodeDescriptionByName(const FName& InNodeName, const FString& InDescription, bool bSetupUndoRedo, bool bMergeUndoAction)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMCollapseNode* Node = Cast<URigVMCollapseNode>(Graph->FindNodeByName(InNodeName));
	return SetNodeDescription(Node, InDescription, bSetupUndoRedo, bMergeUndoAction);
}

bool URigVMController::SetCommentText(URigVMNode* InNode, const FString& InCommentText, const int32& InCommentFontSize, const bool& bInCommentBubbleVisible, const bool& bInCommentColorBubble, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidNodeForGraph(InNode))
	{
		return false;
	}

	if (URigVMCommentNode* CommentNode = Cast<URigVMCommentNode>(InNode))
	{
		if(CommentNode->CommentText == InCommentText && CommentNode->FontSize == InCommentFontSize && CommentNode->bBubbleVisible == bInCommentBubbleVisible && CommentNode->bColorBubble == bInCommentColorBubble)
		{
			return false;
		}

		FRigVMSetCommentTextAction Action;
		if (bSetupUndoRedo)
		{
			Action = FRigVMSetCommentTextAction(CommentNode, InCommentText, InCommentFontSize, bInCommentBubbleVisible, bInCommentColorBubble);
			Action.Title = FString::Printf(TEXT("Set Comment Text"));
			ActionStack->BeginAction(Action);
		}

		CommentNode->CommentText = InCommentText;
		CommentNode->FontSize = InCommentFontSize;
		CommentNode->bBubbleVisible = bInCommentBubbleVisible;
		CommentNode->bColorBubble = bInCommentColorBubble;
		Notify(ERigVMGraphNotifType::CommentTextChanged, InNode);

		if (bSetupUndoRedo)
		{
			ActionStack->EndAction(Action);
		}

		if (bPrintPythonCommand)
		{
			const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
			const FString NodePath = GetSanitizedPinPath(CommentNode->GetNodePath());

			RigVMPythonUtils::Print(GetGraphOuterName(),
				FString::Printf(TEXT("blueprint.get_controller_by_name('%s').set_comment_text_by_name('%s', '%s')"),
				*GraphName,
				*NodePath,
				*InCommentText));
		}

		return true;
	}

	return false;
}

bool URigVMController::SetCommentTextByName(const FName& InNodeName, const FString& InCommentText, const int32& InCommentFontSize, const bool& bInCommentBubbleVisible, const bool& bInCommentColorBubble, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if(!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMNode* Node = Graph->FindNodeByName(InNodeName);
	return SetCommentText(Node, InCommentText, InCommentFontSize, bInCommentBubbleVisible, bInCommentColorBubble, bSetupUndoRedo, bPrintPythonCommand);
}

bool URigVMController::SetRerouteCompactness(URigVMNode* InNode, bool bShowAsFullNode, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidNodeForGraph(InNode))
	{
		return false;
	}

	if (URigVMRerouteNode* RerouteNode = Cast<URigVMRerouteNode>(InNode))
	{
		if (RerouteNode->bShowAsFullNode == bShowAsFullNode)
		{
			return false;
		}

		FRigVMSetRerouteCompactnessAction Action;
		if (bSetupUndoRedo)
		{
			Action = FRigVMSetRerouteCompactnessAction(RerouteNode, bShowAsFullNode);
			Action.Title = FString::Printf(TEXT("Set Reroute Size"));
			ActionStack->BeginAction(Action);
		}

		RerouteNode->bShowAsFullNode = bShowAsFullNode;
		Notify(ERigVMGraphNotifType::RerouteCompactnessChanged, InNode);

		if (bSetupUndoRedo)
		{
			ActionStack->EndAction(Action);
		}

		return true;
	}

	return false;
}

bool URigVMController::SetRerouteCompactnessByName(const FName& InNodeName, bool bShowAsFullNode, bool bSetupUndoRedo)
{
	if(!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMNode* Node = Graph->FindNodeByName(InNodeName);
	return SetRerouteCompactness(Node, bShowAsFullNode, bSetupUndoRedo);
}

bool URigVMController::RenameVariable(const FName& InOldName, const FName& InNewName, bool bSetupUndoRedo)
{
	if(!IsValidGraph())
	{
		return false;
	}

	if (InOldName == InNewName)
	{
		ReportWarning(TEXT("RenameVariable: InOldName and InNewName are equal."));
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	TArray<FRigVMGraphVariableDescription> ExistingVariables = Graph->GetVariableDescriptions();
	for (const FRigVMGraphVariableDescription& ExistingVariable : ExistingVariables)
	{
		if (ExistingVariable.Name == InNewName)
		{
			ReportErrorf(TEXT("Cannot rename variable to '%s' - variable already exists."), *InNewName.ToString());
			return false;
		}
	}

	// If there is a local variable with the old name, a rename of the blueprint member variable does not affect this graph
	for (FRigVMGraphVariableDescription& LocalVariable : Graph->GetLocalVariables(true))
	{
		if (LocalVariable.Name == InOldName)
		{
			return false;
		}
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMRenameVariableAction Action;
	if (bSetupUndoRedo)
	{
		Action = FRigVMRenameVariableAction(InOldName, InNewName);
		Action.Title = FString::Printf(TEXT("Rename Variable"));
		ActionStack->BeginAction(Action);
	}

	TArray<URigVMNode*> RenamedNodes;
	for (URigVMNode* Node : Graph->Nodes)
	{
		if(URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
		{
			if (VariableNode->GetVariableName() == InOldName)
			{
				VariableNode->FindPin(URigVMVariableNode::VariableName)->DefaultValue = InNewName.ToString();
				RenamedNodes.Add(Node);
			}
		}
	}

	for (URigVMNode* RenamedNode : RenamedNodes)
	{
		Notify(ERigVMGraphNotifType::VariableRenamed, RenamedNode);
		if (!bSuspendNotifications)
		{
			Graph->MarkPackageDirty();
		}
	}

	if (bSetupUndoRedo)
	{
		if (RenamedNodes.Num() > 0)
		{
			ActionStack->EndAction(Action);
		}
		else
		{
			ActionStack->CancelAction(Action);
		}
	}

	return RenamedNodes.Num() > 0;
}

bool URigVMController::RenameParameter(const FName& InOldName, const FName& InNewName, bool bSetupUndoRedo)
{
	if(!IsValidGraph())
	{
		return false;
	}

	if (InOldName == InNewName)
	{
		ReportWarning(TEXT("RenameParameter: InOldName and InNewName are equal."));
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	TArray<FRigVMGraphParameterDescription> ExistingParameters = Graph->GetParameterDescriptions();
	for (const FRigVMGraphParameterDescription& ExistingParameter : ExistingParameters)
	{
		if (ExistingParameter.Name == InNewName)
		{
			ReportErrorf(TEXT("Cannot rename parameter to '%s' - parameter already exists."), *InNewName.ToString());
			return false;
		}
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMRenameParameterAction Action;
	if (bSetupUndoRedo)
	{
		Action = FRigVMRenameParameterAction(InOldName, InNewName);
		Action.Title = FString::Printf(TEXT("Rename Parameter"));
		ActionStack->BeginAction(Action);
	}

	TArray<URigVMNode*> RenamedNodes;
	for (URigVMNode* Node : Graph->Nodes)
	{
		if(URigVMParameterNode* ParameterNode = Cast<URigVMParameterNode>(Node))
		{
			if (ParameterNode->GetParameterName() == InOldName)
			{
				ParameterNode->FindPin(URigVMParameterNode::ParameterName)->DefaultValue = InNewName.ToString();
				RenamedNodes.Add(Node);
			}
		}
	}

	for (URigVMNode* RenamedNode : RenamedNodes)
	{
		Notify(ERigVMGraphNotifType::ParameterRenamed, RenamedNode);
		if (!bSuspendNotifications)
		{
			Graph->MarkPackageDirty();
		}
	}

	if (bSetupUndoRedo)
	{
		if (RenamedNodes.Num() > 0)
		{
			ActionStack->EndAction(Action);
		}
		else
		{
			ActionStack->CancelAction(Action);
		}
	}

	return RenamedNodes.Num() > 0;
}

void URigVMController::UpdateRerouteNodeAfterChangingLinks(URigVMPin* PinChanged, bool bSetupUndoRedo)
{
	if (bIgnoreRerouteCompactnessChanges)
	{
		return;
	}

	if (!IsValidGraph())
	{
		return;
	}

	URigVMRerouteNode* Node = Cast<URigVMRerouteNode>(PinChanged->GetNode());
	if (Node == nullptr)
	{
		return;
	}

	int32 NbTotalSources = Node->Pins[0]->GetSourceLinks(true /* recursive */).Num();
	int32 NbTotalTargets = Node->Pins[0]->GetTargetLinks(true /* recursive */).Num();
	int32 NbToplevelSources = Node->Pins[0]->GetSourceLinks(false /* recursive */).Num();
	int32 NbToplevelTargets = Node->Pins[0]->GetTargetLinks(false /* recursive */).Num();

	bool bJustTopLevelConnections = (NbTotalSources == NbToplevelSources) && (NbTotalTargets == NbToplevelTargets);
	bool bOnlyConnectionsOnOneSide = (NbTotalSources == 0) || (NbTotalTargets == 0);
	bool bShowAsFullNode = (!bJustTopLevelConnections) || bOnlyConnectionsOnOneSide;

	SetRerouteCompactness(Node, bShowAsFullNode, bSetupUndoRedo);
}

bool URigVMController::SetPinExpansion(const FString& InPinPath, bool bIsExpanded, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMPin* Pin = Graph->FindPin(InPinPath);
	if (Pin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InPinPath);
		return false;
	}

	const bool bSuccess = SetPinExpansion(Pin, bIsExpanded, bSetupUndoRedo);
	if (bSuccess && bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());

		RigVMPythonUtils::Print(GetGraphOuterName(),
			FString::Printf(TEXT("blueprint.get_controller_by_name('%s').set_pin_expansion('%s', %s)"),
			*GraphName,
			*GetSanitizedPinPath(InPinPath),
			(bIsExpanded) ? TEXT("True") : TEXT("False")));
	}

	return bSuccess;
}

bool URigVMController::SetPinExpansion(URigVMPin* InPin, bool bIsExpanded, bool bSetupUndoRedo)
{
	// If there is nothing to do, just return success
	if (InPin->GetSubPins().Num() == 0 || InPin->IsExpanded() == bIsExpanded)
	{
		return true;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	FRigVMSetPinExpansionAction Action;
	if (bSetupUndoRedo)
	{
		Action = FRigVMSetPinExpansionAction(InPin, bIsExpanded);
		Action.Title = bIsExpanded ? TEXT("Expand Pin") : TEXT("Collapse Pin");
		ActionStack->BeginAction(Action);
	}

	InPin->bIsExpanded = bIsExpanded;

	Notify(ERigVMGraphNotifType::PinExpansionChanged, InPin);
	if (!bSuspendNotifications)
	{
		Graph->MarkPackageDirty();
	}

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	return true;
}

bool URigVMController::SetPinIsWatched(const FString& InPinPath, bool bIsWatched, bool bSetupUndoRedo)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMPin* Pin = Graph->FindPin(InPinPath);
	if (Pin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InPinPath);
		return false;
	}

	return SetPinIsWatched(Pin, bIsWatched, bSetupUndoRedo);
}

bool URigVMController::SetPinIsWatched(URigVMPin* InPin, bool bIsWatched, bool bSetupUndoRedo)
{
	if (!IsValidPinForGraph(InPin))
	{
		return false;
	}

	if (InPin->GetParentPin() != nullptr)
	{
		return false;
	}

	if (InPin->RequiresWatch() == bIsWatched)
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot watch pins in function library graphs."));
		return false;
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMSetPinWatchAction Action;
	if (bSetupUndoRedo)
	{
		Action = FRigVMSetPinWatchAction(InPin, bIsWatched);
		Action.Title = bIsWatched ? TEXT("Watch Pin") : TEXT("Unwatch Pin");
		ActionStack->BeginAction(Action);
	}

	InPin->bRequiresWatch = bIsWatched;

	Notify(ERigVMGraphNotifType::PinWatchedChanged, InPin);
	if (!bSuspendNotifications)
	{
		Graph->MarkPackageDirty();
	}

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	return true;
}

FString URigVMController::GetPinDefaultValue(const FString& InPinPath)
{
	if (!IsValidGraph())
	{
		return FString();
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMPin* Pin = Graph->FindPin(InPinPath);
	if (Pin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InPinPath);
		return FString();
	}
	Pin = Pin->GetPinForLink();

	return Pin->GetDefaultValue();
}

bool URigVMController::SetPinDefaultValue(const FString& InPinPath, const FString& InDefaultValue, bool bResizeArrays, bool bSetupUndoRedo, bool bMergeUndoAction, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMPin* Pin = Graph->FindPin(InPinPath);
	if (Pin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InPinPath);
		return false;
	}

	if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Pin->GetNode()))
	{
		if (Pin->GetName() == URigVMVariableNode::VariableName)
		{
			return SetVariableName(VariableNode, *InDefaultValue, bSetupUndoRedo);
		}
	}
	if (URigVMParameterNode* ParameterNode = Cast<URigVMParameterNode>(Pin->GetNode()))
	{
		if (Pin->GetName() == URigVMParameterNode::ParameterName)
		{
			return SetParameterName(ParameterNode, *InDefaultValue, bSetupUndoRedo);
		}
	}

	if (!SetPinDefaultValue(Pin, InDefaultValue, bResizeArrays, bSetupUndoRedo, bMergeUndoAction))
	{
		return false;
	}

	URigVMPin* PinForLink = Pin->GetPinForLink();
	if (PinForLink != Pin)
	{
		if (!SetPinDefaultValue(PinForLink, InDefaultValue, bResizeArrays, false, bMergeUndoAction))
		{
			return false;
		}
	}

	if (bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());

		RigVMPythonUtils::Print(GetGraphOuterName(),
			FString::Printf(TEXT("blueprint.get_controller_by_name('%s').set_pin_default_value('%s', '%s', %s)"),
			*GraphName,
			*GetSanitizedPinPath(InPinPath),
			*InDefaultValue,
			(bResizeArrays) ? TEXT("True") : TEXT("False")));
	}

	return true;
}

bool URigVMController::SetPinDefaultValue(URigVMPin* InPin, const FString& InDefaultValue, bool bResizeArrays, bool bSetupUndoRedo, bool bMergeUndoAction, bool bNotify)
{
	check(InPin);

	if(!InPin->IsUObject())
	{
		ensure(!InDefaultValue.IsEmpty());
	}

	TGuardValue<bool> Guard(bSuspendNotifications, !bNotify);

	URigVMGraph* Graph = GetGraph();
	check(Graph);
 
	if (bValidatePinDefaults)
	{
		if (!InPin->IsValidDefaultValue(InDefaultValue))
		{
			return false;
		}
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMSetPinDefaultValueAction Action;
	if (bSetupUndoRedo)
	{
		Action = FRigVMSetPinDefaultValueAction(InPin, InDefaultValue);
		Action.Title = FString::Printf(TEXT("Set Pin Default Value"));
		ActionStack->BeginAction(Action);
	}

	const FString ClampedDefaultValue = InPin->IsRootPin() ? InPin->ClampDefaultValueFromMetaData(InDefaultValue) : InDefaultValue;

	bool bSetPinDefaultValueSucceeded = false;
	if (InPin->IsArray())
	{
		if (ShouldPinBeUnfolded(InPin))
		{
			TArray<FString> Elements = URigVMPin::SplitDefaultValue(ClampedDefaultValue);

			if (bResizeArrays)
			{
				while (Elements.Num() > InPin->SubPins.Num())
				{
					InsertArrayPin(InPin, INDEX_NONE, FString(), bSetupUndoRedo);
				}
				while (Elements.Num() < InPin->SubPins.Num())
				{
					RemoveArrayPin(InPin->SubPins.Last()->GetPinPath(), bSetupUndoRedo);
				}
			}
			else
			{
				ensure(Elements.Num() == InPin->SubPins.Num());
			}

			for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ElementIndex++)
			{
				URigVMPin* SubPin = InPin->SubPins[ElementIndex];
				PostProcessDefaultValue(SubPin, Elements[ElementIndex]);
				if (!Elements[ElementIndex].IsEmpty())
				{
					SetPinDefaultValue(SubPin, Elements[ElementIndex], bResizeArrays, false, false);
					bSetPinDefaultValueSucceeded = true;
				}
			}
		}
	}
	else if (InPin->IsStruct())
	{
		TArray<FString> MemberValuePairs = URigVMPin::SplitDefaultValue(ClampedDefaultValue);

		for (const FString& MemberValuePair : MemberValuePairs)
		{
			FString MemberName, MemberValue;
			if (MemberValuePair.Split(TEXT("="), &MemberName, &MemberValue))
			{
				URigVMPin* SubPin = InPin->FindSubPin(MemberName);
				if (SubPin && !MemberValue.IsEmpty())
				{
					PostProcessDefaultValue(SubPin, MemberValue);
					if (!MemberValue.IsEmpty())
					{
						SetPinDefaultValue(SubPin, MemberValue, bResizeArrays, false, false);
						bSetPinDefaultValueSucceeded = true;
					}
				}
			}
		}
	}
	
	if(!bSetPinDefaultValueSucceeded)
	{
		if (InPin->GetSubPins().Num() == 0)
		{
			InPin->DefaultValue = ClampedDefaultValue;
			Notify(ERigVMGraphNotifType::PinDefaultValueChanged, InPin);
			if (!bSuspendNotifications)
			{
				Graph->MarkPackageDirty();
			}
		}
	}

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action, bMergeUndoAction);
	}

	return true;
}

bool URigVMController::ResetPinDefaultValue(const FString& InPinPath, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMPin* Pin = Graph->FindPin(InPinPath);
	if (Pin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InPinPath);
		return false;
	}

	URigVMNode* Node = Pin->GetNode();
	if (!Node->IsA<URigVMUnitNode>() && !Node->IsA<URigVMFunctionReferenceNode>())
	{
		ReportErrorf(TEXT("Pin '%s' is neither part of a unit nor a function reference node."), *InPinPath);
		return false;
	}

	const bool bSuccess = ResetPinDefaultValue(Pin, bSetupUndoRedo);
	if (bSuccess && bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());

		RigVMPythonUtils::Print(GetGraphOuterName(),
			FString::Printf(TEXT("blueprint.get_controller_by_name('%s').reset_pin_default_value('%s')"),
			*GraphName,
			*GetSanitizedPinPath(InPinPath)));
	}

	return bSuccess;
}

bool URigVMController::ResetPinDefaultValue(URigVMPin* InPin, bool bSetupUndoRedo)
{
	check(InPin);

	URigVMNode* RigVMNode = InPin->GetNode();

	// unit nodes
	if (URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(RigVMNode))
	{
		// cut off the first one since it's the node
		static const uint32 Offset = 1;
		const FString DefaultValue = GetPinInitialDefaultValueFromStruct(UnitNode->GetScriptStruct(), InPin, Offset);
		if (!DefaultValue.IsEmpty())
		{
			SetPinDefaultValue(InPin, DefaultValue, true, bSetupUndoRedo, false);
			return true;
		}
	}

	// function reference nodes
	URigVMFunctionReferenceNode* RefNode = Cast<URigVMFunctionReferenceNode>(RigVMNode);
	if (RefNode != nullptr)
	{
		const FString DefaultValue = GetPinInitialDefaultValue(InPin);
		if (!DefaultValue.IsEmpty())
		{
			SetPinDefaultValue(InPin, DefaultValue, true, bSetupUndoRedo, false);
			return true;
		}
	}

	return false;
}

FString URigVMController::GetPinInitialDefaultValue(const URigVMPin* InPin)
{
	static const FString EmptyValue;
	static const FString TArrayInitValue( TEXT("()") );
	static const FString TObjectInitValue( TEXT("()") );
	static const TMap<FString, FString> InitValues =
	{
		{ TEXT("bool"),	TEXT("False") },
		{ TEXT("int32"),	TEXT("0") },
		{ TEXT("float"),	TEXT("0.000000") },
		{ TEXT("double"),	TEXT("0.000000") },
		{ TEXT("FName"),	FName(NAME_None).ToString() },
		{ TEXT("FString"),	TEXT("") }
	};

	if (InPin->IsStruct())
	{
		// offset is useless here as we are going to get the full struct default value
		static const uint32 Offset = 0;
		return GetPinInitialDefaultValueFromStruct(InPin->GetScriptStruct(), InPin, Offset);
	}
		
	if (InPin->IsStructMember())
	{
		if (URigVMPin* ParentPin = InPin->GetParentPin())
		{
			// cut off node's and parent struct's paths if func reference node, only node instead
			static const uint32 Offset = InPin->GetNode()->IsA<URigVMFunctionReferenceNode>() ? 2 : 1;
			return GetPinInitialDefaultValueFromStruct(ParentPin->GetScriptStruct(), InPin, Offset);
		}
	}

	if (InPin->IsArray())
	{
		return TArrayInitValue;
	}
		
	if (InPin->IsUObject())
	{
		return TObjectInitValue;
	}
		
	if (UEnum* Enum = InPin->GetEnum())
	{
		return Enum->GetNameStringByIndex(0);
	}
	
	if (const FString* BasicDefault = InitValues.Find(InPin->GetCPPType()))
	{
		return *BasicDefault;
	}
	
	return EmptyValue;
}

FString URigVMController::GetPinInitialDefaultValueFromStruct(UScriptStruct* ScriptStruct, const URigVMPin* InPin, uint32 InOffset)
{
	FString DefaultValue;
	if (InPin && ScriptStruct)
	{
		TSharedPtr<FStructOnScope> StructOnScope = MakeShareable(new FStructOnScope(ScriptStruct));
		uint8* Memory = (uint8*)StructOnScope->GetStructMemory();
		ScriptStruct->InitializeDefaultValue(Memory);

		if (InPin->GetScriptStruct() == ScriptStruct)
		{
			ScriptStruct->ExportText(DefaultValue, Memory, nullptr, nullptr, PPF_None, nullptr, true);
			return DefaultValue;
		}

		const FString PinPath = InPin->GetPinPath();

		TArray<FString> Parts;
		if (!URigVMPin::SplitPinPath(PinPath, Parts))
		{
			return DefaultValue;
		}

		const uint32 NumParts = Parts.Num();
		if (InOffset >= NumParts)
		{
			return DefaultValue;
		}

		uint32 PartIndex = InOffset;

		UStruct* Struct = ScriptStruct;
		FProperty* Property = Struct->FindPropertyByName(*Parts[PartIndex++]);
		check(Property);

		Memory = Property->ContainerPtrToValuePtr<uint8>(Memory);

		while (PartIndex < NumParts && Property != nullptr)
		{
			if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
			{
				Property = ArrayProperty->Inner;
				check(Property);
				PartIndex++;

				if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
				{
					UScriptStruct* InnerStruct = StructProperty->Struct;
					StructOnScope = MakeShareable(new FStructOnScope(InnerStruct));
					Memory = (uint8 *)StructOnScope->GetStructMemory();
					InnerStruct->InitializeDefaultValue(Memory);
				}
				continue;
			}

			if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				Struct = StructProperty->Struct;
				Property = Struct->FindPropertyByName(*Parts[PartIndex++]);
				check(Property);
				Memory = Property->ContainerPtrToValuePtr<uint8>(Memory);
				continue;
			}

			break;
		}

		if (Memory)
		{
			check(Property);
			Property->ExportTextItem(DefaultValue, Memory, nullptr, nullptr, PPF_None);
		}
	}

	return DefaultValue;
}

FString URigVMController::AddArrayPin(const FString& InArrayPinPath, const FString& InDefaultValue, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	return InsertArrayPin(InArrayPinPath, INDEX_NONE, InDefaultValue, bSetupUndoRedo, bPrintPythonCommand);
}

FString URigVMController::DuplicateArrayPin(const FString& InArrayElementPinPath, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return FString();
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMPin* ElementPin = Graph->FindPin(InArrayElementPinPath);
	if (ElementPin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InArrayElementPinPath);
		return FString();
	}

	if (!ElementPin->IsArrayElement())
	{
		ReportErrorf(TEXT("Pin '%s' is not an array element."), *InArrayElementPinPath);
		return FString();
	}

	URigVMPin* ArrayPin = ElementPin->GetParentPin();
	check(ArrayPin);
	ensure(ArrayPin->IsArray());

	FString DefaultValue = ElementPin->GetDefaultValue();
	return InsertArrayPin(ArrayPin->GetPinPath(), ElementPin->GetPinIndex() + 1, DefaultValue, bSetupUndoRedo, bPrintPythonCommand);
}

FString URigVMController::InsertArrayPin(const FString& InArrayPinPath, int32 InIndex, const FString& InDefaultValue, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return FString();
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMPin* ArrayPin = Graph->FindPin(InArrayPinPath);
	if (ArrayPin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InArrayPinPath);
		return FString();
	}

	URigVMPin* ElementPin = InsertArrayPin(ArrayPin, InIndex, InDefaultValue, bSetupUndoRedo);
	if (ElementPin)
	{
		if (bPrintPythonCommand)
		{
			const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
			
			RigVMPythonUtils::Print(GetGraphOuterName(),
				FString::Printf(TEXT("blueprint.get_controller_by_name('%s').insert_array_pin('%s', %d, '%s')"),
				*GraphName,
				*GetSanitizedPinPath(InArrayPinPath),
				InIndex,
				*InDefaultValue));
		}
		
		return ElementPin->GetPinPath();
	}

	return FString();
}

URigVMPin* URigVMController::InsertArrayPin(URigVMPin* ArrayPin, int32 InIndex, const FString& InDefaultValue, bool bSetupUndoRedo)
{
	if (!ArrayPin->IsArray())
	{
		ReportErrorf(TEXT("Pin '%s' is not an array."), *ArrayPin->GetPinPath());
		return nullptr;
	}

	if (!ShouldPinBeUnfolded(ArrayPin))
	{
		ReportErrorf(TEXT("Cannot insert array pin under '%s'."), *ArrayPin->GetPinPath());
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (InIndex == INDEX_NONE)
	{
		InIndex = ArrayPin->GetSubPins().Num();
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMInsertArrayPinAction Action;
	if (bSetupUndoRedo)
	{
		Action = FRigVMInsertArrayPinAction(ArrayPin, InIndex, InDefaultValue);
		Action.Title = FString::Printf(TEXT("Insert Array Pin"));
		ActionStack->BeginAction(Action);
	}

	for (int32 ExistingIndex = ArrayPin->GetSubPins().Num() - 1; ExistingIndex >= InIndex; ExistingIndex--)
	{
		URigVMPin* ExistingPin = ArrayPin->GetSubPins()[ExistingIndex];
		RenameObject(ExistingPin, *FString::FormatAsNumber(ExistingIndex + 1));
	}

	URigVMPin* Pin = NewObject<URigVMPin>(ArrayPin, *FString::FormatAsNumber(InIndex));
	ConfigurePinFromPin(Pin, ArrayPin);
	Pin->CPPType = ArrayPin->GetArrayElementCppType();
	ArrayPin->SubPins.Insert(Pin, InIndex);

	if (Pin->IsStruct())
	{
		UScriptStruct* ScriptStruct = Pin->GetScriptStruct();
		if (ScriptStruct)
		{
			FString DefaultValue = InDefaultValue;
			CreateDefaultValueForStructIfRequired(ScriptStruct, DefaultValue);
			AddPinsForStruct(ScriptStruct, Pin->GetNode(), Pin, Pin->Direction, DefaultValue, false);
		}
	}
	else if (Pin->IsArray())
	{
		FArrayProperty * ArrayProperty = CastField<FArrayProperty>(FindPropertyForPin(Pin->GetPinPath()));
		if (ArrayProperty)
		{
			TArray<FString> ElementDefaultValues = URigVMPin::SplitDefaultValue(InDefaultValue);
			AddPinsForArray(ArrayProperty, Pin->GetNode(), Pin, Pin->Direction, ElementDefaultValues, false);
		}
	}
	else
	{
		FString DefaultValue = InDefaultValue;
		PostProcessDefaultValue(Pin, DefaultValue);
		Pin->DefaultValue = DefaultValue;
	}

	Notify(ERigVMGraphNotifType::PinArraySizeChanged, ArrayPin);
	if (!bSuspendNotifications)
	{
		Graph->MarkPackageDirty();
	}

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	return Pin;
}

bool URigVMController::RemoveArrayPin(const FString& InArrayElementPinPath, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if(!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMPin* ArrayElementPin = Graph->FindPin(InArrayElementPinPath);
	if (ArrayElementPin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InArrayElementPinPath);
		return false;
	}

	if (!ArrayElementPin->IsArrayElement())
	{
		ReportErrorf(TEXT("Pin '%s' is not an array element."), *InArrayElementPinPath);
		return false;
	}

	URigVMPin* ArrayPin = ArrayElementPin->GetParentPin();
	check(ArrayPin);
	ensure(ArrayPin->IsArray());

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMRemoveArrayPinAction Action;
	if (bSetupUndoRedo)
	{
		Action = FRigVMRemoveArrayPinAction(ArrayElementPin);
		Action.Title = FString::Printf(TEXT("Remove Array Pin"));
		ActionStack->BeginAction(Action);
	}

	int32 IndexToRemove = ArrayElementPin->GetPinIndex();
	if (!RemovePin(ArrayElementPin, bSetupUndoRedo, false))
	{
		return false;
	}

	for (int32 ExistingIndex = ArrayPin->GetSubPins().Num() - 1; ExistingIndex >= IndexToRemove; ExistingIndex--)
	{
		URigVMPin* ExistingPin = ArrayPin->GetSubPins()[ExistingIndex];
		ExistingPin->SetNameFromIndex();
	}

	if (!bSuspendNotifications)
	{
		Graph->MarkPackageDirty();
	}
	Notify(ERigVMGraphNotifType::PinArraySizeChanged, ArrayPin);

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	if (bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());

		RigVMPythonUtils::Print(GetGraphOuterName(),
			FString::Printf(TEXT("blueprint.get_controller_by_name('%s').remove_array_pin('%s')"),
			*GraphName,
			*GetSanitizedPinPath(InArrayElementPinPath)));
	}

	return true;
}

bool URigVMController::RemovePin(URigVMPin* InPinToRemove, bool bSetupUndoRedo, bool bNotify)
{
	FRigVMControllerCompileBracketScope CompileScope(this);
	if (bSetupUndoRedo)
	{
		BreakAllLinks(InPinToRemove, true, bSetupUndoRedo);
		BreakAllLinks(InPinToRemove, false, bSetupUndoRedo);
		BreakAllLinksRecursive(InPinToRemove, true, false, bSetupUndoRedo);
		BreakAllLinksRecursive(InPinToRemove, false, false, bSetupUndoRedo);
	}

	if (URigVMPin* ParentPin = InPinToRemove->GetParentPin())
	{
		ParentPin->SubPins.Remove(InPinToRemove);
	}
	else if(URigVMNode* Node = InPinToRemove->GetNode())
	{
		Node->Pins.Remove(InPinToRemove);
	}

	TArray<URigVMPin*> SubPins = InPinToRemove->GetSubPins();
	for (URigVMPin* SubPin : SubPins)
	{
		if (!RemovePin(SubPin, bSetupUndoRedo, bNotify))
		{
			return false;
		}
	}

	if (bNotify)
	{
		Notify(ERigVMGraphNotifType::PinRemoved, InPinToRemove);
	}

	DestroyObject(InPinToRemove);

	return true;
}

bool URigVMController::ClearArrayPin(const FString& InArrayPinPath, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	return SetArrayPinSize(InArrayPinPath, 0, FString(), bSetupUndoRedo, bPrintPythonCommand);
}

bool URigVMController::SetArrayPinSize(const FString& InArrayPinPath, int32 InSize, const FString& InDefaultValue, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMPin* Pin = Graph->FindPin(InArrayPinPath);
	if (Pin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InArrayPinPath);
		return false;
	}

	if (!Pin->IsArray())
	{
		ReportErrorf(TEXT("Pin '%s' is not an array."), *InArrayPinPath);
		return false;
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMBaseAction Action;
	if (bSetupUndoRedo)
	{
		Action.Title = FString::Printf(TEXT("Set Array Pin Size (%d)"), InSize);
		ActionStack->BeginAction(Action);
	}

	InSize = FMath::Max<int32>(InSize, 0);
	int32 AddedPins = 0;
	int32 RemovedPins = 0;

	FString DefaultValue = InDefaultValue;
	if (DefaultValue.IsEmpty())
	{
		if (Pin->GetSubPins().Num() > 0)
		{
			DefaultValue = Pin->GetSubPins().Last()->GetDefaultValue();
		}
		CreateDefaultValueForStructIfRequired(Pin->GetScriptStruct(), DefaultValue);
	}

	while (Pin->GetSubPins().Num() > InSize)
	{
		if (!RemoveArrayPin(Pin->GetSubPins()[Pin->GetSubPins().Num()-1]->GetPinPath(), bSetupUndoRedo))
		{
			if (bSetupUndoRedo)
			{
				ActionStack->CancelAction(Action);
			}
			return false;
		}
		RemovedPins++;
	}

	while (Pin->GetSubPins().Num() < InSize)
	{
		if (AddArrayPin(Pin->GetPinPath(), DefaultValue, bSetupUndoRedo).IsEmpty())
		{
			if (bSetupUndoRedo)
			{
				ActionStack->CancelAction(Action);
			}
			return false;
		}
		AddedPins++;
	}

	if (bSetupUndoRedo)
	{
		if (RemovedPins > 0 || AddedPins > 0)
		{
			ActionStack->EndAction(Action);
		}
		else
		{
			ActionStack->CancelAction(Action);
		}
	}

	return RemovedPins > 0 || AddedPins > 0;
}

bool URigVMController::BindPinToVariable(const FString& InPinPath, const FString& InNewBoundVariablePath, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMPin* Pin = Graph->FindPin(InPinPath);
	if (Pin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InPinPath);
		return false;
	}

	bool bSuccess = false;
	if (InNewBoundVariablePath.IsEmpty())
	{
		bSuccess = UnbindPinFromVariable(Pin, bSetupUndoRedo);
	}
	else
	{
		bSuccess = BindPinToVariable(Pin, InNewBoundVariablePath, bSetupUndoRedo);
	}
	
	if (bSuccess && bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
		
		RigVMPythonUtils::Print(GetGraphOuterName(),
			FString::Printf(TEXT("blueprint.get_controller_by_name('%s').bind_pin_to_variable('%s', '%s')"),
			*GraphName,
			*GetSanitizedPinPath(InPinPath),
			*InNewBoundVariablePath));
	}
	
	return bSuccess;
}

bool URigVMController::BindPinToVariable(URigVMPin* InPin, const FString& InNewBoundVariablePath, bool bSetupUndoRedo, const FString& InVariableNodeName)
{
	if (!IsValidPinForGraph(InPin))
	{
		return false;
	}

	if (GetGraph()->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot bind pins to variables in function library graphs."));
		return false;
	}

	if (InPin->GetBoundVariablePath() == InNewBoundVariablePath)
	{
		return false;
	}

	if (InPin->GetDirection() != ERigVMPinDirection::Input)
	{
		return false;
	}

	FString VariableName = InNewBoundVariablePath, SegmentPath;
	InNewBoundVariablePath.Split(TEXT("."), &VariableName, &SegmentPath);

	FRigVMExternalVariable Variable;
	for (const FRigVMExternalVariable& VariableDescription : GetAllVariables(true))
	{
		if (VariableDescription.Name.ToString() == VariableName)
		{
			Variable = VariableDescription;
			break;
		}
	}

	if (!Variable.Name.IsValid())
	{
		ReportError(TEXT("Cannot find variable in this graph."));
		return false;
	}

	
	if (!RigVMTypeUtils::AreCompatible(Variable, InPin->ToExternalVariable(), SegmentPath))
	{
		ReportError(TEXT("Cannot find variable in this graph."));
		return false;
	}
	
	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMBaseAction Action;
	if (bSetupUndoRedo)
	{
		Action.Title = TEXT("Bind pin to variable");
		ActionStack->BeginAction(Action);
	}

	{
		if (InPin->IsBoundToVariable())
		{
			UnbindPinFromVariable(InPin, bSetupUndoRedo);
		}
		TArray<URigVMInjectionInfo*> Infos = InPin->GetInjectedNodes();
		for (URigVMInjectionInfo* Info : Infos)
		{
			RemoveNode(Info->Node, bSetupUndoRedo);
		}
		BreakAllLinks(InPin, true, bSetupUndoRedo);
	}

	if (bSetupUndoRedo)
	{
		ActionStack->AddAction(FRigVMSetPinBoundVariableAction(InPin, InNewBoundVariablePath));
	}		
	
	// Create variable node
	URigVMVariableNode* VariableNode = nullptr;
	{
		{
			TGuardValue<bool> GuardNotifications(bSuspendNotifications, true);
			FString CPPType;
			UObject* CPPTypeObject;
			RigVMTypeUtils::CPPTypeFromExternalVariable(Variable, CPPType, &CPPTypeObject);
			VariableNode = AddVariableNode(*VariableName, CPPType, CPPTypeObject, true, FString(), FVector2D::ZeroVector, InVariableNodeName, false);
		}
		if (VariableNode == nullptr)
		{
			if (bSetupUndoRedo)
			{
				ActionStack->CancelAction(Action);
			}
			return false;
		}
	}
	
	URigVMPin* ValuePin = VariableNode->FindPin(URigVMVariableNode::ValueName);
	if (!SegmentPath.IsEmpty())
	{
		ValuePin = ValuePin->FindSubPin(SegmentPath);
	}

	// Add injection 
	URigVMInjectionInfo* InjectionInfo = NewObject<URigVMInjectionInfo>(InPin);
	{
		// re-parent the unit node to be under the injection info
		RenameObject(VariableNode, nullptr, InjectionInfo);
		
		InjectionInfo->Node = VariableNode;
		InjectionInfo->bInjectedAsInput = true;
		InjectionInfo->InputPin = nullptr;
		InjectionInfo->OutputPin = ValuePin;
	
		InPin->InjectionInfos.Add(InjectionInfo);
		Notify(ERigVMGraphNotifType::NodeAdded, VariableNode);
	}

	{		
		if (!AddLink(ValuePin, InPin, false))
		{
			if (bSetupUndoRedo)
			{
				ActionStack->CancelAction(Action);
			}
			return false;
		}
	}
	
	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	return true;
}

bool URigVMController::UnbindPinFromVariable(const FString& InPinPath, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMPin* Pin = Graph->FindPin(InPinPath);
	if (Pin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InPinPath);
		return false;
	}

	const bool bSuccess = UnbindPinFromVariable(Pin, bSetupUndoRedo);
	if (bSuccess && bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
		
		RigVMPythonUtils::Print(GetGraphOuterName(),
			FString::Printf(TEXT("blueprint.get_controller_by_name('%s').unbind_pin_from_variable('%s')"),
			*GraphName,
			*GetSanitizedPinPath(InPinPath)));
	}
	
	return bSuccess;
}

bool URigVMController::UnbindPinFromVariable(URigVMPin* InPin, bool bSetupUndoRedo)
{
	if (!IsValidPinForGraph(InPin))
	{
		return false;
	}


	if (GetGraph()->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot unbind pins from variables in function library graphs."));
		return false;
	}

	if (!InPin->IsBoundToVariable())
	{
		ReportError(TEXT("Pin is not bound to any variable."));
		return false;
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMBaseAction Action;
	if (bSetupUndoRedo)
	{
		Action.Title = TEXT("Unbind pin from variable");
		ActionStack->BeginAction(Action);
	}

	if (bSetupUndoRedo)
	{
		ActionStack->AddAction(FRigVMSetPinBoundVariableAction(InPin, FString()));
	}

	URigVMInjectionInfo* Info = nullptr;
	for (URigVMInjectionInfo* InjectionInfo : InPin->GetInjectedNodes())
	{
		if (InjectionInfo->Node->IsA<URigVMVariableNode>())
		{
			BreakAllLinks(InPin, true, false);
			RemoveNode(InjectionInfo->Node, false);
		}
	}	

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	return true;
}

bool URigVMController::MakeBindingsFromVariableNode(const FName& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Graph->FindNodeByName(InNodeName)))
	{
		return MakeBindingsFromVariableNode(VariableNode, bSetupUndoRedo);
	}

	return false;
}

bool URigVMController::MakeBindingsFromVariableNode(URigVMVariableNode* InNode, bool bSetupUndoRedo)
{
	check(InNode);

	TArray<TPair<URigVMPin*, URigVMPin*>> Pairs;
	TArray<URigVMNode*> NodesToRemove;
	NodesToRemove.Add(InNode);

	if (URigVMPin* ValuePin = InNode->FindPin(URigVMVariableNode::ValueName))
	{
		TArray<URigVMLink*> Links = ValuePin->GetTargetLinks(true);
		for (URigVMLink* Link : Links)
		{
			URigVMPin* SourcePin = Link->GetSourcePin();

			TArray<URigVMPin*> TargetPins;
			TargetPins.Add(Link->GetTargetPin());

			for (int32 TargetPinIndex = 0; TargetPinIndex < TargetPins.Num(); TargetPinIndex++)
			{
				URigVMPin* TargetPin = TargetPins[TargetPinIndex];
				if (Cast<URigVMRerouteNode>(TargetPin->GetNode()))
				{
					NodesToRemove.AddUnique(TargetPin->GetNode());
					TargetPins.Append(TargetPin->GetLinkedTargetPins(false /* recursive */));
				}
				else
				{
					Pairs.Add(TPair<URigVMPin*, URigVMPin*>(SourcePin, TargetPin));
				}
			}
		}
	}

	FName VariableName = InNode->GetVariableName();
	FRigVMExternalVariable Variable = GetVariableByName(VariableName);
	if (!Variable.IsValid(true /* allow nullptr */))
	{
		return false;
	}

	if (Pairs.Num() > 0)
	{
		FRigVMControllerCompileBracketScope CompileScope(this);
		if (bSetupUndoRedo)
		{
			OpenUndoBracket(TEXT("Turn Variable Node into Bindings"));
		}

		for (const TPair<URigVMPin*, URigVMPin*>& Pair : Pairs)
		{
			URigVMPin* SourcePin = Pair.Key;
			URigVMPin* TargetPin = Pair.Value;
			FString SegmentPath = SourcePin->GetSegmentPath();
			FString VariablePathToBind = VariableName.ToString();
			if (!SegmentPath.IsEmpty())
			{
				VariablePathToBind = FString::Printf(TEXT("%s.%s"), *VariablePathToBind, *SegmentPath);
			}

			if (!BindPinToVariable(TargetPin, VariablePathToBind, bSetupUndoRedo))
			{
				CancelUndoBracket();
			}
		}

		for (URigVMNode* NodeToRemove : NodesToRemove)
		{
			RemoveNode(NodeToRemove, bSetupUndoRedo, true);
		}

		if (bSetupUndoRedo)
		{
			CloseUndoBracket();
		}
		return true;
	}

	return false;

}

bool URigVMController::MakeVariableNodeFromBinding(const FString& InPinPath, const FVector2D& InNodePosition, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	return PromotePinToVariable(InPinPath, true, InNodePosition, bSetupUndoRedo, bPrintPythonCommand);
}

bool URigVMController::PromotePinToVariable(const FString& InPinPath, bool bCreateVariableNode, const FVector2D& InNodePosition, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMPin* Pin = Graph->FindPin(InPinPath);
	if (Pin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InPinPath);
		return false;
	}

	const bool bSuccess = PromotePinToVariable(Pin, bCreateVariableNode, InNodePosition, bSetupUndoRedo);
	if (bSuccess && bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
		
		RigVMPythonUtils::Print(GetGraphOuterName(), 
			FString::Printf(TEXT("blueprint.get_controller_by_name('%s').promote_pin_to_variable('%s', %s, %s)"),
			*GraphName,
			*GetSanitizedPinPath(InPinPath),
			(bCreateVariableNode) ? TEXT("True") : TEXT("False"),
			*RigVMPythonUtils::Vector2DToPythonString(InNodePosition)));
	}
	
	return bSuccess;
}

bool URigVMController::PromotePinToVariable(URigVMPin* InPin, bool bCreateVariableNode, const FVector2D& InNodePosition, bool bSetupUndoRedo)
{
	check(InPin);

	if (GetGraph()->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot promote pins to variables in function library graphs."));
		return false;
	}

	if (InPin->GetDirection() != ERigVMPinDirection::Input)
	{
		return false;
	}

	FRigVMExternalVariable VariableForPin;
	FString SegmentPath;
	if (InPin->IsBoundToVariable())
	{
		VariableForPin = GetVariableByName(*InPin->GetBoundVariableName());
		check(VariableForPin.IsValid(true /* allow nullptr */));
		SegmentPath = InPin->GetBoundVariablePath();
		if (SegmentPath.StartsWith(VariableForPin.Name.ToString() + TEXT(".")))
		{
			SegmentPath = SegmentPath.RightChop(VariableForPin.Name.ToString().Len());
		}
		else
		{
			SegmentPath.Empty();
		}
	}
	else
	{
		if (!UnitNodeCreatedContext.GetCreateExternalVariableDelegate().IsBound())
		{
			return false;
		}

		VariableForPin = InPin->ToExternalVariable();
		FName VariableName = UnitNodeCreatedContext.GetCreateExternalVariableDelegate().Execute(VariableForPin, InPin->GetDefaultValue());
		if (VariableName.IsNone())
		{
			return false;
		}

		VariableForPin = GetVariableByName(VariableName);
		if (!VariableForPin.IsValid(true /* allow nullptr*/))
		{
			return false;
		}
	}

	if (bCreateVariableNode)
	{
		FRigVMControllerCompileBracketScope CompileScope(this);
		if (URigVMVariableNode* VariableNode = AddVariableNode(
			VariableForPin.Name,
			VariableForPin.TypeName.ToString(),
			VariableForPin.TypeObject,
			true,
			FString(),
			InNodePosition,
			FString(),
			bSetupUndoRedo))
		{
			if (URigVMPin* ValuePin = VariableNode->FindPin(URigVMVariableNode::ValueName))
			{
				return AddLink(ValuePin->GetPinPath() + SegmentPath, InPin->GetPinPath(), bSetupUndoRedo);
			}
		}
	}
	else
	{
		FRigVMControllerCompileBracketScope CompileScope(this);
		return BindPinToVariable(InPin, VariableForPin.Name.ToString(), bSetupUndoRedo);
	}

	return false;
}

bool URigVMController::AddLink(const FString& InOutputPinPath, const FString& InInputPinPath, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if(!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	FString OutputPinPath = InOutputPinPath;
	FString InputPinPath = InInputPinPath;

	if (FString* RedirectedOutputPinPath = OutputPinRedirectors.Find(OutputPinPath))
	{
		OutputPinPath = *RedirectedOutputPinPath;
	}
	if (FString* RedirectedInputPinPath = InputPinRedirectors.Find(InputPinPath))
	{
		InputPinPath = *RedirectedInputPinPath;
	}

	URigVMPin* OutputPin = Graph->FindPin(OutputPinPath);
	if (OutputPin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *OutputPinPath);
		return false;
	}
	OutputPin = OutputPin->GetPinForLink();

	URigVMPin* InputPin = Graph->FindPin(InputPinPath);
	if (InputPin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InputPinPath);
		return false;
	}
	InputPin = InputPin->GetPinForLink();

	const bool bSuccess = AddLink(OutputPin, InputPin, bSetupUndoRedo);
	if (bSuccess && bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());

		const FString SanitizedInputPinPath = GetSanitizedPinPath(InputPin->GetPinPath());
		const FString SanitizedOutputPinPath = GetSanitizedPinPath(OutputPin->GetPinPath());

		RigVMPythonUtils::Print(GetGraphOuterName(), 
			FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_link('%s', '%s')"),
			*GraphName,
			*SanitizedOutputPinPath,
			*SanitizedInputPinPath));
	}
	
	return bSuccess;
}

bool URigVMController::AddLink(URigVMPin* OutputPin, URigVMPin* InputPin, bool bSetupUndoRedo)
{
	if(OutputPin == nullptr)
	{
		ReportError(TEXT("OutputPin is nullptr."));
		return false;
	}

	if(InputPin == nullptr)
	{
		ReportError(TEXT("InputPin is nullptr."));
		return false;
	}

	if(!IsValidPinForGraph(OutputPin) || !IsValidPinForGraph(InputPin))
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot add links in function library graphs."));
		return false;
	}

	{
		FString FailureReason;
		if (!Graph->CanLink(OutputPin, InputPin, &FailureReason, GetCurrentByteCode()))
		{
			ReportErrorf(TEXT("Cannot link '%s' to '%s': %s."), *OutputPin->GetPinPath(), *InputPin->GetPinPath(), *FailureReason, GetCurrentByteCode());
			return false;
		}
	}

	ensure(!OutputPin->IsLinkedTo(InputPin));
	ensure(!InputPin->IsLinkedTo(OutputPin));

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMBaseAction Action;
	if (bSetupUndoRedo)
	{
		Action.Title = FString::Printf(TEXT("Add Link"));
		ActionStack->BeginAction(Action);
	}

	// resolve types on the pins if needed
	if(InputPin->GetCPPTypeObject() != OutputPin->GetCPPTypeObject())
	{
		if(InputPin->GetCPPTypeObject() == FRigVMUnknownType::StaticStruct())
		{
			Notify(ERigVMGraphNotifType::InteractionBracketOpened, nullptr);
			ResolveUnknownTypePin(InputPin, OutputPin, bSetupUndoRedo);
			Notify(ERigVMGraphNotifType::InteractionBracketClosed, nullptr);
		}
		else if(OutputPin->GetCPPTypeObject() == FRigVMUnknownType::StaticStruct())
		{
			Notify(ERigVMGraphNotifType::InteractionBracketOpened, nullptr);
			ResolveUnknownTypePin(OutputPin, InputPin, bSetupUndoRedo);
			Notify(ERigVMGraphNotifType::InteractionBracketClosed, nullptr);
		}
	}

	if (OutputPin->IsExecuteContext())
	{
		BreakAllLinks(OutputPin, false, bSetupUndoRedo);
	}

	BreakAllLinks(InputPin, true, bSetupUndoRedo);
	if (bSetupUndoRedo)
	{
		BreakAllLinksRecursive(InputPin, true, true, bSetupUndoRedo);
		BreakAllLinksRecursive(InputPin, true, false, bSetupUndoRedo);
	}

	if (bSetupUndoRedo)
	{
		ExpandPinRecursively(OutputPin->GetParentPin(), bSetupUndoRedo);
		ExpandPinRecursively(InputPin->GetParentPin(), bSetupUndoRedo);
	}

	if (bSetupUndoRedo)
	{
		ActionStack->AddAction(FRigVMAddLinkAction(OutputPin, InputPin));
	}

	URigVMLink* Link = NewObject<URigVMLink>(Graph);
	Link->SourcePin = OutputPin;
	Link->TargetPin = InputPin;
	Link->SourcePinPath = OutputPin->GetPinPath();
	Link->TargetPinPath = InputPin->GetPinPath();
	Graph->Links.Add(Link);
	OutputPin->Links.Add(Link);
	InputPin->Links.Add(Link);

	if (!bSuspendNotifications)
	{
		Graph->MarkPackageDirty();
	}
	Notify(ERigVMGraphNotifType::LinkAdded, Link);

	UpdateRerouteNodeAfterChangingLinks(OutputPin, bSetupUndoRedo);
	UpdateRerouteNodeAfterChangingLinks(InputPin, bSetupUndoRedo);

	//TArray<URigVMNode*> NodesVisited;
	//PotentiallyResolvePrototypeNode(Cast<URigVMPrototypeNode>(InputPin->GetNode()), bSetupUndoRedo, NodesVisited);
	//PotentiallyResolvePrototypeNode(Cast<URigVMPrototypeNode>(OutputPin->GetNode()), bSetupUndoRedo, NodesVisited);

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	return true;
}

void URigVMController::RelinkSourceAndTargetPins(URigVMNode* Node, bool bSetupUndoRedo)
{
	TArray<URigVMPin*> SourcePins;
	TArray<URigVMPin*> TargetPins;
	TArray<URigVMLink*> LinksToRemove;

	// store source and target links 
	const TArray<URigVMLink*> RigVMLinks = Node->GetLinks();
	for (URigVMLink* Link: RigVMLinks)
	{
		URigVMPin* SrcPin = Link->GetSourcePin();
		if (SrcPin && SrcPin->GetNode() != Node)
		{
			SourcePins.AddUnique(SrcPin);
			LinksToRemove.AddUnique(Link);
		}

		URigVMPin* DstPin = Link->GetTargetPin();
		if (DstPin && DstPin->GetNode() != Node)
		{
			TargetPins.AddUnique(DstPin);
			LinksToRemove.AddUnique(Link);
		}
	}

	if( SourcePins.Num() > 0 && TargetPins.Num() > 0 )
	{
		// remove previous links 
		for (URigVMLink* Link: LinksToRemove)
		{
			BreakLink(Link->GetSourcePin(), Link->GetTargetPin(), bSetupUndoRedo); 
		}

		// relink pins if feasible 
		TArray<bool> TargetHandled;
		TargetHandled.AddZeroed(TargetPins.Num());
		for (URigVMPin* Src: SourcePins)
		{
			for (int32 Index = 0; Index < TargetPins.Num(); Index++)
			{
				if (!TargetHandled[Index])
				{
					if (URigVMPin::CanLink(Src, TargetPins[Index], nullptr, nullptr))
					{
						// execute pins can be linked to one target only so link to the 1st compatible target
						const bool bNeedNewLink = Src->IsExecuteContext() ? (Src->GetTargetLinks().Num() == 0) : true;
						if (bNeedNewLink)
						{
							AddLink(Src, TargetPins[Index], bSetupUndoRedo);
							TargetHandled[Index] = true;								
						}
					}
				}
			}
		}
	}
}

bool URigVMController::BreakLink(const FString& InOutputPinPath, const FString& InInputPinPath, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if(!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMPin* OutputPin = Graph->FindPin(InOutputPinPath);
	if (OutputPin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InOutputPinPath);
		return false;
	}
	OutputPin = OutputPin->GetPinForLink();

	URigVMPin* InputPin = Graph->FindPin(InInputPinPath);
	if (InputPin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InInputPinPath);
		return false;
	}
	InputPin = InputPin->GetPinForLink();

	const bool bSuccess = BreakLink(OutputPin, InputPin, bSetupUndoRedo);
	if (bSuccess && bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());

		RigVMPythonUtils::Print(GetGraphOuterName(), 
			FString::Printf(TEXT("blueprint.get_controller_by_name('%s').break_link('%s', '%s')"),
			*GraphName,
			*GetSanitizedPinPath(OutputPin->GetPinPath()),
			*GetSanitizedPinPath(InputPin->GetPinPath())));
	}
	return bSuccess;
}

bool URigVMController::BreakLink(URigVMPin* OutputPin, URigVMPin* InputPin, bool bSetupUndoRedo)
{
	if(!IsValidPinForGraph(OutputPin) || !IsValidPinForGraph(InputPin))
	{
		return false;
	}

	if (!OutputPin->IsLinkedTo(InputPin))
	{
		return false;
	}
	ensure(InputPin->IsLinkedTo(OutputPin));

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot break links in function library graphs."));
		return false;
	}

	for (URigVMLink* Link : InputPin->Links)
	{
		if (Link->SourcePin == OutputPin && Link->TargetPin == InputPin)
		{
			FRigVMControllerCompileBracketScope CompileScope(this);
			FRigVMBreakLinkAction Action;
			if (bSetupUndoRedo)
			{
				Action = FRigVMBreakLinkAction(OutputPin, InputPin);
				Action.Title = FString::Printf(TEXT("Break Link"));
				ActionStack->BeginAction(Action);
			}

			OutputPin->Links.Remove(Link);
			InputPin->Links.Remove(Link);
			Graph->Links.Remove(Link);
			
			if (!bSuspendNotifications)
			{
				Graph->MarkPackageDirty();
			}
			Notify(ERigVMGraphNotifType::LinkRemoved, Link);

			DestroyObject(Link);

			UpdateRerouteNodeAfterChangingLinks(OutputPin, bSetupUndoRedo);
			UpdateRerouteNodeAfterChangingLinks(InputPin, bSetupUndoRedo);

			if (bSetupUndoRedo)
			{
				ActionStack->EndAction(Action);
			}

			return true;
		}
	}

	return false;
}

bool URigVMController::BreakAllLinks(const FString& InPinPath, bool bAsInput, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if(!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMPin* Pin = Graph->FindPin(InPinPath);
	if (Pin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InPinPath);
		return false;
	}
	Pin = Pin->GetPinForLink();

	if (!IsValidPinForGraph(Pin))
	{
		return false;
	}

	const bool bSuccess = BreakAllLinks(Pin, bAsInput, bSetupUndoRedo);
	if (bSuccess && bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());

		RigVMPythonUtils::Print(GetGraphOuterName(), 
			FString::Printf(TEXT("blueprint.get_controller_by_name('%s').break_all_links('%s', %s)"),
			*GraphName,
			*GetSanitizedPinPath(Pin->GetPinPath()),
			bAsInput ? TEXT("True") : TEXT("False")));
	}
	return bSuccess;
}

bool URigVMController::BreakAllLinks(URigVMPin* Pin, bool bAsInput, bool bSetupUndoRedo)
{
	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMBaseAction Action;
	if (bSetupUndoRedo)
	{
		Action.Title = FString::Printf(TEXT("Break All Links"));
		ActionStack->BeginAction(Action);
	}

	int32 LinksBroken = 0;
	if (Pin->IsBoundToVariable() && bAsInput && bSetupUndoRedo)
	{
		UnbindPinFromVariable(Pin, bSetupUndoRedo);
		LinksBroken++;
	}

	TArray<URigVMLink*> Links = Pin->GetLinks();
	for (int32 LinkIndex = Links.Num() - 1; LinkIndex >= 0; LinkIndex--)
	{
		URigVMLink* Link = Links[LinkIndex];
		if (bAsInput && Link->GetTargetPin() == Pin)
		{
			LinksBroken += BreakLink(Link->GetSourcePin(), Pin, bSetupUndoRedo) ? 1 : 0;
		}
		else if (!bAsInput && Link->GetSourcePin() == Pin)
		{
			LinksBroken += BreakLink(Pin, Link->GetTargetPin(), bSetupUndoRedo) ? 1 : 0;
		}
	}

	if (bSetupUndoRedo)
	{
		if (LinksBroken > 0)
		{
			ActionStack->EndAction(Action);
		}
		else
		{
			ActionStack->CancelAction(Action);
		}
	}

	return LinksBroken > 0;
}

void URigVMController::BreakAllLinksRecursive(URigVMPin* Pin, bool bAsInput, bool bTowardsParent, bool bSetupUndoRedo)
{
	if (bTowardsParent)
	{
		URigVMPin* ParentPin = Pin->GetParentPin();
		if (ParentPin)
		{
			BreakAllLinks(ParentPin, bAsInput, bSetupUndoRedo);
			BreakAllLinksRecursive(ParentPin, bAsInput, bTowardsParent, bSetupUndoRedo);
		}
	}
	else
	{
		for (URigVMPin* SubPin : Pin->SubPins)
		{
			BreakAllLinks(SubPin, bAsInput, bSetupUndoRedo);
			BreakAllLinksRecursive(SubPin, bAsInput, bTowardsParent, bSetupUndoRedo);
		}
	}
}

FName URigVMController::AddExposedPin(const FName& InPinName, ERigVMPinDirection InDirection, const FString& InCPPType, const FName& InCPPTypeObjectPath, const FString& InDefaultValue, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return NAME_None;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (Graph->IsTopLevelGraph())
	{
		ReportError(TEXT("Exposed pins can only be edited on nested graphs."));
		return NAME_None;
	}

	if (Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot expose pins in function library graphs."));
		return NAME_None;
	}

	URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(Graph->GetOuter());
	check(LibraryNode);

	UObject* CPPTypeObject = nullptr;
	if (!InCPPTypeObjectPath.IsNone())
	{
		if (CPPTypeObject == nullptr)
		{
			CPPTypeObject = URigVMCompiler::GetScriptStructForCPPType(InCPPTypeObjectPath.ToString());
		}
		if (CPPTypeObject == nullptr)
		{
			CPPTypeObject = URigVMPin::FindObjectFromCPPTypeObjectPath<UObject>(InCPPTypeObjectPath.ToString());
		}
	}

	if (CPPTypeObject)
	{
		if (UScriptStruct* ScriptStruct = Cast<UScriptStruct>(CPPTypeObject))
		{
			if (ScriptStruct->IsChildOf(FRigVMExecuteContext::StaticStruct()))
			{
				for (URigVMPin* ExistingPin : LibraryNode->Pins)
				{
					if (ExistingPin->IsExecuteContext())
					{
						return NAME_None;
					}
				}

				InDirection = ERigVMPinDirection::IO;
			}
		}
	}

	// only allow one exposed pin of type execute context per direction
	if(const UScriptStruct* CPPTypeStruct = Cast<UScriptStruct>(CPPTypeObject))
	{
		if(CPPTypeStruct->IsChildOf(FRigVMExecuteContext::StaticStruct()))
		{
			for(URigVMPin* ExistingPin : LibraryNode->Pins)
			{
				if(ExistingPin->IsExecuteContext())
				{
					return NAME_None;
				}
			}
		}
	}

	FName PinName = GetUniqueName(InPinName, [LibraryNode](const FName& InName) {

		if(LibraryNode->FindPin(InName.ToString()) != nullptr)
		{
			return false;
		}

		const TArray<FRigVMGraphVariableDescription>& LocalVariables = LibraryNode->GetContainedGraph()->GetLocalVariables(true);
		for(const FRigVMGraphVariableDescription& VariableDescription : LocalVariables)
		{
			if (VariableDescription.Name == InName)
			{
				return false;
			}
		}
		return true;

	}, false, true);

	URigVMPin* Pin = NewObject<URigVMPin>(LibraryNode, PinName);
	Pin->CPPType = PostProcessCPPType(InCPPType, CPPTypeObject);
	Pin->CPPTypeObjectPath = InCPPTypeObjectPath;
	Pin->bIsConstant = false;
	Pin->Direction = InDirection;
	AddNodePin(LibraryNode, Pin);

	if (Pin->IsStruct())
	{
		FRigVMControllerGraphGuard GraphGuard(this, LibraryNode->GetGraph(), bSetupUndoRedo);

		FString DefaultValue = InDefaultValue;
		CreateDefaultValueForStructIfRequired(Pin->GetScriptStruct(), DefaultValue);
		AddPinsForStruct(Pin->GetScriptStruct(), LibraryNode, Pin, Pin->Direction, DefaultValue, false);
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMAddExposedPinAction Action(Pin);
	if (bSetupUndoRedo)
	{
		ActionStack->BeginAction(Action);
	}

	{
		FRigVMControllerGraphGuard GraphGuard(this, LibraryNode->GetGraph(), bSetupUndoRedo);
		Notify(ERigVMGraphNotifType::PinAdded, Pin);
	}

	if (!InDefaultValue.IsEmpty())
	{
		FRigVMControllerGraphGuard GraphGuard(this, Pin->GetGraph(), bSetupUndoRedo);
		SetPinDefaultValue(Pin, InDefaultValue, true, bSetupUndoRedo, false);
	}

	RefreshFunctionPins(Graph->GetEntryNode(), true);
	RefreshFunctionPins(Graph->GetReturnNode(), true);
	RefreshFunctionReferences(LibraryNode, bSetupUndoRedo);

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	if (bPrintPythonCommand)
	{
		FString DirectionStr;
		switch (InDirection)
		{
			case ERigVMPinDirection::Hidden: DirectionStr = TEXT("unreal.RigVMPinDirection.HIDDEN"); break;
			case ERigVMPinDirection::Input: DirectionStr = TEXT("unreal.RigVMPinDirection.INPUT"); break;
			case ERigVMPinDirection::Output: DirectionStr = TEXT("unreal.RigVMPinDirection.OUTPUT"); break;
			case ERigVMPinDirection::Visible: DirectionStr = TEXT("unreal.RigVMPinDirection.VISIBLE"); break;
			case ERigVMPinDirection::IO: DirectionStr = TEXT("unreal.RigVMPinDirection.IO"); break;
		}
		
		//AddExposedPin(const FName& InPinName, ERigVMPinDirection InDirection, const FString& InCPPType, const FName& InCPPTypeObjectPath, const FString& InDefaultValue, bool bSetupUndoRedo, bool bPrintPythonCommand)

		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
		
		RigVMPythonUtils::Print(GetGraphOuterName(), 
			FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_exposed_pin('%s', %s, '%s', '%s', '%s')"),
				*GraphName,
				*GetSanitizedPinName(InPinName.ToString()),
				*DirectionStr,
				*InCPPType,
				*InCPPTypeObjectPath.ToString(),
				*InDefaultValue));
	}
	
	return PinName;
}

bool URigVMController::RemoveExposedPin(const FName& InPinName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (Graph->IsTopLevelGraph())
	{
		ReportError(TEXT("Exposed pins can only be edited on nested graphs."));
		return false;
	}

	if (Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot remove exposed pins in function library graphs."));
		return false;
	}

	URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(Graph->GetOuter());
	check(LibraryNode);

	URigVMPin* Pin = LibraryNode->FindPin(InPinName.ToString());
	if (Pin == nullptr)
	{
		return false;
	}

	if(bSetupUndoRedo)
	{
		if(RequestBulkEditDialogDelegate.IsBound())
		{
			FRigVMController_BulkEditResult Result = RequestBulkEditDialogDelegate.Execute(LibraryNode, ERigVMControllerBulkEditType::RemoveExposedPin);
			if(Result.bCanceled)
			{
				return false;
			}
			bSetupUndoRedo = Result.bSetupUndoRedo;
		}
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMRemoveExposedPinAction Action(Pin);
	if (bSetupUndoRedo)
	{
		ActionStack->BeginAction(Action);
	}

	bool bSuccessfullyRemovedPin = false;
	{
		FRigVMControllerGraphGuard GraphGuard(this, LibraryNode->GetGraph(), false);
		bSuccessfullyRemovedPin = RemovePin(Pin, bSetupUndoRedo, true);
	}

	TArray<URigVMVariableNode*> NodesToRemove;
	for (URigVMNode* Node : Graph->GetNodes())
	{
		if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
		{
			if (VariableNode->GetVariableName() == InPinName)
			{
				NodesToRemove.Add(VariableNode);
			}
		}
	}
	for (int32 i=NodesToRemove.Num()-1; i >= 0; --i)
	{
		RemoveNode(NodesToRemove[i], bSetupUndoRedo);
	}

	RefreshFunctionPins(Graph->GetEntryNode(), true);
	RefreshFunctionPins(Graph->GetReturnNode(), true);
	RefreshFunctionReferences(LibraryNode, false);

	if (bSetupUndoRedo)
	{
		if (bSuccessfullyRemovedPin)
		{
			ActionStack->EndAction(Action);
		}
		else
		{
			ActionStack->CancelAction(Action);
		}
	}

	if (bSuccessfullyRemovedPin && bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());

		RigVMPythonUtils::Print(GetGraphOuterName(), 
			FString::Printf(TEXT("blueprint.get_controller_by_name('%s').remove_exposed_pin('%s')"),
				*GraphName,
				*GetSanitizedPinName(InPinName.ToString())));
	}

	return bSuccessfullyRemovedPin;
}

bool URigVMController::RenameExposedPin(const FName& InOldPinName, const FName& InNewPinName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (Graph->IsTopLevelGraph())
	{
		ReportError(TEXT("Exposed pins can only be edited on nested graphs."));
		return false;
	}

	if (Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot rename exposed pins in function library graphs."));
		return false;
	}

	URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(Graph->GetOuter());
	check(LibraryNode);

	URigVMPin* Pin = LibraryNode->FindPin(InOldPinName.ToString());
	if (Pin == nullptr)
	{
		return false;
	}

	if (Pin->GetFName() == InNewPinName)
	{
		return false;
	}

	if(bSetupUndoRedo)
	{
		if(RequestBulkEditDialogDelegate.IsBound())
		{
			FRigVMController_BulkEditResult Result = RequestBulkEditDialogDelegate.Execute(LibraryNode, ERigVMControllerBulkEditType::RenameExposedPin); 
			if(Result.bCanceled)
			{
				return false;
			}
			bSetupUndoRedo = Result.bSetupUndoRedo;
		}
	}

	FName PinName = GetUniqueName(InNewPinName, [LibraryNode](const FName& InName) {
		const TArray<FRigVMGraphVariableDescription>& LocalVariables = LibraryNode->GetContainedGraph()->GetLocalVariables(true);
		for(const FRigVMGraphVariableDescription& VariableDescription : LocalVariables)
		{
			if (VariableDescription.Name == InName)
			{
				return false;
			}
		}
		return true;
	}, false, true);

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMRenameExposedPinAction Action;
	if (bSetupUndoRedo)
	{
		Action = FRigVMRenameExposedPinAction(Pin->GetFName(), PinName);
		ActionStack->BeginAction(Action);
	}

	struct Local
	{
		static bool RenamePin(URigVMController* InController, URigVMPin* InPin, const FName& InNewName)
		{
			FRigVMControllerGraphGuard GraphGuard(InController, InPin->GetGraph(), false);

			TArray<URigVMLink*> Links;
			Links.Append(InPin->GetSourceLinks(true));
			Links.Append(InPin->GetTargetLinks(true));

			// store both the ptr + pin path
			for (URigVMLink* Link : Links)
			{
				Link->PrepareForCopy();
				InController->Notify(ERigVMGraphNotifType::LinkRemoved, Link);
			}

			if (!InController->RenameObject(InPin, *InNewName.ToString()))
			{
				return false;
			}

			// update the eventually stored pin path to the new name
			for (URigVMLink* Link : Links)
			{
				Link->PrepareForCopy();
			}

			InController->Notify(ERigVMGraphNotifType::PinRenamed, InPin);

			for (URigVMLink* Link : Links)
			{
				InController->Notify(ERigVMGraphNotifType::LinkAdded, Link);
			}

			return true;
		}
	};

	if (!Local::RenamePin(this, Pin, PinName))
	{
		ActionStack->CancelAction(Action);
		return false;
	}

	if (URigVMFunctionEntryNode* EntryNode = Graph->GetEntryNode())
	{
		if (URigVMPin* EntryPin = EntryNode->FindPin(InOldPinName.ToString()))
		{
			Local::RenamePin(this, EntryPin, PinName);
		}
	}

	if (URigVMFunctionReturnNode* ReturnNode = Graph->GetReturnNode())
	{
		if (URigVMPin* ReturnPin = ReturnNode->FindPin(InOldPinName.ToString()))
		{
			Local::RenamePin(this, ReturnPin, PinName);
		}
	}

	if (URigVMFunctionLibrary* FunctionLibrary = Cast<URigVMFunctionLibrary>(LibraryNode->GetGraph()))
	{
		FunctionLibrary->ForEachReference(LibraryNode->GetFName(), [this, InOldPinName, PinName](URigVMFunctionReferenceNode* ReferenceNode)
        {
			if (URigVMPin* EntryPin = ReferenceNode->FindPin(InOldPinName.ToString()))
			{
                FRigVMControllerGraphGuard GraphGuard(this, ReferenceNode->GetGraph(), false);
                Local::RenamePin(this, EntryPin, PinName);
            }
        });
	}

	for (URigVMNode* Node : Graph->GetNodes())
	{
		if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
		{
			if (VariableNode->GetVariableName() == InOldPinName)
			{
				SetVariableName(VariableNode, InNewPinName, bSetupUndoRedo);
			}
		}
	}

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	if (bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());

		RigVMPythonUtils::Print(GetGraphOuterName(), 
			FString::Printf(TEXT("blueprint.get_controller_by_name('%s').rename_exposed_pin('%s', '%s')"),
				*GraphName,
				*GetSanitizedPinName(InOldPinName.ToString()),
				*GetSanitizedPinName(InNewPinName.ToString())));
	}

	return true;
}

bool URigVMController::ChangeExposedPinType(const FName& InPinName, const FString& InCPPType, const FName& InCPPTypeObjectPath, bool& bSetupUndoRedo, bool bSetupOrphanPins, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (Graph->IsTopLevelGraph())
	{
		ReportError(TEXT("Exposed pins can only be edited on nested graphs."));
		return false;
	}

	if (Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot change exposed pin types in function library graphs."));
		return false;
	}

	URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(Graph->GetOuter());
	check(LibraryNode);

	URigVMPin* Pin = LibraryNode->FindPin(InPinName.ToString());
	if (Pin == nullptr)
	{
		return false;
	}
	
	// only allow one exposed pin of type execute context per direction
	if (!InCPPTypeObjectPath.IsNone())
	{
		if(UObject* CPPTypeObject = URigVMPin::FindObjectFromCPPTypeObjectPath<UObject>(InCPPTypeObjectPath.ToString()))
		{
			if(const UScriptStruct* CPPTypeStruct = Cast<UScriptStruct>(CPPTypeObject))
			{
				if(CPPTypeStruct->IsChildOf(FRigVMExecuteContext::StaticStruct()))
				{
					for(URigVMPin* ExistingPin : LibraryNode->Pins)
					{
						if(ExistingPin != Pin)
						{
							if(ExistingPin->IsExecuteContext())
							{
								return false;
							}
						}
					}
				}
			}
		}
	}

	if(bSetupUndoRedo)
	{
		if(RequestBulkEditDialogDelegate.IsBound())
		{
			const FRigVMController_BulkEditResult Result = RequestBulkEditDialogDelegate.Execute(LibraryNode, ERigVMControllerBulkEditType::ChangeExposedPinType); 
			if(Result.bCanceled)
			{
				return false;
			}
			bSetupUndoRedo = Result.bSetupUndoRedo;
		}
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMBaseAction Action;
	if (bSetupUndoRedo)
	{
		Action.Title = FString::Printf(TEXT("Change Pin Type"));
		ActionStack->BeginAction(Action);
	}

	{
		FRigVMControllerGraphGuard GraphGuard(this, LibraryNode->GetGraph(), bSetupUndoRedo);
		if (!ChangePinType(Pin, InCPPType, InCPPTypeObjectPath, bSetupUndoRedo, bSetupOrphanPins))
		{
			if (bSetupUndoRedo)
			{
				ActionStack->CancelAction(Action);
			}
			return false;
		}
		RemoveUnusedOrphanedPins(LibraryNode, true);
	}

	if (URigVMFunctionEntryNode* EntryNode = Graph->GetEntryNode())
	{
		TArray<URigVMLink*> Links = EntryNode->GetLinks();
		DetachLinksFromPinObjects(&Links, true);
		RepopulatePinsOnNode(EntryNode, true, true);
		ReattachLinksToPinObjects(false, &Links, true);
		
		RemoveUnusedOrphanedPins(EntryNode, true);
	}
	
	if (URigVMFunctionReturnNode* ReturnNode = Graph->GetReturnNode())
	{
		TArray<URigVMLink*> Links = ReturnNode->GetLinks();
		DetachLinksFromPinObjects(&Links, true);
		RepopulatePinsOnNode(ReturnNode, true, true);
		ReattachLinksToPinObjects(false, &Links, true);
		
		RemoveUnusedOrphanedPins(ReturnNode, true);
	}

	if (URigVMFunctionLibrary* FunctionLibrary = Cast<URigVMFunctionLibrary>(LibraryNode->GetGraph()))
	{
		FunctionLibrary->ForEachReference(LibraryNode->GetFName(), [this, &Pin, InCPPType, InCPPTypeObjectPath, bSetupUndoRedo, bSetupOrphanPins](URigVMFunctionReferenceNode* ReferenceNode)
        {
			if (URigVMPin* ReferencedNodePin = ReferenceNode->FindPin(Pin->GetName()))
			{
				FRigVMControllerGraphGuard GraphGuard(this, ReferenceNode->GetGraph(), false);
				ChangePinType(ReferencedNodePin, InCPPType, InCPPTypeObjectPath, bSetupUndoRedo, bSetupOrphanPins);
				RemoveUnusedOrphanedPins(ReferenceNode, true);
			}
        });
	}

	for (URigVMNode* Node : Graph->GetNodes())
	{
		if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
		{
			if (VariableNode->GetVariableName() == InPinName)
			{
				URigVMPin* ValuePin = VariableNode->FindPin(URigVMVariableNode::ValueName);
				if (ValuePin)
				{
					ChangePinType(ValuePin, InCPPType, InCPPTypeObjectPath, bSetupUndoRedo, bSetupOrphanPins);
					RemoveUnusedOrphanedPins(VariableNode, true);
				}
			}
		}
	}

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	if (bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
		
		RigVMPythonUtils::Print(GetGraphOuterName(), 
			FString::Printf(TEXT("blueprint.get_controller_by_name('%s').change_exposed_pin_type('%s', '%s', '%s')"),
				*GraphName,
				*GetSanitizedPinName(InPinName.ToString()),
				*InCPPType,
				*InCPPTypeObjectPath.ToString()));
	}

	return true;
}

bool URigVMController::SetExposedPinIndex(const FName& InPinName, int32 InNewIndex, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	FString PinPath = InPinName.ToString();
	if (PinPath.Contains(TEXT(".")))
	{
		ReportError(TEXT("Cannot change pin index for pins on nodes for now - only within collapse nodes."));
		return false;
	}

	URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(Graph->GetOuter());
	if (LibraryNode == nullptr)
	{
		ReportError(TEXT("Graph is not under a Collapse Node"));
		return false;
	}

	URigVMPin* Pin = LibraryNode->FindPin(PinPath);
	if (Pin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find exposed pin '%s'."), *PinPath);
		return false;
	}

	if (Pin->GetPinIndex() == InNewIndex)
	{
		return false;
	}

	if (InNewIndex < 0 || InNewIndex >= LibraryNode->GetPins().Num())
	{
		ReportErrorf(TEXT("Invalid new pin index '%d'."), InNewIndex);
		return false;
	}

	FRigVMControllerCompileBracketScope CompileBracketScope(this);

	FRigVMSetPinIndexAction PinIndexAction(Pin, InNewIndex);
	{
		LibraryNode->Pins.Remove(Pin);
		LibraryNode->Pins.Insert(Pin, InNewIndex);

		FRigVMControllerGraphGuard GraphGuard(this, LibraryNode->GetGraph(), false);
		Notify(ERigVMGraphNotifType::PinIndexChanged, Pin);
	}

	RefreshFunctionPins(LibraryNode->GetEntryNode(), true);
	RefreshFunctionPins(LibraryNode->GetReturnNode(), true);
	RefreshFunctionReferences(LibraryNode, false);
	
	if (bSetupUndoRedo)
	{
		ActionStack->AddAction(PinIndexAction);
	}

	if (bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());

		RigVMPythonUtils::Print(GetGraphOuterName(), 
			FString::Printf(TEXT("blueprint.get_controller_by_name('%s').set_exposed_pin_index('%s', %d)"),
				*GraphName,
				*GetSanitizedPinName(InPinName.ToString()),
				InNewIndex));
	}

	return true;
}

URigVMFunctionReferenceNode* URigVMController::AddFunctionReferenceNode(URigVMLibraryNode* InFunctionDefinition, const FVector2D& InNodePosition, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot add function reference nodes to function library graphs."));
		return nullptr;
	}

	if (InFunctionDefinition == nullptr)
	{
		ReportError(TEXT("Cannot add a function reference node without a valid function definition."));
		return nullptr;
	}

	if (!InFunctionDefinition->GetGraph()->IsA<URigVMFunctionLibrary>())
	{
		ReportAndNotifyError(TEXT("Cannot use the function definition for a function reference node."));
		return nullptr;
	}

	if(!CanAddFunctionRefForDefinition(InFunctionDefinition, true))
	{
		return nullptr;
	}

	FString NodeName = GetValidNodeName(InNodeName.IsEmpty() ? InFunctionDefinition->GetName() : InNodeName);
	URigVMFunctionReferenceNode* FunctionRefNode = NewObject<URigVMFunctionReferenceNode>(Graph, *NodeName);
	FunctionRefNode->Position = InNodePosition;
	FunctionRefNode->SetReferencedNode(InFunctionDefinition);
	Graph->Nodes.Add(FunctionRefNode);

	FRigVMControllerCompileBracketScope CompileScope(this);
	
	RepopulatePinsOnNode(FunctionRefNode, false, false);

	Notify(ERigVMGraphNotifType::NodeAdded, FunctionRefNode);

	if (URigVMFunctionLibrary* FunctionLibrary = InFunctionDefinition->GetLibrary())
	{
		FunctionLibrary->FunctionReferences.FindOrAdd(InFunctionDefinition).FunctionReferences.Add(FunctionRefNode);
		FunctionLibrary->MarkPackageDirty();
	}

	for (URigVMPin* SourcePin : InFunctionDefinition->Pins)
	{
		if (URigVMPin* TargetPin = FunctionRefNode->FindPin(SourcePin->GetName()))
		{
			FString DefaultValue = SourcePin->GetDefaultValue();
			if (!DefaultValue.IsEmpty())
			{
				SetPinDefaultValue(TargetPin, DefaultValue, true, false, false);
			}
		}
	}

	if (bSetupUndoRedo)
	{
		FRigVMInverseAction InverseAction;
		InverseAction.Title = TEXT("Add function node");

		ActionStack->BeginAction(InverseAction);
		ActionStack->AddAction(FRigVMRemoveNodeAction(FunctionRefNode, this));
		ActionStack->EndAction(InverseAction);
	}

	if (bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
		const FString FunctionDefinitionName = GetSanitizedNodeName(InFunctionDefinition->GetName());

		if (InFunctionDefinition->GetLibrary() == GetGraph()->GetDefaultFunctionLibrary())
		{

			RigVMPythonUtils::Print(GetGraphOuterName(), 
				FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_function_reference_node(library.find_function('%s'), %s, '%s')"),
						*GraphName,
						*FunctionDefinitionName,
						*RigVMPythonUtils::Vector2DToPythonString(InNodePosition),
						*NodeName));
		}
		else
		{
			RigVMPythonUtils::Print(GetGraphOuterName(), 
				FString::Printf(TEXT("function_blueprint = unreal.load_object(name = '%s', outer = None)"),
				*InFunctionDefinition->GetLibrary()->GetOuter()->GetPathName()));
			RigVMPythonUtils::Print(GetGraphOuterName(), 
				FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_function_reference_node(function_blueprint.get_local_function_library().find_function('%s'), %s, '%s')"),
						*GraphName,
						*FunctionDefinitionName,
						*RigVMPythonUtils::Vector2DToPythonString(InFunctionDefinition->GetPosition()), 
						*FunctionDefinitionName));
		}
		
	}

	return FunctionRefNode;
}

bool URigVMController::SetRemappedVariable(URigVMFunctionReferenceNode* InFunctionRefNode,
	const FName& InInnerVariableName, const FName& InOuterVariableName, bool bSetupUndoRedo)
{
	if(!InFunctionRefNode)
	{
		return false;
	}

	if (!IsValidGraph())
	{
		return false;
	}

	if(InInnerVariableName.IsNone())
	{
		return false;
	}

	const FName OldOuterVariableName = InFunctionRefNode->GetOuterVariableName(InInnerVariableName);
	if(OldOuterVariableName == InOuterVariableName)
	{
		return false;
	}

	if(!InFunctionRefNode->RequiresVariableRemapping())
	{
		return false;
	}
	
	URigVMGraph* Graph = GetGraph();
	check(Graph);

	FRigVMExternalVariable InnerExternalVariable;
	{
		FRigVMControllerGraphGuard GraphGuard(this, InFunctionRefNode->GetContainedGraph());
		InnerExternalVariable = GetVariableByName(InInnerVariableName);
	}

	if(!InnerExternalVariable.IsValid(true))
	{
		ReportErrorf(TEXT("External variable '%s' cannot be found."), *InInnerVariableName.ToString());
		return false;
	}

	ensure(InnerExternalVariable.Name == InInnerVariableName);

	if(InOuterVariableName.IsNone())
	{
		InFunctionRefNode->Modify();
		InFunctionRefNode->VariableMap.Remove(InInnerVariableName);
	}
	else
	{
		const FRigVMExternalVariable OuterExternalVariable = GetVariableByName(InOuterVariableName);
		if(!OuterExternalVariable.IsValid(true))
		{
			ReportErrorf(TEXT("External variable '%s' cannot be found."), *InOuterVariableName.ToString());
			return false;
		}

		ensure(OuterExternalVariable.Name == InOuterVariableName);

		if((InnerExternalVariable.TypeObject != nullptr) && (InnerExternalVariable.TypeObject != OuterExternalVariable.TypeObject))
		{
			ReportErrorf(TEXT("Inner and Outer External variables '%s' and '%s' are not compatible."), *InInnerVariableName.ToString(), *InOuterVariableName.ToString());
			return false;
		}
		if((InnerExternalVariable.TypeObject == nullptr) && (InnerExternalVariable.TypeName != OuterExternalVariable.TypeName))
		{
			ReportErrorf(TEXT("Inner and Outer External variables '%s' and '%s' are not compatible."), *InInnerVariableName.ToString(), *InOuterVariableName.ToString());
			return false;
		}

		InFunctionRefNode->Modify();
		InFunctionRefNode->VariableMap.FindOrAdd(InInnerVariableName) = InOuterVariableName;
	}

	Notify(ERigVMGraphNotifType::VariableRemappingChanged, InFunctionRefNode);

	FRigVMControllerCompileBracketScope CompileScope(this);
	if(bSetupUndoRedo)
	{
		ActionStack->AddAction(FRigVMSetRemappedVariableAction(InFunctionRefNode, InInnerVariableName, OldOuterVariableName, InOuterVariableName));
	}
	
	return true;
}

URigVMLibraryNode* URigVMController::AddFunctionToLibrary(const FName& InFunctionName, bool bMutable, const FVector2D& InNodePosition, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (!Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Can only add function definitions to function library graphs."));
		return nullptr;
	}

	FString FunctionName = GetValidNodeName(InFunctionName.IsNone() ? FString(TEXT("Function")) : InFunctionName.ToString());
	URigVMCollapseNode* CollapseNode = NewObject<URigVMCollapseNode>(Graph, *FunctionName);
	CollapseNode->ContainedGraph = NewObject<URigVMGraph>(CollapseNode, TEXT("ContainedGraph"));
	CollapseNode->Position = InNodePosition;
	Graph->Nodes.Add(CollapseNode);

	FRigVMControllerCompileBracketScope CompileScope(this);
	
	if (bMutable)
	{
		URigVMPin* ExecutePin = NewObject<URigVMPin>(CollapseNode, FRigVMStruct::ExecuteContextName);
		ExecutePin->CPPType = FString::Printf(TEXT("F%s"), *ExecuteContextStruct->GetName());
		ExecutePin->CPPTypeObject = ExecuteContextStruct;
		ExecutePin->CPPTypeObjectPath = *ExecutePin->CPPTypeObject->GetPathName();
		ExecutePin->Direction = ERigVMPinDirection::IO;
		AddNodePin(CollapseNode, ExecutePin);
	}

	Notify(ERigVMGraphNotifType::NodeAdded, CollapseNode);

	{
		FRigVMControllerGraphGuard GraphGuard(this, CollapseNode->GetContainedGraph(), false);

		URigVMFunctionEntryNode* EntryNode = NewObject<URigVMFunctionEntryNode>(CollapseNode->ContainedGraph, TEXT("Entry"));
		CollapseNode->ContainedGraph->Nodes.Add(EntryNode);
		EntryNode->Position = FVector2D(-250.f, 0.f);
		RefreshFunctionPins(EntryNode, false);
		Notify(ERigVMGraphNotifType::NodeAdded, EntryNode);

		URigVMFunctionReturnNode* ReturnNode = NewObject<URigVMFunctionReturnNode>(CollapseNode->ContainedGraph, TEXT("Return"));
		CollapseNode->ContainedGraph->Nodes.Add(ReturnNode);
		ReturnNode->Position = FVector2D(250.f, 0.f);
		RefreshFunctionPins(ReturnNode, false);
		Notify(ERigVMGraphNotifType::NodeAdded, ReturnNode);

		if (bMutable)
		{
			AddLink(EntryNode->FindPin(FRigVMStruct::ExecuteContextName.ToString()), ReturnNode->FindPin(FRigVMStruct::ExecuteContextName.ToString()), false);
		}
	}

	if (bSetupUndoRedo)
	{
		FRigVMInverseAction InverseAction;
		InverseAction.Title = TEXT("Add function to library");

		ActionStack->BeginAction(InverseAction);
		ActionStack->AddAction(FRigVMRemoveNodeAction(CollapseNode, this));
		ActionStack->EndAction(InverseAction);
	}

	if (bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());

		//AddFunctionToLibrary(const FName& InFunctionName, bool bMutable, const FVector2D& InNodePosition, bool bSetupUndoRedo, bool bPrintPythonCommand)
		RigVMPythonUtils::Print(GetGraphOuterName(), 
			FString::Printf(TEXT("library_controller.add_function_to_library('%s', %s, %s)"),
				*GetSanitizedNodeName(InFunctionName.ToString()),
				(bMutable) ? TEXT("True") : TEXT("False"),
				*RigVMPythonUtils::Vector2DToPythonString(InNodePosition)));
	}

	return CollapseNode;
}

bool URigVMController::RemoveFunctionFromLibrary(const FName& InFunctionName, bool bSetupUndoRedo)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);


	if (!Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Can only remove function definitions from function library graphs."));
		return false;
	}

	return RemoveNodeByName(InFunctionName, bSetupUndoRedo);
}

bool URigVMController::RenameFunction(const FName& InOldFunctionName, const FName& InNewFunctionName, bool bSetupUndoRedo)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);


	if (!Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Can only remove function definitions from function library graphs."));
		return false;
	}

	URigVMNode* Node = Graph->FindNode(InOldFunctionName.ToString());
	if (!Node)
	{
		ReportErrorf(TEXT("Could not find function called '%s'."), *InOldFunctionName.ToString());
		return false;
	}

	return RenameNode(Node, InNewFunctionName, bSetupUndoRedo);
}

FRigVMGraphVariableDescription URigVMController::AddLocalVariable(const FName& InVariableName, const FString& InCPPType, UObject* InCPPTypeObject, const FString& InDefaultValue, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	FRigVMGraphVariableDescription NewVariable;
	if (!IsValidGraph())
	{
		return NewVariable;
	}
	
	URigVMGraph* Graph = GetGraph();
	check(Graph);

	// Check this is the main graph of a function
	{
		if (URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(Graph->GetOuter()))
		{
			if (!LibraryNode->GetOuter()->IsA<URigVMFunctionLibrary>())
			{
				return NewVariable;
			}
		}
		else
		{
			return NewVariable;
		}
	}

	FName VariableName = GetUniqueName(InVariableName, [Graph](const FName& InName) {
		for (FRigVMGraphVariableDescription LocalVariable : Graph->GetLocalVariables(true))
		{
			if (LocalVariable.Name == InName)
			{
				return false;
			}
		}
		return true;
	}, false, true);

	NewVariable.Name = VariableName;
	NewVariable.CPPType = InCPPType;
	NewVariable.CPPTypeObject = InCPPTypeObject;
	NewVariable.DefaultValue = InDefaultValue;

	Graph->LocalVariables.Add(NewVariable);

	FRigVMControllerCompileBracketScope CompileScope(this);
	
	for (URigVMNode* Node : Graph->GetNodes())
	{
		if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
		{
			if (VariableName == VariableNode->GetVariableName())
			{
				RefreshVariableNode(VariableNode->GetFName(), VariableName, InCPPType, InCPPTypeObject, bSetupUndoRedo, false);
			}
		}
	}

	if (bSetupUndoRedo)
	{
		FRigVMInverseAction InverseAction;
		InverseAction.Title = FString::Printf(TEXT("Add Local Variable %s"), *InVariableName.ToString());

		ActionStack->BeginAction(InverseAction);
		ActionStack->AddAction(FRigVMRemoveLocalVariableAction(NewVariable));
		ActionStack->EndAction(InverseAction);
	}

	if (!bSuspendNotifications)
	{
		Graph->MarkPackageDirty();
	}

	if (bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());

		RigVMPythonUtils::Print(GetGraphOuterName(), 
			FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_local_variable_from_object_path('%s', '%s', '%s', '%s')"),
				*GraphName,
				*NewVariable.Name.ToString(),
				*NewVariable.CPPType,
				(NewVariable.CPPTypeObject) ? *NewVariable.CPPTypeObject->GetPathName() : *FString(),
				*NewVariable.DefaultValue));
	}

	return NewVariable;
}

FRigVMGraphVariableDescription URigVMController::AddLocalVariableFromObjectPath(const FName& InVariableName, const FString& InCPPType, const FString& InCPPTypeObjectPath, const FString& InDefaultValue, bool bSetupUndoRedo)
{
	FRigVMGraphVariableDescription Description;
	if (!IsValidGraph())
	{
		return Description;
	}

	UObject* CPPTypeObject = nullptr;
	if (!InCPPTypeObjectPath.IsEmpty())
	{
		CPPTypeObject = URigVMPin::FindObjectFromCPPTypeObjectPath<UObject>(InCPPTypeObjectPath);
		if (CPPTypeObject == nullptr)
		{
			ReportErrorf(TEXT("Cannot find cpp type object for path '%s'."), *InCPPTypeObjectPath);
			return Description;
		}
	}

	return AddLocalVariable(InVariableName, InCPPType, CPPTypeObject, InDefaultValue, bSetupUndoRedo);
}

bool URigVMController::RemoveLocalVariable(const FName& InVariableName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	TArray<FRigVMGraphVariableDescription>& LocalVariables = Graph->LocalVariables;
	int32 FoundIndex = INDEX_NONE;
	for (int32 Index = 0; Index < LocalVariables.Num(); ++Index)
	{
		if (LocalVariables[Index].Name == InVariableName)
		{
			FoundIndex = Index;
			break;
		}
	}

	if (FoundIndex != INDEX_NONE)
	{
		FRigVMControllerCompileBracketScope CompileScope(this);
		FRigVMBaseAction BaseAction;
		if (bSetupUndoRedo)
		{
			BaseAction.Title = FString::Printf(TEXT("Remove Local Variable %s"), *InVariableName.ToString());
			ActionStack->BeginAction(BaseAction);			
		}	
		
		const FString VarNameStr = InVariableName.ToString();

		bool bSwitchToMemberVariable = false;
		FRigVMExternalVariable ExternalVariableToSwitch;
		{
			TArray<FRigVMExternalVariable> ExternalVariables;
			if (GetExternalVariablesDelegate.IsBound())
			{
				ExternalVariables.Append(GetExternalVariablesDelegate.Execute(GetGraph()));
			}

			for (const FRigVMExternalVariable& ExternalVariable : ExternalVariables)
			{
				if (ExternalVariable.Name == InVariableName)
				{
					bSwitchToMemberVariable = true;
					ExternalVariableToSwitch = ExternalVariable;
					break;
				}	
			}
		}

		if (!bSwitchToMemberVariable)
		{
			TArray<URigVMNode*> Nodes = Graph->GetNodes();
			for (URigVMNode* Node : Nodes)
			{
				if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
				{
					if (URigVMPin* VariablePin = VariableNode->FindPin(URigVMVariableNode::VariableName))
					{
						if (VariablePin->GetDefaultValue() == VarNameStr)
						{
							RemoveNode(Node, bSetupUndoRedo, true);
							continue;
						}
					}
				}
			}
		}
		else
		{
			TArray<URigVMNode*> Nodes = Graph->GetNodes();
			for (URigVMNode* Node : Nodes)
			{
				if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
				{
					if (URigVMPin* VariablePin = VariableNode->FindPin(URigVMVariableNode::VariableName))
					{
						if (VariablePin->GetDefaultValue() == VarNameStr)
						{
							RefreshVariableNode(VariableNode->GetFName(), ExternalVariableToSwitch.Name, ExternalVariableToSwitch.TypeName.ToString(), ExternalVariableToSwitch.TypeObject, bSetupUndoRedo, false);
							continue;
						}
					}
				}

				TArray<URigVMPin*> AllPins = Node->GetAllPinsRecursively();
				for (URigVMPin* Pin : AllPins)
				{
					if (Pin->GetBoundVariableName() == InVariableName.ToString())
					{
						if (Pin->GetCPPType() != ExternalVariableToSwitch.TypeName.ToString() || Pin->GetCPPTypeObject() == ExternalVariableToSwitch.TypeObject)
						{
							UnbindPinFromVariable(Pin, bSetupUndoRedo);
						}
					}
				}
			}		
		}

		if (!bSuspendNotifications)
		{
			Graph->MarkPackageDirty();
		}

		if (bSetupUndoRedo)
		{
			ActionStack->AddAction(FRigVMRemoveLocalVariableAction(LocalVariables[FoundIndex]));
		}
		LocalVariables.RemoveAt(FoundIndex);

		if (bSetupUndoRedo)
		{
			ActionStack->EndAction(BaseAction);
		}

		if (bPrintPythonCommand)
		{
			const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());

			RigVMPythonUtils::Print(GetGraphOuterName(), 
				FString::Printf(TEXT("blueprint.get_controller_by_name('%s').remove_local_variable('%s')"),
					*GraphName,
					*GetSanitizedVariableName(InVariableName.ToString())));
		}
		return true;
	}

	return false;
}

bool URigVMController::RenameLocalVariable(const FName& InVariableName, const FName& InNewVariableName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	TArray<FRigVMGraphVariableDescription>& LocalVariables = Graph->LocalVariables;
	int32 FoundIndex = INDEX_NONE;
	for (int32 Index = 0; Index < LocalVariables.Num(); ++Index)
	{
		if (LocalVariables[Index].Name == InVariableName)
		{
			FoundIndex = Index;
			break;
		}
	}

	if (FoundIndex == INDEX_NONE)
	{
		return false;
	}

	for (int32 Index = 0; Index < LocalVariables.Num(); ++Index)
	{
		if (LocalVariables[Index].Name == InNewVariableName)
		{
			return false;
		}
	}
	
	if (!bSuspendNotifications)
	{
		Graph->MarkPackageDirty();
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	if (bSetupUndoRedo)
	{
		FRigVMInverseAction InverseAction;
		InverseAction.Title = FString::Printf(TEXT("Rename Local Variable %s to %s"), *InVariableName.ToString(), *InNewVariableName.ToString());

		ActionStack->BeginAction(InverseAction);
		ActionStack->AddAction(FRigVMRenameLocalVariableAction(LocalVariables[FoundIndex].Name, InNewVariableName));
		ActionStack->EndAction(InverseAction);
	}	
	
	LocalVariables[FoundIndex].Name = InNewVariableName;

	TArray<URigVMNode*> RenamedNodes;
	for (URigVMNode* Node : Graph->Nodes)
	{
		if(URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
		{
			if (VariableNode->GetVariableName() == InVariableName)
			{
				VariableNode->FindPin(URigVMVariableNode::VariableName)->DefaultValue = InNewVariableName.ToString();
				RenamedNodes.Add(Node);
			}
		}
	}

	for (URigVMNode* RenamedNode : RenamedNodes)
	{
		Notify(ERigVMGraphNotifType::VariableRenamed, RenamedNode);
		if (!bSuspendNotifications)
		{
			Graph->MarkPackageDirty();
		}
	}

	if (bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());

		RigVMPythonUtils::Print(GetGraphOuterName(), 
			FString::Printf(TEXT("blueprint.get_controller_by_name('%s').rename_local_variable('%s', '%s')"),
				*GraphName,
				*GetSanitizedVariableName(InVariableName.ToString()),
				*GetSanitizedVariableName(InNewVariableName.ToString())));
	}

	return true;
}

bool URigVMController::SetLocalVariableType(const FName& InVariableName, const FString& InCPPType,
                                            UObject* InCPPTypeObject, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	TArray<FRigVMGraphVariableDescription>& LocalVariables = Graph->LocalVariables;
	int32 FoundIndex = INDEX_NONE;
	for (int32 Index = 0; Index < LocalVariables.Num(); ++Index)
	{
		if (LocalVariables[Index].Name == InVariableName)
		{
			FoundIndex = Index;
			break;
		}
	}

	if (FoundIndex == INDEX_NONE)
	{
		return false;
	}

	if (!bSuspendNotifications)
	{
		Graph->MarkPackageDirty();
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMBaseAction BaseAction;
	if (bSetupUndoRedo)
	{
		BaseAction.Title = FString::Printf(TEXT("Change Local Variable type %s to %s"), *InVariableName.ToString(), *InCPPType);
		ActionStack->BeginAction(BaseAction);

		ActionStack->AddAction(FRigVMChangeLocalVariableTypeAction(LocalVariables[FoundIndex], InCPPType, InCPPTypeObject));
	}	
	
	LocalVariables[FoundIndex].CPPType = InCPPType;
	LocalVariables[FoundIndex].CPPTypeObject = InCPPTypeObject;

	// Set default value
	if (UScriptStruct* ScriptStruct = Cast<UScriptStruct>(InCPPTypeObject))
	{
		FString DefaultValue;
		CreateDefaultValueForStructIfRequired(ScriptStruct, DefaultValue);
		LocalVariables[FoundIndex].DefaultValue = DefaultValue;
	}
	else
	{
		LocalVariables[FoundIndex].DefaultValue = FString();
	}

	// Change pin types on variable nodes
	TArray<URigVMNode*> Nodes = Graph->GetNodes();
	for (URigVMNode* Node : Nodes)
	{
		if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
		{
			if (URigVMPin* VariablePin = VariableNode->FindPin(URigVMVariableNode::VariableName))
			{
				if (VariablePin->GetDefaultValue() == InVariableName.ToString())
				{
					RefreshVariableNode(Node->GetFName(), InVariableName, InCPPType, InCPPTypeObject, bSetupUndoRedo, false);
					continue;
				}
			}
		}

		const TArray<URigVMPin*> AllPins = Node->GetAllPinsRecursively();
		for (URigVMPin* Pin : AllPins)
		{
			if (Pin->GetBoundVariableName() == InVariableName.ToString())
			{
				UnbindPinFromVariable(Pin, bSetupUndoRedo);
			}
		}
	}

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(BaseAction);
	}

	if (bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());

		//bool URigVMController::SetLocalVariableType(const FName& InVariableName, const FString& InCPPType, UObject* InCPPTypeObject, bool bSetupUndoRedo, bool bPrintPythonCommand)
		RigVMPythonUtils::Print(GetGraphOuterName(), 
			FString::Printf(TEXT("blueprint.get_controller_by_name('%s').set_local_variable_type_from_object_path('%s', '%s', '%s')"),
				*GraphName,
				*GetSanitizedVariableName(InVariableName.ToString()),
				*InCPPType,
				(InCPPTypeObject) ? *InCPPTypeObject->GetPathName() : *FString()));
	}
	
	return true;
}

bool URigVMController::SetLocalVariableTypeFromObjectPath(const FName& InVariableName, const FString& InCPPType, const FString& InCPPTypeObjectPath, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	UObject* CPPTypeObject = nullptr;
	if (!InCPPTypeObjectPath.IsEmpty())
	{
		CPPTypeObject = URigVMPin::FindObjectFromCPPTypeObjectPath<UObject>(InCPPTypeObjectPath);
		if (CPPTypeObject == nullptr)
		{
			ReportErrorf(TEXT("Cannot find cpp type object for path '%s'."), *InCPPTypeObjectPath);
			return false;
		}
	}

	return SetLocalVariableType(InVariableName, InCPPType, CPPTypeObject, bSetupUndoRedo, bPrintPythonCommand);
}

bool URigVMController::SetLocalVariableDefaultValue(const FName& InVariableName, const FString& InDefaultValue, bool bSetupUndoRedo, bool bPrintPythonCommand, bool bNotify)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	TArray<FRigVMGraphVariableDescription>& LocalVariables = Graph->LocalVariables;
	int32 FoundIndex = INDEX_NONE;
	for (int32 Index = 0; Index < LocalVariables.Num(); ++Index)
	{
		if (LocalVariables[Index].Name == InVariableName)
		{
			FoundIndex = Index;
			break;
		}
	}

	if (FoundIndex == INDEX_NONE)
	{
		return false;
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	if (bSetupUndoRedo)
	{
		FRigVMInverseAction InverseAction;
		InverseAction.Title = FString::Printf(TEXT("Change Local Variable %s default value"), *InVariableName.ToString());

		ActionStack->BeginAction(InverseAction);
		ActionStack->AddAction(FRigVMChangeLocalVariableDefaultValueAction(LocalVariables[FoundIndex], InDefaultValue));
		ActionStack->EndAction(InverseAction);
	}	

	FRigVMGraphVariableDescription& VariableDescription = LocalVariables[FoundIndex];
	VariableDescription.DefaultValue = InDefaultValue;
	
	// Refresh variable nodes to reflect change in default value
	TArray<URigVMNode*> Nodes = Graph->GetNodes();
	for (URigVMNode* Node : Nodes)
	{
		if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
		{
			if (URigVMPin* VariablePin = VariableNode->FindPin(URigVMVariableNode::VariableName))
			{
				if (VariablePin->GetDefaultValue() == InVariableName.ToString())
				{
					SetPinDefaultValue(VariableNode->FindPin(URigVMVariableNode::ValueName), InDefaultValue, true, true, true, bNotify);
				}
			}
		}
	}

	if (!bSuspendNotifications)
	{
		Graph->MarkPackageDirty();
	}

	if (bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());

		RigVMPythonUtils::Print(GetGraphOuterName(), 
			FString::Printf(TEXT("blueprint.get_controller_by_name('%s').set_local_variable_default_value('%s', '%s')"),
				*GraphName,
				*GetSanitizedVariableName(InVariableName.ToString()),
				*InDefaultValue));
	}
	
	return true;
}

TArray<TSoftObjectPtr<URigVMFunctionReferenceNode>> URigVMController::GetAffectedReferences(ERigVMControllerBulkEditType InEditType, bool bForceLoad, bool bNotify)
{
	TArray<TSoftObjectPtr<URigVMFunctionReferenceNode>> FunctionReferencePtrs;
	
#if WITH_EDITOR

	check(IsValidGraph());
	URigVMGraph* Graph = GetGraph();
	URigVMFunctionLibrary* FunctionLibrary = Graph->GetTypedOuter<URigVMFunctionLibrary>();
	if(FunctionLibrary == nullptr)
	{
		return FunctionReferencePtrs;
	}

	URigVMLibraryNode* Function = FunctionLibrary->FindFunctionForNode(Graph->GetTypedOuter<URigVMCollapseNode>());
	if(Function == nullptr)
	{
		return FunctionReferencePtrs;
	}

	// get the immediate references
	FunctionReferencePtrs = FunctionLibrary->GetReferencesForFunction(Function->GetFName());
	TMap<FString, int32> VisitedPaths;
	
	for(int32 FunctionReferenceIndex = 0; FunctionReferenceIndex < FunctionReferencePtrs.Num(); FunctionReferenceIndex++)
	{
		TSoftObjectPtr<URigVMFunctionReferenceNode> FunctionReferencePtr = FunctionReferencePtrs[FunctionReferenceIndex];
		VisitedPaths.Add(FunctionReferencePtr.ToSoftObjectPath().ToString(), FunctionReferenceIndex);
	}

	for(int32 FunctionReferenceIndex = 0; FunctionReferenceIndex < FunctionReferencePtrs.Num(); FunctionReferenceIndex++)
	{
		TSoftObjectPtr<URigVMFunctionReferenceNode> FunctionReferencePtr = FunctionReferencePtrs[FunctionReferenceIndex];

		if(bForceLoad)
		{
			if(OnBulkEditProgressDelegate.IsBound() && bNotify)
			{
				OnBulkEditProgressDelegate.Execute(FunctionReferencePtr, InEditType, ERigVMControllerBulkEditProgress::BeginLoad, FunctionReferenceIndex, FunctionReferencePtrs.Num());
			}

			if(!FunctionReferencePtr.IsValid())
			{
				FunctionReferencePtr.LoadSynchronous();
			}

			if(OnBulkEditProgressDelegate.IsBound() && bNotify)
			{
				OnBulkEditProgressDelegate.Execute(FunctionReferencePtr, InEditType, ERigVMControllerBulkEditProgress::FinishedLoad, FunctionReferenceIndex, FunctionReferencePtrs.Num());
			}
		}

		// adding pins / renaming doesn't cause any recursion, so we can stop here
		if((InEditType == ERigVMControllerBulkEditType::AddExposedPin) ||
			(InEditType == ERigVMControllerBulkEditType::RemoveExposedPin) ||
			(InEditType == ERigVMControllerBulkEditType::RenameExposedPin) ||
			(InEditType == ERigVMControllerBulkEditType::ChangeExposedPinType) ||
            (InEditType == ERigVMControllerBulkEditType::RenameVariable))
		{
			continue;
		}

		// for loaded assets we'll recurse now
		if(FunctionReferencePtr.IsValid())
		{
			if(URigVMFunctionReferenceNode* AffectedFunctionReferenceNode = FunctionReferencePtr.Get())
			{
				if(URigVMFunctionLibrary* AffectedFunctionLibrary = AffectedFunctionReferenceNode->GetTypedOuter<URigVMFunctionLibrary>())
				{
					if(URigVMLibraryNode* AffectedFunction = AffectedFunctionLibrary->FindFunctionForNode(AffectedFunctionReferenceNode))
					{
						FRigVMControllerGraphGuard GraphGuard(this, AffectedFunction->GetContainedGraph(), false);
						TArray<TSoftObjectPtr<URigVMFunctionReferenceNode>> AffectedFunctionReferencePtrs = GetAffectedReferences(InEditType, bForceLoad, false);
						for(TSoftObjectPtr<URigVMFunctionReferenceNode> AffectedFunctionReferencePtr : AffectedFunctionReferencePtrs)
						{
							const FString Key = AffectedFunctionReferencePtr.ToSoftObjectPath().ToString();
							if(VisitedPaths.Contains(Key))
							{
								continue;
							}
							VisitedPaths.Add(Key, FunctionReferencePtrs.Add(AffectedFunctionReferencePtr));
						}
					}
				}
			}
		}
	}
	
#endif

	return FunctionReferencePtrs;
}

TArray<FAssetData> URigVMController::GetAffectedAssets(ERigVMControllerBulkEditType InEditType, bool bForceLoad, bool bNotify)
{
	TArray<FAssetData> Assets;

#if WITH_EDITOR

	if(!IsValidGraph())
	{
		return Assets;
	}

	TArray<TSoftObjectPtr<URigVMFunctionReferenceNode>> FunctionReferencePtrs = GetAffectedReferences(InEditType, bForceLoad, bNotify);
	TMap<FString, int32> VisitedAssets;

	URigVMGraph* Graph = GetGraph();
	TSoftObjectPtr<URigVMGraph> GraphPtr = Graph;
	const FString ThisAssetPath = GraphPtr.ToSoftObjectPath().GetAssetPathName().ToString();

	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	for(int32 FunctionReferenceIndex = 0; FunctionReferenceIndex < FunctionReferencePtrs.Num(); FunctionReferenceIndex++)
	{
		TSoftObjectPtr<URigVMFunctionReferenceNode> FunctionReferencePtr = FunctionReferencePtrs[FunctionReferenceIndex];
		const FString AssetPath = FunctionReferencePtr.ToSoftObjectPath().GetAssetPathName().ToString();
		if(AssetPath.StartsWith(TEXT("/Engine/Transient")))
		{
			continue;
		}
		if(VisitedAssets.Contains(AssetPath))
		{
			continue;
		}
		if(AssetPath == ThisAssetPath)
		{
			continue;
		}
					
		const FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(*AssetPath);
		if(AssetData.IsValid())
		{
			VisitedAssets.Add(AssetPath, Assets.Add(AssetData));
		}
	}
	
#endif

	return Assets;
}

void URigVMController::ExpandPinRecursively(URigVMPin* InPin, bool bSetupUndoRedo)
{
	if (InPin == nullptr)
	{
		return;
	}

	if (bSetupUndoRedo)
	{
		OpenUndoBracket(TEXT("Expand Pin Recursively"));
	}

	bool bExpandedSomething = false;
	while (InPin)
	{
		if (SetPinExpansion(InPin, true, bSetupUndoRedo))
		{
			bExpandedSomething = true;
		}
		InPin = InPin->GetParentPin();
	}

	if (bSetupUndoRedo)
	{
		if (bExpandedSomething)
		{
			CloseUndoBracket();
		}
		else
		{
			CancelUndoBracket();
		}
	}
}

bool URigVMController::SetVariableName(URigVMVariableNode* InVariableNode, const FName& InVariableName, bool bSetupUndoRedo)
{
	if (!IsValidNodeForGraph(InVariableNode))
	{
		return false;
	}

	if (InVariableNode->GetVariableName() == InVariableName)
	{
		return false;
	}

	if (InVariableName == NAME_None)
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	TArray<FRigVMGraphVariableDescription> Descriptions = Graph->GetVariableDescriptions();
	TMap<FName, int32> NameToIndex;
	for (int32 VariableIndex = 0; VariableIndex < Descriptions.Num(); VariableIndex++)
	{
		NameToIndex.Add(Descriptions[VariableIndex].Name, VariableIndex);
	}

	FName VariableName = GetUniqueName(InVariableName, [Descriptions, NameToIndex, InVariableNode](const FName& InName) {
		const int32* FoundIndex = NameToIndex.Find(InName);
		if (FoundIndex == nullptr)
		{
			return true;
		}
		return InVariableNode->GetCPPType() == Descriptions[*FoundIndex].CPPType;
	}, false, true);

	int32 NodesSharingName = 0;
	for (URigVMNode* Node : Graph->Nodes)
	{
		if (URigVMVariableNode* OtherVariableNode = Cast<URigVMVariableNode>(Node))
		{
			if (OtherVariableNode->GetVariableName() == InVariableNode->GetVariableName())
			{
				NodesSharingName++;
			}
		}
	}

	if (NodesSharingName == 1)
	{
		Notify(ERigVMGraphNotifType::VariableRemoved, InVariableNode);
	}

	SetPinDefaultValue(InVariableNode->FindPin(URigVMVariableNode::VariableName), VariableName.ToString(), false, bSetupUndoRedo, false);

	Notify(ERigVMGraphNotifType::VariableAdded, InVariableNode);
	Notify(ERigVMGraphNotifType::VariableRenamed, InVariableNode);

	return true;
}

bool URigVMController::SetParameterName(URigVMParameterNode* InParameterNode, const FName& InParameterName, bool bSetupUndoRedo)
{
	if (!IsValidNodeForGraph(InParameterNode))
	{
		return false;
	}

	if (InParameterNode->GetParameterName() == InParameterName)
	{
		return false;
	}

	if (InParameterName == NAME_None)
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	TArray<FRigVMGraphParameterDescription> Descriptions = Graph->GetParameterDescriptions();
	TMap<FName, int32> NameToIndex;
	for (int32 ParameterIndex = 0; ParameterIndex < Descriptions.Num(); ParameterIndex++)
	{
		NameToIndex.Add(Descriptions[ParameterIndex].Name, ParameterIndex);
	}

	FName ParameterName = GetUniqueName(InParameterName, [Descriptions, NameToIndex, InParameterNode](const FName& InName) {
		const int32* FoundIndex = NameToIndex.Find(InName);
		if (FoundIndex == nullptr)
		{
			return true;
		}
		return InParameterNode->GetCPPType() == Descriptions[*FoundIndex].CPPType && InParameterNode->IsInput() == Descriptions[*FoundIndex].bIsInput;
	}, false, true);

	int32 NodesSharingName = 0;
	for (URigVMNode* Node : Graph->Nodes)
	{
		if (URigVMParameterNode* OtherParameterNode = Cast<URigVMParameterNode>(Node))
		{
			if (OtherParameterNode->GetParameterName() == InParameterNode->GetParameterName())
			{
				NodesSharingName++;
			}
		}
	}

	if (NodesSharingName == 1)
	{
		Notify(ERigVMGraphNotifType::ParameterRemoved, InParameterNode);
	}

	SetPinDefaultValue(InParameterNode->FindPin(URigVMParameterNode::ParameterName), ParameterName.ToString(), false, bSetupUndoRedo, false);

	Notify(ERigVMGraphNotifType::ParameterAdded, InParameterNode);

	return true;
}

URigVMRerouteNode* URigVMController::AddFreeRerouteNode(bool bShowAsFullNode, const FString& InCPPType, const FName& InCPPTypeObjectPath, bool bIsConstant, const FName& InCustomWidgetName, const FString& InDefaultValue, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot add reroutes to function library graphs."));
		return nullptr;
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMBaseAction Action;
	if (bSetupUndoRedo)
	{
		Action.Title = FString::Printf(TEXT("Add Reroute"));
		ActionStack->BeginAction(Action);
	}

	FString Name = GetValidNodeName(InNodeName.IsEmpty() ? FString(TEXT("RerouteNode")) : InNodeName);
	URigVMRerouteNode* Node = NewObject<URigVMRerouteNode>(Graph, *Name);
	Node->Position = InPosition;
	Node->bShowAsFullNode = bShowAsFullNode;

	URigVMPin* ValuePin = NewObject<URigVMPin>(Node, *URigVMRerouteNode::ValueName);
	ValuePin->CPPType = InCPPType;
	ValuePin->CPPTypeObjectPath = InCPPTypeObjectPath;
	ValuePin->bIsConstant = bIsConstant;
	ValuePin->CustomWidgetName = InCustomWidgetName;
	ValuePin->Direction = ERigVMPinDirection::IO;
	AddNodePin(Node, ValuePin);
	Graph->Nodes.Add(Node);

	if (ValuePin->IsStruct())
	{
		FString DefaultValue = InDefaultValue;
		CreateDefaultValueForStructIfRequired(ValuePin->GetScriptStruct(), DefaultValue);
		AddPinsForStruct(ValuePin->GetScriptStruct(), Node, ValuePin, ValuePin->Direction, DefaultValue, false);
	}
	else if (!InDefaultValue.IsEmpty() && InDefaultValue != TEXT("()"))
	{
		SetPinDefaultValue(ValuePin, InDefaultValue, true, false, false);
	}

	if (bSetupUndoRedo)
	{
		ActionStack->AddAction(FRigVMAddRerouteNodeAction(Node));
	}

	Notify(ERigVMGraphNotifType::NodeAdded, Node);

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	return Node;
}

URigVMBranchNode* URigVMController::AddBranchNode(const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if(!IsValidGraph())
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	FString Name = GetValidNodeName(InNodeName.IsEmpty() ? FString(TEXT("BranchNode")) : InNodeName);
	URigVMBranchNode* Node = NewObject<URigVMBranchNode>(Graph, *Name);
	Node->Position = InPosition;

	URigVMPin* ExecutePin = NewObject<URigVMPin>(Node, FRigVMStruct::ExecuteContextName);
	ExecutePin->DisplayName = FRigVMStruct::ExecuteName;
	ExecutePin->CPPType = FString::Printf(TEXT("F%s"), *ExecuteContextStruct->GetName());
	ExecutePin->CPPTypeObject = ExecuteContextStruct;
	ExecutePin->CPPTypeObjectPath = *ExecutePin->CPPTypeObject->GetPathName();
	ExecutePin->Direction = ERigVMPinDirection::Input;
	AddNodePin(Node, ExecutePin);

	URigVMPin* ConditionPin = NewObject<URigVMPin>(Node, *URigVMBranchNode::ConditionName);
	ConditionPin->CPPType = TEXT("bool");
	ConditionPin->Direction = ERigVMPinDirection::Input;
	AddNodePin(Node, ConditionPin);

	URigVMPin* TruePin = NewObject<URigVMPin>(Node, *URigVMBranchNode::TrueName);
	TruePin->CPPType = ExecutePin->CPPType;
	TruePin->CPPTypeObject = ExecutePin->CPPTypeObject;
	TruePin->CPPTypeObjectPath = ExecutePin->CPPTypeObjectPath;
	TruePin->Direction = ERigVMPinDirection::Output;
	AddNodePin(Node, TruePin);

	URigVMPin* FalsePin = NewObject<URigVMPin>(Node, *URigVMBranchNode::FalseName);
	FalsePin->CPPType = ExecutePin->CPPType;
	FalsePin->CPPTypeObject = ExecutePin->CPPTypeObject;
	FalsePin->CPPTypeObjectPath = ExecutePin->CPPTypeObjectPath;
	FalsePin->Direction = ERigVMPinDirection::Output;
	AddNodePin(Node, FalsePin);

	Graph->Nodes.Add(Node);

	Notify(ERigVMGraphNotifType::NodeAdded, Node);

	FRigVMControllerCompileBracketScope CompileScope(this);
	if (bSetupUndoRedo)
	{
		ActionStack->AddAction(FRigVMAddBranchNodeAction(Node));
	}

	if (bPrintPythonCommand)
	{
		TArray<FString> Commands = GetAddNodePythonCommands(Node);
		for (const FString& Command : Commands)
		{
			RigVMPythonUtils::Print(GetGraphOuterName(), 
				FString::Printf(TEXT("%s"), *Command));
		}
	}

	return Node;
}

URigVMIfNode* URigVMController::AddIfNode(const FString& InCPPType, const FName& InCPPTypeObjectPath, const FVector2D& InPosition, const FString& InNodeName, bool  bSetupUndoRedo, bool bPrintPythonCommand)
{
	if(!IsValidGraph())
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	ensure(!InCPPType.IsEmpty());

	UObject* CPPTypeObject = nullptr;
	if(!InCPPTypeObjectPath.IsNone())
	{
		CPPTypeObject = URigVMPin::FindObjectFromCPPTypeObjectPath<UObject>(InCPPTypeObjectPath.ToString());
		if (CPPTypeObject == nullptr)
		{
			ReportErrorf(TEXT("Cannot find cpp type object for path '%s'."), *InCPPTypeObjectPath.ToString());
			return nullptr;
		}
	}

	FString CPPType = PostProcessCPPType(InCPPType, CPPTypeObject);

	FString DefaultValue;
	if(UScriptStruct* ScriptStruct = Cast<UScriptStruct>(CPPTypeObject))
	{
		if (ScriptStruct->IsChildOf(FRigVMExecuteContext::StaticStruct()))
		{
			ReportErrorf(TEXT("Cannot create an if node for this type '%s'."), *InCPPTypeObjectPath.ToString());
			return nullptr;
		}
		CreateDefaultValueForStructIfRequired(ScriptStruct, DefaultValue);
	}
	
	FString Name = GetValidNodeName(InNodeName.IsEmpty() ? FString(TEXT("IfNode")) : InNodeName);
	URigVMIfNode* Node = NewObject<URigVMIfNode>(Graph, *Name);
	Node->Position = InPosition;

	URigVMPin* ConditionPin = NewObject<URigVMPin>(Node, *URigVMIfNode::ConditionName);
	ConditionPin->CPPType = TEXT("bool");
	ConditionPin->Direction = ERigVMPinDirection::Input;
	AddNodePin(Node, ConditionPin);

	URigVMPin* TruePin = NewObject<URigVMPin>(Node, *URigVMIfNode::TrueName);
	TruePin->CPPType = CPPType;
	TruePin->CPPTypeObject = CPPTypeObject;
	TruePin->CPPTypeObjectPath = InCPPTypeObjectPath;
	TruePin->Direction = ERigVMPinDirection::Input;
	TruePin->DefaultValue = DefaultValue;
	AddNodePin(Node, TruePin);

	if (TruePin->IsStruct())
	{
		AddPinsForStruct(TruePin->GetScriptStruct(), Node, TruePin, TruePin->Direction, FString(), false);
	}

	URigVMPin* FalsePin = NewObject<URigVMPin>(Node, *URigVMIfNode::FalseName);
	FalsePin->CPPType = CPPType;
	FalsePin->CPPTypeObject = CPPTypeObject;
	FalsePin->CPPTypeObjectPath = InCPPTypeObjectPath;
	FalsePin->Direction = ERigVMPinDirection::Input;
	FalsePin->DefaultValue = DefaultValue;
	AddNodePin(Node, FalsePin);

	if (FalsePin->IsStruct())
	{
		AddPinsForStruct(FalsePin->GetScriptStruct(), Node, FalsePin, FalsePin->Direction, FString(), false);
	}

	URigVMPin* ResultPin = NewObject<URigVMPin>(Node, *URigVMIfNode::ResultName);
	ResultPin->CPPType = CPPType;
	ResultPin->CPPTypeObject = CPPTypeObject;
	ResultPin->CPPTypeObjectPath = InCPPTypeObjectPath;
	ResultPin->Direction = ERigVMPinDirection::Output;
	AddNodePin(Node, ResultPin);

	if (ResultPin->IsStruct())
	{
		AddPinsForStruct(ResultPin->GetScriptStruct(), Node, ResultPin, ResultPin->Direction, FString(), false);
	}

	Graph->Nodes.Add(Node);

	Notify(ERigVMGraphNotifType::NodeAdded, Node);

	FRigVMControllerCompileBracketScope CompileScope(this);
	if (bSetupUndoRedo)
	{
		ActionStack->AddAction(FRigVMAddIfNodeAction(Node));
	}
	
	if (bPrintPythonCommand)
	{
		TArray<FString> Commands = GetAddNodePythonCommands(Node);
		for (const FString& Command : Commands)
		{
			RigVMPythonUtils::Print(GetGraphOuterName(), 
				FString::Printf(TEXT("%s"), *Command));
		}
	}

	return Node;
}

URigVMIfNode* URigVMController::AddIfNodeFromStruct(UScriptStruct* InScriptStruct, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo)
{
	if (!InScriptStruct)
	{
		return nullptr;
	}

	return AddIfNode(InScriptStruct->GetStructCPPName(), FName(InScriptStruct->GetPathName()), InPosition, InNodeName, bSetupUndoRedo);
}

URigVMSelectNode* URigVMController::AddSelectNode(const FString& InCPPType, const FName& InCPPTypeObjectPath, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	ensure(!InCPPType.IsEmpty());

	UObject* CPPTypeObject = nullptr;
	if (!InCPPTypeObjectPath.IsNone())
	{
		CPPTypeObject = URigVMPin::FindObjectFromCPPTypeObjectPath<UObject>(InCPPTypeObjectPath.ToString());
		if (CPPTypeObject == nullptr)
		{
			ReportErrorf(TEXT("Cannot find cpp type object for path '%s'."), *InCPPTypeObjectPath.ToString());
			return nullptr;
		}
	}

	FString CPPType = PostProcessCPPType(InCPPType, CPPTypeObject);

	FString DefaultValue;
	if (UScriptStruct* ScriptStruct = Cast<UScriptStruct>(CPPTypeObject))
	{
		if (ScriptStruct->IsChildOf(FRigVMExecuteContext::StaticStruct()))
		{
			ReportErrorf(TEXT("Cannot create a select node for this type '%s'."), *InCPPTypeObjectPath.ToString());
			return nullptr;
		}
		CreateDefaultValueForStructIfRequired(ScriptStruct, DefaultValue);
	}

	FString Name = GetValidNodeName(InNodeName.IsEmpty() ? FString(TEXT("IfNode")) : InNodeName);
	URigVMSelectNode* Node = NewObject<URigVMSelectNode>(Graph, *Name);
	Node->Position = InPosition;

	URigVMPin* IndexPin = NewObject<URigVMPin>(Node, *URigVMSelectNode::IndexName);
	IndexPin->CPPType = TEXT("int32");
	IndexPin->Direction = ERigVMPinDirection::Input;
	AddNodePin(Node, IndexPin);

	URigVMPin* ValuePin = NewObject<URigVMPin>(Node, *URigVMSelectNode::ValueName);
	ValuePin->CPPType = FString::Printf(TArrayTemplate, *CPPType);
	ValuePin->CPPTypeObject = CPPTypeObject;
	ValuePin->CPPTypeObjectPath = InCPPTypeObjectPath;
	ValuePin->Direction = ERigVMPinDirection::Input;
	ValuePin->bIsExpanded = true;
	AddNodePin(Node, ValuePin);

	URigVMPin* ResultPin = NewObject<URigVMPin>(Node, *URigVMSelectNode::ResultName);
	ResultPin->CPPType = CPPType;
	ResultPin->CPPTypeObject = CPPTypeObject;
	ResultPin->CPPTypeObjectPath = InCPPTypeObjectPath;
	ResultPin->Direction = ERigVMPinDirection::Output;
	AddNodePin(Node, ResultPin);

	Graph->Nodes.Add(Node);

	Notify(ERigVMGraphNotifType::NodeAdded, Node);

	SetArrayPinSize(ValuePin->GetPinPath(), 2, DefaultValue, false);

	FRigVMControllerCompileBracketScope CompileScope(this);
	if (bSetupUndoRedo)
	{
		ActionStack->AddAction(FRigVMAddSelectNodeAction(Node));
	}

	if (bPrintPythonCommand)
	{
		TArray<FString> Commands = GetAddNodePythonCommands(Node);
		for (const FString& Command : Commands)
		{
			RigVMPythonUtils::Print(GetGraphOuterName(), 
				FString::Printf(TEXT("%s"), *Command));
		}
	}

	return Node;
}

URigVMSelectNode* URigVMController::AddSelectNodeFromStruct(UScriptStruct* InScriptStruct, const FVector2D& InPosition,
	const FString& InNodeName, bool bSetupUndoRedo)
{
	if (!InScriptStruct)
	{
		return nullptr;
	}

	return AddSelectNode(InScriptStruct->GetStructCPPName(), FName(InScriptStruct->GetPathName()), InPosition, InNodeName, bSetupUndoRedo);
}

URigVMPrototypeNode* URigVMController::AddPrototypeNode(const FName& InNotation, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	ensure(!InNotation.IsNone());

	const FRigVMPrototype* Prototype = FRigVMRegistry::Get().FindPrototype(InNotation);
	if (Prototype == nullptr)
	{
		ReportErrorf(TEXT("Prototype '%s' cannot be found."), *InNotation.ToString());
		return nullptr;
	}

	FString Name = GetValidNodeName(InNodeName.IsEmpty() ? Prototype->GetName().ToString() : InNodeName);
	URigVMPrototypeNode* Node = NewObject<URigVMPrototypeNode>(Graph, *Name);
	Node->PrototypeNotation = Prototype->GetNotation();
	Node->Position = InPosition;

	int32 FunctionIndex = INDEX_NONE;
	FRigVMPrototype::FTypeMap Types;
	Prototype->Resolve(Types, FunctionIndex);

	for (int32 ArgIndex = 0; ArgIndex < Prototype->NumArgs(); ArgIndex++)
	{
		const FRigVMPrototypeArg* Arg = Prototype->GetArg(ArgIndex);

		URigVMPin* Pin = NewObject<URigVMPin>(Node, Arg->GetName());
		const FRigVMPrototypeArg::FType& Type = Types.FindChecked(Arg->GetName());
		Pin->CPPType = Type.CPPType;
		Pin->CPPTypeObject = Type.CPPTypeObject;
		if (Pin->CPPTypeObject)
		{
			Pin->CPPTypeObjectPath = *Pin->CPPTypeObject->GetPathName();
		}
		Pin->Direction = Arg->GetDirection();
		AddNodePin(Node, Pin);
	}

	Graph->Nodes.Add(Node);

	Notify(ERigVMGraphNotifType::NodeAdded, Node);

	FRigVMControllerCompileBracketScope CompileScope(this);
	if (bSetupUndoRedo)
	{
		ActionStack->AddAction(FRigVMAddPrototypeNodeAction(Node));
	}

	if (bPrintPythonCommand)
	{
		TArray<FString> Commands = GetAddNodePythonCommands(Node);
		for (const FString& Command : Commands)
		{
			RigVMPythonUtils::Print(GetGraphOuterName(), 
				FString::Printf(TEXT("%s"), *Command));
		}
	}

	return Node;
}


URigVMEnumNode* URigVMController::AddEnumNode(const FName& InCPPTypeObjectPath, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

    UObject* CPPTypeObject = URigVMPin::FindObjectFromCPPTypeObjectPath<UObject>(InCPPTypeObjectPath.ToString());
	if (CPPTypeObject == nullptr)
	{
		ReportErrorf(TEXT("Cannot find cpp type object for path '%s'."), *InCPPTypeObjectPath.ToString());
		return nullptr;
	}

	UEnum* Enum = Cast<UEnum>(CPPTypeObject);
	if(Enum == nullptr)
	{
		ReportErrorf(TEXT("Cpp type object for path '%s' is not an enum."), *InCPPTypeObjectPath.ToString());
		return nullptr;
	}

	FString Name = GetValidNodeName(InNodeName.IsEmpty() ? FString(TEXT("IfNode")) : InNodeName);
	URigVMEnumNode* Node = NewObject<URigVMEnumNode>(Graph, *Name);
	Node->Position = InPosition;

	URigVMPin* EnumValuePin = NewObject<URigVMPin>(Node, *URigVMEnumNode::EnumValueName);
	EnumValuePin->CPPType = CPPTypeObject->GetName();
	EnumValuePin->CPPTypeObject = CPPTypeObject;
	EnumValuePin->CPPTypeObjectPath = InCPPTypeObjectPath;
	EnumValuePin->Direction = ERigVMPinDirection::Visible;
	EnumValuePin->DefaultValue = Enum->GetNameStringByValue(0);
	AddNodePin(Node, EnumValuePin);

	URigVMPin* EnumIndexPin = NewObject<URigVMPin>(Node, *URigVMEnumNode::EnumIndexName);
	EnumIndexPin->CPPType = TEXT("int32");
	EnumIndexPin->Direction = ERigVMPinDirection::Output;
	EnumIndexPin->DisplayName = TEXT("Result");
	AddNodePin(Node, EnumIndexPin);

	Graph->Nodes.Add(Node);

	Notify(ERigVMGraphNotifType::NodeAdded, Node);

	FRigVMControllerCompileBracketScope CompileScope(this);
	if (bSetupUndoRedo)
	{
		ActionStack->AddAction(FRigVMAddEnumNodeAction(Node));
	}

	if (bPrintPythonCommand)
	{
		TArray<FString> Commands = GetAddNodePythonCommands(Node);
		for (const FString& Command : Commands)
		{
			RigVMPythonUtils::Print(GetGraphOuterName(), 
				FString::Printf(TEXT("%s"), *Command));
		}
	}

	return Node;
}

URigVMArrayNode* URigVMController::AddArrayNode(ERigVMOpCode InOpCode, const FString& InCPPType,
	UObject* InCPPTypeObject, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo,
	bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

#if UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED

	ReportError(TEXT("This build of Control Rig doesn't support Array Nodes."));
	return nullptr;
	
#endif

	// validate the op code
	bool bIsMutable = false;
	switch(InOpCode)
	{
		case ERigVMOpCode::ArrayReset:
		case ERigVMOpCode::ArrayGetNum: 
		case ERigVMOpCode::ArraySetNum:
		case ERigVMOpCode::ArrayGetAtIndex:  
		case ERigVMOpCode::ArraySetAtIndex:
		case ERigVMOpCode::ArrayAdd:
		case ERigVMOpCode::ArrayInsert:
		case ERigVMOpCode::ArrayRemove:
		case ERigVMOpCode::ArrayFind:
		case ERigVMOpCode::ArrayAppend:
		case ERigVMOpCode::ArrayClone:
		case ERigVMOpCode::ArrayIterator:
		case ERigVMOpCode::ArrayUnion:
		case ERigVMOpCode::ArrayDifference:
		case ERigVMOpCode::ArrayIntersection:
		case ERigVMOpCode::ArrayReverse:
		{
			break;
		}
		default:
		{
			ReportErrorf(TEXT("OpCode '%s' is not valid for Array Node."), *StaticEnum<ERigVMOpCode>()->GetNameStringByValue((int64)InOpCode));
			return nullptr;
		}
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot add array nodes to function library graphs."));
		return nullptr;
	}

	if (InCPPTypeObject == nullptr)
	{
		InCPPTypeObject = URigVMCompiler::GetScriptStructForCPPType(InCPPType);
	}
	if (InCPPTypeObject == nullptr)
	{
		InCPPTypeObject = URigVMPin::FindObjectFromCPPTypeObjectPath<UObject>(InCPPType);
	}

	FString CPPType = PostProcessCPPType(InCPPType, InCPPTypeObject);
	
	const FString Name = GetValidNodeName(InNodeName.IsEmpty() ? FString(TEXT("ArrayNode")) : InNodeName);
	URigVMArrayNode* Node = NewObject<URigVMArrayNode>(Graph, *Name);
	Node->Position = InPosition;
	Node->OpCode = InOpCode;

	struct Local
	{
		static URigVMPin* AddPin(URigVMController* InController, URigVMNode* InNode, const FName& InName, ERigVMPinDirection InDirection, bool bIsArray, const FString& InCPPType, UObject* InCPPTypeObject)
		{
			URigVMPin* Pin = NewObject<URigVMPin>(InNode, InName);
			Pin->CPPType = InCPPType;
			Pin->CPPTypeObject = InCPPTypeObject;
			if(Pin->CPPTypeObject)
			{
				Pin->CPPTypeObjectPath = *Pin->CPPTypeObject->GetPathName();
			}
			if(bIsArray && !Pin->CPPType.StartsWith(TArrayPrefix))
			{
				Pin->CPPType = FString::Printf(TArrayTemplate, *Pin->CPPType);
			}
			Pin->Direction = InDirection;
			Pin->bIsDynamicArray = bIsArray;
			AddNodePin(InNode, Pin);

			if(Pin->Direction != ERigVMPinDirection::Hidden && !bIsArray && !Pin->IsExecuteContext())
			{
				if(UScriptStruct* Struct = Cast<UScriptStruct>(Pin->CPPTypeObject))
				{
					FString DefaultValue;
					InController->CreateDefaultValueForStructIfRequired(Pin->GetScriptStruct(), DefaultValue);
					InController->AddPinsForStruct(Struct, InNode, Pin, InDirection, DefaultValue, true, false);
				}
			}

			return Pin;
		}
		
		static URigVMPin* AddExecutePin(URigVMController* InController, URigVMNode* InNode, ERigVMPinDirection InDirection = ERigVMPinDirection::IO, const FName& InName = NAME_None)
		{
			const FName PinName = InName.IsNone() ? FRigVMStruct::ExecuteContextName : InName;
			URigVMPin* Pin = AddPin(InController, InNode, PinName, InDirection, false, FString::Printf(TEXT("F%s"), *InController->ExecuteContextStruct->GetName()), InController->ExecuteContextStruct);
			if(PinName == FRigVMStruct::ExecuteContextName)
			{
				Pin->DisplayName = FRigVMStruct::ExecuteName;
			}
			return Pin;
		}

		static URigVMPin* AddArrayPin(URigVMController* InController, URigVMNode* InNode, ERigVMPinDirection InDirection, const FString& InCPPType, UObject* InCPPTypeObject, const FName& InName = NAME_None)
		{
			const FName PinName = InName.IsNone() ? *URigVMArrayNode::ArrayName : InName;
			return AddPin(InController, InNode, PinName, InDirection, true, InCPPType, InCPPTypeObject);  
		}

		static URigVMPin* AddElementPin(URigVMController* InController, URigVMNode* InNode, ERigVMPinDirection InDirection, const FString& InCPPType, UObject* InCPPTypeObject)
		{
			return AddPin(InController, InNode, *URigVMArrayNode::ElementName, InDirection, false, InCPPType, InCPPTypeObject);  
		}

		static URigVMPin* AddIndexPin(URigVMController* InController, URigVMNode* InNode, ERigVMPinDirection InDirection)
		{
			return AddPin(InController, InNode, *URigVMArrayNode::IndexName, InDirection, false, TEXT("int32"), nullptr);  
		}

		static URigVMPin* AddNumPin(URigVMController* InController, URigVMNode* InNode, ERigVMPinDirection InDirection)
		{
			return AddPin(InController, InNode, *URigVMArrayNode::NumName, InDirection, false, TEXT("int32"), nullptr);  
		}

		static URigVMPin* AddCountPin(URigVMController* InController, URigVMNode* InNode, ERigVMPinDirection InDirection)
		{
			return AddPin(InController, InNode, *URigVMArrayNode::CountName, InDirection, false, TEXT("int32"), nullptr);  
		}

		static URigVMPin* AddRatioPin(URigVMController* InController, URigVMNode* InNode)
		{
			return AddPin(InController, InNode, *URigVMArrayNode::RatioName, ERigVMPinDirection::Output, false, TEXT("float"), nullptr);  
		}

		static URigVMPin* AddContinuePin(URigVMController* InController, URigVMNode* InNode)
		{
			return AddPin(InController, InNode, *URigVMArrayNode::ContinueName, ERigVMPinDirection::Hidden, false, TEXT("bool"), nullptr);
		}

		static URigVMPin* AddSuccessPin(URigVMController* InController, URigVMNode* InNode)
		{
			return AddPin(InController, InNode, *URigVMArrayNode::SuccessName, ERigVMPinDirection::Output, false, TEXT("bool"), nullptr);  
		}
	};

	switch(InOpCode)
	{
		case ERigVMOpCode::ArrayReset:
		case ERigVMOpCode::ArrayReverse:
		{
			Local::AddExecutePin(this, Node);
			Local::AddArrayPin(this, Node, ERigVMPinDirection::IO, CPPType, InCPPTypeObject);
			break;
		}
		case ERigVMOpCode::ArrayGetNum:
		{
			Local::AddArrayPin(this, Node, ERigVMPinDirection::Input, CPPType, InCPPTypeObject);
			Local::AddNumPin(this, Node, ERigVMPinDirection::Output);
			break;
		} 
		case ERigVMOpCode::ArraySetNum:
		{
			Local::AddExecutePin(this, Node);
			Local::AddArrayPin(this, Node, ERigVMPinDirection::IO, CPPType, InCPPTypeObject);
			Local::AddNumPin(this, Node, ERigVMPinDirection::Input);
			break;
		}
		case ERigVMOpCode::ArrayGetAtIndex:
		{
			Local::AddArrayPin(this, Node, ERigVMPinDirection::Input, CPPType, InCPPTypeObject);
			Local::AddIndexPin(this, Node, ERigVMPinDirection::Input);
			Local::AddElementPin(this, Node, ERigVMPinDirection::Output, CPPType, InCPPTypeObject);
			break;
		}
		case ERigVMOpCode::ArraySetAtIndex:
		case ERigVMOpCode::ArrayInsert:
		{
			Local::AddExecutePin(this, Node);
			Local::AddArrayPin(this, Node, ERigVMPinDirection::IO, CPPType, InCPPTypeObject);
			Local::AddIndexPin(this, Node, ERigVMPinDirection::Input);
			Local::AddElementPin(this, Node, ERigVMPinDirection::Input, CPPType, InCPPTypeObject);
			break;
		}
		case ERigVMOpCode::ArrayAdd:
		{
			Local::AddExecutePin(this, Node);
			Local::AddArrayPin(this, Node, ERigVMPinDirection::IO, CPPType, InCPPTypeObject);
			Local::AddElementPin(this, Node, ERigVMPinDirection::Input, CPPType, InCPPTypeObject);
			Local::AddIndexPin(this, Node, ERigVMPinDirection::Output);
			break;
		}
		case ERigVMOpCode::ArrayFind:
		{
			Local::AddArrayPin(this, Node, ERigVMPinDirection::Input, CPPType, InCPPTypeObject);
			Local::AddElementPin(this, Node, ERigVMPinDirection::Input, CPPType, InCPPTypeObject);
			Local::AddIndexPin(this, Node, ERigVMPinDirection::Output);
			Local::AddSuccessPin(this, Node);
			break;
		}
		case ERigVMOpCode::ArrayRemove:
		{
			Local::AddExecutePin(this, Node);
			Local::AddArrayPin(this, Node, ERigVMPinDirection::IO, CPPType, InCPPTypeObject);
			Local::AddIndexPin(this, Node, ERigVMPinDirection::Input);
			break;
		}
		case ERigVMOpCode::ArrayAppend:
		case ERigVMOpCode::ArrayUnion:
		{
			Local::AddExecutePin(this, Node);
			Local::AddArrayPin(this, Node, ERigVMPinDirection::IO, CPPType, InCPPTypeObject);
			Local::AddArrayPin(this, Node, ERigVMPinDirection::Input, CPPType, InCPPTypeObject, *URigVMArrayNode::OtherName);
			break;
		}
		case ERigVMOpCode::ArrayClone:
		{
			Local::AddArrayPin(this, Node, ERigVMPinDirection::Input, CPPType, InCPPTypeObject);
			Local::AddArrayPin(this, Node, ERigVMPinDirection::Output, CPPType, InCPPTypeObject, *URigVMArrayNode::CloneName);
			break;
		}
		case ERigVMOpCode::ArrayIterator:
		{
			Local::AddExecutePin(this, Node);
			Local::AddArrayPin(this, Node, ERigVMPinDirection::Input, CPPType, InCPPTypeObject);
			Local::AddElementPin(this, Node, ERigVMPinDirection::Output, CPPType, InCPPTypeObject);
			Local::AddIndexPin(this, Node, ERigVMPinDirection::Output);
			Local::AddCountPin(this, Node, ERigVMPinDirection::Output);
			Local::AddRatioPin(this, Node);
			Local::AddContinuePin(this, Node);
			Local::AddExecutePin(this, Node, ERigVMPinDirection::Output, *URigVMArrayNode::CompletedName);
			break;
		}
		case ERigVMOpCode::ArrayDifference:
		case ERigVMOpCode::ArrayIntersection:
		{
			Local::AddArrayPin(this, Node, ERigVMPinDirection::Input, CPPType, InCPPTypeObject);
			Local::AddArrayPin(this, Node, ERigVMPinDirection::Input, CPPType, InCPPTypeObject, *URigVMArrayNode::OtherName);
			Local::AddArrayPin(this, Node, ERigVMPinDirection::Output, CPPType, InCPPTypeObject, *URigVMArrayNode::ResultName);
			break;
		}
		default:
		{
			checkNoEntry();
		}
	}

	Graph->Nodes.Add(Node);

	if (!bSuspendNotifications)
	{
		Graph->MarkPackageDirty();
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMAddArrayNodeAction Action;
	if (bSetupUndoRedo)
	{
		Action = FRigVMAddArrayNodeAction(Node);
		Action.Title = FString::Printf(TEXT("Add %s Array Node"), *Node->GetNodeTitle());
		ActionStack->BeginAction(Action);
	}

	Notify(ERigVMGraphNotifType::NodeAdded, Node);

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	if (bPrintPythonCommand)
	{
		TArray<FString> Commands = GetAddNodePythonCommands(Node);
		for (const FString& Command : Commands)
		{
			RigVMPythonUtils::Print(GetGraphOuterName(), 
				FString::Printf(TEXT("%s"), *Command));
		}
	}

	return Node;
}

URigVMArrayNode* URigVMController::AddArrayNodeFromObjectPath(ERigVMOpCode InOpCode, const FString& InCPPType,
	const FString& InCPPTypeObjectPath, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo,
	bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	UObject* CPPTypeObject = nullptr;
	if (!InCPPTypeObjectPath.IsEmpty())
	{
		CPPTypeObject = URigVMPin::FindObjectFromCPPTypeObjectPath<UObject>(InCPPTypeObjectPath);
		if (CPPTypeObject == nullptr)
		{
			ReportErrorf(TEXT("Cannot find cpp type object for path '%s'."), *InCPPTypeObjectPath);
			return nullptr;
		}
	}

	return AddArrayNode(InOpCode, InCPPType, CPPTypeObject, InPosition, InNodeName, bSetupUndoRedo, bPrintPythonCommand);
}

void URigVMController::ForEveryPinRecursively(URigVMPin* InPin, TFunction<void(URigVMPin*)> OnEachPinFunction)
{
	OnEachPinFunction(InPin);
	for (URigVMPin* SubPin : InPin->SubPins)
	{
		ForEveryPinRecursively(SubPin, OnEachPinFunction);
	}
}

void URigVMController::ForEveryPinRecursively(URigVMNode* InNode, TFunction<void(URigVMPin*)> OnEachPinFunction)
{
	for (URigVMPin* Pin : InNode->GetPins())
	{
		ForEveryPinRecursively(Pin, OnEachPinFunction);
	}
}

void URigVMController::SetExecuteContextStruct(UStruct* InExecuteContextStruct)
{
	check(InExecuteContextStruct);
	ensure(InExecuteContextStruct->IsChildOf(FRigVMExecuteContext::StaticStruct()));
	ExecuteContextStruct = InExecuteContextStruct;
}

FString URigVMController::GetValidNodeName(const FString& InPrefix)
{
	URigVMGraph* Graph = GetGraph();
	check(Graph);

	return GetUniqueName(*InPrefix, [&](const FName& InName) {
		return Graph->IsNameAvailable(InName.ToString());
	}, false, true).ToString();
}

bool URigVMController::IsValidGraph() const
{
	URigVMGraph* Graph = GetGraph();
	if (Graph == nullptr)
	{
		ReportError(TEXT("Controller does not have a graph associated - use SetGraph / set_graph."));
		return false;
	}
	return true;
}

bool URigVMController::IsValidNodeForGraph(URigVMNode* InNode)
{
	if(!IsValidGraph())
	{
		return false;
	}

	if (InNode == nullptr)
	{
		ReportError(TEXT("InNode is nullptr."));
		return false;
	}

	if (InNode->GetGraph() != GetGraph())
	{
		ReportWarningf(TEXT("InNode '%s' is on a different graph. InNode graph is %s, this graph is %s"), *InNode->GetNodePath(), *GetNameSafe(InNode->GetGraph()), *GetNameSafe(GetGraph()));
		return false;
	}

	if (InNode->GetNodeIndex() == INDEX_NONE)
	{
		ReportErrorf(TEXT("InNode '%s' is transient (not yet nested to a graph)."), *InNode->GetName());
	}

	return true;
}

bool URigVMController::IsValidPinForGraph(URigVMPin* InPin)
{
	if(!IsValidGraph())
	{
		return false;
	}

	if (InPin == nullptr)
	{
		ReportError(TEXT("InPin is nullptr."));
		return false;
	}

	if (!IsValidNodeForGraph(InPin->GetNode()))
	{
		return false;
	}

	if (InPin->GetPinIndex() == INDEX_NONE)
	{
		ReportErrorf(TEXT("InPin '%s' is transient (not yet nested properly)."), *InPin->GetName());
	}

	return true;
}

bool URigVMController::IsValidLinkForGraph(URigVMLink* InLink)
{
	if(!IsValidGraph())
	{
		return false;
	}

	if (InLink == nullptr)
	{
		ReportError(TEXT("InLink is nullptr."));
		return false;
	}

	if (InLink->GetGraph() != GetGraph())
	{
		ReportError(TEXT("InLink is on a different graph."));
		return false;
	}

	if(InLink->GetSourcePin() == nullptr)
	{
		ReportError(TEXT("InLink has no source pin."));
		return false;
	}

	if(InLink->GetTargetPin() == nullptr)
	{
		ReportError(TEXT("InLink has no target pin."));
		return false;
	}

	if (InLink->GetLinkIndex() == INDEX_NONE)
	{
		ReportError(TEXT("InLink is transient (not yet nested properly)."));
	}

	if(!IsValidPinForGraph(InLink->GetSourcePin()))
	{
		return false;
	}

	if(!IsValidPinForGraph(InLink->GetTargetPin()))
	{
		return false;
	}

	return true;
}

bool URigVMController::CanAddNode(URigVMNode* InNode, bool bReportErrors, bool bIgnoreFunctionEntryReturnNodes)
{
	if(!IsValidGraph())
	{
		return false;
	}

	check(InNode);

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (URigVMFunctionReferenceNode* FunctionRefNode = Cast<URigVMFunctionReferenceNode>(InNode))
	{
		if (URigVMFunctionLibrary* FunctionLibrary = FunctionRefNode->GetLibrary())
		{
			if (URigVMLibraryNode* FunctionDefinition = FunctionRefNode->GetReferencedNode())
			{
				if(!CanAddFunctionRefForDefinition(FunctionDefinition, false))
				{
					URigVMFunctionLibrary* TargetLibrary = Graph->GetDefaultFunctionLibrary();
					URigVMLibraryNode* NewFunctionDefinition = TargetLibrary->FindPreviouslyLocalizedFunction(FunctionDefinition);
					
					if((NewFunctionDefinition == nullptr) && RequestLocalizeFunctionDelegate.IsBound())
					{
						if(RequestLocalizeFunctionDelegate.Execute(FunctionDefinition))
						{
							NewFunctionDefinition = TargetLibrary->FindPreviouslyLocalizedFunction(FunctionDefinition);
						}
					}

					if(NewFunctionDefinition == nullptr)
					{
						return false;
					}
					
					SetReferencedFunction(FunctionRefNode, NewFunctionDefinition, false);
					FunctionDefinition = NewFunctionDefinition;
				}
				
				if(!CanAddFunctionRefForDefinition(FunctionDefinition, bReportErrors))
				{
					DestroyObject(InNode);
					return false;
				}
			}
		}			
	}
	else if(!bIgnoreFunctionEntryReturnNodes &&
		(InNode->IsA<URigVMFunctionEntryNode>() ||
		InNode->IsA<URigVMFunctionReturnNode>()))
	{
		// only allow entry / return nodes on sub graphs
		if(Graph->IsRootGraph())
		{
			return false;
		}

		// only allow one function entry node
		if(InNode->IsA<URigVMFunctionEntryNode>())
		{
			if(Graph->GetEntryNode() != nullptr)
			{
				return false;
			}
		}
		// only allow one function return node
		else if(InNode->IsA<URigVMFunctionReturnNode>())
		{
			if(Graph->GetReturnNode() != nullptr)
			{
				return false;
			}
		}
	}
	else if(URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(InNode))
	{
		FRigVMControllerGraphGuard GraphGuard(this, CollapseNode->GetContainedGraph(), false);

		TArray<URigVMNode*> ContainedNodes = CollapseNode->GetContainedNodes();
		for(URigVMNode* ContainedNode : ContainedNodes)
		{
			if(!CanAddNode(ContainedNode, bReportErrors, true))
			{
				return false;
			}
		}
	}
	else if(URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(InNode))
	{
		if (URigVMPin* NamePin = VariableNode->FindPin(URigVMVariableNode::VariableName))
		{
			FString VarName = NamePin->GetDefaultValue();
			if (!VarName.IsEmpty())
			{
				TArray<FRigVMExternalVariable> AllVariables = URigVMController::GetAllVariables(true);
				for(const FRigVMExternalVariable& Variable : AllVariables)
				{
					if(Variable.Name.ToString() == VarName)
					{
						return true;
					}
				}
				return false;
			}
		}
	}
	else if (InNode->IsEvent())
	{
		if (URigVMUnitNode* InUnitNode = Cast<URigVMUnitNode>(InNode))
		{
			if (!CanAddEventNode(InUnitNode->GetScriptStruct(), bReportErrors))
			{
				return false;
			}
		}
	}
	
	return true;
}

TObjectPtr<URigVMNode> URigVMController::FindEventNode(const UScriptStruct* InScriptStruct) const
{
	check(InScriptStruct);

	// construct equivalent default struct
	FStructOnScope InDefaultStructScope(InScriptStruct);
	InScriptStruct->InitializeDefaultValue((uint8*)InDefaultStructScope.GetStructMemory());
	
	if (URigVMGraph* Graph = GetGraph())
	{
		TObjectPtr<URigVMNode>* FoundNode = 
		Graph->Nodes.FindByPredicate( [&InDefaultStructScope](const TObjectPtr<URigVMNode>& Node) {
			if (Node->IsEvent())
			{
				if (URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(Node))
				{
					// compare default structures
					TSharedPtr<FStructOnScope> DefaultStructScope = UnitNode->ConstructStructInstance(true);
					if (DefaultStructScope.IsValid() && InDefaultStructScope.GetStruct() == DefaultStructScope->GetStruct())
					{
						return true;
					}
				}
			}
			return false;
		});

		if (FoundNode)
		{
			return *FoundNode;
		}
	}
	
	return TObjectPtr<URigVMNode>();
}

bool URigVMController::CanAddEventNode(UScriptStruct* InScriptStruct, const bool bReportErrors) const
{
	if(!IsValidGraph())
	{
		return false;
	}
	
	check(InScriptStruct);
	
	URigVMGraph* Graph = GetGraph();
	check(Graph);

	// check if we're trying to add a node within a graph which is not the top level one
	if (!Graph->IsTopLevelGraph())
	{
		if (bReportErrors)
		{
			ReportAndNotifyError(TEXT("Event nodes can only be added to top level graphs."));
		}
		return false;
	}

	TObjectPtr<URigVMNode> EventNode = FindEventNode(InScriptStruct);
	const bool bHasEventNode = EventNode != nullptr;
	if (bHasEventNode && bReportErrors)
	{
		const FString ErrorMessage = FString::Printf(TEXT("Rig Graph can only contain one single %s node."),
													 *InScriptStruct->GetDisplayNameText().ToString());
		ReportAndNotifyError(ErrorMessage);
	}
		
	return !bHasEventNode;
}

bool URigVMController::CanAddFunctionRefForDefinition(URigVMLibraryNode* InFunctionDefinition, bool bReportErrors)
{
	if(!IsValidGraph())
	{
		return false;
	}

	check(InFunctionDefinition);

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if(IsFunctionAvailableDelegate.IsBound())
	{
		if(!IsFunctionAvailableDelegate.Execute(InFunctionDefinition))
		{
			if(bReportErrors)
			{
				ReportAndNotifyError(TEXT("Function is not available for placement in another graph host."));
			}
			return false;
		}
	}

	if(IsDependencyCyclicDelegate.IsBound())
	{
		if(IsDependencyCyclicDelegate.Execute(Graph, InFunctionDefinition))
		{
			if(bReportErrors)
			{
				ReportAndNotifyError(TEXT("Function is not available for placement in this graph host due to dependency cycles."));
			}
			return false;
		}
	}

	URigVMLibraryNode* ParentLibraryNode = Cast<URigVMLibraryNode>(Graph->GetOuter());
	while (ParentLibraryNode)
	{
		if (ParentLibraryNode == InFunctionDefinition)
		{
			if(bReportErrors)
			{
				ReportAndNotifyError(TEXT("You cannot place functions inside of itself or an indirect recursion."));
			}
			return false;
		}
		ParentLibraryNode = Cast<URigVMLibraryNode>(ParentLibraryNode->GetGraph()->GetOuter());
	}

	return true;
}

void URigVMController::AddPinsForStruct(UStruct* InStruct, URigVMNode* InNode, URigVMPin* InParentPin, ERigVMPinDirection InPinDirection, const FString& InDefaultValue, bool bAutoExpandArrays, bool bNotify)
{
	if(!ShouldStructBeUnfolded(InStruct))
	{
		return;
	}
	
	TArray<FString> MemberNameValuePairs = URigVMPin::SplitDefaultValue(InDefaultValue);
	TMap<FName, FString> MemberValues;
	for (const FString& MemberNameValuePair : MemberNameValuePairs)
	{
		FString MemberName, MemberValue;
		if (MemberNameValuePair.Split(TEXT("="), &MemberName, &MemberValue))
		{
			MemberValues.Add(*MemberName, MemberValue);
		}
	}

	for (TFieldIterator<FProperty> It(InStruct); It; ++It)
	{
		FName PropertyName = It->GetFName();

		URigVMPin* Pin = NewObject<URigVMPin>(InParentPin == nullptr ? Cast<UObject>(InNode) : Cast<UObject>(InParentPin), PropertyName);
		ConfigurePinFromProperty(*It, Pin, InPinDirection);

		if (InParentPin)
		{
			AddSubPin(InParentPin, Pin);
		}
		else
		{
			AddNodePin(InNode, Pin);
		}

		FString* DefaultValuePtr = MemberValues.Find(Pin->GetFName());

		FStructProperty* StructProperty = CastField<FStructProperty>(*It);
		if (StructProperty)
		{
			if (ShouldStructBeUnfolded(StructProperty->Struct))
			{
				FString DefaultValue;
				if (DefaultValuePtr != nullptr)
				{
					DefaultValue = *DefaultValuePtr;
				}
				CreateDefaultValueForStructIfRequired(StructProperty->Struct, DefaultValue);

				AddPinsForStruct(StructProperty->Struct, InNode, Pin, Pin->GetDirection(), DefaultValue, bAutoExpandArrays);
			}
			else if(DefaultValuePtr != nullptr)
			{
				Pin->DefaultValue = *DefaultValuePtr;
			}
		}

		FArrayProperty* ArrayProperty = CastField<FArrayProperty>(*It);
		if (ArrayProperty)
		{
			ensure(Pin->IsArray());

			if (DefaultValuePtr)
			{
				if (ShouldPinBeUnfolded(Pin))
				{
					TArray<FString> ElementDefaultValues = URigVMPin::SplitDefaultValue(*DefaultValuePtr);
					AddPinsForArray(ArrayProperty, InNode, Pin, Pin->Direction, ElementDefaultValues, bAutoExpandArrays);
				}
				else
				{
					FString DefaultValue = *DefaultValuePtr;
					PostProcessDefaultValue(Pin, DefaultValue);
					Pin->DefaultValue = *DefaultValuePtr;
				}
			}
		}
		
		if (!Pin->IsArray() && !Pin->IsStruct() && DefaultValuePtr != nullptr)
		{
			FString DefaultValue = *DefaultValuePtr;
			PostProcessDefaultValue(Pin, DefaultValue);
			Pin->DefaultValue = DefaultValue;
		}

		if (bNotify)
		{
			Notify(ERigVMGraphNotifType::PinAdded, Pin);
		}
	}
}

void URigVMController::AddPinsForArray(FArrayProperty* InArrayProperty, URigVMNode* InNode, URigVMPin* InParentPin, ERigVMPinDirection InPinDirection, const TArray<FString>& InDefaultValues, bool bAutoExpandArrays)
{
	check(InParentPin);
	if (!ShouldPinBeUnfolded(InParentPin))
	{
		return;
	}

	for (int32 ElementIndex = 0; ElementIndex < InDefaultValues.Num(); ElementIndex++)
	{
		FString ElementName = FString::FormatAsNumber(InParentPin->SubPins.Num());
		URigVMPin* Pin = NewObject<URigVMPin>(InParentPin, *ElementName);

		ConfigurePinFromProperty(InArrayProperty->Inner, Pin, InPinDirection);
		FString DefaultValue = InDefaultValues[ElementIndex];

		AddSubPin(InParentPin, Pin);

		if (bAutoExpandArrays)
		{
			TGuardValue<bool> ErrorGuard(bReportWarningsAndErrors, false);
			ExpandPinRecursively(Pin, false);
		}

		FStructProperty* StructProperty = CastField<FStructProperty>(InArrayProperty->Inner);
		if (StructProperty)
		{
			if (ShouldPinBeUnfolded(Pin))
			{
				// DefaultValue before this point only contains parent struct overrides,
				// see comments in CreateDefaultValueForStructIfRequired
				UScriptStruct* ScriptStruct = Pin->GetScriptStruct();
				if (ScriptStruct)
				{
					CreateDefaultValueForStructIfRequired(ScriptStruct, DefaultValue);
				}
				AddPinsForStruct(StructProperty->Struct, InNode, Pin, Pin->Direction, DefaultValue, bAutoExpandArrays);
			}
			else if (!DefaultValue.IsEmpty())
			{
				PostProcessDefaultValue(Pin, DefaultValue);
				Pin->DefaultValue = DefaultValue;
			}
		}

		FArrayProperty* ArrayProperty = CastField<FArrayProperty>(InArrayProperty->Inner);
		if (ArrayProperty)
		{
			if (ShouldPinBeUnfolded(Pin))
			{
				TArray<FString> ElementDefaultValues = URigVMPin::SplitDefaultValue(DefaultValue);
				AddPinsForArray(ArrayProperty, InNode, Pin, Pin->Direction, ElementDefaultValues, bAutoExpandArrays);
			}
			else if (!DefaultValue.IsEmpty())
			{
				PostProcessDefaultValue(Pin, DefaultValue);
				Pin->DefaultValue = DefaultValue;
			}
		}

		if (!Pin->IsArray() && !Pin->IsStruct())
		{
			PostProcessDefaultValue(Pin, DefaultValue);
			Pin->DefaultValue = DefaultValue;
		}
	}
}

void URigVMController::ConfigurePinFromProperty(FProperty* InProperty, URigVMPin* InOutPin, ERigVMPinDirection InPinDirection)
{
	if (InPinDirection == ERigVMPinDirection::Invalid)
	{
		InOutPin->Direction = FRigVMStruct::GetPinDirectionFromProperty(InProperty);
	}
	else
	{
		InOutPin->Direction = InPinDirection;
	}

#if WITH_EDITOR

	if (!InOutPin->IsArrayElement())
	{
		FString DisplayNameText = InProperty->GetDisplayNameText().ToString();
		if (!DisplayNameText.IsEmpty())
		{
			InOutPin->DisplayName = *DisplayNameText;
		}
		else
		{
			InOutPin->DisplayName = NAME_None;
		}
	}
	InOutPin->bIsConstant = InProperty->HasMetaData(TEXT("Constant"));
	FString CustomWidgetName = InProperty->GetMetaData(TEXT("CustomWidget"));
	InOutPin->CustomWidgetName = CustomWidgetName.IsEmpty() ? FName(NAME_None) : FName(*CustomWidgetName);

	if (InProperty->HasMetaData(FRigVMStruct::ExpandPinByDefaultMetaName))
	{
		InOutPin->bIsExpanded = true;
	}

#endif

	FString ExtendedCppType;
	InOutPin->CPPType = InProperty->GetCPPType(&ExtendedCppType);
	InOutPin->CPPType += ExtendedCppType;

	InOutPin->bIsDynamicArray = false;
#if WITH_EDITOR
	if (InOutPin->Direction == ERigVMPinDirection::Hidden)
	{
		if (!InProperty->HasMetaData(TEXT("ArraySize")))
		{
			InOutPin->bIsDynamicArray = true;
		}
	}

	if (InOutPin->bIsDynamicArray)
	{
		if (InProperty->HasMetaData(FRigVMStruct::SingletonMetaName))
		{
			InOutPin->bIsDynamicArray = false;
		}
	}
#endif

	FProperty* PropertyForType = InProperty;
	FArrayProperty* ArrayProperty = CastField<FArrayProperty>(PropertyForType);
	if (ArrayProperty)
	{
		PropertyForType = ArrayProperty->Inner;
	}

	if (FStructProperty* StructProperty = CastField<FStructProperty>(PropertyForType))
	{
		InOutPin->CPPTypeObject = StructProperty->Struct;
	}
	else if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(PropertyForType))
	{
		InOutPin->CPPTypeObject = ObjectProperty->PropertyClass;
	}
	else if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(PropertyForType))
	{
		InOutPin->CPPTypeObject = EnumProperty->GetEnum();
	}
	else if (FByteProperty* ByteProperty = CastField<FByteProperty>(PropertyForType))
	{
		InOutPin->CPPTypeObject = ByteProperty->Enum;
	}

	if (InOutPin->CPPTypeObject)
	{
		InOutPin->CPPTypeObjectPath = *InOutPin->CPPTypeObject->GetPathName();
	}
}

void URigVMController::ConfigurePinFromPin(URigVMPin* InOutPin, URigVMPin* InPin)
{
	InOutPin->bIsConstant = InPin->bIsConstant;
	InOutPin->Direction = InPin->Direction;
	InOutPin->CPPType = InPin->CPPType;
	InOutPin->CPPTypeObjectPath = InPin->CPPTypeObjectPath;
	InOutPin->CPPTypeObject = InPin->CPPTypeObject;
	InOutPin->DefaultValue = InPin->DefaultValue;
}

bool URigVMController::ShouldStructBeUnfolded(const UStruct* Struct)
{
	if (Struct == nullptr)
	{
		return false;
	}
	if (Struct->IsChildOf(UClass::StaticClass()))
	{
		return false;
	}
	if(Struct->IsChildOf(FRigVMExecuteContext::StaticStruct()))
	{
		return false;
	}
	if(Struct->IsChildOf(FRigVMUnknownType::StaticStruct()))
	{
		return false;
	}
	if (UnfoldStructDelegate.IsBound())
	{
		if (!UnfoldStructDelegate.Execute(Struct))
		{
			return false;
		}
	}
	return true;
}

bool URigVMController::ShouldPinBeUnfolded(URigVMPin* InPin)
{
	if (InPin->IsStruct())
	{
		return ShouldStructBeUnfolded(InPin->GetScriptStruct());
	}
	else if (InPin->IsArray())
	{
		return InPin->GetDirection() == ERigVMPinDirection::Input ||
			InPin->GetDirection() == ERigVMPinDirection::IO;
	}
	return false;
}

FProperty* URigVMController::FindPropertyForPin(const FString& InPinPath)
{
	if(!IsValidGraph())
	{
		return nullptr;
	}

	TArray<FString> Parts;
	if (!URigVMPin::SplitPinPath(InPinPath, Parts))
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMPin* Pin = Graph->FindPin(InPinPath);
	if (Pin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InPinPath);
		return nullptr;
	}

	URigVMNode* Node = Pin->GetNode();

	URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(Node);
	if (UnitNode)
	{
		int32 PartIndex = 1; // cut off the first one since it's the node

		UStruct* Struct = UnitNode->ScriptStruct;
		FProperty* Property = Struct->FindPropertyByName(*Parts[PartIndex++]);

		while (PartIndex < Parts.Num() && Property != nullptr)
		{
			if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
			{
				Property = ArrayProperty->Inner;
				PartIndex++;
				continue;
			}

			if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				Struct = StructProperty->Struct;
				Property = Struct->FindPropertyByName(*Parts[PartIndex++]);
				continue;
			}

			break;
		}

		if (PartIndex == Parts.Num())
		{
			return Property;
		}
	}

	return nullptr;
}

int32 URigVMController::DetachLinksFromPinObjects(const TArray<URigVMLink*>* InLinks, bool bNotify)
{
	URigVMGraph* Graph = GetGraph();
	check(Graph);
	TGuardValue<bool> EventuallySuspendNotifs(bSuspendNotifications, !bNotify);

	TArray<URigVMLink*> Links;
	if (InLinks)
	{
		Links = *InLinks;
	}
	else
	{
		Links = Graph->Links;
	}

	for (URigVMLink* Link : Links)
	{
		Notify(ERigVMGraphNotifType::LinkRemoved, Link);

		URigVMPin* SourcePin = Link->GetSourcePin();
		URigVMPin* TargetPin = Link->GetTargetPin();

		if (SourcePin)
		{
			Link->SourcePinPath = SourcePin->GetPinPath();
			SourcePin->Links.Remove(Link);
		}

		if (TargetPin)
		{
			Link->TargetPinPath = TargetPin->GetPinPath();
			TargetPin->Links.Remove(Link);
		}

		Link->SourcePin = nullptr;
		Link->TargetPin = nullptr;
	}

	if (InLinks == nullptr)
	{
		for (URigVMNode* Node : Graph->Nodes)
		{
			if (URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(Node))
			{
				FRigVMControllerGraphGuard GraphGuard(this, CollapseNode->GetContainedGraph(), false);
				DetachLinksFromPinObjects(InLinks, bNotify);
			}
		}
	}

	return Links.Num();
}

int32 URigVMController::ReattachLinksToPinObjects(bool bFollowCoreRedirectors, const TArray<URigVMLink*>* InLinks, bool bNotify, bool bSetupOrphanedPins)
{
	URigVMGraph* Graph = GetGraph();
	check(Graph);
	TGuardValue<bool> EventuallySuspendNotifs(bSuspendNotifications, !bNotify);
	FScopeLock Lock(&PinPathCoreRedirectorsLock);

	bool bReplacingAllLinks = false;
	TArray<URigVMLink*> Links;
	if (InLinks)
	{
		Links = *InLinks;
	}
	else
	{
		Links = Graph->Links;
		bReplacingAllLinks = true;
	}

	TMap<FString, FString> RedirectedPinPaths;
	if (bFollowCoreRedirectors)
	{
		for (URigVMLink* Link : Links)
		{
			FString RedirectedSourcePinPath;
			if (ShouldRedirectPin(Link->SourcePinPath, RedirectedSourcePinPath))
			{
				OutputPinRedirectors.FindOrAdd(Link->SourcePinPath, RedirectedSourcePinPath);
			}

			FString RedirectedTargetPinPath;
			if (ShouldRedirectPin(Link->TargetPinPath, RedirectedTargetPinPath))
			{
				InputPinRedirectors.FindOrAdd(Link->TargetPinPath, RedirectedTargetPinPath);
			}
		}
	}

	// fix up the pin links based on the persisted data
	TArray<URigVMLink*> NewLinks;
	for (URigVMLink* Link : Links)
	{
		if (FString* RedirectedSourcePinPath = OutputPinRedirectors.Find(Link->SourcePinPath))
		{
			ensure(Link->SourcePin == nullptr);
			Link->SourcePinPath = *RedirectedSourcePinPath;
		}

		if (FString* RedirectedTargetPinPath = InputPinRedirectors.Find(Link->TargetPinPath))
		{
			ensure(Link->TargetPin == nullptr);
			Link->TargetPinPath = *RedirectedTargetPinPath;
		}

		URigVMPin* SourcePin = Link->GetSourcePin();
		URigVMPin* TargetPin = Link->GetTargetPin();

		if(bSetupOrphanedPins && (SourcePin != nullptr) && (TargetPin != nullptr))
		{
			// ignore duplicated links that have been processed
			if (SourcePin->IsLinkedTo(TargetPin))
			{
				continue;
			}

			if (!URigVMPin::CanLink(SourcePin, TargetPin, nullptr, nullptr))
			{
				if(SourcePin->GetNode()->HasOrphanedPins() && bSetupOrphanedPins)
				{
					SourcePin = nullptr;
				}
				else if(TargetPin->GetNode()->HasOrphanedPins() && bSetupOrphanedPins)
				{
					TargetPin = nullptr;
				}
				else
				{
					ReportWarningf(TEXT("Unable to re-create link %s -> %s"), *Link->SourcePinPath, *Link->TargetPinPath);
					TargetPin->Links.Remove(Link);
					SourcePin->Links.Remove(Link);
					continue;
				}
			}
		}

		if(bSetupOrphanedPins)
		{
			bool bRelayedBackToOrphanPin = false;
			for(int32 PinIndex=0; PinIndex<2; PinIndex++)
			{
				URigVMPin*& PinToFind = PinIndex == 0 ? SourcePin : TargetPin;
				
				if(PinToFind == nullptr)
				{
					const FString& PinPathToFind = PinIndex == 0 ? Link->SourcePinPath : Link->TargetPinPath;
					FString NodeName, RemainingPinPath;
					URigVMPin::SplitPinPathAtStart(PinPathToFind, NodeName, RemainingPinPath);
					check(!NodeName.IsEmpty() && !RemainingPinPath.IsEmpty());

					URigVMNode* Node = Graph->FindNode(NodeName);
					if(Node == nullptr)
					{
						continue;
					}

					RemainingPinPath = FString::Printf(TEXT("%s%s"), *URigVMPin::OrphanPinPrefix, *RemainingPinPath);
					PinToFind = Node->FindPin(RemainingPinPath);

					if(PinToFind != nullptr)
					{
						if(PinIndex == 0)
						{
							Link->SourcePinPath = PinToFind->GetPinPath();
							Link->SourcePin = nullptr;
							SourcePin = Link->GetSourcePin();
						}
						else
						{
							Link->TargetPinPath = PinToFind->GetPinPath();
							Link->TargetPin = nullptr;
							TargetPin = Link->GetTargetPin();
						}
						bRelayedBackToOrphanPin = true;
					}
				}
			}
		}
		
		if (SourcePin == nullptr)
		{
			ReportWarningf(TEXT("Unable to re-create link %s -> %s"), *Link->SourcePinPath, *Link->TargetPinPath);
			if (TargetPin != nullptr)
			{
				TargetPin->Links.Remove(Link);
			}
			continue;
		}
		if (TargetPin == nullptr)
		{
			ReportWarningf(TEXT("Unable to re-create link %s -> %s"), *Link->SourcePinPath, *Link->TargetPinPath);
			if (SourcePin != nullptr)
			{
				SourcePin->Links.Remove(Link);
			}
			continue;
		}

		SourcePin->Links.AddUnique(Link);
		TargetPin->Links.AddUnique(Link);
		NewLinks.Add(Link);
	}

	if (bReplacingAllLinks)
	{
		Graph->Links = NewLinks;

		for (URigVMLink* Link : Graph->Links)
		{
			Notify(ERigVMGraphNotifType::LinkAdded, Link);
		}
	}
	else
	{
		// if we are running of a subset of links
		// find the ones we weren't able to connect
		// again and remove them.
		for (URigVMLink* Link : Links)
		{
			if (!NewLinks.Contains(Link))
			{
				Graph->Links.Remove(Link);
				Notify(ERigVMGraphNotifType::LinkRemoved, Link);
			}
			else
			{
				Notify(ERigVMGraphNotifType::LinkAdded, Link);
			}
		}
	}

	if (InLinks == nullptr)
	{
		for (URigVMNode* Node : Graph->Nodes)
		{
			if (URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(Node))
			{
				FRigVMControllerGraphGuard GraphGuard(this, CollapseNode->GetContainedGraph(), false);
				ReattachLinksToPinObjects(bFollowCoreRedirectors, nullptr);
			}
		}
	}

	InputPinRedirectors.Reset();
	OutputPinRedirectors.Reset();

	return NewLinks.Num();
}

void URigVMController::RemoveStaleNodes()
{
	if (!IsValidGraph())
	{
		return;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	Graph->Nodes.Remove(nullptr);
}

void URigVMController::AddPinRedirector(bool bInput, bool bOutput, const FString& OldPinPath, const FString& NewPinPath)
{
	if (OldPinPath.IsEmpty() || NewPinPath.IsEmpty() || OldPinPath == NewPinPath)
	{
		return;
	}

	if (bInput)
	{
		InputPinRedirectors.FindOrAdd(OldPinPath) = NewPinPath;
	}
	if (bOutput)
	{
		OutputPinRedirectors.FindOrAdd(OldPinPath) = NewPinPath;
	}
}

#if WITH_EDITOR

bool URigVMController::ShouldRedirectPin(UScriptStruct* InOwningStruct, const FString& InOldRelativePinPath, FString& InOutNewRelativePinPath) const
{
	FControlRigStructPinRedirectorKey RedirectorKey(InOwningStruct, InOldRelativePinPath);
	if (const FString* RedirectedPinPath = PinPathCoreRedirectors.Find(RedirectorKey))
	{
		InOutNewRelativePinPath = *RedirectedPinPath;
		return InOutNewRelativePinPath != InOldRelativePinPath;
	}

	FString RelativePinPath = InOldRelativePinPath;
	FString PinName, SubPinPath;
	if (!URigVMPin::SplitPinPathAtStart(RelativePinPath, PinName, SubPinPath))
	{
		PinName = RelativePinPath;
		SubPinPath.Empty();
	}

	bool bShouldRedirect = false;
	FCoreRedirectObjectName OldObjectName(*PinName, InOwningStruct->GetFName(), *InOwningStruct->GetOutermost()->GetPathName());
	FCoreRedirectObjectName NewObjectName = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Property, OldObjectName);
	if (OldObjectName != NewObjectName)
	{
		PinName = NewObjectName.ObjectName.ToString();
		bShouldRedirect = true;
	}

	FProperty* Property = InOwningStruct->FindPropertyByName(*PinName);
	if (Property == nullptr)
	{
		return false;
	}

	if (!SubPinPath.IsEmpty())
	{
		if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			FString NewSubPinPath;
			if (ShouldRedirectPin(StructProperty->Struct, SubPinPath, NewSubPinPath))
			{
				SubPinPath = NewSubPinPath;
				bShouldRedirect = true;
			}
		}
		else if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			FString SubPinName, SubSubPinPath;
			if (URigVMPin::SplitPinPathAtStart(SubPinPath, SubPinName, SubSubPinPath))
			{
				if (FStructProperty* InnerStructProperty = CastField<FStructProperty>(ArrayProperty->Inner))
				{
					FString NewSubSubPinPath;
					if (ShouldRedirectPin(InnerStructProperty->Struct, SubSubPinPath, NewSubSubPinPath))
					{
						SubSubPinPath = NewSubSubPinPath;
						SubPinPath = URigVMPin::JoinPinPath(SubPinName, SubSubPinPath);
						bShouldRedirect = true;
					}
				}
			}
		}
	}

	if (bShouldRedirect)
	{
		if (SubPinPath.IsEmpty())
		{
			InOutNewRelativePinPath = PinName;
			PinPathCoreRedirectors.Add(RedirectorKey, InOutNewRelativePinPath);
		}
		else
		{
			InOutNewRelativePinPath = URigVMPin::JoinPinPath(PinName, SubPinPath);

			TArray<FString> OldParts, NewParts;
			if (URigVMPin::SplitPinPath(InOldRelativePinPath, OldParts) &&
				URigVMPin::SplitPinPath(InOutNewRelativePinPath, NewParts))
			{
				ensure(OldParts.Num() == NewParts.Num());

				FString OldPath = OldParts[0];
				FString NewPath = NewParts[0];
				for (int32 PartIndex = 0; PartIndex < OldParts.Num(); PartIndex++)
				{
					if (PartIndex > 0)
					{
						OldPath = URigVMPin::JoinPinPath(OldPath, OldParts[PartIndex]);
						NewPath = URigVMPin::JoinPinPath(NewPath, NewParts[PartIndex]);
					}

					// this is also going to cache paths which haven't been redirected.
					// consumers of the table have to still compare old != new
					FControlRigStructPinRedirectorKey SubRedirectorKey(InOwningStruct, OldPath);
					if (!PinPathCoreRedirectors.Contains(SubRedirectorKey))
					{
						PinPathCoreRedirectors.Add(SubRedirectorKey, NewPath);
					}
				}
			}
		}
	}

	return bShouldRedirect;
}

bool URigVMController::ShouldRedirectPin(const FString& InOldPinPath, FString& InOutNewPinPath) const
{
	URigVMGraph* Graph = GetGraph();
	check(Graph);

	FString PinPathInNode, NodeName;
	URigVMPin::SplitPinPathAtStart(InOldPinPath, NodeName, PinPathInNode);

	URigVMNode* Node = Graph->FindNode(NodeName);
	if (URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(Node))
	{
		FString NewPinPathInNode;
		if (ShouldRedirectPin(UnitNode->GetScriptStruct(), PinPathInNode, NewPinPathInNode))
		{
			InOutNewPinPath = URigVMPin::JoinPinPath(NodeName, NewPinPathInNode);
			return true;
		}
	}
	else if (URigVMRerouteNode* RerouteNode = Cast<URigVMRerouteNode>(Node))
	{
		URigVMPin* ValuePin = RerouteNode->Pins[0];
		if (ValuePin->IsStruct())
		{
			FString ValuePinPath = ValuePin->GetPinPath();
			if (InOldPinPath == ValuePinPath)
			{
				return false;
			}
			else if (!InOldPinPath.StartsWith(ValuePinPath))
			{
				return false;
			}

			FString PinPathInStruct, NewPinPathInStruct;
			if (URigVMPin::SplitPinPathAtStart(PinPathInNode, NodeName, PinPathInStruct))
			{
				if (ShouldRedirectPin(ValuePin->GetScriptStruct(), PinPathInStruct, NewPinPathInStruct))
				{
					InOutNewPinPath = URigVMPin::JoinPinPath(ValuePin->GetPinPath(), NewPinPathInStruct);
					return true;
				}
			}
		}
	}

	return false;
}

void URigVMController::RepopulatePinsOnNode(URigVMNode* InNode, bool bFollowCoreRedirectors, bool bNotify, bool bSetupOrphanedPins)
{
	if (InNode == nullptr)
	{
		ReportError(TEXT("InNode is nullptr."));
		return;
	}

	FRigVMControllerCompileBracketScope CompileBracketScope(this);

	URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(InNode);
	URigVMRerouteNode* RerouteNode = Cast<URigVMRerouteNode>(InNode);
	URigVMFunctionEntryNode* EntryNode = Cast<URigVMFunctionEntryNode>(InNode);
	URigVMFunctionReturnNode* ReturnNode = Cast<URigVMFunctionReturnNode>(InNode);
	URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(InNode);
	URigVMFunctionReferenceNode* FunctionRefNode = Cast<URigVMFunctionReferenceNode>(InNode);
	URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(InNode);

	TGuardValue<bool> EventuallySuspendNotifs(bSuspendNotifications, !bNotify);
	FScopeLock Lock(&PinPathCoreRedirectorsLock);

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	// step 1/3: keep a record of the current state of the node's pins
	TMap<FString, FString> RedirectedPinPaths;
	if (bFollowCoreRedirectors)
	{
		RedirectedPinPaths = GetRedirectedPinPaths(InNode);
	}
	TMap<FString, FPinState> PinStates = GetPinStates(InNode);

	// also in case this node is part of an injection
	FName InjectionInputPinName = NAME_None;
	FName InjectionOutputPinName = NAME_None;
	if (URigVMInjectionInfo* InjectionInfo = InNode->GetInjectionInfo())
	{
		InjectionInputPinName = InjectionInfo->InputPin ? InjectionInfo->InputPin->GetFName() : NAME_None;
		InjectionOutputPinName = InjectionInfo->OutputPin ? InjectionInfo->OutputPin->GetFName() : NAME_None;
	}

	// step 2/3: clear pins on the node and repopulate the node with new pins
	if (UnitNode != nullptr)
	{
		RemovePinsDuringRepopulate(UnitNode, UnitNode->Pins, bNotify, bSetupOrphanedPins);

		UScriptStruct* ScriptStruct = UnitNode->GetScriptStruct();
		if (ScriptStruct == nullptr)
		{
			ReportWarningf(
				TEXT("Control Rig '%s', Node '%s' has no struct assigned. Do you have a broken redirect?"),
				*UnitNode->GetOutermost()->GetPathName(),
				*UnitNode->GetName()
				);

			RemoveNode(UnitNode, false, true);
			return;
		}

		FString NodeColorMetadata;
		ScriptStruct->GetStringMetaDataHierarchical(*URigVMNode::NodeColorName, &NodeColorMetadata);
		if (!NodeColorMetadata.IsEmpty())
		{
			UnitNode->NodeColor = GetColorFromMetadata(NodeColorMetadata);
		}

		FString ExportedDefaultValue;
		CreateDefaultValueForStructIfRequired(ScriptStruct, ExportedDefaultValue);
		AddPinsForStruct(ScriptStruct, UnitNode, nullptr, ERigVMPinDirection::Invalid, ExportedDefaultValue, false, bNotify);
	}
	else if ((RerouteNode != nullptr) || (VariableNode != nullptr))
	{
		if (InNode->GetPins().Num() == 0)
		{
			return;
		}

		URigVMPin* ValuePin = nullptr;
		if(RerouteNode)
		{
			ValuePin = RerouteNode->Pins[0];
		}
		else
		{
			ValuePin = VariableNode->FindPin(URigVMVariableNode::ValueName);
		}
		check(ValuePin);
		EnsurePinValidity(ValuePin, false);
		
		RemovePinsDuringRepopulate(InNode, ValuePin->SubPins, bNotify, bSetupOrphanedPins);

		if (ValuePin->IsStruct())
		{
			UScriptStruct* ScriptStruct = ValuePin->GetScriptStruct();
			if (ScriptStruct == nullptr)
			{
				ReportErrorf(
					TEXT("Control Rig '%s', Node '%s' has no struct assigned. Do you have a broken redirect?"),
					*InNode->GetOutermost()->GetPathName(),
					*InNode->GetName()
				);

				RemoveNode(InNode, false, true);
				return;
			}

			FString ExportedDefaultValue;
			CreateDefaultValueForStructIfRequired(ScriptStruct, ExportedDefaultValue);
			AddPinsForStruct(ScriptStruct, InNode, ValuePin, ValuePin->Direction, ExportedDefaultValue, false);
		}
	}
	else if (EntryNode || ReturnNode)
	{
		if (URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(InNode->GetGraph()->GetOuter()))
		{
			bool bIsEntryNode = EntryNode != nullptr;
			RemovePinsDuringRepopulate(InNode, InNode->Pins, bNotify, bSetupOrphanedPins);

			TArray<URigVMPin*> SortedLibraryPins;

			// add execute pins first
			for (URigVMPin* LibraryPin : LibraryNode->GetPins())
			{
				if (LibraryPin->IsExecuteContext())
				{
					SortedLibraryPins.Add(LibraryPin);
				}
			}

			// add remaining pins
			for (URigVMPin* LibraryPin : LibraryNode->GetPins())
			{
				SortedLibraryPins.AddUnique(LibraryPin);
			}

			for (URigVMPin* LibraryPin : SortedLibraryPins)
			{
				if (LibraryPin->GetDirection() == ERigVMPinDirection::IO && !LibraryPin->IsExecuteContext())
				{
					continue;
				}

				if (bIsEntryNode)
				{
					if (LibraryPin->GetDirection() == ERigVMPinDirection::Output)
					{
						continue;
					}
				}
				else
				{
					if (LibraryPin->GetDirection() == ERigVMPinDirection::Input)
					{
						continue;
					}
				}

				URigVMPin* ExposedPin = NewObject<URigVMPin>(InNode, LibraryPin->GetFName());
				ConfigurePinFromPin(ExposedPin, LibraryPin);

				if (bIsEntryNode)
				{
					ExposedPin->Direction = ERigVMPinDirection::Output;
				}
				else
				{
					ExposedPin->Direction = ERigVMPinDirection::Input;
				}

				AddNodePin(InNode, ExposedPin);

				if (ExposedPin->IsStruct())
				{
					AddPinsForStruct(ExposedPin->GetScriptStruct(), InNode, ExposedPin, ExposedPin->GetDirection(), FString(), false);
				}

				Notify(ERigVMGraphNotifType::PinAdded, ExposedPin);
			}
		}
		else
		{
			// due to earlier bugs with copy and paste we can find entry and return nodes under the top level
			// graph. we'll ignore these for now.
		}
	}
	else if (CollapseNode)
	{
		FRigVMControllerGraphGuard GraphGuard(this, CollapseNode->GetContainedGraph(), false);
		// need to get a copy of the node array since the following function could remove nodes from the graph
		// we don't want to remove elements from the array we are iterating over.
		TArray<URigVMNode*> ContainedNodes = CollapseNode->GetContainedNodes();
		for (URigVMNode* ContainedNode : ContainedNodes)
		{
			RepopulatePinsOnNode(ContainedNode, bFollowCoreRedirectors, bNotify, bSetupOrphanedPins);
		}
	}
	else if (FunctionRefNode)
	{
		if (URigVMLibraryNode* ReferencedNode = FunctionRefNode->GetReferencedNode())
		{
			// we want to make sure notify the graph of a potential name change
			// when repopulating the function ref node
			Notify(ERigVMGraphNotifType::NodeRenamed, FunctionRefNode);
			RemovePinsDuringRepopulate(InNode, InNode->Pins, bNotify, bSetupOrphanedPins);

			TMap<FString, FPinState> ReferencedPinStates = GetPinStates(ReferencedNode);

			for (URigVMPin* ReferencedPin : ReferencedNode->Pins)
			{
				URigVMPin* NewPin = NewObject<URigVMPin>(InNode, ReferencedPin->GetFName());
				ConfigurePinFromPin(NewPin, ReferencedPin);

				AddNodePin(InNode, NewPin);

				if (NewPin->IsStruct())
				{
					AddPinsForStruct(NewPin->GetScriptStruct(), InNode, NewPin, NewPin->GetDirection(), FString(), false);
				}

				Notify(ERigVMGraphNotifType::PinAdded, NewPin);
			}

			ApplyPinStates(InNode, ReferencedPinStates);
		}
	}

	else
	{
		return;
	}

	ApplyPinStates(InNode, PinStates, RedirectedPinPaths);

	if (URigVMInjectionInfo* InjectionInfo = InNode->GetInjectionInfo())
	{
		InjectionInfo->InputPin = InNode->FindPin(InjectionInputPinName.ToString());
		InjectionInfo->OutputPin = InNode->FindPin(InjectionOutputPinName.ToString());
	}
}

void URigVMController::RemovePinsDuringRepopulate(URigVMNode* InNode, TArray<URigVMPin*>& InPins, bool bNotify, bool bSetupOrphanedPins)
{
	TArray<URigVMPin*> Pins = InPins;
	for (URigVMPin* Pin : Pins)
	{
		if(bSetupOrphanedPins && !Pin->IsExecuteContext())
		{
			URigVMPin* RootPin = Pin->GetRootPin();
			const FString OrphanedName = FString::Printf(TEXT("%s%s"), *URigVMPin::OrphanPinPrefix, *RootPin->GetName());

			URigVMPin* OrphanedRootPin = nullptr;
			
			for(URigVMPin* OrphanedPin : InNode->OrphanedPins)
			{
				if(OrphanedPin->GetName() == OrphanedName)
				{
					OrphanedRootPin = OrphanedPin;
					break;
				}
			}

			if(OrphanedRootPin == nullptr)
			{
				if(Pin->IsRootPin()) // if we are passing root pins we can reparent them directly
				{
					RootPin->DisplayName = RootPin->GetFName();
					RenameObject(RootPin, *OrphanedName, nullptr);
					InNode->Pins.Remove(RootPin);

					if(bNotify)
					{
						Notify(ERigVMGraphNotifType::PinRemoved, RootPin);
					}

					InNode->OrphanedPins.Add(RootPin);

					if(bNotify)
					{
						Notify(ERigVMGraphNotifType::PinAdded, RootPin);
					}
				}
				else // while if we are iterating over sub pins - we should reparent them
				{
					OrphanedRootPin = NewObject<URigVMPin>(RootPin->GetNode(), *OrphanedName);
					ConfigurePinFromPin(OrphanedRootPin, RootPin);
					OrphanedRootPin->DisplayName = RootPin->GetFName();
				
					OrphanedRootPin->GetNode()->OrphanedPins.Add(OrphanedRootPin);

					if(bNotify)
					{
						Notify(ERigVMGraphNotifType::PinAdded, OrphanedRootPin);
					}
				}
			}

			if(!Pin->IsRootPin() && (OrphanedRootPin != nullptr))
			{
				RenameObject(Pin, nullptr, OrphanedRootPin);
				RootPin->SubPins.Remove(Pin);
				EnsurePinValidity(Pin, false);
				AddSubPin(OrphanedRootPin, Pin);
			}
		}
	}

	for (URigVMPin* Pin : Pins)
	{
		if(!Pin->IsOrphanPin())
		{
			RemovePin(Pin, false, bNotify);
		}
	}
	InPins.Reset();
}

bool URigVMController::RemoveUnusedOrphanedPins(URigVMNode* InNode, bool bNotify)
{
	if(!InNode->HasOrphanedPins())
	{
		return true;
	}
	
	TArray<URigVMPin*> RemainingOrphanPins;
	for(int32 PinIndex=0; PinIndex < InNode->OrphanedPins.Num(); PinIndex++)
	{
		URigVMPin* OrphanedPin = InNode->OrphanedPins[PinIndex];

		const int32 NumSourceLinks = OrphanedPin->GetSourceLinks(true).Num(); 
		const int32 NumTargetLinks = OrphanedPin->GetTargetLinks(true).Num();

		if(NumSourceLinks + NumTargetLinks == 0)
		{
			RemovePin(OrphanedPin, false, bNotify);
		}
		else
		{
			RemainingOrphanPins.Add(OrphanedPin);
		}
	}

	InNode->OrphanedPins = RemainingOrphanPins;
	
	return !InNode->HasOrphanedPins();
}

#endif

void URigVMController::SetupDefaultUnitNodeDelegates(TDelegate<FName(FRigVMExternalVariable, FString)> InCreateExternalVariableDelegate)
{
	TWeakObjectPtr<URigVMController> WeakThis(this);

	UnitNodeCreatedContext.GetAllExternalVariablesDelegate().BindLambda([WeakThis]() -> TArray<FRigVMExternalVariable> {
		if (WeakThis.IsValid())
		{
			return WeakThis->GetAllVariables();
		}
		return TArray<FRigVMExternalVariable>();
	});

	UnitNodeCreatedContext.GetBindPinToExternalVariableDelegate().BindLambda([WeakThis](FString InPinPath, FString InVariablePath) -> bool {
		if (WeakThis.IsValid())
		{
			return WeakThis->BindPinToVariable(InPinPath, InVariablePath, true);
		}
		return false;
	});

	UnitNodeCreatedContext.GetCreateExternalVariableDelegate() = InCreateExternalVariableDelegate;
}

void URigVMController::ResetUnitNodeDelegates()
{
	UnitNodeCreatedContext.GetAllExternalVariablesDelegate().Unbind();
	UnitNodeCreatedContext.GetBindPinToExternalVariableDelegate().Unbind();
	UnitNodeCreatedContext.GetCreateExternalVariableDelegate().Unbind();
}

FLinearColor URigVMController::GetColorFromMetadata(const FString& InMetadata)
{
	FLinearColor Color = FLinearColor::Black;

	FString Metadata = InMetadata;
	Metadata.TrimStartAndEndInline();
	FString SplitString(TEXT(" "));
	FString Red, Green, Blue, GreenAndBlue;
	if (Metadata.Split(SplitString, &Red, &GreenAndBlue))
	{
		Red.TrimEndInline();
		GreenAndBlue.TrimStartInline();
		if (GreenAndBlue.Split(SplitString, &Green, &Blue))
		{
			Green.TrimEndInline();
			Blue.TrimStartInline();

			float RedValue = FCString::Atof(*Red);
			float GreenValue = FCString::Atof(*Green);
			float BlueValue = FCString::Atof(*Blue);
			Color = FLinearColor(RedValue, GreenValue, BlueValue);
		}
	}

	return Color;
}

TMap<FString, FString> URigVMController::GetRedirectedPinPaths(URigVMNode* InNode) const
{
	TMap<FString, FString> RedirectedPinPaths;
	URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(InNode);
	URigVMRerouteNode* RerouteNode = Cast<URigVMRerouteNode>(InNode);

	UScriptStruct* OwningStruct = nullptr;
	if (UnitNode)
	{
		OwningStruct = UnitNode->GetScriptStruct();
	}
	else if (RerouteNode)
	{
		URigVMPin* ValuePin = RerouteNode->Pins[0];
		if (ValuePin->IsStruct())
		{
			OwningStruct = ValuePin->GetScriptStruct();
		}
	}

	if (OwningStruct)
	{
		TArray<URigVMPin*> AllPins = InNode->GetAllPinsRecursively();
		for (URigVMPin* Pin : AllPins)
		{
			FString NodeName, PinPath;
			URigVMPin::SplitPinPathAtStart(Pin->GetPinPath(), NodeName, PinPath);

			if (RerouteNode)
			{
				FString ValuePinName, SubPinPath;
				if (URigVMPin::SplitPinPathAtStart(PinPath, ValuePinName, SubPinPath))
				{
					FString RedirectedSubPinPath;
					if (ShouldRedirectPin(OwningStruct, SubPinPath, RedirectedSubPinPath))
					{
						FString RedirectedPinPath = URigVMPin::JoinPinPath(ValuePinName, RedirectedSubPinPath);
						RedirectedPinPaths.Add(PinPath, RedirectedPinPath);
					}
				}
			}
			else
			{
				FString RedirectedPinPath;
				if (ShouldRedirectPin(OwningStruct, PinPath, RedirectedPinPath))
				{
					RedirectedPinPaths.Add(PinPath, RedirectedPinPath);
				}
			}
		}
	};
	return RedirectedPinPaths;
}

URigVMController::FPinState URigVMController::GetPinState(URigVMPin* InPin) const
{
	FPinState State;
	State.Direction = InPin->GetDirection();
	State.CPPType = InPin->GetCPPType();
	State.CPPTypeObject = InPin->GetCPPTypeObject();
	State.DefaultValue = InPin->GetDefaultValue();
	State.bIsExpanded = InPin->IsExpanded();
	State.InjectionInfos = InPin->GetInjectedNodes();
	return State;
}

TMap<FString, URigVMController::FPinState> URigVMController::GetPinStates(URigVMNode* InNode) const
{
	TMap<FString, FPinState> PinStates;

	TArray<URigVMPin*> AllPins = InNode->GetAllPinsRecursively();
	for (URigVMPin* Pin : AllPins)
	{
		FString PinPath, NodeName;
		URigVMPin::SplitPinPathAtStart(Pin->GetPinPath(), NodeName, PinPath);

		// we need to ensure validity here because GetPinState()-->GetDefaultValue() needs pin to be in a valid state.
		// some additional context:
		// right after load, some pins will be a invalid state because they don't have their CPPTypeObject,
		// which is expected since it is a transient property.
		// if the CPPTypeObject is not there, those pins may struggle with producing a valid default value
		// because Pin->IsStruct() will always be false if the pin does not have a valid type object.
		if (Pin->IsRootPin())
		{
			EnsurePinValidity(Pin, true);
		}
		FPinState State = GetPinState(Pin);
		PinStates.Add(PinPath, State);
	}

	return PinStates;
}

void URigVMController::ApplyPinState(URigVMPin* InPin, const FPinState& InPinState)
{
	for (URigVMInjectionInfo* InjectionInfo : InPinState.InjectionInfos)
	{
		RenameObject(InjectionInfo, nullptr, InPin);
		InjectionInfo->InputPin = InjectionInfo->InputPin ? InjectionInfo->Node->FindPin(InjectionInfo->InputPin->GetName()) : nullptr;
		InjectionInfo->OutputPin = InjectionInfo->OutputPin ? InjectionInfo->Node->FindPin(InjectionInfo->OutputPin->GetName()) : nullptr;
		InPin->InjectionInfos.Add(InjectionInfo);
	}

	if (!InPinState.DefaultValue.IsEmpty())
	{
		SetPinDefaultValue(InPin, InPinState.DefaultValue, true, false, false);
	}

	SetPinExpansion(InPin, InPinState.bIsExpanded, false);
}

void URigVMController::ApplyPinStates(URigVMNode* InNode, const TMap<FString, URigVMController::FPinState>& InPinStates, const TMap<FString, FString>& InRedirectedPinPaths)
{
	FRigVMControllerCompileBracketScope CompileBracketScope(this);
	for (const TPair<FString, FPinState>& PinStatePair : InPinStates)
	{
		FString PinPath = PinStatePair.Key;
		const FPinState& PinState = PinStatePair.Value;

		if (InRedirectedPinPaths.Contains(PinPath))
		{
			PinPath = InRedirectedPinPaths.FindChecked(PinPath);
		}

		if (URigVMPin* Pin = InNode->FindPin(PinPath))
		{
			ApplyPinState(Pin, PinState);
		}
		else
		{
			for (URigVMInjectionInfo* InjectionInfo : PinState.InjectionInfos)
			{
				RenameObject(InjectionInfo->Node, nullptr, InNode->GetGraph());
				DestroyObject(InjectionInfo);
			}
		}
	}
}

void URigVMController::ReportWarning(const FString& InMessage) const
{
	if(!bReportWarningsAndErrors)
	{
		return;
	}

	FString Message = InMessage;
	if (URigVMGraph* Graph = GetGraph())
	{
		if (UPackage* Package = Cast<UPackage>(Graph->GetOutermost()))
		{
			Message = FString::Printf(TEXT("%s : %s"), *Package->GetPathName(), *InMessage);
		}
	}

	FScriptExceptionHandler::Get().HandleException(ELogVerbosity::Warning, *Message, *FString());
}

void URigVMController::ReportError(const FString& InMessage) const
{
	if(!bReportWarningsAndErrors)
	{
		return;
	}

	FString Message = InMessage;
	if (URigVMGraph* Graph = GetGraph())
	{
		if (UPackage* Package = Cast<UPackage>(Graph->GetOutermost()))
		{
			Message = FString::Printf(TEXT("%s : %s"), *Package->GetPathName(), *InMessage);
		}
	}

	FScriptExceptionHandler::Get().HandleException(ELogVerbosity::Error, *Message, *FString());
}

void URigVMController::ReportAndNotifyError(const FString& InMessage) const
{
	if (!bReportWarningsAndErrors)
	{
		return;
	}

	ReportError(InMessage);

#if WITH_EDITOR
	FNotificationInfo Info(FText::FromString(InMessage));
	Info.bUseSuccessFailIcons = true;
	Info.Image = FEditorStyle::GetBrush(TEXT("MessageLog.Warning"));
	Info.bFireAndForget = true;
	Info.bUseThrobber = true;
	// longer message needs more time to read
	Info.FadeOutDuration = FMath::Clamp(0.1f * InMessage.Len(), 5.0f, 20.0f);
	Info.ExpireDuration = Info.FadeOutDuration;
	TSharedPtr<SNotificationItem> NotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
	if (NotificationPtr)
	{
		NotificationPtr->SetCompletionState(SNotificationItem::CS_Fail);
	}
#endif
}


void URigVMController::CreateDefaultValueForStructIfRequired(UScriptStruct* InStruct, FString& InOutDefaultValue)
{
	if (InStruct != nullptr)
	{
		TArray<uint8, TAlignedHeapAllocator<16>> TempBuffer;
		TempBuffer.AddUninitialized(InStruct->GetStructureSize());

		// call the struct constructor to initialize the struct
		InStruct->InitializeDefaultValue(TempBuffer.GetData());

		// apply any higher-level value overrides
		// for example,  
		// struct B { int Test; B() {Test = 1;}}; ----> This is the constructor initialization, applied first in InitializeDefaultValue() above 
		// struct A 
		// {
		//		Array<B> TestArray;
		//		A() 
		//		{
		//			TestArray.Add(B());
		//			TestArray[0].Test = 2;  ----> This is the overrride, applied below in ImportText()
		//		}
		// }
		// See UnitTest RigVM->Graph->UnitNodeDefaultValue for more use case.
		
		if (!InOutDefaultValue.IsEmpty() && InOutDefaultValue != TEXT("()"))
		{ 
			InStruct->ImportText(*InOutDefaultValue, TempBuffer.GetData(), nullptr, PPF_None, nullptr, FString());
		}

		// in case InOutDefaultValue is not empty, it needs to be cleared
		// before ExportText() because ExportText() appends to it.
		InOutDefaultValue.Reset();

		InStruct->ExportText(InOutDefaultValue, TempBuffer.GetData(), nullptr, nullptr, PPF_None, nullptr);
		InStruct->DestroyStruct(TempBuffer.GetData());
	}
}

void URigVMController::PostProcessDefaultValue(URigVMPin* Pin, FString& OutDefaultValue)
{
	if (Pin->IsArray() && OutDefaultValue.IsEmpty())
	{
		OutDefaultValue = TEXT("()");
	}
	else if (Pin->IsStruct() && (OutDefaultValue.IsEmpty() || OutDefaultValue == TEXT("()")))
	{
		CreateDefaultValueForStructIfRequired(Pin->GetScriptStruct(), OutDefaultValue);
	}
	else if (Pin->IsStringType())
	{
		while (OutDefaultValue.StartsWith(TEXT("\"")))
		{
			OutDefaultValue = OutDefaultValue.RightChop(1);
		}
		while (OutDefaultValue.EndsWith(TEXT("\"")))
		{
			OutDefaultValue = OutDefaultValue.LeftChop(1);
		}
	}
}

FString URigVMController::PostProcessCPPType(const FString& InCPPType, UObject* InCPPTypeObject)
{
	FString CPPType = InCPPType;
	
	if (const UClass* Class = Cast<UClass>(InCPPTypeObject))
	{
		CPPType = FString::Printf(TObjectPtrTemplate, Class->GetPrefixCPP(), *Class->GetName());
	}
	else if (const UScriptStruct* ScriptStruct = Cast<UScriptStruct>(InCPPTypeObject))
	{
		CPPType = ScriptStruct->GetStructCPPName();
	}
	else if (UEnum* Enum = Cast<UEnum>(InCPPTypeObject))
	{
		CPPType = Enum->CppType;
	}

	if(CPPType != InCPPType)
	{
		FString TemplateType = InCPPType;
		while (RigVMTypeUtils::IsArrayType(TemplateType))
		{
			CPPType = RigVMTypeUtils::ArrayTypeFromBaseType(CPPType);
			TemplateType = RigVMTypeUtils::BaseTypeFromArrayType(TemplateType);
		}		
	}
	
	return CPPType;
}

/*
void URigVMController::PotentiallyResolvePrototypeNode(URigVMPrototypeNode* InNode, bool bSetupUndoRedo)
{
	TArray<URigVMNode*> NodesVisited;
	PotentiallyResolvePrototypeNode(InNode, bSetupUndoRedo, NodesVisited);
}

void URigVMController::PotentiallyResolvePrototypeNode(URigVMPrototypeNode* InNode, bool bSetupUndoRedo, TArray<URigVMNode*>& NodesVisited)
{
	if (InNode == nullptr)
	{
		return;
	}

	if (NodesVisited.Contains(InNode))
	{
		return;
	}
	NodesVisited.Add(InNode);

	// propagate types first
	for (URigVMPin* Pin : InNode->GetPins())
	{
		if (Pin->CPPType.IsEmpty())
		{
			TArray<URigVMPin*> LinkedPins = Pin->GetLinkedSourcePins();
			LinkedPins.Append(Pin->GetLinkedTargetPins());

			for (URigVMPin* LinkedPin : LinkedPins)
			{
				if (!LinkedPin->CPPType.IsEmpty())
				{
					ChangePinType(Pin, LinkedPin->CPPType, LinkedPin->CPPTypeObjectPath, bSetupUndoRedo);
					break;
				}
			}
		}
	}

	// check if the node is resolved
	FRigVMPrototype::FTypeMap ResolvedTypes;
	int32 FunctionIndex = InNode->GetResolvedFunctionIndex(&ResolvedTypes);
	if (FunctionIndex != INDEX_NONE)
	{
		// we have a valid node - let's replace this node... first let's find all 
		// links and all default values
		TMap<FString, FString> DefaultValues;
		TArray<TPair<FString, FString>> LinkPaths;

		for (URigVMPin* Pin : InNode->GetPins())
		{
			FString DefaultValue = Pin->GetDefaultValue();
			if (!DefaultValue.IsEmpty())
			{
				DefaultValues.Add(Pin->GetPinPath(), DefaultValue);
			}

			TArray<URigVMLink*> Links = Pin->GetSourceLinks(true);
			Links.Append(Pin->GetTargetLinks(true));

			for (URigVMLink* Link : Links)
			{
				LinkPaths.Add(TPair<FString, FString>(Link->GetSourcePin()->GetPinPath(), Link->GetTargetPin()->GetPinPath()));
			}
		}

		const FRigVMFunction& Function = FRigVMRegistry::Get().GetFunctions()[FunctionIndex];
		FString NodeName = InNode->GetName();
		FVector2D NodePosition = InNode->GetPosition();

		RemoveNode(InNode, bSetupUndoRedo);

		if (URigVMNode* NewNode = AddUnitNode(Function.Struct, Function.GetMethodName(), NodePosition, NodeName, bSetupUndoRedo))
		{
			// set default values again
			for (TPair<FString, FString> Pair : DefaultValues)
			{
				SetPinDefaultValue(Pair.Key, Pair.Value, true, bSetupUndoRedo, false);
			}

			// reestablish links
			for (TPair<FString, FString> Pair : LinkPaths)
			{
				AddLink(Pair.Key, Pair.Value, bSetupUndoRedo);
			}
		}

		return;
	}
	else
	{
		// update all of the pins that might have changed now as well!
		for (URigVMPin* Pin : InNode->GetPins())
		{
			if (Pin->CPPType.IsEmpty())
			{
				if (const FRigVMPrototypeArg::FType* Type = ResolvedTypes.Find(Pin->GetFName()))
				{
					if (!Type->CPPType.IsEmpty())
					{
						ChangePinType(Pin, Type->CPPType, Type->GetCPPTypeObjectPath(), bSetupUndoRedo);
					}
				}
			}
		}
	}

	// then recursively call
	TArray<URigVMNode*> LinkedNodes = InNode->GetLinkedSourceNodes();
	LinkedNodes.Append(InNode->GetLinkedTargetNodes());
	for (URigVMNode* LinkedNode : LinkedNodes)
	{
		PotentiallyResolvePrototypeNode(Cast<URigVMPrototypeNode>(LinkedNode), bSetupUndoRedo, NodesVisited);
	}
}
*/

void URigVMController::ResolveUnknownTypePin(URigVMPin* InPinToResolve, const URigVMPin* InTemplatePin,
	bool bSetupUndoRedo, bool bTraverseNode, bool bTraverseParentPins, bool bTraverseLinks)
{
	check(InPinToResolve);
	check(InTemplatePin);
	check(InPinToResolve->IsUnknownType());
	check(!InTemplatePin->IsUnknownType());

	FRigVMControllerCompileBracketScope CompileScope(this);
	if(bTraverseParentPins)
	{
		URigVMPin* RootPin = InPinToResolve->GetRootPin();
		if(RootPin != InPinToResolve && RootPin->IsUnknownType())
		{
			ResolveUnknownTypePin(RootPin, InTemplatePin, bSetupUndoRedo, bTraverseNode, false, bTraverseLinks);
			return;
		}
	}

	if(InPinToResolve->IsArray())
	{
		for(URigVMPin* SubPin : InPinToResolve->GetSubPins())
		{
			if(SubPin->IsUnknownType())
			{
				ResolveUnknownTypePin(SubPin, InTemplatePin, bSetupUndoRedo, false, false, bTraverseLinks);
			}
		}
	}

	FString ResolvedCPPType = InTemplatePin->IsArray() ? InTemplatePin->GetArrayElementCppType() : InTemplatePin->GetCPPType();
	if(InPinToResolve->IsArray())
	{
		ResolvedCPPType = FString::Printf(TArrayTemplate, *ResolvedCPPType);
	}

	if(!ChangePinType(InPinToResolve, ResolvedCPPType, InTemplatePin->GetCPPTypeObject(), bSetupUndoRedo, false, false, false))
	{
		return;
	}

	if(bTraverseNode)
	{
		if(const URigVMNode* Node = InPinToResolve->GetNode())
		{
			for(URigVMPin* Pin : Node->GetPins())
			{
				if(Pin->IsUnknownType())
				{
					ResolveUnknownTypePin(Pin, InTemplatePin, bSetupUndoRedo, false, bTraverseParentPins, bTraverseLinks);
				}
			}
		}
	}

	if(bTraverseLinks)
	{
		// we don't need to recurse since unknown type pins don't have sub pins
		TArray<URigVMPin*> LinkedPins = InPinToResolve->GetLinkedSourcePins(false); 
		LinkedPins.Append(InPinToResolve->GetLinkedTargetPins(false));

		for(URigVMPin* LinkedPin : LinkedPins)
		{
			if(LinkedPin->IsUnknownType())
			{
				ResolveUnknownTypePin(LinkedPin, InTemplatePin, bSetupUndoRedo);
			}
		}
	}
}

bool URigVMController::ChangePinType(const FString& InPinPath, const FString& InCPPType, const FName& InCPPTypeObjectPath, bool bSetupUndoRedo, bool bSetupOrphanPins, bool bBreakLinks, bool bRemoveSubPins)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (URigVMPin* Pin = Graph->FindPin(InPinPath))
	{
		return ChangePinType(Pin, InCPPType, InCPPTypeObjectPath, bSetupUndoRedo, bSetupOrphanPins, bBreakLinks, bRemoveSubPins);
	}

	return false;
}

bool URigVMController::ChangePinType(URigVMPin* InPin, const FString& InCPPType, const FName& InCPPTypeObjectPath, bool bSetupUndoRedo, bool bSetupOrphanPins, bool bBreakLinks, bool bRemoveSubPins)
{
	if (InCPPType == TEXT("None") || InCPPType.IsEmpty())
	{
		return false;
	}

	UObject* CPPTypeObject = URigVMPin::FindObjectFromCPPTypeObjectPath(InCPPTypeObjectPath.ToString());
	if (InPin->CPPType == InCPPType && InPin->CPPTypeObject == CPPTypeObject)
	{
		return true;
	}

	return ChangePinType(InPin, InCPPType, CPPTypeObject, bSetupUndoRedo, bSetupOrphanPins, bBreakLinks, bRemoveSubPins);
}

bool URigVMController::ChangePinType(URigVMPin* InPin, const FString& InCPPType, UObject* InCPPTypeObject, bool bSetupUndoRedo, bool bSetupOrphanPins, bool bBreakLinks, bool bRemoveSubPins)
{
	if (InCPPType == TEXT("None") || InCPPType.IsEmpty())
	{
		return false;
	}

	FName CPPTypeObjectPath(NAME_None);
	if(InCPPTypeObject)
	{
		CPPTypeObjectPath = *InCPPTypeObject->GetPathName();
	}

	if (FRigVMPropertyDescription::RequiresCPPTypeObject(InCPPType) && !InCPPTypeObject)
	{
		return false;
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMBaseAction Action;
	if (bSetupUndoRedo)
	{
		Action.Title = TEXT("Change pin type");
		ActionStack->BeginAction(Action);
	}

	TArray<URigVMLink*> Links;

	if (bSetupUndoRedo)
	{
		if(!bSetupOrphanPins && bBreakLinks)
		{
			BreakAllLinks(InPin, true, true);
			BreakAllLinks(InPin, false, true);
			BreakAllLinksRecursive(InPin, true, false, true);
			BreakAllLinksRecursive(InPin, false, false, true);
		}
		ActionStack->AddAction(FRigVMChangePinTypeAction(InPin, InCPPType, CPPTypeObjectPath, bSetupOrphanPins, bBreakLinks, bRemoveSubPins));
	}
	
	if(bSetupOrphanPins)
	{
		Links.Append(InPin->GetSourceLinks(true));
		Links.Append(InPin->GetTargetLinks(true));
		DetachLinksFromPinObjects(&Links, true);

		const FString OrphanedName = FString::Printf(TEXT("%s%s"), *URigVMPin::OrphanPinPrefix, *InPin->GetName());
		if(InPin->GetNode()->FindPin(OrphanedName) == nullptr)
		{
			URigVMPin* OrphanedPin = NewObject<URigVMPin>(InPin->GetNode(), *OrphanedName);
			ConfigurePinFromPin(OrphanedPin, InPin);
			OrphanedPin->DisplayName = InPin->GetFName();

			if(OrphanedPin->IsStruct())
			{
				AddPinsForStruct(OrphanedPin->GetScriptStruct(), OrphanedPin->GetNode(), OrphanedPin, OrphanedPin->Direction, OrphanedPin->GetDefaultValue(), false, true);
			}
				
			InPin->GetNode()->OrphanedPins.Add(OrphanedPin);
		}
	}

	if(bRemoveSubPins || !InPin->IsArray())
	{
		TArray<URigVMPin*> Pins = InPin->SubPins;
		for (URigVMPin* Pin : Pins)
		{
			RemovePin(Pin, false, true);
		}
		
		InPin->SubPins.Reset();
	}
	
	InPin->CPPType = InCPPType;
	InPin->CPPTypeObjectPath = CPPTypeObjectPath;
	InPin->CPPTypeObject = InCPPTypeObject;
	// we might want to use GetPinInitialDefaultValue here for a better default value
	InPin->DefaultValue = FString();

	if (InPin->IsExecuteContext() && !InPin->GetNode()->IsA<URigVMFunctionEntryNode>() && !InPin->GetNode()->IsA<URigVMFunctionReturnNode>())
	{
		InPin->Direction = ERigVMPinDirection::IO;
	}

	if (InPin->IsStruct())
	{
		FString DefaultValue = InPin->DefaultValue;
		CreateDefaultValueForStructIfRequired(InPin->GetScriptStruct(), DefaultValue);
		AddPinsForStruct(InPin->GetScriptStruct(), InPin->GetNode(), InPin, InPin->Direction, DefaultValue, false, true);
	}

	Notify(ERigVMGraphNotifType::PinTypeChanged, InPin);
	Notify(ERigVMGraphNotifType::PinDefaultValueChanged, InPin);

	// in cases were we are just changing the type we have to let the
	// clients know that the links are still there
	if(!bSetupOrphanPins && !bBreakLinks && !bRemoveSubPins)
	{
		const TArray<URigVMLink*> CurrentLinks = InPin->GetLinks();
		for(URigVMLink* CurrentLink : CurrentLinks)
		{
			Notify(ERigVMGraphNotifType::LinkRemoved, CurrentLink);
			Notify(ERigVMGraphNotifType::LinkAdded, CurrentLink);
		}
	}

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	if(Links.Num() > 0)
	{
		ReattachLinksToPinObjects(false, &Links, true, true);
		RemoveUnusedOrphanedPins(InPin->GetNode(), true);
	}

	return true;
}

#if WITH_EDITOR

void URigVMController::RewireLinks(URigVMPin* InOldPin, URigVMPin* InNewPin, bool bAsInput, bool bSetupUndoRedo, TArray<URigVMLink*> InLinks)
{
	ensure(InOldPin->GetRootPin() == InOldPin);
	ensure(InNewPin->GetRootPin() == InNewPin);
	FRigVMControllerCompileBracketScope CompileScope(this);

 	if (bAsInput)
	{
		TArray<URigVMLink*> Links = InLinks;
		if (Links.Num() == 0)
		{
			Links = InOldPin->GetSourceLinks(true /* recursive */);
		}

		for (URigVMLink* Link : Links)
		{
			FString SegmentPath = Link->GetTargetPin()->GetSegmentPath();
			URigVMPin* NewPin = SegmentPath.IsEmpty() ? InNewPin : InNewPin->FindSubPin(SegmentPath);
			check(NewPin);

			BreakLink(Link->GetSourcePin(), Link->GetTargetPin(), false);
			AddLink(Link->GetSourcePin(), NewPin, false);
		}
	}
	else
	{
		TArray<URigVMLink*> Links = InLinks;
		if (Links.Num() == 0)
		{
			Links = InOldPin->GetTargetLinks(true /* recursive */);
		}

		for (URigVMLink* Link : Links)
		{
			FString SegmentPath = Link->GetSourcePin()->GetSegmentPath();
			URigVMPin* NewPin = SegmentPath.IsEmpty() ? InNewPin : InNewPin->FindSubPin(SegmentPath);
			check(NewPin);

			BreakLink(Link->GetSourcePin(), Link->GetTargetPin(), false);
			AddLink(NewPin, Link->GetTargetPin(), false);
		}
	}
}

#endif

bool URigVMController::RenameObject(UObject* InObjectToRename, const TCHAR* InNewName, UObject* InNewOuter)
{
	return InObjectToRename->Rename(InNewName, InNewOuter, REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
}

void URigVMController::DestroyObject(UObject* InObjectToDestroy)
{
	RenameObject(InObjectToDestroy, nullptr, GetTransientPackage());
	InObjectToDestroy->RemoveFromRoot();
}

void URigVMController::AddNodePin(URigVMNode* InNode, URigVMPin* InPin)
{
	ValidatePin(InPin);
	check(!InNode->Pins.Contains(InPin));
	InNode->Pins.Add(InPin);
}

void URigVMController::AddSubPin(URigVMPin* InParentPin, URigVMPin* InPin)
{
	ValidatePin(InPin);
	check(!InParentPin->SubPins.Contains(InPin));
	InParentPin->SubPins.Add(InPin);
}


static UObject* FindObjectGloballyWithRedirectors(const TCHAR* InObjectName)
{
	// Do a global search for the CPP type. Note that searching with ANY_PACKAGE _does not_
	// apply redirectors. So only if this fails do we apply them manually below.
	UObject* Object = FindObject<UField>(ANY_PACKAGE, InObjectName);
	if(Object != nullptr)
	{
		return Object;
	}
	
	// Apply redirectors and do another lookup using the redirected name. GetRedirectedName
	// will always return a valid name.
	const FCoreRedirectObjectName NewObjectName = FCoreRedirects::GetRedirectedName(
		ECoreRedirectFlags::Type_Class | ECoreRedirectFlags::Type_Struct | ECoreRedirectFlags::Type_Enum,
		FCoreRedirectObjectName(InObjectName));

	// If there was no package name, then there was no redirect set up for this type.
	if (NewObjectName.PackageName.IsNone())
	{
		return nullptr;
	}

	const FString RedirectedObjectName = NewObjectName.ObjectName.ToString();
	UPackage *Package = FindPackage(nullptr, *NewObjectName.PackageName.ToString());
	if (Package != nullptr)
	{
		Object = FindObject<UField>(Package, *RedirectedObjectName);
	}
	if (Package == nullptr || Object == nullptr)
	{
		// Hail Mary pass.
		Object = FindObject<UField>(ANY_PACKAGE, *RedirectedObjectName);
	}
	return Object;
}

bool URigVMController::EnsurePinValidity(URigVMPin* InPin, bool bRecursive)
{
	check(InPin);
	
	// check if the CPPTypeObject is set up correctly.
	if(FRigVMPropertyDescription::RequiresCPPTypeObject(InPin->GetCPPType()))
	{
		if(InPin->GetCPPTypeObject() == nullptr)
		{
			// try to find the CPPTypeObject by name
			FString CPPType = InPin->IsArray() ? InPin->GetArrayElementCppType() : InPin->GetCPPType();

			UObject* CPPTypeObject = FindObjectGloballyWithRedirectors(*CPPType);

			if (CPPTypeObject == nullptr)
			{
				// If we've mistakenly stored the struct type with the 'F', 'U', or 'A' prefixes, we need to strip them
				// off first. Enums are always named with their prefix intact.
				if (!CPPType.IsEmpty() && (CPPType[0] == TEXT('F') || CPPType[0] == TEXT('U') || CPPType[0] == TEXT('A')))
				{
					CPPType = CPPType.Mid(1);
				}
				CPPTypeObject = FindObjectGloballyWithRedirectors(*CPPType);
			}

			if(CPPTypeObject == nullptr)
			{
				const FString Message = FString::Printf(
					TEXT("%s: Pin '%s' is missing the CPPTypeObject for CPPType '%s'."),
					*InPin->GetPathName(), *InPin->GetPinPath(), *InPin->GetCPPType());
				FScriptExceptionHandler::Get().HandleException(ELogVerbosity::Error, *Message, *FString());
				return false;
			}
			
			InPin->CPPTypeObject = CPPTypeObject;
		}
	}

	InPin->CPPType = PostProcessCPPType(InPin->CPPType, InPin->GetCPPTypeObject());

	if(bRecursive)
	{
		for(URigVMPin* SubPin : InPin->SubPins)
		{
			if(!EnsurePinValidity(SubPin, bRecursive))
			{
				return false;
			}
		}
	}

	return true;
}


void URigVMController::ValidatePin(URigVMPin* InPin)
{
	check(InPin);
	
	// create a property description from the pin here as a test,
	// since the compiler needs this
	FRigVMPropertyDescription(InPin->GetFName(), InPin->GetCPPType(), InPin->GetCPPTypeObject(), InPin->GetDefaultValue());
}

FRigVMExternalVariable URigVMController::GetVariableByName(const FName& InExternalVariableName, const bool bIncludeInputArguments)
{
	TArray<FRigVMExternalVariable> Variables = GetAllVariables(bIncludeInputArguments);
	for (const FRigVMExternalVariable& Variable : Variables)
	{
		if (Variable.Name == InExternalVariableName)
		{
			return Variable;
		}
	}	
	
	return FRigVMExternalVariable();
}

TArray<FRigVMExternalVariable> URigVMController::GetAllVariables(const bool bIncludeInputArguments)
{
	TArray<FRigVMExternalVariable> ExternalVariables;

	if(URigVMGraph* Graph = GetGraph())
	{
		for (FRigVMGraphVariableDescription LocalVariable : Graph->GetLocalVariables(bIncludeInputArguments))
		{
			ExternalVariables.Add(LocalVariable.ToExternalVariable());
		}
	}
	
	if (GetExternalVariablesDelegate.IsBound())
	{
		ExternalVariables.Append(GetExternalVariablesDelegate.Execute(GetGraph()));
	}

	return ExternalVariables;
}

const FRigVMByteCode* URigVMController::GetCurrentByteCode() const
{
	if (GetCurrentByteCodeDelegate.IsBound())
	{
		return GetCurrentByteCodeDelegate.Execute();
	}
	return nullptr;
}

void URigVMController::RefreshFunctionReferences(URigVMLibraryNode* InFunctionDefinition, bool bSetupUndoRedo)
{
	check(InFunctionDefinition);

	if (URigVMFunctionLibrary* FunctionLibrary = Cast<URigVMFunctionLibrary>(InFunctionDefinition->GetGraph()))
	{
		FunctionLibrary->ForEachReference(InFunctionDefinition->GetFName(), [this, bSetupUndoRedo](URigVMFunctionReferenceNode* ReferenceNode)
		{
			FRigVMControllerGraphGuard GraphGuard(this, ReferenceNode->GetGraph(), bSetupUndoRedo);

			TArray<URigVMLink*> Links = ReferenceNode->GetLinks();
			DetachLinksFromPinObjects(&Links, true);
			RepopulatePinsOnNode(ReferenceNode, false, true);
			TGuardValue<bool> ReportGuard(bReportWarningsAndErrors, false);
			ReattachLinksToPinObjects(false, &Links, true);
		});
	}
}

FString URigVMController::GetGraphOuterName() const
{
	check(GetGraph() != nullptr);
	return GetSanitizedName(GetGraph()->GetRootGraph()->GetOuter()->GetFName().ToString(), true, false);
}

FString URigVMController::GetSanitizedName(const FString& InName, bool bAllowPeriod, bool bAllowSpace)
{
	FString CopiedName = InName;
	SanitizeName(CopiedName, bAllowPeriod, bAllowSpace);
	return CopiedName;
}

FString URigVMController::GetSanitizedGraphName(const FString& InName)
{
	return GetSanitizedName(InName, true, true);
}

FString URigVMController::GetSanitizedNodeName(const FString& InName)
{
	return GetSanitizedName(InName, false, true);
}

FString URigVMController::GetSanitizedVariableName(const FString& InName)
{
	return GetSanitizedName(InName, false, true);
}

FString URigVMController::GetSanitizedPinName(const FString& InName)
{
	return GetSanitizedName(InName, false, true);
}

FString URigVMController::GetSanitizedPinPath(const FString& InName)
{
	return GetSanitizedName(InName, true, true);
}

void URigVMController::SanitizeName(FString& InOutName, bool bAllowPeriod, bool bAllowSpace)
{
	// Sanitize the name
	for (int32 i = 0; i < InOutName.Len(); ++i)
	{
		TCHAR& C = InOutName[i];

		const bool bGoodChar =
			FChar::IsAlpha(C) ||											// Any letter (upper and lowercase) anytime
			(C == '_') || (C == '-') || 									// _  and - anytime
			(bAllowPeriod && (C == '.')) ||
			(bAllowSpace && (C == ' ')) ||
			((i > 0) && FChar::IsDigit(C));									// 0-9 after the first character

		if (!bGoodChar)
		{
			C = '_';
		}
	}

	if (InOutName.Len() > GetMaxNameLength())
	{
		InOutName.LeftChopInline(InOutName.Len() - GetMaxNameLength());
	}
}
