// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "SDropTarget.h"

/**
 * A widget that displays a hover cue and handles dropping assets of allowed types onto this widget
 */
class EDITORWIDGETS_API SAssetDropTarget : public SDropTarget
{
public:
	/** Called when a valid asset is dropped */
	DECLARE_DELEGATE_OneParam( FOnAssetDropped, UObject* );

	/** Called when we need to check if an asset type is valid for dropping */
	DECLARE_DELEGATE_RetVal_OneParam( bool, FIsAssetAcceptableForDrop, const UObject* );

	/** Called when we need to check if an asset type is valid for dropping and also will have a reason if it is not */
	DECLARE_DELEGATE_RetVal_TwoParams( bool, FIsAssetAcceptableForDropWithReason, const UObject*, FText& );

	SLATE_BEGIN_ARGS(SAssetDropTarget)
	{ }
		/* Content to display for the in the drop target */
		SLATE_DEFAULT_SLOT( FArguments, Content )
		/** Called when a valid asset is dropped */
		SLATE_EVENT( FOnAssetDropped, OnAssetDropped )
		/** Called to check if an asset is acceptible for dropping */
		SLATE_EVENT( FIsAssetAcceptableForDrop, OnIsAssetAcceptableForDrop )
		/** Called to check if an asset is acceptible for dropping if you also plan on returning a reason text */
		SLATE_EVENT( FIsAssetAcceptableForDropWithReason, OnIsAssetAcceptableForDropWithReason )
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs );

protected:
	FReply OnDropped(TSharedPtr<FDragDropOperation> DragDropOperation);
	virtual bool OnAllowDrop(TSharedPtr<FDragDropOperation> DragDropOperation) const override;
	virtual bool OnIsRecognized(TSharedPtr<FDragDropOperation> DragDropOperation) const override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;

private:
	UObject* GetDroppedObject(TSharedPtr<FDragDropOperation> DragDropOperation, bool& bOutRecognizedEvent) const;

private:
	/** Delegate to call when an asset is dropped */
	FOnAssetDropped OnAssetDropped;
	/** Delegate to call to check validity of the asset */
	FIsAssetAcceptableForDrop OnIsAssetAcceptableForDrop;
	/** Delegate to call to check validity of the asset if you will also provide a reason when returning false */
	FIsAssetAcceptableForDropWithReason OnIsAssetAcceptableForDropWithReason;
};
