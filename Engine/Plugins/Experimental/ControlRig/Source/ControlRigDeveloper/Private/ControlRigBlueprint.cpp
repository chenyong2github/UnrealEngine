// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigBlueprint.h"
#include "ControlRigBlueprintGeneratedClass.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "Modules/ModuleManager.h"
#include "Engine/SkeletalMesh.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "ControlRig.h"
#include "Graph/ControlRigGraph.h"
#include "Graph/ControlRigGraphNode.h"
#include "UObject/UObjectGlobals.h"
#include "ControlRigObjectVersion.h"
#include "ControlRigDeveloper.h"
#include "Curves/CurveFloat.h"
#include "BlueprintCompilationManager.h"
#include "RigVMCompiler/RigVMCompiler.h"
#include "RigVMCore/RigVMRegistry.h"
#include "Units/Execution/RigUnit_BeginExecution.h"

#if WITH_EDITOR
#include "IControlRigEditorModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ControlRigBlueprintUtils.h"
#include "Settings/ControlRigSettings.h"
#endif//WITH_EDITOR

#define LOCTEXT_NAMESPACE "ControlRigBlueprint"

TArray<UControlRigBlueprint*> UControlRigBlueprint::sCurrentlyOpenedRigBlueprints;

UControlRigBlueprint::UControlRigBlueprint(const FObjectInitializer& ObjectInitializer)
{
	bSuspendModelNotificationsForSelf = false;
	bSuspendModelNotificationsForOthers = false;

#if WITH_EDITORONLY_DATA
	GizmoLibrary = UControlRigSettings::Get()->DefaultGizmoLibrary;
#endif

	bRecompileOnLoad = 0;
	bAutoRecompileVM = true;
	bVMRecompilationRequired = false;
	VMRecompilationBracket = 0;

	Model = ObjectInitializer.CreateDefaultSubobject<URigVMGraph>(this, TEXT("RigVMModel"));
	Controller = nullptr;
}

void UControlRigBlueprint::InitializeModelIfRequired()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (Controller == nullptr)
	{
		Controller = NewObject<URigVMController>(this);
		Controller->SetExecuteContextStruct(FControlRigExecuteContext::StaticStruct());
		Controller->SetGraph(Model);
		Controller->OnModified().AddUObject(this, &UControlRigBlueprint::HandleModifiedEvent);

		Controller->UnfoldStructDelegate.BindLambda([](const UStruct* InStruct) -> bool {

			if (InStruct == TBaseStructure<FQuat>::Get())
			{
				return false;
			}
			if (InStruct == FRuntimeFloatCurve::StaticStruct())
			{
				return false;
			}
			return true;
		});

		for (int32 i = 0; i < UbergraphPages.Num(); ++i)
		{
			if (UControlRigGraph* Graph = Cast<UControlRigGraph>(UbergraphPages[i]))
			{
				PopulateModelFromGraphForBackwardsCompatibility(Graph);
				RecompileVM();
				Graph->Initialize(this);
			}
		}

		HierarchyContainer.OnElementAdded.AddUObject(this, &UControlRigBlueprint::HandleOnElementAdded);
		HierarchyContainer.OnElementRemoved.AddUObject(this, &UControlRigBlueprint::HandleOnElementRemoved);
		HierarchyContainer.OnElementRenamed.AddUObject(this, &UControlRigBlueprint::HandleOnElementRenamed);
		HierarchyContainer.OnElementReparented.AddUObject(this, &UControlRigBlueprint::HandleOnElementReparented);
		HierarchyContainer.OnElementSelected.AddUObject(this, &UControlRigBlueprint::HandleOnElementSelected);
	}
}

UControlRigBlueprintGeneratedClass* UControlRigBlueprint::GetControlRigBlueprintGeneratedClass() const
{
	UControlRigBlueprintGeneratedClass* Result = Cast<UControlRigBlueprintGeneratedClass>(*GeneratedClass);
	return Result;
}

UControlRigBlueprintGeneratedClass* UControlRigBlueprint::GetControlRigBlueprintSkeletonClass() const
{
	UControlRigBlueprintGeneratedClass* Result = Cast<UControlRigBlueprintGeneratedClass>(*SkeletonGeneratedClass);
	return Result;
}

UClass* UControlRigBlueprint::GetBlueprintClass() const
{
	return UControlRigBlueprintGeneratedClass::StaticClass();
}

void UControlRigBlueprint::LoadModulesRequiredForCompilation() 
{
}

USkeletalMesh* UControlRigBlueprint::GetPreviewMesh() const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (!PreviewSkeletalMesh.IsValid())
	{
		PreviewSkeletalMesh.LoadSynchronous();
	}

	return PreviewSkeletalMesh.Get();
}

void UControlRigBlueprint::SetPreviewMesh(USkeletalMesh* PreviewMesh, bool bMarkAsDirty/*=true*/)
{
	if(bMarkAsDirty)
	{
		Modify();
	}

	PreviewSkeletalMesh = PreviewMesh;
}

void UControlRigBlueprint::PostLoad()
{
	Super::PostLoad();

	// remove all non-controlrig-graphs
	TArray<UEdGraph*> NewUberGraphPages;
	for (UEdGraph* Graph : UbergraphPages)
	{
		UControlRigGraph* RigGraph = Cast<UControlRigGraph>(Graph);
		if (RigGraph)
		{
			NewUberGraphPages.Add(RigGraph);
		}
		else
		{
			Graph->MarkPendingKill();
			Graph->Rename(nullptr, GetTransientPackage(), REN_ForceNoResetLoaders);
		}
	}
	UbergraphPages = NewUberGraphPages;

	InitializeModelIfRequired();

#if WITH_EDITOR

	Controller->DetachLinksFromPinObjects();
	for (URigVMNode* Node : Model->GetNodes())
	{
		Controller->RepopulatePinsOnNode(Node);
	}
	Controller->ReattachLinksToPinObjects();

	RecompileVM();
	RequestControlRigInit();

#endif
}

