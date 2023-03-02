// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/RigVMController.h"
#include "RigVMModel/RigVMControllerActions.h"
#include "RigVMModel/Nodes/RigVMFunctionEntryNode.h"
#include "RigVMModel/Nodes/RigVMFunctionReturnNode.h"
#include "RigVMModel/Nodes/RigVMFunctionReferenceNode.h"
#include "RigVMModel/Nodes/RigVMAggregateNode.h"
#include "RigVMModel/Nodes/RigVMDispatchNode.h"
#include "RigVMCore/RigVMRegistry.h"
#include "RigVMCore/RigVMExecuteContext.h"
#include "RigVMCore/RigVMUnknownType.h"
#include "RigVMCore/RigVMByteCode.h"
#include "RigVMFunctions/RigVMFunction_ControlFlow.h"
#include "RigVMCompiler/RigVMCompiler.h"
#include "RigVMDeveloperModule.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/CoreMisc.h"
#include "Algo/Sort.h"
#include "Algo/Count.h"
#include "Algo/Transform.h"
#include "RigVMPythonUtils.h"
#include "RigVMTypeUtils.h"
#include "Engine/UserDefinedStruct.h"
#include "RigVMFunctions/RigVMDispatch_If.h"
#include "RigVMFunctions/RigVMDispatch_Select.h"
#include "RigVMFunctions/RigVMDispatch_Array.h"
#include "RigVMModel/RigVMClient.h"
#include "RigVMModel/Nodes/RigVMBranchNode.h"
#include "RigVMModel/Nodes/RigVMArrayNode.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigVMController)

#if WITH_EDITOR
#include "Exporters/Exporter.h"
#include "UnrealExporter.h"
#include "Factories.h"
#include "UObject/CoreRedirects.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Styling/AppStyle.h"
#include "AssetRegistry/AssetRegistryModule.h"
#endif

TMap<URigVMController::FControlRigStructPinRedirectorKey, FString> URigVMController::PinPathCoreRedirectors;

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

void FRigVMClientPatchResult::Merge(const FRigVMClientPatchResult& InOther)
{
	bSucceeded = Succeeded() && InOther.Succeeded();
	bChangedContent = ChangedContent() || InOther.ChangedContent();
	bRequiresToMarkPackageDirty = RequiresToMarkPackageDirty() || InOther.RequiresToMarkPackageDirty();
	ErrorMessages.Append(InOther.GetErrorMessages());
	RemovedNodes.Append(InOther.GetRemovedNodes());
	AddedNodes.Append(InOther.GetAddedNodes());
}

URigVMController::URigVMController()
	: bValidatePinDefaults(true)
	, bSuspendNotifications(false)
	, bReportWarningsAndErrors(true)
	, bIgnoreRerouteCompactnessChanges(false)
	, UserLinkDirection(ERigVMPinDirection::Invalid)
	, bEnableTypeCasting(true)
	, bIsTransacting(false)
	, bIsRunningUnitTest(false)
	, bIsFullyResolvingTemplateNode(false)
	, bSuspendTemplateComputation(false)
#if WITH_EDITOR
	, bRegisterTemplateNodeUsage(true)
#endif
{
}

URigVMController::URigVMController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bValidatePinDefaults(true)
	, bSuspendNotifications(false)
	, bReportWarningsAndErrors(true)
	, bIgnoreRerouteCompactnessChanges(false)
	, UserLinkDirection(ERigVMPinDirection::Invalid)
	, bEnableTypeCasting(true)
	, bIsTransacting(false)
	, bIsRunningUnitTest(false)
	, bIsFullyResolvingTemplateNode(false)
	, bSuspendTemplateComputation(false)
#if WITH_EDITOR
	, bRegisterTemplateNodeUsage(true)
#endif
{
	ActionStack = CreateDefaultSubobject<URigVMActionStack>(TEXT("ActionStack"));

	ActionStack->OnModified().AddLambda([&](ERigVMGraphNotifType NotifType, URigVMGraph* InGraph, UObject* InSubject) -> void {
		Notify(NotifType, InSubject);
	});
}

URigVMController::~URigVMController()
{
}

#if WITH_EDITORONLY_DATA
void URigVMController::DeclareConstructClasses(TArray<FTopLevelAssetPath>& OutConstructClasses, const UClass* SpecificSubclass)
{
	Super::DeclareConstructClasses(OutConstructClasses, SpecificSubclass);
	OutConstructClasses.Add(FTopLevelAssetPath(URigVMActionStack::StaticClass()));
	OutConstructClasses.Add(FTopLevelAssetPath(URigVMInjectionInfo::StaticClass()));
	OutConstructClasses.Add(FTopLevelAssetPath(URigVMPin::StaticClass()));
	OutConstructClasses.Add(FTopLevelAssetPath(URigVMVariableNode::StaticClass()));
	OutConstructClasses.Add(FTopLevelAssetPath(URigVMLink::StaticClass()));
}
#endif

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

void URigVMController::SetIsRunningUnitTest(bool bIsRunning)
{
	bIsRunningUnitTest = bIsRunning;

	if(URigVMBuildData* BuildData = URigVMBuildData::Get())
	{
		BuildData->SetIsRunningUnitTest(bIsRunning);
	}	
}

URigVMController::FPinInfo::FPinInfo()
	: ParentIndex(INDEX_NONE)
	, Name(NAME_None)
	, Direction(ERigVMPinDirection::Invalid)
	, TypeIndex(INDEX_NONE)
	, bIsArray(false)
	, Property(nullptr)
	, bIsExpanded(false)
	, bIsConstant(false)
	, bIsDynamicArray(false)
{
}

URigVMController::FPinInfo::FPinInfo(const URigVMPin* InPin, int32 InParentIndex, ERigVMPinDirection InDirection)
	: ParentIndex(InParentIndex)
	, Name(InPin->GetName())
	, Direction(InDirection == ERigVMPinDirection::Invalid ? InPin->GetDirection() : InDirection)
	, TypeIndex(InPin->GetTypeIndex())
	, bIsArray(InPin->IsArray())
	, Property(nullptr)
	, bIsExpanded(InPin->IsExpanded())
	, bIsConstant(InPin->IsDefinedAsConstant())
	, bIsDynamicArray(InPin->IsDynamicArray())
{
	// this method describes the info as currently represented in the model.

	CorrectExecuteTypeIndex();
	DefaultValue = InPin->GetDefaultValue();
	
	if(DefaultValue.IsEmpty() && (InPin->IsArray() || InPin->IsStruct()))
	{
		static const FString EmptyBraces(TEXT("()"));
		DefaultValue = EmptyBraces;
	}
}

URigVMController::FPinInfo::FPinInfo(FProperty* InProperty, ERigVMPinDirection InDirection, int32 InParentIndex, const uint8* InDefaultValueMemory)
	: ParentIndex(InParentIndex)
	, Name(InProperty->GetFName())
	, Direction(InDirection)
	, TypeIndex(INDEX_NONE)
	, bIsArray(InProperty->IsA<FArrayProperty>())
	, Property(InProperty)
	, bIsExpanded(false)
	, bIsConstant(false)
	, bIsDynamicArray(false)
{
	// this method describes the info as needed based on the property structure

	if (Direction == ERigVMPinDirection::Invalid)
	{
		Direction = FRigVMStruct::GetPinDirectionFromProperty(Property);
	}

#if WITH_EDITOR

	if (CastField<FArrayProperty>(InProperty->GetOwnerProperty()) == nullptr)
	{
		const FString DisplayNameText = InProperty->GetDisplayNameText().ToString();
		if (!DisplayNameText.IsEmpty())
		{
			DisplayName = *DisplayNameText;
		}
	}
	
	bIsConstant = InProperty->HasMetaData(TEXT("Constant"));
	CustomWidgetName = InProperty->GetMetaData(TEXT("CustomWidget"));
	if (InProperty->HasMetaData(FRigVMStruct::ExpandPinByDefaultMetaName))
	{
		bIsExpanded = true;
	}

#endif

#if WITH_EDITOR
	if (Direction == ERigVMPinDirection::Hidden)
	{
		if (!InProperty->HasMetaData(TEXT("ArraySize")))
		{
			bIsDynamicArray = true;
		}
	}
	if (bIsDynamicArray)
	{
		if (InProperty->HasMetaData(FRigVMStruct::SingletonMetaName))
		{
			bIsDynamicArray = false;
		}
	}
#endif

	UObject* CPPTypeObject = nullptr;
	
	FProperty* PropertyForType = InProperty;
	if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(PropertyForType))
	{
		PropertyForType = ArrayProperty->Inner;
	}

	if (const FStructProperty* StructProperty = CastField<FStructProperty>(PropertyForType))
	{
		CPPTypeObject = StructProperty->Struct;
	}
	else if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(PropertyForType))
	{
		if(RigVMCore::SupportsUObjects())
		{
			CPPTypeObject = ObjectProperty->PropertyClass;
		}
	}
	else if (const FInterfaceProperty* InterfaceProperty = CastField<FInterfaceProperty>(PropertyForType))
	{
		if (RigVMCore::SupportsUInterfaces())
		{
			CPPTypeObject = InterfaceProperty->InterfaceClass;
		}
	}
	else
	{
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(PropertyForType))
		{
			CPPTypeObject = EnumProperty->GetEnum();
		}
		if (const FByteProperty* ByteProperty = CastField<FByteProperty>(PropertyForType))
		{
			CPPTypeObject = ByteProperty->Enum;
		}

		if(InDefaultValueMemory)
		{
			InProperty->ExportText_Direct(DefaultValue, InDefaultValueMemory, InDefaultValueMemory, nullptr, PPF_None, nullptr);
		}
	}

	FString ExtendedCppType;
	FString CPPType = InProperty->GetCPPType(&ExtendedCppType);
	CPPType += ExtendedCppType;
	CPPType = RigVMTypeUtils::PostProcessCPPType(CPPType, CPPTypeObject);
	TypeIndex = FRigVMRegistry::Get().GetTypeIndexFromCPPType(CPPType);
	CorrectExecuteTypeIndex();
}

void URigVMController::FPinInfo::CorrectExecuteTypeIndex()
{
	const FRigVMRegistry& Registry = FRigVMRegistry::Get();
	if(Registry.IsExecuteType(TypeIndex))
	{
		TRigVMTypeIndex DefaultExecuteType = RigVMTypeUtils::TypeIndex::Execute;
		if(Registry.IsArrayType(TypeIndex))
		{
			DefaultExecuteType = Registry.GetArrayTypeFromBaseTypeIndex(DefaultExecuteType);
		}
		TypeIndex = DefaultExecuteType;
	}
}

uint32 GetTypeHash(const URigVMController::FPinInfo& InPin)
{
	uint32 Hash = 0; //GetTypeHash(InPin.ParentIndex);
	Hash = HashCombine(Hash, GetTypeHash(InPin.Name));
	Hash = HashCombine(Hash, GetTypeHash((int32)InPin.Direction));
	Hash = HashCombine(Hash, GetTypeHash((int32)InPin.TypeIndex));
	Hash = HashCombine(Hash, GetTypeHash(InPin.bIsArray));
	// we are not hashing the parent index,  pinpath, default value or the property since
	// it doesn't matter for the structure validity of the node
	return Hash;
}

URigVMController::FPinInfoArray::FPinInfoArray(const URigVMNode* InNode)
{
	// this method adds all pins as currently represented in the model.
	for(const URigVMPin* Pin : InNode->GetPins())
	{
		(void)AddPin(Pin, INDEX_NONE);
	}
}

int32 URigVMController::FPinInfoArray::AddPin(const URigVMPin* InPin, int32 InParentIndex, ERigVMPinDirection InDirection)
{
	// this method adds all pins as currently represented in the model.
	const int32 Index = Pins.Emplace(InPin, InParentIndex, InDirection);
	for(const URigVMPin* SubPin : InPin->GetSubPins())
	{
		const int32 SubPinIndex = AddPin(SubPin, Index, InDirection);
		Pins[Index].SubPins.Add(SubPinIndex);
	}
	return Index;
}

URigVMController::FPinInfoArray::FPinInfoArray(const URigVMNode* InNode, URigVMController* InController, const FPinInfoArray* InPreviousPinInfos)
{
	// this method adds pins as needed based on the property structure
	for(const URigVMPin* Pin : InNode->GetPins())
	{
		const FString DefaultValue = Pin->GetDefaultValue();
		(void)AddPin(InController, INDEX_NONE, Pin->GetFName(), Pin->GetDirection(), Pin->GetTypeIndex(), DefaultValue, nullptr, InPreviousPinInfos);
	}
}

URigVMController::FPinInfoArray::FPinInfoArray(const FRigVMGraphFunctionHeader& FunctionHeader,
	URigVMController* InController, const FPinInfoArray* InPreviousPinInfos)
{
	// this method adds pins as needed based on the property structure
	const FRigVMRegistry& Registry = FRigVMRegistry::Get();
	for(const FRigVMGraphFunctionArgument& FunctionArgument : FunctionHeader.Arguments)
	{
		const TRigVMTypeIndex TypeIndex = Registry.GetTypeIndexFromCPPType(FunctionArgument.CPPType.ToString());
		(void)AddPin(InController, INDEX_NONE, FunctionArgument.Name, FunctionArgument.Direction, TypeIndex, FunctionArgument.DefaultValue, nullptr, InPreviousPinInfos);
	}
}

int32 URigVMController::FPinInfoArray::AddPin(FProperty* InProperty, URigVMController* InController,
                                              ERigVMPinDirection InDirection, int32 InParentIndex, const uint8* InDefaultValueMemory)
{
	// this method adds pins as needed based on the property structure

	check(InDefaultValueMemory);
	
	const int32 Index = Pins.Emplace(InProperty, InDirection, InParentIndex, InDefaultValueMemory);
	if(InParentIndex != INDEX_NONE)
	{
		Pins[InParentIndex].SubPins.Add(Index);
	}

	if (const FStructProperty* StructProperty = CastField<FStructProperty>(InProperty))
	{
		(void)AddPins(StructProperty->Struct, InController, Pins[Index].Direction, Index, InDefaultValueMemory);
	}
	else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(InProperty))
	{
		FScriptArrayHelper ArrayHelper(ArrayProperty, InDefaultValueMemory);
		for(int32 ElementIndex = 0; ElementIndex < ArrayHelper.Num(); ElementIndex++)
		{
			const uint8* ElementDefaultValueMemory = ArrayHelper.GetRawPtr(ElementIndex);
			const int32 SubIndex = AddPin(ArrayProperty->Inner, InController, Pins[Index].Direction, Index, ElementDefaultValueMemory);
			Pins[SubIndex].Name = *FString::FormatAsNumber(ElementIndex);
		}
	}

	return Index;
}

int32 URigVMController::FPinInfoArray::AddPin(URigVMController* InController, int32 InParentIndex, const FName& InName, ERigVMPinDirection InDirection,
	TRigVMTypeIndex InTypeIndex, const FString& InDefaultValue, const uint8* InDefaultValueMemory, const FPinInfoArray* InPreviousPinInfos)
{
	const FRigVMRegistry& Registry = FRigVMRegistry::Get();
		
	FPinInfo Info;
	Info.ParentIndex = InParentIndex;
	Info.Name = InName;
	Info.Direction = InDirection;
	Info.TypeIndex = InTypeIndex;
	Info.bIsArray = Registry.IsArrayType(InTypeIndex);
	Info.DefaultValue = InDefaultValue;
	Info.CorrectExecuteTypeIndex();

	const int32 Index = Pins.Add(Info);

	if(InPreviousPinInfos)
	{
		const FString& PinPath = GetPinPath(Index);
		const int32 PreviousIndex = InPreviousPinInfos->GetIndexFromPinPath(PinPath);
		if(PreviousIndex != INDEX_NONE)
		{
			const FPinInfo& PreviousPin = (*InPreviousPinInfos)[PreviousIndex];
			if(PreviousPin.TypeIndex == InTypeIndex)
			{
				Pins[Index].DefaultValue = PreviousPin.DefaultValue;
			}
		}
	}
	
	const FRigVMTemplateArgumentType& Type = Registry.GetType(InTypeIndex);
	if(!Type.IsWildCard())
	{
		if(Info.bIsArray)
		{
			const TRigVMTypeIndex& ElementTypeIndex = Registry.GetBaseTypeFromArrayTypeIndex(InTypeIndex);
			const FRigVMTemplateArgumentType& ElementType = Registry.GetType(ElementTypeIndex);

			const TArray<FString> Elements = URigVMPin::SplitDefaultValue(Pins[Index].DefaultValue);
			for(int32 ElementIndex = 0; ElementIndex < Elements.Num(); ElementIndex++)
			{
				const FString& ElementDefaultValue = Elements[ElementIndex];
				const uint8* ElementDefaultValueMemory = nullptr;
				FStructOnScope ElementDefaultValueMemoryScope;

				if(UScriptStruct* ElementScriptStruct = Cast<UScriptStruct>(ElementType.CPPTypeObject))
				{
					ElementDefaultValueMemoryScope = FStructOnScope(ElementScriptStruct);
		
					FRigVMPinDefaultValueImportErrorContext ErrorPipe;
					ElementScriptStruct->ImportText(*ElementDefaultValue, ElementDefaultValueMemoryScope.GetStructMemory(), nullptr, PPF_None, &ErrorPipe, FString());
					ElementDefaultValueMemory = ElementDefaultValueMemoryScope.GetStructMemory();
				}

				(void)AddPin(InController, Index, *FString::FormatAsNumber(ElementIndex), InDirection, ElementTypeIndex, ElementDefaultValue, ElementDefaultValueMemory, InPreviousPinInfos);
			}
		}
		else if(UScriptStruct* ScriptStruct = Cast<UScriptStruct>(Type.CPPTypeObject))
		{
			const uint8* DefaultValueMemory = InDefaultValueMemory;
			FStructOnScope DefaultValueMemoryScope;
			if(DefaultValueMemory == nullptr)
			{
				FRigVMPinDefaultValueImportErrorContext ErrorPipe;
				DefaultValueMemoryScope = FStructOnScope(ScriptStruct);
				ScriptStruct->ImportText(*Pins[Index].DefaultValue, DefaultValueMemoryScope.GetStructMemory(), nullptr, PPF_None, &ErrorPipe, FString());
				DefaultValueMemory = DefaultValueMemoryScope.GetStructMemory();
			}
			AddPins(ScriptStruct, InController, InDirection, Index, DefaultValueMemory);
		}
	}

	if(InParentIndex != INDEX_NONE)
	{
		Pins[InParentIndex].SubPins.Add(Index);
	}

	return Index;
}

void URigVMController::FPinInfoArray::AddPins(UScriptStruct* InScriptStruct, URigVMController* InController,
	ERigVMPinDirection InDirection, int32 InParentIndex, const uint8* InDefaultValueMemory)
{
	if (InController->ShouldStructBeUnfolded(InScriptStruct))
	{
		TArray<UStruct*> StructsToVisit = FRigVMTemplate::GetSuperStructs(InScriptStruct, true);
		for(UStruct* StructToVisit : StructsToVisit)
		{
			// using EFieldIterationFlags::None excludes the
			// properties of the super struct in this iterator.
			for (TFieldIterator<FProperty> It(StructToVisit, EFieldIterationFlags::None); It; ++It)
			{
				const uint8* DefaultValueMemory = nullptr;
				if(InDefaultValueMemory)
				{
					DefaultValueMemory = It->ContainerPtrToValuePtr<uint8>(InDefaultValueMemory);
				}
				(void)AddPin(*It, InController, InDirection, InParentIndex, DefaultValueMemory);
			}
		}
	}
}

const FString& URigVMController::FPinInfoArray::GetPinPath(const int32 InIndex) const
{
	if(!Pins.IsValidIndex(InIndex))
	{
		static const FString EmptyString;
		return EmptyString;
	}

	if(Pins[InIndex].PinPath.IsEmpty())
	{
		if(Pins[InIndex].ParentIndex == INDEX_NONE)
		{
			Pins[InIndex].PinPath = Pins[InIndex].Name.ToString();
		}
		else
		{
			Pins[InIndex].PinPath = URigVMPin::JoinPinPath(GetPinPath(Pins[InIndex].ParentIndex), Pins[InIndex].Name.ToString());
		}
	}

	return Pins[InIndex].PinPath;
}

int32 URigVMController::FPinInfoArray::GetIndexFromPinPath(const FString& InPinPath) const
{
	if(PinPathLookup.Num() != Num())
	{
		PinPathLookup.Reset();
		for(int32 Index=0; Index < Num(); Index++)
		{
			PinPathLookup.Add(GetPinPath(Index), Index);
		}
	}

	if(const int32* Index = PinPathLookup.Find(InPinPath))
	{
		return *Index;
	}
	return INDEX_NONE;
}

const URigVMController::FPinInfo* URigVMController::FPinInfoArray::GetPinFromPinPath(const FString& InPinPath) const
{
	const int32 Index = GetIndexFromPinPath(InPinPath);
	if(Pins.IsValidIndex(Index))
	{
		return &Pins[Index];
	}
	return nullptr;
}

int32 URigVMController::FPinInfoArray::GetRootIndex(const int32 InIndex) const
{
	if(Pins.IsValidIndex(InIndex))
	{
		if(Pins[InIndex].ParentIndex == INDEX_NONE)
		{
			return InIndex;
		}
		return GetRootIndex(Pins[InIndex].ParentIndex);
	}
	return INDEX_NONE;
}

uint32 GetTypeHash(const URigVMController::FPinInfoArray& InPins)
{
	TArray<uint32> Hashes;
	Hashes.Reserve(InPins.Num());
	
	uint32 OverAllHash = GetTypeHash(InPins.Num());
	for(const URigVMController::FPinInfo& Info : InPins)
	{
		uint32 PinHash = GetTypeHash(Info);
		if(Info.ParentIndex != INDEX_NONE)
		{
			PinHash = HashCombine(PinHash, Hashes[Info.ParentIndex]);
		}
		Hashes.Add(PinHash);
		OverAllHash = HashCombine(OverAllHash, PinHash); 
	}
	return OverAllHash;
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
	
	
	// All nodes
	for (URigVMNode* Node : GetGraph()->GetNodes())
	{
		Commands.Append(GetAddNodePythonCommands(Node));
	}

	// All links
	for (URigVMLink* Link : GetGraph()->GetLinks())
	{
		URigVMPin* SourcePin = Link->GetSourcePin();
		URigVMPin* TargetPin = Link->GetTargetPin();
		
		if (SourcePin->GetInjectedNodes().Num() > 0 || TargetPin->GetInjectedNodes().Num() > 0)
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
				// SetRerouteCompactnessByName(const FName& InNodeName, bool bShowAsFullNode, bool bSetupUndoRedo = true);
				Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').set_reroute_compactness_by_name('%s', %s)"),
					*GraphName,
					*Reroute->GetName(),
					Reroute->GetShowsAsFullNode() ? TEXT("True") : TEXT("False")));
			}
		}
	}

	return Commands;
}

TArray<FString> URigVMController::GetAddNodePythonCommands(URigVMNode* Node) const
{
	TArray<FString> Commands;

	const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
	const FString NodeName = GetSanitizedNodeName(Node->GetName());

	auto GetResolveWildcardPinsPythonCommands = [](const FString& InGraphName, const URigVMTemplateNode* InNode, const FRigVMTemplate* InTemplate)
	{
		TArray<FString> Commands;

		// Lets minimize the number of commands by stopping when the number of permutations left is 1 (or less)
		TArray<int32> Permutations;
		Permutations.SetNumUninitialized(InTemplate->NumPermutations());
		FRigVMTemplate::FTypeMap TypeMap;
		
		for (int32 ArgIndex = 0; ArgIndex < InTemplate->NumArguments(); ++ArgIndex)
		{
			if (Permutations.Num() < 2)
			{
				break;
			}
			
			const FRigVMTemplateArgument* Argument = InTemplate->GetArgument(ArgIndex);
			if (!Argument->IsSingleton())
			{
				URigVMPin* Pin = InNode->FindPin(Argument->GetName().ToString());
				if (!Pin->IsWildCard())
				{
					Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').resolve_wild_card_pin('%s', '%s', '%s')"),
							*InGraphName,
							*Pin->GetPinPath(),
							*Pin->GetCPPType(),
							*Pin->GetCPPTypeObject()->GetPathName()));

					TypeMap.Add(Argument->GetName(), Pin->GetTypeIndex());
					InTemplate->Resolve(TypeMap, Permutations, false);
				}
			}
		}

		return Commands;
	};

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
		else if (UnitNode->IsSingleton())
		{
			// add_struct_node_from_struct_path(script_struct_path, method_name, position=[0.0, 0.0], node_name='', undo=True)
			Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_unit_node_from_struct_path('%s', 'Execute', %s, '%s')"),
					*GraphName,
					*UnitNode->GetScriptStruct()->GetPathName(),
					*RigVMPythonUtils::Vector2DToPythonString(UnitNode->GetPosition()),
					*NodeName));
		}
		else
		{
			// add_template_node(notation, position=[0.0, 0.0], node_name='', undo=True)
			Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_template_node('%s', %s, '%s')"),
							*GraphName,
							*UnitNode->GetNotation().ToString(),
							*RigVMPythonUtils::Vector2DToPythonString(UnitNode->GetPosition()),
							*NodeName));

			// Try to resolve wildcard pins			
			if (const FRigVMTemplate* Template = UnitNode->GetTemplate())
			{
				Commands.Append(GetResolveWildcardPinsPythonCommands(GraphName, UnitNode, Template));
			}			
		}		
	}
	else if (const URigVMDispatchNode* DispatchNode = Cast<URigVMDispatchNode>(Node))
	{
		// add_template_node(notation, position=[0.0, 0.0], node_name='', undo=True)
		Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_template_node('%s', %s, '%s')"),
						*GraphName,
						*DispatchNode->GetNotation().ToString(),
						*RigVMPythonUtils::Vector2DToPythonString(DispatchNode->GetPosition()),
						*NodeName));

		// Try to resolve wildcard pins			
		if (const FRigVMTemplate* Template = DispatchNode->GetTemplate())
		{
			Commands.Append(GetResolveWildcardPinsPythonCommands(GraphName, DispatchNode, Template));
		}			
	}
	else if (const URigVMAggregateNode* AggregateNode = Cast<URigVMAggregateNode>(Node))
	{
		TArray<FString> InnerNodeCommands = GetAddNodePythonCommands(AggregateNode->GetFirstInnerNode());
		Commands.Append(InnerNodeCommands);

		// set_node_position(node_name, position)
		Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').set_node_position_by_name('%s', %s)"),
				*GraphName,
				*AggregateNode->GetName(),
				*RigVMPythonUtils::Vector2DToPythonString(AggregateNode->GetPosition())));

		// add commands for any additional aggregate pin
		const TArray<URigVMPin*> AggregatePins = AggregateNode->IsInputAggregate() ? AggregateNode->GetAggregateInputs() : AggregateNode->GetAggregateOutputs();

		for(int32 Index = 2; Index < AggregatePins.Num(); Index++)
		{
			// add_aggregate_pin(node_name, pin_name)
			Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_aggregate_pin('%s', '%s')"),
					*GraphName,
					*AggregateNode->GetName(),
					*AggregatePins[Index]->GetName()
				));
		}
	}
	else if (const URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
	{
		if (!VariableNode->IsInjected())
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
	}
	else if (const URigVMCommentNode* CommentNode = Cast<URigVMCommentNode>(Node))
	{
		// add_comment_node(comment_text, position=[0.0, 0.0], size=[400.0, 300.0], color=[0.0, 0.0, 0.0, 0.0], node_name='', undo=True)
		Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_comment_node('%s', %s, %s, %s, '%s')"),
					*GraphName,
					*CommentNode->GetCommentText().ReplaceCharWithEscapedChar(),
					*RigVMPythonUtils::Vector2DToPythonString(CommentNode->GetPosition()),
					*RigVMPythonUtils::Vector2DToPythonString(CommentNode->GetSize()),
					*RigVMPythonUtils::LinearColorToPythonString(CommentNode->GetNodeColor()),
					*NodeName));	
	}
	else if (const URigVMRerouteNode* RerouteNode = Cast<URigVMRerouteNode>(Node))
	{
		// add_free_reroute_node(bool bShowAsFullNode, const FString& InCPPType, const FName& InCPPTypeObjectPath, bool bIsConstant, const FName& InCustomWidgetName, const FString& InDefaultValue, const FVector2D& InPosition = FVector2D::ZeroVector, const FString& InNodeName = TEXT(""), bool bSetupUndoRedo = true);
		Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_free_reroute_node(%s, '%s', '%s', %s, '%s', '%s', %s, '%s')"),
				*GraphName,
				RerouteNode->GetShowsAsFullNode() ? TEXT("True") : TEXT("False"),
				*RerouteNode->GetPins()[0]->GetCPPType(),
				*RerouteNode->GetPins()[0]->GetCPPTypeObject()->GetPathName(),
				RerouteNode->GetPins()[0]->IsDefinedAsConstant() ? TEXT("True") : TEXT("False"),
				*RerouteNode->GetPins()[0]->GetCustomWidgetName().ToString(),
				*RerouteNode->GetPins()[0]->GetDefaultValue(),
				*RigVMPythonUtils::Vector2DToPythonString(RerouteNode->GetPosition()),
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
	else if (const URigVMFunctionReferenceNode* RefNode = Cast<URigVMFunctionReferenceNode>(Node))
	{
		if (RefNode->GetReferencedFunctionHeader().LibraryPointer.HostObject == GetGraph()->GetDefaultFunctionLibrary()->GetFunctionHostObjectPath())
		{
			Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_function_reference_node(function_%s, %s, '%s')"),
					*GraphName,
					*RigVMPythonUtils::PythonizeName(RefNode->LoadReferencedNode()->GetContainedGraph()->GetGraphName()),
					*RigVMPythonUtils::Vector2DToPythonString(RefNode->GetPosition()), 
					*NodeName));
		}
		else
		{
			Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_external_function_reference_node('%s', '%s', %s, '%s')"),
						*GraphName,
						*RefNode->GetReferencedFunctionHeader().LibraryPointer.HostObject.ToString(),
						*RefNode->GetReferencedFunctionHeader().Name.ToString(),
						*RigVMPythonUtils::Vector2DToPythonString(RefNode->GetPosition()), 
						*NodeName));
		}
	}
	else if (const URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(Node))
	{
		const FString ContainedGraphName = GetSanitizedGraphName(CollapseNode->GetContainedGraph()->GetGraphName());
		// AddFunctionReferenceNode(URigVMLibraryNode* InFunctionDefinition, const FVector2D& InNodePosition = FVector2D::ZeroVector, const FString& InNodeName = TEXT(""), bool bSetupUndoRedo = true);
		Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_function_reference_node(function_%s, %s, '%s')"),
					*GraphName,
					*RigVMPythonUtils::PythonizeName(ContainedGraphName),
					*RigVMPythonUtils::Vector2DToPythonString(CollapseNode->GetPosition()), 
					*NodeName));
	

		Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').promote_function_reference_node_to_collapse_node('%s')"),
				*GraphName,
				*NodeName));
		Commands.Add(FString::Printf(TEXT("library_controller.remove_function_from_library('%s')"),
				*ContainedGraphName));
	
	}
	else if (const URigVMInvokeEntryNode* InvokeEntryNode = Cast<URigVMInvokeEntryNode>(Node))
	{
		// add_invoke_entry_node(entry_name, position=[0.0, 0.0], node_name='', undo=True)
		Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_invoke_entry_node('%s', %s, '%s')"),
						*GraphName,
						*InvokeEntryNode->GetEntryName().ToString(),
						*RigVMPythonUtils::Vector2DToPythonString(InvokeEntryNode->GetPosition()),
						*NodeName));
	}
	else if (Node->IsA<URigVMFunctionEntryNode>() || Node->IsA<URigVMFunctionReturnNode>())
	{
		
		
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


				TArray<const URigVMPin*> SubPins = { Pin };
				for (int32 i = 0; i < SubPins.Num(); ++i)
				{
					if (SubPins[i]->IsStruct() || SubPins[i]->IsArray())
					{
						SubPins.Append(SubPins[i]->GetSubPins());
						const FString SubPinPath = GetSanitizedPinPath(SubPins[i]->GetPinPath());

						Commands.Add(FString::Printf(TEXT("blueprint.get_controller_by_name('%s').set_pin_expansion('%s', %s)"),
							*GraphName,
							*SubPinPath,
							SubPins[i]->IsExpanded() ? TEXT("True") : TEXT("False")));
					}
				}
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

	if (!bIsTransacting && !IsGraphEditable())
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

	const FRigVMFunction* Function = FRigVMRegistry::Get().FindFunction(InScriptStruct, *InMethodName.ToString());
	if (Function == nullptr)
	{
		ReportErrorf(TEXT("RIGVM_METHOD '%s::%s' cannot be found."), *InScriptStruct->GetStructCPPName(), *InMethodName.ToString());
		return nullptr;
	}

	if(IRigVMClientHost* ClientHost = GetImplementingOuter<IRigVMClientHost>())
	{
		if(const FRigVMClient* Client = ClientHost->GetRigVMClient())
		{
			if(!Function->SupportsExecuteContextStruct(Client->GetExecuteContextStruct()))
			{
				ReportErrorf(TEXT("Cannot add node for function '%s' - incompatible execute context: '%s' vs '%s'."),
						*Function->GetName(),
						*Function->GetExecuteContextStruct()->GetStructCPPName(),
						*Client->GetExecuteContextStruct()->GetStructCPPName());
				return nullptr;
			}
		}
	}

	FString StructureError;
	if (!FRigVMStruct::ValidateStruct(InScriptStruct, &StructureError))
	{
		ReportErrorf(TEXT("Failed to validate struct '%s': %s"), *InScriptStruct->GetName(), *StructureError);
		return nullptr;
	}

	if(const FRigVMTemplate* Template = Function->GetTemplate())
	{
		if(bSetupUndoRedo)
		{
			OpenUndoBracket(FString::Printf(TEXT("Add %s Node"), *Template->GetName().ToString()));
		}

		const FString Name = GetValidNodeName(InNodeName.IsEmpty() ? InScriptStruct->GetName() : InNodeName);
		URigVMUnitNode* TemplateNode = Cast<URigVMUnitNode>(AddTemplateNode(Template->GetNotation(), InPosition, Name, bSetupUndoRedo, bPrintPythonCommand));
		if(TemplateNode == nullptr)
		{
			CancelUndoBracket();
			return nullptr;
		}

		int32 PermutationIndex = Template->FindPermutation(Function);
		FRigVMTemplateTypeMap Types = Template->GetTypesForPermutation(PermutationIndex);
		for (TPair<FName, TRigVMTypeIndex>& Pair : Types)
		{
			if (URigVMPin* Pin = TemplateNode->FindPin(Pair.Key.ToString()))
			{
				if (Pin->IsWildCard())
				{
					ResolveWildCardPin(Pin, Pair.Value, bSetupUndoRedo);
				}
			}
			if (!TemplateNode->HasWildCardPin())
			{
				break;
			}
		}

		if (UnitNodeCreatedContext.IsValid())
		{
			if (TSharedPtr<FStructOnScope> StructScope = TemplateNode->ConstructStructInstance())
			{
				TGuardValue<FName> NodeNameScope(UnitNodeCreatedContext.NodeName, TemplateNode->GetFName());
				FRigVMStruct* StructInstance = (FRigVMStruct*)StructScope->GetStructMemory();
				StructInstance->OnUnitNodeCreated(UnitNodeCreatedContext);
			}
		}
		
		if(bSetupUndoRedo)
		{
			CloseUndoBracket();
		}

		return TemplateNode;
	}

	FStructOnScope StructOnScope(InScriptStruct);
	FRigVMStruct* StructMemory = (FRigVMStruct*)StructOnScope.GetStructMemory();
	const bool bIsEventNode = (!StructMemory->GetEventName().IsNone());
	if (bIsEventNode)
	{
		// don't allow event nodes in anything but top level graphs
		if (!Graph->IsTopLevelGraph())
		{
			ReportAndNotifyError(TEXT("Event nodes can only be added to top level graphs."));
			return nullptr;
		}

		if(StructMemory->CanOnlyExistOnce())
		{
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
	}
	
	FString Name = GetValidNodeName(InNodeName.IsEmpty() ? InScriptStruct->GetName() : InNodeName);
	URigVMUnitNode* Node = NewObject<URigVMUnitNode>(Graph, *Name);
	Node->ResolvedFunctionName = Function->GetName();
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
	{
		TGuardValue<bool> SuspendNotifications(bSuspendNotifications, true);
		AddPinsForStruct(InScriptStruct, Node, nullptr, ERigVMPinDirection::Invalid, ExportedDefaultValue, true);
	}

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

	if (!bIsTransacting && !IsGraphEditable())
	{
		return nullptr;
	}

	UScriptStruct* ScriptStruct = RigVMTypeUtils::FindObjectFromCPPTypeObjectPath<UScriptStruct>(InScriptStructPath);
	if (ScriptStruct == nullptr)
	{
		ReportErrorf(TEXT("Cannot find struct for path '%s'."), *InScriptStructPath);
		return nullptr;
	}

	return AddUnitNode(ScriptStruct, InMethodName, InPosition, InNodeName, bSetupUndoRedo, bPrintPythonCommand);
}

URigVMUnitNode* URigVMController::AddUnitNodeWithDefaults(UScriptStruct* InScriptStruct, const FString& InDefaults,
	const FName& InMethodName, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo,
	bool bPrintPythonCommand)
{
	if(InScriptStruct == nullptr)
	{
		return nullptr;
	}

	FStructOnScope StructOnScope;

	if(!InDefaults.IsEmpty())
	{
		StructOnScope = FStructOnScope(InScriptStruct);
		
		FRigVMPinDefaultValueImportErrorContext ErrorPipe;
		InScriptStruct->ImportText(*InDefaults, StructOnScope.GetStructMemory(), nullptr, PPF_None, &ErrorPipe, FString());

		if(ErrorPipe.NumErrors > 0)
		{
			return nullptr;
		}
	}

	return AddUnitNodeWithDefaults(InScriptStruct, StructOnScope, InMethodName, InPosition, InNodeName, bSetupUndoRedo, bPrintPythonCommand);
}

URigVMUnitNode* URigVMController::AddUnitNodeWithDefaults(UScriptStruct* InScriptStruct, const FRigStructScope& InDefaults,
                                                          const FName& InMethodName, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo,
                                                          bool bPrintPythonCommand)
{
	if(InScriptStruct == nullptr)
	{
		return nullptr;
	}

	const bool bSetPinDefaults = InDefaults.IsValid() && (InDefaults.GetScriptStruct() == InScriptStruct); 
	if(bSetPinDefaults)
	{
		static constexpr TCHAR AddUnitNodeTitle[] = TEXT("Add Unit Node");
		OpenUndoBracket(AddUnitNodeTitle);
	}

	URigVMUnitNode* Node = AddUnitNode(InScriptStruct, InMethodName, InPosition, InNodeName, bSetupUndoRedo, bPrintPythonCommand);
	if(Node == nullptr)
	{
		if(bSetPinDefaults)
		{
			CancelUndoBracket();
		}
		return nullptr;
	}

	if(bSetPinDefaults)
	{
		if(!SetUnitNodeDefaults(Node, InDefaults))
		{
			CancelUndoBracket();
		}
	}

	CloseUndoBracket();
	return Node;
}

bool URigVMController::SetUnitNodeDefaults(URigVMUnitNode* InNode, const FString& InDefaults, bool bSetupUndoRedo,
	bool bPrintPythonCommand)
{
	if(InNode == nullptr)
	{
		return false; 
	}

	UScriptStruct* ScriptStruct = InNode->GetScriptStruct();
	if(ScriptStruct == nullptr)
	{
		return false;
	}
	
	FStructOnScope StructOnScope(ScriptStruct);
	FRigVMPinDefaultValueImportErrorContext ErrorPipe;
	ScriptStruct->ImportText(*InDefaults, StructOnScope.GetStructMemory(), nullptr, PPF_None, &ErrorPipe, FString());

	if(ErrorPipe.NumErrors > 0)
	{
		return false;
	}

	return SetUnitNodeDefaults(InNode, StructOnScope, bSetupUndoRedo, bPrintPythonCommand);
}

bool URigVMController::SetUnitNodeDefaults(URigVMUnitNode* InNode, const FRigStructScope& InDefaults,
                                           bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if(InNode == nullptr || !InDefaults.IsValid())
	{
		return false;
	}

	if(InNode->GetScriptStruct() != InDefaults.GetScriptStruct())
	{
		return false;
	}

	static constexpr TCHAR SetUnitNodeDefaultsTitle[] = TEXT("Set Unit Node Defaults");
	OpenUndoBracket(SetUnitNodeDefaultsTitle);

	for(URigVMPin* Pin : InNode->GetPins())
	{
		if(Pin->GetDirection() != ERigVMPinDirection::Input &&
			Pin->GetDirection() != ERigVMPinDirection::IO &&
			Pin->GetDirection() != ERigVMPinDirection::Visible)
		{
			continue;
		}
		
		if(const FProperty* Property = InDefaults.GetScriptStruct()->FindPropertyByName(Pin->GetFName()))
		{
			const uint8* MemberMemoryPtr = Property->ContainerPtrToValuePtr<uint8>(InDefaults.GetMemory());
			const FString NewDefault = FRigVMStruct::ExportToFullyQualifiedText(Property, MemberMemoryPtr);
			if(NewDefault != Pin->GetDefaultValue())
			{
				SetPinDefaultValue(Pin->GetPinPath(), NewDefault, true, bSetupUndoRedo, false, bPrintPythonCommand);
			}
		}
	}

	CloseUndoBracket();
	return true;
}

URigVMVariableNode* URigVMController::AddVariableNode(const FName& InVariableName, const FString& InCPPType, UObject* InCPPTypeObject, bool bIsGetter, const FString& InDefaultValue, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	if (!bIsTransacting && !IsGraphEditable())
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
		InCPPTypeObject = RigVMTypeUtils::FindObjectFromCPPTypeObjectPath<UObject>(InCPPType);
	}

	FString CPPType = RigVMTypeUtils::PostProcessCPPType(InCPPType, InCPPTypeObject);
	
	FString Name = GetValidNodeName(InNodeName.IsEmpty() ? FString(TEXT("VariableNode")) : InNodeName);
	URigVMVariableNode* Node = NewObject<URigVMVariableNode>(Graph, *Name);
	Node->Position = InPosition;

	if (!bIsGetter)
	{
		URigVMPin* ExecutePin = MakeExecutePin(Node, FRigVMStruct::ExecuteContextName);
		ExecutePin->Direction = ERigVMPinDirection::IO;
		AddNodePin(Node, ExecutePin);
	}

	URigVMPin* VariablePin = NewObject<URigVMPin>(Node, *URigVMVariableNode::VariableName);
	VariablePin->CPPType = RigVMTypeUtils::FNameType;
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
		if (ValuePin->CPPTypeObject)
		{
			ValuePin->CPPTypeObjectPath = *ValuePin->CPPTypeObject->GetPathName();
		}
		ValuePin->bIsDynamicArray = ExternalVariable.bIsArray;

		if(ValuePin->bIsDynamicArray && !RigVMTypeUtils::IsArrayType(ValuePin->CPPType))
		{
			ValuePin->CPPType = RigVMTypeUtils::ArrayTypeFromBaseType(*ValuePin->CPPType);
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
		{
			TGuardValue<bool> SuspendNotifications(bSuspendNotifications, true);
			AddPinsForStruct(ValuePin->GetScriptStruct(), Node, ValuePin, ValuePin->Direction, DefaultValue, false);
		}
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

	if (!bIsTransacting && !IsGraphEditable())
	{
		return nullptr;
	}

	UObject* CPPTypeObject = nullptr;
	if (!InCPPTypeObjectPath.IsEmpty())
	{
		CPPTypeObject = RigVMTypeUtils::FindObjectFromCPPTypeObjectPath<UObject>(InCPPTypeObjectPath);
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

	if (!bIsTransacting && !IsGraphEditable())
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
				SetPinDefaultValue(VariablePin, InVariableName.ToString(), false, bSetupUndoRedo, false);
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

	if (!bIsTransacting && !IsGraphEditable())
	{
		return;
	}

	if (!InVarName.IsValid())
	{
		return;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	// When transacting, the action stack will deal with the deletion of variable nodes
	if(GIsTransacting)
	{
		return;
	}

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
			TGuardValue<bool> GuardEditGraph(CollapseNode->ContainedGraph->bEditable, true);

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

bool URigVMController::OnExternalVariableRenamed(const FName& InOldVarName, const FName& InNewVarName, bool bSetupUndoRedo)
{
	if (!IsValidGraph())
	{
		return false;
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}

	if (!InOldVarName.IsValid() || !InNewVarName.IsValid())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	for (const FRigVMGraphVariableDescription& LocalVariable : Graph->GetLocalVariables(true))
	{
		if (InOldVarName == LocalVariable.Name)
		{
			return false;
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
			TGuardValue<bool> GuardEditGraph(CollapseNode->ContainedGraph->bEditable, true);
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

	return true;
}

void URigVMController::OnExternalVariableTypeChanged(const FName& InVarName, const FString& InCPPType, UObject* InCPPTypeObject, bool bSetupUndoRedo)
{
	if (!IsValidGraph())
	{
		return;
	}

	if (!bIsTransacting && !IsGraphEditable())
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
			TGuardValue<bool> GuardEditGraph(CollapseNode->ContainedGraph->bEditable, true);
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

	if (!bIsTransacting && !IsGraphEditable())
	{
		return;
	}

	UObject* CPPTypeObject = nullptr;
	if (!InCPPTypeObjectPath.IsEmpty())
	{
		CPPTypeObject = RigVMTypeUtils::FindObjectFromCPPTypeObjectPath<UObject>(InCPPTypeObjectPath);
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

	if (!bIsTransacting && !IsGraphEditable())
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

bool URigVMController::UnresolveTemplateNodes(const TArray<FName>& InNodeNames, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return false;
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}

	TArray<URigVMNode*> Nodes;
	for (const FName& NodeName : InNodeNames)
	{
		if (URigVMNode* Node = GetGraph()->FindNodeByName(NodeName))
		{
			Nodes.Add(Node);
		}
	}

	if(UnresolveTemplateNodes(Nodes, bSetupUndoRedo))
	{
		if(bPrintPythonCommand)
		{
			const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
			TArray<FString> NodeNames;
			for(const FName& NodeName : InNodeNames)
			{
				NodeNames.Add(GetSanitizedNodeName(NodeName.ToString()));
			}
			const FString NodeNamesJoined = FString::Join(NodeNames, TEXT("','"));

			// UnresolveTemplateNodes(const TArray<FName>& InNodeNames)
			RigVMPythonUtils::Print(GetGraphOuterName(),
					FString::Printf(TEXT("blueprint.get_controller_by_name('%s').unresolve_template_nodes(['%s'])"),
					*GraphName,
					*NodeNamesJoined));
		}

		return true;
	}

	return false;
}

bool URigVMController::UnresolveTemplateNodes(const TArray<URigVMNode*>& InNodes, bool bSetupUndoRedo)
{
	if (!IsValidGraph() || InNodes.IsEmpty())
	{
		return false;
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
	// check if any of the nodes needs to be unresolved
	const bool bHasNodeToResolve = InNodes.ContainsByPredicate( [](const URigVMNode* Node) -> bool
	{
		if (const URigVMTemplateNode* TemplateNode = Cast<URigVMTemplateNode>(Node))
		{
			return !TemplateNode->IsFullyUnresolved();
		}
		return false;
	});
	if (!bHasNodeToResolve)
	{
		return false;
	}
	
	FRigVMBaseAction Action;
	if(bSetupUndoRedo)
	{
		Action.Title = TEXT("Unresolve nodes");
		ActionStack->BeginAction(Action);
	}

	{
		const FRigVMRegistry& Registry = FRigVMRegistry::Get();
		for (URigVMNode* Node : InNodes)
		{
			EjectAllInjectedNodes(Node, bSetupUndoRedo);
			TArray<URigVMLink*> Links = Node->GetLinks();
			for (int32 i=0; i<Links.Num(); ++i)
			{
				URigVMLink* Link = Links[i];
				URigVMPin* SourcePin = Link->GetSourcePin();
				URigVMPin* TargetPin = Link->GetTargetPin();

				URigVMNode* OtherNode = SourcePin->GetNode() == Node ? TargetPin->GetNode() : SourcePin->GetNode();
				if (!InNodes.Contains(OtherNode))
				{
					const URigVMPin* PinOnNode = SourcePin->GetNode() == Node ? SourcePin : TargetPin;
					if(PinOnNode->IsExecuteContext())
					{
						continue;
					}
					
					if (const URigVMTemplateNode* TemplateNode = Cast<URigVMTemplateNode>(Node))
					{
						if(const FRigVMTemplate* Template = TemplateNode->GetTemplate())
						{
							const URigVMPin* RootPin = PinOnNode->GetRootPin();
							if(const FRigVMTemplateArgument* Argument = Template->FindArgument(RootPin->GetFName()))
							{
								if(Argument->IsSingleton())
								{
									continue;
								}
							}
						}
					}
					
					BreakLink(SourcePin, TargetPin, bSetupUndoRedo);
				}
			}

			if (URigVMTemplateNode* TemplateNode = Cast<URigVMTemplateNode>(Node))
			{
				TemplateNode->InvalidateCache();
				TemplateNode->ResolvedFunctionName.Reset();
				TemplateNode->ResolvedPermutation = INDEX_NONE;

				if (const FRigVMTemplate* Template = TemplateNode->GetTemplate())
				{
					for (int32 i=0; i<Template->NumArguments(); ++i)
					{
						const FRigVMTemplateArgument* Argument = Template->GetArgument(i);
						if (!Argument->IsSingleton())
						{
							if (URigVMPin* Pin = TemplateNode->FindPin(Argument->Name.ToString()))
							{
								TRigVMTypeIndex OldTypeIndex = Pin->GetTypeIndex();
								TRigVMTypeIndex NewTypeIndex = RigVMTypeUtils::TypeIndex::WildCard;
								while (Registry.IsArrayType(OldTypeIndex))
								{
									OldTypeIndex = Registry.GetBaseTypeFromArrayTypeIndex(OldTypeIndex);
									NewTypeIndex = Registry.GetArrayTypeFromBaseTypeIndex(NewTypeIndex);
								}
								ChangePinType(Pin, NewTypeIndex, bSetupUndoRedo, false, true, false);
							}
						}
					}
					UpdateTemplateNodePinTypes(TemplateNode, bSetupUndoRedo);
				}
			}
		}
	}

	if(bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	return true;
}

TArray<URigVMNode*> URigVMController::UpgradeNodes(const TArray<FName>& InNodeNames, bool bRecursive, bool bSetupUndoRedo,
                                                   bool bPrintPythonCommand)
{
	TArray<URigVMNode*> Nodes;
	if (!IsValidGraph())
	{
		return Nodes;
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return Nodes;
	}

	for (const FName& NodeName : InNodeNames)
	{
		if (URigVMNode* Node = GetGraph()->FindNodeByName(NodeName))
		{
			Nodes.Add(Node);
		}
	}

	Nodes = UpgradeNodes(Nodes, bRecursive, bSetupUndoRedo);

	if(bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
		TArray<FString> NodeNames;
		for(const FName& NodeName : InNodeNames)
		{
			NodeNames.Add(GetSanitizedNodeName(NodeName.ToString()));
		}
		const FString NodeNamesJoined = FString::Join(NodeNames, TEXT("','"));

		// UpgradeNodes(const TArray<FName>& InNodeNames)
		RigVMPythonUtils::Print(GetGraphOuterName(),
				FString::Printf(TEXT("blueprint.get_controller_by_name('%s').upgrade_nodes(['%s'])"),
				*GraphName,
				*NodeNamesJoined));
	}

	// log a warning for all nodes which are still marked deprecated
	for(URigVMNode* Node : Nodes)
	{
		if(URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(Node))
		{
			if(UnitNode->IsDeprecated())
			{
				ReportWarningf(TEXT("Node %s cannot be upgraded. There is no automatic upgrade path available."), *UnitNode->GetNodePath());
			}
		}
	}

	return Nodes;
}

TArray<URigVMNode*> URigVMController::UpgradeNodes(const TArray<URigVMNode*>& InNodes, bool bRecursive, bool bSetupUndoRedo)
{
	if (!IsValidGraph())
	{
		return TArray<URigVMNode*>();
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return TArray<URigVMNode*>();
	}

	bool bFoundAnyNodeToUpgrade = false;
	for(URigVMNode* Node : InNodes)
	{
		if(!IsValidNodeForGraph(Node))
		{
			return TArray<URigVMNode*>();
		}

		bFoundAnyNodeToUpgrade |= Node->CanBeUpgraded();
	}

	if(!bFoundAnyNodeToUpgrade)
	{
		return InNodes;
	}

	FRigVMBaseAction Action;
	if(bSetupUndoRedo)
	{
		Action.Title = TEXT("Upgrade nodes");
		ActionStack->BeginAction(Action);
	}

	// find all links affecting the nodes to upgrade
	TArray<TPair<FString, FString>> LinkedPaths = GetLinkedPinPaths(InNodes);
	if(!BreakLinkedPaths(LinkedPaths, bSetupUndoRedo))
	{
		if(bSetupUndoRedo)
		{
			ActionStack->CancelAction(Action, this);
		}
		return TArray<URigVMNode*>();
	}

	TArray<URigVMNode*> UpgradedNodes;
	TMap<FString,FRigVMController_PinPathRemapDelegate> RemapPinDelegates;
	for(URigVMNode* Node : InNodes)
	{
		FRigVMController_PinPathRemapDelegate RemapPinDelegate;
		URigVMNode* UpgradedNode = UpgradeNode(Node, bSetupUndoRedo, &RemapPinDelegate);
		if(UpgradedNode)
		{
			UpgradedNodes.Add(UpgradedNode);
			if(RemapPinDelegate.IsBound())
			{
				RemapPinDelegates.Add(UpgradedNode->GetName(), RemapPinDelegate);
			}
		}
	}

	RestoreLinkedPaths(LinkedPaths, {}, RemapPinDelegates, bSetupUndoRedo);

	if(bRecursive)
	{
		UpgradedNodes = UpgradeNodes(UpgradedNodes, bRecursive, bSetupUndoRedo);
	}

	if(bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	return UpgradedNodes;
}

URigVMNode* URigVMController::UpgradeNode(URigVMNode* InNode, bool bSetupUndoRedo, FRigVMController_PinPathRemapDelegate* OutRemapPinDelegate)
{
	if(!IsValidNodeForGraph(InNode))
	{
		return nullptr;
	}

	if(!InNode->CanBeUpgraded())
	{
		return InNode; 
	}

	TMap<FString, FString> RedirectedPinPaths;
	TMap<FString, FPinState> PinStates = GetPinStates(InNode, true);
	EjectAllInjectedNodes(InNode, bSetupUndoRedo);

	const FString NodeName = InNode->GetName();
	const FVector2D NodePosition = InNode->GetPosition();

	FRigVMBaseAction Action;
	if(bSetupUndoRedo)
	{
		Action.Title = TEXT("Upgrade node");
		ActionStack->BeginAction(Action);
	}

	URigVMNode* UpgradedNode = nullptr;

	const FRigVMStructUpgradeInfo UpgradeInfo = InNode->GetUpgradeInfo();
	check(UpgradeInfo.IsValid());
	
	FName MethodName = TEXT("Execute");
	if(URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(InNode))
	{
		MethodName = UnitNode->GetMethodName();
	}

	if(OutRemapPinDelegate)
	{
		*OutRemapPinDelegate = FRigVMController_PinPathRemapDelegate::CreateLambda([UpgradeInfo](const FString& InPinPath, bool bIsInput) -> FString
		{
			return UpgradeInfo.RemapPin(InPinPath, bIsInput, true);
		});
	}

	if(!RemoveNode(InNode, bSetupUndoRedo, true, false, false))
	{
		if(bSetupUndoRedo)
		{
			ActionStack->CancelAction(Action, this);
		}
		ReportErrorf(TEXT("Unable to remove node %s."), *NodeName);
		return nullptr;
	}

	URigVMNode* NewNode = nullptr;
	if(UpgradeInfo.GetNewStruct()->IsChildOf(FRigVMStruct::StaticStruct()))
	{
		NewNode = AddUnitNode(UpgradeInfo.GetNewStruct(), MethodName, NodePosition, NodeName, bSetupUndoRedo, false);
	}
	else if(UpgradeInfo.GetNewStruct()->IsChildOf(FRigVMDispatchFactory::StaticStruct()) && !UpgradeInfo.NewDispatchFunction.IsNone())
	{
		if(const FRigVMFunction* Function = FRigVMRegistry::Get().FindFunction(*UpgradeInfo.NewDispatchFunction.ToString()))
		{
			if(const FRigVMTemplate* Template = Function->GetTemplate())
			{
				if(const FRigVMDispatchFactory* Factory = Template->GetDispatchFactory())
				{
					if(Factory->GetScriptStruct() == UpgradeInfo.GetNewStruct())
					{
						NewNode = AddTemplateNode(Template->GetNotation(), NodePosition, NodeName, bSetupUndoRedo, false);
						if(NewNode)
						{
							for(int32 ArgumentIndex=0;ArgumentIndex<Function->GetArguments().Num();ArgumentIndex++)
							{
								const FRigVMFunctionArgument& Argument = Function->GetArguments()[ArgumentIndex];
								if(URigVMPin* Pin = NewNode->FindPin(Argument.Name))
								{
									if(Pin->IsWildCard())
									{
										ResolveWildCardPin(Pin, Function->GetArgumentTypeIndices()[ArgumentIndex], bSetupUndoRedo, false);
									}
								}
							}
						}
					}
				}
			}
		}
	}
	
	if(NewNode == nullptr)
	{
		if(bSetupUndoRedo)
		{
			ActionStack->CancelAction(Action, this);
		}
		ReportErrorf(TEXT("Unable to upgrade node %s."), *NodeName);
		return nullptr;
	}

	const TArray<FString>& AggregatePins = UpgradeInfo.GetAggregatePins();
	for(const FString& AggregatePinName : AggregatePins)
	{
		const FName PreviousName = NewNode->GetFName();
		AddAggregatePin(PreviousName.ToString(), AggregatePinName, FString(), bSetupUndoRedo, false);
		NewNode = GetGraph()->FindNodeByName(PreviousName);
	}

	for(URigVMPin* Pin : NewNode->GetPins())
	{
		const FString DefaultValue = UpgradeInfo.GetDefaultValueForPin(Pin->GetFName());
		if(!DefaultValue.IsEmpty())
		{
			SetPinDefaultValue(Pin, DefaultValue, true, bSetupUndoRedo, false);

			if(FPinState* PinState = PinStates.Find(Pin->GetPinPath()))
			{
				PinState->DefaultValue.Reset();
			}
		}
	}

	// redirect pin state paths
	for(TPair<FString, FPinState>& PinState : PinStates)
	{
		for(int32 TrueFalse = 0; TrueFalse < 2; TrueFalse++)
		{
			const FString RemappedInputPath = UpgradeInfo.RemapPin(PinState.Key, TrueFalse == 0, false);
			if(RemappedInputPath != PinState.Key)
			{
				if(!RedirectedPinPaths.Contains(PinState.Key))
				{
					RedirectedPinPaths.Add(PinState.Key, RemappedInputPath);
				}
			}
		}
	}

	UpgradedNode = NewNode;
	check(UpgradedNode);

	ApplyPinStates(UpgradedNode, PinStates, RedirectedPinPaths, bSetupUndoRedo);

	if(bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	return UpgradedNode;
}

URigVMParameterNode* URigVMController::AddParameterNode(const FName& InParameterName, const FString& InCPPType, UObject* InCPPTypeObject, bool bIsInput, const FString& InDefaultValue, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	AddVariableNode(InParameterName, InCPPType, InCPPTypeObject, bIsInput, InDefaultValue, InPosition, InNodeName, bSetupUndoRedo, bPrintPythonCommand);
	ReportWarning(TEXT("AddParameterNode has been deprecated. Adding a variable node instead."));
	return nullptr;
}

URigVMParameterNode* URigVMController::AddParameterNodeFromObjectPath(const FName& InParameterName, const FString& InCPPType, const FString& InCPPTypeObjectPath, bool bIsInput, const FString& InDefaultValue, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return nullptr;
	}

	UObject* CPPTypeObject = nullptr;
	if (!InCPPTypeObjectPath.IsEmpty())
	{
		CPPTypeObject = RigVMTypeUtils::FindObjectFromCPPTypeObjectPath<UObject>(InCPPTypeObjectPath);
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

	if (!bIsTransacting && !IsGraphEditable())
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

	if (!bIsTransacting && !IsGraphEditable())
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
			ActionStack->CancelAction(Action, this);
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

	if (!bIsTransacting && !IsGraphEditable())
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

	if (!bIsTransacting && !IsGraphEditable())
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
		TGuardValue<bool> SuspendNotifications(bSuspendNotifications, true);
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

	if (!bIsTransacting && !IsGraphEditable())
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

	// 1.- Create unit node
	// 2.- Rewire links
	// 3.- Inject node into pin

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMBaseAction Action;
	if (bSetupUndoRedo)
	{
		Action.Title = FString::Printf(TEXT("Add Injected Node"));
		ActionStack->BeginAction(Action);
	}

	// 1.- Create unit node
	URigVMUnitNode* UnitNode = nullptr;
	URigVMPin* InputPin = nullptr;
	URigVMPin* OutputPin = nullptr;
	{
		{
			TGuardValue<bool> GuardNotifications(bSuspendNotifications, true);
			UnitNode = AddUnitNode(InScriptStruct, InMethodName, FVector2D::ZeroVector, InNodeName, bSetupUndoRedo);
		}
		if (UnitNode == nullptr)
		{
			if (bSetupUndoRedo)
			{
				ActionStack->CancelAction(Action, this);
			}
			return nullptr;
		}
		else if (UnitNode->IsMutable())
		{
			ReportErrorf(TEXT("Injected node %s is mutable."), *InScriptStruct->GetName());
			RemoveNode(UnitNode, false);
			if (bSetupUndoRedo)
			{
				ActionStack->CancelAction(Action, this);
			}
			return nullptr;
		}

		InputPin = UnitNode->FindPin(InInputPinName.ToString());
		check(InputPin);
		OutputPin = UnitNode->FindPin(InOutputPinName.ToString());
		check(OutputPin);

		if (InputPin->GetCPPType() != OutputPin->GetCPPType() ||
			InputPin->IsArray() != OutputPin->IsArray())
		{
			ReportErrorf(TEXT("Injected node %s is using incompatible input and output pins."), *InScriptStruct->GetName());
			if (bSetupUndoRedo)
			{
				ActionStack->CancelAction(Action, this);
			}
			return nullptr;
		}

		if (InputPin->GetCPPType() != Pin->GetCPPType() ||
			InputPin->IsArray() != Pin->IsArray())
		{
			ReportErrorf(TEXT("Injected node %s is using incompatible pin."), *InScriptStruct->GetName());
			if (bSetupUndoRedo)
			{
				ActionStack->CancelAction(Action, this);
			}
			return nullptr;
		}
	}

	// 2.- Rewire links
	TArray<URigVMLink*> NewLinks;
	{
		URigVMPin* PreviousInputPin = Pin;
		URigVMPin* PreviousOutputPin = Pin;
		if (Pin->InjectionInfos.Num() > 0)
		{
			PreviousInputPin = Pin->InjectionInfos.Last()->InputPin;
			PreviousOutputPin = Pin->InjectionInfos.Last()->OutputPin;
		}
		if (bAsInput)
		{
			FString PinDefaultValue = PreviousInputPin->GetDefaultValue();
			if (!PinDefaultValue.IsEmpty())
			{
				SetPinDefaultValue(InputPin, PinDefaultValue, true, bSetupUndoRedo, false);
			}
			TArray<URigVMLink*> Links = PreviousInputPin->GetSourceLinks(true /* recursive */);
			if (Links.Num() > 0)
			{
				RewireLinks(PreviousInputPin, InputPin, true, bSetupUndoRedo, Links);
				NewLinks = InputPin->GetSourceLinks();
			}
			AddLink(OutputPin, PreviousInputPin, bSetupUndoRedo);
		}
		else
		{
			TArray<URigVMLink*> Links = PreviousOutputPin->GetTargetLinks(true /* recursive */);
			if (Links.Num() > 0)
			{
				RewireLinks(PreviousOutputPin, OutputPin, false, bSetupUndoRedo, Links);
				NewLinks = OutputPin->GetTargetLinks();
			}
			AddLink(PreviousOutputPin, InputPin, bSetupUndoRedo);
		}
	}

	// 3.- Inject node into pin
	URigVMInjectionInfo* InjectionInfo = InjectNodeIntoPin(InPinPath, bAsInput, InInputPinName, InOutputPinName, bSetupUndoRedo);

	if (!bSuspendNotifications)
	{
		Graph->MarkPackageDirty();
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

	if (!bIsTransacting && !IsGraphEditable())
	{
		return nullptr;
	}

	UScriptStruct* ScriptStruct = RigVMTypeUtils::FindObjectFromCPPTypeObjectPath<UScriptStruct>(InScriptStructPath);
	if (ScriptStruct == nullptr)
	{
		ReportErrorf(TEXT("Cannot find struct for path '%s'."), *InScriptStructPath);
		return nullptr;
	}

	return AddInjectedNode(InPinPath, bAsInput, ScriptStruct, InMethodName, InInputPinName, InOutputPinName, InNodeName, bSetupUndoRedo);
}

bool URigVMController::RemoveInjectedNode(const FString& InPinPath, bool bAsInput, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return false;
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot add injected nodes to function library graphs."));
		return false;
	}

	URigVMPin* Pin = Graph->FindPin(InPinPath);
	if (Pin == nullptr)
	{
		return false;
	}

	if (!Pin->HasInjectedNodes())
	{
		return false;
	}


	// 1.- Eject node
	// 2.- Rewire links
	// 3.- Remove node

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMBaseAction Action;
	if (bSetupUndoRedo)
	{
		Action.Title = FString::Printf(TEXT("Remove Injected Node"));
		ActionStack->BeginAction(Action);
	}

	URigVMInjectionInfo* InjectionInfo = Pin->InjectionInfos.Last();
	URigVMPin* InputPin = InjectionInfo->InputPin;
	URigVMPin* OutputPin = InjectionInfo->OutputPin;

	// 1.- Eject node
	URigVMNode* NodeEjected = EjectNodeFromPin(InPinPath, bSetupUndoRedo);
	if (!NodeEjected)
	{
		ActionStack->CancelAction(Action, this);
		return false;
	}

	// 2.- Rewire links
	if (bAsInput)
	{
		BreakLink(OutputPin, Pin, bSetupUndoRedo);
		if (InputPin)
		{
			TArray<URigVMLink*> Links = InputPin->GetSourceLinks();
			RewireLinks(InputPin, Pin, true, bSetupUndoRedo, Links);
		}
	}
	else
	{
		BreakLink(Pin, InputPin, bSetupUndoRedo);
		TArray<URigVMLink*> Links = InputPin->GetTargetLinks();
		RewireLinks(OutputPin, Pin, false, bSetupUndoRedo, Links);
	}
	
	// 3.- Remove node
	if (!RemoveNode(NodeEjected))
	{
		ActionStack->CancelAction(Action, this);
		return false;
	}

	if (!bSuspendNotifications)
	{
		Graph->MarkPackageDirty();
	}
	
	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	if (bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
		
		RigVMPythonUtils::Print(GetGraphOuterName(), 
							FString::Printf(TEXT("blueprint.get_controller_by_name('%s').remove_injected_node('%s', %s)"),
											*GraphName,
											*GetSanitizedPinPath(InPinPath),
											(bAsInput) ? TEXT("True") : TEXT("False")));
	}

	return true;
}

URigVMInjectionInfo* URigVMController::InjectNodeIntoPin(const FString& InPinPath, bool bAsInput, const FName& InInputPinName, const FName& InOutputPinName, bool bSetupUndoRedo)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMPin* Pin = Graph->FindPin(InPinPath);
	if (!Pin)
	{
		return nullptr;
	}

	return InjectNodeIntoPin(Pin, bAsInput, InInputPinName, InOutputPinName, bSetupUndoRedo);
}

URigVMInjectionInfo* URigVMController::InjectNodeIntoPin(URigVMPin* InPin, bool bAsInput, const FName& InInputPinName, const FName& InOutputPinName, bool bSetupUndoRedo)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return nullptr;
	}
	
	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot inject nodes in function library graphs."));
		return nullptr;
	}

	URigVMPin* PinForLink = InPin->GetPinForLink();

	URigVMNode* NodeToInject = nullptr;
	TArray<URigVMPin*> ConnectedPins = bAsInput ? PinForLink->GetLinkedSourcePins(true) : PinForLink->GetLinkedTargetPins(true);
	if (ConnectedPins.Num() < 1)
	{
		ReportErrorf(TEXT("Cannot find node connected to pin '%s' as %s."), *InPin->GetPinPath(), bAsInput ? TEXT("input") : TEXT("output"));
		return nullptr;
	}

	NodeToInject = ConnectedPins[0]->GetNode();
	for (int32 i = 1; i < ConnectedPins.Num(); ++i)
	{
		if (ConnectedPins[i]->GetNode() != NodeToInject)
		{
			ReportErrorf(TEXT("Found more than one node connected to pin '%s' as %s."), *InPin->GetPinPath(), bAsInput ? TEXT("input") : TEXT("output"));
			return nullptr;
		}
	}

	URigVMPin* InputPin = nullptr;
	URigVMPin* OutputPin = nullptr;
	if (NodeToInject->IsA<URigVMUnitNode>())
	{
		InputPin = NodeToInject->FindPin(InInputPinName.ToString());
		if (!InputPin)
		{
			ReportErrorf(TEXT("Could not find pin '%s' in node %s."), *InInputPinName.ToString(), *NodeToInject->GetNodePath());
			return nullptr;
		}
	}
	OutputPin = NodeToInject->FindPin(InOutputPinName.ToString());
	if (!OutputPin)
	{
		ReportErrorf(TEXT("Could not find pin '%s' in node %s."), *InOutputPinName.ToString(), *NodeToInject->GetNodePath());
		return nullptr;
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMBaseAction Action;
	if (bSetupUndoRedo)
	{
		Action.Title = FString::Printf(TEXT("Inject Node"));
		ActionStack->BeginAction(Action);
	}
	
	URigVMInjectionInfo* InjectionInfo = NewObject<URigVMInjectionInfo>(InPin);
	{
		Notify(ERigVMGraphNotifType::NodeRemoved, NodeToInject);
		
		// re-parent the unit node to be under the injection info
		RenameObject(NodeToInject, nullptr, InjectionInfo);
		
		InjectionInfo->Node = NodeToInject;
		InjectionInfo->bInjectedAsInput = bAsInput;
		InjectionInfo->InputPin = InputPin;
		InjectionInfo->OutputPin = OutputPin;
	
		InPin->InjectionInfos.Add(InjectionInfo);

		Notify(ERigVMGraphNotifType::NodeAdded, NodeToInject);
	}

	// Notify the change in links (after the node is injected)
	{
		TArray<URigVMLink*> NewLinks;
		if (bAsInput)
		{
			if (InputPin)
			{
				NewLinks = InputPin->GetSourceLinks();
			}
		}
		else
		{
			NewLinks = OutputPin->GetTargetLinks();
		}
		for (URigVMLink* Link : NewLinks)
		{
			Notify(ERigVMGraphNotifType::LinkAdded, Link);
		}
	}

	if (!bSuspendNotifications)
	{
		Graph->MarkPackageDirty();
	}

	if (bSetupUndoRedo)
	{
		ActionStack->AddAction(FRigVMInjectNodeIntoPinAction(InjectionInfo));
		ActionStack->EndAction(Action);
	}

	return InjectionInfo;
}

URigVMNode* URigVMController::EjectNodeFromPin(const FString& InPinPath, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMPin* Pin = Graph->FindPin(InPinPath);
	if (!Pin)
	{
		return nullptr;
	}

	return EjectNodeFromPin(Pin, bSetupUndoRedo, bPrintPythonCommand);
}

URigVMNode* URigVMController::EjectNodeFromPin(URigVMPin* InPin, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	if (!bIsTransacting && !IsGraphEditable())
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

	if (!InPin->HasInjectedNodes())
	{
		ReportErrorf(TEXT("Pin '%s' has no injected nodes."), *InPin->GetPinPath());
		return nullptr;
	}

	URigVMInjectionInfo* Injection = InPin->InjectionInfos.Last();

	
	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMInverseAction InverseAction;
	if (bSetupUndoRedo)
	{
		InverseAction.Title = TEXT("Eject node");

		ActionStack->BeginAction(InverseAction);
		ActionStack->AddAction(FRigVMInjectNodeIntoPinAction(Injection));
	}

	FVector2D Position = InPin->GetNode()->GetPosition() + FVector2D(0.f, 12.f) * float(InPin->GetPinIndex());
	if (InPin->GetDirection() == ERigVMPinDirection::Output)
	{
		Position += FVector2D(250.f, 0.f);
	}
	else
	{
		Position -= FVector2D(250.f, 0.f);
	}


	URigVMNode* NodeToEject = Injection->Node;
	URigVMPin* InputPin = Injection->InputPin;
	URigVMPin* OutputPin = Injection->OutputPin;
	Notify(ERigVMGraphNotifType::NodeRemoved, NodeToEject);
	if (Injection->bInjectedAsInput)
	{
		if (InputPin)
		{
			TArray<URigVMLink*> SourceLinks = InputPin->GetSourceLinks(true);
			if (SourceLinks.Num() > 0)
			{
				Notify(ERigVMGraphNotifType::LinkRemoved, SourceLinks[0]);
			}
		}
	}
	else
	{
		TArray<URigVMLink*> TargetLinks = OutputPin->GetTargetLinks(true);
		if (TargetLinks.Num() > 0)
		{
			Notify(ERigVMGraphNotifType::LinkRemoved, TargetLinks[0]);
		}
	}
	
	
	RenameObject(NodeToEject, nullptr, Graph);
	SetNodePosition(NodeToEject, Position, false);
	InPin->InjectionInfos.Remove(Injection);
	DestroyObject(Injection);

	Notify(ERigVMGraphNotifType::NodeAdded, NodeToEject);
	if (InputPin)
	{
		TArray<URigVMLink*> SourceLinks = InputPin->GetSourceLinks(true);
		if (SourceLinks.Num() > 0)
		{
			Notify(ERigVMGraphNotifType::LinkAdded, SourceLinks[0]);
		}
	}
	TArray<URigVMLink*> TargetLinks = OutputPin->GetTargetLinks(true);
	if (TargetLinks.Num() > 0)
	{
		Notify(ERigVMGraphNotifType::LinkAdded, TargetLinks[0]);
	}
		
	if (!bSuspendNotifications)
	{
		Graph->MarkPackageDirty();
	}

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(InverseAction);
	}

	if (bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());

		RigVMPythonUtils::Print(GetGraphOuterName(), 
							FString::Printf(TEXT("blueprint.get_controller_by_name('%s').eject_node_from_pin('%s')"),
											*GraphName,
											*GetSanitizedPinPath(InPin->GetPinPath())));
	}

	return NodeToEject;
}

bool URigVMController::EjectAllInjectedNodes(URigVMNode* InNode, bool bSetupUndoRedo, bool bPrintPythonCommands)
{
	if(!IsValidNodeForGraph(InNode))
	{
		return false;
	}

	bool bHasAnyInjectedNode = false;
	for(URigVMPin* Pin : InNode->GetPins())
	{
		bHasAnyInjectedNode = bHasAnyInjectedNode || Pin->HasInjectedNodes();
	}

	if(!bHasAnyInjectedNode)
	{
		return false;
	}

	FRigVMBaseAction EjectAllInjectedNodesAction;
	if (bSetupUndoRedo)
	{
		ActionStack->BeginAction(EjectAllInjectedNodesAction);
	}

	for(URigVMPin* Pin : InNode->GetPins())
	{
		if(Pin->HasInjectedNodes())
		{
			if(!EjectNodeFromPin(Pin, bSetupUndoRedo, bPrintPythonCommands))
			{
				return false;
			}
		}
	}

	if(bSetupUndoRedo)
	{
		ActionStack->EndAction(EjectAllInjectedNodesAction);
	}

	return true;
}


bool URigVMController::Undo()
{
	if (!IsValidGraph())
	{
		return false;
	}

	return ActionStack->Undo(this);
}

bool URigVMController::Redo()
{
	if (!IsValidGraph())
	{
		return false;
	}

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
	return ActionStack->CloseUndoBracket(this);
}

bool URigVMController::CancelUndoBracket()
{
	if (!IsValidGraph())
	{
		return false;
	}
	return ActionStack->CancelUndoBracket(this);
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
		if (SourcePin && TargetPin && IsValid(SourcePin) && IsValid(TargetPin))
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
	TArray<FName> CreateNodeNames;
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
			URigVMGraph* Graph = Controller->GetGraph();
			check(Graph);

			const FName ValidName = Controller->GetUniqueName(InOutObjName, [Graph, this](const FName& InName) {
				return !CreateNodeNames.Contains(InName) && Graph->IsNameAvailable(InName.ToString());
			}, false, true);
			
			NodeNameMap.Add(InOutObjName, ValidName);
			CreateNodeNames.Add(ValidName);
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

	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}

	FRigVMControllerObjectFactory Factory(this);
	if (!Factory.CanCreateObjectsFromText(InText))
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);
	Factory.ProcessBuffer(Graph, RF_Transactional, InText);

	if (Factory.CreatedNodes.Num() == 0)
	{
		return false;
	}

	// If the graph is a function library, make sure we are pasting unconnected collapse nodes
	if (Graph->IsA<URigVMFunctionLibrary>())
	{
		if (!Factory.CreatedLinks.IsEmpty())
		{
			return false;
		}
		
		for (URigVMNode* Node : Factory.CreatedNodes)
		{
			if (!Node->IsA<URigVMCollapseNode>())
			{
				return false;
			}
		}
	}

	return true;
}

TArray<FName> URigVMController::ImportNodesFromText(const FString& InText, bool bSetupUndoRedo, bool bPrintPythonCommands)
{
	TArray<FName> NodeNames;
	if (!IsValidGraph())
	{
		return NodeNames;
	}

	if (!bIsTransacting && !IsGraphEditable())
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

	TArray<TGuardValue<bool>> EditGuards;
	TArray<URigVMNode*> CreatedNodes = Factory.CreatedNodes;
	for (int32 i=0; i<CreatedNodes.Num(); ++i)
	{
		URigVMNode* CreatedNode = CreatedNodes[i];
		if (URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(CreatedNode))
		{
			if (URigVMGraph* ContainedGraph = CollapseNode->GetContainedGraph())
			{
				EditGuards.Emplace(ContainedGraph->bEditable, true);
				CreatedNodes.Append(ContainedGraph->GetNodes());
			}
		}
	}

	FRigVMUnitNodeCreatedContext::FScope UnitNodeCreatedScope(UnitNodeCreatedContext, ERigVMNodeCreatedReason::Paste);
	TMap<URigVMPin*, TRigVMTypeIndex> TypesWhenPasted;
	for (URigVMNode* CreatedNode : Factory.CreatedNodes)
	{
		if(!CanAddNode(CreatedNode, true))
		{
			continue;
		}

		Graph->Nodes.Add(CreatedNode);

		if (bSetupUndoRedo)
		{
			if (!CreatedNode->IsInjected() || !CreatedNode->IsA<URigVMVariableNode>())
			{
				ActionStack->AddAction(FRigVMRemoveNodeAction(CreatedNode, this));
			}
		}

		// find all nodes affected by this
		TArray<URigVMNode*> SubNodes;
		SubNodes.Add(CreatedNode);

		// Refresh the unit nodes to account for changes in node color, pin additions, pin order, etc
		for(int32 SubNodeIndex=0; SubNodeIndex < SubNodes.Num(); SubNodeIndex++)
		{
			if (URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(SubNodes[SubNodeIndex]))
			{
				TGuardValue<bool> SuspendNotifications(bSuspendNotifications, true);
				RepopulatePinsOnNode(UnitNode);
			}
		}

		for(int32 SubNodeIndex=0; SubNodeIndex < SubNodes.Num(); SubNodeIndex++)
		{
			if(URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(SubNodes[SubNodeIndex]))
			{
				{
					FRigVMControllerGraphGuard GraphGuard(this, CollapseNode->GetContainedGraph(), false);
					TGuardValue<bool> GuardEditGraph(CollapseNode->ContainedGraph->bEditable, true);
					TGuardValue<bool> SuspendNotifications(bSuspendNotifications, true);
					ReattachLinksToPinObjects();
				}
				
				SubNodes.Append(CollapseNode->GetContainedNodes());
			}
		}

		if (URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(CreatedNode))
		{
			TFunction<void(URigVMLibraryNode*)> Recompute = [&](URigVMLibraryNode* OuterNode)
			{
				for (URigVMNode* Node : OuterNode->GetContainedNodes())
				{
					if (URigVMLibraryNode* ContainedNode = Cast<URigVMLibraryNode>(Node))
					{
						Recompute(ContainedNode);
					}
				}

				// If the OuterNode is referencing a non-existing function, the contained graph might be empty
				if (OuterNode->GetContainedGraph() == nullptr)
				{
					return;
				}
			};
			Recompute(LibraryNode);
		}

		for(URigVMNode* SubNode : SubNodes)
		{
			FRigVMControllerGraphGuard GraphGuard(this, SubNode->GetGraph(), false);
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
				if(URigVMBuildData* BuildData = URigVMBuildData::Get())
				{
					BuildData->RegisterFunctionReference(FunctionRefNode->GetReferencedFunctionHeader().LibraryPointer, FunctionRefNode);
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

					if (SourcePin == nullptr)
					{
						URigVMNode* OriginalNode = Graph->FindNode(SourceLeft);
						if (OriginalNode)
						{
							FString OldSourcePinPath = URigVMPin::JoinPinPath(SourceLeft, SourceRight);
							if (URigVMPin* OldPin = Graph->FindPin(OldSourcePinPath))
							{
								if (OldPin->IsStructMember())
								{
									URigVMPin* OldRootPin = OldPin->GetRootPin();
									FString NewSourceRootPinPath = URigVMPin::JoinPinPath(NewSourceNodeName->ToString(), OldRootPin->GetName());
									if (URigVMPin* NewRootPin = Graph->FindPin(NewSourceRootPinPath))
									{
										SourcePin = CreatedLink->GetSourcePin();
									}
								}
							}
						}
					}
					if (TargetPin == nullptr)
					{
						URigVMNode* OriginalNode = Graph->FindNode(TargetLeft);
						if (OriginalNode)
						{
							FString OldTargetPinPath = URigVMPin::JoinPinPath(TargetLeft, TargetRight);
							if (URigVMPin* OldPin = Graph->FindPin(OldTargetPinPath))
							{
								if (OldPin->IsStructMember())
								{
									URigVMPin* OldRootPin = OldPin->GetRootPin();
									FString NewTargetRootPinPath = URigVMPin::JoinPinPath(NewTargetNodeName->ToString(), OldRootPin->GetName());
									if (URigVMPin* NewRootPin = Graph->FindPin(NewTargetRootPinPath))
									{
										TargetPin = CreatedLink->GetTargetPin();
									}
								}
							}
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

							// The current situation is that the outer pin has an injection info, and the injected node exists
							// but the injected node is not linked to the outer pin. BreakAllLinks will try to unbind the outer pin,
							// for that to be successful, the binding needs to be complete
							// Connect it so that the unbound is successful
							if (!SourcePin->IsLinkedTo(TargetPin))
							{
								Graph->Links.Add(CreatedLink);
								SourcePin->Links.Add(CreatedLink);
								TargetPin->Links.Add(CreatedLink);
							}

							if (TargetPin->IsBoundToVariable())
							{
								UnbindPinFromVariable(TargetPin, bSetupUndoRedo);
							}
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
							PrepareToLink(TargetPin, SourcePin, bSetupUndoRedo);

							Graph->Links.Add(CreatedLink);
							SourcePin->Links.Add(CreatedLink);
							TargetPin->Links.Add(CreatedLink);

							if (bSetupUndoRedo)
							{
								ActionStack->AddAction(FRigVMAddLinkAction(SourcePin, TargetPin));
								if (SourcePin->GetNode()->IsInjected())
								{
									ActionStack->AddAction(FRigVMInjectNodeIntoPinAction(SourcePin->GetTypedOuter<URigVMInjectionInfo>()));	
								}
								if (TargetPin->GetNode()->IsInjected())
								{
									ActionStack->AddAction(FRigVMInjectNodeIntoPinAction(TargetPin->GetTypedOuter<URigVMInjectionInfo>()));	
								}
							}
							Notify(ERigVMGraphNotifType::LinkAdded, CreatedLink);
						}
						continue;
					}
				}
			}

			ReportErrorf(TEXT("Cannot import link '%s'."), *URigVMLink::GetPinPathRepresentation(CreatedLink->SourcePinPath, CreatedLink->TargetPinPath));
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

URigVMLibraryNode* URigVMController::LocalizeFunctionFromPath(const FString& InHostPath, const FName& InFunctionName, bool bLocalizeDependentPrivateFunctions, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	if (!bIsTransacting && !IsGraphEditable())
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

	UObject* HostObject = StaticLoadObject(UObject::StaticClass(), NULL, *InHostPath, NULL, LOAD_None, NULL);
	if (!HostObject)
	{
		ReportErrorf(TEXT("Failed to load the Host object %s."), *InHostPath);
		return nullptr;
	}

	IRigVMGraphFunctionHost* FunctionHost = Cast<IRigVMGraphFunctionHost>(HostObject);
	if (!FunctionHost)
	{
		ReportError(TEXT("Host object is not a IRigVMGraphFunctionHost."));
		return nullptr;
	}

	FRigVMGraphFunctionData* Data = FunctionHost->GetRigVMGraphFunctionStore()->FindFunctionByName(InFunctionName);
	if (!Data)
	{
		ReportErrorf(TEXT("Function %s not found in host %s."), *InFunctionName.ToString(), *InHostPath);
		return nullptr;
	}

	return LocalizeFunction(Data->Header.LibraryPointer, bLocalizeDependentPrivateFunctions, bSetupUndoRedo, bPrintPythonCommand);
}

URigVMLibraryNode* URigVMController::LocalizeFunction(
	const FRigVMGraphFunctionIdentifier& InFunctionDefinition,
	bool bLocalizeDependentPrivateFunctions,
	bool bSetupUndoRedo,
	bool bPrintPythonCommand)
{
	if(!IsValidGraph())
	{
		return nullptr;
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return nullptr;
	}

	TArray<FRigVMGraphFunctionIdentifier> FunctionsToLocalize;
	FunctionsToLocalize.Add(InFunctionDefinition);

	TMap<FRigVMGraphFunctionIdentifier, URigVMLibraryNode*> Results = LocalizeFunctions(FunctionsToLocalize, bLocalizeDependentPrivateFunctions, bSetupUndoRedo, bPrintPythonCommand);

	URigVMLibraryNode** LocalizedFunctionPtr = Results.Find(FunctionsToLocalize[0]);
	if(LocalizedFunctionPtr)
	{
		return *LocalizedFunctionPtr;
	}
	return nullptr;
}

TMap<FRigVMGraphFunctionIdentifier, URigVMLibraryNode*> URigVMController::LocalizeFunctions(
	TArray<FRigVMGraphFunctionIdentifier> InFunctionDefinitions,
	bool bLocalizeDependentPrivateFunctions,
	bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	TMap<FRigVMGraphFunctionIdentifier, URigVMLibraryNode*> LocalizedFunctions;

	if(!IsValidGraph())
	{
		return LocalizedFunctions;
	}

	if (!bIsTransacting && !IsGraphEditable())
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

	TArray<FRigVMGraphFunctionData*> FunctionsToLocalize;

	TArray<FRigVMGraphFunctionIdentifier> NodesToVisit;
	for(const FRigVMGraphFunctionIdentifier& FunctionDefinition : InFunctionDefinitions)
	{
		NodesToVisit.AddUnique(FunctionDefinition);
		FunctionsToLocalize.AddUnique(FRigVMGraphFunctionData::FindFunctionData(FunctionDefinition));
	}

	const FSoftObjectPath ThisFunctionHost = ThisLibrary->GetFunctionHostObjectPath();
	
	// find all functions to localize
	for(int32 NodeToVisitIndex=0; NodeToVisitIndex<NodesToVisit.Num(); NodeToVisitIndex++)
	{
		FRigVMGraphFunctionIdentifier& NodeToVisit = NodesToVisit[NodeToVisitIndex];

		// Already local
		if (NodeToVisit.HostObject == ThisFunctionHost)
		{
			continue;
		}

		bool bIsPublic;
		FRigVMGraphFunctionData* FunctionData = FRigVMGraphFunctionData::FindFunctionData(NodeToVisit, &bIsPublic);
		if (!FunctionData)
		{
			ReportAndNotifyErrorf(TEXT("Cannot localize function - could not find function %s in host %s."), *NodeToVisit.LibraryNode.ToString(), *NodeToVisit.HostObject.ToString());
			return LocalizedFunctions;
		}

		// Do not localize public functions
		if (bIsPublic)
		{
			continue;
		}

		if (!bLocalizeDependentPrivateFunctions)
		{
			ReportAndNotifyErrorf(TEXT("Cannot localize function - dependency %s is private."), *NodeToVisit.LibraryNode.ToString());
			return LocalizedFunctions;
		}

		FunctionsToLocalize.AddUnique(FunctionData);

		// Look for its dependencies
		for (TPair<FRigVMGraphFunctionIdentifier, uint32>& Pair : FunctionData->Header.Dependencies)
		{
			NodesToVisit.AddUnique(Pair.Key);
		}
	}
	
	// sort the functions to localize based on their nesting
	Algo::Sort(FunctionsToLocalize, [](FRigVMGraphFunctionData* A, FRigVMGraphFunctionData* B) -> bool
	{
		check(A);
		check(B);
		return B->Header.Dependencies.Contains(A->Header.LibraryPointer);
	});

	FRigVMControllerCompileBracketScope CompileScope(this);
	if (bSetupUndoRedo)
	{
		OpenUndoBracket(TEXT("Localize functions"));
	}

	// import the functions to our local function library
	{
		FRigVMControllerGraphGuard GraphGuard(this, ThisLibrary, bSetupUndoRedo);
		for(FRigVMGraphFunctionData* FunctionToLocalize : FunctionsToLocalize)
		{
			if (URigVMLibraryNode* ReferencedFunction = Cast<URigVMLibraryNode>(FunctionToLocalize->Header.LibraryPointer.LibraryNode.TryLoad()))
			{
				if (IRigVMClientHost* ClientHost = ReferencedFunction->GetImplementingOuter<IRigVMClientHost>())
				{
					ClientHost->GetRigVMClient()->UpdateGraphFunctionSerializedGraph(ReferencedFunction);
				}
			}
			
			TArray<FName> NodeNames = ImportNodesFromText(FunctionToLocalize->SerializedCollapsedNode, false);
			if (NodeNames.Num() > 0)
			{
				URigVMLibraryNode* LocalizedFunction = ThisLibrary->FindFunction(NodeNames[0]);
				LocalizedFunctions.Add(FunctionToLocalize->Header.LibraryPointer, LocalizedFunction);
				ThisLibrary->LocalizedFunctions.FindOrAdd(FunctionToLocalize->Header.LibraryPointer.LibraryNode.ToString(), LocalizedFunction);
			}
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
				URigVMLibraryNode** RemappedNodePtr = LocalizedFunctions.Find(FunctionReferenceNode->GetReferencedFunctionHeader().LibraryPointer);
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
		for (const FRigVMGraphFunctionIdentifier& Identifier : InFunctionDefinitions)
		{
			const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
			FRigVMGraphFunctionData* FunctionData = FRigVMGraphFunctionData::FindFunctionData(Identifier);
			const FString FunctionDefinitionName = GetSanitizedNodeName(FunctionData->Header.Name.ToString());

			RigVMPythonUtils::Print(GetGraphOuterName(), 
				FString::Printf(TEXT("blueprint.get_controller_by_name('%s').localize_function_from_path('%s', '%s', %s)"),
						*GraphName,
						*Identifier.HostObject.ToString(),
						*FunctionDefinitionName,
						bLocalizeDependentPrivateFunctions ? TEXT("True") : TEXT("False")));
		}
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

URigVMCollapseNode* URigVMController::CollapseNodes(const TArray<FName>& InNodeNames, const FString& InCollapseNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand, bool bIsAggregate)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	if (!bIsTransacting && !IsGraphEditable())
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

	URigVMCollapseNode* Node = CollapseNodes(Nodes, InCollapseNodeName, bSetupUndoRedo, bIsAggregate);
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

	if (!bIsTransacting && !IsGraphEditable())
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

URigVMCollapseNode* URigVMController::CollapseNodes(const TArray<URigVMNode*>& InNodes, const FString& InCollapseNodeName, bool bSetupUndoRedo, bool bIsAggregate)
{
	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (!bIsTransacting && !IsGraphEditable())
	{
		return nullptr;
	}

	if (Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot collapse nodes in function library graphs."));
		return nullptr;
	}

	if (InNodes.IsEmpty())
	{
		ReportError(TEXT("No nodes specified to collapse."));
		return nullptr;
	}

#if UE_RIGVM_AGGREGATE_NODES_ENABLED
	if (bIsAggregate)
	{
		if(InNodes.Num() != 1)
		{
			return nullptr;
		}

		if(!InNodes[0]->IsAggregate())
		{
			ReportError(TEXT("Cannot aggregate the given node."));
			return nullptr;
		}
	}
#endif

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

	// sort the links so that the links on the same node are in the right order
	Algo::Sort(LinksToRewire, [&AllLinks](URigVMLink* A, URigVMLink* B) -> bool
	{
		if(A->GetSourcePin()->GetNode() == B->GetSourcePin()->GetNode())
		{
			return A->GetSourcePin()->GetAbsolutePinIndex() < B->GetSourcePin()->GetAbsolutePinIndex();
		}
		
		if(A->GetTargetPin()->GetNode() == B->GetTargetPin()->GetNode())
		{
			return A->GetTargetPin()->GetAbsolutePinIndex() < B->GetTargetPin()->GetAbsolutePinIndex();
		}

		return AllLinks.Find(A) < AllLinks.Find(B);
	});

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMCollapseNodesAction CollapseAction;

	FString CollapseNodeName = GetValidNodeName(InCollapseNodeName.IsEmpty() ? FString(TEXT("CollapseNode")) : InCollapseNodeName);

	if (bSetupUndoRedo)
	{
		CollapseAction = FRigVMCollapseNodesAction(this, Nodes, CollapseNodeName, bIsAggregate); 
		CollapseAction.Title = TEXT("Collapse Nodes");
		ActionStack->BeginAction(CollapseAction);
	}

#if UE_RIGVM_AGGREGATE_NODES_ENABLED
	URigVMCollapseNode* CollapseNode = nullptr;
	if (bIsAggregate)
	{
		CollapseNode = NewObject<URigVMAggregateNode>(Graph, *CollapseNodeName);		
	}
	else
	{
		CollapseNode = NewObject<URigVMCollapseNode>(Graph, *CollapseNodeName);		
	}
#else
	URigVMCollapseNode* CollapseNode = NewObject<URigVMCollapseNode>(Graph, *CollapseNodeName);
#endif
	CollapseNode->ContainedGraph = NewObject<URigVMGraph>(CollapseNode, TEXT("ContainedGraph"));

#if UE_RIGVM_AGGREGATE_NODES_ENABLED
	if (bIsAggregate)
	{
		CollapseNode->ContainedGraph->bEditable = false;
	}
	TGuardValue<bool> GuardEditGraph(CollapseNode->ContainedGraph->bEditable, true);
#endif
	
	CollapseNode->Position = Center;
	Graph->Nodes.Add(CollapseNode);

	// now looper over the links to be rewired
	for (URigVMLink* Link : LinksToRewire)
	{
		bool bSourceToBeCollapsed = NodeToBeCollapsed(Link->GetSourcePin()->GetNode());
		bContainsOutputs = bContainsOutputs || bSourceToBeCollapsed;

		URigVMPin* PinToCollapse = bSourceToBeCollapsed ? Link->GetSourcePin() : Link->GetTargetPin();
		if (CollapsedPins.Contains(PinToCollapse))
		{
			continue;
		}

		if(PinToCollapse->IsExecuteContext() && PinToCollapse->GetDirection() == ERigVMPinDirection::IO)
		{
			for(const TPair<URigVMPin*, URigVMPin*>& Pair : CollapsedPins)
			{
				if(Pair.Key->IsExecuteContext() && Pair.Key->GetDirection() == ERigVMPinDirection::IO)
				{
					CollapsedPins.Add(PinToCollapse, Pair.Value);
					break;
				}
			}
			if (CollapsedPins.Contains(PinToCollapse))
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
		ConfigurePinFromPin(CollapsedPin, PinToCollapse, true);

		if (CollapsedPin->GetDirection() == ERigVMPinDirection::IO)
		{
			if(CollapsedPin->IsExecuteContext())
			{
				bContainsOutputs = true;
			}
			else
			{
				CollapsedPin->Direction = bSourceToBeCollapsed ? ERigVMPinDirection::Output : ERigVMPinDirection::Input;
			}
		}

		if (CollapsedPin->IsStruct())
		{
			TGuardValue<bool> SuspendNotifications(bSuspendNotifications, true);
			AddPinsForStruct(CollapsedPin->GetScriptStruct(), CollapseNode, CollapsedPin, CollapsedPin->GetDirection(), FString(), false);
		}

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
		{
			TGuardValue<bool> SuspendNotifications(bSuspendNotifications, true);
			RefreshFunctionPins(EntryNode);
		}

		Notify(ERigVMGraphNotifType::NodeAdded, EntryNode);

		if (bContainsOutputs)
		{
			ReturnNode = NewObject<URigVMFunctionReturnNode>(CollapseNode->ContainedGraph, TEXT("Return"));
			CollapseNode->ContainedGraph->Nodes.Add(ReturnNode);
			ReturnNode->Position = FVector2D(Diagonal.X, -Diagonal.Y) * 0.5f + FVector2D(300.f, 0.f);
			{
				TGuardValue<bool> SuspendNotifications(bSuspendNotifications, true);
				RefreshFunctionPins(ReturnNode);
			}

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

	if (!InCollapseNodeName.IsEmpty() && CollapseNodeName != InCollapseNodeName)
	{		
		FString ValidName = GetValidNodeName(InCollapseNodeName);
		if (ValidName == InCollapseNodeName)
		{
			RenameNode(CollapseNode, *ValidName, bSetupUndoRedo);
		}
	}

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(CollapseAction);
	}

	return CollapseNode;
}

TArray<URigVMNode*> URigVMController::ExpandLibraryNode(URigVMLibraryNode* InNode, bool bSetupUndoRedo)
{
	if (!bIsTransacting && !IsGraphEditable())
	{
		return TArray<URigVMNode*>();
	}
	
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

	URigVMGraph* InnerGraph = InNode->GetContainedGraph();
	if (URigVMFunctionReferenceNode* RefNode = Cast<URigVMFunctionReferenceNode>(InNode))
	{
		if (URigVMLibraryNode* LibraryNode = RefNode->LoadReferencedNode())
		{
			InnerGraph = LibraryNode->GetContainedGraph();
		}
		else
		{
			ReportError(TEXT("Cannot expand nodes from function reference because the source graph is not found."));
			return TArray<URigVMNode*>();			
		}
	}	

	TArray<URigVMNode*> ContainedNodes = InnerGraph->GetNodes();
	TArray<URigVMLink*> ContainedLinks = InnerGraph->GetLinks();
	if (ContainedNodes.Num() == 0)
	{
		return TArray<URigVMNode*>();
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMExpandNodeAction ExpandAction;

	if (bSetupUndoRedo)
	{
		ExpandAction = FRigVMExpandNodeAction(this, InNode);
		ExpandAction.Title = FString::Printf(TEXT("Expand '%s' Node"), *InNode->GetName());
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
			ActionStack->CancelAction(ExpandAction, this);
		}
		return TArray<URigVMNode*>();
	}

	// Find local variables that need to be added as member variables. If member variables of same name and type already
	// exist, they will be reused. If a local variable is not used, it will not be created.
	if (URigVMFunctionReferenceNode* FunctionReferenceNode = Cast<URigVMFunctionReferenceNode>(InNode))
	{
		TArray<FRigVMGraphVariableDescription> LocalVariables = InnerGraph->LocalVariables;
		TArray<FRigVMExternalVariable> CurrentVariables = GetAllVariables();
		TArray<FRigVMGraphVariableDescription> VariablesToAdd;
		for (const URigVMNode* Node : InnerGraph->GetNodes())
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
							ReportErrorf(TEXT("Found variable %s of incompatible type with a local variable inside function %s"), *LocalVariable.Name.ToString(), *FunctionReferenceNode->GetReferencedFunctionHeader().Name.ToString());
							if (bSetupUndoRedo)
							{
								ActionStack->CancelAction(ExpandAction, this);
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
		FRigVMControllerGraphGuard GraphGuard(this, InnerGraph, false);
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
	if (URigVMFunctionEntryNode* EntryNode = InnerGraph->GetEntryNode())
	{
		TArray<URigVMLink*> EntryLinks = EntryNode->GetLinks();

		for (URigVMNode* Node : InnerGraph->GetNodes())
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
	if (URigVMFunctionReturnNode* ReturnNode = InnerGraph->GetReturnNode())
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

				const FString SourcePinPath = LibraryPinPath;
				FString TargetPinPath = LibraryPinLinks[0];

				// if the pin on the library node is represented by a reroute
				// we need to remap to that instead.
				if(URigVMPin** ReroutedPinPtr = ReroutedInputPins.Find(SourcePinPath))
				{
					if(URigVMPin* ReroutedPin = *ReroutedPinPtr)
					{
						TargetPinPath = ReroutedPin->GetPinPath();
					}
				}

				Local::UpdateRemappedSourcePins(SourcePinPath, TargetPinPath, RemappedSourcePinsForInputs);
			}
		}
		if (LibraryPin->GetDirection() == ERigVMPinDirection::Output ||
			LibraryPin->GetDirection() == ERigVMPinDirection::IO)
		{
			if (const TArray<FString>* LibraryPinLinksPtr = ToReturnNode.Find(LibraryPinPath))
			{
				const TArray<FString>& LibraryPinLinks = *LibraryPinLinksPtr;
				ensure(LibraryPinLinks.Num() == 1);

				const FString SourcePinPath = LibraryPinPath;
				FString TargetPinPath = LibraryPinLinks[0];

				// if the pin on the library node is represented by a reroute
				// we need to remap to that instead.
				if(URigVMPin** ReroutedPinPtr = ReroutedOutputPins.Find(SourcePinPath))
				{
					if(URigVMPin* ReroutedPin = *ReroutedPinPtr)
					{
						TargetPinPath = ReroutedPin->GetPinPath();
					}
				}

				Local::UpdateRemappedSourcePins(SourcePinPath, TargetPinPath, RemappedSourcePinsForOutputs);
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
					if(Cast<URigVMFunctionReturnNode>(InnerGraph->FindNode(ReturnNodeName)))
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

	if (!bIsTransacting && !IsGraphEditable())
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

	if (!bIsTransacting && !IsGraphEditable())
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
	if (!bIsTransacting && !IsGraphEditable())
	{
		return nullptr;
	}
	
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

	for (URigVMPin* Pin : InCollapseNode->GetPins())
	{
		if (Pin->IsWildCard())
		{
			ReportAndNotifyErrorf(TEXT("Cannot create function %s because it contains a wildcard pin %s"), *InCollapseNode->GetName(), *Pin->GetName());
			return nullptr;
		}
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	URigVMFunctionReferenceNode* FunctionRefNode = nullptr;

	// Create Function
	URigVMLibraryNode* FunctionDefinition = nullptr;
	if (!InExistingFunctionDefinitionPath.IsEmpty() && 
		ensureAlwaysMsgf(!FPackageName::IsShortPackageName(InExistingFunctionDefinitionPath), TEXT("Expected full path name for function definition path: \"%s\"), *InExistingFunctionDefinitionPath")))
	{
		FunctionDefinition = FindObject<URigVMLibraryNode>(nullptr, *InExistingFunctionDefinitionPath);
	}

	if (FunctionDefinition == nullptr)
	{
		{
			FRigVMControllerGraphGuard GraphGuard(this, FunctionLibrary, false);
			const FString FunctionName = GetValidNodeName(InCollapseNode->GetName());
			FunctionDefinition = AddFunctionToLibrary(
				*FunctionName,
				InCollapseNode->GetPins().ContainsByPredicate([](const URigVMPin* Pin) -> bool
				{
					return Pin->IsExecuteContext() && (Pin->GetDirection() == ERigVMPinDirection::IO);
				}),
				FVector2D::ZeroVector, false);		
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
	if (!bIsTransacting && !IsGraphEditable())
	{
		return nullptr;
	}
	
	if (!IsValidNodeForGraph(InFunctionRefNode))
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	const URigVMCollapseNode* FunctionDefinition = Cast<URigVMCollapseNode>(InFunctionRefNode->LoadReferencedNode());
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

	if (bSetupUndoRedo)
	{
		ActionStack->AddAction(FRigVMPromoteNodeAction(InFunctionRefNode, NodeName, FunctionDefinition->GetPathName()));
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
			TGuardValue<bool> SuspendNotifications(bSuspendNotifications, true);
			ReattachLinksToPinObjects();

			for (URigVMNode* Node : CollapseNode->GetContainedGraph()->GetNodes())
			{
				if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
				{
					RepopulatePinsOnNode(VariableNode, true, false, true);
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

	if (!bIsTransacting && !IsGraphEditable())
	{
		return;
	}
	
	FRigVMGraphFunctionHeader OldReferencedNode = InFunctionRefNode->GetReferencedFunctionHeader();
	InFunctionRefNode->ReferencedFunctionHeader = InNewReferencedNode->GetFunctionHeader();
	
	if(!(OldReferencedNode == InFunctionRefNode->ReferencedFunctionHeader))
	{
		if(URigVMBuildData* BuildData = URigVMBuildData::Get())
		{
			BuildData->UnregisterFunctionReference(OldReferencedNode.LibraryPointer, InFunctionRefNode);
			BuildData->RegisterFunctionReference(InFunctionRefNode->ReferencedFunctionHeader.LibraryPointer, InFunctionRefNode);
		}
	}

	
	FRigVMControllerGraphGuard GraphGuard(this, InFunctionRefNode->GetGraph(), false);
	GetGraph()->Notify(ERigVMGraphNotifType::NodeReferenceChanged, InFunctionRefNode);
}

void URigVMController::RefreshFunctionPins(URigVMNode* InNode)
{
	if (InNode == nullptr)
	{
		return;
	}

	URigVMFunctionEntryNode* EntryNode = Cast<URigVMFunctionEntryNode>(InNode);
	URigVMFunctionReturnNode* ReturnNode = Cast<URigVMFunctionReturnNode>(InNode);

	if (EntryNode || ReturnNode)
	{
		RepopulatePinsOnNode(InNode, false, false, true);
	}
}

void URigVMController::ReportRemovedLink(const FString& InSourcePinPath, const FString& InTargetPinPath)
{
	if(bSuspendNotifications)
	{
		return;
	}
	if(!IsValidGraph())
	{
		return;
	}
	
	const URigVMPin* TargetPin = GetGraph()->FindPin(InTargetPinPath);
	FString TargetNodeName, TargetSegmentPath;
	if(!URigVMPin::SplitPinPathAtStart(InTargetPinPath, TargetNodeName, TargetSegmentPath))
	{
		TargetSegmentPath = InTargetPinPath;
	}
	
	ReportWarningf(TEXT("Link '%s' -> '%s' was removed."), *InSourcePinPath, *InTargetPinPath);
	SendUserFacingNotification(
		FString::Printf(TEXT("Link to target pin '%s' was removed."), *TargetSegmentPath),
		0.f, TargetPin, TEXT("MessageLog.Note")
	);
}

bool URigVMController::RemoveNode(URigVMNode* InNode, bool bSetupUndoRedo, bool bRecursive, bool bPrintPythonCommand, bool bRelinkPins)
{
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
	if (!IsValidNodeForGraph(InNode))
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (InNode->IsInjected())
	{
		URigVMInjectionInfo* InjectionInfo = InNode->GetInjectionInfo();
		if (InjectionInfo->GetPin()->GetInjectedNodes().Last() != InjectionInfo)
		{
			ReportErrorf(TEXT("Cannot remove injected node %s as it is not the last injection on the pin"), *InNode->GetNodePath());
			return false;
		}
	}

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

		if (!EjectNodeFromPin(Pin->GetPinPath(), bSetupUndoRedo))
		{
			ActionStack->CancelAction(Action, this);
			return false;
		}
		
		if (InjectionInfo->bInjectedAsInput)
		{
			if (InjectionInfo->InputPin)
			{
				URigVMPin* LastInputPin = Pin->GetPinForLink();
				RewireLinks(InjectionInfo->InputPin, LastInputPin, true, bSetupUndoRedo);
			}
		}
		else
		{
			if (InjectionInfo->OutputPin)
			{
				URigVMPin* LastOutputPin = Pin->GetPinForLink();
				RewireLinks(InjectionInfo->OutputPin, LastOutputPin, false, bSetupUndoRedo);
			}
		}
	}

	
	if (URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(InNode))
	{
		// If we are removing a reference, remove the function references to this node in the function library
		if(URigVMFunctionReferenceNode* FunctionReferenceNode = Cast<URigVMFunctionReferenceNode>(LibraryNode))
		{
			if(URigVMBuildData* BuildData = URigVMBuildData::Get())
			{
				BuildData->UnregisterFunctionReference(FunctionReferenceNode->GetReferencedFunctionHeader().LibraryPointer, FunctionReferenceNode);
			}
		}
		// If we are removing a function, remove all the references first
		else if (URigVMFunctionLibrary* FunctionLibrary = Cast<URigVMFunctionLibrary>(LibraryNode->GetGraph()))
		{
			if(URigVMBuildData* BuildData = URigVMBuildData::Get())
			{
				FRigVMGraphFunctionIdentifier Identifier = LibraryNode->GetFunctionIdentifier();
				if (const FRigVMFunctionReferenceArray* ReferencesEntry = BuildData->FindFunctionReferences(Identifier))
				{
					// make a copy since we'll be modifying the array
					TArray< TSoftObjectPtr<URigVMFunctionReferenceNode> > FunctionReferences = ReferencesEntry->FunctionReferences;
					for (const TSoftObjectPtr<URigVMFunctionReferenceNode>& FunctionReferencePtr : FunctionReferences)
					{
						if (!ReferencesEntry->FunctionReferences.Contains(FunctionReferencePtr))
						{
							continue;
						}
						
						if (FunctionReferencePtr.IsValid())
						{
							FRigVMControllerGraphGuard GraphGuard(this, FunctionReferencePtr->GetGraph(), bSetupUndoRedo);
							RemoveNode(FunctionReferencePtr.Get());
						}
					}
				}

				BuildData->GraphFunctionReferences.Remove(Identifier);
			}
			
			for(const auto& Pair : FunctionLibrary->LocalizedFunctions)
			{
				if(Pair.Value == LibraryNode)
				{
					FunctionLibrary->LocalizedFunctions.Remove(Pair.Key);
					break;
				}
			}

			if (FunctionLibrary->PublicFunctionNames.Contains(LibraryNode->GetFName()))
			{
				FunctionLibrary->PublicFunctionNames.Remove(LibraryNode->GetFName());

				if (bSetupUndoRedo)
				{
					ActionStack->AddAction(FRigVMMarkFunctionPublicAction(LibraryNode->GetFName(), true));
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
			// Remove injected nodes
			while (Pin->HasInjectedNodes())
			{
				RemoveInjectedNode(Pin->GetPinPath(), Pin->GetDirection() != ERigVMPinDirection::Output, bSetupUndoRedo);
			}
		
			// breaking links also removes injected nodes 
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
	
	if (URigVMInjectionInfo* InjectionInfo = InNode->GetInjectionInfo())
	{
		DestroyObject(InjectionInfo);
	}

	if (URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(InNode))
	{
		DestroyObject(CollapseNode->GetContainedGraph());
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

	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	return RemoveNode(Graph->FindNodeByName(InNodeName), bSetupUndoRedo, bRecursive, bPrintPythonCommand, bRelinkPins);
}

bool URigVMController::RenameNode(URigVMNode* InNode, const FName& InNewName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
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

	const FSoftObjectPath PreviousObjectPath(InNode);
	InNode->PreviousName = InNode->GetFName();
	if (!RenameObject(InNode, *ValidNewName.ToString()))
	{
		ActionStack->CancelAction(Action, this);
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
			// rename each function reference
			if(URigVMBuildData* BuildData = URigVMBuildData::Get())
			{
				BuildData->ForEachFunctionReference(LibraryNode->GetFunctionIdentifier(), [this, InNewName](URigVMFunctionReferenceNode* ReferenceNode)
				{
					FRigVMControllerGraphGuard GraphGuard(this, ReferenceNode->GetGraph(), false);
					RenameNode(ReferenceNode, InNewName, false);
				});
			}

			if (FunctionLibrary->PublicFunctionNames.Contains(InNode->PreviousName))
			{
				FunctionLibrary->PublicFunctionNames.Remove(InNode->PreviousName);
				FunctionLibrary->PublicFunctionNames.Add(ValidNewName);
			}
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
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
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

	if (!bIsTransacting && !IsGraphEditable())
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

	if (!bIsTransacting && !IsGraphEditable())
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

	if (!bIsTransacting && !IsGraphEditable())
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

	bool bSelectionChanged = false;

	TArray<FName> PreviousSelection = Graph->GetSelectNodes();
	for (const FName& PreviouslySelectedNode : PreviousSelection)
	{
		if (!InNodeNames.Contains(PreviouslySelectedNode))
		{
			if(Graph->SelectedNodes.Remove(PreviouslySelectedNode) > 0)
			{
				bSelectionChanged = true;
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
				bSelectionChanged = true;
			}
		}
	}

	if (bSetupUndoRedo)
	{
		if (bSelectionChanged)
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
			ActionStack->CancelAction(Action, this);
		}
	}

	if (bSelectionChanged)
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

	return bSelectionChanged;
}

bool URigVMController::SetNodePosition(URigVMNode* InNode, const FVector2D& InPosition, bool bSetupUndoRedo, bool bMergeUndoAction, bool bPrintPythonCommand)
{
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
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

	if (!bIsTransacting && !IsGraphEditable())
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
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
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

	if (!bIsTransacting && !IsGraphEditable())
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
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}

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

	if (!bIsTransacting && !IsGraphEditable())
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
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
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

	if (!bIsTransacting && !IsGraphEditable())
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
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
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

	if (!bIsTransacting && !IsGraphEditable())
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
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
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

	if (!bIsTransacting && !IsGraphEditable())
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
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
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

	if (!bIsTransacting && !IsGraphEditable())
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
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
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

	if (!bIsTransacting && !IsGraphEditable())
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

	if (!bIsTransacting && !IsGraphEditable())
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
			ActionStack->CancelAction(Action, this);
		}
	}

	return RenamedNodes.Num() > 0;
}

bool URigVMController::RenameParameter(const FName& InOldName, const FName& InNewName, bool bSetupUndoRedo)
{
	ReportWarning(TEXT("RenameParameter has been deprecated. Please use RenameVariable instead."));
	return false;
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

	if (!bIsTransacting && !IsGraphEditable())
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
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
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

	if (!bIsTransacting && !IsGraphEditable())
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
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
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

	if (!bIsTransacting && !IsGraphEditable())
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

bool URigVMController::SetPinDefaultValue(URigVMPin* InPin, const FString& InDefaultValue, bool bResizeArrays, bool bSetupUndoRedo, bool bMergeUndoAction)
{
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
	check(InPin);

	if(!InPin->IsUObject()
		&& InPin->GetCPPType() != RigVMTypeUtils::FStringType
		&& InPin->GetCPPType() != RigVMTypeUtils::FNameType
		&& bValidatePinDefaults)
	{
		ensure(!InDefaultValue.IsEmpty());
	}

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
		// no need to send notifications if not changing the value
		if (InPin->GetSubPins().IsEmpty() && (InPin->DefaultValue != ClampedDefaultValue))
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

	if (!bIsTransacting && !IsGraphEditable())
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
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
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
		{ RigVMTypeUtils::BoolType,	TEXT("False") },
		{ RigVMTypeUtils::Int32Type,	TEXT("0") },
		{ RigVMTypeUtils::FloatType,	TEXT("0.000000") },
		{ RigVMTypeUtils::DoubleType,	TEXT("0.000000") },
		{ RigVMTypeUtils::FNameType,	FName(NAME_None).ToString() },
		{ RigVMTypeUtils::FStringType,	TEXT("") }
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
			Property->ExportTextItem_Direct(DefaultValue, Memory, nullptr, nullptr, PPF_None);
		}
	}

	return DefaultValue;
}

FString URigVMController::AddAggregatePin(const FString& InNodeName, const FString& InPinName, const FString& InDefaultValue, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
#if UE_RIGVM_AGGREGATE_NODES_ENABLED
	 
	if (!IsValidGraph())
	{
		return FString();
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return FString();
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMNode* Node = Graph->FindNodeByName(*InNodeName);
	if (!Node)
	{
		return FString();
	}

	return AddAggregatePin(Node, InPinName, InDefaultValue, bSetupUndoRedo, bPrintPythonCommand);
}

FString URigVMController::AddAggregatePin(URigVMNode* InNode, const FString& InPinName, const FString& InDefaultValue, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!bIsTransacting && !IsGraphEditable())
	{
		return FString();
	}
	
	if (!InNode)
	{
		return FString();
	}

	if (!IsValidNodeForGraph(InNode))
	{
		return FString();
	}

	URigVMAggregateNode* AggregateNode = Cast<URigVMAggregateNode>(InNode);
	if (AggregateNode == nullptr)
	{
		if(!InNode->IsAggregate())
		{
			return FString();
		}
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMBaseAction Action;
	if (bSetupUndoRedo)
	{
		Action.Title = FString::Printf(TEXT("Add Aggregate Pin"));
		ActionStack->BeginAction(Action);
	}

	if (AggregateNode == nullptr)
	{
		bool bAggregateInputs = false;
		URigVMPin* Arg1 = nullptr;
		URigVMPin* Arg2 = nullptr;
		URigVMPin* ArgOpposite = nullptr;

		const TArray<URigVMPin*> AggregateInputs = InNode->GetAggregateInputs();
		const TArray<URigVMPin*> AggregateOutputs = InNode->GetAggregateOutputs();

		if (AggregateInputs.Num() == 2 && AggregateOutputs.Num() == 1)
		{
			bAggregateInputs = true;
			Arg1 = AggregateInputs[0];
			Arg2 = AggregateInputs[1];
			ArgOpposite = AggregateOutputs[0];
		}
		else if (AggregateInputs.Num() == 1 && AggregateOutputs.Num() == 2)
		{
			bAggregateInputs = false;
			Arg1 = AggregateOutputs[0];
			Arg2 = AggregateOutputs[1];
			ArgOpposite = AggregateInputs[0];
		}
		else
		{
			if (bSetupUndoRedo)
			{
				ActionStack->CancelAction(Action, this);
			}
			return FString();
		}

		if (!Arg1 || !Arg2 || !ArgOpposite)
		{
			if (bSetupUndoRedo)
			{
				ActionStack->CancelAction(Action, this);
			}
			return FString();
		}

		if (Arg1->GetCPPType() != Arg2->GetCPPType() || Arg1->GetCPPTypeObject() != Arg2->GetCPPTypeObject() ||
			Arg1->GetCPPType() != ArgOpposite->GetCPPType() || Arg1->GetCPPTypeObject() != ArgOpposite->GetCPPTypeObject())
		{
			if (bSetupUndoRedo)
			{
				ActionStack->CancelAction(Action, this);
			}
			return FString();
		}

		const FString AggregateArg1 = Arg1->GetName();
		const FString AggregateArg2 = Arg2->GetName();
		const FString AggregateOppositeArg = ArgOpposite->GetName();

		TArray<TPair<FString, FString>> LinkedPaths = GetLinkedPinPaths(InNode);
		if(!BreakLinkedPaths(LinkedPaths, bSetupUndoRedo))
		{
			if(bSetupUndoRedo)
			{
				ActionStack->CancelAction(Action, this);
			}
			return FString();
		}

		// We must resolve the type before we continue
		if (Arg1->IsWildCard())
		{
			TRigVMTypeIndex AnswerType = INDEX_NONE;
			if (RequestPinTypeSelectionDelegate.IsBound())
			{
				if (URigVMTemplateNode* TemplateNode = Cast<URigVMTemplateNode>(InNode))
				{
					if (const FRigVMTemplate* Template = TemplateNode->GetTemplate())
					{
						if (const FRigVMTemplateArgument* Argument = Template->FindArgument(Arg1->GetFName()))
						{
							const TArray<TRigVMTypeIndex>& Types = Argument->GetTypeIndices();
							AnswerType = RequestPinTypeSelectionDelegate.Execute(Types);
						}
					}
				}
			}

			if (AnswerType != INDEX_NONE)
			{
				ResolveWildCardPin(Arg1, AnswerType, bSetupUndoRedo);
			}

		}

		if (Arg1->IsWildCard() || Arg2->IsWildCard() || ArgOpposite->IsWildCard())
		{
			if(bSetupUndoRedo)
			{
				ActionStack->CancelAction(Action, this);
			}
			return FString();
		}

		const FName PreviousNodeName = InNode->GetFName();
		URigVMCollapseNode* CollapseNode = CollapseNodes({InNode}, InNode->GetName(), bSetupUndoRedo, true);
		if (!CollapseNode)
		{
			if (bSetupUndoRedo)
			{
				ActionStack->CancelAction(Action, this);
			}
			return FString();
		}

		InNode = CollapseNode->GetContainedGraph()->FindNodeByName(PreviousNodeName);

		AggregateNode = Cast<URigVMAggregateNode>(CollapseNode);
		if (AggregateNode)
		{
			FRigVMControllerGraphGuard GraphGuard(this, AggregateNode->GetContainedGraph(), bSetupUndoRedo);
			TGuardValue<bool> GuardEditGraph(GetGraph()->bEditable, true);

			for(int32 Index = 0; Index < InNode->GetPins().Num(); Index++)
			{
				URigVMPin* Pin = InNode->GetPins()[Index];
				const FName PinName = Pin->GetFName();
				
				if (URigVMPin* AggregatePin = AggregateNode->FindPin(PinName.ToString()))
				{
					SetExposedPinIndex(PinName, Index, bSetupUndoRedo);
					continue;
				}

				const FName ExposedPinName = AddExposedPin(PinName, Pin->GetDirection(), Pin->GetCPPType(), *Pin->GetCPPTypeObject()->GetPathName(), Pin->GetDefaultValue());

				const FString PinNameStr = PinName.ToString();
				const FString ExposedPinNameStr = ExposedPinName.ToString();

				if(URigVMPin* ExposedPin = AggregateNode->FindPin(ExposedPinNameStr))
				{
					ExposedPin->SetDisplayName(Pin->GetDisplayName());
				}

				if(URigVMPin* ExposedPin = AggregateNode->GetEntryNode()->FindPin(ExposedPinNameStr))
				{
					ExposedPin->SetDisplayName(Pin->GetDisplayName());
				}

				if(URigVMPin* ExposedPin = AggregateNode->GetReturnNode()->FindPin(ExposedPinNameStr))
				{
					ExposedPin->SetDisplayName(Pin->GetDisplayName());
				}

				if (Pin->GetDirection() == ERigVMPinDirection::Input)
				{
					AddLink(FString::Printf(TEXT("Entry.%s"), *ExposedPinNameStr), FString::Printf(TEXT("%s.%s"), *InNode->GetName(), *PinNameStr), bSetupUndoRedo);
				}
				else
				{
					AddLink(FString::Printf(TEXT("%s.%s"), *InNode->GetName(), *PinNameStr), FString::Printf(TEXT("Return.%s"), *ExposedPinNameStr), bSetupUndoRedo);
				}
			}
		}
		else
		{
			if (bSetupUndoRedo)
			{
				ActionStack->CancelAction(Action, this);
			}
			return FString();
		}

		RestoreLinkedPaths(LinkedPaths, {{PreviousNodeName.ToString(), AggregateNode->GetName()}}, {}, bSetupUndoRedo);
	}
	
	if (!AggregateNode)
	{
		if (bSetupUndoRedo)
		{
			ActionStack->CancelAction(Action, this);
		}
		return FString();
	}

	URigVMPin* NewPin = nullptr;	
	{
		FRigVMControllerGraphGuard GraphGuard(this, AggregateNode->GetContainedGraph(), bSetupUndoRedo);
		TGuardValue<bool> GuardEditGraph(GetGraph()->bEditable, true);

		URigVMNode* InnerNode = (AggregateNode == nullptr) ? InNode : AggregateNode->GetFirstInnerNode();

		const FString InnerNodeContent = ExportNodesToText({InnerNode->GetFName()});
		const TArray<FName> NewNodeNames = ImportNodesFromText(InnerNodeContent);
		
		if(NewNodeNames.IsEmpty())
		{
			if (bSetupUndoRedo)
			{
				ActionStack->CancelAction(Action, this);
			}
			return FString();
		}

		URigVMNode* NewNode = AggregateNode->GetContainedGraph()->FindNodeByName(NewNodeNames[0]);

		FName NewPinName = *InPinName;
		if (NewPinName.IsNone())
		{
			URigVMNode* LastInnerNode = AggregateNode->GetLastInnerNode();
			URigVMPin* SecondAggregateInnerPin = LastInnerNode->GetSecondAggregatePin();
			FString LastAggregateName;
			if (AggregateNode->IsInputAggregate())
			{
				TArray<URigVMPin*> SourcePins = SecondAggregateInnerPin->GetLinkedSourcePins();
				if (SourcePins.Num() > 0)
				{
					LastAggregateName = SourcePins[0]->GetName();
				}
			}
			else
			{
				TArray<URigVMPin*> TargetPins = SecondAggregateInnerPin->GetLinkedTargetPins();
				if (TargetPins.Num() > 0)
				{
					LastAggregateName = TargetPins[0]->GetName();
				}
			}

			NewPinName = InnerNode->GetNextAggregateName(*LastAggregateName);
		}
		
		if (NewPinName.IsNone())
		{
			NewPinName = InnerNode->GetSecondAggregatePin()->GetFName();
		}
		
		const URigVMPin* Arg1 = AggregateNode->GetFirstAggregatePin();
		FName NewExposedPinName = AddExposedPin(NewPinName, Arg1->GetDirection(), Arg1->GetCPPType(), *Arg1->GetCPPTypeObject()->GetPathName(), InDefaultValue, bSetupUndoRedo);
		NewPin = AggregateNode->FindPin(NewExposedPinName.ToString());
		URigVMPin* NewUnitPinArg1 = NewNode->GetFirstAggregatePin();
		URigVMPin* NewUnitPinArg2 = NewNode->GetSecondAggregatePin();
		URigVMPin* NewUnitPinOppositeArg = NewNode->GetOppositeAggregatePin();
		URigVMNode* PreviousNode = nullptr;
		if(AggregateNode->IsInputAggregate())
		{
			URigVMFunctionEntryNode* EntryNode = AggregateNode->GetEntryNode();
			URigVMPin* EntryPin = EntryNode->FindPin(NewExposedPinName.ToString());
			URigVMPin* ReturnPin = AggregateNode->GetReturnNode()->FindPin(NewUnitPinOppositeArg->GetName());
			URigVMPin* PreviousReturnPin = ReturnPin->GetLinkedSourcePins()[0];
			PreviousNode = PreviousReturnPin->GetNode();
		
			BreakAllLinks(ReturnPin, true, bSetupUndoRedo);
			AddLink(PreviousReturnPin, NewUnitPinArg1, bSetupUndoRedo);						
			AddLink(EntryPin, NewUnitPinArg2, bSetupUndoRedo);
			AddLink(NewUnitPinOppositeArg, ReturnPin, bSetupUndoRedo);
		}
		else
		{
			URigVMFunctionReturnNode* ReturnNode = AggregateNode->GetReturnNode();
			URigVMPin* NewReturnPin = ReturnNode->FindPin(NewExposedPinName.ToString());
			URigVMPin* OldReturnPin = ReturnNode->GetPins()[ReturnNode->GetPins().Num()-2];
			URigVMPin* PreviousReturnPin = OldReturnPin->GetLinkedSourcePins()[0];
			PreviousNode = PreviousReturnPin->GetNode();

			BreakAllLinks(OldReturnPin, true, bSetupUndoRedo);
			AddLink(PreviousReturnPin, NewUnitPinOppositeArg, bSetupUndoRedo);						
			AddLink(NewUnitPinArg1, OldReturnPin, bSetupUndoRedo);
			AddLink(NewUnitPinArg2, NewReturnPin, bSetupUndoRedo);
		}

		// Rearrange the graph nodes
		URigVMFunctionReturnNode* ReturnNode = AggregateNode->GetReturnNode();
		FVector2D NodeDimensions(200, 150);
		SetNodePosition(NewNode, PreviousNode->GetPosition() + NodeDimensions, bSetupUndoRedo);
		SetNodePosition(ReturnNode, NewNode->GetPosition() + NodeDimensions, bSetupUndoRedo);

		// Connect other input pins
		for (URigVMPin* OtherInputPin : AggregateNode->GetFirstInnerNode()->GetPins())
		{
			if (OtherInputPin->GetName() != NewUnitPinArg1->GetName() &&
				OtherInputPin->GetName() != NewUnitPinArg2->GetName() &&
				OtherInputPin->GetName() != NewUnitPinOppositeArg->GetName())
			{
				URigVMPin* OtherEntryPin = AggregateNode->GetEntryNode()->FindPin(OtherInputPin->GetName());
				AddLink(OtherEntryPin, NewNode->FindPin(OtherEntryPin->GetName()), bSetupUndoRedo);
			}
		}

		AggregateNode->LastInnerNodeCache = NewNode;
	}

	if (!NewPin)
	{
		if (bSetupUndoRedo)
		{
			ActionStack->CancelAction(Action, this);
		}
		return FString();
	}

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	if (bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
		const FString NodePath = GetSanitizedPinPath(InNode->GetNodePath());

		RigVMPythonUtils::Print(GetGraphOuterName(),
			FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_aggregate_pin('%s', '%s', '%s')"),
			*GraphName,
			*NodePath,
			*InPinName,
			*InDefaultValue));
	}
	
	return NewPin->GetPinPath();

#else
	return FString();
#endif
}

bool URigVMController::RemoveAggregatePin(const FString& InPinPath, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
#if UE_RIGVM_AGGREGATE_NODES_ENABLED
	
	if (!IsValidGraph())
	{
		return false;
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	URigVMPin* Pin = Graph->FindPin(*InPinPath);
	if (!Pin)
	{
		return false;
	}

	return RemoveAggregatePin(Pin, bSetupUndoRedo, bPrintPythonCommand);
}

bool URigVMController::RemoveAggregatePin(URigVMPin* InPin, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
	if (!InPin)
	{
		return false;
	}

	if (InPin->GetParentPin())
	{
		return false;
	}

	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMBaseAction Action;
	if (bSetupUndoRedo)
	{
		Action.Title = FString::Printf(TEXT("Remove Aggregate Pin"));
		ActionStack->BeginAction(Action);
	}

	bool bSuccess = false;
	if (URigVMAggregateNode* AggregateNode = Cast<URigVMAggregateNode>(InPin->GetNode()))
	{
		URigVMGraph* Graph = AggregateNode->GetContainedGraph();
		if (AggregateNode->IsInputAggregate())
		{
			if (URigVMFunctionEntryNode* EntryNode = Graph->GetEntryNode())
			{
				if (URigVMPin* EntryPin = EntryNode->FindPin(InPin->GetName()))
				{
					if (EntryPin->GetLinkedTargetPins().Num() > 0)
					{
						FRigVMControllerGraphGuard GraphGuard(this, AggregateNode->GetContainedGraph(), bSetupUndoRedo);
						TGuardValue<bool> GuardEditGraph(GetGraph()->bEditable, true);
						
						URigVMPin* TargetPin = EntryPin->GetLinkedTargetPins()[0];
					
						URigVMNode* NodeToRemove = TargetPin->GetNode();
						URigVMPin* ResultPin = NodeToRemove->GetOppositeAggregatePin();
						URigVMPin* NextNodePin = ResultPin->GetLinkedTargetPins()[0];

						if (NodeToRemove == AggregateNode->FirstInnerNodeCache || NodeToRemove == AggregateNode->LastInnerNodeCache)
						{
							AggregateNode->InvalidateCache();
						}

						const FString FirstAggregatePin = AggregateNode->GetFirstAggregatePin()->GetName();
						const FString SecondAggregatePin = AggregateNode->GetSecondAggregatePin()->GetName();
						FString OtherArg = TargetPin->GetName() == FirstAggregatePin ? SecondAggregatePin : FirstAggregatePin;
						BreakAllLinks(NextNodePin, true, bSetupUndoRedo);
						RewireLinks(NodeToRemove->FindPin(OtherArg), NextNodePin, true, bSetupUndoRedo);
						RemoveNode(NodeToRemove, bSetupUndoRedo);
						RemoveExposedPin(*InPin->GetName(), bSetupUndoRedo);
						bSuccess = true;
					}
				}
			}
		}
		else
		{
			if (URigVMFunctionReturnNode* ReturnNode = Graph->GetReturnNode())
			{
				if (URigVMPin* ReturnPin = ReturnNode->FindPin(InPin->GetName()))
				{
					if (ReturnPin->GetLinkedSourcePins().Num() > 0)
					{
						FRigVMControllerGraphGuard GraphGuard(this, AggregateNode->GetContainedGraph(), bSetupUndoRedo);
						TGuardValue<bool> GuardEditGraph(GetGraph()->bEditable, true);
						
						URigVMPin* SourcePin = ReturnPin->GetLinkedSourcePins()[0];
					
						URigVMNode* NodeToRemove = SourcePin->GetNode();
						URigVMPin* OppositePin = NodeToRemove->GetOppositeAggregatePin();
						URigVMPin* NextNodePin = OppositePin->GetLinkedSourcePins()[0];
						URigVMNode* NextNode = NextNodePin->GetNode();

						if (NodeToRemove == AggregateNode->FirstInnerNodeCache || NodeToRemove == AggregateNode->LastInnerNodeCache)
						{
							AggregateNode->InvalidateCache();
						}

						const FString FirstAggregatePin = AggregateNode->GetFirstAggregatePin()->GetName();
						const FString SecondAggregatePin = AggregateNode->GetSecondAggregatePin()->GetName();
						FString OtherArg = SourcePin->GetName() == FirstAggregatePin ? SecondAggregatePin : FirstAggregatePin;
						BreakAllLinks(NextNodePin, false, bSetupUndoRedo);
						RewireLinks(NodeToRemove->FindPin(OtherArg), NextNodePin, false, bSetupUndoRedo);
						RemoveNode(NodeToRemove, bSetupUndoRedo);
						RemoveExposedPin(*InPin->GetName(), bSetupUndoRedo);
						bSuccess = true;
					}
				}
			}			
		}

		if (bSuccess && AggregateNode->GetContainedNodes().Num() == 3)
		{
			TArray<TPair<FString, FString>> LinkedPaths = GetLinkedPinPaths(AggregateNode);
			if(!BreakLinkedPaths(LinkedPaths, bSetupUndoRedo))
			{
				if(bSetupUndoRedo)
				{
					ActionStack->CancelAction(Action, this);
				}
				return false;
			}

			TMap<FString, FString> PinNameMap;
			for(URigVMPin* Pin : AggregateNode->GetPins())
			{
				if(URigVMPin* EntryPin = AggregateNode->GetEntryNode()->FindPin(Pin->GetName()))
				{
					TArray<URigVMPin*> TargetPins = EntryPin->GetLinkedTargetPins();
					if(TargetPins.Num() > 0)
					{
						PinNameMap.Add(EntryPin->GetName(), TargetPins[0]->GetName());
					}
				}
				else if(URigVMPin* ReturnPin = AggregateNode->GetReturnNode()->FindPin(Pin->GetName()))
				{
					TArray<URigVMPin*> SourcePins = ReturnPin->GetLinkedSourcePins();
					if(SourcePins.Num() > 0)
					{
						PinNameMap.Add(ReturnPin->GetName(), SourcePins[0]->GetName());
					}
				}
			}

			const FString PreviousNodeName = AggregateNode->GetName();
			TArray<URigVMNode*> NodesEjected = ExpandLibraryNode(AggregateNode, bSetupUndoRedo);
			bSuccess = NodesEjected.Num() == 1;

			if(bSuccess)
			{
				URigVMNode* EjectedNode = NodesEjected[0];
				RestoreLinkedPaths(LinkedPaths, {}, {{
					PreviousNodeName, FRigVMController_PinPathRemapDelegate::CreateLambda([
						PreviousNodeName,
						EjectedNode,
						PinNameMap
					](const FString& InPinPath, bool bIsInput) -> FString
					{
						static constexpr TCHAR PinPrefixFormat[] = TEXT("%s.");

						TArray<FString> Segments;
						URigVMPin::SplitPinPath(InPinPath, Segments);
						Segments[0] = EjectedNode->GetName();

						if(const FString* RemappedPin = PinNameMap.Find(Segments[1]))
						{
							Segments[1] = *RemappedPin;
						}
						
						return URigVMPin::JoinPinPath(Segments);
					})
				}}, bSetupUndoRedo);
			}
		}		
	}

	if (bSetupUndoRedo)
	{
		if (bSuccess)
		{
			ActionStack->EndAction(Action);
		}
		else
		{
			ActionStack->CancelAction(Action, this);			
		}
	}

	if (bSuccess && bPrintPythonCommand)
	{
		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
		const FString PinPath = GetSanitizedPinPath(InPin->GetPinPath());

		RigVMPythonUtils::Print(GetGraphOuterName(),
			FString::Printf(TEXT("blueprint.get_controller_by_name('%s').remove_aggregate_pin('%s')"),
			*GraphName,
			*PinPath));
	}

	return bSuccess;

#else
	return false;
#endif
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

	if (!bIsTransacting && !IsGraphEditable())
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

	if (!bIsTransacting && !IsGraphEditable())
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
	if (!bIsTransacting && !IsGraphEditable())
	{
		return nullptr;
	}
	
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
			{
				TGuardValue<bool> SuspendNotifications(bSuspendNotifications, true);
				AddPinsForStruct(ScriptStruct, Pin->GetNode(), Pin, Pin->Direction, DefaultValue, false);
			}
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

	Notify(ERigVMGraphNotifType::PinAdded, Pin);
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

	if (!bIsTransacting && !IsGraphEditable())
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

	// we need to keep at least one element for fixed size arrays
	if(ArrayPin->IsExecuteContext() || ArrayPin->IsFixedSizeArray())
	{
		if(ArrayPin->GetArraySize() == 1)
		{
			return false;
		}
	}

	int32 IndexToRemove = ArrayElementPin->GetPinIndex();
	if (!RemovePin(ArrayElementPin, bSetupUndoRedo))
	{
		return false;
	}

	for (int32 ExistingIndex = IndexToRemove; ExistingIndex < ArrayPin->GetArraySize(); ExistingIndex++)
	{
		URigVMPin* ExistingPin = ArrayPin->GetSubPins()[ExistingIndex];
		ExistingPin->SetNameFromIndex();
		Notify(ERigVMGraphNotifType::PinRenamed, ExistingPin);
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

bool URigVMController::RemovePin(URigVMPin* InPinToRemove, bool bSetupUndoRedo)
{
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
	FRigVMControllerCompileBracketScope CompileScope(this);
	if (bSetupUndoRedo)
	{
		BreakAllLinks(InPinToRemove, true, bSetupUndoRedo);
		BreakAllLinks(InPinToRemove, false, bSetupUndoRedo);
		BreakAllLinksRecursive(InPinToRemove, true, false, bSetupUndoRedo);
		BreakAllLinksRecursive(InPinToRemove, false, false, bSetupUndoRedo);
	}

	TArray<URigVMPin*> SubPins = InPinToRemove->GetSubPins();
	for (URigVMPin* SubPin : SubPins)
	{
		if (!RemovePin(SubPin, bSetupUndoRedo))
		{
			return false;
		}
	}

	if (URigVMPin* ParentPin = InPinToRemove->GetParentPin())
	{
		ParentPin->SubPins.Remove(InPinToRemove);
	}
	else if(URigVMNode* Node = InPinToRemove->GetNode())
	{
		Node->Pins.Remove(InPinToRemove);
		Node->OrphanedPins.Remove(InPinToRemove);
	}

	if (!bSuspendNotifications)
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

	if (!bIsTransacting && !IsGraphEditable())
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
				ActionStack->CancelAction(Action, this);
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
				ActionStack->CancelAction(Action, this);
			}
			return false;
		}
		AddedPins++;
	}

	if (!InDefaultValue.IsEmpty() && InDefaultValue != TEXT("()"))
	{
		SetPinDefaultValue(Pin, InDefaultValue, false, bSetupUndoRedo, true);
	}

	if (bSetupUndoRedo)
	{
		if (RemovedPins > 0 || AddedPins > 0)
		{
			ActionStack->EndAction(Action);
		}
		else
		{
			ActionStack->CancelAction(Action, this);
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

	if (!bIsTransacting && !IsGraphEditable())
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
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
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

	// Unbind any other variables, remove any other injections, and break all links to the input pin
	{
		if (InPin->IsBoundToVariable())
		{
			UnbindPinFromVariable(InPin, bSetupUndoRedo);
		}
		TArray<URigVMInjectionInfo*> Infos = InPin->GetInjectedNodes();
		for (URigVMInjectionInfo* Info : Infos)
		{
			RemoveInjectedNode(Info->GetPin()->GetPinPath(), Info->bInjectedAsInput, bSetupUndoRedo);
		}
		BreakAllLinks(InPin, true, bSetupUndoRedo);
	}

	// Create variable node
	URigVMVariableNode* VariableNode = nullptr;
	{
		{
			TGuardValue<bool> GuardNotifications(bSuspendNotifications, true);
			FString CPPType;
			UObject* CPPTypeObject;
			RigVMTypeUtils::CPPTypeFromExternalVariable(Variable, CPPType, &CPPTypeObject);
			VariableNode = AddVariableNode(*VariableName, CPPType, CPPTypeObject, true, FString(), FVector2D::ZeroVector, InVariableNodeName, bSetupUndoRedo);
		}
		if (VariableNode == nullptr)
		{
			if (bSetupUndoRedo)
			{
				ActionStack->CancelAction(Action, this);
			}
			return false;
		}
	}
	
	URigVMPin* ValuePin = VariableNode->FindPin(URigVMVariableNode::ValueName);
	// Connect value pin to input pin
	{
		if (!SegmentPath.IsEmpty())
		{
			ValuePin = ValuePin->FindSubPin(SegmentPath);
		}

		{
			GetGraph()->ClearAST(true, false);
			if (!AddLink(ValuePin, InPin, bSetupUndoRedo))
			{
				if (bSetupUndoRedo)
				{
					ActionStack->CancelAction(Action, this);
				}
				return false;
			}
		}
	}

	// Inject into pin
	if (!InjectNodeIntoPin(InPin->GetPinPath(), true, FName(), ValuePin->GetFName(), bSetupUndoRedo))
	{
		if (bSetupUndoRedo)
		{
			ActionStack->CancelAction(Action, this);
		}
		return false;
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

	if (!bIsTransacting && !IsGraphEditable())
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
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
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

	RemoveInjectedNode(InPin->GetPinPath(), true, bSetupUndoRedo);

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

	if (!bIsTransacting && !IsGraphEditable())
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
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
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

	if (!bIsTransacting && !IsGraphEditable())
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
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}

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

bool URigVMController::AddLink(const FString& InOutputPinPath, const FString& InInputPinPath, bool bSetupUndoRedo,
	bool bPrintPythonCommand, ERigVMPinDirection InUserDirection, bool bCreateCastNode)
{
	if(!IsValidGraph())
	{
		return false;
	}

	if (!bIsTransacting && !IsGraphEditable())
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

	const bool bSuccess = AddLink(OutputPin, InputPin, bSetupUndoRedo, InUserDirection, bCreateCastNode);
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

bool URigVMController::AddLink(URigVMPin* OutputPin, URigVMPin* InputPin, bool bSetupUndoRedo, ERigVMPinDirection InUserDirection, bool bCreateCastNode)
{
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
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

	TGuardValue<ERigVMPinDirection> UserLinkDirectionGuard(UserLinkDirection,
	InUserDirection == ERigVMPinDirection::Invalid ? UserLinkDirection : InUserDirection);

	if (!bIsTransacting)
	{
		FString FailureReason;
		if (!Graph->CanLink(OutputPin, InputPin, &FailureReason, GetCurrentByteCode(), UserLinkDirection, bCreateCastNode))
		{
			if(OutputPin->IsExecuteContext() && InputPin->IsExecuteContext())
			{
				if(OutputPin->GetNode()->IsA<URigVMFunctionEntryNode>() &&
					InputPin->GetNode()->IsA<URigVMFunctionReturnNode>())
				{
					return false;
				}
			}
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
		Action.Title = TEXT("Add Link");
		ActionStack->BeginAction(Action);
	}

	// check if we need to inject cast node
	if(bCreateCastNode &&
		bEnableTypeCasting &&
		OutputPin->GetTypeIndex() != InputPin->GetTypeIndex() &&
		!OutputPin->IsWildCard() &&
		!InputPin->IsWildCard())
	{
		if(!FRigVMRegistry::Get().CanMatchTypes(OutputPin->GetTypeIndex(), InputPin->GetTypeIndex(), true))
		{
			if(bCreateCastNode)
			{
				const FRigVMFunction* CastFunction = RigVMTypeUtils::GetCastForTypeIndices(OutputPin->GetTypeIndex(), InputPin->GetTypeIndex());

				if(CastFunction == nullptr)
				{
					// this is potentially a template node which has more types available.
					// look through the filtered types and find a matching cast.
				}

				if(CastFunction == nullptr)
				{
					if (bSetupUndoRedo)
					{
						ActionStack->CancelAction(Action, this);
					}
					return false;
				}

				const FVector2D OutputPinPosition = OutputPin->GetNode()->GetPosition() + FVector2D(150, 40.f) + FVector2D(0, 16) * OutputPin->GetRootPin()->GetPinIndex();
				const FVector2D InputPinPosition = InputPin->GetNode()->GetPosition() + FVector2D(-75, 40.f) + FVector2D(0, 16) * InputPin->GetRootPin()->GetPinIndex();
				const FVector2D CastPosition = (OutputPinPosition + InputPinPosition) * 0.5; 

				const URigVMNode* CastNode = nullptr;

				// try to find an existing cast node
				if(CastNode == nullptr)
				{
					for(URigVMLink* ExistingLink : OutputPin->GetLinks())
					{
						if(ExistingLink->GetSourcePin() == OutputPin)
						{
							if(URigVMUnitNode* ExistingCastNode = Cast<URigVMUnitNode>(ExistingLink->GetTargetPin()->GetNode()))
							{
								if(ExistingCastNode->GetScriptStruct() == CastFunction->Struct)
								{
									CastNode = ExistingCastNode;
									break;
								}
							}
						}
					}
				}

				if(CastNode == nullptr)
				{
					CastNode = AddUnitNode(CastFunction->Struct, CastFunction->GetMethodName(), CastPosition, FString(), bSetupUndoRedo, false);
				}
				
				if(CastNode == nullptr)
				{
					if (bSetupUndoRedo)
					{
						ActionStack->CancelAction(Action, this);
					}
					return false;
				}

				static const FString CastTemplateValueName = RigVMTypeUtils::GetCastTemplateValueName().ToString();
				static const FString CastTemplateResultName = RigVMTypeUtils::GetCastTemplateResultName().ToString();
				URigVMPin* ValuePin = CastNode->FindPin(CastTemplateValueName);
				URigVMPin* ResultPin = CastNode->FindPin(CastTemplateResultName);

				if(!OutputPin->IsLinkedTo(ValuePin))
				{
					if(!AddLink(OutputPin, ValuePin, bSetupUndoRedo))
					{
						if (bSetupUndoRedo)
						{
							ActionStack->CancelAction(Action, this);
						}
						return false;
					}
				}

				if(!ResultPin->IsLinkedTo(InputPin))
				{
					if(!AddLink(ResultPin, InputPin, bSetupUndoRedo))
					{
						if (bSetupUndoRedo)
						{
							ActionStack->CancelAction(Action, this);
						}
						return false;
					}
				}

				if(bSetupUndoRedo)
				{
					Action.Title = TEXT("Add Link with Cast");
					ActionStack->EndAction(Action);
				}
				return true;
			}
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

	// resolve types on the pins if needed
	if((InputPin->GetCPPTypeObject() != OutputPin->GetCPPTypeObject() ||
		OutputPin->GetCPPType() != InputPin->GetCPPType()) &&
		!InputPin->IsExecuteContext() &&
		!OutputPin->IsExecuteContext())
	{
		bool bOutputPinCanChangeType = OutputPin->IsWildCard();
		bool bInputPinCanChangeType = InputPin->IsWildCard();

		if(!bOutputPinCanChangeType && !bInputPinCanChangeType)
		{
			bInputPinCanChangeType = UserLinkDirection == ERigVMPinDirection::Output && InputPin->GetNode()->IsA<URigVMTemplateNode>(); 
			bOutputPinCanChangeType = UserLinkDirection == ERigVMPinDirection::Input && OutputPin->GetNode()->IsA<URigVMTemplateNode>(); 
		}
		
		if(bOutputPinCanChangeType)
		{
			Notify(ERigVMGraphNotifType::InteractionBracketOpened, nullptr);
			if(OutputPin->GetNode()->IsA<URigVMRerouteNode>())
			{
				SetPinDefaultValue(OutputPin, InputPin->GetDefaultValue(), true, bSetupUndoRedo, false);
			}
			if(InputPin->GetNode()->IsA<URigVMRerouteNode>())
			{
				SetPinDefaultValue(OutputPin, OutputPin->GetDefaultValue(), true, bSetupUndoRedo, false);
			}
			Notify(ERigVMGraphNotifType::InteractionBracketClosed, nullptr);
		}
	}

	if (bSetupUndoRedo)
	{
		ExpandPinRecursively(OutputPin->GetParentPin(), bSetupUndoRedo);
		ExpandPinRecursively(InputPin->GetParentPin(), bSetupUndoRedo);
	}

	// Before adding the link, let's resolve input and ouput pin types
	// If templates, we will filter the permutations that support this link
	// If any links need to be broken before perfoming this connection, try to find them and break them
	if (!bIsTransacting && !InputPin->IsExecuteContext() && !OutputPin->IsExecuteContext())
	{
		URigVMPin* FirstToResolve = (InUserDirection == ERigVMPinDirection::Input) ? OutputPin : InputPin;
		URigVMPin* SecondToResolve = (FirstToResolve == OutputPin) ? InputPin : OutputPin;
		if (!PrepareToLink(FirstToResolve, SecondToResolve, bSetupUndoRedo))
		{
			if (bSetupUndoRedo)
			{
				ActionStack->CancelAction(Action, this);
			}
			return false;
		}
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

	if (bSetupUndoRedo)
	{
		UpdateRerouteNodeAfterChangingLinks(OutputPin, bSetupUndoRedo);
		UpdateRerouteNodeAfterChangingLinks(InputPin, bSetupUndoRedo);
	}

	if (bSetupUndoRedo)
	{
#if WITH_EDITOR
		if (!bSuspendTemplateComputation)
		{
			auto ResolveTemplateNodeToCommonTypes = [this](URigVMPin* Pin)
			{
				if(!Pin->IsExecuteContext())
				{
					return;
				}
			
				URigVMTemplateNode* TemplateNode = Cast<URigVMTemplateNode>(Pin->GetNode());
				if(TemplateNode == nullptr)
				{
					return;
				}

				const FRigVMTemplate* Template = TemplateNode->GetTemplate();
				if(Template == nullptr)
				{
					return;
				}

				if(!TemplateNode->HasWildCardPin())
				{
					return;
				}

				const FRigVMTemplate::FTypeMap PreferredTypes = GetCommonlyUsedTypesForTemplate(TemplateNode);
				if(PreferredTypes.IsEmpty())
				{
					return;
				}

				const int32 PreferredPermutation = Template->FindPermutation(PreferredTypes);
				if(PreferredPermutation != INDEX_NONE)
				{
					const TGuardValue<bool> DisableRegisterUseOfTemplate(bRegisterTemplateNodeUsage, false);
					if(FullyResolveTemplateNode(TemplateNode, PreferredPermutation, true))
					{
						static const FString Message = TEXT("Template node was automatically resolved to commonly used types.");
						SendUserFacingNotification(Message, 0.f, TemplateNode, TEXT("MessageLog.Note"));
					}
				}
			};

			ResolveTemplateNodeToCommonTypes(OutputPin);
			ResolveTemplateNodeToCommonTypes(InputPin);
		}
#endif
		
		ActionStack->EndAction(Action);
	}

	if (!bIsTransacting)
	{
		ensureMsgf(RigVMTypeUtils::AreCompatible(*OutputPin->GetCPPType(), OutputPin->GetCPPTypeObject(), *InputPin->GetCPPType(), InputPin->GetCPPTypeObject()),
		   TEXT("Incompatible types after successful link %s (%s) -> %s (%s) created in %s")
		   , *OutputPin->GetPinPath(true)
		   , *OutputPin->GetCPPType()
		   , *InputPin->GetPinPath(true)
		   , *InputPin->GetCPPType()
		   , *GetPackage()->GetPathName());
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

	if (!bIsTransacting && !IsGraphEditable())
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
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
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

			// each time a link is removed, existing orphaned pins
			// may become unused and thus can be removed
			RemoveUnusedOrphanedPins(OutputPin->GetNode());
			RemoveUnusedOrphanedPins(InputPin->GetNode());
			
			if (!bSuspendNotifications)
			{
				Graph->MarkPackageDirty();
			}
			Notify(ERigVMGraphNotifType::LinkRemoved, Link);

			DestroyObject(Link);

			if (bSetupUndoRedo)
			{
				UpdateRerouteNodeAfterChangingLinks(OutputPin, bSetupUndoRedo);
				UpdateRerouteNodeAfterChangingLinks(InputPin, bSetupUndoRedo);
			}

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

	if (!bIsTransacting && !IsGraphEditable())
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
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
	if(!Pin->IsLinked(false))
	{
		return false;
	}
	
	FRigVMControllerCompileBracketScope CompileScope(this);
	FRigVMBaseAction Action;
	if (bSetupUndoRedo)
	{
		Action.Title = FString::Printf(TEXT("Break All Links"));
		ActionStack->BeginAction(Action);
	}

	int32 LinksBroken = 0;
	{
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
	}

	if (bSetupUndoRedo)
	{
		if (LinksBroken > 0)
		{
			ActionStack->EndAction(Action);
		}
		else
		{
			ActionStack->CancelAction(Action, this);
		}
	}

	return LinksBroken > 0;
}

bool URigVMController::BreakAllLinksRecursive(URigVMPin* Pin, bool bAsInput, bool bTowardsParent, bool bSetupUndoRedo)
{
	bool bBrokenLinks = false;
	{
		if (bTowardsParent)
		{
			URigVMPin* ParentPin = Pin->GetParentPin();
			if (ParentPin)
			{
				bBrokenLinks |= BreakAllLinks(ParentPin, bAsInput, bSetupUndoRedo);
				bBrokenLinks |= BreakAllLinksRecursive(ParentPin, bAsInput, bTowardsParent, bSetupUndoRedo);
			}
		}
		else
		{
			for (URigVMPin* SubPin : Pin->SubPins)
			{
				bBrokenLinks |= BreakAllLinks(SubPin, bAsInput, bSetupUndoRedo);
				bBrokenLinks |= BreakAllLinksRecursive(SubPin, bAsInput, bTowardsParent, bSetupUndoRedo);
			}
		}
	}

	return bBrokenLinks;
}

FName URigVMController::AddExposedPin(const FName& InPinName, ERigVMPinDirection InDirection, const FString& InCPPType, const FName& InCPPTypeObjectPath, const FString& InDefaultValue, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return NAME_None;
	}

	if (!bIsTransacting && !IsGraphEditable())
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
			CPPTypeObject = RigVMTypeUtils::FindObjectFromCPPTypeObjectPath<UObject>(InCPPTypeObjectPath.ToString());
		}
	}

	// For now we do not allow wildcards for library nodes
	if (CPPTypeObject)
	{
		if(CPPTypeObject == RigVMTypeUtils::GetWildCardCPPTypeObject())
		{
			ReportError(TEXT("Cannot expose pins of wildcard type in functions."));
			return NAME_None;
		}
	}
	
	// only allow one IO / input exposed pin of type execute context per direction
	// except for aggregate nodes that can have multiple exec outputs
	bool bCheckForExecUniqueness = true;
	if (LibraryNode->IsA<URigVMAggregateNode>())
	{
		bCheckForExecUniqueness = InDirection != ERigVMPinDirection::Output;
	}
	
	if (bCheckForExecUniqueness)
	{
		if(const UScriptStruct* CPPTypeStruct = Cast<UScriptStruct>(CPPTypeObject))
		{
			if(CPPTypeStruct->IsChildOf(FRigVMExecuteContext::StaticStruct()))
			{
				for(const URigVMPin* ExistingPin : LibraryNode->Pins)
				{
					if(ExistingPin->IsExecuteContext())
					{
						return NAME_None;
					}
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
	Pin->CPPType = RigVMTypeUtils::PostProcessCPPType(InCPPType, CPPTypeObject);
	Pin->CPPTypeObjectPath = InCPPTypeObjectPath;
	Pin->bIsConstant = false;
	Pin->Direction = InDirection;
	AddNodePin(LibraryNode, Pin);

	if (Pin->IsStruct())
	{
		FRigVMControllerGraphGuard GraphGuard(this, LibraryNode->GetGraph(), bSetupUndoRedo);

		FString DefaultValue = InDefaultValue;
		CreateDefaultValueForStructIfRequired(Pin->GetScriptStruct(), DefaultValue);
		{
			TGuardValue<bool> SuspendNotifications(bSuspendNotifications, true);
			AddPinsForStruct(Pin->GetScriptStruct(), LibraryNode, Pin, Pin->Direction, DefaultValue, false);
		}
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

	URigVMFunctionEntryNode* EntryNode = Graph->GetEntryNode();
	if (!EntryNode)
	{
		EntryNode = NewObject<URigVMFunctionEntryNode>(Graph, TEXT("Entry"));
		Graph->Nodes.Add(EntryNode);
		{
			TGuardValue<bool> SuspendNotifications(bSuspendNotifications, true);
			RefreshFunctionPins(EntryNode);
		}

		Notify(ERigVMGraphNotifType::NodeAdded, EntryNode);
	}

	URigVMFunctionReturnNode* ReturnNode = Graph->GetReturnNode();
	if (!ReturnNode)
	{
		ReturnNode = NewObject<URigVMFunctionReturnNode>(Graph, TEXT("Return"));
		Graph->Nodes.Add(ReturnNode);
		{
			TGuardValue<bool> SuspendNotifications(bSuspendNotifications, true);
			RefreshFunctionPins(ReturnNode);
		}

		Notify(ERigVMGraphNotifType::NodeAdded, ReturnNode);
	}

	RefreshFunctionPins(EntryNode);
	RefreshFunctionPins(ReturnNode);

	RefreshFunctionReferences(LibraryNode, bSetupUndoRedo);

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}

	if (bPrintPythonCommand)
	{
		//AddExposedPin(const FName& InPinName, ERigVMPinDirection InDirection, const FString& InCPPType, const FName& InCPPTypeObjectPath, const FString& InDefaultValue, bool bSetupUndoRedo, bool bPrintPythonCommand)

		const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());
		static constexpr TCHAR AddExposedPinFormat[] = TEXT("blueprint.get_controller_by_name('%s').add_exposed_pin('%s', %s, '%s', '%s', '%s')");
		
		RigVMPythonUtils::Print(GetGraphOuterName(), 
			FString::Printf(AddExposedPinFormat,
				*GraphName,
				*GetSanitizedPinName(InPinName.ToString()),
				*RigVMPythonUtils::EnumValueToPythonString<ERigVMPinDirection>((int64)InDirection),
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

	if (!bIsTransacting && !IsGraphEditable())
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
		{
			FRigVMControllerGraphGuard GraphGuard(this, LibraryNode->GetGraph(), bSetupUndoRedo);
			bSuccessfullyRemovedPin = RemovePin(Pin, bSetupUndoRedo);
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

		RefreshFunctionPins(Graph->GetEntryNode());
		RefreshFunctionPins(Graph->GetReturnNode());
		RefreshFunctionReferences(LibraryNode, false);
	}

	if (bSetupUndoRedo)
	{
		if (bSuccessfullyRemovedPin)
		{
			ActionStack->EndAction(Action);
		}
		else
		{
			ActionStack->CancelAction(Action, this);
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

	if (!bIsTransacting && !IsGraphEditable())
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
			
			InPin->SetDisplayName(InNewName);
			
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
		ActionStack->CancelAction(Action, this);
		return false;
	}

	TArray<URigVMTemplateNode*> InterfaceNodes = {Graph->GetEntryNode(), Graph->GetReturnNode()};
	for (URigVMTemplateNode* InterfaceNode : InterfaceNodes)
	{
		if (InterfaceNode)
		{
			if (URigVMPin* InterfacePin = InterfaceNode->FindPin(InOldPinName.ToString()))
			{
				Local::RenamePin(this, InterfacePin, PinName);
			}
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

	if (!bIsTransacting && !IsGraphEditable())
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

	// We do not allow unresolving exposed pins
	if (InCPPType == RigVMTypeUtils::GetWildCardCPPType())
	{
		ReportError(TEXT("Cannot change exposed pin type to wildcard."));
		return false;
	}
	
	// only allow one exposed pin of type execute context per direction
	UObject* CPPTypeObject = nullptr;
	if (!InCPPTypeObjectPath.IsNone())
	{
		CPPTypeObject = RigVMTypeUtils::FindObjectFromCPPTypeObjectPath<UObject>(InCPPTypeObjectPath.ToString());
		if(CPPTypeObject)
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

	if (Pin->GetDirection() == ERigVMPinDirection::IO)
	{
		bool bIsExecute = false;
		if (CPPTypeObject)
		{
			if(const UScriptStruct* CPPTypeStruct = Cast<UScriptStruct>(CPPTypeObject))
			{
				if(CPPTypeStruct->IsChildOf(FRigVMExecuteContext::StaticStruct()))
				{
					bIsExecute = true;
				}
			}
		}
		if (!bIsExecute)
		{
			ReportAndNotifyError(TEXT("Input/Output pins only allow Execute Context types."));
			return false;
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
		Action.Title = FString::Printf(TEXT("Change Exposed Pin Type"));
		ActionStack->BeginAction(Action);
	}

	FRigVMRegistry& Registry = FRigVMRegistry::Get();

	// If the pin does not support the type, first break all links in the contained graph to this pin (and subpins)
	TArray<URigVMTemplateNode*> InterfaceNodes = {Graph->GetEntryNode(), Graph->GetReturnNode()};

	// Break all links to this pin
	{
		TArray<URigVMLink*> InterfacePinLinks;
		TArray<URigVMPin*> ExtendedInterfacePins;
		for (URigVMNode* Node : Graph->GetNodes())
		{
			if (Node->IsA<URigVMFunctionEntryNode>() || Node->IsA<URigVMFunctionReturnNode>())
			{
				if (URigVMPin* InterfacePin = Node->FindPin(Pin->GetName()))
				{
					ExtendedInterfacePins.Add(InterfacePin);
				}
			}
			else if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
			{
				if (VariableNode->GetVariableName() == InPinName)
				{
					ExtendedInterfacePins.Add(VariableNode->GetValuePin());
				}
			}
		}

		for (URigVMPin* InterfacePin : ExtendedInterfacePins)
		{
			TArray<URigVMPin*> PinsToProcess;
			PinsToProcess.Add(InterfacePin);
			for (int32 i=0; i<PinsToProcess.Num(); ++i)
			{
				InterfacePinLinks.Append(PinsToProcess[i]->GetLinks());
				PinsToProcess.Append(PinsToProcess[i]->GetSubPins());
			}
		}
		
		for (int32 i=0; i<InterfacePinLinks.Num(); ++i)
		{
			BreakLink(InterfacePinLinks[i]->GetSourcePin(), InterfacePinLinks[i]->GetTargetPin(), bSetupUndoRedo);
		}
	}

	// Change pin type of the library node in the function library
	{
		bool bSuccessChangingType;
		{
			FRigVMControllerGraphGuard GraphGuard(this, LibraryNode->GetGraph(), bSetupUndoRedo);
			bSuccessChangingType = ChangePinType(Pin, InCPPType, InCPPTypeObjectPath, bSetupUndoRedo, bSetupOrphanPins);

			if (bSuccessChangingType)
			{
				RemoveUnusedOrphanedPins(LibraryNode);
			}
		}
		if (!bSuccessChangingType)
		{
			if (bSetupUndoRedo)
			{
				ActionStack->CancelAction(Action, this);
			}
			return false;
		}
	}

	// Repopulate pin on interface nodes
	for (URigVMTemplateNode* InterfaceNode : InterfaceNodes)
	{
		if (InterfaceNode)
		{
			RepopulatePinsOnNode(InterfaceNode, true, bSetupOrphanPins, true);
			RemoveUnusedOrphanedPins(InterfaceNode);
		}
	}

	// Change pin type on function references
	if (URigVMFunctionLibrary* FunctionLibrary = Cast<URigVMFunctionLibrary>(LibraryNode->GetGraph()))
	{
		FunctionLibrary->ForEachReference(LibraryNode->GetFName(), [this, &Pin, InCPPType, InCPPTypeObjectPath, bSetupUndoRedo, bSetupOrphanPins](URigVMFunctionReferenceNode* ReferenceNode)
        {
			if (URigVMPin* ReferencedNodePin = ReferenceNode->FindPin(Pin->GetName()))
			{
				FRigVMControllerGraphGuard GraphGuard(this, ReferenceNode->GetGraph(), bSetupUndoRedo);
				ChangePinType(ReferencedNodePin, InCPPType, InCPPTypeObjectPath, bSetupUndoRedo, bSetupOrphanPins);
				RemoveUnusedOrphanedPins(ReferenceNode);
			}
        });
	}

	// Change pin types on input variable nodes
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
					RemoveUnusedOrphanedPins(VariableNode);
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
			FString::Printf(TEXT("blueprint.get_controller_by_name('%s').change_exposed_pin_type('%s', '%s', '%s', %s)"),
				*GraphName,
				*GetSanitizedPinName(InPinName.ToString()),
				*InCPPType,
				*InCPPTypeObjectPath.ToString(),
				(bSetupUndoRedo) ? TEXT("True") : TEXT("False")));
	}

	return true;
}

bool URigVMController::SetExposedPinIndex(const FName& InPinName, int32 InNewIndex, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return false;
	}

	if (!bIsTransacting && !IsGraphEditable())
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
		return true; // Nothing to do, do not fail
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

	RefreshFunctionPins(LibraryNode->GetEntryNode());
	RefreshFunctionPins(LibraryNode->GetReturnNode());
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
	
	if (!InFunctionDefinition)
	{
		return nullptr;
	}

	if (URigVMFunctionReferenceNode* ReferenceNode = AddFunctionReferenceNodeFromDescription(InFunctionDefinition->GetFunctionHeader(), InNodePosition, InNodeName, bSetupUndoRedo, bPrintPythonCommand))
	{
		return ReferenceNode;
	}

	return nullptr;
}

URigVMFunctionReferenceNode* URigVMController::AddFunctionReferenceNodeFromDescription(const FRigVMGraphFunctionHeader& InFunctionDefinition, const FVector2D& InNodePosition, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand, bool bAllowPrivateFunctions)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	if (!bIsTransacting && !IsGraphEditable())
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

	if(!CanAddFunctionRefForDefinition(InFunctionDefinition, true, bAllowPrivateFunctions))
	{
		return nullptr;
	}

	FString NodeName = GetValidNodeName(InNodeName.IsEmpty() ? InFunctionDefinition.Name.ToString() : InNodeName);
	URigVMFunctionReferenceNode* FunctionRefNode = NewObject<URigVMFunctionReferenceNode>(Graph, *NodeName);
	FunctionRefNode->Position = InNodePosition;
	FunctionRefNode->ReferencedFunctionHeader = InFunctionDefinition;
	Graph->Nodes.Add(FunctionRefNode);

	FRigVMControllerCompileBracketScope CompileScope(this);
	
	RepopulatePinsOnNode(FunctionRefNode, false, false, false);

	Notify(ERigVMGraphNotifType::NodeAdded, FunctionRefNode);

	if (URigVMBuildData* BuildData = URigVMBuildData::Get())
	{
		BuildData->RegisterFunctionReference(FunctionRefNode->GetReferencedFunctionHeader().LibraryPointer, FunctionRefNode);
	}
	
	for (const FRigVMGraphFunctionArgument& Argument : InFunctionDefinition.Arguments)
	{
		if (URigVMPin* TargetPin = FunctionRefNode->FindPin(Argument.Name.ToString()))
		{
			const FString& DefaultValue = Argument.DefaultValue;
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
		const FString FunctionDefinitionName = GetSanitizedNodeName(InFunctionDefinition.Name.ToString());

		bool bLocal = false;
		if(IRigVMClientHost* ClientHost = GetImplementingOuter<IRigVMClientHost>())
		{
			if (InFunctionDefinition.LibraryPointer.HostObject == Cast<UObject>(ClientHost->GetRigVMGraphFunctionHost()))
			{
				bLocal = true;
				RigVMPythonUtils::Print(GetGraphOuterName(), 
					FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_function_reference_node(library.find_function('%s'), %s, '%s')"),
							*GraphName,
							*FunctionDefinitionName,
							*RigVMPythonUtils::Vector2DToPythonString(InNodePosition),
							*NodeName));
			}
		}
		
		if (!bLocal)
		{
			RigVMPythonUtils::Print(GetGraphOuterName(), 
				FString::Printf(TEXT("blueprint.get_controller_by_name('%s').add_external_function_reference_node('%s', '%s', %s, '%s')"),
						*GraphName,
						*InFunctionDefinition.LibraryPointer.HostObject.ToString(),
						*FunctionDefinitionName,
						*RigVMPythonUtils::Vector2DToPythonString(InNodePosition), 
						*NodeName));
		}
	}

	return FunctionRefNode;
}

URigVMFunctionReferenceNode* URigVMController::AddExternalFunctionReferenceNode(const FString& InHostPath, const FName& InFunctionName, const FVector2D& InNodePosition, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	if (!bIsTransacting && !IsGraphEditable())
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

	UObject* HostObject = StaticLoadObject(UObject::StaticClass(), NULL, *InHostPath, NULL, LOAD_None, NULL);
	if (!HostObject)
	{
		ReportErrorf(TEXT("Failed to load the Host object %s."), *InHostPath);
		return nullptr;
	}

	IRigVMGraphFunctionHost* FunctionHost = Cast<IRigVMGraphFunctionHost>(HostObject);
	if (!FunctionHost)
	{
		ReportError(TEXT("Host object is not a IRigVMGraphFunctionHost."));
		return nullptr;
	}

	FRigVMGraphFunctionData* Data = FunctionHost->GetRigVMGraphFunctionStore()->FindFunctionByName(InFunctionName);
	if (!Data)
	{
		ReportErrorf(TEXT("Function %s not found in host %s."), *InFunctionName.ToString(), *InHostPath);
		return nullptr;
	}

	return AddFunctionReferenceNodeFromDescription(Data->Header, InNodePosition, InNodeName, bSetupUndoRedo, bPrintPythonCommand);
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

	if (!bIsTransacting && !IsGraphEditable())
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
		if (FRigVMExternalVariable* Variable = InFunctionRefNode->ReferencedFunctionHeader.ExternalVariables.FindByPredicate([InInnerVariableName](const FRigVMExternalVariable& Variable)
		{
			return Variable.Name == InInnerVariableName;
		}))
		{
			InnerExternalVariable = *Variable;
		}
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

	if (!bIsTransacting && !IsGraphEditable())
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
	
	Notify(ERigVMGraphNotifType::NodeAdded, CollapseNode);
	
	if (bMutable)
	{
		const UScriptStruct* ExecuteContextStruct = FRigVMExecuteContext::StaticStruct();
		
		FRigVMControllerGraphGuard GraphGuard(this, CollapseNode->ContainedGraph, bSetupUndoRedo);
		AddExposedPin(FRigVMStruct::ExecuteContextName,
			ERigVMPinDirection::IO,
			FString::Printf(TEXT("F%s"), *ExecuteContextStruct->GetName()),
			*ExecuteContextStruct->GetPathName(),
			FString(),
			bSetupUndoRedo);
	}

	{
		FRigVMControllerGraphGuard GraphGuard(this, CollapseNode->GetContainedGraph(), false);
		TGuardValue<bool> GuardEditGraph(CollapseNode->ContainedGraph->bEditable, true);

		URigVMNode* EntryNode = CollapseNode->ContainedGraph->FindNode(TEXT("Entry"));
		URigVMNode* ReturnNode = CollapseNode->ContainedGraph->FindNode(TEXT("Return"));

		if (EntryNode == nullptr)
		{
			EntryNode = NewObject<URigVMFunctionEntryNode>(CollapseNode->ContainedGraph, TEXT("Entry"));
			CollapseNode->ContainedGraph->Nodes.Add(EntryNode);
			{
				TGuardValue<bool> SuspendNotifications(bSuspendNotifications, true);
				RefreshFunctionPins(EntryNode);
			}
			EntryNode->Position = FVector2D(-250.f, 0.f);
			Notify(ERigVMGraphNotifType::NodeAdded, EntryNode);
		}

		if (ReturnNode == nullptr)
		{
			ReturnNode = NewObject<URigVMFunctionReturnNode>(CollapseNode->ContainedGraph, TEXT("Return"));
			CollapseNode->ContainedGraph->Nodes.Add(ReturnNode);
			{
				TGuardValue<bool> SuspendNotifications(bSuspendNotifications, true);
				RefreshFunctionPins(ReturnNode);
			}
			ReturnNode->Position = FVector2D(250.f, 0.f);
			Notify(ERigVMGraphNotifType::NodeAdded, ReturnNode);
		}


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

	if (!bIsTransacting && !IsGraphEditable())
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

	if (!bIsTransacting && !IsGraphEditable())
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

bool URigVMController::MarkFunctionAsPublic(const FName& InFunctionName, bool bInIsPublic, bool bSetupUndoRedo,	bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return false;
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);


	if (!Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Can only change function definitions from function library graphs."));
		return false;
	}

	URigVMNode* Node = Graph->FindNode(InFunctionName.ToString());
	if (!Node)
	{
		ReportErrorf(TEXT("Could not find function called '%s'."), *InFunctionName.ToString());
		return false;
	}

	if (URigVMFunctionLibrary* FunctionLibrary = Cast<URigVMFunctionLibrary>(Graph))
	{
		bool bOldIsPublic = FunctionLibrary->PublicFunctionNames.Contains(InFunctionName); 
		if ((bInIsPublic && bOldIsPublic) || (!bInIsPublic && !bOldIsPublic))
		{
			return true;
		}

		if (bSetupUndoRedo)
		{
			FRigVMBaseAction BaseAction;
			BaseAction.Title = FString::Printf(TEXT("Mark function %s as %s"), *InFunctionName.ToString(), (bInIsPublic) ? TEXT("Public") : TEXT("Private"));
			ActionStack->BeginAction(BaseAction);
			ActionStack->AddAction(FRigVMMarkFunctionPublicAction(InFunctionName, bInIsPublic));
			ActionStack->EndAction(BaseAction);
		}

		if (bInIsPublic)
		{
			FunctionLibrary->PublicFunctionNames.Add(InFunctionName);
		}
		else
		{
			FunctionLibrary->PublicFunctionNames.Remove(InFunctionName);
		}
	}

	Notify(ERigVMGraphNotifType::FunctionAccessChanged, Node);

	if (!bSuspendNotifications)
	{
		Graph->MarkPackageDirty();
	}

	if (bPrintPythonCommand)
	{
		RigVMPythonUtils::Print(GetGraphOuterName(), 
			FString::Printf(TEXT("library_controller.mark_function_as_public('%s', %s)"),
				*GetSanitizedNodeName(InFunctionName.ToString()),
				(bInIsPublic) ? TEXT("True") : TEXT("False")));
	}

	return true;
}

bool URigVMController::IsFunctionPublic(const FName& InFunctionName)
{
	if (!IsValidGraph())
	{
		return false;
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);


	if (!Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Can only check function definitions from function library graphs."));
		return false;
	}

	URigVMNode* Node = Graph->FindNode(InFunctionName.ToString());
	if (!Node)
	{
		ReportErrorf(TEXT("Could not find function called '%s'."), *InFunctionName.ToString());
		return false;
	}

	if (URigVMFunctionLibrary* FunctionLibrary = Cast<URigVMFunctionLibrary>(Graph))
	{
		return FunctionLibrary->PublicFunctionNames.Contains(InFunctionName);
	}

	return false;
}

FRigVMGraphVariableDescription URigVMController::AddLocalVariable(const FName& InVariableName, const FString& InCPPType, UObject* InCPPTypeObject, const FString& InDefaultValue, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	FRigVMGraphVariableDescription NewVariable;
	if (!IsValidGraph())
	{
		return NewVariable;
	}

	if (!bIsTransacting && !IsGraphEditable())
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

	if (!bIsTransacting && !IsGraphEditable())
	{
		return Description;
	}

	UObject* CPPTypeObject = nullptr;
	if (!InCPPTypeObjectPath.IsEmpty())
	{
		CPPTypeObject = RigVMTypeUtils::FindObjectFromCPPTypeObjectPath<UObject>(InCPPTypeObjectPath);
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

	if (!bIsTransacting && !IsGraphEditable())
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

	if (!bIsTransacting && !IsGraphEditable())
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
		FRigVMBaseAction BaseAction;
		BaseAction.Title = FString::Printf(TEXT("Rename Local Variable %s to %s"), *InVariableName.ToString(), *InNewVariableName.ToString());

		ActionStack->BeginAction(BaseAction);
		ActionStack->AddAction(FRigVMRenameLocalVariableAction(LocalVariables[FoundIndex].Name, InNewVariableName));
		ActionStack->EndAction(BaseAction);
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

	if (!bIsTransacting && !IsGraphEditable())
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
		CPPTypeObject = RigVMTypeUtils::FindObjectFromCPPTypeObjectPath<UObject>(InCPPTypeObjectPath);
		if (CPPTypeObject == nullptr)
		{
			ReportErrorf(TEXT("Cannot find cpp type object for path '%s'."), *InCPPTypeObjectPath);
			return false;
		}
	}

	return SetLocalVariableType(InVariableName, InCPPType, CPPTypeObject, bSetupUndoRedo, bPrintPythonCommand);
}

bool URigVMController::SetLocalVariableDefaultValue(const FName& InVariableName, const FString& InDefaultValue, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return false;
	}

	if (!bIsTransacting && !IsGraphEditable())
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

URigVMUserWorkflowOptions* URigVMController::MakeOptionsForWorkflow(UObject* InSubject, const FRigVMUserWorkflow& InWorkflow)
{
	URigVMUserWorkflowOptions* Options = nullptr;

	UClass* Class = InWorkflow.GetOptionsClass();
	if(Class == nullptr)
	{
		return Options;
	}

	if(!Class->IsChildOf(URigVMUserWorkflowOptions::StaticClass()))
	{
		return Options;
	}
	
	Options = NewObject<URigVMUserWorkflowOptions>(GetTransientPackage(), Class, NAME_None, RF_Transient);
	Options->Subject = InSubject;
	Options->Workflow = InWorkflow;

	TWeakObjectPtr<URigVMController> WeakThis = this;
	Options->ReportDelegate = FRigVMReportDelegate::CreateLambda([WeakThis](
		EMessageSeverity::Type InSeverity, UObject* InSubject, const FString& InMessage)
		{
			if(URigVMController* StrongThis = WeakThis.Get())
			{
				if(InSeverity == EMessageSeverity::Error)
				{
					StrongThis->ReportAndNotifyError(InMessage);
				}
				else if(InSeverity == EMessageSeverity::Warning ||
					InSeverity == EMessageSeverity::PerformanceWarning)
				{
					StrongThis->ReportAndNotifyWarning(InMessage);
				}
				else 
				{
					StrongThis->ReportInfo(InMessage);
				}
			}
		}
	);

	if(ConfigureWorkflowOptionsDelegate.IsBound())
	{
		ConfigureWorkflowOptionsDelegate.Execute(Options);
	}
	
	return Options;
}

bool URigVMController::PerformUserWorkflow(const FRigVMUserWorkflow& InWorkflow,
	const URigVMUserWorkflowOptions* InOptions, bool bSetupUndoRedo)
{
	if(!InWorkflow.IsValid() || !ensure(InOptions != nullptr))
	{
		return false;
	}


	FRigVMBaseAction Bracket;
	Bracket.Title = InWorkflow.GetTitle();
	ActionStack->BeginAction(Bracket);

	const bool bSuccess = InWorkflow.Perform(InOptions, this);

	ActionStack->EndAction(Bracket);

	if(!bSuccess)
	{
		// if the workflow was run as the top level action we'll undo
		if(ActionStack->CurrentActions.IsEmpty())
		{
			ActionStack->Undo(this);
		}
	}

	return bSuccess;
}

TArray<TSoftObjectPtr<URigVMFunctionReferenceNode>> URigVMController::GetAffectedReferences(ERigVMControllerBulkEditType InEditType, bool bForceLoad)
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
			if(OnBulkEditProgressDelegate.IsBound() && !bSuspendNotifications)
			{
				OnBulkEditProgressDelegate.Execute(FunctionReferencePtr, InEditType, ERigVMControllerBulkEditProgress::BeginLoad, FunctionReferenceIndex, FunctionReferencePtrs.Num());
			}

			if(!FunctionReferencePtr.IsValid())
			{
				FunctionReferencePtr.LoadSynchronous();
			}

			if(OnBulkEditProgressDelegate.IsBound() && !bSuspendNotifications)
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
				if(URigVMLibraryNode* AffectedFunction = AffectedFunctionReferenceNode->FindFunctionForNode())
				{
					FRigVMControllerGraphGuard GraphGuard(this, AffectedFunction->GetContainedGraph(), false);
					TGuardValue<bool> SuspendNotifications(bSuspendNotifications, true);
					TArray<TSoftObjectPtr<URigVMFunctionReferenceNode>> AffectedFunctionReferencePtrs = GetAffectedReferences(InEditType, bForceLoad);
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
	
#endif

	return FunctionReferencePtrs;
}

TArray<FAssetData> URigVMController::GetAffectedAssets(ERigVMControllerBulkEditType InEditType, bool bForceLoad)
{
	TArray<FAssetData> Assets;

#if WITH_EDITOR

	if(!IsValidGraph())
	{
		return Assets;
	}

	TArray<TSoftObjectPtr<URigVMFunctionReferenceNode>> FunctionReferencePtrs = GetAffectedReferences(InEditType, bForceLoad);
	TMap<FString, int32> VisitedAssets;

	URigVMGraph* Graph = GetGraph();
	TSoftObjectPtr<URigVMGraph> GraphPtr = Graph;
	const FString ThisAssetPath = GraphPtr.ToSoftObjectPath().GetAssetPath().ToString();

	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	for(int32 FunctionReferenceIndex = 0; FunctionReferenceIndex < FunctionReferencePtrs.Num(); FunctionReferenceIndex++)
	{
		TSoftObjectPtr<URigVMFunctionReferenceNode> FunctionReferencePtr = FunctionReferencePtrs[FunctionReferenceIndex];
		const FString AssetPath = FunctionReferencePtr.ToSoftObjectPath().GetAssetPath().ToString();
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
					
		const FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(AssetPath));
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
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
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

	TArray<FRigVMExternalVariable> Descriptions = GetAllVariables();
	TMap<FName, int32> NameToIndex;
	for (int32 VariableIndex = 0; VariableIndex < Descriptions.Num(); VariableIndex++)
	{
		NameToIndex.Add(Descriptions[VariableIndex].Name, VariableIndex);
	}

	const FRigVMExternalVariable VariableType = RigVMTypeUtils::ExternalVariableFromCPPType(InVariableName, InVariableNode->GetCPPType(), InVariableNode->GetCPPTypeObject());
	FName VariableName = GetUniqueName(InVariableName, [Descriptions, NameToIndex, VariableType](const FName& InName) {
		const int32* FoundIndex = NameToIndex.Find(InName);
		if (FoundIndex == nullptr)
		{
			return true;
		}
		return VariableType.TypeName == Descriptions[*FoundIndex].TypeName &&
				VariableType.TypeObject == Descriptions[*FoundIndex].TypeObject &&
				VariableType.bIsArray == Descriptions[*FoundIndex].bIsArray;
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

URigVMRerouteNode* URigVMController::AddFreeRerouteNode(bool bShowAsFullNode, const FString& InCPPType, const FName& InCPPTypeObjectPath, bool bIsConstant, const FName& InCustomWidgetName, const FString& InDefaultValue, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	if (!bIsTransacting && !IsGraphEditable())
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
		{
			TGuardValue<bool> SuspendNotifications(bSuspendNotifications, true);
			AddPinsForStruct(ValuePin->GetScriptStruct(), Node, ValuePin, ValuePin->Direction, DefaultValue, false);
		}
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

URigVMNode* URigVMController::AddBranchNode(const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	return AddUnitNode(FRigVMFunction_ControlFlowBranch::StaticStruct(), FRigVMStruct::ExecuteName, InPosition, InNodeName, bSetupUndoRedo, bPrintPythonCommand);
}

URigVMNode* URigVMController::AddIfNode(const FString& InCPPType, const FName& InCPPTypeObjectPath, const FVector2D& InPosition, const FString& InNodeName, bool  bSetupUndoRedo, bool bPrintPythonCommand)
{
	if(!IsValidGraph())
	{
		return nullptr;
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	ensure(!InCPPType.IsEmpty());

	UObject* CPPTypeObject = nullptr;
	if(!InCPPTypeObjectPath.IsNone())
	{
		CPPTypeObject = RigVMTypeUtils::FindObjectFromCPPTypeObjectPath<UObject>(InCPPTypeObjectPath.ToString());
		if (CPPTypeObject == nullptr)
		{
			ReportErrorf(TEXT("Cannot find cpp type object for path '%s'."), *InCPPTypeObjectPath.ToString());
			return nullptr;
		}
	}

	if(bSetupUndoRedo)
	{
		OpenUndoBracket(TEXT("Add If Node"));
	}

	const FString CPPType = RigVMTypeUtils::PostProcessCPPType(InCPPType, CPPTypeObject);
	const FString Name = GetValidNodeName(InNodeName.IsEmpty() ? FString(TEXT("IfNode")) : InNodeName);
	const TRigVMTypeIndex& TypeIndex = FRigVMRegistry::Get().FindOrAddType({*CPPType, CPPTypeObject});
	
	const FRigVMDispatchFactory* Factory = FRigVMRegistry::Get().FindOrAddDispatchFactory(FRigVMDispatch_If::StaticStruct());
	URigVMNode* Node = AddTemplateNode(Factory->GetTemplate()->GetNotation(), InPosition, Name, bSetupUndoRedo, bPrintPythonCommand);
	if(Node)
	{
		ResolveWildCardPin(Node->GetPins().Last(), TypeIndex, bSetupUndoRedo, bPrintPythonCommand);
	}
	
	if(bSetupUndoRedo)
	{
		CloseUndoBracket();
	}

	return Node;
}

URigVMNode* URigVMController::AddIfNodeFromStruct(UScriptStruct* InScriptStruct, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo)
{
	if (!InScriptStruct)
	{
		return nullptr;
	}

	return AddIfNode(RigVMTypeUtils::GetUniqueStructTypeName(InScriptStruct), FName(InScriptStruct->GetPathName()), InPosition, InNodeName, bSetupUndoRedo);
}

URigVMNode* URigVMController::AddSelectNode(const FString& InCPPType, const FName& InCPPTypeObjectPath, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	ensure(!InCPPType.IsEmpty());

	UObject* CPPTypeObject = nullptr;
	if (!InCPPTypeObjectPath.IsNone())
	{
		CPPTypeObject = RigVMTypeUtils::FindObjectFromCPPTypeObjectPath<UObject>(InCPPTypeObjectPath.ToString());
		if (CPPTypeObject == nullptr)
		{
			ReportErrorf(TEXT("Cannot find cpp type object for path '%s'."), *InCPPTypeObjectPath.ToString());
			return nullptr;
		}
	}

	if(bSetupUndoRedo)
	{
		OpenUndoBracket(TEXT("Add Select Node"));
	}

	const FString CPPType = RigVMTypeUtils::PostProcessCPPType(InCPPType, CPPTypeObject);
	const FString Name = GetValidNodeName(InNodeName.IsEmpty() ? FString(TEXT("SelectNode")) : InNodeName);
	const TRigVMTypeIndex& TypeIndex = FRigVMRegistry::Get().FindOrAddType({*CPPType, CPPTypeObject});
	
	const FRigVMDispatchFactory* Factory = FRigVMRegistry::Get().FindOrAddDispatchFactory(FRigVMDispatch_SelectInt32::StaticStruct());
	URigVMNode* Node = AddTemplateNode(Factory->GetTemplate()->GetNotation(), InPosition, Name, bSetupUndoRedo, bPrintPythonCommand);
	if(Node)
	{
		ResolveWildCardPin(Node->GetPins().Last(), TypeIndex, bSetupUndoRedo, bPrintPythonCommand);
	}

	if(bSetupUndoRedo)
	{
		CloseUndoBracket();
	}
	return Node;
}

URigVMNode* URigVMController::AddSelectNodeFromStruct(UScriptStruct* InScriptStruct, const FVector2D& InPosition,
	const FString& InNodeName, bool bSetupUndoRedo)
{
	if (!InScriptStruct)
	{
		return nullptr;
	}

	return AddSelectNode(RigVMTypeUtils::GetUniqueStructTypeName(InScriptStruct), FName(InScriptStruct->GetPathName()), InPosition, InNodeName, bSetupUndoRedo);
}

URigVMTemplateNode* URigVMController::AddTemplateNode(const FName& InNotation, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	ensure(!InNotation.IsNone());

	const FRigVMTemplate* Template = FRigVMRegistry::Get().FindTemplate(InNotation);
	if (Template == nullptr)
	{
		ReportErrorf(TEXT("Template '%s' cannot be found."), *InNotation.ToString());
		return nullptr;
	}

	if(IRigVMClientHost* ClientHost = GetImplementingOuter<IRigVMClientHost>())
	{
		if(const FRigVMClient* Client = ClientHost->GetRigVMClient())
		{
			if(!Template->SupportsExecuteContextStruct(Client->GetExecuteContextStruct()))
			{
				ReportErrorf(TEXT("Cannot add node for template '%s' - incompatible execute context: '%s' vs '%s'."),
						*Template->GetNotation().ToString(),
						*Template->GetExecuteContextStruct()->GetStructCPPName(),
						*Client->GetExecuteContextStruct()->GetStructCPPName());
				return nullptr;
			}
		}
	}

	FString Name = GetValidNodeName(InNodeName.IsEmpty() ? Template->GetName().ToString() : InNodeName);
	URigVMTemplateNode* Node = nullptr;

	// determine what kind of node we need to create
	if(Template->UsesDispatch())
	{
		Node = NewObject<URigVMDispatchNode>(Graph, *Name);
	}
	else if(const FRigVMFunction* FirstFunction = Template->GetPermutation(0))
	{
		const UScriptStruct* PotentialUnitStruct = FirstFunction->Struct;
		if(PotentialUnitStruct && PotentialUnitStruct->IsChildOf(FRigVMStruct::StaticStruct()))
		{
			Node = NewObject<URigVMUnitNode>(Graph, *Name); 
		}
	}

	if(Node == nullptr)
	{
		const FString TemplateName = Template->GetName().ToString();
		if(TemplateName == URigVMRerouteNode::RerouteName)
		{
			Node = NewObject<URigVMRerouteNode>(Graph, *Name);
		}
	}

	if(Node == nullptr)
	{
		ReportErrorf(TEXT("Template node '%s' cannot be created. Unknown template."), *InNotation.ToString());
		return nullptr;
	}
	
	Node->TemplateNotation = Template->GetNotation();
	Node->Position = InPosition;

	int32 PermutationIndex = INDEX_NONE;
	FRigVMTemplate::FTypeMap Types;
	Template->FullyResolve(Types, PermutationIndex);

	FRigVMRegistry& Registry = FRigVMRegistry::Get();
	AddPinsForTemplate(Template, Types, Node);

	if (Node->HasWildCardPin())
	{
		UpdateTemplateNodePinTypes(Node, false);
	}
	else
	{
		if (!Node->IsA<URigVMFunctionEntryNode>() && !Node->IsA<URigVMFunctionReturnNode>())
		{
			FullyResolveTemplateNode(Node, INDEX_NONE, false);
		}
	}

	Graph->Nodes.Add(Node);

	Notify(ERigVMGraphNotifType::NodeAdded, Node);

	FRigVMAddTemplateNodeAction Action;
	FRigVMControllerCompileBracketScope CompileScope(this);
	if (bSetupUndoRedo)
	{
		Action = FRigVMAddTemplateNodeAction(Node);
		ActionStack->BeginAction(Action);
	}
	
	ResolveTemplateNodeMetaData(Node, bSetupUndoRedo);

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

TArray<UScriptStruct*> URigVMController::GetRegisteredUnitStructs()
{
	TArray<UScriptStruct*> UnitStructs;

	for(const FRigVMFunction& Function : FRigVMRegistry::Get().GetFunctions())
	{
		if(!Function.IsValid())
		{
			continue;
		}
		if(UScriptStruct* Struct = Function.Struct)
		{
			if (!Struct->IsChildOf(FRigVMStruct::StaticStruct()))
			{
				continue;
			}
			UnitStructs.Add(Struct);
		}
	}
	
	return UnitStructs; 
}

TArray<FString> URigVMController::GetRegisteredTemplates()
{
	TArray<FString> Templates;

	for(const FRigVMTemplate& Template : FRigVMRegistry::Get().GetTemplates())
	{
		if(!Template.IsValid() || Template.NumPermutations() < 2)
		{
			continue;
		}
		Templates.Add(Template.GetNotation().ToString());
	}
	
	return Templates; 
}

TArray<UScriptStruct*> URigVMController::GetUnitStructsForTemplate(const FName& InNotation)
{
	TArray<UScriptStruct*> UnitStructs;

	const FRigVMTemplate* Template = FRigVMRegistry::Get().FindTemplate(InNotation);
	if(Template)
	{
		if(!Template->UsesDispatch())
		{
			for(int32 PermutationIndex = 0; PermutationIndex < Template->NumPermutations(); PermutationIndex++)
			{
				UnitStructs.Add(Template->GetPermutation(PermutationIndex)->Struct);
			}
		}
	}
	
	return UnitStructs;
}

FString URigVMController::GetTemplateForUnitStruct(UScriptStruct* InFunction, const FString& InMethodName)
{
	if(const FRigVMFunction* Function = FRigVMRegistry::Get().FindFunction(InFunction, *InMethodName))
	{
		if(const FRigVMTemplate* Template = Function->GetTemplate())
		{
			return Template->GetNotation().ToString();
		}
	}
	return FString();
}

URigVMEnumNode* URigVMController::AddEnumNode(const FName& InCPPTypeObjectPath, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

    UObject* CPPTypeObject = RigVMTypeUtils::FindObjectFromCPPTypeObjectPath<UObject>(InCPPTypeObjectPath.ToString());
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
	EnumIndexPin->CPPType = RigVMTypeUtils::Int32Type;
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

URigVMNode* URigVMController::AddArrayNode(ERigVMOpCode InOpCode, const FString& InCPPType,
	UObject* InCPPTypeObject, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo,
	bool bPrintPythonCommand, bool bIsPatching)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return nullptr;
	}

	const FString CPPType = RigVMTypeUtils::IsArrayType(InCPPType) ? RigVMTypeUtils::BaseTypeFromArrayType(InCPPType) : InCPPType;
	const TRigVMTypeIndex& ElementTypeIndex = FRigVMRegistry::Get().FindOrAddType({*CPPType, InCPPTypeObject});
	if(ElementTypeIndex == INDEX_NONE)
	{
		return nullptr;
	}
	const TRigVMTypeIndex& ArrayTypeIndex = FRigVMRegistry::Get().GetArrayTypeFromBaseTypeIndex(ElementTypeIndex);

	const FName FactoryName = FRigVMDispatch_ArrayBase::GetFactoryNameForOpCode(InOpCode);
	if(FactoryName.IsNone())
	{
		ReportErrorf(TEXT("OpCode '%s' is not valid for Array Node."), *StaticEnum<ERigVMOpCode>()->GetNameStringByValue((int64)InOpCode));
		return nullptr;
	}

	const FRigVMDispatchFactory* Factory = FRigVMRegistry::Get().FindDispatchFactory(FactoryName);
	if(Factory == nullptr)
	{
		ReportErrorf(TEXT("Cannot find array dispatch '%s'."), *FactoryName.ToString());
		return nullptr;
	}

	if(bSetupUndoRedo)
	{
		OpenUndoBracket(TEXT("Add Array Node"));
	}
	
	const FRigVMTemplate* Template = Factory->GetTemplate();
	URigVMTemplateNode* Node = AddTemplateNode(
		Template->GetNotation(),
		InPosition, 
		InNodeName, 
		bSetupUndoRedo, 
		bPrintPythonCommand);

	if(!FRigVMRegistry::Get().IsWildCardType(ElementTypeIndex))
	{
		FName ArgumentNameToResolve = NAME_None;
		TRigVMTypeIndex TypeIndex = INDEX_NONE;
		for(int32 Index = 0; Index < Template->NumArguments(); Index++)
		{
			const FRigVMTemplateArgument* Argument = Template->GetArgument(Index);
			if(Argument->IsSingleton())
			{
				continue;
			}
			if(Argument->GetArrayType() == FRigVMTemplateArgument::EArrayType_SingleValue)
			{
				ArgumentNameToResolve = Argument->GetName();
				TypeIndex = ElementTypeIndex;
				break;
			}
			if(Argument->GetArrayType() == FRigVMTemplateArgument::EArrayType_ArrayValue)
			{
				ArgumentNameToResolve = Argument->GetName();
				TypeIndex = ArrayTypeIndex;
				break;
			}
		}

		if(!ArgumentNameToResolve.IsNone() && TypeIndex != INDEX_NONE)
		{
			if(bIsPatching)
			{
				FRigVMTemplate::FTypeMap TypeMap;
				TypeMap.Add(ArgumentNameToResolve, TypeIndex);

				TArray<int32> Permutations;
				Template->Resolve(TypeMap, Permutations, false);
				check(Permutations.Num() == 1);

				for(const FRigVMTemplate::FTypePair& Pair : TypeMap)
				{
					if(!FRigVMRegistry::Get().IsWildCardType(Pair.Value))
					{
						if(URigVMPin* Pin = Node->FindPin(Pair.Key.ToString()))
						{
							ChangePinType(Pin, Pair.Value, false, false);
						}
					}
				}

				FullyResolveTemplateNode(Node, Permutations[0], false);
			}
			else if(const URigVMPin* Pin = Node->FindPin(ArgumentNameToResolve.ToString()))
			{
				ResolveWildCardPin(Pin->GetPinPath(), TypeIndex, bSetupUndoRedo, bPrintPythonCommand);
			}
		}
	}

	if(bSetupUndoRedo)
	{
		CloseUndoBracket();
	}

	return Node;
}

URigVMNode* URigVMController::AddArrayNodeFromObjectPath(ERigVMOpCode InOpCode, const FString& InCPPType,
	const FString& InCPPTypeObjectPath, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo,
	bool bPrintPythonCommand, bool bIsPatching)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return nullptr;
	}

	UObject* CPPTypeObject = nullptr;
	if (!InCPPTypeObjectPath.IsEmpty())
	{
		CPPTypeObject = RigVMTypeUtils::FindObjectFromCPPTypeObjectPath<UObject>(InCPPTypeObjectPath);
		if (CPPTypeObject == nullptr)
		{
			ReportErrorf(TEXT("Cannot find cpp type object for path '%s'."), *InCPPTypeObjectPath);
			return nullptr;
		}
	}

	return AddArrayNode(InOpCode, InCPPType, CPPTypeObject, InPosition, InNodeName, bSetupUndoRedo, bPrintPythonCommand, bIsPatching);
}

URigVMInvokeEntryNode* URigVMController::AddInvokeEntryNode(const FName& InEntryName, const FVector2D& InPosition,
	const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return nullptr;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (Graph->IsA<URigVMFunctionLibrary>())
	{
		ReportError(TEXT("Cannot add invoke entry nodes to function library graphs."));
		return nullptr;
	}

	FString Name = GetValidNodeName(InNodeName.IsEmpty() ? FString(TEXT("InvokeEntryNode")) : InNodeName);
	URigVMInvokeEntryNode* Node = NewObject<URigVMInvokeEntryNode>(Graph, *Name);
	Node->Position = InPosition;

	URigVMPin* ExecutePin = MakeExecutePin(Node, FRigVMStruct::ExecuteContextName);
	ExecutePin->Direction = ERigVMPinDirection::IO;
	AddNodePin(Node, ExecutePin);

	URigVMPin* EntryNamePin = NewObject<URigVMPin>(Node, *URigVMInvokeEntryNode::EntryName);
	EntryNamePin->CPPType = RigVMTypeUtils::FNameType;
	EntryNamePin->Direction = ERigVMPinDirection::Input;
	EntryNamePin->bIsConstant = true;
	EntryNamePin->DefaultValue = InEntryName.ToString();
	EntryNamePin->CustomWidgetName = TEXT("EntryName");
	AddNodePin(Node, EntryNamePin);

	Graph->Nodes.Add(Node);

	if (!bSuspendNotifications)
	{
		Graph->MarkPackageDirty();
	}

	FRigVMControllerCompileBracketScope CompileScope(this);

	Notify(ERigVMGraphNotifType::NodeAdded, Node);
	Notify(ERigVMGraphNotifType::VariableAdded, Node);

	if (bSetupUndoRedo)
	{
		FRigVMAddInvokeEntryNodeAction Action(Node);
		Action.Title = FString::Printf(TEXT("Add Invoke %s Entry"), *InEntryName.ToString());
		ActionStack->AddAction(Action);
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

	if (!IsValid(Graph))
	{
		return false;
	}

	return true;
}

bool URigVMController::IsGraphEditable() const
{
	if(!IsValidGraph())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	return Graph->bEditable;
}

bool URigVMController::IsValidNodeForGraph(URigVMNode* InNode)
{
	if(!IsValidGraph())
	{
		return false;
	}

	if (!bIsTransacting && !IsGraphEditable())
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

	if (!bIsTransacting && !IsGraphEditable())
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

	if (!bIsTransacting && !IsGraphEditable())
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

	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}

	check(InNode);

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (Graph->IsA<URigVMFunctionLibrary>())
	{
		if (!InNode->IsA<URigVMCollapseNode>())
		{
			return false;
		}
	}

	if (URigVMFunctionReferenceNode* FunctionRefNode = Cast<URigVMFunctionReferenceNode>(InNode))
	{
		if (URigVMFunctionLibrary* FunctionLibrary = FunctionRefNode->GetLibrary())
		{			
			FRigVMGraphFunctionHeader FunctionDefinition = FunctionRefNode->GetReferencedFunctionHeader();
			if(!CanAddFunctionRefForDefinition(FunctionDefinition, false))
			{
				URigVMFunctionLibrary* TargetLibrary = Graph->GetDefaultFunctionLibrary();
				URigVMLibraryNode* NewFunctionDefinition = TargetLibrary->FindPreviouslyLocalizedFunction(FunctionDefinition.LibraryPointer);
			
				if((NewFunctionDefinition == nullptr) && RequestLocalizeFunctionDelegate.IsBound())
				{
					if(RequestLocalizeFunctionDelegate.Execute(FunctionDefinition.LibraryPointer))
					{
						NewFunctionDefinition = TargetLibrary->FindPreviouslyLocalizedFunction(FunctionDefinition.LibraryPointer);
					}
				}

				if(NewFunctionDefinition == nullptr)
				{
					return false;
				}
			
				SetReferencedFunction(FunctionRefNode, NewFunctionDefinition, false);
				FunctionDefinition = FunctionRefNode->GetReferencedFunctionHeader();
			}
			
			if(!CanAddFunctionRefForDefinition(FunctionDefinition, bReportErrors))
			{
				DestroyObject(InNode);
				return false;
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

	if (!bIsTransacting && !IsGraphEditable())
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
	const bool bHasEventNode = (EventNode != nullptr) && EventNode->CanOnlyExistOnce();
	if (bHasEventNode && bReportErrors)
	{
		const FString ErrorMessage = FString::Printf(TEXT("Rig Graph can only contain one single %s node."),
													 *InScriptStruct->GetDisplayNameText().ToString());
		ReportAndNotifyError(ErrorMessage);
	}
		
	return !bHasEventNode;
}

bool URigVMController::CanAddFunctionRefForDefinition(const FRigVMGraphFunctionHeader& InFunctionDefinition, bool bReportErrors, bool bAllowPrivateFunctions)
{
	if(!IsValidGraph())
	{
		return false;
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (!bAllowPrivateFunctions)
	{
		if(IRigVMClientHost* ClientHost = GetImplementingOuter<IRigVMClientHost>())
		{
			bool bIsAvailable = ClientHost->GetRigVMClient()->GetFunctionLibrary()->GetFunctionHostObjectPath() ==
				InFunctionDefinition.LibraryPointer.HostObject;
			if (!bIsAvailable)
			{
				if (IRigVMGraphFunctionHost* Host = Cast<IRigVMGraphFunctionHost>(InFunctionDefinition.LibraryPointer.HostObject.TryLoad()))
				{
					bIsAvailable = Host->GetRigVMGraphFunctionStore()->IsFunctionPublic(InFunctionDefinition.LibraryPointer);		
				}
			}
			if (!bIsAvailable)
			{
				if(bReportErrors)
				{
					ReportAndNotifyError(TEXT("Function is not available for placement in another graph host."));
				}
				return false;
			}
		}
	}

	if (URigVMNode* Node = Cast<URigVMNode>(Graph->GetOuter()))
	{
		if (URigVMLibraryNode* LibraryNode = Node->FindFunctionForNode())
		{
			if (InFunctionDefinition.Dependencies.Contains(LibraryNode->GetFunctionIdentifier()))
			{
				if(bReportErrors)
				{
					ReportAndNotifyError(TEXT("Function is not available for placement in this graph host due to dependency cycles."));
				}
				return false;
			}
		}
	}
	
	URigVMLibraryNode* ParentLibraryNode = Cast<URigVMLibraryNode>(Graph->GetOuter());
	while (ParentLibraryNode)
	{
		if (TSoftObjectPtr<UObject>(ParentLibraryNode).ToSoftObjectPath() == InFunctionDefinition.LibraryPointer.LibraryNode)
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

void URigVMController::AddPinsForStruct(UStruct* InStruct, URigVMNode* InNode, URigVMPin* InParentPin, ERigVMPinDirection InPinDirection, const FString& InDefaultValue, bool bAutoExpandArrays, const FPinInfoArray* PreviousPins)
{
	if(!ShouldStructBeUnfolded(InStruct))
	{
		return;
	}

	// todo: reuse the default values when creating the pins
	
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

	TArray<UStruct*> StructsToVisit = FRigVMTemplate::GetSuperStructs(InStruct, true);
	for(UStruct* StructToVisit : StructsToVisit)
	{
		// using EFieldIterationFlags::None excludes the
		// properties of the super struct in this iterator.
		for (TFieldIterator<FProperty> It(StructToVisit, EFieldIterationFlags::None); It; ++It)
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
					{
						TGuardValue<bool> SuspendNotifications(bSuspendNotifications, true);
						AddPinsForStruct(StructProperty->Struct, InNode, Pin, Pin->GetDirection(), DefaultValue, bAutoExpandArrays);
					}
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

			if (!bSuspendNotifications)
			{
				Notify(ERigVMGraphNotifType::PinAdded, Pin);
			}
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
				{
					TGuardValue<bool> SuspendNotifications(bSuspendNotifications, true);
					AddPinsForStruct(StructProperty->Struct, InNode, Pin, Pin->Direction, DefaultValue, bAutoExpandArrays);
				}
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

void URigVMController::AddPinsForTemplate(const FRigVMTemplate* InTemplate, const FRigVMTemplateTypeMap& InPinTypeMap, URigVMNode* InNode)
{
	const FRigVMRegistry& Registry = FRigVMRegistry::Get();

	FRigVMDispatchContext DispatchContext;
	if(const URigVMDispatchNode* DispatchNode = Cast<URigVMDispatchNode>(InNode))
	{
		DispatchContext = DispatchNode->GetDispatchContext();
	}

	auto AddExecutePins = [InTemplate, InNode, &Registry, &DispatchContext, this](ERigVMPinDirection InPinDirection)
	{
		for (int32 ArgIndex = 0; ArgIndex < InTemplate->NumExecuteArguments(DispatchContext); ArgIndex++)
		{
			const FRigVMExecuteArgument* Arg = InTemplate->GetExecuteArgument(ArgIndex, DispatchContext);
			if(Arg->Direction != InPinDirection)
			{
				continue;
			}
			
			URigVMPin* Pin = NewObject<URigVMPin>(InNode, Arg->Name);
			const FRigVMTemplateArgumentType Type = Registry.GetType(Arg->TypeIndex);
	        
			Pin->CPPType = Type.CPPType.ToString();
			Pin->CPPTypeObject = Type.CPPTypeObject;
			if (Pin->CPPTypeObject)
			{
				Pin->CPPTypeObjectPath = *Pin->CPPTypeObject->GetPathName();
			}
			Pin->Direction = Arg->Direction;
			Pin->LastKnownTypeIndex = Arg->TypeIndex;
			Pin->LastKnownCPPType = Pin->CPPType;

			AddNodePin(InNode, Pin);

			if(Registry.IsArrayType(Arg->TypeIndex))
			{
				if(const URigVMDispatchNode* DispatchNode = Cast<URigVMDispatchNode>(Pin->GetNode()))
				{
					if(const FRigVMDispatchFactory* Factory = DispatchNode->GetFactory())
					{
						const FString DefaultValue =  Factory->GetArgumentDefaultValue(Pin->GetFName(), Arg->TypeIndex);
						if(!DefaultValue.IsEmpty())
						{
							SetPinDefaultValue(Pin, DefaultValue, true, false, false);
						}
					}
				}
			}
		}
	};

	AddExecutePins(ERigVMPinDirection::IO);
	AddExecutePins(ERigVMPinDirection::Input);

	for (int32 ArgIndex = 0; ArgIndex < InTemplate->NumArguments(); ArgIndex++)
	{
		const FRigVMTemplateArgument* Arg = InTemplate->GetArgument(ArgIndex);

		URigVMPin* Pin = NewObject<URigVMPin>(InNode, Arg->GetName());
		const TRigVMTypeIndex& TypeIndex = InPinTypeMap.FindChecked(Arg->GetName());
		const FRigVMTemplateArgumentType Type = FRigVMRegistry::Get().GetType(TypeIndex);
		Pin->CPPType = Type.CPPType.ToString();
		Pin->CPPTypeObject = Type.CPPTypeObject;
		if (Pin->CPPTypeObject)
		{
			Pin->CPPTypeObjectPath = *Pin->CPPTypeObject->GetPathName();
		}
		Pin->Direction = Arg->GetDirection();

		AddNodePin(InNode, Pin);

		if(!Pin->IsWildCard() && !Pin->IsArray())
		{
			FString DefaultValue;
			if(const URigVMTemplateNode* TemplateNode = Cast<URigVMTemplateNode>(InNode))
			{
				DefaultValue = TemplateNode->GetInitialDefaultValueForPin(Pin->GetFName());
			}

			TGuardValue<bool> SuspendNotifications(bSuspendNotifications, true);
			if(UScriptStruct* ScriptStruct = Cast<UScriptStruct>(Pin->CPPTypeObject))
			{
				AddPinsForStruct(ScriptStruct, Pin->GetNode(), Pin, Pin->Direction, DefaultValue, false);
			}
			else if(!DefaultValue.IsEmpty())
			{
				SetPinDefaultValue(Pin, DefaultValue, true, false, false);
			}
		}
		else if(Pin->IsFixedSizeArray())
		{
			if(const URigVMDispatchNode* DispatchNode = Cast<URigVMDispatchNode>(Pin->GetNode()))
			{
				if(const FRigVMDispatchFactory* Factory = DispatchNode->GetFactory())
				{
					const FString DefaultValue =  Factory->GetArgumentDefaultValue(Pin->GetFName(), RigVMTypeUtils::TypeIndex::WildCardArray);
					if(!DefaultValue.IsEmpty())
					{
						SetPinDefaultValue(Pin, DefaultValue, true, false, false);
					}
				}
			}
		}
	}

	AddExecutePins(ERigVMPinDirection::Output);
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
		if(RigVMCore::SupportsUObjects())
		{
			InOutPin->CPPTypeObject = ObjectProperty->PropertyClass;
		}
		else
		{
			ReportErrorf(TEXT("Unsupported type '%s' for pin."), *ObjectProperty->PropertyClass->GetName(), *InOutPin->GetName());
			InOutPin->CPPType = FString();
			InOutPin->CPPTypeObject = nullptr;
		}
	}
	else if (FInterfaceProperty* InterfaceProperty = CastField<FInterfaceProperty>(PropertyForType))
	{
		if (RigVMCore::SupportsUInterfaces())
		{
			InOutPin->CPPTypeObject = InterfaceProperty->InterfaceClass;
		}
		else
		{
			ReportErrorf(TEXT("Unsupported type '%s' for pin."), *InterfaceProperty->InterfaceClass->GetName(), *InOutPin->GetName());
			InOutPin->CPPType = FString();
			InOutPin->CPPTypeObject = nullptr;
		}
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

	InOutPin->CPPType = RigVMTypeUtils::PostProcessCPPType(InOutPin->CPPType, InOutPin->GetCPPTypeObject());

	if(InOutPin->IsExecuteContext() && InOutPin->CPPTypeObject != FRigVMExecuteContext::StaticStruct())
	{
		MakeExecutePin(InOutPin);
	}
}

void URigVMController::ConfigurePinFromPin(URigVMPin* InOutPin, URigVMPin* InPin, bool bCopyDisplayName)
{
	// it is important we copy things that define the identity of the pin
	// things that defines the state of the pin is copied during GetPinState()
	// though addmittedly these two functions have overlaps currently
	InOutPin->bIsConstant = InPin->bIsConstant;
	InOutPin->Direction = InPin->Direction;
	InOutPin->CPPType = InPin->CPPType;
	InOutPin->CPPTypeObjectPath = InPin->CPPTypeObjectPath;
	InOutPin->CPPTypeObject = InPin->CPPTypeObject;
	InOutPin->DefaultValue = InPin->DefaultValue;
	InOutPin->bIsDynamicArray = InPin->bIsDynamicArray;
	if(bCopyDisplayName)
	{
		InOutPin->SetDisplayName(InPin->GetDisplayName());
	}

	if(InOutPin->IsExecuteContext() && InOutPin->CPPTypeObject != FRigVMExecuteContext::StaticStruct())
	{
		MakeExecutePin(InOutPin);
	}
}

void URigVMController::ConfigurePinFromArgument(URigVMPin* InOutPin, const FRigVMGraphFunctionArgument& InArgument, bool bCopyDisplayName)
{
	// it is important we copy things that define the identity of the pin
	// things that defines the state of the pin is copied during GetPinState()
	// though addmittedly these two functions have overlaps currently
	InOutPin->bIsConstant = InArgument.bIsConst;
	InOutPin->Direction = InArgument.Direction;
	InOutPin->CPPType = InArgument.CPPType.ToString();
	InOutPin->CPPTypeObjectPath = *InArgument.CPPTypeObject.ToSoftObjectPath().ToString();
	InOutPin->CPPTypeObject = InArgument.CPPTypeObject.Get();
	InOutPin->DefaultValue = InArgument.DefaultValue;
	InOutPin->bIsDynamicArray = InArgument.bIsArray;
	if(bCopyDisplayName)
	{
		InOutPin->SetDisplayName(InArgument.DisplayName);
	}

	if(InOutPin->IsExecuteContext() && InOutPin->CPPTypeObject != FRigVMExecuteContext::StaticStruct())
	{
		MakeExecutePin(InOutPin);
	}
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
	if(Struct->IsChildOf(RigVMTypeUtils::GetWildCardCPPTypeObject()))
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
	if (InPin->IsArray())
	{
		return InPin->GetDirection() == ERigVMPinDirection::Input ||
			InPin->GetDirection() == ERigVMPinDirection::IO ||
			InPin->IsFixedSizeArray();
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

		UStruct* Struct = UnitNode->GetScriptStruct();
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

int32 URigVMController::DetachLinksFromPinObjects(const TArray<URigVMLink*>* InLinks)
{
	URigVMGraph* Graph = GetGraph();
	check(Graph);

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
				TGuardValue<bool> GuardEditGraph(CollapseNode->ContainedGraph->bEditable, true);
				DetachLinksFromPinObjects(InLinks);
			}
		}
	}

	return Links.Num();
}

int32 URigVMController::ReattachLinksToPinObjects(bool bFollowCoreRedirectors, const TArray<URigVMLink*>* InLinks, bool bSetupOrphanedPins, bool bAllowNonArgumentLinks, bool bRecursive)
{
	URigVMGraph* Graph = GetGraph();
	check(Graph);
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
				NewLinks.Add(Link);
				continue;
			}

			if (!URigVMPin::CanLink(SourcePin, TargetPin, nullptr, nullptr, ERigVMPinDirection::IO, bAllowNonArgumentLinks))
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
					ReportWarningf(TEXT("Unable to re-create link %s"), *URigVMLink::GetPinPathRepresentation(Link->SourcePinPath, Link->TargetPinPath));
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
			ReportWarningf(TEXT("Unable to re-create link %s"), *URigVMLink::GetPinPathRepresentation(Link->SourcePinPath, Link->TargetPinPath));
			if (TargetPin != nullptr)
			{
				TargetPin->Links.Remove(Link);
			}
			continue;
		}
		if (TargetPin == nullptr)
		{
			ReportWarningf(TEXT("Unable to re-create link %s"), *URigVMLink::GetPinPathRepresentation(Link->SourcePinPath, Link->TargetPinPath));
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
		if(Graph->Links.Num() != NewLinks.Num())
		{
			ReportWarningf(TEXT("Number of links changed during ReattachLinksToPinObjects in graph %s in project %s"), *Graph->GetPathName(), *GetPackage()->GetPathName());
		}
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

	if (bRecursive && InLinks == nullptr)
	{
		for (URigVMNode* Node : Graph->Nodes)
		{
			if (URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(Node))
			{
				FRigVMControllerGraphGuard GraphGuard(this, CollapseNode->GetContainedGraph(), false);
				TGuardValue<bool> GuardEditGraph(CollapseNode->ContainedGraph->bEditable, true);
				ReattachLinksToPinObjects(bFollowCoreRedirectors, nullptr, bSetupOrphanedPins, bAllowNonArgumentLinks, bRecursive);
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
	if(InOwningStruct == nullptr) // potentially a template node
	{
		return false;
	}
	
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

void URigVMController::RepopulatePinsOnNode(URigVMNode* InNode, bool bFollowCoreRedirectors, bool bSetupOrphanedPins, bool bDetachAndReattachLinks)
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
	URigVMDispatchNode* DispatchNode = Cast<URigVMDispatchNode>(InNode);

	FScopeLock Lock(&PinPathCoreRedirectorsLock);
	const FRigVMRegistry& Registry = FRigVMRegistry::Get();

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	// step 0/3: update execute pins
	for(URigVMPin* Pin : InNode->Pins)
	{
		if(Pin->IsExecuteContext())
		{
			MakeExecutePin(Pin);
		}
	}

	bool bSetupOrphanPinsForThisNode = bSetupOrphanedPins;
	if(CollapseNode)
	{
		if(CollapseNode->GetOuter()->IsA<URigVMFunctionLibrary>())
		{
			bSetupOrphanPinsForThisNode = false;
		}
	}

	const FPinInfoArray PreviousPinInfos(InNode);
	const uint32 PreviousPinHash = GetTypeHash(PreviousPinInfos);
	FPinInfoArray NewPinInfos;

	// step 2/3: clear pins on the node and repopulate the node with new pins
	if (UnitNode != nullptr)
	{
		UScriptStruct* ScriptStruct = UnitNode->GetScriptStruct();
		if (ScriptStruct == nullptr)
		{
			// this may be an unresolved template node
			// in that case there's nothing we can do here
			return;
		}

		FString NodeColorMetadata;
		ScriptStruct->GetStringMetaDataHierarchical(*URigVMNode::NodeColorName, &NodeColorMetadata);
		if (!NodeColorMetadata.IsEmpty())
		{
			UnitNode->NodeColor = GetColorFromMetadata(NodeColorMetadata);
		}

		const TSharedPtr<FStructOnScope> DefaultValueContent = UnitNode->ConstructStructInstance(false);
		NewPinInfos.AddPins(ScriptStruct, this, ERigVMPinDirection::Invalid, INDEX_NONE, DefaultValueContent->GetStructMemory());
	}
	else if (DispatchNode)
	{
		TMap<FName, TRigVMTypeIndex> PinTypeMap;
		for (const URigVMPin* Pin : DispatchNode->Pins)
		{
			PinTypeMap.Add(Pin->GetFName(), Pin->GetTypeIndex());
		}
		
		const FRigVMTemplate* Template = DispatchNode->GetTemplate();

		FRigVMDispatchContext DispatchContext = DispatchNode->GetDispatchContext();
		auto AddExecutePins = [Template, DispatchNode, &Registry, &DispatchContext, &NewPinInfos, &PreviousPinInfos, this](ERigVMPinDirection InPinDirection)
		{
			for (int32 ArgIndex = 0; ArgIndex < Template->NumExecuteArguments(DispatchContext); ArgIndex++)
			{
				const FRigVMExecuteArgument* Arg = Template->GetExecuteArgument(ArgIndex, DispatchContext);
				const FRigVMTemplateArgumentType Type = Registry.GetType(Arg->TypeIndex);
				const TRigVMTypeIndex TypeIndex = Registry.GetTypeIndex(Type);

				FString DefaultValue;
				if(Registry.IsArrayType(Arg->TypeIndex))
				{
					if(const FRigVMDispatchFactory* Factory = DispatchNode->GetFactory())
					{
						DefaultValue = Factory->GetArgumentDefaultValue(Arg->Name, Arg->TypeIndex);
					}
				}

				(void)NewPinInfos.AddPin(this, INDEX_NONE, Arg->Name, Arg->Direction, TypeIndex, DefaultValue, nullptr, &PreviousPinInfos);
			}
		};

		AddExecutePins(ERigVMPinDirection::IO);
		AddExecutePins(ERigVMPinDirection::Input);
		
		for (int32 ArgIndex = 0; ArgIndex < Template->NumArguments(); ArgIndex++)
		{
			const FRigVMTemplateArgument* Arg = Template->GetArgument(ArgIndex);

			TRigVMTypeIndex TypeIndex = INDEX_NONE;
			if(const TRigVMTypeIndex* ExistingTypeIndex = PinTypeMap.Find(Arg->GetName()))
			{
				TypeIndex = *ExistingTypeIndex;
				if(!Arg->SupportsTypeIndex(TypeIndex))
				{
					TypeIndex = INDEX_NONE;
				}
			}

			if(TypeIndex == INDEX_NONE)
			{
				if(Arg->IsSingleton())
				{
					TypeIndex = Arg->GetSupportedTypeIndices()[0];
				}
				else if(Arg->GetArrayType() == FRigVMTemplateArgument::EArrayType_ArrayValue)
				{
					TypeIndex = RigVMTypeUtils::TypeIndex::WildCardArray;
				}
				else
				{
					TypeIndex = RigVMTypeUtils::TypeIndex::WildCard;
				}
			}

			FString DefaultValue;
			UScriptStruct* ArgumentScriptStruct = nullptr;
			const uint8* DefaultValueMemory = nullptr;

			if(const URigVMPin* ArgumentPin = DispatchNode->FindPin(Arg->Name.ToString()))
			{
				DefaultValue = ArgumentPin->GetDefaultValue();
				ArgumentScriptStruct = Cast<UScriptStruct>(ArgumentPin->GetCPPTypeObject());
			}
			else if(const FRigVMDispatchFactory* Factory = DispatchNode->GetFactory())
			{
				if(Arg->IsSingleton())
				{
					DefaultValue = Factory->GetArgumentDefaultValue(Arg->Name, Arg->GetTypeIndices()[0]);
					const FRigVMTemplateArgumentType& Type = Registry.GetType(Arg->GetTypeIndices()[0]);
					ArgumentScriptStruct = Cast<UScriptStruct>(Type.CPPTypeObject);
				}
			}

			FStructOnScope DefaultValueMemoryScope; // has to be in this scope so that DefaultValueMemory is valid
			if(ArgumentScriptStruct && !DefaultValue.IsEmpty())
			{
				DefaultValueMemoryScope = FStructOnScope(ArgumentScriptStruct);

				FRigVMPinDefaultValueImportErrorContext ErrorPipe;
				ArgumentScriptStruct->ImportText(*DefaultValue, DefaultValueMemoryScope.GetStructMemory(), nullptr, PPF_None, &ErrorPipe, FString());
				DefaultValueMemory = DefaultValueMemoryScope.GetStructMemory();
			}
			
			(void)NewPinInfos.AddPin(this, INDEX_NONE, Arg->Name, Arg->GetDirection(), TypeIndex, DefaultValue, DefaultValueMemory, &PreviousPinInfos);
		}

		AddExecutePins(ERigVMPinDirection::Output);
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

		if(VariableNode)
		{
			// this includes local variables for validation
			const TArray<FRigVMExternalVariable> ExternalVariables = GetAllVariables(false);
			const FRigVMGraphVariableDescription VariableDescription = VariableNode->GetVariableDescription();
			const FRigVMExternalVariable CurrentExternalVariable = VariableDescription.ToExternalVariable();

			FRigVMExternalVariable Variable;
			if (VariableNode->IsInputArgument())
			{
				if (URigVMFunctionEntryNode* GraphEntryNode = Graph->GetEntryNode())
				{
					if (URigVMPin* EntryPin = GraphEntryNode->FindPin(VariableDescription.Name.ToString()))
					{
						Variable = RigVMTypeUtils::ExternalVariableFromCPPType(VariableDescription.Name, EntryPin->GetCPPType(), EntryPin->GetCPPTypeObject());
					}
				}
			}
			else
			{				
				for(const FRigVMExternalVariable& ExternalVariable : ExternalVariables)
				{
					if(ExternalVariable.Name == CurrentExternalVariable.Name)
					{
						Variable = ExternalVariable;
						break;
					}
				}
			}

			if (Variable.IsValid(true))
			{
				if(Variable.TypeName != CurrentExternalVariable.TypeName ||
				   Variable.TypeObject != CurrentExternalVariable.TypeObject ||
				   Variable.bIsArray != CurrentExternalVariable.bIsArray)
				{
					FString CPPType;
					UObject* CPPTypeObject;
				
					if(RigVMTypeUtils::CPPTypeFromExternalVariable(Variable, CPPType, &CPPTypeObject))
					{
						RefreshVariableNode(VariableNode->GetFName(), Variable.Name, CPPType, Variable.TypeObject, false, bSetupOrphanPinsForThisNode);
					}
					else
					{
						ReportErrorf(
							TEXT("Control Rig '%s', Type of Variable '%s' cannot be resolved."),
							*InNode->GetOutermost()->GetPathName(),
							*Variable.Name.ToString()
						);
					}
				}
			}
			else
			{
				ReportWarningf(
					TEXT("Control Rig '%s', Variable '%s' not found."),
					*InNode->GetOutermost()->GetPathName(),
					*CurrentExternalVariable.Name.ToString()
				);
			}
		}

		NewPinInfos = FPinInfoArray(InNode, this, &PreviousPinInfos);
	}
	else if (EntryNode || ReturnNode)
	{
		if (URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(InNode->GetGraph()->GetOuter()))
		{
			bool bIsEntryNode = EntryNode != nullptr;

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

				ERigVMPinDirection Direction = bIsEntryNode ? ERigVMPinDirection::Output : ERigVMPinDirection::Input;  
				(void)NewPinInfos.AddPin(LibraryPin, INDEX_NONE, Direction);
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
		NewPinInfos = FPinInfoArray(InNode, this, &PreviousPinInfos);
	}
	else if (FunctionRefNode)
	{
		const FRigVMGraphFunctionHeader& FunctionHeader = FunctionRefNode->GetReferencedFunctionHeader();
		if(FunctionHeader.IsValid())
		{
			NewPinInfos = FPinInfoArray(FunctionHeader, this, &PreviousPinInfos);
		}
		// if we can't find referenced node anymore
		// let's keep all pins
		else
		{
			NewPinInfos = FPinInfoArray(FunctionRefNode, this, &PreviousPinInfos);
		}
	}
	else
	{
		return;
	}

	auto RecursivelyRepopulatePinsOnCollapseNode = [this, bFollowCoreRedirectors, bSetupOrphanedPins]
		(URigVMCollapseNode* InCollapseNode)
		{
			if(InCollapseNode)
			{
				FRigVMControllerGraphGuard GraphGuard(this, InCollapseNode->GetContainedGraph(), false);
				TGuardValue<bool> GuardEditGraph(InCollapseNode->ContainedGraph->bEditable, true);
				// need to get a copy of the node array since the following function could remove nodes from the graph
				// we don't want to remove elements from the array we are iterating over.
				TArray<URigVMNode*> ContainedNodes = InCollapseNode->GetContainedNodes();
				for (URigVMNode* ContainedNode : ContainedNodes)
				{
					RepopulatePinsOnNode(ContainedNode, bFollowCoreRedirectors, bSetupOrphanedPins);
				}
			}
		};

	// if the nodes match in structure - nothing to do here
	if(GetTypeHash(NewPinInfos) == PreviousPinHash)
	{
		// we at least need to recurse into a collapse node
		RecursivelyRepopulatePinsOnCollapseNode(CollapseNode);
		return;
	}
	
#if UE_RIGVMCONTROLLER_VERBOSE_REPOPULATE
	UE_LOG(LogRigVMDeveloper, Display, TEXT("Repopulating pins on node %s"), *InNode->GetPathName());
#endif
	
	bool bRequireDetachLinks = false;
	bool bRequirePinStates = false;

	TArray<int32> NewPinsToAdd, PreviousPinsToRemove, PreviousPinsToOrphan, PreviousPinsToUpdate;
	for(int32 Index = 0; Index < PreviousPinInfos.Num(); Index++)
	{
		const FString PinPath = PreviousPinInfos.GetPinPath(Index);
		const int32 NewIndex = NewPinInfos.GetIndexFromPinPath(PinPath);
		
		if(NewIndex == INDEX_NONE)
		{
			const int32 RootIndex = PreviousPinInfos.GetRootIndex(Index);
			if(PreviousPinInfos[Index].Direction != ERigVMPinDirection::Hidden)
			{
				if(URigVMPin* Pin = InNode->FindPin(PinPath))
				{
					if(!Pin->GetLinks().IsEmpty())
					{
						bRequireDetachLinks = true;
						bRequirePinStates = true;
					}
					
					if(!PreviousPinsToOrphan.Contains(RootIndex))
					{
						URigVMPin* RootPin = Pin->GetRootPin();

						bool bPinShouldBeOrphaned = bSetupOrphanPinsForThisNode; 
						if(!bPinShouldBeOrphaned)
						{
							if(RootPin->GetSourceLinks(true).Num() > 0 ||
								RootPin->GetTargetLinks(true).Num() > 0)
							{
								bPinShouldBeOrphaned = true;
							}
						}
		
						if(bPinShouldBeOrphaned)
						{
							PreviousPinsToOrphan.Add(RootIndex);
							
							bRequireDetachLinks = true;
							bRequirePinStates = true;
#if UE_RIGVMCONTROLLER_VERBOSE_REPOPULATE
							UE_LOG(LogRigVMDeveloper, Display, TEXT("Previously existing pin '%s' needs to be orphaned."), *RootPin->GetPinPath());
#endif
						}
					}
				}
			}

			if(!PreviousPinsToOrphan.Contains(RootIndex))
			{
				PreviousPinsToRemove.Add(Index);
#if UE_RIGVMCONTROLLER_VERBOSE_REPOPULATE
				UE_LOG(LogRigVMDeveloper, Display, TEXT("Previously existing pin '%s' is now obsolete."), *PinPath);
#endif
			}
		}
		else if(GetTypeHash(PreviousPinInfos[Index]) != GetTypeHash(NewPinInfos[NewIndex]))
		{
			const bool bTypesDiffer = !Registry.CanMatchTypes(PreviousPinInfos[Index].TypeIndex, NewPinInfos[NewIndex].TypeIndex, true);
			if(PreviousPinInfos[Index].Direction != NewPinInfos[NewIndex].Direction)
			{
				bRequireDetachLinks = true;
			}
			else if(PreviousPinInfos[Index].Direction != ERigVMPinDirection::Hidden)
			{
				bRequireDetachLinks |= bTypesDiffer;
			}

			if(Registry.CanMatchTypes(PreviousPinInfos[Index].TypeIndex, NewPinInfos[NewIndex].TypeIndex, true))
			{
				PreviousPinsToUpdate.Add(Index);
			}
			else
            {
            	PreviousPinsToRemove.Add(Index);
            	NewPinsToAdd.Add(NewIndex);
            }
			
#if UE_RIGVMCONTROLLER_VERBOSE_REPOPULATE
			const FName& PreviousCPPType = Registry.GetType(PreviousPinInfos[Index].TypeIndex).CPPType;
			const FName& NewCPPType = Registry.GetType(NewPinInfos[NewIndex].TypeIndex).CPPType;

			const FString PreviousDirection = StaticEnum<ERigVMPinDirection>()->GetDisplayNameTextByValue((int64)PreviousPinInfos[Index].Direction).ToString();
			const FString NewDirection = StaticEnum<ERigVMPinDirection>()->GetDisplayNameTextByValue((int64)NewPinInfos[NewIndex].Direction).ToString();

			UE_LOG(LogRigVMDeveloper, Display,
				TEXT("Previous pin '%s' (Index %d, %s, %s) differs with new pin (Index %d, %s, %s)."),
				*PinPath,
				Index,
				*PreviousCPPType.ToString(),
				*PreviousDirection,
				NewIndex,
				*NewCPPType.ToString(),
				*NewDirection
			);
#endif
		}
	}
	for(int32 Index = 0; Index < NewPinInfos.Num(); Index++)
	{
		const FString PinPath = NewPinInfos.GetPinPath(Index);
		const int32 PreviousIndex = PreviousPinInfos.GetIndexFromPinPath(PinPath);
		if(PreviousIndex == INDEX_NONE)
		{
			NewPinsToAdd.Add(Index);
#if UE_RIGVMCONTROLLER_VERBOSE_REPOPULATE
			UE_LOG(LogRigVMDeveloper, Display, TEXT("Newly required pin '%s' needs to be added."), *PinPath);
#endif
		}
		else
		{
			// the previous pin exists - but it has been orphaned
			const int32 PreviousRootIndex = PreviousPinInfos.GetRootIndex(PreviousIndex);
			if(PreviousPinsToOrphan.Contains(PreviousRootIndex))
			{
				NewPinsToAdd.Add(Index);

#if UE_RIGVMCONTROLLER_VERBOSE_REPOPULATE
				UE_LOG(LogRigVMDeveloper, Display, TEXT("Orphaned pin '%s' needs to be re-added."), *PinPath);
#endif
			}
		}
	}

	auto CreatePinFromPinInfo = [this, &Registry, &PreviousPinInfos](const FPinInfo& InPinInfo, const FString& InPinPath, UObject* InOuter) -> URigVMPin*
	{
		check(InOuter);
		URigVMPin* Pin = NewObject<URigVMPin>(InOuter, InPinInfo.Name);
		if(InPinInfo.Property)
		{
			ConfigurePinFromProperty(InPinInfo.Property, Pin, InPinInfo.Direction);
		}
		else
		{
			const FRigVMTemplateArgumentType& Type = Registry.GetType(InPinInfo.TypeIndex);
			Pin->CPPType = Type.CPPType.ToString();
			Pin->CPPTypeObject = Type.CPPTypeObject;
			if(Pin->CPPTypeObject)
			{
				Pin->CPPTypeObjectPath = *Pin->CPPTypeObject->GetPathName();
			}
			if(Registry.IsExecuteType(InPinInfo.TypeIndex))
			{
				MakeExecutePin(Pin);
			}
				
			Pin->Direction = InPinInfo.Direction;
			Pin->DisplayName = InPinInfo.DisplayName.IsEmpty() ? NAME_None : FName(*InPinInfo.DisplayName);
			Pin->bIsConstant = InPinInfo.bIsConstant;
			Pin->bIsDynamicArray = InPinInfo.bIsDynamicArray;
			Pin->CustomWidgetName = InPinInfo.CustomWidgetName.IsEmpty() ? NAME_None : FName(*InPinInfo.CustomWidgetName);
		}

		Pin->bIsExpanded = InPinInfo.bIsExpanded;
		Pin->DefaultValue = InPinInfo.DefaultValue;

		// reuse expansion state and default value
		if(const FPinInfo* PreviousPin = PreviousPinInfos.GetPinFromPinPath(InPinPath))
		{
			if(PreviousPin->TypeIndex == InPinInfo.TypeIndex)
			{
				Pin->bIsExpanded = PreviousPin->bIsExpanded;
				Pin->DefaultValue = PreviousPin->DefaultValue;
			}
		}

		if (URigVMPin* ParentPin = Cast<URigVMPin>(InOuter))
		{
			AddSubPin(ParentPin, Pin);
		}
		else
		{
			AddNodePin(CastChecked<URigVMNode>(InOuter), Pin);
		}

		Notify(ERigVMGraphNotifType::PinAdded, Pin);

		return Pin;
	};

	// step 1/3: keep a record of the current state of the node's pins
	TMap<FString, FString> RedirectedPinPaths;
	if (bFollowCoreRedirectors)
	{
		RedirectedPinPaths = GetRedirectedPinPaths(InNode);
	}

	// also in case this node is part of an injection
	FName InjectionInputPinName = NAME_None;
	FName InjectionOutputPinName = NAME_None;
	if (URigVMInjectionInfo* InjectionInfo = InNode->GetInjectionInfo())
	{
		InjectionInputPinName = InjectionInfo->InputPin ? InjectionInfo->InputPin->GetFName() : NAME_None;
		InjectionOutputPinName = InjectionInfo->OutputPin ? InjectionInfo->OutputPin->GetFName() : NAME_None;
	}

	TMap<FString, FPinState> PinStates;
	TArray<URigVMLink*> DetachedLinks;

	if(bRequirePinStates)
	{
		PinStates = GetPinStates(InNode);
	}

	if(bDetachAndReattachLinks && bRequireDetachLinks)
	{
#if UE_RIGVMCONTROLLER_VERBOSE_REPOPULATE
		UE_LOG(LogRigVMDeveloper, Display, TEXT("Detaching links of node %s."), *InNode->GetPathName());
#endif
		DetachedLinks = InNode->GetLinks();
		DetachLinksFromPinObjects(&DetachedLinks);
	}

	// we can do a simpler version of the update and simply
	// add new pins, remove obsolete pins and change types as needed 
#if UE_RIGVMCONTROLLER_VERBOSE_REPOPULATE
	UE_LOG(LogRigVMDeveloper, Display, TEXT("Performing fast update of node %s ..."), *InNode->GetPathName());
#endif

	// orphan pins
	for(int32 Index = 0; Index < PreviousPinsToOrphan.Num(); Index++)
	{
		const FString& PinPath = PreviousPinInfos.GetPinPath(PreviousPinsToOrphan[Index]);
		if(URigVMPin* Pin = InNode->FindPin(PinPath))
		{
#if UE_RIGVMCONTROLLER_VERBOSE_REPOPULATE
			UE_LOG(LogRigVMDeveloper, Display, TEXT("Orphaning pin '%s'."), *PinPath);
#endif
			check(Pin->IsRootPin());

			const FString OrphanedName = FString::Printf(TEXT("%s%s"), *URigVMPin::OrphanPinPrefix, *Pin->GetName());
			if(InNode->FindPin(OrphanedName) == nullptr)
			{
				Pin->DisplayName = Pin->GetFName();
				RenameObject(Pin, *OrphanedName, nullptr);
				InNode->Pins.Remove(Pin);

				Notify(ERigVMGraphNotifType::PinRemoved, Pin);
				InNode->OrphanedPins.Add(Pin);
				Notify(ERigVMGraphNotifType::PinAdded, Pin);
			}
			else
			{
				RemovePin(Pin, false);
			}
		}
	}

	// remove obsolete pins
	for(int32 Index = PreviousPinsToRemove.Num() - 1; Index >= 0; Index--)
	{
		const FString& PinPath = PreviousPinInfos.GetPinPath(PreviousPinsToRemove[Index]);
		if(URigVMPin* Pin = InNode->FindPin(PinPath))
		{
#if UE_RIGVMCONTROLLER_VERBOSE_REPOPULATE
			UE_LOG(LogRigVMDeveloper, Display, TEXT("Removing pin '%s'."), *PinPath);
#endif
			RemovePin(Pin, false);
		}
	}
	// add missing pins
	for(int32 Index = 0; Index < NewPinsToAdd.Num(); Index++)
	{
		const FString& PinPath = NewPinInfos.GetPinPath(NewPinsToAdd[Index]);
		FString ParentPinPath, PinName;
		UObject* OuterForPin = InNode;
		if(URigVMPin::SplitPinPathAtEnd(PinPath, ParentPinPath, PinName))
		{
			OuterForPin = InNode->FindPin(ParentPinPath);
		}
		
		(void)CreatePinFromPinInfo(NewPinInfos[NewPinsToAdd[Index]], PinPath, OuterForPin);
#if UE_RIGVMCONTROLLER_VERBOSE_REPOPULATE
		UE_LOG(LogRigVMDeveloper, Display, TEXT("Adding new pin '%s'."), *PinPath);
#endif
	}
	// update existing pins
	for(int32 Index = 0; Index < PreviousPinsToUpdate.Num(); Index++)
	{
		const FString& PinPath = PreviousPinInfos.GetPinPath(PreviousPinsToUpdate[Index]);
		const FPinInfo* NewPinInfo = NewPinInfos.GetPinFromPinPath(PinPath);
		check(NewPinInfo);
		
		if(URigVMPin* Pin = InNode->FindPin(PinPath))
		{
			if(Pin->IsExecuteContext())
			{
				MakeExecutePin(Pin);
			}
			
			if(Pin->GetTypeIndex() != NewPinInfo->TypeIndex)
			{
				// we expect these changes to only apply to float and double pins.
				check((NewPinInfo->TypeIndex == RigVMTypeUtils::TypeIndex::Float) ||
					(NewPinInfo->TypeIndex == RigVMTypeUtils::TypeIndex::FloatArray) ||
					(NewPinInfo->TypeIndex == RigVMTypeUtils::TypeIndex::Double) ||
					(NewPinInfo->TypeIndex == RigVMTypeUtils::TypeIndex::DoubleArray));

				const FRigVMTemplateArgumentType& NewType = Registry.GetType(NewPinInfo->TypeIndex);
#if UE_RIGVMCONTROLLER_VERBOSE_REPOPULATE
				UE_LOG(LogRigVMDeveloper, Display, TEXT("Changing pin '%s' type from %s to %s."),
					*PinPath,
					*Pin->CPPType,
					*NewType.CPPType.ToString()
				);
#endif
				Pin->CPPType = NewType.CPPType.ToString();
				Pin->CPPTypeObject = NewType.CPPTypeObject;
				check(Pin->CPPTypeObject == nullptr);
				Pin->CPPTypeObjectPath = NAME_None;
				Pin->LastKnownTypeIndex = NewPinInfo->TypeIndex;

				Notify(ERigVMGraphNotifType::PinTypeChanged, Pin);
			}
		}
	}

	// create a map representing the order of expected pins
	TMap<FString, TArray<FName>> PinOrder;
	for(int32 Index = 0; Index < NewPinInfos.Num(); Index++)
	{
		const FPinInfo& NewPin = NewPinInfos[Index];
		FString ParentPinPath;
		if(NewPin.ParentIndex != INDEX_NONE)
		{
			ParentPinPath = NewPinInfos.GetPinPath(NewPin.ParentIndex);
			if(NewPinInfos[NewPin.ParentIndex].bIsArray)
			{
				continue;
			}
		}

		TArray<FName>& SubPinOrder = PinOrder.FindOrAdd(ParentPinPath);
		SubPinOrder.Add(NewPin.Name);
	}

	auto SortPinArray = [this](TArray<TObjectPtr<URigVMPin>>& Pins, const TArray<FName>* PinOrder)
	{
		if(PinOrder == nullptr)
		{
			return;
		}

		if(Pins.Num() < 2)
		{
			return;
		}

		const TArray<TObjectPtr<URigVMPin>> PreviousPins = Pins;

		if(Pins[0]->IsArrayElement())
		{
			Algo::Sort(Pins, [PinOrder](const TObjectPtr<URigVMPin>& A, const TObjectPtr<URigVMPin>& B) -> bool
			{
				return A->GetFName().Compare(B->GetFName()) < 0;
			});
		}
		else
		{
			Algo::Sort(Pins, [PinOrder](const TObjectPtr<URigVMPin>& A, const TObjectPtr<URigVMPin>& B) -> bool
			{
				const int32 IndexA = PinOrder->Find(A->GetFName());
				const int32 IndexB = PinOrder->Find(B->GetFName());
				return IndexA < IndexB;
			});
		}

		for(int32 Index=0;Index<Pins.Num();Index++)
		{
			if(PreviousPins[Index] != Pins[Index])
			{
				Notify(ERigVMGraphNotifType::PinIndexChanged, Pins[Index]);
#if UE_RIGVMCONTROLLER_VERBOSE_REPOPULATE
				UE_LOG(LogRigVMDeveloper, Display, TEXT("Pin '%s' changed index from %d to %d."),
					*Pins[Index]->GetPinPath(),
					PreviousPins.Find(Pins[Index]),
					Index
				);
#endif
			}
		}
	};

	SortPinArray(InNode->Pins, PinOrder.Find(FString()));
	for(URigVMPin* Pin : InNode->Pins)
	{
		SortPinArray(Pin->SubPins, PinOrder.Find(Pin->GetPinPath()));
	}
	
	if(DispatchNode)
	{
		ResolveTemplateNodeMetaData(DispatchNode, false);
	}
	else if(CollapseNode)
	{
		if (!CollapseNode->GetOuter()->IsA<URigVMFunctionLibrary>())
		{
			// no need to notify since the function library graph is invisible anyway
			RemoveUnusedOrphanedPins(CollapseNode);
		}

		RecursivelyRepopulatePinsOnCollapseNode(CollapseNode);
	}
	else if (FunctionRefNode)
	{
		// we want to make sure notify the graph of a potential name change
		// when repopulating the function ref node
		Notify(ERigVMGraphNotifType::NodeRenamed, FunctionRefNode);
	}

	if(!PinStates.IsEmpty())
	{
#if UE_RIGVMCONTROLLER_VERBOSE_REPOPULATE
		UE_LOG(LogRigVMDeveloper, Display, TEXT("Reapplying pin-states of node %s..."), *InNode->GetPathName());
#endif
		ApplyPinStates(InNode, PinStates, RedirectedPinPaths);
	}

	if(!DetachedLinks.IsEmpty())
	{
#if UE_RIGVMCONTROLLER_VERBOSE_REPOPULATE
		UE_LOG(LogRigVMDeveloper, Display, TEXT("Reattaching links of node %s..."), *InNode->GetPathName());
#endif
		ReattachLinksToPinObjects(bFollowCoreRedirectors, &DetachedLinks, bSetupOrphanPinsForThisNode);
	}

	if (URigVMInjectionInfo* InjectionInfo = InNode->GetInjectionInfo())
	{
		InjectionInfo->InputPin = InNode->FindPin(InjectionInputPinName.ToString());
		InjectionInfo->OutputPin = InNode->FindPin(InjectionOutputPinName.ToString());
	}

#if UE_RIGVMCONTROLLER_VERBOSE_REPOPULATE
	UE_LOG(LogRigVMDeveloper, Display, TEXT("Repopulate of node %s is completed.\n"), *InNode->GetPathName());
#endif
}

void URigVMController::RemovePinsDuringRepopulate(URigVMNode* InNode, TArray<URigVMPin*>& InPins, bool bSetupOrphanedPins)
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

					if(!bSuspendNotifications)
					{
						Notify(ERigVMGraphNotifType::PinRemoved, RootPin);
					}

					InNode->OrphanedPins.Add(RootPin);

					if(!bSuspendNotifications)
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

					if(!bSuspendNotifications)
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
			RemovePin(Pin, false);
		}
	}
	InPins.Reset();
}

bool URigVMController::RemoveUnusedOrphanedPins(URigVMNode* InNode)
{
	if(!InNode->HasOrphanedPins())
	{
		return true;
	}

	TArray<URigVMPin*> OrphanedPins = InNode->OrphanedPins; 
	TArray<URigVMPin*> RemainingOrphanPins;
	for(int32 PinIndex=0; PinIndex < OrphanedPins.Num(); PinIndex++)
	{
		URigVMPin* OrphanedPin = OrphanedPins[PinIndex];

		const int32 NumSourceLinks = OrphanedPin->GetSourceLinks(true).Num(); 
		const int32 NumTargetLinks = OrphanedPin->GetTargetLinks(true).Num();

		if(NumSourceLinks + NumTargetLinks == 0)
		{
			RemovePin(OrphanedPin, false);
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

URigVMController::FPinState URigVMController::GetPinState(URigVMPin* InPin, bool bStoreWeakInjectionInfos) const
{
	FPinState State;
	State.Direction = InPin->GetDirection();
	State.CPPType = InPin->GetCPPType();
	State.CPPTypeObject = InPin->GetCPPTypeObject();
	State.DefaultValue = InPin->GetDefaultValue();
	State.bIsExpanded = InPin->IsExpanded();
	State.InjectionInfos = InPin->GetInjectedNodes();

	if(bStoreWeakInjectionInfos)
	{
		for(URigVMInjectionInfo* InjectionInfo : State.InjectionInfos)
		{
			State.WeakInjectionInfos.Add(InjectionInfo->GetWeakInfo());
		}
		State.InjectionInfos.Reset();
	}
	
	return State;
}

TMap<FString, URigVMController::FPinState> URigVMController::GetPinStates(URigVMNode* InNode, bool bStoreWeakInjectionInfos) const
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
		FPinState State = GetPinState(Pin, bStoreWeakInjectionInfos);
		PinStates.Add(PinPath, State);
	}

	return PinStates;
}

void URigVMController::ApplyPinState(URigVMPin* InPin, const FPinState& InPinState, bool bSetupUndoRedo)
{
	for (URigVMInjectionInfo* InjectionInfo : InPinState.InjectionInfos)
	{
		if (InjectionInfo)
		{
			RenameObject(InjectionInfo, nullptr, InPin);
			InjectionInfo->InputPin = InjectionInfo->InputPin ? InjectionInfo->Node->FindPin(InjectionInfo->InputPin->GetName()) : nullptr;
			InjectionInfo->OutputPin = InjectionInfo->OutputPin ? InjectionInfo->Node->FindPin(InjectionInfo->OutputPin->GetName()) : nullptr;
			InPin->InjectionInfos.Add(InjectionInfo);
		}
	}

	// alternatively if the injection infos are not provided as strong pointers
	// we can fall back onto the weak ptr information and try again
	if(InPinState.InjectionInfos.IsEmpty())
	{
		for (const URigVMInjectionInfo::FWeakInfo& InjectionInfo : InPinState.WeakInjectionInfos)
		{
			if(URigVMNode* FormerlyInjectedNode = InjectionInfo.Node.Get())
			{
				if (FormerlyInjectedNode->IsInjected())
				{
					URigVMInjectionInfo* Injection = Cast<URigVMInjectionInfo>(FormerlyInjectedNode->GetOuter());
					RenameObject(FormerlyInjectedNode, nullptr, InPin->GetGraph());
					DestroyObject(Injection);
				}
				if(InjectionInfo.bInjectedAsInput)
				{
					const FString OutputPinPath = URigVMPin::JoinPinPath(FormerlyInjectedNode->GetNodePath(), InjectionInfo.OutputPinName.ToString()); 
					AddLink(OutputPinPath, InPin->GetPinPath(), bSetupUndoRedo, false);
				}
				else
				{
					const FString InputPinPath = URigVMPin::JoinPinPath(FormerlyInjectedNode->GetNodePath(), InjectionInfo.InputPinName.ToString()); 
					AddLink(InPin->GetPinPath(), InputPinPath, bSetupUndoRedo, false);
				}

				if(InPin->IsRootPin())
				{
					InjectNodeIntoPin(InPin, InjectionInfo.bInjectedAsInput, InjectionInfo.InputPinName, InjectionInfo.OutputPinName, bSetupUndoRedo);
				}
			}
		}
	}

	if (!InPinState.DefaultValue.IsEmpty())
	{
		FString DefaultValue = InPinState.DefaultValue;
		PostProcessDefaultValue(InPin, DefaultValue);
		if(!DefaultValue.IsEmpty())
		{
			SetPinDefaultValue(InPin, DefaultValue, true, bSetupUndoRedo, false);
		}
	}

	SetPinExpansion(InPin, InPinState.bIsExpanded, bSetupUndoRedo);
}

void URigVMController::ApplyPinStates(URigVMNode* InNode, const TMap<FString, URigVMController::FPinState>& InPinStates, const TMap<FString, FString>& InRedirectedPinPaths, bool bSetupUndoRedo)
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
			ApplyPinState(Pin, PinState, bSetupUndoRedo);
		}
		else
		{
			for (URigVMInjectionInfo* InjectionInfo : PinState.InjectionInfos)
			{
				if(URigVMPin* OuterPin = Cast<URigVMPin>(InjectionInfo->GetOuter()))
				{
					OuterPin->InjectionInfos.Remove(InjectionInfo);
				}
				RenameObject(InjectionInfo->Node, nullptr, InNode->GetGraph());
				DestroyObject(InjectionInfo);
			}
		}
	}
}

void URigVMController::ReportInfo(const FString& InMessage) const
{
	if (URigVMGraph* Graph = GetGraph())
	{
		if (UPackage* Package = Cast<UPackage>(Graph->GetOutermost()))
		{
			UE_LOG(LogRigVMDeveloper, Display, TEXT("%s : %s"), *Package->GetPathName(), *InMessage);
			return;
		}
	}

	UE_LOG(LogRigVMDeveloper, Display, TEXT("%s"), *InMessage);
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

void URigVMController::ReportAndNotifyInfo(const FString& InMessage) const
{
	ReportWarning(InMessage);
	SendUserFacingNotification(InMessage, 0.f, nullptr, TEXT("MessageLog.Note"));
}

void URigVMController::ReportAndNotifyWarning(const FString& InMessage) const
{
	if (!bReportWarningsAndErrors)
	{
		return;
	}

	ReportWarning(InMessage);
	SendUserFacingNotification(InMessage, 0.f, nullptr, TEXT("MessageLog.Warning"));
}

void URigVMController::ReportAndNotifyError(const FString& InMessage) const
{
	if (!bReportWarningsAndErrors)
	{
		return;
	}

	ReportError(InMessage);
	SendUserFacingNotification(InMessage, 0.f, nullptr, TEXT("MessageLog.Error"));
}

void URigVMController::ReportPinTypeChange(URigVMPin* InPin, const FString& InNewCPPType)
{
	/*
	UE_LOG(LogRigVMDeveloper,
		Warning,
		TEXT("Pin '%s' is about to change type from '%s' to '%s'."),
		*InPin->GetPinPath(),
		*InPin->GetCPPType(),
		*InNewCPPType
	);
	*/
}

void URigVMController::SendUserFacingNotification(const FString& InMessage, float InDuration, const UObject* InSubject, const FName& InBrushName) const
{
#if WITH_EDITOR

	if(InDuration < SMALL_NUMBER)
	{
		InDuration = FMath::Clamp(0.1f * InMessage.Len(), 5.0f, 20.0f);
	}
	
	FNotificationInfo Info(FText::FromString(InMessage));
	Info.bUseSuccessFailIcons = true;
	Info.Image = FAppStyle::GetBrush(InBrushName);
	Info.bFireAndForget = true;
	Info.bUseThrobber = true;
	// longer message needs more time to read
	Info.FadeOutDuration = FMath::Min(InDuration, 1.f);
	Info.ExpireDuration = InDuration;

	if(InSubject)
	{
		if(const URigVMNode* Node = Cast<URigVMNode>(InSubject))
		{
			Info.HyperlinkText = FText::FromString(Node->GetNodePath());
		}
		else if(const URigVMPin* Pin = Cast<URigVMPin>(InSubject))
		{
			Info.HyperlinkText = FText::FromString(Pin->GetPinPath());
		}
		else if(const URigVMLink* Link = Cast<URigVMLink>(InSubject))
		{
			Info.HyperlinkText = FText::FromString(((URigVMLink*)Link)->GetPinPathRepresentation());
		}
		else
		{
			Info.HyperlinkText = FText::FromName(InSubject->GetFName());
		}

		Info.Hyperlink = FSimpleDelegate::CreateLambda([InSubject, this]()
		{
			if(RequestJumpToHyperlinkDelegate.IsBound())
			{
				RequestJumpToHyperlinkDelegate.Execute(InSubject);
			}
		});
	}
	
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
			FRigVMPinDefaultValueImportErrorContext ErrorPipe;
			InStruct->ImportText(*InOutDefaultValue, TempBuffer.GetData(), nullptr, PPF_None, &ErrorPipe, FString());
		}

		// in case InOutDefaultValue is not empty, it needs to be cleared
		// before ExportText() because ExportText() appends to it.
		InOutDefaultValue.Reset();

		InStruct->ExportText(InOutDefaultValue, TempBuffer.GetData(), TempBuffer.GetData(), nullptr, PPF_None, nullptr);
		InStruct->DestroyStruct(TempBuffer.GetData());
	}
}

void URigVMController::PostProcessDefaultValue(URigVMPin* Pin, FString& OutDefaultValue)
{
	static const FString NoneString = FName(NAME_None).ToString();
	static const FString QuotedNoneString = FString::Printf(TEXT("\"%s\""), *NoneString);
	if(OutDefaultValue == NoneString || OutDefaultValue == QuotedNoneString)
	{
		if(!Pin->IsStringType())
		{
			OutDefaultValue.Reset();
		}
	}
	if (Pin->IsStruct() || Pin->IsArray())
	{
		if (!OutDefaultValue.IsEmpty())
		{
			if (OutDefaultValue[0] != TCHAR('(') || OutDefaultValue[OutDefaultValue.Len()-1] != TCHAR(')'))
			{
				OutDefaultValue.Reset();
			}
		}
	}

	if (Pin->IsArray() && OutDefaultValue.IsEmpty())
	{
		OutDefaultValue = TEXT("()");
	}
	else if (Pin->IsEnum() && OutDefaultValue.IsEmpty())
	{
		int32 EnumIndex = Pin->GetEnum()->GetIndexByName(*NoneString);
		// make sure that none is a valid enum value
		if (EnumIndex != INDEX_NONE)
		{
			OutDefaultValue = NoneString;
		}
		else
		{
			// when none string is given but none is not a valid enum value,
			// it implies that the pin should use the default value of the enum
			// the value at index 0 is the best guess that can be made.
			// usually a user provided pin default is given later to override this value
			OutDefaultValue = Pin->GetEnum()->GetNameStringByIndex(0);
		}
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
		if(OutDefaultValue.IsEmpty() && Pin->GetCPPType() == RigVMTypeUtils::FNameType)
		{
			OutDefaultValue = FName(NAME_None).ToString();
		}
	}
}

void URigVMController::ResolveTemplateNodeMetaData(URigVMTemplateNode* InNode, bool bSetupUndoRedo)
{
#if WITH_EDITOR
	check(InNode);
	
	TArray<int32> FilteredPermutationIndices = InNode->GetResolvedPermutationIndices(false);
	if(InNode->IsA<URigVMUnitNode>())
	{
		const FLinearColor PreviousColor = InNode->NodeColor;
		InNode->NodeColor = InNode->GetTemplate()->GetColor(FilteredPermutationIndices);
		if(!InNode->NodeColor.Equals(PreviousColor, 0.01f))
		{
			Notify(ERigVMGraphNotifType::NodeColorChanged, InNode);
		}
	}
#endif

	for(URigVMPin* Pin : InNode->GetPins())
	{
		const FName DisplayName = InNode->GetDisplayNameForPin(Pin->GetFName());
		if(Pin->DisplayName != DisplayName)
		{
			Pin->DisplayName = DisplayName;
			Notify(ERigVMGraphNotifType::PinRenamed, Pin);
		}
	}

	if(InNode->IsResolved())
	{
		for(URigVMPin* Pin : InNode->GetPins())
		{
			if(Pin->IsWildCard() || Pin->ContainsWildCardSubPin() || Pin->GetDirection() == ERigVMPinDirection::Hidden)
			{
				continue;
			}
			if(!Pin->IsValidDefaultValue(Pin->GetDefaultValue()))
			{
				const FString NewDefaultValue = InNode->GetInitialDefaultValueForPin(Pin->GetFName(), FilteredPermutationIndices);
				if(!NewDefaultValue.IsEmpty())
				{
					SetPinDefaultValue(Pin, NewDefaultValue, true, bSetupUndoRedo, false);
				}
			}
		}
	}
}

bool URigVMController::FullyResolveTemplateNode(URigVMTemplateNode* InNode, int32 InPermutationIndex, bool bSetupUndoRedo)
{
	if(bIsFullyResolvingTemplateNode)
	{
		return false;
	}
	TGuardValue<bool> ReentryGuard(bIsFullyResolvingTemplateNode, true);
	
	check(InNode);

	if (InNode->IsSingleton())
	{
		return true;
	}

	const FRigVMTemplate* Template = InNode->GetTemplate();
	const FRigVMDispatchFactory* Factory = Template->GetDispatchFactory();
	
	InNode->ResolvedPermutation = InPermutationIndex;
	
	// Figure out the permutation index from the pin types
	if (InPermutationIndex == INDEX_NONE)
	{
		TArray<int32> Permutations = InNode->GetResolvedPermutationIndices(false);
		check(!Permutations.IsEmpty());
		InNode->ResolvedPermutation = Permutations[0];

		// make sure the permutation exists
		if(Factory)
		{
			FRigVMTemplateTypeMap TypeMap = InNode->GetTemplate()->GetTypesForPermutation(InNode->ResolvedPermutation);
			const FRigVMFunctionPtr DispatchFunction = Factory->GetDispatchFunction(TypeMap);
			const FRigVMFunction* ResolvedFunction = Template->GetPermutation(InNode->ResolvedPermutation);
			check(DispatchFunction);
			check(ResolvedFunction);
			check(ResolvedFunction->FunctionPtr == DispatchFunction);
		}
	}
	

	const FRigVMFunction* ResolvedFunction = Template->GetPermutation(InNode->ResolvedPermutation);
	const TArray<int32> PermutationIndices = {InNode->ResolvedPermutation};

	// find all existing pins that we may need to change
	TArray<FRigVMTemplateArgument> MissingPins;
	TArray<URigVMPin*> PinsToRemove;
	TMap<URigVMPin*, TRigVMTypeIndex> PinTypesToChange;
	for(int32 ArgIndex = 0; ArgIndex < Template->NumArguments(); ArgIndex++)
	{
		const FRigVMTemplateArgument* Argument = Template->GetArgument(ArgIndex);
		const TRigVMTypeIndex ResolvedTypeIndex = Argument->GetSupportedTypeIndices(PermutationIndices)[0];

		URigVMPin* Pin = InNode->FindPin(Argument->GetName().ToString());
		if(Pin == nullptr)
		{
			ReportErrorf(TEXT("Template node %s is missing a pin for argument %s"),
				*InNode->GetNodePath(),
				*Argument->GetName().ToString()
			);
			return false;
		}

		if(Pin->GetTypeIndex() != ResolvedTypeIndex && ResolvedTypeIndex != RigVMTypeUtils::TypeIndex::Execute)
		{
			PinTypesToChange.Add(Pin, ResolvedTypeIndex);
		}
	}

	// find all missing pins which are not arguments for the template
	if(ResolvedFunction)
	{
		if(ResolvedFunction->Struct == nullptr)
		{
			const TArray<FRigVMTemplateArgument>& Arguments = Template->Arguments;
			for(const FRigVMTemplateArgument& Argument : Arguments)
			{
				const TRigVMTypeIndex ExpectedTypeIndex = Argument.TypeIndices[InNode->ResolvedPermutation];
				if(URigVMPin* Pin = InNode->FindPin(Argument.GetName().ToString()))
				{
					if(Pin->GetTypeIndex() != ExpectedTypeIndex && ExpectedTypeIndex != RigVMTypeUtils::TypeIndex::Execute)
					{
						PinTypesToChange.Add(Pin, ExpectedTypeIndex);
					}
				}
				else
				{
					MissingPins.Add(Argument);
				}
			}
		}
		else
		{
			TArray<UStruct*> StructsToVisit = FRigVMTemplate::GetSuperStructs(ResolvedFunction->Struct, true);
			for(UStruct* StructToVisit : StructsToVisit)
			{
				for (TFieldIterator<FProperty> It(StructToVisit, EFieldIterationFlags::None); It; ++It)
				{
					const FRigVMTemplateArgument ExpectedArgument(*It);
					const TRigVMTypeIndex ExpectedTypeIndex = ExpectedArgument.GetSupportedTypeIndices()[0];

					if(URigVMPin* Pin = InNode->FindPin(It->GetFName().ToString()))
					{
						if(Pin->GetTypeIndex() != ExpectedTypeIndex && ExpectedTypeIndex != RigVMTypeUtils::TypeIndex::Execute)
						{
							PinTypesToChange.Add(Pin, ExpectedTypeIndex);
						}
					}
					else
					{
						MissingPins.Add(ExpectedArgument);
					}
				}
			}
		}
	}

	// find all pins which don't have a matching arg on the function
	if(ResolvedFunction)
	{
		if(ResolvedFunction->Struct == nullptr)
		{
			FRigVMDispatchContext DispatchContext;
			if(const URigVMDispatchNode* DispatchNode = Cast<URigVMDispatchNode>(InNode))
			{
				DispatchContext = DispatchNode->GetDispatchContext();
			}

			const TArray<FRigVMTemplateArgument>& Arguments = Template->Arguments;
			for(URigVMPin* Pin : InNode->GetPins())
			{
				bool bFound = Arguments.ContainsByPredicate([Pin](const FRigVMTemplateArgument& Argument)
				{
					return Pin->GetFName() == Argument.GetName();
				});
				if(!bFound)
				{
					bFound = Template->GetExecuteArguments(DispatchContext).ContainsByPredicate([Pin](const FRigVMExecuteArgument& Argument)
					{
						return Pin->GetFName() == Argument.Name;
					});
				}
				if(!bFound)
				{
					PinsToRemove.Add(Pin);
				}
			}
		}
		else
		{
			for(URigVMPin* Pin : InNode->GetPins())
			{
				if(ResolvedFunction->Struct->FindPropertyByName(Pin->GetFName()) == nullptr)
				{
					PinsToRemove.Add(Pin);
				}
			}
		}

		// update the cached resolved function name
		InNode->ResolvedFunctionName = ResolvedFunction->Name;
	}

	// exit out early if there's nothing to do
	if(PinTypesToChange.IsEmpty() && MissingPins.IsEmpty() && PinsToRemove.IsEmpty())
	{
		ResolveTemplateNodeMetaData(InNode, bSetupUndoRedo);
		return true;
	}

	if(bSetupUndoRedo)
	{
		OpenUndoBracket(TEXT("Resolve Template Node"));
	}

	// update the incorrectly typed pins
	for(const TPair<URigVMPin*, TRigVMTypeIndex>& Pair : PinTypesToChange)
	{
		URigVMPin* Pin = Pair.Key;
		const TRigVMTypeIndex& ExpectedTypeIndex = Pair.Value;
		
		if(!Pin->IsWildCard())
		{
			if(Pin->GetTypeIndex() != ExpectedTypeIndex)
			{
				if (Pin->GetDirection() != ERigVMPinDirection::Hidden)
				{
					const int32 WildCardIndex = Pin->IsArray() ? RigVMTypeUtils::TypeIndex::WildCardArray : RigVMTypeUtils::TypeIndex::WildCard;
					if(!ChangePinType(Pin, WildCardIndex, bSetupUndoRedo, false, true, true))
					{
						if(bSetupUndoRedo)
						{
							CancelUndoBracket();
						}
						return false;
					}
				}
				if(!ChangePinType(Pin, ExpectedTypeIndex, bSetupUndoRedo, false, true, true))
				{
					if(bSetupUndoRedo)
					{
						CancelUndoBracket();
					}
					return false;
				}
			}
		}
	}

	// remove obsolete pins
	for(URigVMPin* PinToRemove : PinsToRemove)
	{
		RemovePin(PinToRemove, false);
	}

	// add missing pins
	if(ResolvedFunction)
	{
		if(ResolvedFunction->Struct == nullptr)
		{
			for(const FRigVMTemplateArgument& MissingPin : MissingPins)
			{
				check(MissingPin.GetDirection() == ERigVMPinDirection::Hidden);
				
				URigVMPin* Pin = NewObject<URigVMPin>(Cast<UObject>(InNode), MissingPin.GetName());

				const TRigVMTypeIndex TypeIndex = MissingPin.TypeIndices[InNode->ResolvedPermutation];
				const FRigVMTemplateArgumentType& Type = FRigVMRegistry::Get().GetType(TypeIndex); 
				
				Pin->Direction = MissingPin.GetDirection();
				Pin->CPPType = Pin->LastKnownCPPType = Type.CPPType.ToString();
				Pin->CPPTypeObject = Type.CPPTypeObject;
				Pin->CPPTypeObjectPath = Type.GetCPPTypeObjectPath();
				Pin->LastKnownTypeIndex = TypeIndex;

				if(Factory)
				{
					const FName DisplayNameText = Factory->GetDisplayNameForArgument(MissingPin.GetName());
					if(!DisplayNameText.IsNone())
					{
						Pin->DisplayName = *DisplayNameText.ToString();
					}
				}
				
				AddNodePin(InNode, Pin);
				Notify(ERigVMGraphNotifType::PinAdded, Pin);

				// we don't need to set the default value here since the pin is hidden
			}
		}
		else
		{
			for(const FRigVMTemplateArgument& MissingPin : MissingPins)
			{
				check(MissingPin.GetDirection() == ERigVMPinDirection::Hidden);
				
				FProperty* Property = ResolvedFunction->Struct->FindPropertyByName(MissingPin.GetName());
				check(Property);

				URigVMPin* Pin = NewObject<URigVMPin>(Cast<UObject>(InNode), MissingPin.GetName());
				ConfigurePinFromProperty(Property, Pin, MissingPin.GetDirection());

				AddNodePin(InNode, Pin);
				Notify(ERigVMGraphNotifType::PinAdded, Pin);

				// we don't need to set the default value here since the pin is hidden
			}
		}
	}

	if(bSetupUndoRedo)
	{
#if WITH_EDITOR
		RegisterUseOfTemplate(InNode);
#endif
		CloseUndoBracket();
	}

	return true;
}

bool URigVMController::ResolveWildCardPin(const FString& InPinPath, const FString& InCPPType, const FName& InCPPTypeObjectPath, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return false;
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	UObject* CPPTypeObject = nullptr;
	if (!InCPPTypeObjectPath.IsNone())
	{
		CPPTypeObject = RigVMTypeUtils::FindObjectFromCPPTypeObjectPath<UObject>(InCPPTypeObjectPath.ToString());
		if (CPPTypeObject == nullptr)
		{
			ReportErrorf(TEXT("Cannot find cpp type object for path '%s'."), *InCPPTypeObjectPath.ToString());
			return false;
		}
	}

	const FString CPPType = RigVMTypeUtils::PostProcessCPPType(InCPPType, CPPTypeObject);

	if (URigVMPin* Pin = Graph->FindPin(InPinPath))
	{
		if (ResolveWildCardPin(Pin, FRigVMTemplateArgumentType(*CPPType, CPPTypeObject), bSetupUndoRedo, bPrintPythonCommand))
		{
			if (bPrintPythonCommand)
			{
				const FString GraphName = GetSanitizedGraphName(GetGraph()->GetGraphName());

				// bool ResolveWildCardPin(const FString& InPinPath, const FString& InCPPType, const FName& InCPPTypeObjectPath, bool bSetupUndoRedo = true, bool bPrintPythonCommand = false);
				RigVMPythonUtils::Print(GetGraphOuterName(), 
									FString::Printf(TEXT("blueprint.get_controller_by_name('%s').resolve_wild_card_pin('%s', '%s', '%s')"),
													*GraphName,
													*InPinPath,
													*InCPPType,
													*InCPPTypeObjectPath.ToString()));
			}
			
			return true;
		}
	}

	return false;
}

bool URigVMController::ResolveWildCardPin(URigVMPin* InPin, const FRigVMTemplateArgumentType& InType, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return false;
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}

	return ResolveWildCardPin(InPin, FRigVMRegistry::Get().GetTypeIndex(InType), bSetupUndoRedo, bPrintPythonCommand);
}

bool URigVMController::ResolveWildCardPin(const FString& InPinPath, TRigVMTypeIndex InTypeIndex, bool bSetupUndoRedo,
	bool bPrintPythonCommand)
{
	if (!IsValidGraph())
	{
		return false;
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (URigVMPin* Pin = Graph->FindPin(InPinPath))
	{
		return ResolveWildCardPin(Pin, InTypeIndex, bSetupUndoRedo, bPrintPythonCommand);
	}
	return false;
}

bool URigVMController::ResolveWildCardPin(URigVMPin* InPin, TRigVMTypeIndex InTypeIndex, bool bSetupUndoRedo, bool bPrintPythonCommand)
{
	if (InPin->IsStructMember())
	{
		return false;
	}

	if(FRigVMRegistry::Get().IsWildCardType(InTypeIndex))
	{
		return false;
	}

	if (InPin->GetTypeIndex() == InTypeIndex)
	{
		return false;
	}

	URigVMPin* RootPin = InPin;
	TRigVMTypeIndex Type = InTypeIndex;	
	while (RootPin->IsArrayElement())
	{
		RootPin = InPin->GetParentPin();
		Type = FRigVMRegistry::Get().GetArrayTypeFromBaseTypeIndex(Type);
	}
	
	ensure(RootPin->GetNode()->IsA<URigVMTemplateNode>());
	URigVMTemplateNode* TemplateNode = CastChecked<URigVMTemplateNode>(RootPin->GetNode());

	TRigVMTypeIndex NewType = INDEX_NONE;
	TemplateNode->SupportsType(RootPin, Type, &NewType);
	if (NewType != INDEX_NONE)
	{
		// We support the new type, and its different than the pin type
		if (NewType == Type && InPin->GetTypeIndex() != NewType)
		{
			 
		}
		// We support the new type, but its different than the one provided
		else if (NewType != Type)
		{
			// if its the same as the pin, we are done
			if (InPin->GetTypeIndex() == NewType)
			{
				return false;
			}
			Type = NewType;
		}
	}
	else
	{
		return false;
	}
	
	FRigVMBaseAction Action;
	if (bSetupUndoRedo)
	{
		Action.Title = TEXT("Resolve Wildcard Pin");
		ActionStack->BeginAction(Action);
	}

	if (!InPin->IsWildCard())
	{
		if (!UnresolveTemplateNodes({TemplateNode}, bSetupUndoRedo))
		{
			return false;
		}
	}
	
	if (!ChangePinType(RootPin, Type, bSetupUndoRedo, true, true, false))
	{
		if (bSetupUndoRedo)
		{
			ActionStack->CancelAction(Action, this);
		}
		return false;
	}

	UpdateTemplateNodePinTypes(TemplateNode, bSetupUndoRedo);

	if (bSetupUndoRedo)
	{
		ActionStack->EndAction(Action);
	}
	
	return true;

}

bool URigVMController::UpdateTemplateNodePinTypes(URigVMTemplateNode* InNode, bool bSetupUndoRedo, bool bInitializeDefaultValue)
{
	URigVMGraph* Graph = GetGraph();
	check(InNode->GetGraph() == Graph);
	bool bAnyTypeChanged = false;

	const FRigVMTemplate* Template = InNode->GetTemplate();
	if(Template == nullptr)
	{
		return false;
	}

	const FRigVMRegistry& Registry = FRigVMRegistry::Get();

	if (InNode->IsA<URigVMFunctionEntryNode>() || InNode->IsA<URigVMFunctionReturnNode>())
	{
		return false;
	}

	TArray<int32> ResolvedPermutations = InNode->GetResolvedPermutationIndices(true);
	InNode->ResolvedPermutation = ResolvedPermutations.Num() == 1 ? ResolvedPermutations[0] : INDEX_NONE;

	TArray<URigVMPin*> Pins = InNode->GetPins();
	TArray<const FRigVMTemplateArgument*> Arguments;
	Arguments.SetNumUninitialized(Pins.Num());
	for(int32 PinIndex=0; PinIndex < Pins.Num(); ++PinIndex)
	{
		URigVMPin* Pin = Pins[PinIndex];
		Arguments[PinIndex] = Template->FindArgument(Pin->GetFName());
	}

	// Remove invalid permutations
	ResolvedPermutations.RemoveAll([&](const int32& Permutation)
	{
		for (const FRigVMTemplateArgument* Argument : Arguments)
		{
			if (Argument && Argument->TypeIndices[Permutation] == INDEX_NONE)
			{
				return true;
			}
		}
		return false;
	});

	FRigVMDispatchContext DispatchContext;
	if(const URigVMDispatchNode* DispatchNode = Cast<URigVMDispatchNode>(InNode))
	{
		DispatchContext = DispatchNode->GetDispatchContext();
	}

	// Find the types for each possible permutation
	TMap<int32, TArray<TRigVMTypeIndex>> PinTypes; // permutation to pin types
	for (int32 ResolvedPermutation : ResolvedPermutations)
	{
		for(int32 PinIndex=0; PinIndex < Pins.Num(); ++PinIndex)
		{
			URigVMPin* Pin = Pins[PinIndex];
			if (Pin->GetDirection() == ERigVMPinDirection::Hidden)
			{
				PinTypes.FindOrAdd(ResolvedPermutation).Add(INDEX_NONE);
				continue;
			}

			bool bAddedType = false;
			TArray<TRigVMTypeIndex>& Types = PinTypes.FindOrAdd(ResolvedPermutation);
			if (const FRigVMTemplateArgument* Argument = Arguments[PinIndex])
			{
				Types.Add(Argument->TypeIndices[ResolvedPermutation]);
				bAddedType = true;
			}
			else if (const FRigVMExecuteArgument* ExecuteArgument = Template->FindExecuteArgument(Pin->GetFName(), DispatchContext))
			{
				Types.Add(ExecuteArgument->TypeIndex);
				bAddedType = true;
			}
			
			if (!bAddedType)
			{
				// This is a pin with no argument
				// Its marked as invalid, and will show wildcard type
				Types.Add(INDEX_NONE);
			}
		}
	}

	// Some pins can benefit from reducing types to a single option
	// Even if some pins reduce to a single type (maybe represented by a single permutation), the rest should still display a wildcard pin
	// If reduction happens for multiple pins, we need to make sure that they reduce to the same permutation
	// If possible, we want to respect the current pin type if its not wildcard
	TArray<bool> WasReduced;
	WasReduced.SetNumZeroed(Pins.Num());

	TMap<int32, TArray<TRigVMTypeIndex>> ReducedTypes = PinTypes;
	TArray<TRigVMTypeIndex> FinalPinTypes;
	FinalPinTypes.SetNumUninitialized(Pins.Num());
	
	// Reduce compatible types, try to find the permutation in the reduced types
	for(int32 PinIndex=0; PinIndex < Pins.Num(); ++PinIndex)
	{
		URigVMPin* Pin = Pins[PinIndex];
		if (Pin->GetDirection() == ERigVMPinDirection::Hidden)
		{
			continue;
		}
		TArray<TRigVMTypeIndex> Types;
		Types.Reserve(ResolvedPermutations.Num());
		TRigVMTypeIndex PreferredType = INDEX_NONE;
		int32 TypesFoundInReduced = 0;
		for (int32 ResolvedPermutation : ResolvedPermutations)
		{
			Types.Add(PinTypes.FindChecked(ResolvedPermutation)[PinIndex]);
			if (ReducedTypes.Contains(ResolvedPermutation))
			{
				TypesFoundInReduced++;
				if (PreferredType == INDEX_NONE)
				{
					PreferredType = PinTypes.FindChecked(ResolvedPermutation)[PinIndex];
				}
			}
		}

		if (TypesFoundInReduced > 1)
		{
			PreferredType = Pin->GetTypeIndex();
		}
		FinalPinTypes[PinIndex] = InNode->TryReduceTypesToSingle(Types, PreferredType);
		if (FinalPinTypes[PinIndex] != INDEX_NONE)
		{
			WasReduced[PinIndex] =  true;
		}

		// Remove reduced types which do not match this type
		TArray<int32> PermutationToRemove;
		for (TPair<int32, TArray<TRigVMTypeIndex>>& Pair : ReducedTypes)
		{
			if (Pair.Value[PinIndex] != FinalPinTypes[PinIndex])
			{
				PermutationToRemove.Add(Pair.Key);
			}
		}
		for (int32& ToRemove : PermutationToRemove)
		{
			ReducedTypes.Remove(ToRemove);
		}
	}
	
	// First unresolve any pins that need unresolving, then resolve everything else
	for (int32 UnresolveResolve=0; UnresolveResolve<2; ++UnresolveResolve)
	{
		for(int32 PinIndex=0; PinIndex < Pins.Num(); ++PinIndex)
		{
			URigVMPin* Pin = Pins[PinIndex];
			if (UnresolveResolve == 0 && Pin->IsWildCard())
			{
				continue;
			}
			
			if (Pin->GetDirection() == ERigVMPinDirection::Hidden)
			{
				continue;
			}

			bool bShouldUnresolve = FinalPinTypes[PinIndex] == INDEX_NONE;
			// If we are about to resolve the pin to a different type, first unresolve it
			if (!bShouldUnresolve && UnresolveResolve == 0 && Pin->GetTypeIndex() != FinalPinTypes[PinIndex])
			{
				bShouldUnresolve = true;
			}

			if (bShouldUnresolve)
			{
				// Unresolve
				if (Pin->HasInjectedNodes())
				{
					EjectNodeFromPin(Pin, bSetupUndoRedo);
				}
				
				const FRigVMTemplateArgument* Argument = Template->FindArgument(*Pin->GetName());
				FRigVMTemplateArgument::EArrayType ArrayType;
				if (Argument)
				{
					ArrayType = Argument->GetArrayType();
				}
				else
				{
					ArrayType = Pin->IsArray() ? FRigVMTemplateArgument::EArrayType::EArrayType_ArrayValue : FRigVMTemplateArgument::EArrayType::EArrayType_SingleValue;
				}
				
				FString CPPType = RigVMTypeUtils::GetWildCardCPPType();
				UObject* CPPObjectType = RigVMTypeUtils::GetWildCardCPPTypeObject();

				if (ArrayType == FRigVMTemplateArgument::EArrayType_ArrayValue)
				{
					CPPType = RigVMTypeUtils::GetWildCardArrayCPPType();
				}
				else if(ArrayType == FRigVMTemplateArgument::EArrayType_Mixed)
				{
					CPPType = Pin->IsArray() ? RigVMTypeUtils::GetWildCardArrayCPPType() : RigVMTypeUtils::GetWildCardCPPType();
				}

				// execute pins are no longer part of the template, avoid changing the type
				if(Argument == nullptr && Pin->IsExecuteContext())
				{
					CPPType = Pin->GetCPPType();
					CPPObjectType = Pin->GetCPPTypeObject();
				}

				if (Pin->GetCPPType() != CPPType || Pin->GetCPPTypeObject() != CPPObjectType)
				{
					bAnyTypeChanged = !Pin->IsExecuteContext();
					if(bAnyTypeChanged)
					{
						ReportPinTypeChange(Pin, CPPType);
					}
					ChangePinType(Pin, CPPType, CPPObjectType, bSetupUndoRedo, false, false, false, bInitializeDefaultValue);
				}
			}
			else 
			{
				// Resolve
				if (Pin->GetTypeIndex() != FinalPinTypes[PinIndex])
				{
					bAnyTypeChanged = !Pin->IsExecuteContext();
					if(bAnyTypeChanged)
					{
						const FString CPPType = Registry.GetType(FinalPinTypes[PinIndex]).CPPType.ToString();
						ReportPinTypeChange(Pin, CPPType);
					}
					ChangePinType(Pin, FinalPinTypes[PinIndex], bSetupUndoRedo, false, false, false, bInitializeDefaultValue);
				}
			}
		}
	}

	bool bHasWildcard = InNode->HasWildCardPin();
	
	if (bHasWildcard)
	{
		InNode->ResolvedPermutation = INDEX_NONE;
	}
	else
	{
		// If we got into a resolved permutation through reducing types, we need to set the result in the node
		ResolvedPermutations = InNode->GetResolvedPermutationIndices(false);
		check(ResolvedPermutations.Num() == 1);
		InNode->ResolvedPermutation = ResolvedPermutations[0];
	}
	
	if (URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(InNode))
	{
		if (const FRigVMFunction* Function = UnitNode->GetResolvedFunction())
		{
			UnitNode->ResolvedFunctionName = Function->GetName();
		}
	}
	
	return bAnyTypeChanged;
}

bool URigVMController::PrepareToLink(URigVMPin* FirstToResolve, URigVMPin* SecondToResolve, bool bSetupUndoRedo)
{
	FRigVMRegistry& Registry = FRigVMRegistry::Get();
	
	// Check if there is anything to do
	if (!FirstToResolve->IsWildCard() &&
		!SecondToResolve->IsWildCard() &&
		Registry.CanMatchTypes(FirstToResolve->GetTypeIndex(), SecondToResolve->GetTypeIndex(), true))
	{
		return true;
	}
	
	// Find out the matching supported types
	TArray<TRigVMTypeIndex> MatchingTypes;
	{
		auto GetPinSupportedTypes = [&Registry](URigVMPin* Pin) -> TArray<TRigVMTypeIndex>
		{
			URigVMTemplateNode* TemplateNode = Cast<URigVMTemplateNode>(Pin->GetNode());
			if (!TemplateNode || TemplateNode->IsSingleton() || !Pin->IsWildCard())
			{
				return {Pin->GetTypeIndex()};
			}
			else
			{
				if (const FRigVMTemplate* Template = TemplateNode->GetTemplate())
				{
					URigVMPin* RootPin = Pin;
					uint8 ArrayLevels = 0;
					while (RootPin->GetParentPin() != nullptr)
					{
						ArrayLevels++;
						RootPin = RootPin->GetParentPin();
					}
					if (const FRigVMTemplateArgument* Argument = Template->FindArgument(RootPin->GetFName()))
					{
						TArray<int32> ResolvedPermutations = TemplateNode->GetResolvedPermutationIndices(true);
						TArray<TRigVMTypeIndex> Types = Argument->GetSupportedTypeIndices(ResolvedPermutations);
						for (int32 i=0; i<ArrayLevels; ++i)
						{
							for (TRigVMTypeIndex& Type : Types)
							{
								Type = Registry.GetBaseTypeFromArrayTypeIndex(Type);
							}
						}
						return Types;
					}
				}
			}
			return {};
		};
		MatchingTypes = GetPinSupportedTypes(FirstToResolve);
		TArray<TRigVMTypeIndex> SecondTypes = GetPinSupportedTypes(SecondToResolve);
		MatchingTypes.RemoveAll([&Registry, SecondTypes](const TRigVMTypeIndex& FirstType)
		{
			return !SecondTypes.ContainsByPredicate([&Registry, FirstType](const TRigVMTypeIndex& SecondType)
			{
				return Registry.CanMatchTypes(FirstType, SecondType, true);
			});
		});
	}

	if (MatchingTypes.IsEmpty())
	{
		return false;
	}

	// reduce matching types by duplicate entries for float / double
	{
		TArray<TRigVMTypeIndex> FilteredMatchingTypes;
		FilteredMatchingTypes.Reserve(MatchingTypes.Num());
		for(const TRigVMTypeIndex& MatchingType : MatchingTypes)
		{
			// special case float singe & array
			if(MatchingType == RigVMTypeUtils::TypeIndex::Float)
			{
				if(MatchingTypes.Contains(RigVMTypeUtils::TypeIndex::Double))
				{
					continue;
				}
			}
			if(MatchingType == RigVMTypeUtils::TypeIndex::FloatArray)
			{
				if(MatchingTypes.Contains(RigVMTypeUtils::TypeIndex::DoubleArray))
				{
					continue;
				}
			}

			bool bAlreadyContainsMatch = false;
			for(const TRigVMTypeIndex& FilteredType : FilteredMatchingTypes)
			{
				if(Registry.CanMatchTypes(MatchingType, FilteredType, true))
				{
					bAlreadyContainsMatch = true;
					break;
				}
			}
			if(bAlreadyContainsMatch)
			{
				continue;
			}
			FilteredMatchingTypes.Add(MatchingType);
		}
		Swap(FilteredMatchingTypes, MatchingTypes);
	}

	TRigVMTypeIndex FinalType = INDEX_NONE;
	if (MatchingTypes.Num() > 1)
	{
		// Query the user for the type to resolve the pin (from the list of MatchingTypes)
		if (RequestPinTypeSelectionDelegate.IsBound())
		{
			FinalType = RequestPinTypeSelectionDelegate.Execute(MatchingTypes);
		}
		else
		{
			return false;
		}
	}
	else
	{
		FinalType = MatchingTypes[0];
	}
	
	// If only one match, resolve both pins to that
	bool bSuccess = true;
	if (FinalType != INDEX_NONE)
	{
		if (FirstToResolve->IsWildCard())
		{
			bSuccess &= ResolveWildCardPin(FirstToResolve, FinalType, bSetupUndoRedo);
		}
		if (SecondToResolve->IsWildCard())
		{
			bSuccess &= ResolveWildCardPin(SecondToResolve, FinalType, bSetupUndoRedo);
		}
	}
	else
	{
		return false;
	}

	return bSuccess;
}

bool URigVMController::ChangePinType(const FString& InPinPath, const FString& InCPPType, const FName& InCPPTypeObjectPath, bool bSetupUndoRedo, bool bSetupOrphanPins, bool bBreakLinks, bool bRemoveSubPins, bool bInitializeDefaultValue)
{
	if (!IsValidGraph())
	{
		return false;
	}

	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}

	URigVMGraph* Graph = GetGraph();
	check(Graph);

	if (URigVMPin* Pin = Graph->FindPin(InPinPath))
	{
		return ChangePinType(Pin, InCPPType, InCPPTypeObjectPath, bSetupUndoRedo, bSetupOrphanPins, bBreakLinks, bRemoveSubPins, bInitializeDefaultValue);
	}

	return false;
}

bool URigVMController::ChangePinType(URigVMPin* InPin, const FString& InCPPType, const FName& InCPPTypeObjectPath, bool bSetupUndoRedo, bool bSetupOrphanPins, bool bBreakLinks, bool bRemoveSubPins, bool bInitializeDefaultValue)
{
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
	if (InCPPType == TEXT("None") || InCPPType.IsEmpty())
	{
		return false;
	}

	UObject* CPPTypeObject = RigVMTypeUtils::FindObjectFromCPPTypeObjectPath(InCPPTypeObjectPath.ToString());

	// always refresh pin type if it is a user defined struct, whose internal layout can change at anytime
	bool bForceRefresh = false;
	if (CPPTypeObject && CPPTypeObject->IsA<UUserDefinedStruct>())
	{
		bForceRefresh = true;
	}

	if (!bForceRefresh)
	{
		if (InPin->CPPType == InCPPType && InPin->CPPTypeObject == CPPTypeObject)
		{
			return true;
		}
	}

	return ChangePinType(InPin, InCPPType, CPPTypeObject, bSetupUndoRedo, bSetupOrphanPins, bBreakLinks, bRemoveSubPins, bInitializeDefaultValue);
}

bool URigVMController::ChangePinType(URigVMPin* InPin, const FString& InCPPType, UObject* InCPPTypeObject, bool bSetupUndoRedo, bool bSetupOrphanPins, bool bBreakLinks, bool bRemoveSubPins, bool bInitializeDefaultValue)
{
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
	if (InCPPType == TEXT("None") || InCPPType.IsEmpty())
	{
		return false;
	}

	if (RigVMTypeUtils::RequiresCPPTypeObject(InCPPType) && !InCPPTypeObject)
	{
		return false;
	}

	const FRigVMTemplateArgumentType Type(*InCPPType, InCPPTypeObject);
	// pin types are chosen from the graph pin type menu so it is not guaranteed that the chosen type
	// is registered, hence the use of FindOrAddType
	const TRigVMTypeIndex TypeIndex = FRigVMRegistry::Get().FindOrAddType(Type);
	return ChangePinType(InPin, TypeIndex, bSetupUndoRedo, bSetupOrphanPins, bBreakLinks, bRemoveSubPins, bInitializeDefaultValue);
}

bool URigVMController::ChangePinType(URigVMPin* InPin, TRigVMTypeIndex InTypeIndex, bool bSetupUndoRedo, bool bSetupOrphanPins, bool bBreakLinks, bool bRemoveSubPins, bool bInitializeDefaultValue)
{
	if (!bIsTransacting && !IsGraphEditable())
	{
		return false;
	}
	
	if (InTypeIndex == INDEX_NONE)
	{
		return false;
	}

	check(InPin->GetGraph() == GetGraph());
	if(InPin->IsExecuteContext() && FRigVMRegistry::Get().IsExecuteType(InTypeIndex))
	{
		return false;
	}
	

	// only allow valid pin cpp types on template nodes
	TRigVMTypeIndex TypeIndex = InTypeIndex;
	if(URigVMTemplateNode* TemplateNode = Cast<URigVMTemplateNode>(InPin->GetNode()))
	{
		if (!bIsTransacting)
		{
			if (InPin->GetDirection() != ERigVMPinDirection::Hidden)
			{
				if (TemplateNode->IsA<URigVMUnitNode>() || TemplateNode->IsA<URigVMDispatchNode>())
				{
					if(!TemplateNode->SupportsType(InPin, InTypeIndex))
					{
						ReportErrorf(TEXT("ChangePinType: %s doesn't support type '%s'."), *InPin->GetPinPath(), *FRigVMRegistry::Get().GetType(InTypeIndex).CPPType.ToString());
						return false;
					}
				}
			}
		}

		// If changing to wildcard, try to maintain the container type
		const FRigVMRegistry& Registry = FRigVMRegistry::Get();
		if (Registry.IsWildCardType(TypeIndex)) 
		{
			const bool bIsArrayType = FRigVMRegistry::Get().IsArrayType(TypeIndex);
			if(InPin->IsRootPin() && bIsArrayType != InPin->IsArray())
			{
				// nothing to do here - leave the type as is 
			}
			else
			{
				const TRigVMTypeIndex BaseTypeIndex = bIsArrayType ? FRigVMRegistry::Get().GetBaseTypeFromArrayTypeIndex(TypeIndex) : TypeIndex;
				TypeIndex = InPin->IsArray() ? Registry.GetArrayTypeFromBaseTypeIndex(BaseTypeIndex) : BaseTypeIndex;
			}
		}
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
	}
	
	if(bSetupOrphanPins)
	{
		Links.Append(InPin->GetSourceLinks(true));
		Links.Append(InPin->GetTargetLinks(true));
		if (Links.Num() > 0)
		{
			DetachLinksFromPinObjects(&Links);

			const FString OrphanedName = FString::Printf(TEXT("%s%s"), *URigVMPin::OrphanPinPrefix, *InPin->GetName());
			if(InPin->GetNode()->FindPin(OrphanedName) == nullptr)
			{
				URigVMPin* OrphanedPin = NewObject<URigVMPin>(InPin->GetNode(), *OrphanedName);
				ConfigurePinFromPin(OrphanedPin, InPin);
				OrphanedPin->DisplayName = InPin->GetFName();

				if(OrphanedPin->IsStruct())
				{
					AddPinsForStruct(OrphanedPin->GetScriptStruct(), OrphanedPin->GetNode(), OrphanedPin, OrphanedPin->Direction, OrphanedPin->GetDefaultValue(), false);
				}
				
				InPin->GetNode()->OrphanedPins.Add(OrphanedPin);
			}
		}
	}

	if(bRemoveSubPins || !InPin->IsArray())
	{
		TArray<URigVMPin*> Pins = InPin->SubPins;
		for (URigVMPin* Pin : Pins)
		{
			RemovePin(Pin, bSetupUndoRedo);
		}
		
		InPin->SubPins.Reset();
	}

	if (bSetupUndoRedo)
	{
		ActionStack->AddAction(FRigVMChangePinTypeAction(InPin, TypeIndex, bSetupOrphanPins, bBreakLinks, bRemoveSubPins));
	}
	
	// compute the number of remaining wildcard pins
	auto WildCardPinCountPredicate = [](const URigVMPin* Pin) { return Pin->IsWildCard(); };
	TArray<URigVMPin*> AllPins = InPin->GetNode()->GetAllPinsRecursively();
	int32 RemainingWildCardPins = Algo::CountIf(AllPins, WildCardPinCountPredicate);
	const bool bPinWasWildCard = InPin->IsWildCard();

	const FPinState PreviousPinState = GetPinState(InPin);
	const FString PreviousCPPType = InPin->CPPType;
	
	const FRigVMTemplateArgumentType Type = FRigVMRegistry::Get().GetType(TypeIndex);
	InPin->CPPType = Type.CPPType.ToString();
	InPin->CPPTypeObjectPath = Type.GetCPPTypeObjectPath();
	InPin->CPPTypeObject = Type.CPPTypeObject;
	InPin->bIsDynamicArray = FRigVMRegistry::Get().IsArrayType(TypeIndex);

	if (bInitializeDefaultValue)
	{
		InPin->DefaultValue = FString();

		if(InPin->IsRootPin() && !InPin->IsWildCard())
		{
			if(URigVMTemplateNode* TemplateNode = Cast<URigVMTemplateNode>(InPin->GetNode()))
			{
				InPin->DefaultValue = TemplateNode->GetInitialDefaultValueForPin(InPin->GetFName());
			}
		}
	}

	if (InPin->IsExecuteContext() && !InPin->GetNode()->IsA<URigVMFunctionEntryNode>() && !InPin->GetNode()->IsA<URigVMFunctionReturnNode>())
	{
		InPin->Direction = ERigVMPinDirection::IO;
	}

	if (InPin->IsStruct() && !InPin->IsArray())
	{
		FString DefaultValue = InPin->DefaultValue;
		CreateDefaultValueForStructIfRequired(InPin->GetScriptStruct(), DefaultValue);
		AddPinsForStruct(InPin->GetScriptStruct(), InPin->GetNode(), InPin, InPin->Direction, DefaultValue, false);
	}

	if (InPin->IsArray())
	{
		const TRigVMTypeIndex BaseTypeIndex = FRigVMRegistry::Get().GetBaseTypeFromArrayTypeIndex(TypeIndex);
		for (int32 i=0; i<InPin->GetSubPins().Num(); ++i)
		{
			URigVMPin* SubPin = InPin->GetSubPins()[i];
			if (SubPin->GetDirection() == ERigVMPinDirection::Hidden)
			{
				continue;
			}
			ChangePinType(SubPin, BaseTypeIndex, bSetupUndoRedo, bSetupOrphanPins, bBreakLinks, bRemoveSubPins, bInitializeDefaultValue);
		}
	}

	// if the pin didn't change type - let's maintain the pin state
	if(PreviousCPPType == InPin->CPPType && !InPin->IsWildCard())
	{
		ApplyPinState(InPin, PreviousPinState, false);
	}

	// if this is a template clear its caches
	if(URigVMTemplateNode* TemplateNode = Cast<URigVMTemplateNode>(InPin->GetNode()))
	{
		TemplateNode->InvalidateCache();
	}

	Notify(ERigVMGraphNotifType::PinTypeChanged, InPin);
	Notify(ERigVMGraphNotifType::PinDefaultValueChanged, InPin);

	// let's see if this was the last resolved wildcard pin
	if(RemainingWildCardPins > 0)
	{
		// compute the number of current wildcard pins
		RemainingWildCardPins = 0;
		if(InPin->GetNode()->IsA<URigVMTemplateNode>())
		{
			AllPins = InPin->GetNode()->GetAllPinsRecursively();
			RemainingWildCardPins = Algo::CountIf(AllPins, WildCardPinCountPredicate);
		}

		// if this is the first time that there are no wild card pins left
		if(RemainingWildCardPins == 0)
		{
			struct Local
			{
				static bool IsPinDefaultEmpty(URigVMPin* InPin)
				{
					const FString DefaultValue = InPin->GetDefaultValue();
					static const FString EmptyBraces = TEXT("()");
					return DefaultValue.IsEmpty() || DefaultValue == EmptyBraces;
				}
				
				static void ApplyResolvedDefaultValue(
					URigVMController* InController, 
					URigVMPin* InPin, 
					const FString& RemainingPinPath, 
					const FString& InDefaultValue, 
					bool bSetupUndoRedo)
				{
					if(InDefaultValue.IsEmpty())
					{
						return;
					}
					
					if(RemainingPinPath.IsEmpty())
					{
						InController->SetPinDefaultValue(InPin, InDefaultValue, true, bSetupUndoRedo, false);
						return;
					}

					FString PinName;
					FString SubPinPath;
					if(!URigVMPin::SplitPinPathAtStart(RemainingPinPath, PinName, SubPinPath))
					{
						PinName = RemainingPinPath;
						SubPinPath.Empty();
					}

					TArray<FString> MemberValuePairs = URigVMPin::SplitDefaultValue(InDefaultValue);
					for (const FString& MemberValuePair : MemberValuePairs)
					{
						FString MemberName, MemberValue;
						if (MemberValuePair.Split(TEXT("="), &MemberName, &MemberValue))
						{
							if(MemberName.Equals(PinName))
							{
								PostProcessDefaultValue(InPin, MemberValue);
								ApplyResolvedDefaultValue(InController, InPin, SubPinPath, MemberValue, bSetupUndoRedo);
								break;
							}
						}
					}
				}
			};
			
			for(URigVMPin* Pin : AllPins)
			{
				// skip struct pins or array pins
				if(Pin->GetSubPins().Num() > 0)
				{
					continue;
				}
				
				if(!Local::IsPinDefaultEmpty(Pin))
				{
					continue;
				}

				if(URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(Pin->GetNode()))
				{
					if(UnitNode->GetScriptStruct())
					{
						TSharedPtr<FStructOnScope> StructOnScope = UnitNode->ConstructStructInstance(true);
						const FString StructDefaultValue = FRigVMStruct::ExportToFullyQualifiedText(UnitNode->GetScriptStruct(), StructOnScope->GetStructMemory());
						Local::ApplyResolvedDefaultValue(this, Pin, Pin->GetSegmentPath(true), StructDefaultValue, bSetupUndoRedo);
						if(!Local::IsPinDefaultEmpty(Pin))
						{
							continue;
						}
					}
				}

				// create the default value for the parent struct pin
				if(Pin->IsStructMember())
				{
					const URigVMPin* ParentPin = Pin->GetParentPin();
					TSharedPtr<FStructOnScope> StructOnScope = MakeShareable(new FStructOnScope(ParentPin->GetScriptStruct()));
					const FString StructDefaultValue = FRigVMStruct::ExportToFullyQualifiedText(ParentPin->GetScriptStruct(), StructOnScope->GetStructMemory());
					Local::ApplyResolvedDefaultValue(this, Pin, Pin->GetName(), StructDefaultValue, bSetupUndoRedo);
				}
				else
				{
					// plain types within an array or at the root
					FString SimpleTypeDefaultValue;
					if(Pin->GetCPPType() == RigVMTypeUtils::BoolType)
					{
						static const FString BoolDefaultValue = TEXT("False");
						SimpleTypeDefaultValue = BoolDefaultValue;
					}
					else if(Pin->GetCPPType() == RigVMTypeUtils::FloatType || Pin->GetCPPType() == RigVMTypeUtils::DoubleType)
					{
						static const FString FloatingPointDefaultValue = TEXT("0.000000");
						SimpleTypeDefaultValue = FloatingPointDefaultValue;
					}
					else if(Pin->GetCPPType() == RigVMTypeUtils::Int32Type)
					{
						static const FString IntegerDefaultValue = TEXT("0");
						SimpleTypeDefaultValue = IntegerDefaultValue;
					}
					Local::ApplyResolvedDefaultValue(this, Pin, FString(), SimpleTypeDefaultValue, bSetupUndoRedo);
				}
			}

			if (URigVMTemplateNode* TemplateNode = Cast<URigVMTemplateNode>(InPin->GetNode()))
			{
				if (!TemplateNode->IsA<URigVMFunctionEntryNode>() && !TemplateNode->IsA<URigVMFunctionReturnNode>())
				{
					// Figure out the permutation from the pin types. During undo, the filtered permutations are not
					// reliable as to which permutation we are resolving to.
					FullyResolveTemplateNode(TemplateNode, INDEX_NONE, bSetupUndoRedo);
				}
			}
		}
	}

	// since the resolved pin may affect the node title we need to let
	// graph views know to invalidate the node title text widget
	Notify(ERigVMGraphNotifType::NodeDescriptionChanged, InPin->GetNode());

	// in cases where we are just changing the type we have to let the
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
		ReattachLinksToPinObjects(false, &Links, true);
		RemoveUnusedOrphanedPins(InPin->GetNode());
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

			BreakLink(Link->GetSourcePin(), Link->GetTargetPin(), bSetupUndoRedo);
			AddLink(Link->GetSourcePin(), NewPin, bSetupUndoRedo);
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

			BreakLink(Link->GetSourcePin(), Link->GetTargetPin(), bSetupUndoRedo);
			AddLink(NewPin, Link->GetTargetPin(), bSetupUndoRedo);
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
	InObjectToDestroy->MarkAsGarbage();
}

URigVMPin* URigVMController::MakeExecutePin(URigVMNode* InNode, const FName& InName)
{
	URigVMPin* ExecutePin = NewObject<URigVMPin>(InNode, InName);
	ExecutePin->DisplayName = FRigVMStruct::ExecuteName;
	MakeExecutePin(ExecutePin);
	return ExecutePin;
}

void URigVMController::MakeExecutePin(URigVMPin* InOutPin)
{
	if(InOutPin->CPPTypeObject != FRigVMExecuteContext::StaticStruct())
	{
		const bool bIsArray = InOutPin->IsArray();
		InOutPin->CPPType = FRigVMExecuteContext::StaticStruct()->GetStructCPPName();
		InOutPin->CPPTypeObject = FRigVMExecuteContext::StaticStruct();
		InOutPin->CPPTypeObjectPath = *InOutPin->CPPTypeObject->GetPathName();

		if(bIsArray)
		{
			InOutPin->CPPType = RigVMTypeUtils::ArrayTypeFromBaseType(InOutPin->CPPType);
			InOutPin->LastKnownTypeIndex = FRigVMRegistry::Get().GetArrayTypeFromBaseTypeIndex(RigVMTypeUtils::TypeIndex::Execute);
		}
		else
		{
			InOutPin->LastKnownTypeIndex = RigVMTypeUtils::TypeIndex::Execute;
		}
		InOutPin->LastKnownCPPType = InOutPin->CPPType;
	}
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

bool URigVMController::EnsurePinValidity(URigVMPin* InPin, bool bRecursive)
{
	check(InPin);
	
	// check if the CPPTypeObject is set up correctly.
	if(RigVMTypeUtils::RequiresCPPTypeObject(InPin->GetCPPType()))
	{
		// GetCPPTypeObject attempts to update pin type information to the latest
		// without testing for redirector
		if(InPin->GetCPPTypeObject() == nullptr)
		{
			FString CPPType = InPin->GetCPPType();
			InPin->CPPTypeObject = RigVMTypeUtils::ObjectFromCPPType(CPPType);
			InPin->CPPType = CPPType;
		}
		else
		{
			InPin->CPPType = RigVMTypeUtils::PostProcessCPPType(InPin->CPPType, InPin->GetCPPTypeObject());
		}
	}

	if (InPin->GetCPPType().IsEmpty() || InPin->GetCPPType() == FName().ToString())
	{
		return false;
	}
	
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

	if(InPin->IsExecuteContext())
	{
		ensure(InPin->GetCPPTypeObject() == FRigVMExecuteContext::StaticStruct());
	}
}

void URigVMController::EnsureLocalVariableValidity()
{
	if (URigVMGraph* Graph = GetGraph())
	{
		for (FRigVMGraphVariableDescription& Variable : Graph->LocalVariables)
		{
			// CPPType can become invalid when the type object is defined by
			// an asset that have changed name or asset path, user defined struct is one possibility
			Variable.CPPType = RigVMTypeUtils::PostProcessCPPType(Variable.CPPType, Variable.CPPTypeObject);
		}
	}
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
			RepopulatePinsOnNode(ReferenceNode, false, false, true);
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

TArray<TPair<FString, FString>> URigVMController::GetLinkedPinPaths(URigVMNode* InNode, bool bIncludeInjectionNodes)
{
	const TArray<URigVMNode*> Nodes = {InNode};
	return GetLinkedPinPaths(Nodes, bIncludeInjectionNodes);
}

TArray<TPair<FString, FString>> URigVMController::GetLinkedPinPaths(const TArray<URigVMNode*>& InNodes, bool bIncludeInjectionNodes)
{
	TArray<TPair<FString, FString>> LinkedPaths;
	for(URigVMNode* Node : InNodes)
	{
		TArray<URigVMLink*> Links = Node->GetLinks();
		for(URigVMLink* Link : Links)
		{
			if(!bIncludeInjectionNodes)
			{
				if(Link->GetSourcePin()->GetNode()->IsInjected() ||
					Link->GetTargetPin()->GetNode()->IsInjected())
				{
					continue;
				}
			}
			TPair<FString, FString> LinkedPath(Link->GetSourcePin()->GetPinPath(), Link->GetTargetPin()->GetPinPath());
			LinkedPaths.AddUnique(LinkedPath);
		}
	}
	return LinkedPaths;
}

bool URigVMController::BreakLinkedPaths(const TArray<TPair<FString, FString>>& InLinkedPaths, bool bSetupUndoRedo)
{
	for(const TPair<FString, FString>& LinkedPath : InLinkedPaths)
	{
		if(!BreakLink(LinkedPath.Key, LinkedPath.Value, bSetupUndoRedo))
		{
			ReportErrorf(TEXT("Couldn't remove link '%s' -> '%s'"), *LinkedPath.Key, *LinkedPath.Value);
			return false;
		}
	}

	return true;
}

bool URigVMController::RestoreLinkedPaths(
	const TArray<TPair<FString, FString>>& InLinkedPaths,
	const TMap<FString, FString>& InNodeNameMap,
	const TMap<FString,FRigVMController_PinPathRemapDelegate>& InRemapDelegates,
	FRigVMController_CheckPinComatibilityDelegate InCompatibilityDelegate,
	bool bSetupUndoRedo,
	ERigVMPinDirection InUserDirection)
{
	bool bSuccess = true;

	auto RemapNodeName = [InNodeNameMap, InRemapDelegates](const FString& InPinPath, bool bAsInput) -> FString
	{
		FString NodeName, SegmentPath;
		if(!URigVMPin::SplitPinPathAtStart(InPinPath, NodeName, SegmentPath))
		{
			return InPinPath;
		}

		FString PinPath = InPinPath;

		if(const FRigVMController_PinPathRemapDelegate* RemapDelegate = InRemapDelegates.Find(NodeName))
		{
			PinPath = RemapDelegate->Execute(PinPath, bAsInput);
		}
		else if(const FString* RemappedNodeName = InNodeNameMap.Find(NodeName))
		{
			PinPath = URigVMPin::JoinPinPath(*RemappedNodeName, SegmentPath);
		}

		return PinPath;
	};
	
	for(const TPair<FString, FString>& LinkedPath : InLinkedPaths)
	{
		const FString SourcePath = RemapNodeName(LinkedPath.Key, false);
		const FString TargetPath = RemapNodeName(LinkedPath.Value, true);

		URigVMPin* SourcePin = GetGraph()->FindPin(SourcePath);
		URigVMPin* TargetPin = GetGraph()->FindPin(TargetPath);

		if(SourcePin == nullptr || TargetPin == nullptr)
		{
			ReportRemovedLink(SourcePath, TargetPath);
			bSuccess = false;
			continue;
		}

		if(InCompatibilityDelegate.IsBound())
		{
			if(!InCompatibilityDelegate.Execute(SourcePin, TargetPin))
			{
				bSuccess = false;
				continue;
			}
		}

		// it's ok if this fails - we want to maintain the minimum set of links
		if(!AddLink(SourcePin, TargetPin, bSetupUndoRedo, InUserDirection))
		{
			ReportRemovedLink(SourcePath, TargetPath);
			bSuccess = false;
		}
	}
	
	return bSuccess;
}

#if WITH_EDITOR

void URigVMController::RegisterUseOfTemplate(const URigVMTemplateNode* InNode)
{
	check(InNode);

	if(!bRegisterTemplateNodeUsage)
	{
		return;
	}

	const FRigVMTemplate* Template = InNode->GetTemplate();
	if(Template == nullptr)
	{
		return;
	}

	if(!InNode->IsResolved())
	{
		return;
	}

	const int32& ResolvedPermutation = InNode->ResolvedPermutation;
	if(!ensure(ResolvedPermutation != INDEX_NONE))
	{
		return;
	}

	URigVMControllerSettings* Settings = GetMutableDefault<URigVMControllerSettings>();
	Settings->Modify();

	const FName& Notation = Template->GetNotation();
	FRigVMController_CommonTypePerTemplate& TypesForTemplate = Settings->TemplateDefaultTypes.FindOrAdd(Notation);

	const FString TypesString = FRigVMTemplate::GetStringFromArgumentTypes(Template->GetTypesForPermutation(ResolvedPermutation));
	int32& Count = TypesForTemplate.Counts.FindOrAdd(TypesString);
	Count++;
}

FRigVMTemplate::FTypeMap URigVMController::GetCommonlyUsedTypesForTemplate(
	const URigVMTemplateNode* InNode) const
{
	static FRigVMTemplate::FTypeMap EmptyTypes;

	const URigVMControllerSettings* Settings = GetDefault<URigVMControllerSettings>();
	if(!Settings->bAutoResolveTemplateNodesWhenLinkingExecute)
	{
		return EmptyTypes;
	}
	
	const FRigVMTemplate* Template = InNode->GetTemplate();
	if(Template == nullptr)
	{
		return EmptyTypes;
	}

	const FName& Notation = Template->GetNotation();

	const FRigVMController_CommonTypePerTemplate* TypesForTemplate = Settings->TemplateDefaultTypes.Find(Notation);
	if(TypesForTemplate == nullptr)
	{
		return EmptyTypes;
	}

	if(TypesForTemplate->Counts.IsEmpty())
	{
		return EmptyTypes;
	}

	TPair<FString,int32> MaxPair;
	for(const TPair<FString,int32>& Pair : TypesForTemplate->Counts)
	{
		if(Pair.Value > MaxPair.Value)
		{
			MaxPair = Pair;
		}
	}

	const FString& TypesString = MaxPair.Key;
	return Template->GetArgumentTypesFromString(TypesString);
}

FRigVMClientPatchResult URigVMController::PatchUnitNodesOnLoad()
{
	FRigVMClientPatchResult Result;
	
	if (const URigVMGraph* Graph = GetGraph())
	{
		TArray<URigVMUnitNode*> UnitNodesToTurnToDispatches;
		
		// check for unit nodes that should be dispatches
		for(URigVMNode* Node : Graph->GetNodes())
		{
			if(URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(Node))
			{
				if(const FRigVMTemplate* Template = UnitNode->GetTemplate())
				{
					if(Template->GetDispatchFactory() != nullptr)
					{
						UnitNodesToTurnToDispatches.Add(UnitNode);
					}
				}
			}
		}

		// convert unit nodes to dispatches
		for(URigVMUnitNode* UnitNode : UnitNodesToTurnToDispatches)
		{
			TArray<TPair<FString, FString>> LinkedPaths = GetLinkedPinPaths(UnitNode);
			const FVector2D NodePosition = UnitNode->GetPosition();
			const FString NodeName = UnitNode->GetName();
			TMap<FString, FPinState> PinStates = GetPinStates(UnitNode, false);
			
			FRigVMTemplate::FTypeMap TypeMap;
			for(URigVMPin* Pin : UnitNode->GetPins())
			{
				if(Pin->GetDirection() == ERigVMPinDirection::Input ||
					Pin->GetDirection() == ERigVMPinDirection::Visible ||
					Pin->GetDirection() == ERigVMPinDirection::IO ||
					Pin->GetDirection() == ERigVMPinDirection::Output)
				{
					TypeMap.Add(Pin->GetFName(), Pin->GetTypeIndex());
				}
			}

			const FRigVMTemplate* Template = UnitNode->GetTemplate();

			Result.RemovedNodes.Add(UnitNode->GetPathName());
			Result.bChangedContent = true;

			RemoveNode(UnitNode, false, false, false, false);

			URigVMTemplateNode* NewNode = AddTemplateNode(
				Template->GetNotation(),
				NodePosition, 
				NodeName, 
				false, 
				false);

			Result.AddedNodes.Add(NewNode);

			TArray<int32> Permutations;
			Template->Resolve(TypeMap, Permutations, false);

			for(URigVMPin* Pin : NewNode->GetPins())
			{
				if(Pin->IsWildCard())
				{
					if(const TRigVMTypeIndex* ResolvedTypeIndex = TypeMap.Find(Pin->GetFName()))
					{
						if(!FRigVMRegistry::Get().IsWildCardType(*ResolvedTypeIndex))
						{
							ChangePinType(Pin, *ResolvedTypeIndex, false, false);
						}
					}
				}
			}

			ApplyPinStates(NewNode, PinStates, {}, false);
			RestoreLinkedPaths(LinkedPaths, {}, {}, false);
		}
	}

	return Result;
}

FRigVMClientPatchResult URigVMController::PatchDispatchNodesOnLoad()
{
	FRigVMClientPatchResult Result;

	if (const URigVMGraph* Graph = GetGraph())
	{
		for(URigVMNode* Node : Graph->GetNodes())
		{
			if(URigVMDispatchNode* DispatchNode = Cast<URigVMDispatchNode>(Node))
			{
				// find template performs backwards lookup
				if(const FRigVMTemplate* Template = DispatchNode->GetTemplate())
				{
					if(Template->GetNotation() != DispatchNode->TemplateNotation)
					{
						Result.bChangedContent = Result.bChangedContent || DispatchNode->TemplateNotation != Template->GetNotation(); 
						DispatchNode->TemplateNotation = Template->GetNotation();
						
						if(!DispatchNode->ResolvedFunctionName.IsEmpty())
						{
							FString FactoryName, ArgumentsString;
							if(DispatchNode->ResolvedFunctionName.Split(TEXT("::"), &FactoryName, &ArgumentsString))
							{
								DispatchNode->ResolvedFunctionName.Reset();
								DispatchNode->ResolvedPermutation = INDEX_NONE;
								
								const FRigVMTemplateTypeMap ArgumentTypes = Template->GetArgumentTypesFromString(ArgumentsString);
								if(ArgumentTypes.Num() == Template->NumArguments())
								{
									if(const FRigVMDispatchFactory* Factory = Template->GetDispatchFactory())
									{
										const FString ResolvedPermutationName = Factory->GetPermutationName(ArgumentTypes);
										if(const FRigVMFunction* Function = FRigVMRegistry::Get().FindFunction(*ResolvedPermutationName))
										{
											DispatchNode->ResolvedFunctionName = Function->GetName();
											DispatchNode->ResolvedPermutation = Template->FindPermutation(Function);
											Result.bChangedContent = true;
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	return Result;
}

FRigVMClientPatchResult URigVMController::PatchBranchNodesOnLoad()
{
	FRigVMClientPatchResult Result;

	if (const URigVMGraph* Graph = GetGraph())
	{
		TArray<URigVMNode*> BranchNodes = Graph->GetNodes().FilterByPredicate([](URigVMNode* Node)
		{
			return Node->IsA<UDEPRECATED_RigVMBranchNode>();
		});

		for(URigVMNode* BranchNode : BranchNodes)
		{
			TArray<TPair<FString, FString>> LinkedPaths = GetLinkedPinPaths(BranchNode);
			const FVector2D NodePosition = BranchNode->GetPosition();
			const FString NodeName = BranchNode->GetName();
			const URigVMPin* OldConditionPin = BranchNode->FindPin(GET_MEMBER_NAME_CHECKED(FRigVMFunction_ControlFlowBranch, Condition).ToString());
			const FString ConditionDefault = GetPinDefaultValue(OldConditionPin->GetPinPath());

			Result.RemovedNodes.Add(BranchNode->GetPathName());
			Result.bChangedContent = true;

			RemoveNode(BranchNode, false, true, false, false);

			const URigVMNode* NewNode = AddUnitNode(FRigVMFunction_ControlFlowBranch::StaticStruct(), FRigVMStruct::ExecuteName, NodePosition, NodeName, false, false);

			Result.AddedNodes.Add(NewNode);

			if(!ConditionDefault.IsEmpty())
			{
				const URigVMPin* ConditionPin = NewNode->FindPin(GET_MEMBER_NAME_CHECKED(FRigVMFunction_ControlFlowBranch, Condition).ToString());
				SetPinDefaultValue(ConditionPin->GetPinPath(), ConditionDefault, false, false, false, false);
			}
			RestoreLinkedPaths(LinkedPaths, {}, {}, false);
		}
	}
	
	return Result;
}

FRigVMClientPatchResult URigVMController::PatchIfSelectNodesOnLoad()
{
	FRigVMClientPatchResult Result;
	
	if (const URigVMGraph* Graph = GetGraph())
	{
		TArray<URigVMNode*> IfOrSelectNodes = Graph->GetNodes().FilterByPredicate([](URigVMNode* Node)
		{
			return Node->IsA<UDEPRECATED_RigVMIfNode>() ||
				Node->IsA<UDEPRECATED_RigVMSelectNode>();
		});

		for(URigVMNode* IfOrSelectNode : IfOrSelectNodes)
		{
			const bool bIsIfNode = IfOrSelectNode->IsA<UDEPRECATED_RigVMIfNode>();
			TArray<TPair<FString, FString>> LinkedPaths = GetLinkedPinPaths(IfOrSelectNode);
			const FVector2D NodePosition = IfOrSelectNode->GetPosition();
			const FString NodeName = IfOrSelectNode->GetName();
			const TRigVMTypeIndex TypeIndex = IfOrSelectNode->GetPins().Last()->GetTypeIndex();
			TMap<FString, FPinState> PinStates = GetPinStates(IfOrSelectNode, true);

			Result.RemovedNodes.Add(IfOrSelectNode->GetPathName());
			Result.bChangedContent = true;

			RemoveNode(IfOrSelectNode, false, true, false, false);

			const FRigVMDispatchFactory* Factory = FRigVMRegistry::Get().FindOrAddDispatchFactory(
				bIsIfNode ? FRigVMDispatch_If::StaticStruct() : FRigVMDispatch_SelectInt32::StaticStruct());

			const FRigVMTemplate* Template = Factory->GetTemplate();
			URigVMTemplateNode* NewNode = AddTemplateNode(
				Template->GetNotation(),
				NodePosition, 
				NodeName, 
				false, 
				false);

			Result.AddedNodes.Add(NewNode);

			if(!FRigVMRegistry::Get().IsWildCardType(TypeIndex))
			{
				TArray<int32> Permutations;
				FRigVMTemplateTypeMap Types;
				Types.Add(IfOrSelectNode->GetPins().Last()->GetFName(), TypeIndex);
				Template->Resolve(Types, Permutations, false);

				for(URigVMPin* Pin : NewNode->GetPins())
				{
					if(Pin->IsWildCard())
					{
						if(const TRigVMTypeIndex* ResolvedTypeIndex = Types.Find(Pin->GetFName()))
						{
							if(!FRigVMRegistry::Get().IsWildCardType(*ResolvedTypeIndex))
							{
								ResolveWildCardPin(Pin, *ResolvedTypeIndex, false);
							}
						}
					}
				}
			}

			ApplyPinStates(NewNode, PinStates, {}, false);
			RestoreLinkedPaths(LinkedPaths, {}, {}, false);
		}
	}

	return Result;
}

FRigVMClientPatchResult URigVMController::PatchArrayNodesOnLoad()
{
	FRigVMClientPatchResult Result;
	
	if (const URigVMGraph* Graph = GetGraph())
	{
		TArray<URigVMNode*> ArrayNodes = Graph->GetNodes().FilterByPredicate([](URigVMNode* Node)
		{
			return Node->IsA<UDEPRECATED_RigVMArrayNode>();
		});

		for(URigVMNode* ModelNode : ArrayNodes)
		{
			UDEPRECATED_RigVMArrayNode* ArrayNode = CastChecked<UDEPRECATED_RigVMArrayNode>(ModelNode);
			TArray<TPair<FString, FString>> LinkedPaths = GetLinkedPinPaths(ArrayNode);
			const FVector2D NodePosition = ArrayNode->GetPosition();
			const FString NodeName = ArrayNode->GetName();
			const FString CPPType = ArrayNode->GetCPPType();
			UObject* CPPTypeObject = ArrayNode->GetCPPTypeObject();
			const ERigVMOpCode OpCode = ArrayNode->GetOpCode();
			TMap<FString, FPinState> PinStates = GetPinStates(ArrayNode, true);

			Result.RemovedNodes.Add(ArrayNode->GetPathName());
			Result.bChangedContent = true;

			RemoveNode(ArrayNode, false, false, false, false);

			URigVMNode* NewNode = AddArrayNode(OpCode, CPPType, CPPTypeObject, NodePosition, NodeName, false, false, true);

			Result.AddedNodes.Add(NewNode);

			ApplyPinStates(NewNode, PinStates, {}, false);
			RestoreLinkedPaths(LinkedPaths, {}, {}, false);
		}
	}

	return Result;
}

FRigVMClientPatchResult URigVMController::PatchInvalidLinksOnWildcards()
{
	FRigVMClientPatchResult Result;
	
	if (const URigVMGraph* Graph = GetGraph())
	{
		// Remove links between wildcard pins
		TArray<URigVMLink*> LinksToRemove;
		for (URigVMLink* Link : Graph->GetLinks())
		{
			bool bRemove = false;
			if (URigVMPin* SourcePin = Link->GetSourcePin())
			{
				if (SourcePin->IsWildCard())
				{
					bRemove = true;
				}
			}
			if (URigVMPin* TargetPin = Link->GetTargetPin())
			{
				if (TargetPin->IsWildCard())
				{
					bRemove = true;
				}
			}
			if (bRemove)
			{
				LinksToRemove.Add(Link);
			}
		}
		if (!LinksToRemove.IsEmpty())
		{
			Result.bChangedContent = true;
		}
		for (URigVMLink* Link : LinksToRemove)
		{
			if (!BreakLink(Link->GetSourcePin(), Link->GetTargetPin()))
			{
				Result.ErrorMessages.Add(FString::Printf(TEXT("Error breaking link %s in PatchInvalidLinksOnWildcards"), *Link->GetPinPathRepresentation()));
				Result.bSucceeded = false;
			}
		}

		// Remove exposed pins of type wildcard
		if (URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(Graph->GetOuter()))
		{
			TArray<URigVMPin*> ExposedPins = CollapseNode->GetPins();
			for (URigVMPin* ExposedPin : ExposedPins)
			{
				if (ExposedPin->IsWildCard())
				{
					FRigVMControllerGraphGuard GraphGuard(this, CollapseNode->GetContainedGraph());
					if (!RemoveExposedPin(ExposedPin->GetFName(), false))
					{
						Result.ErrorMessages.Add(FString::Printf(TEXT("Error removing exposed pin %s PatchInvalidLinksOnWildcards"), *ExposedPin->GetPinPath(true)));
						Result.bSucceeded = false;
					}
				}
			}
		}
	}

	return Result;
}

void URigVMController::PostDuplicateHost(const FString& InOldPathName, const FString& InNewPathName)
{
	if (const URigVMGraph* Graph = GetGraph())
	{
		TArray<URigVMNode*> FunctionRefNodes = Graph->GetNodes().FilterByPredicate([](URigVMNode* Node)
		{
			return Node->IsA<URigVMFunctionReferenceNode>();
		});

		for(URigVMNode* Node : FunctionRefNodes)
		{
			URigVMFunctionReferenceNode* FunctionReferenceNode = CastChecked<URigVMFunctionReferenceNode>(Node);
			FunctionReferenceNode->ReferencedFunctionHeader.PostDuplicateHost(InOldPathName, InNewPathName);
		}
	}
}

#endif

URigVMControllerSettings::URigVMControllerSettings(const FObjectInitializer& Initializer)
	: Super(Initializer)
{
	bAutoResolveTemplateNodesWhenLinkingExecute = true;
}

