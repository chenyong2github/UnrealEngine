// Copyright Epic Games, Inc. All Rights Reserved.


#include "Debugging/SKismetDebuggingView.h"

#include "BlueprintEditor.h"
#include "ClassViewerFilter.h"
#include "ClassViewerModule.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Textures/SlateIcon.h"
#include "Framework/Commands/UIAction.h"
#include "Widgets/Images/SImage.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SButton.h"
#include "EditorStyleSet.h"
#include "GameFramework/Actor.h"
#include "Editor/EditorEngine.h"
#include "EngineGlobals.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Editor.h"
#include "GraphEditorSettings.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "K2Node.h"
#include "Kismet2/Breakpoint.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "Kismet2/DebuggerCommands.h"
#include "Debugging/KismetDebugCommands.h"
#include "Widgets/Input/SHyperlink.h"
#include "ToolMenus.h"
#include "PropertyEditor/Private/SDetailsView.h"
#include "Styling/SlateIconFinder.h"
#include "Styling/StyleColors.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "PropertyInfoViewStyle.h"
#include "Widgets/Input/SSearchBox.h"

#define LOCTEXT_NAMESPACE "DebugViewUI"

DEFINE_LOG_CATEGORY_STATIC(LogBlueprintDebuggingView, Log, All);

//////////////////////////////////////////////////////////////////////////

namespace KismetDebugViewConstants
{
	const FName ColumnId_Name( "Name" );
	const FName ColumnId_Value( "Value" );
	const FText ColumnText_Name( NSLOCTEXT("DebugViewUI", "Name", "Name") );
	const FText ColumnText_Value( NSLOCTEXT("DebugViewUI", "Value", "Value") );
	const FText ColumnText_DebugKey( FText::GetEmpty() );
	const FText ColumnText_Info( NSLOCTEXT("DebugViewUI", "Info", "Info") );
}

//////////////////////////////////////////////////////////////////////////
// FDebugLineItem

uint16 FDebugLineItem::ActiveTypeBitset = TNumericLimits<uint16>::Max(); // set all to active by default

FText FDebugLineItem::GetDisplayName() const
{
	return FText::GetEmpty();
}

FText FDebugLineItem::GetDescription() const
{
	return FText::GetEmpty();
}

TSharedRef<SWidget> FDebugLineItem::GenerateNameWidget()
{
	return SNew(PropertyInfoViewStyle::STextHighlightOverlay)
		.FullText(this, &FDebugLineItem::GetDisplayName)
		.HighlightText(SearchBox.Get(), &SSearchBox::GetText)
		[
			SNew(STextBlock)
				.ToolTipText(this, &FDebugLineItem::GetDisplayName)
				.Text(this, &FDebugLineItem::GetDisplayName)
		];
}

TSharedRef<SWidget> FDebugLineItem::GenerateValueWidget()
{
	return SNew(PropertyInfoViewStyle::STextHighlightOverlay)
		.FullText(this, &FDebugLineItem::GetDescription)
		.HighlightText(SearchBox.Get(), &SSearchBox::GetText)
		[
			SNew(STextBlock)
				.ToolTipText(this, &FDebugLineItem::GetDescription)
				.Text(this, &FDebugLineItem::GetDescription)
		];
}

void FDebugLineItem::UpdateSearchFlags(bool bIsRootNode)
{
	const FString SearchString = SearchBox->GetText().ToString();
	
	bVisible = GetDisplayName().ToString().Contains(SearchString) || GetDescription().ToString().Contains(SearchString);

	// for root nodes, bParentsMatchSearch always matches bVisible
	if(bVisible || bIsRootNode)
	{
		bParentsMatchSearch = bVisible;
	}
}

bool FDebugLineItem::IsVisible()
{
	return bVisible;
}

bool FDebugLineItem::DoParentsMatchSearch()
{
	return bParentsMatchSearch;
}

bool FDebugLineItem::HasChildren()
{
	return false;
}

TSharedRef<SWidget> FDebugLineItem::GetNameIcon()
{
	static const FSlateBrush* CachedBrush = FEditorStyle::GetBrush(TEXT("NoBrush"));	
	return SNew(SImage).Image(CachedBrush);
}

TSharedRef<SWidget> FDebugLineItem::GetValueIcon()
{
	static const FSlateBrush* CachedBrush = FEditorStyle::GetBrush(TEXT("NoBrush"));	
	return SNew(SImage).Image(CachedBrush);
}

UBlueprint* FDebugLineItem::GetBlueprintForObject(UObject* ParentObject)
{
	if(ParentObject == nullptr)
	{
		return nullptr;
	}
	
	if(UBlueprint* ParentBlueprint = Cast<UBlueprint>(ParentObject))
	{
		return ParentBlueprint;
	}

	if(UClass* ParentClass = ParentObject->GetClass())
	{
		if(UBlueprint* ParentBlueprint = Cast<UBlueprint>(ParentClass->ClassGeneratedBy))
		{
			return ParentBlueprint;
		}
	}
	
	// recursively walk up ownership heirrarchy until we find the blueprint
	return GetBlueprintForObject(ParentObject->GetOuter());
}

UBlueprintGeneratedClass* FDebugLineItem::GetClassForObject(UObject* ParentObject)
{
	if (ParentObject != nullptr)
	{
		if (UBlueprint* Blueprint = Cast<UBlueprint>(ParentObject))
		{
			return Cast<UBlueprintGeneratedClass>(*Blueprint->GeneratedClass);
		}
		else if (UBlueprintGeneratedClass* Result = Cast<UBlueprintGeneratedClass>(ParentObject))
		{
			return Result;
		}
		else
		{
			return Cast<UBlueprintGeneratedClass>(ParentObject->GetClass());
		}
	}

	return nullptr;
}

bool FDebugLineItem::IsDebugLineTypeActive(EDebugLineType Type)
{
	const uint16 Mask = 1 << Type;
	return ActiveTypeBitset & Mask;
}