void UControlRigBlueprint::RecompileVM()
{
	UControlRigBlueprintGeneratedClass* RigClass = GetControlRigBlueprintGeneratedClass();
	UControlRig* CDO = Cast<UControlRig>(RigClass->GetDefaultObject(true /* create if needed */));
	if (CDO->VM != nullptr)
	{
		if (CDO->VM->GetOuter() != CDO)
		{
			CDO->VM = NewObject<URigVM>(CDO, TEXT("VM"));
		}

		CDO->Modify();
		CDO->VM->Empty();

		FRigUnitContext Context;
		Context.State = EControlRigState::Init;
		Context.Hierarchy = &CDO->Hierarchy;
		void* ContextPtr = &Context;
		FRigVMUserDataArray UserData = FRigVMUserDataArray(&ContextPtr, 1);

		URigVMCompiler* Compiler = URigVMCompiler::StaticClass()->GetDefaultObject<URigVMCompiler>();
		Compiler->Settings = VMCompileSettings;
		Compiler->Compile(Model, CDO->VM, UserData, &PinToOperandMap);

		CDO->Execute(EControlRigState::Init); // need to clarify if we actually need this
		Statistics = CDO->VM->GetStatistics();

		TArray<UObject*> ArchetypeInstances;
		CDO->GetArchetypeInstances(ArchetypeInstances);
		for (UObject* Instance : ArchetypeInstances)
		{
			if (UControlRig* InstanceRig = Cast<UControlRig>(Instance))
			{
				InstanceRig->Hierarchy.Initialize(false);
				InstanceRig->InstantiateVMFromCDO();
			}
		}

		bVMRecompilationRequired = false;
		VMRecompilationBracket = 0;
		VMCompiledEvent.Broadcast(this, CDO->VM);
	}
}

void UControlRigBlueprint::RecompileVMIfRequired()
{
	if (bVMRecompilationRequired)
	{
		RecompileVM();
	}
}

void UControlRigBlueprint::RequestAutoVMRecompilation()
{
	bVMRecompilationRequired = true;
	if (bAutoRecompileVM && VMRecompilationBracket == 0)
	{
		VMCompileSettings.ASTSettings = FRigVMParserASTSettings::Fast();
		RecompileVMIfRequired();
	}
}

void UControlRigBlueprint::IncrementVMRecompileBracket()
{
	VMRecompilationBracket++;
}

void UControlRigBlueprint::DecrementVMRecompileBracket()
{
	if (VMRecompilationBracket == 1)
	{
		if (bAutoRecompileVM)
		{
			if (bVMRecompilationRequired)
			{
				VMCompileSettings.ASTSettings = FRigVMParserASTSettings::Fast();
			}
			RecompileVMIfRequired();
		}
		VMRecompilationBracket = 0;
	}
	else if (VMRecompilationBracket > 0)
	{
		VMRecompilationBracket--;
	}
}

void UControlRigBlueprint::RequestControlRigInit()
{
	UControlRigBlueprintGeneratedClass* RigClass = GetControlRigBlueprintGeneratedClass();
	UControlRig* CDO = Cast<UControlRig>(RigClass->GetDefaultObject(true /* create if needed */));
	CDO->RequestInit();

	TArray<UObject*> ArchetypeInstances;
	CDO->GetArchetypeInstances(ArchetypeInstances);
	for (UObject* Instance : ArchetypeInstances)
	{
		if (UControlRig* InstanceRig = Cast<UControlRig>(Instance))
		{
			InstanceRig->RequestInit();
		}
	}
}

void UControlRigBlueprint::GetTypeActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	IControlRigEditorModule::Get().GetTypeActions(this, ActionRegistrar);
}

void UControlRigBlueprint::SetObjectBeingDebugged(UObject* NewObject)
{
	UControlRig* PreviousRigBeingDebugged = Cast<UControlRig>(GetObjectBeingDebugged());
	if (PreviousRigBeingDebugged && PreviousRigBeingDebugged != NewObject)
	{
		PreviousRigBeingDebugged->DrawInterface = nullptr;
		PreviousRigBeingDebugged->ControlRigLog = nullptr;
	}

	Super::SetObjectBeingDebugged(NewObject);
}

void UControlRigBlueprint::PostTransacted(const FTransactionObjectEvent& TransactionEvent)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()
	Super::PostTransacted(TransactionEvent);

	if (TransactionEvent.GetEventType() == ETransactionObjectEventType::UndoRedo)
	{
		if (TransactionEvent.GetChangedProperties().Contains(TEXT("HierarchyContainer")))
		{
			int32 TransactionIndex = GEditor->Trans->FindTransactionIndex(TransactionEvent.GetTransactionId());
			const FTransaction* Transaction = GEditor->Trans->GetTransaction(TransactionIndex);

			if (Transaction->GenerateDiff().TransactionTitle == TEXT("Transform Gizmo"))
			{
				PropagatePoseFromBPToInstances();
				return;
			}

			PropagateHierarchyFromBPToInstances(true, true);
			HierarchyContainer.OnElementChanged.Broadcast(&HierarchyContainer, FRigElementKey());

			// make sure the bone name list is up 2 date for the editor graph
	for (UEdGraph* Graph : UbergraphPages)
	{
		UControlRigGraph* RigGraph = Cast<UControlRigGraph>(Graph);
				if (RigGraph == nullptr)
				{
					continue;
				}
				RigGraph->CacheNameLists(&HierarchyContainer, &DrawContainer);
			}

			RequestAutoVMRecompilation();
			MarkPackageDirty();
		}

		else if (TransactionEvent.GetChangedProperties().Contains(TEXT("DrawContainer")))
		{
			PropagateDrawInstructionsFromBPToInstances();
		}
	}
}

FRigVMGraphModifiedEvent& UControlRigBlueprint::OnModified()
{
	return ModifiedEvent;
}


FOnVMCompiledEvent& UControlRigBlueprint::OnVMCompiled()
{
	return VMCompiledEvent;
}

