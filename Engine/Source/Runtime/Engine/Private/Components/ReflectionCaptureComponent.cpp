// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	
=============================================================================*/

#include "Components/ReflectionCaptureComponent.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/RenderingObjectVersion.h"
#include "UObject/ReflectionCaptureObjectVersion.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/Actor.h"
#include "RHI.h"
#include "RenderingThread.h"
#include "RenderResource.h"
#include "Misc/ScopeLock.h"
#include "Components/BillboardComponent.h"
#include "Engine/CollisionProfile.h"
#include "Serialization/MemoryReader.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Texture2D.h"
#include "SceneManagement.h"
#include "Engine/ReflectionCapture.h"
#include "DerivedDataCacheInterface.h"
#include "EngineModule.h"
#include "ShaderCompiler.h"
#include "UObject/RenderingObjectVersion.h"
#include "Engine/SphereReflectionCapture.h"
#include "Components/SphereReflectionCaptureComponent.h"
#include "Components/DrawSphereComponent.h"
#include "Components/BoxReflectionCaptureComponent.h"
#include "Engine/PlaneReflectionCapture.h"
#include "Engine/BoxReflectionCapture.h"
#include "EngineUtils.h"
#include "Components/PlaneReflectionCaptureComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkyLightComponent.h"
#include "ProfilingDebugging/CookStats.h"
#include "Engine/MapBuildDataRegistry.h"
#include "ComponentRecreateRenderStateContext.h"
#include "Engine/TextureCube.h"

#if WITH_EDITOR
#include "Factories/TextureFactory.h"
#endif

// ES3.0+ devices support seamless cubemap filtering, averaging edges will produce artifacts on those devices
#define MOBILE_AVERAGE_CUBEMAP_EDGES 0 

// ES3.0+ devices support seamless cubemap filtering, averaging edges will produce artifacts on those devices
#define MOBILE_AVERAGE_CUBEMAP_EDGES 0 

DEFINE_LOG_CATEGORY_STATIC(LogReflectionCaptureComponent, Log, All);

/** 
 * Size of all reflection captures.
 * Reflection capture derived data versions must be changed if modifying this
 */
ENGINE_API TAutoConsoleVariable<int32> CVarReflectionCaptureSize(
	TEXT("r.ReflectionCaptureResolution"),
	128,
	TEXT("Set the resolution for all reflection capture cubemaps. Should be set via project's Render Settings. Must be power of 2. Defaults to 128.\n")
	);

ENGINE_API TAutoConsoleVariable<int32> CVarMobileReflectionCaptureCompression(
	TEXT("r.Mobile.ReflectionCaptureCompression"),
	0,
	TEXT("Whether to use the Reflection Capture Compression or not for mobile. It will use ETC2 format to do the compression.\n")
);

TAutoConsoleVariable<int32> CVarReflectionCaptureUpdateEveryFrame(
	TEXT("r.ReflectionCaptureUpdateEveryFrame"),
	0,
	TEXT("When set, reflection captures will constantly be scheduled for update.\n")
);

static int32 SanitizeReflectionCaptureSize(int32 ReflectionCaptureSize)
{
	const int32 MaxReflectionCaptureSize = GetMaxCubeTextureDimension();
	const int32 MinReflectionCaptureSize = 1;

	return FMath::Clamp(ReflectionCaptureSize, MinReflectionCaptureSize, MaxReflectionCaptureSize);
}

int32 UReflectionCaptureComponent::GetReflectionCaptureSize()
{
	return SanitizeReflectionCaptureSize(CVarReflectionCaptureSize.GetValueOnAnyThread());
}

FReflectionCaptureMapBuildData* UReflectionCaptureComponent::GetMapBuildData() const
{
	AActor* Owner = GetOwner();

	if (Owner)
	{
		ULevel* OwnerLevel = Owner->GetLevel();

		if (OwnerLevel && OwnerLevel->OwningWorld)
		{
			ULevel* ActiveLightingScenario = OwnerLevel->OwningWorld->GetActiveLightingScenario();
			UMapBuildDataRegistry* MapBuildData = NULL;

			if (ActiveLightingScenario && ActiveLightingScenario->MapBuildData)
			{
				MapBuildData = ActiveLightingScenario->MapBuildData;
			}
			
			// Fixed: Reflection capture lost when switching lighting scenario sublevel.
			if (OwnerLevel->MapBuildData)
			{
				MapBuildData = OwnerLevel->MapBuildData;
			}
			 
			if (MapBuildData)
			{
				FReflectionCaptureMapBuildData* ReflectionBuildData = MapBuildData->GetReflectionCaptureBuildData(MapBuildDataId);

				if (ReflectionBuildData && (ReflectionBuildData->CubemapSize == UReflectionCaptureComponent::GetReflectionCaptureSize() || ReflectionBuildData->HasBeenUploadedFinal()))
				{
					return ReflectionBuildData;
				}
			}
		}
	}
	
	return NULL;
}

void UReflectionCaptureComponent::PropagateLightingScenarioChange()
{
	// GetMapBuildData has changed, re-upload
	MarkDirtyForRecaptureOrUpload();
}

AReflectionCapture::AReflectionCapture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CaptureComponent = CreateDefaultSubobject<UReflectionCaptureComponent>(TEXT("NewReflectionComponent"));

	bCanBeInCluster = true;

#if WITH_EDITORONLY_DATA
	SpriteComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (!IsRunningCommandlet() && (SpriteComponent != nullptr))
	{
		// Structure to hold one-time initialization
		struct FConstructorStatics
		{
			FName NAME_ReflectionCapture;
			ConstructorHelpers::FObjectFinderOptional<UTexture2D> DecalTexture;
			FConstructorStatics()
				: NAME_ReflectionCapture(TEXT("ReflectionCapture"))
				, DecalTexture(TEXT("/Engine/EditorResources/S_ReflActorIcon"))
			{
			}
		};
		static FConstructorStatics ConstructorStatics;

		SpriteComponent->Sprite = ConstructorStatics.DecalTexture.Get();
		SpriteComponent->SetRelativeScale3D_Direct(FVector(0.5f, 0.5f, 0.5f));
		SpriteComponent->bHiddenInGame = true;
		SpriteComponent->SetUsingAbsoluteScale(true);
		SpriteComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		SpriteComponent->bIsScreenSizeScaled = true;
	}

	CaptureOffsetComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("CaptureOffset"));
	if (!IsRunningCommandlet() && (CaptureOffsetComponent != nullptr))
	{
		// Structure to hold one-time initialization
		struct FConstructorStatics
		{
			FName NAME_ReflectionCapture;
			ConstructorHelpers::FObjectFinderOptional<UTexture2D> DecalTexture;
			FConstructorStatics()
				: NAME_ReflectionCapture(TEXT("ReflectionCapture"))
				, DecalTexture(TEXT("/Engine/EditorResources/S_ReflActorIcon"))
			{
			}
		};
		static FConstructorStatics ConstructorStatics;

		CaptureOffsetComponent->Sprite = ConstructorStatics.DecalTexture.Get();
		CaptureOffsetComponent->SetRelativeScale3D_Direct(FVector(0.2f, 0.2f, 0.2f));
		CaptureOffsetComponent->bHiddenInGame = true;
		CaptureOffsetComponent->SetUsingAbsoluteScale(true);
		CaptureOffsetComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		CaptureOffsetComponent->bIsScreenSizeScaled = true;
	}

	if (CaptureComponent)
	{
		CaptureComponent->CaptureOffsetComponent = CaptureOffsetComponent;
	}
	
