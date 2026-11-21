#!/bin/bash
SDK_DIR="${1}"

if [[ -z "${SDK_DIR}" ]]
then
	echo "must supply install dir of MWCaptureSDK"
	exit 1
fi

if [[ ! -d "${SDK_DIR}" ]]
then
	echo "${SDK_DIR} does not exist"
	exit 1
fi

SDK_VER="${SDK_DIR##* }"

echo "Copying version ${SDK_VER} from ${SDK_DIR}"

for d in Debug Release
do
	for p in x64 Win32
	do
		mkdir -p lib/${p}/${d}
	done
done
mkdir -p include

cp -a "${SDK_DIR}/SDKv3/Include/LibMWCapture" include/
for e in LockUtils MWFOURCC MWSDKCommon ProductVer StringUtils
do
	cp  "${SDK_DIR}/SDKv3/Include/${e}.h" include/
done

for p in x64 Win32
do
	cp "${SDK_DIR}/SDKv3/Lib/${p}/LibMWCapture.lib" lib/${p}/Release/
	cp "${SDK_DIR}/SDKv3/Lib/${p}/LibMWCaptured.lib" lib/${p}/Debug/LibMWCapture.lib
done

cp "${SDK_DIR}/Runtime/RedistLicense.rtf" .
cp "${SDK_DIR}/Runtime/MWCaptureRT.exe" .

echo "${SDK_VER}" > VERSION
