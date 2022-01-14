// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCompiler/RigVMCompiler.h"
#include "RigVMModel/RigVMController.h"
#include "RigVMCore/RigVMExecuteContext.h"
#include "RigVMDeveloperModule.h"
#include "UObject/PropertyPortFlags.h"
#include "Stats/StatsHierarchical.h"
#include "RigVMTypeUtils.h"

class FRigVMCompilerImportErrorContext : public FOutputDevice
{
public:

	URigVMCompiler* Compiler;
	int32 NumErrors;

	FRigVMCompilerImportErrorContext(URigVMCompiler* InCompiler)
		: FOutputDevice()
		, Compiler(InCompiler)
		, NumErrors(0)
	{
	}

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category) override
	{
		switch (Verbosity)
		{
		case ELogVerbosity::Error:
		case ELogVerbosity::Fatal:
		{
			Compiler->ReportError(V);
			break;
		}
		case ELogVerbosity::Warning:
		{
			Compiler->ReportWarning(V);
			break;
		}
		default:
		{
			Compiler->ReportInfo(V);
			break;
		}
		}
		NumErrors++;
	}
};

FRigVMCompileSettings::FRigVMCompileSettings()
	: SurpressInfoMessages(true)
	, SurpressWarnings(false)
	, SurpressErrors(false)
	, EnablePinWatches(true)
	, IsPreprocessorPhase(false)
	, ASTSettings(FRigVMParserASTSettings::Optimized())
	, SetupNodeInstructionIndex(true)
{
}

#if !UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED

FRigVMOperand FRigVMCompilerWorkData::AddProperty(
	ERigVMMemoryType InMemoryType,
	const FName& InName,
	const FString& InCPPType,
	UObject* InCPPTypeObject,
	const FString& InDefaultValue)
{
	check(bSetupMemory);
	
	FRigVMPropertyDescription Description(InName, InCPPType, InCPPTypeObject, InDefaultValue);

	TArray<FRigVMPropertyDescription>& PropertyArray = PropertyDescriptions.FindOrAdd(InMemoryType);
	const int32 PropertyIndex = PropertyArray.Add(Description);

	return FRigVMOperand(InMemoryType, PropertyIndex);
}

FRigVMOperand FRigVMCompilerWorkData::FindProperty(ERigVMMemoryType InMemoryType, const FName& InName)
{
	TArray<FRigVMPropertyDescription>* PropertyArray = PropertyDescriptions.Find(InMemoryType);
	if(PropertyArray)
	{
		for(int32 Index=0;Index<PropertyArray->Num();Index++)
		{
			if(PropertyArray->operator[](Index).Name == InName)
			{
				return FRigVMOperand(InMemoryType, Index);
			}
		}
	}
	return FRigVMOperand();
}

FRigVMPropertyDescription FRigVMCompilerWorkData::GetProperty(const FRigVMOperand& InOperand)
{
	TArray<FRigVMPropertyDescription>* PropertyArray = PropertyDescriptions.Find(InOperand.GetMemoryType());
	if(PropertyArray)
	{
		if(PropertyArray->IsValidIndex(InOperand.GetRegisterIndex()))
		{
			return PropertyArray->operator[](InOperand.GetRegisterIndex());
		}
	}
	return FRigVMPropertyDescription();
}

int32 FRigVMCompilerWorkData::FindOrAddPropertyPath(const FRigVMOperand& InOperand, const FString& InHeadCPPType, const FString& InSegmentPath)
{
	if(InSegmentPath.IsEmpty())
	{
		return INDEX_NONE;
	}

	TArray<FRigVMPropertyPathDescription>& Descriptions = PropertyPathDescriptions.FindOrAdd(InOperand.GetMemoryType());
	for(int32 Index = 0; Index < Descriptions.Num(); Index++)
	{
		const FRigVMPropertyPathDescription& Description = Descriptions[Index]; 
		if(Description.HeadCPPType == InHeadCPPType && Description.SegmentPath == InSegmentPath)
		{
			return Index;
		}
	}
	return Descriptions.Add(FRigVMPropertyPathDescription(InOperand.GetRegisterIndex(), InHeadCPPType, InSegmentPath));
}

#endif

URigVMCompiler::URigVMCompiler()
{
}

bool URigVMCompiler::Compile(URigVMGraph* InGraph, URigVMController* InController, URigVM* OutVM, const TArray<FRigVMExternalVariable>& InExternalVariables, const TArray<FRigVMUserDataArray>& InRigVMUserData, TMap<FString, FRigVMOperand>* OutOperands, TSharedPtr<FRigVMParserAST> InAST)
{
	if (InGraph == nullptr)
	{
		ReportError(TEXT("Provided graph is nullptr."));
		return false;
	}
	if (OutVM == nullptr)
	{
		ReportError(TEXT("Provided vm is nullptr."));
		return false;
	}

	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	TArray<FRigVMUserDataArray> UserData = InRigVMUserData;
	if (UserData.Num() == 0)
	{
		UserData.Add(FRigVMUserDataArray());
	}

	OutVM->Reset();

	TMap<FString, FRigVMOperand> LocalOperands;
	if (OutOperands == nullptr)
	{
		OutOperands = &LocalOperands;
	}
	OutOperands->Reset();

#if WITH_EDITOR

	// traverse all graphs and try to clear out orphan pins
	// also check on function references with unmapped variables
	TArray<URigVMGraph*> VisitedGraphs;
	VisitedGraphs.Add(InGraph);

	bool bEncounteredGraphError = false;
	for(int32 GraphIndex=0; GraphIndex<VisitedGraphs.Num(); GraphIndex++)
	{
		URigVMGraph* VisitedGraph = VisitedGraphs[GraphIndex];
		
		{
			FRigVMControllerGraphGuard Guard(InController, VisitedGraph, false);
			// make sure variables are up to date before validating other things.
			// that is, make sure their cpp type and type object agree with each other
			InController->EnsureLocalVariableValidity();
		}
		
		for(URigVMNode* ModelNode : VisitedGraph->GetNodes())
		{
			FRigVMControllerGraphGuard Guard(InController, VisitedGraph, false);

			// make sure pins are up to date before validating other things.
			// that is, make sure their cpp type and type object agree with each other
			for(URigVMPin* Pin : ModelNode->Pins)
			{
				if(!URigVMController::EnsurePinValidity(Pin, true))
				{
					return false;
				}
			}
			
			if(!InController->RemoveUnusedOrphanedPins(ModelNode, true))
			{
				static const FString LinkedMessage = TEXT("Node @@ uses pins that no longer exist. Please rewire the links and re-compile.");
				Settings.ASTSettings.Report(EMessageSeverity::Error, ModelNode, LinkedMessage);
				bEncounteredGraphError = true;
			}

			// avoid function reference related validation for temp assets, a temp asset may get generated during
			// certain content validation process. It is usually just a simple file-level copy of the source asset
			// so these references are usually not fixed-up properly. Thus, it is meaningless to validate them.
			if (!ModelNode->GetPackage()->GetName().StartsWith(TEXT("/Temp/")))
			{
				if(URigVMFunctionReferenceNode* FunctionReferenceNode = Cast<URigVMFunctionReferenceNode>(ModelNode))
				{
					if(!FunctionReferenceNode->IsFullyRemapped())
					{
						static const FString UnmappedMessage = TEXT("Node @@ has unmapped variables. Please adjust the node and re-compile.");
						Settings.ASTSettings.Report(EMessageSeverity::Error, ModelNode, UnmappedMessage);
						bEncounteredGraphError = true;
					}
				}
			}

			if(ModelNode->IsA<URigVMFunctionEntryNode>() || ModelNode->IsA<URigVMFunctionReturnNode>())
			{
				for(URigVMPin* ExecutePin : ModelNode->Pins)
				{
					if(ExecutePin->IsExecuteContext())
					{
						if(ExecutePin->GetLinks().Num() == 0)
						{
							static const FString UnlinkedExecuteMessage = TEXT("Node @@ has an unconnected Execute pin.\nThe function might cause unexpected behavior.");
							Settings.ASTSettings.Report(EMessageSeverity::Error, ModelNode, UnlinkedExecuteMessage);
							bEncounteredGraphError = true;
						}
					}
				}
			}

			if(URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(ModelNode))
			{
				if(URigVMGraph* ContainedGraph = LibraryNode->GetContainedGraph())
				{
					VisitedGraphs.AddUnique(ContainedGraph);
				}
			}

			// for variable let's validate ill formed variable nodes
			if(URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(ModelNode))
			{
				static const FString IllFormedVariableNodeMessage = TEXT("Variable Node @@ is ill-formed (pin type doesn't match the variable type).\nConsider to recreate the node.");

				const FRigVMGraphVariableDescription VariableDescription = VariableNode->GetVariableDescription();
				const TArray<FRigVMGraphVariableDescription> LocalVariables = VisitedGraph->GetLocalVariables(true);

				bool bFoundVariable = false;
				for(const FRigVMGraphVariableDescription& LocalVariable : LocalVariables)
				{
					if(LocalVariable.Name == VariableDescription.Name)
					{
						bFoundVariable = true;
						
						if(LocalVariable.CPPType != VariableDescription.CPPType ||
							LocalVariable.CPPTypeObject != VariableDescription.CPPTypeObject)
						{
							Settings.ASTSettings.Report(EMessageSeverity::Error, ModelNode, IllFormedVariableNodeMessage);
							bEncounteredGraphError = true;
						}
					}
				}

				// if the variable is not a local variable, let's test against the external variables.
				if(!bFoundVariable)
				{
					const FRigVMExternalVariable ExternalVariable = VariableDescription.ToExternalVariable();
					for(const FRigVMExternalVariable& InExternalVariable : InExternalVariables)
					{
						if(InExternalVariable.Name == ExternalVariable.Name)
						{
							bFoundVariable = true;
							
							if(InExternalVariable.TypeName != ExternalVariable.TypeName ||
								InExternalVariable.TypeObject != ExternalVariable.TypeObject ||
								InExternalVariable.bIsArray != ExternalVariable.bIsArray)
							{
								Settings.ASTSettings.Report(EMessageSeverity::Error, ModelNode, IllFormedVariableNodeMessage);
								bEncounteredGraphError = true;
							}
						}
					}
				}
			}

			for(URigVMPin* Pin : ModelNode->Pins)
			{
				if(!URigVMController::EnsurePinValidity(Pin, true))
				{
					return false;
				}
			}
		}
	}

	if(bEncounteredGraphError)
	{
		return false;
	}
#endif
	
	OutVM->ClearExternalVariables();
	
	for (const FRigVMExternalVariable& ExternalVariable : InExternalVariables)
	{
#if !UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED
		check(ExternalVariable.Property);
#endif
		FRigVMOperand Operand = OutVM->AddExternalVariable(ExternalVariable);
		FString Hash = FString::Printf(TEXT("Variable::%s"), *ExternalVariable.Name.ToString());
		OutOperands->Add(Hash, Operand);
	}

	TSharedPtr<FRigVMParserAST> AST = InAST;
	if (!AST.IsValid())
	{
		AST = MakeShareable(new FRigVMParserAST(InGraph, InController, Settings.ASTSettings, InExternalVariables, UserData));
		InGraph->RuntimeAST = AST;
#if UE_BUILD_DEBUG
		//UE_LOG(LogRigVMDeveloper, Display, TEXT("%s"), *AST->DumpDot());
#endif
	}
	ensure(AST.IsValid());

	FRigVMCompilerWorkData WorkData;
	WorkData.VM = OutVM;
	WorkData.PinPathToOperand = OutOperands;
	WorkData.RigVMUserData = UserData[0];
	WorkData.bSetupMemory = true;
	WorkData.ProxySources = &AST->SharedOperandPins;

	// tbd: do we need this only when we have no pins?
	//if(!WorkData.WatchedPins.IsEmpty())
	{
		// create the inverse map for the proxies
		WorkData.ProxyTargets.Reserve(WorkData.ProxySources->Num());
		for(const TPair<FRigVMASTProxy,FRigVMASTProxy>& Pair : *WorkData.ProxySources)
		{
			WorkData.ProxyTargets.FindOrAdd(Pair.Value).Add(Pair.Key);
		}
	}

	UE_LOG_RIGVMMEMORY(TEXT("RigVMCompiler: Begin '%s'..."), *InGraph->GetPathName());

#if WITH_EDITOR
	// If in editor, make sure we visit all the graphs to initialize local variables
	// in case the user wants to edit default values
	URigVMFunctionLibrary* FunctionLibrary = InGraph->GetDefaultFunctionLibrary();
	if (FunctionLibrary)
	{
		for (URigVMLibraryNode* LibraryNode : FunctionLibrary->GetFunctions())
		{
			{
				FRigVMControllerGraphGuard Guard(InController, LibraryNode->GetContainedGraph(), false);
				// make sure variables are up to date before validating other things.
				// that is, make sure their cpp type and type object agree with each other
				InController->EnsureLocalVariableValidity();
			}

			for (FRigVMGraphVariableDescription& Variable : LibraryNode->GetContainedGraph()->LocalVariables)
			{
				FString Path = FString::Printf(TEXT("LocalVariableDefault::%s|%s::Const"), *LibraryNode->GetFName().ToString(), *Variable.Name.ToString());
				FRigVMOperand Operand = WorkData.AddProperty(ERigVMMemoryType::Literal, *Path, Variable.CPPType, Variable.CPPTypeObject, Variable.DefaultValue);
				WorkData.PinPathToOperand->Add(Path, Operand);

				for (const FRigVMExternalVariable& ExternalVariable : InExternalVariables)
				{
					if (ExternalVariable.Name == Variable.Name)
					{
						ReportWarningf(TEXT("Blueprint variable %s is being shadowed by a local variable in function %s"), *ExternalVariable.Name.ToString(), *LibraryNode->GetName());
					}
				}
			}
		}
	}
#endif

	// Look for all local variables to create the register with the default value in the literal memory
	int32 IndexLocalVariable = 0;
	for(URigVMGraph* VisitedGraph : VisitedGraphs)
	{
		for (const FRigVMGraphVariableDescription& LocalVariable : VisitedGraph->LocalVariables)
		{
			auto AddDefaultValueOperand = [&](URigVMPin* Pin)
			{
				FRigVMASTProxy PinProxy = FRigVMASTProxy::MakeFromUObject(Pin);
				FRigVMVarExprAST* TempVarExpr = AST->MakeExpr<FRigVMVarExprAST>(FRigVMExprAST::EType::Literal, PinProxy);
				FRigVMOperand Operand = FindOrAddRegister(TempVarExpr, WorkData, false);

#if UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED
				WorkData.VM->GetLiteralMemory().SetRegisterValueFromString(Operand, LocalVariable.CPPType, LocalVariable.CPPTypeObject, {LocalVariable.DefaultValue});
#else
				check(Operand.GetMemoryType() == ERigVMMemoryType::Literal);
				TArray<FRigVMPropertyDescription>& LiteralProperties = WorkData.PropertyDescriptions.FindChecked(Operand.GetMemoryType());
				LiteralProperties[Operand.GetRegisterIndex()].DefaultValue = LocalVariable.DefaultValue;
#endif
			};
			
			// To create the default value in the literal memory, we need to find a pin in a variable node (or bounded to a local variable) that
			// uses this local variable
			for (URigVMNode* Node : VisitedGraph->GetNodes())
			{
				if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
				{
					if (URigVMPin* Pin = VariableNode->FindPin(URigVMVariableNode::VariableName))
					{
						if (Pin->GetDefaultValue() == LocalVariable.Name.ToString())
						{
							URigVMPin* ValuePin = VariableNode->FindPin(URigVMVariableNode::ValueName);
							AddDefaultValueOperand(ValuePin);
							break;
						}
					}
				}
			}
		}
	}	

#if !UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED
	if(Settings.EnablePinWatches)
	{
		for(int32 GraphIndex=0; GraphIndex<VisitedGraphs.Num(); GraphIndex++)
		{
			URigVMGraph* VisitedGraph = VisitedGraphs[GraphIndex];
			for(URigVMNode* ModelNode : VisitedGraph->GetNodes())
			{
				for(URigVMPin* ModelPin : ModelNode->GetPins())
				{
					if(ModelPin->RequiresWatch(true))
					{
						WorkData.WatchedPins.AddUnique(ModelPin);
					}
				}
			}
		}
	}
#endif

	// define all parameters independent from sorted nodes
	for (const FRigVMExprAST* Expr : AST->Expressions)
	{
		if (Expr->IsA(FRigVMExprAST::EType::Literal))
		{
			continue;
		}
		if (Expr->IsA(FRigVMExprAST::EType::Var))
		{
			const FRigVMVarExprAST* VarExpr = Expr->To<FRigVMVarExprAST>();
			{
				if(Cast<URigVMParameterNode>(VarExpr->GetPin()->GetNode()))
				{
					if (VarExpr->GetPin()->GetName() == TEXT("Value"))
					{
						FindOrAddRegister(VarExpr, WorkData, false /* watchvalue */);
					}
				}
			}
		}
	}

	WorkData.ExprComplete.Reset();
	for (FRigVMExprAST* RootExpr : *AST)
	{
		TraverseExpression(RootExpr, WorkData);
	}

	// If a parameter node has no corresponding AST node, because it's not on the execution path,
	// then add a dummy parameter with no register index, so that we can properly add the pins on the 
	// ControlRig node later and not lose any existing external connections.
	for (URigVMNode* Node : InGraph->GetNodes())
	{
		if (URigVMParameterNode* ParameterNode = Cast<URigVMParameterNode>(Node))
		{
			FRigVMASTProxy ParameterNodeProxy = FRigVMASTProxy::MakeFromUObject(ParameterNode);
			const FRigVMExprAST* ParameterExpr = AST->GetExprForSubject(ParameterNodeProxy);
			if (!ParameterExpr || ParameterExpr->IsA(FRigVMExprAST::NoOp))
			{
				FName Name = ParameterNode->GetParameterName();

				if (!WorkData.VM->ParametersNameMap.Contains(Name))
				{
					ERigVMParameterType ParameterType = ParameterNode->IsInput() ? ERigVMParameterType::Input : ERigVMParameterType::Output;
					FRigVMParameter Parameter(ParameterType, Name, INDEX_NONE, ParameterNode->GetCPPType(), nullptr);
					WorkData.VM->ParametersNameMap.Add(Parameter.Name, WorkData.VM->Parameters.Add(Parameter));
				}
			}
		}
	}

#if !UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED

	if(WorkData.WatchedPins.Num() > 0)
	{
		for(int32 GraphIndex=0; GraphIndex<VisitedGraphs.Num(); GraphIndex++)
		{
			URigVMGraph* VisitedGraph = VisitedGraphs[GraphIndex];
			for(URigVMNode* ModelNode : VisitedGraph->GetNodes())
			{
				for(URigVMPin* ModelPin : ModelNode->GetPins())
				{
					if(ModelPin->GetDirection() == ERigVMPinDirection::Input)
					{
						if(ModelPin->GetSourceLinks(true).Num() == 0)
						{
							continue;
						}
					}
					FRigVMASTProxy PinProxy = FRigVMASTProxy::MakeFromUObject(ModelPin);
					FRigVMVarExprAST TempVarExpr(FRigVMExprAST::EType::Var, PinProxy);
					TempVarExpr.ParserPtr = AST.Get();

					FindOrAddRegister(&TempVarExpr, WorkData, true);
				}
			}
		}
	}

	// now that we have determined the needed memory, let's
	// setup properties as needed as well as property paths
	TArray<ERigVMMemoryType> MemoryTypes;
	MemoryTypes.Add(ERigVMMemoryType::Work);
	MemoryTypes.Add(ERigVMMemoryType::Literal);
	MemoryTypes.Add(ERigVMMemoryType::Debug);

	for(ERigVMMemoryType MemoryType : MemoryTypes)
	{
		UPackage* Package = InGraph->GetOutermost();

		TArray<FRigVMPropertyDescription>* Properties = WorkData.PropertyDescriptions.Find(MemoryType);
		if(Properties == nullptr)
		{
			URigVMMemoryStorageGeneratorClass::RemoveStorageClass(Package, MemoryType);
			continue;
		}

		URigVMMemoryStorageGeneratorClass::CreateStorageClass(Package, MemoryType, *Properties);
	}

	WorkData.VM->ClearMemory();

#endif

	WorkData.bSetupMemory = false;
	WorkData.ExprComplete.Reset();
	for (FRigVMExprAST* RootExpr : *AST)
	{
		TraverseExpression(RootExpr, WorkData);
	}

	if (WorkData.VM->GetByteCode().GetInstructions().Num() == 0)
	{
		WorkData.VM->GetByteCode().AddExitOp();
	}

	WorkData.VM->GetByteCode().AlignByteCode();

	// setup debug registers after all other registers have been created
	if(Settings.EnablePinWatches)
	{
#if UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED
		for(int32 GraphIndex=0; GraphIndex<VisitedGraphs.Num(); GraphIndex++)
		{
			URigVMGraph* VisitedGraph = VisitedGraphs[GraphIndex];
			for(URigVMNode* ModelNode : VisitedGraph->GetNodes())
			{
				for(URigVMPin* ModelPin : ModelNode->GetPins())
				{
					if(ModelPin->RequiresWatch(true))
					{
						CreateDebugRegister(ModelPin, WorkData.VM, WorkData.PinPathToOperand, AST);
					}
				}
			}
		}
#else
		for(URigVMPin* WatchedPin : WorkData.WatchedPins)
		{
			MarkDebugWatch(true, WatchedPin, WorkData.VM, WorkData.PinPathToOperand, AST);
		}
#endif
	}

#if !UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED

	// now that we have determined the needed memory, let's
	// update the property paths once more
	for(ERigVMMemoryType MemoryType : MemoryTypes)
	{
		const TArray<FRigVMPropertyPathDescription>* Descriptions = WorkData.PropertyPathDescriptions.Find(MemoryType);
		if(URigVMMemoryStorage* MemoryStorageObject = WorkData.VM->GetMemoryByType(MemoryType))
		{
			if(URigVMMemoryStorageGeneratorClass* Class = Cast<URigVMMemoryStorageGeneratorClass>(MemoryStorageObject->GetClass()))
			{
				if(Descriptions)
				{
					Class->PropertyPathDescriptions = *Descriptions;
				}
				else
				{
					Class->PropertyPathDescriptions.Reset();
				}
				Class->RefreshPropertyPaths();
			}
		}
	}

	if(const TArray<FRigVMPropertyPathDescription>* Descriptions = WorkData.PropertyPathDescriptions.Find(ERigVMMemoryType::External))
	{
		WorkData.VM->ExternalPropertyPathDescriptions = *Descriptions;
	}

#endif

	return true;
}

