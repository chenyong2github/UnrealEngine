// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterConfigurationStrings.h"
#include "DisplayClusterConfigurationJsonTypes.generated.h"


USTRUCT()
struct FDisplayClusterConfigurationJsonRectangle
{
	GENERATED_BODY()

public:
	FDisplayClusterConfigurationJsonRectangle()
		: X(0), Y(0), W(0), H(0)
	{ }

	FDisplayClusterConfigurationJsonRectangle(int32 _X, int32 _Y, int32 _W, int32 _H)
		: X(_X), Y(_Y), W(_W), H(_H)
	{ }

public:
	UPROPERTY()
	int32 X;

	UPROPERTY()
	int32 Y;

	UPROPERTY()
	int32 W;

	UPROPERTY()
	int32 H;
};


USTRUCT()
struct FDisplayClusterConfigurationJsonVector
{
	GENERATED_BODY()

public:
	FDisplayClusterConfigurationJsonVector()
		: FDisplayClusterConfigurationJsonVector(0.f, 0.f, 0.f)
	{ }

	FDisplayClusterConfigurationJsonVector(float _X, float _Y, float _Z)
		: X(_X), Y(_Y), Z(_Z)
	{ }

	FDisplayClusterConfigurationJsonVector(const FVector& Vector)
		: FDisplayClusterConfigurationJsonVector(Vector.X, Vector.Y, Vector.Z)
	{ }

public:
	UPROPERTY()
	float X;

	UPROPERTY()
	float Y;

	UPROPERTY()
	float Z;

public:
	static FVector ToVector(const FDisplayClusterConfigurationJsonVector& Data)
	{
		return FVector(Data.X, Data.Y, Data.Z);
	}

	static FDisplayClusterConfigurationJsonVector FromVector(const FVector& Vector)
	{
		return FDisplayClusterConfigurationJsonVector(Vector.X, Vector.Y, Vector.Z);
	}
};


USTRUCT()
struct FDisplayClusterConfigurationJsonRotator
{
	GENERATED_BODY()

public:
	FDisplayClusterConfigurationJsonRotator()
		: FDisplayClusterConfigurationJsonRotator(0.f, 0.f, 0.f)
	{ }

	FDisplayClusterConfigurationJsonRotator(float P, float Y, float R)
		: Pitch(P), Yaw(Y), Roll(R)
	{ }

	FDisplayClusterConfigurationJsonRotator(const FRotator& Rotator)
		: FDisplayClusterConfigurationJsonRotator(Rotator.Pitch, Rotator.Yaw, Rotator.Roll)
	{ }

public:
	UPROPERTY()
	float Pitch;

	UPROPERTY()
	float Yaw;

	UPROPERTY()
	float Roll;

public:
	static FRotator ToRotator(const FDisplayClusterConfigurationJsonRotator& Data)
	{
		return FRotator(Data.Pitch, Data.Yaw, Data.Roll);
	}

	static FDisplayClusterConfigurationJsonRotator FromRotator(const FRotator& Rotator)
	{
		return FDisplayClusterConfigurationJsonRotator(Rotator.Pitch, Rotator.Yaw, Rotator.Roll);
	}
};


USTRUCT()
struct FDisplayClusterConfigurationJsonSizeInt
{
	GENERATED_BODY()

public:
	FDisplayClusterConfigurationJsonSizeInt()
		: FDisplayClusterConfigurationJsonSizeInt(0, 0)
	{ }

	FDisplayClusterConfigurationJsonSizeInt(int W, int H)
		: Width(W), Height(H)
	{ }

public:
	UPROPERTY()
	int Width;

	UPROPERTY()
	int Height;

public:
	static FIntPoint ToPoint(const FDisplayClusterConfigurationJsonSizeInt& Data)
	{
		return FIntPoint(Data.Width, Data.Height);
	}

	static FDisplayClusterConfigurationJsonSizeInt FromPoint(const FIntPoint& Point)
	{
		return FDisplayClusterConfigurationJsonSizeInt(Point.X, Point.Y);
	}
};


USTRUCT()
struct FDisplayClusterConfigurationJsonSizeFloat
{
	GENERATED_BODY()

public:
	FDisplayClusterConfigurationJsonSizeFloat()
		: FDisplayClusterConfigurationJsonSizeFloat(0.f, 0.f)
	{ }

	FDisplayClusterConfigurationJsonSizeFloat(float W, float H)
		: Width(W), Height(H)
	{ }

	FDisplayClusterConfigurationJsonSizeFloat(const FVector2D& Vector)
		: FDisplayClusterConfigurationJsonSizeFloat(Vector.X, Vector.Y)
	{ }

public:
	UPROPERTY()
	float Width;

	UPROPERTY()
	float Height;

public:
	static FVector2D ToVector(const FDisplayClusterConfigurationJsonSizeFloat& Data)
	{
		return FVector2D(Data.Width, Data.Height);
	}

