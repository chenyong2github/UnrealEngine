// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using Gauntlet.Utils;
using System.Collections.Generic;

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

		public static void RecordStart(string deviceName, UnrealTargetPlatform platform, EventType et, EventState state = EventState.Success, string comment = "")
		{
			RecordToAll(deviceName, platform, et, true, state, comment);
		}

		public static void RecordEnd(string deviceName, UnrealTargetPlatform platform, EventType et, EventState state = EventState.Success, string comment = "")
		{
			RecordToAll(deviceName, platform, et, false, state, comment);
		}

		public static void RecordComment(string deviceName, UnrealTargetPlatform platform, EventType et, string comment)
		{
			CommentToAll(deviceName, platform, et, comment);
		}

		private static List<IDeviceUsageReporter> GetEnabledReporters()
		{
			List<IDeviceUsageReporter> ValidReporters = new List<IDeviceUsageReporter>();

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
					ValidReporters.Add(reporter);
				}
			}

			if (!bFoundAnyReporters)
			{
				Gauntlet.Log.VeryVerbose("Skipped reporting DeviceUsage event - no reporter implementations found!");
			}
			else if (!bFoundEnabledReporters)
			{
				Gauntlet.Log.VeryVerbose("Skipped reporting DeviceUsage event - reporter implementations were found, but none were enabled!");
			}

			return ValidReporters;
		}

		private static void RecordToAll(string deviceName, UnrealTargetPlatform platform, EventType ev, bool bStarting, EventState state, string comment = "")
		{
			foreach (IDeviceUsageReporter reporter in GetEnabledReporters())
			{
				Gauntlet.Log.Verbose("Reporting DeviceUsage event {0} via reporter {1}", ev.ToString(), reporter.GetType().Name);
				reporter.RecordEvent(deviceName, platform, ev, bStarting, state, comment);
			}
		}

		private static void CommentToAll(string DeviceName, UnrealTargetPlatform platform, EventType ev, string comment)
		{
			foreach (IDeviceUsageReporter reporter in GetEnabledReporters())
			{
				Gauntlet.Log.Verbose("Adding Comment to DeviceUsage event {0} via reporter {1}", ev.ToString(), reporter.GetType().Name);
				reporter.AddCommentToEvent(DeviceName, platform, ev, comment);
			}
		}

		public abstract void RecordEvent(string deviceName, UnrealTargetPlatform platform, EventType ev, bool bStarting, EventState state = EventState.Success, string comment = "");
		public abstract void AddCommentToEvent(string deviceName, UnrealTargetPlatform platform, EventType ev, string comment);

		public abstract bool IsEnabled();
	}
}