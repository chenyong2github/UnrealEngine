// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_GetTransform.h"
#include "Units/RigUnitContext.h"

FString FRigUnit_GetTransform::GetUnitLabel() const
{
	FString Initial = bInitial ? TEXT(" Initial") : FString();
	FString Type = StaticEnum<ERigElementType>()->GetDisplayNameTextByValue((int64)Item.Type).ToString();
	return FString::Printf(TEXT("Get Transform - %s%s"), *Type, *Initial);
}

FRigUnit_GetTransform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (const FRigHierarchyContainer* Hierarchy = Context.Hierarchy)
	{
		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				CachedIndex.Reset();
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
					if(bInitial || Context.State == EControlRigState::Init)
					{
						switch (Space)
						{
							case EBoneGetterSetterMode::GlobalSpace:
							{
								Transform = Hierarchy->GetInitialGlobalTransform(CachedIndex);
								break;
							}
							case EBoneGetterSetterMode::LocalSpace:
							{
								Transform = Hierarchy->GetInitialTransform(CachedIndex);
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
						switch (Space)
						{
							case EBoneGetterSetterMode::GlobalSpace:
							{
								Transform = Hierarchy->GetGlobalTransform(CachedIndex);
								break;
							}
							case EBoneGetterSetterMode::LocalSpace:
							{
								Transform = Hierarchy->GetLocalTransform(CachedIndex);
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