void URigVMCompiler::TraverseExpression(const FRigVMExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	if (WorkData.ExprToSkip.Contains(InExpr))
	{
		return;
	}

	if (WorkData.ExprComplete.Contains(InExpr))
	{
		return;
	}
	WorkData.ExprComplete.Add(InExpr, true);

	InitializeLocalVariables(InExpr, WorkData);	

	switch (InExpr->GetType())
	{
		case FRigVMExprAST::EType::Block:
		{
			TraverseBlock(InExpr->To<FRigVMBlockExprAST>(), WorkData);
			break;
		}
		case FRigVMExprAST::EType::Entry:
		{
			TraverseEntry(InExpr->To<FRigVMEntryExprAST>(), WorkData);
			break;
		}
		case FRigVMExprAST::EType::CallExtern:
		{
			const FRigVMCallExternExprAST* CallExternExpr = InExpr->To<FRigVMCallExternExprAST>();
			if (URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(CallExternExpr->GetNode()))
			{
				if (UnitNode->IsLoopNode())
				{
					TraverseForLoop(CallExternExpr, WorkData);
					break;
				}
			}

			TraverseCallExtern(CallExternExpr, WorkData);
			break;
		}
		case FRigVMExprAST::EType::NoOp:
		{
			TraverseNoOp(InExpr->To<FRigVMNoOpExprAST>(), WorkData);
			break;
		}
		case FRigVMExprAST::EType::Var:
		{
			TraverseVar(InExpr->To<FRigVMVarExprAST>(), WorkData);
			break;
		}
		case FRigVMExprAST::EType::Literal:
		{
			TraverseLiteral(InExpr->To<FRigVMLiteralExprAST>(), WorkData);
			break;
		}
		case FRigVMExprAST::EType::ExternalVar:
		{
			TraverseExternalVar(InExpr->To<FRigVMExternalVarExprAST>(), WorkData);
			break;
		}
		case FRigVMExprAST::EType::Assign:
		{
			TraverseAssign(InExpr->To<FRigVMAssignExprAST>(), WorkData);
			break;
		}
		case FRigVMExprAST::EType::Copy:
		{
			TraverseCopy(InExpr->To<FRigVMCopyExprAST>(), WorkData);
			break;
		}
		case FRigVMExprAST::EType::CachedValue:
		{
			TraverseCachedValue(InExpr->To<FRigVMCachedValueExprAST>(), WorkData);
			break;
		}
		case FRigVMExprAST::EType::Exit:
		{
			TraverseExit(InExpr->To<FRigVMExitExprAST>(), WorkData);
			break;
		}
		case FRigVMExprAST::EType::Branch:
		{
			TraverseBranch(InExpr->To<FRigVMBranchExprAST>(), WorkData);
			break;
		}
		case FRigVMExprAST::EType::If:
		{
			TraverseIf(InExpr->To<FRigVMIfExprAST>(), WorkData);
			break;
		}
		case FRigVMExprAST::EType::Select:
		{
			TraverseSelect(InExpr->To<FRigVMSelectExprAST>(), WorkData);
			break;
		}
		case FRigVMExprAST::EType::Array:
		{
			TraverseArray(InExpr->To<FRigVMArrayExprAST>(), WorkData);
			break;
		}
		default:
		{
			ensure(false);
			break;
		}
	}
}

void URigVMCompiler::TraverseChildren(const FRigVMExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	for (FRigVMExprAST* ChildExpr : *InExpr)
	{
		TraverseExpression(ChildExpr, WorkData);
	}
}

void URigVMCompiler::TraverseBlock(const FRigVMBlockExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	if (InExpr->IsObsolete())
	{
		return;
	}
	TraverseChildren(InExpr, WorkData);
}

void URigVMCompiler::TraverseEntry(const FRigVMEntryExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(InExpr->GetNode());
	if(!ValidateNode(UnitNode))
	{
		return;
	}

	if (WorkData.bSetupMemory)
	{
		TSharedPtr<FStructOnScope> DefaultStruct = UnitNode->ConstructStructInstance();
		WorkData.DefaultStructs.Add(DefaultStruct);
		TraverseChildren(InExpr, WorkData);
		WorkData.DefaultStructs.Pop();
	}
	else
	{
		// todo: define the entry in the VM
		TArray<FRigVMOperand> Operands;
		for (FRigVMExprAST* ChildExpr : *InExpr)
		{
			if (ChildExpr->IsA(FRigVMExprAST::EType::Var))
			{
				Operands.Add(WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(ChildExpr)));
			}
			else
			{
				break;
			}
		}

		// setup the instruction
		int32 FunctionIndex = WorkData.VM->AddRigVMFunction(UnitNode->GetScriptStruct(), UnitNode->GetMethodName());
		WorkData.VM->GetByteCode().AddExecuteOp(FunctionIndex, Operands);
		
		int32 EntryInstructionIndex = WorkData.VM->GetByteCode().GetNumInstructions() - 1;
		FName Entryname = UnitNode->GetEventName();

		if (WorkData.VM->GetByteCode().FindEntryIndex(Entryname) == INDEX_NONE)
		{
			FRigVMByteCodeEntry Entry;
			Entry.Name = Entryname;
			Entry.InstructionIndex = EntryInstructionIndex;
			WorkData.VM->GetByteCode().Entries.Add(Entry);
		}

		if (Settings.SetupNodeInstructionIndex)
		{
			const FRigVMCallstack Callstack = InExpr->GetProxy().GetCallstack();
			WorkData.VM->GetByteCode().SetSubject(EntryInstructionIndex, Callstack.GetCallPath(), Callstack.GetStack());
		}

		TraverseChildren(InExpr, WorkData);
	}
}

