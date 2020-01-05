// Copyright Epic Games, Inc. All Rights Reserved.

#include "AppleARKitConversion.h"
#include "AppleARKitModule.h"
#include "HAL/PlatformMisc.h"
#include "AppleARKitFaceSupport.h"

template<typename TEnum>
static FString GetEnumValueAsString(const FString& Name, TEnum Value)
{
	if (const UEnum* EnumClass = FindObject<UEnum>(ANY_PACKAGE, *Name, true))
	{
		return EnumClass->GetNameByValue((int64)Value).ToString();
	}

	return FString("Invalid");
}

#if SUPPORTS_ARKIT_1_0
ARWorldAlignment FAppleARKitConversion::ToARWorldAlignment( const EARWorldAlignment& InWorldAlignment )
{
	switch ( InWorldAlignment )
	{
		case EARWorldAlignment::Gravity:
			return ARWorldAlignmentGravity;

		case EARWorldAlignment::GravityAndHeading:
			return ARWorldAlignmentGravityAndHeading;

		case EARWorldAlignment::Camera:
			return ARWorldAlignmentCamera;
	};
};

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpartial-availability"

#if SUPPORTS_ARKIT_1_5

ARVideoFormat* FAppleARKitConversion::ToARVideoFormat(const FARVideoFormat& DesiredFormat, NSArray<ARVideoFormat*>* Formats)
{
	if (Formats != nullptr)
	{
		for (ARVideoFormat* Format in Formats)
		{
			if (Format != nullptr &&
				DesiredFormat.FPS == Format.framesPerSecond &&
				DesiredFormat.Width == Format.imageResolution.width &&
				DesiredFormat.Height == Format.imageResolution.height)
			{
				return Format;
			}
		}
	}
	return nullptr;
}

FARVideoFormat FAppleARKitConversion::FromARVideoFormat(ARVideoFormat* Format)
{
	FARVideoFormat ConvertedFormat;
	if (Format != nullptr)
	{
		ConvertedFormat.FPS = Format.framesPerSecond;
		ConvertedFormat.Width = Format.imageResolution.width;
		ConvertedFormat.Height = Format.imageResolution.height;
	}
	return ConvertedFormat;
}

TArray<FARVideoFormat> FAppleARKitConversion::FromARVideoFormatArray(NSArray<ARVideoFormat*>* Formats)
{
	TArray<FARVideoFormat> ConvertedArray;
	if (Formats != nullptr)
	{
		for (ARVideoFormat* Format in Formats)
		{
			if (Format != nullptr)
			{
				ConvertedArray.Add(FromARVideoFormat(Format));
			}
		}
	}
	return ConvertedArray;
}

NSSet* FAppleARKitConversion::InitImageDetection(UARSessionConfig* SessionConfig, TMap< FString, UARCandidateImage* >& CandidateImages, TMap< FString, CGImageRef >& ConvertedCandidateImages)
{
	const TArray<UARCandidateImage*>& ConfigCandidateImages = SessionConfig->GetCandidateImageList();
	if (!ConfigCandidateImages.Num())
	{
		return nullptr;
	}

	NSMutableSet* ConvertedImageSet = [[NSMutableSet new] autorelease];
	for (UARCandidateImage* Candidate : ConfigCandidateImages)
	{
		if (Candidate != nullptr && Candidate->GetCandidateTexture() != nullptr)
		{
			// Don't crash if the physical size is invalid
			if (Candidate->GetPhysicalWidth() <= 0.f || Candidate->GetPhysicalHeight() <= 0.f)
			{
				UE_LOG(LogAppleARKit, Error, TEXT("Unable to process candidate image (%s - %s) due to an invalid physical size (%f,%f)"),
				   *Candidate->GetFriendlyName(), *Candidate->GetName(), Candidate->GetPhysicalWidth(), Candidate->GetPhysicalHeight());
				continue;
			}
			// Store off so the session object can quickly match the anchor to our representation
			// This stores it even if we weren't able to convert to apple's type for GC reasons
			CandidateImages.Add(Candidate->GetFriendlyName(), Candidate);
			// Convert our texture to an Apple compatible image type
			CGImageRef ConvertedImage = nullptr;
			// Avoid doing the expensive conversion work if it's in the cache already
			CGImageRef* FoundImage = ConvertedCandidateImages.Find(Candidate->GetFriendlyName());
			if (FoundImage != nullptr)
			{
				ConvertedImage = *FoundImage;
			}
			else
			{
				ConvertedImage = IAppleImageUtilsPlugin::Get().UTexture2DToCGImage(Candidate->GetCandidateTexture());
				// If it didn't convert this time, it never will, so always store it off
				ConvertedCandidateImages.Add(Candidate->GetFriendlyName(), ConvertedImage);
			}
			if (ConvertedImage != nullptr)
			{
				float ImageWidth = (float)Candidate->GetPhysicalWidth() / 100.f;
				ARReferenceImage* ReferenceImage = [[[ARReferenceImage alloc] initWithCGImage: ConvertedImage orientation: kCGImagePropertyOrientationUp physicalWidth: ImageWidth] autorelease];
				ReferenceImage.name = Candidate->GetFriendlyName().GetNSString();
				[ConvertedImageSet addObject: ReferenceImage];
			}
		}
	}
	return ConvertedImageSet;
}

