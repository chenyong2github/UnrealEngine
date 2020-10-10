// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace DisplayClusterConfigurationStrings
{
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

		namespace input
		{
			namespace devices
			{
				static constexpr auto VrpnDeviceAnalog   = TEXT("VRPN_Analog");
				static constexpr auto VrpnDeviceButton   = TEXT("VRPN_Button");
				static constexpr auto VrpnDeviceKeyboard = TEXT("VRPN_Keyboard");
				static constexpr auto VrpnDeviceTracker  = TEXT("VRPN_Tracker");

				static constexpr auto Address     = TEXT("address");
				static constexpr auto Remapping   = TEXT("remapping");

				static constexpr auto OriginLocation  = TEXT("OriginLocation");
				static constexpr auto OriginRotation  = TEXT("OriginRotation");
				static constexpr auto OriginComponent = TEXT("OriginComponent");

				static constexpr auto Front = TEXT("front");
				static constexpr auto Right = TEXT("right");
				static constexpr auto Up    = TEXT("up");

				static constexpr auto MapX    = TEXT("x");
				static constexpr auto MapNX   = TEXT("-x");
				static constexpr auto MapY    = TEXT("y");
				static constexpr auto MapNY   = TEXT("-y");
				static constexpr auto MapZ    = TEXT("z");
				static constexpr auto MapNZ   = TEXT("-z");

				static constexpr auto ReflectType     = TEXT("reflection");
				static constexpr auto ReflectNdisplay = TEXT("ndisplay");
				static constexpr auto ReflectCore     = TEXT("core");
				static constexpr auto ReflectAll      = TEXT("all");
				static constexpr auto ReflectNone     = TEXT("none");
			}

			namespace binding
			{
				static constexpr auto BindChannel = TEXT("channel");
				static constexpr auto BindKey     = TEXT("key");
				static constexpr auto BindTo      = TEXT("bind");
			}
		}
	}
};
