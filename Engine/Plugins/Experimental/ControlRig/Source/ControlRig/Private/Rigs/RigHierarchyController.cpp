// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rigs/RigHierarchyController.h"

#if WITH_EDITOR
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "EditorStyleSet.h"
#include "ScopedTransaction.h"
#include "Engine/SkeletalMesh.h"
#endif

#include "ControlRig.h"

////////////////////////////////////////////////////////////////////////////////
// URigHierarchyController
////////////////////////////////////////////////////////////////////////////////

URigHierarchyController::~URigHierarchyController()
{
}

void URigHierarchyController::SetHierarchy(URigHierarchy* InHierarchy)
{
	if(Hierarchy)
	{
		if(!Hierarchy->HasAnyFlags(RF_BeginDestroyed) && Hierarchy->IsValidLowLevel())
		{
			Hierarchy->OnModified().RemoveAll(this);
		}
		Hierarchy->LastControllerPtr.Reset();
	}

	Hierarchy = InHierarchy;

	if(Hierarchy)
	{
		Hierarchy->OnModified().AddUObject(this, &URigHierarchyController::HandleHierarchyModified);
		Hierarchy->LastControllerPtr = this;
	}
}

bool URigHierarchyController::SelectElement(FRigElementKey InKey, bool bSelect, bool bClearSelection)
{
	if(!IsValid())
	{
		return false;
	}

	if(bClearSelection)
	{
		TArray<FRigElementKey> KeysToSelect;
		KeysToSelect.Add(InKey);
		return SetSelection(KeysToSelect);
	}

	if(URigHierarchy* HierarchyForSelection = Hierarchy->HierarchyForSelectionPtr.Get())
	{
		if(URigHierarchyController* ControllerForSelection = HierarchyForSelection->GetController())
		{
			return ControllerForSelection->SelectElement(InKey, bSelect, bClearSelection);
		}
	}

	FRigBaseElement* Element = Hierarchy->Find(InKey);
	if(Element == nullptr)
	{
		return false;
	}

	if(Element->bSelected == bSelect)
	{
		return false;
	}

	Element->bSelected = bSelect;

	if(Element->bSelected)
	{
		Notify(ERigHierarchyNotification::ElementSelected, Element);
	}
	else
	{
		Notify(ERigHierarchyNotification::ElementDeselected, Element);
	}

	return true;
}

bool URigHierarchyController::SetSelection(TArray<FRigElementKey> InKeys)
{
	if(!IsValid())
	{
		return false;
	}

	if(URigHierarchy* HierarchyForSelection = Hierarchy->HierarchyForSelectionPtr.Get())
	{
		if(URigHierarchyController* ControllerForSelection = HierarchyForSelection->GetController())
		{
			return ControllerForSelection->SetSelection(InKeys);
		}
	}

	TArray<FRigElementKey> PreviousSelection = Hierarchy->GetSelectedKeys();

	for(const FRigElementKey& KeyToDeselect : PreviousSelection)
	{
		if(!InKeys.Contains(KeyToDeselect))
		{
			if(!SelectElement(KeyToDeselect, false))
			{
				return false;
			}
		}
	}

	for(const FRigElementKey& KeyToSelect : InKeys)
	{
		if(!PreviousSelection.Contains(KeyToSelect))
		{
			if(!SelectElement(KeyToSelect, true))
			{
				return false;
			}
		}
	}

	return true;
}

FRigElementKey URigHierarchyController::AddBone(FName InName, FRigElementKey InParent, FTransform InTransform,
                                                bool bTransformInGlobal, ERigBoneType InBoneType, bool bSetupUndo)
{
	if(!IsValid())
	{
		return FRigElementKey();
	}

#if WITH_EDITOR
	TSharedPtr<FScopedTransaction> TransactionPtr;
	if(bSetupUndo)
	{
		TransactionPtr = MakeShared<FScopedTransaction>(NSLOCTEXT("RigHierarchyController", "Add Bone", "Add Bone"));
        Hierarchy->Modify();
	}
#endif

	FRigBoneElement* NewElement = new FRigBoneElement();
	NewElement->Key.Type = ERigElementType::Bone;
	NewElement->Key.Name = Hierarchy->GetSafeNewName(InName.ToString(), NewElement->Key.Type);
	NewElement->BoneType = InBoneType;
	AddElement(NewElement, Hierarchy->Get(Hierarchy->GetIndex(InParent)), true);

	if(bTransformInGlobal)
	{
		Hierarchy->SetTransform(NewElement, InTransform, ERigTransformType::InitialGlobal, true, false);
	}
	else
	{
		Hierarchy->SetTransform(NewElement, InTransform, ERigTransformType::InitialLocal, true, false);
	}

	NewElement->Pose.Current = NewElement->Pose.Initial;

#if WITH_EDITOR
	TransactionPtr.Reset();
#endif

	return NewElement->Key;
}

FRigElementKey URigHierarchyController::AddSpace(FName InName, FRigElementKey InParent, FTransform InTransform,
	bool bTransformInGlobal, bool bSetupUndo)
{
	if(!IsValid())
	{
		return FRigElementKey();
	}

#if WITH_EDITOR
	TSharedPtr<FScopedTransaction> TransactionPtr;
	if(bSetupUndo)
	{
		TransactionPtr = MakeShared<FScopedTransaction>(NSLOCTEXT("RigHierarchyController", "Add Space", "Add Space"));
		Hierarchy->Modify();
	}
#endif

	FRigSpaceElement* NewElement = new FRigSpaceElement();
	NewElement->Key.Type = ERigElementType::Space;
	NewElement->Key.Name = Hierarchy->GetSafeNewName(InName.ToString(), NewElement->Key.Type);
	AddElement(NewElement, Hierarchy->Get(Hierarchy->GetIndex(InParent)), false);

	if(bTransformInGlobal)
	{
		Hierarchy->SetTransform(NewElement, InTransform, ERigTransformType::InitialGlobal, true, false);
	}
	else
	{
		Hierarchy->SetTransform(NewElement, InTransform, ERigTransformType::InitialLocal, true, false);
	}

	NewElement->Parent.MarkDirty(ERigTransformType::InitialGlobal);
	NewElement->Parent.Current = NewElement->Parent.Initial;
	NewElement->Pose.Current = NewElement->Pose.Initial;

#if WITH_EDITOR
	TransactionPtr.Reset();
#endif

	return NewElement->Key;
}

FRigElementKey URigHierarchyController::AddControl(
	FName InName,
	FRigElementKey InParent,
	FRigControlSettings InSettings,
	FRigControlValue InValue,
	FTransform InOffsetTransform,
	FTransform InGizmoTransform,
	bool bSetupUndo)
{
	if(!IsValid())
	{
		return FRigElementKey();
	}

#if WITH_EDITOR
	TSharedPtr<FScopedTransaction> TransactionPtr;
	if(bSetupUndo)
	{
		TransactionPtr = MakeShared<FScopedTransaction>(NSLOCTEXT("RigHierarchyController", "Add Control", "Add Control"));
		Hierarchy->Modify();
	}
#endif

	FRigControlElement* NewElement = new FRigControlElement();
	NewElement->Key.Type = ERigElementType::Control;
	NewElement->Key.Name = Hierarchy->GetSafeNewName(InName.ToString(), NewElement->Key.Type);
	NewElement->Settings = InSettings;
	AddElement(NewElement, Hierarchy->Get(Hierarchy->GetIndex(InParent)), false);
	
	NewElement->Offset.Set(ERigTransformType::InitialLocal, InOffsetTransform);  
	NewElement->Gizmo.Set(ERigTransformType::InitialLocal, InGizmoTransform);  
	Hierarchy->SetControlValue(NewElement, InValue, ERigControlValueType::Initial, false);

	NewElement->Parent.MarkDirty(ERigTransformType::InitialGlobal);
	NewElement->Parent.Current = NewElement->Parent.Initial;
	NewElement->Offset.Current = NewElement->Offset.Initial;
	NewElement->Pose.Current = NewElement->Pose.Initial;
	NewElement->Gizmo.Current = NewElement->Gizmo.Initial;

#if WITH_EDITOR
	TransactionPtr.Reset();
#endif

	return NewElement->Key;
}

