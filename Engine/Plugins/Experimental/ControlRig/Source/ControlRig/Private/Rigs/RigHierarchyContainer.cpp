// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Rigs/RigHierarchyContainer.h"
#include "ControlRig.h"
#include "HelperUtil.h"
#include "UObject/PropertyPortFlags.h"

////////////////////////////////////////////////////////////////////////////////
// FRigHierarchyContainer
////////////////////////////////////////////////////////////////////////////////

FRigHierarchyContainer::FRigHierarchyContainer()
{
	Initialize();
}

FRigHierarchyContainer& FRigHierarchyContainer::operator= (const FRigHierarchyContainer &InOther)
{
	BoneHierarchy = InOther.BoneHierarchy;
	SpaceHierarchy = InOther.SpaceHierarchy;
	ControlHierarchy = InOther.ControlHierarchy;
	CurveContainer = InOther.CurveContainer;
	return *this;
}

void FRigHierarchyContainer::Initialize()
{
	BoneHierarchy.Container = this;
	SpaceHierarchy.Container = this;
	ControlHierarchy.Container = this;
	CurveContainer.Container = this;

#if WITH_EDITOR
	BoneHierarchy.OnBoneAdded.RemoveAll(this);
	BoneHierarchy.OnBoneRemoved.RemoveAll(this);
	BoneHierarchy.OnBoneRenamed.RemoveAll(this);
	BoneHierarchy.OnBoneReparented.RemoveAll(this);
	BoneHierarchy.OnBoneSelected.RemoveAll(this);

	BoneHierarchy.OnBoneAdded.AddRaw(this, &FRigHierarchyContainer::HandleOnElementAdded);
	BoneHierarchy.OnBoneRemoved.AddRaw(this, &FRigHierarchyContainer::HandleOnElementRemoved);
	BoneHierarchy.OnBoneRenamed.AddRaw(this, &FRigHierarchyContainer::HandleOnElementRenamed);
	BoneHierarchy.OnBoneReparented.AddRaw(this, &FRigHierarchyContainer::HandleOnElementReparented);
	BoneHierarchy.OnBoneSelected.AddRaw(this, &FRigHierarchyContainer::HandleOnElementSelected);

	SpaceHierarchy.OnSpaceAdded.RemoveAll(this);
	SpaceHierarchy.OnSpaceRemoved.RemoveAll(this);
	SpaceHierarchy.OnSpaceRenamed.RemoveAll(this);
	SpaceHierarchy.OnSpaceReparented.RemoveAll(this);
	SpaceHierarchy.OnSpaceSelected.RemoveAll(this);

	SpaceHierarchy.OnSpaceAdded.AddRaw(this, &FRigHierarchyContainer::HandleOnElementAdded);
	SpaceHierarchy.OnSpaceRemoved.AddRaw(this, &FRigHierarchyContainer::HandleOnElementRemoved);
	SpaceHierarchy.OnSpaceRenamed.AddRaw(this, &FRigHierarchyContainer::HandleOnElementRenamed);
	SpaceHierarchy.OnSpaceReparented.AddRaw(this, &FRigHierarchyContainer::HandleOnElementReparented);
	SpaceHierarchy.OnSpaceSelected.AddRaw(this, &FRigHierarchyContainer::HandleOnElementSelected);

	ControlHierarchy.OnControlAdded.RemoveAll(this);
	ControlHierarchy.OnControlRemoved.RemoveAll(this);
	ControlHierarchy.OnControlRenamed.RemoveAll(this);
	ControlHierarchy.OnControlReparented.RemoveAll(this);
	ControlHierarchy.OnControlSelected.RemoveAll(this);

	ControlHierarchy.OnControlAdded.AddRaw(this, &FRigHierarchyContainer::HandleOnElementAdded);
	ControlHierarchy.OnControlRemoved.AddRaw(this, &FRigHierarchyContainer::HandleOnElementRemoved);
	ControlHierarchy.OnControlRenamed.AddRaw(this, &FRigHierarchyContainer::HandleOnElementRenamed);
	ControlHierarchy.OnControlReparented.AddRaw(this, &FRigHierarchyContainer::HandleOnElementReparented);
	ControlHierarchy.OnControlSelected.AddRaw(this, &FRigHierarchyContainer::HandleOnElementSelected);

	CurveContainer.OnCurveAdded.RemoveAll(this);
	CurveContainer.OnCurveRemoved.RemoveAll(this);
	CurveContainer.OnCurveRenamed.RemoveAll(this);
	CurveContainer.OnCurveSelected.RemoveAll(this);

	CurveContainer.OnCurveAdded.AddRaw(this, &FRigHierarchyContainer::HandleOnElementAdded);
	CurveContainer.OnCurveRemoved.AddRaw(this, &FRigHierarchyContainer::HandleOnElementRemoved);
	CurveContainer.OnCurveRenamed.AddRaw(this, &FRigHierarchyContainer::HandleOnElementRenamed);
	CurveContainer.OnCurveSelected.AddRaw(this, &FRigHierarchyContainer::HandleOnElementSelected);

	// wire them between each other
	BoneHierarchy.OnBoneRenamed.RemoveAll(&SpaceHierarchy);
	SpaceHierarchy.OnSpaceRenamed.RemoveAll(&ControlHierarchy);
	ControlHierarchy.OnControlRenamed.RemoveAll(&SpaceHierarchy);
	BoneHierarchy.OnBoneRenamed.AddRaw(&SpaceHierarchy, &FRigSpaceHierarchy::HandleOnElementRenamed);
	SpaceHierarchy.OnSpaceRenamed.AddRaw(&ControlHierarchy, &FRigControlHierarchy::HandleOnElementRenamed);
	ControlHierarchy.OnControlRenamed.AddRaw(&SpaceHierarchy, &FRigSpaceHierarchy::HandleOnElementRenamed);
	BoneHierarchy.OnBoneRemoved.AddRaw(&SpaceHierarchy, &FRigSpaceHierarchy::HandleOnElementRemoved);
	ControlHierarchy.OnControlRemoved.AddRaw(&SpaceHierarchy, &FRigSpaceHierarchy::HandleOnElementRemoved);
	SpaceHierarchy.OnSpaceRemoved.AddRaw(&ControlHierarchy, &FRigControlHierarchy::HandleOnElementRemoved);

#endif

	BoneHierarchy.Initialize();
	SpaceHierarchy.Initialize();
	ControlHierarchy.Initialize();
	CurveContainer.Initialize();

	ResetTransforms();
}

