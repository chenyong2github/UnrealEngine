// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputBehaviorSet.h"
#include "InteractiveToolActionSet.h"
#include "Shader.h"
#include "ToolContextInterfaces.h"
#include "UObject/UObjectGlobals.h"
#include "InteractiveTool.generated.h"

class UInteractiveToolManager;
class FCanvas;

/** Passed to UInteractiveTool::Shutdown to indicate how Tool should shut itself down*/
enum class EToolShutdownType
{
	/** Tool cleans up and exits. Pass this to tools that do not have Accept/Cancel options. */
	Completed = 0,
	/** Tool commits current preview to scene */
	Accept = 1,
	/** Tool discards current preview without modifying scene */
	Cancel = 2
};

/**
 * FInteractiveToolInfo provides information about a tool (name, tooltip, etc)
 */
struct INTERACTIVETOOLSFRAMEWORK_API FInteractiveToolInfo
{
	/** Name of Tool. May be FText::Empty(), but will default to Tool->GetClass()->GetDisplayNameText() in InteractiveTool constructor */
	FText ToolDisplayName = FText::GetEmpty();
};


class INTERACTIVETOOLSFRAMEWORK_API FWatchablePropertySet
{
public:
	//
	// Property watching infrastructure
	//
	class FPropertyWatcher
	{
	public:
		virtual ~FPropertyWatcher() = default;
		virtual void CheckAndUpdate() = 0;
		virtual void SilentUpdate() = 0;
	};

	template <typename PropType>
	class TPropertyWatcher : public FPropertyWatcher
	{
	public:
		using FValueGetter = TFunction<PropType(void)>;
		using FChangedCallback = TFunction<void(const PropType&)>;

		TPropertyWatcher(const PropType& Property,
						 FChangedCallback OnChangedIn)
			: GetValue([&Property](){return Property;}),
			  OnChanged(MoveTemp(OnChangedIn))
		{}
		TPropertyWatcher(FValueGetter GetValueIn,
						 FChangedCallback OnChangedIn)
			: GetValue(MoveTemp(GetValueIn)), OnChanged(MoveTemp(OnChangedIn))
		{}
		void CheckAndUpdate() final
		{
			PropType Value = GetValue();
			if ((!Cached.IsSet()) || (Cached.GetValue() != Value))
			{
				Cached = Value;
				OnChanged(Cached.GetValue());
			}
		}
		void SilentUpdate() final
		{
			Cached = GetValue();
		}
	private:
		TOptional<PropType> Cached;
		FValueGetter GetValue;
		FChangedCallback OnChanged;
	};

	FWatchablePropertySet() = default;
	FWatchablePropertySet(const FWatchablePropertySet&) = delete;
	FWatchablePropertySet& operator=(const FWatchablePropertySet&) = delete;

	void CheckAndUpdateWatched()
	{
		for ( auto& PropWatcher : PropertyWatchers )
		{
			PropWatcher->CheckAndUpdate();
		}
	}
	void SilentUpdateWatched()
	{
		for ( auto& PropWatcher : PropertyWatchers )
		{
			PropWatcher->SilentUpdate();
		}
	}

	template <typename PropType>
	void WatchProperty(const PropType& ValueIn,
					   typename TPropertyWatcher<PropType>::FChangedCallback OnChangedIn)
	{
		PropertyWatchers.Emplace(MakeUnique<TPropertyWatcher<PropType>>(ValueIn, OnChangedIn));
	}
	template <typename PropType>
	void WatchProperty(typename TPropertyWatcher<PropType>::FValueGetter GetValueIn,
					   typename TPropertyWatcher<PropType>::FChangedCallback OnChangedIn)
	{
		PropertyWatchers.Emplace(MakeUnique<TPropertyWatcher<PropType>>(GetValueIn, OnChangedIn));
	}
private:
	TArray<TUniquePtr<FPropertyWatcher>> PropertyWatchers;
};

/** This delegate is used by UInteractiveToolPropertySet */
DECLARE_MULTICAST_DELEGATE_TwoParams(FInteractiveToolPropertySetModifiedSignature, UObject*, FProperty*);


