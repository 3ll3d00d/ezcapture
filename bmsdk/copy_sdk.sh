#!/bin/bash -x
SDK_DIR="${1}"

if [[ -z "${SDK_DIR}" ]]
then
	echo "must supply install dir of Blackmagic SDK"
	exit 1
fi

if [[ ! -d "${SDK_DIR}" ]]
then
	echo "${SDK_DIR} does not exist"
	exit 1
fi

SDK_VER="${SDK_DIR##* }"

echo "Copying version ${SDK_VER} from ${SDK_DIR}"

mkdir -p include

cp "${SDK_DIR}"/Win/include/*.idl include/
cp "${SDK_DIR}"/Win/include/*.h include/
cp "${SDK_DIR}/End User License Agreement.pdf" .

echo "${SDK_VER}" > VERSION