void FRigHierarchyContainer::Reset()
{
	BoneHierarchy.Reset();
	SpaceHierarchy.Reset();
	ControlHierarchy.Reset();
	CurveContainer.Reset();

	Initialize();
}

void FRigHierarchyContainer::ResetTransforms()
{
	BoneHierarchy.ResetTransforms();
	SpaceHierarchy.ResetTransforms();
	ControlHierarchy.ResetValues();
	CurveContainer.ResetValues();
}

FTransform FRigHierarchyContainer::GetInitialTransform(ERigElementType InElementType, int32 InIndex) const
{
	if(InIndex == INDEX_NONE)
	{
		return FTransform::Identity;
	}

	switch(InElementType)
	{
		case ERigElementType::Bone:
		{
			return BoneHierarchy.GetInitialTransform(InIndex);
		}
		case ERigElementType::Space:
		{
			return SpaceHierarchy.GetInitialTransform(InIndex);
		}
		case ERigElementType::Control:
		{
			return ControlHierarchy.GetInitialValue<FTransform>(InIndex);
		}
		case ERigElementType::Curve:
		{
			break;
		}
	}

	return FTransform::Identity;
}

#if WITH_EDITOR

void FRigHierarchyContainer::SetInitialTransform(ERigElementType InElementType, int32 InIndex, const FTransform& InTransform)
{
	if (InIndex == INDEX_NONE)
	{
		return;
	}

	switch(InElementType)
	{
		case ERigElementType::Bone:
		{
			BoneHierarchy.SetInitialTransform(InIndex, InTransform);
			break;
		}
		case ERigElementType::Space:
		{
			SpaceHierarchy.SetInitialTransform(InIndex, InTransform);
			break;
		}
		case ERigElementType::Control:
		{
			ControlHierarchy.SetInitialValue<FTransform>(InIndex, InTransform);
			break;
		}
		case ERigElementType::Curve:
		{
			break;
		}
	}
}

