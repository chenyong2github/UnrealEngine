// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "UObject/WeakFieldPtr.h"

class FProtocolBindingViewModel;
class URemoteControlPreset;

/** Contains all bindings for a given Entity (ie. Property) */
class REMOTECONTROLPROTOCOLWIDGETS_API FProtocolEntityViewModel : public TSharedFromThis<FProtocolEntityViewModel>, public FEditorUndoClient
{
public:
	/** Create a new ViewModel for the given Preset and EntityId */
	static TSharedRef<FProtocolEntityViewModel> Create(URemoteControlPreset* InPreset, const FGuid& InEntityId);
	virtual ~FProtocolEntityViewModel();

	/** Check if the bound entity type is supported by Protocol Binding */
	bool CanAddBinding(const FName& InProtocolName, FText& OutMessage) const;

	/** Add a new Protocol Binding */
	TSharedPtr<FProtocolBindingViewModel> AddBinding(const FName& InProtocolName);

	/** Remove a Protocol Binding by Id */
	void RemoveBinding(const FGuid& InBindingId);

	/** Get the Entity Id */
	const FGuid& GetId() const { return PropertyId; }

	/** Get the bound FProperty */
	TWeakFieldPtr<FProperty> GetProperty() const { return Property; }

	/** Get the Protocol Binding ViewModels */
	const TArray<TSharedPtr<FProtocolBindingViewModel>>& GetBindings() const { return Bindings; }
	
	bool IsValid() const { return Preset.IsValid() && PropertyId.IsValid(); }

public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnBindingAdded, TSharedRef<FProtocolBindingViewModel> /* InBindingViewModel */);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnBindingRemoved, FGuid /* InBindingId */);
	DECLARE_MULTICAST_DELEGATE(FOnChanged);

	FOnBindingAdded& OnBindingAdded() { return OnBindingAddedDelegate; }
	FOnBindingRemoved& OnBindingRemoved() { return OnBindingRemovedDelegate; }

	/** Something has changed within the ViewModel */
	FOnChanged& OnChanged() { return OnChangedDelegate; }

private:
	template <typename FProtocolEntityViewModel>
    friend class SharedPointerInternals::TIntrusiveReferenceController;
	
	FProtocolEntityViewModel() = default;
	FProtocolEntityViewModel(URemoteControlPreset* InPreset, const FGuid& InEntityId);

	void Initialize();

	//~ Begin FEditorUndoClient Interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override { PostUndo(bSuccess); }
	// End of FEditorUndoClient

private:
	friend class FProtocolBindingViewModel;

	/** Owning Preset */
	TWeakObjectPtr<URemoteControlPreset> Preset;

	/** Bound property */
	TWeakFieldPtr<FProperty> Property;

	/** Unique Id of the bound Property */
	FGuid PropertyId;

	/** Protocol Binding ViewModels for this Entity */
	TArray<TSharedPtr<FProtocolBindingViewModel>> Bindings;

	FOnBindingAdded OnBindingAddedDelegate;
	FOnBindingRemoved OnBindingRemovedDelegate;
	FOnChanged OnChangedDelegate;
};