TArray<UControlRigBlueprint*> UControlRigBlueprint::GetCurrentlyOpenRigBlueprints()
{
	return sCurrentlyOpenedRigBlueprints;
}

TArray<UStruct*> UControlRigBlueprint::GetAvailableRigUnits()
{
	const TArray<FRigVMFunction>& Functions = FRigVMRegistry::Get().GetFunctions();

	TArray<UStruct*> Structs;
	UStruct* BaseStruct = FRigUnit::StaticStruct();

	for (const FRigVMFunction& Function : Functions)
	{
		if (Function.Struct)
		{
			if (Function.Struct->IsChildOf(BaseStruct))
			{
				Structs.Add(Function.Struct);
			}
		}
	}

	return Structs;
}

UControlRigHierarchyModifier* UControlRigBlueprint::GetHierarchyModifier()
{
	if (HierarchyModifier == nullptr)
	{
		HierarchyModifier = NewObject<UControlRigHierarchyModifier>(this, TEXT("HierarchyModifier"));
		HierarchyModifier->Container = &HierarchyContainer;
	}
	return HierarchyModifier;
}

#if WITH_EDITOR

FName UControlRigBlueprint::AddTransientControl(URigVMPin* InPin)
{
	// for now we only allow one pin control at the same time
	ClearTransientControls();

	UControlRigBlueprintGeneratedClass* RigClass = GetControlRigBlueprintGeneratedClass();
	UControlRig* CDO = Cast<UControlRig>(RigClass->GetDefaultObject(true /* create if needed */));

	FName SpaceName = NAME_None;
	if (URigVMStructNode* StructNode = Cast<URigVMStructNode>(InPin->GetPinForLink()->GetNode()))
	{
		if (TSharedPtr<FStructOnScope> DefaultStructScope = StructNode->ConstructStructInstance())
		{
			FRigVMStruct* DefaultStruct = (FRigVMStruct*)DefaultStructScope->GetStructMemory();

			FString PinPath = InPin->GetPinForLink()->GetPinPath();
			FString Left, Right;

			if (URigVMPin::SplitPinPathAtStart(PinPath, Left, Right))
		{
				SpaceName = DefaultStruct->DetermineSpaceForPin(Right, &HierarchyContainer);
			}
		}
		}

	FName ReturnName = NAME_None;
	TArray<UObject*> ArchetypeInstances;
	CDO->GetArchetypeInstances(ArchetypeInstances);
	for (UObject* ArchetypeInstance : ArchetypeInstances)
	{
		UControlRig* InstancedControlRig = Cast<UControlRig>(ArchetypeInstance);
		if (InstancedControlRig)
		{
			FName ControlName = InstancedControlRig->AddTransientControl(InPin, SpaceName);
			if (ReturnName == NAME_None)
		{
				ReturnName = ControlName;
			}
		}
	}

	if (ReturnName != NAME_None)
	{
		HierarchyContainer.OnElementAdded.Broadcast(&HierarchyContainer, FRigElementKey(ReturnName, ERigElementType::Control));
		HierarchyContainer.OnElementSelected.Broadcast(&HierarchyContainer, FRigElementKey(ReturnName, ERigElementType::Control), true);
	}

	return ReturnName;
}

FName UControlRigBlueprint::RemoveTransientControl(URigVMPin* InPin)
{
	UControlRigBlueprintGeneratedClass* RigClass = GetControlRigBlueprintGeneratedClass();
	UControlRig* CDO = Cast<UControlRig>(RigClass->GetDefaultObject(true /* create if needed */));

	FName RemovedName = NAME_None;
	TArray<UObject*> ArchetypeInstances;
	CDO->GetArchetypeInstances(ArchetypeInstances);
	for (UObject* ArchetypeInstance : ArchetypeInstances)
	{
		UControlRig* InstancedControlRig = Cast<UControlRig>(ArchetypeInstance);
		if (InstancedControlRig)
		{
			FName Name = InstancedControlRig->RemoveTransientControl(InPin);
			if (RemovedName == NAME_None)
	{
				RemovedName = Name;
			}
		}
	}

	if (RemovedName != NAME_None)
		{
		HierarchyContainer.OnElementSelected.Broadcast(&HierarchyContainer, FRigElementKey(RemovedName, ERigElementType::Control), false);
		HierarchyContainer.OnElementRemoved.Broadcast(&HierarchyContainer, FRigElementKey(RemovedName, ERigElementType::Control));
	}
	return RemovedName;
}

FName UControlRigBlueprint::AddTransientControl(const FRigElementKey& InElement)
{
	// for now we only allow one pin control at the same time
	ClearTransientControls();

	UControlRigBlueprintGeneratedClass* RigClass = GetControlRigBlueprintGeneratedClass();
	UControlRig* CDO = Cast<UControlRig>(RigClass->GetDefaultObject(true /* create if needed */));

	FName ReturnName = NAME_None;
	TArray<UObject*> ArchetypeInstances;
	CDO->GetArchetypeInstances(ArchetypeInstances);
	for (UObject* ArchetypeInstance : ArchetypeInstances)
	{
		UControlRig* InstancedControlRig = Cast<UControlRig>(ArchetypeInstance);
		if (InstancedControlRig)
			{
			FName ControlName = InstancedControlRig->AddTransientControl(InElement);
			if (ReturnName == NAME_None)
				{
				ReturnName = ControlName;
				}
			}
		}

	if (ReturnName != NAME_None)
	{
		HierarchyContainer.OnElementAdded.Broadcast(&HierarchyContainer, FRigElementKey(ReturnName, ERigElementType::Control));
		HierarchyContainer.OnElementSelected.Broadcast(&HierarchyContainer, FRigElementKey(ReturnName, ERigElementType::Control), true);
	}

	return ReturnName;

}

