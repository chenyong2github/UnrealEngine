// Copyright Epic Games, Inc. All Rights Reserved.
import { FunctionalTest, getRootDataClient, P4Client, P4Util, Stream } from '../framework'

const streams: Stream[] = [
	{name: 'Main', streamType: 'mainline'},
	{name: 'Dev-NoGate', streamType: 'development', parent: 'Main'},
	{name: 'Dev-PlusOne', streamType: 'development', parent: 'Main'},
	{name: 'Dev-Exact', streamType: 'development', parent: 'Main'}
]

const GATE_FILENAME = 'TestEdgeGate-gate'

export class TestEdgeGate extends FunctionalTest {
	gateClient: P4Client

	async setup() {
		await this.p4.depot('stream', this.depotSpec())
		await this.createStreamsAndWorkspaces(streams)
	
		this.gateClient = await getRootDataClient(this.p4, 'RoboMergeData_' + this.constructor.name)

		const mainClient = this.getClient('Main', 'testuser1')
		await P4Util.addFileAndSubmit(mainClient, 'test.txt', 'Initial content')

		const desc = 'Initial branch of files from Main'
		await Promise.all([
			this.p4.populate(this.getStreamPath('Dev-Exact'), desc),
			this.p4.populate(this.getStreamPath('Dev-PlusOne'), desc),
			this.p4.populate(this.getStreamPath('Dev-NoGate'), desc)
		])

		const firstEditCl = await P4Util.editFileAndSubmit(mainClient, 'test.txt', 'Initial content\n\nFirst addition')

		// must happen in set-up, and for this test, before second addition (i.e. earlier CL numbers)
		await Promise.all([
			P4Util.addFile(this.gateClient, GATE_FILENAME + 'exact.json', `{"Change":${firstEditCl}}`),
			P4Util.addFile(this.gateClient, GATE_FILENAME + 'plusone.json', `{"Change":${firstEditCl + 1}}`)
		])

		await this.gateClient.submit('Added gates')
		await P4Util.editFileAndSubmit(mainClient, 'test.txt', 'Initial content\n\nFirst addition\n\nSecond addition')
	}

	async run() {
	}

	verify() {
		return Promise.all([
			this.checkHeadRevision('Main', 'test.txt', 3),
			this.checkHeadRevision('Dev-Exact', 'test.txt', 2),
			this.checkHeadRevision('Dev-PlusOne', 'test.txt', 2),
			this.checkHeadRevision('Dev-NoGate', 'test.txt', 3)
		])
	}

	getBranches() {
		const mainSpec = this.makeForceAllBranchDef('Main', ['Dev-Exact', 'Dev-NoGate', 'Dev-PlusOne'])
		mainSpec.initialCL = 1
		return [
			mainSpec,
			this.makeForceAllBranchDef('Dev-Exact', []),
			this.makeForceAllBranchDef('Dev-PlusOne', []),
			this.makeForceAllBranchDef('Dev-NoGate', [])
		]
	}

	getEdges() {
		return [
		  { from: this.fullBranchName('Main'), to: this.fullBranchName('Dev-Exact')
		  , lastGoodCLPath: this.gateClient.stream + '/' + GATE_FILENAME + 'exact.json'
		  }
		, { from: this.fullBranchName('Main'), to: this.fullBranchName('Dev-PlusOne')
		  , lastGoodCLPath: this.gateClient.stream + '/' + GATE_FILENAME + 'plusone.json'
		  }
		]
	}
}
