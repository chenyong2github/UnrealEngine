// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading;
using EpicGames.Core;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using UnrealBuildTool.Artifacts;

#nullable enable

namespace UnrealBuildToolTests
{

	[TestClass]
	public class ArtifactsTest
	{
		static readonly Utf8String s_input1Data = new Utf8String("This is some sample input");
		static readonly Utf8String s_input2Data = new Utf8String("This is some more sample input");
		static readonly Utf8String s_output1Data = new Utf8String("This is the sample output");

		private static Artifact MakeInput1()
		{
			return new Artifact(ArtifactDirectoryTree.Absolute, "Input1", IoHash.Compute(s_input1Data.Span));
		}

		private static Artifact MakeInput2()
		{
			return new Artifact(ArtifactDirectoryTree.Absolute, "Input2", IoHash.Compute(s_input2Data.Span));
		}

		private static Artifact MakeOutput1()
		{
			return new Artifact(ArtifactDirectoryTree.Absolute, "Output1", IoHash.Compute(s_output1Data.Span));
		}

		public static ArtifactMapping MakeBundle1()
		{
			return new ArtifactMapping(
				IoHash.Compute(new Utf8String("SampleKey")),
				IoHash.Compute(new Utf8String("SampleBundleKey")),
				new Artifact[] { MakeInput1(), MakeInput2() },
				new Artifact[] { MakeOutput1() }
			);
		}

		public static ArtifactMapping MakeBundle2()
		{
			return new ArtifactMapping(
				IoHash.Compute(new Utf8String("SampleKey")),
				IoHash.Compute(new Utf8String("SampleBundleKeyV2")),
				new Artifact[] { MakeInput1(), MakeInput2() },
				new Artifact[] { MakeOutput1() }
			);
		}

		[TestMethod]
		public void ArtifactBundleStorageTest1()
		{
			CancellationToken cancellationToken = default;
			
			IArtifactCache cache = HordeStorageArtifactCache.CreateMemoryCache(NullLogger.Instance);

			_ = cache.WaitForReadyAsync().Result;
			Assert.AreEqual(ArtifactCacheState.Available, cache.State);

			ArtifactMapping bundle1 = MakeBundle1();
			cache.SaveArtifactMappingsAsync(new ArtifactMapping[] { bundle1 }, cancellationToken).Wait();

			ArtifactMapping[] readBack1 = cache.QueryArtifactMappingsAsync(new IoHash[] { bundle1.Key }, cancellationToken).Result;
			Assert.AreEqual(1, readBack1.Length);

			ArtifactMapping bundle2 = MakeBundle2();
			cache.SaveArtifactMappingsAsync(new ArtifactMapping[] { bundle2 }, cancellationToken).Wait();

			ArtifactMapping[] readBack2 = cache.QueryArtifactMappingsAsync(new IoHash[] { bundle1.Key }, cancellationToken).Result;
			Assert.AreEqual(2, readBack2.Length);

			cache.FlushChangesAsync(cancellationToken).Wait();
		}

#if DISABLED
		[TestMethod]
		public void ArtifactBundleStorageTest2()
		{
			CancellationToken cancellationToken = default;

			IArtifactCache cache = HordeStorageArtifactCache.CreateMemoryCache(NullLogger.Instance);

			cache.WaitForReadyAsync().Wait(cancellationToken);

			ArtifactBundle bundle1 = MakeBundle1();
			ArtifactBundle[] readBack2 = cache.QueryArtifactBundles(new IoHash[] { bundle1.Key }, cancellationToken);
			Assert.AreEqual(2, readBack2.Length);
		}
#endif
	}
}
