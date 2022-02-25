// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using EpicGames.Core;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace EpicGames.Serialization.Tests
{
	[TestClass]
	public class VarIntTests
	{
		[TestMethod]
		public void TestVarInt()
		{
			byte[] Buffer = new byte[20];

			int Length = VarInt.Write(Buffer, -1);
			Assert.AreEqual(9, Length);
			Assert.AreEqual(9, VarInt.Measure(-1));

			Assert.AreEqual(9, VarInt.Measure(Buffer));
			int Value = (int)(long)VarInt.Read(Buffer, out int BytesRead);
			Assert.AreEqual(9, BytesRead);

			Assert.AreEqual(-1, Value);
		}
	}
}