void FDebugLineItem::OnDebugLineTypeActiveChanged(ECheckBoxState CheckState, EDebugLineType Type)
{
	const uint16 Mask = 1 << Type;
	switch (CheckState)
	{
		case ECheckBoxState::Checked:
		{
			ActiveTypeBitset |= Mask;
			break;
		}
		default:
		{
			ActiveTypeBitset &= ~Mask;
			break;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// ILineItemWithChildren

class FLineItemWithChildren : public FDebugLineItem
{
public:
	FLineItemWithChildren(EDebugLineType InType, TSharedPtr<SSearchBox> InSearchBox) :
		FDebugLineItem(InType, MoveTemp(InSearchBox))
	{}
	
	virtual ~FLineItemWithChildren() override = default;

	virtual bool HasChildren() override
	{
		return !ChildrenMirrors.IsEmpty();
	}

	virtual bool CanHaveChildren() override { return true; }

	bool SearchRecursive(TArray<FLineItemWithChildren*>& Parents,
		TSharedPtr<STreeView<FDebugTreeItemPtr>> DebugTreeView)
	{
		TSharedPtr<ITableRow> Row = DebugTreeView->WidgetFromItem(SharedThis(this));
		bVisible = false;

		UpdateSearchFlags();

		bool bChildMatch = false;
		Parents.Push(this);
		
		TArray<FDebugTreeItemPtr> Children;
		GatherChildren(Children, false);
		for(const FDebugTreeItemPtr& ChildRef : Children)
		{
			if(ChildRef->CanHaveChildren())
			{
				ChildRef->bParentsMatchSearch = bParentsMatchSearch;
				FLineItemWithChildren* Child = StaticCast<FLineItemWithChildren*>(ChildRef.Get());
				
                // check if the child has been seen already in parents.
                // if it has, skip it. (avoids stack overflows)
                if(Parents.FindByPredicate(
                [Child](const FLineItemWithChildren* Relative)
                	{
                		return (Relative->Type == Child->Type) && Relative->Compare(Child);
                	}
                ))
                {
                	continue;
                }

				// if any children need to expand, so should this
				if(Child->SearchRecursive(Parents, DebugTreeView))
				{
					bVisible = true;
					bChildMatch = true;

					// exit early if children aren't in the tree yet anyway and
					// we already know to expand this
					if(!Row)
					{
						break;
					}
				}
			}
			else
			{
				ChildRef->UpdateSearchFlags();
				
				// if any children need to expand, so should this
				if(ChildRef->IsVisible())
				{
					bVisible = true;
					bChildMatch = true;

					// exit early if children aren't in the tree yet anyway and
					// we already know to expand this
					if(!Row)
					{
						break;
					}
				}
			}
		}
		
		Parents.Pop(false);
		if(bChildMatch)
		{
			if(Row && !Row->IsItemExpanded())
			{
				Row->ToggleExpansion();
			}
		}

		return bVisible;
	}

protected:
	// List of children 
	TArray<FDebugTreeItemPtr> ChildrenMirrors;
	
	/**
	 * Adds either Item or an identical node that was previously
	 * created (present in ChildrenMirrors) as a child to OutChildren.
	 *
	 * O( # Children )
	 */
	void EnsureChildIsAdded(TArray<FDebugTreeItemPtr>& OutChildren, const FDebugLineItem& Item, bool bRespectSearch)
	{
		for (int32 i = 0; i < ChildrenMirrors.Num(); ++i)
		{
			TSharedPtr< FDebugLineItem > MirrorItem = ChildrenMirrors[i];
			if(bParentsMatchSearch)
			{
				// propogate parents search state to children
				MirrorItem->bParentsMatchSearch = true;
			}

			if (MirrorItem->Type == Item.Type)
			{
				if (Item.Compare(MirrorItem.Get()))
				{
					MirrorItem->UpdateData(Item);
					
					// only add item if it matches search
					if(!bRespectSearch || SearchBox->GetText().IsEmpty() || MirrorItem->IsVisible() || MirrorItem->DoParentsMatchSearch())
					{
						OutChildren.Add(MirrorItem);
					}
					return;
				}
			}
		}

		TSharedPtr< FDebugLineItem > Result = MakeShareable(Item.Duplicate());
		ChildrenMirrors.Add(Result);

		OutChildren.Add(Result);
	}
};


//////////////////////////////////////////////////////////////////////////
// FMessageLineItem

struct FMessageLineItem : public FDebugLineItem
{
protected:
	FString Message;
public:
	// Message line
	FMessageLineItem(const FString& InMessage, TSharedPtr<SSearchBox> InSearchBox)
		: FDebugLineItem(DLT_Message, MoveTemp(InSearchBox))
		, Message(InMessage)
	{
	}
protected:
	virtual bool Compare(const FDebugLineItem* BaseOther) const override
	{
		FMessageLineItem* Other = (FMessageLineItem*)BaseOther;
		return Message == Other->Message;
	}

	virtual FDebugLineItem* Duplicate() const override
	{
		return new FMessageLineItem(Message, SearchBox);
	}

	virtual FText GetDescription() const override
	{
		return FText::FromString(Message);
	}
};

//////////////////////////////////////////////////////////////////////////
// FLatentActionLineItem

struct FLatentActionLineItem : public FDebugLineItem
{
protected:
	int32 UUID;
	TWeakObjectPtr< UObject > ParentObjectRef;
public:
	FLatentActionLineItem(int32 InUUID, UObject* ParentObject, TSharedPtr<SSearchBox> InSearchBox)
		: FDebugLineItem(DLT_LatentAction, MoveTemp(InSearchBox))
	{
		UUID = InUUID;
		check(UUID != INDEX_NONE);
		ParentObjectRef = ParentObject;
	}

protected:
	virtual bool Compare(const FDebugLineItem* BaseOther) const override
	{
		FLatentActionLineItem* Other = (FLatentActionLineItem*)BaseOther;
		return (ParentObjectRef.Get() == Other->ParentObjectRef.Get()) &&
			(UUID == Other->UUID);
	}

	virtual FDebugLineItem* Duplicate() const override
	{
		return new FLatentActionLineItem(UUID, ParentObjectRef.Get(), SearchBox);
	}
protected:
	virtual TSharedRef<SWidget> GenerateNameWidget() override;
	virtual TSharedRef<SWidget> GetNameIcon() override;
	virtual FText GetDescription() const override;
	virtual FText GetDisplayName() const override;
	void OnNavigateToLatentNode();

	class UEdGraphNode* FindAssociatedNode() const;
};

FText FLatentActionLineItem::GetDescription() const
{
	if (UObject* ParentObject = ParentObjectRef.Get())
	{
		if (UWorld* World = GEngine->GetWorldFromContextObject(ParentObject, EGetWorldErrorMode::ReturnNull))
		{
			FLatentActionManager& LatentActionManager = World->GetLatentActionManager();
			return FText::FromString(LatentActionManager.GetDescription(ParentObject, UUID));
		}
	}

	return LOCTEXT("nullptrObject", "Object has been destroyed");
}

TSharedRef<SWidget> FLatentActionLineItem::GenerateNameWidget()
{
	return SNew(PropertyInfoViewStyle::STextHighlightOverlay)
		.FullText(this, &FLatentActionLineItem::GetDisplayName)
		.HighlightText(SearchBox.Get(), &SSearchBox::GetText)
		[
			SNew(SHyperlink)
				.Style(FEditorStyle::Get(), "HoverOnlyHyperlink")
				.OnNavigate(this, &FLatentActionLineItem::OnNavigateToLatentNode)
				.Text(this, &FLatentActionLineItem::GetDisplayName)
				.ToolTipText( LOCTEXT("NavLatentActionLoc_Tooltip", "Navigate to the latent action location") )
		];
}

TSharedRef<SWidget> FLatentActionLineItem::GetNameIcon()
{
	return SNew(SImage)
		.Image(FEditorStyle::GetBrush(TEXT("Kismet.LatentActionIcon")));
}

UEdGraphNode* FLatentActionLineItem::FindAssociatedNode() const
{
	if (UBlueprintGeneratedClass* Class = GetClassForObject(ParentObjectRef.Get()))
	{
		return Class->GetDebugData().FindNodeFromUUID(UUID);
	}

	return nullptr;
}

FText FLatentActionLineItem::GetDisplayName() const
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("ID"), UUID);
	if (UK2Node* Node = Cast<UK2Node>(FindAssociatedNode()))
	{
		Args.Add(TEXT("Title"), Node->GetCompactNodeTitle());

		return FText::Format(LOCTEXT("ID", "{Title} (ID: {ID})"), Args);
	}
	else
	{
		return FText::Format(LOCTEXT("LatentAction", "Latent action # {ID}"), Args);
	}
}

void FLatentActionLineItem::OnNavigateToLatentNode( )
{
	if (UEdGraphNode* Node = FindAssociatedNode())
	{
		FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Node);
	}
}

struct FWatchChildLineItem : public FLineItemWithChildren
{
protected:
	FPropertyInstanceInfo Data;
public:
	FWatchChildLineItem(const FPropertyInstanceInfo& Child, TSharedPtr<SSearchBox> InSearchBox) :
		FLineItemWithChildren(DLT_WatchChild, MoveTemp(InSearchBox)),
		Data(Child)
	{}

	virtual bool Compare(const FDebugLineItem* BaseOther) const override
	{
		FWatchChildLineItem* Other = (FWatchChildLineItem*)BaseOther;
		
		return Data.Property == Other->Data.Property &&
			Data.DisplayName.CompareTo(Other->Data.DisplayName) == 0;
	}

	virtual void UpdateData(const FDebugLineItem& NewerData) override
	{
		// Compare returns true even if the value or children of this node
		// is different. use this function to update the data without completely
		// replacing the node
		FWatchChildLineItem& Other = (FWatchChildLineItem&)NewerData;
		Data = Other.Data;
	}

	virtual FDebugLineItem* Duplicate() const override
	{
		return new FWatchChildLineItem(Data, SearchBox);
	}
	
	virtual FText GetDescription() const override
	{
		const FString ValStr = Data.Value.ToString();
		return FText::FromString(ValStr.Replace(TEXT("\n") ,TEXT(" ")));
	}
	
	virtual FText GetDisplayName() const override
	{
		return Data.DisplayName;
	}

	// uses the icon and color associated with the property type
	virtual TSharedRef<SWidget> GetNameIcon() override
	{
		FSlateColor Color;
		FSlateColor SecondaryColor;
		FSlateBrush const* SecondaryIcon;
		const FSlateBrush* Icon = FBlueprintEditor::GetVarIconAndColorFromProperty(
			Data.Property.Get(),
			Color,
			SecondaryIcon,
			SecondaryColor
		);
		return SNew(SImage)
			.Image(Icon)
			.ColorAndOpacity(Color);
	}

	virtual void GatherChildren(TArray<FDebugTreeItemPtr>& OutChildren, bool bRespectSearch) override
	{
		for (const TSharedPtr<FPropertyInstanceInfo>& ChildData : Data.Children)
		{
			EnsureChildIsAdded(OutChildren, FWatchChildLineItem(*ChildData, SearchBox), bRespectSearch);
		}
	}
};

