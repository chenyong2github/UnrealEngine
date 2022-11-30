// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "RenderResource.h"
#include "UniformBuffer.h"
#include "Engine/World.h"
#include "SceneTypes.h"
#include "ShowFlags.h"
#include "ConvexVolume.h"
#include "Engine/GameViewportClient.h"
#include "SceneInterface.h"
#include "FinalPostProcessSettings.h"
#include "GlobalDistanceFieldParameters.h"
#include "DebugViewModeHelpers.h"
#include "RendererInterface.h"

class FForwardLightingViewResources;
class FSceneView;
class FSceneViewFamily;
class FSceneViewStateInterface;
class FViewElementDrawer;
class ISceneViewExtension;
class FSceneViewFamily;
class FVolumetricFogViewResources;
class FIESLightProfileResource;
class ITemporalUpscaler;
struct FExposureBufferData;

enum class ERayTracingRenderMode
{
	Disabled			= 0,
	PathTracing			= 1,
	RayTracingDebug		= 2,
};

// Projection data for a FSceneView
struct FSceneViewProjectionData
{
	/** The view origin. */
	FVector ViewOrigin;

	/** Rotation matrix transforming from world space to view space. */
	FMatrix ViewRotationMatrix;

	/** UE4 projection matrix projects such that clip space Z=1 is the near plane, and Z=0 is the infinite far plane. */
	FMatrix ProjectionMatrix;

protected:
	//The unconstrained (no aspect ratio bars applied) view rectangle (also unscaled)
	FIntRect ViewRect;

	// The constrained view rectangle (identical to UnconstrainedUnscaledViewRect if aspect ratio is not constrained)
	FIntRect ConstrainedViewRect;

public:
	void SetViewRectangle(const FIntRect& InViewRect)
	{
		ViewRect = InViewRect;
		ConstrainedViewRect = InViewRect;
	}

	void SetConstrainedViewRectangle(const FIntRect& InViewRect)
	{
		ConstrainedViewRect = InViewRect;
	}

	bool IsValidViewRectangle() const
	{
		return (ConstrainedViewRect.Min.X >= 0) &&
			(ConstrainedViewRect.Min.Y >= 0) &&
			(ConstrainedViewRect.Width() > 0) &&
			(ConstrainedViewRect.Height() > 0);
	}

	bool IsPerspectiveProjection() const
	{
		return ProjectionMatrix.M[3][3] < 1.0f;
	}

	const FIntRect& GetViewRect() const { return ViewRect; }
	const FIntRect& GetConstrainedViewRect() const { return ConstrainedViewRect; }

	FMatrix ComputeViewProjectionMatrix() const
	{
		return FTranslationMatrix(-ViewOrigin) * ViewRotationMatrix * ProjectionMatrix;
	}
};

/** Method used for primary screen percentage method. */
enum class EPrimaryScreenPercentageMethod
{
	// Add spatial upscale pass at the end of post processing chain, before the secondary upscale.
	SpatialUpscale,

	// Let temporal AA's do the upscale.
	TemporalUpscale,

	// No upscaling or up sampling, just output the view rect smaller.
	// This is useful for VR's render thread dynamic resolution with MSAA.
	RawOutput,
};

/**
 * Method used for second screen percentage method, that is a second spatial upscale pass at the
 * very end, independent of screen percentage show flag.
 */
enum class ESecondaryScreenPercentageMethod
{
	// Helpful to work on aliasing issue on HighDPI monitors.
	NearestSpatialUpscale,

	// Upscale to simulate smaller pixel density on HighDPI monitors.
	LowerPixelDensitySimulation,

	// TODO: Same config as primary upscale?
};

// Construction parameters for a FSceneView
struct FSceneViewInitOptions : public FSceneViewProjectionData
{
	const FSceneViewFamily* ViewFamily;
	FSceneViewStateInterface* SceneViewStateInterface;
	const AActor* ViewActor;
	int32 PlayerIndex;
	FViewElementDrawer* ViewElementDrawer;

	FLinearColor BackgroundColor;
	FLinearColor OverlayColor;
	FLinearColor ColorScale;

	/** For stereoscopic rendering, whether or not this is a full pass, or a left / right eye pass */
	EStereoscopicPass StereoPass;

	/** For stereoscopic scene capture rendering. Half of the view's stereo IPD (- for lhs, + for rhs) */
	float StereoIPD;

	/** Conversion from world units (uu) to meters, so we can scale motion to the world appropriately */
	float WorldToMetersScale;

	TSet<FPrimitiveComponentId> HiddenPrimitives;

	/** The primitives which are visible for this view. If the array is not empty, all other primitives will be hidden. */
	TOptional<TSet<FPrimitiveComponentId>> ShowOnlyPrimitives;

	// -1,-1 if not setup
	FIntPoint CursorPos;

	float LODDistanceFactor;

	/** If > 0, overrides the view's far clipping plane with a plane at the specified distance. */
	float OverrideFarClippingPlaneDistance;

	/** World origin offset value. Non-zero only for a single frame when origin is rebased */
	FVector OriginOffsetThisFrame;

	/** Was there a camera cut this frame? */
	bool bInCameraCut;

	/** Whether to use FOV when computing mesh LOD. */
	bool bUseFieldOfViewForLOD;

	/** Actual field of view and that desired by the camera originally */
	float FOV;
	float DesiredFOV;

#if WITH_EDITOR
	/** default to 0'th view index, which is a bitfield of 1 */
	uint64 EditorViewBitflag;

	/** this can be specified for ortho views so that it's min draw distance/LOD parenting etc, is controlled by a perspective viewport */
	FVector OverrideLODViewOrigin;

	/** In case of ortho, generate a fake view position that has a non-zero W component. The view position will be derived based on the view matrix. */
	bool bUseFauxOrthoViewPos;

	/** Whether game screen percentage should be disabled. */
	bool bDisableGameScreenPercentage;
#endif

	FSceneViewInitOptions()
		: ViewFamily(NULL)
		, SceneViewStateInterface(NULL)
		, ViewActor(NULL)
		, PlayerIndex(INDEX_NONE)
		, ViewElementDrawer(NULL)
		, BackgroundColor(FLinearColor::Transparent)
		, OverlayColor(FLinearColor::Transparent)
		, ColorScale(FLinearColor::White)
		, StereoPass(eSSP_FULL)
		, StereoIPD(0.0f)
		, WorldToMetersScale(100.f)
		, CursorPos(-1, -1)
		, LODDistanceFactor(1.0f)
		, OverrideFarClippingPlaneDistance(-1.0f)
		, OriginOffsetThisFrame(ForceInitToZero)
		, bInCameraCut(false)
		, bUseFieldOfViewForLOD(true)
		, FOV(90.f)
		, DesiredFOV(90.f)
#if WITH_EDITOR
		, EditorViewBitflag(1)
		, OverrideLODViewOrigin(ForceInitToZero)
		, bUseFauxOrthoViewPos(false)
		, bDisableGameScreenPercentage(false)
		//@TODO: , const TBitArray<>& InSpriteCategoryVisibility=TBitArray<>()
#endif
	{
	}
};


//////////////////////////////////////////////////////////////////////////

struct FViewMatrices
{
	struct FMinimalInitializer
	{
		FMatrix ViewRotationMatrix = FMatrix::Identity;
		FMatrix ProjectionMatrix = FMatrix::Identity;
		FVector ViewOrigin = FVector::ZeroVector;
		FIntRect ConstrainedViewRect = FIntRect(0, 0, 0, 0);
		EStereoscopicPass StereoPass = eSSP_FULL;
#if WITH_EDITOR
		bool bUseFauxOrthoViewPos = false;
#endif
	};

	FViewMatrices()
	{
		ProjectionMatrix.SetIdentity();
		ViewMatrix.SetIdentity();
		HMDViewMatrixNoRoll.SetIdentity();
		TranslatedViewMatrix.SetIdentity();
		TranslatedViewProjectionMatrix.SetIdentity();
		InvTranslatedViewProjectionMatrix.SetIdentity();
		PreViewTranslation = FVector::ZeroVector;
		ViewOrigin = FVector::ZeroVector;
		ProjectionScale = FVector2D::ZeroVector;
		TemporalAAProjectionJitter = FVector2D::ZeroVector;
		ScreenScale = 1.f;
	}

	ENGINE_API FViewMatrices(const FMinimalInitializer& Initializer);
	ENGINE_API FViewMatrices(const FSceneViewInitOptions& InitOptions);

private:

	void Init(const FMinimalInitializer& Initializer);

