// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// AppleARKit
#include "AppleARKitFrame.h"
#include "AppleARKitModule.h"
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

FAppleARKitFrame::FAppleARKitFrame( ARFrame* InARFrame )
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
}

FAppleARKitFrame::FAppleARKitFrame( const FAppleARKitFrame& Other )
	: Timestamp( Other.Timestamp )
	, CameraImage( nullptr )
	, CameraDepth( nullptr )
	, Camera( Other.Camera )
	, LightEstimate( Other.LightEstimate )
	, WorldMappingState(Other.WorldMappingState)
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
	
	// Member-wise copy
	Timestamp = Other.Timestamp;
	Camera = Other.Camera;
	LightEstimate = Other.LightEstimate;
	WorldMappingState = Other.WorldMappingState;

	NativeFrame = Other.NativeFrame;

	return *this;
}

#endif
