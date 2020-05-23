// Copyright Epic Games, Inc. All Rights Reserved.
import { P4Util } from '../framework'
import { EdgeState } from '../BranchState'
import { MultipleDevAndReleaseTestBase } from '../MultipleDevAndReleaseTestBase'


export class MultipleConflicts extends MultipleDevAndReleaseTestBase {
	async setup() {
		await this.p4.depot('stream', this.depotSpec())
		await this.createStreamsAndWorkspaces()

		await P4Util.addFileAndSubmit(this.getClient('Main'), 'test.uasset', 'Initial content', true)

		await this.initialPopulate()
	}

	async run() {
		// prepare some conflicts
		await Promise.all(['Dev-Perkin', 'Dev-Pootle']
			.map(branchName => this.getClient(branchName))
			.map(client => client.sync()
				.then(() => P4Util.editFileAndSubmit(client, 'test.uasset', 'Different content')))
		)

		const r1client = this.getClient('Release-1.0')
		await r1client.sync()
		await P4Util.editFileAndSubmit(r1client, 'test.uasset', 'Initial content\nSome more!')
	}

	async verify() {
		await Promise.all([
			this.checkHeadRevision('Main', 'test.uasset', 2),
			this.checkHeadRevision('Release-2.0', 'test.uasset', 2),
			this.checkHeadRevision('Dev-Perkin', 'test.uasset', 2),
			this.checkHeadRevision('Dev-Pootle', 'test.uasset', 2),
			this.verifyAndPerformStomp('Main', 'Dev-Pootle', 'Pootle'),
			async () => {
				const edgeState: EdgeState = await this.getEdgeState('Main', 'Dev-Perkin')

				const results = await Promise.all([
					this.wasMessagePostedToSlack(this.botName.toLowerCase(), edgeState.conflictCl),
					this.wasMessagePostedToSlack('Pootle', edgeState.conflictCl)
				])
				if (!results[0]) {
					throw new Error('Expected message!') // todo: details
				}

				if (results[1]) {
					throw new Error('Not expecting message!') // todo: details
				}
			}
		])

		// Give RM plenty of time to stomp and merge all those test.uasset revisions 
		await this.waitForRobomergeIdle()

		await Promise.all([
			this.checkHeadRevision('Dev-Perkin', 'test.uasset', 2),
			this.checkHeadRevision('Dev-Pootle', 'test.uasset', 3)
		])
	}

	getEdges() {
		return [{
			from: this.fullBranchName('Main'),
			to: this.fullBranchName('Dev-Pootle'),
			additionalSlackChannel: 'Pootle'
		}]
	}

}
