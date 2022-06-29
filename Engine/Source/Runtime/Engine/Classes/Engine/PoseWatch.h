// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "PoseWatch.generated.h"


struct FCompactHeapPose;
class UAnimBlueprint;
class UBlendProfile;

struct FAnimNodePoseWatch
{
	// Object (anim instance) that this pose came from
	TWeakObjectPtr<const UObject>	Object;
	TWeakObjectPtr<UPoseWatch>		PoseWatch;
	TSharedPtr<FCompactHeapPose>	PoseInfo;
	int32							NodeID;
};

namespace PoseWatchUtil
{
	/** Gets all pose watches that are parented to Folder, if Folder is nullptr then gets orphans */
	TSet<UPoseWatch*> GetChildrenPoseWatchOf(const UPoseWatchFolder* Folder, const UAnimBlueprint* AnimBlueprint);

	/** Gets all pose watches folders that are parented to Folder, if Folder is nullptr then gets orphans */
	TSet<UPoseWatchFolder*> GetChildrenPoseWatchFoldersOf(const UPoseWatchFolder* Folder, const UAnimBlueprint* AnimBlueprint);

	/** Returns a UPoseWatch/UPoseWatchFolder from PoseWatchCollection that is inside InFolder has has label Label, if one exists */
	template <typename T>
	T* FindInFolderInCollection(const FName& Label, UPoseWatchFolder* InFolder, const TArray<TObjectPtr<T>>& PoseWatchCollection);

	/** Finds unique name for a pose watch or folder inside of InParent */
	template <typename T>
	FText FindUniqueNameInFolder(UPoseWatchFolder* InParent, const T* Item, const TArray<TObjectPtr<T>>& Collection);

	/** Returns a new random color */
	FColor ChoosePoseWatchColor();
}

UCLASS()
class ENGINE_API UPoseWatchFolder
	: public UObject
{
	GENERATED_UCLASS_BODY()

public:
#if WITH_EDITOR
	/** Returns the slash delimited path of this folder (e.g. MyFolder/MyNestedFolder/MyPoseWatch)*/
	const FText GetPath() const;

	/** The default name given to all new folders */
	FText GetDefaultLabel() const;

	/** Returns the display label assigned to this pose watch folder */
	FText GetLabel() const;

	/** Returns the visibility of this pose watch folder */
	bool GetIsVisible() const;

	/** Returns the parent folder this folder belongs to, if any */
	UPoseWatchFolder* GetParent() const;
	
	/**  Attempts to set this folder's parent, returns false if unsuccessful (if moving causes a name clash)
	 *	 Using bForce may change the folder's label to ensure no name clashes
	 */
	bool SetParent(UPoseWatchFolder* Parent, bool bForce = false);

	/** Alias of SetParent */
	void MoveTo(UPoseWatchFolder* InFolder);

	/** Attempts to set the label, returns false if unsuccessful (if there's a name clash with another  folder in the current directory) */
	bool SetLabel(const FText& InLabel);

	/** Sets the visibility of this folder, must contain at least one post watch descendant to become visible */
	void SetIsVisible(bool bInIsVisible, bool bUpdateChildren=true);

	/** Called before the pose watch folder is deleted to cleanup it's children and update it's parent */
	void OnRemoved();

	/** Returns true if InFolder is the parent of this */
	bool IsIn(const UPoseWatchFolder* InFolder) const;

	/** Returns true if this is a descendant of InFolder */
	bool IsDescendantOf(const UPoseWatchFolder* InFolder) const;

	/** Returns true if this folder is inside another folder */
	bool IsAssignedFolder() const;

	/** Returns true if there would be no name clashes when assigning the label InLabel */
	bool ValidateLabelRename(const FText& InLabel, FText& OutErrorMessage);

	/** Returns true if InLabel is a unique folder label among the children of InFolder,  excluding this */
	bool IsFolderLabelUniqueInFolder(const FText& InLabel, UPoseWatchFolder* InFolder) const;

	/** Returns true if at least one UPoseWatch/UPoseWatchFolder has this as it's parent */
	bool HasChildren() const;

	/** Takes GetDefaultLabel() and generates unique labels to avoid name clashes (e.g. NewFolder1, NewFolder2, ...) */
	void SetUniqueDefaultLabel();

	/** Called when a child is removed/added to a folder */
	void UpdateVisibility();

	/** Returns the anim blueprint this pose watch folder is stored inside */
	UAnimBlueprint* GetAnimBlueprint() const;

	void SetIsExpanded(bool bInIsExpanded);

	bool GetIsExpanded() const;

private:
	/** Returns a unique name for a new folder placed within InParent */
	FText FindUniqueNameInFolder(UPoseWatchFolder* InParent) const;

	/** Returns true if there is at least one direct pose watch child */
	bool HasPoseWatchChildren() const;

	/** Returns true if there is at least one pose watch descendant (nested folders) */
	bool HasPoseWatchDescendents() const;
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
protected:
	UPROPERTY()
	FText Label;

	UPROPERTY()
	TWeakObjectPtr<UPoseWatchFolder> Parent;

	UPROPERTY()
	bool bIsVisible = false;

	UPROPERTY(Transient)
	bool bIsExpanded = true;
#endif // WITH_EDITORONLY_DATA
};


