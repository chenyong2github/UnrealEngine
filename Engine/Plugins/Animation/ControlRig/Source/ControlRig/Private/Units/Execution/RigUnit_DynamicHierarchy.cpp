// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_DynamicHierarchy.h"
#include "Rigs/RigHierarchyController.h"
#include "Units/RigUnitContext.h"

FRigUnit_AddParent_Execute()
{
	if((Context.State != EControlRigState::Update) || (ExecuteContext.Hierarchy == nullptr))
	{
		return;
	}

	FRigTransformElement* ChildElement = ExecuteContext.Hierarchy->Find<FRigTransformElement>(Child);
	if(ChildElement == nullptr)
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Child item %s does not exist."), *Child.ToString())
		return;
	}

	FRigTransformElement* ParentElement = ExecuteContext.Hierarchy->Find<FRigTransformElement>(Parent);
	if(ParentElement == nullptr)
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Parent item %s does not exist."), *Parent.ToString())
		return;
	}

	if(URigHierarchyController* Controller = ExecuteContext.Hierarchy->GetController(true))
	{
		Controller->AddParent(ChildElement, ParentElement, 0.f, true, false);
	}
}

FRigUnit_SwitchParent_Execute()
{
	if((Context.State != EControlRigState::Update) || (ExecuteContext.Hierarchy == nullptr))
	{
		return;
	}

	FRigTransformElement* ChildElement = ExecuteContext.Hierarchy->Find<FRigTransformElement>(Child);
	if(ChildElement == nullptr)
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Child item %s does not exist."), *Child.ToString())
		return;
	}
	if(!ChildElement->IsA<FRigMultiParentElement>())
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Child item %s cannot be space switched (only Nulls and Controls can)."), *Child.ToString())
		return;
	}

	FRigTransformElement* ParentElement = nullptr;

	if(Mode == ERigSwitchParentMode::ParentItem)
	{
		ParentElement = ExecuteContext.Hierarchy->Find<FRigTransformElement>(Parent);
		if(ParentElement == nullptr)
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Parent item %s does not exist."), *Parent.ToString())
			return;
		}

		if(!ParentElement->IsA<FRigTransformElement>())
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Parent item %s does not have a transform."), *Parent.ToString())
			return;
		}
	}

	const ERigTransformType::Type TransformTypeToMaintain =
		bMaintainGlobal ? ERigTransformType::CurrentGlobal : ERigTransformType::CurrentLocal;
	
	const FTransform Transform = ExecuteContext.Hierarchy->GetTransform(ChildElement, TransformTypeToMaintain);

	switch(Mode)
	{
		case ERigSwitchParentMode::World:
		{
			if(!ExecuteContext.Hierarchy->SwitchToWorldSpace(ChildElement, false, true))
			{
				return;
			}
			break;
		}
		case ERigSwitchParentMode::DefaultParent:
		{
			if(!ExecuteContext.Hierarchy->SwitchToDefaultParent(ChildElement, false, true))
			{
				return;
			}
			break;
		}
		case ERigSwitchParentMode::ParentItem:
		default:
		{
			FString FailureReason;
			URigHierarchy::TElementDependencyMap DependencyMap;
#if WITH_EDITOR
			if(ExecuteContext.VM)
			{
				DependencyMap = ExecuteContext.Hierarchy->GetDependenciesForVM(ExecuteContext.VM);
			}
#endif
			if(!ExecuteContext.Hierarchy->SwitchToParent(ChildElement, ParentElement, false, true, DependencyMap, &FailureReason))
			{
				if(!FailureReason.IsEmpty())
				{
					UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("%s"), *FailureReason);
				}
				return;
			}
			break;
		}
	}
	
	ExecuteContext.Hierarchy->SetTransform(ChildElement, Transform, TransformTypeToMaintain, true);
}

FRigUnit_HierarchyGetParentWeights_Execute()
{
	FRigUnit_HierarchyGetParentWeightsArray::StaticExecute(RigVMExecuteContext, Child, Weights, Parents.Keys, Context);
}

FRigUnit_HierarchyGetParentWeightsArray_Execute()
{
	if((Context.State != EControlRigState::Update) || (Context.Hierarchy == nullptr))
	{
		return;
	}

	FRigBaseElement* ChildElement = Context.Hierarchy->Find(Child);
	if(ChildElement == nullptr)
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Item %s does not exist."), *Child.ToString())
		return;
	}
	
	Weights = Context.Hierarchy->GetParentWeightArray(ChildElement, false);
	Parents = Context.Hierarchy->GetParents(ChildElement->GetKey(), false);
}

FRigUnit_HierarchySetParentWeights_Execute()
{
	if((Context.State != EControlRigState::Update) || (ExecuteContext.Hierarchy == nullptr))
	{
		return;
	}

	FRigBaseElement* ChildElement = Context.Hierarchy->Find(Child);
	if(ChildElement == nullptr)
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Item %s does not exist."), *Child.ToString())
		return;
	}

	if(Weights.Num() != ExecuteContext.Hierarchy->GetNumberOfParents(ChildElement))
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Provided incorrect number of weights(%d) for %s - expected %d."), Weights.Num(), *Child.ToString(), ExecuteContext.Hierarchy->GetNumberOfParents(Child))
		return;
	}

	ExecuteContext.Hierarchy->SetParentWeightArray(ChildElement, Weights, false, true);
}