#endif

FTransform FRigHierarchyContainer::GetInitialGlobalTransform(ERigElementType InElementType, int32 InIndex) const
{
	if (InIndex == INDEX_NONE)
	{
		return FTransform::Identity;
	}

	switch (InElementType)
	{
		case ERigElementType::Bone:
		{
			return BoneHierarchy.GetInitialTransform(InIndex);
		}
		case ERigElementType::Space:
		{
			return SpaceHierarchy.GetInitialGlobalTransform(InIndex);
		}
		case ERigElementType::Control:
		{
			return ControlHierarchy.GetInitialGlobalTransform(InIndex);
		}
		case ERigElementType::Curve:
		{
			break;
		}
	}

	return FTransform::Identity;
}

void FRigHierarchyContainer::SetInitialGlobalTransform(ERigElementType InElementType, int32 InIndex, const FTransform& InTransform)
{
	if (InIndex == INDEX_NONE)
	{
		return;
	}

	switch (InElementType)
	{
		case ERigElementType::Bone:
		{
			BoneHierarchy.SetInitialTransform(InIndex, InTransform);
			break;
		}
		case ERigElementType::Space:
		{
			SpaceHierarchy.SetInitialGlobalTransform(InIndex, InTransform);
			break;
		}
		case ERigElementType::Control:
		{
			ControlHierarchy.SetInitialGlobalTransform(InIndex, InTransform);
			break;
		}
		case ERigElementType::Curve:
		{
			break;
		}
	}
}

FTransform FRigHierarchyContainer::GetLocalTransform(ERigElementType InElementType, int32 InIndex) const
{
	if(InIndex == INDEX_NONE)
	{
		return FTransform::Identity;
	}

	switch(InElementType)
	{
		case ERigElementType::Bone:
		{
			return BoneHierarchy.GetLocalTransform(InIndex);
		}
		case ERigElementType::Space:
		{
			return SpaceHierarchy.GetLocalTransform(InIndex);
		}
		case ERigElementType::Control:
		{
			return ControlHierarchy.GetLocalTransform(InIndex);
		}
		case ERigElementType::Curve:
		{
			break;
		}
	}

	return FTransform::Identity;
}

void FRigHierarchyContainer::SetLocalTransform(ERigElementType InElementType, int32 InIndex, const FTransform& InTransform)
{
	if (InIndex == INDEX_NONE)
	{
		return;
	}

	switch(InElementType)
	{
		case ERigElementType::Bone:
		{
			BoneHierarchy.SetLocalTransform(InIndex, InTransform);
			break;
		}
		case ERigElementType::Space:
		{
			SpaceHierarchy.SetLocalTransform(InIndex, InTransform);
			break;
		}
		case ERigElementType::Control:
		{
			ControlHierarchy.SetLocalTransform(InIndex, InTransform);
			break;
		}
		case ERigElementType::Curve:
		{
			break;
		}
	}
}

FTransform FRigHierarchyContainer::GetGlobalTransform(ERigElementType InElementType, int32 InIndex) const
{
	if(InIndex == INDEX_NONE)
	{
		return FTransform::Identity;
	}

	switch(InElementType)
	{
		case ERigElementType::Bone:
		{
			return BoneHierarchy.GetGlobalTransform(InIndex);
		}
		case ERigElementType::Space:
		{
			return SpaceHierarchy.GetGlobalTransform(InIndex);
		}
		case ERigElementType::Control:
		{
			return ControlHierarchy.GetGlobalTransform(InIndex);
		}
		case ERigElementType::Curve:
		{
			break;
		}
	}

	return FTransform::Identity;
}

