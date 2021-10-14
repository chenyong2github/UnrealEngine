// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading;
using OpenTracing;
using OpenTracing.Mock;
using OpenTracing.Propagation;
using OpenTracing.Tag;
using OpenTracing.Util;

namespace EpicGames.Core
{
	public class JsonTracerSpanContext : ISpanContext
	{
		public IEnumerable<KeyValuePair<string, string>> GetBaggageItems()
		{
			throw new NotImplementedException();
		}

		public string TraceId { get; }
		public string SpanId { get; }

		public JsonTracerSpanContext(string TraceId, string SpanId)
		{
			this.TraceId = TraceId;
			this.SpanId = SpanId;
		}
	}

	public class JsonTracerSpan : ISpan
	{
		/// <summary>
		/// Used to monotonically update ids
		/// </summary>
		private static long _nextIdCounter = 0;

		/// <summary>
		/// A simple-as-possible (consecutive for repeatability) id generator.
		/// </summary>
		private static string NextId()
		{
			return Interlocked.Increment(ref _nextIdCounter).ToString(CultureInfo.InvariantCulture);
		}
		
		public JsonTracerSpanContext Context
		{
			// C# doesn't have "return type covariance" so we use the trick with the explicit interface implementation
			// and this separate property.
			get
			{
				return JsonTracerContext;
			}
		}
		ISpanContext ISpan.Context => Context;
		
		public string OperationName { get; private set; }
		
		private readonly JsonTracer Tracer;
		private JsonTracerSpanContext JsonTracerContext;
		private DateTimeOffset _FinishTimestamp;
		private bool Finished;
		private readonly Dictionary<string, object> _Tags;
		private readonly List<JsonTracerSpan.Reference> References;
		
		public DateTimeOffset StartTimestamp { get; }
		public Dictionary<string, object> Tags => new Dictionary<string, object>(_Tags);
		
		/// <summary>
		/// The finish time of the span; only valid after a call to <see cref="Finish()"/>.
		/// </summary>
		public DateTimeOffset FinishTimestamp
		{
			get
			{
				if (_FinishTimestamp == DateTimeOffset.MinValue)
					throw new InvalidOperationException("Must call Finish() before FinishTimestamp");

				return _FinishTimestamp;
			}
		}

		/// <summary>
		/// The spanId of the span's first <see cref="OpenTracing.References.ChildOf"/> reference, or the first reference of any type,
		/// or null if no reference exists.
		/// </summary>
		/// <seealso cref="MockSpanContext.SpanId"/>
		/// <seealso cref="MockSpan.References"/>
		public string? ParentId { get; }

		private readonly object Lock = new object();

		public JsonTracerSpan(JsonTracer JsonTracer, string OperationName, DateTimeOffset StartTimestamp, Dictionary<string, object>? InitialTags, List<Reference>? References)
		{
			this.Tracer = JsonTracer;
			this.OperationName = OperationName;
			this.StartTimestamp = StartTimestamp;
			
			_Tags = InitialTags == null
				? new Dictionary<string, object>()
				: new Dictionary<string, object>(InitialTags);

			this.References = References == null
				? new List<Reference>()
				: References.ToList();
			
			JsonTracerSpanContext? ParentContext = FindPreferredParentRef(this.References);
			if (ParentContext == null)
			{
				// we are a root span
				JsonTracerContext = new JsonTracerSpanContext(NextId(), NextId());
				ParentId = null;
			}
			else
			{
				// we are a child span
				JsonTracerContext = new JsonTracerSpanContext(ParentContext.TraceId, NextId());
				ParentId = ParentContext.SpanId;
			}
		}

		public ISpan SetTag(string Key, string Value) { return SetObjectTag(Key, Value); }
		public ISpan SetTag(string Key, bool Value) { return SetObjectTag(Key, Value); }
		public ISpan SetTag(string Key, int Value) { return SetObjectTag(Key, Value); }
		public ISpan SetTag(string Key, double Value) { return SetObjectTag(Key, Value); }
		public ISpan SetTag(BooleanTag Tag, bool Value) { SetObjectTag(Tag.Key, Value); return this; }
		public ISpan SetTag(IntOrStringTag Tag, string Value) { SetObjectTag(Tag.Key, Value); return this; }
		public ISpan SetTag(IntTag Tag, int Value) { SetObjectTag(Tag.Key, Value); return this; }
		public ISpan SetTag(StringTag Tag, string Value) { SetObjectTag(Tag.Key, Value); return this; }

		private ISpan SetObjectTag(string Key, object Value)
		{
			lock (Lock)
			{
				CheckForFinished("Setting tag [{0}:{1}] on already finished span", Key, Value);
				_Tags[Key] = Value;
				return this;
			}
		}

		public ISpan Log(IEnumerable<KeyValuePair<string, object>> fields) { throw new NotSupportedException("Log() calls are not supported in JsonTracerSpans"); }
		public ISpan Log(DateTimeOffset timestamp, IEnumerable<KeyValuePair<string, object>> fields) { throw new NotSupportedException("Log() calls are not supported in JsonTracerSpans"); }
		public ISpan Log(string @event) { throw new NotSupportedException("Log() calls are not supported in JsonTracerSpans"); }
		public ISpan Log(DateTimeOffset timestamp, string @event) { throw new NotSupportedException("Log() calls are not supported in JsonTracerSpans"); }