#endif // WITH_EDITORONLY_DATA
}

ASphereReflectionCapture::ASphereReflectionCapture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<USphereReflectionCaptureComponent>(TEXT("NewReflectionComponent")))
{
	USphereReflectionCaptureComponent* SphereComponent = CastChecked<USphereReflectionCaptureComponent>(GetCaptureComponent());
	RootComponent = SphereComponent;
#if WITH_EDITORONLY_DATA
	if (GetSpriteComponent())
	{
		GetSpriteComponent()->SetupAttachment(SphereComponent);
	}
	if (GetCaptureOffsetComponent())
	{
		GetCaptureOffsetComponent()->SetupAttachment(SphereComponent);
	}
#endif	//WITH_EDITORONLY_DATA

	UDrawSphereComponent* DrawInfluenceRadius = CreateDefaultSubobject<UDrawSphereComponent>(TEXT("DrawRadius0"));
	DrawInfluenceRadius->SetupAttachment(GetCaptureComponent());
	DrawInfluenceRadius->bDrawOnlyIfSelected = true;
	DrawInfluenceRadius->bUseEditorCompositing = true;
	DrawInfluenceRadius->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SphereComponent->PreviewInfluenceRadius = DrawInfluenceRadius;

	DrawCaptureRadius = CreateDefaultSubobject<UDrawSphereComponent>(TEXT("DrawRadius1"));
	DrawCaptureRadius->SetupAttachment(GetCaptureComponent());
	DrawCaptureRadius->bDrawOnlyIfSelected = true;
	DrawCaptureRadius->bUseEditorCompositing = true;
	DrawCaptureRadius->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	DrawCaptureRadius->ShapeColor = FColor(100, 90, 40);
}

#if WITH_EDITOR
void ASphereReflectionCapture::EditorApplyScale(const FVector& DeltaScale, const FVector* PivotLocation, bool bAltDown, bool bShiftDown, bool bCtrlDown)
{
	USphereReflectionCaptureComponent* SphereComponent = Cast<USphereReflectionCaptureComponent>(GetCaptureComponent());
	check(SphereComponent);
	const FVector ModifiedScale = DeltaScale * ( AActor::bUsePercentageBasedScaling ? 5000.0f : 50.0f );
	FMath::ApplyScaleToFloat(SphereComponent->InfluenceRadius, ModifiedScale);
	GetCaptureComponent()->InvalidateLightingCache();
	PostEditChange();
}

void APlaneReflectionCapture::EditorApplyScale(const FVector& DeltaScale, const FVector* PivotLocation, bool bAltDown, bool bShiftDown, bool bCtrlDown)
{
	UPlaneReflectionCaptureComponent* PlaneComponent = Cast<UPlaneReflectionCaptureComponent>(GetCaptureComponent());
	check(PlaneComponent);
	const FVector ModifiedScale = DeltaScale * ( AActor::bUsePercentageBasedScaling ? 5000.0f : 50.0f );
	FMath::ApplyScaleToFloat(PlaneComponent->InfluenceRadiusScale, ModifiedScale);
	GetCaptureComponent()->InvalidateLightingCache();
	PostEditChange();
}
#endif

ABoxReflectionCapture::ABoxReflectionCapture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UBoxReflectionCaptureComponent>(TEXT("NewReflectionComponent")))
{
	UBoxReflectionCaptureComponent* BoxComponent = CastChecked<UBoxReflectionCaptureComponent>(GetCaptureComponent());
	BoxComponent->SetRelativeScale3D_Direct(FVector(1000, 1000, 400));
	RootComponent = BoxComponent;
#if WITH_EDITORONLY_DATA
	if (GetSpriteComponent())
	{
		GetSpriteComponent()->SetupAttachment(BoxComponent);
	}
	if (GetCaptureOffsetComponent())
	{
		GetCaptureOffsetComponent()->SetupAttachment(BoxComponent);
	}
#endif	//WITH_EDITORONLY_DATA
	UBoxComponent* DrawInfluenceBox = CreateDefaultSubobject<UBoxComponent>(TEXT("DrawBox0"));
	DrawInfluenceBox->SetupAttachment(GetCaptureComponent());
	DrawInfluenceBox->bDrawOnlyIfSelected = true;
	DrawInfluenceBox->bUseEditorCompositing = true;
	DrawInfluenceBox->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	DrawInfluenceBox->InitBoxExtent(FVector(1, 1, 1));
	BoxComponent->PreviewInfluenceBox = DrawInfluenceBox;

	UBoxComponent* DrawCaptureBox = CreateDefaultSubobject<UBoxComponent>(TEXT("DrawBox1"));
	DrawCaptureBox->SetupAttachment(GetCaptureComponent());
	DrawCaptureBox->bDrawOnlyIfSelected = true;
	DrawCaptureBox->bUseEditorCompositing = true;
	DrawCaptureBox->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	DrawCaptureBox->ShapeColor = FColor(100, 90, 40);
	DrawCaptureBox->InitBoxExtent(FVector(1, 1, 1));
	BoxComponent->PreviewCaptureBox = DrawCaptureBox;
}