FRigElementKey URigHierarchyController::AddCurve(FName InName, float InValue, bool bSetupUndo)
{
	if(!IsValid())
	{
		return FRigElementKey();
	}

#if WITH_EDITOR
	TSharedPtr<FScopedTransaction> TransactionPtr;
	if(bSetupUndo)
	{
		TransactionPtr = MakeShared<FScopedTransaction>(NSLOCTEXT("RigHierarchyController", "Add Curve", "Add Curve"));
		Hierarchy->Modify();
	}
#endif

	FRigCurveElement* NewElement = new FRigCurveElement();
	NewElement->Key.Type = ERigElementType::Curve;
	NewElement->Key.Name = Hierarchy->GetSafeNewName(InName.ToString(), NewElement->Key.Type);
	NewElement->Value = InValue;
	AddElement(NewElement, nullptr, false);

#if WITH_EDITOR
	TransactionPtr.Reset();
#endif

	return NewElement->Key;
}

FRigControlSettings URigHierarchyController::GetControlSettings(FRigElementKey InKey) const
{
	if(!IsValid())
	{
		return FRigControlSettings();
	}

	FRigControlElement* ControlElement = Hierarchy->Find<FRigControlElement>(InKey);
	if(ControlElement == nullptr)
	{
		return FRigControlSettings();
	}

	return ControlElement->Settings;
}

bool URigHierarchyController::SetControlSettings(FRigElementKey InKey, FRigControlSettings InSettings, bool bSetupUndo) const
{
	if(!IsValid())
	{
		return false;
	}

	FRigControlElement* ControlElement = Hierarchy->Find<FRigControlElement>(InKey);
	if(ControlElement == nullptr)
	{
		return false;
	}

	#if WITH_EDITOR
	TSharedPtr<FScopedTransaction> TransactionPtr;
	if(bSetupUndo)
	{
		TransactionPtr = MakeShared<FScopedTransaction>(NSLOCTEXT("RigHierarchyController", "SetControlSettings", "Set Control Settings"));
		Hierarchy->Modify();
	}
#endif

	ControlElement->Settings = InSettings;

	FRigControlValue InitialValue = Hierarchy->GetControlValue(ControlElement, ERigControlValueType::Initial);
	FRigControlValue CurrentValue = Hierarchy->GetControlValue(ControlElement, ERigControlValueType::Current);

	ControlElement->Settings.ApplyLimits(InitialValue);
	ControlElement->Settings.ApplyLimits(CurrentValue);

	Hierarchy->Notify(ERigHierarchyNotification::ControlSettingChanged, ControlElement);

	Hierarchy->SetControlValue(ControlElement, InitialValue, ERigControlValueType::Initial, bSetupUndo);
	Hierarchy->SetControlValue(ControlElement, CurrentValue, ERigControlValueType::Current, bSetupUndo);

#if WITH_EDITOR
    TransactionPtr.Reset();
#endif

	return true;
}

TArray<FRigElementKey> URigHierarchyController::ImportBones(const FReferenceSkeleton& InSkeleton,
                                                            const FName& InNameSpace, bool bReplaceExistingBones, bool bRemoveObsoleteBones, bool bSelectBones,
                                                            bool bSetupUndo)
{
	TArray<FRigElementKey> AddedBones;

	if(!IsValid())
	{
		return AddedBones;
	}
	
	TArray<FRigElementKey> BonesToSelect;
	TMap<FName, FName> BoneNameMap;

	Hierarchy->ResetPoseToInitial();

	const TArray<FMeshBoneInfo>& BoneInfos = InSkeleton.GetRefBoneInfo();
	const TArray<FTransform>& BonePoses = InSkeleton.GetRefBonePose();

	struct Local
	{
		static FName DetermineBoneName(const FName& InBoneName, const FName& InLocalNameSpace)
		{
			if (InLocalNameSpace == NAME_None || InBoneName == NAME_None)
			{
				return InBoneName;
			}
			return *FString::Printf(TEXT("%s_%s"), *InLocalNameSpace.ToString(), *InBoneName.ToString());
		}
	};

	if (bReplaceExistingBones)
	{
		GetHierarchy()->ForEach<FRigBoneElement>([&BoneNameMap](FRigBoneElement* BoneElement) -> bool
        {
			BoneNameMap.Add(BoneElement->GetName(), BoneElement->GetName());
			return true;
		});

		for (int32 Index = 0; Index < BoneInfos.Num(); ++Index)
		{
			const FRigElementKey ExistingBoneKey(BoneInfos[Index].Name, ERigElementType::Bone);
			const int32 ExistingBoneIndex = Hierarchy->GetIndex(ExistingBoneKey);
			
			const FName DesiredBoneName = Local::DetermineBoneName(BoneInfos[Index].Name, InNameSpace);
			FName ParentName = (BoneInfos[Index].ParentIndex != INDEX_NONE) ? BoneInfos[BoneInfos[Index].ParentIndex].Name : NAME_None;
			ParentName = Local::DetermineBoneName(ParentName, InNameSpace);

			const FName* MappedParentNamePtr = BoneNameMap.Find(ParentName);
			if (MappedParentNamePtr)
			{
				ParentName = *MappedParentNamePtr;
			}

			const FRigElementKey ParentKey(ParentName, ERigElementType::Bone);

			// if this bone already exists
			if (ExistingBoneIndex != INDEX_NONE)
			{
				const int32 ParentIndex = Hierarchy->GetIndex(ParentKey);
				
				// check it's parent
				if (ParentIndex != INDEX_NONE)
				{
					SetParent(ExistingBoneKey, ParentKey, bSetupUndo);
				}

				Hierarchy->SetLocalTransform(ExistingBoneIndex, BonePoses[Index], true, bSetupUndo);

				BonesToSelect.Add(ExistingBoneKey);
			}
			else
			{
				const FRigElementKey AddedBoneKey = AddBone(DesiredBoneName, ParentKey, BonePoses[Index], false, ERigBoneType::Imported, bSetupUndo);
				BoneNameMap.Add(DesiredBoneName, AddedBoneKey.Name);
				AddedBones.Add(AddedBoneKey);
				BonesToSelect.Add(AddedBoneKey);
			}
		}

	}
	else // import all as new
	{
		for (int32 Index = 0; Index < BoneInfos.Num(); ++Index)
		{
			FName DesiredBoneName = Local::DetermineBoneName(BoneInfos[Index].Name, InNameSpace);
			FName ParentName = (BoneInfos[Index].ParentIndex != INDEX_NONE) ? BoneInfos[BoneInfos[Index].ParentIndex].Name : NAME_None;
			ParentName = Local::DetermineBoneName(ParentName, InNameSpace);

			const FName* MappedParentNamePtr = BoneNameMap.Find(ParentName);
			if (MappedParentNamePtr)
			{
				ParentName = *MappedParentNamePtr;
			}

			const FRigElementKey ParentKey(ParentName, ERigElementType::Bone);
			const FRigElementKey AddedBoneKey = AddBone(DesiredBoneName, ParentKey, BonePoses[Index], false, ERigBoneType::Imported, bSetupUndo);
			BoneNameMap.Add(DesiredBoneName, AddedBoneKey.Name);
			AddedBones.Add(AddedBoneKey);
			BonesToSelect.Add(AddedBoneKey);
		}
	}

	if (bReplaceExistingBones && bRemoveObsoleteBones)
	{
		TMap<FName, int32> BoneNameToIndexInSkeleton;
		for (const FMeshBoneInfo& BoneInfo : BoneInfos)
		{
			FName DesiredBoneName = Local::DetermineBoneName(BoneInfo.Name, InNameSpace);
			BoneNameToIndexInSkeleton.Add(DesiredBoneName, BoneNameToIndexInSkeleton.Num());
		}
		
		TArray<FRigElementKey> BonesToDelete;
		GetHierarchy()->ForEach<FRigBoneElement>([BoneNameToIndexInSkeleton, &BonesToDelete](FRigBoneElement* BoneElement) -> bool
        {
            if (!BoneNameToIndexInSkeleton.Contains(BoneElement->GetName()))
			{
				if (BoneElement->BoneType == ERigBoneType::Imported)
				{
					BonesToDelete.Add(BoneElement->GetKey());
				}
			}
			return true;
		});

		for (const FRigElementKey& BoneToDelete : BonesToDelete)
		{
			TArray<FRigElementKey> Children = Hierarchy->GetChildren(BoneToDelete);
			Algo::Reverse(Children);
			
			for (const FRigElementKey& Child : Children)
			{
				if(BonesToDelete.Contains(Child))
				{
					continue;
				}
				RemoveAllParents(Child, true, bSetupUndo);
			}
		}

		for (const FRigElementKey& BoneToDelete : BonesToDelete)
		{
			RemoveElement(BoneToDelete);
			BonesToSelect.Remove(BoneToDelete);
		}
	}

	if (bSelectBones)
	{
		SetSelection(BonesToSelect);
	}

	return AddedBones;

}

