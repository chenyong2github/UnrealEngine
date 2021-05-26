// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using EpicGames.Core;
using HordeAgent;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace HordeAgentTests
{
	[TestClass]
	public class ActionExecutorTest
	{
		private readonly string TempDir;

		public ActionExecutorTest()
		{
			TempDir = GetTemporaryDirectory();
		}

		[TestMethod]
		public void TestPaths()
		{
			string SubDirPath = Path.Join(TempDir, "1");
			string SubSubDirPath = Path.Join(SubDirPath, "2");
			string SubSubSubDirPath = Path.Join(SubSubDirPath, "3");
			Directory.CreateDirectory(SubSubSubDirPath);
			File.WriteAllText(Path.Join(TempDir, "foo"), "a");
			File.WriteAllText(Path.Join(SubDirPath, "bar"), "b");
			File.WriteAllText(Path.Join(SubSubDirPath, "baz"), "b");
			File.WriteAllText(Path.Join(SubSubDirPath, "qux"), "c");
			File.WriteAllText(Path.Join(SubSubSubDirPath, "waldo"), "d");

			string[] OutputPaths =
			{
				"foo",
				"1/bar",
				"1/2", // directory
				"doesNotExist",
				//"foo", // duplicated (to be fixed)
			};

			DirectoryReference SandboxDir = new DirectoryReference(TempDir);
			var Resolved = ActionExecutor.ResolveOutputPaths(SandboxDir, OutputPaths);
			Assert.AreEqual(Path.Join(TempDir, "foo"), Resolved[0].FileRef.FullName);
			Assert.AreEqual("foo", Resolved[0].RelativePath);
			
			Assert.AreEqual(Path.Join(TempDir, "1", "bar"), Resolved[1].FileRef.FullName);
			Assert.AreEqual("1/bar", Resolved[1].RelativePath);
			Assert.AreEqual(Path.Join(TempDir, "1", "2", "baz"), Resolved[2].FileRef.FullName);
			Assert.AreEqual("1/2/baz", Resolved[2].RelativePath);
			Assert.AreEqual(Path.Join(TempDir, "1", "2", "qux"), Resolved[3].FileRef.FullName);
			Assert.AreEqual("1/2/qux", Resolved[3].RelativePath);
			Assert.AreEqual(Path.Join(TempDir, "1", "2", "3", "waldo"), Resolved[4].FileRef.FullName);
			Assert.AreEqual("1/2/3/waldo", Resolved[4].RelativePath);
		}

		[TestCleanup]
		public void Cleanup()
		{
			Directory.Delete(TempDir, true);
		}
		
		private static string GetTemporaryDirectory()
		{
			string TempDir = Path.Join(Path.GetTempPath(), "horde-" + Path.GetRandomFileName());
			if (Directory.Exists(TempDir))
			{
				Directory.Delete(TempDir, true);
			}

			Directory.CreateDirectory(TempDir);

			return TempDir;
		}
	}
}