FName UControlRigBlueprint::RemoveTransientControl(const FRigElementKey& InElement)
{
	UControlRigBlueprintGeneratedClass* RigClass = GetControlRigBlueprintGeneratedClass();
	UControlRig* CDO = Cast<UControlRig>(RigClass->GetDefaultObject(true /* create if needed */));

	FName RemovedName = NAME_None;
	TArray<UObject*> ArchetypeInstances;
	CDO->GetArchetypeInstances(ArchetypeInstances);
	for (UObject* ArchetypeInstance : ArchetypeInstances)
	{
		UControlRig* InstancedControlRig = Cast<UControlRig>(ArchetypeInstance);
		if (InstancedControlRig)
		{
			FName Name = InstancedControlRig->RemoveTransientControl(InElement);
			if (RemovedName == NAME_None)
			{
				RemovedName = Name;
			}
		}
	}

	if (RemovedName != NAME_None)
	{
		HierarchyContainer.OnElementSelected.Broadcast(&HierarchyContainer, FRigElementKey(RemovedName, ERigElementType::Control), false);
		HierarchyContainer.OnElementRemoved.Broadcast(&HierarchyContainer, FRigElementKey(RemovedName, ERigElementType::Control));
	}
	return RemovedName;
}

void UControlRigBlueprint::ClearTransientControls()
{
	UControlRigBlueprintGeneratedClass* RigClass = GetControlRigBlueprintGeneratedClass();
	UControlRig* CDO = Cast<UControlRig>(RigClass->GetDefaultObject(true /* create if needed */));

	TArray<FRigControl> PreviousControls;
	TArray<UObject*> ArchetypeInstances;
	CDO->GetArchetypeInstances(ArchetypeInstances);
	for (UObject* ArchetypeInstance : ArchetypeInstances)
	{
		UControlRig* InstancedControlRig = Cast<UControlRig>(ArchetypeInstance);
		if (InstancedControlRig)
		{
			if (PreviousControls.Num() == 0)
			{
				PreviousControls = InstancedControlRig->TransientControls;
			}
			InstancedControlRig->ClearTransientControls();
		}
	}

	for(const FRigControl& RemovedControl : PreviousControls)
	{
		HierarchyContainer.OnElementSelected.Broadcast(&HierarchyContainer, FRigElementKey(RemovedControl.Name, ERigElementType::Control), false);
		HierarchyContainer.OnElementRemoved.Broadcast(&HierarchyContainer, FRigElementKey(RemovedControl.Name, ERigElementType::Control));
	}
}

#endif

