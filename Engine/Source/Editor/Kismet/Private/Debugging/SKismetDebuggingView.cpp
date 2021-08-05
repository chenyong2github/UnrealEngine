// Copyright Epic Games, Inc. All Rights Reserved.


#include "Debugging/SKismetDebuggingView.h"

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
	return SNew(STextBlock)
		.Text(this, &FDebugLineItem::GetDisplayName);
}

TSharedRef<SWidget> FDebugLineItem::GenerateValueWidget()
{
	return SNew(STextBlock)
		.Text(this, &FDebugLineItem::GetDescription);
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

	if(UClass *ParentClass = ParentObject->GetClass())
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

class ILineItemWithChildren
{
public:
	virtual ~ILineItemWithChildren() = default;
	
protected:
	// List of children 
	TArray<FDebugTreeItemPtr> ChildrenMirrors;
	
	/**
	 * Adds either Item or an identical node that was previously
	 * created (present in ChildrenMirrors) as a child to OutChildren.
	 *
	 * O( # Children )
	 */
	void EnsureChildIsAdded(TArray<FDebugTreeItemPtr>& OutChildren, const FDebugLineItem& Item)
	{
		for (int32 i = 0; i < ChildrenMirrors.Num(); ++i)
		{
			TSharedPtr< FDebugLineItem > MirrorItem = ChildrenMirrors[i];

			if (MirrorItem->Type == Item.Type)
			{
				if (Item.Compare(MirrorItem.Get()))
				{
					OutChildren.Add(MirrorItem);
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
	FMessageLineItem(const FString& InMessage)
		: FDebugLineItem(DLT_Message)
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
		return new FMessageLineItem(Message);
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
	FLatentActionLineItem(int32 InUUID, UObject* ParentObject)
		: FDebugLineItem(DLT_LatentAction)
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
		return new FLatentActionLineItem(UUID, ParentObjectRef.Get());
	}
protected:
	virtual TSharedRef<SWidget> GenerateNameWidget() override;
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
	return
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SImage)
			. Image(FEditorStyle::GetBrush(TEXT("Kismet.LatentActionIcon")))
		]
		+SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.AutoWidth()
		[
				SNew(SHyperlink)
			.Style(FEditorStyle::Get(), "HoverOnlyHyperlink")
			.OnNavigate(this, &FLatentActionLineItem::OnNavigateToLatentNode)
			.Text(this, &FLatentActionLineItem::GetDisplayName)
			.ToolTipText( LOCTEXT("NavLatentActionLoc_Tooltip", "Navigate to the latent action location") )
		];
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

struct FWatchChildLineItem : public FDebugLineItem, ILineItemWithChildren
{
protected:
	FDebugInfo Data;
public:
	FWatchChildLineItem(FDebugInfo Child) :
		FDebugLineItem(DLT_WatchChild),
		Data(Child)
	{}

	virtual bool Compare(const FDebugLineItem* BaseOther) const override
	{
		FWatchChildLineItem* Other = (FWatchChildLineItem*)BaseOther;
		return
			Data.DisplayName.CompareTo(Other->Data.DisplayName) == 0 &&
			Data.Value.CompareTo(Other->Data.Value) == 0;
	}

	virtual FDebugLineItem* Duplicate() const override
	{
		return new FWatchChildLineItem(Data);
	}
	
	virtual FText GetDescription() const override
	{
		FString ValStr = Data.Value.ToString();
		if(int32 idx; ValStr.FindChar('\n', idx))
		{
			if(ValStr.Len() < 60)
			{
				return FText::FromString(ValStr.Replace(TEXT("\n") ,TEXT("")));
			}
			return FText::Format(LOCTEXT("BracketedDatatype","[{0}]"), Data.Type);
		}
		return Data.Value;
	}
	
	virtual FText GetDisplayName() const override
	{
		return Data.DisplayName;
	}

	virtual void GatherChildren(TArray<FDebugTreeItemPtr>& OutChildren) override
	{
		for (FDebugInfo& ChildData : Data.Children)
		{
			EnsureChildIsAdded(OutChildren, FWatchChildLineItem(ChildData));
		}
	}
};

//////////////////////////////////////////////////////////////////////////
// FWatchLineItem 


struct FWatchLineItem : public FDebugLineItem, ILineItemWithChildren
{
protected:
	TWeakObjectPtr< UObject > ParentObjectRef;
	FEdGraphPinReference ObjectRef;
	
	// List of children 
	TArray<FDebugTreeItemPtr> ChildrenMirrors;
public:
	FWatchLineItem(UEdGraphPin* PinToWatch, UObject* ParentObject)
		: FDebugLineItem(DLT_Watch)
	{
		ObjectRef = PinToWatch;
		ParentObjectRef = ParentObject;
	}

	virtual bool Compare(const FDebugLineItem* BaseOther) const override
	{
		FWatchLineItem* Other = (FWatchLineItem*)BaseOther;
		return (ParentObjectRef.Get() == Other->ParentObjectRef.Get()) &&
			(ObjectRef.Get() == Other->ObjectRef.Get());
	}

	virtual FDebugLineItem* Duplicate() const override
	{
		return new FWatchLineItem(ObjectRef.Get(), ParentObjectRef.Get());
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
	
	virtual void GatherChildren(TArray<FDebugTreeItemPtr>& OutChildren) override
	{
		if (UEdGraphPin* PinToWatch = ObjectRef.Get())
		{
			// Try to determine the blueprint that generated the watch
			UBlueprint* ParentBlueprint = GetBlueprintForObject(ParentObjectRef.Get());

			// Find a valid property mapping and display the current value
			UObject* ParentObject = ParentObjectRef.Get();
			if ((ParentBlueprint != ParentObject) && (ParentBlueprint != nullptr))
			{
				FDebugInfo DebugInfo;
				const FKismetDebugUtilities::EWatchTextResult WatchStatus = FKismetDebugUtilities::GetDebugInfo(DebugInfo, ParentBlueprint, ParentObject, PinToWatch);

				if(WatchStatus == FKismetDebugUtilities::EWTR_Valid)
				{
					for (FDebugInfo& ChildData : DebugInfo.Children)
					{
						EnsureChildIsAdded(OutChildren, FWatchChildLineItem(ChildData));
					}
				}
			}
		}
	}
	

protected:
	virtual FText GetDescription() const override;
	virtual FText GetDisplayName() const override;
	virtual TSharedRef<SWidget> GenerateNameWidget() override;

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
			
			FDebugInfo DebugInfo;
			const FKismetDebugUtilities::EWatchTextResult WatchStatus = FKismetDebugUtilities::GetDebugInfo(DebugInfo, ParentBlueprint, ParentObject, PinToWatch);

			switch (WatchStatus)
			{
			case FKismetDebugUtilities::EWTR_Valid:
			{
				int32 Index = INDEX_NONE;
				if(DebugInfo.Value.ToString().FindChar('\n', Index))
				{
					return DebugInfo.Type;
				}
				return DebugInfo.Value;
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
	return SNew(SHorizontalBox)
		
		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SImage)
			.Image(FEditorStyle::GetBrush(TEXT("Kismet.WatchIcon")))
		]
		+SHorizontalBox::Slot()
		.HAlign(HAlign_Left)
		.AutoWidth()
		[
				SNew(SHyperlink)
			.Style(FEditorStyle::Get(), "HoverOnlyHyperlink")
			.OnNavigate(this, &FWatchLineItem::OnNavigateToWatchLocation)
			.Text(this, &FWatchLineItem::GetDisplayName)
			.ToolTipText( LOCTEXT("NavWatchLoc", "Navigate to the watch location") )
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
	FBreakpointLineItem(TSoftObjectPtr<UEdGraphNode> BreakpointToWatch, UObject* ParentObject)
		: FDebugLineItem(DLT_Breakpoint)
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
		return new FBreakpointLineItem(BreakpointNode, ParentObjectRef.Get());
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
		return SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				. OnClicked(this, &FBreakpointLineItem::OnUserToggledEnabled)
				. ToolTipText(LOCTEXT("ToggleBreakpointButton_ToolTip", "Toggle this breakpoint"))
				. ButtonStyle( FEditorStyle::Get(), "NoBorder" )
				. ContentPadding(0.0f)
				[
					SNew(SImage)
					. Image(this, &FBreakpointLineItem::GetStatusImage)
					. ToolTipText(this, &FBreakpointLineItem::GetStatusTooltip)
				]
			]

		+ SHorizontalBox::Slot()
			.AutoWidth()
			. VAlign(VAlign_Center)
			[
				SNew(SHyperlink)
				. Style(FEditorStyle::Get(), "HoverOnlyHyperlink")
				. Text(this, &FBreakpointLineItem::GetLocationDescription)
				. ToolTipText( LOCTEXT("NavBreakpointLoc", "Navigate to the breakpoint location") )
				. OnNavigate(this, &FBreakpointLineItem::OnNavigateToBreakpointLocation)
			];
	}
	
	
	FText GetLocationDescription() const;
	FReply OnUserToggledEnabled();

	void OnNavigateToBreakpointLocation();

	const FSlateBrush* GetStatusImage() const;
	FText GetStatusTooltip() const;
};

FText FBreakpointLineItem::GetLocationDescription() const
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

class FBreakpointParentItem : public FDebugLineItem, ILineItemWithChildren
{
protected:

	// List of children 
	TArray<FDebugTreeItemPtr> ChildrenMirrors;
public:
	// The parent object
	TWeakObjectPtr<UBlueprint> Blueprint;
	
	FBreakpointParentItem(TWeakObjectPtr<UBlueprint> Blueprint)
		: FDebugLineItem(DLT_TraceStackParent),
		  Blueprint(Blueprint)
	{
	}

	virtual void GatherChildren(TArray<FDebugTreeItemPtr>& OutChildren) override
	{
		if(!Blueprint.IsValid())
		{
			return;
		}
		
		// Create children for each breakpoint
		FKismetDebugUtilities::ForeachBreakpoint(
			Blueprint.Get(),
			[this, &OutChildren]
			(FBlueprintBreakpoint& Breakpoint)
			{
				EnsureChildIsAdded(OutChildren, FBreakpointLineItem(Breakpoint.GetLocation(), Blueprint.Get()));
			}
		);

		// Make sure there is something there, to let the user know if there is nothing
		if (OutChildren.Num() == 0)
		{
			EnsureChildIsAdded(OutChildren, FMessageLineItem( LOCTEXT("NoBreakpoints", "No breakpoints").ToString() ));
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

class FParentLineItem : public FDebugLineItem, ILineItemWithChildren
{
protected:
	// The parent object
	TWeakObjectPtr<UObject> ObjectRef;

	// List of children 
	TArray<FDebugTreeItemPtr> ChildrenMirrors;
public:
	FParentLineItem(UObject* Object)
		: FDebugLineItem(DLT_Parent)
	{
		ObjectRef = Object;
	}

	virtual UObject* GetParentObject() override
	{
		return ObjectRef.Get();
	}

	virtual void GatherChildren(TArray<FDebugTreeItemPtr>& OutChildren) override
	{
		if (UObject* ParentObject = ObjectRef.Get())
		{
			UBlueprint* ParentBP = FDebugLineItem::GetBlueprintForObject(ParentObject);
			if ((ParentBP != nullptr) && (ParentBP == ParentObject))
			{
				// Create children for each watch
				if(IsDebugLineTypeActive(DLT_Watch))
				{
					for (FEdGraphPinReference WatchedPin : ParentBP->WatchedPins)
                    {
                    	EnsureChildIsAdded(OutChildren, FWatchLineItem(WatchedPin.Get(), ParentObject));
                    }
				}

				// Make sure there is something there, to let the user know if there is nothing
				if (OutChildren.Num() == 0)
				{
					EnsureChildIsAdded(OutChildren, FMessageLineItem( LOCTEXT("NoWatches", "No watches").ToString() ));
				}
			}
			else
			{
				if (ParentBP != nullptr)
				{
					// Create children for each watch
					if(IsDebugLineTypeActive(DLT_Watch))
					{
						for (FEdGraphPinReference WatchedPin : ParentBP->WatchedPins)
						{
							EnsureChildIsAdded(OutChildren, FWatchLineItem(WatchedPin.Get(), ParentObject));
						}
					}
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
							EnsureChildIsAdded(OutChildren, FLatentActionLineItem(UUID, ParentObject));
						}
					}
				}

				// Make sure there is something there, to let the user know if there is nothing
				if (OutChildren.Num() == 0)
				{
					EnsureChildIsAdded(OutChildren, FMessageLineItem( LOCTEXT("NoDebugInfo", "No debugging info").ToString() ));
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
		return FSlateColor::UseForeground();
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
		return new FParentLineItem(ObjectRef.Get());
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

	virtual TSharedRef<SWidget> GenerateNameWidget() override
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SImage)
				.Image(this, &FParentLineItem::GetStatusImage)
				.ColorAndOpacity_Raw(this, &FParentLineItem::GetStatusColor)
				.ToolTipText(this, &FParentLineItem::GetStatusTooltip)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			. VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &FParentLineItem::GetDisplayName)
			];
	}

	virtual void MakeMenu(class FMenuBuilder& MenuBuilder) override
	{
		if (UBlueprint* BP = Cast<UBlueprint>(ObjectRef.Get()))
		{
			if (BP->WatchedPins.Num() > 0)
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
	FTraceStackChildItem(int32 InStackIndex)
		: FDebugLineItem(DLT_TraceStackChild)
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

	// Index icon and node name
	virtual TSharedRef<SWidget> GenerateNameWidget() override
	{
		return SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SImage)
				. Image( FEditorStyle::GetBrush((StackIndex > 0) ? TEXT("Kismet.Trace.PreviousIndex") : TEXT("Kismet.Trace.CurrentIndex")) )
			]

		+ SHorizontalBox::Slot()
			.AutoWidth()
			. VAlign(VAlign_Center)
			[
				SNew(SHyperlink)
				. Text(this, &FTraceStackChildItem::GetDisplayName)
				. Style(FEditorStyle::Get(), "HoverOnlyHyperlink")
				. ToolTipText(LOCTEXT("NavigateToDebugTraceLocationHyperlink_ToolTip", "Navigate to the trace location"))
				. OnNavigate(this, &FTraceStackChildItem::OnNavigateToNode)
			];
	}

	// Visit time and actor name
	virtual TSharedRef<SWidget> GenerateValueWidget() override
	{
		return SNew(SHorizontalBox)
			
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SHyperlink)
				. Text(this, &FTraceStackChildItem::GetContextObjectName)
				. Style(FEditorStyle::Get(), "HoverOnlyHyperlink")
				. ToolTipText( LOCTEXT("SelectActor_Tooltip", "Select this actor") )
				. OnNavigate(this, &FTraceStackChildItem::OnSelectContextObject)
			]
			
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(this, &FTraceStackChildItem::GetVisitTime)
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

class FTraceStackParentItem : public FDebugLineItem
{
protected:
	// List of children 
	TArray< TSharedPtr<FTraceStackChildItem> > ChildrenMirrors;
public:
	FTraceStackParentItem()
		: FDebugLineItem(DLT_TraceStackParent)
	{
	}

	virtual void GatherChildren(TArray<FDebugTreeItemPtr>& OutChildren) override
	{
		const TSimpleRingBuffer<FKismetTraceSample>& TraceStack = FKismetDebugUtilities::GetTraceStack();
		const int32 NumVisible = TraceStack.Num();

		// Create any new stack entries that are needed
		for (int32 i = ChildrenMirrors.Num(); i < NumVisible; ++i)
		{
			ChildrenMirrors.Add(MakeShareable( new FTraceStackChildItem(i) ));
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
			ColumnContent = SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew( SExpanderArrow, SharedThis(this) )
				]
			+SHorizontalBox::Slot()
				. FillWidth(1.0f)
				[
					ItemToEdit->GenerateNameWidget()
				];
		}
		else if (ColumnName == KismetDebugViewConstants::ColumnId_Value)
		{
			ColumnContent = ItemToEdit->GenerateValueWidget();
		}
		else
		{
			ColumnContent = SNew(STextBlock)
				. Text( LOCTEXT("Error", "Error") );
		}
		
		return SNew(SBox).Padding(FMargin(0.0f, 5.0f))
		[
			ColumnContent.ToSharedRef()
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
	
	FClassViewerModule &ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");

	FOnClassPicked OnClassPicked;
	OnClassPicked.BindRaw(this, &SKismetDebuggingView::OnBlueprintClassPicked);
	return
		SNew(SBox)
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
			.Text_Lambda([BlueprintToWatchPtr = BlueprintToWatchPtr]()
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
				. HeaderRow
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
				. HeaderRow
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
			+ SHorizontalBox::Slot().AutoWidth()
				[ GetDebugLineTypeToggle(FDebugLineItem::DLT_Watch, LOCTEXT("Watchpoints", "Watchpoints")) ]
			+ SHorizontalBox::Slot().AutoWidth()
				[ GetDebugLineTypeToggle(FDebugLineItem::DLT_LatentAction, LOCTEXT("LatentActions", "Latent Actions")) ]
			+ SHorizontalBox::Slot().AutoWidth()
				[ GetDebugLineTypeToggle(FDebugLineItem::DLT_BreakpointParent, LOCTEXT("Breakpoints", "Breakpoints")) ]
			+ SHorizontalBox::Slot().AutoWidth()
				[ GetDebugLineTypeToggle(FDebugLineItem::DLT_TraceStackParent, LOCTEXT("ExecutionTrace", "Execution Trace")) ]
		]
	];

	TraceStackItem = MakeShareable(new FTraceStackParentItem);
	BreakpointParentItem = MakeShareable(new FBreakpointParentItem(BlueprintToWatchPtr));
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

	// This will pull anything out of Old that is also New (sticking around), so afterwards Old is a list of things to remove
	RootTreeItems.Empty();
	for (TSet<UObject*>::TIterator NewIter(NewRootSet); NewIter; ++NewIter)
	{
		TWeakObjectPtr ObjectToAdd = *NewIter;

		// destroyed objects can still appear if they haven't ben GCed yet.
		// weak object pointers will detect it and return nullptr
		if(!ObjectToAdd.Get())
		{
			continue;
		}

		if (OldRootSet.Contains(ObjectToAdd.Get()))
		{
			OldRootSet.Remove(ObjectToAdd.Get());
			RootTreeItems.Add(ObjectToTreeItemMap.FindChecked(ObjectToAdd.Get()));
		}
		else
		{
			
			FDebugTreeItemPtr NewPtr = FDebugTreeItemPtr(new FParentLineItem(ObjectToAdd.Get()));
			ObjectToTreeItemMap.Add(ObjectToAdd.Get(), NewPtr);
			RootTreeItems.Add(NewPtr);

			// Autoexpand newly selected items
			DebugTreeView->SetItemExpansion(NewPtr, true);
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
			bIsDebugging ? LOCTEXT("NoInstances", "No instances of this blueprint in existence").ToString() : LOCTEXT("NoPIEorSIE", "run PIE or SIE to see instance debug info").ToString())));
	}

	// Refresh the list
	DebugTreeView->RequestTreeRefresh();

	OtherTreeItems.Empty();

	// Show Breakpoints
	if(FDebugLineItem::IsDebugLineTypeActive(FDebugLineItem::DLT_BreakpointParent))
	{
		OtherTreeItems.Add(BreakpointParentItem);
	}

	// Show the trace stack when debugging
	if (bIsDebugging && FDebugLineItem::IsDebugLineTypeActive(FDebugLineItem::DLT_TraceStackParent))
	{
		OtherTreeItems.Add(TraceStackItem);
	}
	OtherTreeView->RequestTreeRefresh();
}


#undef LOCTEXT_NAMESPACE
