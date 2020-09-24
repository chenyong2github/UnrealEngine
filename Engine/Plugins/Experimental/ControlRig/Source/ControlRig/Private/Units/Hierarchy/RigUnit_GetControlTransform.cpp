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
				CachedControlIndex.Reset();
			}
			case EControlRigState::Update:
			{
				if (!CachedControlIndex.UpdateCache(Control, Hierarchy))
				{
					UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Control '%s' is not valid."), *Control.ToString());
				}
				else
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
				CachedControlIndex.Reset();
			}
			case EControlRigState::Update:
			{
				if (!CachedControlIndex.UpdateCache(Control, Hierarchy))
				{
					UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Control '%s' is not valid."), *Control.ToString());
				}
				else
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


FRigUnit_GetControlInteger_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	const FRigControlHierarchy* Hierarchy = Context.GetControls();
	if (Hierarchy)
	{
		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				CachedControlIndex.Reset();
			}
			case EControlRigState::Update:
			{
				if (!CachedControlIndex.UpdateCache(Control, Hierarchy))
				{
					UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Control '%s' is not valid."), *Control.ToString());
				}
				else
				{
					IntegerValue = Hierarchy->GetValue(CachedControlIndex).Get<int32>();
					Minimum = Hierarchy->GetValue(CachedControlIndex, ERigControlValueType::Minimum).Get<int32>();
					Maximum = Hierarchy->GetValue(CachedControlIndex, ERigControlValueType::Maximum).Get<int32>();
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
				CachedControlIndex.Reset();
			}
			case EControlRigState::Update:
			{
				if (!CachedControlIndex.UpdateCache(Control, Hierarchy))
				{
					UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Control '%s' is not valid."), *Control.ToString());
				}
				else
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
				CachedControlIndex.Reset();
			}
			case EControlRigState::Update:
			{
				if (!CachedControlIndex.UpdateCache(Control, Hierarchy))
				{
					UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Control '%s' is not valid."), *Control.ToString());
				}
				else
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
				CachedControlIndex.Reset();
			}
			case EControlRigState::Update:
			{
				if (!CachedControlIndex.UpdateCache(Control, Hierarchy))
				{
					UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Control '%s' is not valid."), *Control.ToString());
				}
				else
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
				CachedControlIndex.Reset();
			}
			case EControlRigState::Update:
			{
				if (!CachedControlIndex.UpdateCache(Control, Hierarchy))
				{
					UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Control '%s' is not valid."), *Control.ToString());
				}
				else
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
	ControlHierarchy.Add(TEXT("Root"), ERigControlType::Transform, NAME_None, NAME_None, FTransform(FVector(1.f, 0.f, 0.f)));
	ControlHierarchy.Add(TEXT("ControlA"), ERigControlType::Transform, TEXT("Root"), NAME_None, FTransform(FVector(1.f, 2.f, 3.f)));
	ControlHierarchy.Initialize();

	Unit.Control = TEXT("Unknown");
	Unit.Space = EBoneGetterSetterMode::GlobalSpace;
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(0.f, 0.f, 0.f)), TEXT("unexpected global transform (0)"));
	Unit.Space = EBoneGetterSetterMode::LocalSpace;
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(0.f, 0.f, 0.f)), TEXT("unexpected local transform (0)"));

	Unit.Control = TEXT("Root");
	Unit.Space = EBoneGetterSetterMode::GlobalSpace;
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(1.f, 0.f, 0.f)), TEXT("unexpected global transform (1)"));
	Unit.Space = EBoneGetterSetterMode::LocalSpace;
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(0.f, 0.f, 0.f)), TEXT("unexpected local transform (1)"));

	Unit.Control = TEXT("ControlA");
	Unit.Space = EBoneGetterSetterMode::GlobalSpace;
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(2.f, 2.f, 3.f)), TEXT("unexpected global transform (2)"));
	Unit.Space = EBoneGetterSetterMode::LocalSpace;
	InitAndExecute();
	AddErrorIfFalse(Unit.Transform.GetTranslation().Equals(FVector(0.f, 0.f, 0.f)), TEXT("unexpected local transform (2)"));

	return true;
}
#endif