int32 URigVMCompiler::TraverseCallExtern(const FRigVMCallExternExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(InExpr->GetNode());
	if(!ValidateNode(UnitNode))
	{
		return INDEX_NONE;
	}

	int32 InstructionIndex = INDEX_NONE;

	if (WorkData.bSetupMemory)
	{
		TSharedPtr<FStructOnScope> DefaultStruct = UnitNode->ConstructStructInstance();
		WorkData.DefaultStructs.Add(DefaultStruct);
		TraverseChildren(InExpr, WorkData);
		WorkData.DefaultStructs.Pop();
	}
	else
	{
		TArray<FRigVMOperand> Operands;
		for (FRigVMExprAST* ChildExpr : *InExpr)
		{
			if (ChildExpr->GetType() == FRigVMExprAST::EType::CachedValue)
			{
				Operands.Add(WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(ChildExpr->To<FRigVMCachedValueExprAST>()->GetVarExpr())));
			}
			else if (ChildExpr->IsA(FRigVMExprAST::EType::Var))
			{
				Operands.Add(WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(ChildExpr->To<FRigVMVarExprAST>())));
			}
			else
			{
				break;
			}
		}

		TraverseChildren(InExpr, WorkData);

		// setup the instruction
		int32 FunctionIndex = WorkData.VM->AddRigVMFunction(UnitNode->GetScriptStruct(), UnitNode->GetMethodName());
		WorkData.VM->GetByteCode().AddExecuteOp(FunctionIndex, Operands);
		InstructionIndex = WorkData.VM->GetByteCode().GetNumInstructions() - 1;

#if WITH_EDITORONLY_DATA
		TArray<FRigVMOperand> InputsOperands, OutputOperands;

		for(const URigVMPin* InputPin : UnitNode->GetPins())
		{
			if(InputPin->IsExecuteContext())
			{
				continue;
			}

			const FRigVMOperand& Operand = Operands[InputPin->GetPinIndex()];

			if(InputPin->GetDirection() == ERigVMPinDirection::Output || InputPin->GetDirection() == ERigVMPinDirection::IO)
			{
				OutputOperands.Add(Operand);
			}

			if(InputPin->GetDirection() != ERigVMPinDirection::Input && InputPin->GetDirection() != ERigVMPinDirection::IO)
			{
				continue;
			}

			InputsOperands.Add(Operand);
		}

		WorkData.VM->GetByteCode().SetOperandsForInstruction(
			InstructionIndex,
			FRigVMOperandArray(InputsOperands.GetData(), InputsOperands.Num()),
			FRigVMOperandArray(OutputOperands.GetData(), OutputOperands.Num()));

#endif
		
		if (Settings.SetupNodeInstructionIndex)
		{
			const FRigVMCallstack Callstack = InExpr->GetProxy().GetCallstack();
			WorkData.VM->GetByteCode().SetSubject(InstructionIndex, Callstack.GetCallPath(), Callstack.GetStack());
		}
	}

	return InstructionIndex;
}

void URigVMCompiler::TraverseForLoop(const FRigVMCallExternExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	if (WorkData.bSetupMemory)
	{
		TraverseCallExtern(InExpr, WorkData);
		return;
	}

	URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(InExpr->GetNode());
	if(!ValidateNode(UnitNode))
	{
		return;
	}

	const FRigVMCallstack Callstack = InExpr->GetProxy().GetCallstack();

	const FRigVMVarExprAST* CompletedExpr = InExpr->FindVarWithPinName(FRigVMStruct::ForLoopCompletedPinName);
	check(CompletedExpr);
	const FRigVMVarExprAST* ExecuteExpr = InExpr->FindVarWithPinName(FRigVMStruct::ExecuteContextName);
	check(ExecuteExpr);
	WorkData.ExprToSkip.AddUnique(CompletedExpr);
	WorkData.ExprToSkip.AddUnique(ExecuteExpr);

	// set the index to 0
	const FRigVMVarExprAST* IndexExpr = InExpr->FindVarWithPinName(FRigVMStruct::ForLoopIndexPinName);
	check(IndexExpr);
	FRigVMOperand IndexOperand = WorkData.ExprToOperand.FindChecked(IndexExpr);
	WorkData.VM->GetByteCode().AddZeroOp(IndexOperand);
	if (Settings.SetupNodeInstructionIndex)
	{
		WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
	}

	// call the for loop compute
	int32 ForLoopInstructionIndex = TraverseCallExtern(InExpr, WorkData);
	if (Settings.SetupNodeInstructionIndex)
	{
		WorkData.VM->GetByteCode().SetSubject(ForLoopInstructionIndex, Callstack.GetCallPath(), Callstack.GetStack());
	}

	// set up the jump forward (jump out of the loop)
	const FRigVMVarExprAST* ContinueLoopExpr = InExpr->FindVarWithPinName(FRigVMStruct::ForLoopContinuePinName);
	check(ContinueLoopExpr);
	FRigVMOperand ContinueLoopOperand = WorkData.ExprToOperand.FindChecked(ContinueLoopExpr);

	uint64 JumpToEndByte = WorkData.VM->GetByteCode().AddJumpIfOp(ERigVMOpCode::JumpForwardIf, 0, ContinueLoopOperand, false);
	int32 JumpToEndInstruction = WorkData.VM->GetByteCode().GetNumInstructions() - 1;
	if (Settings.SetupNodeInstructionIndex)
	{
		WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
	}

	// begin the loop's block
	const FRigVMVarExprAST* CountExpr = InExpr->FindVarWithPinName(FRigVMStruct::ForLoopCountPinName);
	check(CountExpr);
	FRigVMOperand CountOperand = WorkData.ExprToOperand.FindChecked(CountExpr);
	WorkData.VM->GetByteCode().AddBeginBlockOp(CountOperand, IndexOperand);
	if (Settings.SetupNodeInstructionIndex)
	{
		WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
	}

	// traverse the body of the loop
	WorkData.ExprToSkip.Remove(ExecuteExpr);
	TraverseExpression(ExecuteExpr, WorkData);

	// end the loop's block
	WorkData.VM->GetByteCode().AddEndBlockOp();
	if (Settings.SetupNodeInstructionIndex)
	{
		WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
	}

	// increment the index
	WorkData.VM->GetByteCode().AddIncrementOp(IndexOperand);
	if (Settings.SetupNodeInstructionIndex)
	{
		WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
	}

	// jump to the beginning of the loop
	int32 JumpToStartInstruction = WorkData.VM->GetByteCode().GetNumInstructions();
	WorkData.VM->GetByteCode().AddJumpOp(ERigVMOpCode::JumpBackward, JumpToStartInstruction - ForLoopInstructionIndex);
	if (Settings.SetupNodeInstructionIndex)
	{
		WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
	}

	// update the jump operator with the right address
	int32 InstructionsToEnd = WorkData.VM->GetByteCode().GetNumInstructions() - JumpToEndInstruction;
	WorkData.VM->GetByteCode().GetOpAt<FRigVMJumpIfOp>(JumpToEndByte).InstructionIndex = InstructionsToEnd;

	// now traverse everything else connected to the completed pin
	WorkData.ExprToSkip.Remove(CompletedExpr);
	TraverseExpression(CompletedExpr, WorkData);
}

void URigVMCompiler::TraverseNoOp(const FRigVMNoOpExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	TraverseChildren(InExpr, WorkData);
}

void URigVMCompiler::TraverseVar(const FRigVMVarExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	TraverseChildren(InExpr, WorkData);

	if (WorkData.bSetupMemory)
	{
		FindOrAddRegister(InExpr, WorkData);
	}
}

void URigVMCompiler::TraverseLiteral(const FRigVMVarExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	TraverseVar(InExpr, WorkData);
}

void URigVMCompiler::TraverseExternalVar(const FRigVMExternalVarExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	TraverseVar(InExpr, WorkData);
}

void URigVMCompiler::TraverseAssign(const FRigVMAssignExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	TraverseChildren(InExpr, WorkData);

	ensure(InExpr->NumChildren() > 0);

	const FRigVMVarExprAST* SourceExpr = nullptr;

	const FRigVMExprAST* ChildExpr = InExpr->ChildAt(0);
	if (ChildExpr->IsA(FRigVMExprAST::EType::Var))
	{
		SourceExpr = ChildExpr->To<FRigVMVarExprAST>();
	}
	else if (ChildExpr->GetType() == FRigVMExprAST::EType::CachedValue)
	{
		SourceExpr = ChildExpr->To<FRigVMCachedValueExprAST>()->GetVarExpr();
	}
	else if (ChildExpr->GetType() == FRigVMExprAST::EType::NoOp)
	{
		ensure(ChildExpr->NumChildren() > 0);

		for (FRigVMExprAST* GrandChild : *ChildExpr)
		{
			if (GrandChild->IsA(FRigVMExprAST::EType::Var))
			{
				const FRigVMVarExprAST* VarExpr = GrandChild->To<FRigVMVarExprAST>();
				if (VarExpr->GetPin()->GetName() == TEXT("Value") ||
					VarExpr->GetPin()->GetName() == TEXT("EnumIndex"))
				{
					SourceExpr = VarExpr;
					break;
				}
			}
		}

		check(SourceExpr);
	}
	else
	{
		checkNoEntry();
	}

	FRigVMOperand Source = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(SourceExpr));

	if (!WorkData.bSetupMemory)
	{
		const FRigVMVarExprAST* TargetExpr = InExpr->GetFirstParentOfType(FRigVMVarExprAST::EType::Var)->To<FRigVMVarExprAST>();
		TargetExpr = GetSourceVarExpr(TargetExpr);
		
		FRigVMOperand Target = WorkData.ExprToOperand.FindChecked(TargetExpr);
		if(Target == Source)
		{
			return;
		}

		// if this is a copy - we should check if operands need offsets
		if (InExpr->GetType() == FRigVMExprAST::EType::Copy)
		{
			struct Local
			{
				static void SetupRegisterOffset(URigVM* VM, URigVMPin* Pin, FRigVMOperand& Operand, const FRigVMVarExprAST* VarExpr, bool bSource, FRigVMCompilerWorkData& WorkData)
				{
					URigVMPin* RootPin = Pin->GetRootPin();
					if (Pin == RootPin)
					{
						return;
					}

					if (URigVMSelectNode* SelectNode = Cast<URigVMSelectNode>(RootPin->GetNode()))
					{
						if (Pin->GetParentPin() == RootPin && RootPin->GetName() == URigVMSelectNode::ValueName)
						{
							return;
						}
					}

					FString SegmentPath = Pin->GetSegmentPath();

#if UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED

					int32 ArrayIndex = INDEX_NONE;
					if (RootPin->IsArray())
					{
						FString SegmentArrayIndex = SegmentPath;
						int32 DotIndex = INDEX_NONE;
						if (SegmentPath.FindChar('.', DotIndex))
						{
							SegmentArrayIndex = SegmentPath.Left(DotIndex);
							SegmentPath = SegmentPath.Mid(DotIndex + 1);
						}
						else
						{		
							SegmentPath = FString();
						}
						ArrayIndex = FCString::Atoi(*SegmentArrayIndex);
					}
				
					if (Operand.GetMemoryType() == ERigVMMemoryType::External)
					{
						const FRigVMExternalVariable& ExternalVariable = VM->GetExternalVariables()[Operand.GetRegisterIndex()];
						UScriptStruct* ScriptStruct = CastChecked<UScriptStruct>(ExternalVariable.TypeObject);
						int32 RegisterOffset = VM->GetWorkMemory().GetOrAddRegisterOffset(INDEX_NONE, ScriptStruct, SegmentPath, ArrayIndex == INDEX_NONE ? 0 : ArrayIndex, ExternalVariable.Size);
						Operand = FRigVMOperand(ERigVMMemoryType::External, Operand.GetRegisterIndex(), RegisterOffset);
					}
					else
					{
						FRigVMMemoryContainer& Memory = Operand.GetMemoryType() == ERigVMMemoryType::Literal ? VM->GetLiteralMemory() :
							(Operand.GetMemoryType() == ERigVMMemoryType::Work ? VM->GetWorkMemory() : VM->GetDebugMemory());
						Operand = Memory.GetOperand(Operand.GetRegisterIndex(), SegmentPath, ArrayIndex);
					}

#else

					const int32 PropertyPathIndex = WorkData.FindOrAddPropertyPath(Operand, RootPin->GetCPPType(), SegmentPath);
					Operand = FRigVMOperand(Operand.GetMemoryType(), Operand.GetRegisterIndex(), PropertyPathIndex);
					
#endif
				}
			};

			Local::SetupRegisterOffset(WorkData.VM, InExpr->GetSourcePin(), Source, SourceExpr, true, WorkData);
			Local::SetupRegisterOffset(WorkData.VM, InExpr->GetTargetPin(), Target, TargetExpr, false, WorkData);
		}

		FRigVMCopyOp CopyOp = WorkData.VM->GetCopyOpForOperands(Source, Target);
		if(CopyOp.IsValid())
		{
			WorkData.VM->GetByteCode().AddCopyOp(CopyOp);

			int32 InstructionIndex = WorkData.VM->GetByteCode().GetNumInstructions() - 1;
			if (Settings.SetupNodeInstructionIndex)
			{
				if (URigVMPin* SourcePin = InExpr->GetSourcePin())
				{
					if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(SourcePin->GetNode()))
					{
						const FRigVMCallstack Callstack = SourceExpr->GetProxy().GetSibling(VariableNode).GetCallstack();
						WorkData.VM->GetByteCode().SetSubject(InstructionIndex, Callstack.GetCallPath(), Callstack.GetStack());
					}
				}

				if (URigVMPin* TargetPin = InExpr->GetTargetPin())
				{
					if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(TargetPin->GetNode()))
					{
						const FRigVMCallstack Callstack = TargetExpr->GetProxy().GetSibling(VariableNode).GetCallstack();
						WorkData.VM->GetByteCode().SetSubject(InstructionIndex, Callstack.GetCallPath(), Callstack.GetStack());
					}
				}
			}
		}
	}
}

