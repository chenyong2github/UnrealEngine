// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Hierarchy/RigUnit_SetTransform.h"
#include "RigUnit_GetTransform.h"
#include "Units/RigUnitContext.h"
#include "Units/Execution/RigUnit_PrepareForExecution.h"
#include "Units/Execution/RigUnit_InverseExecution.h"

FString FRigUnit_SetTransform::GetUnitLabel() const
{
	FString Initial = bInitial ? TEXT(" Initial") : FString();
	FString Type = StaticEnum<ERigElementType>()->GetDisplayNameTextByValue((int64)Item.Type).ToString();
	return FString::Printf(TEXT("Set Transform - %s%s"), *Type, *Initial);
}

FRigUnit_SetTransform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Weight < SMALL_NUMBER)
	{
		return;
	}

	if (FRigHierarchyContainer* Hierarchy = ExecuteContext.Hierarchy)
	{
		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				CachedIndex.Reset();

				if (ExecuteContext.EventName == FRigUnit_InverseExecution::EventName)
				{
					return;
				}
			}
			case EControlRigState::Update:
			{
				if (!CachedIndex.UpdateCache(Item, Hierarchy))
				{
					if(Context.State != EControlRigState::Init)
					{
						UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Item '%s' is not valid."), *Item.ToString());
					}
				}
				else
				{
					if(bInitial)
					{
						// special case controls - since we want their offset to behave differently
						if (CachedIndex.GetKey().Type == ERigElementType::Control)
						{
							FTransform OffsetTransform = Transform;

							if (Space == EBoneGetterSetterMode::GlobalSpace)
							{
								FRigElementKey ParentKey = Hierarchy->GetParentKey(CachedIndex);
								if (ParentKey.IsValid())
								{
									FTransform ParentTransform = Hierarchy->GetInitialGlobalTransform(ParentKey);
									OffsetTransform = OffsetTransform.GetRelativeTransform(ParentTransform);
								}
							}

							FRigControl& Control = (*ExecuteContext.GetControls())[CachedIndex];
							Control.OffsetTransform = OffsetTransform;

							if (Control.ControlType == ERigControlType::Transform ||
								Control.ControlType == ERigControlType::TransformNoScale || 
								Control.ControlType == ERigControlType::Position || 
								Control.ControlType == ERigControlType::Rotator || 
								Control.ControlType == ERigControlType::Scale)
							{
								Control.SetValueFromTransform(FTransform::Identity, ERigControlValueType::Initial);
								Control.SetValueFromTransform(FTransform::Identity, ERigControlValueType::Current);
							}

							return;
						}

						switch (Space)
						{
							case EBoneGetterSetterMode::GlobalSpace:
							{
								Hierarchy->SetInitialGlobalTransform(CachedIndex, Transform);

								if (ExecuteContext.EventName == FRigUnit_PrepareForExecution::EventName)
								{
									Hierarchy->SetGlobalTransform(CachedIndex, Transform);
								}
								break;
							}
							case EBoneGetterSetterMode::LocalSpace:
							{
								Hierarchy->SetInitialTransform(CachedIndex, Transform);

								if (ExecuteContext.EventName == FRigUnit_PrepareForExecution::EventName)
								{
									Hierarchy->SetLocalTransform(CachedIndex, Transform);
								}
								break;
							}
							default:
							{
								break;
							}
						}
					}
					else
					{
						FTransform WeightedTransform = Transform;
						if (Weight < 1.f - SMALL_NUMBER)
						{
							FTransform PreviousTransform = WeightedTransform;
							switch (Space)
							{
								case EBoneGetterSetterMode::GlobalSpace:
								{
									PreviousTransform = Hierarchy->GetGlobalTransform(CachedIndex);
									break;
								}
								case EBoneGetterSetterMode::LocalSpace:
								{
									PreviousTransform = Hierarchy->GetLocalTransform(CachedIndex);
									break;
								}
								default:
								{
									break;
								}
							}
							WeightedTransform = FControlRigMathLibrary::LerpTransform(PreviousTransform, WeightedTransform, Weight);
						}

						switch (Space)
						{
							case EBoneGetterSetterMode::GlobalSpace:
							{
								Hierarchy->SetGlobalTransform(CachedIndex, WeightedTransform, bPropagateToChildren);
								break;
							}
							case EBoneGetterSetterMode::LocalSpace:
							{
								Hierarchy->SetLocalTransform(CachedIndex, WeightedTransform, bPropagateToChildren);
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
			default:
			{
				break;
			}
		}
	}
}

FString FRigUnit_SetTranslation::GetUnitLabel() const
{
	FString Type = StaticEnum<ERigElementType>()->GetDisplayNameTextByValue((int64)Item.Type).ToString();
	return FString::Printf(TEXT("Set Translation - %s"), *Type);
}

FRigUnit_SetTranslation_Execute()
{
	FTransform Transform = FTransform::Identity;
	FRigUnit_GetTransform::StaticExecute(RigVMExecuteContext, Item, Space, false, Transform, CachedIndex, Context);
	Transform.SetLocation(Translation);
	FRigUnit_SetTransform::StaticExecute(RigVMExecuteContext, Item, Space, false, Transform, Weight, bPropagateToChildren, CachedIndex, ExecuteContext, Context);
}

FString FRigUnit_SetRotation::GetUnitLabel() const
{
	FString Type = StaticEnum<ERigElementType>()->GetDisplayNameTextByValue((int64)Item.Type).ToString();
	return FString::Printf(TEXT("Set Rotation - %s"), *Type);
}

FRigUnit_SetRotation_Execute()
{
	FTransform Transform = FTransform::Identity;
	FRigUnit_GetTransform::StaticExecute(RigVMExecuteContext, Item, Space, false, Transform, CachedIndex, Context);
	Transform.SetRotation(Rotation);
	FRigUnit_SetTransform::StaticExecute(RigVMExecuteContext, Item, Space, false, Transform, Weight, bPropagateToChildren, CachedIndex, ExecuteContext, Context);
}

FString FRigUnit_SetScale::GetUnitLabel() const
{
	FString Type = StaticEnum<ERigElementType>()->GetDisplayNameTextByValue((int64)Item.Type).ToString();
	return FString::Printf(TEXT("Set Scale - %s"), *Type);
}

FRigUnit_SetScale_Execute()
{
	FTransform Transform = FTransform::Identity;
	FRigUnit_GetTransform::StaticExecute(RigVMExecuteContext, Item, Space, false, Transform, CachedIndex, Context);
	Transform.SetScale3D(Scale);
	FRigUnit_SetTransform::StaticExecute(RigVMExecuteContext, Item, Space, false, Transform, Weight, bPropagateToChildren, CachedIndex, ExecuteContext, Context);
}
