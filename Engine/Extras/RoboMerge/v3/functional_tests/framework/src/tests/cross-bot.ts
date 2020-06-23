// Copyright Epic Games, Inc. All Rights Reserved.

import { FunctionalTest, P4Util, RobomergeBranchSpec, Stream } from '../framework'

const DEPOT_NAME = 'CrossBot'


// @todo make an auto merge target and make sure ~blah works cross bot
const streams: Stream[] = [
	{name: 'Main', streamType: 'mainline', depotName: DEPOT_NAME},
	{name: 'Release', streamType: 'release', parent: 'Main', depotName: DEPOT_NAME},
	{name: 'Dev', streamType: 'development', parent: 'Main', depotName: DEPOT_NAME}
]

let crossBot2: CrossBotTest2 | null = null
export class CrossBotTest extends FunctionalTest {
	async setup() {
		await this.p4.depot('stream', this.depotSpec(DEPOT_NAME))
		await this.createStreamsAndWorkspaces(streams, DEPOT_NAME)

		const mainClient = this.getClientForStream('Main')
		for (const n of [1, 2, 3]) {
			await P4Util.addFileAndSubmit(mainClient, `test${n}.txt`, 'Initial content')
		}

		await Promise.all([
			this.p4.populate(this.getStreamPath('Release', DEPOT_NAME), 'Initial branch of files from Main')
				.then(() => this.getClientForStream('Release').sync()),
			this.p4.populate(this.getStreamPath('Dev', DEPOT_NAME), 'Initial branch of files from Main')
		])
	}

	async run() {
		const releaseClient = this.getClientForStream('Release')

		// integrate to 'dev' (different bot)
		await P4Util.editFile(releaseClient, 'test1.txt', 'Initial content\n\nMergeable line')
		await P4Util.submit(releaseClient, `Edit\n#robomerge[${crossBot2!.botName}] dev`)

		await P4Util.editFile(releaseClient, 'test2.txt', 'Initial content\n\nMergeable line')
		await P4Util.submit(releaseClient, `Edit\n#robomerge[SomeOtherBot] dev`)

		await P4Util.editFile(releaseClient, 'test3.txt', 'Initial content\n\nMergeable line')
		await P4Util.submit(releaseClient, `Edit\n#robomerge[${crossBot2!.botName}-alias] dev`)
	}

	verify() {
		return Promise.all([
			this.ensureNotBlocked('Release'),
			...[1, 2, 3].map(n => this.checkHeadRevision('Main', `test${n}.txt`, 2, DEPOT_NAME))
		])
	}

	getBranches() {
		const releaseStream = this.makeForceAllBranchDef('Release', ['Main'])
		releaseStream.incognitoMode = true
		return [
			this.makeForceAllBranchDef('Main', []),
			releaseStream	
		]
	}

	protected makeForceAllBranchDef(stream: string, to: string[]): RobomergeBranchSpec {
		return {
			streamDepot: DEPOT_NAME,
			name: this.fullBranchName(stream),
			streamName: stream,
			flowsTo: to.map(str => this.fullBranchName(str)),
			forceAll: true
		}
	}

	private getClientForStream(stream: string) {
		return this.getClient(stream, 'testuser1', DEPOT_NAME)
	}
}

// assuming this comes after CrossBotTest in the tests list, and that that
// means they live in different bots
export class CrossBotTest2 extends FunctionalTest {
	async setup() {
		crossBot2 = this

		// add clients to the isRoboMergeIdle list
		this.p4Client('testuser1', 'Main', DEPOT_NAME)
		this.p4Client('testuser1', 'Release', DEPOT_NAME)
		this.p4Client('testuser1', 'Dev', DEPOT_NAME)
	}

	async run() {}

	verify() {
		return Promise.all([
			this.ensureNotBlocked('Main', 'Dev'),
			this.checkHeadRevision('Release', 'test1.txt', 2, DEPOT_NAME),
			this.checkHeadRevision('Release', 'test2.txt', 2, DEPOT_NAME),			
			this.checkHeadRevision('Dev', 'test1.txt', 2, DEPOT_NAME),
			this.checkHeadRevision('Dev', 'test2.txt', 1, DEPOT_NAME), // unrelated bot name
			this.checkHeadRevision('Dev', 'test3.txt', 2, DEPOT_NAME)  // alias
		])
	}

	getBranches() {
		const devStream = this.makeBranchDef('Dev', [])
		devStream.aliases = ['dev']
		return [
			this.makeBranchDef('Main', ['Dev']),
			devStream
		]
	}

	protected makeBranchDef(stream: string, to: string[]): RobomergeBranchSpec {
		return {
			streamDepot: DEPOT_NAME,
			name: this.fullBranchName(stream),
			streamName: stream,
			flowsTo: to.map(str => this.fullBranchName(str))
		}
	}
}