void FAppleARKitConversion::InitImageDetection(UARSessionConfig* SessionConfig, ARWorldTrackingConfiguration* WorldConfig, TMap< FString, UARCandidateImage* >& CandidateImages, TMap< FString, CGImageRef >& ConvertedCandidateImages)
{
	if (FAppleARKitAvailability::SupportsARKit15())
	{
		WorldConfig.detectionImages = InitImageDetection(SessionConfig, CandidateImages, ConvertedCandidateImages);
	}
#if SUPPORTS_ARKIT_2_0
	if (FAppleARKitAvailability::SupportsARKit20())
	{
		WorldConfig.maximumNumberOfTrackedImages = SessionConfig->GetMaxNumSimultaneousImagesTracked();
	}
#endif
}
#endif

#if SUPPORTS_ARKIT_2_0
void FAppleARKitConversion::InitImageDetection(UARSessionConfig* SessionConfig, ARImageTrackingConfiguration* ImageConfig, TMap< FString, UARCandidateImage* >& CandidateImages, TMap< FString, CGImageRef >& ConvertedCandidateImages)
{
	ImageConfig.trackingImages = InitImageDetection(SessionConfig, CandidateImages, ConvertedCandidateImages);
	ImageConfig.maximumNumberOfTrackedImages = SessionConfig->GetMaxNumSimultaneousImagesTracked();
	ImageConfig.autoFocusEnabled = SessionConfig->ShouldEnableAutoFocus();
}

AREnvironmentTexturing FAppleARKitConversion::ToAREnvironmentTexturing(EAREnvironmentCaptureProbeType CaptureType)
{
	switch (CaptureType)
	{
		case EAREnvironmentCaptureProbeType::Manual:
		{
			return AREnvironmentTexturingManual;
		}
		case EAREnvironmentCaptureProbeType::Automatic:
		{
			return AREnvironmentTexturingAutomatic;
		}
	}
	return AREnvironmentTexturingNone;
}

ARWorldMap* FAppleARKitConversion::ToARWorldMap(const TArray<uint8>& WorldMapData)
{
	uint8* Buffer = (uint8*)WorldMapData.GetData();
	FARWorldSaveHeader InHeader(Buffer);
	// Check for our format and reject if invalid
	if (InHeader.Magic != AR_SAVE_WORLD_KEY || InHeader.Version != AR_SAVE_WORLD_VER)
	{
		UE_LOG(LogAppleARKit, Log, TEXT("Failed to load the world map data from the session object due to incompatible versions: magic (0x%x), ver(%d)"), InHeader.Magic, (uint32)InHeader.Version);
		return nullptr;
	}

	// Decompress the data
	uint8* CompressedData = Buffer + AR_SAVE_WORLD_HEADER_SIZE;
	uint32 CompressedSize = WorldMapData.Num() - AR_SAVE_WORLD_HEADER_SIZE;
	uint32 UncompressedSize = InHeader.UncompressedSize;
	TArray<uint8> UncompressedData;
	UncompressedData.AddUninitialized(UncompressedSize);
	if (!FCompression::UncompressMemory(NAME_Zlib, UncompressedData.GetData(), UncompressedSize, CompressedData, CompressedSize))
	{
		UE_LOG(LogAppleARKit, Log, TEXT("Failed to load the world map data from the session object due to a decompression error"));
		return nullptr;
	}
	
	// Serialize into the World map data
	NSData* WorldNSData = [NSData dataWithBytesNoCopy: UncompressedData.GetData() length: UncompressedData.Num() freeWhenDone: NO];
	NSError* ErrorObj = nullptr;
	ARWorldMap* WorldMap = [NSKeyedUnarchiver unarchivedObjectOfClass: ARWorldMap.class fromData: WorldNSData error: &ErrorObj];
	if (ErrorObj != nullptr)
	{
		FString Error = [ErrorObj localizedDescription];
		UE_LOG(LogAppleARKit, Log, TEXT("Failed to load the world map data from the session object with error string (%s)"), *Error);
	}
	return WorldMap;
}

