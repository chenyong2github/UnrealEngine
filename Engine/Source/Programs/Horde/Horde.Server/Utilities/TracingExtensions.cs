// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using EpicGames.Core;
using Horde.Server.Jobs;
using Horde.Server.Streams;
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
	}
}