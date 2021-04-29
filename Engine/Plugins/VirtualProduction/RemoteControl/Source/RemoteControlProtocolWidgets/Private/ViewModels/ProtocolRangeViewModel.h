// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ProtocolBindingViewModel.h"
#include "RemoteControlPreset.h"
#include "ScopedTransaction.h"

class FProtocolBindingViewModel;
class IPropertyHandle;

#define LOCTEXT_NAMESPACE "ProtocolBindingViewModel"

/** Represents a single range mapping for a given protocol binding  */
class REMOTECONTROLPROTOCOLWIDGETS_API FProtocolRangeViewModel : public TSharedFromThis<FProtocolRangeViewModel>, public FEditorUndoClient
{
public:
	/** Create a new ViewModel for the given Protocol Binding and Range Id */
	static TSharedRef<FProtocolRangeViewModel> Create(const TSharedRef<FProtocolBindingViewModel>& InParentViewModel, const FGuid& InRangeId);
	virtual ~FProtocolRangeViewModel();

	/** Copy the input to the given PropertyHandle */
	void CopyInputValue(const TSharedPtr<IPropertyHandle>& InDstHandle) const;

	/** Set input from raw data */
	void SetInputData(const TSharedPtr<IPropertyHandle>& InSrcHandle) const;

	/** Set input */
	template <typename ValueType>
    void SetInputValue(ValueType InValue, bool bWithTransaction = false)
	{
		check(IsValid());

		if(bWithTransaction)
		{
			FScopedTransaction Transaction(LOCTEXT("SetInputValue", "Set Input Value"));
			Preset->Modify();	
		}
		
		GetRangesData()->SetRangeValue(InValue);
	}

	/** Copy the output to the given PropertyHandle */
	void CopyOutputValue(const TSharedPtr<IPropertyHandle>& InDstHandle) const;

	/** Set underlying output from raw data */
	void SetOutputData(const TSharedPtr<IPropertyHandle>& InSrcHandle) const;

	/** Set output */
	template <typename ValueType>
	void SetOutputValue(ValueType InValue, bool bWithTransaction = false)
	{
		check(IsValid());

		if(bWithTransaction)
		{
			FScopedTransaction Transaction(LOCTEXT("SetOutputValue", "Set Output Value"));
        	Preset->Modify();	
		}

		GetRangesData()->SetRangeValueAsPrimitive(InValue); 
	}

	/** Get the Range Id */
	const FGuid& GetId() const { return RangeId; }

	/** Get the owning ProtocolBinding Id */
	const FGuid& GetBindingId() const { return GetBinding()->GetId(); }

	/** Get the owning Preset */
	const URemoteControlPreset* GetPreset() const { return Preset.Get(); }

	FProperty* GetInputProperty() const;

	/** Get the bound FProperty */
	TWeakFieldPtr<FProperty> GetProperty() const { return ParentViewModel.Pin()->GetProperty(); }

	/** Get the input TypeName */
	FName GetInputTypeName() const;

	/** Removes this Range Mapping */
	void Remove() const;

	bool IsValid() const { return ParentViewModel.IsValid() && Preset.IsValid() && RangeId.IsValid(); }

public:
	DECLARE_MULTICAST_DELEGATE(FOnChanged);
	
	/** Something has changed within the ViewModel */
	FOnChanged& OnChanged() { return OnChangedDelegate; }

protected:
	template <typename FProtocolRangeViewModel>
	friend class SharedPointerInternals::TIntrusiveReferenceController;

	FProtocolRangeViewModel() = default;
	FProtocolRangeViewModel(const TSharedRef<FProtocolBindingViewModel>& InParentViewModel, const FGuid& InRangeId);
	
	void Initialize();;

	FRemoteControlProtocolMapping* GetRangesData() const { return ParentViewModel.Pin()->GetRangesMapping(RangeId); }
	FRemoteControlProtocolBinding* GetBinding() const { return ParentViewModel.Pin()->GetBinding(); }

	//~ Begin FEditorUndoClient Interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override { PostUndo(bSuccess); }
	// End of FEditorUndoClient

private:
	/** Owning Preset */
	TWeakObjectPtr<URemoteControlPreset> Preset;
	
	/** Owning ProtocolBinding */
	TWeakPtr<FProtocolBindingViewModel> ParentViewModel;

	/** Unique Id of this Range */
	FGuid RangeId;

	FOnChanged OnChangedDelegate;
};

#undef LOCTEXT_NAMESPACE