void FRigHierarchyContainer::SetGlobalTransform(ERigElementType InElementType, int32 InIndex, const FTransform& InTransform)
{
	if (InIndex == INDEX_NONE)
	{
		return;
	}

	switch(InElementType)
	{
		case ERigElementType::Bone:
		{
			BoneHierarchy.SetGlobalTransform(InIndex, InTransform);
			break;
		}
		case ERigElementType::Space:
		{
			SpaceHierarchy.SetGlobalTransform(InIndex, InTransform);
			break;
		}
		case ERigElementType::Control:
		{
			ControlHierarchy.SetGlobalTransform(InIndex, InTransform);
			break;
		}
		case ERigElementType::Curve:
		{
			break;
		}
	}
}

#if WITH_EDITOR

void FRigHierarchyContainer::HandleOnElementAdded(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey)
{
	// todo
	OnElementAdded.Broadcast(InContainer, InKey);
	OnElementChanged.Broadcast(InContainer, InKey);
}

void FRigHierarchyContainer::HandleOnElementRemoved(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey)
{
	// todo
	OnElementRemoved.Broadcast(InContainer, InKey);
	OnElementChanged.Broadcast(InContainer, InKey);
}

void FRigHierarchyContainer::HandleOnElementRenamed(FRigHierarchyContainer* InContainer, ERigElementType InElementType, const FName& InOldName, const FName& InNewName)
{
	// todo
	OnElementRenamed.Broadcast(InContainer, InElementType, InOldName, InNewName);
	OnElementChanged.Broadcast(InContainer, FRigElementKey(InNewName, InElementType));
}

void FRigHierarchyContainer::HandleOnElementReparented(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey, const FName& InOldParentName, const FName& InNewParentName)
{
	// todo
	OnElementReparented.Broadcast(InContainer, InKey, InOldParentName, InNewParentName);
	OnElementChanged.Broadcast(InContainer, InKey);
}

void FRigHierarchyContainer::HandleOnElementSelected(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey, bool bSelected)
{
	OnElementSelected.Broadcast(InContainer, InKey, bSelected);
	OnElementChanged.Broadcast(InContainer, InKey);
}

#endif

bool FRigHierarchyContainer::IsParentedTo(ERigElementType InChildType, int32 InChildIndex, ERigElementType InParentType, int32 InParentIndex) const
{
	if (InParentIndex == INDEX_NONE || InChildIndex == INDEX_NONE)
	{
		return false;
	}

	switch (InChildType)
	{
		case ERigElementType::Curve:
		{
			return false;
		}
		case ERigElementType::Bone:
		{
			switch (InParentType)
			{
				case ERigElementType::Bone:
				{
					if (BoneHierarchy[InChildIndex].ParentIndex != INDEX_NONE)
					{
						if (BoneHierarchy[InChildIndex].ParentIndex == InParentIndex)
						{
							return true;
						}
						return IsParentedTo(ERigElementType::Bone, BoneHierarchy[InChildIndex].ParentIndex, InParentType, InParentIndex);
					}
					// no break - fall through to next case
				}
				case ERigElementType::Space:
				case ERigElementType::Control:
				case ERigElementType::Curve:
				{
					return false;
				}
			}
		}
		case ERigElementType::Space:
		{
			const FRigSpace& ChildSpace = SpaceHierarchy[InChildIndex];
			switch (ChildSpace.SpaceType)
			{
				case ERigSpaceType::Global:
				{
					return false;
				}
				case ERigSpaceType::Bone:
				{
					if (ChildSpace.ParentIndex == InParentIndex && InParentType == ERigElementType::Bone)
					{
						return true;
					}
					return IsParentedTo(ERigElementType::Bone, ChildSpace.ParentIndex, InParentType, InParentIndex);
				}
				case ERigSpaceType::Space:
				{
					if (ChildSpace.ParentIndex == InParentIndex && InParentType == ERigElementType::Space)
					{
						return true;
					}
					return IsParentedTo(ERigElementType::Space, ChildSpace.ParentIndex, InParentType, InParentIndex);
				}
				case ERigSpaceType::Control:
				{
					if (ChildSpace.ParentIndex == InParentIndex && InParentType == ERigElementType::Control)
					{
						return true;
					}
					return IsParentedTo(ERigElementType::Control, ChildSpace.ParentIndex, InParentType, InParentIndex);
				}
			}
		}
		case ERigElementType::Control:
		{
			const FRigControl& ChildControl = ControlHierarchy[InChildIndex];
			switch (InParentType)
			{
				case ERigElementType::Space:
				{
					if (ChildControl.SpaceIndex == InParentIndex)
					{
						return true;
					}
					// no break - fall through to next cases
				}
				case ERigElementType::Control:
				case ERigElementType::Bone:
				{
					if (ChildControl.SpaceIndex != INDEX_NONE)
					{
						return IsParentedTo(ERigElementType::Space, ChildControl.SpaceIndex, InParentType, InParentIndex);
					}
					else if (ChildControl.ParentIndex != INDEX_NONE)
					{
						if (ChildControl.ParentIndex == InParentIndex)
						{
							return true;
						}
						return IsParentedTo(ERigElementType::Control, ChildControl.ParentIndex, InParentType, InParentIndex);
					}
				}
				case ERigElementType::Curve:
				{
					return false;
				}
			}
		}
	}

	return false;
}