		public ISpan SetBaggageItem(string key, string value) { throw new NotImplementedException(); }
		public string GetBaggageItem(string key) { throw new NotImplementedException(); }

		public ISpan SetOperationName(string OperationName)
		{
			CheckForFinished("Setting operationName [{0}] on already finished span", OperationName);
			this.OperationName = OperationName;
			return this;
		}

		public void Finish()
		{
			Finish(DateTimeOffset.UtcNow);
		}

		public void Finish(DateTimeOffset FinishTimestamp)
		{
			lock (Lock)
			{
				CheckForFinished("Tried to finish already finished span");
				_FinishTimestamp = FinishTimestamp;
				Tracer.AppendFinishedSpan(this);
				Finished = true;
			}
		}
		
		private static JsonTracerSpanContext? FindPreferredParentRef(IList<Reference> References)
		{
			if (!References.Any())
				return null;

			// return the context of the parent, if applicable
			foreach (var Reference in References)
			{
				if (OpenTracing.References.ChildOf.Equals(Reference.ReferenceType))
					return Reference.Context;
			}

			// otherwise, return the context of the first reference
			return References.First().Context;
		}
		
		private void CheckForFinished(string Format, params object[] Args)
		{
			if (Finished)
			{
				throw new InvalidOperationException(string.Format(Format, Args));
			}
		}
		
		public sealed class Reference : IEquatable<Reference>
		{
			public JsonTracerSpanContext Context { get; }

			/// <summary>
			/// See <see cref="OpenTracing.References"/>.
			/// </summary>
			public string ReferenceType { get; }

			public Reference(JsonTracerSpanContext Context, string ReferenceType)
			{
				this.Context = Context ?? throw new ArgumentNullException(nameof(Context));
				this.ReferenceType = ReferenceType ?? throw new ArgumentNullException(nameof(ReferenceType));
			}

			public override bool Equals(object? Obj)
			{
				return Equals(Obj as Reference);
			}
			
			public bool Equals(Reference? Other)
			{
				return Other != null &&
				       EqualityComparer<JsonTracerSpanContext>.Default.Equals(Context, Other.Context) &&
				       ReferenceType == Other.ReferenceType;
			}
			
			public override int GetHashCode()
			{
				int HashCode = 2083322454;
				HashCode = HashCode * -1521134295 + EqualityComparer<JsonTracerSpanContext>.Default.GetHashCode(Context);
				HashCode = HashCode * -1521134295 + EqualityComparer<string>.Default.GetHashCode(ReferenceType);
				return HashCode;
			}
		}
	}

	public class JsonTracerSpanBuilder : ISpanBuilder
	{
		private readonly JsonTracer Tracer;
		private readonly string OperationName;
		private DateTimeOffset StartTimestamp = DateTimeOffset.MinValue;
		private readonly List<JsonTracerSpan.Reference> References = new List<JsonTracerSpan.Reference>();
		private readonly Dictionary<string, object> Tags = new Dictionary<string, object>();
		private bool _IgnoreActiveSpan;
		
		public JsonTracerSpanBuilder(JsonTracer Tracer, string OperationName)
		{
			this.Tracer = Tracer;
			this.OperationName = OperationName;
		}
		
		public ISpanBuilder AsChildOf(ISpanContext? Parent)
		{
			if (Parent == null)
				return this;

			return AddReference(OpenTracing.References.ChildOf, Parent);
		}

		public ISpanBuilder AsChildOf(ISpan? Parent)
		{
			if (Parent == null)
				return this;

			return AddReference(OpenTracing.References.ChildOf, Parent.Context);
		}

		public ISpanBuilder AddReference(string ReferenceType, ISpanContext? ReferencedContext)
		{
			if (ReferencedContext != null)
			{
				References.Add(new JsonTracerSpan.Reference((JsonTracerSpanContext)ReferencedContext, ReferenceType));
			}

			return this;
		}

		public ISpanBuilder IgnoreActiveSpan()
		{
			_IgnoreActiveSpan = true;
			return this;
		}

		public ISpanBuilder WithTag(string Key, string Value) { Tags[Key] = Value; return this; }
		public ISpanBuilder WithTag(string Key, bool Value) { Tags[Key] = Value; return this; }
		public ISpanBuilder WithTag(string Key, int Value) { Tags[Key] = Value; return this; }
		public ISpanBuilder WithTag(string Key, double Value) { Tags[Key] = Value; return this; }
		public ISpanBuilder WithTag(BooleanTag Tag, bool Value) { Tags[Tag.Key] = Value; return this; }
		public ISpanBuilder WithTag(IntOrStringTag Tag, string Value) { Tags[Tag.Key] = Value; return this; }
		public ISpanBuilder WithTag(IntTag Tag, int Value) { Tags[Tag.Key] = Value; return this; }
		public ISpanBuilder WithTag(StringTag Tag, string Value) { Tags[Tag.Key] = Value; return this; }

