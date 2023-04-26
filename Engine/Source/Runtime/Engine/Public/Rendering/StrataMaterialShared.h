// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StrataDefinitions.h"
#include "Serialization/MemoryImage.h"


// Structures in this files are only used a compilation result return by the compiler.
// They are also used to present material information in the editor UI.


struct FStrataRegisteredSharedLocalBasis
{
	DECLARE_TYPE_LAYOUT(FStrataRegisteredSharedLocalBasis, NonVirtual);
public:
	FStrataRegisteredSharedLocalBasis();

	LAYOUT_FIELD_EDITORONLY(int32, NormalCodeChunk);
	LAYOUT_FIELD_EDITORONLY(int32, TangentCodeChunk);
	LAYOUT_FIELD_EDITORONLY(uint64, NormalCodeChunkHash);
	LAYOUT_FIELD_EDITORONLY(uint64, TangentCodeChunkHash);
	LAYOUT_FIELD_EDITORONLY(uint8, GraphSharedLocalBasisIndex);
};

struct FStrataOperator
{
	DECLARE_TYPE_LAYOUT(FStrataOperator, NonVirtual);
public:
	FStrataOperator();

	LAYOUT_FIELD_EDITORONLY(int32, OperatorType);
	LAYOUT_BITFIELD_EDITORONLY(uint8, bNodeRequestParameterBlending, 1);

	LAYOUT_FIELD_EDITORONLY(int32, Index);			// Index into the array of operators
	LAYOUT_FIELD_EDITORONLY(int32, ParentIndex);	// Parent operator index
	LAYOUT_FIELD_EDITORONLY(int32, LeftIndex);		// Left child operator index
	LAYOUT_FIELD_EDITORONLY(int32, RightIndex);		// Right child operator index
	LAYOUT_FIELD_EDITORONLY(int32, ThicknessIndex);	// Thickness expression index

	// Data used for BSDF type nodes only
	LAYOUT_FIELD_EDITORONLY(int32, BSDFIndex);		// Index in the array of BSDF if a BSDF operator
	LAYOUT_FIELD_EDITORONLY(int32, BSDFType);
	LAYOUT_FIELD_EDITORONLY(FStrataRegisteredSharedLocalBasis, BSDFRegisteredSharedLocalBasis);
	LAYOUT_BITFIELD_EDITORONLY(uint8, bBSDFHasSSS, 1);
	LAYOUT_BITFIELD_EDITORONLY(uint8, bBSDFHasMFPPluggedIn, 1);
	LAYOUT_BITFIELD_EDITORONLY(uint8, bBSDFHasEdgeColor, 1);
	LAYOUT_BITFIELD_EDITORONLY(uint8, bBSDFHasFuzz, 1);
	LAYOUT_BITFIELD_EDITORONLY(uint8, bBSDFHasSecondRoughnessOrSimpleClearCoat, 1);
	LAYOUT_BITFIELD_EDITORONLY(uint8, bBSDFHasAnisotropy, 1);

	// Data derived after the tree has been built.
	LAYOUT_FIELD_EDITORONLY(int32, MaxDistanceFromLeaves);
	LAYOUT_FIELD_EDITORONLY(int32, LayerDepth);
	LAYOUT_BITFIELD_EDITORONLY(uint8, bIsTop, 1);
	LAYOUT_BITFIELD_EDITORONLY(uint8, bIsBottom, 1);

	LAYOUT_BITFIELD_EDITORONLY(uint8, bUseParameterBlending, 1);			// True when part of a sub tree where parameter blending is in use
	LAYOUT_BITFIELD_EDITORONLY(uint8, bRootOfParameterBlendingSubTree, 1);	// True when the root of a sub tree where parameter blending is in use. Only this node will register a BSDF

	void CombineFlagsForParameterBlending(FStrataOperator& A, FStrataOperator& B);

	void CopyFlagsForParameterBlending(FStrataOperator& A);

	bool IsDiscarded() const;
};

#define STRATA_COMPILATION_OUTPUT_MAX_OPERATOR 32

struct FStrataMaterialCompilationOutput
{
	DECLARE_TYPE_LAYOUT(FStrataMaterialCompilationOutput, NonVirtual);
public:

	FStrataMaterialCompilationOutput();

	////
	//// The following data is required at runtime
	////

	/** Strata material type, at compile time (0:simple, 1:single, 2: complex) */
	LAYOUT_BITFIELD(uint8, StrataMaterialType, 2);

	/** Strata BSDF count, at compile time (0-7) */
	LAYOUT_BITFIELD(uint8, StrataBSDFCount, 3);

	/** Strata uint per pixel, at compile time (0-255) */
	LAYOUT_BITFIELD(uint8, StrataUintPerPixel, 8);

	////
	//// The following data is only needed when compiling with the editor.
	////

	// STRATA_TODO Strata Tree topology
	// STRATA_TODO corresponding uint8 ShadingModel? IsThin?

	/** The Strata verbose description */
	LAYOUT_FIELD_EDITORONLY(FMemoryImageString, StrataMaterialDescription);

	/** The number of local normal/tangent bases */
	LAYOUT_FIELD_EDITORONLY(uint8, SharedLocalBasesCount);
	/** Material requested byte count per pixel */
	LAYOUT_FIELD_EDITORONLY(uint8, RequestedBytePixePixel);
	/** The byte count per pixel supported by the platform the material has been compiled against */
	LAYOUT_FIELD_EDITORONLY(uint8, PlatformBytePixePixel);

	/** The byte per pixel count supported by the platform the material has been compiled against */
	LAYOUT_BITFIELD_EDITORONLY(uint8, bMaterialOutOfBudgetHasBeenSimplified, 1);

	LAYOUT_FIELD_EDITORONLY(uint8, RootOperatorIndex);
	LAYOUT_ARRAY_EDITORONLY(FStrataOperator, Operators, STRATA_COMPILATION_OUTPUT_MAX_OPERATOR);
};

