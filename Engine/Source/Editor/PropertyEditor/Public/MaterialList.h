// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomNodeBuilder.h"
#include "DetailWidgetRow.h"
#include "Materials/MaterialInterface.h"

class FMaterialItemView;
class FMaterialListBuilder;
class IDetailChildrenBuilder;
class IDetailLayoutBuilder;
class IMaterialListBuilder;

/**
 * Delegate called when we need to get new materials for the list
 */
DECLARE_DELEGATE_OneParam(FOnGetMaterials, IMaterialListBuilder&);

/**
 * Delegate called when a user changes the material
 */
DECLARE_DELEGATE_FourParams( FOnMaterialChanged, UMaterialInterface*, UMaterialInterface*, int32, bool );

DECLARE_DELEGATE_RetVal_TwoParams( TSharedRef<SWidget>, FOnGenerateWidgetsForMaterial, UMaterialInterface*, int32 );

DECLARE_DELEGATE_TwoParams( FOnResetMaterialToDefaultClicked, UMaterialInterface*, int32 );

DECLARE_DELEGATE_RetVal(bool, FOnMaterialListDirty);

DECLARE_DELEGATE_RetVal(bool, FOnCanCopyMaterialList);
DECLARE_DELEGATE(FOnCopyMaterialList);
DECLARE_DELEGATE(FOnPasteMaterialList);

DECLARE_DELEGATE_RetVal_OneParam(bool, FOnCanCopyMaterialItem, int32);
DECLARE_DELEGATE_OneParam(FOnCopyMaterialItem, int32);
DECLARE_DELEGATE_OneParam(FOnPasteMaterialItem, int32);

struct FMaterialListDelegates
{
	FMaterialListDelegates()
		: OnGetMaterials()
		, OnMaterialChanged()
		, OnGenerateCustomNameWidgets()
		, OnGenerateCustomMaterialWidgets()
		, OnResetMaterialToDefaultClicked()
	{}

	/** Delegate called to populate the list with materials */
	FOnGetMaterials OnGetMaterials;
	/** Delegate called when a user changes the material */
	FOnMaterialChanged OnMaterialChanged;
	/** Delegate called to generate custom widgets under the name of in the left column of a details panel*/
	FOnGenerateWidgetsForMaterial OnGenerateCustomNameWidgets;
	/** Delegate called to generate custom widgets under each material */
	FOnGenerateWidgetsForMaterial OnGenerateCustomMaterialWidgets;
	/** Delegate called when a material list item should be reset to default */
	FOnResetMaterialToDefaultClicked OnResetMaterialToDefaultClicked;
	/** Delegate called when we tick the material list to know if the list is dirty*/
	FOnMaterialListDirty OnMaterialListDirty;

	/** Delegate called Copying a material list */
	FOnCopyMaterialList OnCopyMaterialList;
	/** Delegate called to know if we can copy a material list */
	FOnCanCopyMaterialList OnCanCopyMaterialList;
	/** Delegate called Pasting a material list */
	FOnPasteMaterialList OnPasteMaterialList;

	/** Delegate called Copying a material item */
	FOnCopyMaterialItem OnCopyMaterialItem;
	/** Delegate called to know if we can copy a material item */
	FOnCanCopyMaterialItem OnCanCopyMaterialItem;
	/** Delegate called Pasting a material item */
	FOnPasteMaterialItem OnPasteMaterialItem;
};

/**
 * Builds up a list of unique materials while creating some information about the materials
 */
class IMaterialListBuilder
{
public:

	/** Virtual destructor. */
	virtual ~IMaterialListBuilder(){};

	/** 
	 * Adds a new material to the list
	 * 
	 * @param SlotIndex The slot (usually mesh element index) where the material is located on the component.
	 * @param Material The material being used.
	 * @param bCanBeReplced Whether or not the material can be replaced by a user.
	 */
	virtual void AddMaterial( uint32 SlotIndex, UMaterialInterface* Material, bool bCanBeReplaced ) = 0;
};


/**
 * A Material item in a material list slot
 */
struct FMaterialListItem
{
	/** Material being used */
	TWeakObjectPtr<UMaterialInterface> Material;

	/** Slot on a component where this material is at (mesh element) */
	int32 SlotIndex;

	/** Whether or not this material can be replaced by a new material */
	bool bCanBeReplaced;

