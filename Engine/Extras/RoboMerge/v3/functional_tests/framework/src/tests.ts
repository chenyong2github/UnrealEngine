// Copyright Epic Games, Inc. All Rights Reserved.
import { DEFAULT_BOT_SETTINGS, EdgeProperties, FunctionalTest, getRootDataClient, P4Client,
				P4Util, RobomergeBranchSpec, ROBOMERGE_DOMAIN } from './framework'
import * as System from './system'
import * as bent from 'bent'
import { Perforce } from './test-perforce'
import { BlockAssets } from './tests/block-assets'
import { ConfirmBinaryStomp } from './tests/confirm-binary-stomp'
import { ConfirmTextResolve } from './tests/confirm-text-resolve'
import { ConfirmTextResolveBinaryStomp } from './tests/confirm-text-resolve-binary-stomp'
import { CrossDepotStreamIntegration } from './tests/cross-depot-stream-integration'
import { EdgeIndependence } from './tests/edge-independence'
import { EdigrateMainRevToRelease } from './tests/edigrate-main-rev-to-release'
import { ExclusiveCheckout } from './tests/exclusive-checkout'
import { ExcludeAuthors } from './tests/exclude-authors'
import { ExcludeAuthorsPerEdge } from './tests/exclude-authors-per-edge'
import { ForwardCommands } from './tests/forward-commands'
import { IncognitoEdge } from './tests/incognito-edge'
import { IncognitoTest } from './tests/incognito-test'
import { IndirectTarget } from './tests/indirect-target'
import { MergeMainRevToMultipleRelease } from './tests/merge-main-rev-to-multiple-release'
import { MergeMainRevToRelease } from './tests/merge-main-rev-to-release'
import { MultipleConflicts } from './tests/multiple-conflicts'
import { OverriddenCommand } from './tests/overridden-command'
import { RejectBranchResolveStomp } from './tests/reject-branch-resolve-stomp'
import { RejectDeleteResolveStomp } from './tests/reject-delete-resolve-stomp'
import { RejectTextConflictStomp } from './tests/reject-text-conflict-stomp'
import { RequestShelf } from './tests/request-shelf'
import { RequestShelfIndirectTarget } from './tests/request-shelf-indirect-target'
import { ResolveAfterSkip } from './tests/resolve-after-skip'
import { RespectStreamPath } from './tests/respect-stream-path'
import { SyntaxErrorOnUnknownBranch } from './tests/syntax-error-on-unknown-branch'
import { TestChain } from './tests/test-chain'
import { TestEdgeGate } from './tests/test-edge-gate'
import { TestFlags } from './tests/test-flags'
import { TestGate } from './tests/test-gate'
import { EdgeInitialCl } from './tests/edge-initial-cl'
import { TestReconsider } from './tests/test-reconsider'
import { TestEdgeReconsider } from './tests/test-edge-reconsider'
import { TestTerminal } from './tests/test-terminal'

import { CrossBotTest, CrossBotTest2 } from './tests/cross-bot'

const P4_USERS: [string, string][] = [
	['testuser1', 'RoboMerge TestUser1'],
	['testuser2', 'RoboMerge TestUser2'],
	['robomerge', 'RoboMerge'],
	['buildmachine', 'Example automated account']
]

async function addToRoboMerge(p4: Perforce, tests: FunctionalTest[]) {
	const rootClient = await getRootDataClient(p4, 'RoboMergeData_BranchMaps')

	const botNames = ['ft1', 'ft2', 'ft3', 'ft4']
	let branches: RobomergeBranchSpec[][] = [[], [], [], []]
	let edges: EdgeProperties[][] = [[], [], [], []]
	let groupIndex = 0
	for (const test of tests) {
		branches[groupIndex] = [...branches[groupIndex], ...test.getBranches()]
		edges[groupIndex] = [...edges[groupIndex], ...test.getEdges()]
		test.botName = botNames[groupIndex].toUpperCase()
		groupIndex = (groupIndex + 1) % botNames.length
	}

	const settings = DEFAULT_BOT_SETTINGS

	await Promise.all([
		P4Util.addFile(rootClient, 'ft1.branchmap.json', JSON.stringify({...settings, branches: branches[0],
																edges: edges[0], slackChannel: 'ft1', alias: 'ft1-alias'})),
		P4Util.addFile(rootClient, 'ft2.branchmap.json', JSON.stringify({...settings, branches: branches[1],
																edges: edges[1], slackChannel: 'ft2', alias: 'ft2-alias'})),
		P4Util.addFile(rootClient, 'ft3.branchmap.json', JSON.stringify({...settings, branches: branches[2],
																edges: edges[2], slackChannel: 'ft3', alias: 'ft3-alias'})),
		P4Util.addFile(rootClient, 'ft4.branchmap.json', JSON.stringify({...settings, branches: branches[3],
																edges: edges[3], slackChannel: 'ft4', alias: 'ft4-alias'}))
	])

	await rootClient.submit('Adding branchspecs')

	const post = bent(ROBOMERGE_DOMAIN, 'POST')
	await post('/api/control/start')

	for (let safety = 0; safety !== 10; ++safety) {
		await System.sleep(.5)
		const response: {[key:string]: any} = await bent('json')(ROBOMERGE_DOMAIN + '/api/branches')
		if (response.started) {
			break
		}
		// console.log('waiting!')
	}

	tests.map(test => test.storeNodesAndEdges())
}

