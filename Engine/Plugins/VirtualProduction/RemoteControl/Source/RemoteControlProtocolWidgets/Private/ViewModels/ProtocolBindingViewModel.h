// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EditorUndoClient.h"
#include "IRemoteControlProtocol.h"
#include "RemoteControlProtocolBinding.h"
#include "UObject/WeakFieldPtr.h"

class FProtocolEntityViewModel;
class FProtocolRangeViewModel;
struct FRemoteControlProtocolBinding;
class URemoteControlPreset;

/** Represents a single protocol binding for an entity */
class REMOTECONTROLPROTOCOLWIDGETS_API FProtocolBindingViewModel : public TSharedFromThis<FProtocolBindingViewModel>, public FEditorUndoClient
{
public:
	/** Create a new ViewModel for the given Entity and Protocol Binding */
	static TSharedRef<FProtocolBindingViewModel> Create(const TSharedRef<FProtocolEntityViewModel>& InParentViewModel, const TSharedRef<FRemoteControlProtocolBinding>& InBinding);
	virtual ~FProtocolBindingViewModel();

	/** Add a new RangeMapping */
	FGuid AddRangeMapping();

	/** Remove a RangeMapping by Id */
	void RemoveRangeMapping(const FGuid& InId);

	/** Creates default range mappings depending on the input and output types */
	void AddDefaultRangeMappings();

	/** Get the Binding Id */
	const FGuid& GetId() const { return BindingId; }

	/** Get the bound FProperty. Should always be valid so long as the VM itself is valid. */
	TWeakFieldPtr<FProperty> GetProperty() const { return Property; }

	/** Get the underlying protocol binding instance */
	FRemoteControlProtocolBinding* GetBinding() const;

	/** Get the bound protocol */
	TSharedPtr<IRemoteControlProtocol> GetProtocol() const;

	/** Get the bound protocol name as Text */
	FText GetProtocolName() const { return FText::FromName(GetBinding()->GetProtocolName()); }

	/** Get the Range ViewModels */
	const TArray<TSharedPtr<FProtocolRangeViewModel>>& GetRanges() const { return Ranges; }

	/** Get the underlying RangeMapping */
	FRemoteControlProtocolMapping* GetRangesMapping(const FGuid& InRangeId) const;

	/** Removes this Protocol Binding */
	void Remove() const;

	bool IsValid() const { return Preset.IsValid() && PropertyId.IsValid() && BindingId.IsValid(); }

public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnRangeMappingAdded, TSharedRef<FProtocolRangeViewModel> /* InRangeViewModel */);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnRangeMappingRemoved, FGuid /* InRangeId */);
	DECLARE_MULTICAST_DELEGATE(FOnChanged);

	FOnRangeMappingAdded& OnRangeMappingAdded() { return OnRangeMappingAddedDelegate; }
	FOnRangeMappingRemoved& OnRangeMappingRemoved() { return OnRangeMappingRemovedDelegate; }

	/** Something has changed within the ViewModel */
	FOnChanged& OnChanged() { return OnChangedDelegate; }

private:
	template <typename FProtocolBindingViewModel>
    friend class SharedPointerInternals::TIntrusiveReferenceController;
	
	FProtocolBindingViewModel() = default;
	FProtocolBindingViewModel(const TSharedRef<FProtocolEntityViewModel>& InParentViewModel, const TSharedRef<FRemoteControlProtocolBinding>& InBinding);

	void Initialize();

	TSharedPtr<FProtocolRangeViewModel>& AddRangeMappingInternal();

	//~ Begin FEditorUndoClient Interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override { PostUndo(bSuccess); }
	// End of FEditorUndoClient

private:
	friend class FProtocolRangeViewModel;

	/** Owning Preset */
	TWeakObjectPtr<URemoteControlPreset> Preset;

	/** Owning Entity (Property within a Preset) */
	TWeakPtr<FProtocolEntityViewModel> ParentViewModel;

	/** Bound property */
	TWeakFieldPtr<FProperty> Property;

	/** Unique Id of the bound Property */
	FGuid PropertyId;

	/** Unique Id of this protocol binding */
	FGuid BindingId;

	/** Range ViewModels for this protocol binding */
	TArray<TSharedPtr<FProtocolRangeViewModel>> Ranges;
	
	FOnRangeMappingAdded OnRangeMappingAddedDelegate;
	FOnRangeMappingRemoved OnRangeMappingRemovedDelegate;
	FOnChanged OnChangedDelegate;
};
