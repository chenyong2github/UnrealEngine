/**
 * Header to expose Shewchuk's exact predicate functions.
 * This is a private interface to third party code.
 * Most code should instead use ExactPredicates.h, the Unreal Engine-style interface
 */

#pragma once

namespace ShewchukExactPredicates
{

	/** @return true if exactinit() has already been run; useful for check()ing that */
	bool IsExactPredicateDataInitialized();

	/** must be called before running any exact predicate function.  called by module startup. */
	void exactinit();

	// TODO also expose the math routines underlying the predicates

	// TODO also build+expose float version

	double orient2dfast(double* pa, double* pb, double* pc);
	double orient2d(double* pa, double* pb, double* pc);
	double orient3dfast(double* pa, double* pb, double* pc, double* pd);
	double orient3d(double* pa, double* pb, double* pc, double* pd);
	double incirclefast(double* pa, double* pb, double* pc, double* pd);
	double incircle(double* pa, double* pb, double* pc, double* pd);
	double inspherefast(double* pa, double* pb, double* pc, double* pd, double* pe);
	double insphere(double* pa, double* pb, double* pc, double* pd, double* pe);
} // namespace ExactPredicates