NSSet* FAppleARKitConversion::ToARReferenceObjectSet(const TArray<UARCandidateObject*>& CandidateObjects, TMap< FString, UARCandidateObject* >& CandidateObjectMap)
{
	CandidateObjectMap.Empty();

	if (CandidateObjects.Num() == 0)
	{
		return nullptr;
	}

	NSMutableSet* ConvertedObjectSet = [[NSMutableSet new] autorelease];
	for (UARCandidateObject* Candidate : CandidateObjects)
	{
		if (Candidate != nullptr && Candidate->GetCandidateObjectData().Num() > 0)
		{
			NSData* CandidateData = [NSData dataWithBytesNoCopy: (uint8*)Candidate->GetCandidateObjectData().GetData() length: Candidate->GetCandidateObjectData().Num() freeWhenDone: NO];
			NSError* ErrorObj = nullptr;
			ARReferenceObject* RefObject = [NSKeyedUnarchiver unarchivedObjectOfClass: ARReferenceObject.class fromData: CandidateData error: &ErrorObj];
			if (RefObject != nullptr)
			{
				// Store off so the session object can quickly match the anchor to our representation
				// This stores it even if we weren't able to convert to apple's type for GC reasons
				CandidateObjectMap.Add(Candidate->GetFriendlyName(), Candidate);
				RefObject.name = Candidate->GetFriendlyName().GetNSString();
				[ConvertedObjectSet addObject: RefObject];
			}
			else
			{
				UE_LOG(LogAppleARKit, Log, TEXT("Failed to convert to ARReferenceObject (%s)"), *Candidate->GetFriendlyName());
			}
		}
		else
		{
			UE_LOG(LogAppleARKit, Log, TEXT("Missing candidate object data for ARCandidateObject (%s)"), Candidate != nullptr ? *Candidate->GetFriendlyName() : TEXT("null"));
		}
	}
	return ConvertedObjectSet;
}
#endif

