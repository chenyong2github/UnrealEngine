#!/bin/bash
set -e

echo Creating robousers...
p4 user -fi < testuser1.spec
p4 user -fi < testuser2.spec
p4 user -fi < robomerge.spec