	/** ViewToClip : UE4 projection matrix projects such that clip space Z=1 is the near plane, and Z=0 is the infinite far plane. */
	FMatrix		ProjectionMatrix;
	/** ViewToClipNoAA : UE4 projection matrix projects such that clip space Z=1 is the near plane, and Z=0 is the infinite far plane. Don't apply any AA jitter */
	FMatrix		ProjectionNoAAMatrix;
	/** ClipToView : UE4 projection matrix projects such that clip space Z=1 is the near plane, and Z=0 is the infinite far plane. */
	FMatrix		InvProjectionMatrix;
	// WorldToView..
	FMatrix		ViewMatrix;
	// ViewToWorld..
	FMatrix		InvViewMatrix;
	// WorldToClip : UE4 projection matrix projects such that clip space Z=1 is the near plane, and Z=0 is the infinite far plane. */
	FMatrix		ViewProjectionMatrix;
	// ClipToWorld : UE4 projection matrix projects such that clip space Z=1 is the near plane, and Z=0 is the infinite far plane. */
	FMatrix		InvViewProjectionMatrix;
	// HMD WorldToView with roll removed
	FMatrix		HMDViewMatrixNoRoll;
	/** WorldToView with PreViewTranslation. */
	FMatrix		TranslatedViewMatrix;
	/** ViewToWorld with PreViewTranslation. */
	FMatrix		InvTranslatedViewMatrix;
	/** WorldToView with PreViewTranslation. */
	FMatrix		OverriddenTranslatedViewMatrix;
	/** ViewToWorld with PreViewTranslation. */
	FMatrix		OverriddenInvTranslatedViewMatrix;
	/** The view-projection transform, starting from world-space points translated by -ViewOrigin. */
	FMatrix		TranslatedViewProjectionMatrix;
	/** The inverse view-projection transform, ending with world-space points translated by -ViewOrigin. */
	FMatrix		InvTranslatedViewProjectionMatrix;
	/** The translation to apply to the world before TranslatedViewProjectionMatrix. Usually it is -ViewOrigin but with rereflections this can differ */
	FVector		PreViewTranslation;
	/** To support ortho and other modes this is redundant, in world space */
	FVector		ViewOrigin;
	/** Scale applied by the projection matrix in X and Y. */
	FVector2D	ProjectionScale;
	/** TemporalAA jitter offset currently stored in the projection matrix */
	FVector2D	TemporalAAProjectionJitter;

	/**
	 * Scale factor to use when computing the size of a sphere in pixels.
	 * 
	 * A common calculation is to determine the size of a sphere in pixels when projected on the screen:
	 *		ScreenRadius = max(0.5 * ViewSizeX * ProjMatrix[0][0], 0.5 * ViewSizeY * ProjMatrix[1][1]) * SphereRadius / ProjectedSpherePosition.W
	 * Instead you can now simply use:
	 *		ScreenRadius = ScreenScale * SphereRadius / ProjectedSpherePosition.W
	 */
	float ScreenScale;

	//
	// World = TranslatedWorld - PreViewTranslation
	// TranslatedWorld = World + PreViewTranslation
	// 

	// ----------------

public:
	ENGINE_API void UpdateViewMatrix(const FVector& ViewLocation, const FRotator& ViewRotation);

	void UpdatePlanarReflectionViewMatrix(const FSceneView& SourceView, const FMirrorMatrix& MirrorMatrix);

	inline const FMatrix& GetProjectionMatrix() const
	{
		return ProjectionMatrix;
	}

	inline const FMatrix& GetProjectionNoAAMatrix() const
	{
		return ProjectionNoAAMatrix;
	}

	inline const FMatrix& GetInvProjectionMatrix() const
	{
		return InvProjectionMatrix;
	}

	inline const FMatrix& GetViewMatrix() const
	{
		return ViewMatrix;
	}

	inline const FMatrix& GetInvViewMatrix() const
	{
		return InvViewMatrix;
	}

	inline const FMatrix& GetViewProjectionMatrix() const
	{
		return ViewProjectionMatrix;
	}

	inline const FMatrix& GetInvViewProjectionMatrix() const
	{
		return InvViewProjectionMatrix;
	}
	
	inline const FMatrix& GetHMDViewMatrixNoRoll() const
	{
		return HMDViewMatrixNoRoll;
	}
	
	inline const FMatrix& GetTranslatedViewMatrix() const
	{
		return TranslatedViewMatrix;
	}

	inline const FMatrix& GetInvTranslatedViewMatrix() const
	{
		return InvTranslatedViewMatrix;
	}

	inline const FMatrix& GetOverriddenTranslatedViewMatrix() const
	{
		return OverriddenTranslatedViewMatrix;
	}

	inline const FMatrix& GetOverriddenInvTranslatedViewMatrix() const
	{
		return OverriddenInvTranslatedViewMatrix;
	}

	inline const FMatrix& GetTranslatedViewProjectionMatrix() const
	{
		return TranslatedViewProjectionMatrix;
	}

	inline const FMatrix& GetInvTranslatedViewProjectionMatrix() const
	{
		return InvTranslatedViewProjectionMatrix;
	}

	inline const FVector& GetPreViewTranslation() const
	{
		return PreViewTranslation;
	}
	
	inline const FVector& GetViewOrigin() const
	{
		return ViewOrigin;
	}

	inline float GetScreenScale() const
	{
		return ScreenScale;
	}

	inline const FVector2D& GetProjectionScale() const
	{
		return ProjectionScale;
	} 

	/** @return true:perspective, false:orthographic */
	inline bool IsPerspectiveProjection() const
	{
		return ProjectionMatrix.M[3][3] < 1.0f;
	}

	inline void HackOverrideViewMatrixForShadows(const FMatrix& InViewMatrix)
	{
		OverriddenTranslatedViewMatrix = ViewMatrix = InViewMatrix;
		OverriddenInvTranslatedViewMatrix = InViewMatrix.Inverse();
	}

	void SaveProjectionNoAAMatrix()
	{
		ProjectionNoAAMatrix = ProjectionMatrix;
	}

	void HackAddTemporalAAProjectionJitter(const FVector2D& InTemporalAAProjectionJitter)
	{
		ensure(TemporalAAProjectionJitter.X == 0.0f && TemporalAAProjectionJitter.Y == 0.0f);

		TemporalAAProjectionJitter = InTemporalAAProjectionJitter;

		ProjectionMatrix.M[2][0] += TemporalAAProjectionJitter.X;
		ProjectionMatrix.M[2][1] += TemporalAAProjectionJitter.Y;
		InvProjectionMatrix = InvertProjectionMatrix(ProjectionMatrix);

		RecomputeDerivedMatrices();
	}

	void HackRemoveTemporalAAProjectionJitter()
	{
		ProjectionMatrix.M[2][0] -= TemporalAAProjectionJitter.X;
		ProjectionMatrix.M[2][1] -= TemporalAAProjectionJitter.Y;
		InvProjectionMatrix = InvertProjectionMatrix(ProjectionMatrix);

		TemporalAAProjectionJitter = FVector2D::ZeroVector;
		RecomputeDerivedMatrices();
	}

	const FMatrix ComputeProjectionNoAAMatrix() const
	{
		FMatrix ProjNoAAMatrix = ProjectionMatrix;

		ProjNoAAMatrix.M[2][0] -= TemporalAAProjectionJitter.X;
		ProjNoAAMatrix.M[2][1] -= TemporalAAProjectionJitter.Y;

		return ProjNoAAMatrix;
	}

	inline const FVector2D GetTemporalAAJitter() const
	{
		return TemporalAAProjectionJitter;
	}

	const FMatrix ComputeViewRotationProjectionMatrix() const
	{
		return ViewMatrix.RemoveTranslation() * ProjectionMatrix;
	}
	
	const FMatrix ComputeInvProjectionNoAAMatrix() const
	{
		return InvertProjectionMatrix( ComputeProjectionNoAAMatrix() );
	}

	// @return in radians (horizontal,vertical)
	const FVector2D ComputeHalfFieldOfViewPerAxis() const
	{
		const FMatrix ClipToView = ComputeInvProjectionNoAAMatrix();

		FVector VCenter = FVector(ClipToView.TransformPosition(FVector(0.0, 0.0, 0.0)));
		FVector VUp = FVector(ClipToView.TransformPosition(FVector(0.0, 1.0, 0.0)));
		FVector VRight = FVector(ClipToView.TransformPosition(FVector(1.0, 0.0, 0.0)));

		VCenter.Normalize();
		VUp.Normalize();
		VRight.Normalize();

		return FVector2D(FMath::Acos(VCenter | VRight), FMath::Acos(VCenter | VUp));
	}

	void ApplyWorldOffset(const FVector& InOffset)
	{
		ViewOrigin+= InOffset;
		PreViewTranslation-= InOffset;
	
		ViewMatrix.SetOrigin(ViewMatrix.GetOrigin() + ViewMatrix.TransformVector(-InOffset));
		InvViewMatrix.SetOrigin(ViewOrigin);
		RecomputeDerivedMatrices();
	}

private:
	inline void RecomputeDerivedMatrices()
	{
		// Compute the view projection matrix and its inverse.
		ViewProjectionMatrix = GetViewMatrix() * GetProjectionMatrix();
		InvViewProjectionMatrix = GetInvProjectionMatrix() * GetInvViewMatrix();

		// Compute a transform from view origin centered world-space to clip space.
		TranslatedViewProjectionMatrix = GetTranslatedViewMatrix() * GetProjectionMatrix();
		InvTranslatedViewProjectionMatrix = GetInvProjectionMatrix() * GetInvTranslatedViewMatrix();
	}

	static const FMatrix InvertProjectionMatrix( const FMatrix& M )
	{
		if( M.M[1][0] == 0.0f &&
			M.M[3][0] == 0.0f &&
			M.M[0][1] == 0.0f &&
			M.M[3][1] == 0.0f &&
			M.M[0][2] == 0.0f &&
			M.M[1][2] == 0.0f &&
			M.M[0][3] == 0.0f &&
			M.M[1][3] == 0.0f &&
			M.M[2][3] == 1.0f &&
			M.M[3][3] == 0.0f )
		{
			// Solve the common case directly with very high precision.
			/*
			M = 
			| a | 0 | 0 | 0 |
			| 0 | b | 0 | 0 |
			| s | t | c | 1 |
			| 0 | 0 | d | 0 |
			*/

			double a = M.M[0][0];
			double b = M.M[1][1];
			double c = M.M[2][2];
			double d = M.M[3][2];
			double s = M.M[2][0];
			double t = M.M[2][1];

			return FMatrix(
				FPlane( 1.0 / a, 0.0f, 0.0f, 0.0f ),
				FPlane( 0.0f, 1.0 / b, 0.0f, 0.0f ),
				FPlane( 0.0f, 0.0f, 0.0f, 1.0 / d ),
				FPlane( -s/a, -t/b, 1.0f, -c/d )
			);
		}
		else
		{
			return M.Inverse();
		}
	}
};