void UControlRigBlueprint::PopulateModelFromGraphForBackwardsCompatibility(UControlRigGraph* InGraph)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	int32 LinkerVersion = GetLinkerCustomVersion(FControlRigObjectVersion::GUID);
	if (LinkerVersion >= FControlRigObjectVersion::SwitchedToRigVM)
	{
		return;
	}

	if (LinkerVersion < FControlRigObjectVersion::RemovalOfHierarchyRefPins)
	{
		UE_LOG(LogControlRigDeveloper, Warning, TEXT("Control Rig is too old (prior 4.23) - cannot automatically upgrade. Clearing graph."));
		RebuildGraphFromModel();
		return;
	}

	TGuardValue<bool> ReentrantGuardSelf(bSuspendModelNotificationsForSelf, true);
	{
		TGuardValue<bool> ReentrantGuardOthers(bSuspendModelNotificationsForOthers, true);

		struct LocalHelpers
		{
			static FString FixUpPinPath(const FString& InPinPath)
			{
				FString PinPath = InPinPath;
				if (!PinPath.Contains(TEXT(".")))
				{
					PinPath += TEXT(".Value");
				}

				PinPath = PinPath.Replace(TEXT("["), TEXT("."), ESearchCase::IgnoreCase);
				PinPath = PinPath.Replace(TEXT("]"), TEXT(""), ESearchCase::IgnoreCase);

				return PinPath;
			}
		};

		for (UEdGraphNode* Node : InGraph->Nodes)
		{
			if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(Node))
			{
				FName PropertyName = RigNode->PropertyName_DEPRECATED;
				FVector2D NodePosition = FVector2D((float)RigNode->NodePosX, (float)RigNode->NodePosY);
				FString StructPath = RigNode->StructPath_DEPRECATED;

				if (StructPath.IsEmpty() && PropertyName != NAME_None)
				{
					if(FStructProperty* StructProperty = CastField<FStructProperty>(GetControlRigBlueprintGeneratedClass()->FindPropertyByName(PropertyName)))
					{
						StructPath = StructProperty->Struct->GetPathName();
					}
				}

				URigVMNode* ModelNode = nullptr;

				UScriptStruct* UnitStruct = URigVMPin::FindObjectFromCPPTypeObjectPath<UScriptStruct>(StructPath);
				if (UnitStruct && UnitStruct->IsChildOf(FRigVMStruct::StaticStruct()))
				{ 
					ModelNode = Controller->AddStructNode(UnitStruct, TEXT("Execute"), NodePosition, PropertyName.ToString(), false);
				}
				else if (PropertyName != NAME_None) // check if this is a variable
				{
					bool bHasInputLinks = false;
					bool bHasOutputLinks = false;
					FString DefaultValue;

					FEdGraphPinType PinType = RigNode->PinType_DEPRECATED;
					if (RigNode->Pins.Num() > 0)
				{
						for (UEdGraphPin* Pin : RigNode->Pins)
						{
							if (!Pin->GetName().Contains(TEXT(".")))
							{
								PinType = Pin->PinType;

								if (Pin->Direction == EGPD_Input)
								{
									bHasInputLinks = Pin->LinkedTo.Num() > 0;
									DefaultValue = Pin->DefaultValue;
								}
								else if (Pin->Direction == EGPD_Output)
								{
									bHasOutputLinks = Pin->LinkedTo.Num() > 0;
								}
							}
						}
					}

					FName DataType = PinType.PinCategory;
					UObject* DataTypeObject = nullptr;
					if (DataType == NAME_None)
					{
						continue;
					}
					if (DataType == UEdGraphSchema_K2::PC_Struct)
					{
						DataType = NAME_None;
						if (UScriptStruct* DataStruct = Cast<UScriptStruct>(PinType.PinSubCategoryObject))
						{
							DataTypeObject = DataStruct;
							DataType = *DataStruct->GetStructCPPName();
						}
					}

					if (DataType == TEXT("int"))
					{
						DataType = TEXT("int32");
					}
					else if (DataType == TEXT("name"))
						{
						DataType = TEXT("FName");
						}
					else if (DataType == TEXT("string"))
					{
						DataType = TEXT("FString");
					}

					FProperty* ParameterProperty = GetControlRigBlueprintGeneratedClass()->FindPropertyByName(PropertyName);
					if(ParameterProperty)
					{
						bool bIsInput = true;

						if (ParameterProperty->HasMetaData(TEXT("AnimationInput")) || bHasOutputLinks)
					{
							bIsInput = true;
						}
						else if (ParameterProperty->HasMetaData(TEXT("AnimationOutput")))
						{
							bIsInput = false;
						}

						ModelNode = Controller->AddParameterNode(PropertyName, DataType.ToString(), DataTypeObject, bIsInput, FString(), NodePosition, PropertyName.ToString(), false);
					}
				}
				else
				{
					continue;
				}

				if (ModelNode)
				{
				for (UEdGraphPin* Pin : RigNode->Pins)
				{
						FString PinPath = LocalHelpers::FixUpPinPath(Pin->GetName());

						// check the material + mesh pins for deprecated control nodes
						if (URigVMStructNode* ModelStructNode = Cast<URigVMStructNode>(ModelNode))
						{
							if (ModelStructNode->GetScriptStruct()->IsChildOf(FRigUnit_Control::StaticStruct()))
							{
								if (Pin->GetName().EndsWith(TEXT(".StaticMesh")) || Pin->GetName().EndsWith(TEXT(".Materials")))
								{
									continue;
								}
							}
						}

					if (Pin->Direction == EGPD_Input && Pin->PinType.ContainerType == EPinContainerType::Array)
					{
						int32 ArraySize = Pin->SubPins.Num();
							Controller->SetArrayPinSize(PinPath, ArraySize, FString(), false);
					}

						if (RigNode->ExpandedPins_DEPRECATED.Find(Pin->GetName()) != INDEX_NONE)
					{
							Controller->SetPinExpansion(PinPath, true, false);
					}

						if (Pin->SubPins.Num() == 0 && !Pin->DefaultValue.IsEmpty() && Pin->Direction == EGPD_Input)
					{
							Controller->SetPinDefaultValue(PinPath, Pin->DefaultValue, false, false, false);
					}
				}
			}

				const int32 VarIndex = FBlueprintEditorUtils::FindNewVariableIndex(this, PropertyName);
				if (VarIndex != INDEX_NONE)
				{
					NewVariables.RemoveAt(VarIndex);
					FBlueprintEditorUtils::RemoveVariableNodes(this, PropertyName);
				}
			}
			else if (const UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Node))
			{
				FVector2D NodePosition = FVector2D((float)CommentNode->NodePosX, (float)CommentNode->NodePosY);
				FVector2D NodeSize = FVector2D((float)CommentNode->NodeWidth, (float)CommentNode->NodeHeight);
				Controller->AddCommentNode(CommentNode->NodeComment, NodePosition, NodeSize, CommentNode->CommentColor, CommentNode->GetName(), false);
			}
		}

		for (UEdGraphNode* Node : InGraph->Nodes)
		{
			if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(Node))
			{
				for (UEdGraphPin* Pin : RigNode->Pins)
				{
					if (Pin->Direction == EGPD_Input)
					{
						continue;
					}

					for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
					{
						UControlRigGraphNode* LinkedRigNode = Cast<UControlRigGraphNode>(LinkedPin->GetOwningNode());
						if (LinkedRigNode != nullptr)
						{
							FString SourcePinPath = LocalHelpers::FixUpPinPath(Pin->GetName());
							FString TargetPinPath = LocalHelpers::FixUpPinPath(LinkedPin->GetName());
							Controller->AddLink(SourcePinPath, TargetPinPath, false);
						}
					}
				}
			}
		}
	}

	RebuildGraphFromModel();
}

void UControlRigBlueprint::RebuildGraphFromModel()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	TGuardValue<bool> SelfGuard(bSuspendModelNotificationsForSelf, true);
	check(Controller);

	for (UEdGraph* Graph : UbergraphPages)
	{
		TArray<UEdGraphNode*> Nodes = Graph->Nodes;
		for (UEdGraphNode* Node : Nodes)
		{
			Graph->RemoveNode(Node);
		}
	}

	Controller->ResendAllNotifications();
}
void UControlRigBlueprint::Notify(ERigVMGraphNotifType InNotifType, UObject* InSubject)
{
	Controller->Notify(InNotifType, InSubject);
}

void UControlRigBlueprint::HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

