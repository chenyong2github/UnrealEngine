// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace EpicGames.Serialization.Tests
{
	[TestClass]
	public class VarIntTests
	{
		[TestMethod]
		public void TestVarInt()
		{
			byte[] buffer = new byte[20];

			int length = VarInt.Write(buffer, -1);
			Assert.AreEqual(9, length);
			Assert.AreEqual(9, VarInt.Measure(-1));

			Assert.AreEqual(9, VarInt.Measure(buffer));
			int value = (int)(long)VarInt.Read(buffer, out int bytesRead);
			Assert.AreEqual(9, bytesRead);

			Assert.AreEqual(-1, value);
		}
	}
}
