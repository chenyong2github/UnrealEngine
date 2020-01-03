// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_GetControlTransform.h"
#include "Units/RigUnitContext.h"

FString FRigUnit_GetControlTransform::GetUnitLabel() const
{
	return FString::Printf(TEXT("Get Transform %s"), *Control.ToString());
}

FRigUnit_GetControlTransform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	const FRigControlHierarchy* Hierarchy = Context.GetControls();
	if (Hierarchy)
	{
		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				CachedControlIndex = Hierarchy->GetIndex(Control);
				break;				
			}
			case EControlRigState::Update:
			{
				if (CachedControlIndex != INDEX_NONE)
				{
					switch (Space)
					{
						case EBoneGetterSetterMode::GlobalSpace:
						{
							Transform = Hierarchy->GetGlobalTransform(CachedControlIndex);
							break;
						}
						case EBoneGetterSetterMode::LocalSpace:
						{
							Transform = Hierarchy->GetLocalTransform(CachedControlIndex);
							break;
						}
						default:
						{
							break;
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

#if WITH_DEV_AUTOMATION_TESTS
#include "Units/RigUnitTest.h"

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_GetControlTransform)
{
	ControlHierarchy.Add(TEXT("Root"), ERigControlType::Transform, NAME_None, NAME_None, FRigControlValue::Make(FTransform(FVector(1.f, 0.f, 0.f))));
	ControlHierarchy.Add(TEXT("ControlA"), ERigControlType::Transform, TEXT("Root"), NAME_None, FRigControlValue::Make(FTransform(FVector(1.f, 2.f, 3.f))));
	ControlHierarchy.Initialize();

	Unit.Control = TEXT("Unknown");
	Unit.Space = EBoneGetterSetterMode::GlobalSpace;
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(0.f, 0.f, 0.f)), TEXT("unexpected global transform"));
	Unit.Space = EBoneGetterSetterMode::LocalSpace;
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(0.f, 0.f, 0.f)), TEXT("unexpected local transform"));

	Unit.Control = TEXT("Root");
	Unit.Space = EBoneGetterSetterMode::GlobalSpace;
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(1.f, 0.f, 0.f)), TEXT("unexpected global transform"));
	Unit.Space = EBoneGetterSetterMode::LocalSpace;
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(1.f, 0.f, 0.f)), TEXT("unexpected local transform"));

	Unit.Control = TEXT("ControlA");
	Unit.Space = EBoneGetterSetterMode::GlobalSpace;
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(2.f, 2.f, 3.f)), TEXT("unexpected global transform"));
	Unit.Space = EBoneGetterSetterMode::LocalSpace;
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(1.f, 2.f, 3.f)), TEXT("unexpected local transform"));

	return true;
}
#endif