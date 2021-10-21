// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

namespace Chaos
{

/** Parent base class for all the solver constraint containers */
class CHAOS_API FConstraintSolverContainer
{
public:

	/** Virtual destructor */
	virtual ~FConstraintSolverContainer() {}
		
	/** Reset the container to have zero constraints
	* @param MaxConstraints Maximum number of constraints that could be in the container
	*/
	virtual void Reset(const int32 MaxConstraints) {}
};
	
}