//////////////////////////////////////////////////////////////////////////\
// FSelfWatchLineItem
struct FSelfWatchLineItem : public FLineItemWithChildren
{
protected:
	// watches a UObject instead of a pin
	TWeakObjectPtr<UObject> ObjectToWatch;
public:
	FSelfWatchLineItem(UObject* Object, TSharedPtr<SSearchBox> InSearchBox)	: 
		FLineItemWithChildren(DLT_Watch, MoveTemp(InSearchBox)),
		ObjectToWatch(Object)
	{}

	virtual bool Compare(const FDebugLineItem* BaseOther) const override
	{
		FSelfWatchLineItem* Other = (FSelfWatchLineItem*)BaseOther;
		return (ObjectToWatch.Get() == Other->ObjectToWatch.Get());
	}

	virtual FDebugLineItem* Duplicate() const override
	{
		return new FSelfWatchLineItem(ObjectToWatch.Get(), SearchBox);
	}
	
	virtual void GatherChildren(TArray<FDebugTreeItemPtr>& OutChildren, bool bRespectSearch) override
	{
		if (UObject* Object = ObjectToWatch.Get())
		{
			for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
			{
				TSharedPtr<FPropertyInstanceInfo> DebugInfo;
				FProperty* Property = *It;
				if(Property->HasAllPropertyFlags(CPF_BlueprintVisible))
				{
					void* Value = Property->ContainerPtrToValuePtr<void*>(Object);
					FKismetDebugUtilities::GetDebugInfoInternal(DebugInfo, Property, Value);
				
					EnsureChildIsAdded(OutChildren, FWatchChildLineItem(*DebugInfo, SearchBox), bRespectSearch);
				}
			}
		}
	}

protected:
	virtual FText GetDisplayName() const override
	{
		return LOCTEXT("SelfName", "Self");
	}

	virtual TSharedRef<SWidget> GetNameIcon() override
	{
		return SNew(SImage)
			.Image(FEditorStyle::GetBrush(TEXT("Kismet.WatchIcon")));
	}
};

//////////////////////////////////////////////////////////////////////////
// FWatchLineItem 


struct FWatchLineItem : public FLineItemWithChildren
{
protected:
	TWeakObjectPtr< UObject > ParentObjectRef;
	FEdGraphPinReference ObjectRef;
public:
	FWatchLineItem(UEdGraphPin* PinToWatch, UObject* ParentObject, TSharedPtr<SSearchBox> InSearchBox)
		: FLineItemWithChildren(DLT_Watch, MoveTemp(InSearchBox))
	{
		ObjectRef = PinToWatch;
		ParentObjectRef = ParentObject;
	}

	virtual bool Compare(const FDebugLineItem* BaseOther) const override
	{
		FWatchLineItem* Other = (FWatchLineItem*)BaseOther;
		return (ParentObjectRef == Other->ParentObjectRef) &&
			(ObjectRef == Other->ObjectRef);
	}

	virtual FDebugLineItem* Duplicate() const override
	{
		return new FWatchLineItem(ObjectRef.Get(), ParentObjectRef.Get(), SearchBox);
	}	

	virtual void MakeMenu(class FMenuBuilder& MenuBuilder) override
	{
		if (UEdGraphPin* WatchedPin = ObjectRef.Get())
		{
			FUIAction ClearThisWatch(
				FExecuteAction::CreateStatic( &FDebuggingActionCallbacks::ClearWatch, WatchedPin )
				);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("ClearWatch", "Stop watching"),
				LOCTEXT("ClearWatch_ToolTip", "Stop watching this variable"),
				FSlateIcon(),
				ClearThisWatch);
		}
	}
	
	virtual void GatherChildren(TArray<FDebugTreeItemPtr>& OutChildren, bool bRespectSearch) override
	{
		if (UEdGraphPin* PinToWatch = ObjectRef.Get())
		{
			// Try to determine the blueprint that generated the watch
			UBlueprint* ParentBlueprint = GetBlueprintForObject(ParentObjectRef.Get());

			// Find a valid property mapping and display the current value
			UObject* ParentObject = ParentObjectRef.Get();
			if ((ParentBlueprint != ParentObject) && (ParentBlueprint != nullptr))
			{
				TSharedPtr<FPropertyInstanceInfo> DebugInfo;
				const FKismetDebugUtilities::EWatchTextResult WatchStatus = FKismetDebugUtilities::GetDebugInfo(DebugInfo, ParentBlueprint, ParentObject, PinToWatch);

				if(WatchStatus == FKismetDebugUtilities::EWTR_Valid)
				{
					check(DebugInfo);
					for (const TSharedPtr<FPropertyInstanceInfo>& ChildData : DebugInfo->Children)
					{
						EnsureChildIsAdded(OutChildren, FWatchChildLineItem(*ChildData, SearchBox), bRespectSearch);
					}
				}
			}
		}
	}
	

protected:
	virtual FText GetDescription() const override;
	virtual FText GetDisplayName() const override;
	virtual TSharedRef<SWidget> GenerateNameWidget() override;
	virtual TSharedRef<SWidget> GetNameIcon() override;

	void OnNavigateToWatchLocation();
};

FText FWatchLineItem::GetDisplayName() const
{
	if (UEdGraphPin* PinToWatch = ObjectRef.Get())
	{
		if (UBlueprint* Blueprint = GetBlueprintForObject(ParentObjectRef.Get()))
		{
			if (FProperty* Property = FKismetDebugUtilities::FindClassPropertyForPin(Blueprint, PinToWatch))
			{
				return FText::FromString(UEditorEngine::GetFriendlyName(Property));
			}
		}

		FFormatNamedArguments Args;
		Args.Add(TEXT("PinWatchName"), FText::FromString(PinToWatch->GetName()));
		return FText::Format(LOCTEXT("DisplayNameNoProperty", "{PinWatchName} (no prop)"), Args);
	}
	else
	{
		return FText::GetEmpty();
	}
}

FText FWatchLineItem::GetDescription() const
{
	if (UEdGraphPin* PinToWatch = ObjectRef.Get())
	{
		// Try to determine the blueprint that generated the watch
		UBlueprint* ParentBlueprint = GetBlueprintForObject(ParentObjectRef.Get());

		// Find a valid property mapping and display the current value
		UObject* ParentObject = ParentObjectRef.Get();
		if ((ParentBlueprint != ParentObject) && (ParentBlueprint != nullptr))
		{
			TSharedPtr<FPropertyInstanceInfo> DebugInfo;
			const FKismetDebugUtilities::EWatchTextResult WatchStatus = FKismetDebugUtilities::GetDebugInfo(DebugInfo, ParentBlueprint, ParentObject, PinToWatch);

			switch (WatchStatus)
			{
			case FKismetDebugUtilities::EWTR_Valid:
			{
				check(DebugInfo);
				const FString ValStr = DebugInfo->Value.ToString();
				return FText::FromString(ValStr.Replace(TEXT("\n") ,TEXT(" ")));
			}

			case FKismetDebugUtilities::EWTR_NotInScope:
				return LOCTEXT("NotInScope", "Not in scope");

			case FKismetDebugUtilities::EWTR_NoProperty:
				return LOCTEXT("UnknownProperty", "No debug data");

			default:
			case FKismetDebugUtilities::EWTR_NoDebugObject:
				return LOCTEXT("NoDebugObject", "No debug object");
			}			
		}
	}

	return FText::GetEmpty();
}

TSharedRef<SWidget> FWatchLineItem::GenerateNameWidget()
{
	return SNew(PropertyInfoViewStyle::STextHighlightOverlay)
		.FullText(this, &FWatchLineItem::GetDisplayName)
		.HighlightText(SearchBox.Get(), &SSearchBox::GetText)
		[
			SNew(SHyperlink)
				.Style(FEditorStyle::Get(), "HoverOnlyHyperlink")
				.OnNavigate(this, &FWatchLineItem::OnNavigateToWatchLocation)
				.Text(this, &FWatchLineItem::GetDisplayName)
				.ToolTipText( LOCTEXT("NavWatchLoc", "Navigate to the watch location") )
		];
}