ARConfiguration* FAppleARKitConversion::ToARConfiguration( UARSessionConfig* SessionConfig, TMap< FString, UARCandidateImage* >& CandidateImages, TMap< FString, CGImageRef >& ConvertedCandidateImages, TMap< FString, UARCandidateObject* >& CandidateObjects )
{
	EARSessionType SessionType = SessionConfig->GetSessionType();
	ARConfiguration* SessionConfiguration = nullptr;
	switch (SessionType)
	{
		case EARSessionType::Orientation:
		{
			if (AROrientationTrackingConfiguration.isSupported == FALSE)
			{
				return nullptr;
			}
			AROrientationTrackingConfiguration* OrientationTrackingConfiguration = [AROrientationTrackingConfiguration new];
#if SUPPORTS_ARKIT_1_5
			if (FAppleARKitAvailability::SupportsARKit15())
			{
				OrientationTrackingConfiguration.autoFocusEnabled = SessionConfig->ShouldEnableAutoFocus();
			}
#endif
			SessionConfiguration = OrientationTrackingConfiguration;
			break;
		}
		case EARSessionType::World:
		{
			if (ARWorldTrackingConfiguration.isSupported == FALSE)
			{
				return nullptr;
			}
			ARWorldTrackingConfiguration* WorldTrackingConfiguration = [ARWorldTrackingConfiguration new];
			WorldTrackingConfiguration.planeDetection = ARPlaneDetectionNone;
			if ( EnumHasAnyFlags(EARPlaneDetectionMode::HorizontalPlaneDetection, SessionConfig->GetPlaneDetectionMode()))
			{
				WorldTrackingConfiguration.planeDetection |= ARPlaneDetectionHorizontal;
			}
#if SUPPORTS_ARKIT_1_5
			if (FAppleARKitAvailability::SupportsARKit15())
			{
				if (EnumHasAnyFlags(EARPlaneDetectionMode::VerticalPlaneDetection, SessionConfig->GetPlaneDetectionMode()) )
				{
					WorldTrackingConfiguration.planeDetection |= ARPlaneDetectionVertical;
				}
				WorldTrackingConfiguration.autoFocusEnabled = SessionConfig->ShouldEnableAutoFocus();
				// Add any images that wish to be detected
				FAppleARKitConversion::InitImageDetection(SessionConfig, WorldTrackingConfiguration, CandidateImages, ConvertedCandidateImages);
				ARVideoFormat* Format = FAppleARKitConversion::ToARVideoFormat(SessionConfig->GetDesiredVideoFormat(), ARWorldTrackingConfiguration.supportedVideoFormats);
				if (Format != nullptr)
				{
					WorldTrackingConfiguration.videoFormat = Format;
				}
			}
#endif
#if SUPPORTS_ARKIT_2_0
			if (FAppleARKitAvailability::SupportsARKit20())
			{
				// Check for environment capture probe types
				WorldTrackingConfiguration.environmentTexturing = ToAREnvironmentTexturing(SessionConfig->GetEnvironmentCaptureProbeType());
				// Load the world if requested
				if (SessionConfig->GetWorldMapData().Num() > 0)
				{
					ARWorldMap* WorldMap = ToARWorldMap(SessionConfig->GetWorldMapData());
					WorldTrackingConfiguration.initialWorldMap = WorldMap;
					[WorldMap release];
				}
				// Convert any candidate objects that are to be detected
				WorldTrackingConfiguration.detectionObjects = ToARReferenceObjectSet(SessionConfig->GetCandidateObjectList(), CandidateObjects);
			}
#endif
			SessionConfiguration = WorldTrackingConfiguration;
			break;
		}
		case EARSessionType::Image:
		{
#if SUPPORTS_ARKIT_2_0
			if (FAppleARKitAvailability::SupportsARKit20())
			{
				if (ARImageTrackingConfiguration.isSupported == FALSE)
				{
					return nullptr;
				}
				ARImageTrackingConfiguration* ImageTrackingConfiguration = [ARImageTrackingConfiguration new];
				// Add any images that wish to be detected
				InitImageDetection(SessionConfig, ImageTrackingConfiguration, CandidateImages, ConvertedCandidateImages);
				SessionConfiguration = ImageTrackingConfiguration;
			}
#endif
			break;
		}
		case EARSessionType::ObjectScanning:
		{
#if SUPPORTS_ARKIT_2_0
			if (FAppleARKitAvailability::SupportsARKit20())
			{
				if (ARObjectScanningConfiguration.isSupported == FALSE)
				{
					return nullptr;
				}
				ARObjectScanningConfiguration* ObjectScanningConfiguration = [ARObjectScanningConfiguration new];
				if (EnumHasAnyFlags(EARPlaneDetectionMode::HorizontalPlaneDetection, SessionConfig->GetPlaneDetectionMode()))
				{
					ObjectScanningConfiguration.planeDetection |= ARPlaneDetectionHorizontal;
				}
				if (EnumHasAnyFlags(EARPlaneDetectionMode::VerticalPlaneDetection, SessionConfig->GetPlaneDetectionMode()))
				{
					ObjectScanningConfiguration.planeDetection |= ARPlaneDetectionVertical;
				}
				ObjectScanningConfiguration.autoFocusEnabled = SessionConfig->ShouldEnableAutoFocus();
				SessionConfiguration = ObjectScanningConfiguration;
			}
#endif
			break;
		}
		case EARSessionType::PoseTracking:
		{
#if SUPPORTS_ARKIT_3_0
			if (FAppleARKitAvailability::SupportsARKit30())
			{
				if (ARBodyTrackingConfiguration.isSupported == FALSE)
				{
					return nullptr;
				}
				
				ARBodyTrackingConfiguration* BodyTrackingConfiguration = [ARBodyTrackingConfiguration new];
				BodyTrackingConfiguration.planeDetection = ARPlaneDetectionNone;
				if (EnumHasAnyFlags(EARPlaneDetectionMode::HorizontalPlaneDetection, SessionConfig->GetPlaneDetectionMode()))
				{
					BodyTrackingConfiguration.planeDetection |= ARPlaneDetectionHorizontal;
				}
				
				if (EnumHasAnyFlags(EARPlaneDetectionMode::VerticalPlaneDetection, SessionConfig->GetPlaneDetectionMode()) )
				{
					BodyTrackingConfiguration.planeDetection |= ARPlaneDetectionVertical;
				}
				BodyTrackingConfiguration.autoFocusEnabled = SessionConfig->ShouldEnableAutoFocus();
				
				// Add any images that wish to be detected
				FAppleARKitConversion::InitImageDetection(SessionConfig, BodyTrackingConfiguration, CandidateImages, ConvertedCandidateImages);
				
				ARVideoFormat* Format = FAppleARKitConversion::ToARVideoFormat(SessionConfig->GetDesiredVideoFormat(), ARBodyTrackingConfiguration.supportedVideoFormats);
				if (Format != nullptr)
				{
					BodyTrackingConfiguration.videoFormat = Format;
				}
				
				// Check for environment capture probe types
				BodyTrackingConfiguration.environmentTexturing = ToAREnvironmentTexturing(SessionConfig->GetEnvironmentCaptureProbeType());
				// Load the world if requested
				if (SessionConfig->GetWorldMapData().Num() > 0)
				{
					ARWorldMap* WorldMap = ToARWorldMap(SessionConfig->GetWorldMapData());
					BodyTrackingConfiguration.initialWorldMap = WorldMap;
					[WorldMap release];
				}
				
				SessionConfiguration = BodyTrackingConfiguration;
			}
#endif
			break;
		}
		default:
			return nullptr;
	}
	
	if (SessionConfiguration != nullptr)
	{
		// Copy / convert properties
		SessionConfiguration.lightEstimationEnabled = SessionConfig->GetLightEstimationMode() != EARLightEstimationMode::None;
		SessionConfiguration.providesAudioData = NO;
		SessionConfiguration.worldAlignment = FAppleARKitConversion::ToARWorldAlignment(SessionConfig->GetWorldAlignment());
	}
	
	return SessionConfiguration;
}