#if WITH_EDITOR

TArray<FRigElementKey> URigHierarchyController::ImportBonesFromAsset(FString InAssetPath, FName InNameSpace,
	bool bReplaceExistingBones, bool bRemoveObsoleteBones, bool bSelectBones, bool bSetupUndo)
{
	if(USkeleton* Skeleton = GetSkeletonFromAssetPath(InAssetPath))
	{
		return ImportBones(Skeleton, InNameSpace, bReplaceExistingBones, bRemoveObsoleteBones, bSelectBones, bSetupUndo);
	}
	return TArray<FRigElementKey>();
}

TArray<FRigElementKey> URigHierarchyController::ImportCurvesFromAsset(FString InAssetPath, FName InNameSpace,
	bool bSelectCurves, bool bSetupUndo)
{
	if(USkeleton* Skeleton = GetSkeletonFromAssetPath(InAssetPath))
	{
		return ImportCurves(Skeleton, InNameSpace, bSelectCurves, bSetupUndo);
	}
	return TArray<FRigElementKey>();
}

USkeleton* URigHierarchyController::GetSkeletonFromAssetPath(const FString& InAssetPath)
{
	UObject* AssetObject = StaticLoadObject(UObject::StaticClass(), NULL, *InAssetPath, NULL, LOAD_None, NULL);
	if(AssetObject == nullptr)
	{
		return nullptr;
    }

	if(USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(AssetObject))
	{
		return SkeletalMesh->GetSkeleton();
	}

	if(USkeleton* Skeleton = Cast<USkeleton>(AssetObject))
	{
		return Skeleton;
	}

	return nullptr;
}

#endif

TArray<FRigElementKey> URigHierarchyController::ImportCurves(USkeleton* InSkeleton, FName InNameSpace,
                                                             bool bSelectCurves, bool bSetupUndo)
{
	check(InSkeleton);

	TArray<FRigElementKey> Keys;
	if(!IsValid())
	{
		return Keys;
	}

	const FSmartNameMapping* SmartNameMapping = InSkeleton->GetSmartNameContainer(USkeleton::AnimCurveMappingName);

	TArray<FName> NameArray;
	SmartNameMapping->FillNameArray(NameArray);

	for (int32 Index = 0; Index < NameArray.Num(); ++Index)
	{
		FName Name = NameArray[Index];
		if (!InNameSpace.IsNone())
		{
			Name = *FString::Printf(TEXT("%s::%s"), *InNameSpace.ToString(), *Name.ToString());
		}

		const FRigElementKey ExpectedKey(Name, ERigElementType::Curve);
		if(Hierarchy->Contains(ExpectedKey))
		{
			Keys.Add(ExpectedKey);
			continue;
		}
		
		const FRigElementKey CurveKey = AddCurve(Name, 0.f, bSetupUndo);
		Keys.Add(FRigElementKey(Name, ERigElementType::Curve));
	}

	if(bSelectCurves)
	{
		SetSelection(Keys);
	}

	return Keys;
}

FString URigHierarchyController::ExportSelectionToText() const
{
	if(!IsValid())
	{
		return FString();
	}
	return ExportToText(Hierarchy->GetSelectedKeys());
}

FString URigHierarchyController::ExportToText(TArray<FRigElementKey> InKeys) const
{
	if(!IsValid() || InKeys.IsEmpty())
	{
		return FString();
	}

	// sort the keys by traversal order
	TArray<FRigElementKey> Keys = Hierarchy->SortKeys(InKeys);

	FRigHierarchyCopyPasteContent Data;
	for (const FRigElementKey& Key : Keys)
	{
		FRigBaseElement* Element = Hierarchy->Find(Key);
		if(Element == nullptr)
		{
			continue;
		}

		FRigHierarchyCopyPasteContentPerElement PerElementData;
		PerElementData.Key = Key;
		PerElementData.Parents = Hierarchy->GetParents(Key);

		if(FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(Element))
		{
			ensure(PerElementData.Parents.Num() == MultiParentElement->ParentWeights.Num());
			PerElementData.ParentWeights = MultiParentElement->ParentWeights;
		}
		else
		{
			PerElementData.ParentWeights.SetNumZeroed(PerElementData.Parents.Num());
			if(PerElementData.ParentWeights.Num() > 0)
			{
				PerElementData.ParentWeights[0] = 1.f;
			}
		}

		if(FRigTransformElement* TransformElement = Cast<FRigTransformElement>(Element))
		{
			PerElementData.Pose.Initial.Local.Set(Hierarchy->GetTransform(TransformElement, ERigTransformType::InitialLocal));
			PerElementData.Pose.Initial.Global.Set(Hierarchy->GetTransform(TransformElement, ERigTransformType::InitialGlobal));
			PerElementData.Pose.Current.Local.Set(Hierarchy->GetTransform(TransformElement, ERigTransformType::CurrentLocal));
			PerElementData.Pose.Current.Global.Set(Hierarchy->GetTransform(TransformElement, ERigTransformType::CurrentGlobal));
		}

		switch (Key.Type)
		{
			case ERigElementType::Bone:
			{
				FRigBoneElement DefaultElement;
				FRigBoneElement::StaticStruct()->ExportText(PerElementData.Content, Element, &DefaultElement, nullptr, PPF_None, nullptr);
				break;
			}
			case ERigElementType::Control:
			{
				FRigControlElement DefaultElement;
				FRigControlElement::StaticStruct()->ExportText(PerElementData.Content, Element, &DefaultElement, nullptr, PPF_None, nullptr);
				break;
			}
			case ERigElementType::Space:
			{
				FRigSpaceElement DefaultElement;
				FRigSpaceElement::StaticStruct()->ExportText(PerElementData.Content, Element, &DefaultElement, nullptr, PPF_None, nullptr);
				break;
			}
			case ERigElementType::Curve:
			{
				FRigCurveElement DefaultElement;
				FRigCurveElement::StaticStruct()->ExportText(PerElementData.Content, Element, &DefaultElement, nullptr, PPF_None, nullptr);
				break;
			}
			default:
			{
				ensure(false);
				break;
			}
		}

		Data.Elements.Add(PerElementData);
	}

	FString ExportedText;
	FRigHierarchyCopyPasteContent DefaultContent;
	FRigHierarchyCopyPasteContent::StaticStruct()->ExportText(ExportedText, &Data, &DefaultContent, nullptr, PPF_None, nullptr);
	return ExportedText;
}

class FRigHierarchyImportErrorContext : public FOutputDevice
{
public:

	int32 NumErrors;

	FRigHierarchyImportErrorContext()
        : FOutputDevice()
        , NumErrors(0)
	{
	}

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category) override
	{
		UE_LOG(LogControlRig, Error, TEXT("Error Importing To Hierarchy: %s"), V);
		NumErrors++;
	}
};

