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

namespace EpicGames.Core
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
	/// Sink for tracing information
	/// </summary>
	public interface ITraceSink
	{
		/// <summary>
		/// Create a new trace span
		/// </summary>
		/// <param name="Operation">Name of the operation</param>
		/// <param name="Resource">Resource that the operation is being performed on</param>
		/// <param name="Service">Name of the service</param>
		/// <returns>New span instance</returns>
		ITraceSpan Create(string Operation, string? Resource, string? Service);

		/// <summary>
		/// Flush all the current trace data
		/// </summary>
		void Flush();
	}

	/// <summary>
	/// Default implementation of ITraceSink. Writes trace information to a JSON file on application exit if the UE_TELEMETRY_DIR environment variable is set.
	/// </summary>
	public class JsonTraceSink : ITraceSink
	{
		class TraceSpanImpl : ITraceSpan
		{
			public string Name;
			public string? Resource;
			public string? Service;
			public DateTimeOffset StartTime;
			public DateTimeOffset? FinishTime;
			public Dictionary<string, string> Metadata = new Dictionary<string, string>();

			public TraceSpanImpl(string Name, string? Resource, string? Service)
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
				if (FinishTime == null)
				{
					FinishTime = DateTimeOffset.Now;
				}
			}
		}

		/// <summary>
		/// Output directory for telemetry info
		/// </summary>
		DirectoryReference TelemetryDir;

		/// <summary>
		/// The current scope provider
		/// </summary>
		List<TraceSpanImpl> Spans = new List<TraceSpanImpl>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="TelemetryDir">Directory to store telemetry files</param>
		public JsonTraceSink(DirectoryReference TelemetryDir)
		{
			this.TelemetryDir = TelemetryDir;
		}

		/// <summary>
		/// Creates a scope using the current provider
		/// </summary>
		public ITraceSpan Create(string Name, string? Resource = null, string? Service = null)
		{
			TraceSpanImpl Span = new TraceSpanImpl(Name, Resource, Service);
			Spans.Add(Span);
			return Span;
		}

		/// <summary>
		/// Saves all the scope information to a file
		/// </summary>
		public void Flush()
		{
			FileReference File;
			using (Process Process = Process.GetCurrentProcess())
			{
				DirectoryReference.CreateDirectory(TelemetryDir);

				string FileName = String.Format("{0}.{1}.json", Path.GetFileName(Assembly.GetEntryAssembly()!.Location), Process.Id, Process.StartTime.Ticks);
				File = FileReference.Combine(TelemetryDir, FileName);
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

	/// <summary>
	/// Methods for creating ITraceScope instances
	/// </summary>
	public static class TraceSpan
	{
		class CombinedTraceSpan : ITraceSpan
		{
			ITraceSpan[] Spans;

			public CombinedTraceSpan(ITraceSpan[] Spans)
			{
				this.Spans = Spans;
			}

			public void AddMetadata(string Name, string Value)
			{
				foreach(ITraceSpan Span in Spans)
				{
					Span.AddMetadata(Name, Value);
				}
			}

			public void Dispose()
			{
				foreach (ITraceSpan Span in Spans)
				{
					Span.Dispose();
				}
			}
		}

		/// <summary>
		/// The sinks to use
		/// </summary>
		static List<ITraceSink> Sinks = GetDefaultSinks();

		/// <summary>
		/// Build a list of default sinks
		/// </summary>
		/// <returns></returns>
		static List<ITraceSink> GetDefaultSinks()
		{
			List<ITraceSink> Sinks = new List<ITraceSink>();

			string? TelemetryDir = Environment.GetEnvironmentVariable("UE_TELEMETRY_DIR");
			if (TelemetryDir != null)
			{
				Sinks.Add(new JsonTraceSink(new DirectoryReference(TelemetryDir)));
			}

			return Sinks;
		}

		/// <summary>
		/// Adds a new sink
		/// </summary>
		/// <param name="Sink">The sink to add</param>
		public static void AddSink(ITraceSink Sink)
		{
			Sinks.Add(Sink);
		}

		/// <summary>
		/// Remove a sink from the current list
		/// </summary>
		/// <param name="Sink">The sink to remove</param>
		public static void RemoveSink(ITraceSink Sink)
		{
			Sinks.Remove(Sink);
		}

		/// <summary>
		/// Creates a scope using the current provider
		/// </summary>
		public static ITraceSpan Create(string Operation, string? Resource = null, string? Service = null)
		{
			if (Sinks.Count == 0)
			{
				return new CombinedTraceSpan(Array.Empty<ITraceSpan>());
			}
			else if (Sinks.Count == 1)
			{
				return Sinks[0].Create(Operation, Resource, Service);
			}
			else
			{
				return new CombinedTraceSpan(Sinks.ConvertAll(x => x.Create(Operation, Resource, Service)).ToArray());
			}
		}

		/// <summary>
		/// Saves all the scope information to a file
		/// </summary>
		public static void Flush()
		{
			foreach (ITraceSink Sink in Sinks)
			{
				Sink.Flush();
			}
		}
	}
}