	static FDisplayClusterConfigurationJsonSizeFloat FromVector(const FVector2D& Vector)
	{
		return FDisplayClusterConfigurationJsonSizeFloat(Vector.X, Vector.Y);
	}
};


USTRUCT()
struct FDisplayClusterConfigurationJsonPolymorphicEntity
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FString Type;

	UPROPERTY()
	TMap<FString, FString> Parameters;
};


USTRUCT()
struct FDisplayClusterConfigurationJsonSceneComponent
{
	GENERATED_BODY()
};

USTRUCT()
struct FDisplayClusterConfigurationJsonSceneComponentXform
	: public FDisplayClusterConfigurationJsonSceneComponent
{
	GENERATED_BODY()

public:
	FDisplayClusterConfigurationJsonSceneComponentXform()
		: FDisplayClusterConfigurationJsonSceneComponentXform(FString(), FVector::ZeroVector, FRotator::ZeroRotator, FString(), INDEX_NONE)
	{ }

	FDisplayClusterConfigurationJsonSceneComponentXform(const FString& InParent, const FVector& InLocation, const FRotator& InRotation, const FString& InTrackerId, int32 InTrackerChannel)
		: Parent(InParent)
		, Location(InLocation)
		, Rotation(InRotation)
		, TrackerId(InTrackerId)
		, TrackerChannel(InTrackerChannel)
	{ }

public:
	UPROPERTY()
	FString Parent;

	UPROPERTY()
	FDisplayClusterConfigurationJsonVector Location;

	UPROPERTY()
	FDisplayClusterConfigurationJsonRotator Rotation;

	UPROPERTY()
	FString TrackerId;

	UPROPERTY()
	int32 TrackerChannel;
};

USTRUCT()
struct FDisplayClusterConfigurationJsonSceneComponentCamera
	: public FDisplayClusterConfigurationJsonSceneComponentXform
{
	GENERATED_BODY()

public:
	FDisplayClusterConfigurationJsonSceneComponentCamera()
		: FDisplayClusterConfigurationJsonSceneComponentCamera(FString(), FVector::ZeroVector, FRotator::ZeroRotator, FString(), INDEX_NONE, 0.064f, false, DisplayClusterConfigurationStrings::config::scene::camera::CameraStereoOffsetNone)
	{ }

	FDisplayClusterConfigurationJsonSceneComponentCamera(const FString& InParent, const FVector& InLocation, const FRotator& InRotation, const FString& InTrackerId, int32 InTrackerChannel, float InInterpupillaryDistance, bool bInSwapEyes, FString InStereoOffset)
		: FDisplayClusterConfigurationJsonSceneComponentXform(InParent, InLocation, InRotation, InTrackerId, InTrackerChannel)
		, InterpupillaryDistance(InInterpupillaryDistance)
		, SwapEyes(bInSwapEyes)
		, StereoOffset(InStereoOffset)
	{ }

public:
	UPROPERTY()
	float InterpupillaryDistance;

	UPROPERTY()
	bool SwapEyes;

	UPROPERTY()
	FString StereoOffset;
};

USTRUCT()
struct FDisplayClusterConfigurationJsonSceneComponentScreen
	: public FDisplayClusterConfigurationJsonSceneComponentXform
{
	GENERATED_BODY()

public:
	FDisplayClusterConfigurationJsonSceneComponentScreen()
		: FDisplayClusterConfigurationJsonSceneComponentScreen(FString(), FVector::ZeroVector, FRotator::ZeroRotator, FString(), INDEX_NONE, FVector2D::ZeroVector)
	{ }

	FDisplayClusterConfigurationJsonSceneComponentScreen(const FString& InParent, const FVector& InLocation, const FRotator& InRotation, const FString& InTrackerId, int32 InTrackerChannel, const FVector2D& InSize)
		: FDisplayClusterConfigurationJsonSceneComponentXform(InParent, InLocation, InRotation, InTrackerId, InTrackerChannel)
		, Size(InSize)
	{ }

public:
	UPROPERTY()
	FDisplayClusterConfigurationJsonSizeFloat Size;
};

USTRUCT()
struct FDisplayClusterConfigurationJsonSceneComponentMesh
	: public FDisplayClusterConfigurationJsonSceneComponentXform
{
	GENERATED_BODY()

public:
	FDisplayClusterConfigurationJsonSceneComponentMesh()
		: FDisplayClusterConfigurationJsonSceneComponentMesh(FString(), FVector::ZeroVector, FRotator::ZeroRotator, FString(), INDEX_NONE, FString())
	{ }

	FDisplayClusterConfigurationJsonSceneComponentMesh(const FString& InParent, const FVector& InLocation, const FRotator& InRotation, const FString& InTrackerId, int32 InTrackerChannel, const FString& InAsset)
		: FDisplayClusterConfigurationJsonSceneComponentXform(InParent, InLocation, InRotation, InTrackerId, InTrackerChannel)
		, Asset(InAsset)
	{ }

public:
	UPROPERTY()
	FString Asset;
};

