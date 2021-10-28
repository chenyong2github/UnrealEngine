// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SceneTypes.h"		// FPrimitiveComponentId
#include "Image/SpatialPhotoSet.h"

class UTextureRenderTarget2D;
class AActor;

namespace UE
{
namespace Geometry
{

/**
 * ERenderCaptureType defines which type of render buffer should be captured.
 */
enum class ERenderCaptureType
{
	BaseColor = 1,
	Roughness = 2,
	Metallic = 4,
	Specular = 8,
	Emissive = 16,
	WorldNormal = 32,
	CombinedMRS = 128
};

/**
 * FRenderCaptureTypeFlags is a set of per-capture-type booleans
 */
struct MODELINGCOMPONENTS_API FRenderCaptureTypeFlags
{
	bool bBaseColor = false;
	bool bRoughness = false;
	bool bMetallic = false;
	bool bSpecular = false;
	bool bEmissive = false;
	bool bWorldNormal = false;
	bool bCombinedMRS = false;

	/** @return FRenderCaptureTypeFlags with all types enabled/true */
	static FRenderCaptureTypeFlags All();

	/** @return FRenderCaptureTypeFlags with all types disabled/false */
	static FRenderCaptureTypeFlags None();

	/** @return FRenderCaptureTypeFlags with only BaseColor enabled/true */
	static FRenderCaptureTypeFlags BaseColor();

	/** @return FRenderCaptureTypeFlags with only WorldNormal enabled/true */
	static FRenderCaptureTypeFlags WorldNormal();

	/** @return FRenderCaptureTypeFlags with a single capture type enabled/true */
	static FRenderCaptureTypeFlags Single(ERenderCaptureType CaptureType);

	/** Set the indicated CaptureType to enabled */
	void SetEnabled(ERenderCaptureType CaptureType, bool bEnabled);
};


/**
 * FWorldRenderCapture captures a rendering of a set of Actors in a World from a
 * specific viewpoint. Various types of rendering are supported, as defined by ERenderCaptureType.
 */
class MODELINGCOMPONENTS_API FWorldRenderCapture
{
public:
	FWorldRenderCapture();
	~FWorldRenderCapture();

	/** Explicitly release any allocated textures or other data structres */
	void Shutdown();

	/** Set the target World */
	void SetWorld(UWorld* World);

	/** 
	 *  Set the set of Actors in the target World that should be included in the Rendering.
	 *  Currently rendering an entire World, ie without an explicit list of Actors, is not supported.
	 */
	void SetVisibleActors(const TArray<AActor*>& Actors);

	/** Get bounding-box of the Visible actors */
	FBoxSphereBounds GetVisibleActorBounds() const { return VisibleBounds; }

	/** 
	 * Compute a sphere where, if the camera is on the sphere pointed at the center, then the Visible Actors
	 * will be fully visible (ie a square capture will not have any clipping), for the given Field of View.
	 * SafetyBoundsScale multiplier is applied to the bounding box.
	 */
	FSphere ComputeContainingRenderSphere(float HorzFOVDegrees, float SafetyBoundsScale = 1.25f) const;

	/** Set desired pixel dimensions of the rendered target image */
	void SetDimensions(const FImageDimensions& Dimensions);

	/** @return pixel dimensions that target image will be rendered at */
	const FImageDimensions& GetDimensions() const { return Dimensions; }

	/**
	 * Capture the desired buffer type CaptureType with the given view/camera parameters.
	 * @param ResultImageOut output iamge of size GetDimensions() is stored here.
	 * @return true if capture could be rendered successfully
	 */
	bool CaptureFromPosition(
		ERenderCaptureType CaptureType,
		const FFrame3d& ViewFrame,
		double HorzFOVDegrees,
		double NearPlaneDist,
		FImageAdapter& ResultImageOut);

	/**
	 * Enable debug image write. The captured image will be written to <Project>/Intermediate/<FolderName>/<CaptureType>_<ImageCounter>.bmp
	 * If FolderName is not specified, "WorldRenderCapture" is used by default.
	 * If an ImageCounter is not specified, an internal static counter is used that increments on every write
	 */
	void SetEnableWriteDebugImage(bool bEnable, int32 ImageCounter = -1, FString FolderName = FString());

protected:
	UWorld* World = nullptr;

	TArray<AActor*> CaptureActors;
	TSet<FPrimitiveComponentId> VisiblePrimitives;
	FBoxSphereBounds VisibleBounds;

	FImageDimensions Dimensions;

	// Temporary textures used as render targets. We explicitly prevent this from being GC'd internally
	UTextureRenderTarget2D* LinearRenderTexture = nullptr;
	UTextureRenderTarget2D* GammaRenderTexture = nullptr;
	FImageDimensions RenderTextureDimensions;
	UTextureRenderTarget2D* GetRenderTexture(bool bLinear);

	// temporary buffer used to read from texture
	TArray<FLinearColor> ReadImageBuffer;

	/** Emissive is a special case and uses different code than capture of color/property channels */
	bool CaptureEmissiveFromPosition(
		const FFrame3d& Frame,
		double HorzFOVDegrees,
		double NearPlaneDist,
		FImageAdapter& ResultImageOut);

	/** Combined Metallic/Roughness/Specular uses a custom postprocess material */
	bool CaptureMRSFromPosition(
		const FFrame3d& Frame,
		double HorzFOVDegrees,
		double NearPlaneDist,
		FImageAdapter& ResultImageOut);

	bool bWriteDebugImage = false;
	int32 DebugImageCounter = -1;
	FString DebugImageFolderName = TEXT("WorldRenderCapture");
	void WriteDebugImage(const FImageAdapter& ResultImageOut, const FString& ImageTypeName);
};





} // end namespace UE::Geometry
} // end namespace UE
