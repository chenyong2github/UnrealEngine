// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PoseWatchManagerFwd.h"
#include "Layout/Visibility.h"
#include "Input/DragAndDrop.h"
#include "DragAndDrop/CompositeDragDropOp.h"
#include "IPoseWatchManagerTreeItem.h"
#include "Engine/PoseWatch.h"

/** Enum to describe the compatibility of a drag drop operation */
enum class EPoseWatchManagerDropCompatibility : uint8
{
	Compatible,
	Incompatible
};

struct FPoseWatchManagerDragDropPayload
{
	FPoseWatchManagerDragDropPayload(const FDragDropOperation& InOperation = FDragDropOperation())
		: SourceOperation(InOperation)
	{}

	template<typename TreeType>
	FPoseWatchManagerDragDropPayload(const TreeType& InDraggedItem, const FDragDropOperation& InOperation = FDragDropOperation())
		: SourceOperation(InOperation)
	{
		DraggedItem = InDraggedItem;
	}

	TWeakPtr<IPoseWatchManagerTreeItem> DraggedItem;

	/** The source FDragDropOperation */
	const FDragDropOperation& SourceOperation;
};

/** Struct used for validation of a drag/drop operation in the pose watch manager */
struct FPoseWatchManagerDragValidationInfo
{
	/** The tooltip type to display on the operation */
	EPoseWatchManagerDropCompatibility CompatibilityType;

	/** The tooltip text to display on the operation */
	FText ValidationText;

	/** Construct this validation information out of a tooltip type and some text */
	FPoseWatchManagerDragValidationInfo(const EPoseWatchManagerDropCompatibility InCompatibilityType, const FText InValidationText)
		: CompatibilityType(InCompatibilityType)
		, ValidationText(InValidationText)
	{}

	/** Return a generic invalid result */
	static FPoseWatchManagerDragValidationInfo Invalid()
	{
		return FPoseWatchManagerDragValidationInfo(EPoseWatchManagerDropCompatibility::Incompatible, FText());
	}

	/** @return true if this operation is valid, false otherwise */
	bool IsValid() const
	{
		return CompatibilityType == EPoseWatchManagerDropCompatibility::Compatible;
	}
};

/** A drag/drop operation that was started from the pose watch manager */
struct PERSONA_API FPoseWatchManagerDragDropOp : public FCompositeDragDropOp
{
	DRAG_DROP_OPERATOR_TYPE(FPoseWatchManagerDragDropOp, FCompositeDragDropOp);

	FPoseWatchManagerDragDropOp();

	using FDragDropOperation::Construct;

	void ResetTooltip()
	{
		OverrideText = FText();
		OverrideIcon = nullptr;
	}

	void SetTooltip(FText InOverrideText, const FSlateBrush* InOverrideIcon)
	{
		OverrideText = InOverrideText;
		OverrideIcon = InOverrideIcon;
	}

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;

private:

	EVisibility GetOverrideVisibility() const;
	EVisibility GetDefaultVisibility() const;

	FText OverrideText;
	FText GetOverrideText() const { return OverrideText; }

	const FSlateBrush* OverrideIcon;
	const FSlateBrush* GetOverrideIcon() const { return OverrideIcon; }
};


class FPoseWatchDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FPoseWatchDragDropOp, FDecoratedDragDropOp)

	/** Pose Watch that we are dragging */
	TWeakObjectPtr<UPoseWatch> PoseWatch;

	void Init(const TWeakObjectPtr<UPoseWatch>& InPoseWatch)
	{
		PoseWatch = InPoseWatch;

		// Set text and icon
		UClass* CommonSelClass = NULL;
		CurrentIconBrush = FEditorStyle::Get().GetBrush(TEXT("ClassIcon.PoseAsset"));
		CurrentHoverText = PoseWatch->GetLabel();

		SetupDefaults();
	}
};

class FPoseWatchFolderDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FPoseWatchFolderDragDropOp, FDecoratedDragDropOp)

	/** Pose Watch Folder that we are dragging */
	TWeakObjectPtr<UPoseWatchFolder> PoseWatchFolder;

	void Init(const TWeakObjectPtr<UPoseWatchFolder>& InPoseWatchFolder)
	{
		PoseWatchFolder = InPoseWatchFolder;

		CurrentIconBrush = FEditorStyle::Get().GetBrush(TEXT("SceneOutliner.FolderClosed"));
		CurrentHoverText = PoseWatchFolder->GetLabel();


		SetupDefaults();
	}
};
