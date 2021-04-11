#!/bin/sh
cd "$(dirname $0)"
arduino --board arduino:avr:nano --port /dev/ttyUSB0 --upload display-node.c --verbose