TArray<FRigElementKey> URigHierarchyController::ImportFromText(FString InContent, bool bReplaceExistingElements, bool bSelectNewElements, bool bSetupUndo)
{
	TArray<FRigElementKey> PastedKeys;
	if(!IsValid())
	{
		return PastedKeys;
	}

	FRigHierarchyCopyPasteContent Data;
	FRigHierarchyImportErrorContext ErrorPipe;
	FRigHierarchyCopyPasteContent::StaticStruct()->ImportText(*InContent, &Data, nullptr, EPropertyPortFlags::PPF_None, &ErrorPipe, FRigHierarchyCopyPasteContent::StaticStruct()->GetName(), true);
	if (ErrorPipe.NumErrors > 0)
	{
		return PastedKeys;
	}

	if (Data.Elements.Num() == 0)
	{
		return PastedKeys;
	}

#if WITH_EDITOR
	TSharedPtr<FScopedTransaction> TransactionPtr;
	if(bSetupUndo)
	{
		TransactionPtr = MakeShared<FScopedTransaction>(NSLOCTEXT("RigHierarchyController", "Add Bone", "Add Bone"));
		Hierarchy->Modify();
	}
#endif

	TMap<FRigElementKey, FRigElementKey> KeyMap;
	for(FRigBaseElement* Element : *Hierarchy)
	{
		KeyMap.Add(Element->GetKey(), Element->GetKey());
	}
	
	for(const FRigHierarchyCopyPasteContentPerElement& PerElementData : Data.Elements)
	{
		ErrorPipe.NumErrors = 0;

		FRigBaseElement* NewElement = nullptr;

		switch (PerElementData.Key.Type)
		{
			case ERigElementType::Bone:
			{
				NewElement = new FRigBoneElement();
				FRigBoneElement::StaticStruct()->ImportText(*PerElementData.Content, NewElement, nullptr, EPropertyPortFlags::PPF_None, &ErrorPipe, FRigBoneElement::StaticStruct()->GetName(), true);
				break;
			}
			case ERigElementType::Space:
			{
				NewElement = new FRigSpaceElement();
				FRigSpaceElement::StaticStruct()->ImportText(*PerElementData.Content, NewElement, nullptr, EPropertyPortFlags::PPF_None, &ErrorPipe, FRigSpaceElement::StaticStruct()->GetName(), true);
				break;
			}
			case ERigElementType::Control:
			{
				NewElement = new FRigControlElement();
				FRigControlElement::StaticStruct()->ImportText(*PerElementData.Content, NewElement, nullptr, EPropertyPortFlags::PPF_None, &ErrorPipe, FRigControlElement::StaticStruct()->GetName(), true);
				break;
			}
			case ERigElementType::Curve:
			{
				NewElement = new FRigCurveElement();
				FRigCurveElement::StaticStruct()->ImportText(*PerElementData.Content, NewElement, nullptr, EPropertyPortFlags::PPF_None, &ErrorPipe, FRigCurveElement::StaticStruct()->GetName(), true);
				break;
			}
			default:
			{
				ensure(false);
				break;
			}
		}

		check(NewElement);
		ensure(NewElement->GetKey() == PerElementData.Key);

		if(bReplaceExistingElements)
		{
			if(FRigBaseElement* ExistingElement = Hierarchy->Find(NewElement->GetKey()))
			{
				ExistingElement->CopyPose(NewElement, true, true);

				TArray<FRigElementKey> CurrentParents = Hierarchy->GetParents(NewElement->GetKey());

				bool bUpdateParents = CurrentParents.Num() != PerElementData.Parents.Num();
				if(!bUpdateParents)
				{
					for(const FRigElementKey& CurrentParent : CurrentParents)
					{
						if(!PerElementData.Parents.Contains(CurrentParent))
						{
							bUpdateParents = true;
							break;
						}
					}
				}

				if(bUpdateParents)
				{
					RemoveAllParents(ExistingElement->GetKey(), true, bSetupUndo);

					for(const FRigElementKey& NewParent : PerElementData.Parents)
					{
						AddParent(ExistingElement->GetKey(), NewParent, true, bSetupUndo);
					}
				}
				
				for(int32 ParentIndex = 0; ParentIndex < PerElementData.ParentWeights.Num(); ParentIndex++)
				{
					Hierarchy->SetParentWeight(ExistingElement, ParentIndex, PerElementData.ParentWeights[ParentIndex], true, true);
					Hierarchy->SetParentWeight(ExistingElement, ParentIndex, PerElementData.ParentWeights[ParentIndex], false, true);
				}

				PastedKeys.Add(ExistingElement->GetKey());
				delete NewElement;
				continue;
			}
		}
		
		NewElement->Key.Name = Hierarchy->GetSafeNewName(NewElement->Key.Name.ToString(), NewElement->Key.Type);
		AddElement(NewElement, nullptr, true);

		KeyMap.FindOrAdd(PerElementData.Key) = NewElement->Key;

		for(const FRigElementKey& OriginalParent : PerElementData.Parents)
		{
			FRigElementKey Parent = OriginalParent;
			if(const FRigElementKey* RemappedParent = KeyMap.Find(Parent))
			{
				Parent = *RemappedParent;
			}

			AddParent(NewElement->GetKey(), Parent, true, bSetupUndo);
		}
		
		for(int32 ParentIndex = 0; ParentIndex < PerElementData.ParentWeights.Num(); ParentIndex++)
		{
			Hierarchy->SetParentWeight(NewElement, ParentIndex, PerElementData.ParentWeights[ParentIndex], true, true);
			Hierarchy->SetParentWeight(NewElement, ParentIndex, PerElementData.ParentWeights[ParentIndex], false, true);
		}

		PastedKeys.Add(NewElement->GetKey());
	}

	for(const FRigHierarchyCopyPasteContentPerElement& PerElementData : Data.Elements)
	{
		FRigElementKey MappedKey = KeyMap.FindChecked(PerElementData.Key);
		FRigBaseElement* Element = Hierarchy->FindChecked(MappedKey);

		if(FRigTransformElement* TransformElement = Cast<FRigTransformElement>(Element))
		{
			Hierarchy->SetTransform(TransformElement, PerElementData.Pose.Initial.Local.Transform, ERigTransformType::InitialLocal, true, true);
			Hierarchy->SetTransform(TransformElement, PerElementData.Pose.Current.Local.Transform, ERigTransformType::CurrentLocal, true, true);
		}
	}
	
#if WITH_EDITOR
	TransactionPtr.Reset();
#endif

	return PastedKeys;
}

void URigHierarchyController::Notify(ERigHierarchyNotification InNotifType, const FRigBaseElement* InElement)
{
	if(!IsValid())
	{
		return;
	}
	Hierarchy->Notify(InNotifType, InElement);
}

void URigHierarchyController::HandleHierarchyModified(ERigHierarchyNotification InNotifType, URigHierarchy* InHierarchy, const FRigBaseElement* InElement) const
{
	ensure(IsValid());
	ensure(InHierarchy == Hierarchy);
	ModifiedEvent.Broadcast(InNotifType, InHierarchy, InElement);
}

int32 URigHierarchyController::AddElement(FRigBaseElement* InElementToAdd, FRigBaseElement* InFirstParent, bool bMaintainGlobalTransform)
{
	ensure(IsValid());

	InElementToAdd->SubIndex = Hierarchy->Num(InElementToAdd->Key.Type);
	InElementToAdd->Index = Hierarchy->Elements.Add(InElementToAdd);

	Hierarchy->IndexLookup.Add(InElementToAdd->Key, InElementToAdd->Index);
	Hierarchy->TopologyVersion++;

	Notify(ERigHierarchyNotification::ElementAdded, InElementToAdd);

	SetParent(InElementToAdd, InFirstParent, bMaintainGlobalTransform);

	return InElementToAdd->Index;
}

