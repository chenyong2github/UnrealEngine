// Copyright Epic Games, Inc. All Rights Reserved.
import { FunctionalTest, P4Util, Stream } from '../framework'
import { Change } from '../test-perforce'

const streams: Stream[] =
	[ {name: 'Main', streamType: 'mainline'}
	, {name: 'Dev-Perkin', streamType: 'development', parent: 'Main'}
	, {name: 'Dev-Perkin-Child', streamType: 'development', parent: 'Dev-Perkin'}
	, {name: 'Dev-Pootle', streamType: 'development', parent: 'Main'}
	, {name: 'Dev-Pootle-Child', streamType: 'development', parent: 'Dev-Pootle'}
	]

export class IncognitoTest extends FunctionalTest {
	async setup() {
		await this.p4.depot('stream', this.depotSpec())
		await this.createStreamsAndWorkspaces(streams)

		await P4Util.addFileAndSubmit(this.getClient('Main'), 'test.txt', 'Initial content')

		const desc = 'Initial populate'

		await Promise.all([
			this.p4.populate(this.getStreamPath('Dev-Perkin'), desc)
				.then(() => void this.p4.populate(this.getStreamPath('Dev-Perkin-Child'), desc)),
			this.p4.populate(this.getStreamPath('Dev-Pootle'), desc)
				.then(() => void this.p4.populate(this.getStreamPath('Dev-Pootle-Child'), desc)) 
		])
	}

	run() {
		return P4Util.editFileAndSubmit(this.getClient('Main'), 'test.txt', 'Initial content\n\nMore stuff')
	}

	verify() {
		const testNameLower = this.testName.toLowerCase()

		this.info('Ensuring Dev-Pootle-Child commit has no incriminating information')

		return Promise.all([
			this.getClient('Dev-Perkin-Child').changes(1)
				.then((changes: Change[]) => {
					const description = changes[0]!.description.toLowerCase()
					if (description.indexOf(testNameLower) < 0) {
						throw new Error('Expected test name to appear in description')
					}
					if (description.indexOf('dev-perkin') < 0) {
						throw new Error('Expected parent branch name to appear in description')
					}
				}),
			this.getClient('Dev-Pootle-Child').changes(1)
				.then((changes: Change[]) => {
					const description = changes[0]!.description.toLowerCase()
					if (description.indexOf(testNameLower) >= 0) {
						throw new Error('Expected test name not to appear in description')
					}
					if (description.indexOf('dev-pootle') >= 0) {
						throw new Error('Expected parent branch name not to appear in description')
					}
				}),
			this.checkHeadRevision('Main', 'test.txt', 2),
			this.checkHeadRevision('Dev-Perkin', 'test.txt', 2),
			this.checkHeadRevision('Dev-Pootle', 'test.txt', 2),
			this.checkHeadRevision('Dev-Perkin-Child', 'test.txt', 2),
			this.checkHeadRevision('Dev-Pootle-Child', 'test.txt', 2)
		])
	}

	private branch(stream: string, to: string[], incognito?: boolean) {
		const branch = this.makeForceAllBranchDef(stream, to)
		if (incognito) {
			branch.incognitoMode = true
		}
		branch.forceAll = true
		return branch
	}

	getBranches() {
		return [ this.branch('Main', ['Dev-Perkin', 'Dev-Pootle'])
		       , this.branch('Dev-Perkin', ['Dev-Perkin-Child'])
		       , this.branch('Dev-Pootle', ['Dev-Pootle-Child'], true)
		       , this.branch('Dev-Perkin-Child', [])
		       , this.branch('Dev-Pootle-Child', [])
		       ]
	}
}
