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

namespace HordeServerTests
{
	public class Fixture
	{
		public IJob Job1 { get; private set; } = null!;
		public IJob Job2 { get; private set; } = null!;
		public ITemplate Template { get; private set; } = null!;
		public IGraph Graph { get; private set; } = null!;
		public IStream? Stream { get; private set; }
		public TemplateRefId TemplateRefId1 { get; private set; }
		public TemplateRefId TemplateRefId2 { get; private set; }
		public TemplateRef TemplateRef1 { get; private set; } = null!;
		public TemplateRef TemplateRef2 { get; private set; } = null!;
		public Artifact Job1Artifact { get; private set; } = null!;
		public string Job1ArtifactData { get; private set; } = null!;
		public IAgent Agent1 { get; private set; } = null!;
		public string Agent1Name { get; private set; } = null!;

		private static Fixture? _fixture;

		public static async Task<Fixture> Create(bool ForceNewFixture, IGraphCollection GraphCollection, TemplateService TemplateService, JobService JobService, ArtifactService ArtifactService, StreamService StreamService, AgentService AgentService, IPerforceService PerforceService)
		{
			if (_fixture == null || ForceNewFixture)
			{
				_fixture = new Fixture();
				await _fixture.Populate(GraphCollection, TemplateService, JobService, ArtifactService, StreamService, AgentService, PerforceService);
			}
			
			(PerforceService as PerforceServiceStub)?.AddChange("//UE5/Main", 112233, "leet.coder", "Did stuff", new []{"file.cpp"});
			(PerforceService as PerforceServiceStub)?.AddChange("//UE5/Main", 1111, "swarm", "A shelved CL here", new []{"renderer.cpp"});
		
			return _fixture;
		}

		private async Task Populate(IGraphCollection GraphCollection, TemplateService TemplateService, JobService JobService, ArtifactService ArtifactService, StreamService StreamService, AgentService AgentService, IPerforceService PerforceService)
		{
			var Fg = new FixtureGraph();
			Fg.Id = ContentHash.Empty;
			Fg.Schema = 1122;
			Fg.Groups = new List<INodeGroup>();
			Fg.Aggregates = new List<IAggregate>();
			Fg.Labels = new List<ILabel>();

			Template = await TemplateService.CreateTemplateAsync("Test template", null, false, null, null,
				new List<TemplateCounter>(), new List<string>(), new List<Parameter>());
			Graph = await GraphCollection.AddAsync(Template);

			TemplateRefId1 = new TemplateRefId("template1");
			TemplateRefId2 = new TemplateRefId("template2");
			TemplateRef1 = new TemplateRef(Template, false, false, null, null, null);
			TemplateRef2 = new TemplateRef(Template, false, false, null, null, null);

			Dictionary<TemplateRefId, TemplateRef> TemplateRefs = new Dictionary<TemplateRefId, TemplateRef>
			{
				{TemplateRefId1, TemplateRef1},
				{TemplateRefId2, TemplateRef2}
			};
			
			List<StreamTab> Tabs = new List<StreamTab>();
			Tabs.Add(new JobsTab("foo", true, new List<TemplateRefId> { TemplateRefId1, TemplateRefId2 }, new List<string>(), new List<JobsTabColumn>()));

			Stream = await StreamService.TryCreateStreamAsync(
				Id: new StreamId("ue5-main"),
				Name: "//UE5/Main",
				ProjectId: new ProjectId("does-not-exist"),
				Order: null,
				Tabs: Tabs,
				AgentTypes: null,
				WorkspaceTypes: null,
				TemplateRefs: TemplateRefs,
				Properties: null,
				Acl: null
			);
			
			Job1 = await JobService.CreateJobAsync(
				JobId: new ObjectId("5f283932841e7fdbcafb6ab5"),
				StreamId: Stream!.Id,
				TemplateRefId: TemplateRefId1,
				TemplateHash: Template.Id,
				Graph: Graph,
				Name: "hello1",
				Change: 1000001,
				CodeChange: 1000002,
				PreflightChange: 1001,
				ClonedPreflightChange: null,
				StartedByUserId: null,
				StartedByUserName: "SomeUser",
				Priority: Priority.Normal,
				null,
				null,
				false,
				false,
				null,
				null,
				null,
				Template.Counters,
				Arguments: new List<string>()
			);
			Job1 = (await JobService.GetJobAsync(Job1.Id))!;

			Job2 = await JobService.CreateJobAsync(
				JobId: new ObjectId("5f69ea1b68423e921b035106"),
				StreamId: Stream!.Id,
				TemplateRefId: new TemplateRefId("template-id-1"),
				TemplateHash: ContentHash.MD5("made-up-template-hash"),
				Graph: Fg,
				Name: "hello2",
				Change: 2000001,
				CodeChange: 2000002,
				PreflightChange: null,
				ClonedPreflightChange: null,
				StartedByUserId: null,
				StartedByUserName: "SomeUser",
				Priority: Priority.Normal,
				null,
				null,
				false,
				false,
				null,
				null,
				null,
				Template.Counters,
				Arguments: new List<string>()
			);
			Job2 = (await JobService.GetJobAsync(Job2.Id))!;

			Job1ArtifactData = "For The Horde!";
			Job1Artifact = await ArtifactService.CreateArtifactAsync(Job1.Id, SubResourceId.Parse("22"), "myFile.txt",
				"text/plain", new MemoryStream(Encoding.UTF8.GetBytes(Job1ArtifactData)));

			Agent1Name = "testAgent1";
			Agent1 = await AgentService.CreateAgentAsync(Agent1Name, true, false, null, null);
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