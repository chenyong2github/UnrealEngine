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
#include "Units/Hierarchy/RigUnit_SetBoneTransform.h"
#include "Async/TaskGraphInterfaces.h"
#include "Misc/CoreDelegates.h"

#if WITH_EDITOR
#include "IControlRigEditorModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ControlRigBlueprintUtils.h"
#include "Settings/ControlRigSettings.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "CookOnTheSide/CookOnTheFlyServer.h"
#endif//WITH_EDITOR

#define LOCTEXT_NAMESPACE "ControlRigBlueprint"

TArray<UControlRigBlueprint*> UControlRigBlueprint::sCurrentlyOpenedRigBlueprints;

UControlRigBlueprint::UControlRigBlueprint(const FObjectInitializer& ObjectInitializer)
{
	bSuspendModelNotificationsForSelf = false;
	bSuspendModelNotificationsForOthers = false;
	bSuspendAllNotifications = false;

#if WITH_EDITORONLY_DATA
	GizmoLibrary = UControlRigSettings::Get()->DefaultGizmoLibrary;
#endif

	bRecompileOnLoad = 0;
	bAutoRecompileVM = true;
	bVMRecompilationRequired = false;
	VMRecompilationBracket = 0;

	Model = ObjectInitializer.CreateDefaultSubobject<URigVMGraph>(this, TEXT("RigVMModel"));

	Validator = ObjectInitializer.CreateDefaultSubobject<UControlRigValidator>(this, TEXT("ControlRigValidator"));

	bDirtyDuringLoad = false;

	SupportedEventNames.Reset();
	bExposesAnimatableControls = false;
}