void FAppleARKitConversion::ConfigureSessionTrackingFeatures(UARSessionConfig* SessionConfig, ARConfiguration* SessionConfiguration)
{
#if SUPPORTS_ARKIT_3_0
		// Enable additional frame semantics for ARKit 3.0
		if (FAppleARKitAvailability::SupportsARKit30())
		{
			EARSessionType SessionType = SessionConfig->GetSessionType();
			const EARSessionTrackingFeature SessionTrackingFeature = SessionConfig->GetEnabledSessionTrackingFeature();
			if (SessionTrackingFeature != EARSessionTrackingFeature::None)
			{
				if (IsSessionTrackingFeatureSupported(SessionType, SessionTrackingFeature))
				{
					SessionConfiguration.frameSemantics = ToARFrameSemantics(SessionTrackingFeature);
				}
				else
				{
					UE_LOG(LogAppleARKit, Error, TEXT("Session type [%s] doesn't support the required session feature: [%s]!"),
						   *GetEnumValueAsString<>(TEXT("EARSessionType"), SessionType),
						   *GetEnumValueAsString<>(TEXT("EARSessionTrackingFeature"), SessionTrackingFeature));
				}
			}
		}
#endif
}

bool FAppleARKitConversion::IsSessionTrackingFeatureSupported(EARSessionType SessionType, EARSessionTrackingFeature SessionTrackingFeature)
{
#if SUPPORTS_ARKIT_3_0
	if (FAppleARKitAvailability::SupportsARKit30())
	{
		const ARFrameSemantics Semantics = ToARFrameSemantics(SessionTrackingFeature);
		if (Semantics != ARFrameSemanticNone)
		{
			switch (SessionType)
			{
				case EARSessionType::Orientation:
					return [AROrientationTrackingConfiguration supportsFrameSemantics: Semantics];
				
				case EARSessionType::World:
					return [ARWorldTrackingConfiguration supportsFrameSemantics: Semantics];
				
				case EARSessionType::Face:
					{
						static TMap<EARSessionTrackingFeature, bool> SupportFlags;
						
						if (!SupportFlags.Contains(SessionTrackingFeature))
						{
							SupportFlags.Add(SessionTrackingFeature, false);
							
							TArray<IAppleARKitFaceSupport*> Impls = IModularFeatures::Get().GetModularFeatureImplementations<IAppleARKitFaceSupport>("AppleARKitFaceSupport");
							if (Impls.Num() && Impls[0]->IsARFrameSemanticsSupported(Semantics))
							{
								SupportFlags[SessionTrackingFeature] = true;
							}
						}
						
						return SupportFlags[SessionTrackingFeature];
					}
				
				case EARSessionType::Image:
					return [ARImageTrackingConfiguration supportsFrameSemantics: Semantics];
				
				case EARSessionType::ObjectScanning:
					return [ARObjectScanningConfiguration supportsFrameSemantics: Semantics];
					
				case EARSessionType::PoseTracking:
					return [ARBodyTrackingConfiguration supportsFrameSemantics: Semantics];
			}
		}
	}
#endif
	
	return false;
}