void URigVMCompiler::TraverseCopy(const FRigVMCopyExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	TraverseAssign(InExpr->To<FRigVMAssignExprAST>(), WorkData);
}

void URigVMCompiler::TraverseCachedValue(const FRigVMCachedValueExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	TraverseChildren(InExpr, WorkData);
}

void URigVMCompiler::TraverseExit(const FRigVMExitExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	ensure(InExpr->NumChildren() == 0);
	if (!WorkData.bSetupMemory)
	{
		WorkData.VM->GetByteCode().AddExitOp();
	}
}

void URigVMCompiler::TraverseBranch(const FRigVMBranchExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	ensure(InExpr->NumChildren() == 4);

	if (WorkData.bSetupMemory)
	{
		TraverseChildren(InExpr, WorkData);
		return;
	}

	URigVMBranchNode* BranchNode = Cast<URigVMBranchNode>(InExpr->GetNode());
	const FRigVMCallstack Callstack = InExpr->GetProxy().GetCallstack();

	const FRigVMVarExprAST* ExecuteContextExpr = InExpr->ChildAt<FRigVMVarExprAST>(0);
	const FRigVMVarExprAST* ConditionExpr = InExpr->ChildAt<FRigVMVarExprAST>(1);
	const FRigVMVarExprAST* TrueExpr = InExpr->ChildAt<FRigVMVarExprAST>(2);
	const FRigVMVarExprAST* FalseExpr = InExpr->ChildAt<FRigVMVarExprAST>(3);

	// traverse the condition first
	TraverseExpression(ConditionExpr, WorkData);

	if (ConditionExpr->IsA(FRigVMExprAST::CachedValue))
	{
		ConditionExpr = ConditionExpr->To<FRigVMCachedValueExprAST>()->GetVarExpr();
	}

	FRigVMOperand& ConditionOperand = WorkData.ExprToOperand.FindChecked(ConditionExpr);

	// setup the first jump
	uint64 JumpToFalseByte = WorkData.VM->GetByteCode().AddJumpIfOp(ERigVMOpCode::JumpForwardIf, 1, ConditionOperand, false);
	int32 JumpToFalseInstruction = WorkData.VM->GetByteCode().GetNumInstructions() - 1;
	if (Settings.SetupNodeInstructionIndex)
	{
		WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
	}

	// traverse the true case
	TraverseExpression(TrueExpr, WorkData);

	uint64 JumpToEndByte = WorkData.VM->GetByteCode().AddJumpOp(ERigVMOpCode::JumpForward, 1);
	int32 JumpToEndInstruction = WorkData.VM->GetByteCode().GetNumInstructions() - 1;
	if (Settings.SetupNodeInstructionIndex)
	{
		WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
	}

	// correct the jump to false instruction index
	int32 NumInstructionsInTrueCase = WorkData.VM->GetByteCode().GetNumInstructions() - JumpToFalseInstruction;
	WorkData.VM->GetByteCode().GetOpAt<FRigVMJumpIfOp>(JumpToFalseByte).InstructionIndex = NumInstructionsInTrueCase;

	// traverse the false case
	TraverseExpression(FalseExpr, WorkData);

	// correct the jump to end instruction index
	int32 NumInstructionsInFalseCase = WorkData.VM->GetByteCode().GetNumInstructions() - JumpToEndInstruction;
	WorkData.VM->GetByteCode().GetOpAt<FRigVMJumpOp>(JumpToEndByte).InstructionIndex = NumInstructionsInFalseCase;
}

void URigVMCompiler::TraverseIf(const FRigVMIfExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	ensure(InExpr->NumChildren() == 4);

	if (WorkData.bSetupMemory)
	{
		TraverseChildren(InExpr, WorkData);
		return;
	}

	URigVMIfNode* IfNode = Cast<URigVMIfNode>(InExpr->GetNode());
	if(!ValidateNode(IfNode))
	{
		return;
	}

	const FRigVMCallstack Callstack = InExpr->GetProxy().GetCallstack();

	const FRigVMVarExprAST* ConditionExpr = InExpr->ChildAt<FRigVMVarExprAST>(0);
	const FRigVMVarExprAST* TrueExpr = InExpr->ChildAt<FRigVMVarExprAST>(1);
	const FRigVMVarExprAST* FalseExpr = InExpr->ChildAt<FRigVMVarExprAST>(2);
	const FRigVMVarExprAST* ResultExpr = InExpr->ChildAt<FRigVMVarExprAST>(3);

	// traverse the condition first
	TraverseExpression(ConditionExpr, WorkData);

	if (ConditionExpr->IsA(FRigVMExprAST::CachedValue))
	{
		ConditionExpr = ConditionExpr->To<FRigVMCachedValueExprAST>()->GetVarExpr();
	}

	FRigVMOperand& ConditionOperand = WorkData.ExprToOperand.FindChecked(ConditionExpr);
	FRigVMOperand& ResultOperand = WorkData.ExprToOperand.FindChecked(ResultExpr);

	// setup the first jump
	uint64 JumpToFalseByte = WorkData.VM->GetByteCode().AddJumpIfOp(ERigVMOpCode::JumpForwardIf, 1, ConditionOperand, false);
	int32 JumpToFalseInstruction = WorkData.VM->GetByteCode().GetNumInstructions() - 1;
	if (Settings.SetupNodeInstructionIndex)
	{
		WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
	}

	// traverse the true case
	TraverseExpression(TrueExpr, WorkData);

	if (TrueExpr->IsA(FRigVMExprAST::CachedValue))
	{
		TrueExpr = TrueExpr->To<FRigVMCachedValueExprAST>()->GetVarExpr();
	}

	FRigVMOperand& TrueOperand = WorkData.ExprToOperand.FindChecked(TrueExpr);

	WorkData.VM->GetByteCode().AddCopyOp(WorkData.VM->GetCopyOpForOperands(TrueOperand, ResultOperand));
	if (Settings.SetupNodeInstructionIndex)
	{
		WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
	}

	uint64 JumpToEndByte = WorkData.VM->GetByteCode().AddJumpOp(ERigVMOpCode::JumpForward, 1);
	int32 JumpToEndInstruction = WorkData.VM->GetByteCode().GetNumInstructions() - 1;
	if (Settings.SetupNodeInstructionIndex)
	{
		WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
	}

	// correct the jump to false instruction index
	int32 NumInstructionsInTrueCase = WorkData.VM->GetByteCode().GetNumInstructions() - JumpToFalseInstruction;
	WorkData.VM->GetByteCode().GetOpAt<FRigVMJumpIfOp>(JumpToFalseByte).InstructionIndex = NumInstructionsInTrueCase;

	// traverse the false case
	TraverseExpression(FalseExpr, WorkData);

	if (FalseExpr->IsA(FRigVMExprAST::CachedValue))
	{
		FalseExpr = FalseExpr->To<FRigVMCachedValueExprAST>()->GetVarExpr();
	}

	FRigVMOperand& FalseOperand = WorkData.ExprToOperand.FindChecked(FalseExpr);

	WorkData.VM->GetByteCode().AddCopyOp(WorkData.VM->GetCopyOpForOperands(FalseOperand, ResultOperand));
	if (Settings.SetupNodeInstructionIndex)
	{
		WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
	}

	// correct the jump to end instruction index
	int32 NumInstructionsInFalseCase = WorkData.VM->GetByteCode().GetNumInstructions() - JumpToEndInstruction;
	WorkData.VM->GetByteCode().GetOpAt<FRigVMJumpOp>(JumpToEndByte).InstructionIndex = NumInstructionsInFalseCase;
}

void URigVMCompiler::TraverseSelect(const FRigVMSelectExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	URigVMSelectNode* SelectNode = Cast<URigVMSelectNode>(InExpr->GetNode());
	if(!ValidateNode(SelectNode))
	{
		return;
	}

	const FRigVMCallstack Callstack = InExpr->GetProxy().GetCallstack();

	int32 NumCases = SelectNode->FindPin(URigVMSelectNode::ValueName)->GetArraySize();

	if (WorkData.bSetupMemory)
	{
		TraverseChildren(InExpr, WorkData);

		// setup literals for each index (we don't need zero)
		for (int32 CaseIndex = 1; CaseIndex < NumCases; CaseIndex++)
		{
			if (!WorkData.IntegerLiterals.Contains(CaseIndex))
			{
				FName LiteralName = *FString::FromInt(CaseIndex);

#if UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED
				
				int32 Register = WorkData.VM->GetLiteralMemory().Add<int32>(LiteralName, CaseIndex);
 
#if WITH_EDITORONLY_DATA
				WorkData.VM->GetLiteralMemory().Registers[Register].BaseCPPType = TEXT("int32");
				WorkData.VM->GetLiteralMemory().Registers[Register].BaseCPPTypeObject = nullptr;
#endif

				FRigVMOperand Operand = WorkData.VM->GetLiteralMemory().GetOperand(Register);
				
#else

				const FString DefaultValue = FString::FromInt(CaseIndex);
				FRigVMOperand Operand = WorkData.AddProperty(
					ERigVMMemoryType::Literal,
					LiteralName,
					TEXT("int32"),
					nullptr,
					DefaultValue);
				
#endif
				WorkData.IntegerLiterals.Add(CaseIndex, Operand);
			}
		}

		if (!WorkData.ComparisonOperand.IsValid())
		{
#if UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED
			
			int32 Register = WorkData.VM->GetWorkMemory().Add<bool>(FName(TEXT("IntEquals")), false);

#if WITH_EDITORONLY_DATA
			WorkData.VM->GetWorkMemory().Registers[Register].BaseCPPType = TEXT("bool");
			WorkData.VM->GetWorkMemory().Registers[Register].BaseCPPTypeObject = nullptr;
#endif

			WorkData.ComparisonOperand = WorkData.VM->GetWorkMemory().GetOperand(Register);

#else

			WorkData.ComparisonOperand = WorkData.AddProperty(
				ERigVMMemoryType::Work,
				FName(TEXT("IntEquals")),
				TEXT("bool"),
				nullptr,
				TEXT("false"));

#endif
		}
		return;
	}

	const FRigVMVarExprAST* IndexExpr = InExpr->ChildAt<FRigVMVarExprAST>(0);
	TArray<const FRigVMVarExprAST*> CaseExpressions;
	for (int32 CaseIndex = 0; CaseIndex < NumCases; CaseIndex++)
	{
		CaseExpressions.Add(InExpr->ChildAt<FRigVMVarExprAST>(CaseIndex + 1));
	}

	const FRigVMVarExprAST* ResultExpr = InExpr->ChildAt<FRigVMVarExprAST>(InExpr->NumChildren() - 1);

	// traverse the condition first
	TraverseExpression(IndexExpr, WorkData);

	// this can happen if the optimizer doesn't remove it
	if (CaseExpressions.Num() == 0)
	{
		return;
	}

	if (IndexExpr->IsA(FRigVMExprAST::CachedValue))
	{
		IndexExpr = IndexExpr->To<FRigVMCachedValueExprAST>()->GetVarExpr();
	}

	FRigVMOperand& IndexOperand = WorkData.ExprToOperand.FindChecked(IndexExpr);
	FRigVMOperand& ResultOperand = WorkData.ExprToOperand.FindChecked(ResultExpr);

	// setup the jumps for each case
	TArray<uint64> JumpToCaseBytes;
	TArray<int32> JumpToCaseInstructions;
	JumpToCaseBytes.Add(0);
	JumpToCaseInstructions.Add(0);

	for (int32 CaseIndex = 1; CaseIndex < NumCases; CaseIndex++)
	{
		// compare and jump eventually
		WorkData.VM->GetByteCode().AddEqualsOp(IndexOperand, WorkData.IntegerLiterals.FindChecked(CaseIndex), WorkData.ComparisonOperand);
		if (Settings.SetupNodeInstructionIndex)
		{
			WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
		}

		uint64 JumpByte = WorkData.VM->GetByteCode().AddJumpIfOp(ERigVMOpCode::JumpForwardIf, 1, WorkData.ComparisonOperand, true);
		int32 JumpInstruction = WorkData.VM->GetByteCode().GetNumInstructions() - 1;
		if (Settings.SetupNodeInstructionIndex)
		{
			WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
		}

		JumpToCaseBytes.Add(JumpByte);
		JumpToCaseInstructions.Add(JumpInstruction);
	}

	TArray<uint64> JumpToEndBytes;
	TArray<int32> JumpToEndInstructions;

	for (int32 CaseIndex = 0; CaseIndex < NumCases; CaseIndex++)
	{
		if (CaseIndex > 0)
		{
			int32 NumInstructionsInCase = WorkData.VM->GetByteCode().GetNumInstructions() - JumpToCaseInstructions[CaseIndex];
			WorkData.VM->GetByteCode().GetOpAt<FRigVMJumpIfOp>(JumpToCaseBytes[CaseIndex]).InstructionIndex = NumInstructionsInCase;
		}

		TraverseExpression(CaseExpressions[CaseIndex], WorkData);

		// add copy op to copy the result
		FRigVMOperand& CaseOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(CaseExpressions[CaseIndex]));
		WorkData.VM->GetByteCode().AddCopyOp(WorkData.VM->GetCopyOpForOperands(CaseOperand, ResultOperand));
		if (Settings.SetupNodeInstructionIndex)
		{
			WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
		}
		
		if (CaseIndex < NumCases - 1)
		{
			uint64 JumpByte = WorkData.VM->GetByteCode().AddJumpOp(ERigVMOpCode::JumpForward, 1);
			int32 JumpInstruction = WorkData.VM->GetByteCode().GetNumInstructions() - 1;
			if (Settings.SetupNodeInstructionIndex)
			{
				WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
			}

			JumpToEndBytes.Add(JumpByte);
			JumpToEndInstructions.Add(JumpInstruction);
		}
	}

	for (int32 CaseIndex = 0; CaseIndex < NumCases - 1; CaseIndex++)
	{
		int32 NumInstructionsToEnd = WorkData.VM->GetByteCode().GetNumInstructions() - JumpToEndInstructions[CaseIndex];
		WorkData.VM->GetByteCode().GetOpAt<FRigVMJumpOp>(JumpToEndBytes[CaseIndex]).InstructionIndex = NumInstructionsToEnd;
	}
}

