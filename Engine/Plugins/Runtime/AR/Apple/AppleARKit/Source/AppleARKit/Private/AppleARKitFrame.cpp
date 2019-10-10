// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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

FAppleARKitFrame::FAppleARKitFrame( ARFrame* InARFrame, const FVector2D MinCameraUV, const FVector2D MaxCameraUV )
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
	Camera = Other.Camera;
	LightEstimate = Other.LightEstimate;
	WorldMappingState = Other.WorldMappingState;

	NativeFrame = Other.NativeFrame;
	
	Tracked2DPose = Other.Tracked2DPose;

	return *this;
}

#endif