//////////////////////////////////////////////////////////////////////////

static const int MAX_MOBILE_SHADOWCASCADES = 4;

/** The uniform shader parameters for a mobile directional light and its shadow.
  * One uniform buffer will be created for the first directional light in each lighting channel.
  */
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT_WITH_CONSTRUCTOR(FMobileDirectionalLightShaderParameters, ENGINE_API)
	SHADER_PARAMETER_EX(FLinearColor, DirectionalLightColor, EShaderPrecisionModifier::Half)
	SHADER_PARAMETER_EX(FVector4, DirectionalLightDirectionAndShadowTransition, EShaderPrecisionModifier::Half)
	SHADER_PARAMETER_EX(FVector4, DirectionalLightShadowSize, EShaderPrecisionModifier::Half)
	SHADER_PARAMETER_EX(FVector4, DirectionalLightDistanceFadeMADAndSpecularScale, EShaderPrecisionModifier::Half) // .z is used for SpecularScale, .w is used for OverlapWithStaticShadowAmount
	SHADER_PARAMETER_EX(FVector4, DirectionalLightShadowDistances, EShaderPrecisionModifier::Half)
	SHADER_PARAMETER_ARRAY(FMatrix, DirectionalLightScreenToShadow, [MAX_MOBILE_SHADOWCASCADES])
	SHADER_PARAMETER_TEXTURE(Texture2D, DirectionalLightShadowTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, DirectionalLightShadowSampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

//////////////////////////////////////////////////////////////////////////

/** 
 * Enumeration for currently used translucent lighting volume cascades 
 */
enum ETranslucencyVolumeCascade
{
	TVC_Inner,
	TVC_Outer,

	TVC_MAX,
};

// View uniform buffer member declarations
#define VIEW_UNIFORM_BUFFER_MEMBER_TABLE \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, TranslatedWorldToClip) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, WorldToClip) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, ClipToWorld)  \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, TranslatedWorldToView) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, ViewToTranslatedWorld) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, TranslatedWorldToCameraView) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, CameraViewToTranslatedWorld) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, ViewToClip) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, ViewToClipNoAA) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, ClipToView) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, ClipToTranslatedWorld) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, SVPositionToTranslatedWorld) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, ScreenToWorld) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, ScreenToTranslatedWorld) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, MobileMultiviewShadowTransform) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(FVector, ViewForward, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(FVector, ViewUp, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(FVector, ViewRight, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(FVector, HMDViewNoRollUp, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(FVector, HMDViewNoRollRight, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector4, InvDeviceZToWorldZTransform) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(FVector4, ScreenPositionScaleBias, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector, WorldCameraOrigin) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector, TranslatedWorldCameraOrigin) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector, WorldViewOrigin) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector, PreViewTranslation) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, PrevProjection) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, PrevViewProj) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, PrevViewRotationProj) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, PrevViewToClip) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, PrevClipToView) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, PrevTranslatedWorldToClip) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, PrevTranslatedWorldToView) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, PrevViewToTranslatedWorld) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, PrevTranslatedWorldToCameraView) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, PrevCameraViewToTranslatedWorld) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector, PrevWorldCameraOrigin) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector, PrevWorldViewOrigin) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector, PrevPreViewTranslation) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, PrevInvViewProj) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, PrevScreenToTranslatedWorld) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, ClipToPrevClip) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector4, TemporalAAJitter) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector4, GlobalClippingPlane) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector2D, FieldOfViewWideAngles) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector2D, PrevFieldOfViewWideAngles) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(FVector4, ViewRectMin, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector4, ViewSizeAndInvSize) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector4, LightProbeSizeRatioAndInvSizeRatio) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector4, BufferSizeAndInvSize) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector4, BufferBilinearUVMinMax) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector4, ScreenToViewSpace) \
	VIEW_UNIFORM_BUFFER_MEMBER(int32, NumSceneColorMSAASamples) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, PreExposure) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, OneOverPreExposure) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(FVector4, DiffuseOverrideParameter, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(FVector4, SpecularOverrideParameter, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(FVector4, NormalOverrideParameter, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(FVector2D, RoughnessOverrideParameter, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, PrevFrameGameTime) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, PrevFrameRealTime) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(float, OutOfBoundsMask, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector, WorldCameraMovementSinceLastFrame) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, CullingSign) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(float, NearPlane, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, AdaptiveTessellationFactor) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, GameTime) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, RealTime) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, DeltaTime) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, MaterialTextureMipBias) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, MaterialTextureDerivativeMultiply) \
	VIEW_UNIFORM_BUFFER_MEMBER(uint32, Random) \
	VIEW_UNIFORM_BUFFER_MEMBER(uint32, FrameNumber) \
	VIEW_UNIFORM_BUFFER_MEMBER(uint32, StateFrameIndexMod8) \
	VIEW_UNIFORM_BUFFER_MEMBER(uint32, StateFrameIndex) \
	VIEW_UNIFORM_BUFFER_MEMBER(uint32, DebugViewModeMask) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(float, CameraCut, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(float, UnlitViewmodeMask, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(FLinearColor, DirectionalLightColor, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(FVector, DirectionalLightDirection, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER_ARRAY(FVector4, TranslucencyLightingVolumeMin, [TVC_MAX]) \
	VIEW_UNIFORM_BUFFER_MEMBER_ARRAY(FVector4, TranslucencyLightingVolumeInvSize, [TVC_MAX]) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector4, TemporalAAParams) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector4, CircleDOFParams) \
	VIEW_UNIFORM_BUFFER_MEMBER(uint32, ForceDrawAllVelocities) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, DepthOfFieldSensorWidth) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, DepthOfFieldFocalDistance) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, DepthOfFieldScale) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, DepthOfFieldFocalLength) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, DepthOfFieldFocalRegion) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, DepthOfFieldNearTransitionRegion) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, DepthOfFieldFarTransitionRegion) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, MotionBlurNormalizedToPixel) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, bSubsurfacePostprocessEnabled) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, GeneralPurposeTweak) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(float, DemosaicVposOffset, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector, IndirectLightingColorScale) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(float, AtmosphericFogSunPower, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(float, AtmosphericFogPower, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(float, AtmosphericFogDensityScale, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(float, AtmosphericFogDensityOffset, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(float, AtmosphericFogGroundOffset, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(float, AtmosphericFogDistanceScale, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(float, AtmosphericFogAltitudeScale, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(float, AtmosphericFogHeightScaleRayleigh, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(float, AtmosphericFogStartDistance, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(float, AtmosphericFogDistanceOffset, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(float, AtmosphericFogSunDiscScale, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER_ARRAY(FVector4, AtmosphereLightDirection, [NUM_ATMOSPHERE_LIGHTS]) \
	VIEW_UNIFORM_BUFFER_MEMBER_ARRAY(FLinearColor, AtmosphereLightColor, [NUM_ATMOSPHERE_LIGHTS]) \
	VIEW_UNIFORM_BUFFER_MEMBER_ARRAY(FLinearColor, AtmosphereLightColorGlobalPostTransmittance, [NUM_ATMOSPHERE_LIGHTS]) \
	VIEW_UNIFORM_BUFFER_MEMBER_ARRAY(FLinearColor, AtmosphereLightDiscLuminance, [NUM_ATMOSPHERE_LIGHTS]) \
	VIEW_UNIFORM_BUFFER_MEMBER_ARRAY(FVector4, AtmosphereLightDiscCosHalfApexAngle, [NUM_ATMOSPHERE_LIGHTS]) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector4, SkyViewLutSizeAndInvSize) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector, SkyWorldCameraOrigin) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector4, SkyPlanetCenterAndViewHeight) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, SkyViewLutReferential) \
	VIEW_UNIFORM_BUFFER_MEMBER(FLinearColor, SkyAtmosphereSkyLuminanceFactor) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, SkyAtmospherePresentInScene) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, SkyAtmosphereHeightFogContribution) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, SkyAtmosphereBottomRadiusKm) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, SkyAtmosphereTopRadiusKm) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector4, SkyAtmosphereCameraAerialPerspectiveVolumeSizeAndInvSize) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, SkyAtmosphereAerialPerspectiveStartDepthKm) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, SkyAtmosphereCameraAerialPerspectiveVolumeDepthResolution) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, SkyAtmosphereCameraAerialPerspectiveVolumeDepthResolutionInv) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, SkyAtmosphereCameraAerialPerspectiveVolumeDepthSliceLengthKm) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, SkyAtmosphereCameraAerialPerspectiveVolumeDepthSliceLengthKmInv) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, SkyAtmosphereApplyCameraAerialPerspectiveVolume) \
	VIEW_UNIFORM_BUFFER_MEMBER(uint32, AtmosphericFogRenderMask) \
	VIEW_UNIFORM_BUFFER_MEMBER(uint32, AtmosphericFogInscatterAltitudeSampleNum) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector, NormalCurvatureToRoughnessScaleBias) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, RenderingReflectionCaptureMask) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, RealTimeReflectionCapture) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, RealTimeReflectionCapturePreExposure) \
	VIEW_UNIFORM_BUFFER_MEMBER(FLinearColor, AmbientCubemapTint) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, AmbientCubemapIntensity) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, SkyLightApplyPrecomputedBentNormalShadowingFlag) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, SkyLightAffectReflectionFlag) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, SkyLightAffectGlobalIlluminationFlag) \
	VIEW_UNIFORM_BUFFER_MEMBER(FLinearColor, SkyLightColor) \
	VIEW_UNIFORM_BUFFER_MEMBER_ARRAY(FVector4, MobileSkyIrradianceEnvironmentMap, [7]) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, MobilePreviewMode) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, HMDEyePaddingOffset) \
	VIEW_UNIFORM_BUFFER_MEMBER_EX(float, ReflectionCubemapMaxMip, EShaderPrecisionModifier::Half) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, ShowDecalsMask) \
	VIEW_UNIFORM_BUFFER_MEMBER(uint32, DistanceFieldAOSpecularOcclusionMode) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, IndirectCapsuleSelfShadowingIntensity) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector, ReflectionEnvironmentRoughnessMixingScaleBiasAndLargestWeight) \
	VIEW_UNIFORM_BUFFER_MEMBER(int32, StereoPassIndex) \
	VIEW_UNIFORM_BUFFER_MEMBER_ARRAY(FVector4, GlobalVolumeCenterAndExtent, [GMaxGlobalDistanceFieldClipmaps]) \
	VIEW_UNIFORM_BUFFER_MEMBER_ARRAY(FVector4, GlobalVolumeWorldToUVAddAndMul, [GMaxGlobalDistanceFieldClipmaps]) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, GlobalVolumeDimension) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, GlobalVolumeTexelSize) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, MaxGlobalDistance) \
	VIEW_UNIFORM_BUFFER_MEMBER(FIntPoint, CursorPosition) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, bCheckerboardSubsurfaceProfileRendering) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector, VolumetricFogInvGridSize) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector, VolumetricFogGridZParams) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector2D, VolumetricFogSVPosToVolumeUV) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, VolumetricFogMaxDistance) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector, VolumetricLightmapWorldToUVScale) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector, VolumetricLightmapWorldToUVAdd) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector, VolumetricLightmapIndirectionTextureSize) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, VolumetricLightmapBrickSize) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector, VolumetricLightmapBrickTexelSize) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, StereoIPD) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, IndirectLightingCacheShowFlag) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, EyeToPixelSpreadAngle) \
	VIEW_UNIFORM_BUFFER_MEMBER(FMatrix, WorldToVirtualTexture) \
	VIEW_UNIFORM_BUFFER_MEMBER_ARRAY(FVector4, XRPassthroughCameraUVs, [2]) \
	VIEW_UNIFORM_BUFFER_MEMBER(uint32, VirtualTextureFeedbackStride) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector4, RuntimeVirtualTextureMipLevel) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector2D, RuntimeVirtualTexturePackHeight) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector4, RuntimeVirtualTextureDebugParams) \
	VIEW_UNIFORM_BUFFER_MEMBER(int32, FarShadowStaticMeshLODBias) \
	VIEW_UNIFORM_BUFFER_MEMBER(float, MinRoughness) \
	VIEW_UNIFORM_BUFFER_MEMBER(FVector4, HairRenderInfo) \
	VIEW_UNIFORM_BUFFER_MEMBER(uint32, EnableSkyLight) \
	VIEW_UNIFORM_BUFFER_MEMBER(uint32, HairRenderInfoBits) \
	VIEW_UNIFORM_BUFFER_MEMBER(uint32, HairComponents) \

