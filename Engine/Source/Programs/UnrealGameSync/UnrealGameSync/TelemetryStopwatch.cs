// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Data.SqlClient;
using System.Diagnostics;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;

namespace UnrealGameSync
{
	class TelemetryStopwatch : IDisposable
	{
		readonly string EventName;
		readonly Dictionary<string, object> EventData;
		readonly Stopwatch Timer;

		public TelemetryStopwatch(string EventName, string Project)
		{
			this.EventName = EventName;

			EventData = new Dictionary<string, object>();
			EventData["Project"] = Project;

			Timer = Stopwatch.StartNew();
		}

		public void AddData(object Data)
		{
			foreach (PropertyInfo Property in Data.GetType().GetProperties())
			{
				EventData[Property.Name] = Property.GetValue(Data);
			}
		}

		public TimeSpan Stop(string InResult)
		{
			if (Timer.IsRunning)
			{
				Timer.Stop();

				EventData["Result"] = InResult;
				EventData["TimeSeconds"] = Timer.Elapsed.TotalSeconds;
			}
			return Elapsed;
		}

		public void Dispose()
		{
			if (Timer.IsRunning)
			{
				Stop("Aborted");
			}
			Telemetry.SendEvent(EventName, EventData);
		}

		public TimeSpan Elapsed
		{
			get { return Timer.Elapsed; }
		}
	}
}
