// Copyright Epic Games, Inc. All Rights Reserved.

// AppleARKit
#include "AppleARKitFrame.h"
#include "AppleARKitModule.h"
#include "AppleARKitConversion.h"
#include "Misc/ScopeLock.h"

// Default constructor
FAppleARKitFrame::FAppleARKitFrame()
	: Timestamp(0.0)
#if SUPPORTS_ARKIT_1_0
	, CameraImage(nullptr)
	, CameraDepth(nullptr)
	, NativeFrame(nullptr)
#endif
	, WorldMappingState(EARWorldMappingState::NotAvailable)
{
};

#if SUPPORTS_ARKIT_2_0
EARWorldMappingState ToEARWorldMappingState(ARWorldMappingStatus MapStatus)
{
	switch (MapStatus)
	{
		// These both mean more data is needed
		case ARWorldMappingStatusLimited:
		case ARWorldMappingStatusExtending:
			return EARWorldMappingState::StillMappingNotRelocalizable;

		case ARWorldMappingStatusMapped:
			return EARWorldMappingState::Mapped;
	}
	return EARWorldMappingState::NotAvailable;
}
#endif

#if SUPPORTS_ARKIT_1_0

#if MATERIAL_CAMERAIMAGE_CONVERSION
FAppleARKitFrame::FAppleARKitFrame(ARFrame* InARFrame, const FVector2D MinCameraUV, const FVector2D MaxCameraUV, CVMetalTextureCacheRef MetalTextureCache)
#else
FAppleARKitFrame::FAppleARKitFrame(ARFrame* InARFrame, const FVector2D MinCameraUV, const FVector2D MaxCameraUV)
#endif
	: Camera( InARFrame.camera )
	, LightEstimate( InARFrame.lightEstimate )
	, WorldMappingState(EARWorldMappingState::NotAvailable)
{
	// Sanity check
	check( InARFrame );

	// Copy timestamp
	Timestamp = InARFrame.timestamp;

	CameraImage = nullptr;
	CameraDepth = nullptr;

	if ( InARFrame.capturedImage )
	{
		CameraImage = InARFrame.capturedImage;
		CFRetain(CameraImage);
		
#if MATERIAL_CAMERAIMAGE_CONVERSION
		// Update SizeX & Y
		CapturedYImageWidth = CVPixelBufferGetWidthOfPlane( InARFrame.capturedImage, 0 );
		CapturedYImageHeight = CVPixelBufferGetHeightOfPlane( InARFrame.capturedImage, 0 );
		CapturedCbCrImageWidth = CVPixelBufferGetWidthOfPlane( InARFrame.capturedImage, 1 );
		CapturedCbCrImageHeight = CVPixelBufferGetHeightOfPlane( InARFrame.capturedImage, 1 );
		
		// Create a metal texture from the CVPixelBufferRef. The CVMetalTextureRef will
		// be released in the FAppleARKitFrame destructor.
		// NOTE: On success, CapturedImage will be a new CVMetalTextureRef with a ref count of 1
		// 		 so we don't need to CFRetain it. The corresponding CFRelease is handled in
		//
		CVReturn Result = CVMetalTextureCacheCreateTextureFromImage(nullptr, MetalTextureCache, InARFrame.capturedImage, nullptr, MTLPixelFormatR8Unorm, CapturedYImageWidth, CapturedYImageHeight, /*PlaneIndex*/0, &CapturedYImage);
		check( Result == kCVReturnSuccess );
		check( CapturedYImage );
		check( CFGetRetainCount(CapturedYImage) == 1);
		
		Result = CVMetalTextureCacheCreateTextureFromImage(nullptr, MetalTextureCache, InARFrame.capturedImage, nullptr, MTLPixelFormatRG8Unorm, CapturedCbCrImageWidth, CapturedCbCrImageHeight, /*PlaneIndex*/1, &CapturedCbCrImage);
		check( Result == kCVReturnSuccess );
		check( CapturedCbCrImage );
		check( CFGetRetainCount(CapturedCbCrImage) == 1);
#endif
	}

	if (InARFrame.capturedDepthData)
	{
		CameraDepth = InARFrame.capturedDepthData;
		CFRetain(CameraDepth);
	}

	NativeFrame = (void*)CFRetain(InARFrame);

#if SUPPORTS_ARKIT_2_0
	if (FAppleARKitAvailability::SupportsARKit20())
	{
		WorldMappingState = ToEARWorldMappingState(InARFrame.worldMappingStatus);
	}
#endif

#if SUPPORTS_ARKIT_3_0
	if (FAppleARKitAvailability::SupportsARKit30())
	{
		if (InARFrame.detectedBody)
		{
			Tracked2DPose = FAppleARKitConversion::ToARPose2D(InARFrame.detectedBody);
			
			// Convert the joint location from the normalized arkit camera space to UE4's normalized screen space
			const FVector2D UVSize = MaxCameraUV - MinCameraUV;
			for (int Index = 0; Index < Tracked2DPose.JointLocations.Num(); ++Index)
			{
				if (Tracked2DPose.IsJointTracked[Index])
				{
					FVector2D& JointLocation = Tracked2DPose.JointLocations[Index];
					JointLocation = (JointLocation - MinCameraUV) / UVSize;
				}
			}
		}
		
		if (InARFrame.segmentationBuffer)
		{
			SegmentationBuffer = InARFrame.segmentationBuffer;
			CFRetain(SegmentationBuffer);
		}
		
		if (InARFrame.estimatedDepthData)
		{
			EstimatedDepthData = InARFrame.estimatedDepthData;
			CFRetain(EstimatedDepthData);
		}
	}
#endif
}

