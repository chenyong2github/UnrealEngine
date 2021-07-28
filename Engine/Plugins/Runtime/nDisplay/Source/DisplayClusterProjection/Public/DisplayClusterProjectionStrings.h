// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


namespace DisplayClusterProjectionStrings
{
	namespace cfg
	{
		namespace simple
		{
			static constexpr auto Screen = TEXT("screen");
		}

		namespace easyblend
		{
			static constexpr auto File   = TEXT("file");
			static constexpr auto Origin = TEXT("origin");
			static constexpr auto Scale  = TEXT("scale");
		}

		namespace VIOSO
		{
			static constexpr auto Origin = TEXT("origin");

			static constexpr auto INIFile     = TEXT("inifile");
			static constexpr auto ChannelName = TEXT("channel");

			static constexpr auto File         = TEXT("file");
			static constexpr auto CalibIndex   = TEXT("index");
			static constexpr auto CalibAdapter = TEXT("adapter");

			static constexpr auto Gamma = TEXT("gamma");

			static constexpr auto BaseMatrix = TEXT("base");
		}

		namespace manual
		{
			static constexpr auto Rendering    = TEXT("ManualRendering");
			static constexpr auto Type         = TEXT("ManualFrustum");
			static constexpr auto Rotation     = TEXT("rot");

			static constexpr auto Matrix       = TEXT("matrix");
			static constexpr auto MatrixLeft   = TEXT("matrix_left");
			static constexpr auto MatrixRight  = TEXT("matrix_right");

			static constexpr auto Frustum      = TEXT("frustum");
			static constexpr auto FrustumLeft  = TEXT("frustum_left");
			static constexpr auto FrustumRight = TEXT("frustum_right");

			static constexpr auto AngleL      = TEXT("l");
			static constexpr auto AngleR      = TEXT("r");
			static constexpr auto AngleT      = TEXT("t");
			static constexpr auto AngleB      = TEXT("b");
			namespace FrustumType
			{
				static constexpr auto Matrix = TEXT("Matrix");
				static constexpr auto Angles = TEXT("Angles");
			}
			namespace RenderingType
			{
				static constexpr auto Mono = TEXT("Mono");
				static constexpr auto Stereo = TEXT("Stereo");
				static constexpr auto MonoStereo = TEXT("Mono & Stereo");
			}
		}

		namespace camera
		{
			static constexpr auto Component = TEXT("camera_component");
			static constexpr auto Native = TEXT("native");
		}
		
		namespace mesh
		{
			static constexpr auto FileID    = TEXT("@UESM");
			static constexpr auto BufferID  = TEXT("@@Buf");
			static constexpr auto Component = TEXT("mesh_component");
		}

		namespace mpcdi
		{
			static constexpr auto File   = TEXT("file");
			static constexpr auto Buffer = TEXT("buffer");
			static constexpr auto Region = TEXT("region");
			static constexpr auto Origin = TEXT("origin");

			static constexpr auto MPCDIType = TEXT("mpcdi");

			static constexpr auto FilePFM       = TEXT("pfm");
			static constexpr auto WorldScale    = TEXT("scale");
			static constexpr auto UseUnrealAxis = TEXT("ue_space");

			static constexpr auto FileAlpha  = TEXT("alpha");
			static constexpr auto AlphaGamma = TEXT("alpha_gamma");
			static constexpr auto FileBeta   = TEXT("beta");

			static constexpr auto MPCDITypeKey = TEXT("MPCDIType");
			static constexpr auto TypeMPCDI = TEXT("MPCDI");
			static constexpr auto TypePFM = TEXT("Explicit PFM");

			static constexpr auto EnablePreview = TEXT("EnablePreview");
		}

		namespace domeprojection
		{
			static constexpr auto File    = TEXT("file");
			static constexpr auto Channel = TEXT("channel");
			static constexpr auto Origin  = TEXT("origin");
		}
	}

	namespace projection
	{
		static constexpr auto Camera         = TEXT("camera");
		static constexpr auto Simple         = TEXT("simple");
		static constexpr auto MPCDI          = TEXT("mpcdi");
		static constexpr auto Mesh           = TEXT("mesh");
		static constexpr auto EasyBlend      = TEXT("easyblend");
		static constexpr auto VIOSO          = TEXT("vioso");
		static constexpr auto Manual         = TEXT("manual");
		static constexpr auto Domeprojection = TEXT("domeprojection");
		static constexpr auto Link           = TEXT("link");
	}

	namespace rhi
	{
		static constexpr auto D3D11  = TEXT("D3D11");
		static constexpr auto D3D12  = TEXT("D3D12");
	}
};