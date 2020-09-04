// Copyright Epic Games, Inc. All Rights Reserved.
import * as Path from 'path'
import { FunctionalTest, P4Client, P4Util, RobomergeBranchSpec } from '../framework'
import * as System from '../system'
import { Perforce } from '../test-perforce'

// const jsonlint: any = require('jsonlint')

type PerClient = {
	client: P4Client
	textFilePath: string
	binaryFilePath: string
	collectionFilePath: string
}

export class ConfirmBinaryStomp extends FunctionalTest {

	private mainUser1: PerClient
	private mainUser2: PerClient

	// Explicit Target
	private devBranch1User1: PerClient
	// Implicit Target
	private devBranch2User1: PerClient

	private perClient(user: string, stream: string) {
		const client = this.p4Client(user, stream)
		return {
			client,
			textFilePath: Path.join(client.root, 'textfile.txt'),
			binaryFilePath: Path.join(client.root, 'fake.uasset'),
			collectionFilePath: Path.join(client.root, 'fake.collection')
		}
	}

	constructor(p4: Perforce) {
		super(p4)

		this.mainUser1 = this.perClient('testuser1', 'Main')
		this.devBranch1User1 = this.perClient('testuser1', 'DevBranch1')
		this.devBranch2User1 = this.perClient('testuser1', 'DevBranch2')
		this.mainUser2 = this.perClient('testuser2', 'Main')
	}

	async setup() {
		// Set up depot

		await this.p4.depot('stream', this.depotSpec())

		// Setup streams
		await this.p4.stream(this.streamSpec('Main', 'mainline'))
		await this.p4.stream(this.streamSpec('DevBranch1', 'development', 'Main'))
		await this.p4.stream(this.streamSpec('DevBranch2', 'development', 'Main'))

		await this.mainUser1.client.create(P4Util.specForClient(this.mainUser1.client))

		// Add base files
		await System.writeFile(this.mainUser1.textFilePath, 'Simple functional test text file') 
		await this.mainUser1.client.add('textfile.txt')
		await System.writeFile(this.mainUser1.binaryFilePath, 'Simple functional test binary file') 
		await this.mainUser1.client.add('fake.uasset', true)
		await System.writeFile(this.mainUser1.collectionFilePath, 'Simple functional test collection file') 
		await this.mainUser1.client.add('fake.collection')

		await this.mainUser1.client.submit("Adding initial files 'textfile.txt', 'fake.uasset' and 'fake.collection'")

		// Populate DevBranch streams
		await this.p4.populate(this.getStreamPath('DevBranch1'), `Initial branch of files from Main (${this.getStreamPath('Main')}) to DevBranch1 (${this.getStreamPath('DevBranch1')})`)
		await this.p4.populate(this.getStreamPath('DevBranch2'), `Initial branch of files from Main (${this.getStreamPath('Main')}) to DevBranch2 (${this.getStreamPath('DevBranch2')})`)

		// Create Main workspace for testuser2 to create binary conflict
		await this.mainUser2.client.create(P4Util.specForClient(this.mainUser2.client))
		await this.mainUser2.client.sync()


		// Create DevBranch1 workspace for testuser1
		await this.devBranch1User1.client.create(P4Util.specForClient(this.devBranch1User1.client))
		await this.devBranch1User1.client.sync()

		// Create DevBranch2 workspace for testuser1
		await this.devBranch2User1.client.create(P4Util.specForClient(this.devBranch2User1.client))
		await this.devBranch2User1.client.sync()

		// Create future binary file conflict in DevBranch1
		await this.devBranch1User1.client.edit('fake.uasset')
		await System.writeFile(this.devBranch1User1.binaryFilePath, 'Create second revision in DevBranch1 for binary file')
		await this.devBranch1User1.client.edit('fake.collection')
		await System.writeFile(this.devBranch1User1.collectionFilePath, 'Create second revision in DevBranch1 for collection file')
		// This should result in a conflict on 'fake.uasset' in both streams so we can test stomp verification and execution for explicit targets!
		await P4Util.submit(this.devBranch1User1.client, 'Creating second revision in DevBranch1 for both binary file and text file for merging')

		// Create future binary file conflict in DevBranch2
		await this.devBranch2User1.client.edit('fake.uasset')
		await System.writeFile(this.devBranch2User1.binaryFilePath, 'Create second revision in DevBranch2 for binary file')
		await this.devBranch2User1.client.edit('fake.collection')
		await System.writeFile(this.devBranch2User1.collectionFilePath, 'Create second revision in DevBranch2 for collection file')

		// This should result in a conflict on 'fake.uasset' in both streams so we can test stomp verification and execution for implicit targets!
		await P4Util.submit(this.devBranch2User1.client, 'Creating second revision in DevBranch2 for both binary file and text file for merging')
	}

	async run() {
		// Create future binary file conflict
		await this.mainUser2.client.edit('fake.uasset')
		await System.writeFile(this.mainUser2.binaryFilePath, 'Create second revision in Main for binary file')
		await this.mainUser2.client.edit('fake.collection')
		await System.writeFile(this.mainUser2.collectionFilePath, 'Create second revision in Main for collection file')

		// Submit 
		await this.mainUser2.client.submit(`Creating second revision in Main for binary and collection files\n#robomerge ${this.fullBranchName("DevBranch1")}`)

		// Now Main should be blocked for both DevBranch1 and DevBranch2
	}

	getBranches(): RobomergeBranchSpec[] {
		return [{
			streamDepot: this.testName,
			name: this.testName + 'Main',
			streamName: 'Main',
			flowsTo: [this.testName + 'DevBranch1', this.testName + 'DevBranch2'],
			forceFlowTo: [this.testName + 'DevBranch2'],
			forceAll: false,
			additionalSlackChannelForBlockages: this.testName
		}, {
			streamDepot: this.testName,
			name: this.testName + 'DevBranch1',
			streamName: 'DevBranch1',
			flowsTo: [this.testName + 'Main'],
		},
		{
			streamDepot: this.testName,
			name: this.testName + 'DevBranch2',
			streamName: 'DevBranch2',
			flowsTo: [this.testName + 'Main']
		}
	]}

	async verify() {
		// Test implicit target stomp
		await this.performAndVerifyStomp('DevBranch2')

		await Promise.all([
			this.ensureBlocked('Main', 'DevBranch1'), // This better still be blocked!
			this.ensureNotBlocked('Main', 'DevBranch2')
		])

		// Test explicit target stomp
		await this.performAndVerifyStomp('DevBranch1')

		await Promise.all([
			this.ensureNotBlocked('Main', 'DevBranch1'),
			this.ensureNotBlocked('Main', 'DevBranch2') 
		])
	}


	private async performAndVerifyStomp(branch: string) {
		await Promise.all([
			this.checkHeadRevision(branch, 'fake.collection', 2),
			this.checkHeadRevision(branch, 'fake.uasset', 2)
		])

		// verify stomp
		let result = await this.verifyAndPerformStomp('Main', branch, this.testName)

		let files = result.verify.files

		if (files[0].targetFileName === `//${this.testName}/${branch}/fake.collection` &&
			files[1].targetFileName === `//${this.testName}/${branch}/fake.uasset`) {
			this.info('Expected files were stomped')
		}
		else {
			this.error(files ? JSON.stringify(files) : 'no files')
			throw new Error('Stomp Verify returned unexpected values. Erroring...')
		}

		await Promise.all([
			this.checkHeadRevision(branch, 'fake.collection', 3),
			this.checkHeadRevision(branch, 'fake.uasset', 3)
		])
	}
}
