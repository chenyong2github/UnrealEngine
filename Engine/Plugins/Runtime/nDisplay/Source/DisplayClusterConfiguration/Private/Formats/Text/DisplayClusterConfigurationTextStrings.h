// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace DisplayClusterConfigurationTextStrings
{
	// Config data
	namespace cfg
	{
		// Config data tokens
		namespace data
		{
			static constexpr auto Id       = TEXT("id");
			static constexpr auto ParentId = TEXT("parent");
			static constexpr auto Loc      = TEXT("loc");
			static constexpr auto Rot      = TEXT("rot");

			namespace comment
			{
				static constexpr auto Header  = TEXT("#");
			}

			// Config info
			namespace info
			{
				static constexpr auto Header  = TEXT("[info]");
				static constexpr auto Version = TEXT("version");
			}

			// Cluster tokens
			namespace cluster
			{
				static constexpr auto Header  = TEXT("[cluster_node]");
				static constexpr auto Addr    = TEXT("addr");
				static constexpr auto Window  = TEXT("window");
				static constexpr auto PortCS  = TEXT("port_cs");
				static constexpr auto PortSS  = TEXT("port_ss");
				static constexpr auto PortCE  = TEXT("port_ce");
				static constexpr auto PortCEB = TEXT("port_ceb");
				static constexpr auto Master  = TEXT("master");
				static constexpr auto Sound   = TEXT("sound");
				// + Id
			}

			// Window tokens
			namespace window
			{
				static constexpr auto Header       = TEXT("[window]");
				static constexpr auto Viewports    = TEXT("viewports");
				static constexpr auto Postprocess  = TEXT("postprocess");
				static constexpr auto Fullscreen   = TEXT("fullscreen");
				static constexpr auto WinX         = TEXT("WinX");
				static constexpr auto WinY         = TEXT("WinY");
				static constexpr auto ResX         = TEXT("ResX");
				static constexpr auto ResY         = TEXT("ResY");
				// + Id
			}

			// Screen tokens
			namespace screen
			{
				static constexpr auto Header = TEXT("[screen]");
				static constexpr auto Size   = TEXT("size");
				// + Id, Parent, Loc, Rot, TrackerId, TrackerCh
			}

			// Viewport tokens
			namespace viewport
			{
				static constexpr auto Header      = TEXT("[viewport]");
				static constexpr auto Projection  = TEXT("projection");
				static constexpr auto Camera      = TEXT("camera");
				static constexpr auto PosX        = TEXT("x");
				static constexpr auto PosY        = TEXT("y");
				static constexpr auto Width       = TEXT("width");
				static constexpr auto Height      = TEXT("height");
				static constexpr auto BufferRatio = TEXT("buffer_ratio");
				static constexpr auto GPUIndex    = TEXT("gpu_node");
				static constexpr auto IsShared    = TEXT("share");
				static constexpr auto AllowCrossGPUTransfer = TEXT("allow_gpu_transfer");
				// + Id
			}

			// Postprocess tokens
			namespace postprocess
			{
				static constexpr auto Header      = TEXT("[postprocess]");
				static constexpr auto Type        = TEXT("type");
				// + Id
			}

			// Camera tokens
			namespace camera
			{
				static constexpr auto Header      = TEXT("[camera]");
				static constexpr auto EyeDist     = TEXT("eye_dist");
				static constexpr auto EyeSwap     = TEXT("eye_swap");
				static constexpr auto ForceOffset = TEXT("force_offset");

				// + Id, Loc, Rot, Parent, TrackerId, TrackerCh
			}

			// Scene node (transforms)
			namespace scene
			{
				static constexpr auto Header    = TEXT("[scene_node]");
				static constexpr auto TrackerId = TEXT("tracker_id");
				static constexpr auto TrackerCh = TEXT("tracker_ch");
				// + Id, Loc, Rot, Parent, TrackerId, TrackerCh
			}

			// Input tokens
			namespace input
			{
				static constexpr auto Header  = TEXT("[input]");
				static constexpr auto Type    = TEXT("type");
				static constexpr auto Address = TEXT("addr");
				static constexpr auto Remap   = TEXT("remap");

				static constexpr auto Right   = TEXT("right");
				static constexpr auto Front   = TEXT("front");
				static constexpr auto Up      = TEXT("up");

				static constexpr auto MapX    = TEXT("x");
				static constexpr auto MapNX   = TEXT("-x");
				static constexpr auto MapY    = TEXT("y");
				static constexpr auto MapNY   = TEXT("-y");
				static constexpr auto MapZ    = TEXT("z");
				static constexpr auto MapNZ   = TEXT("-z");

				static constexpr auto DeviceTracker  = TEXT("tracker");
				static constexpr auto DeviceAnalog   = TEXT("analog");
				static constexpr auto DeviceButtons  = TEXT("buttons");
				static constexpr auto DeviceKeyboard = TEXT("keyboard");

				static constexpr auto Reflect         = TEXT("reflect");
				static constexpr auto ReflectNdisplay = TEXT("ndisplay");
				static constexpr auto ReflectCore     = TEXT("core");
				static constexpr auto ReflectBoth     = TEXT("both");
				static constexpr auto ReflectNone     = TEXT("none");

				// + Id
			}

			// Input Setup tokens
			namespace inputsetup
			{
				static constexpr auto Header  = TEXT("[input_setup]");
				static constexpr auto Channel = TEXT("ch");
				static constexpr auto Key     = TEXT("key");
				static constexpr auto Bind    = TEXT("bind");

				// + Id
			}

			// General settings tokens
			namespace general
			{
				static constexpr auto Header                = TEXT("[general]");
				static constexpr auto SwapSyncPolicy        = TEXT("swap_sync_policy");
				static constexpr auto UnrealInputSyncPolicy = TEXT("ue4_input_sync_policy");
			}

			namespace nvidia
			{
				static constexpr auto Header         = TEXT("[nvidia]");
				static constexpr auto SyncGroup      = TEXT("sync_group");
				static constexpr auto SyncBarrier    = TEXT("sync_barrier");
			}

			// Network tokens
			namespace network
			{
				static constexpr auto Header                      = TEXT("[network]");
				static constexpr auto ClientConnectTriesAmount    = TEXT("cln_conn_tries_amount");
				static constexpr auto ClientConnectRetryDelay     = TEXT("cln_conn_retry_delay");
				static constexpr auto BarrierGameStartWaitTimeout = TEXT("game_start_timeout");
				static constexpr auto BarrierWaitTimeout          = TEXT("barrier_wait_timeout");
			}

			// Debug tokens
			namespace debug
			{
				static constexpr auto Header    = TEXT("[debug]");
				static constexpr auto LagSim    = TEXT("lag_simulation");
				static constexpr auto LagTime   = TEXT("lag_max_time");
				static constexpr auto DrawStats = TEXT("draw_stats");
			}

			// Custom arguments
			namespace custom
			{
				static constexpr auto Header = TEXT("[custom]");
			}

			// Projection data
			namespace projection
			{
				static constexpr auto Header = TEXT("[projection]");
				static constexpr auto Type   = TEXT("type");

				// + Id
			}
		}
	};
};