USTRUCT()
struct FDisplayClusterConfigurationJsonScene
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TMap<FString, FDisplayClusterConfigurationJsonSceneComponentXform> Xforms;

	UPROPERTY()
	TMap<FString, FDisplayClusterConfigurationJsonSceneComponentCamera> Cameras;

	UPROPERTY()
	TMap<FString, FDisplayClusterConfigurationJsonSceneComponentScreen> Screens;

	UPROPERTY()
	TMap<FString, FDisplayClusterConfigurationJsonSceneComponentMesh> Meshes;
};

USTRUCT()
struct FDisplayClusterConfigurationJsonMasterNode
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FString Id;

	UPROPERTY()
	TMap<FString, uint16> Ports;
};

USTRUCT()
struct FDisplayClusterConfigurationJsonClusterSyncPolicy
	: public FDisplayClusterConfigurationJsonPolymorphicEntity
{
	GENERATED_BODY()
};

USTRUCT()
struct FDisplayClusterConfigurationJsonClusterSync
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FDisplayClusterConfigurationJsonClusterSyncPolicy RenderSyncPolicy;

	UPROPERTY()
	FDisplayClusterConfigurationJsonClusterSyncPolicy InputSyncPolicy;
};

USTRUCT()
struct FDisplayClusterConfigurationJsonPostprocess
	: public FDisplayClusterConfigurationJsonPolymorphicEntity
{
	GENERATED_BODY()
};

USTRUCT()
struct FDisplayClusterConfigurationJsonProjectionPolicy
	: public FDisplayClusterConfigurationJsonPolymorphicEntity
{
	GENERATED_BODY()
};

USTRUCT()
struct FDisplayClusterConfigurationJsonViewport
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FString Camera;

	UPROPERTY()
	float BufferRatio;

	UPROPERTY()
	int GPUIndex;

	UPROPERTY()
	bool AllowCrossGPUTransfer;

	UPROPERTY()
	bool IsShared;

	UPROPERTY()
	FDisplayClusterConfigurationJsonRectangle Region;

	UPROPERTY()
	FDisplayClusterConfigurationJsonProjectionPolicy ProjectionPolicy;
};

USTRUCT()
struct FDisplayClusterConfigurationJsonClusterNode
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FString Host;

	UPROPERTY()
	bool Sound;

	UPROPERTY()
	bool FullScreen;

	UPROPERTY()
	FDisplayClusterConfigurationJsonRectangle Window;

	UPROPERTY()
	TMap<FString, FDisplayClusterConfigurationJsonPostprocess> Postprocess;

	UPROPERTY()
	TMap<FString, FDisplayClusterConfigurationJsonViewport> Viewports;
};

USTRUCT()
struct FDisplayClusterConfigurationJsonInputDevice
	: public FDisplayClusterConfigurationJsonPolymorphicEntity
{
	GENERATED_BODY()
};

USTRUCT()
struct FDisplayClusterConfigurationJsonInput
	: public FDisplayClusterConfigurationJsonPolymorphicEntity
{
	GENERATED_BODY()
};

USTRUCT()
struct FDisplayClusterConfigurationJsonInputBinding
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FString Device;

	UPROPERTY()
	TMap<FString, FString> Parameters;
};

USTRUCT()
struct FDisplayClusterConfigurationJsonCluster
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FDisplayClusterConfigurationJsonMasterNode MasterNode;

	UPROPERTY()
	FDisplayClusterConfigurationJsonClusterSync Sync;

	UPROPERTY()
	TMap<FString, FString> Network;

	UPROPERTY()
	TMap<FString, FDisplayClusterConfigurationJsonClusterNode> Nodes;
};

USTRUCT()
struct FDisplayClusterConfigurationJsonDiagnostics
{
	GENERATED_BODY()

public:
	UPROPERTY()
	bool SimulateLag;

	UPROPERTY()
	float MinLagTime;

	UPROPERTY()
	float MaxLagTime;
};


// "nDisplay" category structure
USTRUCT()
struct FDisplayClusterConfigurationJsonNdisplay
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FString Description;

	UPROPERTY()
	FString Version;

	UPROPERTY()
	FDisplayClusterConfigurationJsonScene Scene;

	UPROPERTY()
	FDisplayClusterConfigurationJsonCluster Cluster;

	UPROPERTY()
	TMap<FString, FDisplayClusterConfigurationJsonInputDevice> Input;

	UPROPERTY()
	TArray<FDisplayClusterConfigurationJsonInputBinding> InputBindings;

	UPROPERTY()
	TMap<FString, FString> CustomParameters;

	UPROPERTY()
	FDisplayClusterConfigurationJsonDiagnostics Diagnostics;
};

// The main nDisplay configuration structure. It's supposed to extract nDisplay related data from a collecting JSON file.
USTRUCT()
struct FDisplayClusterConfigurationJsonContainer
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FDisplayClusterConfigurationJsonNdisplay nDisplay;
};
