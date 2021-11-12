// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;
using HordeCommon;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Services;
using HordeServerTests.Stubs.Services;
using MongoDB.Bson;
using StreamId = HordeServer.Utilities.StringId<HordeServer.Models.IStream>;
using TemplateRefId = HordeServer.Utilities.StringId<HordeServer.Models.TemplateRef>;
using ProjectId = HordeServer.Utilities.StringId<HordeServer.Models.IProject>;
using HordeServer.Api;
using HordeServer.Jobs;
using HordeServer.Utilities;

namespace HordeServerTests
{
	using JobId = ObjectId<IJob>;

	public class Fixture
	{
		public IJob Job1 { get; private set; } = null!;
		public IJob Job2 { get; private set; } = null!;
		public ITemplate Template { get; private set; } = null!;
		public IGraph Graph { get; private set; } = null!;
		public IStream? Stream { get; private set; }
		public TemplateRefId TemplateRefId1 { get; private set; }
		public TemplateRefId TemplateRefId2 { get; private set; }
		public IArtifact Job1Artifact { get; private set; } = null!;
		public string Job1ArtifactData { get; private set; } = null!;
		public IAgent Agent1 { get; private set; } = null!;
		public string Agent1Name { get; private set; } = null!;

		public static async Task<Fixture> Create(IGraphCollection GraphCollection, ITemplateCollection TemplateCollection, JobService JobService, IArtifactCollection ArtifactCollection, StreamService StreamService, AgentService AgentService, IPerforceService PerforceService)
		{
			Fixture _fixture = new Fixture();
			await _fixture.Populate(GraphCollection, TemplateCollection, JobService, ArtifactCollection, StreamService, AgentService, PerforceService);

//			(PerforceService as PerforceServiceStub)?.AddChange("//UE5/Main", 112233, "leet.coder", "Did stuff", new []{"file.cpp"});
//			(PerforceService as PerforceServiceStub)?.AddChange("//UE5/Main", 1111, "swarm", "A shelved CL here", new []{"renderer.cpp"});
		
			return _fixture;
		}

		private async Task Populate(IGraphCollection GraphCollection, ITemplateCollection TemplateCollection, JobService JobService, IArtifactCollection ArtifactCollection, StreamService StreamService, AgentService AgentService, IPerforceService PerforceService)
		{
			var Fg = new FixtureGraph();
			Fg.Id = ContentHash.Empty;
			Fg.Schema = 1122;
			Fg.Groups = new List<INodeGroup>();
			Fg.Aggregates = new List<IAggregate>();
			Fg.Labels = new List<ILabel>();

			Template = await TemplateCollection.AddAsync("Test template", null, false, null, null,
				new List<string>(), new List<Parameter>());
			Graph = await GraphCollection.AddAsync(Template);

			TemplateRefId1 = new TemplateRefId("template1");
			TemplateRefId2 = new TemplateRefId("template2");

			List<CreateTemplateRefRequest> Templates = new List<CreateTemplateRefRequest>();
			Templates.Add(new CreateTemplateRefRequest { Id = TemplateRefId1.ToString(), Name = "Test Template" });
			Templates.Add(new CreateTemplateRefRequest { Id = TemplateRefId2.ToString(), Name = "Test Template" });

			List<CreateStreamTabRequest> Tabs = new List<CreateStreamTabRequest>();
			Tabs.Add(new CreateJobsTabRequest { Title = "foo", Templates = new List<string> { TemplateRefId1.ToString(), TemplateRefId2.ToString() } });

			Stream = await StreamService.StreamCollection.GetAsync(new StreamId("ue5-main"));
			Stream = await StreamService.StreamCollection.TryCreateOrReplaceAsync(
				new StreamId("ue5-main"),
				Stream,
				"",
				"",
				new ProjectId("does-not-exist"),
				new StreamConfig { Name = "//UE5/Main", Tabs = Tabs, Templates = Templates }
			);
			
			Job1 = await JobService.CreateJobAsync(
				JobId: new JobId("5f283932841e7fdbcafb6ab5"),
				Stream: Stream!,
				TemplateRefId: TemplateRefId1,
				TemplateHash: Template.Id,
				Graph: Graph,
				Name: "hello1",
				Change: 1000001,
				CodeChange: 1000002,
				PreflightChange: 1001,
				ClonedPreflightChange: null,
				StartedByUserId: null,
				Priority: Priority.Normal,
				null,
				null,
				null,
				false,
				false,
				null,
				null,
				Arguments: new List<string>()
			);
			Job1 = (await JobService.GetJobAsync(Job1.Id))!;

			Job2 = await JobService.CreateJobAsync(
				JobId: new JobId("5f69ea1b68423e921b035106"),
				Stream: Stream!,
				TemplateRefId: new TemplateRefId("template-id-1"),
				TemplateHash: ContentHash.MD5("made-up-template-hash"),
				Graph: Fg,
				Name: "hello2",
				Change: 2000001,
				CodeChange: 2000002,
				PreflightChange: null,
				ClonedPreflightChange: null,
				StartedByUserId: null,
				Priority: Priority.Normal,
				null,
				null,
				null,
				false,
				false,
				null,
				null,
				Arguments: new List<string>()
			);
			Job2 = (await JobService.GetJobAsync(Job2.Id))!;

			Job1ArtifactData = "For The Horde!";
			Job1Artifact = await ArtifactCollection.CreateArtifactAsync(Job1.Id, SubResourceId.Parse("22"), "myFile.txt",
				"text/plain", new MemoryStream(Encoding.UTF8.GetBytes(Job1ArtifactData)));

			Agent1Name = "testAgent1";
			Agent1 = await AgentService.CreateAgentAsync(Agent1Name, true, null, null);
		}

		private class FixtureGraph : IGraph
		{
			public ContentHash Id { get; set; } = ContentHash.Empty;
			public int Schema { get; set; }
			public IReadOnlyList<INodeGroup> Groups { get; set; } = null!;
			public IReadOnlyList<IAggregate> Aggregates { get; set; } = null!;
			public IReadOnlyList<ILabel> Labels { get; set; } = null!;
		}
	}
}