#define VIEW_UNIFORM_BUFFER_MEMBER(type, identifier) \
	SHADER_PARAMETER(type, identifier)

#define VIEW_UNIFORM_BUFFER_MEMBER_EX(type, identifier, precision) \
	SHADER_PARAMETER_EX(type, identifier, precision)

#define VIEW_UNIFORM_BUFFER_MEMBER_ARRAY(type, identifier, dimension) \
	SHADER_PARAMETER_ARRAY(type, identifier, dimension)

/** The uniform shader parameters associated with a view. */
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT_WITH_CONSTRUCTOR(FViewUniformShaderParameters, ENGINE_API)

	VIEW_UNIFORM_BUFFER_MEMBER_TABLE

	// Same as Wrap_WorldGroupSettings and Clamp_WorldGroupSettings, but with mipbias=MaterialTextureMipBias.
	SHADER_PARAMETER_SAMPLER(SamplerState, MaterialTextureBilinearWrapedSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, MaterialTextureBilinearClampedSampler)

	SHADER_PARAMETER_TEXTURE(Texture3D<uint4>, VolumetricLightmapIndirectionTexture) // FPrecomputedVolumetricLightmapLightingPolicy
	SHADER_PARAMETER_TEXTURE(Texture3D, VolumetricLightmapBrickAmbientVector) // FPrecomputedVolumetricLightmapLightingPolicy
	SHADER_PARAMETER_TEXTURE(Texture3D, VolumetricLightmapBrickSHCoefficients0) // FPrecomputedVolumetricLightmapLightingPolicy
	SHADER_PARAMETER_TEXTURE(Texture3D, VolumetricLightmapBrickSHCoefficients1) // FPrecomputedVolumetricLightmapLightingPolicy
	SHADER_PARAMETER_TEXTURE(Texture3D, VolumetricLightmapBrickSHCoefficients2) // FPrecomputedVolumetricLightmapLightingPolicy
	SHADER_PARAMETER_TEXTURE(Texture3D, VolumetricLightmapBrickSHCoefficients3) // FPrecomputedVolumetricLightmapLightingPolicy
	SHADER_PARAMETER_TEXTURE(Texture3D, VolumetricLightmapBrickSHCoefficients4) // FPrecomputedVolumetricLightmapLightingPolicy
	SHADER_PARAMETER_TEXTURE(Texture3D, VolumetricLightmapBrickSHCoefficients5) // FPrecomputedVolumetricLightmapLightingPolicy
	SHADER_PARAMETER_TEXTURE(Texture3D, SkyBentNormalBrickTexture) // FPrecomputedVolumetricLightmapLightingPolicy
	SHADER_PARAMETER_TEXTURE(Texture3D, DirectionalLightShadowingBrickTexture) // FPrecomputedVolumetricLightmapLightingPolicy

	SHADER_PARAMETER_SAMPLER(SamplerState, VolumetricLightmapBrickAmbientVectorSampler) // FPrecomputedVolumetricLightmapLightingPolicy
	SHADER_PARAMETER_SAMPLER(SamplerState, VolumetricLightmapTextureSampler0) // FPrecomputedVolumetricLightmapLightingPolicy
	SHADER_PARAMETER_SAMPLER(SamplerState, VolumetricLightmapTextureSampler1) // FPrecomputedVolumetricLightmapLightingPolicy
	SHADER_PARAMETER_SAMPLER(SamplerState, VolumetricLightmapTextureSampler2) // FPrecomputedVolumetricLightmapLightingPolicy
	SHADER_PARAMETER_SAMPLER(SamplerState, VolumetricLightmapTextureSampler3) // FPrecomputedVolumetricLightmapLightingPolicy
	SHADER_PARAMETER_SAMPLER(SamplerState, VolumetricLightmapTextureSampler4) // FPrecomputedVolumetricLightmapLightingPolicy
	SHADER_PARAMETER_SAMPLER(SamplerState, VolumetricLightmapTextureSampler5) // FPrecomputedVolumetricLightmapLightingPolicy
	SHADER_PARAMETER_SAMPLER(SamplerState, SkyBentNormalTextureSampler) // FPrecomputedVolumetricLightmapLightingPolicy
	SHADER_PARAMETER_SAMPLER(SamplerState, DirectionalLightShadowingTextureSampler) // FPrecomputedVolumetricLightmapLightingPolicy

	SHADER_PARAMETER_TEXTURE(Texture3D, GlobalDistanceFieldTexture0)
	SHADER_PARAMETER_SAMPLER(SamplerState, GlobalDistanceFieldSampler0)
	SHADER_PARAMETER_TEXTURE(Texture3D, GlobalDistanceFieldTexture1)
	SHADER_PARAMETER_SAMPLER(SamplerState, GlobalDistanceFieldSampler1)
	SHADER_PARAMETER_TEXTURE(Texture3D, GlobalDistanceFieldTexture2)
	SHADER_PARAMETER_SAMPLER(SamplerState, GlobalDistanceFieldSampler2)
	SHADER_PARAMETER_TEXTURE(Texture3D, GlobalDistanceFieldTexture3)
	SHADER_PARAMETER_SAMPLER(SamplerState, GlobalDistanceFieldSampler3)

	SHADER_PARAMETER_TEXTURE(Texture2D, AtmosphereTransmittanceTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, AtmosphereTransmittanceTextureSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, AtmosphereIrradianceTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, AtmosphereIrradianceTextureSampler)
	SHADER_PARAMETER_TEXTURE(Texture3D, AtmosphereInscatterTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, AtmosphereInscatterTextureSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, PerlinNoiseGradientTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, PerlinNoiseGradientTextureSampler)
	SHADER_PARAMETER_TEXTURE(Texture3D, PerlinNoise3DTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, PerlinNoise3DTextureSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D<uint>, SobolSamplingTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, SharedPointWrappedSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, SharedPointClampedSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, SharedBilinearWrappedSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, SharedBilinearClampedSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, SharedTrilinearWrappedSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, SharedTrilinearClampedSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, PreIntegratedBRDF)
	SHADER_PARAMETER_SAMPLER(SamplerState, PreIntegratedBRDFSampler)
	SHADER_PARAMETER_SRV(StructuredBuffer<float4>, PrimitiveSceneData)
	SHADER_PARAMETER_TEXTURE(Texture2D<float4>, PrimitiveSceneDataTexture)
	SHADER_PARAMETER_SRV(StructuredBuffer<float4>, LightmapSceneData)
	SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SkyIrradianceEnvironmentMap)

	SHADER_PARAMETER_TEXTURE(Texture2D, TransmittanceLutTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, TransmittanceLutTextureSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, SkyViewLutTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, SkyViewLutTextureSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, DistantSkyLightLutTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, DistantSkyLightLutTextureSampler)
	SHADER_PARAMETER_TEXTURE(Texture3D, CameraAerialPerspectiveVolume)
	SHADER_PARAMETER_SAMPLER(SamplerState, CameraAerialPerspectiveVolumeSampler)

	SHADER_PARAMETER_TEXTURE(Texture3D, HairScatteringLUTTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, HairScatteringLUTSampler)

	SHADER_PARAMETER_SRV(StructuredBuffer<float4>, WaterIndirection)
	SHADER_PARAMETER_SRV(StructuredBuffer<float4>, WaterData)

	SHADER_PARAMETER_UAV(RWBuffer<uint>, VTFeedbackBuffer)
	SHADER_PARAMETER_UAV(RWTexture2D<uint>, QuadOverdraw)