#if WITH_EDITOR

FString FRigHierarchyContainer::ExportSelectionToText() const
{
	TArray<FRigElementKey> Selection = CurrentSelection();
	return ExportToText(Selection);
}

FString FRigHierarchyContainer::ExportToText(const TArray<FRigElementKey>& InSelection) const
{
	FRigHierarchyCopyPasteContent Data;
	for (const FRigElementKey& Key : InSelection)
	{
		Data.Types.Add(Key.Type);
		FString Content;
		switch (Key.Type)
		{
		case ERigElementType::Bone:
		{
			FRigBone::StaticStruct()->ExportText(Content, &BoneHierarchy[Key.Name], nullptr, nullptr, PPF_None, nullptr);
			break;
		}
		case ERigElementType::Control:
		{
			FRigControl::StaticStruct()->ExportText(Content, &ControlHierarchy[Key.Name], nullptr, nullptr, PPF_None, nullptr);
			break;
		}
		case ERigElementType::Space:
		{
			FRigSpace::StaticStruct()->ExportText(Content, &SpaceHierarchy[Key.Name], nullptr, nullptr, PPF_None, nullptr);
			break;
		}
		case ERigElementType::Curve:
		{
			FRigCurve::StaticStruct()->ExportText(Content, &CurveContainer[Key.Name], nullptr, nullptr, PPF_None, nullptr);
			break;
		}
		default:
		{
			ensure(false);
			break;
		}
		}
		Data.Contents.Add(Content);
		Data.LocalTransforms.Add(GetLocalTransform(Key));
		Data.GlobalTransforms.Add(GetGlobalTransform(Key));
	}

	FString ExportedText;
	FRigHierarchyCopyPasteContent::StaticStruct()->ExportText(ExportedText, &Data, nullptr, nullptr, PPF_None, nullptr);
	return ExportedText;
}

class FRigHierarchyContainerImportErrorContext : public FOutputDevice
{
public:

	int32 NumErrors;

