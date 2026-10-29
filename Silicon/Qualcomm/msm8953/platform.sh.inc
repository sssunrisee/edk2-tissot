function platform_build_bootimg(){
	python3 "${ROOTDIR}/tools/mkbootimg.py" \
		--kernel "${WORKSPACE}/uefi-${DEVICE}-kernel" \
		--ramdisk ramdisk \
		--base 0x80000000 \
		--kernel_offset 0x00008000 \
		--ramdisk_offset 0x01000000 \
		--second_offset 0x00F00000 \
		--tags_offset 0x00000100 \
		--pagesize 2048 \
		--header_version 0 \
		-o "${OUTDIR}/boot-${DEVICE}${EXT}.img" \
		||return "$?"
	if [ -f "${ROOTDIR}/Platform/${VENDOR_NAME}/${SOC_PLATFORM_L}/FdtBlob_compat/qcom_dt.img" ]; then
		DT_SIZE=$(stat -c%s "${ROOTDIR}/Platform/${VENDOR_NAME}/${SOC_PLATFORM_L}/FdtBlob_compat/qcom_dt.img")
		python3 -c "import struct; f = open('${OUTDIR}/boot-${DEVICE}${EXT}.img', 'r+b'); f.seek(40); f.write(struct.pack('<I', ${DT_SIZE})); f.close()"
		cat "${ROOTDIR}/Platform/${VENDOR_NAME}/${SOC_PLATFORM_L}/FdtBlob_compat/qcom_dt.img" >> "${OUTDIR}/boot-${DEVICE}${EXT}.img"
	fi
}