void UControlRigBlueprint::InitializeModelIfRequired(bool bRecompileVM)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (Controllers.Num() == 0)
	{
		GetOrCreateController(Model);

		for (int32 i = 0; i < UbergraphPages.Num(); ++i)
		{
			if (UControlRigGraph* Graph = Cast<UControlRigGraph>(UbergraphPages[i]))
			{
				PopulateModelFromGraphForBackwardsCompatibility(Graph);

				if (bRecompileVM)
				{
					RecompileVM();
				}

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

void UControlRigBlueprint::PreSave(const class ITargetPlatform* TargetPlatform)
{
	Super::PreSave(TargetPlatform);

	SupportedEventNames.Reset();
	if (UControlRigBlueprintGeneratedClass* RigClass = GetControlRigBlueprintGeneratedClass())
	{
		if (UControlRig* CDO = Cast<UControlRig>(RigClass->GetDefaultObject(true /* create if needed */)))
		{
			SupportedEventNames = CDO->GetSupportedEvents();
		}
	}

	bExposesAnimatableControls = false;
	for (FRigControl& RigControl : HierarchyContainer.ControlHierarchy)
	{
		if (RigControl.bAnimatable)
		{
			bExposesAnimatableControls = true;
			break;
		}
	}
}

void UControlRigBlueprint::PostLoad()
{
	Super::PostLoad();

	HierarchyContainer.ControlHierarchy.PostLoad();

	// correct the offset transforms
	if (GetLinkerCustomVersion(FControlRigObjectVersion::GUID) < FControlRigObjectVersion::ControlOffsetTransform)
	{
		if (HierarchyContainer.ControlHierarchy.Num() > 0)
		{
			bDirtyDuringLoad = true;
		}

		for (FRigControl& Control : HierarchyContainer.ControlHierarchy)
		{
			FTransform PreviousOffsetTransform = HierarchyContainer.ControlHierarchy.GetLocalTransform(Control.Index, ERigControlValueType::Initial);
			Control.OffsetTransform = PreviousOffsetTransform;
			Control.InitialValue = Control.Value;

			if (Control.ControlType == ERigControlType::Transform)
			{
				Control.InitialValue = FRigControlValue::Make<FTransform>(FTransform::Identity);
			}
			else if (Control.ControlType == ERigControlType::TransformNoScale)
			{
				Control.InitialValue = FRigControlValue::Make<FTransformNoScale>(FTransformNoScale::Identity);
			}
			else if (Control.ControlType == ERigControlType::EulerTransform)
			{
				Control.InitialValue = FRigControlValue::Make<FEulerTransform>(FEulerTransform::Identity);
			}
		}
		PropagateHierarchyFromBPToInstances(true, true);
	}

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

	InitializeModelIfRequired(false /* recompile vm */);

	PatchVariableNodesOnLoad();

#if WITH_EDITOR

	if (!IsInAsyncLoadingThread() || IsRunningCommandlet())
	{
		GetOrCreateController()->DetachLinksFromPinObjects();
		TArray<URigVMNode*> Nodes = Model->GetNodes();
		for (URigVMNode* Node : Nodes)
		{
			GetOrCreateController()->RepopulatePinsOnNode(Node);
		}
		SetupPinRedirectorsForBackwardsCompatibility();
	}
	GetOrCreateController()->ReattachLinksToPinObjects(true /* follow redirectors */);

	RecompileVM();
	RequestControlRigInit();

	FCoreUObjectDelegates::OnObjectModified.RemoveAll(this);
	OnChanged().RemoveAll(this);
	FCoreUObjectDelegates::OnObjectModified.AddUObject(this, &UControlRigBlueprint::OnPreVariableChange);
	OnChanged().AddUObject(this, &UControlRigBlueprint::OnPostVariableChange);

#endif

	if (UPackage* Package = GetOutermost())
	{
		Package->SetDirtyFlag(bDirtyDuringLoad);
	}
}

void UControlRigBlueprint::RecompileVM()
{
	UControlRigBlueprintGeneratedClass* RigClass = GetControlRigBlueprintGeneratedClass();
	UControlRig* CDO = Cast<UControlRig>(RigClass->GetDefaultObject(true /* create if needed */));
	if (CDO->VM != nullptr)
	{
		TGuardValue<bool> ReentrantGuardSelf(bSuspendModelNotificationsForSelf, true);
		TGuardValue<bool> ReentrantGuardOthers(bSuspendModelNotificationsForOthers, true);

		CDO->Hierarchy = HierarchyContainer;

		if (CDO->VM->GetOuter() != CDO)
		{
			CDO->VM = NewObject<URigVM>(CDO, TEXT("VM"));
		}

		if (!HasAnyFlags(RF_Transient | RF_Transactional))
		{
			CDO->Modify(false);
		}
		CDO->VM->Reset();

		FRigUnitContext InitContext;
		InitContext.State = EControlRigState::Init;
		InitContext.Hierarchy = &CDO->Hierarchy;

		FRigUnitContext UpdateContext = InitContext;
		InitContext.State = EControlRigState::Update;

		void* InitContextPtr = &InitContext;
		void* UpdateContextPtr = &UpdateContext;

		TArray<FRigVMUserDataArray> UserData;
		UserData.Add(FRigVMUserDataArray(&InitContextPtr, 1));
		UserData.Add(FRigVMUserDataArray(&UpdateContextPtr, 1));

		URigVMCompiler* Compiler = URigVMCompiler::StaticClass()->GetDefaultObject<URigVMCompiler>();
		Compiler->Settings = VMCompileSettings;
		Compiler->Compile(Model, GetController(), CDO->VM, CDO->GetExternalVariablesImpl(false), UserData, &PinToOperandMap);

		CDO->Execute(EControlRigState::Init, FRigUnit_BeginExecution::EventName); // need to clarify if we actually need this
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

URigVMGraph* UControlRigBlueprint::GetModel(const UEdGraph* InEdGraph) const
{
	if (InEdGraph == nullptr)
	{
		return Model;
	}

	const UControlRigGraph* RigGraph = Cast< UControlRigGraph>(InEdGraph);
	check(RigGraph);

	if (RigGraph->GetOuter() == this)
	{
		return Model;
	}

	ensure(false);
	return nullptr;
}

TArray<URigVMGraph*> UControlRigBlueprint::GetAllModels() const
{
	TArray<URigVMGraph*> Models;
	Models.Add(GetModel());
	Models.Append(GetModel()->GetContainedGraphs());
	return Models;
}

URigVMController* UControlRigBlueprint::GetController(URigVMGraph* InGraph) const
{
	if (InGraph == nullptr)
	{
		InGraph = Model;
	}

	URigVMController* const* ControllerPtr = Controllers.Find(InGraph);
	if (ControllerPtr)
	{
		return *ControllerPtr;
	}
	return nullptr;
}

URigVMController* UControlRigBlueprint::GetOrCreateController(URigVMGraph* InGraph)
{
	if (URigVMController* ExistingController = GetController(InGraph))
	{
		return ExistingController;
	}

	if (InGraph == nullptr)
	{
		InGraph = Model;
	}

	URigVMController* Controller = NewObject<URigVMController>(this);
	Controller->SetExecuteContextStruct(FControlRigExecuteContext::StaticStruct());
	Controller->SetGraph(InGraph);
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

	TWeakObjectPtr<UControlRigBlueprint> WeakThis(this);

	// this delegate is used by the controller to determine variable validity
	// during a bind process. the controller itself doesn't own the variables,
	// so we need a delegate to request them from the owning blueprint
	Controller->GetExternalVariablesDelegate.BindLambda([WeakThis]() -> TArray<FRigVMExternalVariable> {

		if (WeakThis.IsValid())
		{
			if (UControlRigBlueprintGeneratedClass* RigClass = WeakThis->GetControlRigBlueprintGeneratedClass())
			{
				if (UControlRig* CDO = Cast<UControlRig>(RigClass->GetDefaultObject(true /* create if needed */)))
				{
					return CDO->GetExternalVariablesImpl(true /* rely on variables within blueprint */);
				}
			}
		}
		return TArray<FRigVMExternalVariable>();

	});

#if WITH_EDITOR

	// this sets up three delegates:
	// a) get external variables (mapped to Controller->GetExternalVariables)
	// b) bind pin to variable (mapped to Controller->BindPinToVariable)
	// c) create external variable (mapped to the passed in tfunction)
	// the last one is defined within the blueprint since the controller
	// doesn't own the variables and can't create one itself.
	Controller->SetupDefaultUnitNodeDelegates(TDelegate<FName(FRigVMExternalVariable)>::CreateLambda(
		[WeakThis](FRigVMExternalVariable InVariableToCreate) -> FName {
			if (WeakThis.IsValid())
			{
				return WeakThis->AddCRMemberVariableFromExternal(InVariableToCreate);
			}
			return NAME_None;
		}
	));
#endif

	Controller->RemoveStaleNodes();
	Controllers.Add(InGraph, Controller);
	return Controller;
}

URigVMController* UControlRigBlueprint::GetController(const UEdGraph* InEdGraph) const
{
	return GetController(GetModel(InEdGraph));
}

URigVMController* UControlRigBlueprint::GetOrCreateController(const UEdGraph* InEdGraph)
{
	return GetOrCreateController(GetModel(InEdGraph));
}

void UControlRigBlueprint::GetTypeActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	IControlRigEditorModule::Get().GetTypeActions((UControlRigBlueprint*)this, ActionRegistrar);
}

void UControlRigBlueprint::GetInstanceActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	IControlRigEditorModule::Get().GetInstanceActions((UControlRigBlueprint*)this, ActionRegistrar);
}

void UControlRigBlueprint::SetObjectBeingDebugged(UObject* NewObject)
{
	UControlRig* PreviousRigBeingDebugged = Cast<UControlRig>(GetObjectBeingDebugged());
	if (PreviousRigBeingDebugged && PreviousRigBeingDebugged != NewObject)
	{
		PreviousRigBeingDebugged->DrawInterface.Reset();
		PreviousRigBeingDebugged->ControlRigLog = nullptr;
	}

	Super::SetObjectBeingDebugged(NewObject);

	if (Validator)
	{
		if (Validator->GetControlRig() != nullptr)
		{
			Validator->SetControlRig(Cast<UControlRig>(GetObjectBeingDebugged()));
		}
	}
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

UClass* UControlRigBlueprint::GetControlRigClass()
{
	return GeneratedClass;
}

UControlRig* UControlRigBlueprint::CreateControlRig()
{
	RecompileVMIfRequired();

	UControlRig* Rig = NewObject<UControlRig>(this, GetControlRigClass());
	Rig->Initialize(true);
	return Rig;
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

FName UControlRigBlueprint::AddMemberVariable(const FName& InName, const FString& InCPPType, bool bIsPublic, bool bIsReadOnly)
{
	FRigVMExternalVariable Variable;
	Variable.Name = InName;
	Variable.bIsPublic = bIsPublic;
	Variable.bIsReadOnly = bIsReadOnly;

	FString CPPType = InCPPType;
	if (CPPType.StartsWith(TEXT("TMap<")))
	{
		UE_LOG(LogControlRigDeveloper, Warning, TEXT("TMap Variables are not supported."));
		return NAME_None;
	}

	Variable.bIsArray = CPPType.StartsWith(TEXT("TArray<"));
	if (Variable.bIsArray)
	{
		CPPType = CPPType.RightChop(7).LeftChop(1);
	}

	if (CPPType == TEXT("bool"))
	{
		Variable.TypeName = *CPPType;
		Variable.Size = sizeof(bool);
	}
	else if (CPPType == TEXT("float"))
	{
		Variable.TypeName = *CPPType;
		Variable.Size = sizeof(float);
	}
	else if (CPPType == TEXT("int32"))
	{
		Variable.TypeName = *CPPType;
		Variable.Size = sizeof(int32);
	}
	else if (CPPType == TEXT("FString"))
	{
		Variable.TypeName = *CPPType;
		Variable.Size = sizeof(FString);
	}
	else if (CPPType == TEXT("FName"))
	{
		Variable.TypeName = *CPPType;
		Variable.Size = sizeof(FName);
	}
	else if (CPPType == TEXT("FName"))
	{
		Variable.TypeName = *CPPType;
		Variable.Size = sizeof(FName);
	}
	else if(UScriptStruct* ScriptStruct = URigVMPin::FindObjectFromCPPTypeObjectPath<UScriptStruct>(CPPType))
	{
		Variable.TypeName = *ScriptStruct->GetStructCPPName();
		Variable.TypeObject = ScriptStruct;
		Variable.Size = ScriptStruct->GetStructureSize();
	}
	else if (UEnum* Enum= URigVMPin::FindObjectFromCPPTypeObjectPath<UEnum>(CPPType))
	{
		Variable.TypeName = *Enum->CppType;
		Variable.TypeObject = Enum;
		Variable.Size = Enum->GetResourceSizeBytes(EResourceSizeMode::EstimatedTotal);
	}

	FName Result = AddCRMemberVariableFromExternal(Variable);
	if (!Result.IsNone())
	{
		FBPCompileRequest Request(this, EBlueprintCompileOptions::None, nullptr);
		FBlueprintCompilationManager::CompileSynchronously(Request);
	}
	return Result;
}

FName UControlRigBlueprint::AddTransientControl(URigVMPin* InPin)
{
	TUniquePtr<FControlValueScope> ValueScope;
	if (!UControlRigSettings::Get()->bResetControlsOnPinValueInteraction) // if we need to retain the controls
	{
		ValueScope = MakeUnique<FControlValueScope>(this);
	}

	// for now we only allow one pin control at the same time
	ClearTransientControls();

	UControlRigBlueprintGeneratedClass* RigClass = GetControlRigBlueprintGeneratedClass();
	UControlRig* CDO = Cast<UControlRig>(RigClass->GetDefaultObject(true /* create if needed */));

	FRigElementKey SpaceKey;
	if (URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(InPin->GetPinForLink()->GetNode()))
	{
		if (TSharedPtr<FStructOnScope> DefaultStructScope = UnitNode->ConstructStructInstance())
		{
			FRigUnit* DefaultStruct = (FRigUnit*)DefaultStructScope->GetStructMemory();

			FString PinPath = InPin->GetPinForLink()->GetPinPath();
			FString Left, Right;

			if (URigVMPin::SplitPinPathAtStart(PinPath, Left, Right))
		{
				SpaceKey = DefaultStruct->DetermineSpaceForPin(Right, &HierarchyContainer);
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
			FName ControlName = InstancedControlRig->AddTransientControl(InPin, SpaceKey);
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
	TUniquePtr<FControlValueScope> ValueScope;
	if (!UControlRigSettings::Get()->bResetControlsOnPinValueInteraction) // if we need to retain the controls
	{
		ValueScope = MakeUnique<FControlValueScope>(this);
	}

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
	TUniquePtr<FControlValueScope> ValueScope;
	if (!UControlRigSettings::Get()->bResetControlsOnPinValueInteraction) // if we need to retain the controls
	{
		ValueScope = MakeUnique<FControlValueScope>(this);
	}

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
	TUniquePtr<FControlValueScope> ValueScope;
	if (!UControlRigSettings::Get()->bResetControlsOnPinValueInteraction) // if we need to retain the controls
	{
		ValueScope = MakeUnique<FControlValueScope>(this);
	}

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
	TUniquePtr<FControlValueScope> ValueScope;
	if (!UControlRigSettings::Get()->bResetControlsOnPinValueInteraction) // if we need to retain the controls
	{
		ValueScope = MakeUnique<FControlValueScope>(this);
	}

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

	bDirtyDuringLoad = true;

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
					else
					{
						// at this point the BP skeleton might not have been compiled,
						// we should look into the new variables array to find the property
						for (FBPVariableDescription NewVariable : NewVariables)
						{
							if (NewVariable.VarName == PropertyName && NewVariable.VarType.PinCategory == UEdGraphSchema_K2::PC_Struct)
							{
								if (UScriptStruct* Struct = Cast<UScriptStruct>(NewVariable.VarType.PinSubCategoryObject))
								{
									StructPath = Struct->GetPathName();
									break;
								}
							}
						}
					}
				}

				URigVMNode* ModelNode = nullptr;

				UScriptStruct* UnitStruct = URigVMPin::FindObjectFromCPPTypeObjectPath<UScriptStruct>(StructPath);
				if (UnitStruct && UnitStruct->IsChildOf(FRigVMStruct::StaticStruct()))
				{ 
					ModelNode = GetOrCreateController()->AddUnitNode(UnitStruct, TEXT("Execute"), NodePosition, PropertyName.ToString(), false);
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

						ModelNode = GetOrCreateController()->AddParameterNode(PropertyName, DataType.ToString(), DataTypeObject, bIsInput, FString(), NodePosition, PropertyName.ToString(), false);
					}
				}
				else
				{
					continue;
				}

				if (ModelNode)
				{
					bool bWasReportingEnabled = GetOrCreateController()->IsReportingEnabled();
					GetOrCreateController()->EnableReporting(false);

					for (UEdGraphPin* Pin : RigNode->Pins)
					{
							FString PinPath = LocalHelpers::FixUpPinPath(Pin->GetName());

							// check the material + mesh pins for deprecated control nodes
							if (URigVMUnitNode* ModelUnitNode = Cast<URigVMUnitNode>(ModelNode))
							{
								if (ModelUnitNode->GetScriptStruct()->IsChildOf(FRigUnit_Control::StaticStruct()))
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
							GetOrCreateController()->SetArrayPinSize(PinPath, ArraySize, FString(), false);
						}

						if (RigNode->ExpandedPins_DEPRECATED.Find(Pin->GetName()) != INDEX_NONE)
						{
								GetOrCreateController()->SetPinExpansion(PinPath, true, false);
						}

						if (Pin->SubPins.Num() == 0 && !Pin->DefaultValue.IsEmpty() && Pin->Direction == EGPD_Input)
						{
								GetOrCreateController()->SetPinDefaultValue(PinPath, Pin->DefaultValue, false, false, false);
						}
					}

					GetOrCreateController()->EnableReporting(bWasReportingEnabled);
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
				GetOrCreateController()->AddCommentNode(CommentNode->NodeComment, NodePosition, NodeSize, CommentNode->CommentColor, CommentNode->GetName(), false);
			}
		}

		SetupPinRedirectorsForBackwardsCompatibility();

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
							GetOrCreateController()->AddLink(SourcePinPath, TargetPinPath, false);
						}
					}
				}
			}
		}
	}

	RebuildGraphFromModel();
}

void UControlRigBlueprint::SetupPinRedirectorsForBackwardsCompatibility()
{
	for (URigVMNode* Node : Model->GetNodes())
	{
		if (URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(Node))
		{
			UScriptStruct* Struct = UnitNode->GetScriptStruct();
			if (Struct == FRigUnit_SetBoneTransform::StaticStruct())
			{
				URigVMPin* TransformPin = UnitNode->FindPin(TEXT("Transform"));
				URigVMPin* ResultPin = UnitNode->FindPin(TEXT("Result"));
				GetOrCreateController()->AddPinRedirector(false, true, TransformPin->GetPinPath(), ResultPin->GetPinPath());
			}
		}
	}
}

void UControlRigBlueprint::RebuildGraphFromModel()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	TGuardValue<bool> SelfGuard(bSuspendModelNotificationsForSelf, true);
	check(GetOrCreateController());

	for (UEdGraph* Graph : UbergraphPages)
	{
		TArray<UEdGraphNode*> Nodes = Graph->Nodes;
		for (UEdGraphNode* Node : Nodes)
		{
			Graph->RemoveNode(Node);
		}
	}

	GetController()->ResendAllNotifications();
}

void UControlRigBlueprint::Notify(ERigVMGraphNotifType InNotifType, UObject* InSubject)
{
	GetOrCreateController()->Notify(InNotifType, InSubject);
}

void UControlRigBlueprint::HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

#if WITH_EDITOR

	if (bSuspendAllNotifications)
	{
		return;
	}

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

							if (Pin->IsDefinedAsConstant() || Pin->GetRootPin()->IsDefinedAsConstant())
							{
								// re-init the rigs
								RequestControlRigInit();
								bRequiresRecompile = true;
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
			case ERigVMGraphNotifType::PinBoundVariableChanged:
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

void UControlRigBlueprint::SuspendNotifications(bool bSuspendNotifs)
{
	if (bSuspendAllNotifications == bSuspendNotifs)
	{
		return;
	}

	bSuspendAllNotifications = bSuspendNotifs;
	if (!bSuspendNotifs)
	{
		RebuildGraphFromModel();
		RefreshEditorEvent.Broadcast(this);
		RequestAutoVMRecompilation();
	}
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

void UControlRigBlueprint::CreateMemberVariablesOnLoad()
{
#if WITH_EDITOR

	int32 LinkerVersion = GetLinkerCustomVersion(FControlRigObjectVersion::GUID);
	if (LinkerVersion < FControlRigObjectVersion::SwitchedToRigVM)
	{
		InitializeModelIfRequired();
	}

	AddedMemberVariableMap.Reset();

	for (int32 VariableIndex = 0; VariableIndex < NewVariables.Num(); VariableIndex++)
	{
		AddedMemberVariableMap.Add(NewVariables[VariableIndex].VarName, VariableIndex);
	}

	if (Model == nullptr)
	{
		return;
	}

	// setup variables on the blueprint based on the previous "parameters"
	if (GetLinkerCustomVersion(FControlRigObjectVersion::GUID) < FControlRigObjectVersion::BlueprintVariableSupport)
	{
		TSharedPtr<FKismetNameValidator> NameValidator = MakeShareable(new FKismetNameValidator(this, NAME_None, nullptr));

		TArray<URigVMNode*> Nodes = Model->GetNodes();
		for (URigVMNode* Node : Nodes)
		{
			if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
			{
				if (URigVMPin* VariablePin = VariableNode->FindPin(TEXT("Variable")))
				{
					if (VariablePin->GetDirection() != ERigVMPinDirection::Visible)
					{
						continue;
					}
				}

				FRigVMGraphVariableDescription Description = VariableNode->GetVariableDescription();
				if (AddedMemberVariableMap.Contains(Description.Name))
				{
					continue;
				}

				FEdGraphPinType PinType = UControlRig::GetPinTypeFromExternalVariable(Description.ToExternalVariable());
				if (!PinType.PinCategory.IsValid())
				{
					continue;
				}

				FName VarName = FindCRMemberVariableUniqueName(NameValidator, Description.Name.ToString());
				int32 VariableIndex = AddCRMemberVariable(this, VarName, PinType, false, false);
				if (VariableIndex != INDEX_NONE)
				{
					AddedMemberVariableMap.Add(Description.Name, VariableIndex);
					bDirtyDuringLoad = true;
				}
			}

			if (URigVMParameterNode* ParameterNode = Cast<URigVMParameterNode>(Node))
			{
				if (URigVMPin* ParameterPin = ParameterNode->FindPin(TEXT("Parameter")))
				{
					if (ParameterPin->GetDirection() != ERigVMPinDirection::Visible)
					{
						continue;
					}
				}

				FRigVMGraphParameterDescription Description = ParameterNode->GetParameterDescription();
				if (AddedMemberVariableMap.Contains(Description.Name))
				{
					continue;
				}

				FEdGraphPinType PinType = UControlRig::GetPinTypeFromExternalVariable(Description.ToExternalVariable());
				if (!PinType.PinCategory.IsValid())
				{
					continue;
				}

				FName VarName = FindCRMemberVariableUniqueName(NameValidator, Description.Name.ToString());
				int32 VariableIndex = AddCRMemberVariable(this, VarName, PinType, true, !Description.bIsInput);
				if (VariableIndex != INDEX_NONE)
				{
					AddedMemberVariableMap.Add(Description.Name, VariableIndex);
					bDirtyDuringLoad = true;
				}
			}
		}
	}

#endif
}

#if WITH_EDITOR

FName UControlRigBlueprint::FindCRMemberVariableUniqueName(TSharedPtr<FKismetNameValidator> InNameValidator, const FString& InBaseName)
{
	FString KismetName = InBaseName;
	if (InNameValidator->IsValid(KismetName) == EValidatorResult::ContainsInvalidCharacters)
	{
		for (TCHAR& TestChar : KismetName)
		{
			for (TCHAR BadChar : UE_BLUEPRINT_INVALID_NAME_CHARACTERS)
			{
				if (TestChar == BadChar)
				{
					TestChar = TEXT('_');
					break;
				}
			}
		}
	}

	int32 Suffix = 0;
	while (InNameValidator->IsValid(KismetName) != EValidatorResult::Ok)
	{
		KismetName = FString::Printf(TEXT("%s_%d"), *InBaseName, Suffix);
		Suffix++;
	}


	return *KismetName;
}

int32 UControlRigBlueprint::AddCRMemberVariable(UControlRigBlueprint* InBlueprint, const FName& InVarName, FEdGraphPinType InVarType, bool bIsPublic, bool bIsReadOnly)
{
	FBPVariableDescription NewVar;

	NewVar.VarName = InVarName;
	NewVar.VarGuid = FGuid::NewGuid();
	NewVar.FriendlyName = FName::NameToDisplayString(InVarName.ToString(), (InVarType.PinCategory == UEdGraphSchema_K2::PC_Boolean) ? true : false);
	NewVar.VarType = InVarType;

	NewVar.PropertyFlags |= (CPF_Edit | CPF_BlueprintVisible | CPF_DisableEditOnInstance);

	if (bIsPublic)
	{
		NewVar.PropertyFlags &= ~CPF_DisableEditOnInstance;
	}

	if (bIsReadOnly)
	{
		NewVar.PropertyFlags |= CPF_BlueprintReadOnly;
	}

	NewVar.ReplicationCondition = COND_None;

	NewVar.Category = UEdGraphSchema_K2::VR_DefaultCategory;

	// user created variables should be none of these things
	NewVar.VarType.bIsConst = false;
	NewVar.VarType.bIsWeakPointer = false;
	NewVar.VarType.bIsReference = false;

	// Text variables, etc. should default to multiline
	NewVar.SetMetaData(TEXT("MultiLine"), TEXT("true"));

	return InBlueprint->NewVariables.Add(NewVar);
}

FName UControlRigBlueprint::AddCRMemberVariableFromExternal(FRigVMExternalVariable InVariableToCreate)
{
	FEdGraphPinType PinType = UControlRig::GetPinTypeFromExternalVariable(InVariableToCreate);
	if (!PinType.PinCategory.IsValid())
	{
		return NAME_None;
	}

	Modify();

	TSharedPtr<FKismetNameValidator> NameValidator = MakeShareable(new FKismetNameValidator(this, NAME_None, nullptr));
	FName VarName = FindCRMemberVariableUniqueName(NameValidator, InVariableToCreate.Name.ToString());
	int32 VariableIndex = AddCRMemberVariable(this, VarName, PinType, InVariableToCreate.bIsPublic, InVariableToCreate.bIsReadOnly);
	if (VariableIndex != INDEX_NONE)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(this);
		return VarName;
	}

	return NAME_None;
}

#endif

void UControlRigBlueprint::PatchVariableNodesOnLoad()
{
#if WITH_EDITOR

	// setup variables on the blueprint based on the previous "parameters"
	if (GetLinkerCustomVersion(FControlRigObjectVersion::GUID) < FControlRigObjectVersion::BlueprintVariableSupport)
	{
		TGuardValue<bool> GuardNotifsSelf(bSuspendModelNotificationsForSelf, true);

		GetOrCreateController()->ReattachLinksToPinObjects();

		check(Model);

		TArray<URigVMNode*> Nodes = Model->GetNodes();
		for (URigVMNode* Node : Nodes)
		{
			if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
			{
				FRigVMGraphVariableDescription Description = VariableNode->GetVariableDescription();
				if (!AddedMemberVariableMap.Contains(Description.Name))
				{
					continue;
				}

				int32 VariableIndex = AddedMemberVariableMap.FindChecked(Description.Name);
				FName VarName = NewVariables[VariableIndex].VarName;
				GetOrCreateController()->RefreshVariableNode(VariableNode->GetFName(), VarName, Description.CPPType, Description.CPPTypeObject, false);
				bDirtyDuringLoad = true;
			}

			if (URigVMParameterNode* ParameterNode = Cast<URigVMParameterNode>(Node))
			{
				FRigVMGraphParameterDescription Description = ParameterNode->GetParameterDescription();
				if (!AddedMemberVariableMap.Contains(Description.Name))
				{
					continue;
				}

				int32 VariableIndex = AddedMemberVariableMap.FindChecked(Description.Name);
				FName VarName = NewVariables[VariableIndex].VarName;
				GetOrCreateController()->ReplaceParameterNodeWithVariable(ParameterNode->GetFName(), VarName, Description.CPPType, Description.CPPTypeObject, false);
				bDirtyDuringLoad = true;
			}
		}
	}

	AddedMemberVariableMap.Reset();
	LastNewVariables = NewVariables;

#endif
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
		OutputControl.OffsetTransform = InputControl.OffsetTransform;
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
						OutputControl.OffsetTransform = InputControl.OffsetTransform;
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
	if (bSuspendAllNotifications)
	{
		return;
	}
	PropagateHierarchyFromBPToInstances();
}

void UControlRigBlueprint::HandleOnElementRemoved(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey)
{
	if (bSuspendAllNotifications)
	{
		return;
	}

	Modify();
	Influences.OnKeyRemoved(InKey);
	PropagateHierarchyFromBPToInstances();
}

void UControlRigBlueprint::HandleOnElementRenamed(FRigHierarchyContainer* InContainer, ERigElementType InElementType, const FName& InOldName, const FName& InNewName)
{
	if (bSuspendAllNotifications)
	{
		return;
	}

	Modify();
	Influences.OnKeyRenamed(FRigElementKey(InOldName, InElementType), FRigElementKey(InNewName, InElementType));
	PropagateHierarchyFromBPToInstances();
}

void UControlRigBlueprint::HandleOnElementReparented(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey, const FName& InOldParentName, const FName& InNewParentName)
{
	if (bSuspendAllNotifications)
	{
		return;
	}
	PropagateHierarchyFromBPToInstances();
}

void UControlRigBlueprint::HandleOnElementSelected(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey, bool bSelected)
{
	if (bSuspendAllNotifications)
	{
		return;
	}
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

UControlRigBlueprint::FControlValueScope::FControlValueScope(UControlRigBlueprint* InBlueprint)
: Blueprint(InBlueprint)
{
#if WITH_EDITOR
	check(Blueprint);

	if (UControlRig* CR = Cast<UControlRig>(Blueprint->GetObjectBeingDebugged()))
	{
		const TArray<FRigControl>& Controls = CR->AvailableControls();
		for (const FRigControl& Control : Controls)
		{
			ControlValues.Add(Control.Name, CR->GetControlValue(Control.Name));
		}
	}
#endif
}

UControlRigBlueprint::FControlValueScope::~FControlValueScope()
{
#if WITH_EDITOR
	check(Blueprint);

	if (UControlRig* CR = Cast<UControlRig>(Blueprint->GetObjectBeingDebugged()))
	{
		for (const TPair<FName, FRigControlValue>& Pair : ControlValues)
		{
			if (CR->FindControl(Pair.Key))
			{
				CR->SetControlValue(Pair.Key, Pair.Value);
			}
		}
	}
#endif
}

#if WITH_EDITOR

void UControlRigBlueprint::OnPreVariableChange(UObject* InObject)
{
	if (InObject != this)
	{
		return;
	}
	LastNewVariables = NewVariables;
}

void UControlRigBlueprint::OnPostVariableChange(UBlueprint* InBlueprint)
{
	if (InBlueprint != this)
	{
		return;
	}

	TMap<FGuid, int32> NewVariablesByGuid;
	for (int32 VarIndex = 0; VarIndex < NewVariables.Num(); VarIndex++)
	{
		NewVariablesByGuid.Add(NewVariables[VarIndex].VarGuid, VarIndex);
	}

	TMap<FGuid, int32> OldVariablesByGuid;
	for (int32 VarIndex = 0; VarIndex < LastNewVariables.Num(); VarIndex++)
	{
		OldVariablesByGuid.Add(LastNewVariables[VarIndex].VarGuid, VarIndex);
	}

	for (const FBPVariableDescription& OldVariable : LastNewVariables)
	{
		if (!NewVariablesByGuid.Contains(OldVariable.VarGuid))
		{
			OnVariableRemoved(OldVariable.VarName);
			continue;
		}
	}

	for (const FBPVariableDescription& NewVariable : NewVariables)
	{
		if (!OldVariablesByGuid.Contains(NewVariable.VarGuid))
		{
			OnVariableAdded(NewVariable.VarName);
			continue;
		}

		int32 OldVarIndex = OldVariablesByGuid.FindChecked(NewVariable.VarGuid);
		const FBPVariableDescription& OldVariable = LastNewVariables[OldVarIndex];
		if (OldVariable.VarName != NewVariable.VarName)
		{
			OnVariableRenamed(OldVariable.VarName, NewVariable.VarName);
		}

		if (OldVariable.VarType != NewVariable.VarType)
		{
			OnVariableTypeChanged(NewVariable.VarName, OldVariable.VarType, NewVariable.VarType);
		}
	}

	LastNewVariables = NewVariables;
}

void UControlRigBlueprint::OnVariableAdded(const FName& InVarName)
{
	BroadcastExternalVariablesChangedEvent();
}

void UControlRigBlueprint::OnVariableRemoved(const FName& InVarName)
{
	if (URigVMController* Controller = GetController())
	{
		Controller->OnExternalVariableRemoved(InVarName, true);
	}
	BroadcastExternalVariablesChangedEvent();
}

void UControlRigBlueprint::OnVariableRenamed(const FName& InOldVarName, const FName& InNewVarName)
{
	if (URigVMController* Controller = GetController())
	{
		Controller->OnExternalVariableRenamed(InOldVarName, InNewVarName, true);
	}
	BroadcastExternalVariablesChangedEvent();
}

void UControlRigBlueprint::OnVariableTypeChanged(const FName& InVarName, FEdGraphPinType InOldPinType, FEdGraphPinType InNewPinType)
{
	if (URigVMController* Controller = GetController())
	{
		FRigVMExternalVariable NewVariable = UControlRig::GetExternalVariableFromPinType(InVarName, InNewPinType);
		if (NewVariable.IsValid(true)) // allow nullptr
		{
			Controller->OnExternalVariableTypeChanged(InVarName, NewVariable.TypeName.ToString(), NewVariable.TypeObject, true);
		}
		else
		{
			Controller->OnExternalVariableRemoved(InVarName, true);
		}
	}
	BroadcastExternalVariablesChangedEvent();
}

void UControlRigBlueprint::BroadcastExternalVariablesChangedEvent()
{
	if (UControlRigBlueprintGeneratedClass* RigClass = GetControlRigBlueprintGeneratedClass())
	{
		if (UControlRig* CDO = Cast<UControlRig>(RigClass->GetDefaultObject(true /* create if needed */)))
		{
			ExternalVariablesChangedEvent.Broadcast(CDO->GetExternalVariables());
		}
	}
}

#endif

#undef LOCTEXT_NAMESPACE