APlaneReflectionCapture::APlaneReflectionCapture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UPlaneReflectionCaptureComponent>(TEXT("NewReflectionComponent")))
{
	UPlaneReflectionCaptureComponent* PlaneComponent = CastChecked<UPlaneReflectionCaptureComponent>(GetCaptureComponent());
	PlaneComponent->SetRelativeScale3D_Direct(FVector(1, 1000, 1000));
	RootComponent = PlaneComponent;
#if WITH_EDITORONLY_DATA
	if (GetSpriteComponent())
	{
		GetSpriteComponent()->SetupAttachment(PlaneComponent);
	}
	if (GetCaptureOffsetComponent())
	{
		GetCaptureOffsetComponent()->SetupAttachment(PlaneComponent);
	}
#endif	//#if WITH_EDITORONLY_DATA
	UDrawSphereComponent* DrawInfluenceRadius = CreateDefaultSubobject<UDrawSphereComponent>(TEXT("DrawRadius0"));
	DrawInfluenceRadius->SetupAttachment(GetCaptureComponent());
	DrawInfluenceRadius->bDrawOnlyIfSelected = true;
	DrawInfluenceRadius->SetUsingAbsoluteScale(true);
	DrawInfluenceRadius->bUseEditorCompositing = true;
	DrawInfluenceRadius->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	PlaneComponent->PreviewInfluenceRadius = DrawInfluenceRadius;

	UBoxComponent* DrawCaptureBox = CreateDefaultSubobject<UBoxComponent>(TEXT("DrawBox1"));
	DrawCaptureBox->SetupAttachment(GetCaptureComponent());
	DrawCaptureBox->bDrawOnlyIfSelected = true;
	DrawCaptureBox->bUseEditorCompositing = true;
	DrawCaptureBox->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	DrawCaptureBox->ShapeColor = FColor(100, 90, 40);
	DrawCaptureBox->InitBoxExtent(FVector(1, 1, 1));
	PlaneComponent->PreviewCaptureBox = DrawCaptureBox;
}

FColor RGBMEncode( FLinearColor Color, float MaxValueRGBM)
{
	FColor Encoded;

	// Convert to gamma space
	Color.R = FMath::Sqrt( Color.R );
	Color.G = FMath::Sqrt( Color.G );
	Color.B = FMath::Sqrt( Color.B );

	// Range
	Color /= MaxValueRGBM;
	
	float MaxValue = FMath::Max( FMath::Max(Color.R, Color.G), FMath::Max(Color.B, DELTA) );
	
	if( MaxValue > 0.75f )
	{
		// Fit to valid range by leveling off intensity
		float Tonemapped = ( MaxValue - 0.75 * 0.75 ) / ( MaxValue - 0.5 );
		Color *= Tonemapped / MaxValue;
		MaxValue = Tonemapped;
	}

	Encoded.A = FMath::Min( FMath::CeilToInt( MaxValue * 255.0f ), 255 );
	Encoded.R = FMath::RoundToInt( ( Color.R * 255.0f / Encoded.A ) * 255.0f );
	Encoded.G = FMath::RoundToInt( ( Color.G * 255.0f / Encoded.A ) * 255.0f );
	Encoded.B = FMath::RoundToInt( ( Color.B * 255.0f / Encoded.A ) * 255.0f );

	return Encoded;
}

// Based off of CubemapGen
// https://code.google.com/p/cubemapgen/

#define FACE_X_POS 0
#define FACE_X_NEG 1
#define FACE_Y_POS 2
#define FACE_Y_NEG 3
#define FACE_Z_POS 4
#define FACE_Z_NEG 5

#define EDGE_LEFT   0	 // u = 0
#define EDGE_RIGHT  1	 // u = 1
#define EDGE_TOP    2	 // v = 0
#define EDGE_BOTTOM 3	 // v = 1

#define CORNER_NNN  0
#define CORNER_NNP  1
#define CORNER_NPN  2
#define CORNER_NPP  3
#define CORNER_PNN  4
#define CORNER_PNP  5
#define CORNER_PPN  6
#define CORNER_PPP  7

// D3D cube map face specification
//   mapping from 3D x,y,z cube map lookup coordinates 
//   to 2D within face u,v coordinates
//
//   --------------------> U direction 
//   |                   (within-face texture space)
//   |         _____
//   |        |     |
//   |        | +Y  |
//   |   _____|_____|_____ _____
//   |  |     |     |     |     |
//   |  | -X  | +Z  | +X  | -Z  |
//   |  |_____|_____|_____|_____|
//   |        |     |
//   |        | -Y  |
//   |        |_____|
//   |
//   v   V direction
//      (within-face texture space)

// Index by [Edge][FaceOrEdge]
static const int32 CubeEdgeListA[12][2] =
{
	{FACE_X_POS, EDGE_LEFT},
	{FACE_X_POS, EDGE_RIGHT},
	{FACE_X_POS, EDGE_TOP},
	{FACE_X_POS, EDGE_BOTTOM},

	{FACE_X_NEG, EDGE_LEFT},
	{FACE_X_NEG, EDGE_RIGHT},
	{FACE_X_NEG, EDGE_TOP},
	{FACE_X_NEG, EDGE_BOTTOM},

	{FACE_Z_POS, EDGE_TOP},
	{FACE_Z_POS, EDGE_BOTTOM},
	{FACE_Z_NEG, EDGE_TOP},
	{FACE_Z_NEG, EDGE_BOTTOM}
};

static const int32 CubeEdgeListB[12][2] =
{
	{FACE_Z_POS, EDGE_RIGHT },
	{FACE_Z_NEG, EDGE_LEFT  },
	{FACE_Y_POS, EDGE_RIGHT },
	{FACE_Y_NEG, EDGE_RIGHT },

	{FACE_Z_NEG, EDGE_RIGHT },
	{FACE_Z_POS, EDGE_LEFT  },
	{FACE_Y_POS, EDGE_LEFT  },
	{FACE_Y_NEG, EDGE_LEFT  },

	{FACE_Y_POS, EDGE_BOTTOM },
	{FACE_Y_NEG, EDGE_TOP    },
	{FACE_Y_POS, EDGE_TOP    },
	{FACE_Y_NEG, EDGE_BOTTOM },
};

// Index by [Face][Corner]
static const int32 CubeCornerList[6][4] =
{
	{ CORNER_PPP, CORNER_PPN, CORNER_PNP, CORNER_PNN },
	{ CORNER_NPN, CORNER_NPP, CORNER_NNN, CORNER_NNP },
	{ CORNER_NPN, CORNER_PPN, CORNER_NPP, CORNER_PPP },
	{ CORNER_NNP, CORNER_PNP, CORNER_NNN, CORNER_PNN },
	{ CORNER_NPP, CORNER_PPP, CORNER_NNP, CORNER_PNP },
	{ CORNER_PPN, CORNER_NPN, CORNER_PNN, CORNER_NNN }
};

