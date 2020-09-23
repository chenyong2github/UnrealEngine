// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "InteractiveTool.h" //UInteractiveToolPropertySet
#include "VectorTypes.h"

#include "RevolveProperties.generated.h"

class UNewMeshMaterialProperties;
class FCurveSweepOp;

UENUM()
enum class ERevolvePropertiesCapFillMode : uint8
{
	/** No cap. */
	None,
	/** Cap is triangulated to maximize the minimal angle in the triangles (if they were to be
	   projected onto a best-fit plane). */
	Delaunay,
	/** Cap is triangualted using a standard ear clipping approach. This could result in some
	   very thin triangles. */
	EarClipping,
	/** A vertex is placed in the center and a fan is created to the boundary. This is nice if
	   the cross section is convex, but creates invalid geometry if it isn't. */
	CenterFan
};


UENUM()
enum class ERevolvePropertiesPolygroupMode : uint8
{
	/** One polygroup for body of output mesh */
	Single,
	/** One polygroup per generated quad/triangle. */
	PerFace,
	/** Groups will be arranged in strips running in the profile curve direction, one per revolution step. */
	PerStep,
	/** Groups will be arranged in strips running along in the revolution direction according to profile curve. */
	AccordingToProfileCurve
};

UENUM()
enum class ERevolvePropertiesQuadSplit : uint8
{
	/** Quads will always be split the same way relative to an unrolled mesh, regardless of quad shape. */
	Uniform,
	
	/** Quads will be split such that the shortest diagonal is connected. */
	ShortestDiagonal
};

/**
 * Common properties for revolving a polyline to create a mesh.
 */
UCLASS()
class MESHMODELINGTOOLS_API URevolveProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:

	/** Revolution extent. */
	UPROPERTY(EditAnywhere, Category = RevolveSettings, meta = (UIMin = "0", UIMax = "360", ClampMin = "0", ClampMax = "360"))
	double RevolutionDegrees = 360;

	/** The angle by which to shift the profile curve around the axis before beginning the revolve */
	UPROPERTY(EditAnywhere, Category = RevolveSettings, meta = (UIMin = "-360", UIMax = "360", ClampMin = "-36000", ClampMax = "36000"), AdvancedDisplay)
	double RevolutionDegreesOffset = 0;

	/** Number of steps to take while revolving. */
	UPROPERTY(EditAnywhere, Category = RevolveSettings, meta = (UIMin = "1", ClampMin = "1", UIMax = "100", ClampMax = "5000"))
	int Steps = 24;

	/** By default, revolution is done counterclockwise if looking down the revolution axis. This reverses the direction.*/
	UPROPERTY(EditAnywhere, Category = RevolveSettings)
	bool bReverseRevolutionDirection = false;

	/** Flips the mesh inside out. */
	UPROPERTY(EditAnywhere, Category = RevolveSettings)
	bool bFlipMesh = false;

	/** If true, then rather than revolving the profile directly, it is interpreted as the midpoint cross section of
	 the first rotation step. Useful, for instance, for using the tool to create square columns. */
	UPROPERTY(EditAnywhere, Category = RevolveSettings, AdvancedDisplay)
	bool bProfileIsCrossSectionOfSide = false;

	/** Determines grouping of generated triangles into polygroups. 
	   Caps (if present) will always be separate groups. */
	UPROPERTY(EditAnywhere, Category = RevolveSettings, AdvancedDisplay)
	ERevolvePropertiesPolygroupMode PolygroupMode = ERevolvePropertiesPolygroupMode::PerFace;

	/** Determines how any generated quads are split into triangles. */
	UPROPERTY(EditAnywhere, Category = RevolveSettings, AdvancedDisplay)
	ERevolvePropertiesQuadSplit QuadSplitMode = ERevolvePropertiesQuadSplit::ShortestDiagonal;
	
	/** When quads are generated using "shortest" diagonal, this biases the diagonal length comparison
	 to prefer one slightly in the case of similar diagonals (for example, a value of 0.01 allows a
	 1% difference in lengths before the triangulation is flipped). Helps symmetric quads be uniformly
	 triangulated. */
	UPROPERTY(EditAnywhere, Category = RevolveSettings, AdvancedDisplay, meta = (ClampMin = "0.0",  ClampMax = "2.0", 
		EditCondition = "QuadSplitMode == ERevolvePropertiesQuadSplit::ShortestDiagonal", EditConditionHides))
	double DiagonalProportionTolerance = 0.01;

	/** Determines how caps are created if the revolution is partial. Not relevant if the
	  revolution is full and welded. */
	UPROPERTY(EditAnywhere, Category = RevolveSettings, AdvancedDisplay)
	ERevolvePropertiesCapFillMode CapFillMode = ERevolvePropertiesCapFillMode::Delaunay;

	/** If true, the ends of a fully revolved profile are welded together, rather than duplicating
	  vertices at the seam. Not relevant if the revolution is not full. */
	UPROPERTY(EditAnywhere, Category = RevolveSettings, AdvancedDisplay)
	bool bWeldFullRevolution = true;

	/** If true, vertices sufficiently close to the axis will not be replicated, instead reusing
	  the same vertex for any adjacent triangles. */
	UPROPERTY(EditAnywhere, Category = RevolveSettings, AdvancedDisplay)
	bool bWeldVertsOnAxis = true;

	/** If welding vertices on the axis, the distance that a vertex can be from the axis and still be welded */
	UPROPERTY(EditAnywhere, Category = RevolveSettings, AdvancedDisplay, meta = (ClampMin = "0.0", ClampMax = "20.0", EditCondition = "bWeldVertsOnAxis"))
	double AxisWeldTolerance = 0.1;

	/** If true, normals are not averaged or shared between triangles with sufficient angle difference. */
	UPROPERTY(EditAnywhere, Category = RevolveSettings)
	bool bSharpNormals = false;

	/** When using sharp normals, the degree difference to accept between adjacent triangle normals to allow them to share
	 normals at their vertices. */
	UPROPERTY(EditAnywhere, Category = RevolveSettings, meta = (ClampMin = "0.0", ClampMax = "90.0", EditCondition = "bSharpNormals"))
	double SharpNormalAngleTolerance = 0.1;

	/** If true, UV coordinates will be flipped in the V direction. */
	UPROPERTY(EditAnywhere, Category = RevolveSettings, AdvancedDisplay)
	bool bFlipVs = false;

	/* If true, UV layout is not affected by segments of the profile curve that 
	  do not result in any triangles (i.e., when both ends of the segment are welded
	  due to being on the revolution axis).*/
	UPROPERTY(EditAnywhere, Category = RevolveSettings, AdvancedDisplay)
	bool bUVsSkipFullyWeldedEdges = true;


	/**
	 * Sets most of the settings for a FCurveSweepOp except for the profile curve itself. Should be called
	 * after setting the profile curve, as the function reverses it if necessary.
	 *
	 * CurveSweepOpOut.ProfileCurve and CurveSweepOpOut.bProfileCurveIsClosed must be initialized in advance.
	 */
	void ApplyToCurveSweepOp(const UNewMeshMaterialProperties& MaterialProperties,
		const FVector3d& RevolutionAxisOrigin, const FVector3d& RevolutionAxisDirection,
		FCurveSweepOp& CurveSweepOpOut) const;
};