#if WITH_EDITOR

	if (!bSuspendModelNotificationsForSelf)
	{
		switch (InNotifType)
		{
			case ERigVMGraphNotifType::InteractionBracketOpened:
			{
				IncrementVMRecompileBracket();
				break;
			}
			case ERigVMGraphNotifType::InteractionBracketClosed:
			case ERigVMGraphNotifType::InteractionBracketCanceled:
			{
				DecrementVMRecompileBracket();
				break;
			}
			case ERigVMGraphNotifType::PinDefaultValueChanged:
			{
				if (URigVMPin* Pin = Cast<URigVMPin>(InSubject))
				{
					bool bRequiresRecompile = false;

					URigVMPin* RootPin = Pin->GetRootPin();
					if (const FRigVMOperand* Operand = PinToOperandMap.Find(RootPin->GetPinPath()))
					{
						if(const FRigVMExprAST* Expression = InGraph->GetRuntimeAST()->GetExprForSubject(RootPin))
						{
							bRequiresRecompile = Expression->NumParents() > 1;
						}
						else
						{
							bRequiresRecompile = true;
						}

						if(!bRequiresRecompile)
						{
							TArray<FString> DefaultValues;
							if (RootPin->IsArray())
							{
								for (URigVMPin* ArrayElementPin : RootPin->GetSubPins())
								{
									DefaultValues.Add(ArrayElementPin->GetDefaultValue());
								}
							}
							else
							{
								DefaultValues.Add(RootPin->GetDefaultValue());
							}

							UControlRigBlueprintGeneratedClass* RigClass = GetControlRigBlueprintGeneratedClass();
							UControlRig* CDO = Cast<UControlRig>(RigClass->GetDefaultObject(true /* create if needed */));
							if (CDO->VM != nullptr)
							{
								CDO->VM->SetRegisterValueFromString(*Operand, RootPin->GetCPPType(), RootPin->GetCPPTypeObject(), DefaultValues);
							}

							TArray<UObject*> ArchetypeInstances;
							CDO->GetArchetypeInstances(ArchetypeInstances);
							for (UObject* ArchetypeInstance : ArchetypeInstances)
							{
								UControlRig* InstancedControlRig = Cast<UControlRig>(ArchetypeInstance);
								if (InstancedControlRig)
								{
									if (InstancedControlRig->VM)
									{
										InstancedControlRig->VM->SetRegisterValueFromString(*Operand, RootPin->GetCPPType(), RootPin->GetCPPTypeObject(), DefaultValues);
									}
								}
							}

							if (Pin->IsConstant() || Pin->GetRootPin()->IsConstant())
							{
								// re-init the rigs
								RequestControlRigInit();
							}
						}
					}
					else
					{
						bRequiresRecompile = true;
					}
				
					if(bRequiresRecompile)
					{
						RequestAutoVMRecompilation();
					}

					// check if this pin is part of an injected node, and if it is a visual debug node,
					// we might need to recreate the control pin
					if (UClass* MyControlRigClass = GeneratedClass)
					{
						if (UControlRig* DefaultObject = Cast<UControlRig>(MyControlRigClass->GetDefaultObject(false)))
						{
							TArray<UObject*> ArchetypeInstances;
							DefaultObject->GetArchetypeInstances(ArchetypeInstances);
							for (UObject* ArchetypeInstance : ArchetypeInstances)
							{
								if (UControlRig* InstanceRig = Cast<UControlRig>(ArchetypeInstance))
								{
									for (const FRigControl& Control : InstanceRig->TransientControls)
									{
										if (URigVMPin* ControlledPin = Model->FindPin(Control.Name.ToString()))
										{
											URigVMPin* ControlledPinForLink = ControlledPin->GetPinForLink();

											if(ControlledPin->GetRootPin() == Pin->GetRootPin() ||
											   ControlledPinForLink->GetRootPin() == Pin->GetRootPin())
											{
												InstanceRig->SetTransientControlValue(ControlledPin->GetPinForLink());
											}
											else if (ControlledPin->GetNode() == Pin->GetNode() ||
													 ControlledPinForLink->GetNode() == Pin->GetNode())
											{
												InstanceRig->ClearTransientControls();
												InstanceRig->AddTransientControl(ControlledPin);
											}
											break;
										}
									}
								}
							}
						}
					}
				}
				MarkPackageDirty();
				break;
			}
			case ERigVMGraphNotifType::NodeAdded:
			case ERigVMGraphNotifType::NodeRemoved:
			case ERigVMGraphNotifType::LinkAdded:
			case ERigVMGraphNotifType::LinkRemoved:
			case ERigVMGraphNotifType::PinArraySizeChanged:
			case ERigVMGraphNotifType::PinDirectionChanged:
			{
				ClearTransientControls();
				RequestAutoVMRecompilation();
				MarkPackageDirty();

				// This is not necessarily required but due to workflow
				// expectations we still mark the blueprint as dirty.
				FBlueprintEditorUtils::MarkBlueprintAsModified(this);
				break;
			}
			case ERigVMGraphNotifType::PinWatchedChanged:
			case ERigVMGraphNotifType::PinTypeChanged:
			{
				if (URigVMPin* ModelPin = Cast<URigVMPin>(InSubject))
				{
					for (int32 i = 0; i < UbergraphPages.Num(); ++i)
					{
						if (UControlRigGraph* Graph = Cast<UControlRigGraph>(UbergraphPages[i]))
						{
							if (UEdGraphNode* EdNode = Graph->FindNodeForModelNodeName(ModelPin->GetNode()->GetFName()))
							{
								if (UEdGraphPin* EdPin = EdNode->FindPin(*ModelPin->GetPinPath()))
								{
									if (ModelPin->RequiresWatch())
									{
										WatchedPins.AddUnique(EdPin);
									}
									else
									{
										WatchedPins.Remove(EdPin);
									}
									RequestAutoVMRecompilation();
										MarkPackageDirty();
									}
								}
						}
					}
				}
				break;
			}
			case ERigVMGraphNotifType::ParameterAdded:
			case ERigVMGraphNotifType::ParameterRemoved:
			case ERigVMGraphNotifType::ParameterRenamed:
			{
				RequestAutoVMRecompilation();
				MarkPackageDirty();
				break;
			}
			default:
			{
				break;
			}
		}
	}

	if (!bSuspendModelNotificationsForOthers)
	{
		if (ModifiedEvent.IsBound())
		{
			ModifiedEvent.Broadcast(InNotifType, InGraph, InSubject);
		}
	}
#endif
}

void UControlRigBlueprint::CleanupBoneHierarchyDeprecated()
{
	if (Hierarchy_DEPRECATED.Num() > 0)
	{
		HierarchyContainer.BoneHierarchy = Hierarchy_DEPRECATED;
		Hierarchy_DEPRECATED.Reset();
	}

	if (CurveContainer_DEPRECATED.Num() > 0)
	{
		HierarchyContainer.CurveContainer = CurveContainer_DEPRECATED;
		CurveContainer_DEPRECATED.Reset();
	}

}