/**
 * A UInteractiveTool contains a set of UObjects that contain "properties" of the Tool, ie
 * the configuration flags, parameters, etc that control the Tool. Currently any UObject
 * can be added as a property set, however there is no automatic mechanism for those child
 * UObjects to notify the Tool when a property changes.
 *
 * If you make your property set UObjects subclasses of UInteractiveToolPropertySet, then
 * when the Tool Properties are changed *in the Editor*, the parent Tool will be automatically notified.
 * You can override UInteractiveTool::OnPropertyModified() to act on these notifications
 */
UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API UInteractiveToolPropertySet : public UObject, public FWatchablePropertySet
{
	GENERATED_BODY()

public:
	/** @return the multicast delegate that is called when properties are modified */
	FInteractiveToolPropertySetModifiedSignature& GetOnModified()
	{
		return OnModified;
	}

	/** Return true if this property set is enabled. Enabled/Disable state is intended to be used to control things like visibility in UI/etc. */
	bool IsPropertySetEnabled() const
	{
		return bIsPropertySetEnabled;
	}

	//
	// Setting saving/serialization
	//
	/**
	 * Save and restore values of current Tool Properties between tool invocations
	 *
	 * The default behaviour of these functions is to Save or Restore every property in the property set.  It is not
	 * necessary to save/restore all possible Properties (in many cases this would not make sense), so individual
	 * properties may be skipped by adding the "TransientToolProperty" tag to their metadata on a property by property
	 * basis.
	 *
	 * Property sets which need more exotic behaviour upon Save and Restore may override these routines
	 *
	 * GetPropertyCache() and GetDynamicPropertyCache() can be used to return an instance of either the static or the
	 * dynamic type of the specified property set subclass which may be used as a place to save/restore these properties
	 * by customized Save/Restore functions
	 */
	virtual void SaveProperties(UInteractiveTool* SaveFromTool);
	virtual void RestoreProperties(UInteractiveTool* RestoreToTool);
private:

	// Utility func used to implement the default Save/RestoreProperties funcs
	void SaveRestoreProperties(UInteractiveTool* RestoreToTool, bool bSaving);
protected:
	/**
	 * GetPropertyCache returns a class-internal object that subclasses can use to save/restore properties.
	 * If the subclass is UMyPropertySet, this function should only ever be called as GetPropertyCache<UMyPropertySet>().
	 */
	template<typename ObjType>
	static ObjType* GetPropertyCache()
	{
		ObjType* CDO = GetMutableDefault<ObjType>();
		if (CDO->CachedProperties == nullptr)
		{
			CDO->CachedProperties = NewObject<ObjType>();
		}
		return CastChecked<ObjType>(CDO->CachedProperties);
	}

	UInteractiveToolPropertySet* GetDynamicPropertyCache()
	{
		UInteractiveToolPropertySet* CDO = GetMutableDefault<UInteractiveToolPropertySet>(GetClass());
		if (CDO->CachedProperties == nullptr)
		{
			CDO->CachedProperties = NewObject<UInteractiveToolPropertySet>((UObject*)GetTransientPackage(), GetClass());
		}
		return CDO->CachedProperties;
	}

public:
#if WITH_EDITOR
	/**
	  * Posts a message to the OnModified delegate with the modified FProperty
	  * @warning Please consider listening to OnModified instead of overriding this function
	  * @warning this function is currently only called in Editor (not at runtime)
	  */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent);
#endif

protected:
	// CachedProperties should only ever be set to an instance of the subclass, ideally via GetPropertyCache().
	UPROPERTY()
	UInteractiveToolPropertySet* CachedProperties = nullptr;

	UPROPERTY()
	bool bIsPropertySetEnabled = true;

	friend class UInteractiveTool;	// so that tool can enable/disable

	FInteractiveToolPropertySetModifiedSignature OnModified;
};