#if SUPPORTS_ARKIT_3_0
void FAppleARKitConversion::InitImageDetection(UARSessionConfig* SessionConfig, ARBodyTrackingConfiguration* BodyTrackingConfig, TMap< FString, UARCandidateImage* >& CandidateImages, TMap< FString, CGImageRef >& ConvertedCandidateImages)
{
	if (FAppleARKitAvailability::SupportsARKit15())
	{
		BodyTrackingConfig.detectionImages = InitImageDetection(SessionConfig, CandidateImages, ConvertedCandidateImages);
	}
	
	if (FAppleARKitAvailability::SupportsARKit20())
	{
		BodyTrackingConfig.maximumNumberOfTrackedImages = SessionConfig->GetMaxNumSimultaneousImagesTracked();
	}
}

ARFrameSemantics FAppleARKitConversion::ToARFrameSemantics(EARSessionTrackingFeature SessionTrackingFeature)
{
	static const TMap<EARSessionTrackingFeature, ARFrameSemantics> SessionTrackingFeatureToFrameSemantics =
	{
		{ EARSessionTrackingFeature::None, ARFrameSemanticNone },
		{ EARSessionTrackingFeature::PoseDetection2D, ARFrameSemanticBodyDetection },
		{ EARSessionTrackingFeature::PersonSegmentation, ARFrameSemanticPersonSegmentation },
		{ EARSessionTrackingFeature::PersonSegmentationWithDepth, ARFrameSemanticPersonSegmentationWithDepth },
	};
	
	if (SessionTrackingFeatureToFrameSemantics.Contains(SessionTrackingFeature))
	{
		return SessionTrackingFeatureToFrameSemantics[SessionTrackingFeature];
	}
	
	return ARFrameSemanticNone;
}

void FAppleARKitConversion::ToSkeletonDefinition(const ARSkeletonDefinition* InARSkeleton, FARSkeletonDefinition& OutSkeletonDefinition)
{
	check(InARSkeleton);
	
	// TODO: these values should not change over time so they can be cached somewhere
	
	const int NumJoints = InARSkeleton.jointCount;
	
	OutSkeletonDefinition.NumJoints = NumJoints;
	OutSkeletonDefinition.JointNames.AddUninitialized(NumJoints);
	OutSkeletonDefinition.ParentIndices.AddUninitialized(NumJoints);
	
	for (int Index = 0; Index < NumJoints; ++Index)
	{
		OutSkeletonDefinition.JointNames[Index] = *FString(InARSkeleton.jointNames[Index]);
		OutSkeletonDefinition.ParentIndices[Index] = InARSkeleton.parentIndices[Index].intValue;
	}
}

