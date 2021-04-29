// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProtocolRangeViewModel.h"

#include "Editor.h"
#include "IRemoteControlModule.h"
#include "PropertyHandle.h"

#define LOCTEXT_NAMESPACE "ProtocolBindingViewModel"

TSharedRef<FProtocolRangeViewModel> FProtocolRangeViewModel::Create(const TSharedRef<FProtocolBindingViewModel>& InParentViewModel, const FGuid& InRangeId)
{
	TSharedRef<FProtocolRangeViewModel> ViewModel = MakeShared<FProtocolRangeViewModel>(InParentViewModel, InRangeId);
	ViewModel->Initialize();

	return ViewModel;
}

FProtocolRangeViewModel::FProtocolRangeViewModel(const TSharedRef<FProtocolBindingViewModel>& InParentViewModel, const FGuid& InRangeId)
	: Preset(InParentViewModel->Preset),
	ParentViewModel(InParentViewModel),
	RangeId(InRangeId)
{
	GEditor->RegisterForUndo(this);
}

FProtocolRangeViewModel::~FProtocolRangeViewModel()
{
	GEditor->UnregisterForUndo(this);
}

void FProtocolRangeViewModel::Initialize()
{
	// Nothing at the moment
}

void FProtocolRangeViewModel::CopyInputValue(const TSharedPtr<IPropertyHandle>& InDstHandle) const
{
	check(IsValid());
	
	TArray<void*> DstPropertyData;
	InDstHandle->AccessRawData(DstPropertyData);
	check(DstPropertyData.IsValidIndex(0));

	GetRangesData()->CopyRawRangeData(InDstHandle->GetProperty(), DstPropertyData[0]);
}

void FProtocolRangeViewModel::SetInputData(const TSharedPtr<IPropertyHandle>& InSrcHandle) const
{
	check(IsValid());

	FScopedTransaction Transaction(LOCTEXT("SetInputData", "Set Input Data"));
	Preset->Modify();

	TArray<void*> SrcPropertyData;
	InSrcHandle->AccessRawData(SrcPropertyData);
	check(SrcPropertyData.IsValidIndex(0));

	GetRangesData()->SetRawRangeData(Preset.Get(), InSrcHandle->GetProperty(), SrcPropertyData[0]);
}

void FProtocolRangeViewModel::CopyOutputValue(const TSharedPtr<IPropertyHandle>& InDstHandle) const
{
	check(IsValid());
	
	TArray<void*> DstPropertyData;
	InDstHandle->AccessRawData(DstPropertyData);
	check(DstPropertyData.IsValidIndex(0));

	GetRangesData()->CopyRawMappingData(InDstHandle->GetProperty(), DstPropertyData[0]);
}

void FProtocolRangeViewModel::SetOutputData(const TSharedPtr<IPropertyHandle>& InSrcHandle) const
{
	check(IsValid());

	FScopedTransaction Transaction(LOCTEXT("SetOutputData", "Set Output Data"));
	Preset->Modify();

	TArray<void*> SrcPropertyData;
	InSrcHandle->AccessRawData(SrcPropertyData);
	check(SrcPropertyData.IsValidIndex(0));

	GetRangesData()->SetRawMappingData(Preset.Get(), InSrcHandle->GetProperty(), SrcPropertyData[0]);
}

FProperty* FProtocolRangeViewModel::GetInputProperty() const
{
	return ParentViewModel.Pin()->GetProtocol()->GetRangeInputTemplateProperty();
}

FName FProtocolRangeViewModel::GetInputTypeName() const
{
	check(IsValid());

	return GetBinding()->GetRemoteControlProtocolEntityPtr()->Get()->GetRangePropertyName();
}

void FProtocolRangeViewModel::Remove() const
{
	ParentViewModel.Pin()->RemoveRangeMapping(GetId());
}

void FProtocolRangeViewModel::PostUndo(bool bSuccess)
{
	OnChangedDelegate.Broadcast();
}

#undef LOCTEXT_NAMESPACE