// overlays the watch icon on top of a faded icon associated with the pin type
TSharedRef<SWidget> FWatchLineItem::GetNameIcon()
{
	const FSlateBrush* PinIcon;
	FLinearColor PinIconColor;
	if (UEdGraphPin* ObjectToFocus = ObjectRef.Get())
	{
		PinIcon = FBlueprintEditorUtils::GetIconFromPin(ObjectToFocus->PinType);
		
		const UEdGraphSchema* Schema = ObjectToFocus->GetSchema();
		PinIconColor = Schema->GetPinTypeColor(ObjectToFocus->PinType);
		PinIconColor.A = 0.3f;
	}
	else
	{
		PinIcon = FEditorStyle::GetBrush(TEXT("NoBrush"));
	}
	
	return SNew(SOverlay)
		+ SOverlay::Slot()
		.Padding(FMargin(10.f, 0.f, 0.f, 0.f))
		[
			SNew(SImage)
				.Image(PinIcon)
				.ColorAndOpacity(PinIconColor)
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		[
			SNew(SImage)
				.Image(FEditorStyle::GetBrush(TEXT("Kismet.WatchIcon")))
		];
}

void FWatchLineItem::OnNavigateToWatchLocation( )
{
	if (UEdGraphPin* ObjectToFocus = ObjectRef.Get())
	{
		FKismetEditorUtilities::BringKismetToFocusAttentionOnPin(ObjectToFocus);
	}
}

//////////////////////////////////////////////////////////////////////////
// FBreakpointLineItem

struct FBreakpointLineItem : public FDebugLineItem
{
protected:
	TWeakObjectPtr<UObject> ParentObjectRef;
	TSoftObjectPtr<UEdGraphNode> BreakpointNode;
public:
	FBreakpointLineItem(TSoftObjectPtr<UEdGraphNode> BreakpointToWatch, UObject* ParentObject, TSharedPtr<SSearchBox> InSearchBox)
		: FDebugLineItem(DLT_Breakpoint, MoveTemp(InSearchBox))
	{
		BreakpointNode = BreakpointToWatch;
		ParentObjectRef = ParentObject;
	}

	virtual bool Compare(const FDebugLineItem* BaseOther) const override
	{
		FBreakpointLineItem* Other = (FBreakpointLineItem*)BaseOther;
		return (ParentObjectRef.Get() == Other->ParentObjectRef.Get()) &&
			(BreakpointNode == Other->BreakpointNode);
	}

	virtual FDebugLineItem* Duplicate() const override
	{
		return new FBreakpointLineItem(BreakpointNode, ParentObjectRef.Get(), SearchBox);
	}	

	virtual void MakeMenu(class FMenuBuilder& MenuBuilder) override
	{
		FBlueprintBreakpoint* Breakpoint = GetBreakpoint();
		const UBlueprint* ParentBlueprint = GetBlueprintForObject(ParentObjectRef.Get());

		// By default, we don't allow actions to execute when in debug mode.
		// Create an empty action to always allow execution for these commands (they are allowed in debug mode)
		FCanExecuteAction AlwaysAllowExecute;

		if (Breakpoint != nullptr)
		{
			const bool bNewEnabledState = !Breakpoint->IsEnabledByUser();

			FUIAction ToggleThisBreakpoint(
				FExecuteAction::CreateStatic(
					&FDebuggingActionCallbacks::SetBreakpointEnabled, BreakpointNode, ParentBlueprint, bNewEnabledState
					),
				AlwaysAllowExecute
				);

			if (bNewEnabledState)
			{
				// Enable
				MenuBuilder.AddMenuEntry(
					LOCTEXT("EnableBreakpoint", "Enable breakpoint"),
					LOCTEXT("EnableBreakpoint_ToolTip", "Enable this breakpoint; the debugger will appear when this node is about to be executed."),
					FSlateIcon(),
					ToggleThisBreakpoint);
			}
			else
			{
				// Disable
				MenuBuilder.AddMenuEntry(
					LOCTEXT("DisableBreakpoint", "Disable breakpoint"),
					LOCTEXT("DisableBreakpoint_ToolTip", "Disable this breakpoint."),
					FSlateIcon(),
					ToggleThisBreakpoint);
			}
		}

		if ((Breakpoint != nullptr) && (ParentBlueprint != nullptr))
		{
			FUIAction ClearThisBreakpoint(
				FExecuteAction::CreateStatic( &FDebuggingActionCallbacks::ClearBreakpoint, BreakpointNode, ParentBlueprint ),
				AlwaysAllowExecute
				);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("ClearBreakpoint", "Remove breakpoint"),
				LOCTEXT("ClearBreakpoint_ToolTip", "Remove the breakpoint from this node."),
				FSlateIcon(),
				ClearThisBreakpoint);
		}
	}
protected:
	FBlueprintBreakpoint* GetBreakpoint() const
	{
		if(UEdGraphNode* Node = BreakpointNode.Get())
		{
			if(const UBlueprint* Blueprint = GetBlueprintForObject(Node))
			{
				return FKismetDebugUtilities::FindBreakpointForNode(Node, Blueprint);
			}
		}
		return nullptr;
	}
	
	virtual TSharedRef<SWidget> GenerateNameWidget() override
	{
		return SNew(PropertyInfoViewStyle::STextHighlightOverlay)
			.FullText(this, &FBreakpointLineItem::GetDisplayName)
			.HighlightText(SearchBox.Get(), &SSearchBox::GetText)
			[
				SNew(SHyperlink)
					.Style(FEditorStyle::Get(), "HoverOnlyHyperlink")
					.Text(this, &FBreakpointLineItem::GetDisplayName)
					.ToolTipText( LOCTEXT("NavBreakpointLoc", "Navigate to the breakpoint location") )
					.OnNavigate(this, &FBreakpointLineItem::OnNavigateToBreakpointLocation)
			];
	}

	virtual TSharedRef<SWidget> GetNameIcon() override
	{
		return SNew(SButton)
			.OnClicked(this, &FBreakpointLineItem::OnUserToggledEnabled)
			.ToolTipText(LOCTEXT("ToggleBreakpointButton_ToolTip", "Toggle this breakpoint"))
			.ButtonStyle( FEditorStyle::Get(), "NoBorder" )
			.ContentPadding(0.0f)
			[
				SNew(SImage)
					.Image(this, &FBreakpointLineItem::GetStatusImage)
					.ToolTipText(this, &FBreakpointLineItem::GetStatusTooltip)
			];
	}
	

	virtual FText GetDisplayName() const override;
	FReply OnUserToggledEnabled();

	void OnNavigateToBreakpointLocation();

	const FSlateBrush* GetStatusImage() const;
	FText GetStatusTooltip() const;
};

FText FBreakpointLineItem::GetDisplayName() const
{
	if (FBlueprintBreakpoint* MyBreakpoint = GetBreakpoint())
	{
		return MyBreakpoint->GetLocationDescription();
	}
	return FText::GetEmpty();
}

FReply FBreakpointLineItem::OnUserToggledEnabled()
{
	if (FBlueprintBreakpoint* MyBreakpoint = GetBreakpoint())
	{
		FKismetDebugUtilities::SetBreakpointEnabled(*MyBreakpoint, !MyBreakpoint->IsEnabledByUser());
	}
	return FReply::Handled();
}

void FBreakpointLineItem::OnNavigateToBreakpointLocation()
{
	if (FBlueprintBreakpoint* MyBreakpoint = GetBreakpoint())
	{
		FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(MyBreakpoint->GetLocation());
	}
}

const FSlateBrush* FBreakpointLineItem::GetStatusImage() const
{
	if (FBlueprintBreakpoint* MyBreakpoint = GetBreakpoint())
	{
		if (MyBreakpoint->IsEnabledByUser())
		{
			return FEditorStyle::GetBrush(FKismetDebugUtilities::IsBreakpointValid(*MyBreakpoint) ? TEXT("Kismet.Breakpoint.EnabledAndValid") : TEXT("Kismet.Breakpoint.EnabledAndInvalid"));
		}
		else
		{
			return FEditorStyle::GetBrush(TEXT("Kismet.Breakpoint.Disabled"));
		}
	}

	return FEditorStyle::GetDefaultBrush();
}

FText FBreakpointLineItem::GetStatusTooltip() const
{
	if (FBlueprintBreakpoint* MyBreakpoint = GetBreakpoint())
	{
		if (!FKismetDebugUtilities::IsBreakpointValid(*MyBreakpoint))
		{
			return LOCTEXT("Breakpoint_NoHit", "This breakpoint will not be hit because its node generated no code");
		}
		else
		{
			return MyBreakpoint->IsEnabledByUser() ? LOCTEXT("ActiveBreakpoint", "Active breakpoint") : LOCTEXT("InactiveBreakpoint", "Inactive breakpoint");
		}
	}
	else
	{
		return LOCTEXT("NoBreakpoint", "No Breakpoint");
	}
}

//////////////////////////////////////////////////////////////////////////
// FTraceStackParentItem

