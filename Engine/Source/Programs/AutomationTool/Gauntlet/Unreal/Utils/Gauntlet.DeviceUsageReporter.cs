// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using Gauntlet.Utils;

namespace Gauntlet
{
	public abstract class IDeviceUsageReporter
	{
		public enum EventType
		{
			  Device
			, Install
			, Test
			, SavingArtifacts
		};

		public enum EventState : int
		{
			  Failure = 0
			, Success = 1
		};

		public static void RecordStart(string deviceName, UnrealTargetPlatform platform, EventType et, EventState state = EventState.Success)
		{
			RecordToAll(deviceName, platform, et, true, state);
		}

		public static void RecordEnd(string deviceName, UnrealTargetPlatform platform, EventType et, EventState state = EventState.Success)
		{
			RecordToAll(deviceName, platform, et, false, state);
		}

		private static void RecordToAll(string deviceName, UnrealTargetPlatform platform, EventType ev, bool bStarting, EventState state)
		{
			bool bFoundAnyReporters = false;
			bool bFoundEnabledReporters = false;
			foreach (IDeviceUsageReporter reporter in InterfaceHelpers.FindImplementations<IDeviceUsageReporter>(true))
			{
				// Just in case we get handed an abstract one
				if(reporter.GetType().IsAbstract)
				{
					Gauntlet.Log.Warning("Got abstract IDeviceUsageReporter from InterfaceHelpers.FindImplementations - {0}", reporter.GetType().Name);
					continue;
				}

				bFoundAnyReporters = true;

				if (reporter.IsEnabled())
				{
					bFoundEnabledReporters = true;
					Gauntlet.Log.Verbose("Reporting DeviceUsage event {0} via reporter {1}", ev.ToString(), reporter.GetType().Name);
					reporter.RecordEvent(deviceName, platform, ev, bStarting, state);
				}
			}
			if(!bFoundAnyReporters)
			{
				Gauntlet.Log.Verbose("Skipped reporting DeviceUsage event {0} - no reporter implementations found!", ev.ToString());
			}
			else if(!bFoundEnabledReporters)
			{
				Gauntlet.Log.Verbose("Skipped reporting DeviceUsage event {0} - reporter implementations were found, but none were enabled!", ev.ToString());
			}
		}

		public abstract void RecordEvent(string deviceName, UnrealTargetPlatform platform, EventType ev, bool bStarting, EventState state = EventState.Success);

		public abstract bool IsEnabled();
	}
}