async function verifyWrapper(test: FunctionalTest) {
	try {
		await test.verify()
	}
	catch (e) {
		// log some RoboMerge state
		test.error('Failed to verify: ' + e.toString())
		throw e
	}
}

async function go() {
	const p4 = new Perforce()
	await p4.init()

	const availableTests: FunctionalTest[] = [
		new ConfirmBinaryStomp(p4),

		new ConfirmTextResolve(p4),
		new ConfirmTextResolveBinaryStomp(p4),
		new CrossDepotStreamIntegration(p4),
		new EdgeIndependence(p4),
		new EdigrateMainRevToRelease(p4), // 5

		new ExclusiveCheckout(p4),
		new ForwardCommands(p4),
		new IncognitoTest(p4),
		new IndirectTarget(p4),
		new MergeMainRevToMultipleRelease(p4), // 10

		new MergeMainRevToRelease(p4),
		new MultipleConflicts(p4),
		new RejectBranchResolveStomp(p4),
		new RejectDeleteResolveStomp(p4),
		new RejectTextConflictStomp(p4), // 15

		new RequestShelf(p4),
		new RequestShelfIndirectTarget(p4),
		new ResolveAfterSkip(p4),
		new RespectStreamPath(p4),
		new SyntaxErrorOnUnknownBranch(p4),	// 20

		new OverriddenCommand(p4),
		new TestChain(p4),
		new TestEdgeGate(p4),
		new TestFlags(p4),
		new EdgeInitialCl(p4), // 25

		new IncognitoEdge(p4),
		new ExcludeAuthors(p4),
		new ExcludeAuthorsPerEdge(p4),
		new TestReconsider(p4),
		new TestEdgeReconsider(p4), // 30

		new BlockAssets(p4),
		new TestTerminal(p4),

		// these two must be consecutive
		new CrossBotTest(p4), 
		new CrossBotTest2(p4), 

		new TestGate(p4),

	]

	// const testToDebug = availableTests[30]
	// testToDebug.botName = 'FT1'
	// await testToDebug.run()

	// set up users and data depot
	await Promise.all(P4_USERS.map(([id, name]) =>
		p4.user(P4Util.specFormat(['User', id], ['FullName', name], ['Email', id + '@robomerge']))
	))

	// hack - depotSpec is effectively static if you provide a name
	await p4.depot('stream', availableTests[0].depotSpec('RoboMergeData'))
	await p4.stream(availableTests[0].streamSpec({
		name: 'Main',
		streamType: 'mainline',
		depotName: 'RoboMergeData'
	}))
	const rootClient = new P4Client(p4, 'root', 'root_RoboMergeData_Main',
		'/p4testworkspaces/root_RoboMergeData_Main', '//RoboMergeData/Main', 'Main')
	await rootClient.create(P4Util.specForClient(rootClient))

	///////////////////////
	// TESTS TO RUN 

	const tests = /*/[availableTests[10]]/*/availableTests/**/

	//
	///////////////////////

	console.log(`${tests[0].testName} to ${tests[tests.length - 1].testName}`)

	console.log(`Running set-up for ${tests.length} tests`)
	await Promise.all(tests.map(test => test.setup()))

	console.log('Updating RoboMerge branchmaps')
	await addToRoboMerge(p4, tests)

	// allow RM to update branch map
	await System.mediumSleep()

	console.log('Running tests')
	await Promise.all(tests.map(test => 
		test.run()
		.then(() => test.waitForRobomergeIdle())
	))

	console.log('Verifying tests')
	await Promise.all(tests.map(test => verifyWrapper(test)))
}

go()





// test ideas

// code in framework to check number of conflicts and pause state after each test
//	overridable function returning expected number of conflicts, defaulting to 0

// skip and null merge of distant branch (do chain first)