static void EdgeWalkSetup( bool ReverseDirection, int32 Edge, int32 MipSize, int32& EdgeStart, int32& EdgeStep )
{
	if( ReverseDirection )
	{
		switch( Edge )
		{
		case EDGE_LEFT:		//start at lower left and walk up
			EdgeStart = MipSize * (MipSize - 1);
			EdgeStep = -MipSize;
			break;
		case EDGE_RIGHT:	//start at lower right and walk up
			EdgeStart = MipSize * (MipSize - 1) + (MipSize - 1);
			EdgeStep = -MipSize;
			break;
		case EDGE_TOP:		//start at upper right and walk left
			EdgeStart = (MipSize - 1);
			EdgeStep = -1;
			break;
		case EDGE_BOTTOM:	//start at lower right and walk left
			EdgeStart = MipSize * (MipSize - 1) + (MipSize - 1);
			EdgeStep = -1;
			break;
		}            
	}
	else
	{
		switch( Edge )
		{
		case EDGE_LEFT:		//start at upper left and walk down
			EdgeStart = 0;
			EdgeStep = MipSize;
			break;
		case EDGE_RIGHT:	//start at upper right and walk down
			EdgeStart = (MipSize - 1);
			EdgeStep = MipSize;
			break;
		case EDGE_TOP:		//start at upper left and walk left
			EdgeStart = 0;
			EdgeStep = 1;
			break;
		case EDGE_BOTTOM:	//start at lower left and walk left
			EdgeStart = MipSize * (MipSize - 1);
			EdgeStep = 1;
			break;
		}
	}
}

float GetMaxValueRGBM(const TArray<uint8>& FullHDRData, int32 CubemapSize, float Brightness)
{
	const int32 NumMips = FMath::CeilLogTwo(CubemapSize) + 1;
	// get MaxValue from Mip0
	float MaxValue = 0;
	
	const int32 MipSize = 1 << (NumMips - 1);
	const int32 SourceCubeFaceBytes = MipSize * MipSize * sizeof(FFloat16Color);

	for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
	{
		const int32 FaceSourceIndex = CubeFace * SourceCubeFaceBytes;
		const FFloat16Color* FaceSourceData = (const FFloat16Color*)&FullHDRData[FaceSourceIndex];

		for (int32 y = 0; y < MipSize; y++)
		{
			for (int32 x = 0; x < MipSize; x++)
			{
				int32 TexelIndex = x + y * MipSize;
				const FLinearColor LinearColor = FLinearColor(FaceSourceData[TexelIndex]) * Brightness;
				float MaxValueTexel = FMath::Max(FMath::Max(LinearColor.R, LinearColor.G), FMath::Max(LinearColor.B, DELTA));
				if (MaxValue < MaxValueTexel)
				{
					MaxValue = MaxValueTexel;
				}
			}
		}
	}

	MaxValue = FMath::Max(MaxValue, 1.0f);
	return MaxValue;
}

void GenerateEncodedHDRData(const TArray<uint8>& FullHDRData, int32 CubemapSize, float Brightness, float MaxValueRGBM, TArray<uint8>& OutEncodedHDRData)
{
	check(FullHDRData.Num() > 0);
	const int32 NumMips = FMath::CeilLogTwo(CubemapSize) + 1;

	int32 SourceMipBaseIndex = 0;
	int32 DestMipBaseIndex = 0;

	int32 EncodedDataSize = FullHDRData.Num() * sizeof(FColor) / sizeof(FFloat16Color);

	OutEncodedHDRData.Empty(EncodedDataSize);
	OutEncodedHDRData.AddZeroed(EncodedDataSize);
	
	MaxValueRGBM = FMath::Max(MaxValueRGBM, 1.0f);

	for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
	{
		const int32 MipSize = 1 << (NumMips - MipIndex - 1);
		const int32 SourceCubeFaceBytes = MipSize * MipSize * sizeof(FFloat16Color);
		const int32 DestCubeFaceBytes = MipSize * MipSize * sizeof(FColor);

		const FFloat16Color*	MipSrcData = (const FFloat16Color*)&FullHDRData[SourceMipBaseIndex];
		FColor*					MipDstData = (FColor*)&OutEncodedHDRData[DestMipBaseIndex];

#if MOBILE_AVERAGE_CUBEMAP_EDGES
		// Fix cubemap seams by averaging colors across edges
		int32 CornerTable[4] =
		{
			0,
			MipSize - 1,
			MipSize * (MipSize - 1),
			MipSize * (MipSize - 1) + MipSize - 1,
		};

		// Average corner colors
		FLinearColor AvgCornerColors[8];
		FPlatformMemory::Memset( AvgCornerColors, 0, sizeof( AvgCornerColors ) );
		for( int32 Face = 0; Face < CubeFace_MAX; Face++ )
		{
			const FFloat16Color* FaceSrcData = MipSrcData + Face * MipSize * MipSize;

			for( int32 Corner = 0; Corner < 4; Corner++ )
			{
				AvgCornerColors[ CubeCornerList[Face][Corner] ] += FLinearColor( FaceSrcData[ CornerTable[Corner] ] );
			}
		}

		// Encode corners
		for( int32 Face = 0; Face < CubeFace_MAX; Face++ )
		{
			FColor* FaceDstData = MipDstData + Face * MipSize * MipSize;

			for( int32 Corner = 0; Corner < 4; Corner++ )
			{
				const FLinearColor LinearColor = AvgCornerColors[ CubeCornerList[Face][Corner] ] / 3.0f;
				FaceDstData[ CornerTable[Corner] ] = RGBMEncode( LinearColor * Brightness, MaxValueRGBM);
			}
		}

		// Average edge colors
		for( int32 EdgeIndex = 0; EdgeIndex < 12; EdgeIndex++ )
		{
			int32 FaceA = CubeEdgeListA[ EdgeIndex ][0];
			int32 EdgeA = CubeEdgeListA[ EdgeIndex ][1];

			int32 FaceB = CubeEdgeListB[ EdgeIndex ][0];
			int32 EdgeB = CubeEdgeListB[ EdgeIndex ][1];

			const FFloat16Color*	FaceSrcDataA = MipSrcData + FaceA * MipSize * MipSize;
			FColor*					FaceDstDataA = MipDstData + FaceA * MipSize * MipSize;

			const FFloat16Color*	FaceSrcDataB = MipSrcData + FaceB * MipSize * MipSize;
			FColor*					FaceDstDataB = MipDstData + FaceB * MipSize * MipSize;

			int32 EdgeStartA = 0;
			int32 EdgeStepA = 0;
			int32 EdgeStartB = 0;
			int32 EdgeStepB = 0;

			EdgeWalkSetup( false, EdgeA, MipSize, EdgeStartA, EdgeStepA );
			EdgeWalkSetup( EdgeA == EdgeB || EdgeA + EdgeB == 3, EdgeB, MipSize, EdgeStartB, EdgeStepB );

			// Walk edge
			// Skip corners
			for( int32 Texel = 1; Texel < MipSize - 1; Texel++ )       
			{
				int32 EdgeTexelA = EdgeStartA + EdgeStepA * Texel;
				int32 EdgeTexelB = EdgeStartB + EdgeStepB * Texel;

				check( 0 <= EdgeTexelA && EdgeTexelA < MipSize * MipSize );
				check( 0 <= EdgeTexelB && EdgeTexelB < MipSize * MipSize );

				const FLinearColor EdgeColorA = FLinearColor( FaceSrcDataA[ EdgeTexelA ] );
				const FLinearColor EdgeColorB = FLinearColor( FaceSrcDataB[ EdgeTexelB ] );
				const FLinearColor AvgColor = 0.5f * ( EdgeColorA + EdgeColorB );
				
				FaceDstDataA[ EdgeTexelA ] = FaceDstDataB[ EdgeTexelB ] = RGBMEncode( AvgColor * Brightness, MaxValueRGBM);
			}
		}
#endif // MOBILE_AVERAGE_CUBEMAP_EDGES

		// Encode rest of texels
		for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
		{
			const int32 FaceSourceIndex = SourceMipBaseIndex + CubeFace * SourceCubeFaceBytes;
			const int32 FaceDestIndex = DestMipBaseIndex + CubeFace * DestCubeFaceBytes;
			const FFloat16Color* FaceSourceData = (const FFloat16Color*)&FullHDRData[FaceSourceIndex];
			FColor* FaceDestData = (FColor*)&OutEncodedHDRData[FaceDestIndex];

			// Convert each texel from linear space FP16 to RGBM FColor
			// Note: Brightness on the capture is baked into the encoded HDR data
			// Skip edges
			const int32 SkipEdges = MOBILE_AVERAGE_CUBEMAP_EDGES ? 1 : 0;

			// Find MaxValue
			for (int32 y = SkipEdges; y < MipSize - SkipEdges; y++)
			{
				for (int32 x = SkipEdges; x < MipSize - SkipEdges; x++)
				{
					int32 TexelIndex = x + y * MipSize;
					const FLinearColor LinearColor = FLinearColor(FaceSourceData[TexelIndex]) * Brightness;
					FaceDestData[ TexelIndex ] = RGBMEncode( LinearColor, MaxValueRGBM);
				}
			}
		}

		SourceMipBaseIndex += SourceCubeFaceBytes * CubeFace_MAX;
		DestMipBaseIndex += DestCubeFaceBytes * CubeFace_MAX;
	}
}


