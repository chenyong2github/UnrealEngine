// Copyright Epic Games, Inc. All Rights Reserved.

using System.Net;
using System.Threading.Tasks;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace HordeServerTests
{
    [TestClass]
    public class JobsControllerTest : ControllerIntegrationTest
    {
        private static string GetUri(string JobId, string StepId, string FileName)
        {
            return $"/api/v1/jobs/{JobId}/steps/{StepId}/artifacts/{FileName}/data";
        }

        [TestMethod]
        public async Task GetArtifactDataByFilenameTest()
        {
            var Fixture = await GetFixture();
            var Art = Fixture.Job1Artifact;

            // Test existing filename
            var Res = await client.GetAsync(GetUri(Art.JobId.ToString(), Art.StepId.ToString()!, Art.Name));
            Res.EnsureSuccessStatusCode();
            Assert.AreEqual(Fixture.Job1ArtifactData, await Res.Content.ReadAsStringAsync());

            // Test non-existing filename
            Res = await client.GetAsync(GetUri(Art.JobId.ToString(), Art.StepId.ToString()!, "bogus.txt"));
            Assert.AreEqual(Res.StatusCode, HttpStatusCode.NotFound);
        }
    }
}