/**
 * UInteractiveTool is the base class for all Tools in the InteractiveToolsFramework.
 * A Tool is is a "lightweight mode" that may "own" one or more Actors/Components/etc in
 * the current scene, may capture certain input devices or event streams, and so on.
 * The base implementation essentially does nothing but provide sane default behaviors.
 *
 * The BaseTools/ subfolder contains implementations of various kinds of standard
 * "tool behavior", like a tool that responds to a mouse click, etc, that can be
 * extended to implement custom behaviors.
 *
 * In the framework, you do not create instances of UInteractiveTool yourself.
 * You provide a UInteractiveToolBuilder implementation that can properly construct
 * an instance of your Tool, this is where for example default parameters would be set.
 * The ToolBuilder is registered with the ToolManager, and then UInteractiveToolManager::ActivateTool()
 * is used to kick things off.
 *
 * @todo callback/delegate for if/when .InputBehaviors changes
 * @todo callback/delegate for when tool properties change
 */
UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API UInteractiveTool : public UObject, public IInputBehaviorSource
{
	GENERATED_BODY()

public:
	UInteractiveTool();

	/**
	 * Called by ToolManager to initialize the Tool *after* ToolBuilder::BuildTool() has been called
	 */
	virtual void Setup();

	/**
	 * Called by ToolManager to shut down the Tool
	 * @param ShutdownType indicates how the tool should shutdown (ie Accept or Cancel current preview, etc)
	 */
	virtual void Shutdown(EToolShutdownType ShutdownType);

	/**
	 * Allow the Tool to do any custom drawing (ie via PDI/RHI)
	 * @param RenderAPI Abstraction that provides access to Rendering in the current ToolsContext
	 */
	virtual void Render(IToolsContextRenderAPI* RenderAPI);

	/**
	 * Allow the Tool to do any custom screen space drawing
	 * @param Canvas the FCanvas to use to do the drawing
	 * @param RenderAPI Abstraction that provides access to Rendering in the current ToolsContext
	 */
	virtual void DrawHUD( FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI );

	/**
	 * Non overrideable func which does processing and calls the tool's OnTick
	 * @param DeltaTime the time delta since last tick
	 */
	virtual void Tick(float DeltaTime) final;

	/**
	 * @return ToolManager that owns this Tool
	 */
	virtual UInteractiveToolManager* GetToolManager() const;


	/**
	 * @return true if this Tool support being Cancelled, ie calling Shutdown(EToolShutdownType::Cancel)
	 */
	virtual bool HasCancel() const;

	/**
	 * @return true if this Tool support being Accepted, ie calling Shutdown(EToolShutdownType::Accept)
	 */
	virtual bool HasAccept() const;

	/**
	 * @return true if this Tool is currently in a state where it can be Accepted. This may be false if for example there was an error in the Tool.
	 */
	virtual bool CanAccept() const;



	//
	// Input Behaviors support
	//

	/**
	 * Add an input behavior for this Tool
	 * @param Behavior behavior to add
	 */
	virtual void AddInputBehavior(UInputBehavior* Behavior);

	/**
	 * @return Current input behavior set.
	 */
	virtual const UInputBehaviorSet* GetInputBehaviors() const;


	//
	// Property support
	//

	/**
	 * @return list of property UObjects for this tool (ie to add to a DetailsViewPanel, for example)
	 */
	virtual TArray<UObject*> GetToolProperties(bool bEnabledOnly = true) const;

	/**
	 * OnPropertySetsModified is broadcast whenever the contents of the ToolPropertyObjects array is modified
	 */
	DECLARE_MULTICAST_DELEGATE(OnInteractiveToolPropertySetsModified);
	OnInteractiveToolPropertySetsModified OnPropertySetsModified;

	/**
	 * Automatically called by UInteractiveToolPropertySet.OnModified delegate to notify Tool of child property set changes
	 * @param PropertySet which UInteractiveToolPropertySet was modified
	 * @param Property which FProperty in the set was modified
	 */
	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property)
	{
	}