class FBreakpointParentItem : public FLineItemWithChildren
{
public:
	// The parent object
	TWeakObjectPtr<UBlueprint> Blueprint;
	
	FBreakpointParentItem(TWeakObjectPtr<UBlueprint> Blueprint, TSharedPtr<SSearchBox> InSearchBox)
		: FLineItemWithChildren(DLT_TraceStackParent, MoveTemp(InSearchBox)),
		  Blueprint(Blueprint)
	{
	}

	virtual void GatherChildren(TArray<FDebugTreeItemPtr>& OutChildren, bool bRespectSearch) override
	{
		// update search flags to match that of a root node
		UpdateSearchFlags(/* bIsRootNode */ true);
		
		if(!Blueprint.IsValid())
		{
			return;
		}
		
		// Create children for each breakpoint
		FKismetDebugUtilities::ForeachBreakpoint(
			Blueprint.Get(),
			[this, &OutChildren, bRespectSearch]
			(FBlueprintBreakpoint& Breakpoint)
			{
				EnsureChildIsAdded(OutChildren,
					FBreakpointLineItem(Breakpoint.GetLocation(), Blueprint.Get(), SearchBox), bRespectSearch);
			}
		);

		// Make sure there is something there, to let the user know if there is nothing
		if (OutChildren.Num() == 0)
		{
			EnsureChildIsAdded(OutChildren,
				FMessageLineItem(LOCTEXT("NoBreakpoints", "No breakpoints").ToString(), SearchBox), bRespectSearch);
		}
	}

protected:
	virtual FText GetDisplayName() const override
	{
		return LOCTEXT("Breakpoints", "Breakpoints");
	}

	virtual bool Compare(const FDebugLineItem* BaseOther) const override
	{
		check(false);
		return false;
	}

	virtual FDebugLineItem* Duplicate() const override
	{
		check(false);
		return nullptr;
	}
};

//////////////////////////////////////////////////////////////////////////
// FParentLineItem

class FParentLineItem : public FLineItemWithChildren
{
protected:
	// The parent object
	TWeakObjectPtr<UObject> ObjectRef;
public:
	FParentLineItem(UObject* Object, TSharedPtr<SSearchBox> InSearchBox)
		: FLineItemWithChildren(DLT_Parent, MoveTemp(InSearchBox))
	{
		ObjectRef = Object;
	}

	virtual UObject* GetParentObject() override
	{
		return ObjectRef.Get();
	}

	virtual void GatherChildren(TArray<FDebugTreeItemPtr>& OutChildren, bool bRespectSearch) override
	{
		// update search flags to match that of a root node
		UpdateSearchFlags(/* bIsRootNode */ true);
		
		if (UObject* ParentObject = ObjectRef.Get())
		{
			// every instance should have an automatic watch for 'self'
			EnsureChildIsAdded(OutChildren, FSelfWatchLineItem(ParentObject, SearchBox), bRespectSearch);
			
			UBlueprint* ParentBP = FDebugLineItem::GetBlueprintForObject(ParentObject);
			if (ParentBP != nullptr)
			{
				// Create children for each watch
				if(IsDebugLineTypeActive(DLT_Watch))
				{
					FKismetDebugUtilities::ForeachPinWatch(
						ParentBP,
						[this, &OutChildren, ParentObject, bRespectSearch](UEdGraphPin* WatchedPin)
						{
							EnsureChildIsAdded(OutChildren,
								FWatchLineItem(WatchedPin, ParentObject, SearchBox), bRespectSearch);
						}
					);
				}

				// It could also have active latent behaviors
				if(IsDebugLineTypeActive(DLT_LatentAction))
				{
					if (UWorld* World = GEngine->GetWorldFromContextObject(ParentObject, EGetWorldErrorMode::ReturnNull))
					{
						FLatentActionManager& LatentActionManager = World->GetLatentActionManager();

						// Get the current list of action UUIDs
						TSet<int32> UUIDSet;
						LatentActionManager.GetActiveUUIDs(ParentObject, /*inout*/ UUIDSet);

						// Add the new ones
						for (TSet<int32>::TConstIterator RemainingIt(UUIDSet); RemainingIt; ++RemainingIt)
						{
							const int32 UUID = *RemainingIt;
							EnsureChildIsAdded(OutChildren,
								FLatentActionLineItem(UUID, ParentObject, SearchBox), bRespectSearch);
						}
					}
				}

				// Make sure there is something there, to let the user know if there is nothing
				if (OutChildren.Num() == 0)
				{
					EnsureChildIsAdded(OutChildren,
						FMessageLineItem(LOCTEXT("NoDebugInfo", "No debugging info").ToString(), SearchBox), bRespectSearch);
				}
			}
			//@TODO: try to get at TArray<struct FDebugDisplayProperty> DebugProperties in UGameViewportClient, if available
		}
	}

	const FSlateBrush* GetStatusImage() const
	{
		if(SKismetDebuggingView::CurrentActiveObject == ObjectRef)
		{
			return FEditorStyle::GetBrush(TEXT("Kismet.Trace.CurrentIndex"));
		}
		if(ObjectRef.IsValid())
		{
			return FSlateIconFinder::FindIconBrushForClass(ObjectRef->GetClass());
		}
		return FEditorStyle::GetBrush(TEXT("None"));
	}

	FSlateColor GetStatusColor() const
	{
		if(SKismetDebuggingView::CurrentActiveObject == ObjectRef)
		{
			return FSlateColor(EStyleColor::AccentYellow);
		}
		const UGraphEditorSettings* Settings = GetDefault<UGraphEditorSettings>();
		return Settings->ObjectPinTypeColor;
	}
	
	FText GetStatusTooltip() const
	{
		if(SKismetDebuggingView::CurrentActiveObject == ObjectRef)
		{
			return LOCTEXT("BreakpointHIt", "Breakpoint Hit");
		}
		return FText::GetEmpty();
	}

protected:
	virtual bool Compare(const FDebugLineItem* BaseOther) const override
	{
		FParentLineItem* Other = (FParentLineItem*)BaseOther;
		return ObjectRef.Get() == Other->ObjectRef.Get();
	}

	virtual FDebugLineItem* Duplicate() const override
	{
		return new FParentLineItem(ObjectRef.Get(), SearchBox);
	}

	virtual FText GetDisplayName() const override
	{
		UObject* Object = ObjectRef.Get();
		AActor* Actor = Cast<AActor>(Object);

		if (Actor != nullptr)
		{
			 return FText::FromString(Actor->GetActorLabel());
		}
		else
		{
			return (Object != nullptr) ? FText::FromString(Object->GetName()) : LOCTEXT("nullptr", "(nullptr)");
		}
	}

	virtual TSharedRef<SWidget> GetNameIcon() override
	{
		return SNew(SImage)
			.Image(this, &FParentLineItem::GetStatusImage)
			.ColorAndOpacity_Raw(this, &FParentLineItem::GetStatusColor)
			.ToolTipText(this, &FParentLineItem::GetStatusTooltip);
	}

	virtual void MakeMenu(class FMenuBuilder& MenuBuilder) override
	{
		if (UBlueprint* BP = Cast<UBlueprint>(ObjectRef.Get()))
		{
			if (FKismetDebugUtilities::BlueprintHasPinWatches(BP))
			{
				FUIAction ClearAllWatches(
					FExecuteAction::CreateStatic( &FDebuggingActionCallbacks::ClearWatches, BP )
					);

				MenuBuilder.AddMenuEntry(
					LOCTEXT("ClearWatches", "Clear all watches"),
					LOCTEXT("ClearWatches_ToolTip", "Clear all watches in this blueprint"),
					FSlateIcon(),
					ClearAllWatches);
			}

			if (FKismetDebugUtilities::BlueprintHasBreakpoints(BP))
			{
				FUIAction ClearAllBreakpoints(
					FExecuteAction::CreateStatic( &FDebuggingActionCallbacks::ClearBreakpoints, BP )
					);

				MenuBuilder.AddMenuEntry(
					LOCTEXT("ClearBreakpoints", "Remove all breakpoints"),
					LOCTEXT("ClearBreakpoints_ToolTip", "Clear all breakpoints in this blueprint"),
					FSlateIcon(),
					ClearAllBreakpoints);
			}
		}
	}
};

//////////////////////////////////////////////////////////////////////////
// FTraceStackChildItem

class FTraceStackChildItem : public FDebugLineItem
{
protected:
	int32 StackIndex;
public:
	FTraceStackChildItem(int32 InStackIndex, TSharedPtr<SSearchBox> InSearchBox)
		: FDebugLineItem(DLT_TraceStackChild, MoveTemp(InSearchBox))
	{
		StackIndex = InStackIndex;
	}
protected:
	virtual bool Compare(const FDebugLineItem* BaseOther) const override
	{
		check(false);
		return false;
	}

