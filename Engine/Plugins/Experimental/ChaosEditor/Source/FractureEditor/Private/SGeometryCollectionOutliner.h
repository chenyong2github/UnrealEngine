// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Views/STreeView.h"

#include "Misc/Attribute.h"

#include "Containers/Array.h"
#include "Templates/SharedPointer.h"
#include "UObject/ObjectKey.h"

#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionComponent.h"

#include "SGeometryCollectionOutliner.generated.h"

class FGeometryCollection;
class FGeometryCollectionTreeItem;
class FGeometryCollectionTreeItemBone;

typedef TArray<TSharedPtr<FGeometryCollectionTreeItem>> FGeometryCollectionTreeItemList;
typedef TSharedPtr<FGeometryCollectionTreeItem> FGeometryCollectionTreeItemPtr;


UENUM(BlueprintType)
enum class EOutlinerItemNameEnum : uint8
{
	BoneName = 0					UMETA(DisplayName = "Bone Name"),
	BoneIndex = 1					UMETA(DisplayName = "Bone Index"),
};

/** Settings for Outliner configuration. **/
UCLASS()
class UOutlinerSettings : public UObject
{

	GENERATED_BODY()
public:
	UOutlinerSettings(const FObjectInitializer& ObjInit);

	/** What is displayed in Outliner text */
	UPROPERTY(EditAnywhere, Category = OutlinerSettings, meta = (DisplayName = "Item Text"))
	EOutlinerItemNameEnum ItemText;
};


class FGeometryCollectionTreeItem : public TSharedFromThis<FGeometryCollectionTreeItem>
{
public:
	virtual ~FGeometryCollectionTreeItem() {}
	virtual TSharedRef<ITableRow> MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable) = 0;
	virtual void GetChildren(FGeometryCollectionTreeItemList& OutChildren) = 0;
	virtual UGeometryCollectionComponent* GetComponent() const = 0;

	virtual int32 GetBoneIndex() const { return INDEX_NONE; }
};

class FGeometryCollectionTreeItemComponent : public FGeometryCollectionTreeItem
{
public:
	FGeometryCollectionTreeItemComponent(UGeometryCollectionComponent* InComponent)
		: Component(InComponent)
	{
		RegenerateChildren();
	}

	/** FGeometryCollectionTreeItem interface */
	virtual TSharedRef<ITableRow> MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable);
	virtual void GetChildren(FGeometryCollectionTreeItemList& OutChildren) override;
	virtual UGeometryCollectionComponent* GetComponent() const override { return Component.Get(); }


	FGeometryCollectionTreeItemPtr GetItemFromBoneIndex(int32 BoneIndex) const;
	 
	void GetChildrenForBone(FGeometryCollectionTreeItemBone& BoneItem, FGeometryCollectionTreeItemList& OutChildren) const;
	FText GetDisplayNameForBone(const FGuid& Guid) const;

	void ExpandAll(TSharedPtr<STreeView<FGeometryCollectionTreeItemPtr>> TreeView);
	void RegenerateChildren();

	void SetHistogramSelection(TArray<int32>& SelectedBones);

private:
	bool FilterBoneIndex(int32 BoneIndex) const;

private:
	TWeakObjectPtr<UGeometryCollectionComponent> Component;

	/** The direct children under this component */
	TArray<FGeometryCollectionTreeItemPtr> MyChildren;

	TMap<FGuid, FGeometryCollectionTreeItemPtr> NodesMap;
	TMap<FGuid, int32> GuidIndexMap;
	FGuid RootGuid;
	int32 RootIndex;

	TArray<int32> HistogramSelection;
};

class FGeometryCollectionTreeItemBone : public FGeometryCollectionTreeItem
{

public:
	FGeometryCollectionTreeItemBone(const FGuid NewGuid, const int32 InBoneIndex, const FGeometryCollectionTreeItemComponent& InParentComponentItem)
		: Guid(NewGuid)
		, BoneIndex(InBoneIndex)
		, ParentComponentItem(&InParentComponentItem)
	{}

	/** FGeometryCollectionTreeItem interface */
	TSharedRef<ITableRow> MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable);//, const TAttribute<FText>& InFilterText) = 0;
	virtual void GetChildren(FGeometryCollectionTreeItemList& OutChildren) override;
	virtual int32 GetBoneIndex() const override { return BoneIndex; }
	virtual UGeometryCollectionComponent* GetComponent() const { return ParentComponentItem->GetComponent(); }
	const FGuid& GetGuid() const { return Guid; }

	// TArray<TSharedPtr<FGeometryCollectionTreeItem>>& GetChildren();
	// TArray<TSharedPtr<FGeometryCollectionTreeItem>> Children;
	// TWeakPtr<SGeometryCollectionOutliner> GeomOutliner;
	
private:
	const FGuid Guid;
	const int32 BoneIndex;
	const FGeometryCollectionTreeItemComponent* ParentComponentItem;
};

class SGeometryCollectionOutliner: public SCompoundWidget
{
	DECLARE_DELEGATE_TwoParams(FOnBoneSelectionChanged, UGeometryCollectionComponent*, TArray<int32>&);

	SLATE_BEGIN_ARGS( SGeometryCollectionOutliner ) 
	{}

		SLATE_EVENT(FOnBoneSelectionChanged, OnBoneSelectionChanged)

	SLATE_END_ARGS() 

public:
	void Construct(const FArguments& InArgs);

	void RegenerateItems();

	TSharedRef<ITableRow> MakeTreeRowWidget(FGeometryCollectionTreeItemPtr InInfo, const TSharedRef<STableViewBase>& OwnerTable);
	void OnGetChildren(TSharedPtr<FGeometryCollectionTreeItem> InInfo, TArray< TSharedPtr<FGeometryCollectionTreeItem> >& OutChildren);

	void UpdateGeometryCollection();
	void SetComponents(const TArray<UGeometryCollectionComponent*>& InNewComponents);
	void SetBoneSelection(UGeometryCollectionComponent* RootComponent, const TArray<int32>& InSelection, bool bClearCurrentSelection);

	void ExpandAll();
	void ExpandRecursive(TSharedPtr<FGeometryCollectionTreeItem> TreeItem, bool bInExpansionState) const;

	// Set the histogram filter on the component matching RootComponent.
	void SetHistogramSelection(UGeometryCollectionComponent* RootComponent, TArray<int32>& SelectedBones);

private:
	// void GetItemParentsForGuid(int32 Guid);
	//void GetItemChildrenForGuid(int32 Guid);
	void OnSelectionChanged(FGeometryCollectionTreeItemPtr Item, ESelectInfo::Type SelectInfo);
private:
	TSharedPtr<STreeView<FGeometryCollectionTreeItemPtr>> TreeView;
	TArray<TSharedPtr<FGeometryCollectionTreeItemComponent>> RootNodes;
	FOnBoneSelectionChanged BoneSelectionChangedDelegate;
	bool bPerformingSelection;
};