void URigVMCompiler::TraverseArray(const FRigVMArrayExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	URigVMArrayNode* ArrayNode = Cast<URigVMArrayNode>(InExpr->GetNode());
	if(!ValidateNode(ArrayNode))
	{
		return;
	}
	
	if (WorkData.bSetupMemory)
	{
		TraverseChildren(InExpr, WorkData);
	}
	else
	{
		const FRigVMCallstack Callstack = InExpr->GetProxy().GetCallstack();

		static const FName ExecuteName = FRigVMStruct::ExecuteName;
		static const FName ArrayName = *URigVMArrayNode::ArrayName;
		static const FName NumName = *URigVMArrayNode::NumName;
		static const FName IndexName = *URigVMArrayNode::IndexName;
		static const FName ElementName = *URigVMArrayNode::ElementName;
		static const FName SuccessName = *URigVMArrayNode::SuccessName;
		static const FName OtherName = *URigVMArrayNode::OtherName;
		static const FName CloneName = *URigVMArrayNode::CloneName;
		static const FName CountName = *URigVMArrayNode::CountName;
		static const FName RatioName = *URigVMArrayNode::RatioName;
		static const FName ResultName = *URigVMArrayNode::ResultName;
		static const FName ContinueName = *URigVMArrayNode::ContinueName;
		static const FName CompletedName = *URigVMArrayNode::CompletedName;

		const ERigVMOpCode OpCode = ArrayNode->GetOpCode();
		switch(OpCode)
		{
			case ERigVMOpCode::ArrayReset:
			{
				TraverseChildren(InExpr, WorkData);
				const FRigVMOperand& ArrayOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(1)));
				WorkData.VM->GetByteCode().AddArrayResetOp(ArrayOperand);
				if (Settings.SetupNodeInstructionIndex)
				{
					WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
				}
				break;
			}
			case ERigVMOpCode::ArrayGetNum:
			{
				TraverseChildren(InExpr, WorkData);
				const FRigVMOperand& ArrayOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(0)));
				const FRigVMOperand& NumOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(1)));
				WorkData.VM->GetByteCode().AddArrayGetNumOp(ArrayOperand, NumOperand);
				if (Settings.SetupNodeInstructionIndex)
				{
					WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
				}
				break;
			}
			case ERigVMOpCode::ArraySetNum:
			{
				TraverseChildren(InExpr, WorkData);
				const FRigVMOperand& ArrayOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(1)));
				const FRigVMOperand& NumOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(2)));
				WorkData.VM->GetByteCode().AddArraySetNumOp(ArrayOperand, NumOperand);
				if (Settings.SetupNodeInstructionIndex)
				{
					WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
				}
				break;
			}
			case ERigVMOpCode::ArrayGetAtIndex:
			{
				TraverseChildren(InExpr, WorkData);
				const FRigVMOperand& ArrayOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(0)));
				const FRigVMOperand& IndexOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(1)));
				const FRigVMOperand& ElementOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(2)));
				WorkData.VM->GetByteCode().AddArrayGetAtIndexOp(ArrayOperand, IndexOperand, ElementOperand);
				if (Settings.SetupNodeInstructionIndex)
				{
					WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
				}
				break;
			}
			case ERigVMOpCode::ArraySetAtIndex:
			{
				TraverseChildren(InExpr, WorkData);
				const FRigVMOperand& ArrayOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(1)));
				const FRigVMOperand& IndexOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(2)));
				const FRigVMOperand& ElementOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(3)));
				WorkData.VM->GetByteCode().AddArraySetAtIndexOp(ArrayOperand, IndexOperand, ElementOperand);
				if (Settings.SetupNodeInstructionIndex)
				{
					WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
				}
				break;
			}
			case ERigVMOpCode::ArrayAdd:
			{
				TraverseChildren(InExpr, WorkData);
				const FRigVMOperand& ArrayOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(1)));
				const FRigVMOperand& ElementOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(2)));
				const FRigVMOperand& IndexOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(3)));
				WorkData.VM->GetByteCode().AddArrayAddOp(ArrayOperand, ElementOperand, IndexOperand);
				if (Settings.SetupNodeInstructionIndex)
				{
					WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
				}
				break;
			}
			case ERigVMOpCode::ArrayInsert:
			{
				TraverseChildren(InExpr, WorkData);
				const FRigVMOperand& ArrayOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(1)));
				const FRigVMOperand& IndexOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(2)));
				const FRigVMOperand& ElementOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(3)));
				WorkData.VM->GetByteCode().AddArrayInsertOp(ArrayOperand, IndexOperand, ElementOperand);
				if (Settings.SetupNodeInstructionIndex)
				{
					WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
				}
				break;
			}
			case ERigVMOpCode::ArrayRemove:
			{
				TraverseChildren(InExpr, WorkData);
				const FRigVMOperand& ArrayOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(1)));
				const FRigVMOperand& IndexOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(2)));
				WorkData.VM->GetByteCode().AddArrayRemoveOp(ArrayOperand, IndexOperand);
				if (Settings.SetupNodeInstructionIndex)
				{
					WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
				}
				break;
			}
			case ERigVMOpCode::ArrayFind:
			{
				TraverseChildren(InExpr, WorkData);
				const FRigVMOperand& ArrayOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(0)));
				const FRigVMOperand& ElementOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(1)));
				const FRigVMOperand& IndexOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(2)));
				const FRigVMOperand& SuccessOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(3)));
				WorkData.VM->GetByteCode().AddArrayFindOp(ArrayOperand, ElementOperand, IndexOperand, SuccessOperand);
				if (Settings.SetupNodeInstructionIndex)
				{
					WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
				}
				break;
			}
			case ERigVMOpCode::ArrayAppend:
			{
				TraverseChildren(InExpr, WorkData);
				const FRigVMOperand& ArrayOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(1)));
				const FRigVMOperand& OtherOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(2)));
				WorkData.VM->GetByteCode().AddArrayAppendOp(ArrayOperand, OtherOperand);
				if (Settings.SetupNodeInstructionIndex)
				{
					WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
				}
				break;
			}
			case ERigVMOpCode::ArrayClone:
			{
				TraverseChildren(InExpr, WorkData);
				const FRigVMOperand& ArrayOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(0)));
				const FRigVMOperand& CloneOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(1)));
				WorkData.VM->GetByteCode().AddArrayCloneOp(ArrayOperand, CloneOperand);
				if (Settings.SetupNodeInstructionIndex)
				{
					WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
				}
				break;
			}
			case ERigVMOpCode::ArrayIterator:
			{
				const FRigVMExprAST* ExecuteExpr = InExpr->ChildAt(0);
				const FRigVMExprAST* ArrayExpr = InExpr->ChildAt(1);
				const FRigVMExprAST* ElementExpr = InExpr->ChildAt(2);
				const FRigVMExprAST* IndexExpr = InExpr->ChildAt(3);
				const FRigVMExprAST* CountExpr = InExpr->ChildAt(4);
				const FRigVMExprAST* RatioExpr = InExpr->ChildAt(5);
				const FRigVMExprAST* ContinueExpr = InExpr->ChildAt(6);
				const FRigVMExprAST* CompletedExpr = InExpr->ChildAt(7);
				const FRigVMOperand& ExecuteOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(ExecuteExpr));
				const FRigVMOperand& ArrayOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(ArrayExpr));
				const FRigVMOperand& ElementOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(ElementExpr));
				const FRigVMOperand& IndexOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(IndexExpr));
				const FRigVMOperand& CountOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(CountExpr));
				const FRigVMOperand& RatioOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(RatioExpr));
				const FRigVMOperand& ContinueOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(ContinueExpr));
				const FRigVMOperand& CompletedOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(CompletedExpr));

				WorkData.ExprToSkip.AddUnique(ExecuteExpr);
				WorkData.ExprToSkip.AddUnique(CompletedExpr);

				// traverse the input array
				TraverseExpression(ArrayExpr, WorkData);

				// zero the index
				WorkData.VM->GetByteCode().AddZeroOp(IndexOperand);
				if (Settings.SetupNodeInstructionIndex)
				{
					WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
				}

				// add the iterator
				WorkData.VM->GetByteCode().AddArrayIteratorOp(ArrayOperand, ElementOperand, IndexOperand, CountOperand, RatioOperand, ContinueOperand);
				const int32 IteratorInstruction = WorkData.VM->GetByteCode().GetNumInstructions() - 1;
				if (Settings.SetupNodeInstructionIndex)
				{
					WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
				}

				// jump to the end of the loop
				const uint64 JumpToEndByte = WorkData.VM->GetByteCode().AddJumpIfOp(ERigVMOpCode::JumpForwardIf, 0, ContinueOperand, false);
				const int32 JumpToEndInstruction = WorkData.VM->GetByteCode().GetNumInstructions() - 1;
				if (Settings.SetupNodeInstructionIndex)
				{
					WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
				}

				// begin the block
				WorkData.VM->GetByteCode().AddBeginBlockOp(CountOperand, IndexOperand);
				if (Settings.SetupNodeInstructionIndex)
				{
					WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
				}

				// traverse the per iteration instructions
				WorkData.ExprToSkip.Remove(ExecuteExpr);
				TraverseExpression(ExecuteExpr, WorkData);

				// end the block
				WorkData.VM->GetByteCode().AddEndBlockOp();
				if (Settings.SetupNodeInstructionIndex)
				{
					WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
				}

				// increment index per loop iteration
				WorkData.VM->GetByteCode().AddIncrementOp(IndexOperand);
				if (Settings.SetupNodeInstructionIndex)
				{
					WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());

				}

				// jump backwards instruction (to the beginning of the iterator)
				const int32 JumpToStartInstruction = WorkData.VM->GetByteCode().GetNumInstructions();
				WorkData.VM->GetByteCode().AddJumpOp(ERigVMOpCode::JumpBackward, JumpToStartInstruction - IteratorInstruction);
				if (Settings.SetupNodeInstructionIndex)
				{
					WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
				}

				// fix up the first jump instruction
				const int32 InstructionsToEnd = WorkData.VM->GetByteCode().GetNumInstructions() - JumpToEndInstruction;
				WorkData.VM->GetByteCode().GetOpAt<FRigVMJumpIfOp>(JumpToEndByte).InstructionIndex = InstructionsToEnd;
				if (Settings.SetupNodeInstructionIndex)
				{
					WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
				}
					
				WorkData.ExprToSkip.Remove(CompletedExpr);
				TraverseExpression(CompletedExpr, WorkData);
				break;
			}
			case ERigVMOpCode::ArrayUnion:
			{
				TraverseChildren(InExpr, WorkData);
				const FRigVMOperand& ArrayOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(1)));
				const FRigVMOperand& OtherOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(2)));
				WorkData.VM->GetByteCode().AddArrayUnionOp(ArrayOperand, OtherOperand);
				if (Settings.SetupNodeInstructionIndex)
				{
					WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
				}
				break;
			}
			case ERigVMOpCode::ArrayDifference:
			{
				TraverseChildren(InExpr, WorkData);
				const FRigVMOperand& ArrayOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(0)));
				const FRigVMOperand& OtherOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(1)));
				const FRigVMOperand& ResultOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(2)));
				WorkData.VM->GetByteCode().AddArrayDifferenceOp(ArrayOperand, OtherOperand, ResultOperand);
				if (Settings.SetupNodeInstructionIndex)
				{
					WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
				}
				break;
			}
			case ERigVMOpCode::ArrayIntersection:
			{
				TraverseChildren(InExpr, WorkData);
				const FRigVMOperand& ArrayOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(0)));
				const FRigVMOperand& OtherOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(1)));
				const FRigVMOperand& ResultOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(2)));
				WorkData.VM->GetByteCode().AddArrayIntersectionOp(ArrayOperand, OtherOperand, ResultOperand);
				if (Settings.SetupNodeInstructionIndex)
				{
					WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
				}
				break;
			}
			case ERigVMOpCode::ArrayReverse:
			{
				TraverseChildren(InExpr, WorkData);
				const FRigVMOperand& ArrayOperand = WorkData.ExprToOperand.FindChecked(GetSourceVarExpr(InExpr->ChildAt(1)));
				WorkData.VM->GetByteCode().AddArrayReverseOp(ArrayOperand);
				if (Settings.SetupNodeInstructionIndex)
				{
					WorkData.VM->GetByteCode().SetSubject(WorkData.VM->GetByteCode().GetNumInstructions() - 1, Callstack.GetCallPath(), Callstack.GetStack());
				}
				break;
			}
			default:
			{
				checkNoEntry();
				break;
			}
		}
	}
}