	virtual FDebugLineItem* Duplicate() const override
	{
		check(false);
		return nullptr;
	}

	UEdGraphNode* GetNode() const
	{
		const TSimpleRingBuffer<FKismetTraceSample>& TraceStack = FKismetDebugUtilities::GetTraceStack();
		if (StackIndex < TraceStack.Num())
		{
			const FKismetTraceSample& Sample = TraceStack(StackIndex);
			UObject* ObjectContext = Sample.Context.Get();

			FString ContextName = (ObjectContext != nullptr) ? ObjectContext->GetName() : LOCTEXT("ObjectDoesNotExist", "(object no longer exists)").ToString();
			FString NodeName = TEXT(" ");

			if (ObjectContext != nullptr)
			{
				// Try to find the node that got executed
				UEdGraphNode* Node = FKismetDebugUtilities::FindSourceNodeForCodeLocation(ObjectContext, Sample.Function.Get(), Sample.Offset);
				return Node;
			}
		}

		return nullptr;
	}

	virtual FText GetDisplayName() const override
	{
		UEdGraphNode* Node = GetNode();
		if (Node != nullptr)
		{
			return Node->GetNodeTitle(ENodeTitleType::ListView);
		}
		else
		{
			return LOCTEXT("Unknown", "(unknown)");
		}
	}

	virtual TSharedRef<SWidget> GenerateNameWidget() override
	{
		return SNew(PropertyInfoViewStyle::STextHighlightOverlay)
			.FullText(this, &FTraceStackChildItem::GetDisplayName)
			.HighlightText(SearchBox.Get(), &SSearchBox::GetText)
			[
				SNew(SHyperlink)
					.Text(this, &FTraceStackChildItem::GetDisplayName)
					.Style(FEditorStyle::Get(), "HoverOnlyHyperlink")
					.ToolTipText(LOCTEXT("NavigateToDebugTraceLocationHyperlink_ToolTip", "Navigate to the trace location"))
					.OnNavigate(this, &FTraceStackChildItem::OnNavigateToNode)
			];
	}

	virtual TSharedRef<SWidget> GetNameIcon() override
	{
		return SNew(SImage)
			.Image( FEditorStyle::GetBrush(
				(StackIndex > 0) ?
				TEXT("Kismet.Trace.PreviousIndex") :
				TEXT("Kismet.Trace.CurrentIndex"))
			);
	}

	// Visit time and actor name
	virtual TSharedRef<SWidget> GenerateValueWidget() override
	{
		return SNew(PropertyInfoViewStyle::STextHighlightOverlay)
			.FullText(this, &FTraceStackChildItem::GetDescription)
			.HighlightText(SearchBox.Get(), &SSearchBox::GetText)
			[
				SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SHyperlink)
							.Text(this, &FTraceStackChildItem::GetContextObjectName)
							.Style(FEditorStyle::Get(), "HoverOnlyHyperlink")
							.ToolTipText( LOCTEXT("SelectActor_Tooltip", "Select this actor") )
							.OnNavigate(this, &FTraceStackChildItem::OnSelectContextObject)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
							.Text(this, &FTraceStackChildItem::GetVisitTime)
					]
			];
	}

	FText GetVisitTime() const
	{
		const TSimpleRingBuffer<FKismetTraceSample>& TraceStack = FKismetDebugUtilities::GetTraceStack();
		if (StackIndex < TraceStack.Num())
		{
			static const FNumberFormattingOptions TimeFormatOptions = FNumberFormattingOptions()
				.SetMinimumFractionalDigits(2)
				.SetMaximumFractionalDigits(2);
			return FText::Format(LOCTEXT("VisitTimeFmt", " @ {0} s"), FText::AsNumber(TraceStack(StackIndex).ObservationTime - GStartTime, &TimeFormatOptions));
		}

		return FText::GetEmpty();
	}

	FText GetContextObjectName() const
	{
		const TSimpleRingBuffer<FKismetTraceSample>& TraceStack = FKismetDebugUtilities::GetTraceStack();

		UObject* ObjectContext = (StackIndex < TraceStack.Num()) ? TraceStack(StackIndex).Context.Get() : nullptr;
		
		return (ObjectContext != nullptr) ? FText::FromString(ObjectContext->GetName()) : LOCTEXT("ObjectDoesNotExist", "(object no longer exists)");
	}

	virtual FText GetDescription() const override
	{
		return FText::FromString(GetContextObjectName().ToString() + GetVisitTime().ToString());
	}

	void OnNavigateToNode()
	{
		if (UEdGraphNode* Node = GetNode())
		{
			FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Node);
		}		
	}

	void OnSelectContextObject()
	{
		const TSimpleRingBuffer<FKismetTraceSample>& TraceStack = FKismetDebugUtilities::GetTraceStack();

		UObject* ObjectContext = (StackIndex < TraceStack.Num()) ? TraceStack(StackIndex).Context.Get() : nullptr;

		// Add the object to the selection set
		if (AActor* Actor = Cast<AActor>(ObjectContext))
		{
			GEditor->SelectActor(Actor, true, true, true);
		}
		else
		{
			UE_LOG(LogBlueprintDebuggingView, Warning, TEXT("Cannot select the non-actor object '%s'"), (ObjectContext != nullptr) ? *ObjectContext->GetName() : TEXT("(nullptr)") );
		}
	}

};

//////////////////////////////////////////////////////////////////////////
// FTraceStackParentItem

class FTraceStackParentItem : public FLineItemWithChildren
{
public:
	FTraceStackParentItem(TSharedPtr<SSearchBox> InSearchBox)
		: FLineItemWithChildren(DLT_TraceStackParent, MoveTemp(InSearchBox))
	{
	}

	virtual void GatherChildren(TArray<FDebugTreeItemPtr>& OutChildren, bool bRespectSearch) override
	{
		// update search flags to match that of a root node
		UpdateSearchFlags(/* bIsRootNode */ true);
		
		const TSimpleRingBuffer<FKismetTraceSample>& TraceStack = FKismetDebugUtilities::GetTraceStack();
		const int32 NumVisible = TraceStack.Num();

		// Create any new stack entries that are needed
		for (int32 i = ChildrenMirrors.Num(); i < NumVisible; ++i)
		{
			ChildrenMirrors.Add(MakeShareable( new FTraceStackChildItem(i, SearchBox) ));
		}

		// Add the visible stack entries as children
		for (int32 i = 0; i < NumVisible; ++i)
		{
			OutChildren.Add(ChildrenMirrors[i]);
		}
	}

protected:
	virtual FText GetDisplayName() const override
	{
		return LOCTEXT("ExecutionTrace", "Execution Trace");
	}

	virtual bool Compare(const FDebugLineItem* BaseOther) const override
	{
		check(false);
		return false;
	}

	virtual FDebugLineItem* Duplicate() const override
	{
		check(false);
		return nullptr;
	}
};

//////////////////////////////////////////////////////////////////////////
// SDebugLineItem

class SDebugLineItem : public SMultiColumnTableRow< FDebugTreeItemPtr >
{
protected:
	FDebugTreeItemPtr ItemToEdit;
public:
	SLATE_BEGIN_ARGS(SDebugLineItem){}
	SLATE_END_ARGS()

	virtual TSharedRef<SWidget> GenerateWidgetForColumn( const FName& ColumnName ) override
	{
		TSharedPtr<SWidget> ColumnContent = nullptr;
		if (ColumnName == KismetDebugViewConstants::ColumnId_Name)
		{
			SAssignNew(ColumnContent, SHorizontalBox)
				+SHorizontalBox::Slot()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Fill)
					.AutoWidth()
					[
						SNew( PropertyInfoViewStyle::SIndent, SharedThis(this) )
					]
				+ SHorizontalBox::Slot()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					.AutoWidth()
					[
						SNew(PropertyInfoViewStyle::SExpanderArrow, SharedThis(this))
							.HasChildren_Lambda([ItemToEdit = ItemToEdit]()
							{
								const bool HasChildren = ItemToEdit->HasChildren();
								return HasChildren;
							})
					]
	            + SHorizontalBox::Slot()
	                .AutoWidth()
	                .HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
	                [
	                    ItemToEdit->GetNameIcon()
	                ]
	                + SHorizontalBox::Slot()
	                .AutoWidth()
	                .HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
	                .Padding(5.f,0.f,0.f,0.f)
	                [
	                    ItemToEdit->GenerateNameWidget()
	                ];
		}
		else if (ColumnName == KismetDebugViewConstants::ColumnId_Value)
		{
			SAssignNew(ColumnContent, SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					ItemToEdit->GetValueIcon()
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				.Padding(.5f, 1.f)
				[
					ItemToEdit->GenerateValueWidget()
				];
		}
		else
		{
			SAssignNew(ColumnContent, STextBlock)
				.Text( LOCTEXT("Error", "Error") );
		}
		
		return SNew(SBox)
			.Padding(FMargin(0.5f, 0.5f))
			[
				SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryMiddle"))
					.BorderBackgroundColor_Static(
						PropertyInfoViewStyle::GetRowBackgroundColor,
						static_cast<ITableRow*>(this)
					)
					[
						ColumnContent.ToSharedRef()
					]
			];
	}