bool URigHierarchyController::RemoveElement(FRigElementKey InElement, bool bSetupUndo)
{
	if(!IsValid())
	{
		return false;
	}

	FRigBaseElement* Element = Hierarchy->Find(InElement);
	if(Element == nullptr)
	{
		ReportWarningf(TEXT("Cannot Remove Element: '%s' not found."), *InElement.ToString());
		return false;
	}

#if WITH_EDITOR
	TSharedPtr<FScopedTransaction> TransactionPtr;
	if(bSetupUndo)
	{
		TransactionPtr = MakeShared<FScopedTransaction>(NSLOCTEXT("RigHierarchyController", "Remove Element", "Remove Element"));
		Hierarchy->Modify();
	}
#endif

	const bool bRemoved = RemoveElement(Element);
	
#if WITH_EDITOR
	if(!bRemoved && TransactionPtr.IsValid())
	{
		TransactionPtr->Cancel();
	}
	TransactionPtr.Reset();
#endif
	
	return bRemoved;
}

bool URigHierarchyController::RemoveElement(FRigBaseElement* InElement)
{
	if(InElement == nullptr)
	{
		return false;
	}

	// make sure this element is part of this hierarchy
	ensure(Hierarchy->FindChecked(InElement->Key) == InElement);

	// deselect if needed
	if(InElement->IsSelected())
	{
		SelectElement(InElement->GetKey(), false);
	}

	// if this is a transform element - make sure to allow dependents to store their global transforms
	if(FRigTransformElement* TransformElement = Cast<FRigTransformElement>(InElement))
	{
		TArray<FRigTransformElement::FElementToDirty> PreviousElementsToDirty = TransformElement->ElementsToDirty; 
		for(const FRigTransformElement::FElementToDirty& ElementToDirty : PreviousElementsToDirty)
		{
			if(FRigSingleParentElement* SingleParentElement = Cast<FRigSingleParentElement>(ElementToDirty.Element))
			{
				if(SingleParentElement->ParentElement == InElement)
				{
					RemoveParent(SingleParentElement, InElement, true);
				}
			}
			else if(FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(ElementToDirty.Element))
			{
				if(MultiParentElement->ParentElements.Contains(InElement))
				{
					RemoveParent(MultiParentElement, InElement, true);
				}
			}
		}
	}

	const int32 NumElementsRemoved = Hierarchy->Elements.Remove(InElement);
	ensure(NumElementsRemoved == 1);

	const int32 NumLookupsRemoved = Hierarchy->IndexLookup.Remove(InElement->Key);
	ensure(NumLookupsRemoved == 1);
	for(TPair<FRigElementKey, int32>& Pair : Hierarchy->IndexLookup)
	{
		if(Pair.Value > InElement->Index)
		{
			Pair.Value--;
		}
	}

	// update the indices of all other elements
	for (FRigBaseElement* RemainingElement : Hierarchy->Elements)
	{
		if(RemainingElement->Index > InElement->Index)
		{
			RemainingElement->Index--;
		}
	}

	if(FRigSingleParentElement* SingleParentElement = Cast<FRigSingleParentElement>(InElement))
	{
		RemoveElementToDirty(SingleParentElement->ParentElement, InElement);
	}
	else if(FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(InElement))
	{
		for(FRigTransformElement* ParentElement : MultiParentElement->ParentElements)
		{
			RemoveElementToDirty(ParentElement, InElement);
		}
	}

	if(InElement->SubIndex != INDEX_NONE)
	{
		for(FRigBaseElement* Element : *Hierarchy)
		{
			if(Element->SubIndex > InElement->SubIndex && Element->GetType() == InElement->GetType())
			{
				Element->SubIndex--;
			}
		}
	}

	Hierarchy->TopologyVersion++;

	Notify(ERigHierarchyNotification::ElementRemoved, InElement);
	if(Hierarchy->Num() == 0)
	{
		Notify(ERigHierarchyNotification::HierarchyReset, nullptr);
	}

	delete InElement;

	return NumElementsRemoved == 1;
}

FRigElementKey URigHierarchyController::RenameElement(FRigElementKey InElement, FName InName, bool bSetupUndo)
{
	if(!IsValid())
	{
		return FRigElementKey();
	}

	FRigBaseElement* Element = Hierarchy->Find(InElement);
	if(Element == nullptr)
	{
		ReportWarningf(TEXT("Cannot Rename Element: '%s' not found."), *InElement.ToString());
		return FRigElementKey();
	}

#if WITH_EDITOR
	TSharedPtr<FScopedTransaction> TransactionPtr;
	if(bSetupUndo)
	{
		TransactionPtr = MakeShared<FScopedTransaction>(NSLOCTEXT("RigHierarchyController", "Rename Element", "Rename Element"));
		Hierarchy->Modify();
	}
#endif

	const bool bRenamed = RenameElement(Element, InName);

#if WITH_EDITOR
	if(!bRenamed && TransactionPtr.IsValid())
	{
		TransactionPtr->Cancel();
	}
	TransactionPtr.Reset();
#endif

	return bRenamed ? Element->GetKey() : FRigElementKey();
}

bool URigHierarchyController::RenameElement(FRigBaseElement* InElement, const FName &InName)
{
	if(InElement == nullptr)
	{
		return false;
	}

	if(InElement->GetName() == InName)
	{
		return false;
	}

	const FRigElementKey OldKey = InElement->GetKey();
	InElement->Key.Name = Hierarchy->GetSafeNewName(InName.ToString(), InElement->GetType());
	const FRigElementKey NewKey = InElement->GetKey();

	Hierarchy->IndexLookup.Remove(OldKey);
	Hierarchy->IndexLookup.Add(NewKey, InElement->Index);

	// update all multi parent elements' index lookups
	for (FRigBaseElement* Element : Hierarchy->Elements)
	{
		if(FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(Element))
		{
			const int32* ExistingIndexPtr = MultiParentElement->IndexLookup.Find(OldKey);
			if(ExistingIndexPtr)
			{
				const int32 ExistingIndex = *ExistingIndexPtr;
				MultiParentElement->IndexLookup.Remove(OldKey);
				MultiParentElement->IndexLookup.Add(NewKey, ExistingIndex);
			}
		}
	}
	
	Hierarchy->PreviousNameMap.FindOrAdd(NewKey) = OldKey;
	Notify(ERigHierarchyNotification::ElementRenamed, InElement);

	return true;
}

bool URigHierarchyController::AddParent(FRigElementKey InChild, FRigElementKey InParent, bool bMaintainGlobalTransform, bool bSetupUndo)
{
	if(!IsValid())
	{
		return false;
	}

	FRigBaseElement* Child = Hierarchy->Find(InChild);
	if(Child == nullptr)
	{
		ReportWarningf(TEXT("Cannot Add Parent, Child '%s' not found."), *InChild.ToString());
		return false;
	}

	FRigBaseElement* Parent = Hierarchy->Find(InParent);
	if(Parent == nullptr)
	{
		ReportWarningf(TEXT("Cannot Add Parent, Parent '%s' not found."), *InParent.ToString());
		return false;
	}

#if WITH_EDITOR
	TSharedPtr<FScopedTransaction> TransactionPtr;
	if(bSetupUndo)
	{
		TransactionPtr = MakeShared<FScopedTransaction>(NSLOCTEXT("RigHierarchyController", "Add Parent", "Add Parent"));
		Hierarchy->Modify();
	}
#endif

	const bool bAdded = AddParent(Child, Parent, bMaintainGlobalTransform);

#if WITH_EDITOR
	if(!bAdded && TransactionPtr.IsValid())
	{
		TransactionPtr->Cancel();
	}
	TransactionPtr.Reset();
#endif
	
	return bAdded;
}