protected:

	/** The current set of InputBehaviors provided by this Tool */
	UPROPERTY()
	UInputBehaviorSet* InputBehaviors;

	/** The current set of Property UObjects provided by this Tool. May contain pointer to itself. */
	UPROPERTY()
	TArray<UObject*> ToolPropertyObjects;

	/**
	 * Add a Property object for this Tool
	 * @param Property object to add
	 */
	virtual void AddToolPropertySource(UObject* PropertyObject);

	/**
	 * Add a PropertySet object for this Tool
	 * @param PropertySet Property Set object to add
	 */
	virtual void AddToolPropertySource(UInteractiveToolPropertySet* PropertySet);

	/**
	 * Remove a PropertySet object from this Tool. If found, will broadcast OnPropertySetsModified
	 * @param PropertySet property set to remove.
	 * @return true if PropertySet is found and removed
	 */
	virtual bool RemoveToolPropertySource(UInteractiveToolPropertySet* PropertySet);

	/**
	 * Replace a PropertySet object on this Tool with another property set. If replaced, will broadcast OnPropertySetsModified
	 * @param CurPropertySet property set to remove
	 * @param ReplaceWith property set to add
	 * @param bSetToEnabled if true, ReplaceWith property set is explicitly enabled (otherwise enable/disable state is unmodified)
	 * @return true if CurPropertySet is found and replaced
	 */
	virtual bool ReplaceToolPropertySource(UInteractiveToolPropertySet* CurPropertySet, UInteractiveToolPropertySet* ReplaceWith, bool bSetToEnabled = true);

	/**
	 * Enable/Disable a PropertySet object for this Tool. If found and state was modified, will broadcast OnPropertySetsModified
	 * @param PropertySet Property Set object to modify
	 * @param bEnabled whether to enable or disable
	 * @return true if PropertySet was found
	 */
	virtual bool SetToolPropertySourceEnabled(UInteractiveToolPropertySet* PropertySet, bool bEnabled);


	//
	// Action support/system
	//
	// Your Tool subclass can register a set of "Actions" it can execute
	// by overloading RegisterActions(). Then external systems can use GetActionSet() to
	// find out what Actions your Tool supports, and ExecuteAction() to run those actions.
	//

private:
	/**
	 * Allow the Tool to do any necessary processing on Tick
	 * @param DeltaTime the time delta since last tick
	 */
	virtual void OnTick(float DeltaTime){};

public:
	/**
	 * Get the internal Action Set for this Tool. The action set is created and registered on-demand.
	 * @return pointer to initialized Action set
	 */
	virtual FInteractiveToolActionSet* GetActionSet();

	/**
	 * Request that the Action identified by ActionID be executed.
	 * Default implementation forwards these requests to internal ToolActionSet.
	 */
	virtual void ExecuteAction(int32 ActionID);


protected:
	/**
	 * Override this function to register the set of Actions this Tool supports, using FInteractiveToolActionSet::RegisterAction.
	 * Note that for the actions to be triggered, you will also need to add corresponding registration per tool
	 *  -- see Engine\Plugins\Experimental\ModelingToolsEditorMode\Source\ModelingToolsEditorMode\Public\ModelingToolsActions.h for examples
	 */
	virtual void RegisterActions(FInteractiveToolActionSet& ActionSet);


private:
	/**
	 * Set of actions this Tool can execute. This variable is allocated on-demand.
	 * Use GetActionSet() instead of accessing this pointer directly!
	 */
	FInteractiveToolActionSet* ToolActionSet = nullptr;



	//
	// Tool Information (name, icon, help text, etc)
	//


public:
	/**
	 * @return ToolInfo structure for this Tool
	 */
	virtual FInteractiveToolInfo GetToolInfo() const
	{
		return DefaultToolInfo;
	}

	/**
	 * Replace existing ToolInfo with new data
	 */
	virtual void SetToolInfo(const FInteractiveToolInfo& NewInfo)
	{
		DefaultToolInfo = NewInfo;
	}

	/**
	 * Set Tool name
	 */
	virtual void SetToolDisplayName(const FText& NewName)
	{
		DefaultToolInfo.ToolDisplayName = NewName;
	}

private:
	/**
	 * ToolInfo for this Tool
	 */
	FInteractiveToolInfo DefaultToolInfo;



private:

	// InteractionMechanic needs to be able to talk to Tool internals, eg property sets, behaviors, etc
	friend class UInteractionMechanic;
};