FARPose2D FAppleARKitConversion::ToARPose2D(const ARBody2D* InARPose2D)
{
	check(InARPose2D);
	
    const EDeviceScreenOrientation ScreenOrientation = FPlatformMisc::GetDeviceOrientation();
	
	FARPose2D Pose2D;
	if (FAppleARKitAvailability::SupportsARKit30())
	{
		const ARSkeleton2D* Skeleton2D = InARPose2D.skeleton;
		ToSkeletonDefinition(Skeleton2D.definition, Pose2D.SkeletonDefinition);
		const int NumJoints = Pose2D.SkeletonDefinition.NumJoints;
		
		Pose2D.JointLocations.AddUninitialized(NumJoints);
		Pose2D.IsJointTracked.AddUninitialized(NumJoints);
		
		for (int Index = 0; Index < NumJoints; ++Index)
		{
			const bool bIsTracked = [Skeleton2D isJointTracked: Index];
			Pose2D.IsJointTracked[Index] = bIsTracked;
			
            if (bIsTracked)
            {
                FVector2D OriginalLandmark(Skeleton2D.jointLandmarks[Index][0], Skeleton2D.jointLandmarks[Index][1]);
				
                switch (ScreenOrientation)
                {
                    case EDeviceScreenOrientation::Portrait:
                        Pose2D.JointLocations[Index] = FVector2D(1.f - OriginalLandmark.Y, OriginalLandmark.X);
                        break;
                        
                    case EDeviceScreenOrientation::PortraitUpsideDown:
                        Pose2D.JointLocations[Index] = FVector2D(OriginalLandmark.Y, OriginalLandmark.X);
                        break;
                        
                    case EDeviceScreenOrientation::LandscapeLeft:
                        Pose2D.JointLocations[Index] = FVector2D(1.f - OriginalLandmark.X, 1.f - OriginalLandmark.Y);
                        break;
                        
                    case EDeviceScreenOrientation::LandscapeRight:
                        Pose2D.JointLocations[Index] = OriginalLandmark;
                        break;
                        
                    default:
                        Pose2D.JointLocations[Index] = OriginalLandmark;
                }
            }
            else
            {
                Pose2D.JointLocations[Index] = FVector2D::ZeroVector;
            }
		}
	}
	
	return Pose2D;
}

FARPose3D FAppleARKitConversion::ToARPose3D(const ARSkeleton3D* Skeleton3D, bool bIdentityForUntracked)
{
	check(Skeleton3D);
	
	FARPose3D Pose3D;
	if (FAppleARKitAvailability::SupportsARKit30())
	{
		ToSkeletonDefinition(Skeleton3D.definition, Pose3D.SkeletonDefinition);
		const int NumJoints = Pose3D.SkeletonDefinition.NumJoints;
		
		Pose3D.JointTransforms.AddUninitialized(NumJoints);
		Pose3D.IsJointTracked.AddUninitialized(NumJoints);
		Pose3D.JointTransformSpace = EARJointTransformSpace::Model;
		
		for (int Index = 0; Index < NumJoints; ++Index)
		{
			const bool bIsTracked = [Skeleton3D isJointTracked: Index];
			Pose3D.IsJointTracked[Index] = bIsTracked;
			if (bIsTracked || !bIdentityForUntracked)
			{
				Pose3D.JointTransforms[Index] = ToFTransform(Skeleton3D.jointModelTransforms[Index]);
			}
			else
			{
				Pose3D.JointTransforms[Index] = FTransform::Identity;
			}
		}
	}
	
	return Pose3D;
}

FARPose3D FAppleARKitConversion::ToARPose3D(const ARBodyAnchor* InARBodyAnchor)
{
	check(InARBodyAnchor);
	
	FARPose3D Pose3D;
	if (FAppleARKitAvailability::SupportsARKit30())
    {
		Pose3D = ToARPose3D(InARBodyAnchor.skeleton, true);
	}
	
	return Pose3D;
}

#endif

#pragma clang diagnostic pop
#endif
