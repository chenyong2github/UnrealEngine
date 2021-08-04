// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace DisplayClusterConfigurationStrings
{
	// GUI
	namespace gui
	{
		namespace preview
		{
			static constexpr auto PreviewNodeAll = TEXT("All");
			static constexpr auto PreviewNodeNone = TEXT("None");
		}
	}

	// Property Categories
	namespace categories
	{
		static constexpr auto DefaultCategory              = TEXT("NDisplay");

		static constexpr auto ClusterCategory              = TEXT("NDisplay Cluster");
		static constexpr auto ClusterConfigurationCategory = TEXT("NDisplay Cluster Configuration");
		static constexpr auto ClusterPostprocessCategory   = TEXT("Post Process");
		static constexpr auto ColorGradingCategory         = TEXT("Color Grading");
		static constexpr auto CameraColorGradingCategory   = TEXT("Inner Frustum Color Grading");
		static constexpr auto ChromaKeyCategory            = TEXT("Chromakey");
		static constexpr auto LightcardCategory            = TEXT("Light Cards");
		static constexpr auto OCIOCategory                 = TEXT("OCIO");
		static constexpr auto OverrideCategory             = TEXT("Texture Replacement");
		static constexpr auto ViewportsCategory            = TEXT("Viewports");
		static constexpr auto ICVFXCategory                = TEXT("In Camera VFX");
		static constexpr auto ConfigurationCategory        = TEXT("Configuration");
		static constexpr auto PreviewCategory              = TEXT("Editor Preview");
		static constexpr auto AdvancedCategory             = TEXT("Advanced");
		static constexpr auto TextureShareCategory         = TEXT("Texture Share");
	}

	// Command line arguments
	namespace args
	{
		static constexpr auto Gpu    = TEXT("dc_gpu");
	}

	// Config file extensions
	namespace file
	{
		static constexpr auto FileExtCfg  = TEXT("cfg");
		static constexpr auto FileExtJson = TEXT("ndisplay");
	}

	namespace config
	{
		namespace cluster
		{
			namespace ports
			{
				static constexpr auto PortClusterSync         = TEXT("ClusterSync");
				static constexpr auto PortRenderSync          = TEXT("RenderSync");
				static constexpr auto PortClusterEventsJson   = TEXT("ClusterEventsJson");
				static constexpr auto PortClusterEventsBinary = TEXT("ClusterEventsBinary");
			}

			namespace network
			{
				static constexpr auto NetConnectRetriesAmount     = TEXT("ConnectRetriesAmount");
				static constexpr auto NetConnectRetryDelay        = TEXT("ConnectRetryDelay");
				static constexpr auto NetGameStartBarrierTimeout  = TEXT("GameStartBarrierTimeout");
				static constexpr auto NetFrameStartBarrierTimeout = TEXT("FrameStartBarrierTimeout");
				static constexpr auto NetFrameEndBarrierTimeout   = TEXT("FrameEndBarrierTimeout");
				static constexpr auto NetRenderSyncBarrierTimeout = TEXT("RenderSyncBarrierTimeout");
			}

			namespace input_sync
			{
				static constexpr auto InputSyncPolicyNone            = TEXT("None");
				static constexpr auto InputSyncPolicyReplicateMaster = TEXT("ReplicateMaster");
			}

			namespace render_sync
			{
				static constexpr auto None              = TEXT("none");
				static constexpr auto Ethernet          = TEXT("ethernet");
				static constexpr auto EthernetBarrier   = TEXT("ethernet_barrier");

				static constexpr auto Nvidia            = TEXT("nvidia");
				static constexpr auto NvidiaSwapBarrier = TEXT("swap_barrier");
				static constexpr auto NvidiaSwapGroup   = TEXT("swap_group");
			}
		}

		namespace scene
		{
			namespace camera
			{
				static constexpr auto CameraStereoOffsetNone  = TEXT("none");
				static constexpr auto CameraStereoOffsetLeft  = TEXT("left");
				static constexpr auto CameraStereoOffsetRight = TEXT("right");
			}
		}
	}
};