void GenerateEncodedHDRTextureCube(UMapBuildDataRegistry* Registry, FReflectionCaptureData& ReflectionCaptureData, FString& TextureName, float MaxValueRGBM, UReflectionCaptureComponent* CaptureComponent, bool bIsReflectionCaptureCompressionProjectSetting)
{
#if WITH_EDITOR
	UTextureFactory* TextureFactory = NewObject<UTextureFactory>();
	TextureFactory->SuppressImportOverwriteDialog();

	TextureFactory->CompressionSettings = TC_ReflectionCapture;
	UTextureCube* TextureCube = TextureFactory->CreateTextureCube(Registry, FName(TextureName), RF_Public);

	if (TextureCube)
	{
		TArray<uint8> TemporaryEncodedHDRCapturedData;

		GenerateEncodedHDRData(ReflectionCaptureData.FullHDRCapturedData, ReflectionCaptureData.CubemapSize, ReflectionCaptureData.Brightness, MaxValueRGBM, TemporaryEncodedHDRCapturedData);
		const int32 NumMips = FMath::CeilLogTwo(ReflectionCaptureData.CubemapSize) + 1;
		TextureCube->Source.Init(
			ReflectionCaptureData.CubemapSize,
			ReflectionCaptureData.CubemapSize,
			6,
			NumMips,
			TSF_BGRA8,
			TemporaryEncodedHDRCapturedData.GetData()
		);
		// the loader can suggest a compression setting
		TextureCube->LODGroup = TEXTUREGROUP_World;

		bool bIsCompressed = false;
		if (CaptureComponent != nullptr)
		{
			bIsCompressed = CaptureComponent->MobileReflectionCompression == EMobileReflectionCompression::Default ? bIsReflectionCaptureCompressionProjectSetting : CaptureComponent->MobileReflectionCompression == EMobileReflectionCompression::On;
		}

		TextureCube->CompressionSettings = TC_ReflectionCapture;
		TextureCube->CompressionNone = !bIsCompressed;
		TextureCube->CompressionQuality = TCQ_Highest;
		TextureCube->Filter = TF_Trilinear;
		TextureCube->SRGB = 0;

		// for now we don't support mip map generation on cubemaps
		TextureCube->MipGenSettings = TMGS_LeaveExistingMips;

		TextureCube->UpdateResource();
		TextureCube->MarkPackageDirty();
	}
	ReflectionCaptureData.EncodedCaptureData = TextureCube;
#endif
}

TArray<UReflectionCaptureComponent*> UReflectionCaptureComponent::ReflectionCapturesToUpdate;
TArray<UReflectionCaptureComponent*> UReflectionCaptureComponent::ReflectionCapturesToUpdateForLoad;
FCriticalSection UReflectionCaptureComponent::ReflectionCapturesToUpdateForLoadLock;

UReflectionCaptureComponent::UReflectionCaptureComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Brightness = 1;
	bModifyMaxValueRGBM = false;
	MaxValueRGBM = 0.0f;
	// Shouldn't be able to change reflection captures at runtime
	Mobility = EComponentMobility::Static;
	CachedEncodedHDRCubemap = nullptr;
	CachedAverageBrightness = 1.0f;
	bNeedsRecaptureOrUpload = false;
}

void UReflectionCaptureComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	Super::CreateRenderState_Concurrent(Context);

	UpdatePreviewShape();

	if (ShouldComponentAddToScene() && ShouldRender())
	{
		GetWorld()->Scene->AddReflectionCapture(this);
	}
}

void UReflectionCaptureComponent::SendRenderTransform_Concurrent()
{	
	// Don't update the transform of a component that needs to be recaptured,
	// Otherwise the RT will get the new transform one frame before the capture
	if (!bNeedsRecaptureOrUpload)
	{
		UpdatePreviewShape();

		if (ShouldComponentAddToScene() && ShouldRender())
		{
			GetWorld()->Scene->UpdateReflectionCaptureTransform(this);
		}
	}

	Super::SendRenderTransform_Concurrent();
}

