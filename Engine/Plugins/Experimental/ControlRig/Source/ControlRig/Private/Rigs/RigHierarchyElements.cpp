// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rigs/RigHierarchyElements.h"
#include "Rigs/RigHierarchy.h"
#include "Units/RigUnitContext.h"
#include "UObject/AnimObjectVersion.h"

////////////////////////////////////////////////////////////////////////////////
// FRigBaseElement
////////////////////////////////////////////////////////////////////////////////

void FRigBaseElement::Serialize(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase)
{
	Ar.UsingCustomVersion(FAnimObjectVersion::GUID);

	if (Ar.IsSaving() || Ar.IsObjectReferenceCollector())
	{
		Save(Ar, Hierarchy, SerializationPhase);
	}
	else if (Ar.IsLoading())
	{
		Load(Ar, Hierarchy, SerializationPhase);
	}
	else
	{
		checkNoEntry();
	}
}

void FRigBaseElement::Save(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase)
{
	if(SerializationPhase == ESerializationPhase::StaticData)
	{
		Ar << Key;
	}
}

void FRigBaseElement::Load(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase)
{
	if(SerializationPhase == ESerializationPhase::StaticData)
	{
		FRigElementKey LoadedKey;
	
		Ar << LoadedKey;

		ensure(LoadedKey.Type == Key.Type);
		Key = LoadedKey;
	}
}

////////////////////////////////////////////////////////////////////////////////
// FRigComputedTransform
////////////////////////////////////////////////////////////////////////////////

void FRigComputedTransform::Save(FArchive& Ar)
{
	Ar << Transform;
	Ar << bDirty;
}

void FRigComputedTransform::Load(FArchive& Ar)
{
	// load and save are identical
	Save(Ar);
}

////////////////////////////////////////////////////////////////////////////////
// FRigLocalAndGlobalTransform
////////////////////////////////////////////////////////////////////////////////

void FRigLocalAndGlobalTransform::Save(FArchive& Ar)
{
	Local.Save(Ar);
	Global.Save(Ar);
}

void FRigLocalAndGlobalTransform::Load(FArchive& Ar)
{
	Local.Load(Ar);
	Global.Load(Ar);
}

////////////////////////////////////////////////////////////////////////////////
// FRigCurrentAndInitialTransform
////////////////////////////////////////////////////////////////////////////////

void FRigCurrentAndInitialTransform::Save(FArchive& Ar)
{
	Current.Save(Ar);
	Initial.Save(Ar);
}

void FRigCurrentAndInitialTransform::Load(FArchive& Ar)
{
	Current.Load(Ar);
	Initial.Load(Ar);
}

////////////////////////////////////////////////////////////////////////////////
// FRigTransformElement
////////////////////////////////////////////////////////////////////////////////

void FRigTransformElement::Save(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase)
{
	Super::Save(Ar, Hierarchy, SerializationPhase);

	if(SerializationPhase == ESerializationPhase::StaticData)
	{
		Pose.Save(Ar);
	}
}

void FRigTransformElement::Load(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase)
{
	Super::Load(Ar, Hierarchy, SerializationPhase);

	if(SerializationPhase == ESerializationPhase::StaticData)
	{
		Pose.Load(Ar);
	}
}