bool URigHierarchyController::AddParent(FRigBaseElement* InChild, FRigBaseElement* InParent, bool bMaintainGlobalTransform, bool bRemoveAllParents)
{
	if(InChild == nullptr || InParent == nullptr)
	{
		return false;
	}

	// single parent children can't be parented multiple times
	if(FRigSingleParentElement* SingleParentElement = Cast<FRigSingleParentElement>(InChild))
	{
		if(SingleParentElement->ParentElement == InParent)
		{
			return false;
		}
		bRemoveAllParents = true;
	}

	else if(FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(InChild))
	{
		if(MultiParentElement->ParentElements.Contains(InParent))
		{
			return false;
		}
	}

	if(Hierarchy->IsParentedTo(InParent, InChild))
	{
		ReportAndNotifyErrorf(TEXT("Cannot parent '%s' to '%s' - would cause a cycle."), *InChild->Key.ToString(), *InParent->Key.ToString());
		return false;
	}

	if(bRemoveAllParents)
	{
		RemoveAllParents(InChild, bMaintainGlobalTransform);		
	}

	if(FRigTransformElement* TransformElement = Cast<FRigTransformElement>(InChild))
	{
		if(bMaintainGlobalTransform)
		{
			Hierarchy->GetTransform(TransformElement, ERigTransformType::CurrentGlobal);
			Hierarchy->GetTransform(TransformElement, ERigTransformType::InitialGlobal);
			TransformElement->Pose.MarkDirty(ERigTransformType::CurrentLocal);
			TransformElement->Pose.MarkDirty(ERigTransformType::InitialLocal);
		}
		else
		{
			Hierarchy->GetTransform(TransformElement, ERigTransformType::CurrentLocal);
			Hierarchy->GetTransform(TransformElement, ERigTransformType::InitialLocal);
			TransformElement->Pose.MarkDirty(ERigTransformType::CurrentGlobal);
			TransformElement->Pose.MarkDirty(ERigTransformType::InitialGlobal);
		}
	}

	if(FRigSingleParentElement* SingleParentElement = Cast<FRigSingleParentElement>(InChild))
	{
		if(FRigTransformElement* NewTransformParent = Cast<FRigTransformElement>(InParent))
		{
			AddElementToDirty(NewTransformParent, SingleParentElement);
			SingleParentElement->ParentElement = NewTransformParent;

			Hierarchy->TopologyVersion++;

			if(!bMaintainGlobalTransform)
			{
				Hierarchy->PropagateDirtyFlags(SingleParentElement, true, true);
				Hierarchy->PropagateDirtyFlags(SingleParentElement, false, true);
			}

			Notify(ERigHierarchyNotification::ParentChanged, SingleParentElement);
			return true;
		}
	}
	else if(FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(InChild))
	{
		if(FRigTransformElement* NewTransformParent = Cast<FRigTransformElement>(InParent))
		{
			AddElementToDirty(NewTransformParent, MultiParentElement);

			const int32 ParentIndex = MultiParentElement->ParentElements.Add(NewTransformParent);
			MultiParentElement->ParentWeights.Add(1.f);
			MultiParentElement->ParentWeightsInitial.Add(1.f);
			MultiParentElement->IndexLookup.Add(NewTransformParent->GetKey(), ParentIndex);

			MultiParentElement->Parent.MarkDirty(ERigTransformType::CurrentGlobal);  
			MultiParentElement->Parent.MarkDirty(ERigTransformType::InitialGlobal);

			if(FRigControlElement* ControlElement = Cast<FRigControlElement>(MultiParentElement))
			{
				ControlElement->Offset.MarkDirty(ERigTransformType::CurrentGlobal);  
				ControlElement->Offset.MarkDirty(ERigTransformType::InitialGlobal);
				ControlElement->Gizmo.MarkDirty(ERigTransformType::CurrentGlobal);  
				ControlElement->Gizmo.MarkDirty(ERigTransformType::InitialGlobal);
			}

			Hierarchy->TopologyVersion++;

			if(!bMaintainGlobalTransform)
			{
				Hierarchy->PropagateDirtyFlags(MultiParentElement, true, true);
				Hierarchy->PropagateDirtyFlags(MultiParentElement, false, true);
			}

			Notify(ERigHierarchyNotification::ParentChanged, MultiParentElement);
			return true;
		}
	}
	
	return false;
}

bool URigHierarchyController::RemoveParent(FRigElementKey InChild, FRigElementKey InParent, bool bMaintainGlobalTransform, bool bSetupUndo)
{
	if(!IsValid())
	{
		return false;
	}

	FRigBaseElement* Child = Hierarchy->Find(InChild);
	if(Child == nullptr)
	{
		ReportWarningf(TEXT("Cannot Remove Parent, Child '%s' not found."), *InChild.ToString());
		return false;
	}

	FRigBaseElement* Parent = Hierarchy->Find(InParent);
	if(Parent == nullptr)
	{
		ReportWarningf(TEXT("Cannot Remove Parent, Parent '%s' not found."), *InParent.ToString());
		return false;
	}

#if WITH_EDITOR
	TSharedPtr<FScopedTransaction> TransactionPtr;
	if(bSetupUndo)
	{
		TransactionPtr = MakeShared<FScopedTransaction>(NSLOCTEXT("RigHierarchyController", "Remove Parent", "Remove Parent"));
		Hierarchy->Modify();
	}
#endif

	const bool bRemoved = RemoveParent(Child, Parent, bMaintainGlobalTransform);

#if WITH_EDITOR
	if(!bRemoved && TransactionPtr.IsValid())
	{
		TransactionPtr->Cancel();
	}
	TransactionPtr.Reset();
#endif
	
	return bRemoved;
}

bool URigHierarchyController::RemoveParent(FRigBaseElement* InChild, FRigBaseElement* InParent, bool bMaintainGlobalTransform)
{
	if(InChild == nullptr || InParent == nullptr)
	{
		return false;
	}

	FRigTransformElement* ParentTransformElement = Cast<FRigTransformElement>(InParent);
	if(ParentTransformElement == nullptr)
	{
		return false;
	}

	// single parent children can't be parented multiple times
	if(FRigSingleParentElement* SingleParentElement = Cast<FRigSingleParentElement>(InChild))
	{
		if(SingleParentElement->ParentElement == ParentTransformElement)
		{
			if(bMaintainGlobalTransform)
			{
				Hierarchy->GetTransform(SingleParentElement, ERigTransformType::CurrentGlobal);
				Hierarchy->GetTransform(SingleParentElement, ERigTransformType::InitialGlobal);
				SingleParentElement->Pose.MarkDirty(ERigTransformType::CurrentLocal);
				SingleParentElement->Pose.MarkDirty(ERigTransformType::InitialLocal);
			}
			else
			{
				Hierarchy->GetTransform(SingleParentElement, ERigTransformType::CurrentLocal);
				Hierarchy->GetTransform(SingleParentElement, ERigTransformType::InitialLocal);
				SingleParentElement->Pose.MarkDirty(ERigTransformType::CurrentGlobal);
				SingleParentElement->Pose.MarkDirty(ERigTransformType::InitialGlobal);
			}

			const FRigElementKey PreviousParentKey = SingleParentElement->ParentElement->GetKey();
			Hierarchy->PreviousParentMap.FindOrAdd(SingleParentElement->GetKey()) = PreviousParentKey;
			
			// remove the previous parent
			SingleParentElement->ParentElement = nullptr;
			RemoveElementToDirty(InParent, SingleParentElement); 
			Hierarchy->TopologyVersion++;

			if(!bMaintainGlobalTransform)
			{
				Hierarchy->PropagateDirtyFlags(SingleParentElement, true, true);
				Hierarchy->PropagateDirtyFlags(SingleParentElement, false, true);
			}

			Notify(ERigHierarchyNotification::ParentChanged, SingleParentElement);

			return true;
		}
	}

	// single parent children can't be parented multiple times
	else if(FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(InChild))
	{
		const int32 ParentIndex = MultiParentElement->ParentElements.Find(ParentTransformElement);
		if(MultiParentElement->ParentElements.IsValidIndex(ParentIndex))
		{
			if(bMaintainGlobalTransform)
			{
				Hierarchy->GetTransform(MultiParentElement, ERigTransformType::CurrentGlobal);
				Hierarchy->GetTransform(MultiParentElement, ERigTransformType::InitialGlobal);
				MultiParentElement->Pose.MarkDirty(ERigTransformType::CurrentLocal);
				MultiParentElement->Pose.MarkDirty(ERigTransformType::InitialLocal);
			}
			else
			{
				Hierarchy->GetTransform(MultiParentElement, ERigTransformType::CurrentLocal);
				Hierarchy->GetTransform(MultiParentElement, ERigTransformType::InitialLocal);
				MultiParentElement->Pose.MarkDirty(ERigTransformType::CurrentGlobal);
				MultiParentElement->Pose.MarkDirty(ERigTransformType::InitialGlobal);
			}

			// remove the previous parent
			RemoveElementToDirty(InParent, MultiParentElement); 

			const FRigElementKey PreviousParentKey = MultiParentElement->ParentElements[ParentIndex]->GetKey();
			Hierarchy->PreviousParentMap.FindOrAdd(MultiParentElement->GetKey()) = PreviousParentKey;

			MultiParentElement->ParentElements.RemoveAt(ParentIndex);
			MultiParentElement->ParentWeights.RemoveAt(ParentIndex);
			MultiParentElement->ParentWeightsInitial.RemoveAt(ParentIndex);
			MultiParentElement->IndexLookup.Remove(ParentTransformElement->GetKey());
			for(TPair<FRigElementKey, int32>& Pair : MultiParentElement->IndexLookup)
			{
				if(Pair.Value > ParentIndex)
				{
					Pair.Value--;
				}
			}

			MultiParentElement->Parent.MarkDirty(ERigTransformType::CurrentGlobal);  
			MultiParentElement->Parent.MarkDirty(ERigTransformType::InitialGlobal);  

			if(FRigControlElement* ControlElement = Cast<FRigControlElement>(MultiParentElement))
			{
				ControlElement->Offset.MarkDirty(ERigTransformType::CurrentGlobal);  
				ControlElement->Offset.MarkDirty(ERigTransformType::InitialGlobal);
				ControlElement->Gizmo.MarkDirty(ERigTransformType::CurrentGlobal);  
				ControlElement->Gizmo.MarkDirty(ERigTransformType::InitialGlobal);
			}

			Hierarchy->TopologyVersion++;

			if(!bMaintainGlobalTransform)
			{
				Hierarchy->PropagateDirtyFlags(MultiParentElement, true, true);
				Hierarchy->PropagateDirtyFlags(MultiParentElement, false, true);
			}

			Notify(ERigHierarchyNotification::ParentChanged, MultiParentElement);

			return true;
		}
	}

	return false;
}

