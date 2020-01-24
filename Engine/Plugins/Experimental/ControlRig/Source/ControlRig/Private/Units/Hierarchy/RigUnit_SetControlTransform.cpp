// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_SetControlTransform.h"
#include "Units/RigUnitContext.h"
#include "Math/ControlRigMathLibrary.h"

FRigUnit_SetControlBool_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigControlHierarchy* Hierarchy = ExecuteContext.GetControls();
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
					Hierarchy->SetValue(CachedControlIndex, FRigControlValue::Make<bool>(BoolValue));
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

FRigUnit_SetControlFloat_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigControlHierarchy* Hierarchy = ExecuteContext.GetControls();
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
					if(FMath::IsNearlyEqual(Weight, 1.f))
					{
						Hierarchy->SetValue(CachedControlIndex, FRigControlValue::Make<float>(FloatValue));
					}
					else
					{
						float PreviousValue = Hierarchy->GetValue(CachedControlIndex).Get<float>();
						Hierarchy->SetValue(CachedControlIndex, FRigControlValue::Make<float>(FMath::Lerp<float>(PreviousValue, FloatValue, FMath::Clamp<float>(Weight, 0.f, 1.f))));
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

FRigUnit_SetControlVector2D_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigControlHierarchy* Hierarchy = ExecuteContext.GetControls();
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
					if(FMath::IsNearlyEqual(Weight, 1.f))
					{
						Hierarchy->SetValue(CachedControlIndex, FRigControlValue::Make<FVector2D>(Vector));
					}
					else
					{
						FVector2D PreviousValue = Hierarchy->GetValue(CachedControlIndex).Get<FVector2D>();
						Hierarchy->SetValue(CachedControlIndex, FRigControlValue::Make<FVector2D>(FMath::Lerp<FVector2D>(PreviousValue, Vector, FMath::Clamp<float>(Weight, 0.f, 1.f))));
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

FRigUnit_SetControlVector_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigControlHierarchy* Hierarchy = ExecuteContext.GetControls();
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
					FTransform Transform = FTransform::Identity;
					if (Space == EBoneGetterSetterMode::GlobalSpace)
					{
						Transform = Hierarchy->GetGlobalTransform(CachedControlIndex);
					}

					if ((*Hierarchy)[CachedControlIndex].ControlType == ERigControlType::Position)
					{
						if(FMath::IsNearlyEqual(Weight, 1.f))
						{
							Transform.SetLocation(Vector);
						}
						else
						{
							FVector PreviousValue = Transform.GetLocation();
							Transform.SetLocation(FMath::Lerp<FVector>(PreviousValue, Vector, FMath::Clamp<float>(Weight, 0.f, 1.f)));
						}
					}
					else if ((*Hierarchy)[CachedControlIndex].ControlType == ERigControlType::Scale)
					{
						if(FMath::IsNearlyEqual(Weight, 1.f))
						{
							Transform.SetScale3D(Vector);
						}
						else
						{
							FVector PreviousValue = Transform.GetScale3D();
							Transform.SetScale3D(FMath::Lerp<FVector>(PreviousValue, Vector, FMath::Clamp<float>(Weight, 0.f, 1.f)));
						}
					}

					switch (Space)
					{
						case EBoneGetterSetterMode::GlobalSpace:
						{
							Hierarchy->SetGlobalTransform(CachedControlIndex, Transform);
							break;
						}
						case EBoneGetterSetterMode::LocalSpace:
						{
							Hierarchy->SetLocalTransform(CachedControlIndex, Transform);
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

FRigUnit_SetControlRotator_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigControlHierarchy* Hierarchy = ExecuteContext.GetControls();
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
					FTransform Transform = FTransform::Identity;
					if (Space == EBoneGetterSetterMode::GlobalSpace)
					{
						Transform = Hierarchy->GetGlobalTransform(CachedControlIndex);
					}

					FQuat Quat = FQuat(Rotator);
					if (FMath::IsNearlyEqual(Weight, 1.f))
					{
						Transform.SetRotation(Quat);
					}
					else
					{
						FQuat PreviousValue = Transform.GetRotation();
						Transform.SetRotation(FQuat::Slerp(PreviousValue, Quat, FMath::Clamp<float>(Weight, 0.f, 1.f)));
					}
					Transform.NormalizeRotation();

					switch (Space)
					{
						case EBoneGetterSetterMode::GlobalSpace:
						{
							Hierarchy->SetGlobalTransform(CachedControlIndex, Transform);
							break;
						}
						case EBoneGetterSetterMode::LocalSpace:
						{
							Hierarchy->SetLocalTransform(CachedControlIndex, Transform);
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

FRigUnit_SetControlTransform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigControlHierarchy* Hierarchy = ExecuteContext.GetControls();
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
							if(FMath::IsNearlyEqual(Weight, 1.f))
							{
								Hierarchy->SetGlobalTransform(CachedControlIndex, Transform);
							}
							else
							{
								FTransform PreviousTransform = Hierarchy->GetGlobalTransform(CachedControlIndex);
								Hierarchy->SetGlobalTransform(CachedControlIndex, FControlRigMathLibrary::LerpTransform(PreviousTransform, Transform, FMath::Clamp<float>(Weight, 0.f, 1.f)));
							}
							break;
						}
						case EBoneGetterSetterMode::LocalSpace:
						{
							if(FMath::IsNearlyEqual(Weight, 1.f))
							{
								Hierarchy->SetLocalTransform(CachedControlIndex, Transform);
							}
							else
							{
								FTransform PreviousTransform = Hierarchy->GetLocalTransform(CachedControlIndex);
								Hierarchy->SetLocalTransform(CachedControlIndex, FControlRigMathLibrary::LerpTransform(PreviousTransform, Transform, FMath::Clamp<float>(Weight, 0.f, 1.f)));
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
			default:
			{
				break;
			}
		}
	}
}
