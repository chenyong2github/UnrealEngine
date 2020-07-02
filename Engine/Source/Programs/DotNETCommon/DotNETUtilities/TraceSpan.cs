// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;

namespace Tools.DotNETCommon
{
	/// <summary>
	/// Interface for a trace span
	/// </summary>
	public interface ITraceSpan : IDisposable
	{
		/// <summary>
		/// Adds additional metadata to this scope
		/// </summary>
		/// <param name="Name">Name of the key</param>
		/// <param name="Value">Value for this metadata</param>
		void AddMetadata(string Name, string Value);
	}

	/// <summary>
	/// Methods for creating ITraceScope instances
	/// </summary>
	public static class TraceSpan
	{
		/// <summary>
		/// Concrete implementation of ITraceScope
		/// </summary>
		class TraceSpanImpl : ITraceSpan
		{
			public string Name;
			public string Resource;
			public string Service;
			public DateTimeOffset StartTime;
			public DateTimeOffset? FinishTime;
			public Dictionary<string, string> Metadata = new Dictionary<string, string>();

			public TraceSpanImpl(string Name, string Resource, string Service)
			{
				this.Name = Name;
				this.Resource = Resource;
				this.Service = Service;
				this.StartTime = DateTimeOffset.Now;
			}

			public void AddMetadata(string Name, string Value)
			{
				Metadata[Name] = Value;
			}

			public void Dispose()
			{
				if(FinishTime == null)
				{
					FinishTime = DateTimeOffset.Now;
				}
			}
		}

		/// <summary>
		/// The current scope provider
		/// </summary>
		static List<TraceSpanImpl> Spans = new List<TraceSpanImpl>();

		/// <summary>
		/// Creates a scope using the current provider
		/// </summary>
		public static ITraceSpan Create(string Name, string Resource = null, string Service = null)
		{
			TraceSpanImpl Span = new TraceSpanImpl(Name, Resource, Service);
			Spans.Add(Span);
			return Span;
		}

		/// <summary>
		/// Saves all the scope information to a file
		/// </summary>
		public static void Flush()
		{
			string TelemetryDir = Environment.GetEnvironmentVariable("UE_TELEMETRY_DIR");
			if (TelemetryDir != null)
			{
				FileReference File;
				using (Process Process = Process.GetCurrentProcess())
				{
					DirectoryReference TelemetryDirRef = new DirectoryReference(TelemetryDir);
					DirectoryReference.CreateDirectory(TelemetryDirRef);

					string FileName = String.Format("{0}.{1}.json", Path.GetFileName(Assembly.GetEntryAssembly().Location), Process.Id, Process.StartTime.Ticks);
					File = FileReference.Combine(TelemetryDirRef, FileName);
				}

				using (JsonWriter Writer = new JsonWriter(File))
				{
					Writer.WriteObjectStart();
					Writer.WriteArrayStart("Spans");
					foreach (TraceSpanImpl Span in Spans)
					{
						if (Span.FinishTime != null)
						{
							Writer.WriteObjectStart();
							Writer.WriteValue("Name", Span.Name);
							if (Span.Resource != null)
							{
								Writer.WriteValue("Resource", Span.Resource);
							}
							if (Span.Service != null)
							{
								Writer.WriteValue("Service", Span.Service);
							}
							Writer.WriteValue("StartTime", Span.StartTime.ToString("o", CultureInfo.InvariantCulture));
							Writer.WriteValue("FinishTime", Span.FinishTime.Value.ToString("o", CultureInfo.InvariantCulture));
							Writer.WriteObjectStart("Metadata");
							foreach (KeyValuePair<string, string> Pair in Span.Metadata)
							{
								Writer.WriteValue(Pair.Key, Pair.Value);
							}
							Writer.WriteObjectEnd();
							Writer.WriteObjectEnd();
						}
					}
					Writer.WriteArrayEnd();
					Writer.WriteObjectEnd();
				}
			}
		}
	}
}
