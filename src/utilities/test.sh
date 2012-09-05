#!/bin/bash

TESTFILE="/home/multimedia/To Burn/Movies/BBC - Israel Secret Weapon.avi"

function test1() {
	"du" -b "${TESTFILE}"

	./bkrencode -Vn -Dh -Fe <"${TESTFILE}" | wc -c
}

function test2() {
	TMP_DIR=$(mktemp -d /tmp/fifodir.XXXXXX)

	[ -d ${TMP_DIR} ] || exit 1

	mkfifo ${TMP_DIR}/fifo

	"du" -b "${TESTFILE}"

	wc -c ${TMP_DIR}/fifo &

	./bkrencode -Vn -Dh -Fe <"${TESTFILE}" | ./bkrencode -u -Vn -Dh -Fe | tee ${TMP_DIR}/fifo | cmp "${TESTFILE}"

	rm -Rf ${TMP_DIR}
}

test$1
