// Copyright Epic Games, Inc. All Rights Reserved.
import { DEFAULT_BOT_SETTINGS, EdgeProperties, FunctionalTest, getRootDataClient, P4Client,
				P4Util, RobomergeBranchSpec, ROBOMERGE_DOMAIN, retryWithBackoff } from './framework'
import * as bent from 'bent'
import { Perforce } from './test-perforce'
import { BlockAssets } from './tests/block-assets'
import { BlockIgnore } from './tests/block-ignore'
import { ConfirmBinaryStomp } from './tests/confirm-binary-stomp'
import { ConfirmTextResolve } from './tests/confirm-text-resolve'
import { ConfirmTextResolveBinaryStomp } from './tests/confirm-text-resolve-binary-stomp'
import { CrossDepotStreamIntegration } from './tests/cross-depot-stream-integration'
import { EdgeIndependence } from './tests/edge-independence'
import { EdigrateMainRevToRelease } from './tests/edigrate-main-rev-to-release'
import { ExclusiveCheckout } from './tests/exclusive-checkout'
import { ExcludeAuthors } from './tests/exclude-authors'
import { ExcludeAuthorsPerEdge } from './tests/exclude-authors-per-edge'
import { ForwardCommands, ForwardCommands2 } from './tests/forward-commands'
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
import { StompWithAdd } from './tests/stomp-with-add'
import { TestChain } from './tests/test-chain'
import { TestEdgeGate } from './tests/test-edge-gate'
import { TestFlags } from './tests/test-flags'
import { TestGate } from './tests/test-gate'
import { EdgeInitialCl } from './tests/edge-initial-cl'
import { TestReconsider } from './tests/test-reconsider'
import { TestEdgeReconsider } from './tests/test-edge-reconsider'
import { TestTerminal } from './tests/test-terminal'

import { CrossBotTest, CrossBotTest2, ComplexCrossBot, ComplexCrossBot2, ComplexCrossBot3 } from './tests/cross-bot'

const P4_USERS: [string, string][] = [
	['testuser1', 'RoboMerge TestUser1'],
	['testuser2', 'RoboMerge TestUser2'],
	['robomerge', 'RoboMerge'],
	['buildmachine', 'Example automated account']
]

type BranchMapSettings = {
	branches: RobomergeBranchSpec[]
	edges: EdgeProperties[]
	macros: {[name:string]: string[]}
}

function addTest(settings: BranchMapSettings, test: FunctionalTest) {
	settings.branches = [...settings.branches, ...test.getBranches()]
	settings.edges = [...settings.edges, ...test.getEdges()]
	settings.macros = {...settings.macros, ...test.getMacros()}
}

async function addToRoboMerge(p4: Perforce, tests: FunctionalTest[]) {
	const rootClient = await getRootDataClient(p4, 'RoboMergeData_BranchMaps')

	const botNames = ['ft1', 'ft2', 'ft3', 'ft4']
	const settings: [string, BranchMapSettings][] = botNames.map(n => [n, {branches: [], edges: [], macros: {}}])
	let groupIndex = 0
	for (const test of tests) {
		test.botName = botNames[groupIndex].toUpperCase()

		groupIndex = (groupIndex + 1) % botNames.length
	}

	// cross bot tests rely on all bot names being set before tests are added
	groupIndex = 0
	for (const test of tests) {
		addTest(settings[groupIndex][1], test)
		test.storeNodesAndEdges()

		groupIndex = (groupIndex + 1) % botNames.length
	}

	await Promise.all(settings.map(([botName, s]) => 
		P4Util.addFile(rootClient, botName + '.branchmap.json', JSON.stringify({...DEFAULT_BOT_SETTINGS, ...s, slackChannel: botName, alias: botName + '-alias'}))
	))

	await rootClient.submit('Adding branchspecs')

	const post = bent(ROBOMERGE_DOMAIN, 'POST')
	await post('/api/control/start')

	await retryWithBackoff('Waiting for all branchmaps to load', async () => {

		for (const test of tests) {
			for (const node of test.nodes) {
				try {
					await FunctionalTest.getBranchState(test.botName, node)
				}
				catch (exc) {
					return false
				}
			}
		}
		return true
	})
}

async function checkForSyntaxErrors(test: FunctionalTest) {
	if (!test.allowSyntaxErrors()) {
		for (const branch of test.getBranches()) {
			const branchState = await FunctionalTest.getBranchState(test.botName, branch.name)
			if (branchState.is_blocked) {
				test.error(branchState.blockage.message)
				throw new Error('Unexpected syntax error')
			}
		}
	}
}

async function verifyWrapper(test: FunctionalTest) {
	try {
		// console.log(test.testName, await test.isRobomergeIdle())
		await test.verify()
		// console.log(test.testName, await test.isRobomergeIdle())
	}
	catch (e) {
		test.error('Failed to verify: ' + e.toString().split('\n')[0])
		return e
	}
	return null
}

async function go() {
	const p4 = new Perforce()
	await p4.init()

	const availableTests: FunctionalTest[] = [
		new BlockAssets(p4),

		new ConfirmBinaryStomp(p4),
		new ConfirmTextResolve(p4),
		new ConfirmTextResolveBinaryStomp(p4),
		new CrossDepotStreamIntegration(p4),
		new EdgeIndependence(p4), // 5

		new EdigrateMainRevToRelease(p4),
		new ExclusiveCheckout(p4),
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

		new TestGate(p4),

		// these must be consecutive (try to start on a multiple of 4)
		new ForwardCommands(p4),
		new ForwardCommands2(p4),

		new CrossBotTest(p4),
		new CrossBotTest2(p4), 	// 35

		new ComplexCrossBot(p4),
		new ComplexCrossBot2(p4),
		new ComplexCrossBot3(p4),

		new TestTerminal(p4),
		new BlockIgnore(p4), // 40

		new StompWithAdd(p4)
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

	const tests = /*/[availableTests[30]]/*/availableTests  /* .slice(34, 39)/**/

	//
	///////////////////////

	console.log(`${tests[0].testName} to ${tests[tests.length - 1].testName}`)

	console.log(`Running set-up for ${tests.length} tests`)
	await Promise.all(tests.map(test => test.setup()))

	console.log('Updating RoboMerge branchmaps')
	await addToRoboMerge(p4, tests)

	console.log('Running tests')
	await Promise.all(tests.map(test => test.run()))

	// wait for all tests after running, in case tests caused activity in other test streams (cross-bot, I'm looking at you)
	for (const test of tests) {
		await test.waitForRobomergeIdle()
	}

	console.log('Verifying tests')
	let error: Error | null = null
	await Promise.all(tests.map(async (test) => { error = await verifyWrapper(test) || error }))
	await Promise.all(tests.map(test => checkForSyntaxErrors(test)))

	if (error) {
		throw error
	}
}

go()





// test ideas

// code in framework to check number of conflicts and pause state after each test
//	overridable function returning expected number of conflicts, defaulting to 0

// skip and null merge of distant branch (do chain first)
