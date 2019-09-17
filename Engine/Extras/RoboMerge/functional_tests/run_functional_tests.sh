#!/bin/bash
set -e

check_for_error() {
    ERR=$?
    echo
    if [ "$ERR" -ne "0" ]; then
        echo Error \($ERR\) encountered running test, please check logs.
        exit $?
    fi
}

run_test()
{
    PREV_DIRECTORY=`pwd`
    TEST_DIRECTORY=$1

    # Change to test directory
    cd $TEST_DIRECTORY

    # Set up P4 depots, streams, seed data, etc.
    echo Setting up $TEST_DIRECTORY seed data...
    ./setup.sh
    check_for_error

    # Update Robomerge branchmappings to enable tests
    echo Adding $TEST_DIRECTORY branchmapping to robomerge...
    ./add_test_to_robomerge.sh
    check_for_error

    # Allow Robomerge Time to catch up
    # This shouldn't really be a sleep.
    echo Waiting...
    sleep 5

    # Change P4 data for test
    echo Executing $TEST_DIRECTORY test...
    ./run_test.sh
    check_for_error

    # Allow Robomerge Time to catch up
    # This shouldn't really be a sleep.
    echo Waiting for Robomerge to do work...
    sleep 10

    # Verify Results after Robomerge runs
    echo Verifying $TEST_DIRECTORY results...
    ./verify.sh
    check_for_error

    # Go back to originating directory
    cd $PREV_DIRECTORY
}

echo
echo

# MergeMainRevToRelease
run_test MergeMainRevToRelease





echo Functional testing complete.
