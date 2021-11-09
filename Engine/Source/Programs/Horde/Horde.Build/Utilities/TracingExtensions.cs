// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using EpicGames.Core;
using HordeServer.Models;
using HordeServer.Utilities;
using OpenTracing;

namespace Horde.Build.Utilities
{
	/// <summary>
	/// Extensions to handle Horde specific data types in the OpenTracing library
	/// </summary>
	public static class OpenTracingSpanExtensions
	{
		/// <summary>Set a key:value tag on the span</summary>
		/// <returns>This span instance, for chaining</returns>
		public static ISpan SetTag<T>(this ISpan Span, string Key, StringId<T>? Value)
		{
			if (Value != null) Span.SetTag(Key, Value.ToString());
			return Span;
		}
		
		/// <summary>Set a key:value tag on the span</summary>
		/// <returns>This span instance, for chaining</returns>
		public static ISpan SetTag<T>(this ISpan Span, string Key, StringId<T> Value)
		{
			Span.SetTag(Key, Value.ToString());
			return Span;
		}

		/// <summary>Set a key:value tag on the span</summary>
		/// <returns>This span instance, for chaining</returns>
		public static ISpan SetTag<T>(this ISpan Span, string Key, IEnumerable<StringId<T>>? Value)
		{
			if (Value != null) Span.SetTag(Key, string.Join(',', Value));
			return Span;
		}

		/// <summary>Set a key:value tag on the span</summary>
		/// <returns>This span instance, for chaining</returns>
		public static ISpan SetTag<T>(this ISpan Span, string Key, ObjectId<T>? Value)
		{
			if (Value != null) Span.SetTag(Key, Value.ToString());
			return Span;
		}
		
		/// <summary>Set a key:value tag on the span</summary>
		/// <returns>This span instance, for chaining</returns>
		public static ISpan SetTag<T>(this ISpan Span, string Key, ObjectId<T> Value)
		{
			Span.SetTag(Key, Value.ToString());
			return Span;
		}
		
		/// <summary>Set a key:value tag on the span</summary>
		/// <returns>This span instance, for chaining</returns>
		public static ISpan SetTag(this ISpan Span, string Key, ContentHash Value)
		{
			Span.SetTag(Key, Value.ToString());
			return Span;
		}
		
		/// <summary>Set a key:value tag on the span</summary>
		/// <returns>This span instance, for chaining</returns>
		public static ISpan SetTag(this ISpan Span, string Key, SubResourceId Value)
		{
			Span.SetTag(Key, Value.ToString());
			return Span;
		}
		
		/// <summary>Set a key:value tag on the span</summary>
		/// <returns>This span instance, for chaining</returns>
		public static ISpan SetTag<T>(this ISpan Span, string Key, IEnumerable<ObjectId<T>>? Value)
		{
			if (Value != null) Span.SetTag(Key, string.Join(',', Value));
			return Span;
		}
		
		/// <summary>Set a key:value tag on the span</summary>
		/// <returns>This span instance, for chaining</returns>
		public static ISpan SetTag(this ISpan Span, string Key, int? Value)
		{
			if (Value != null) Span.SetTag(Key, Value.Value);
			return Span;
		}
		
		/// <summary>Set a key:value tag on the span</summary>
		/// <returns>This span instance, for chaining</returns>
		public static ISpan SetTag(this ISpan Span, string Key, DateTimeOffset? Value)
		{
			if (Value != null) Span.SetTag(Key, Value.ToString());
			return Span;
		}
	}
}