	FRigHierarchyContainerImportErrorContext()
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

TArray<FRigElementKey> FRigHierarchyContainer::ImportFromText(const FString& InContent, ERigHierarchyImportMode InImportMode, bool bSelectNewElements)
{
	TArray<FRigElementKey> PastedKeys;

	FRigHierarchyCopyPasteContent Data;
	FRigHierarchyContainerImportErrorContext ErrorPipe;
	FRigHierarchyCopyPasteContent::StaticStruct()->ImportText(*InContent, &Data, nullptr, EPropertyPortFlags::PPF_None, &ErrorPipe, FRigHierarchyCopyPasteContent::StaticStruct()->GetName(), true);
	if (ErrorPipe.NumErrors > 0)
	{
		return PastedKeys;
	}

	if (Data.Contents.Num() == 0 ||
		(Data.Types.Num() != Data.Contents.Num()) ||
		(Data.LocalTransforms.Num() != Data.Contents.Num()) ||
		(Data.GlobalTransforms.Num() != Data.Contents.Num()))
	{
		return PastedKeys;
	}

	TMap<FRigElementKey, FRigElementKey> ElementMap;
	for (const FRigBone& Element : BoneHierarchy)
	{
		ElementMap.Add(Element.GetElementKey(), Element.GetElementKey());
	}
	for (const FRigControl& Element : ControlHierarchy)
	{
		ElementMap.Add(Element.GetElementKey(), Element.GetElementKey());
	}
	for (const FRigSpace& Element : SpaceHierarchy)
	{
		ElementMap.Add(Element.GetElementKey(), Element.GetElementKey());
	}
	for (const FRigCurve& Element : CurveContainer)
	{
		ElementMap.Add(Element.GetElementKey(), Element.GetElementKey());
	}

	TArray<TSharedPtr<FRigElement>> Elements;
	for (int32 Index = 0; Index < Data.Types.Num(); Index++)
	{
		ErrorPipe.NumErrors = 0;
		TSharedPtr<FRigElement> NewElement;
		switch (Data.Types[Index])
		{
			case ERigElementType::Bone:
			{
				NewElement = MakeShared<FRigBone>();
				FRigBone::StaticStruct()->ImportText(*Data.Contents[Index], NewElement.Get(), nullptr, EPropertyPortFlags::PPF_None, &ErrorPipe, FRigBone::StaticStruct()->GetName(), true);
				break;
			}
			case ERigElementType::Control:
			{
				NewElement = MakeShared<FRigControl>();
				FRigControl::StaticStruct()->ImportText(*Data.Contents[Index], NewElement.Get(), nullptr, EPropertyPortFlags::PPF_None, &ErrorPipe, FRigControl::StaticStruct()->GetName(), true);
				break;
			}
			case ERigElementType::Space:
			{
				NewElement = MakeShared<FRigSpace>();
				FRigSpace::StaticStruct()->ImportText(*Data.Contents[Index], NewElement.Get(), nullptr, EPropertyPortFlags::PPF_None, &ErrorPipe, FRigSpace::StaticStruct()->GetName(), true);
				break;
			}
			case ERigElementType::Curve:
			{
				NewElement = MakeShared<FRigCurve>();
				FRigCurve::StaticStruct()->ImportText(*Data.Contents[Index], NewElement.Get(), nullptr, EPropertyPortFlags::PPF_None, &ErrorPipe, FRigCurve::StaticStruct()->GetName(), true);
				break;
			}
			default:
			{
				ensure(false);
				break;
			}
		}

		if (ErrorPipe.NumErrors > 0)
		{
			return PastedKeys;
		}

		Elements.Add(NewElement);
	}

	TArray<FRigElementKey> Selection = CurrentSelection();

	switch (InImportMode)
	{
		case ERigHierarchyImportMode::Append:
		{
			for (int32 Index = 0; Index < Data.Types.Num(); Index++)
			{
				switch (Data.Types[Index])
				{
					case ERigElementType::Bone:
					{
						const FRigBone& Element = *static_cast<FRigBone*>(Elements[Index].Get());

						FName ParentName = NAME_None;
						if (const FRigElementKey* ParentKey = ElementMap.Find(Element.GetParentElementKey()))
						{
							ParentName = ParentKey->Name;
						}

						FRigBone& NewElement = BoneHierarchy.Add(Element.Name, ParentName, ERigBoneType::User, Element.InitialTransform, Element.LocalTransform, Data.GlobalTransforms[Index]);
						ElementMap.FindOrAdd(Element.GetElementKey()) = NewElement.GetElementKey();
						PastedKeys.Add(NewElement.GetElementKey());
						break;
					}
					case ERigElementType::Control:
					{
						const FRigControl& Element = *static_cast<FRigControl*>(Elements[Index].Get());

						FName ParentName = NAME_None;
						if (const FRigElementKey* ParentKey = ElementMap.Find(Element.GetParentElementKey()))
						{
							ParentName = ParentKey->Name;
						}

						FName SpaceName = NAME_None;
						if (const FRigElementKey* SpaceKey = ElementMap.Find(Element.GetSpaceElementKey()))
						{
							SpaceName = SpaceKey->Name;
						}

						FRigControl& NewElement = ControlHierarchy.Add(Element.Name, Element.ControlType, ParentName, SpaceName, Element.InitialValue, Element.GizmoName, Element.GizmoTransform, Element.GizmoColor);
						ElementMap.FindOrAdd(Element.GetElementKey()) = NewElement.GetElementKey();
						PastedKeys.Add(NewElement.GetElementKey());

						break;
					}
					case ERigElementType::Space:
					{
						const FRigSpace& Element = *static_cast<FRigSpace*>(Elements[Index].Get());

						FName ParentName = NAME_None;
						if (const FRigElementKey* ParentKey = ElementMap.Find(Element.GetParentElementKey()))
						{
							ParentName = ParentKey->Name;
						}

						FRigSpace& NewElement = SpaceHierarchy.Add(Element.Name, Element.SpaceType, ParentName, Element.InitialTransform);
						ElementMap.FindOrAdd(Element.GetElementKey()) = NewElement.GetElementKey();
						PastedKeys.Add(NewElement.GetElementKey());
						break;
					}
					case ERigElementType::Curve:
					{
						const FRigCurve& Element = *static_cast<FRigCurve*>(Elements[Index].Get());
						FRigCurve& NewElement = CurveContainer.Add(Element.Name, Element.Value);
						ElementMap.FindOrAdd(Element.GetElementKey()) = NewElement.GetElementKey();
						PastedKeys.Add(NewElement.GetElementKey());
						break;
					}
					default:
					{
						ensure(false);
						break;
					}
				}
			}
			break;
		}
		case ERigHierarchyImportMode::ReplaceLocalTransform:
		case ERigHierarchyImportMode::ReplaceGlobalTransform:
		{
			if (Selection.Num() == 0)
			{
				for (const TSharedPtr<FRigElement>& Element : Elements)
				{
					Selection.Add(Element->GetElementKey());
				}
			}
			else if (Selection.Num() != Data.Types.Num())
			{
				// todo: error message
				return PastedKeys;
			}

			for (int32 Index = 0; Index < Data.Types.Num(); Index++)
			{
				if(InImportMode == ERigHierarchyImportMode::ReplaceLocalTransform)
				{
					Data.LocalTransforms[Index].NormalizeRotation();
					SetLocalTransform(Selection[Index], Data.LocalTransforms[Index]);
				}
				else
				{
					Data.GlobalTransforms[Index].NormalizeRotation();
					SetGlobalTransform(Selection[Index], Data.GlobalTransforms[Index]);
				}
				PastedKeys.Add(Selection[Index]);
			}
			break;
		}
		default:
		{
			ensure(false);
			break;
		}
	}

	if (PastedKeys.Num() > 0 && InImportMode == ERigHierarchyImportMode::Append)
	{
		BoneHierarchy.Initialize();
		SpaceHierarchy.Initialize();
		ControlHierarchy.Initialize();
		CurveContainer.Initialize();
	}

	if (bSelectNewElements && PastedKeys.Num() > 0)
	{
		ClearSelection();
		for (const FRigElementKey& Key : PastedKeys)
		{
			Select(Key, true);
		}
	}

	return PastedKeys;
}

TArray<FRigElementKey> FRigHierarchyContainer::CurrentSelection() const
{
	TArray<FRigElementKey> Selection;
	for (const FRigBone& Element : BoneHierarchy)
	{
		if (BoneHierarchy.IsSelected(Element.Name))
		{
			Selection.Add(Element.GetElementKey());
		}
	}
	for (const FRigControl& Element : ControlHierarchy)
	{
		if (ControlHierarchy.IsSelected(Element.Name))
		{
			Selection.Add(Element.GetElementKey());
		}
	}
	for (const FRigSpace& Element : SpaceHierarchy)
	{
		if (SpaceHierarchy.IsSelected(Element.Name))
		{
			Selection.Add(Element.GetElementKey());
		}
	}
	for (const FRigCurve& Element : CurveContainer)
	{
		if (CurveContainer.IsSelected(Element.Name))
		{
			Selection.Add(Element.GetElementKey());
		}
	}

	Selection.Sort([&](const FRigElementKey& A, const FRigElementKey& B) { 
		int32 IndexA = GetIndex(A);
		int32 IndexB = GetIndex(B);
		return IsParentedTo(B.Type, IndexB, A.Type, IndexA);
	});

	return Selection;
}

TArray<FRigElementKey> FRigHierarchyContainer::GetAllItems(bool bSort) const
{
	TArray<FRigElementKey> Items;
	for (const FRigBone& Element : BoneHierarchy)
	{
		Items.Add(Element.GetElementKey());
	}
	for (const FRigControl& Element : ControlHierarchy)
	{
		Items.Add(Element.GetElementKey());
	}
	for (const FRigSpace& Element : SpaceHierarchy)
	{
		Items.Add(Element.GetElementKey());
	}
	for (const FRigCurve& Element : CurveContainer)
	{
		Items.Add(Element.GetElementKey());
	}
	
	if (bSort)
	{
		Items.Sort([&](const FRigElementKey& A, const FRigElementKey& B) {
			int32 IndexA = GetIndex(A);
			int32 IndexB = GetIndex(B);
			return IsParentedTo(B.Type, IndexB, A.Type, IndexA);
		});
	}

	return Items;
}

bool FRigHierarchyContainer::Select(const FRigElementKey& InKey, bool bSelect)
{
	switch(InKey.Type)
	{
		case ERigElementType::Bone:
		{
			return BoneHierarchy.Select(InKey.Name, bSelect);
		}
		case ERigElementType::Space:
		{
			return SpaceHierarchy.Select(InKey.Name, bSelect);
		}
		case ERigElementType::Control:
		{
			return ControlHierarchy.Select(InKey.Name, bSelect);
		}
		case ERigElementType::Curve:
		{
			return CurveContainer.Select(InKey.Name, bSelect);
		}
	}
	return false;
}

bool FRigHierarchyContainer::ClearSelection()
{
	ClearSelection(ERigElementType::Bone);
	ClearSelection(ERigElementType::Control);
	ClearSelection(ERigElementType::Space);
	ClearSelection(ERigElementType::Curve);
	return true;
}

bool FRigHierarchyContainer::ClearSelection(ERigElementType InElementType)
{
	switch(InElementType)
	{
		case ERigElementType::Bone:
		{
			return BoneHierarchy.ClearSelection();
		}
		case ERigElementType::Space:
		{
			return SpaceHierarchy.ClearSelection();
		}
		case ERigElementType::Control:
		{
			return ControlHierarchy.ClearSelection();
		}
		case ERigElementType::Curve:
		{
			return CurveContainer.ClearSelection();
		}
	}
	return false;
}

bool FRigHierarchyContainer::IsSelected(const FRigElementKey& InKey) const
{
	switch(InKey.Type)
	{
		case ERigElementType::Bone:
		{
			return BoneHierarchy.IsSelected(InKey.Name);
		}
		case ERigElementType::Space:
		{
			return SpaceHierarchy.IsSelected(InKey.Name);
		}
		case ERigElementType::Control:
		{
			return ControlHierarchy.IsSelected(InKey.Name);
		}
		case ERigElementType::Curve:
		{
			return CurveContainer.IsSelected(InKey.Name);
		}
	}
	return false;
}

#endif