void UReflectionCaptureComponent::OnRegister()
{
	const ERHIFeatureLevel::Type FeatureLevel = GetWorld()->FeatureLevel;
	const bool bEncodedDataRequired = (FeatureLevel == ERHIFeatureLevel::ES3_1) && !IsMobileDeferredShadingEnabled(GMaxRHIShaderPlatform);

	if (bEncodedDataRequired)
	{
		const FReflectionCaptureMapBuildData* MapBuildData = GetMapBuildData();

		// If the MapBuildData is valid, update it. If it is not we will use the cached values, if there are any
		if (MapBuildData)
		{
			CachedEncodedHDRCubemap = MapBuildData->EncodedCaptureData;
			CachedAverageBrightness = MapBuildData->AverageBrightness;
		}
	}
	else
	{
		// SM5 doesn't require cached values
		CachedEncodedHDRCubemap = nullptr;
		CachedAverageBrightness = 0;
	}

	Super::OnRegister();
}

void UReflectionCaptureComponent::DestroyRenderState_Concurrent()
{
	Super::DestroyRenderState_Concurrent();
	GetWorld()->Scene->RemoveReflectionCapture(this);
}

void UReflectionCaptureComponent::InvalidateLightingCacheDetailed(bool bInvalidateBuildEnqueuedLighting, bool bTranslationOnly)
{
	// Save the static mesh state for transactions, force it to be marked dirty if we are going to discard any static lighting data.
	Modify(true);

	Super::InvalidateLightingCacheDetailed(bInvalidateBuildEnqueuedLighting, bTranslationOnly);

	MapBuildDataId = FGuid::NewGuid();

	MarkRenderStateDirty();
}

void UReflectionCaptureComponent::PostInitProperties()
{
	Super::PostInitProperties();

	// Gets overwritten with saved value (if being loaded from disk)
	FPlatformMisc::CreateGuid(MapBuildDataId);
#if WITH_EDITOR
	bMapBuildDataIdLoaded = false;
#endif

	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		FScopeLock Lock(&ReflectionCapturesToUpdateForLoadLock);
		ReflectionCapturesToUpdateForLoad.AddUnique(this);
		bNeedsRecaptureOrUpload = true; 
	}
}

void UReflectionCaptureComponent::SerializeLegacyData(FArchive& Ar)
{
	Ar.UsingCustomVersion(FRenderingObjectVersion::GUID);
	Ar.UsingCustomVersion(FReflectionCaptureObjectVersion::GUID);

	if (Ar.CustomVer(FReflectionCaptureObjectVersion::GUID) < FReflectionCaptureObjectVersion::MoveReflectionCaptureDataToMapBuildData)
	{
		if (Ar.UE4Ver() >= VER_UE4_REFLECTION_CAPTURE_COOKING)
		{
			bool bLegacy = false;
			Ar << bLegacy;
		}

		if (Ar.UE4Ver() >= VER_UE4_REFLECTION_DATA_IN_PACKAGES)
		{
			FGuid SavedVersion;
			Ar << SavedVersion;

			float AverageBrightness = 1.0f;

			if (Ar.CustomVer(FRenderingObjectVersion::GUID) >= FRenderingObjectVersion::ReflectionCapturesStoreAverageBrightness)
			{
				Ar << AverageBrightness;
			}

			int32 EndOffset = 0;
			Ar << EndOffset;

			FGuid LegacyReflectionCaptureVer(0x0c669396, 0x9cb849ae, 0x9f4120ff, 0x5812f4d3);

			if (SavedVersion != LegacyReflectionCaptureVer)
			{
				// Guid version of saved source data doesn't match latest, skip the data
				// The skipping is done so we don't have to maintain legacy serialization code paths when changing the format
				Ar.Seek(EndOffset);
			}
			else
			{
				bool bValid = false;
				Ar << bValid;

				if (bValid)
				{
					FReflectionCaptureMapBuildData* LegacyMapBuildData = new FReflectionCaptureMapBuildData();

					if (Ar.CustomVer(FRenderingObjectVersion::GUID) >= FRenderingObjectVersion::CustomReflectionCaptureResolutionSupport)
					{
						Ar << LegacyMapBuildData->CubemapSize;
					}
					else
					{
						LegacyMapBuildData->CubemapSize = 128;
					}

					{
						TArray<uint8> CompressedCapturedData;
						Ar << CompressedCapturedData;

						check(CompressedCapturedData.Num() > 0);
						FMemoryReader MemoryAr(CompressedCapturedData);

						int32 UncompressedSize;
						MemoryAr << UncompressedSize;

						int32 CompressedSize;
						MemoryAr << CompressedSize;

						LegacyMapBuildData->FullHDRCapturedData.Empty(UncompressedSize);
						LegacyMapBuildData->FullHDRCapturedData.AddUninitialized(UncompressedSize);

						const uint8* SourceData = &CompressedCapturedData[MemoryAr.Tell()];
						verify(FCompression::UncompressMemory(NAME_Zlib, LegacyMapBuildData->FullHDRCapturedData.GetData(), UncompressedSize, SourceData, CompressedSize));
					}

					LegacyMapBuildData->AverageBrightness = AverageBrightness;
					LegacyMapBuildData->Brightness = Brightness;

					FReflectionCaptureMapBuildLegacyData LegacyComponentData;
					LegacyComponentData.Id = MapBuildDataId;
					LegacyComponentData.MapBuildData = LegacyMapBuildData;
					GReflectionCapturesWithLegacyBuildData.AddAnnotation(this, MoveTemp(LegacyComponentData));
				}
			}
		}
	}
}

void UReflectionCaptureComponent::Serialize(FArchive& Ar)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UReflectionCaptureComponent::Serialize"), STAT_ReflectionCaptureComponent_Serialize, STATGROUP_LoadTime);

#if WITH_EDITOR
	FGuid OldMapBuildDataId = MapBuildDataId;
#endif

	Super::Serialize(Ar);

	SerializeLegacyData(Ar);

#if WITH_EDITOR
	// Check to see if we overwrote the MapBuildDataId with a loaded one
	if (Ar.IsLoading())
	{
		bMapBuildDataIdLoaded = OldMapBuildDataId != MapBuildDataId;
	}
	else
	{
		// If we're cooking, display a deterministic cook warning if we didn't overwrite the generated GUID at load time
		UE_CLOG(Ar.IsCooking() && !GetOutermost()->HasAnyPackageFlags(PKG_CompiledIn) && !bMapBuildDataIdLoaded, LogReflectionCaptureComponent, Warning, TEXT("%s contains a legacy UReflectionCaptureComponent and is being non-deterministically cooked - please resave the asset and recook."), *GetOutermost()->GetName());
	}
