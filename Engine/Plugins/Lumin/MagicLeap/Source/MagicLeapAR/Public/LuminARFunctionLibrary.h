// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/LatentActionManager.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "LuminARTypes.h"
#include "LuminARSessionConfig.h"
#include "LuminARFunctionLibrary.generated.h"

/** A function library that provides static/Blueprint functions associated with LuminAR session.*/
UCLASS()
class MAGICLEAPAR_API ULuminARSessionFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	//-----------------Lifecycle---------------------

	/**
	 * Starts a new LuminAR tracking session LuminAR specific configuration.
	 * If the session already started and the config isn't the same, it will stop the previous session and start a new session with the new config.
	 * Note that this is a latent action, you can query the session start result by querying GetLuminARSessionStatus() after the latent action finished.
	 *
	 * @param Configuration				The LuminARSession configuration to start the session.
	 */
	UFUNCTION(BlueprintCallable, Category = "LuminAR|Session", meta = (Latent, LatentInfo = "LatentInfo", WorldContext = "WorldContextObject", Keywords = "luminar session start config"))
	static void StartLuminARSession(UObject* WorldContextObject, struct FLatentActionInfo LatentInfo, ULuminARSessionConfig* Configuration);
};

/** A function library that provides static/Blueprint functions associated with most recent LuminAR tracking frame.*/
UCLASS()
class MAGICLEAPAR_API ULuminARFrameFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Returns the current ARCore session status.
	 *
	 * @return	A EARSessionStatus enum that describes the session status.
	 */
	UFUNCTION(BlueprintPure, Category = "LuminAR|MotionTracking", meta = (DeprecatedFunction, DeprecationMessage = "Use GetTrackingQuality() & GetHeadTrackingMapEvents()", Keywords = "luminar session"))
	static ELuminARTrackingState GetTrackingState();

	/**
	 * Traces a ray from the user's device in the direction of the given location in the camera
	 * view. Intersections with detected scene geometry are returned, sorted by distance from the
	 * device; the nearest intersection is returned first.
	 *
	 * @param WorldContextObject	The world context.
	 * @param ScreenPosition		The position on the screen to cast the ray from.
	 * @param ARObjectType			A set of ELuminARLineTraceChannel indicate which type of line trace it should perform.
	 * @param OutHitResults			The list of hit results sorted by distance.
	 * @return						True if there is a hit detected.
	 */
	UFUNCTION(BlueprintCallable, Category = "LuminAR|LineTrace", meta = (WorldContext = "WorldContextObject", Keywords = "luminar raycast hit"))
	static bool LuminARLineTrace(UObject* WorldContextObject, const FVector2D& ScreenPosition, TSet<ELuminARLineTraceChannel> TraceChannels, TArray<FARTraceResult>& OutHitResults);
};

UCLASS()
class MAGICLEAPAR_API ULuminARImageTrackingFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Create a LuminARCandidateImage object and add it to the ARCandidateImageList of the given \c UARSessionConfig object.
	 *
	 * Note that you need to restart the AR session with the \c UARSessionConfig you are adding to to make the change take effect.
	 *
	 * On ARCore platform, you can leave the PhysicalWidth to 0 if you don't know the physical size of the image or
	 * the physical size is dynamic. And this function takes time to perform non-trivial image processing (20ms - 30ms),
	 * and should be run on a background thread.
	 *
	 * @return A \c ULuminARCandidateImage Object pointer if the underlying ARPlatform added the candidate image at runtime successfully.
	 *		  Return nullptr otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "LuminAR|Image Tracking", meta = (DeprecatedFunction, DeprecatedMessage="Deprecated & will be removed in 0.26.0. Use AddLuminRuntimeCandidateImageEx() instead.", Keywords = "lumin ar augmentedreality augmented reality candidate image"))
	static ULuminARCandidateImage* AddLuminRuntimeCandidateImage(UARSessionConfig* SessionConfig, UTexture2D* CandidateTexture, FString FriendlyName, float PhysicalWidth, bool bUseUnreliablePose, bool bImageIsStationary);

	UFUNCTION(BlueprintCallable, Category = "LuminAR|Image Tracking", meta = (Keywords = "lumin ar augmentedreality augmented reality candidate image"))
	static ULuminARCandidateImage* AddLuminRuntimeCandidateImageEx(UARSessionConfig* SessionConfig, UTexture2D* CandidateTexture, FString FriendlyName, float PhysicalWidth, bool bUseUnreliablePose, bool bImageIsStationary, EMagicLeapImageTargetOrientation InAxisOrientation);
};
