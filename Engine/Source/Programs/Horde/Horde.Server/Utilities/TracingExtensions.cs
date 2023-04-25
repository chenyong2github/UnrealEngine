// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.CompilerServices;
using EpicGames.Core;
using Horde.Server.Jobs;
using Horde.Server.Streams;
using MongoDB.Driver;
using OpenTelemetry.Trace;
using OpenTracing;

namespace Horde.Server.Utilities
{
	/// <summary>
	/// Extensions to handle Horde specific data types in the OpenTracing library
	/// </summary>
	public static class OpenTracingSpanExtensions
	{
		/// <summary>Set a key:value tag on the span</summary>
		/// <returns>This span instance, for chaining</returns>
		public static ISpan SetTag(this ISpan span, string key, ContentHash value)
		{
			span.SetTag(key, value.ToString());
			return span;
		}
		
		/// <summary>Set a key:value tag on the span</summary>
		/// <returns>This span instance, for chaining</returns>
		public static ISpan SetTag(this ISpan span, string key, SubResourceId value)
		{
			span.SetTag(key, value.ToString());
			return span;
		}
		
		/// <summary>Set a key:value tag on the span</summary>
		/// <returns>This span instance, for chaining</returns>
		public static ISpan SetTag(this ISpan span, string key, int? value)
		{
			if (value != null)
			{
				span.SetTag(key, value.Value);
			}
			return span;
		}
		
		/// <summary>Set a key:value tag on the span</summary>
		/// <returns>This span instance, for chaining</returns>
		public static ISpan SetTag(this ISpan span, string key, DateTimeOffset? value)
		{
			if (value != null)
			{
				span.SetTag(key, value.ToString());
			}
			return span;
		}
	}

	/// <summary>
	/// Static initialization of all available OpenTelemetry tracers
	/// </summary>
	public static class OpenTelemetryTracers
	{
		/// <summary>
		/// Name of default Horde tracer (aka activity source)
		/// </summary>
		public const string HordeName = "Horde";
		
		/// <summary>
		/// Name of MongoDB tracer (aka activity source)
		/// </summary>
		public const string MongoDbName = "MongoDB";

		/// <summary>
		/// List of all source names configured in this class.
		/// They are needed at startup when initializing OpenTelemetry
		/// </summary>
		public static string[] SourceNames => new[] { HordeName, MongoDbName };
		
		/// <summary>
		/// Default tracer used inside Horde
		/// </summary>
		public static readonly Tracer Horde = TracerProvider.Default.GetTracer(HordeName);
		
		/// <summary>
		/// Tracer specific to MongoDB
		/// </summary>
		public static readonly Tracer MongoDb = TracerProvider.Default.GetTracer(MongoDbName);
	}
	
	/// <summary>
	/// Extensions to handle Horde specific data types in the OpenTelemetry library
	/// </summary>
	public static class OpenTelemetrySpanExtensions
	{
		/// <summary>Set a key:value tag on the span</summary>
		/// <returns>This span instance, for chaining</returns>
		public static TelemetrySpan SetAttribute(this TelemetrySpan span, string key, ContentHash value)
		{
			span.SetAttribute(key, value.ToString());
			return span;
		}
		
		/// <summary>Set a key:value tag on the span</summary>
		/// <returns>This span instance, for chaining</returns>
		public static TelemetrySpan SetAttribute(this TelemetrySpan span, string key, SubResourceId value)
		{
			span.SetAttribute(key, value.ToString());
			return span;
		}
		
		/// <summary>Set a key:value tag on the span</summary>
		/// <returns>This span instance, for chaining</returns>
		public static TelemetrySpan SetAttribute(this TelemetrySpan span, string key, int? value)
		{
			if (value != null)
			{
				span.SetAttribute(key, value.Value);
			}
			return span;
		}
		
		/// <summary>Set a key:value tag on the span</summary>
		/// <returns>This span instance, for chaining</returns>
		public static TelemetrySpan SetAttribute(this TelemetrySpan span, string key, DateTimeOffset? value)
		{
			if (value != null)
			{
				span.SetAttribute(key, value.ToString());
			}
			return span;
		}
		
		/// <inheritdoc cref="ISpan.SetTag(System.String, System.String)"/>
		public static TelemetrySpan SetAttribute(this TelemetrySpan span, string key, StreamId? value) => span.SetAttribute(key, value?.ToString());
		
		/// <inheritdoc cref="ISpan.SetTag(System.String, System.String)"/>
		public static TelemetrySpan SetAttribute(this TelemetrySpan span, string key, TemplateId? value) => span.SetAttribute(key, value?.ToString());
		
		/// <inheritdoc cref="ISpan.SetTag(System.String, System.String)"/>
		public static TelemetrySpan SetAttribute(this TelemetrySpan span, string key, TemplateId[]? values) => span.SetAttribute(key, values != null ? String.Join(',', values.Select(x => x.Id.ToString())) : null);

		/// <summary>
		/// Start a MongoDB-based tracing span
		/// </summary>
		/// <param name="tracer">Current tracer being extended</param>
		/// <param name="spanName">Name of the span</param>
		/// <param name="collection">An optional MongoDB collection, the name will be used as an attribute</param>
		/// <returns>A new telemetry span</returns>
		[MethodImpl(MethodImplOptions.AggressiveInlining)]
		public static TelemetrySpan StartMongoDbSpan<T>(this Tracer tracer, string spanName, IMongoCollection<T>? collection = null)
		{
			string name = "mongodb." + spanName;
			TelemetrySpan span = OpenTelemetryTracers.MongoDb
				.StartActiveSpan(name, parentContext: Tracer.CurrentSpan.Context)
				.SetAttribute("type", "db")
				.SetAttribute("operation.name", name)
				.SetAttribute("service.name", OpenTelemetryTracers.MongoDbName);

			if (collection != null)
			{
				span.SetAttribute("collection", collection.CollectionNamespace.CollectionName);
			}
			
			return span;
		}
	}
}