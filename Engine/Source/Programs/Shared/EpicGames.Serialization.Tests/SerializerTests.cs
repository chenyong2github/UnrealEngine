// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace EpicGames.Serialization.Tests
{
	[TestClass]
	public class SerializerTests
	{
		class TestObject : IEquatable<TestObject>
		{
			[CbField]
			public int A { get; set; }

			[CbField]
			public string B { get; set; } = String.Empty;

			[CbField]
			public IoHash C { get; set; }

			[CbField]
			public List<TestObject> Children { get; set; } = new List<TestObject>();

			public override bool Equals(object? obj) => obj is TestObject other && Equals(other);

			public bool Equals(TestObject? other) => other != null && A == other.A && B == other.B && C == other.C && Enumerable.SequenceEqual(Children, other.Children);

			public override int GetHashCode() => HashCode.Combine(A, B, C);
		}

		[TestMethod]
		public void RoundTrip()
		{
			TestObject obj1 = CreateTestObject(new Random(123));
			CbObject cbObj1 = CbSerializer.Serialize(obj1);
			ReadOnlyMemory<byte> mem1 = cbObj1.GetView();

			TestObject obj2 = CbSerializer.Deserialize<TestObject>(mem1);
			CbObject cbObj2 = CbSerializer.Serialize(obj2);
			ReadOnlyMemory<byte> mem2 = cbObj2.GetView();

			Assert.AreEqual(obj1, obj2);
			Assert.IsTrue(mem1.Span.SequenceEqual(mem2.Span));
		}

		[TestMethod]
		public void StreamTest()
		{
			TestObject obj1 = CreateTestObject(new Random(123));

			CbWriter writer = new CbWriter();
			CbSerializer.Serialize(writer, obj1);

			byte[] data1 = writer.ToByteArray();
			byte[] data2;
			using (Stream stream = writer.AsStream())
			{
				data2 = new byte[stream.Length];

				int readLength = stream.Read(data2, 0, data2.Length);
				Assert.AreEqual(data2.Length, readLength);

				readLength = stream.Read(new byte[10], 0, 10);
				Assert.AreEqual(0, readLength);
			}
			Assert.IsTrue(data1.AsSpan().SequenceEqual(data2));

			TestObject obj2 = CbSerializer.Deserialize<TestObject>(data1);
			Assert.AreEqual(obj1, obj2);
		}

		static TestObject CreateTestObject(Random random)
		{
			TestObject obj = new TestObject();
			obj.A = random.Next();
			obj.B = random.NextInt64().ToString();
			obj.C = IoHash.Compute(Encoding.UTF8.GetBytes(obj.B));

			for (int idx = 0; idx < random.Next(0, 3); idx++)
			{
				obj.Children.Add(CreateTestObject(random));
			}

			return obj;
		}
	}
}