bool URigHierarchyController::RemoveAllParents(FRigElementKey InChild, bool bMaintainGlobalTransform, bool bSetupUndo)
{
	if(!IsValid())
	{
		return false;
	}

	FRigBaseElement* Child = Hierarchy->Find(InChild);
	if(Child == nullptr)
	{
		ReportWarningf(TEXT("Cannot Remove All Parents, Child '%s' not found."), *InChild.ToString());
		return false;
	}

#if WITH_EDITOR
	TSharedPtr<FScopedTransaction> TransactionPtr;
	if(bSetupUndo)
	{
		TransactionPtr = MakeShared<FScopedTransaction>(NSLOCTEXT("RigHierarchyController", "Remove Parent", "Remove Parent"));
		Hierarchy->Modify();
	}
#endif

	const bool bRemoved = RemoveAllParents(Child, bMaintainGlobalTransform);

#if WITH_EDITOR
	if(!bRemoved && TransactionPtr.IsValid())
	{
		TransactionPtr->Cancel();
	}
	TransactionPtr.Reset();
#endif

	return bRemoved;
}

bool URigHierarchyController::RemoveAllParents(FRigBaseElement* InChild, bool bMaintainGlobalTransform)
{
	if(FRigSingleParentElement* SingleParentElement = Cast<FRigSingleParentElement>(InChild))
	{
		return RemoveParent(SingleParentElement, SingleParentElement->ParentElement, bMaintainGlobalTransform);
	}
	else if(FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(InChild))
	{
		bool bSuccess = true;

		TArray<FRigTransformElement*> Parents = MultiParentElement->ParentElements;
		for(FRigTransformElement* Parent : Parents)
		{
			if(!RemoveParent(MultiParentElement, Parent, bMaintainGlobalTransform))
			{
				bSuccess = false;
			}
		}

		return bSuccess;
	}
	return false;
}

bool URigHierarchyController::SetParent(FRigElementKey InChild, FRigElementKey InParent, bool bMaintainGlobalTransform, bool bSetupUndo)
{
	if(!IsValid())
	{
		return false;
	}

	FRigBaseElement* Child = Hierarchy->Find(InChild);
	if(Child == nullptr)
	{
		ReportWarningf(TEXT("Cannot Set Parent, Child '%s' not found."), *InChild.ToString());
		return false;
	}

	FRigBaseElement* Parent = Hierarchy->Find(InParent);
	if(Parent == nullptr)
	{
		ReportWarningf(TEXT("Cannot Set Parent, Parent '%s' not found."), *InParent.ToString());
		return false;
	}

#if WITH_EDITOR
	TSharedPtr<FScopedTransaction> TransactionPtr;
	if(bSetupUndo)
	{
		TransactionPtr = MakeShared<FScopedTransaction>(NSLOCTEXT("RigHierarchyController", "Set Parent", "Set Parent"));
		Hierarchy->Modify();
	}
#endif

	const bool bParentSet = SetParent(Child, Parent, bMaintainGlobalTransform);

#if WITH_EDITOR
	if(!bParentSet && TransactionPtr.IsValid())
	{
		TransactionPtr->Cancel();
	}
	TransactionPtr.Reset();
#endif

	return bParentSet;
}

TArray<FRigElementKey> URigHierarchyController::DuplicateElements(TArray<FRigElementKey> InKeys, bool bSelectNewElements, bool bSetupUndo)
{
	const FString Content = ExportToText(InKeys);
	return ImportFromText(Content, false, bSelectNewElements, bSetupUndo);
}

