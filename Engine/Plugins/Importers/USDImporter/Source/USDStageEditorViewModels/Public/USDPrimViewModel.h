// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "USDTreeItemViewModel.h"
#include "Templates/SharedPointer.h"
#include "Internationalization/Text.h"

#include "UsdWrappers/UsdStage.h"
#include "UsdWrappers/UsdPrim.h"

using FUsdPrimViewModelRef = TSharedRef< class FUsdPrimViewModel >;
using FUsdPrimViewModelPtr = TSharedPtr< class FUsdPrimViewModel >;

class USDSTAGEEDITORVIEWMODELS_API FUsdPrimModel : public TSharedFromThis< FUsdPrimModel >
{
public:
	FText GetName() const { return Name; }
	FText GetType() const { return Type; }
	bool HasPayload() const { return bHasPayload; }
	bool IsLoaded() const { return bIsLoaded; }
	bool HasCompositionArcs() const {return bHasCompositionArcs; }
	bool IsVisible() const { return bIsVisible; }

	FText Name;
	FText Type;
	bool bHasPayload = false;
	bool bIsLoaded = false;
	bool bHasCompositionArcs = false;
	bool bIsVisible = true;
};

class USDSTAGEEDITORVIEWMODELS_API FUsdPrimViewModel : public IUsdTreeViewItem, public TSharedFromThis< FUsdPrimViewModel >
{
public:
	FUsdPrimViewModel( FUsdPrimViewModel* InParentItem, const UE::FUsdStage& InUsdStage, const UE::FUsdPrim& InUsdPrim );

	FUsdPrimViewModel( FUsdPrimViewModel* InParentItem, const UE::FUsdStage& InUsdStage );

	TArray< FUsdPrimViewModelRef >& UpdateChildren();

	void FillChildren();

	void RefreshData( bool bRefreshChildren );

	bool CanExecutePrimAction() const;
	bool HasVisibilityAttribute() const;
	void ToggleVisibility();
	void TogglePayload();

	void DefinePrim( const TCHAR* PrimName );

	/** Delegate for hooking up an inline editable text block to be notified that a rename is requested. */
	DECLARE_DELEGATE( FOnRenameRequest );

	/** Broadcasts whenever a rename is requested */
	FOnRenameRequest RenameRequestEvent;

public:
	void AddReference( const TCHAR* AbsoluteFilePath );
	void ClearReferences();

public:
	UE::FUsdStage UsdStage;
	UE::FUsdPrim UsdPrim;
	FUsdPrimViewModel* ParentItem;

	TArray< FUsdPrimViewModelRef > Children;

	TSharedRef< FUsdPrimModel > RowData; // Data model

	bool bIsRenamingExistingPrim = false;
};
