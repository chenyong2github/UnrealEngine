// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolTargets/ToolTarget.h"
#include "ToolContextInterfaces.h"

#include "ToolTargetManager.generated.h"

// TODO: Do we need more control over factory order to prioritize which gets used if we can
// make multiple qualifying targets? It should theoretically not matter, but in practice, one
// target or another could be more efficient for certain tasks. This is probably only worth
// thinking about once we encounter it.

/**
 * The tool target manager converts input objects into tool targets- objects that
 * can expose various interfaces that tools might expect but which the original
 * objects may not know about.
 *
 * Someday, the tool target manager may implement caching of targets.
 */
UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API UToolTargetManager : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * @return true if ToolManager is currently active, ie between Initialize() and Shutdown()
	 */
	bool IsActive() const { return bIsActive; }

	virtual void AddTargetFactory(UToolTargetFactory* Factory);

	/** Examines stored target factories to see if one can build the requested type of target. */
	virtual bool CanBuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& TargetRequirements) const;

	/** 
	 * Uses one of the stored factories to build a tool target out of the given input object
	 * that satisfies the given requirements. If multiple factories are capable of building a 
	 * qualifying target, the first encountered one will be used. If none are capable, a nullptr 
	 * is returned.
	 * 
	 * @return Tool target that staisfies given requirements, or nullptr if none could be created.
	 */
	virtual UToolTarget* BuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& TargetRequirements);

	/** Much like BuildTarget, but casts the target to the template argument before returning. */
	template<typename CastToType>
	CastToType* BuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& TargetType)
	{
		UToolTarget* Result = BuildTarget(SourceObject, TargetType);
		return (Result != nullptr) ? Cast<CastToType>(Result) : nullptr;
	}

	/**
	 * Looks through the currently selected components and actors and counts the number of
	 * inputs that could be used to create qualifying tool targets.
	 */
	virtual int32 CountSelectedAndTargetable(const FToolBuilderState& SceneState,
		const FToolTargetTypeRequirements& TargetRequirements) const;
	
	/**
	 * Looks through the currently selected components and actors and builds a target out of
	 * the first encountered element that satisfies the requirements. 
	 */
	virtual UToolTarget* BuildFirstSelectedTargetable(const FToolBuilderState& SceneState,
		const FToolTargetTypeRequirements& TargetRequirements);

protected:
	friend class UInteractiveToolsContext;		// to call Initialize/Shutdown

	// This should be removed once tools are transitioned to using tool targets, and the
	// functions in ComponentSourceInterfaces.h do not exist. For now, this is here to call
	// Initialize().
	friend INTERACTIVETOOLSFRAMEWORK_API void AddFactoryToDeprecatedToolTargetManager(UToolTargetFactory* Factory);

	UToolTargetManager(){};

	/** Initialize the ToolTargetManager. UInteractiveToolsContext calls this, you should not. */
	virtual void Initialize();

	/** Shutdown the ToolTargetManager. Called by UInteractiveToolsContext. */
	virtual void Shutdown();

	/** This flag is set to true on Initialize() and false on Shutdown(). */
	bool bIsActive = false;

	UPROPERTY()
	TArray<TObjectPtr<UToolTargetFactory>> Factories;

	// More state will go here if the manager deals with target caching.
};