TArray<FRigElementKey> URigHierarchyController::MirrorElements(TArray<FRigElementKey> InKeys, FRigMirrorSettings InSettings, bool bSelectNewElements, bool bSetupUndo)
{
	TArray<FRigElementKey> OriginalKeys = Hierarchy->SortKeys(InKeys);
	TArray<FRigElementKey> DuplicatedKeys = DuplicateElements(OriginalKeys, bSelectNewElements, bSetupUndo);

	if (DuplicatedKeys.Num() != OriginalKeys.Num())
	{
		return DuplicatedKeys;
	}

	for (int32 Index = 0; Index < OriginalKeys.Num(); Index++)
	{
		if (DuplicatedKeys[Index].Type != OriginalKeys[Index].Type)
		{
			return DuplicatedKeys;
		}
	}

	// mirror the transforms
	for (int32 Index = 0; Index < OriginalKeys.Num(); Index++)
	{
		FTransform GlobalTransform = Hierarchy->GetGlobalTransform(OriginalKeys[Index]);
		FTransform InitialTransform = Hierarchy->GetInitialGlobalTransform(OriginalKeys[Index]);

		// also mirror the offset, limits and gizmo transform
		if (OriginalKeys[Index].Type == ERigElementType::Control)
		{
			if(FRigControlElement* DuplicatedControlElement = Hierarchy->Find<FRigControlElement>(DuplicatedKeys[Index]))
			{
				TGuardValue<bool> DisableTranslationLimit(DuplicatedControlElement->Settings.bLimitTranslation, false);
				TGuardValue<bool> DisableRotationLimit(DuplicatedControlElement->Settings.bLimitRotation, false);
				TGuardValue<bool> DisableScaleLimit(DuplicatedControlElement->Settings.bLimitScale, false);

				// mirror offset
				FTransform OriginalGlobalOffsetTransform = Hierarchy->GetGlobalControlOffsetTransform(OriginalKeys[Index]);
				FTransform ParentTransform = Hierarchy->GetParentTransform(DuplicatedKeys[Index]);
				FTransform OffsetTransform = InSettings.MirrorTransform(OriginalGlobalOffsetTransform).GetRelativeTransform(ParentTransform);
				Hierarchy->SetControlOffsetTransform(DuplicatedKeys[Index], OffsetTransform, true, false, true);
				Hierarchy->SetControlOffsetTransform(DuplicatedKeys[Index], OffsetTransform, false, false, true);

				// mirror limits
				FTransform DuplicatedGlobalOffsetTransform = Hierarchy->GetGlobalControlOffsetTransform(DuplicatedKeys[Index]);

				for (ERigControlValueType ValueType = ERigControlValueType::Minimum;
                    ValueType <= ERigControlValueType::Maximum;
                    ValueType = (ERigControlValueType)(uint8(ValueType) + 1)
                    )
				{
					const FRigControlValue LimitValue = Hierarchy->GetControlValue(DuplicatedKeys[Index], ValueType);
					const FTransform LocalLimitTransform = LimitValue.GetAsTransform(DuplicatedControlElement->Settings.ControlType, DuplicatedControlElement->Settings.PrimaryAxis);
					FTransform GlobalLimitTransform = LocalLimitTransform * OriginalGlobalOffsetTransform;
					FTransform DuplicatedLimitTransform = InSettings.MirrorTransform(GlobalLimitTransform).GetRelativeTransform(DuplicatedGlobalOffsetTransform);
					FRigControlValue DuplicatedValue;
					DuplicatedValue.SetFromTransform(DuplicatedLimitTransform, DuplicatedControlElement->Settings.ControlType, DuplicatedControlElement->Settings.PrimaryAxis);
					Hierarchy->SetControlValue(DuplicatedControlElement, DuplicatedValue, ValueType, false);
				}

				// we need to do this here to make sure that the limits don't apply ( the tguardvalue is still active within here )
				Hierarchy->SetGlobalTransform(DuplicatedKeys[Index], InSettings.MirrorTransform(GlobalTransform), true, false, true);
				Hierarchy->SetGlobalTransform(DuplicatedKeys[Index], InSettings.MirrorTransform(GlobalTransform), false, false, true);

				// mirror gizmo transform
				FTransform GlobalGizmoTransform = Hierarchy->GetControlGizmoTransform(DuplicatedControlElement, ERigTransformType::InitialLocal) * OriginalGlobalOffsetTransform;
				Hierarchy->SetControlGizmoTransform(DuplicatedControlElement, InSettings.MirrorTransform(GlobalGizmoTransform).GetRelativeTransform(DuplicatedGlobalOffsetTransform), ERigTransformType::InitialLocal, true);
				Hierarchy->SetControlGizmoTransform(DuplicatedControlElement, InSettings.MirrorTransform(GlobalGizmoTransform).GetRelativeTransform(DuplicatedGlobalOffsetTransform), ERigTransformType::CurrentLocal, true);
			}
		}
		else
		{
			Hierarchy->SetGlobalTransform(DuplicatedKeys[Index], InSettings.MirrorTransform(GlobalTransform), true, false, true);
			Hierarchy->SetGlobalTransform(DuplicatedKeys[Index], InSettings.MirrorTransform(GlobalTransform), false, false, true);
		}
	}

	// correct the names
	if (!InSettings.OldName.IsEmpty() && !InSettings.NewName.IsEmpty())
	{
		URigHierarchyController* Controller = Hierarchy->GetController(true);
		check(Controller);
		
		for (int32 Index = 0; Index < DuplicatedKeys.Num(); Index++)
		{
			FName OldName = OriginalKeys[Index].Name;
			FString OldNameStr = OldName.ToString();
			FString NewNameStr = OldNameStr.Replace(*InSettings.OldName, *InSettings.NewName, ESearchCase::CaseSensitive);
			if (NewNameStr != OldNameStr)
			{
				Controller->RenameElement(DuplicatedKeys[Index], *NewNameStr, true);
			}
		}
	}

	return DuplicatedKeys;
}

bool URigHierarchyController::SetParent(FRigBaseElement* InChild, FRigBaseElement* InParent, bool bMaintainGlobalTransform)
{
	if(InChild == nullptr || InParent == nullptr)
	{
		return false;
	}
	return AddParent(InChild, InParent, bMaintainGlobalTransform, true);
}

void URigHierarchyController::AddElementToDirty(FRigBaseElement* InParent, FRigBaseElement* InElementToAdd, int32 InHierarchyDistance) const
{
	if(InParent == nullptr)
	{
		return;
	} 

	FRigTransformElement* ElementToAdd = Cast<FRigTransformElement>(InElementToAdd);
	if(ElementToAdd == nullptr)
	{
		return;
	}

	if(FRigTransformElement* TransformParent = Cast<FRigTransformElement>(InParent))
	{
		const FRigTransformElement::FElementToDirty ElementToDirty(ElementToAdd, InHierarchyDistance);
		TransformParent->ElementsToDirty.AddUnique(ElementToDirty);
	}

#if URIGHIERARCHY_RECURSIVE_DIRTY_PROPAGATION
	// nothing to do 
#else
	if(FRigSingleParentElement* SingleParentElement = Cast<FRigSingleParentElement>(InParent))
	{
		AddElementToDirty(SingleParentElement->ParentElement, InElementToAdd, InHierarchyDistance + 1);
	}
	else if(FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(InParent))
	{
		for(FRigTransformElement* ParentElement : MultiParentElement->ParentElements)
		{
			AddElementToDirty(ParentElement, InElementToAdd, InHierarchyDistance + 1);
		}
	}
#endif
}

void URigHierarchyController::RemoveElementToDirty(FRigBaseElement* InParent, FRigBaseElement* InElementToRemove) const
{
	if(InParent == nullptr)
	{
		return;
	}

	FRigTransformElement* ElementToRemove = Cast<FRigTransformElement>(InElementToRemove);
	if(ElementToRemove == nullptr)
	{
		return;
	}

	if(FRigTransformElement* TransformParent = Cast<FRigTransformElement>(InParent))
	{
		TransformParent->ElementsToDirty.Remove(ElementToRemove);
	}

#if URIGHIERARCHY_RECURSIVE_DIRTY_PROPAGATION
	// nothing to do 
#else
	if(FRigSingleParentElement* SingleParentElement = Cast<FRigSingleParentElement>(InParent))
	{
		RemoveElementToDirty(SingleParentElement->ParentElement, InElementToRemove);
	}
	else if(FRigMultiParentElement* MultiParentElement = Cast<FRigMultiParentElement>(InParent))
	{
		for(FRigTransformElement* ParentElement : MultiParentElement->ParentElements)
		{
			RemoveElementToDirty(ParentElement, InElementToRemove);
		}
	}
#endif
}

void URigHierarchyController::ReportWarning(const FString& InMessage) const
{
	if(!bReportWarningsAndErrors)
	{
		return;
	}

	FString Message = InMessage;
	if (Hierarchy)
	{
		if (UPackage* Package = Cast<UPackage>(Hierarchy->GetOutermost()))
		{
			Message = FString::Printf(TEXT("%s : %s"), *Package->GetPathName(), *InMessage);
		}
	}

	FScriptExceptionHandler::Get().HandleException(ELogVerbosity::Warning, *Message, *FString());
}

void URigHierarchyController::ReportError(const FString& InMessage) const
{
	if(!bReportWarningsAndErrors)
	{
		return;
	}

	FString Message = InMessage;
	if (Hierarchy)
	{
		if (UPackage* Package = Cast<UPackage>(Hierarchy->GetOutermost()))
		{
			Message = FString::Printf(TEXT("%s : %s"), *Package->GetPathName(), *InMessage);
		}
	}

	FScriptExceptionHandler::Get().HandleException(ELogVerbosity::Error, *Message, *FString());
}

void URigHierarchyController::ReportAndNotifyError(const FString& InMessage) const
{
	if (!bReportWarningsAndErrors)
	{
		return;
	}

	ReportError(InMessage);

#if WITH_EDITOR
	FNotificationInfo Info(FText::FromString(InMessage));
	Info.bUseSuccessFailIcons = true;
	Info.Image = FEditorStyle::GetBrush(TEXT("MessageLog.Warning"));
	Info.bFireAndForget = true;
	Info.bUseThrobber = true;
	// longer message needs more time to read
	Info.FadeOutDuration = FMath::Clamp(0.1f * InMessage.Len(), 5.0f, 20.0f);
	Info.ExpireDuration = Info.FadeOutDuration;
	TSharedPtr<SNotificationItem> NotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
	if (NotificationPtr)
	{
		NotificationPtr->SetCompletionState(SNotificationItem::CS_Fail);
	}
#endif
}
