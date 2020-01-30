// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_GetControlTransform.h"
#include "Units/RigUnitContext.h"

FRigUnit_GetControlBool_Execute()
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
			}
			case EControlRigState::Update:
			{
				if (CachedControlIndex != INDEX_NONE)
				{
					BoolValue = Hierarchy->GetValue(CachedControlIndex).Get<bool>();
				}
			}
			default:
			{
				break;
			}
		}
	}
}

FRigUnit_GetControlFloat_Execute()
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
			}
			case EControlRigState::Update:
			{
				if (CachedControlIndex != INDEX_NONE)
				{
					FloatValue = Hierarchy->GetValue(CachedControlIndex).Get<float>();
					Minimum = Hierarchy->GetValue(CachedControlIndex, ERigControlValueType::Minimum).Get<float>();
					Maximum = Hierarchy->GetValue(CachedControlIndex, ERigControlValueType::Maximum).Get<float>();
				}
			}
			default:
			{
				break;
			}
		}
	}
}

FRigUnit_GetControlVector2D_Execute()
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
			}
			case EControlRigState::Update:
			{
				if (CachedControlIndex != INDEX_NONE)
				{
					Vector = Hierarchy->GetValue(CachedControlIndex).Get<FVector2D>();
					Minimum = Hierarchy->GetValue(CachedControlIndex, ERigControlValueType::Minimum).Get<FVector2D>();
					Maximum = Hierarchy->GetValue(CachedControlIndex, ERigControlValueType::Maximum).Get<FVector2D>();
				}
			}
			default:
			{
				break;
			}
		}
	}
}

FRigUnit_GetControlVector_Execute()
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
			}
			case EControlRigState::Update:
			{
				if (CachedControlIndex != INDEX_NONE)
				{
					FTransform Transform = FTransform::Identity;
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

					if((*Hierarchy)[CachedControlIndex].ControlType == ERigControlType::Position)
					{
						Vector = Transform.GetLocation();
					}
					else if((*Hierarchy)[CachedControlIndex].ControlType == ERigControlType::Scale)
					{
						Vector = Transform.GetScale3D();
					}

					Minimum = Hierarchy->GetValue(CachedControlIndex, ERigControlValueType::Minimum).Get<FVector>();
					Maximum = Hierarchy->GetValue(CachedControlIndex, ERigControlValueType::Maximum).Get<FVector>();
				}
			}
			default:
			{
				break;
			}
		}
	}
}

FRigUnit_GetControlRotator_Execute()
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
			}
			case EControlRigState::Update:
			{
				if (CachedControlIndex != INDEX_NONE)
				{
					FTransform Transform = FTransform::Identity;
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

					Rotator = Transform.GetRotation().Rotator();
					Minimum = Hierarchy->GetValue(CachedControlIndex, ERigControlValueType::Minimum).Get<FRotator>();
					Maximum = Hierarchy->GetValue(CachedControlIndex, ERigControlValueType::Maximum).Get<FRotator>();
				}
			}
			default:
			{
				break;
			}
		}
	}
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
					Minimum = Hierarchy->GetValue(CachedControlIndex, ERigControlValueType::Minimum).Get<FTransform>();
					Maximum = Hierarchy->GetValue(CachedControlIndex, ERigControlValueType::Maximum).Get<FTransform>();
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
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(0.f, 0.f, 0.f)), TEXT("unexpected local transform"));

	Unit.Control = TEXT("ControlA");
	Unit.Space = EBoneGetterSetterMode::GlobalSpace;
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(2.f, 2.f, 3.f)), TEXT("unexpected global transform"));
	Unit.Space = EBoneGetterSetterMode::LocalSpace;
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(0.f, 0.f, 0.f)), TEXT("unexpected local transform"));

	return true;
}
#endif