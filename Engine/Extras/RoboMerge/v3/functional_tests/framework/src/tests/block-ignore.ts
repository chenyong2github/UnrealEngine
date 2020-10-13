// Copyright Epic Games, Inc. All Rights Reserved.
import { FunctionalTest, P4Client, P4Util, RobomergeBranchSpec, Stream } from '../framework'

export const streams: Stream[] = [
	{name: 'Main', streamType: 'mainline'},
	{name: 'Release', streamType: 'release', parent: 'Main'}
]

const TEXT_FILENAME = 'test.txt'
export class BlockIgnore extends FunctionalTest {
	async setup() {
		await this.p4.depot('stream', this.depotSpec())
		await this.createStreamsAndWorkspaces(streams)

		this.mainClient = this.getClient('Main')

		await P4Util.addFileAndSubmit(this.mainClient, TEXT_FILENAME, 'Initial content')

		await this.p4.populate(this.getStreamPath('Release'), 'Initial branch of files from Main')
	}

	run() {
		return P4Util.editFileAndSubmit(this.mainClient, TEXT_FILENAME, 'change', 'deadend')
	}

	verify() {
		return Promise.all([this.ensureBlocked('Main'), this.ensureNotBlocked('Release')])
	}

	allowSyntaxErrors() {
		return true
	}

	getBranches() {
		const main = this.branch('Main', [])
		main.disallowDeadend = true
		return [
			main,
			this.branch('Release', ['Main'])
		]
	}

	private branch(stream: string, to: string[]): RobomergeBranchSpec {
		return {
			streamDepot: this.constructor.name,
			name: this.fullBranchName(stream),
			streamName: stream,
			flowsTo: to.map(str => this.fullBranchName(str)),
			forceAll: true
		}
	}

	private mainClient: P4Client
}
