// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

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

			public override bool Equals(object? obj) => obj is TestObject Other && Equals(Other);

			public bool Equals(TestObject? Other) => Other != null && A == Other.A && B == Other.B && C == Other.C && Enumerable.SequenceEqual(Children, Other.Children);

			public override int GetHashCode() => HashCode.Combine(A, B, C);
		}

		[TestMethod]
		public void RoundTrip()
		{
			TestObject Obj1 = CreateTestObject(new Random(123));
			CbObject CbObj1 = CbSerializer.Serialize(Obj1);
			ReadOnlyMemory<byte> Mem1 = CbObj1.GetView();

			TestObject Obj2 = CbSerializer.Deserialize<TestObject>(Mem1);
			CbObject CbObj2 = CbSerializer.Serialize(Obj2);
			ReadOnlyMemory<byte> Mem2 = CbObj1.GetView();

			Assert.AreEqual(Obj1, Obj2);
			Assert.AreEqual(Mem1, Mem2);
		}

		[TestMethod]
		public void StreamTest()
		{
			TestObject Obj1 = CreateTestObject(new Random(123));

			CbWriter Writer = new CbWriter();
			CbSerializer.Serialize(Writer, Obj1);

			byte[] Data1 = Writer.ToByteArray();
			byte[] Data2;
			using (Stream Stream = Writer.AsStream())
			{
				Data2 = new byte[Stream.Length];

				int ReadLength = Stream.Read(Data2, 0, Data2.Length);
				Assert.AreEqual(Data2.Length, ReadLength);

				ReadLength = Stream.Read(new byte[10], 0, 10);
				Assert.AreEqual(0, ReadLength);
			}
			Assert.IsTrue(Data1.AsSpan().SequenceEqual(Data2));

			TestObject Obj2 = CbSerializer.Deserialize<TestObject>(Data1);
			Assert.AreEqual(Obj1, Obj2);
		}

		static TestObject CreateTestObject(Random Random)
		{
			TestObject Obj = new TestObject();
			Obj.A = Random.Next();
			Obj.B = Random.NextInt64().ToString();
			Obj.C = IoHash.Compute(Encoding.UTF8.GetBytes(Obj.B));

			for (int Idx = 0; Idx < Random.Next(0, 3); Idx++)
			{
				Obj.Children.Add(CreateTestObject(Random));
			}

			return Obj;
		}
	}
}
