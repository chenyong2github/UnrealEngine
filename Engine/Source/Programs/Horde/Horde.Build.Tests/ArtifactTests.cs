// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Amazon.Runtime;
using Horde.Build.Acls;
using Horde.Build.Artifacts;
using Horde.Build.Storage;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace Horde.Build.Tests
{
	[TestClass]
	public class ArtifactTests : TestSetup
	{
		[TestMethod]
		public async Task CreateArtifact()
		{
			IArtifactCollection artifactCollection = ServiceProvider.GetRequiredService<IArtifactCollection>();
			IArtifact artifact = await artifactCollection.AddAsync("test", ArtifactType.StepOutput, new string[] { "test1", "test2" }, Namespace.Artifacts, null, AclScopeName.Root);

			{
				List<IArtifact> artifacts = await artifactCollection.FindAsync(keys: new[] { "test1" }).ToListAsync();
				Assert.AreEqual(1, artifacts.Count);
				Assert.AreEqual(artifact.Id, artifacts[0].Id);
			}

			{
				List<IArtifact> artifacts = await artifactCollection.FindAsync(keys: new[] { "test2" }).ToListAsync();
				Assert.AreEqual(1, artifacts.Count);
				Assert.AreEqual(artifact.Id, artifacts[0].Id);
			}

			{
				List<IArtifact> artifacts = await artifactCollection.FindAsync(keys: new[] { "test3" }).ToListAsync();
				Assert.AreEqual(0, artifacts.Count);
			}
		}
	}
}