END_GLOBAL_SHADER_PARAMETER_STRUCT()

/** Copy of the view uniform shader parameters associated with a view for instanced stereo. */
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT_WITH_CONSTRUCTOR(FInstancedViewUniformShaderParameters, ENGINE_API)
	VIEW_UNIFORM_BUFFER_MEMBER_TABLE
END_GLOBAL_SHADER_PARAMETER_STRUCT()

#undef VIEW_UNIFORM_BUFFER_MEMBER_TABLE
#undef VIEW_UNIFORM_BUFFER_MEMBER
#undef VIEW_UNIFORM_BUFFER_MEMBER_EX
#undef VIEW_UNIFORM_BUFFER_MEMBER_ARRAY

namespace EDrawDynamicFlags
{
	enum Type : int32
	{
		None = 0,
		ForceLowestLOD = 0x1,
		FarShadowCascade = 0x2,
	};
}

/**
 * A projection from scene space into a 2D screen region.
 */
class ENGINE_API FSceneView
{
public:
	const FSceneViewFamily* Family;
	/** can be 0 (thumbnail rendering) */
	FSceneViewStateInterface* State;

	/** The uniform buffer for the view's parameters. This is only initialized in the rendering thread's copies of the FSceneView. */
	TUniformBufferRef<FViewUniformShaderParameters> ViewUniformBuffer;

	/** Mobile Directional Lighting uniform buffers, one for each lighting channel 
	  * The first is used for primitives with no lighting channels set.
	  * Only initialized in the rendering thread's copies of the FSceneView.
	  */
	TUniformBufferRef<FMobileDirectionalLightShaderParameters> MobileDirectionalLightUniformBuffers[NUM_LIGHTING_CHANNELS+1];

private:
	/** During GetDynamicMeshElements this will be the correct cull volume for shadow stuff */
	const FConvexVolume* DynamicMeshElementsShadowCullFrustum;
	/** If the above is non-null, a translation that is applied to world-space before transforming by one of the shadow matrices. */
	FVector		PreShadowTranslation;

public:
	FSceneViewInitOptions SceneViewInitOptions;

	/** The actor which is being viewed from. */
	const AActor* ViewActor;
	 
	/** Player index this view is associated with or INDEX_NONE. */
	int32 PlayerIndex;

	/** An interaction which draws the view's interaction elements. */
	FViewElementDrawer* Drawer;

	/* Final position of the view in the final render target (in pixels), potentially constrained by an aspect ratio requirement (black bars) */
	const FIntRect UnscaledViewRect;

	/* Raw view size (in pixels), used for screen space calculations */
	FIntRect UnconstrainedViewRect;

	/** Maximum number of shadow cascades to render with. */
	int32 MaxShadowCascades;

	FViewMatrices ViewMatrices;

	/** Variables used to determine the view matrix */
	FVector		ViewLocation;
	FRotator	ViewRotation;
	FQuat		BaseHmdOrientation;
	FVector		BaseHmdLocation;
	float		WorldToMetersScale;
	TOptional<FTransform> PreviousViewTransform;

	// normally the same as ViewMatrices unless "r.Shadow.FreezeCamera" is activated
	FViewMatrices ShadowViewMatrices;

	FMatrix ProjectionMatrixUnadjustedForRHI;

	FLinearColor BackgroundColor;
	FLinearColor OverlayColor;

	/** Color scale multiplier used during post processing */
	FLinearColor ColorScale;

	/** For stereoscopic rendering, whether or not this is a full pass, or a left / right eye pass */
	EStereoscopicPass StereoPass;

	/** Half of the view's stereo IPD (- for lhs, + for rhs) */
	float StereoIPD;

	/** Allow cross GPU transfer for this view */
	bool bAllowCrossGPUTransfer;

	/** Use custom GPUmask */
	bool bOverrideGPUMask;

	/** The GPU nodes on which to render this view. */
	FRHIGPUMask GPUMask;

	/** Whether this view should render the first instance only of any meshes using instancing. */
	bool bRenderFirstInstanceOnly;

	// Whether to use FOV when computing mesh LOD.
	bool bUseFieldOfViewForLOD;

	/** Actual field of view and that desired by the camera originally */
	float FOV;
	float DesiredFOV;

	EDrawDynamicFlags::Type DrawDynamicFlags;

	/** Current buffer visualization mode */
	FName CurrentBufferVisualizationMode;

	/** Current visualize calibration color material name */
	FName CurrentVisualizeCalibrationColorMaterialName;

	/** Current visualize calibration grayscale material name */
	FName CurrentVisualizeCalibrationGrayscaleMaterialName;

	/** Current visualize calibration custom material name */
	FName CurrentVisualizeCalibrationCustomMaterialName;

#if WITH_EDITOR
	/* Whether to use the pixel inspector */
	bool bUsePixelInspector;
#endif //WITH_EDITOR

	/**
	* These can be used to override material parameters across the scene without recompiling shaders.
	* The last component is how much to include of the material's value for that parameter, so 0 will completely remove the material's value.
	*/
	FVector4 DiffuseOverrideParameter;
	FVector4 SpecularOverrideParameter;
	FVector4 NormalOverrideParameter;
	FVector2D RoughnessOverrideParameter;

	/** Mip bias to apply in material's samplers. */
	float MaterialTextureMipBias;

	/** The primitives which are hidden for this view. */
	TSet<FPrimitiveComponentId> HiddenPrimitives;

	/** The primitives which are visible for this view. If the array is not empty, all other primitives will be hidden. */
	TOptional<TSet<FPrimitiveComponentId>> ShowOnlyPrimitives;

	// Derived members.

	bool bAllowTemporalJitter;

	FConvexVolume ViewFrustum;

	bool bHasNearClippingPlane;

	FPlane NearClippingPlane;

	float NearClippingDistance;

	/** true if ViewMatrix.Determinant() is negative. */
	bool bReverseCulling;

	/* Vector used by shaders to convert depth buffer samples into z coordinates in world space */
	FVector4 InvDeviceZToWorldZTransform;

	/** World origin offset value. Non-zero only for a single frame when origin is rebased */
	FVector OriginOffsetThisFrame;

	/** Multiplier for cull distance on objects */
	float LODDistanceFactor;

	/** Whether we did a camera cut for this view this frame. */
	bool bCameraCut;
	
	// -1,-1 if not setup
	FIntPoint CursorPos;

	/** True if this scene was created from a game world. */
	bool bIsGameView;

	/** For sanity checking casts that are assumed to be safe. */
	bool bIsViewInfo;

	/** Whether this view is being used to render a scene capture. */
	bool bIsSceneCapture;

	/** Whether this view uses ray tracing, for views that are used to render a scene capture. */
	bool bSceneCaptureUsesRayTracing;

	/** Whether this view is being used to render a reflection capture. */
	bool bIsReflectionCapture;

	/** Whether this view is being used to render a planar reflection. */
	bool bIsPlanarReflection;

	/** Whether this view is being used to render a runtime virtual texture. */
	bool bIsVirtualTexture;

	/** Whether this view is being used to render a high quality offline render */
	bool bIsOfflineRender;

	/** Whether to force two sided rendering for this view. */
	bool bRenderSceneTwoSided;

	/** Whether this view was created from a locked viewpoint. */
	bool bIsLocked;

	/** 
	 * Whether to only render static lights and objects.  
	 * This is used when capturing the scene for reflection captures, which aren't updated at runtime. 
	 */
	bool bStaticSceneOnly;

	/** True if instanced stereo is enabled. */
	bool bIsInstancedStereoEnabled;

	/** True if multi-view is enabled. */
	bool bIsMultiViewEnabled;

	/** True if mobile multi-view is enabled. */
	bool bIsMobileMultiViewEnabled;

	/** True if we need to bind the instanced view uniform buffer parameters. */
	bool bShouldBindInstancedViewUB;

	/** How far below the water surface this view is. -1 means the view is out of water. */
	float UnderwaterDepth;

	/** True if we need to force the camera to discard previous frames occlusion. Necessary for overlapped tile rendering
	 * where we discard previous frame occlusion because the projection matrix changes.
	 */
	bool bForceCameraVisibilityReset;

	/** Global clipping plane being applied to the scene, or all 0's if disabled.  This is used when rendering the planar reflection pass. */
	FPlane GlobalClippingPlane;

	/** Aspect ratio constrained view rect. In the editor, when attached to a camera actor and the camera black bar showflag is enabled, the normal viewrect 
	  * remains as the full viewport, and the black bars are just simulated by drawing black bars. This member stores the effective constrained area within the
	  * bars.
	 **/
	FIntRect CameraConstrainedViewRect;

	/** Sort axis for when TranslucentSortPolicy is SortAlongAxis */
	FVector TranslucentSortAxis;

