// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BlueprintCompiledStatement.h"

struct FBPTerminal;
struct FKismetFunctionContext;
class UEdGraphPin;

namespace UE::KismetCompiler::CastingUtils
{
	using StatementNamePair = TPair<EKismetCompiledStatementType, const TCHAR*>;

	/**
	 * Given a specific EKismetCompiledStatementType enum, this function returns its inverse.
	 * eg: the inverse of KCST_DoubleToFloatCast is KCST_FloatToDoubleCast.
	 * Invalid enums (ie: not cast-related) will return an unset TOptional.
	 * 
	 * @param Statement - The enum to query for an inverse.
	 * @return A set TOptional with the enum's inverse. Unset if no inverse is found.
	 */
	KISMETCOMPILER_API TOptional<EKismetCompiledStatementType> GetInverseCastStatement(EKismetCompiledStatementType Statement);

	/**
	 * Analyzes the NetMap of the current function context for potential implicit casts.
	 * If any are found, they're added to ImplicitCastMap in the context.
	 * After function compilation, the Kismet compiler will validate that the map is empty.
	 * It's up to the nodes to check the map and insert cast statements where necessary.
	 * 
	 * @param Context - Current function context to analyze. Assumes the NetMap has been populated.
	 */
	KISMETCOMPILER_API void RegisterImplicitCasts(FKismetFunctionContext& Context);

	/**
	 * Utility function used by nodes for inserting implicit cast statements.
	 * During compilation, a node that potentially may need to handle a cast should call this function.
	 * If the current pin needs a cast, a statement is inserted, and a new terminal for the temporary is returned.
	 * 
	 * @param Context - Current function context to analyze. Assumes the ImplicitCastMap has been populated.
	 * @param DestinationPin - Used as a key in the ImplicitCastMap. These pins are always inputs.
	 * @param RHSTerm - The current terminal that should have its data read from.
	 * @return A new FBPTerminal, EKismetCompiledStatementType pair for the temporary variable that has the casted result (if one exists)
	 */
	KISMETCOMPILER_API TOptional<TPair<FBPTerminal*, EKismetCompiledStatementType>> InsertImplicitCastStatement(FKismetFunctionContext& Context,
																												UEdGraphPin* DestinationPin,
																												FBPTerminal* RHSTerm);

	/**
	 * Removes the specific UEdGraphPin from the context's implicit cast map.
	 * In most cases, InsertImplicitCastStatement should be used to remove the cast map entry.
	 * However, some nodes need to implement custom behavior for casting.
	 * 
	 * @param Context - Current function context to analyze. Assumes the ImplicitCastMap has been populated.
	 * @param DestinationPin - Used as a key in the ImplicitCastMap. These pins are always inputs.
	 * @return True if DestinationPin was found in the ImplicitCastMap.
	 */
	KISMETCOMPILER_API bool RemoveRegisteredImplicitCast(FKismetFunctionContext& Context, const UEdGraphPin* DestinationPin);

	/**
	 * Retrieves the conversion type needed between two arbitrary pins (if necessary). Specifically, this indicates if either
	 * a narrowing or widening cast is needed between a float or a double type (including containers). In addition to the
	 * corresponding EKismetCompiledStatementType that represents the cast type, a string literal describing the cast is also
	 * returned.
	 * 
	 * @param SourcePin - The source pin to compare.
	 * @param DestinationPin - The destination pin to compare.
	 * @return A new StatementNamePair containing the cast information. The result is unset if no conversion is needed.
	 */
	KISMETCOMPILER_API TOptional<StatementNamePair> GetFloatingPointConversionType(const UEdGraphPin& SourcePin, const UEdGraphPin& DestinationPin);

} // UE::KismetCompiler::CastingUtils