#endif
}

FReflectionCaptureProxy* UReflectionCaptureComponent::CreateSceneProxy()
{
	return new FReflectionCaptureProxy(this);
}

void UReflectionCaptureComponent::UpdatePreviewShape() 
{
	if (CaptureOffsetComponent)
	{
		CaptureOffsetComponent->SetRelativeLocation_Direct(CaptureOffset / GetComponentTransform().GetScale3D());
	}
}

#if WITH_EDITOR
bool UReflectionCaptureComponent::CanEditChange(const FProperty* Property) const
{
	bool bCanEditChange = Super::CanEditChange(Property);

	if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UReflectionCaptureComponent, Cubemap) ||
		Property->GetFName() == GET_MEMBER_NAME_CHECKED(UReflectionCaptureComponent, SourceCubemapAngle))
	{
		bCanEditChange &= ReflectionSourceType == EReflectionSourceType::SpecifiedCubemap;
	}

	return bCanEditChange;
}

void UReflectionCaptureComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UReflectionCaptureComponent, Cubemap) ||
		PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UReflectionCaptureComponent, SourceCubemapAngle) ||
		PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UReflectionCaptureComponent, MobileReflectionCompression) ||
		PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UReflectionCaptureComponent, bModifyMaxValueRGBM) ||
		PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UReflectionCaptureComponent, MaxValueRGBM) ||
		PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UReflectionCaptureComponent, ReflectionSourceType))
	{
		MarkDirtyForRecapture();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif // WITH_EDITOR

void UReflectionCaptureComponent::BeginDestroy()
{
	// Deregister the component from the update queue
	if (bNeedsRecaptureOrUpload)
	{
		FScopeLock Lock(&ReflectionCapturesToUpdateForLoadLock);
		ReflectionCapturesToUpdate.Remove(this);
		ReflectionCapturesToUpdateForLoad.Remove(this);
	}

	// Have to do this because we can't use GetWorld in BeginDestroy
	for (TSet<FSceneInterface*>::TConstIterator SceneIt(GetRendererModule().GetAllocatedScenes()); SceneIt; ++SceneIt)
	{
		FSceneInterface* Scene = *SceneIt;
		Scene->ReleaseReflectionCubemap(this);
	}

	// Begin a fence to track the progress of the above BeginReleaseResource being completed on the RT
	ReleaseResourcesFence.BeginFence();

	Super::BeginDestroy();
}

bool UReflectionCaptureComponent::IsReadyForFinishDestroy()
{
	// Wait until the fence is complete before allowing destruction
	return Super::IsReadyForFinishDestroy() && ReleaseResourcesFence.IsFenceComplete();
}

void UReflectionCaptureComponent::FinishDestroy()
{
	CachedEncodedHDRCubemap = nullptr;

	Super::FinishDestroy();
}

void UReflectionCaptureComponent::MarkDirtyForRecaptureOrUpload() 
{ 
	if (GetVisibleFlag())
	{
		ReflectionCapturesToUpdate.AddUnique(this);
		bNeedsRecaptureOrUpload = true; 
	}
}

void UReflectionCaptureComponent::MarkDirtyForRecapture() 
{ 
	if (GetVisibleFlag())
	{
		MarkPackageDirty();
		MapBuildDataId = FGuid::NewGuid();
		ReflectionCapturesToUpdate.AddUnique(this);
		bNeedsRecaptureOrUpload = true; 
	}
}

void UReflectionCaptureComponent::UpdateReflectionCaptureContents(UWorld* WorldToUpdate, const TCHAR* CaptureReason, bool bVerifyOnlyCapturing, bool bCapturingForMobile)
{
	if (WorldToUpdate->Scene 
		// Don't capture and read back capture contents if we are currently doing async shader compiling
		// This will keep the update requests in the queue until compiling finishes
		// Note: this will also prevent uploads of cubemaps from DDC, which is unintentional
		&& (GShaderCompilingManager == NULL || !GShaderCompilingManager->IsCompiling()))
	{
		//guarantee that all render proxies are up to date before kicking off this render
		WorldToUpdate->SendAllEndOfFrameUpdates();

		if (CVarReflectionCaptureUpdateEveryFrame.GetValueOnGameThread())
		{
			for (FActorIterator It(WorldToUpdate); It; ++It)
			{
				TInlineComponentArray<UReflectionCaptureComponent*> Components;
				(*It)->GetComponents(Components);
				for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ComponentIndex++)
				{
					Components[ComponentIndex]->MarkDirtyForRecapture(); // Continuously refresh reflection captures
				}
			}
		}

		TArray<UReflectionCaptureComponent*> WorldCombinedCaptures;

		for (int32 CaptureIndex = ReflectionCapturesToUpdate.Num() - 1; CaptureIndex >= 0; CaptureIndex--)
		{
			UReflectionCaptureComponent* CaptureComponent = ReflectionCapturesToUpdate[CaptureIndex];

			if (CaptureComponent->GetWorld() == WorldToUpdate)
			{
				WorldCombinedCaptures.Add(CaptureComponent);
				ReflectionCapturesToUpdate.RemoveAt(CaptureIndex);
			}
		}

		if (ReflectionCapturesToUpdateForLoad.Num() > 0)
		{
			FScopeLock Lock(&ReflectionCapturesToUpdateForLoadLock);
			for (int32 CaptureIndex = ReflectionCapturesToUpdateForLoad.Num() - 1; CaptureIndex >= 0; CaptureIndex--)
			{
				UReflectionCaptureComponent* CaptureComponent = ReflectionCapturesToUpdateForLoad[CaptureIndex];

				if (CaptureComponent->GetWorld() == WorldToUpdate)
				{
					WorldCombinedCaptures.Add(CaptureComponent);
					ReflectionCapturesToUpdateForLoad.RemoveAt(CaptureIndex);
				}
			}
		}

		WorldToUpdate->Scene->AllocateReflectionCaptures(WorldCombinedCaptures, CaptureReason, bVerifyOnlyCapturing, bCapturingForMobile);
	}
}

#if WITH_EDITOR
void UReflectionCaptureComponent::PreFeatureLevelChange(ERHIFeatureLevel::Type PendingFeatureLevel)
{
	if (SupportsTextureCubeArray(PendingFeatureLevel))
	{
		CachedEncodedHDRCubemap = nullptr;

		MarkDirtyForRecaptureOrUpload();
	}
}
#endif // WITH_EDITOR