	/** Translucent sort mode */
	TEnumAsByte<ETranslucentSortPolicy::Type> TranslucentSortPolicy;
	
	/** The frame index to override, useful for keeping determinism when rendering sequences. **/
	TOptional<uint32> OverrideFrameIndexValue;

	/** In some cases, the principal point of the lens is not at the center of the screen, especially for overlapped tile
	 *  rendering. So given a UV in [-1,1] viewport space, convert it to the [-1,1] viewport space of the lens using
	 *  LensUV = LensPrincipalPointOffsetScale.xy ScreenUV * LensPrincipalPointOffsetScale.zw;
	 *  This value is FVector4(0,0,1,1) unless overridden.
	 */
	FVector4 LensPrincipalPointOffsetScale;

#if WITH_EDITOR
	/** The set of (the first 64) groups' visibility info for this view */
	uint64 EditorViewBitflag;

	/** For ortho views, this can control how to determine LOD parenting (ortho has no "distance-to-camera") */
	FVector OverrideLODViewOrigin;

	/** True if we should draw translucent objects when rendering hit proxies */
	bool bAllowTranslucentPrimitivesInHitProxy;

	/** BitArray representing the visibility state of the various sprite categories in the editor for this view */
	TBitArray<> SpriteCategoryVisibility;
	/** Selection color for the editor (used by post processing) */
	FLinearColor SelectionOutlineColor;
	/** Selection color for use in the editor with inactive primitives */
	FLinearColor SubduedSelectionOutlineColor;
	/** True if any components are selected in isolation (independent of actor selection) */
	bool bHasSelectedComponents;
#endif

	/**
	 * The final settings for the current viewer position (blended together from many volumes).
	 * Setup by the main thread, passed to the render thread and never touched again by the main thread.
	 */
	FFinalPostProcessSettings FinalPostProcessSettings;

	// The antialiasing method.
	EAntiAliasingMethod AntiAliasingMethod;

	// Primary screen percentage method to use.
	EPrimaryScreenPercentageMethod PrimaryScreenPercentageMethod;

	/** Parameters for atmospheric fog. */
	FTextureRHIRef AtmosphereTransmittanceTexture;
	FTextureRHIRef AtmosphereIrradianceTexture;
	FTextureRHIRef AtmosphereInscatterTexture;

	/** Points to the view state's resources if a view state exists. */
	FForwardLightingViewResources* ForwardLightingResources;

	/** Water rendering related data */
	FShaderResourceViewRHIRef WaterIndirectionBuffer;
	FShaderResourceViewRHIRef WaterDataBuffer;

	/** Feature level for this scene */
	const ERHIFeatureLevel::Type FeatureLevel;

#if RHI_RAYTRACING
	FIESLightProfileResource* IESLightProfileResource;
#endif

protected:
	friend class FSceneRenderer;

public:

	static const int32 NumBufferedSubIsOccludedArrays = 2;
	TArray<bool> FrameSubIsOccluded[NumBufferedSubIsOccludedArrays];

	/** Initialization constructor. */
	FSceneView(const FSceneViewInitOptions& InitOptions);

#if DO_CHECK
	/** Verifies all the assertions made on members. */
	bool VerifyMembersChecks() const;
#endif

	FORCEINLINE bool AllowGPUParticleUpdate() const { return !bIsPlanarReflection && !bIsSceneCapture && !bIsReflectionCapture; }

	/** Transforms a point from world-space to the view's screen-space. */
	FVector4 WorldToScreen(const FVector& WorldPoint) const;

	/** Transforms a point from the view's screen-space to world-space. */
	FVector ScreenToWorld(const FVector4& ScreenPoint) const;

	/** Transforms a point from the view's screen-space into pixel coordinates relative to the view's X,Y. */
	bool ScreenToPixel(const FVector4& ScreenPoint,FVector2D& OutPixelLocation) const;

	/** Transforms a point from pixel coordinates relative to the view's X,Y (left, top) into the view's screen-space. */
	FVector4 PixelToScreen(float X,float Y,float Z) const;

	/** Transforms a point from the view's world-space into pixel coordinates relative to the view's X,Y (left, top). */
	bool WorldToPixel(const FVector& WorldPoint,FVector2D& OutPixelLocation) const;

	/** Transforms a point from pixel coordinates relative to the view's X,Y (left, top) into the view's world-space. */
	FVector4 PixelToWorld(float X,float Y,float Z) const;

	/** 
	 * Transforms a point from the view's world-space into the view's screen-space. 
	 * Divides the resulting X, Y, Z by W before returning. 
	 */
	FPlane Project(const FVector& WorldPoint) const;

	/** 
	 * Transforms a point from the view's screen-space into world coordinates
	 * multiplies X, Y, Z by W before transforming. 
	 */
	FVector Deproject(const FPlane& ScreenPoint) const;

	/** 
	 * Transforms 2D screen coordinates into a 3D world-space origin and direction 
	 * @param ScreenPos - screen coordinates in pixels
	 * @param out_WorldOrigin (out) - world-space origin vector
	 * @param out_WorldDirection (out) - world-space direction vector
	 */
	void DeprojectFVector2D(const FVector2D& ScreenPos, FVector& out_WorldOrigin, FVector& out_WorldDirection) const;

	/** 
	 * Transforms 2D screen coordinates into a 3D world-space origin and direction 
	 * @param ScreenPos - screen coordinates in pixels
	 * @param ViewRect - view rectangle
	 * @param InvViewMatrix - inverse view matrix
	 * @param InvProjMatrix - inverse projection matrix
	 * @param out_WorldOrigin (out) - world-space origin vector
	 * @param out_WorldDirection (out) - world-space direction vector
	 */
	static void DeprojectScreenToWorld(const FVector2D& ScreenPos, const FIntRect& ViewRect, const FMatrix& InvViewMatrix, const FMatrix& InvProjMatrix, FVector& out_WorldOrigin, FVector& out_WorldDirection);

	/** Overload to take a single combined view projection matrix. */
	static void DeprojectScreenToWorld(const FVector2D& ScreenPos, const FIntRect& ViewRect, const FMatrix& InvViewProjMatrix, FVector& out_WorldOrigin, FVector& out_WorldDirection);

	/** 
	 * Transforms 3D world-space origin into 2D screen coordinates
	 * @param WorldPosition - the 3d world point to transform
	 * @param ViewRect - view rectangle
	 * @param ViewProjectionMatrix - combined view projection matrix
	 * @param out_ScreenPos (out) - screen coordinates in pixels
	 */
	static bool ProjectWorldToScreen(const FVector& WorldPosition, const FIntRect& ViewRect, const FMatrix& ViewProjectionMatrix, FVector2D& out_ScreenPos);

	inline FVector GetViewRight() const { return ViewMatrices.GetViewMatrix().GetColumn(0); }
	inline FVector GetViewUp() const { return ViewMatrices.GetViewMatrix().GetColumn(1); }
	inline FVector GetViewDirection() const { return ViewMatrices.GetViewMatrix().GetColumn(2); }

	inline const FConvexVolume* GetDynamicMeshElementsShadowCullFrustum() const { return DynamicMeshElementsShadowCullFrustum; }
	inline void SetDynamicMeshElementsShadowCullFrustum(const FConvexVolume* InDynamicMeshElementsShadowCullFrustum) { DynamicMeshElementsShadowCullFrustum = InDynamicMeshElementsShadowCullFrustum; }

	inline const FVector& GetPreShadowTranslation() const { return PreShadowTranslation; }
	inline void SetPreShadowTranslation(const FVector& InPreShadowTranslation) { PreShadowTranslation = InPreShadowTranslation; }

	/** @return true:perspective, false:orthographic */
	inline bool IsPerspectiveProjection() const { return ViewMatrices.IsPerspectiveProjection(); }

	bool IsUnderwater() const { return UnderwaterDepth > 0.0f; }

	/** Returns the location used as the origin for LOD computations
	 * @param Index, 0 or 1, which LOD origin to return
	 * @return LOD origin
	 */
	FVector GetTemporalLODOrigin(int32 Index, bool bUseLaggedLODTransition = true) const;

	/** 
	 * Returns the blend factor between the last two LOD samples
	 */
	float GetTemporalLODTransition() const;

	/** 
	 * returns a unique key for the view state if one exists, otherwise returns zero
	 */
	uint32 GetViewKey() const;

	/** 
	 * returns a the occlusion frame counter or MAX_uint32 if there is no view state
	 */
	uint32 GetOcclusionFrameCounter() const;

	void UpdateProjectionMatrix(const FMatrix& NewProjectionMatrix);

	/** Allow things like HMD displays to update the view matrix at the last minute, to minimize perceived latency */
	void UpdateViewMatrix();

	/** If we late update a view, we need to also late update any planar reflection views derived from it */
	void UpdatePlanarReflectionViewMatrix(const FSceneView& SourceView, const FMirrorMatrix& MirrorMatrix);

	/** Setup defaults and depending on view position (postprocess volumes) */
	void StartFinalPostprocessSettings(FVector InViewLocation);

	/**
	 * custom layers can be combined with the existing settings
	 * @param Weight usually 0..1 but outside range is clamped
	 */
	void OverridePostProcessSettings(const FPostProcessSettings& Src, float Weight);

	/** applied global restrictions from show flags */
	void EndFinalPostprocessSettings(const FSceneViewInitOptions& ViewInitOptions);

	void SetupAntiAliasingMethod();

	/** Configure post process settings for the buffer visualization system */
	void ConfigureBufferVisualizationSettings();

#if !(UE_BUILD_SHIPPING)
	/** Configure post process settings for calibration material */
	void ConfigureVisualizeCalibrationSettings();
#endif