UCLASS()
class ENGINE_API UPoseWatch
	: public UObject
{
	GENERATED_UCLASS_BODY()

public:
#if WITH_EDITOR
	/** Returns the slash delimited path of this pose watch */
	const FText GetPath() const;

	/** The default name given to all new pose watches */
	FText GetDefaultLabel() const;

	/** Returns the display label assigned to this pose watch */
	FText GetLabel() const;

	/** Returns the visibility of this pose watch */
	bool GetIsVisible() const;

	/** Returns true if the pose watch is connected to the output pose */
	bool GetIsEnabled() const;

	/** Returns the color to display the pose watch using */
	FColor GetColor() const;

	/** Returns true if this pose watch should be deleted after the user has deselected its assigned node (Editor preference) */
	bool GetShouldDeleteOnDeselect() const;

	/** Returns the parent folder this folder belongs to, if any */
	UPoseWatchFolder* GetParent() const;

	/**  Attempts to set this pose watch's parent, returns false if unsuccessful (if moving causes a name clash)
	 *	 Using bForce may change the pose watch's label to ensure no name clashes
	 */
	bool SetParent(UPoseWatchFolder* InParent, bool bForce=false);

	/** If set, denotes the pose watch is able to be drawn to the viewport */
	void SetIsEnabled(bool bInIsEnabled);

	/** Alias of SetParent */
	void MoveTo(UPoseWatchFolder* InFolder);

	/** Attempts to set the label, returns false if unsuccessful (if there's a name clash with another pose watch in the current directory) */
	bool SetLabel(const FText& InLabel);

	/** Sets whether or not to render this pose watch to the viewport */
	void SetIsVisible(bool bInIsVisible);

	/** Sets the display color of this pose watch in the UI and viewport */
	void SetColor(const FColor& InColor);

	/** Sets whether this pose watch should delete after deselecting it's assigned node (Editor preference) */
	void SetShouldDeleteOnDeselect(const bool bInDeleteOnDeselection);

	/** Called when a pose watch is deleted to update it's parent */
	void OnRemoved();

	/** Toggle's the pose watch's visibility */
	void ToggleIsVisible();

	/** Returns true if this pose watch is inside InFolder */
	bool IsIn(const UPoseWatchFolder* InFolder) const;

	/** Returns true if this pose watch is inside some pose watch folder */
	bool IsAssignedFolder() const;

	/** Returns true if there would be no name clashes when assigning the label InLabel */
	bool ValidateLabelRename(const FText& InLabel, FText& OutErrorMessage);

	/** Returns true if InLabel is a unique pose watch label among the children of InFolder, excluding this */
	bool IsPoseWatchLabelUniqueInFolder(const FText& InLabel, UPoseWatchFolder* InFolder) const;

	/** Takes GetDefaultLabel() and generates unique labels to avoid name clashes (e.g. PoseWatch1, PoseWatch2, ...) */
	void SetUniqueDefaultLabel();

	/** Returns the anim blueprint this pose watch is stored inside */
	UAnimBlueprint* GetAnimBlueprint() const;

private:
	/** Returns a unique name for a new pose watch  placed within InParent */
	FText FindUniqueNameInFolder(UPoseWatchFolder* InParent) const;
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
public:
	UPROPERTY()
	TWeakObjectPtr<class UEdGraphNode> Node;

	/** Optionally select a Blend Mask to control which bones on the skeleton are rendered. Any non-zero entries are rendered. */
	UPROPERTY(EditAnywhere, editfixedsize, Category = Default, meta = (UseAsBlendMask = true))
	TObjectPtr<UBlendProfile> ViewportMask;

	/** Invert which bones are rendered when using a viewport mask */
	UPROPERTY(EditAnywhere, Category = Default, meta = (EditCondition = "ViewportMask != nullptr"))
	bool bInvertViewportMask;

	/** The threshold which each bone's blend scale much surpass to be rendered using the viewport mask */
	UPROPERTY(EditAnywhere, Category = Default, meta = (ClampMin = 0.f, ClampMax = 1.f, EditCondition="ViewportMask != nullptr"))
	float BlendScaleThreshold;

	/** Offset the rendering of the bones in the viewport. */
	UPROPERTY(EditAnywhere, Category = Default)
	FVector3d ViewportOffset;

protected:
	UPROPERTY()
	bool bDeleteOnDeselection = false;

	// If true will draw the pose to the viewport
	UPROPERTY()
	bool bIsVisible = true;

	// If true, the pose is able to be drawn to the viewport
	UPROPERTY(Transient)
	bool bIsEnabled = false;

	UPROPERTY()
	FColor Color;

	UPROPERTY()
	FText Label;

	UPROPERTY()
	TWeakObjectPtr<UPoseWatchFolder> Parent;
#endif // WITH_EDITORONLY_DATA
};