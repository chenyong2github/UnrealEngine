// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Templates/SubclassOf.h"

class UObject;
class UClass;
class IAnimBlueprintCompilationContext;
class IAnimBlueprintCompilationBracketContext;
class IAnimBlueprintPostExpansionStepContext;
class IAnimBlueprintCopyTermDefaultsContext;
class IAnimBlueprintGeneratedClassCompiledData;
class UAnimGraphNode_Base;
class UEdGraph;
class UEdGraphSchema;

/** Begin ordered delegate calls - these functions are called in the order presented here */

/** 
 * Delegate fired when the class starts compiling. The class may be new or recycled.
 * @param	InClass			The class that is being compiled. This could be a newly created class or a recycled class
 * @param	InCompilerContext	The compiler context for the current compilation
 * @param	OutCompiledData	The compiled data that this handler can write to
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnStartCompilingClass, const UClass* /*InClass*/, IAnimBlueprintCompilationBracketContext& /*InCompilerContext*/, IAnimBlueprintGeneratedClassCompiledData& /*OutCompiledData*/);

/** 
 * Delegate fired before all animation nodes are processed
 * @param	InAnimNodes		The set of anim nodes that should be processed. Note that these nodes have not yet been pruned for connectivity
 * @param	InCompilerContext	The compiler context for the current compilation
 * @param	OutCompiledData	The compiled data that this handler can write to
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnPreProcessAnimationNodes, TArrayView<UAnimGraphNode_Base*> /*InAnimNodes*/, IAnimBlueprintCompilationContext& /*InCompilerContext*/, IAnimBlueprintGeneratedClassCompiledData& /*OutCompiledData*/);

/** 
 * Delegate fired after all animation nodes are processed
 * @param	InAnimNodes		The set of anim nodes that were processed. Note that these nodes were not pruned for connectivity (they will be the same set passed to PreProcessAnimationNodes)
 * @param	InCompilerContext	The compiler context for the current compilation
 * @param	OutCompiledData	The compiled data that this handler can write to
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnPostProcessAnimationNodes, TArrayView<UAnimGraphNode_Base*> /*InAnimNodes*/, IAnimBlueprintCompilationContext& /*InCompilerContext*/, IAnimBlueprintGeneratedClassCompiledData& /*OutCompiledData*/);

/** 
 * Delegate fired post- graph expansion
 * @param	InGraph		The graph that was just expanded
 * @param	InCompilerContext	The compiler context for the current compilation
 * @param	OutCompiledData	The compiled data that this handler can write to
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnPostExpansionStep, const UEdGraph* /*InGraph*/, IAnimBlueprintPostExpansionStepContext& /*InCompilerContext*/, IAnimBlueprintGeneratedClassCompiledData& /*OutCompiledData*/);

/** 
 * Delegate fired when the class has finished compiling
 * @param	InClass			The class that was compiled
 * @param	InCompilerContext	The compiler context for the current compilation
 * @param	OutCompiledData	The compiled data that this handler can write to
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnFinishCompilingClass, const UClass* /*InClass*/, IAnimBlueprintCompilationBracketContext& /*InCompilerContext*/, IAnimBlueprintGeneratedClassCompiledData& /*OutCompiledData*/);

/** 
 * Delegate fired when data is being copied to the CDO
 * @param	InDefaultObject		The CDO for the just-compiled class
 * @param	InCompilerContext	The compiler context for the current compilation
 * @param	OutCompiledData	The compiled data that this handler can write to
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnCopyTermDefaultsToDefaultObject, UObject* /*InDefaultObject*/, IAnimBlueprintCopyTermDefaultsContext& /*InCompilerContext*/, IAnimBlueprintGeneratedClassCompiledData& /*OutCompiledData*/);

/** End ordered calls */

/** Interface to the creation of an anim BP compiler context */
class ANIMGRAPH_API IAnimBlueprintCompilerCreationContext
{
public:
	virtual ~IAnimBlueprintCompilerCreationContext() {}

	/** Delegate fired when the class starts compiling. The class may be new or recycled. */
	virtual FOnStartCompilingClass& OnStartCompilingClass() = 0;

	/** Delegate fired before all animation nodes are processed */
	virtual FOnPreProcessAnimationNodes& OnPreProcessAnimationNodes() = 0;

	/** Delegate fired after all animation nodes are processed */
	virtual FOnPostProcessAnimationNodes& OnPostProcessAnimationNodes() = 0;

	/** Delegate fired post- graph expansion */
	virtual FOnPostExpansionStep& OnPostExpansionStep() = 0;

	/** Delegate fired when the class has finished compiling */
	virtual FOnFinishCompilingClass& OnFinishCompilingClass() = 0;

	/** Delegate fired when data is being copied to the CDO */
	virtual FOnCopyTermDefaultsToDefaultObject& OnCopyTermDefaultsToDefaultObject() = 0;

	/** Registers a graphs schema class to the anim BP compiler so that default function processing is not performed on it */
	virtual void RegisterKnownGraphSchema(TSubclassOf<UEdGraphSchema> InGraphSchemaClass) = 0;
};