	/** Get the feature level for this view (cached from the scene so this is not different per view) **/
	ERHIFeatureLevel::Type GetFeatureLevel() const { return FeatureLevel; }

	/** Get the feature level for this view **/
	EShaderPlatform GetShaderPlatform() const;

	/** True if the view should render as an instanced stereo pass */
	bool IsInstancedStereoPass() const;

	/** Sets up the view rect parameters in the view's uniform shader parameters */
	void SetupViewRectUniformBufferParameters(FViewUniformShaderParameters& ViewUniformShaderParameters, 
		const FIntPoint& InBufferSize,
		const FIntRect& InEffectiveViewRect,
		const FViewMatrices& InViewMatrices,
		const FViewMatrices& InPrevViewMatrice) const;

	/** 
	 * Populates the uniform buffer prameters common to all scene view use cases
	 * View parameters should be set up in this method if they are required for the view to render properly.
	 * This is to avoid code duplication and uninitialized parameters in other places that create view uniform parameters (e.g Slate) 
	 */
	void SetupCommonViewUniformBufferParameters(FViewUniformShaderParameters& ViewUniformShaderParameters,
		const FIntPoint& InBufferSize,
		int32 NumMSAASamples,
		const FIntRect& InEffectiveViewRect,
		const FViewMatrices& InViewMatrices,
		const FViewMatrices& InPrevViewMatrices) const;

#if RHI_RAYTRACING
	/** Setup ray tracing based rendering */
	void SetupRayTracedRendering();

	ERayTracingRenderMode RayTracingRenderMode = ERayTracingRenderMode::Disabled;

	/** Current ray tracing debug visualization mode */
	FName CurrentRayTracingDebugVisualizationMode;
#endif
	/** Tells if the eye adaptation texture / buffer exists without attempting to allocate it. */
	bool HasValidEyeAdaptationTexture() const;
	bool HasValidEyeAdaptationBuffer() const;

	/** Returns the eye adaptation texture (SM5+ only) or null if it doesn't exist. */
	IPooledRenderTarget* GetEyeAdaptationTexture() const;

	/** Returns the eye adaptation buffer (mobile) or null if it doesn't exist. */
	const FExposureBufferData* GetEyeAdaptationBuffer() const;


protected:
	FSceneViewStateInterface* EyeAdaptationViewState = nullptr;
};

//////////////////////////////////////////////////////////////////////////

// for r.DisplayInternals (allows for easy passing down data from main to render thread)
struct FDisplayInternalsData
{
	//
	int32 DisplayInternalsCVarValue;
	// current time Matinee location (in seconds) of the single playing playing actor, -1 if none is playing, -2 if multiple are playing
	float MatineeTime;
	// -1 if not set, from IStreamingManager::Get().StreamAllResources(Duration) in FStreamAllResourcesLatentCommand
	uint32 NumPendingStreamingRequests;

	FDisplayInternalsData()
		: DisplayInternalsCVarValue(0)
		, MatineeTime(-1.0f)
		, NumPendingStreamingRequests(-1)
	{
		check(!IsValid());
	}

	// called on main thread
	// @param World may be 0
	void Setup(UWorld *World);

	bool IsValid() const { return DisplayInternalsCVarValue != 0; }
};

//////////////////////////////////////////////////////////////////////////

/** Exposed screen percentage settings to ISceneViewFamilyScreenPercentage. */
struct FSceneViewScreenPercentageConfig
{
	// Screen percentage / 100 to apply to a given view of the view family.
	float PrimaryResolutionFraction;


	FSceneViewScreenPercentageConfig()
		: PrimaryResolutionFraction(1.f)
	{ }


	// Sets the minimal and max screen percentage.
	static constexpr float kMinResolutionFraction = 0.01f;
	static constexpr float kMaxResolutionFraction = 4.0f;

	// Sets the minimal and maximal screen percentage for TAA upsample.
	static constexpr float kMinTAAUpsampleResolutionFraction = 0.5f;
	static constexpr float kMaxTAAUpsampleResolutionFraction = 2.0f;


	#if DO_CHECK
		static bool IsValidResolutionFraction(float ResolutionFraction)
		{
			return ResolutionFraction >= kMinResolutionFraction && ResolutionFraction <= kMaxResolutionFraction;
		}
	#endif
};


/*
 * Game thread and render thread interface that takes care of a FSceneViewFamily's screen percentage.
 *
 * The renderer reserves the right to delete and replace the view family's screen percentage interface
 * for testing purposes with the r.Test.OverrideScreenPercentageInterface CVar.
 */
class ENGINE_API ISceneViewFamilyScreenPercentage
{
protected:
	/** 
	 * Called by the destructor of the view family.
	 * Can be called on game or rendering thread.
	 */
	virtual ~ISceneViewFamilyScreenPercentage() {};

	/** 
	 * Method to know the maximum value that can be set in FSceneViewScreenPercentageConfig::ResolutionFraction.
	 * Can be called on game or rendering thread. This should return >= 1 if screen percentage show flag is disabled.
	 */
	virtual float GetPrimaryResolutionFractionUpperBound() const = 0;

	/** Create a new screen percentage interface for a new view family. */
	virtual ISceneViewFamilyScreenPercentage* Fork_GameThread(const class FSceneViewFamily& ViewFamily) const = 0;

	/**
	 * Setup view family's view's screen percentage on rendering thread.
	 * This should leave ResolutionFraction == 1 if screen percentage show flag is disabled.
	 * @param OutViewScreenPercentageConfigs Screen percentage config to set on the view of the view family.
	 */
	virtual void ComputePrimaryResolutionFractions_RenderThread(TArray<FSceneViewScreenPercentageConfig>& OutViewScreenPercentageConfigs) const = 0;

	friend class FSceneViewFamily;
	friend class FSceneRenderer;
};


//////////////////////////////////////////////////////////////////////////

/**
 * A set of views into a scene which only have different view transforms and owner actors.
 */
class ENGINE_API FSceneViewFamily
{
public:
	/**
	* Helper struct for creating FSceneViewFamily instances
	* If created with specifying a time it will retrieve them from the world in the given scene.
	* 
	* @param InRenderTarget		The render target which the views are being rendered to.
	* @param InScene			The scene being viewed.
	* @param InShowFlags		The show flags for the views.
	*
	*/
	struct ConstructionValues
	{
		ConstructionValues(
			const FRenderTarget* InRenderTarget,
			FSceneInterface* InScene,
			const FEngineShowFlags& InEngineShowFlags
			)
		:	RenderTarget(InRenderTarget)
		,	Scene(InScene)
		,	EngineShowFlags(InEngineShowFlags)
		,	ViewModeParam(-1)
		,	CurrentWorldTime(0.0f)
		,	DeltaWorldTime(0.0f)
		,	CurrentRealTime(0.0f)
		,	GammaCorrection(1.0f)
		,	bAdditionalViewFamily(false)
		,	bRealtimeUpdate(false)
		,	bDeferClear(false)
		,	bResolveScene(true)			
		,	bTimesSet(false)
		{
			if( InScene != NULL )			
			{
				UWorld* World = InScene->GetWorld();
				// Ensure the world is valid and that we are being called from a game thread (GetRealTimeSeconds requires this)
				if( World && IsInGameThread() )
				{					
					CurrentWorldTime = World->GetTimeSeconds();
					DeltaWorldTime = World->GetDeltaSeconds();
					CurrentRealTime = World->GetRealTimeSeconds();
					bTimesSet = true;
				}
			}
		}
		/** The views which make up the family. */
		const FRenderTarget* RenderTarget;

		/** The render target which the views are being rendered to. */
		FSceneInterface* Scene;

		/** The engine show flags for the views. */
		FEngineShowFlags EngineShowFlags;

		/** Additional view params related to the current viewmode (example : texcoord index) */
		int32 ViewModeParam;
		/** An name bound to the current viewmode param. (example : texture name) */
		FName ViewModeParamName;

		/** The current world time. */
		float CurrentWorldTime;

		/** The difference between the last world time and CurrentWorldTime. */
		float DeltaWorldTime;
		
		/** The current real time. */
		float CurrentRealTime;

		/** Gamma correction used when rendering this family. Default is 1.0 */
		float GammaCorrection;

		/** Indicates whether the view family is additional. */
		uint32 bAdditionalViewFamily : 1;

		/** Indicates whether the view family is updated in real-time. */
		uint32 bRealtimeUpdate:1;
		
		/** Used to defer the back buffer clearing to just before the back buffer is drawn to */
		uint32 bDeferClear:1;
		
		/** If true then results of scene rendering are copied/resolved to the RenderTarget. */
		uint32 bResolveScene:1;		
		
		/** Safety check to ensure valid times are set either from a valid world/scene pointer or via the SetWorldTimes function */
		uint32 bTimesSet:1;

		/** Set the world time ,difference between the last world time and CurrentWorldTime and current real time. */
		ConstructionValues& SetWorldTimes(const float InCurrentWorldTime,const float InDeltaWorldTime,const float InCurrentRealTime) { CurrentWorldTime = InCurrentWorldTime; DeltaWorldTime = InDeltaWorldTime; CurrentRealTime = InCurrentRealTime;bTimesSet = true;return *this; }

		/** Set  whether the view family is additional. */
		ConstructionValues& SetAdditionalViewFamily(const bool Value) { bAdditionalViewFamily = Value; return *this; }

		/** Set  whether the view family is updated in real-time. */
		ConstructionValues& SetRealtimeUpdate(const bool Value) { bRealtimeUpdate = Value; return *this; }
		
		/** Set whether to defer the back buffer clearing to just before the back buffer is drawn to */
		ConstructionValues& SetDeferClear(const bool Value) { bDeferClear = Value; return *this; }
		
