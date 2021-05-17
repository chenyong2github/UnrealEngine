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
		static constexpr auto ClusterPostprocessCategory   = TEXT("NDisplay Cluster Postprocess");

		static constexpr auto ICVFXCategory                = TEXT("NDisplay ICVFX");
		static constexpr auto ConfigurationCategory        = TEXT("NDisplay Configuration");
		static constexpr auto PreviewCategory              = TEXT("NDisplay Preview (Editor only)");
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
				static constexpr auto None     = TEXT("none");
				
				static constexpr auto Ethernet = TEXT("ethernet");

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