USphereReflectionCaptureComponent::USphereReflectionCaptureComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InfluenceRadius = 3000;
}

void USphereReflectionCaptureComponent::UpdatePreviewShape()
{
	if (PreviewInfluenceRadius)
	{
		PreviewInfluenceRadius->InitSphereRadius(InfluenceRadius);
	}

	Super::UpdatePreviewShape();
}

#if WITH_EDITOR
void USphereReflectionCaptureComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// AActor::PostEditChange will ForceUpdateComponents()
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property && 
		(PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(USphereReflectionCaptureComponent, InfluenceRadius)))
	{
		InvalidateLightingCache();
	}
}
#endif // WITH_EDITOR

UBoxReflectionCaptureComponent::UBoxReflectionCaptureComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	BoxTransitionDistance = 100;
}

void UBoxReflectionCaptureComponent::UpdatePreviewShape()
{
	if (PreviewCaptureBox)
	{
		PreviewCaptureBox->InitBoxExtent(((GetComponentTransform().GetScale3D() - FVector(BoxTransitionDistance)) / GetComponentTransform().GetScale3D()).ComponentMax(FVector::ZeroVector));
	}

	Super::UpdatePreviewShape();
}

float UBoxReflectionCaptureComponent::GetInfluenceBoundingRadius() const
{
	return (GetComponentTransform().GetScale3D() + FVector(BoxTransitionDistance)).Size();
}

#if WITH_EDITOR
void UBoxReflectionCaptureComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// AActor::PostEditChange will ForceUpdateComponents()
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UBoxReflectionCaptureComponent, BoxTransitionDistance))
	{
		InvalidateLightingCache();
	}
}
#endif // WITH_EDITOR

UPlaneReflectionCaptureComponent::UPlaneReflectionCaptureComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InfluenceRadiusScale = 2;
}

void UPlaneReflectionCaptureComponent::UpdatePreviewShape()
{
	if (PreviewInfluenceRadius)
	{
		PreviewInfluenceRadius->InitSphereRadius(GetInfluenceBoundingRadius());
	}

	Super::UpdatePreviewShape();
}

float UPlaneReflectionCaptureComponent::GetInfluenceBoundingRadius() const
{
	return FVector2D(GetComponentTransform().GetScale3D().Y, GetComponentTransform().GetScale3D().Z).Size() * InfluenceRadiusScale;
}

FReflectionCaptureProxy::FReflectionCaptureProxy(const UReflectionCaptureComponent* InComponent)
{
	PackedIndex = INDEX_NONE;
	SortedCaptureIndex = INDEX_NONE;
	CaptureOffset = InComponent->CaptureOffset;

	const USphereReflectionCaptureComponent* SphereComponent = Cast<const USphereReflectionCaptureComponent>(InComponent);
	const UBoxReflectionCaptureComponent* BoxComponent = Cast<const UBoxReflectionCaptureComponent>(InComponent);
	const UPlaneReflectionCaptureComponent* PlaneComponent = Cast<const UPlaneReflectionCaptureComponent>(InComponent);

	// Initialize shape specific settings
	Shape = EReflectionCaptureShape::Num;
	BoxTransitionDistance = 0;

	if (SphereComponent)
	{
		Shape = EReflectionCaptureShape::Sphere;
	}
	else if (BoxComponent)
	{
		Shape = EReflectionCaptureShape::Box;
		BoxTransitionDistance = BoxComponent->BoxTransitionDistance;
	}
	else if (PlaneComponent)
	{
		Shape = EReflectionCaptureShape::Plane;
	}
	else
	{
		check(0);
	}
	
	// Initialize common settings
	Component = InComponent;
	const FReflectionCaptureMapBuildData* MapBuildData = InComponent->GetMapBuildData();

	EncodedHDRCubemap = Component->CachedEncodedHDRCubemap != nullptr ? Component->CachedEncodedHDRCubemap->Resource: nullptr;


	
	EncodedHDRAverageBrightness = Component->CachedAverageBrightness;
	MaxValueRGBM = Component->MaxValueRGBM;
	SetTransform(InComponent->GetComponentTransform().ToMatrixWithScale());
	InfluenceRadius = InComponent->GetInfluenceBoundingRadius();
	Brightness = InComponent->Brightness;
	Guid = GetTypeHash( Component->GetPathName() );

	bUsingPreviewCaptureData = MapBuildData == NULL;
}

void FReflectionCaptureProxy::SetTransform(const FMatrix& InTransform)
{
	Position = InTransform.GetOrigin();
	BoxTransform = InTransform.Inverse();

	FVector ForwardVector(1.0f,0.0f,0.0f);
	FVector RightVector(0.0f,-1.0f,0.0f);
	const FVector4 PlaneNormal = InTransform.TransformVector(ForwardVector);

	// Normalize the plane
	ReflectionPlane = FPlane(Position, FVector(PlaneNormal).GetSafeNormal());
	const FVector ReflectionXAxis = InTransform.TransformVector(RightVector);
	const FVector ScaleVector = InTransform.GetScaleVector();
	BoxScales = ScaleVector;
	// Include the owner's draw scale in the axes
	ReflectionXAxisAndYScale = ReflectionXAxis.GetSafeNormal() * ScaleVector.Y;
	ReflectionXAxisAndYScale.W = ScaleVector.Y / ScaleVector.Z;
}

void FReflectionCaptureProxy::UpdateMobileUniformBuffer()
{
	FTexture* CaptureTexture = GBlackTextureCube;
	if (EncodedHDRCubemap)
	{
		check(EncodedHDRCubemap->IsInitialized());
		CaptureTexture = EncodedHDRCubemap;
	}
		
	FMobileReflectionCaptureShaderParameters Parameters;
	//To keep ImageBasedReflectionLighting coherence with PC, use AverageBrightness instead of InvAverageBrightness to calculate the IBL contribution
	Parameters.Params = FVector4(EncodedHDRAverageBrightness, 0.f, MaxValueRGBM <= 0.0f ? 16.0f: MaxValueRGBM, 0.f);
	Parameters.Texture = CaptureTexture->TextureRHI;
	Parameters.TextureSampler = CaptureTexture->SamplerStateRHI;

	if (MobileUniformBuffer.GetReference())
	{
		MobileUniformBuffer.UpdateUniformBufferImmediate(Parameters);
	}
	else
	{
		MobileUniformBuffer = TUniformBufferRef<FMobileReflectionCaptureShaderParameters>::CreateUniformBufferImmediate(Parameters, UniformBuffer_MultiFrame);
	}
}