	FMaterialListItem( UMaterialInterface* InMaterial = NULL, uint32 InSlotIndex = 0, bool bInCanBeReplaced = false )
		: Material( InMaterial )
		, SlotIndex( InSlotIndex )
		, bCanBeReplaced( bInCanBeReplaced )
	{}

	friend uint32 GetTypeHash( const FMaterialListItem& InItem )
	{
		return GetTypeHash( InItem.Material ) + InItem.SlotIndex ;
	}

	bool operator==( const FMaterialListItem& Other ) const
	{
		return Material == Other.Material && SlotIndex == Other.SlotIndex ;
	}

	bool operator!=( const FMaterialListItem& Other ) const
	{
		return !(*this == Other);
	}
};


class FMaterialList
	: public IDetailCustomNodeBuilder
	, public TSharedFromThis<FMaterialList>
{
public:
	PROPERTYEDITOR_API FMaterialList( IDetailLayoutBuilder& InDetailLayoutBuilder, FMaterialListDelegates& MaterialListDelegates, const TArray<FAssetData>& InOwnerAssetDataArray, bool bInAllowCollapse = false, bool bInShowUsedTextures = true, bool bInDisplayCompactSize = false);

	/**
	 * @return true if materials are being displayed.                                                          
	 */
	bool IsDisplayingMaterials() const { return true; }

private:

	/**
	 * Called when a user expands all materials in a slot.
	 *
	 * @param SlotIndex The index of the slot being expanded.
	 */
	void OnDisplayMaterialsForElement( int32 SlotIndex );

	/**
	 * Called when a user hides all materials in a slot.
	 *
	 * @param SlotIndex The index of the slot being hidden.
	 */
	void OnHideMaterialsForElement( int32 SlotIndex );

	/** IDetailCustomNodeBuilder interface */
	virtual void SetOnRebuildChildren( FSimpleDelegate InOnRebuildChildren  ) override { OnRebuildChildren = InOnRebuildChildren; } 
	virtual bool RequiresTick() const override { return true; }
	virtual void Tick( float DeltaTime ) override;
	virtual void GenerateHeaderRowContent( FDetailWidgetRow& NodeRow ) override;
	virtual void GenerateChildContent( IDetailChildrenBuilder& ChildrenBuilder ) override;
	virtual FName GetName() const override { return NAME_None; }
	virtual bool InitiallyCollapsed() const override { return bAllowCollpase; }

	/**
	 * Adds a new material item to the list
	 *
	 * @param Row			The row to add the item to
	 * @param CurrentSlot	The slot id of the material
	 * @param Item			The material item to add
	 * @param bDisplayLink	If a link to the material should be displayed instead of the actual item (for multiple materials)
	 */
	void AddMaterialItem(FDetailWidgetRow& Row, int32 CurrentSlot, const FMaterialListItem& Item, bool bDisplayLink);

private:
	bool OnCanCopyMaterialList() const;
	void OnCopyMaterialList();
	void OnPasteMaterialList();

	bool OnCanCopyMaterialItem(int32 CurrentSlot) const;
	void OnCopyMaterialItem(int32 CurrentSlot);
	void OnPasteMaterialItem(int32 CurrentSlot);

	/** Delegates for the material list */
	FMaterialListDelegates MaterialListDelegates;

	/** Called to rebuild the children of the detail tree */
	FSimpleDelegate OnRebuildChildren;

	/** Parent detail layout this list is in */
	IDetailLayoutBuilder& DetailLayoutBuilder;

	/** Set of all unique displayed materials */
	TArray< FMaterialListItem > DisplayedMaterials;

	/** Set of all materials currently in view (may be less than DisplayedMaterials) */
	TArray< TSharedRef<FMaterialItemView> > ViewedMaterials;

	/** Set of all expanded slots */
	TSet<uint32> ExpandedSlots;

	/** Material list builder used to generate materials */
	TSharedRef<FMaterialListBuilder> MaterialListBuilder;

	/** Allow Collapse of material header row. Right now if you allow collapse, it will initially collapse. */
	bool bAllowCollpase;
	/** Whether or not to use the used textures menu for each material entry */
	bool bShowUsedTextures;
	/** Whether or not to display a compact form of material entry*/
	bool bDisplayCompactSize;
	/** The mesh asset that owns these materials */
	TArray<FAssetData> OwnerAssetDataArray;
};

