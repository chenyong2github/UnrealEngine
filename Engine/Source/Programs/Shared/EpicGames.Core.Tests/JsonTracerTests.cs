// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.VisualStudio.TestTools.UnitTesting;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using OpenTracing;
using System.Threading.Tasks;
using System.Diagnostics;

namespace EpicGames.Core.Tests
{
	[TestClass]
	public class JsonTracerTests
	{
		[TestMethod]
		public void Run()
		{
			JsonTracer Tracer = new JsonTracer();

			using (IScope Scope1 = Tracer.BuildSpan("bogusOp1").StartActive()) { }
			using (IScope Scope2 = Tracer.BuildSpan("bogusOp2").StartActive()) { }
			
			Assert.AreEqual(2, Tracer.GetFinishedSpans().Count);
		}
		
		[TestMethod]
		public void SpansWithTags()
		{
			JsonTracer Tracer = new JsonTracer();

			using (IScope Scope1 = Tracer.BuildSpan("bogusOp1").WithTag("age", 94).StartActive()) { }
			using (IScope Scope2 = Tracer.BuildSpan("bogusOp1").WithTag("foo", "bar").StartActive()) { }

			List<JsonTracerSpan> Spans = Tracer.GetFinishedSpans();
			
			Assert.AreEqual(2, Spans.Count);
			Assert.AreEqual("bogusOp1", Spans[0].OperationName);
		}

		[TestMethod]
		public void SerializeToJson()
		{
			JsonTracer Tracer = new JsonTracer();

			using (IScope Scope1 = Tracer.BuildSpan("bogusOp1").WithTag("age", 94).WithTag("foo", "bar").StartActive()) { }
			using (IScope Scope2 = Tracer.BuildSpan("bogusOp2").WithTag("Service", "myService").WithTag("Resource", "myResource").StartActive()) { }

			using StringWriter Sw = new StringWriter();
			using JsonWriter Jw = new JsonWriter(Sw);
			Tracer.GetFinishedSpansAsJson(Jw);
			byte[] Data = Encoding.ASCII.GetBytes(Sw.ToString());

			using (JsonDocument Document = JsonDocument.Parse(Data.AsMemory()))
			{
				JsonElement Root = Document.RootElement;
				JsonElement SpansElement = Root.GetProperty("Spans");
				List<JsonElement> SpansList = SpansElement.EnumerateArray().ToList();
				Console.WriteLine(SpansList[0]);
				Assert.AreEqual("bogusOp1", SpansList[0].GetProperty("Name").GetString());
				Assert.IsNotNull(SpansList[0].GetProperty("StartTime").GetDateTime());
				Assert.IsNotNull(SpansList[0].GetProperty("FinishTime").GetDateTime());
				Assert.AreEqual("myService", SpansList[1].GetProperty("Service").GetString());
				Assert.AreEqual("myResource", SpansList[1].GetProperty("Resource").GetString());
			}
		}

		[TestMethod]
		public void SpansWithParallelFor()
		{
			JsonTracer Tracer = new JsonTracer();

			using (IScope ParentScope1 = Tracer.BuildSpan("Parent").StartActive()) 
			{
				Parallel.For(0, 100, Index =>
				{
					using (IScope ChildScope = Tracer.BuildSpan("Child").StartActive()) { }
				});
			}
		}

		private static async Task<bool> AsyncSpanTest(JsonTracer Tracer)
		{
			for (int Index = 0; Index < 100; Index++)
			{
				using (IScope AsyncChildScope = Tracer.BuildSpan("AsyncChild").StartActive())
				{
					await Task.Run(() => 
					{
						using (IScope TaskChildScope = Tracer.BuildSpan("TaskChild").StartActive())
						{
							return true;
						}
					});
				}
			}
			return true;
		}

		[TestMethod]
		public void SpansWithAsync()
		{
			JsonTracer Tracer = new JsonTracer();

			using (IScope ParentScope1 = Tracer.BuildSpan("Parent").StartActive())
			{
				 AsyncSpanTest(Tracer).Wait();
			}
		}
	}
}
