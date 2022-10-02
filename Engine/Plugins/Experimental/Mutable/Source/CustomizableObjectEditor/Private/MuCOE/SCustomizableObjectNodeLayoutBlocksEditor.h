// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Math/UnrealMathSSE.h"
#include "Templates/SharedPointer.h"
#include "Types/SlateEnums.h"
#include "UObject/GCObject.h"
#include "UObject/NameTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FReferenceCollector;
class ICustomizableObjectInstanceEditor;
class ISlateStyle;
class SWidget;
struct FCustomizableObjectLayoutBlock;
struct FGuid;


/**
 * CustomizableObject Editor Preview viewport widget
 */
class SCustomizableObjectNodeLayoutBlocksEditor : public SCompoundWidget, public FGCObject
{
public:
	SLATE_BEGIN_ARGS( SCustomizableObjectNodeLayoutBlocksEditor ){}
		SLATE_ARGUMENT(TWeakPtr<ICustomizableObjectInstanceEditor>, CustomizableObjectEditor)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	~SCustomizableObjectNodeLayoutBlocksEditor();
	
	// FSerializableObject interface
	void AddReferencedObjects( FReferenceCollector& Collector ) override;
	virtual FString GetReferencerName() const override
	{
		return TEXT("SCustomizableObjectNodeLayoutBlocksEditor");
	}
	// End of FSerializableObject interface


	/** Binds commands associated with the viewport client. */
	void BindCommands();

	/**  */
	void SetCurrentLayout( class UCustomizableObjectLayout* Layout );

private:

	/** Pointer back to the editor tool that owns us */
	TWeakPtr<ICustomizableObjectInstanceEditor> CustomizableObjectEditorPtr;

	/** */
	class UCustomizableObjectLayout* CurrentLayout;

	/** */
	TSharedPtr<class SCustomizableObjectLayoutGrid> LayoutGridWidget;

	TSharedPtr< class SVerticalBox > LayoutGridSizeWidget;

	/** Widget for displaying the available layout block grid sizes. */
	TSharedPtr< class STextComboBox > LayoutGridSizeCombo;
	TSharedPtr< class STextComboBox > MaxLayoutGridSizeCombo;

	/** List of available layout grid sizes. */
	TArray< TSharedPtr< FString > > LayoutGridSizes;

	/** List of available layout max grid sizes. */
	TArray< TSharedPtr< FString > > MaxLayoutGridSizes;

	/** List of available layout packing strategies. */
	TArray< TSharedPtr< FString > > LayoutPackingStrategies;

	/** Widget for displaying the available layout packing strategies. */
	TSharedPtr< class STextComboBox > LayoutPackingStrategyCombo;

	/** Widget to select the layout packing strategy */
	TSharedPtr<class SHorizontalBox> LayoutStrategyWidget;

	/** Widget to select the fixed layout properties */
	TSharedPtr<class SHorizontalBox> FixedLayoutWidget;

	TSharedPtr<SWidget> StrategyWidget;

	/** */
	FIntPoint GetGridSize() const;
	void OnBlockChanged(FGuid BlockId, FIntRect Block );
	TArray<FCustomizableObjectLayoutBlock> GetBlocks() const;

	/** Callbacks from the layout block editor. */
	TSharedRef<SWidget> BuildLayoutToolBar();
	TSharedRef<SWidget> BuildLayoutStrategyWidgets(const ISlateStyle* Style, const FName& StyleName);
	void OnAddBlock();
	void OnAddBlockAt(const FIntPoint Min, const FIntPoint Max);
	void OnRemoveBlock();

	/** Generate layout blocks using UVs*/
	void OnGenerateBlocks();
		
	/** . */
	void OnGridSizeChanged( TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo );
	void OnMaxGridSizeChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);

	/** Sets the block priority from the input text. */
	void OnSetBlockPriority(int32 InValue);

	/** Called when the packing strategy has changed. */
	void OnLayoutPackingStrategyChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);

};