		/** Setting to if true then results of scene rendering are copied/resolved to the RenderTarget. */
		ConstructionValues& SetResolveScene(const bool Value) { bResolveScene = Value; return *this; }
		
		/** Set Gamma correction used when rendering this family. */
		ConstructionValues& SetGammaCorrection(const float Value) { GammaCorrection = Value; return *this; }		

		/** Set the view param. */
		ConstructionValues& SetViewModeParam(const int InViewModeParam, const FName& InViewModeParamName) { ViewModeParam = InViewModeParam; ViewModeParamName = InViewModeParamName; return *this; }		
	};
	
	/** The views which make up the family. */
	TArray<const FSceneView*> Views;

	/** View mode of the family. */
	EViewModeIndex ViewMode;

	/** The render target which the views are being rendered to. */
	const FRenderTarget* RenderTarget;

	/** The scene being viewed. */
	FSceneInterface* Scene;

	/** The new show flags for the views (meant to replace the old system). */
	FEngineShowFlags EngineShowFlags;

	/** The current world time. */
	float CurrentWorldTime;

	/** The difference between the last world time and CurrentWorldTime. */
	float DeltaWorldTime;

	/** The current real time. */
	float CurrentRealTime;

	/** Copy from main thread GFrameNumber to be accessible on render thread side. UINT_MAX before CreateSceneRenderer() or BeginRenderingViewFamily() was called */
	uint32 FrameNumber;

	/** Indicates this view family is an additional one. */
	bool bAdditionalViewFamily;

	/** Indicates whether the view family is updated in realtime. */
	bool bRealtimeUpdate;

	/** Used to defer the back buffer clearing to just before the back buffer is drawn to */
	bool bDeferClear;

	/** if true then results of scene rendering are copied/resolved to the RenderTarget. */
	bool bResolveScene;

	/** if true then each view is not rendered using the same GPUMask. */
	bool bMultiGPUForkAndJoin;

	/** 
	 * Which component of the scene rendering should be output to the final render target.
	 * If SCS_FinalColorLDR this indicates do nothing.
	 */
	ESceneCaptureSource SceneCaptureSource;
	

	/** When enabled, the scene capture will composite into the render target instead of overwriting its contents. */
	ESceneCaptureCompositeMode SceneCaptureCompositeMode;

	/**
	 * GetWorld->IsPaused() && !Simulate
	 * Simulate is excluded as the camera can move which invalidates motionblur
	 */
	bool bWorldIsPaused;

	/** When enabled, the post processing will output in HDR space */
	bool bIsHDR;

	/** True if scenecolor and depth should be multiview-allocated */
	bool bRequireMultiView;

	/** Gamma correction used when rendering this family. Default is 1.0 */
	float GammaCorrection;
	
	/** Editor setting to allow designers to override the automatic expose. 0:Automatic, following indices: -4 .. +4 */
	FExposureSettings ExposureSettings;

    /** Extensions that can modify view parameters on the render thread. */
    TArray<TSharedRef<class ISceneViewExtension, ESPMode::ThreadSafe> > ViewExtensions;

	// for r.DisplayInternals (allows for easy passing down data from main to render thread)
	FDisplayInternalsData DisplayInternalsData;

	/**
	 * Secondary view fraction to support High DPI monitor still with same primary screen percentage range for temporal
	 * upscale to test content consistently in editor no mater of the HighDPI scale. 
	 */
	float SecondaryViewFraction;
	ESecondaryScreenPercentageMethod SecondaryScreenPercentageMethod;

#if WITH_EDITOR
	// Override the LOD of landscape in this viewport
	int8 LandscapeLODOverride;

	/** Indicates whether, or not, the base attachment volume should be drawn. */
	bool bDrawBaseInfo;

	/**
	 * Indicates whether the shader world space position should be forced to 0. Also sets the view vector to (0,0,1) for all pixels.
	 * This is used in the texture streaming build when computing material tex coords scale.
	 * Because the material are rendered in tiles, there is no actual valid mapping for world space position.
	 * World space mapping would require to render mesh with the level transforms to be valid.
	 */
	bool bNullifyWorldSpacePosition;
#endif

	/** Initialization constructor. */
	FSceneViewFamily( const ConstructionValues& CVS );
	~FSceneViewFamily();

	ERHIFeatureLevel::Type GetFeatureLevel() const;

	EShaderPlatform GetShaderPlatform() const { return GShaderPlatformForFeatureLevel[GetFeatureLevel()]; }

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	EDebugViewShaderMode DebugViewShaderMode;
	int32 ViewModeParam;
	FName ViewModeParamName;

	bool bUsedDebugViewVSDSHS;
	FORCEINLINE EDebugViewShaderMode GetDebugViewShaderMode() const { return DebugViewShaderMode; }
	FORCEINLINE int32 GetViewModeParam() const { return ViewModeParam; }
	FORCEINLINE const FName& GetViewModeParamName() const { return ViewModeParamName; }
	EDebugViewShaderMode ChooseDebugViewShaderMode() const;
	FORCEINLINE bool UseDebugViewVSDSHS() const { return bUsedDebugViewVSDSHS; }
	FORCEINLINE bool UseDebugViewPS() const { return DebugViewShaderMode != DVSM_None; }
#else
	FORCEINLINE EDebugViewShaderMode GetDebugViewShaderMode() const { return DVSM_None; }
	FORCEINLINE int32 GetViewModeParam() const { return -1; }
	FORCEINLINE FName GetViewModeParamName() const { return NAME_None; }
	FORCEINLINE bool UseDebugViewVSDSHS() const { return false; }
	FORCEINLINE bool UseDebugViewPS() const { return false; }
#endif

	/** Returns the appropriate view for a given eye in a stereo pair. */
	const FSceneView& GetStereoEyeView(const EStereoscopicPass Eye) const;

	/** Returns whether the screen percentage show flag is supported or not for this view family. */
	bool SupportsScreenPercentage() const;

	bool AllowTranslucencyAfterDOF() const;

	/* Returns the maximum FSceneViewScreenPercentageConfig::PrimaryResolutionFraction. */
	FORCEINLINE float GetPrimaryResolutionFractionUpperBound() const
	{
		check(ScreenPercentageInterface != nullptr);
		float PrimaryUpperBoundFraction = ScreenPercentageInterface->GetPrimaryResolutionFractionUpperBound();

		checkf(FSceneViewScreenPercentageConfig::IsValidResolutionFraction(PrimaryUpperBoundFraction),
			TEXT("ISceneViewFamilyScreenPercentage::GetPrimaryResolutionFractionUpperBound()")
			TEXT(" should return a valide value."));

		if (!EngineShowFlags.ScreenPercentage)
		{
			checkf(PrimaryUpperBoundFraction >= 1.0f,
				TEXT("ISceneViewFamilyScreenPercentage::GetPrimaryResolutionFractionUpperBound()")
				TEXT(" should return >= 1 if screen percentage show flag is off."));
		}

		return PrimaryUpperBoundFraction;
	}

	FORCEINLINE const ISceneViewFamilyScreenPercentage* GetScreenPercentageInterface() const
	{
		return ScreenPercentageInterface;
	}

	/**
	 * Safely sets the view family's screen percentage interface.
	 * This is meant to be set by one of the ISceneViewExtension::BeginRenderViewFamily(). And collision will
	 * automatically be detected. If no extension sets it, that is fine since the renderer is going to use an
	 * internal default one.
	 *
	 * The renderer reserves the right to delete and replace the view family's screen percentage interface
	 * for testing purposes with the r.Test.OverrideScreenPercentageInterface CVar.
	 */
	FORCEINLINE void SetScreenPercentageInterface(ISceneViewFamilyScreenPercentage* InScreenPercentageInterface)
	{
		check(InScreenPercentageInterface);
		checkf(ScreenPercentageInterface == nullptr, TEXT("View family already had a screen percentage interface assigned."));
		ScreenPercentageInterface = InScreenPercentageInterface;
	}

	// View family assignment operator is not allowed because of ScreenPercentageInterface lifetime.
	void operator = (const FSceneViewFamily&) = delete;

	// Allow moving view family as long as no screen percentage interface are set.
	FSceneViewFamily(const FSceneViewFamily&& InViewFamily)
		: FSceneViewFamily(static_cast<const FSceneViewFamily&>(InViewFamily))
	{
		check(ScreenPercentageInterface == nullptr);
		check(TemporalUpscalerInterface == nullptr);
	}


	FORCEINLINE void SetTemporalUpscalerInterface(const ITemporalUpscaler* InTemporalUpscalerInterface)
	{
		check(InTemporalUpscalerInterface);
		checkf(TemporalUpscalerInterface == nullptr, TEXT("View family already had a temporal upscaler assigned."));
		TemporalUpscalerInterface = InTemporalUpscalerInterface;
	}

	FORCEINLINE const ITemporalUpscaler* GetTemporalUpscalerInterface() const
	{
		return TemporalUpscalerInterface;
	}

private:
	/** Interface to handle screen percentage of the views of the family. */
	ISceneViewFamilyScreenPercentage* ScreenPercentageInterface;

	const ITemporalUpscaler* TemporalUpscalerInterface;

	// Only FSceneRenderer can copy a view family.
	FSceneViewFamily(const FSceneViewFamily&) = default;

	friend class FSceneRenderer;
};

/**
 * A view family which deletes its views when it goes out of scope.
 */
class FSceneViewFamilyContext : public FSceneViewFamily
{
public:
	/** Initialization constructor. */
	FSceneViewFamilyContext( const ConstructionValues& CVS)
		:	FSceneViewFamily(CVS)
	{}

	/** Destructor. */
	ENGINE_API ~FSceneViewFamilyContext();
};