FAppleARKitFrame::FAppleARKitFrame( const FAppleARKitFrame& Other )
	: Timestamp( Other.Timestamp )
	, CameraImage( nullptr )
	, CameraDepth( nullptr )
#if MATERIAL_CAMERAIMAGE_CONVERSION
	, CapturedYImageWidth( Other.CapturedYImageWidth )
	, CapturedYImageHeight( Other.CapturedYImageHeight )
	, CapturedCbCrImageWidth( Other.CapturedCbCrImageWidth )
	, CapturedCbCrImageHeight( Other.CapturedCbCrImageHeight )
#endif
	, Camera( Other.Camera )
	, LightEstimate( Other.LightEstimate )
	, WorldMappingState(Other.WorldMappingState)
	, Tracked2DPose(Other.Tracked2DPose)
{
	if(Other.NativeFrame != nullptr)
	{
		NativeFrame = (void*)CFRetain((CFTypeRef)Other.NativeFrame);
	}
}

FAppleARKitFrame::~FAppleARKitFrame()
{
	// Release captured image
#if MATERIAL_CAMERAIMAGE_CONVERSION
	if (CapturedYImage)
	{
		CFRelease(CapturedYImage);
	}
	
	if (CapturedCbCrImage)
	{
		CFRelease(CapturedCbCrImage);
	}
#endif
	
	if (CameraImage != nullptr)
	{
		CFRelease(CameraImage);
	}
	if (CameraDepth != nullptr)
	{
		CFRelease(CameraDepth);
	}
	if(NativeFrame != nullptr)
	{
		CFRelease((CFTypeRef)NativeFrame);
	}
#if SUPPORTS_ARKIT_3_0
	if (SegmentationBuffer)
	{
		CFRelease(SegmentationBuffer);
	}
	
	if (EstimatedDepthData)
	{
		CFRelease(EstimatedDepthData);
	}
#endif
}

FAppleARKitFrame& FAppleARKitFrame::operator=( const FAppleARKitFrame& Other )
{
	if (&Other == this)
	{
		return *this;
	}

	// Release outgoing image
#if MATERIAL_CAMERAIMAGE_CONVERSION
	if (CapturedYImage)
	{
		CFRelease(CapturedYImage);
		CapturedYImage = nullptr;
	}
	
	if (CapturedCbCrImage)
	{
		CFRelease(CapturedCbCrImage);
		CapturedYImage = nullptr;
	}
#endif
	
	if (CameraImage != nullptr)
	{
		CFRelease(CameraImage);
		CameraImage = nullptr;
	}
	if (CameraDepth != nullptr)
	{
		CFRelease(CameraDepth);
		CameraDepth = nullptr;
	}
	if(NativeFrame != nullptr)
	{
		CFRelease((CFTypeRef)NativeFrame);
		NativeFrame = nullptr;
	}

	if(Other.NativeFrame != nullptr)
	{
		NativeFrame = (void*)CFRetain((CFTypeRef)Other.NativeFrame);
	}
	
#if SUPPORTS_ARKIT_3_0
	if (SegmentationBuffer)
	{
		CFRelease(SegmentationBuffer);
		SegmentationBuffer = nullptr;
	}
	
	if (EstimatedDepthData)
	{
		CFRelease(EstimatedDepthData);
		EstimatedDepthData = nullptr;
	}
#endif
	
	// Member-wise copy
	Timestamp = Other.Timestamp;
#if MATERIAL_CAMERAIMAGE_CONVERSION
	CapturedYImageWidth = Other.CapturedYImageWidth;
	CapturedYImageHeight = Other.CapturedYImageHeight;
#endif
	Camera = Other.Camera;
	LightEstimate = Other.LightEstimate;
	WorldMappingState = Other.WorldMappingState;

	NativeFrame = Other.NativeFrame;
	
	Tracked2DPose = Other.Tracked2DPose;

	return *this;
}

#endif