void UControlRigBlueprint::PropagatePoseFromInstanceToBP(UControlRig* InControlRig)
{
	check(InControlRig);

	for (const FRigBone& InputBone : InControlRig->Hierarchy.BoneHierarchy)
	{
		FRigBone& OutputBone = HierarchyContainer.BoneHierarchy[InputBone.Name];
		OutputBone.InitialTransform = InputBone.InitialTransform;
		OutputBone.LocalTransform = InputBone.LocalTransform;
		OutputBone.GlobalTransform = InputBone.GlobalTransform;
	}

	for (const FRigSpace& InputSpace: InControlRig->Hierarchy.SpaceHierarchy)
	{
		FRigSpace& OutputSpace = HierarchyContainer.SpaceHierarchy[InputSpace.Name];
		OutputSpace.InitialTransform = InputSpace.InitialTransform;
		OutputSpace.LocalTransform = InputSpace.LocalTransform;
	}

	for (const FRigControl& InputControl : InControlRig->Hierarchy.ControlHierarchy)
		{
		FRigControl& OutputControl = HierarchyContainer.ControlHierarchy[InputControl.Name];
		OutputControl.InitialValue = InputControl.InitialValue;
		OutputControl.Value = InputControl.Value;
		}
}

void UControlRigBlueprint::PropagatePoseFromBPToInstances()
{
	if (UClass* MyControlRigClass = GeneratedClass)
	{
		if (UControlRig* DefaultObject = Cast<UControlRig>(MyControlRigClass->GetDefaultObject(false)))
		{
			TArray<UObject*> ArchetypeInstances;
			DefaultObject->GetArchetypeInstances(ArchetypeInstances);
			for (UObject* ArchetypeInstance : ArchetypeInstances)
			{
				if (UControlRig* InstanceRig = Cast<UControlRig>(ArchetypeInstance))
		{
					for (const FRigBone& InputBone : HierarchyContainer.BoneHierarchy)
					{
						FRigBone& OutputBone = InstanceRig->Hierarchy.BoneHierarchy[InputBone.Name];
						OutputBone.InitialTransform = InputBone.InitialTransform;
						OutputBone.LocalTransform = InputBone.LocalTransform;
						OutputBone.GlobalTransform = InputBone.GlobalTransform;
		}

					for (const FRigSpace& InputSpace : HierarchyContainer.SpaceHierarchy)
					{
						FRigSpace& OutputSpace = InstanceRig->Hierarchy.SpaceHierarchy[InputSpace.Name];
						OutputSpace.InitialTransform = InputSpace.InitialTransform;
						OutputSpace.LocalTransform = InputSpace.LocalTransform;
					}

					for (const FRigControl& InputControl : HierarchyContainer.ControlHierarchy)
		{
						FRigControl& OutputControl = InstanceRig->Hierarchy.ControlHierarchy[InputControl.Name];
						OutputControl.InitialValue = InputControl.InitialValue;
						OutputControl.Value = InputControl.Value;
		}
		}
	}
		}
	}
}

void UControlRigBlueprint::PropagateHierarchyFromBPToInstances(bool bInitializeContainer, bool bInitializeRigs)
{
	if (UClass* MyControlRigClass = GeneratedClass)
	{
		if (UControlRig* DefaultObject = Cast<UControlRig>(MyControlRigClass->GetDefaultObject(false)))
		{
			if (bInitializeContainer)
			{
				HierarchyContainer.Initialize();
				HierarchyContainer.ResetTransforms();
			}

			DefaultObject->Hierarchy = HierarchyContainer;
			if (bInitializeRigs)
			{
				DefaultObject->Initialize(true);
			}
			else
				{
				DefaultObject->Hierarchy.Initialize(false);
			}

						TArray<UObject*> ArchetypeInstances;
						DefaultObject->GetArchetypeInstances(ArchetypeInstances);
						for (UObject* ArchetypeInstance : ArchetypeInstances)
						{
				if (UControlRig* InstanceRig = Cast<UControlRig>(ArchetypeInstance))
				{
					InstanceRig->Hierarchy = HierarchyContainer;
					if (bInitializeRigs)
					{
						InstanceRig->Initialize(true);
						}
					else
					{
						InstanceRig->Hierarchy.Initialize(false);
					}
				}
			}
		}
	}
}

void UControlRigBlueprint::PropagateDrawInstructionsFromBPToInstances()
{
	if (UClass* MyControlRigClass = GeneratedClass)
	{
		if (UControlRig* DefaultObject = Cast<UControlRig>(MyControlRigClass->GetDefaultObject(false)))
	{
			DefaultObject->DrawContainer = DrawContainer;

			TArray<UObject*> ArchetypeInstances;
			DefaultObject->GetArchetypeInstances(ArchetypeInstances);

			for (UObject* ArchetypeInstance : ArchetypeInstances)
			{
				if (UControlRig* InstanceRig = Cast<UControlRig>(ArchetypeInstance))
	{
					InstanceRig->DrawContainer = DrawContainer;
				}
			}
		}
	}


	// make sure the bone name list is up 2 date for the editor graph
	for (UEdGraph* Graph : UbergraphPages)
	{
		UControlRigGraph* RigGraph = Cast<UControlRigGraph>(Graph);
		if (RigGraph == nullptr)
		{
			continue;
		}
		RigGraph->CacheNameLists(&HierarchyContainer, &DrawContainer);
	}
}

