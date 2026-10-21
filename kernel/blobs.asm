; Sartax OS - bootloader dolaczony do jadra (instalator zapisuje go na dysk)
section .rodata
global blob_mbr, blob_mbr_end, blob_stage2, blob_stage2_end
align 16
blob_mbr:
    incbin "build/mbr.bin"
blob_mbr_end:
align 16
blob_stage2:
    incbin "build/stage2.bin"
blob_stage2_end:
