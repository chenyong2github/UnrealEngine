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
#include "Graph/ControlRigGraphSchema.h"
#include "UObject/ObjectSaveContext.h"
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
#include "AssetRegistryModule.h"

#if WITH_EDITOR
#include "IControlRigEditorModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ControlRigBlueprintUtils.h"
#include "Settings/ControlRigSettings.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "CookOnTheSide/CookOnTheFlyServer.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#endif//WITH_EDITOR

#define LOCTEXT_NAMESPACE "ControlRigBlueprint"

FEdGraphPinType FControlRigPublicFunctionArg::GetPinType() const
{
	FRigVMExternalVariable Variable;
	Variable.Name = Name;
	Variable.bIsArray = bIsArray;
	Variable.TypeName = CPPType;
	
	if(CPPTypeObjectPath.IsValid())
	{
		Variable.TypeObject = URigVMPin::FindObjectFromCPPTypeObjectPath(CPPTypeObjectPath.ToString());
	}

	return UControlRig::GetPinTypeFromExternalVariable(Variable);
}

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
	bIsCompiling = false;
	VMRecompilationBracket = 0;

	Model = ObjectInitializer.CreateDefaultSubobject<URigVMGraph>(this, TEXT("RigVMModel"));
	FunctionLibrary = ObjectInitializer.CreateDefaultSubobject<URigVMFunctionLibrary>(this, TEXT("RigVMFunctionLibrary"));
	FunctionLibraryEdGraph = ObjectInitializer.CreateDefaultSubobject<UControlRigGraph>(this, TEXT("RigVMFunctionLibraryEdGraph"));
	FunctionLibraryEdGraph->Schema = UControlRigGraphSchema::StaticClass();
	FunctionLibraryEdGraph->bAllowRenaming = 0;
	FunctionLibraryEdGraph->bEditable = 0;
	FunctionLibraryEdGraph->bAllowDeletion = 0;
	FunctionLibraryEdGraph->bIsFunctionDefinition = false;
	FunctionLibraryEdGraph->Initialize(this);

	Model->SetDefaultFunctionLibrary(FunctionLibrary);

	Validator = ObjectInitializer.CreateDefaultSubobject<UControlRigValidator>(this, TEXT("ControlRigValidator"));

	bDirtyDuringLoad = false;
	bErrorsDuringCompilation = false;

	SupportedEventNames.Reset();
	bExposesAnimatableControls = false;

	VMCompileSettings.ASTSettings.ReportDelegate.BindUObject(this, &UControlRigBlueprint::HandleReportFromCompiler);

#if WITH_EDITOR
	CompileLog.SetSourcePath(GetPathName());
	CompileLog.bLogDetailedResults = false;
	CompileLog.EventDisplayThresholdMs = false;
#endif

	Hierarchy = CreateDefaultSubobject<URigHierarchy>(TEXT("Hierarchy"));
	HierarchyController = CreateDefaultSubobject<URigHierarchyController>(TEXT("HierarchyController"));
	HierarchyController->SetHierarchy(Hierarchy);
	HierarchyController->OnModified().AddUObject(this, &UControlRigBlueprint::HandleHierarchyModified);
}

UControlRigBlueprint::UControlRigBlueprint()
{
}

void UControlRigBlueprint::InitializeModelIfRequired(bool bRecompileVM)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (Controllers.Num() == 0)
	{
		GetOrCreateController(Model);
		GetOrCreateController(FunctionLibrary);

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

		FunctionLibraryEdGraph->Initialize(this);
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

UClass* UControlRigBlueprint::RegenerateClass(UClass* ClassToRegenerate, UObject* PreviousCDO)
{
	UClass* Result = nullptr;
	{
		TGuardValue<bool> NotificationGuard(bSuspendAllNotifications, true);
		Result = Super::RegenerateClass(ClassToRegenerate, PreviousCDO);
	}
	PropagateHierarchyFromBPToInstances();
	return Result;
}

void UControlRigBlueprint::LoadModulesRequiredForCompilation() 
{
}

bool UControlRigBlueprint::ExportGraphToText(UEdGraph* InEdGraph, FString& OutText)
{
	OutText.Empty();

	if (URigVMGraph* RigGraph = GetModel(InEdGraph))
	{
		if (URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(RigGraph->GetOuter()))
		{
			if (URigVMController* Controller = GetOrCreateController(CollapseNode->GetGraph()))
			{
				TArray<FName> NodeNamesToExport;
				NodeNamesToExport.Add(CollapseNode->GetFName());
				OutText = Controller->ExportNodesToText(NodeNamesToExport);
			}
		}
	}

	// always return true so that the default mechanism doesn't take over
	return true;
}

bool UControlRigBlueprint::CanImportGraphFromText(const FString& InClipboardText)
{
	return GetTemplateController()->CanImportNodesFromText(InClipboardText);
}

void UControlRigBlueprint::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
	PostEditChangeChainPropertyEvent.Broadcast(PropertyChangedEvent);
}

bool UControlRigBlueprint::TryImportGraphFromText(const FString& InClipboardText, UEdGraph** OutGraphPtr)
{
	if (OutGraphPtr)
	{
		*OutGraphPtr = nullptr;
	}

	if (URigVMController* FunctionLibraryController = GetOrCreateController(GetLocalFunctionLibrary()))
	{
		TGuardValue<FRigVMController_RequestLocalizeFunctionDelegate> RequestLocalizeDelegateGuard(
            FunctionLibraryController->RequestLocalizeFunctionDelegate,
            FRigVMController_RequestLocalizeFunctionDelegate::CreateLambda([this](URigVMLibraryNode* InFunctionToLocalize)
            {
            	BroadcastRequestLocalizeFunctionDialog(InFunctionToLocalize);

                const URigVMLibraryNode* LocalizedFunctionNode = GetLocalFunctionLibrary()->FindPreviouslyLocalizedFunction(InFunctionToLocalize);
                return LocalizedFunctionNode != nullptr;
            })
        );
		
		TArray<FName> ImportedNodeNames = FunctionLibraryController->ImportNodesFromText(InClipboardText, true);
		if (ImportedNodeNames.Num() == 0)
		{
			return false;
		}

		URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(GetLocalFunctionLibrary()->FindFunction(ImportedNodeNames[0]));
		if (ImportedNodeNames.Num() > 1 || CollapseNode == nullptr || CollapseNode->GetContainedGraph() == nullptr)
		{
			FunctionLibraryController->Undo();
			return false;
		}

		UEdGraph* EdGraph = GetEdGraph(CollapseNode->GetContainedGraph());
		if (OutGraphPtr)
		{
			*OutGraphPtr = EdGraph;
		}

		BroadcastGraphImported(EdGraph);
	}

	// always return true so that the default mechanism doesn't take over
	return true;
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

void UControlRigBlueprint::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if(Ar.IsObjectReferenceCollector())
	{
		TArray<UBlueprint*> ReferencedBlueprints;
		
		TArray<UEdGraph*> EdGraphs;
		GetAllGraphs(EdGraphs);
		for (UEdGraph* EdGraph : EdGraphs)
		{
			for(UEdGraphNode* Node : EdGraph->Nodes)
			{
				if(UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(Node))
				{
					if(URigVMFunctionReferenceNode* FunctionRefNode = Cast<URigVMFunctionReferenceNode>(RigNode->GetModelNode()))
					{
						if(URigVMLibraryNode* ReferencedNode = FunctionRefNode->GetReferencedNode())
						{
							if(URigVMFunctionLibrary* ReferencedFunctionLibrary = ReferencedNode->GetLibrary())
							{
								if(ReferencedFunctionLibrary == GetLocalFunctionLibrary())
								{
									continue;
								}

								if(UBlueprint* ReferencedBlueprint = Cast<UBlueprint>(ReferencedFunctionLibrary->GetOuter()))
								{
									ReferencedBlueprints.AddUnique(ReferencedBlueprint);
								}
							}
						}
					}
				}
			}
		}

		for(UBlueprint* ReferencedBlueprint : ReferencedBlueprints)
		{
			Ar << ReferencedBlueprints;
		}
	}
}

void UControlRigBlueprint::PreSave(const class ITargetPlatform* TargetPlatform)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS;
	Super::PreSave(TargetPlatform);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS;
}

void UControlRigBlueprint::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	Super::PreSave(ObjectSaveContext);

	SupportedEventNames.Reset();
	if (UControlRigBlueprintGeneratedClass* RigClass = GetControlRigBlueprintGeneratedClass())
	{
		if (UControlRig* CDO = Cast<UControlRig>(RigClass->GetDefaultObject(true /* create if needed */)))
		{
			SupportedEventNames = CDO->GetSupportedEvents();
		}
	}

	bExposesAnimatableControls = false;
	Hierarchy->ForEach<FRigControlElement>([this](FRigControlElement* ControlElement) -> bool
    {
		if (ControlElement->Settings.bAnimatable)
		{
			bExposesAnimatableControls = true;
			return false;
		}
		return true;
	});

	for(FControlRigPublicFunctionData& FunctionData : PublicFunctions)
	{
		if(URigVMLibraryNode* FunctionNode = FunctionLibrary->FindFunction(FunctionData.Name))
		{
			if(UControlRigGraph* Graph = Cast<UControlRigGraph>(GetEdGraph(FunctionNode->GetContainedGraph())))
			{
				FunctionData = Graph->GetPublicFunctionData();
			}
		}
	}
}

void UControlRigBlueprint::PostLoad()
{
	Super::PostLoad();

	// temporarily disable default value validation during load time, serialized values should always be accepted
	TGuardValue<bool> DisablePinDefaultValueValidation(GetOrCreateController()->bValidatePinDefaults, false);

	// correct the offset transforms
	if (GetLinkerCustomVersion(FControlRigObjectVersion::GUID) < FControlRigObjectVersion::ControlOffsetTransform)
	{
		HierarchyContainer_DEPRECATED.ControlHierarchy.PostLoad();
		if (HierarchyContainer_DEPRECATED.ControlHierarchy.Num() > 0)
		{
			bDirtyDuringLoad = true;
		}

		for (FRigControl& Control : HierarchyContainer_DEPRECATED.ControlHierarchy)
		{
			const FTransform PreviousOffsetTransform = Control.GetTransformFromValue(ERigControlValueType::Initial);
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
	}

	// convert the hierarchy from V1 to V2
	if (GetLinkerCustomVersion(FControlRigObjectVersion::GUID) < FControlRigObjectVersion::RigHierarchyV2)
	{
		Modify();
		
		TGuardValue<bool> SuspendNotifGuard(Hierarchy->GetSuspendNotificationsFlag(), true);
		
		Hierarchy->Reset();

		TMap<FRigElementKey, FRigElementKey> KeyMap;;

		for(const FRigBone& Bone : HierarchyContainer_DEPRECATED.BoneHierarchy)
		{
			const FRigElementKey OriginalParentKey = Bone.GetParentElementKey(true);
			const FRigElementKey* ParentKey = nullptr;
			if(OriginalParentKey.IsValid())
			{
				ParentKey = KeyMap.Find(OriginalParentKey);
			}
			if(ParentKey == nullptr)
			{
				ParentKey = &OriginalParentKey;
			}

			const FRigElementKey Key = HierarchyController->AddBone(Bone.Name, *ParentKey, Bone.InitialTransform, true, Bone.Type, false);
			KeyMap.Add(Bone.GetElementKey(), Key);
		}
		for(const FRigSpace& Space : HierarchyContainer_DEPRECATED.SpaceHierarchy)
		{
			const FRigElementKey Key = HierarchyController->AddNull(Space.Name, FRigElementKey(), Space.InitialTransform, false, false);
			KeyMap.Add(Space.GetElementKey(), Key);
		}
		for(const FRigControl& Control : HierarchyContainer_DEPRECATED.ControlHierarchy)
		{
			FRigControlSettings Settings;
			Settings.ControlType = Control.ControlType;
			Settings.DisplayName = Control.DisplayName;
			Settings.PrimaryAxis = Control.PrimaryAxis;
			Settings.bIsCurve = Control.bIsCurve;
			Settings.bAnimatable = Control.bAnimatable;
			Settings.bLimitTranslation = Control.bLimitTranslation;
			Settings.bLimitRotation = Control.bLimitRotation;
			Settings.bLimitScale = Control.bLimitScale;
			Settings.bDrawLimits = Control.bDrawLimits;
			Settings.MinimumValue = Control.MinimumValue;
			Settings.MaximumValue = Control.MaximumValue;
			Settings.bGizmoEnabled = Control.bGizmoEnabled;
			Settings.bGizmoVisible = Control.bGizmoVisible;
			Settings.GizmoName = Control.GizmoName;
			Settings.GizmoColor = Control.GizmoColor;
			Settings.ControlEnum = Control.ControlEnum;

			FRigControlValue InitialValue = Control.InitialValue;
			if(!InitialValue.IsValid())
			{
				InitialValue.SetFromTransform(InitialValue.Storage_DEPRECATED, Settings.ControlType, Settings.PrimaryAxis);
			}
			
			const FRigElementKey Key = HierarchyController->AddControl(
				Control.Name,
				FRigElementKey(),
				Settings,
				InitialValue,
				Control.OffsetTransform,
				Control.GizmoTransform,
				false);

			KeyMap.Add(Control.GetElementKey(), Key);
		}
		
		for(const FRigCurve& Curve : HierarchyContainer_DEPRECATED.CurveContainer)
		{
			const FRigElementKey Key = HierarchyController->AddCurve(Curve.Name, Curve.Value, false);
			KeyMap.Add(Curve.GetElementKey(), Key);
		}

		for(const FRigSpace& Space : HierarchyContainer_DEPRECATED.SpaceHierarchy)
		{
			const FRigElementKey OriginalParentKey = Space.GetParentElementKey(true);
			if(OriginalParentKey.IsValid())
			{
				FRigElementKey ParentKey;
				if(const FRigElementKey* ParentKeyPtr = KeyMap.Find(OriginalParentKey))
				{
					ParentKey = *ParentKeyPtr;
				}
				HierarchyController->SetParent(Space.GetElementKey(), ParentKey, false, false);
			}
		}

		for(const FRigControl& Control : HierarchyContainer_DEPRECATED.ControlHierarchy)
		{
			FRigElementKey OriginalParentKey = Control.GetParentElementKey(true);
			const FRigElementKey SpaceKey = Control.GetSpaceElementKey(true);
			OriginalParentKey = SpaceKey.IsValid() ? SpaceKey : OriginalParentKey;
			if(OriginalParentKey.IsValid())
			{
				FRigElementKey ParentKey;
				if(const FRigElementKey* ParentKeyPtr = KeyMap.Find(OriginalParentKey))
				{
					ParentKey = *ParentKeyPtr;
				}
				HierarchyController->SetParent(Control.GetElementKey(), ParentKey, false, false);
			}
		}

#if WITH_EDITOR
		if(!IsRunningCommandlet()) // don't show warnings like this if we are cooking
		{
			for(const TPair<FRigElementKey, FRigElementKey>& Pair: KeyMap)
			{
				if(Pair.Key != Pair.Value)
				{
					check(Pair.Key.Type == Pair.Value.Type);
					const FText TypeLabel = StaticEnum<ERigElementType>()->GetDisplayNameTextByValue((int64)Pair.Key.Type);
					GetController()->ReportWarningf(TEXT("%s '%s' was renamed to '%s' during load (fixing invalid name)."), *TypeLabel.ToString(), *Pair.Key.Name.ToString(), *Pair.Value.Name.ToString());
				}
			}
		}
	}
#endif

	PropagateHierarchyFromBPToInstances();
	
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

	TArray<URigVMGraph*> GraphsToDetach;
	GraphsToDetach.Add(GetModel());
	GraphsToDetach.Add(GetLocalFunctionLibrary());

	check(IsInGameThread());
	{
		for (URigVMGraph* GraphToDetach : GraphsToDetach)
		{
			URigVMController* Controller = GetOrCreateController(GraphToDetach);
			Controller->DetachLinksFromPinObjects();
			TArray<URigVMNode*> Nodes = GraphToDetach->GetNodes();
			for (URigVMNode* Node : Nodes)
			{
				Controller->RepopulatePinsOnNode(Node, true, false, true);
			}
		}
		SetupPinRedirectorsForBackwardsCompatibility();
	}

	for (URigVMGraph* GraphToDetach : GraphsToDetach)
	{
		URigVMController* Controller = GetOrCreateController(GraphToDetach);
		Controller->ReattachLinksToPinObjects(true /* follow redirectors */, nullptr, false, true);
		
		for(URigVMNode* Node : GraphToDetach->GetNodes())
		{
			Controller->RemoveUnusedOrphanedPins(Node, false);
		}
	}

	// perform backwards compat value upgrades
	TArray<URigVMGraph*> GraphsToValidate = GetAllModels();
	for (int32 GraphIndex = 0; GraphIndex < GraphsToValidate.Num(); GraphIndex++)
	{
		URigVMGraph* GraphToValidate = GraphsToValidate[GraphIndex];
		if(GraphToValidate == nullptr)
		{
			continue;
		}
			
		for(URigVMNode* Node : GraphToValidate->GetNodes())
		{
			TArray<URigVMPin*> Pins = Node->GetAllPinsRecursively();
			for(URigVMPin* Pin : Pins)
			{
				if(Pin->GetCPPTypeObject() == StaticEnum<ERigElementType>())
				{
					if(Pin->GetDefaultValue() == TEXT("Space"))
					{
						if(URigVMController* Controller = GetController(GraphToValidate))
						{
							Controller->SuspendNotifications(true);
							Controller->SetPinDefaultValue(Pin->GetPinPath(), TEXT("Null"), false, false, false);
							Controller->SuspendNotifications(false);
						}
					}
				}
			}

			if(URigVMFunctionReferenceNode* FunctionReferenceNode = Cast<URigVMFunctionReferenceNode>(Node))
			{
				if(URigVMLibraryNode* DependencyNode = FunctionReferenceNode->GetReferencedNode())
				{
					if(UControlRigBlueprint* DependencyBlueprint = DependencyNode->GetTypedOuter<UControlRigBlueprint>())
					{
						if(DependencyBlueprint != this)
						{
							DependencyBlueprint->GetLocalFunctionLibrary()->UpdateReferencesForReferenceNode(FunctionReferenceNode);
						}
					}
				}
			}
		}
	}	

	CompileLog.Messages.Reset();
	CompileLog.NumErrors = CompileLog.NumWarnings = 0;
	
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
	if(bIsCompiling)
	{
		return;
	}
	TGuardValue<bool> CompilingGuard(bIsCompiling, true);
	
	bErrorsDuringCompilation = false;

	UControlRigBlueprintGeneratedClass* RigClass = GetControlRigBlueprintGeneratedClass();
	UControlRig* CDO = Cast<UControlRig>(RigClass->GetDefaultObject(true /* create if needed */));
	if (CDO->VM != nullptr)
	{
		TGuardValue<bool> ReentrantGuardSelf(bSuspendModelNotificationsForSelf, true);
		TGuardValue<bool> ReentrantGuardOthers(bSuspendModelNotificationsForOthers, true);

		CDO->GetHierarchy()->CopyHierarchy(Hierarchy);

		if (CDO->VM->GetOuter() != CDO)
		{
			CDO->SetVM(NewObject<URigVM>(CDO, TEXT("VM")));
		}

		if (!HasAnyFlags(RF_Transient | RF_Transactional))
		{
			CDO->Modify(false);
		}
		CDO->VM->Reset();

		FRigNameCache TempNameCache;
		FRigUnitContext InitContext;
		InitContext.State = EControlRigState::Init;
		InitContext.Hierarchy = CDO->DynamicHierarchy;
		InitContext.NameCache = &TempNameCache;

		FRigUnitContext UpdateContext = InitContext;
		UpdateContext.State = EControlRigState::Update;

		void* InitContextPtr = &InitContext;
		void* UpdateContextPtr = &UpdateContext;

		TArray<FRigVMUserDataArray> UserData;
		UserData.Add(FRigVMUserDataArray(&InitContextPtr, 1));
		UserData.Add(FRigVMUserDataArray(&UpdateContextPtr, 1));

		CompileLog.Messages.Reset();
		CompileLog.NumErrors = CompileLog.NumWarnings = 0;

		URigVMCompiler* Compiler = URigVMCompiler::StaticClass()->GetDefaultObject<URigVMCompiler>();
		Compiler->Settings = VMCompileSettings;
		Compiler->Compile(Model, GetOrCreateController(), CDO->VM, CDO->GetExternalVariablesImpl(false), UserData, &PinToOperandMap);

		if (bErrorsDuringCompilation)
		{
			if(CDO->VM)
			{
				VMCompiledEvent.Broadcast(this, CDO->VM);
			}
			return;
		}

		CDO->Execute(EControlRigState::Init, FRigUnit_BeginExecution::EventName); // need to clarify if we actually need this
		Statistics = CDO->VM->GetStatistics();

		TArray<UObject*> ArchetypeInstances;
		CDO->GetArchetypeInstances(ArchetypeInstances);
		for (UObject* Instance : ArchetypeInstances)
		{
			if (UControlRig* InstanceRig = Cast<UControlRig>(Instance))
			{
				InstanceRig->InstantiateVMFromCDO();
			}
		}

		bVMRecompilationRequired = false;
		VMRecompilationBracket = 0;
		VMCompiledEvent.Broadcast(this, CDO->VM);

#if WITH_EDITOR
		RefreshControlRigBreakpoints();
#endif
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

void UControlRigBlueprint::HandleReportFromCompiler(EMessageSeverity::Type InSeverity, UObject* InSubject, const FString& InMessage)
{
	UObject* SubjectForMessage = InSubject;
	if(URigVMNode* ModelNode = Cast<URigVMNode>(SubjectForMessage))
	{
		if(UControlRigBlueprint* RigBlueprint = ModelNode->GetTypedOuter<UControlRigBlueprint>())
		{
			if(UControlRigGraph* EdGraph = Cast<UControlRigGraph>(RigBlueprint->GetEdGraph(ModelNode->GetGraph())))
			{
				if(UEdGraphNode* EdNode = EdGraph->FindNodeForModelNodeName(ModelNode->GetFName()))
				{
					SubjectForMessage = EdNode;
				}
			}
		}
	}

	FCompilerResultsLog* Log = CurrentMessageLog ? CurrentMessageLog : &CompileLog;
	if (InSeverity == EMessageSeverity::Error)
	{
		Status = BS_Error;
		MarkPackageDirty();

		if(InMessage.Contains(TEXT("@@")))
		{
			Log->Error(*InMessage, SubjectForMessage);
		}
		else
		{
			Log->Error(*InMessage);
		}

		BroadCastReportCompilerMessage(InSeverity, InSubject, InMessage);
		FScriptExceptionHandler::Get().HandleException(ELogVerbosity::Error, *InMessage, *FString());
		bErrorsDuringCompilation = true;
	}
	else if (InSeverity == EMessageSeverity::Warning)
	{
		if(InMessage.Contains(TEXT("@@")))
		{
			Log->Warning(*InMessage, SubjectForMessage);
		}
		else
		{
			Log->Warning(*InMessage);
		}

		BroadCastReportCompilerMessage(InSeverity, InSubject, InMessage);
		FScriptExceptionHandler::Get().HandleException(ELogVerbosity::Warning, *InMessage, *FString());
	}
	else
	{
		if(InMessage.Contains(TEXT("@@")))
		{
			Log->Note(*InMessage, SubjectForMessage);
		}
		else
		{
			Log->Note(*InMessage);
		}

		UE_LOG(LogControlRigDeveloper, Display, TEXT("%s"), *InMessage);
	}

	if (UControlRigGraphNode* EdGraphNode = Cast<UControlRigGraphNode>(SubjectForMessage))
	{
		EdGraphNode->ErrorType = (int32)InSeverity;
		EdGraphNode->ErrorMsg = InMessage;
		EdGraphNode->bHasCompilerMessage = EdGraphNode->ErrorType <= int32(EMessageSeverity::Info);
	}
}

#if WITH_EDITOR

void UControlRigBlueprint::ClearBreakpoints()
{
	RigVMBreakpointNodes.Empty();
	RefreshControlRigBreakpoints();
}

bool UControlRigBlueprint::AddBreakpoint(const FString& InBreakpointNodePath)
{
	URigVMLibraryNode* FunctionNode = nullptr;
	
	// Find the node in the graph
	URigVMNode* BreakpointNode = GetModel()->FindNode(InBreakpointNodePath);
	if (BreakpointNode == nullptr)
	{
		// If we cannot find the node, it might be because it is inside a function
		FString FunctionName = InBreakpointNodePath, Right;
		URigVMNode::SplitNodePathAtStart(InBreakpointNodePath, FunctionName, Right);

		// Look inside the local function library
		if (URigVMLibraryNode* LibraryNode = GetLocalFunctionLibrary()->FindFunction(FName(FunctionName)))
		{
			BreakpointNode = LibraryNode->GetContainedGraph()->FindNode(Right);
			FunctionNode = LibraryNode;
		}
	}

	return AddBreakpoint(BreakpointNode, FunctionNode);
}

bool UControlRigBlueprint::AddBreakpoint(URigVMNode* InBreakpointNode, URigVMLibraryNode* LibraryNode)
{
	if (InBreakpointNode == nullptr)
	{
		return false;
	}

	bool bSuccess = true;
	if (LibraryNode)
	{
		// If the breakpoint node is inside a library node, find all references to the library node
		TArray<TSoftObjectPtr<URigVMFunctionReferenceNode>> References = LibraryNode->GetLibrary()->GetReferencesForFunction(LibraryNode->GetFName());
		for (TSoftObjectPtr<URigVMFunctionReferenceNode> Reference : References)
		{
			if (!Reference.IsValid())
			{
				continue;
			}

			UControlRigBlueprint* ReferenceBlueprint = Reference->GetTypedOuter<UControlRigBlueprint>();

			// If the reference is not inside another function, add a breakpoint in the blueprint containing the
			// reference, without a function specified
			bool bIsInsideFunction = Reference->GetRootGraph()->IsA<URigVMFunctionLibrary>();
			if(!bIsInsideFunction)
			{
				bSuccess &= ReferenceBlueprint->AddBreakpoint(InBreakpointNode);
			}
			else
			{
				// Otherwise, we need to add breakpoints to all the blueprints that reference this
				// function (when the blueprint graph is flattened)
				
				// Get all the functions containing this reference
				URigVMNode* Node = Reference.Get();
				while (Node->GetGraph() != ReferenceBlueprint->GetLocalFunctionLibrary())
				{
					if (URigVMLibraryNode* ParentLibraryNode = Cast<URigVMLibraryNode>(Node->GetGraph()->GetOuter()))
					{
						// Recursively add breakpoints to the reference blueprint, specifying the parent function
						bSuccess &= ReferenceBlueprint->AddBreakpoint(InBreakpointNode, ParentLibraryNode);
					}

					Node = Cast<URigVMNode>(Node->GetGraph()->GetOuter());
				}
			}
		}
	}
	else
	{
		if (!RigVMBreakpointNodes.Contains(InBreakpointNode))
		{
			// Add the breakpoint to the VM
			bSuccess = AddBreakpointToControlRig(InBreakpointNode);
		}
	}

	return bSuccess;
}

bool UControlRigBlueprint::AddBreakpointToControlRig(URigVMNode* InBreakpointNode)
{
	UControlRigBlueprintGeneratedClass* RigClass = GetControlRigBlueprintGeneratedClass();
	UControlRig* CDO = Cast<UControlRig>(RigClass->GetDefaultObject(false));
	const FRigVMByteCode* ByteCode = GetController()->GetCurrentByteCode();
	TSet<FString> AddedCallpaths;

	if (CDO && ByteCode)
	{
		FRigVMInstructionArray Instructions = ByteCode->GetInstructions();	

		// For each instruction, see if the node is in the callpath
		// Only add one breakpoint for each callpath related to this node (i.e. if a node produces multiple
		// instructions, only add a breakpoint to the first instruction)
		for (int32 i = 0; i< Instructions.Num(); ++i)
		{
			const FRigVMASTProxy Proxy = FRigVMASTProxy::MakeFromCallPath(ByteCode->GetCallPathForInstruction(i), GetModel());
			if (Proxy.GetCallstack().Contains(InBreakpointNode))
			{
				// Find the callpath related to the breakpoint node
				FRigVMASTProxy BreakpointProxy = Proxy;
				while(BreakpointProxy.GetSubject() != InBreakpointNode)
				{
					BreakpointProxy = BreakpointProxy.GetParent();
				}
				const FString& BreakpointCallPath = BreakpointProxy.GetCallstack().GetCallPath();

				// Only add this callpath breakpoint once
				if (!AddedCallpaths.Contains(BreakpointCallPath))
				{
					AddedCallpaths.Add(BreakpointCallPath);
					CDO->AddBreakpoint(i, InBreakpointNode);
				}
			}
		}
	}

	if (AddedCallpaths.Num() > 0)
	{
		RigVMBreakpointNodes.AddUnique(InBreakpointNode);
		return true;
	}
	
	return false;
}

bool UControlRigBlueprint::RemoveBreakpoint(const FString& InBreakpointNodePath)
{
	// Find the node in the graph
	URigVMNode* BreakpointNode = GetModel()->FindNode(InBreakpointNodePath);
	if (BreakpointNode == nullptr)
	{
		// If we cannot find the node, it might be because it is inside a function
		FString FunctionName = InBreakpointNodePath, Right;
		URigVMNode::SplitNodePathAtStart(InBreakpointNodePath, FunctionName, Right);

		// Look inside the local function library
		if (URigVMLibraryNode* LibraryNode = GetLocalFunctionLibrary()->FindFunction(FName(FunctionName)))
		{
			BreakpointNode = LibraryNode->GetContainedGraph()->FindNode(Right);
		}
	}

	bool bSuccess = RemoveBreakpoint(BreakpointNode);

	// Remove the breakpoint from all the loaded dependent blueprints
	TArray<UControlRigBlueprint*> DependentBlueprints = GetDependentBlueprints(true, true);
	DependentBlueprints.Remove(this);
	for (UControlRigBlueprint* Dependent : DependentBlueprints)
	{
		bSuccess &= Dependent->RemoveBreakpoint(BreakpointNode);
	}
	return bSuccess;
}

bool UControlRigBlueprint::RemoveBreakpoint(URigVMNode* InBreakpointNode)
{
	if (RigVMBreakpointNodes.Contains(InBreakpointNode))
	{
		RigVMBreakpointNodes.Remove(InBreakpointNode);

		// Multiple breakpoint nodes might set a breakpoint to the same instruction. When we remove
		// one of the breakpoint nodes, we do not want to remove the instruction breakpoint if there
		// is another breakpoint node addressing it. For that reason, we just recompute all the
		// breakpoint instructions.
		// Refreshing breakpoints in the control rig will keep the state it had before.
		RefreshControlRigBreakpoints();
		return true;
	}

	return false;
}


void UControlRigBlueprint::RefreshControlRigBreakpoints()
{
	UControlRigBlueprintGeneratedClass* RigClass = GetControlRigBlueprintGeneratedClass();
	UControlRig* CDO = Cast<UControlRig>(RigClass->GetDefaultObject(false));
	CDO->GetDebugInfo().Clear();
	for (URigVMNode* Node : RigVMBreakpointNodes)
	{
		AddBreakpointToControlRig(Node);
	}
}

#endif

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

	if(InEdGraph->GetOutermost() != GetOutermost())
	{
		return nullptr;
	}

#if WITH_EDITORONLY_DATA
	if (InEdGraph == FunctionLibraryEdGraph)
	{
		return FunctionLibrary;
	}
#endif

	const UControlRigGraph* RigGraph = Cast< UControlRigGraph>(InEdGraph);
	check(RigGraph);

	FString ModelNodePath = RigGraph->ModelNodePath;

	if (RigGraph->bIsFunctionDefinition)
	{
		if (URigVMLibraryNode* LibraryNode = FunctionLibrary->FindFunction(*ModelNodePath))
		{
			return LibraryNode->GetContainedGraph();
		}
	}

	if (RigGraph->GetOuter() == this)
	{
		return Model;
	}

	ensure(!ModelNodePath.IsEmpty());

	URigVMGraph* SubModel = Model;
	if (ModelNodePath.StartsWith(FunctionLibrary->GetNodePath()))
	{
		SubModel = FunctionLibrary;
		ModelNodePath = ModelNodePath.Right(ModelNodePath.Len() - FunctionLibrary->GetNodePath().Len() - 1);
	}

	while (!ModelNodePath.IsEmpty())
	{
		FString NodeName = ModelNodePath;
		if (NodeName.Contains(TEXT("|")))
		{
			NodeName = NodeName.Left(NodeName.Find(TEXT("|")));
			ModelNodePath = ModelNodePath.Right(ModelNodePath.Len() - NodeName.Len() - 1);
		}
		else
		{
			ModelNodePath.Reset();
		}

		URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(SubModel->FindNodeByName(*NodeName));
		if (CollapseNode == nullptr)
		{
			return nullptr;
		}

		SubModel = CollapseNode->GetContainedGraph();
	}

	return SubModel;
}

URigVMGraph* UControlRigBlueprint::GetModel(const FString& InNodePath) const
{
	if (!InNodePath.IsEmpty())
	{
		if (URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(Model->FindNode(InNodePath)))
		{
			return LibraryNode->GetContainedGraph();
		}
		return nullptr;
	}
	return Model;
}


TArray<URigVMGraph*> UControlRigBlueprint::GetAllModels() const
{
	TArray<URigVMGraph*> Models;
	Models.Add(GetModel());
	Models.Append(GetModel()->GetContainedGraphs(true /* recursive */));
	Models.Add(GetLocalFunctionLibrary());
	Models.Append(GetLocalFunctionLibrary()->GetContainedGraphs(true /* recursive */));
	return Models;
}

URigVMFunctionLibrary* UControlRigBlueprint::GetLocalFunctionLibrary() const
{
	return FunctionLibrary;
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
	Controller->GetExternalVariablesDelegate.BindLambda([](URigVMGraph* InGraph) -> TArray<FRigVMExternalVariable> {

		if (InGraph)
		{
			if(UControlRigBlueprint* Blueprint = InGraph->GetTypedOuter<UControlRigBlueprint>())
			{
				if (UControlRigBlueprintGeneratedClass* RigClass = Blueprint->GetControlRigBlueprintGeneratedClass())
				{
                    if (UControlRig* CDO = Cast<UControlRig>(RigClass->GetDefaultObject(true /* create if needed */)))
                    {
                        return CDO->GetExternalVariablesImpl(true /* rely on variables within blueprint */);
                    }
                }
			}
		}
		return TArray<FRigVMExternalVariable>();

	});


	// this delegate is used by the controller to retrieve the current bytecode of the VM
	Controller->GetCurrentByteCodeDelegate.BindLambda([WeakThis]() -> const FRigVMByteCode* {

		if (WeakThis.IsValid())
		{
			if (UControlRigBlueprintGeneratedClass* RigClass = WeakThis->GetControlRigBlueprintGeneratedClass())
			{
				if (UControlRig* CDO = Cast<UControlRig>(RigClass->GetDefaultObject(false)))
				{
					if (CDO->VM)
					{
						return &CDO->VM->GetByteCode();
					}
				}
			}
		}
		return nullptr;

	});

	Controller->IsFunctionAvailableDelegate.BindLambda([WeakThis](URigVMLibraryNode* InFunction) -> bool
	{
		if(InFunction == nullptr)
		{
			return false;
		}
		
		if(URigVMFunctionLibrary* Library = Cast<URigVMFunctionLibrary>(InFunction->GetOuter()))
		{
			if(UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(Library->GetOuter()))
			{
				if(Blueprint->IsFunctionPublic(InFunction->GetFName()))
				{
					return true;
				}

				// if it is private - we still see it as public if we are within the same blueprint 
				if(WeakThis.IsValid())
				{
					if(WeakThis.Get() == Blueprint)
					{
						return true;
					}
				}
			}
		}

		return false;
	});

	Controller->IsDependencyCyclicDelegate.BindLambda([WeakThis](UObject* InDependentObject, UObject* InDependencyObject) -> bool
	{
	    if(InDependentObject == nullptr || InDependencyObject == nullptr)
	    {
	        return false;
	    }

		UControlRigBlueprint* DependentBlueprint = InDependentObject->GetTypedOuter<UControlRigBlueprint>();
		UControlRigBlueprint* DependencyBlueprint = InDependencyObject->GetTypedOuter<UControlRigBlueprint>();

		if(DependentBlueprint == nullptr || DependencyBlueprint == nullptr)
		{
			return false;
		}

		if(DependentBlueprint == DependencyBlueprint)
		{
			return false;
		}

		const TArray<UControlRigBlueprint*> DependencyDependencies = DependencyBlueprint->GetDependencies(true);
		return DependencyDependencies.Contains(DependentBlueprint);
	});

#if WITH_EDITOR

	// this sets up three delegates:
	// a) get external variables (mapped to Controller->GetExternalVariables)
	// b) bind pin to variable (mapped to Controller->BindPinToVariable)
	// c) create external variable (mapped to the passed in tfunction)
	// the last one is defined within the blueprint since the controller
	// doesn't own the variables and can't create one itself.
	Controller->SetupDefaultUnitNodeDelegates(TDelegate<FName(FRigVMExternalVariable, FString)>::CreateLambda(
		[WeakThis](FRigVMExternalVariable InVariableToCreate, FString InDefaultValue) -> FName {
			if (WeakThis.IsValid())
			{
				return WeakThis->AddCRMemberVariableFromExternal(InVariableToCreate, InDefaultValue);
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

URigVMGraph* UControlRigBlueprint::GetTemplateModel()
{
#if WITH_EDITORONLY_DATA
	if (TemplateModel == nullptr)
	{
		TemplateModel = NewObject<URigVMGraph>(this, TEXT("TemplateModel"));
		TemplateModel->SetFlags(RF_Transient);
	}
	return TemplateModel;
#else
	return nullptr;
#endif
}

URigVMController* UControlRigBlueprint::GetTemplateController()
{
#if WITH_EDITORONLY_DATA
	if (TemplateController == nullptr)
	{
		TemplateController = NewObject<URigVMController>(this, TEXT("TemplateController"));
		TemplateController->SetExecuteContextStruct(FControlRigExecuteContext::StaticStruct());
		TemplateController->SetGraph(GetTemplateModel());
		TemplateController->EnableReporting(false);
		TemplateController->SetFlags(RF_Transient);
	}
	return TemplateController;
#else
	return nullptr;
#endif
}

UEdGraph* UControlRigBlueprint::GetEdGraph(URigVMGraph* InModel) const
{
	if (InModel == nullptr)
	{
		return nullptr;
	}

	if(InModel->GetOutermost() != GetOutermost())
	{
		return nullptr;
	}

#if WITH_EDITORONLY_DATA
	if (InModel == FunctionLibrary)
	{
		return FunctionLibraryEdGraph;
	}
#endif

	TArray<UEdGraph*> EdGraphs;
	GetAllGraphs(EdGraphs);

	bool bIsFunctionDefinition = false;
	if (URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(InModel->GetOuter()))
	{
		bIsFunctionDefinition = LibraryNode->GetGraph()->IsA<URigVMFunctionLibrary>();
	}

	for (UEdGraph* EdGraph : EdGraphs)
	{
		if (UControlRigGraph* RigGraph = Cast<UControlRigGraph>(EdGraph))
		{
			if (RigGraph->bIsFunctionDefinition != bIsFunctionDefinition)
			{
				continue;
			}

			if (RigGraph->ModelNodePath == InModel->GetNodePath())
			{
				return RigGraph;
			}
		}
	}
	return nullptr;
}

UEdGraph* UControlRigBlueprint::GetEdGraph(const FString& InNodePath) const
{
	if (URigVMGraph* ModelForNodePath = GetModel(InNodePath))
	{
		return GetEdGraph(ModelForNodePath);
	}
	return nullptr;
}

bool UControlRigBlueprint::IsFunctionPublic(const FName& InFunctionName) const
{
	for(const FControlRigPublicFunctionData& PublicFunction : PublicFunctions)
	{
		if(PublicFunction.Name == InFunctionName)
		{
			return true;
		}
	}
	
	return false;
}

void UControlRigBlueprint::MarkFunctionPublic(const FName& InFunctionName, bool bIsPublic)
{
	if(IsFunctionPublic(InFunctionName) == bIsPublic)
	{
		return;
	}
	
	Modify();

	if(bIsPublic)
	{
		if(URigVMLibraryNode* FunctionNode = GetLocalFunctionLibrary()->FindFunction(InFunctionName))
		{
			if(UControlRigGraph* RigGraph = Cast<UControlRigGraph>(GetEdGraph(FunctionNode->GetContainedGraph())))
			{
				const FControlRigPublicFunctionData NewFunctionData = RigGraph->GetPublicFunctionData();
				for(FControlRigPublicFunctionData& ExistingFunctionData : PublicFunctions)
				{
					if(ExistingFunctionData.Name == NewFunctionData.Name)
					{
						ExistingFunctionData = NewFunctionData;
						return;
					}
				}
				PublicFunctions.Add(NewFunctionData);
			}
		}
	}
	else
	{
		for(int32 Index = 0; Index < PublicFunctions.Num(); Index++)
		{
			if(PublicFunctions[Index].Name == InFunctionName)
			{
				PublicFunctions.RemoveAt(Index);
				return;
			}
		}
	}
}

TArray<UControlRigBlueprint*> UControlRigBlueprint::GetDependencies(bool bRecursive) const
{
	TArray<UControlRigBlueprint*> Dependencies;

	TArray<URigVMGraph*> Graphs = GetAllModels();
	for(URigVMGraph* Graph : Graphs)
	{
		for(URigVMNode* Node : Graph->GetNodes())
		{
			if(URigVMFunctionReferenceNode* FunctionReferenceNode = Cast<URigVMFunctionReferenceNode>(Node))
			{
				if(URigVMLibraryNode* LibraryNode = FunctionReferenceNode->GetReferencedNode())
				{
					if(UControlRigBlueprint* DependencyBlueprint = LibraryNode->GetTypedOuter<UControlRigBlueprint>())
					{
						if(DependencyBlueprint != this)
						{
							if(!Dependencies.Contains(DependencyBlueprint))
							{
								Dependencies.Add(DependencyBlueprint);

								if(bRecursive)
								{
									TArray<UControlRigBlueprint*> ChildDependencies = DependencyBlueprint->GetDependencies(true);
									for(UControlRigBlueprint* ChildDependency : ChildDependencies)
									{
										Dependencies.AddUnique(ChildDependency);
									}
								}
							}
						}
					}
				}
			}
		}
	}

	return Dependencies;
}

TArray<FAssetData> UControlRigBlueprint::GetDependentAssets() const
{
	TArray<FAssetData> Dependents;
	TArray<FName> AssetPaths;

	if(FunctionLibrary)
	{
		const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<URigVMLibraryNode*> Functions = FunctionLibrary->GetFunctions();
		for(URigVMLibraryNode* Function : Functions)
		{
			const FName FunctionName = Function->GetFName();
			if(IsFunctionPublic(FunctionName))
			{
				TArray<TSoftObjectPtr<URigVMFunctionReferenceNode>> References = FunctionLibrary->GetReferencesForFunction(FunctionName);
				for(const TSoftObjectPtr<URigVMFunctionReferenceNode>& Reference : References)
				{
					const FName AssetPath = Reference.ToSoftObjectPath().GetAssetPathName();
					if(AssetPath.ToString().StartsWith(TEXT("/Engine/Transient")))
					{
						continue;
					}
					
					if(!AssetPaths.Contains(AssetPath))
					{
						AssetPaths.Add(AssetPath);

						const FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(*AssetPath.ToString());
						if(AssetData.IsValid())
						{
							Dependents.Add(AssetData);
						}
					}
				}
			}
		}
	}

	return Dependents;
}

TArray<UControlRigBlueprint*> UControlRigBlueprint::GetDependentBlueprints(bool bRecursive, bool bOnlyLoaded) const
{
	TArray<FAssetData> Assets = GetDependentAssets();
	TArray<UControlRigBlueprint*> Dependents;

	for(const FAssetData& Asset : Assets)
	{
		if (!bOnlyLoaded || Asset.IsAssetLoaded())
		{
			if(UControlRigBlueprint* Dependent = Cast<UControlRigBlueprint>(Asset.GetAsset()))
			{
				if(!Dependents.Contains(Dependent))
				{
					Dependents.Add(Dependent);

					if(bRecursive && Dependent != this)
					{
						TArray<UControlRigBlueprint*> ParentDependents = Dependent->GetDependentBlueprints(true);
						for(UControlRigBlueprint* ParentDependent : ParentDependents)
						{
							Dependents.AddUnique(ParentDependent);
						}
					}
				}
			}
		}
	}

	return Dependents;
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

			PropagateHierarchyFromBPToInstances();

			// make sure the bone name list is up 2 date for the editor graph
			for (UEdGraph* Graph : UbergraphPages)
			{
				UControlRigGraph* RigGraph = Cast<UControlRigGraph>(Graph);
				if (RigGraph == nullptr)
				{
					continue;
				}
				RigGraph->CacheNameLists(Hierarchy, &DrawContainer);
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

void UControlRigBlueprint::ReplaceDeprecatedNodes()
{
	TArray<UEdGraph*> EdGraphs;
	GetAllGraphs(EdGraphs);

	for (UEdGraph* EdGraph : EdGraphs)
	{
		if (UControlRigGraph* RigGraph = Cast<UControlRigGraph>(EdGraph))
		{
			RigGraph->Schema = UControlRigGraphSchema::StaticClass();
		}
	}

	Super::ReplaceDeprecatedNodes();
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

#if WITH_EDITOR

FName UControlRigBlueprint::AddMemberVariable(const FName& InName, const FString& InCPPType, bool bIsPublic, bool bIsReadOnly, FString InDefaultValue)
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

	FName Result = AddCRMemberVariableFromExternal(Variable, InDefaultValue);
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
				SpaceKey = DefaultStruct->DetermineSpaceForPin(Right, Hierarchy);
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

	return RemovedName;
}

FName UControlRigBlueprint::AddTransientControl(const FRigElementKey& InElement)
{
	TUniquePtr<FControlValueScope> ValueScope;
	if (!UControlRigSettings::Get()->bResetControlsOnPinValueInteraction) // if we need to retain the controls
	{
		ValueScope = MakeUnique<FControlValueScope>(this);
	}

	UControlRigBlueprintGeneratedClass* RigClass = GetControlRigBlueprintGeneratedClass();
	UControlRig* CDO = Cast<UControlRig>(RigClass->GetDefaultObject(true /* create if needed */));

	FName ReturnName = NAME_None;
	TArray<UObject*> ArchetypeInstances;
	CDO->GetArchetypeInstances(ArchetypeInstances);

	// hierarchy transforms will be reset when ClearTransientControls() is called,
	// so to retain any bone transform modifications we have to save them
	TMap<UObject*, FTransform> SavedElementLocalTransforms;
	for (UObject* ArchetypeInstance : ArchetypeInstances)
	{
		UControlRig* InstancedControlRig = Cast<UControlRig>(ArchetypeInstance);
		if (InstancedControlRig)
		{
			if (InstancedControlRig->DynamicHierarchy)
			{ 
				SavedElementLocalTransforms.FindOrAdd(InstancedControlRig) = InstancedControlRig->DynamicHierarchy->GetLocalTransform(InElement);
			}
		}
	}

	// for now we only allow one pin control at the same time
	ClearTransientControls();
	
	for (UObject* ArchetypeInstance : ArchetypeInstances)
	{
		UControlRig* InstancedControlRig = Cast<UControlRig>(ArchetypeInstance);
		if (InstancedControlRig)
		{
			// restore the element transforms so that transient controls are created at the right place
			if (const FTransform* SavedTransform = SavedElementLocalTransforms.Find(InstancedControlRig))
			{
				if (InstancedControlRig->DynamicHierarchy)
				{ 
					InstancedControlRig->DynamicHierarchy->SetLocalTransform(InElement, *SavedTransform);
				}
			}
			
			FName ControlName = InstancedControlRig->AddTransientControl(InElement);
			if (ReturnName == NAME_None)
			{
				ReturnName = ControlName;
			}
		}
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

	TArray<UObject*> ArchetypeInstances;
	CDO->GetArchetypeInstances(ArchetypeInstances);
	for (UObject* ArchetypeInstance : ArchetypeInstances)
	{
		UControlRig* InstancedControlRig = Cast<UControlRig>(ArchetypeInstance);
		if (InstancedControlRig)
		{
			InstancedControlRig->ClearTransientControls();
		}
	}
}

void UControlRigBlueprint::SetTransientControlValue(const FRigElementKey& InElement)
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
			InstancedControlRig->SetTransientControlValue(InElement);
		}
	}
}

#endif

void UControlRigBlueprint::PopulateModelFromGraphForBackwardsCompatibility(UControlRigGraph* InGraph)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()
	
	

	// temporarily disable default value validation during load time, serialized values should always be accepted
	TGuardValue<bool> DisablePinDefaultValueValidation(GetOrCreateController()->bValidatePinDefaults, false);

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

	TArray<UEdGraph*> EdGraphs;
	GetAllGraphs(EdGraphs);

	for (UEdGraph* Graph : EdGraphs)
	{
		TArray<UEdGraphNode*> Nodes = Graph->Nodes;
		for (UEdGraphNode* Node : Nodes)
		{
			Graph->RemoveNode(Node);
		}

		if (UControlRigGraph* RigGraph = Cast<UControlRigGraph>(Graph))
		{
			if (RigGraph->bIsFunctionDefinition)
			{
				FunctionGraphs.Remove(RigGraph);
			}
		}
	}

	TArray<URigVMGraph*> RigGraphs;
	RigGraphs.Add(GetModel());
	RigGraphs.Add(GetLocalFunctionLibrary());

	GetOrCreateController(RigGraphs[0])->ResendAllNotifications();
	GetOrCreateController(RigGraphs[1])->ResendAllNotifications();

	for (int32 RigGraphIndex = 0; RigGraphIndex < RigGraphs.Num(); RigGraphIndex++)
	{
		URigVMGraph* RigGraph = RigGraphs[RigGraphIndex];

		for (URigVMNode* RigNode : RigGraph->GetNodes())
		{
			if (URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(RigNode))
			{
				CreateEdGraphForCollapseNodeIfNeeded(CollapseNode, true);
				RigGraphs.Add(CollapseNode->GetContainedGraph());
			}
		}
	}

	EdGraphs.Reset();
	GetAllGraphs(EdGraphs);

	for (UEdGraph* Graph : EdGraphs)
	{
		if (UControlRigGraph* RigGraph = Cast<UControlRigGraph>(Graph))
		{
			RigGraph->CacheNameLists(Hierarchy, &DrawContainer);
		}
	}

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

	// since it's possible that a notification will be already sent / forwarded to the
	// listening objects within the switch statement below - we keep a flag to mark
	// the notify for still pending (or already sent)
	bool bNotifForOthersPending = true;

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
						FRigVMASTProxy RootPinProxy = FRigVMASTProxy::MakeFromUObject(RootPin);
						if(const FRigVMExprAST* Expression = InGraph->GetRuntimeAST()->GetExprForSubject(RootPinProxy))
						{
							bRequiresRecompile = Expression->NumParents() > 1;
						}
						else
						{
							bRequiresRecompile = true;
						}

						// If we are only changing a pin's default value, we need to
						// check if there is a connection to a sub-pin of the root pin
						// that has its value is directly stored in the root pin due to optimization, if so,
						// we want to recompile to make sure the pin's new default value and values from other connections
						// are both applied to the root pin because GetDefaultValue() alone cannot account for values
						// from other connections.
						if(!bRequiresRecompile)
						{
							TArray<URigVMPin*> SourcePins = RootPin->GetLinkedSourcePins(true);
							for (const URigVMPin* SourcePin : SourcePins)
							{
								// check if the source node is optimized out, if so, only a recompile will allows us
								// to re-query its value.
								FRigVMASTProxy SourceNodeProxy = FRigVMASTProxy::MakeFromUObject(SourcePin->GetNode());
								if (InGraph->GetRuntimeAST()->GetExprForSubject(SourceNodeProxy) == nullptr)
								{
									bRequiresRecompile = true;
									break;
								}
							}
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
									Hierarchy->ForEach<FRigControlElement>([this, InstanceRig, Pin](FRigControlElement* ControlElement) -> bool
                                    {
										if(!ControlElement->Settings.bIsTransientControl)
										{
											return true;
										}
										
										if (URigVMPin* ControlledPin = Model->FindPin(ControlElement->GetName().ToString()))
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
											return false;
										}

										return true;
									});
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
			{
				if (InNotifType == ERigVMGraphNotifType::NodeRemoved)
				{
					if (URigVMNode* RigVMNode = Cast<URigVMNode>(InSubject))
					{
						RemoveBreakpoint(RigVMNode);
					}
				}
					
				if (URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(InSubject))
				{
					if (InNotifType == ERigVMGraphNotifType::NodeAdded)
					{
						CreateEdGraphForCollapseNodeIfNeeded(CollapseNode);
					}
					else
					{
						bNotifForOthersPending = !RemoveEdGraphForCollapseNode(CollapseNode, true);
					}

					ClearTransientControls();
					RequestAutoVMRecompilation();

					if(CollapseNode->GetOuter()->IsA<URigVMFunctionLibrary>())
					{
						for(int32 Index = 0; Index < PublicFunctions.Num(); Index++)
						{
							if(PublicFunctions[Index].Name == CollapseNode->GetFName())
							{
								Modify();
								PublicFunctions.RemoveAt(Index);
							}
						}
					}

					MarkPackageDirty();
					FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(this);
					break;
				}

				// fall through to the next case
			}
			case ERigVMGraphNotifType::LinkAdded:
			case ERigVMGraphNotifType::LinkRemoved:
			case ERigVMGraphNotifType::PinArraySizeChanged:
			case ERigVMGraphNotifType::PinDirectionChanged:
			{
				ClearTransientControls();
				RequestAutoVMRecompilation();
				MarkPackageDirty();
				FBlueprintEditorUtils::MarkBlueprintAsModified(this);
				break;
			}
			case ERigVMGraphNotifType::PinWatchedChanged:
			case ERigVMGraphNotifType::PinTypeChanged:
			case ERigVMGraphNotifType::PinIndexChanged:
			{
				if (URigVMPin* ModelPin = Cast<URigVMPin>(InSubject))
				{
					if (UEdGraph* EdGraph = GetEdGraph(InGraph))
					{							
						if (UControlRigGraph* Graph = Cast<UControlRigGraph>(EdGraph))
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
			case ERigVMGraphNotifType::VariableRemappingChanged:
			{
				RequestAutoVMRecompilation();
				MarkPackageDirty();
				break;
			}
			case ERigVMGraphNotifType::NodeRenamed:
			{
				if (URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(InSubject))
				{
					FString NewNodePath = CollapseNode->GetNodePath(true /* recursive */);
					FString Left, Right = NewNodePath;
					URigVMNode::SplitNodePathAtEnd(NewNodePath, Left, Right);
					FString OldNodePath = CollapseNode->GetPreviousFName().ToString();
					if (!Left.IsEmpty())
					{
						OldNodePath = URigVMNode::JoinNodePath(Left, OldNodePath);
					}

					FString NewNodePathPrefix = NewNodePath + TEXT("|");
					FString OldNodePathPrefix = OldNodePath + TEXT("|");

					TArray<UEdGraph*> EdGraphs;
					GetAllGraphs(EdGraphs);

					for (UEdGraph* EdGraph : EdGraphs)
					{
						if (UControlRigGraph* RigGraph = Cast<UControlRigGraph>(EdGraph))
						{
							if (RigGraph->ModelNodePath == OldNodePath)
							{
								RigGraph->ModelNodePath = NewNodePath;
							}
							else if (RigGraph->ModelNodePath.StartsWith(OldNodePathPrefix))
							{
								RigGraph->ModelNodePath = NewNodePathPrefix + RigGraph->ModelNodePath.LeftChop(OldNodePathPrefix.Len());
							}
						}
					}

					if (UEdGraph* ContainedEdGraph = GetEdGraph(CollapseNode->GetContainedGraph()))
					{
						ContainedEdGraph->Rename(*CollapseNode->GetEditorSubGraphName(), nullptr);
					}

					if(CollapseNode->GetOuter()->IsA<URigVMFunctionLibrary>())
					{
						for(int32 Index = 0; Index < PublicFunctions.Num(); Index++)
						{
							if(PublicFunctions[Index].Name == CollapseNode->GetPreviousFName())
							{
								Modify();
								PublicFunctions[Index].Name = CollapseNode->GetFName();
							}
						}
					}

					FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(this);
				}
				break;
			}
			case ERigVMGraphNotifType::NodeCategoryChanged:
			case ERigVMGraphNotifType::NodeKeywordsChanged:
			{
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(this);
				break;
			}
			default:
			{
				break;
			}
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
				int32 VariableIndex = AddCRMemberVariable(this, VarName, PinType, false, false, FString());
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
				int32 VariableIndex = AddCRMemberVariable(this, VarName, PinType, true, !Description.bIsInput, FString());
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

int32 UControlRigBlueprint::AddCRMemberVariable(UControlRigBlueprint* InBlueprint, const FName& InVarName, FEdGraphPinType InVarType, bool bIsPublic, bool bIsReadOnly, FString InDefaultValue)
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

	NewVar.DefaultValue = InDefaultValue;

	return InBlueprint->NewVariables.Add(NewVar);
}

FName UControlRigBlueprint::AddCRMemberVariableFromExternal(FRigVMExternalVariable InVariableToCreate, FString InDefaultValue)
{
	FEdGraphPinType PinType = UControlRig::GetPinTypeFromExternalVariable(InVariableToCreate);
	if (!PinType.PinCategory.IsValid())
	{
		return NAME_None;
	}

	Modify();

	TSharedPtr<FKismetNameValidator> NameValidator = MakeShareable(new FKismetNameValidator(this, NAME_None, nullptr));
	FName VarName = FindCRMemberVariableUniqueName(NameValidator, InVariableToCreate.Name.ToString());
	int32 VariableIndex = AddCRMemberVariable(this, VarName, PinType, InVariableToCreate.bIsPublic, InVariableToCreate.bIsReadOnly, InDefaultValue);
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
	Hierarchy->CopyPose(InControlRig->GetHierarchy(), true, true);
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
					InstanceRig->GetHierarchy()->CopyPose(Hierarchy, true, true);
				}
			}
		}
	}
}

void UControlRigBlueprint::PropagateHierarchyFromBPToInstances()
{
	if (UClass* MyControlRigClass = GeneratedClass)
	{
		if (UControlRig* DefaultObject = Cast<UControlRig>(MyControlRigClass->GetDefaultObject(false)))
		{
			DefaultObject->GetHierarchy()->CopyHierarchy(Hierarchy);
			DefaultObject->Initialize(true);

			TArray<UObject*> ArchetypeInstances;
			DefaultObject->GetArchetypeInstances(ArchetypeInstances);
			for (UObject* ArchetypeInstance : ArchetypeInstances)
			{
				if (UControlRig* InstanceRig = Cast<UControlRig>(ArchetypeInstance))
				{
					InstanceRig->GetHierarchy()->CopyHierarchy(Hierarchy);
					InstanceRig->Initialize(true);
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
		RigGraph->CacheNameLists(Hierarchy, &DrawContainer);
	}
}

void UControlRigBlueprint::PropagatePropertyFromBPToInstances(FRigElementKey InRigElement, const FProperty* InProperty)
{
	int32 ElementIndex = Hierarchy->GetIndex(InRigElement);
	ensure(ElementIndex != INDEX_NONE);
	check(InProperty);

	if (UClass* MyControlRigClass = GeneratedClass)
	{
		if (UControlRig* DefaultObject = Cast<UControlRig>(MyControlRigClass->GetDefaultObject(false)))
		{
			TArray<UObject*> ArchetypeInstances;
			DefaultObject->GetArchetypeInstances(ArchetypeInstances);

			const int32 PropertyOffset = InProperty->GetOffset_ReplaceWith_ContainerPtrToValuePtr();
			const int32 PropertySize = InProperty->GetSize();

			uint8* Source = ((uint8*)Hierarchy->Get(ElementIndex)) + PropertyOffset;
			for (UObject* ArchetypeInstance : ArchetypeInstances)
			{
				if (UControlRig* InstanceRig = Cast<UControlRig>(ArchetypeInstance))
				{
					uint8* Dest = ((uint8*)InstanceRig->GetHierarchy()->Get(ElementIndex)) + PropertyOffset;
					FMemory::Memcpy(Dest, Source, PropertySize);
				}
			}
		}
	}
}

void UControlRigBlueprint::PropagatePropertyFromInstanceToBP(FRigElementKey InRigElement, const FProperty* InProperty, UControlRig* InInstance)
{
	const int32 ElementIndex = Hierarchy->GetIndex(InRigElement);
	ensure(ElementIndex != INDEX_NONE);
	check(InProperty);

	const int32 PropertyOffset = InProperty->GetOffset_ReplaceWith_ContainerPtrToValuePtr();
	const int32 PropertySize = InProperty->GetSize();
	uint8* Source = ((uint8*)InInstance->GetHierarchy()->Get(ElementIndex)) + PropertyOffset;
	uint8* Dest = ((uint8*)Hierarchy->Get(ElementIndex)) + PropertyOffset;
	FMemory::Memcpy(Dest, Source, PropertySize);
}


void UControlRigBlueprint::HandleHierarchyModified(ERigHierarchyNotification InNotification, URigHierarchy* InHierarchy, const FRigBaseElement* InElement)
{
#if WITH_EDITOR

	if(bSuspendAllNotifications)
	{
		return;
	}

	switch(InNotification)
	{
		case ERigHierarchyNotification::ElementRemoved:
		{
			Modify();
			Influences.OnKeyRemoved(InElement->GetKey());
			PropagateHierarchyFromBPToInstances();
			break;
		}
		case ERigHierarchyNotification::ElementRenamed:
		{
			Modify();
			Influences.OnKeyRenamed(FRigElementKey(InHierarchy->GetPreviousName(InElement->GetKey()), InElement->GetType()), InElement->GetKey());
			PropagateHierarchyFromBPToInstances();
			break;
		}
		case ERigHierarchyNotification::ElementAdded:
		case ERigHierarchyNotification::ParentChanged:
		case ERigHierarchyNotification::HierarchyReset:
		{
			Modify();
			PropagateHierarchyFromBPToInstances();
			break;
		}
		case ERigHierarchyNotification::ElementSelected:
		{
			bool bClearTransientControls = true;
			if (const FRigControlElement* ControlElement = Cast<FRigControlElement>(InElement))
			{
				if (ControlElement->Settings.bIsTransientControl)
				{
					bClearTransientControls = false;
				}
			}

			if(bClearTransientControls)
			{
				ClearTransientControls();
			}
			break;
		}
		default:
		{
			break;
		}
	}
	
#endif
}

UControlRigBlueprint::FControlValueScope::FControlValueScope(UControlRigBlueprint* InBlueprint)
: Blueprint(InBlueprint)
{
#if WITH_EDITOR
	check(Blueprint);

	if (UControlRig* CR = Cast<UControlRig>(Blueprint->GetObjectBeingDebugged()))
	{
		TArray<FRigControlElement*> Controls = CR->AvailableControls();
		for (FRigControlElement* ControlElement : Controls)
		{
			ControlValues.Add(ControlElement->GetName(), CR->GetControlValue(ControlElement->GetName()));
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
		// we use the storage within the CDO for the default values,
		// no need to maintain the default value as a string
		NewVariables[VarIndex].DefaultValue = FString();

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
	TArray<URigVMGraph*> AllGraphs = GetAllModels();
	for (URigVMGraph* Graph : AllGraphs)
	{
		if (URigVMController* Controller = GetOrCreateController(Graph))
		{
			Controller->OnExternalVariableRemoved(InVarName, true);
		}
	}
	BroadcastExternalVariablesChangedEvent();
}

void UControlRigBlueprint::OnVariableRenamed(const FName& InOldVarName, const FName& InNewVarName)
{
	TArray<URigVMGraph*> AllGraphs = GetAllModels();
	for (URigVMGraph* Graph : AllGraphs)
	{
		if (URigVMController* Controller = GetOrCreateController(Graph))
		{
			Controller->OnExternalVariableRenamed(InOldVarName, InNewVarName, true);
		}
	}
	BroadcastExternalVariablesChangedEvent();
}

void UControlRigBlueprint::OnVariableTypeChanged(const FName& InVarName, FEdGraphPinType InOldPinType, FEdGraphPinType InNewPinType)
{
	TArray<URigVMGraph*> AllGraphs = GetAllModels();
	for (URigVMGraph* Graph : AllGraphs)
	{
		if (URigVMController* Controller = GetOrCreateController(Graph))
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

void UControlRigBlueprint::BroadcastNodeDoubleClicked(URigVMNode* InNode)
{
	NodeDoubleClickedEvent.Broadcast(this, InNode);
}

void UControlRigBlueprint::BroadcastGraphImported(UEdGraph* InGraph)
{
	GraphImportedEvent.Broadcast(InGraph);
}

void UControlRigBlueprint::BroadcastPostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent)
{
	PostEditChangeChainPropertyEvent.Broadcast(PropertyChangedChainEvent);
}

void UControlRigBlueprint::BroadcastRequestLocalizeFunctionDialog(URigVMLibraryNode* InFunction, bool bForce)
{
	RequestLocalizeFunctionDialog.Broadcast(InFunction, this, bForce);
}

void UControlRigBlueprint::BroadCastReportCompilerMessage(EMessageSeverity::Type InSeverity, UObject* InSubject, const FString& InMessage)
{
	ReportCompilerMessageEvent.Broadcast(InSeverity, InSubject, InMessage);
}

#endif

void UControlRigBlueprint::CreateEdGraphForCollapseNodeIfNeeded(URigVMCollapseNode* InNode, bool bForce)
{
	check(InNode);

	if (bForce)
	{
		RemoveEdGraphForCollapseNode(InNode, false);
	}

	if (InNode->GetGraph()->IsA<URigVMFunctionLibrary>())
	{
		if (URigVMGraph* ContainedGraph = InNode->GetContainedGraph())
		{
			bool bFunctionGraphExists = false;
			for (UEdGraph* FunctionGraph : FunctionGraphs)
			{
				if (UControlRigGraph* RigFunctionGraph = Cast<UControlRigGraph>(FunctionGraph))
				{
					if (RigFunctionGraph->ModelNodePath == ContainedGraph->GetNodePath())
					{
						bFunctionGraphExists = true;
						break;
					}
				}
			}

			if (!bFunctionGraphExists)
			{
				// create a sub graph
				UControlRigGraph* RigFunctionGraph = NewObject<UControlRigGraph>(this, *InNode->GetName(), RF_Transactional);
				RigFunctionGraph->Schema = UControlRigGraphSchema::StaticClass();
				RigFunctionGraph->bAllowRenaming = 1;
				RigFunctionGraph->bEditable = 1;
				RigFunctionGraph->bAllowDeletion = 1;
				RigFunctionGraph->ModelNodePath = ContainedGraph->GetNodePath();
				RigFunctionGraph->bIsFunctionDefinition = true;

				FunctionGraphs.Add(RigFunctionGraph);

				RigFunctionGraph->Initialize(this);

				GetOrCreateController(ContainedGraph)->ResendAllNotifications();
			}

		}
	}
	else if (UControlRigGraph* RigGraph = Cast<UControlRigGraph>(GetEdGraph(InNode->GetGraph())))
	{
		if (URigVMGraph* ContainedGraph = InNode->GetContainedGraph())
		{
			bool bSubGraphExists = false;
			for (UEdGraph* SubGraph : RigGraph->SubGraphs)
			{
				if (UControlRigGraph* SubRigGraph = Cast<UControlRigGraph>(SubGraph))
				{
					if (SubRigGraph->ModelNodePath == ContainedGraph->GetNodePath())
					{
						bSubGraphExists = true;
						break;
					}
				}
			}

			if (!bSubGraphExists)
			{
				// create a sub graph
				UControlRigGraph* SubRigGraph = NewObject<UControlRigGraph>(RigGraph, *InNode->GetEditorSubGraphName(), RF_Transactional);
				SubRigGraph->Schema = UControlRigGraphSchema::StaticClass();
				SubRigGraph->bAllowRenaming = 1;
				SubRigGraph->bEditable = 1;
				SubRigGraph->bAllowDeletion = 1;
				SubRigGraph->ModelNodePath = ContainedGraph->GetNodePath();
				SubRigGraph->bIsFunctionDefinition = false;

				RigGraph->SubGraphs.Add(SubRigGraph);

				SubRigGraph->Initialize(this);

				GetOrCreateController(ContainedGraph)->ResendAllNotifications();
			}
		}
	}
}

bool UControlRigBlueprint::RemoveEdGraphForCollapseNode(URigVMCollapseNode* InNode, bool bNotify)
{
	check(InNode);

	if (InNode->GetGraph()->IsA<URigVMFunctionLibrary>())
	{
		if (URigVMGraph* ContainedGraph = InNode->GetContainedGraph())
		{
			for (UEdGraph* FunctionGraph : FunctionGraphs)
			{
				if (UControlRigGraph* RigFunctionGraph = Cast<UControlRigGraph>(FunctionGraph))
				{
					if (RigFunctionGraph->ModelNodePath == ContainedGraph->GetNodePath())
					{
						if (URigVMController* SubController = GetController(ContainedGraph))
						{
							SubController->OnModified().RemoveAll(RigFunctionGraph);
						}

						if (ModifiedEvent.IsBound() && bNotify)
						{
							ModifiedEvent.Broadcast(ERigVMGraphNotifType::NodeRemoved, InNode->GetGraph(), InNode);
						}

						FunctionGraphs.Remove(RigFunctionGraph);
						return bNotify;
					}
				}
			}
		}
	}
	else if (UControlRigGraph* RigGraph = Cast<UControlRigGraph>(GetEdGraph(InNode->GetGraph())))
	{
		if (URigVMGraph* ContainedGraph = InNode->GetContainedGraph())
		{
			for (UEdGraph* SubGraph : RigGraph->SubGraphs)
			{
				if (UControlRigGraph* SubRigGraph = Cast<UControlRigGraph>(SubGraph))
				{
					if (SubRigGraph->ModelNodePath == ContainedGraph->GetNodePath())
					{
						if (URigVMController* SubController = GetController(ContainedGraph))
						{
							SubController->OnModified().RemoveAll(SubRigGraph);
						}

						if (ModifiedEvent.IsBound() && bNotify)
						{
							ModifiedEvent.Broadcast(ERigVMGraphNotifType::NodeRemoved, InNode->GetGraph(), InNode);
						}

						RigGraph->SubGraphs.Remove(SubRigGraph);
						return bNotify;
					}
				}
			}
		}
	}

	return false;
}

#undef LOCTEXT_NAMESPACE