void FRigTransformElement::CopyPose(FRigBaseElement* InOther, bool bCurrent, bool bInitial)
{
	Super::CopyPose(InOther, bCurrent, bInitial);

	if(FRigTransformElement* Other = Cast<FRigTransformElement>(InOther))
	{
		if(bCurrent)
		{
			Pose.Current = Other->Pose.Current;
		}
		if(bInitial)
		{
			Pose.Initial = Other->Pose.Initial;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
// FRigSingleParentElement
////////////////////////////////////////////////////////////////////////////////

void FRigSingleParentElement::Save(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase)
{
	Super::Save(Ar, Hierarchy, SerializationPhase);

	if(SerializationPhase == ESerializationPhase::InterElementData)
	{
		FRigElementKey ParentKey;
		if(ParentElement)
		{
			ParentKey = ParentElement->GetKey();
		}
		Ar << ParentKey;
	}
}

void FRigSingleParentElement::Load(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase)
{
	Super::Load(Ar, Hierarchy, SerializationPhase);

	if(SerializationPhase == ESerializationPhase::InterElementData)
	{
		FRigElementKey ParentKey;
		Ar << ParentKey;

		if(ParentKey.IsValid())
		{
			ParentElement = Hierarchy->FindChecked<FRigTransformElement>(ParentKey);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
// FRigMultiParentElement
////////////////////////////////////////////////////////////////////////////////

void FRigMultiParentElement::Save(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase)
{
	Super::Save(Ar, Hierarchy, SerializationPhase);

	if(SerializationPhase == ESerializationPhase::StaticData)
	{
		Parent.Save(Ar);

		int32 NumParents = ParentElements.Num();
		Ar << NumParents;
	}
	else if(SerializationPhase == ESerializationPhase::InterElementData)
	{
		for(int32 ParentIndex = 0; ParentIndex < ParentElements.Num(); ParentIndex++)
		{
			FRigElementKey ParentKey;
			if(ParentElements[ParentIndex])
			{
				ParentKey = ParentElements[ParentIndex]->GetKey();
			}

			Ar << ParentKey;
			Ar << ParentWeightsInitial[ParentIndex];
			Ar << ParentWeights[ParentIndex];
		}
	}
}

void FRigMultiParentElement::Load(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase)
{
	Super::Load(Ar, Hierarchy, SerializationPhase);

	if(SerializationPhase == ESerializationPhase::StaticData)
	{
		Parent.Load(Ar);

		int32 NumParents = 0;
		Ar << NumParents;

		ParentElements.SetNumZeroed(NumParents);
		ParentWeights.SetNumZeroed(NumParents);
		ParentWeightsInitial.SetNumZeroed(NumParents);
	}
	else if(SerializationPhase == ESerializationPhase::InterElementData)
	{
		for(int32 ParentIndex = 0; ParentIndex < ParentElements.Num(); ParentIndex++)
		{
			FRigElementKey ParentKey;
			Ar << ParentKey;
			ensure(ParentKey.IsValid());

			ParentElements[ParentIndex] = Hierarchy->FindChecked<FRigTransformElement>(ParentKey);

			Ar << ParentWeightsInitial[ParentIndex];
			Ar << ParentWeights[ParentIndex];

			IndexLookup.Add(ParentKey, ParentIndex);
		}
	}
}

void FRigMultiParentElement::CopyPose(FRigBaseElement* InOther, bool bCurrent, bool bInitial)
{
	Super::CopyPose(InOther, bCurrent, bInitial);

	if(FRigMultiParentElement* Other = Cast<FRigMultiParentElement>(InOther))
	{
		if(bCurrent)
		{
			Parent.Current = Other->Parent.Current;
		}
		if(bInitial)
		{
			Parent.Initial = Other->Parent.Initial;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
// FRigBoneElement
////////////////////////////////////////////////////////////////////////////////

void FRigBoneElement::Save(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase)
{
	Super::Save(Ar, Hierarchy, SerializationPhase);

	if(SerializationPhase == ESerializationPhase::StaticData)
	{
		static const UEnum* BoneTypeEnum = StaticEnum<ERigBoneType>();
		FName TypeName = BoneTypeEnum->GetNameByValue((int64)BoneType);
		Ar << TypeName;
	}
}

void FRigBoneElement::Load(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase)
{
	Super::Load(Ar, Hierarchy, SerializationPhase);

	if(SerializationPhase == ESerializationPhase::StaticData)
	{
		static const UEnum* BoneTypeEnum = StaticEnum<ERigBoneType>();
		FName TypeName;
		Ar << TypeName;
		BoneType = (ERigBoneType)BoneTypeEnum->GetValueByName(TypeName);
	}
}

////////////////////////////////////////////////////////////////////////////////
// FRigControlSettings
////////////////////////////////////////////////////////////////////////////////

FRigControlSettings::FRigControlSettings()
: ControlType(ERigControlType::Transform)
, DisplayName(NAME_None)
, PrimaryAxis(ERigControlAxis::X)
, bIsCurve(false)
, bAnimatable(true)
, bLimitTranslation(false)
, bLimitRotation(false)
, bLimitScale(false)
, bDrawLimits(true)
, MinimumValue()
, MaximumValue()
, bGizmoEnabled(true)
, bGizmoVisible(true)
, GizmoName(TEXT("Gizmo"))
, GizmoColor(FLinearColor::Red)
, bIsTransientControl(false)
, ControlEnum(nullptr)
{
}

void FRigControlSettings::Save(FArchive& Ar)
{
	static const UEnum* ControlTypeEnum = StaticEnum<ERigControlType>();
	static const UEnum* ControlAxisEnum = StaticEnum<ERigControlAxis>();

	FName ControlTypeName = ControlTypeEnum->GetNameByValue((int64)ControlType);
	FName PrimaryAxisName = ControlAxisEnum->GetNameByValue((int64)PrimaryAxis);
	FTransform MinimumTransform = MinimumValue.GetAsTransform(ControlType, PrimaryAxis);
	FTransform MaximumTransform = MaximumValue.GetAsTransform(ControlType, PrimaryAxis);
	FString ControlEnumPathName;
	if(ControlEnum)
	{
		ControlEnumPathName = ControlEnum->GetPathName();
	}

	Ar << ControlTypeName;
	Ar << DisplayName;
	Ar << PrimaryAxisName;
	Ar << bIsCurve;
	Ar << bAnimatable;
	Ar << bLimitTranslation;
	Ar << bLimitRotation;
	Ar << bLimitScale;
	Ar << bDrawLimits;
	Ar << MinimumTransform;
	Ar << MaximumTransform;
	Ar << bGizmoEnabled;
	Ar << bGizmoVisible;
	Ar << GizmoName;
	Ar << GizmoColor;
	Ar << bIsTransientControl;
	Ar << ControlEnumPathName;
}

void FRigControlSettings::Load(FArchive& Ar)
{
	static const UEnum* ControlTypeEnum = StaticEnum<ERigControlType>();
	static const UEnum* ControlAxisEnum = StaticEnum<ERigControlAxis>();

	FName ControlTypeName, PrimaryAxisName;
	FTransform MinimumTransform, MaximumTransform;
	FString ControlEnumPathName;

	Ar << ControlTypeName;
	Ar << DisplayName;
	Ar << PrimaryAxisName;
	Ar << bIsCurve;
	Ar << bAnimatable;
	Ar << bLimitTranslation;
	Ar << bLimitRotation;
	Ar << bLimitScale;
	Ar << bDrawLimits;
	Ar << MinimumTransform;
	Ar << MaximumTransform;
	Ar << bGizmoEnabled;
	Ar << bGizmoVisible;
	Ar << GizmoName;
	Ar << GizmoColor;
	Ar << bIsTransientControl;
	Ar << ControlEnumPathName;

	ControlType = (ERigControlType)ControlTypeEnum->GetValueByName(ControlTypeName);
	PrimaryAxis = (ERigControlAxis)ControlAxisEnum->GetValueByName(PrimaryAxisName);
	MinimumValue.SetFromTransform(MinimumTransform, ControlType, PrimaryAxis);
	MaximumValue.SetFromTransform(MaximumTransform, ControlType, PrimaryAxis);

	ControlEnum = nullptr;
	if(!ControlEnumPathName.IsEmpty())
	{
		ControlEnum = FindObject<UEnum>(ANY_PACKAGE, *ControlEnumPathName);
	}
}

////////////////////////////////////////////////////////////////////////////////
// FRigControlElement
////////////////////////////////////////////////////////////////////////////////

void FRigControlElement::Save(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase)
{
	Super::Save(Ar, Hierarchy, SerializationPhase);

	if(SerializationPhase == ESerializationPhase::StaticData)
	{
		Settings.Save(Ar);
		Offset.Save(Ar);
		Gizmo.Save(Ar);
	}
}

void FRigControlElement::Load(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase)
{
	Super::Load(Ar, Hierarchy, SerializationPhase);

	if(SerializationPhase == ESerializationPhase::StaticData)
	{
		Settings.Load(Ar);
		Offset.Load(Ar);
		Gizmo.Load(Ar);
	}
}

void FRigControlElement::CopyPose(FRigBaseElement* InOther, bool bCurrent, bool bInitial)
{
	Super::CopyPose(InOther, bCurrent, bInitial);
	
	if(FRigControlElement* Other = Cast<FRigControlElement>(InOther))
	{
		if(bCurrent)
		{
			Offset.Current = Other->Offset.Current;
			Gizmo.Current = Other->Gizmo.Current;
		}
		if(bInitial)
		{
			Offset.Initial = Other->Offset.Initial;
			Gizmo.Initial = Other->Gizmo.Initial;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
// FRigCurveElement
////////////////////////////////////////////////////////////////////////////////

void FRigCurveElement::Save(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase)
{
	Super::Save(Ar, Hierarchy, SerializationPhase);

	if(SerializationPhase == ESerializationPhase::StaticData)
	{
		Ar << Value;
	}
}

void FRigCurveElement::Load(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase)
{
	Super::Load(Ar, Hierarchy, SerializationPhase);

	if(SerializationPhase == ESerializationPhase::StaticData)
	{
		Ar << Value;
	}
}

void FRigCurveElement::CopyPose(FRigBaseElement* InOther, bool bCurrent, bool bInitial)
{
	Super::CopyPose(InOther, bCurrent, bInitial);
	
	if(FRigCurveElement* Other = Cast<FRigCurveElement>(InOther))
	{
		Value = Other->Value;
	}
}

////////////////////////////////////////////////////////////////////////////////
// FRigRigidBodySettings
////////////////////////////////////////////////////////////////////////////////

FRigRigidBodySettings::FRigRigidBodySettings()
	: Mass(1.f)
{
}

void FRigRigidBodySettings::Save(FArchive& Ar)
{
	Ar << Mass;
}

void FRigRigidBodySettings::Load(FArchive& Ar)
{
	Ar << Mass;
}

////////////////////////////////////////////////////////////////////////////////
// FRigRigidBodyElement
////////////////////////////////////////////////////////////////////////////////

void FRigRigidBodyElement::Save(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase)
{
	Super::Save(Ar, Hierarchy, SerializationPhase);

	if(SerializationPhase == ESerializationPhase::StaticData)
	{
		Settings.Save(Ar);
	}
}

void FRigRigidBodyElement::Load(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase)
{
	Super::Load(Ar, Hierarchy, SerializationPhase);

	if(SerializationPhase == ESerializationPhase::StaticData)
	{
		Settings.Load(Ar);
	}
}

////////////////////////////////////////////////////////////////////////////////
// FRigAuxiliaryElement
////////////////////////////////////////////////////////////////////////////////

void FRigAuxiliaryElement::Save(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase)
{
	Super::Save(Ar, Hierarchy, SerializationPhase);
}

void FRigAuxiliaryElement::Load(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase)
{
	Super::Load(Ar, Hierarchy, SerializationPhase);
}

FTransform FRigAuxiliaryElement::GetAuxiliaryWorldTransform(const FRigUnitContext* InContext, bool bInitial) const
{
	if(GetWorldTransformDelegate.IsBound())
	{
		return GetWorldTransformDelegate.Execute(InContext, GetKey(), bInitial);
	}
	return FTransform::Identity;
}

void FRigAuxiliaryElement::CopyPose(FRigBaseElement* InOther, bool bCurrent, bool bInitial)
{
	Super::CopyPose(InOther, bCurrent, bInitial);
	
	if(FRigAuxiliaryElement* Other = Cast<FRigAuxiliaryElement>(InOther))
	{
		if(Other->GetWorldTransformDelegate.IsBound())
		{
			GetWorldTransformDelegate = Other->GetWorldTransformDelegate;
		}
	}
}