	void Construct(const FArguments& InArgs, TSharedRef<STableViewBase> OwnerTableView, FDebugTreeItemPtr InItemToEdit)
	{
		ItemToEdit = InItemToEdit;
		SMultiColumnTableRow<FDebugTreeItemPtr>::Construct( FSuperRowType::FArguments(), OwnerTableView );	
	}
};


//////////////////////////////////////////////////////////////////////////
// SKismetDebuggingView

TWeakObjectPtr<const UObject> SKismetDebuggingView::CurrentActiveObject = nullptr;

TSharedRef<ITableRow> SKismetDebuggingView::OnGenerateRowForWatchTree(FDebugTreeItemPtr InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew( SDebugLineItem, OwnerTable, InItem );
}

void SKismetDebuggingView::OnGetChildrenForWatchTree(FDebugTreeItemPtr InParent, TArray<FDebugTreeItemPtr>& OutChildren)
{
	InParent->GatherChildren(OutChildren);
}

TSharedRef<SHorizontalBox> SKismetDebuggingView::GetDebugLineTypeToggle(FDebugLineItem::EDebugLineType Type, const FText& Text)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SCheckBox)
				.IsChecked(true)
				.OnCheckStateChanged_Static(&FDebugLineItem::OnDebugLineTypeActiveChanged, Type)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4.0f, 0.0f, 10.0f, 0.0f)
		.VAlign(VAlign_Center)
		[
			SNew( STextBlock )
				.Text(Text)
		];
}

TSharedPtr<SWidget> SKismetDebuggingView::OnMakeContextMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.BeginSection("DebugActions", LOCTEXT("DebugActionsMenuHeading", "Debug Actions"));
	{
		const TArray<FDebugTreeItemPtr> SelectionList = DebugTreeView->GetSelectedItems();

		for (int32 SelIndex = 0; SelIndex < SelectionList.Num(); ++SelIndex)
		{
			FDebugTreeItemPtr Ptr = SelectionList[SelIndex];
			Ptr->MakeMenu(MenuBuilder);
		}
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SKismetDebuggingView::OnSearchTextCommitted(const FText& Text, ETextCommit::Type Type)
{
	if(Type == ETextCommit::OnEnter)
	{
		DebugTreeView->ClearExpandedItems();
		OtherTreeView->ClearExpandedItems();
	}
}


FText SKismetDebuggingView::GetTabLabel() const
{
	return BlueprintToWatchPtr.IsValid() ?
		FText::FromString(BlueprintToWatchPtr->GetName()) :
		NSLOCTEXT("BlueprintExecutionFlow", "TabTitle", "Data Flow");
}

FText SKismetDebuggingView::GetTopText() const
{
	return LOCTEXT("ShowDebugForActors", "Showing debug info for instances of the blueprint:");
}

bool SKismetDebuggingView::CanDisableAllBreakpoints() const
{
	if(BlueprintToWatchPtr.IsValid())
	{
		return FKismetDebugUtilities::BlueprintHasBreakpoints(BlueprintToWatchPtr.Get());
	}
	return false;
}

FReply SKismetDebuggingView::OnDisableAllBreakpointsClicked()
{
	if(BlueprintToWatchPtr.IsValid())
	{
		FDebuggingActionCallbacks::SetEnabledOnAllBreakpoints(BlueprintToWatchPtr.Get(), false);
	}
	
	return FReply::Handled();
}

class FBlueprintFilter : public IClassViewerFilter
{
public:
	FBlueprintFilter() = default;

	virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs ) override
	{
		return InClass && !InClass->HasAnyClassFlags(CLASS_Deprecated) &&
				InClass->HasAllClassFlags(CLASS_CompiledFromBlueprint);
	}

	virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
	{
		return !InUnloadedClassData->HasAnyClassFlags(CLASS_Deprecated) &&
				InUnloadedClassData->HasAllClassFlags(CLASS_CompiledFromBlueprint);
	}
};

void SKismetDebuggingView::OnBlueprintClassPicked(UClass* PickedClass)
{
	BlueprintToWatchPtr = Cast<UBlueprint>(PickedClass->ClassGeneratedBy);
	BreakpointParentItem->Blueprint = BlueprintToWatchPtr;
	DebugClassComboButton->SetIsOpen(false);
}

TSharedRef<SWidget> SKismetDebuggingView::ConstructBlueprintClassPicker()
{
	FClassViewerInitializationOptions Options;
	Options.Mode = EClassViewerMode::ClassPicker;
	Options.bShowBackgroundBorder = false;
	Options.ClassFilters.Add(MakeShared<FBlueprintFilter>());
	Options.bIsBlueprintBaseOnly = true;
	Options.bShowUnloadedBlueprints = false;
	
	FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");

	FOnClassPicked OnClassPicked;
	OnClassPicked.BindRaw(this, &SKismetDebuggingView::OnBlueprintClassPicked);
	return SNew(SBox)
		.HeightOverride(500.f)
		[
			ClassViewerModule.CreateClassViewer(Options, OnClassPicked)
		];
}