void URigVMCompiler::InitializeLocalVariables(const FRigVMExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	// Initialize local variables if we are entering a new graph
	if (!WorkData.bSetupMemory)
	{
		FRigVMByteCode& ByteCode = WorkData.VM->GetByteCode();
		const FRigVMASTProxy* Proxy = nullptr;
		switch (InExpr->GetType())
		{
			case FRigVMExprAST::EType::CallExtern:
			{
				Proxy = &InExpr->To<FRigVMCallExternExprAST>()->GetProxy();
				break;
			}
			case FRigVMExprAST::EType::NoOp:
			{
				Proxy = &InExpr->To<FRigVMNoOpExprAST>()->GetProxy();
				break;
			}
			case FRigVMExprAST::EType::Var:
			{
				Proxy = &InExpr->To<FRigVMVarExprAST>()->GetProxy();
				break;
			}
			case FRigVMExprAST::EType::Literal:
			{
				Proxy = &InExpr->To<FRigVMLiteralExprAST>()->GetProxy();
				break;
			}
			case FRigVMExprAST::EType::ExternalVar:
			{
				Proxy = &InExpr->To<FRigVMExternalVarExprAST>()->GetProxy();
				break;
			}
			case FRigVMExprAST::EType::Branch:
			{
				Proxy = &InExpr->To<FRigVMBranchExprAST>()->GetProxy();
				break;
			}
			case FRigVMExprAST::EType::If:
			{
				Proxy = &InExpr->To<FRigVMIfExprAST>()->GetProxy();
				break;
			}
			case FRigVMExprAST::EType::Select:
			{
				Proxy = &InExpr->To<FRigVMSelectExprAST>()->GetProxy();
				break;
			}
			case FRigVMExprAST::EType::Array:
			{
				Proxy = &InExpr->To<FRigVMArrayExprAST>()->GetProxy();
				break;
			}
		}

		if(Proxy != nullptr)
		{
			const FRigVMCallstack& Callstack = Proxy->GetCallstack();
			ensure(Callstack.Num() > 0);

			// Find all function references in the callstack and initialize their local variables if necessary
			for (int32 SubjectIndex=0; SubjectIndex<Callstack.Num(); ++SubjectIndex)
			{
				if (const URigVMLibraryNode* Node = Cast<const URigVMLibraryNode>(Callstack[SubjectIndex]))
				{
					// Check if this is the first time we are accessing this function reference
					bool bFound = false;
					for (int32 i=ByteCode.GetNumInstructions()-1; i>0; --i)
					{
						const TArray<UObject*>* PreviousCallstack = ByteCode.GetCallstackForInstruction(i);
						if (PreviousCallstack && PreviousCallstack->Contains(Node))
						{
							bFound = true;
							break;
						}
					}

					// If it is the first time we access this function reference, initialize all local variables
					if (!bFound)
					{
						for (FRigVMGraphVariableDescription Variable : Node->GetContainedGraph()->LocalVariables)
						{
							FString TargetPath = FString::Printf(TEXT("LocalVariable::%s|%s"), *Node->GetNodePath(), *Variable.Name.ToString());
							FString SourcePath = FString::Printf(TEXT("LocalVariableDefault::%s|%s::Const"), *Node->GetContainedGraph()->GetGraphName(), *Variable.Name.ToString());
							FRigVMOperand* TargetPtr = WorkData.PinPathToOperand->Find(TargetPath);
							FRigVMOperand* SourcePtr = WorkData.PinPathToOperand->Find(SourcePath);
							if (SourcePtr && TargetPtr) 
							{
								const FRigVMOperand& Source = *SourcePtr;
								const FRigVMOperand& Target = *TargetPtr;
								
								ByteCode.AddCopyOp(WorkData.VM->GetCopyOpForOperands(Source, Target));
								if(Settings.SetupNodeInstructionIndex)
								{
									const int32 InstructionIndex = ByteCode.GetNumInstructions() - 1;
									WorkData.VM->GetByteCode().SetSubject(InstructionIndex, Callstack.GetCallPath(), Callstack.GetStack());
								}							
							}					
						}
					}
				}
			}
		}
	}
}

FString URigVMCompiler::GetPinHash(const URigVMPin* InPin, const FRigVMVarExprAST* InVarExpr, bool bIsDebugValue)
{
	FString Prefix = bIsDebugValue ? TEXT("DebugWatch:") : TEXT("");
	FString Suffix;

	if (InPin->IsExecuteContext())
	{
		return TEXT("ExecuteContext!");
	}

	URigVMNode* Node = InPin->GetNode();

	bool bIsExecutePin = false;
	bool bIsLiteral = false;
	bool bIsParameter = false;
	bool bIsVariable = false;

	if (InVarExpr != nullptr && !bIsDebugValue)
	{
		if (InVarExpr->IsA(FRigVMExprAST::ExternalVar))
		{
			URigVMPin::FPinOverride PinOverride(InVarExpr->GetProxy(), InVarExpr->GetParser()->GetPinOverrides());
			FString VariablePath = InPin->GetBoundVariablePath(PinOverride);
			return FString::Printf(TEXT("%sVariable::%s%s"), *Prefix, *VariablePath, *Suffix);
		}

		// for IO array pins we'll walk left and use that pin hash instead
		if(const FRigVMVarExprAST* SourceVarExpr = GetSourceVarExpr(InVarExpr))
		{
			if(SourceVarExpr != InVarExpr)
			{
				return GetPinHash(SourceVarExpr->GetPin(), SourceVarExpr, bIsDebugValue);
			}
		}

		bIsExecutePin = InPin->IsExecuteContext();
		bIsLiteral = InVarExpr->GetType() == FRigVMExprAST::EType::Literal;

		bIsParameter = Cast<URigVMParameterNode>(Node) != nullptr;
		bIsVariable = Cast<URigVMVariableNode>(Node) != nullptr || InVarExpr->IsA(FRigVMExprAST::ExternalVar);

		// determine if this is an initialization for an IO pin
		if (!bIsLiteral && !bIsParameter && !bIsVariable &&
			!bIsExecutePin && (InPin->GetDirection() == ERigVMPinDirection::IO ||
			(InPin->GetDirection() == ERigVMPinDirection::Input && InPin->GetSourceLinks().Num() == 0)))
		{
			Suffix = TEXT("::IO");
		}
		else if (bIsLiteral)
		{
			Suffix = TEXT("::Const");
		}
	}

	bool bUseFullNodePath = true;
	if (URigVMParameterNode* ParameterNode = Cast<URigVMParameterNode>(Node))
	{
		if (InPin->GetName() == TEXT("Value") && !bIsLiteral && !bIsDebugValue)
		{
			return FString::Printf(TEXT("%sParameter::%s%s"), *Prefix, *ParameterNode->GetParameterName().ToString(), *Suffix);
		}
	}
	else if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
	{
		if (InPin->GetName() == TEXT("Value") && !bIsDebugValue)
		{
			FName VariableName = VariableNode->GetVariableName();

			if(VariableNode->IsLocalVariable())
			{
				if (bIsLiteral)
				{
					// Literal values will be reused for all instance of local variables
					if (InVarExpr->NumParents() == 0 && InVarExpr->NumChildren() == 0)
					{
						return FString::Printf(TEXT("%sLocalVariableDefault::%s|%s%s"), *Prefix, *Node->GetGraph()->GetGraphName(), *VariableName.ToString(), *Suffix);
					}
					else
					{
						return FString::Printf(TEXT("%sLocalVariable::%s|%s%s"), *Prefix, *Node->GetGraph()->GetGraphName(), *VariableName.ToString(), *Suffix);					
					}			
				}
				else
				{
					if(InVarExpr)
					{
						FRigVMASTProxy ParentProxy = InVarExpr->GetProxy();
						while(ParentProxy.GetCallstack().Num() > 1)
						{
							ParentProxy = ParentProxy.GetParent();

							if(URigVMLibraryNode* LibraryNode = ParentProxy.GetSubject<URigVMLibraryNode>())
							{
								// Local variables for non-root graphs are in the format "LocalVariable::PathToGraph|VariableName"
								return FString::Printf(TEXT("%sLocalVariable::%s|%s%s"), *Prefix, *LibraryNode->GetNodePath(true), *VariableName.ToString(), *Suffix);
							}
						}

						// Local variables for root graphs are in the format "LocalVariable::VariableName"
						return FString::Printf(TEXT("%sLocalVariable::%s%s"), *Prefix, *VariableName.ToString(), *Suffix);
					}
				}
			}

			if (!bIsLiteral)
			{		
				// determine if this variable needs to be remapped
				if(InVarExpr)
				{
					FRigVMASTProxy ParentProxy = InVarExpr->GetProxy();
					while(ParentProxy.GetCallstack().Num() > 1)
					{
						ParentProxy = ParentProxy.GetParent();

						if(URigVMFunctionReferenceNode* FunctionReferenceNode = ParentProxy.GetSubject<URigVMFunctionReferenceNode>())
						{
							const FName RemappedVariableName = FunctionReferenceNode->GetOuterVariableName(VariableName);
							if(!RemappedVariableName.IsNone())
							{
								VariableName = RemappedVariableName;
							}
						}
					}
				}
			
				return FString::Printf(TEXT("%sVariable::%s%s"), *Prefix, *VariableName.ToString(), *Suffix);
			}
		}		
	}
	else
	{
#if UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED
		if (InVarExpr)
#else
		if (InVarExpr && !bIsDebugValue)
#endif
		{
			const FRigVMASTProxy NodeProxy = InVarExpr->GetProxy().GetSibling(Node);
			if (const FRigVMExprAST* NodeExpr = InVarExpr->GetParser()->GetExprForSubject(NodeProxy))
			{
				// rely on the proxy callstack to differentiate registers
				const FString CallStackPath = NodeProxy.GetCallstack().GetCallPath(false /* include last */);
				if (!CallStackPath.IsEmpty())
				{
					Prefix += CallStackPath + TEXT("|");
					bUseFullNodePath = false;
				}
			}
		}
	}

	return FString::Printf(TEXT("%s%s%s"), *Prefix, *InPin->GetPinPath(bUseFullNodePath), *Suffix);
}

const FRigVMVarExprAST* URigVMCompiler::GetSourceVarExpr(const FRigVMExprAST* InExpr)
{
	if(InExpr)
	{
		if(InExpr->IsA(FRigVMExprAST::EType::CachedValue))
		{
			return GetSourceVarExpr(InExpr->To<FRigVMCachedValueExprAST>()->GetVarExpr());
		}

		if(InExpr->IsA(FRigVMExprAST::EType::Var))
		{
			const FRigVMVarExprAST* VarExpr = InExpr->To<FRigVMVarExprAST>();
			
			if(VarExpr->GetPin()->IsReferenceCountedContainer() &&
				((VarExpr->GetPin()->GetDirection() == ERigVMPinDirection::Input) || (VarExpr->GetPin()->GetDirection() == ERigVMPinDirection::IO)))
			{
				// if this is a variable setter we cannot follow the source var
				if(VarExpr->GetPin()->GetDirection() == ERigVMPinDirection::Input)
				{
					if(VarExpr->GetPin()->GetNode()->IsA<URigVMVariableNode>())
					{
						return VarExpr;
					}
				}
				
				if(const FRigVMExprAST* AssignExpr = VarExpr->GetFirstChildOfType(FRigVMExprAST::EType::Assign))
				{
					// don't follow a copy assignment
					if(AssignExpr->IsA(FRigVMExprAST::EType::Copy))
					{
						return VarExpr;
					}
					
					if(const FRigVMExprAST* CachedValueExpr = VarExpr->GetFirstChildOfType(FRigVMExprAST::EType::CachedValue))
					{
						return GetSourceVarExpr(CachedValueExpr->To<FRigVMCachedValueExprAST>()->GetVarExpr());
					}
					else if(const FRigVMExprAST* ChildExpr = VarExpr->GetFirstChildOfType(FRigVMExprAST::EType::Var))
					{
						return GetSourceVarExpr(ChildExpr->To<FRigVMVarExprAST>());
					}
				}
			}
			return VarExpr;
		}
	}

	return nullptr;
}

#if UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED

void URigVMCompiler::CreateDebugRegister(URigVMPin* InPin, URigVM* OutVM,
	TMap<FString, FRigVMOperand>* OutOperands, TSharedPtr<FRigVMParserAST> InRuntimeAST)
{
	check(InPin);
	check(OutVM);
	check(OutOperands);
	check(InRuntimeAST.IsValid());
	
	FRigVMCompilerWorkData WorkData;
	WorkData.VM = OutVM;
	WorkData.PinPathToOperand = OutOperands;

	URigVMPin* Pin = InPin->GetRootPin();
	URigVMPin* SourcePin = Pin;
	if(Settings.ASTSettings.bFoldAssignments)
	{
		while(SourcePin->GetSourceLinks().Num() > 0)
		{
			SourcePin = SourcePin->GetSourceLinks()[0]->GetSourcePin();
		}
	}
	
	TArray<const FRigVMExprAST*> Expressions = InRuntimeAST->GetExpressionsForSubject(SourcePin);
	for(const FRigVMExprAST* Expression : Expressions)
	{
		check(Expression->IsA(FRigVMExprAST::EType::Var));
		const FRigVMVarExprAST* VarExpression = Expression->To<FRigVMVarExprAST>();
		const FString PinHash = GetPinHash(SourcePin, VarExpression, false);
		if(const FRigVMOperand* Operand = OutOperands->Find(PinHash))
		{
			const FRigVMASTProxy PinProxy = FRigVMASTProxy::MakeFromUObject(Pin);
			FRigVMVarExprAST TempVarExpr(FRigVMExprAST::EType::Var, PinProxy);
			TempVarExpr.ParserPtr = InRuntimeAST.Get();
	
			const FRigVMOperand DebugOperand = FindOrAddRegister(&TempVarExpr, WorkData, true);
			if(DebugOperand.IsValid())
			{
				FRigVMOperand KeyOperand(Operand->GetMemoryType(), Operand->GetRegisterIndex()); // no register offset
				OutVM->OperandToDebugRegisters.FindOrAdd(KeyOperand).AddUnique(DebugOperand);
			}
		}
	}
}

void URigVMCompiler::RemoveDebugRegister(URigVMPin* InPin, URigVM* OutVM,
	TMap<FString, FRigVMOperand>* OutOperands, TSharedPtr<FRigVMParserAST> InRuntimeAST)
{
	check(OutVM);
	check(OutOperands);

	URigVMPin* Pin = InPin->GetRootPin();
	const FRigVMASTProxy PinProxy = FRigVMASTProxy::MakeFromUObject(Pin);
	FRigVMVarExprAST TempVarExpr(FRigVMExprAST::EType::Var, PinProxy);
	TempVarExpr.ParserPtr = InRuntimeAST.Get();

	const FString PinHash = GetPinHash(Pin, &TempVarExpr, true);
	if(const FRigVMOperand* DebugOperand = OutOperands->Find(PinHash))
	{
		TArray<FRigVMOperand> KeysToRemove;
		for(TPair<FRigVMOperand, TArray<FRigVMOperand>>& Pair : OutVM->OperandToDebugRegisters)
		{
			if(Pair.Value.Remove(*DebugOperand) > 0)
			{
				if(Pair.Value.IsEmpty())
				{
					KeysToRemove.AddUnique(Pair.Key);
				}
			}
		}
		for(const FRigVMOperand& KeyToRemove : KeysToRemove)
		{
			OutVM->OperandToDebugRegisters.Remove(KeyToRemove);
		}
		OutOperands->Remove(PinHash);
	}
}

#else