		public ISpanBuilder WithStartTimestamp(DateTimeOffset Timestamp)
		{
			StartTimestamp = Timestamp;
			return this;
		}

		public IScope StartActive()
		{
			return StartActive(true);
		}

		public IScope StartActive(bool FinishSpanOnDispose)
		{
			ISpan Span = Start();
			return Tracer.ScopeManager.Activate(Span, FinishSpanOnDispose);
		}

		public ISpan Start()
		{
			if (StartTimestamp == DateTimeOffset.MinValue) // value was not set by builder
			{
				StartTimestamp = DateTimeOffset.UtcNow;
			}

			ISpanContext? ActiveSpanContext = Tracer.ActiveSpan?.Context;
			if (!References.Any() && !_IgnoreActiveSpan && ActiveSpanContext != null)
			{
				References.Add(new JsonTracerSpan.Reference((JsonTracerSpanContext)ActiveSpanContext, OpenTracing.References.ChildOf));
			}

			return new JsonTracerSpan(Tracer, OperationName, StartTimestamp, Tags, References);
		}
	}
	
	public class JsonTracer : ITracer
	{
		public IScopeManager ScopeManager { get; }
		public ISpan? ActiveSpan => ScopeManager.Active?.Span;

		private readonly object Lock = new object();
		private readonly List<JsonTracerSpan> FinishedSpans = new List<JsonTracerSpan>();
		private readonly DirectoryReference? TelemetryDir;
		
		public JsonTracer(DirectoryReference? TelemetryDir = null)
		{
			ScopeManager = new AsyncLocalScopeManager();
			this.TelemetryDir = TelemetryDir;
		}

		public static JsonTracer? TryRegisterAsGlobalTracer()
		{
			string? TelemetryDir = Environment.GetEnvironmentVariable("UE_TELEMETRY_DIR");
			if (TelemetryDir != null)
			{
				JsonTracer Tracer = new JsonTracer(new DirectoryReference(TelemetryDir));
				return GlobalTracer.RegisterIfAbsent(Tracer) ? Tracer : null;
			}

			return null;
		}

		public ISpanBuilder BuildSpan(string OperationName)
		{
			return new JsonTracerSpanBuilder(this, OperationName);
		}

		public void Inject<TCarrier>(ISpanContext SpanContext, IFormat<TCarrier> Format, TCarrier Carrier)
		{
			throw new NotSupportedException(string.Format("Tracer.Inject is not implemented for {0} by JsonTracer", Format));
		}

		public ISpanContext Extract<TCarrier>(IFormat<TCarrier> Format, TCarrier Carrier)
		{
			throw new NotSupportedException(string.Format("Tracer.Extract is not implemented for {0} by JsonTracer", Format));
		}

		public void Flush()
		{
			if (TelemetryDir != null)
			{
				string TelemetryScopeId = Environment.GetEnvironmentVariable("UE_TELEMETRY_SCOPE_ID") ?? "noscope";
				
				FileReference File;
				using (System.Diagnostics.Process Process = System.Diagnostics.Process.GetCurrentProcess())
				{
					DirectoryReference.CreateDirectory(TelemetryDir);

					string FileName = String.Format("{0}.{1}.{2}.opentracing.json", Path.GetFileName(Assembly.GetEntryAssembly()!.Location), TelemetryScopeId, Process.Id);
					File = FileReference.Combine(TelemetryDir, FileName);
				}

				using (JsonWriter Writer = new JsonWriter(File))
				{
					GetFinishedSpansAsJson(Writer);
				}
			}
		}
		
		public List<JsonTracerSpan> GetFinishedSpans()
		{
			lock (Lock)
			{
				return new List<JsonTracerSpan>(FinishedSpans);
			}
		}

		public void GetFinishedSpansAsJson(JsonWriter Writer)
		{
			Writer.WriteObjectStart();
			Writer.WriteArrayStart("Spans");
			foreach (JsonTracerSpan Span in GetFinishedSpans())
			{
				Writer.WriteObjectStart();
				Writer.WriteValue("Name", Span.OperationName);
				Dictionary<string, object> Tags = Span.Tags;
				if (Tags.TryGetValue("Resource", out object? Resource) && Resource is string ResourceString)
				{
					Writer.WriteValue("Resource", ResourceString);
				}
				if (Tags.TryGetValue("Service", out object? Service) && Service is string ServiceString)
				{
					Writer.WriteValue("Service", ServiceString);
				}
				Writer.WriteValue("StartTime", Span.StartTimestamp.ToString("o", CultureInfo.InvariantCulture));
				Writer.WriteValue("FinishTime", Span.FinishTimestamp.ToString("o", CultureInfo.InvariantCulture));
				Writer.WriteObjectStart("Metadata");
				// TODO: Write tags as metadata?
				Writer.WriteObjectEnd();
				Writer.WriteObjectEnd();
			}
			Writer.WriteArrayEnd();
			Writer.WriteObjectEnd();
		}
		
		internal void AppendFinishedSpan(JsonTracerSpan JsonTracerSpan)
		{
			lock (Lock)
			{
				FinishedSpans.Add(JsonTracerSpan);
			}
		}
	}
}