void SKismetDebuggingView::Construct(const FArguments& InArgs)
{	
	BlueprintToWatchPtr = InArgs._BlueprintToWatch;

	// Build the debug toolbar
	static const FName ToolbarName = "Kismet.DebuggingViewToolBar";
	if (!UToolMenus::Get()->IsMenuRegistered(ToolbarName))
	{
		UToolMenu* ToolBar = UToolMenus::Get()->RegisterMenu(ToolbarName, NAME_None, EMultiBoxType::SlimHorizontalToolBar);

		{
			FToolMenuSection& Section = ToolBar->AddSection("Debug");
			FPlayWorldCommands::BuildToolbar(Section);
		}
	}

	FToolMenuContext MenuContext(FPlayWorldCommands::GlobalPlayWorldActions);
	TSharedRef<SWidget> ToolbarWidget = UToolMenus::Get()->GenerateWidget(ToolbarName, MenuContext);
	
	DebugClassComboButton =
		SNew(SComboButton)
			.OnGetMenuContent_Raw(this, &SKismetDebuggingView::ConstructBlueprintClassPicker)
			.ButtonContent()
			[
				SNew(STextBlock)
					.Text_Lambda([&BlueprintToWatchPtr = BlueprintToWatchPtr]()
					{
						return BlueprintToWatchPtr.IsValid()?
							FText::FromString(BlueprintToWatchPtr->GetName()) :
							LOCTEXT("SelectBlueprint", "Select Blueprint");
					})
			];

	FBlueprintContextTracker::OnEnterScriptContext.AddLambda(
		[](const FBlueprintContextTracker& ContextTracker, const UObject* ContextObject, const UFunction* ContextFunction)
		{
			CurrentActiveObject = ContextObject;
		}
	);
	
	FBlueprintContextTracker::OnExitScriptContext.AddLambda(
		[](const FBlueprintContextTracker& ContextTracker)
		{
			CurrentActiveObject = nullptr;
		}
	);

	
	this->ChildSlot
	[
		SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
					.BorderImage( FEditorStyle::GetBrush( TEXT("NoBorder") ) )
					[
						ToolbarWidget
					]
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				
				SNew( SVerticalBox )
					+SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew( STextBlock )
							.Text( this, &SKismetDebuggingView::GetTopText )
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew( SHorizontalBox )
							+ SHorizontalBox::Slot()
							.HAlign( HAlign_Left )
							[
								SNew(SBox)
									.WidthOverride(400.f)
									[
										DebugClassComboButton.ToSharedRef()
									]
							]
							+ SHorizontalBox::Slot()
							.HAlign( HAlign_Right )
							[
								SNew( SButton )
					                .IsEnabled( this, &SKismetDebuggingView::CanDisableAllBreakpoints )
					                .Text( LOCTEXT( "DisableAllBreakPoints", "Disable All Breakpoints" ) )
					                .OnClicked( this, &SKismetDebuggingView::OnDisableAllBreakpointsClicked )
							]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SAssignNew(SearchBox, SSearchBox)
						.OnTextCommitted(this, &SKismetDebuggingView::OnSearchTextCommitted)
					]
			]
			+SVerticalBox::Slot()
			[
				SNew(SSplitter)
					.Orientation(Orient_Vertical)
					+SSplitter::Slot()
					[
						SAssignNew( DebugTreeView, STreeView< FDebugTreeItemPtr > )
							.TreeItemsSource( &RootTreeItems )
							.SelectionMode( ESelectionMode::Single )
							.OnGetChildren( this, &SKismetDebuggingView::OnGetChildrenForWatchTree )
							.OnGenerateRow( this, &SKismetDebuggingView::OnGenerateRowForWatchTree ) 
							.OnContextMenuOpening( this, &SKismetDebuggingView::OnMakeContextMenu )
							.TreeViewStyle(&FAppStyle::Get().GetWidgetStyle<FTableViewStyle>("PropertyTable.InViewport.ListView"))
							.HeaderRow
							(
								SNew(SHeaderRow)
									+ SHeaderRow::Column(KismetDebugViewConstants::ColumnId_Name)
									.DefaultLabel(KismetDebugViewConstants::ColumnText_Name)
									+ SHeaderRow::Column(KismetDebugViewConstants::ColumnId_Value)
									.DefaultLabel(KismetDebugViewConstants::ColumnText_Value)
							)
					]
					+SSplitter::Slot()
					[
						SAssignNew( OtherTreeView, STreeView< FDebugTreeItemPtr > )
							.TreeItemsSource( &OtherTreeItems )
							.SelectionMode( ESelectionMode::Single )
							.OnGetChildren( this, &SKismetDebuggingView::OnGetChildrenForWatchTree )
							.OnGenerateRow( this, &SKismetDebuggingView::OnGenerateRowForWatchTree ) 
							.OnContextMenuOpening( this, &SKismetDebuggingView::OnMakeContextMenu )
							.TreeViewStyle(&FAppStyle::Get().GetWidgetStyle<FTableViewStyle>("PropertyTable.InViewport.ListView"))
							.HeaderRow
							(
								SNew(SHeaderRow)
									+ SHeaderRow::Column(KismetDebugViewConstants::ColumnId_Name)
									.DefaultLabel(KismetDebugViewConstants::ColumnText_DebugKey)
									+ SHeaderRow::Column(KismetDebugViewConstants::ColumnId_Value)
									.DefaultLabel(KismetDebugViewConstants::ColumnText_Info)
							)
					]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						GetDebugLineTypeToggle(FDebugLineItem::DLT_Watch, LOCTEXT("Watchpoints", "Watchpoints"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						GetDebugLineTypeToggle(FDebugLineItem::DLT_LatentAction, LOCTEXT("LatentActions", "Latent Actions"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						GetDebugLineTypeToggle(FDebugLineItem::DLT_BreakpointParent, LOCTEXT("Breakpoints", "Breakpoints"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						GetDebugLineTypeToggle(FDebugLineItem::DLT_TraceStackParent, LOCTEXT("ExecutionTrace", "Execution Trace"))
					]
			]
	];

	TraceStackItem = MakeShareable(new FTraceStackParentItem(SearchBox));
	BreakpointParentItem = MakeShareable(new FBreakpointParentItem(BlueprintToWatchPtr, SearchBox));
}

void SKismetDebuggingView::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	// Gather the old root set
	TSet<UObject*> OldRootSet;
	for (int32 i = 0; i < RootTreeItems.Num(); ++i)
	{
		if (UObject* OldObject = RootTreeItems[i]->GetParentObject())
		{
			OldRootSet.Add(OldObject);
		}
	}

	// Gather what we'd like to be the new root set
	const bool bIsDebugging = GEditor->PlayWorld != nullptr;

	TSet<UObject*> NewRootSet;
	
	if(bIsDebugging && BlueprintToWatchPtr.IsValid())
	{
		UClass* GeneratedClass = Cast<UClass>(BlueprintToWatchPtr->GeneratedClass);
		for(FThreadSafeObjectIterator Iter(GeneratedClass); Iter; ++Iter)
        {
            UObject* Instance = *Iter;
			if(!Instance)
			{
				continue;
			}

			// only include non temporary, non archetype objects
            if(Instance->HasAnyFlags(RF_ArchetypeObject | RF_Transient))
            {
                continue;
            }
			
			// only include actors in current world
            if(AActor* Actor = Cast<AActor>(Instance))
            {
                if(!GEditor->PlayWorld->ContainsActor(Actor))
                {
                    continue;
                }
            }
            
            NewRootSet.Add(Instance);
        }
	}

	auto TryAddToRoot = [](const TSharedPtr<FDebugLineItem>& Item, TArray<FDebugTreeItemPtr>& Root,
		 const FText& SearchText, const TSharedPtr<STreeView<FDebugTreeItemPtr>> &Tree)
	{
		if(SearchText.IsEmpty())
		{
			Root.Add(Item);
		}
		else
		{
			if(Item->CanHaveChildren())
			{
				TArray<FLineItemWithChildren*> StackOverflowAvoidence;
				FLineItemWithChildren* ItemWithChildren = StaticCast<FLineItemWithChildren*>(Item.Get());
				if(ItemWithChildren->SearchRecursive(StackOverflowAvoidence, Tree))
                {
                	Root.Add(Item);
                }
			}
			else
			{
				Item->UpdateSearchFlags();
				if(Item->IsVisible())
				{
                	Root.Add(Item);
				}
			}
		}
	};

	// This will pull anything out of Old that is also New (sticking around), so afterwards Old is a list of things to remove
	RootTreeItems.Empty();
	for (TSet<UObject*>::TIterator NewIter(NewRootSet); NewIter; ++NewIter)
	{
		TWeakObjectPtr<UObject> ObjectToAdd = *NewIter;

		// destroyed objects can still appear if they haven't ben GCed yet.
		// weak object pointers will detect it and return nullptr
		if(!ObjectToAdd.Get())
		{
			continue;
		}

		if (OldRootSet.Contains(ObjectToAdd.Get()))
		{
			OldRootSet.Remove(ObjectToAdd.Get());
			
			const TSharedPtr<FDebugLineItem>& Item = ObjectToTreeItemMap.FindChecked(ObjectToAdd.Get());
			TryAddToRoot(Item, RootTreeItems, SearchBox->GetText(), DebugTreeView);
		}
		else
		{
			FDebugTreeItemPtr NewPtr = FDebugTreeItemPtr(new FParentLineItem(ObjectToAdd.Get(), SearchBox));
			ObjectToTreeItemMap.Add(ObjectToAdd.Get(), NewPtr);
			TryAddToRoot(NewPtr, RootTreeItems, SearchBox->GetText(), DebugTreeView);
		}
	}

	// Remove the old root set items that didn't get used again
	for (TSet<UObject*>::TIterator DeadIter(OldRootSet); DeadIter; ++DeadIter)
	{
		UObject* ObjectToRemove = *DeadIter;
		ObjectToTreeItemMap.Remove(ObjectToRemove);
	}

	// Add a message if there are no active instances of DebugClass
	if (RootTreeItems.Num() == 0)
	{
		RootTreeItems.Add(MakeShareable(new FMessageLineItem(
			bIsDebugging ?
				LOCTEXT("NoInstances", "No instances of this blueprint in existence").ToString() :
				LOCTEXT("NoPIEorSIE", "run PIE or SIE to see instance debug info").ToString(),
			SearchBox
		)));
	}

	// Refresh the list
	DebugTreeView->RequestTreeRefresh();

	OtherTreeItems.Empty();

	// Show Breakpoints
	if(FDebugLineItem::IsDebugLineTypeActive(FDebugLineItem::DLT_BreakpointParent))
	{
		TryAddToRoot(BreakpointParentItem, OtherTreeItems, SearchBox->GetText(), OtherTreeView);
	}

	// Show the trace stack when debugging
	if (bIsDebugging && FDebugLineItem::IsDebugLineTypeActive(FDebugLineItem::DLT_TraceStackParent))
	{
		TryAddToRoot(TraceStackItem, OtherTreeItems, SearchBox->GetText(), OtherTreeView);
	}
	OtherTreeView->RequestTreeRefresh();
}


#undef LOCTEXT_NAMESPACE