void UControlRigBlueprint::PropagatePropertyFromBPToInstances(FRigElementKey InRigElement, const FProperty* InProperty)
{
	int32 ElementIndex = HierarchyContainer.GetIndex(InRigElement);
	ensure(ElementIndex != INDEX_NONE);
	check(InProperty);

	if (UClass* MyControlRigClass = GeneratedClass)
	{
		if (UControlRig* DefaultObject = Cast<UControlRig>(MyControlRigClass->GetDefaultObject(false)))
		{
			TArray<UObject*> ArchetypeInstances;
			DefaultObject->GetArchetypeInstances(ArchetypeInstances);

			int32 PropertyOffset = InProperty->GetOffset_ReplaceWith_ContainerPtrToValuePtr();
			int32 PropertySize = InProperty->GetSize();

			switch (InRigElement.Type)
			{
				case ERigElementType::Bone:
				{
					uint8* Source = ((uint8*)&HierarchyContainer.BoneHierarchy[ElementIndex]) + PropertyOffset;
					for (UObject* ArchetypeInstance : ArchetypeInstances)
					{
						if (UControlRig* InstanceRig = Cast<UControlRig>(ArchetypeInstance))
						{
							uint8* Dest = ((uint8*)&InstanceRig->Hierarchy.BoneHierarchy[ElementIndex]) + PropertyOffset;
							FMemory::Memcpy(Dest, Source, PropertySize);
						}
					}
					break;
				}
				case ERigElementType::Space:
				{
					uint8* Source = ((uint8*)&HierarchyContainer.SpaceHierarchy[ElementIndex]) + PropertyOffset;
					for (UObject* ArchetypeInstance : ArchetypeInstances)
					{
						if (UControlRig* InstanceRig = Cast<UControlRig>(ArchetypeInstance))
						{
							uint8* Dest = ((uint8*)&InstanceRig->Hierarchy.SpaceHierarchy[ElementIndex]) + PropertyOffset;
							FMemory::Memcpy(Dest, Source, PropertySize);
						}
					}
					break;
				}
				case ERigElementType::Control:
				{
					uint8* Source = ((uint8*)&HierarchyContainer.ControlHierarchy[ElementIndex]) + PropertyOffset;
					for (UObject* ArchetypeInstance : ArchetypeInstances)
					{
						if (UControlRig* InstanceRig = Cast<UControlRig>(ArchetypeInstance))
						{
							uint8* Dest = ((uint8*)&InstanceRig->Hierarchy.ControlHierarchy[ElementIndex]) + PropertyOffset;
							FMemory::Memcpy(Dest, Source, PropertySize);
						}
					}
					break;
				}
				case ERigElementType::Curve:
				{
					uint8* Source = ((uint8*)&HierarchyContainer.CurveContainer[ElementIndex]) + PropertyOffset;
					for (UObject* ArchetypeInstance : ArchetypeInstances)
					{
						if (UControlRig* InstanceRig = Cast<UControlRig>(ArchetypeInstance))
						{
							uint8* Dest = ((uint8*)&InstanceRig->Hierarchy.CurveContainer[ElementIndex]) + PropertyOffset;
							FMemory::Memcpy(Dest, Source, PropertySize);
						}
					}
					break;
				}
				default:
			{
					break;
				}
			}
		}
			}
}

void UControlRigBlueprint::PropagatePropertyFromInstanceToBP(FRigElementKey InRigElement, const FProperty* InProperty, UControlRig* InInstance)
{
	int32 ElementIndex = HierarchyContainer.GetIndex(InRigElement);
	ensure(ElementIndex != INDEX_NONE);
	check(InProperty);

	int32 PropertyOffset = InProperty->GetOffset_ReplaceWith_ContainerPtrToValuePtr();
	int32 PropertySize = InProperty->GetSize();

	switch (InRigElement.Type)
	{
		case ERigElementType::Bone:
		{
			uint8* Source = ((uint8*)&InInstance->Hierarchy.BoneHierarchy[ElementIndex]) + PropertyOffset;
			uint8* Dest = ((uint8*)&HierarchyContainer.BoneHierarchy[ElementIndex]) + PropertyOffset;
			FMemory::Memcpy(Dest, Source, PropertySize);
			break;
		}
		case ERigElementType::Space:
			{
			uint8* Source = ((uint8*)&InInstance->Hierarchy.SpaceHierarchy[ElementIndex]) + PropertyOffset;
			uint8* Dest = ((uint8*)&HierarchyContainer.SpaceHierarchy[ElementIndex]) + PropertyOffset;
			FMemory::Memcpy(Dest, Source, PropertySize);
			break;
		}
		case ERigElementType::Control:
				{
			uint8* Source = ((uint8*)&InInstance->Hierarchy.ControlHierarchy[ElementIndex]) + PropertyOffset;
			uint8* Dest = ((uint8*)&HierarchyContainer.ControlHierarchy[ElementIndex]) + PropertyOffset;
			FMemory::Memcpy(Dest, Source, PropertySize);
			break;
				}
		case ERigElementType::Curve:
		{
			uint8* Source = ((uint8*)&InInstance->Hierarchy.CurveContainer[ElementIndex]) + PropertyOffset;
			uint8* Dest = ((uint8*)&HierarchyContainer.CurveContainer[ElementIndex]) + PropertyOffset;
			FMemory::Memcpy(Dest, Source, PropertySize);
			break;
			}
		default:
		{
			break;
		}
	}
}

#if WITH_EDITOR

void UControlRigBlueprint::HandleOnElementAdded(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey)
{
	PropagateHierarchyFromBPToInstances();
}

void UControlRigBlueprint::HandleOnElementRemoved(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey)
{
	PropagateHierarchyFromBPToInstances();
}

void UControlRigBlueprint::HandleOnElementRenamed(FRigHierarchyContainer* InContainer, ERigElementType InElementType, const FName& InOldName, const FName& InNewName)
{
	PropagateHierarchyFromBPToInstances();
}

void UControlRigBlueprint::HandleOnElementReparented(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey, const FName& InOldParentName, const FName& InNewParentName)
{
	PropagateHierarchyFromBPToInstances();
}

void UControlRigBlueprint::HandleOnElementSelected(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey, bool bSelected)
{
	if (InKey.Type == ERigElementType::Control)
	{
		if (UControlRig* RigBeingDebugged = Cast<UControlRig>(GetObjectBeingDebugged()))
		{
			if (FRigControl* Control = RigBeingDebugged->FindControl(InKey.Name))
			{
				if (!Control->bIsTransientControl)
				{
					ClearTransientControls();
				}
			}
		}
	}
}

#endif

#undef LOCTEXT_NAMESPACE