void URigVMCompiler::MarkDebugWatch(bool bRequired, URigVMPin* InPin, URigVM* OutVM,
	TMap<FString, FRigVMOperand>* OutOperands, TSharedPtr<FRigVMParserAST> InRuntimeAST)
{
	check(InPin);
	check(OutVM);
	check(OutOperands);
	check(InRuntimeAST.IsValid());
	
	URigVMPin* Pin = InPin->GetRootPin();
	URigVMPin* SourcePin = Pin;
	if(Settings.ASTSettings.bFoldAssignments)
	{
		while(SourcePin->GetSourceLinks().Num() > 0)
		{
			SourcePin = SourcePin->GetSourceLinks()[0]->GetSourcePin();
		}
	}
		
	TArray<const FRigVMExprAST*> Expressions = InRuntimeAST->GetExpressionsForSubject(SourcePin);
	TArray<FRigVMOperand> VisitedKeys;
	for(const FRigVMExprAST* Expression : Expressions)
	{
		check(Expression->IsA(FRigVMExprAST::EType::Var));
		const FRigVMVarExprAST* VarExpression = Expression->To<FRigVMVarExprAST>();

		if(VarExpression->GetPin() == Pin)
		{
			// literals don't need to be stored on the debug memory
			if(VarExpression->IsA(FRigVMExprAST::Literal))
			{
				continue;
			}
		}
		
		const FString PinHash = GetPinHash(SourcePin, VarExpression, false);
		if(const FRigVMOperand* Operand = OutOperands->Find(PinHash))
		{
			const FRigVMASTProxy PinProxy = FRigVMASTProxy::MakeFromUObject(Pin);
			FRigVMVarExprAST TempVarExpr(FRigVMExprAST::EType::Var, PinProxy);
			TempVarExpr.ParserPtr = InRuntimeAST.Get();

			const FString DebugPinHash = GetPinHash(Pin, &TempVarExpr, true);
			const FRigVMOperand* DebugOperand = OutOperands->Find(DebugPinHash);
			if(DebugOperand)
			{
				if(DebugOperand->IsValid())
				{
					FRigVMOperand KeyOperand(Operand->GetMemoryType(), Operand->GetRegisterIndex()); // no register offset
					if(bRequired)
					{
						if(!VisitedKeys.Contains(KeyOperand))
						{
							OutVM->OperandToDebugRegisters.FindOrAdd(KeyOperand).AddUnique(*DebugOperand);
							VisitedKeys.Add(KeyOperand);
						}
					}
					else
					{
						TArray<FRigVMOperand>* MappedOperands = OutVM->OperandToDebugRegisters.Find(KeyOperand);
						if(MappedOperands)
						{
							MappedOperands->Remove(*DebugOperand);

							if(MappedOperands->IsEmpty())
							{
								OutVM->OperandToDebugRegisters.Remove(KeyOperand);
							}
						}
					}
				}
			}
		}
	}
}

#endif

UScriptStruct* URigVMCompiler::GetScriptStructForCPPType(const FString& InCPPType)
{
	if (InCPPType == TEXT("FRotator"))
	{
		return TBaseStructure<FRotator>::Get();
	}
	if (InCPPType == TEXT("FQuat"))
	{
		return TBaseStructure<FQuat>::Get();
	}
	if (InCPPType == TEXT("FTransform"))
	{
		return TBaseStructure<FTransform>::Get();
	}
	if (InCPPType == TEXT("FLinearColor"))
	{
		return TBaseStructure<FLinearColor>::Get();
	}
	if (InCPPType == TEXT("FColor"))
	{
		return TBaseStructure<FColor>::Get();
	}
	if (InCPPType == TEXT("FPlane"))
	{
		return TBaseStructure<FPlane>::Get();
	}
	if (InCPPType == TEXT("FVector"))
	{
		return TBaseStructure<FVector>::Get();
	}
	if (InCPPType == TEXT("FVector2D"))
	{
		return TBaseStructure<FVector2D>::Get();
	}
	if (InCPPType == TEXT("FVector4"))
	{
		return TBaseStructure<FVector4>::Get();
	}
	return nullptr;
}

TArray<URigVMPin*> URigVMCompiler::GetLinkedPins(URigVMPin* InPin, bool bInputs, bool bOutputs, bool bRecursive)
{
	TArray<URigVMPin*> LinkedPins;
	for (URigVMLink* Link : InPin->GetLinks())
	{
		if (bInputs && Link->GetTargetPin() == InPin)
		{
			LinkedPins.Add(Link->GetSourcePin());
		}
		else if (bOutputs && Link->GetSourcePin() == InPin)
		{
			LinkedPins.Add(Link->GetTargetPin());
		}
	}

	if (bRecursive)
	{
		for (URigVMPin* SubPin : InPin->GetSubPins())
		{
			LinkedPins.Append(GetLinkedPins(SubPin, bInputs, bOutputs, bRecursive));
		}
	}

	return LinkedPins;
}

uint16 URigVMCompiler::GetElementSizeFromCPPType(const FString& InCPPType, UScriptStruct* InScriptStruct)
{
	if (InScriptStruct == nullptr)
	{
		InScriptStruct = GetScriptStructForCPPType(InCPPType);
	}
	if (InScriptStruct != nullptr)
	{
		return InScriptStruct->GetStructureSize();
	}
	if (InCPPType == TEXT("bool"))
	{
		return sizeof(bool);
	}
	else if (InCPPType == TEXT("int32"))
	{
		return sizeof(int32);
	}
	if (InCPPType == TEXT("float"))
	{
		return sizeof(float);
	}
	if (InCPPType == TEXT("double"))
	{
		return sizeof(double);
	}
	if (InCPPType == TEXT("FName"))
	{
		return sizeof(FName);
	}
	if (InCPPType == TEXT("FString"))
	{
		return sizeof(FString);
	}

	ensure(false);
	return 0;
}

FRigVMOperand URigVMCompiler::FindOrAddRegister(const FRigVMVarExprAST* InVarExpr, FRigVMCompilerWorkData& WorkData, bool bIsDebugValue)
{
	if(!bIsDebugValue)
	{
		InVarExpr = GetSourceVarExpr(InVarExpr);
	}
	
	if (!bIsDebugValue)
	{
		FRigVMOperand const* ExistingOperand = WorkData.ExprToOperand.Find(InVarExpr);
		if (ExistingOperand)
		{
			return *ExistingOperand;
		}
	}

	const URigVMPin::FPinOverrideMap& PinOverrides = InVarExpr->GetParser()->GetPinOverrides();
	URigVMPin::FPinOverride PinOverride(InVarExpr->GetProxy(), PinOverrides);

	URigVMPin* Pin = InVarExpr->GetPin();

#if !UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED
	if(Pin->IsExecuteContext() && bIsDebugValue)
	{
		return FRigVMOperand();
	}
#endif
	
	FString CPPType = Pin->GetCPPType();
	FString BaseCPPType = Pin->IsArray() ? Pin->GetArrayElementCppType() : CPPType;
	FString Hash = GetPinHash(Pin, InVarExpr, bIsDebugValue);
	FRigVMOperand Operand;
	FString RegisterKey = Hash;

	bool bIsExecutePin = Pin->IsExecuteContext();
	bool bIsLiteral = InVarExpr->GetType() == FRigVMExprAST::EType::Literal && !bIsDebugValue;
	bool bIsParameter = InVarExpr->IsGraphParameter();
	bool bIsVariable = Pin->IsRootPin() && (Pin->GetName() == URigVMVariableNode::ValueName) &&
		InVarExpr->GetPin()->GetNode()->IsA<URigVMVariableNode>();

	// external variables don't require to add any register.
	if(bIsVariable && !bIsDebugValue)
	{
		for(int32 ExternalVariableIndex = 0; ExternalVariableIndex < WorkData.VM->GetExternalVariables().Num(); ExternalVariableIndex++)
		{
			const FName& ExternalVariableName = WorkData.VM->GetExternalVariables()[ExternalVariableIndex].Name;
			const FString ExternalVariableHash = FString::Printf(TEXT("Variable::%s"), *ExternalVariableName.ToString());
			if(ExternalVariableHash == Hash)
			{
				Operand = FRigVMOperand(ERigVMMemoryType::External, ExternalVariableIndex, INDEX_NONE);
				WorkData.ExprToOperand.Add(InVarExpr, Operand);
				WorkData.PinPathToOperand->FindOrAdd(Hash) = Operand;
				return Operand;
			}
		}
	}

#if UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED
	
	FRigVMMemoryContainer& Memory = 
	    bIsLiteral ? WorkData.VM->GetLiteralMemory() :
			(bIsDebugValue ? WorkData.VM->GetDebugMemory() : WorkData.VM->GetWorkMemory());

	const ERigVMMemoryType MemoryType = Memory.GetMemoryType();

#else

	const ERigVMMemoryType MemoryType =
		bIsLiteral ? ERigVMMemoryType::Literal:
		(bIsDebugValue ? ERigVMMemoryType::Debug : ERigVMMemoryType::Work);
	
#endif

	FRigVMOperand const* ExistingOperand = WorkData.PinPathToOperand->Find(Hash);
	if (ExistingOperand)
	{
		if(ExistingOperand->GetMemoryType() == MemoryType)
		{
			if (!bIsDebugValue)
			{
				check(!WorkData.ExprToOperand.Contains(InVarExpr));
				WorkData.ExprToOperand.Add(InVarExpr, *ExistingOperand);
			}
			return *ExistingOperand;
		}
	}

	// create remaining operands / registers
	if (!Operand.IsValid())
	{
		FName RegisterName = *RegisterKey;

		FString JoinedDefaultValue;
		TArray<FString> DefaultValues;
		if (Pin->IsArray())
		{
			if (Pin->GetDirection() == ERigVMPinDirection::Hidden)
			{
#if UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED
				ensure(WorkData.DefaultStructs.Num() > 0);
				TSharedPtr<FStructOnScope> DefaultStruct = WorkData.DefaultStructs.Last();
				FRigVMStruct* VMStruct = (FRigVMStruct *)DefaultStruct->GetStructMemory();

				int32 DesiredArraySize = VMStruct->GetArraySize(Pin->GetFName(), WorkData.RigVMUserData);

#endif
				
				JoinedDefaultValue = Pin->GetDefaultValue(PinOverride);
				DefaultValues = URigVMPin::SplitDefaultValue(JoinedDefaultValue);

#if UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED

				if (DefaultValues.Num() != DesiredArraySize)
				{
					FString FirstDefaultValue;
					if (Pin->GetArraySize() > 0)
					{
						FirstDefaultValue = Pin->GetSubPins()[0]->GetDefaultValue(PinOverride);
					}

					DefaultValues.Reset();
					for (int32 Index = 0; Index < DesiredArraySize; Index++)
					{
						DefaultValues.Add(FirstDefaultValue);
					}
				}
				
#endif
			}
			else
			{
				JoinedDefaultValue = Pin->GetDefaultValue(PinOverride);
				if(!JoinedDefaultValue.IsEmpty())
				{
					if(JoinedDefaultValue[0] == TCHAR('('))
					{
						DefaultValues = URigVMPin::SplitDefaultValue(JoinedDefaultValue);
					}
					else
					{
						DefaultValues.Add(JoinedDefaultValue);
					}
				}
			}

			while (DefaultValues.Num() < Pin->GetSubPins().Num())
			{
				DefaultValues.Add(FString());
			}
		}
		else if (URigVMEnumNode* EnumNode = Cast<URigVMEnumNode>(Pin->GetNode()))
		{
			FString EnumValueStr = EnumNode->GetDefaultValue(PinOverride);
			if (UEnum* Enum = EnumNode->GetEnum())
			{
				JoinedDefaultValue = FString::FromInt((int32)Enum->GetValueByNameString(EnumValueStr));
				DefaultValues.Add(JoinedDefaultValue);
			}
			else
			{
				JoinedDefaultValue = FString::FromInt(0);
				DefaultValues.Add(JoinedDefaultValue);
			}
		}
		else
		{
			JoinedDefaultValue = Pin->GetDefaultValue(PinOverride);
			DefaultValues.Add(JoinedDefaultValue);
		}

		UScriptStruct* ScriptStruct = Pin->GetScriptStruct();
		if (ScriptStruct == nullptr)
		{
			ScriptStruct = GetScriptStructForCPPType(BaseCPPType);
		}

		if (!Operand.IsValid())
		{
			const int32 NumSlices = 1;
			int32 Register = INDEX_NONE;

			// debug watch register might already exists - look for them by name
#if UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED
			if(bIsDebugValue && Memory.SupportsNames())
			{
				Register = Memory.GetIndex(RegisterName);
				if(Register != INDEX_NONE)
				{
					bool bFoundValidRegister = false;
					if(ScriptStruct)
					{
						if(Memory[Register].BaseCPPTypeObject == ScriptStruct)
						{
							bFoundValidRegister = true;
						}
					}
					else if(UEnum* Enum = Pin->GetEnum())
					{
						if(Memory[Register].BaseCPPTypeObject == Enum)
						{
							bFoundValidRegister = true;
						}
					}
					else
					{
						if(Memory[Register].BaseCPPType == *BaseCPPType)
						{
							bFoundValidRegister = true;
						}
					}

					if(bFoundValidRegister)
					{
						Operand = Memory.GetOperand(Register);
						if(ExistingOperand == nullptr)
						{
							WorkData.PinPathToOperand->Add(Hash, Operand);
						}
						return Operand;
					}
				}
			}
#else
			if(bIsDebugValue)
			{
				Operand = WorkData.FindProperty(MemoryType, RegisterName);
				if(Operand.IsValid())
				{
					FRigVMPropertyDescription Property = WorkData.GetProperty(Operand);
					if(Property.IsValid())
					{
						if(ExistingOperand == nullptr)
						{
							WorkData.PinPathToOperand->Add(Hash, Operand);
						}
						return Operand;
					}
				}
			}
#endif

#if UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED
			
			if (ScriptStruct)
			{
				TArray<uint8, TAlignedHeapAllocator<16>> Data;
				Data.AddUninitialized(ScriptStruct->GetStructureSize() * DefaultValues.Num());
				uint8* Ptr = Data.GetData();

				for (FString DefaultValue : DefaultValues)
				{
					ScriptStruct->InitializeStruct(Ptr, 1);
					if (!DefaultValue.IsEmpty() && DefaultValue != TEXT("()"))
					{
						FRigVMCompilerImportErrorContext ErrorPipe(this);
						ScriptStruct->ImportText(*DefaultValue, Ptr, nullptr, PPF_None, &ErrorPipe, ScriptStruct->GetName());
					}
					Ptr = Ptr + ScriptStruct->GetStructureSize();
				}

				Register = Memory.AddRegisterArray(!Pin->IsDynamicArray() && !bIsDebugValue, RegisterName, ScriptStruct->GetStructureSize(), DefaultValues.Num(), Pin->IsArray(), Data.GetData(), NumSlices, ERigVMRegisterType::Struct, ScriptStruct);
				check(Register != INDEX_NONE);
				
#if WITH_EDITORONLY_DATA
				Memory.Registers[Register].BaseCPPType = *ScriptStruct->GetStructCPPName();
				Memory.Registers[Register].BaseCPPTypeObject = ScriptStruct;
#endif
				ScriptStruct->DestroyStruct(Data.GetData(), DefaultValues.Num());

				Operand = Memory.GetOperand(Register);
			}
			else if (UEnum* Enum = Pin->GetEnum())
			{
				FRigVMByteArray Values;
				for (FString DefaultValue : DefaultValues)
				{
					if (DefaultValue.IsEmpty())
					{
						Values.Add((uint8)Enum->GetValueByIndex(0));
					}
					else
					{
						Values.Add((uint8)Enum->GetValueByNameString(DefaultValue));
					}
				}

				Register = Memory.AddRegisterArray<uint8>(!Pin->IsDynamicArray() && !bIsDebugValue, RegisterName, Values.Num(), Pin->IsArray(), Values.GetData(), NumSlices, ERigVMRegisterType::Plain, nullptr);
				check(Register != INDEX_NONE);
				
#if WITH_EDITORONLY_DATA
				Memory.Registers[Register].BaseCPPType = *Enum->CppType;
				Memory.Registers[Register].BaseCPPTypeObject = Enum;
#endif
				Operand = Memory.GetOperand(Register);
			}
			else if (BaseCPPType == TEXT("bool"))
			{
				TArray<bool> Values;
				for (FString DefaultValue : DefaultValues)
				{
					Values.Add((DefaultValue == TEXT("True")) || (DefaultValue == TEXT("true")) || (DefaultValue == TEXT("1")));
				}

				Register = Memory.AddRegisterArray<bool>(!Pin->IsDynamicArray() && !bIsDebugValue, RegisterName, Values.Num(), Pin->IsArray(), (uint8*)Values.GetData(), NumSlices, ERigVMRegisterType::Plain, nullptr);
				check(Register != INDEX_NONE);
				
#if WITH_EDITORONLY_DATA
				Memory.Registers[Register].BaseCPPType = *BaseCPPType;
				Memory.Registers[Register].BaseCPPTypeObject = nullptr;
#endif
				Operand = Memory.GetOperand(Register);
			}
			else if (BaseCPPType == TEXT("int32"))
			{
				TArray<int32> Values;
				for (FString DefaultValue : DefaultValues)
				{
					if (DefaultValue.IsEmpty())
					{
						Values.Add(0);
					}
					else
					{
						Values.Add(FCString::Atoi(*DefaultValue));
					}
				}
				Register = Memory.AddRegisterArray<int32>(!Pin->IsDynamicArray() && !bIsDebugValue, RegisterName, Values.Num(), Pin->IsArray(), (uint8*)Values.GetData(), NumSlices, ERigVMRegisterType::Plain, nullptr);
				check(Register != INDEX_NONE);

#if WITH_EDITORONLY_DATA
				Memory.Registers[Register].BaseCPPType = *BaseCPPType;
				Memory.Registers[Register].BaseCPPTypeObject = nullptr;
#endif
				Operand = Memory.GetOperand(Register);
			}
			else if (BaseCPPType == TEXT("float"))
			{
				TArray<float> Values;
				for (FString DefaultValue : DefaultValues)
				{
					if (DefaultValue.IsEmpty())
					{
						Values.Add(0.f);
					}
					else
					{
						Values.Add(FCString::Atof(*DefaultValue));
					}
				}
				Register = Memory.AddRegisterArray<float>(!Pin->IsDynamicArray() && !bIsDebugValue, RegisterName, Values.Num(), Pin->IsArray(), (uint8*)Values.GetData(), NumSlices, ERigVMRegisterType::Plain, nullptr);
				check(Register != INDEX_NONE);
				
#if WITH_EDITORONLY_DATA
				Memory.Registers[Register].BaseCPPType = *BaseCPPType;
				Memory.Registers[Register].BaseCPPTypeObject = nullptr;
#endif
				Operand = Memory.GetOperand(Register);
			}
			else if (BaseCPPType == TEXT("double"))
			{
				TArray<double> Values;
				for (FString DefaultValue : DefaultValues)
				{
					if (DefaultValue.IsEmpty())
					{
						Values.Add(0.0);
					}
					else
					{
						Values.Add(FCString::Atod(*DefaultValue));
					}
				}
				Register = Memory.AddRegisterArray<double>(!Pin->IsDynamicArray() && !bIsDebugValue, RegisterName, Values.Num(), Pin->IsArray(), (uint8*)Values.GetData(), NumSlices, ERigVMRegisterType::Plain, nullptr);
				check(Register != INDEX_NONE);
				
#if WITH_EDITORONLY_DATA
				Memory.Registers[Register].BaseCPPType = *BaseCPPType;
				Memory.Registers[Register].BaseCPPTypeObject = nullptr;
#endif
				Operand = Memory.GetOperand(Register);
			}
			else if (BaseCPPType == TEXT("FName"))
			{
				TArray<FName> Values;
				for (FString DefaultValue : DefaultValues)
				{
					Values.Add(*DefaultValue);
				}

				Register = Memory.AddRegisterArray<FName>(!Pin->IsDynamicArray() && !bIsDebugValue, RegisterName, Values.Num(), Pin->IsArray(), (uint8*)Values.GetData(), NumSlices, ERigVMRegisterType::Name, nullptr);
				check(Register != INDEX_NONE);
				
#if WITH_EDITORONLY_DATA
				Memory.Registers[Register].BaseCPPType = *BaseCPPType;
				Memory.Registers[Register].BaseCPPTypeObject = nullptr;
#endif
				Operand = Memory.GetOperand(Register);
			}
			else if (BaseCPPType == TEXT("FString"))
			{
				Register = Memory.AddRegisterArray<FString>(!Pin->IsDynamicArray() && !bIsDebugValue, RegisterName, DefaultValues.Num(), Pin->IsArray(), (uint8*)DefaultValues.GetData(), NumSlices, ERigVMRegisterType::String, nullptr);
				check(Register != INDEX_NONE);
				
#if WITH_EDITORONLY_DATA
				Memory.Registers[Register].BaseCPPType = *BaseCPPType;
				Memory.Registers[Register].BaseCPPTypeObject = nullptr;
#endif
				Operand = Memory.GetOperand(Register);
			}
			else
			{
				ensure(false);
			}

			Memory[Operand.GetRegisterIndex()].bIsArray = Pin->IsArray();
		}

#else

		}

		if(bIsDebugValue)
		{
			// debug values are always stored as arrays
			CPPType = RigVMTypeUtils::ArrayTypeFromBaseType(CPPType);
			JoinedDefaultValue = URigVMPin::GetDefaultValueForArray({ JoinedDefaultValue });
		}
		else if(Pin->GetDirection() == ERigVMPinDirection::Hidden && Pin->GetNode()->IsA<URigVMUnitNode>())
		{
			UScriptStruct* UnitStruct = Cast<URigVMUnitNode>(Pin->GetNode())->GetScriptStruct();
			const FProperty* Property = UnitStruct->FindPropertyByName(Pin->GetFName());
			check(Property);

			if (!Property->HasMetaData(FRigVMStruct::SingletonMetaName))
			{
				CPPType = RigVMTypeUtils::ArrayTypeFromBaseType(CPPType);
				JoinedDefaultValue = URigVMPin::GetDefaultValueForArray({ JoinedDefaultValue });
			}
		}

		if(Pin->GetDirection() == ERigVMPinDirection::Hidden)
		{
			JoinedDefaultValue.Empty();
		}
	
		Operand = WorkData.AddProperty(MemoryType, RegisterName, CPPType, Pin->GetCPPTypeObject(), JoinedDefaultValue);
		
#endif

		if (bIsParameter && !bIsLiteral && !bIsDebugValue)
		{
			URigVMParameterNode* ParameterNode = Cast<URigVMParameterNode>(Pin->GetNode());
			check(ParameterNode);
			FName Name = ParameterNode->GetParameterName();
			ERigVMParameterType ParameterType = ParameterNode->IsInput() ? ERigVMParameterType::Input : ERigVMParameterType::Output;
			FRigVMParameter Parameter(ParameterType, Name, Operand.GetRegisterIndex(), Pin->GetCPPType(), ScriptStruct);
			WorkData.VM->ParametersNameMap.FindOrAdd(Parameter.Name) = WorkData.VM->Parameters.Add(Parameter);
		}
	}
	ensure(Operand.IsValid());

	// Get all possible pins that lead to the same operand
	if(Settings.ASTSettings.bFoldAssignments)
	{
		// tbd: this functionality is only needed when there is a watch anywhere?
		//if(!WorkData.WatchedPins.IsEmpty())
		{
			const FRigVMCompilerWorkData::FRigVMASTProxyArray& PinProxies = FindProxiesWithSharedOperand(InVarExpr, WorkData);
			ensure(!PinProxies.IsEmpty());

			for (const FRigVMASTProxy& Proxy : PinProxies)
			{
				if (URigVMPin* VirtualPin = Cast<URigVMPin>(Proxy.GetSubject()))
				{
					FString VirtualPinHash = GetPinHash(VirtualPin, InVarExpr, bIsDebugValue);
					WorkData.PinPathToOperand->Add(VirtualPinHash, Operand);
				}	
			}
		}
	}
	else
	{
		if(ExistingOperand == nullptr)
		{
			WorkData.PinPathToOperand->Add(Hash, Operand);
		}
	}
	
	if (!bIsDebugValue)
	{
		check(!WorkData.ExprToOperand.Contains(InVarExpr));
		WorkData.ExprToOperand.Add(InVarExpr, Operand);
	}

	return Operand;
}

const FRigVMCompilerWorkData::FRigVMASTProxyArray& URigVMCompiler::FindProxiesWithSharedOperand(const FRigVMVarExprAST* InVarExpr, FRigVMCompilerWorkData& WorkData)
{
	const FRigVMASTProxy& InProxy = InVarExpr->GetProxy();
	if(const FRigVMCompilerWorkData::FRigVMASTProxyArray* ExistingArray = WorkData.CachedProxiesWithSharedOperand.Find(InProxy))
	{
		return *ExistingArray;
	}
	
	FRigVMCompilerWorkData::FRigVMASTProxyArray PinProxies, PinProxiesToProcess;
	const FRigVMCompilerWorkData::FRigVMASTProxySourceMap& ProxySources = *WorkData.ProxySources;
	const FRigVMCompilerWorkData::FRigVMASTProxyTargetsMap& ProxyTargets = WorkData.ProxyTargets;

	PinProxiesToProcess.Add(InProxy);

	for(int32 ProxyIndex = 0; ProxyIndex < PinProxiesToProcess.Num(); ProxyIndex++)
	{
		const FRigVMASTProxy& CurrentProxy = PinProxiesToProcess[ProxyIndex];

		if (CurrentProxy.IsValid())
		{
			if (URigVMPin* Pin = Cast<URigVMPin>(CurrentProxy.GetSubject()))
			{
				if (Pin->GetNode()->IsA<URigVMVariableNode>() || Pin->GetNode()->IsA<URigVMParameterNode>())
				{
					if (Pin->GetDirection() == ERigVMPinDirection::Input)
					{
						continue;
					}
				}
			}
			PinProxies.Add(CurrentProxy);
		}

		if(const FRigVMASTProxy* SourceProxy = ProxySources.Find(CurrentProxy))
		{
			if(SourceProxy->IsValid())
			{
				if (!PinProxies.Contains(*SourceProxy) && !PinProxiesToProcess.Contains(*SourceProxy))
				{
					PinProxiesToProcess.Add(*SourceProxy);
				}
			}
		}

		if(const FRigVMCompilerWorkData::FRigVMASTProxyArray* TargetProxies = WorkData.ProxyTargets.Find(CurrentProxy))
		{
			for(const FRigVMASTProxy& TargetProxy : *TargetProxies)
			{
				if(TargetProxy.IsValid())
				{
					if (!PinProxies.Contains(TargetProxy) && !PinProxiesToProcess.Contains(TargetProxy))
					{
						PinProxiesToProcess.Add(TargetProxy);
					}
				}
			}
		}
	}

	if (PinProxies.IsEmpty())
	{
		PinProxies.Add(InVarExpr->GetProxy());
	}

	// store the cache for all other proxies within this group
	for(const FRigVMASTProxy& CurrentProxy : PinProxies)
	{
		if(CurrentProxy != InProxy)
		{
			WorkData.CachedProxiesWithSharedOperand.Add(CurrentProxy, PinProxies);
		}
	}

	// finally store and return the cache the the input proxy
	return WorkData.CachedProxiesWithSharedOperand.Add(InProxy, PinProxies);
}

bool URigVMCompiler::ValidateNode(URigVMNode* InNode)
{
	check(InNode);

	if(InNode->HasUnknownTypePin())
	{
		static const FString UnknownTypeMessage = TEXT("Node @@ has unresolved pins of unknown type.");
		Settings.Report(EMessageSeverity::Error, InNode, UnknownTypeMessage);
		return false;
	}

	return true;
}

void URigVMCompiler::ReportInfo(const FString& InMessage)
{
	if (Settings.SurpressInfoMessages)
	{
		return;
	}
	Settings.Report(EMessageSeverity::Info, nullptr, InMessage);
}

void URigVMCompiler::ReportWarning(const FString& InMessage)
{
	Settings.Report(EMessageSeverity::Warning, nullptr, InMessage);
}

void URigVMCompiler::ReportError(const FString& InMessage)
{
	Settings.Report(EMessageSeverity::Error, nullptr, InMessage);
}
